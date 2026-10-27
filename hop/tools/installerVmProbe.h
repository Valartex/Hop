#pragma once
#include "vlessPolicyVmProbe.h"
#include <tlhelp32.h>
#include <QLocalServer>
#include <dbghelp.h>
#include <psapi.h>
#include "processResourceVmProbe.h"
#pragma comment(lib, "dbghelp.lib")
#include "installerUpgradeVmProbe.h"

inline void reportGuiException(const DEBUG_EVENT &event, const PROCESS_INFORMATION &child, QTextStream &output)
{
    struct Module { quint64 base, size; QString name; }; QList<Module> modules;
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, child.dwProcessId);
    if (snapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W entry {}; entry.dwSize = sizeof(entry);
        if (Module32FirstW(snapshot,&entry)) { do { modules.append({ quint64(entry.modBaseAddr), entry.modBaseSize, QString::fromWCharArray(entry.szModule) }); } while (Module32NextW(snapshot,&entry)); }
        CloseHandle(snapshot);
    }
    const auto location = [&](quint64 address) {
        for (const auto &module : modules) { if (address >= module.base && address - module.base < module.size) {
            return module.name + QLatin1Char('+') + QString::number(address - module.base,16);
        } }
        return QStringLiteral("0x") + QString::number(address,16);
    };
    output << "GUI exception: " << Qt::hex << event.u.Exception.ExceptionRecord.ExceptionCode << Qt::dec
        << " at " << location(quint64(event.u.Exception.ExceptionRecord.ExceptionAddress)) << Qt::endl;
    const auto thread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,FALSE,event.dwThreadId); if (!thread) { return; }
    const auto close = qScopeGuard([&]() { CloseHandle(thread); });
    CONTEXT context {}; context.ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext(thread,&context) || !SymInitialize(child.hProcess,nullptr,TRUE)) { return; }
    const auto cleanup = qScopeGuard([&]() { SymCleanup(child.hProcess); });
    STACKFRAME64 frame {}; frame.AddrPC.Offset=context.Rip; frame.AddrStack.Offset=context.Rsp; frame.AddrFrame.Offset=context.Rbp;
    frame.AddrPC.Mode=frame.AddrStack.Mode=frame.AddrFrame.Mode=AddrModeFlat;
    for (int index=0; index<20; ++index) {
        output << "GUI frame " << index << ": " << location(frame.AddrPC.Offset) << Qt::endl;
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64,child.hProcess,thread,&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr) || !frame.AddrPC.Offset) { break; }
    }
}

inline bool launchInstalledGuiAsOrdinaryUser(const QString &path, QTextStream &output, const QString &importData = {}, const QString &idleMode = {}, int lanPort = 0, int selectionMode = -1)
{
    std::unique_ptr<InstalledHopResourceSample> idleResources;
    QJsonObject idleResult;
    bool idleCompleted = false;
    const bool vpnTest = !importData.isEmpty();
    const bool networkTest = importData.contains(QStringLiteral("# HopInstalledNetworkTest"));
    MIB_IFROW physical {}; NET_LUID physicalLuid {}; bool interfaceDown = false, networkRestored = false, downObserved = false;
    QElapsedTimer interruption;
    if (networkTest) {
        const auto uplink = NetworkUtilities::getGatewayAndIfaceTo(QHostAddress("1.0.0.1"));
        physical.dwIndex = uplink.second.index();
        if (!uplink.second.isValid() || uplink.second.humanReadableName().startsWith("Hop")
            || GetIfEntry(&physical) != NO_ERROR || physical.dwAdminStatus != MIB_IF_ADMIN_STATUS_UP
            || ConvertInterfaceIndexToLuid(physical.dwIndex,&physicalLuid) != NO_ERROR) { return false; }
    }
    const auto restoreInterface = [&]() {
        if (!interfaceDown) { return networkRestored; }
        NET_LUID current {};
        networkRestored = ConvertInterfaceIndexToLuid(physical.dwIndex,&current) == NO_ERROR && current.Value == physicalLuid.Value
            && SetIfEntry(&physical) == NO_ERROR;
        auto observed = physical;
        networkRestored = networkRestored && GetIfEntry(&observed) == NO_ERROR && observed.dwAdminStatus == MIB_IF_ADMIN_STATUS_UP;
        if (networkRestored) { interfaceDown = false; }
        return networkRestored;
    };
    const auto restorePhysical = qScopeGuard([&]() { restoreInterface(); });
    QLocalServer source;
    const auto pipeName = QStringLiteral("hop-gui-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    source.setSocketOptions(QLocalServer::UserAccessOption);
    if (vpnTest && !source.listen(pipeName)) { return false; }
    DWORD session = 0; if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) { return false; }
    HANDLE token = nullptr;
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) { return false; }
    const auto closeSnapshot = qScopeGuard([&]() { CloseHandle(snapshot); });
    PROCESSENTRY32W entry {}; entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) { return false; }
    do {
        DWORD candidateSession = 0;
        if (QString::fromWCharArray(entry.szExeFile).compare("explorer.exe", Qt::CaseInsensitive) != 0
            || !ProcessIdToSessionId(entry.th32ProcessID, &candidateSession) || candidateSession != session) { continue; }
        const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!process) { continue; }
        HANDLE source = nullptr;
        const bool opened = OpenProcessToken(process, TOKEN_DUPLICATE | TOKEN_QUERY, &source);
        CloseHandle(process);
        if (!opened) { continue; }
        TOKEN_ELEVATION elevation {}; DWORD size = 0;
        if (GetTokenInformation(source, TokenElevation, &elevation, sizeof(elevation), &size) && !elevation.TokenIsElevated) {
            DuplicateTokenEx(source, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, TokenPrimary, &token);
        }
        CloseHandle(source);
        if (token) { break; }
    } while (Process32NextW(snapshot, &entry));
    if (!token) { output << "No ordinary desktop token for GUI validation" << Qt::endl; return false; }
    const auto closeToken = qScopeGuard([&]() { CloseHandle(token); });
    const auto arguments = vpnTest ? (importData == QStringLiteral("HopPersistentApiVpnTest") || importData == QStringLiteral("HopPersistentApiFreshVpnTest")
        ? QStringLiteral(" --hop-ui-api-vpn-test ") : QStringLiteral(" --hop-ui-vpn-test ")) + pipeName : QStringLiteral(" --hop-ui-smoke-test");
    auto command = QStringLiteral("\"") + QDir::toNativeSeparators(path) + QLatin1Char('"') + arguments;
    const auto nativePath = QDir::toNativeSeparators(path);
    const auto nativeDirectory = QDir::toNativeSeparators(QFileInfo(path).absolutePath());
    STARTUPINFOW startup {}; startup.cb = sizeof(startup); startup.lpDesktop = const_cast<LPWSTR>(L"winsta0\\default");
    PROCESS_INFORMATION child {};
    if (!CreateProcessWithTokenW(token, 0, reinterpret_cast<LPCWSTR>(nativePath.utf16()),
        reinterpret_cast<LPWSTR>(command.data()), 0, nullptr, reinterpret_cast<LPCWSTR>(nativeDirectory.utf16()), &startup, &child)) {
        output << "Token GUI launch error: " << GetLastError() << Qt::endl;
        command = QStringLiteral("\"") + nativePath + QLatin1Char('"') + arguments;
        if (!CreateProcessAsUserW(token, reinterpret_cast<LPCWSTR>(nativePath.utf16()), reinterpret_cast<LPWSTR>(command.data()),
            nullptr, nullptr, FALSE, 0, nullptr, reinterpret_cast<LPCWSTR>(nativeDirectory.utf16()), &startup, &child)) {
            output << "Ordinary GUI launch error: " << GetLastError() << Qt::endl; return false;
        }
    }
    const auto closeChild = qScopeGuard([&]() { CloseHandle(child.hProcess); CloseHandle(child.hThread); });
    if (!DebugActiveProcess(child.dwProcessId)) {
        output << "Own GUI debug attach failed: " << GetLastError() << Qt::endl;
        TerminateProcess(child.hProcess,97); WaitForSingleObject(child.hProcess,3000); return false;
    }
    QObject::connect(&source, &QLocalServer::newConnection, &source, [&]() {
        while (source.hasPendingConnections()) {
            auto *socket = source.nextPendingConnection(); if (!socket) { continue; }
            ULONG pid = 0;
            // Секрет выдаётся только созданному дочернему GUI, без файла и argv.
            if (GetNamedPipeClientProcessId(reinterpret_cast<HANDLE>(socket->socketDescriptor()), &pid) && pid == child.dwProcessId) {
                socket->write(QJsonDocument(QJsonObject { { "importData", importData }, { "idleMode", idleMode }, { "lanPort", lanPort }, { "selectionMode", selectionMode } }).toJson(QJsonDocument::Compact) + '\n');
                socket->flush(); source.close();
                if (!idleMode.isEmpty()) {
                    QObject::connect(socket, &QLocalSocket::readyRead, &source, [&,socket]() {
                        while (socket->canReadLine()) {
                            const auto message = QJsonDocument::fromJson(socket->readLine(1024)).object();
                            if (message.value("phase") == "idle-begin" && !idleResources && !idleCompleted) {
                                idleResources = std::make_unique<InstalledHopResourceSample>(QFileInfo(path).absolutePath());
                                idleResources->sample(); socket->write("{\"phase\":\"idle-ready\"}\n"); socket->flush();
                            } else if (message.value("phase") == "idle-end" && idleResources) {
                                idleResources->sample(); idleResult = idleResources->result(); idleResources.reset();
                                idleCompleted = idleResult.value("complete").toBool() && idleResult.value("elapsedMs").toInteger() >= 29000;
                                socket->write(QJsonDocument(QJsonObject { { "phase", "idle-finished" }, { "accepted", idleCompleted } }).toJson(QJsonDocument::Compact) + '\n'); socket->flush();
                            }
                        }
                    });
                }
                if (networkTest) {
                    QObject::connect(socket,&QLocalSocket::readyRead,&source,[&,socket]() {
                        if (socket->bytesAvailable() > 1024) { socket->abort(); return; }
                        if (!socket->canReadLine()) { return; }
                        const auto request = QJsonDocument::fromJson(socket->readLine(1024)).object();
                        if (request.value("phase") != QStringLiteral("connected") || interfaceDown || networkRestored) { return; }
                        auto down = physical; down.dwAdminStatus = MIB_IF_ADMIN_STATUS_DOWN;
                        interfaceDown = SetIfEntry(&down) == NO_ERROR;
                        if (!interfaceDown) { socket->write("{\"restored\":false}\n"); socket->flush(); return; }
                        interruption.start(); auto observed = physical;
                        downObserved = GetIfEntry(&observed) == NO_ERROR && observed.dwAdminStatus == MIB_IF_ADMIN_STATUS_DOWN;
                        QTimer::singleShot(4000,&source,[&,socket]() {
                            const bool restored = restoreInterface();
                            socket->write(QJsonDocument(QJsonObject { { "restored", restored }, { "downObserved", downObserved },
                                { "interruptionMs", interruption.elapsed() } }).toJson(QJsonDocument::Compact) + '\n'); socket->flush();
                        });
                    });
                } else if (idleMode.isEmpty()) { socket->disconnectFromServer(); }
            } else { socket->close(); }
            if (!networkTest && idleMode.isEmpty()) { socket->deleteLater(); }
        }
    });
    QElapsedTimer deadline; deadline.start();
    InstalledHopResourceSample allResources(QFileInfo(path).absolutePath());
    HANDLE measuredService = nullptr; quint64 serviceTicks = 0, peakWorking = 0, peakPrivate = 0; qint64 lastSample = -1000;
    const auto processTicks = [](HANDLE process) -> quint64 {
        FILETIME created {}, ended {}, kernel {}, user {};
        if (!GetProcessTimes(process,&created,&ended,&kernel,&user)) { return 0; }
        return (quint64(kernel.dwHighDateTime) << 32) + kernel.dwLowDateTime + (quint64(user.dwHighDateTime) << 32) + user.dwLowDateTime;
    };
    if (vpnTest) {
        const auto manager = OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);
        const auto service = manager ? OpenServiceW(manager,L"Hop-service",SERVICE_QUERY_STATUS) : nullptr;
        SERVICE_STATUS_PROCESS status {}; DWORD size = 0;
        if (service && QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<LPBYTE>(&status),sizeof(status),&size)) {
            measuredService = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,FALSE,status.dwProcessId);
            wchar_t image[32768]; DWORD length = 32768;
            const auto expected = QDir::cleanPath(QFileInfo(path).absolutePath() + "/Hop-service.exe");
            if (measuredService && (!QueryFullProcessImageNameW(measuredService,0,image,&length)
                || QDir::cleanPath(QDir::fromNativeSeparators(QString::fromWCharArray(image,length))).compare(expected,Qt::CaseInsensitive) != 0)) {
                CloseHandle(measuredService); measuredService = nullptr;
            }
        }
        if (service) { CloseServiceHandle(service); } if (manager) { CloseServiceHandle(manager); }
        if (measuredService) { serviceTicks = processTicks(measuredService); }
    }
    const auto closeMeasured = qScopeGuard([&]() { if (measuredService) { CloseHandle(measuredService); } });
    while (WaitForSingleObject(child.hProcess, 20) == WAIT_TIMEOUT && deadline.elapsed() < (vpnTest ? 110000 : 20000)) {
        if (measuredService && deadline.elapsed() - lastSample >= 1000) {
            PROCESS_MEMORY_COUNTERS_EX memory {}; memory.cb = sizeof(memory);
            if (GetProcessMemoryInfo(measuredService,reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),sizeof(memory))) {
                peakWorking = std::max(peakWorking,quint64(memory.WorkingSetSize)); peakPrivate = std::max(peakPrivate,quint64(memory.PrivateUsage));
            }
            lastSample = deadline.elapsed();
            allResources.sample();
            if (idleResources) { idleResources->sample(); }
        }
        DEBUG_EVENT event {};
        if (WaitForDebugEvent(&event, 5)) {
            DWORD action = DBG_CONTINUE;
            if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
                const auto code = event.u.Exception.ExceptionRecord.ExceptionCode;
                if (!event.u.Exception.dwFirstChance) { reportGuiException(event,child,output); }
                if (code != EXCEPTION_BREAKPOINT && code != EXCEPTION_SINGLE_STEP) { action = DBG_EXCEPTION_NOT_HANDLED; }
            } else if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
                if (event.u.CreateProcessInfo.hFile) { CloseHandle(event.u.CreateProcessInfo.hFile); }
                // Debug-дескрипторы процесса и потоков закрывает ОС после соответствующего EXIT.
                // Ранний CloseHandle приводил к повторному закрытию уже переиспользованного номера.
            } else if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT && event.u.LoadDll.hFile) { CloseHandle(event.u.LoadDll.hFile); }
            ContinueDebugEvent(event.dwProcessId,event.dwThreadId,action);
        }
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
    }
    if (WaitForSingleObject(child.hProcess, 0) != WAIT_OBJECT_0) { TerminateProcess(child.hProcess, 97); WaitForSingleObject(child.hProcess, 3000); return false; }
    DWORD code = 99; GetExitCodeProcess(child.hProcess, &code);
    const auto prefix = QDir::tempPath() + (vpnTest ? QStringLiteral("/hop-ui-vpn-%1") : QStringLiteral("/hop-ui-smoke-%1")).arg(child.dwProcessId);
    QFile result(prefix + ".json");
    const auto data = result.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(result.readAll()).object() : QJsonObject();
    if (vpnTest) {
        if (!idleMode.isEmpty()) { output << "Installed client idle " << idleMode << ": " << QJsonDocument(idleResult).toJson(QJsonDocument::Compact) << Qt::endl; }
        allResources.sample();
        output << "All installed Hop processes during complete GUI scenario: " << QJsonDocument(allResources.result()).toJson(QJsonDocument::Compact) << Qt::endl;
        if (networkTest) { output << "Own guest uplink DOWN observed=" << downObserved << "; restored UP=" << networkRestored << Qt::endl; }
        const auto endTicks = measuredService ? processTicks(measuredService) : 0;
        if (measuredService && peakWorking && serviceTicks && endTicks >= serviceTicks) {
            output << "Main service during complete GUI scenario: peak working/private MiB " << double(peakWorking)/1048576 << "/" << double(peakPrivate)/1048576
                << "; average CPU one core % " << double(endTicks-serviceTicks)/100/std::max(qint64(1),deadline.elapsed()) << Qt::endl;
        }
        output << "Installed GUI VPN: exit " << code << "; result " << QJsonDocument(data).toJson(QJsonDocument::Compact) << Qt::endl;
        QFile::copy(prefix + ".png", QStringLiteral("//VBOXSVR/HopTestResults/installed-gui-vpn-%1.png").arg(child.dwProcessId));
        QFile::copy(prefix + ".json", QStringLiteral("//VBOXSVR/HopTestResults/installed-gui-vpn-%1.json").arg(child.dwProcessId));
        if (importData == QStringLiteral("HopPersistentApiVpnTest") || importData == QStringLiteral("HopPersistentApiFreshVpnTest")) {
            return code == 0 && data.value("persistentApiTest").toBool() && data.value("apiConfigurationSaved").toBool()
                && data.value("connectButton").toBool() && (data.value("apiHttps").toBool() || data.value("apiDomainHttps").toBool())
                && data.value("disconnectButton").toBool() && data.value("protectionUiCleared").toBool()
                && data.value("ordinaryUser").toBool() && data.value("noConsole").toBool()
                && data.value("qmlWarnings").toInt(-1) == 0;
        }
        if (importData == QStringLiteral("HopReadOnlyCatalogTest")) {
            return code == 0 && data.value("catalogReadOnly").toBool() && data.value("apiCatalogReady").toBool()
                && data.value("ordinaryUser").toBool() && data.value("noConsole").toBool()
                && data.value("qmlWarnings").toInt(-1) == 0;
        }
        if (importData == QStringLiteral("HopReadOnlyApiHealthTest")) {
            return code == 0 && data.value("apiCachedProbeAvailable").toBool()
                && data.value("apiConfigurationUnchanged").toBool() && data.value("guardAfterProbe").toInt(-1) == 0
                && data.value("apiStandaloneHealth").toObject().value("state") == "reachable"
                && data.value("ordinaryUser").toBool() && data.value("qmlWarnings").toInt(-1) == 0;
        }
        if (lanPort) {
            return code == 0 && data.value("isolatedLanScenario").toBool()
                && data.value("lanBeforeConnect").toBool() && data.value("lanDirect").toBool()
                && data.value("lanBlockedAfterEdit").toBool() && data.value("lanDirectSystemRoutePreserved").toBool()
                && data.value("lanBlockRouteCaptured").toBool()
                && data.value("lanOriginalRouteRestored").toBool() && data.value("lanAfterDisconnect").toBool()
                && data.value("ordinaryUser").toBool() && data.value("noConsole").toBool()
                && data.value("notificationTitle").toString() == QStringLiteral("Hop")
                && data.value("connectButton").toBool() && data.value("reconnectedAfterEdit").toBool()
                && data.value("disconnectButton").toBool() && data.value("protectionUiCleared").toBool()
                && data.value("disconnected").toBool() && data.value("qmlWarnings").toInt(-1) == 0;
        }
        if (idleMode == QStringLiteral("monitoring")) {
            return code == 0 && data.value("refreshBeforeFailoverHeldConnection").toBool()
                && data.value("refreshBeforeFailoverRecovered").toBool() && data.value("refreshDetachedGuardHeld").toBool()
                && data.value("refreshFinishedSuccessfully").toBool() && data.value("refreshRetryStarted").toBool()
                && data.value("vpnBeforeEdit").toBool() && data.value("directBeforeEdit").toBool()
                && data.value("disconnectButton").toBool() && data.value("protectionUiCleared").toBool()
                && data.value("disconnected").toBool() && data.value("qmlWarnings").toInt(-1) == 0;
        }
        return code == 0 && data.value("noConsole").toBool() && data.value("directDashboardStartup").toBool()
            && data.value("desktopNavigation").toBool() && (idleMode.isEmpty() || idleCompleted) && data.value("ordinaryUser").toBool() && data.value("subscriptionImported").toBool()
            && (!(importData.contains(QStringLiteral("# HopInstalledAwgGuiTest")) && !networkTest && idleMode.isEmpty())
                || (data.value("ruleOrderNoReconnect").toBool() && data.value("ruleOrderAppliedOnce").toBool() && data.value("ruleOrderRoutesVerified").toBool()
                    && data.value("locationHistoryMeasured").toBool() && data.value("locationStatisticsOpened").toBool()))
            && (!networkTest || (downObserved && networkRestored && data.value("networkRecovered").toBool()))
            && data.value("connectButton").toBool() && data.value("reconnectedAfterEdit").toBool()
            && data.value("domainBlockedAfterEdit").toBool() && data.value("directAfterEdit").toBool()
            && data.value("disconnectButton").toBool() && data.value("disconnected").toBool() && data.value("qmlWarnings").toInt(-1) == 0;
    }
    output << "Installed ordinary GUI: exit " << code << "; IPC " << data.value("serviceReady").toBool()
        << "; routes " << data.value("routesVisible").toBool() << "; QML warnings " << data.value("qmlWarnings").toInt(-1) << Qt::endl;
    output << "GUI idle CPU (one core %): " << data.value("idleCpuPercentOneCore").toDouble(-1)
        << "; RAM working/private MiB: " << data.value("workingSetMiB").toDouble(-1) << "/" << data.value("privateMiB").toDouble(-1) << Qt::endl;
    const auto image = prefix + ".png";
    if (QFileInfo::exists(image)) {
        QFile::copy(image, QStringLiteral("//VBOXSVR/HopTestResults/installed-routing-ui-%1.png").arg(child.dwProcessId));
    }
    return code == 0 && data.value("ordinaryUser").toBool() && data.value("routesVisible").toBool()
        && data.value("serviceReady").toBool() && data.value("qmlWarnings").toInt(-1) == 0;
}

inline int probeInstallerInVm(HANDLE mainService, const QUrl &url, QTextStream &output, const QString &phase = QStringLiteral("all"), const QString &importData = {})
{
    if (!mainService) { return 7; }
    const auto package = QCoreApplication::applicationDirPath() + "/Hop-Setup.msi";
    const auto destination = QStringLiteral("C:/Program Files/Hop VM Install");
    if (phase == QStringLiteral("inspect")) {
        for (const auto &kind : { QStringLiteral("install"), QStringLiteral("uninstall") }) {
            const auto log = QDir::tempPath() + QStringLiteral("/Hop-MSI-%1.log").arg(kind);
            const auto target = QStringLiteral("//VBOXSVR/HopTestResults/HopJobs/Hop-MSI-%1-%2.log").arg(kind).arg(QCoreApplication::applicationPid());
            output << "MSI guest log copied: " << kind << ": " << QFile::copy(log, target) << Qt::endl;
        }
        const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!manager) { return 7; }
        const auto service = OpenServiceW(manager, L"Hop-service", SERVICE_QUERY_STATUS);
        if (service) { SERVICE_STATUS status {}; QueryServiceStatus(service, &status);
            output << "MSI remaining service state: " << status.dwCurrentState << Qt::endl; CloseServiceHandle(service); }
        else { output << "MSI remaining service query error: " << GetLastError() << Qt::endl; }
        CloseServiceHandle(manager);
        output << "MSI client/service files present: " << QFileInfo::exists(destination + "/Hop.exe") << "/" << QFileInfo::exists(destination + "/Hop-service.exe") << Qt::endl;
        return 0;
    }

    if (!QFileInfo::exists(package)) { return 7; }
    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) { return 7; }
    const auto closeManager = qScopeGuard([&]() { CloseServiceHandle(manager); });
    const auto serviceMatches = [&](bool mustRun) {
        const auto service = OpenServiceW(manager, L"Hop-service", SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
        if (!service) { return false; }
        const auto closeService = qScopeGuard([&]() { CloseServiceHandle(service); });
        SERVICE_STATUS status {}; DWORD needed = 0; QueryServiceConfigW(service, nullptr, 0, &needed);
        if (needed < sizeof(QUERY_SERVICE_CONFIGW) || needed > 65536) { return false; }
        QByteArray buffer(needed, Qt::Uninitialized); auto *config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(buffer.data());
        if (!QueryServiceConfigW(service, config, needed, &needed) || !QueryServiceStatus(service, &status)) { return false; }
        const auto command = QDir::fromNativeSeparators(QString::fromWCharArray(config->lpBinaryPathName));
        return config->dwStartType == SERVICE_AUTO_START && (!mustRun || status.dwCurrentState == SERVICE_RUNNING)
            && command.compare(QLatin1Char('"') + destination + "/Hop-service.exe\"", Qt::CaseInsensitive) == 0;
    };
    const auto existing = OpenServiceW(manager, L"Hop-service", SERVICE_QUERY_STATUS);
    if (existing) {
        CloseServiceHandle(existing);
        if (!serviceMatches(false)) { output << "Installer test refuses a foreign Hop service" << Qt::endl; return 7; }
    } else if (GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST) { return 7; }
    if (!TerminateProcess(mainService, 95) || WaitForSingleObject(mainService, 5000) != WAIT_OBJECT_0) { return 7; }
    if (phase == "start-service") {
        // Только восстановление тестовой VM после отрицательного MSI-сценария.
        if (!serviceMatches(false)) { return 7; }
        const auto service = OpenServiceW(manager, L"Hop-service", SERVICE_START | SERVICE_QUERY_STATUS);
        if (!service) { return 7; }
        const auto closeService = qScopeGuard([&]() { CloseServiceHandle(service); });
        SERVICE_STATUS status {};
        if (!QueryServiceStatus(service, &status)) { return 7; }
        if (status.dwCurrentState == SERVICE_STOPPED && !StartServiceW(service, 0, nullptr)
            && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) { return 7; }
        for (int attempt = 0; attempt < 150; ++attempt) {
            if (serviceMatches(true)) { output << "Verified installed VM Hop service resumed" << Qt::endl; return 0; }
            QThread::msleep(100);
        }
        return 7;
    }
    if (phase == "upgrade" || phase == "rollback" || phase == "downgrade") {
        return probeInstallerUpgradeInVm(phase, destination, serviceMatches, output);
    }
    const auto runMsi = [&](bool install) {
        QProcess process;
        const auto log = QDir::tempPath() + QStringLiteral("/Hop-MSI-%1.log").arg(install ? "install" : "uninstall");
        // msiexec разбирает свойства самостоятельно: кавычки вокруг значения, а не всего NAME=value.
        auto arguments = QStringLiteral("%1 \"%2\" /qn /norestart /L*V \"%3\"").arg(install ? "/i" : "/x",
            QDir::toNativeSeparators(package), QDir::toNativeSeparators(log));
        if (install) { arguments += QStringLiteral(" INSTALLFOLDER=\"%1\"").arg(QDir::toNativeSeparators(destination)); }
        process.setProgram(QStringLiteral("C:/Windows/System32/msiexec.exe")); process.setNativeArguments(arguments); process.start();
        const bool done = process.waitForFinished(90000);
        if (!done) { process.kill(); process.waitForFinished(3000); }
        const auto target = QStringLiteral("//VBOXSVR/HopTestResults/HopJobs/Hop-MSI-%1-%2.log").arg(install ? "install" : "uninstall").arg(QCoreApplication::applicationPid());
        QFile::copy(log, target);
        return done ? process.exitCode() : -1;
    };
    const auto sourceMatches = [&]() {
        for (const auto &name : { QStringLiteral("Hop.exe"), QStringLiteral("Hop-service.exe") }) {
            QFile installed(destination + '/' + name), source(QCoreApplication::applicationDirPath() + '/' + name);
            if (!installed.open(QIODevice::ReadOnly) || !source.open(QIODevice::ReadOnly)
                || QCryptographicHash::hash(installed.readAll(), QCryptographicHash::Sha256) != QCryptographicHash::hash(source.readAll(), QCryptographicHash::Sha256)) { return false; }
        }
        return true;
    };
    if (phase == "install" || phase == "all") {
        if (!serviceMatches(true) || !sourceMatches()) {
            const int result = runMsi(true); output << "MSI install result: " << result << Qt::endl;
            if (result != 0 && result != 3010) { return 7; }
        }
        const bool valid = serviceMatches(true) && sourceMatches();
        output << "MSI service auto-start, owned path and file hash: " << valid << Qt::endl;
        if (!valid) { return 7; }
        if (phase == "install") { return 0; }
    }
    if (phase == "gui-all-fastest") {
        return serviceMatches(true) && sourceMatches()
            && launchInstalledGuiAsOrdinaryUser(destination + "/Hop.exe", output, url.toString(), {}, 0, 4) ? 0 : 7;
    }
    if (phase == "gui-vpn" || phase == "gui-lan" || phase == "gui-catalog" || phase == "gui-api-health" || phase == "gui-api" || phase == "gui-api-fresh" || phase == "gui-idle-connected" || phase == "gui-idle-disconnected" || phase == "gui-idle-monitoring") {
        if (phase == "gui-lan") {
            const auto match = QRegularExpression(QStringLiteral("# HopLanPort=([0-9]{1,5})(?:\\r?\\n|$)")).match(importData);
            const int port = match.hasMatch() ? match.captured(1).toInt() : 0;
            return port >= 1024 && port <= 65535 && serviceMatches(true) && sourceMatches()
                && launchInstalledGuiAsOrdinaryUser(destination + "/Hop.exe", output, url.toString(), {}, port) ? 0 : 7;
        }
        const auto idle = phase == "gui-vpn" || phase == "gui-catalog" || phase == "gui-api" ? QString() : phase.mid(9);
        if (phase == "gui-api") { return serviceMatches(true) && sourceMatches() && launchInstalledGuiAsOrdinaryUser(destination + "/Hop.exe", output, QStringLiteral("HopPersistentApiVpnTest")) ? 0 : 7; }
        if (phase == "gui-api-fresh") { return serviceMatches(true) && sourceMatches() && launchInstalledGuiAsOrdinaryUser(destination + "/Hop.exe", output, QStringLiteral("HopPersistentApiFreshVpnTest")) ? 0 : 7; }
        if (phase == "gui-catalog") { return serviceMatches(true) && sourceMatches() && launchInstalledGuiAsOrdinaryUser(destination + "/Hop.exe", output, QStringLiteral("HopReadOnlyCatalogTest")) ? 0 : 7; }
        if (phase == "gui-api-health") { return serviceMatches(true) && sourceMatches() && launchInstalledGuiAsOrdinaryUser(destination + "/Hop.exe", output, QStringLiteral("HopReadOnlyApiHealthTest")) ? 0 : 7; }
        return serviceMatches(true) && sourceMatches() && launchInstalledGuiAsOrdinaryUser(destination + "/Hop.exe", output, importData.isEmpty() ? url.toString() : importData, idle) ? 0 : 7;
    }
    if (phase == "verify" || phase == "all") {
        if (!serviceMatches(true) || !sourceMatches()) { return 7; }
        const bool gui = launchInstalledGuiAsOrdinaryUser(destination + "/Hop.exe", output);
        const bool vpn = probeVlessPolicyInVm(url, output) == 0;
        output << "Installed SCM VLESS policy: " << vpn << Qt::endl;
        if (phase == "verify") { return gui && vpn ? 0 : 7; }
        if (!gui || !vpn) { return 7; }
    }
    if (phase == "remove" || phase == "all") {
        if (!serviceMatches(false)) { return 7; }
        QRemoteObjectNode node; node.connectToNode(QUrl(QStringLiteral("local:") + amnezia::getIpcServiceUrl()));
        const QSharedPointer<IpcInterfaceReplica> replica(node.acquire<IpcInterfaceReplica>());
        if (!replica->waitForSource(10000)) { return 7; }
        const QJsonObject guard { { "vpnServer", "192.0.2.1" }, { "hopTransportPort", 443 }, { "killSwitchOption", true },
            { "splitTunnelType", 0 }, { "dns1", "9.9.9.9" } };
        auto protection = replica->applyTrafficGuard(guard, QString());
        if (!protection.waitForFinished(5000) || !protection.returnValue()) { return 7; }
        auto count = replica->trafficGuardFilterCount();
        if (!count.waitForFinished(5000) || count.returnValue() <= 0) { return 7; }
        output << "Retained protection before uninstall: " << count.returnValue() << Qt::endl;
        const int result = runMsi(false); output << "MSI uninstall result: " << result << Qt::endl;
        const auto service = OpenServiceW(manager, L"Hop-service", SERVICE_QUERY_STATUS);
        const bool removed = !service && GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST;
        if (service) { CloseServiceHandle(service); }
        const bool filesRemoved = !QFileInfo::exists(destination + "/Hop.exe");
        output << "MSI files/service removed: " << (filesRemoved && removed) << Qt::endl;
        const auto filters = ownProtectionFiltersInVm();
        output << "Hop WFP filters after uninstall: " << filters << Qt::endl;
        return (result == 0 || result == 3010) && removed && filesRemoved && filters == 0 ? 0 : 7;
    }
    return 7;
}

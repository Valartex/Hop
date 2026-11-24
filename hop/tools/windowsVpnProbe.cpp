#include <winsock2.h>
#include <ws2tcpip.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <QCoreApplication>
#include <QFile>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QScopeGuard>
#include <QSettings>
#include <QTextStream>
#include <QTimer>
#include <QUrlQuery>
#include <QDir>
#include <QProcess>
#include <memory>
#include <functional>
#include <array>
#include <Windows.h>

#include "core/protocols/xrayProtocol.h"
#include "core/subscriptions/subscriptionFetcher.h"
#include "core/subscriptions/subscriptionParser.h"
#include "core/utils/serialization/serialization.h"
#include "windowsScmProbe.h"
#include "platforms/windows/native/daemon/windowsTunnelSecrets.h"
#include "extraVmInput.h"
#include "amneziaVmProbe.h"
#include "routedPolicyVmProbe.h"
#include "vlessPolicyVmProbe.h"
#include "installerVmProbe.h"
#include "runtimeDeploymentVmProbe.h"
#include "selectionVmProbe.h"
#include "providerVmProbe.h"
#include "bootGuardVmProbe.h"
#include "awgEndpointVmProbe.h"
#include "datasetVmProbe.h"
#include "controlLeaseVmProbe.h"
#include "ipcQuotaVmProbe.h"
#include "routeRecoveryVmProbe.h"
#include "directUdpLeaseVmProbe.h"

namespace {
bool isVirtualBoxGuest()
{
    const QSettings bios(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS"), QSettings::NativeFormat);
    if (bios.value(QStringLiteral("SystemProductName")).toString().contains(QStringLiteral("VirtualBox"), Qt::CaseInsensitive)) {
        return true;
    }
    // Старые версии Guest Additions не всегда публикуют модель в этом ключе реестра.
    constexpr DWORD rawSmbiosProvider = 0x52534d42;
    const DWORD size = GetSystemFirmwareTable(rawSmbiosProvider, 0, nullptr, 0);
    if (size < 8 || size > 16 * 1024 * 1024) { return false; }
    QByteArray bytes(static_cast<qsizetype>(size), Qt::Uninitialized);
    if (GetSystemFirmwareTable(rawSmbiosProvider, 0, bytes.data(), size) != size) { return false; }
    for (qsizetype offset = 8; offset + 6 <= bytes.size();) {
        const auto type = static_cast<unsigned char>(bytes.at(offset));
        const auto length = static_cast<unsigned char>(bytes.at(offset + 1));
        if (length < 4 || offset + length >= bytes.size()) { return false; }
        const auto end = bytes.indexOf(QByteArray("\0\0", 2), offset + length);
        if (end < 0) { return false; }
        if (type == 1 && length >= 6) {
            const auto productIndex = static_cast<unsigned char>(bytes.at(offset + 5));
            const auto strings = bytes.mid(offset + length, end - offset - length).split('\0');
            return productIndex > 0 && productIndex <= strings.size()
                && strings.at(productIndex - 1).toLower().contains("virtualbox");
        }
        offset = end + 2;
    }
    return false;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream output(stdout);
    if (!isVirtualBoxGuest()) {
        output << "This network test is restricted to a VirtualBox guest" << Qt::endl;
        return 2;
    }
    const auto bootObservation = inspectPreparedBootGuard(output);
    QProcess initialRecovery;
    initialRecovery.start(app.applicationDirPath() + QStringLiteral("/hop_guard_recover.exe"),
        { QStringLiteral("--vm-test-only"), QStringLiteral("--vm-test-guard-only") });
    if (!initialRecovery.waitForFinished(20000) || initialRecovery.exitCode() != 0) {
        output << "Initial owned VM cleanup failed" << Qt::endl;
        return 2;
    }
    // Работающая служба владеет lock журнала; очищаем её маршруты через штатный ACK IPC.
    const bool initialRoutes = IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> replica) {
        auto reply = replica->clearSavedRoutes(); return reply.waitForFinished(5000) && reply.returnValue();
    }, []() { return false; });
    if (!initialRoutes) { output << "Initial owned routes cleanup failed" << Qt::endl; return 2; }
    QString sealedPath;
    {
        WindowsTunnelSecrets sealed;
        const auto synthetic = QStringLiteral("[Interface]\nPrivateKey = synthetic-not-a-real-key\n");
        if (!sealed.seal(synthetic) || WindowsTunnelSecrets::unseal(sealed.path()) != synthetic) { return 2; }
        sealedPath = sealed.path();
        const auto encrypted = CreateFileW(reinterpret_cast<LPCWSTR>(sealedPath.utf16()), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (encrypted == INVALID_HANDLE_VALUE) { return 2; }
        const auto closeEncrypted = qScopeGuard([&]() { CloseHandle(encrypted); });
        QByteArray bytes(65536, Qt::Uninitialized);
        DWORD read = 0;
        if (!ReadFile(encrypted, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)) { return 2; }
        bytes.resize(read);
        if (bytes.contains("synthetic-not-a-real-key")) { return 2; }
    }
    if (QFileInfo::exists(sealedPath)) { return 2; }
    output << "Sealed tunnel configuration: round trip and automatic removal passed" << Qt::endl;
    const bool faultTest = app.arguments().contains(QStringLiteral("--guard-fault"));
    HANDLE serviceProcess = nullptr;
    const auto closeService = qScopeGuard([&]() { if (serviceProcess) { CloseHandle(serviceProcess); } });
    if (faultTest) {
        const int argument = app.arguments().indexOf(QStringLiteral("--service-pid"));
        if (argument < 0 || argument + 1 >= app.arguments().size()) { return 2; }
        bool validPid = false;
        const auto pid = app.arguments().at(argument + 1).toULong(&validPid);
        if (!validPid || pid == 0) { return 2; }
        serviceProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (!serviceProcess) { return 2; }
        std::array<wchar_t, 32768> image {};
        DWORD size = static_cast<DWORD>(image.size());
        if (!QueryFullProcessImageNameW(serviceProcess, 0, image.data(), &size)
            || QDir::cleanPath(QDir::fromNativeSeparators(QString::fromWCharArray(image.data(), size))).compare(
                QDir::cleanPath(app.applicationDirPath() + QStringLiteral("/Hop-service.exe")), Qt::CaseInsensitive) != 0) { return 2; }
    }
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) { return 2; }
    const QUrl url(QString::fromUtf8(input.readLine(16385)).trimmed(), QUrl::StrictMode);
    bool extraRequested = false;
    const auto extra = extraVmInput(extraRequested);
    if (extraRequested) {
        if (!extra.startsWith(QStringLiteral("vpn://")) && !extra.startsWith(QStringLiteral("[Interface]"))) {
            output << "Extra VM key delivery failed" << Qt::endl; return 6;
        }
        if (extra.endsWith(QStringLiteral("# HopProviderTest"))) { return probeProviderInVm(extra.left(extra.lastIndexOf(QLatin1Char('\n'))).trimmed(), url, output); }
        if (extra.contains(QStringLiteral("# HopControlLeaseTest"))) { return probeControlLeaseInVm(output, serviceProcess); }
        if (extra.contains(QStringLiteral("# HopDirectUdpLeaseTest"))) { return probeDirectUdpLeaseInVm(extra, output); }
        if (extra.contains(QStringLiteral("# HopTunUdpLeaseTest"))) { return probeTunUdpLeaseInVm(extra, output); }
        if (extra.contains(QStringLiteral("# HopDatasetTest"))) { return probeDatasetsInVm(output); }
        if (extra.contains(QStringLiteral("# HopAwgEndpointTest"))) { return probeAwgEndpointInVm(extra, output); }
        if (extra.contains(QStringLiteral("# HopBootPrepareTest"))) { return prepareBootGuardInVm(output); }
        if (extra.contains(QStringLiteral("# HopGuardScmRestartTest"))) { return probeGuardScmRestartInVm(output); }
        if (extra.contains(QStringLiteral("# HopBootDiagnosticsTest"))) { return probeBootDiagnosticsInVm(output); }
        if (extra.contains(QStringLiteral("# HopWfpInventoryTest"))) {
            const auto snapshot = bootGuardSnapshot(true);
            output << "Own WFP persistence inventory: " << QJsonDocument(snapshot).toJson(QJsonDocument::Compact) << Qt::endl;
            return snapshot.isEmpty() ? 14 : 0;
        }
        if (extra.contains(QStringLiteral("# HopBootInspectTest"))) {
            const bool accepted = bootObservation.value("retained").toBool() && bootObservation.value("direct").toBool()
                && bootObservation.value("protected").toBool();
            output << "Post-boot policy acceptance: " << accepted << Qt::endl;
            return accepted ? 0 : 14;
        }
        if (extra.contains(QStringLiteral("# HopPacketMonitorInventory"))) {
            for (const auto &arguments : { QStringList { "status" }, QStringList { "filter", "list" }, QStringList { "list", "--json" }, QStringList { "help", "start" } }) {
                QProcess command; command.start(QStringLiteral("C:/Windows/System32/pktmon.exe"),arguments);
                if (!command.waitForFinished(5000)) { command.kill(); command.waitForFinished(2000); return 13; }
                output << "pktmon " << arguments.join(' ') << ": exit " << command.exitCode() << Qt::endl;
                output << QString::fromLocal8Bit(command.readAllStandardOutput().left(16000)) << Qt::endl;
                output << QString::fromLocal8Bit(command.readAllStandardError().left(2000)) << Qt::endl;
            }
            return 0;
        }
        if (extra.contains(QStringLiteral("# HopPacketMonitorControl"))) {
            return physicalDnsDeniedInVm(output,false) ? 0 : 13;
        }
        if (extra.contains(QStringLiteral("# HopIpcQuotaTest"))) { return probeIpcQuotaInVm(output); }
        if (extra.contains(QStringLiteral("# HopIpcScopeTest"))) { return probeIpcScopeInVm(output); }
        if (extra.contains(QStringLiteral("# HopRouteRecoveryTest"))) { return probeRouteRecoveryInVm(serviceProcess, output); }
        if (extra.contains(QStringLiteral("# HopRuntimeStageTest"))) { return stageRuntimeInVm(serviceProcess, output); }
        if (extra.contains(QStringLiteral("# HopGuiAllFastest"))) { return probeInstallerInVm(serviceProcess, url, output, QStringLiteral("gui-all-fastest")); }
        if (extra.contains(QStringLiteral("# HopLanGuiTest"))) { return probeInstallerInVm(serviceProcess, url, output, QStringLiteral("gui-lan"), extra); }
        if (extra.contains(QStringLiteral("# HopReadOnlyCatalogTest"))) { return probeInstallerInVm(serviceProcess, url, output, QStringLiteral("gui-catalog")); }
        if (extra.contains(QStringLiteral("# HopReadOnlyApiHealthTest"))) { return probeInstallerInVm(serviceProcess, url, output, QStringLiteral("gui-api-health")); }
        if (extra.contains(QStringLiteral("# HopPersistentApiVpnTest"))) { return probeInstallerInVm(serviceProcess, url, output, QStringLiteral("gui-api")); }
        if (extra.contains(QStringLiteral("# HopPersistentApiFreshVpnTest"))) { return probeInstallerInVm(serviceProcess, url, output, QStringLiteral("gui-api-fresh")); }
        if (extra.contains(QStringLiteral("# HopGuiIdleConnected"))) { return probeInstallerInVm(serviceProcess, url, output, QStringLiteral("gui-idle-connected"), extra.contains(QStringLiteral("# HopInstalledAwgGuiTest")) ? extra : QString()); }
        if (extra.contains(QStringLiteral("# HopGuiIdleDisconnected"))) { return probeInstallerInVm(serviceProcess, url, output, QStringLiteral("gui-idle-disconnected"), extra.contains(QStringLiteral("# HopInstalledAwgGuiTest")) ? extra : QString()); }
        if (extra.contains(QStringLiteral("# HopGuiMonitoring"))) { return probeInstallerInVm(serviceProcess, url, output, QStringLiteral("gui-idle-monitoring")); }
        if (extra.contains(QStringLiteral("# HopInstalledAwgGuiTest"))) { return probeInstallerInVm(serviceProcess, url, output, QStringLiteral("gui-vpn"), extra); }
        if (extra.contains(QStringLiteral("# HopSelectionTest"))) { return probeSelectionInVm(extra, output); }
        if (extra.contains(QStringLiteral("# HopCrossSelectionTest"))) { return probeSelectionInVm(extra, output, url); }
        for (const auto &phase : { QStringLiteral("install"), QStringLiteral("verify"), QStringLiteral("gui-vpn"), QStringLiteral("remove"), QStringLiteral("upgrade"), QStringLiteral("rollback"), QStringLiteral("downgrade"), QStringLiteral("start-service") }) {
            if (extra.contains(QStringLiteral("# HopInstallerPhase=") + phase)) { return probeInstallerInVm(serviceProcess, url, output, phase); }
        }
        if (extra.contains(QStringLiteral("# HopInstallerInspect"))) { return probeInstallerInVm(serviceProcess, url, output, QStringLiteral("inspect")); }
        if (extra.contains(QStringLiteral("# HopInstallerTest"))) { return probeInstallerInVm(serviceProcess, url, output); }
        if (extra.contains(QStringLiteral("# HopVlessPolicyTest"))) { return probeVlessPolicyInVm(url, output); }
        if (extra.contains(QStringLiteral("# HopCdnRoutingTest"))) { return probeVlessPolicyInVm(url, output, false, true); }
        if (extra.contains(QStringLiteral("# HopCdnNetworkCleanupTest"))) { return inspectCdnNetworkInVm(output); }
        if (extra.contains(QStringLiteral("# HopSmbRoutingTest"))) { return probeVlessPolicyInVm(url, output, true); }
        if (extra.contains(QStringLiteral("# HopRoutedPolicyTest"))) { return probeRoutedPolicyInVm(extra, output); }
        return probeAmneziaInVm(extra, output, serviceProcess);
    }
    hop::SubscriptionFetcher fetcher;
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy::NoProxy);
    std::unique_ptr<XrayProtocol> protocol;
    bool restoring = false;
    bool requestStarted = false;
    bool restartingForCrash = false;
    QString testIpv4;
    int requestStep = 0;
    bool tunnelOk = true;
    const auto clearRoutes = []() {
        return IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) {
            auto reply = iface->clearSavedRoutes();
            return reply.waitForFinished(5000) && reply.returnValue();
        }, []() { return false; });
    };
    const auto cleanup = qScopeGuard([&]() {
        if (protocol) { protocol->stop(); clearRoutes(); }
    });
    const auto fail = [&](const QString &message) { output << message << Qt::endl; app.exit(3); };
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &app, [&]() { fail(QStringLiteral("VPN test deadline exceeded")); });
    std::function<void()> request;
    request = [&]() {
        const bool socks = requestStep == 0 || (faultTest && requestStep == 6);
        const bool direct = faultTest && (requestStep == 3 || requestStep == 5 || requestStep == 8 || requestStep == 10);
        const bool blocked = faultTest && (requestStep == 4 || requestStep == 9);
        const bool ipOnly = requestStep == 1 || blocked || (faultTest && (requestStep == 7 || requestStep == 11));
        const bool recovery = requestStep == (faultTest ? 11 : 3);
        network.setProxy(socks ? protocol->localProxy() : QNetworkProxy(QNetworkProxy::NoProxy));
        network.clearConnectionCache();
        QNetworkRequest req(QUrl(direct ? QStringLiteral("https://1.1.1.1/cdn-cgi/trace") : ipOnly
            ? QStringLiteral("https://%1/generate_204").arg(testIpv4)
            : requestStep == 2 ? QStringLiteral("https://connectivitycheck.gstatic.com/generate_204")
                               : QStringLiteral("https://www.gstatic.com/generate_204")));
        if (ipOnly) {
            req.setRawHeader("Host", "www.gstatic.com");
            req.setPeerVerifyName(QStringLiteral("www.gstatic.com"));
        }
        req.setTransferTimeout(blocked ? 4000 : 12000);
        req.setRawHeader("Connection", "close");
        auto *reply = network.get(req);
        QObject::connect(reply, &QNetworkReply::finished, &app, [&, reply, socks, ipOnly, recovery, direct, blocked]() {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool ok = blocked ? status == 0 && reply->error() != QNetworkReply::NoError
                                    : status == (direct ? 200 : 204) && reply->error() == QNetworkReply::NoError;
            const auto label = blocked ? (requestStep == 4 ? "Protected IP after tunnel failure" : "Protected IP after service crash")
                : direct ? (requestStep == 3 ? "Direct IP with VPN active" : "Direct IP with protection retained")
                : recovery ? "Network after disconnect" : socks ? "Xray SOCKS HTTPS" : ipOnly ? "TUN IPv4 HTTPS" : "TUN DNS HTTPS";
            output << label << ": HTTP " << status << ", Qt " << int(reply->error()) << Qt::endl;
            if (!ok && !blocked) { output << "Request failure: " << reply->errorString() << Qt::endl; }
            reply->deleteLater();
            if (recovery) { app.exit(ok && tunnelOk ? 0 : ok ? 3 : 4); return; }
            tunnelOk = tunnelOk && ok;
            ++requestStep;
            if (!ok && faultTest) { fail(QStringLiteral("Fault test prerequisite or isolation assertion failed")); return; }
            if (requestStep == (faultTest ? 4 : 3)) {
                restoring = true;
                if (faultTest) { protocol->stopAfterFailure(); }
                else { protocol->stop(); }
                if (!clearRoutes()) { fail(QStringLiteral("Route cleanup failed")); return; }
                network.clearAccessCache();
                if (faultTest) {
                    const int count = IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) {
                        auto reply = iface->trafficGuardFilterCount();
                        return reply.waitForFinished(5000) ? reply.returnValue() : -1;
                    }, []() { return -1; });
                    output << "Persistent filters after failure cleanup: " << count << Qt::endl;
                    if (count <= 0) { fail(QStringLiteral("Protection disappeared on failure cleanup")); return; }
                }
            }
            if (faultTest && requestStep == 6) {
                restartingForCrash = true;
                restoring = false;
                // QtRO ожидает ответы через вложенный цикл событий; не запускаем его внутри finished сетевого ответа.
                QTimer::singleShot(0, &app, [&]() {
                    const auto result = protocol->start();
                    if (result != ErrorCode::NoError) { fail(QStringLiteral("Reconnect before active-service crash failed")); }
                });
                return;
            }
            if (faultTest && requestStep == 9) {
                // Убираем маршруты отдельно: недоступность после аварии должна доказывать WFP, а не мёртвый TUN.
                if (!clearRoutes()) { fail(QStringLiteral("Pre-crash route cleanup failed")); return; }
                restoring = true;
                if (!TerminateProcess(serviceProcess, 71) || WaitForSingleObject(serviceProcess, 5000) != WAIT_OBJECT_0) {
                    fail(QStringLiteral("Could not terminate the owned guest test service")); return;
                }
                output << "Owned test service terminated" << Qt::endl;
            }
            if (faultTest && requestStep == 11) {
                QProcess recoveryProcess;
                recoveryProcess.start(app.applicationDirPath() + QStringLiteral("/hop_guard_recover.exe"), { QStringLiteral("--vm-test-only") });
                if (!recoveryProcess.waitForFinished(10000) || recoveryProcess.exitCode() != 0) {
                    fail(QStringLiteral("Persistent guard recovery failed")); return;
                }
                output << recoveryProcess.readAllStandardOutput() << Qt::endl;
                network.clearAccessCache();
            }
            QTimer::singleShot(requestStep == 3 ? 500 : 0, &app, request);
        });
    };
    QObject::connect(&fetcher, &hop::SubscriptionFetcher::failed, &app, [&](const QString &message, int) {
        fail(QStringLiteral("Subscription download failed: ") + message);
    });
    QObject::connect(&fetcher, &hop::SubscriptionFetcher::loaded, &app,
                     [&](const QByteArray &body, const QByteArray &, const QByteArray &, bool) {
        const auto parsed = hop::parseSubscription(body);
        if (!parsed.isValid()) { fail(QStringLiteral("Subscription validation failed")); return; }
        QString uri;
        for (const auto &entry : parsed.entries) {
            if (QUrlQuery(QUrl(entry.uri)).queryItemValue(QStringLiteral("security")) == QStringLiteral("reality")) {
                uri = entry.uri; break;
            }
        }
        if (uri.isEmpty()) { fail(QStringLiteral("No Reality endpoint")); return; }
        const auto resolved = QHostInfo::fromName(QStringLiteral("www.gstatic.com"));
        for (const auto &address : resolved.addresses()) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol) { testIpv4 = address.toString(); break; }
        }
        if (testIpv4.isEmpty()) { fail(QStringLiteral("Pre-VPN DNS lookup failed")); return; }
        QString alias, error;
        auto xray = amnezia::serialization::vless::Deserialize(uri, &alias, &error);
        if (xray.isEmpty() || !error.isEmpty()) { fail(QStringLiteral("VLESS conversion failed")); return; }
        xray.insert(QStringLiteral("log"), QJsonObject { { QStringLiteral("loglevel"), QStringLiteral("none") } });
        const QJsonObject config {
            { QStringLiteral("xray_config_data"), QJsonObject {
                { QStringLiteral("config"), QString::fromUtf8(QJsonDocument(xray).toJson(QJsonDocument::Compact)) } } },
            { QStringLiteral("hostName"), QUrl(uri).host() },
            { QStringLiteral("dns1"), QStringLiteral("9.9.9.9") },
            { QStringLiteral("dns2"), QStringLiteral("8.8.8.8") },
            { QStringLiteral("killSwitchOption"), QStringLiteral("true") },
            { QStringLiteral("splitTunnelType"), faultTest ? 2 : 0 },
            { QStringLiteral("splitTunnelSites"), faultTest ? QJsonArray { QStringLiteral("1.1.1.1/32") } : QJsonArray {} }
        };
        protocol = std::make_unique<XrayProtocol>(config);
        QObject::connect(protocol.get(), &VpnProtocol::connectionStateChanged, &app, [&](Vpn::ConnectionState state) {
            if (state == Vpn::ConnectionState::Error && !restoring) {
                fail(QStringLiteral("VPN protocol error: %1").arg(int(protocol->lastError())));
            } else if (state == Vpn::ConnectionState::Connected && restartingForCrash) {
                restartingForCrash = false;
                output << "Tunnel reconnected before active-service crash" << Qt::endl;
                request();
            } else if (state == Vpn::ConnectionState::Connected && !requestStarted) {
                requestStarted = true;
                output << "TUN and routing configured" << Qt::endl;
                request();
            }
        });
        const auto result = protocol->start();
        if (result != ErrorCode::NoError) { fail(QStringLiteral("VPN start failed: %1").arg(int(result))); }
    });
    if (!fetcher.fetch(url)) { return 2; }
    deadline.start(80000);
    const auto result = app.exec();
    if (faultTest && result == 0) {
        protocol.reset();
        return probeWindowsScm(output) ? 0 : 5;
    }
    return result;
}

#pragma once
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRemoteObjectNode>
#include <QScopeGuard>
#include <QThread>
#include <iphlpapi.h>
#include "core/utils/networkUtilities.h"

inline bool vmRouteExists(const QString &destination, MIB_IPFORWARDROW *value = nullptr)
{
    ULONG size = 0;
    if (GetIpForwardTable(nullptr, &size, FALSE) != ERROR_INSUFFICIENT_BUFFER || size > 32 * 1024 * 1024) { return true; }
    QByteArray bytes(size, Qt::Uninitialized); auto *table = reinterpret_cast<MIB_IPFORWARDTABLE *>(bytes.data());
    if (GetIpForwardTable(table, &size, FALSE) != NO_ERROR) { return true; }
    const auto target = htonl(QHostAddress(destination).toIPv4Address());
    for (DWORD index = 0; index < table->dwNumEntries; ++index) {
        const auto &row = table->table[index];
        if (row.dwForwardDest == target && row.dwForwardMask == 0xffffffff) { if (value) { *value = row; } return true; }
    }
    return false;
}
inline bool probeOnLinkRouteJournalInVm(const QPair<QString, QNetworkInterface> &uplink, QTextStream &output)
{
    const auto destination = QStringLiteral("198.51.100.126"), control = QStringLiteral("198.51.100.127");
    if (vmRouteExists(destination) || vmRouteExists(control)) { return false; }
    const auto call = [](const QString &gateway, const QString &prefix) {
        return IpcClient::withInterface([&](QSharedPointer<IpcInterfaceReplica> replica) {
            auto reply = replica->routeAddList(gateway, {prefix});
            return reply.waitForFinished(5000) ? reply.returnValue() : -1;
        }, []() { return -1; });
    };
    MIB_IPFORWARD_ROW2 foreign {}; InitializeIpForwardEntry(&foreign);
    foreign.InterfaceIndex = uplink.second.index(); foreign.DestinationPrefix.Prefix.si_family = AF_INET;
    foreign.DestinationPrefix.Prefix.Ipv4.sin_addr.s_addr = htonl(QHostAddress(control).toIPv4Address());
    foreign.DestinationPrefix.PrefixLength = 32; foreign.NextHop.si_family = AF_INET;
    foreign.Protocol = MIB_IPPROTO_NETMGMT; foreign.Metric = 1;
    const auto created = CreateIpForwardEntry2(&foreign);
    output << "On-link foreign control creation: " << created << Qt::endl;
    if (created != NO_ERROR) { return false; }
    const auto removeControl = qScopeGuard([&]() { DeleteIpForwardEntry2(&foreign); });
    const auto cleanup = []() {
        return IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> replica) {
            auto reply = replica->clearSavedRoutes();
            return reply.waitForFinished(5000) && reply.returnValue();
        }, []() { return false; });
    };
    const auto clearOwn = qScopeGuard([&]() { cleanup(); });
    MIB_IPFORWARDROW before {}, observed {}, after {};
    if (!vmRouteExists(control, &before)) { return false; }
    const int added = call(QStringLiteral("0.0.0.0"), destination + "/32");
    const bool ownExists = vmRouteExists(destination, &observed);
    output << "On-link journal add: " << added << "; present=" << ownExists
        << "; legacy next-hop=" << QHostAddress(ntohl(observed.dwForwardNextHop)).toString() << Qt::endl;
    if (added != 1 || !ownExists || observed.dwForwardType != MIB_IPROUTE_TYPE_DIRECT
        || observed.dwForwardIfIndex != ULONG(uplink.second.index())) { return false; }
    const int existing = call(QStringLiteral("0.0.0.0"), control + "/32");
    const bool cleaned = cleanup();
    const bool preserved = vmRouteExists(control, &after) && after.dwForwardNextHop == before.dwForwardNextHop
        && after.dwForwardIfIndex == before.dwForwardIfIndex && after.dwForwardMetric1 == before.dwForwardMetric1;
    const bool removed = !vmRouteExists(destination);
    output << "On-link cleanup: own removed=" << removed << "; existing accepted=" << existing
        << "; foreign preserved=" << preserved << Qt::endl;
    return cleaned && removed && existing == 1 && preserved;
}
// Запускается только после проверки VBox и точного image path собственного процесса службы.
inline int probeRouteRecoveryInVm(HANDLE service, QTextStream &output)
{
    QFile image(QCoreApplication::applicationDirPath() + "/Hop-service.exe");
    if (!image.open(QIODevice::ReadOnly)) { return 12; }
    output << "VM route-test service SHA256: " << QCryptographicHash::hash(image.readAll(), QCryptographicHash::Sha256).toHex() << Qt::endl;
    image.close();
    // Установленная служба VM может держать журнал после закрытия GUI. Приостанавливаем
    // только её, с проверкой точного пути; после теста возвращаем прежнее состояние SCM.
    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager || ownProtectionFiltersInVm() != 0) { if (manager) { CloseServiceHandle(manager); } return 12; }
    const auto closeManager = qScopeGuard([&]() { CloseServiceHandle(manager); });
    const auto installed = OpenServiceW(manager, L"Hop-service", SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP);
    const auto installedError = installed ? NO_ERROR : GetLastError();
    const auto closeInstalled = qScopeGuard([&]() { if (installed) { CloseServiceHandle(installed); } });
    SERVICE_STATUS status {};
    bool restoreRunning = false;
    const auto waitState = [&](DWORD wanted) {
        QElapsedTimer timer; timer.start();
        do {
            if (!QueryServiceStatus(installed, &status)) { return false; }
            if (status.dwCurrentState == wanted) { return true; }
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50); QThread::msleep(50);
        } while (timer.elapsed() < 10000);
        return false;
    };
    const auto restoreInstalled = qScopeGuard([&]() {
        if (restoreRunning) {
            StartServiceW(installed, 0, nullptr);
            output << "VM installed service restored: " << waitState(SERVICE_RUNNING) << Qt::endl;
        }
    });
    QProcess isolated;
    const auto stopIsolated = qScopeGuard([&]() {
        if (isolated.state() != QProcess::NotRunning) { isolated.kill(); isolated.waitForFinished(3000); }
        const auto errors = isolated.readAllStandardError();
        if (!errors.isEmpty()) { output << "Isolated service diagnostics: " << errors.left(4000) << Qt::endl; }
    });
    HANDLE isolatedHandle = nullptr;
    const auto closeIsolated = qScopeGuard([&]() { if (isolatedHandle) { CloseHandle(isolatedHandle); } });
    if (installed) {
        DWORD size = 0; QueryServiceConfigW(installed, nullptr, 0, &size);
        if (size < sizeof(QUERY_SERVICE_CONFIGW) || size > 65536) { return 12; }
        QByteArray bytes(size, Qt::Uninitialized); auto *config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(bytes.data());
        if (!QueryServiceConfigW(installed, config, size, &size) || !QueryServiceStatus(installed, &status)
            || QDir::fromNativeSeparators(QString::fromWCharArray(config->lpBinaryPathName)).compare(
                QStringLiteral("\"C:/Program Files/Hop VM Install/Hop-service.exe\""), Qt::CaseInsensitive) != 0) { return 12; }
        if (status.dwCurrentState == SERVICE_RUNNING) {
            restoreRunning = true; ControlService(installed, SERVICE_CONTROL_STOP, &status);
            if (!waitState(SERVICE_STOPPED)) { return 12; }
            if (!TerminateProcess(service, 98) || WaitForSingleObject(service, 10000) != WAIT_OBJECT_0) { return 12; }
            isolated.setProgram(QCoreApplication::applicationDirPath() + "/Hop-service.exe"); isolated.setArguments({"-exec"});
            isolated.start();
            if (!isolated.waitForStarted(5000)) { return 12; }
            isolatedHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, DWORD(isolated.processId()));
            if (!isolatedHandle) { return 12; }
            service = isolatedHandle;
            QRemoteObjectNode readyNode;
            readyNode.connectToNode(QUrl(QStringLiteral("local:") + amnezia::getIpcServiceUrl()));
            const QSharedPointer<IpcInterfaceReplica> readyReplica(readyNode.acquire<IpcInterfaceReplica>());
            if (!readyReplica->waitForSource(10000)) { output << "Isolated service IPC unavailable" << Qt::endl; return 12; }
            output << "VM installed service paused; isolated console service started" << Qt::endl;
        } else if (status.dwCurrentState != SERVICE_STOPPED) { return 12; }
    } else if (installedError != ERROR_SERVICE_DOES_NOT_EXIST) { return 12; }
    const auto destination = QStringLiteral("198.51.100.123"), control = QStringLiteral("198.51.100.124"), changed = QStringLiteral("198.51.100.125");
    if (!service || vmRouteExists(destination) || vmRouteExists(control) || vmRouteExists(changed)) { return 12; }
    const auto uplink = NetworkUtilities::getGatewayAndIfaceTo(QHostAddress("1.0.0.1"));
    if (!uplink.second.isValid() || uplink.second.humanReadableName().startsWith("Hop")) { return 12; }
    if (!probeOnLinkRouteJournalInVm(uplink, output)) { return 12; }
    MIB_IPFORWARDROW foreign {}; foreign.dwForwardDest = htonl(QHostAddress(control).toIPv4Address()); foreign.dwForwardMask = 0xffffffff;
    foreign.dwForwardNextHop = htonl(QHostAddress(uplink.first).toIPv4Address()); foreign.dwForwardIfIndex = uplink.second.index();
    foreign.dwForwardProto = MIB_IPPROTO_NETMGMT; foreign.dwForwardType = foreign.dwForwardNextHop ? MIB_IPROUTE_TYPE_INDIRECT : MIB_IPROUTE_TYPE_DIRECT;
    MIB_IPINTERFACE_ROW iface {}; InitializeIpInterfaceEntry(&iface); iface.Family = AF_INET; iface.InterfaceIndex = foreign.dwForwardIfIndex;
    if (GetIpInterfaceEntry(&iface) != NO_ERROR) { return 12; }
    foreign.dwForwardMetric1 = iface.Metric + 1;
    if (CreateIpForwardEntry(&foreign) != NO_ERROR) { return 12; }
    const auto removeControl = qScopeGuard([&]() { DeleteIpForwardEntry(&foreign); });
    const int added = IpcClient::withInterface([&](QSharedPointer<IpcInterfaceReplica> replica) {
        auto reply = replica->routeAddList(uplink.first, { destination + "/32", control + "/32", changed + "/32" });
        return reply.waitForFinished(5000) ? reply.returnValue() : -1;
    }, []() { return -1; });
    const auto onlink = QStringLiteral("198.51.100.126");
    const int onlinkAdded = IpcClient::withInterface([&](QSharedPointer<IpcInterfaceReplica> replica) {
        auto reply = replica->routeAddList(QStringLiteral("0.0.0.0"), {onlink + "/32"});
        return reply.waitForFinished(5000) ? reply.returnValue() : -1;
    }, []() { return -1; });
    MIB_IPFORWARDROW modified {};
    if (added != 3 || onlinkAdded != 1 || !vmRouteExists(onlink) || !vmRouteExists(destination)
        || !vmRouteExists(control) || !vmRouteExists(changed, &modified)) { return 12; }
    const auto removeChanged = qScopeGuard([&]() { DeleteIpForwardEntry(&modified); });
    modified.dwForwardMetric1 += 17;
    if (SetIpForwardEntry(&modified) != NO_ERROR) { return 12; }
    output << "Route journal: own gateway and on-link /32 created; existing control /32 not adopted; third route changed externally" << Qt::endl;
    if (!TerminateProcess(service, 98) || WaitForSingleObject(service, 10000) != WAIT_OBJECT_0) { return 12; }
    if (!vmRouteExists(destination)) { return 12; }
    QProcess restarted; restarted.setProgram(QCoreApplication::applicationDirPath() + "/Hop-service.exe"); restarted.setArguments({ "-exec" });
    restarted.start();
    const auto stop = qScopeGuard([&]() { if (restarted.state() != QProcess::NotRunning) { restarted.kill(); restarted.waitForFinished(3000); } });
    if (!restarted.waitForStarted(5000)) { return 12; }
    QElapsedTimer timer; timer.start();
    while ((vmRouteExists(destination) || vmRouteExists(onlink)) && timer.elapsed() < 12000 && restarted.state() != QProcess::NotRunning) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50); QThread::msleep(50);
    }
    MIB_IPFORWARDROW after {};
    const bool recovered = !vmRouteExists(destination) && !vmRouteExists(onlink), preserved = vmRouteExists(control);
    const bool changePreserved = vmRouteExists(changed, &after) && after.dwForwardMetric1 == modified.dwForwardMetric1;
    output << "Route journal after own service restart: own removed=" << recovered << "; control preserved=" << preserved
        << "; externally changed route preserved=" << changePreserved << Qt::endl;
    return recovered && preserved && changePreserved ? 0 : 12;
}

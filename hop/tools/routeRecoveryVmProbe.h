#pragma once
#include <QElapsedTimer>
#include <QProcess>
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
// Запускается только после проверки VBox и точного image path собственного процесса службы.
inline int probeRouteRecoveryInVm(HANDLE service, QTextStream &output)
{
    const auto destination = QStringLiteral("198.51.100.123"), control = QStringLiteral("198.51.100.124"), changed = QStringLiteral("198.51.100.125");
    if (!service || vmRouteExists(destination) || vmRouteExists(control) || vmRouteExists(changed)) { return 12; }
    const auto uplink = NetworkUtilities::getGatewayAndIfaceTo(QHostAddress("1.0.0.1"));
    if (!uplink.second.isValid() || uplink.second.humanReadableName().startsWith("Hop")) { return 12; }
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
    MIB_IPFORWARDROW modified {};
    if (added != 3 || !vmRouteExists(destination) || !vmRouteExists(control) || !vmRouteExists(changed, &modified)) { return 12; }
    const auto removeChanged = qScopeGuard([&]() { DeleteIpForwardEntry(&modified); });
    modified.dwForwardMetric1 += 17;
    if (SetIpForwardEntry(&modified) != NO_ERROR) { return 12; }
    output << "Route journal: own /32 created; existing control /32 not adopted; third route changed externally" << Qt::endl;
    if (!TerminateProcess(service, 98) || WaitForSingleObject(service, 10000) != WAIT_OBJECT_0) { return 12; }
    if (!vmRouteExists(destination)) { return 12; }
    QProcess restarted; restarted.setProgram(QCoreApplication::applicationDirPath() + "/Hop-service.exe"); restarted.setArguments({ "-exec" });
    restarted.start();
    const auto stop = qScopeGuard([&]() { if (restarted.state() != QProcess::NotRunning) { restarted.kill(); restarted.waitForFinished(3000); } });
    if (!restarted.waitForStarted(5000)) { return 12; }
    QElapsedTimer timer; timer.start();
    while (vmRouteExists(destination) && timer.elapsed() < 12000 && restarted.state() != QProcess::NotRunning) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50); QThread::msleep(50);
    }
    MIB_IPFORWARDROW after {};
    const bool recovered = !vmRouteExists(destination), preserved = vmRouteExists(control);
    const bool changePreserved = vmRouteExists(changed, &after) && after.dwForwardMetric1 == modified.dwForwardMetric1;
    output << "Route journal after own service restart: own removed=" << recovered << "; control preserved=" << preserved
        << "; externally changed route preserved=" << changePreserved << Qt::endl;
    return recovered && preserved && changePreserved ? 0 : 12;
}

#include "router_win.h"

#include <string>
#include <tlhelp32.h>
#include <tchar.h>

#include <QProcess>
#include <QtConcurrent>

#include <core/utils/networkUtilities.h>
#include "platforms/windows/daemon/windowsfirewall.h"
#include "version.h"
#include "windowsRouteJournal.h"

LONG (NTAPI * NtSuspendProcess)(HANDLE ProcessHandle) = NULL;
LONG (NTAPI * NtResumeProcess)(HANDLE ProcessHandle)  = NULL;

#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)

QList<QString> RouterWin::kIpv6Subnets = { "fc00::/7", "2000::/4", "3000::/4" };

RouterWin &RouterWin::Instance()
{
    static RouterWin s;
    if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop")) { return s; }
    BOOL ok;
    ok = s.InitNtFunctions();
    if (!ok) qDebug() << "RouterWin::Instance failed to InitNtFunctions";

    ok = s.EnableDebugPrivilege();
    if (!ok) qDebug() << "RouterWin::Instance failed to EnableDebugPrivilege";

    return s;
}

int RouterWin::routeAddList(const QString &gw, const QStringList &ips)
{
    const bool hop = QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop");
    if (hop && !WindowsRouteJournal::instance().initialize()) { return 0; }
//    qDebug().noquote() << QString("ROUTE ADD List: IPs size:%1, GW: %2")
//                          .arg(ips.size())
//                          .arg(gw);

//    qDebug().noquote() << QString("ROUTE ADD List: IPs:\n%1")
//                          .arg(ips.join("\n"));


    if (!NetworkUtilities::checkIPv4Format(gw)) {
        qCritical().noquote() << "Trying to add invalid route, gw: " << gw;
        return 0;
    }

    DWORD dwStatus = 0;

    int success_count = 0;
    MIB_IPFORWARDROW ipfrow {};

    ipfrow.dwForwardPolicy = 0;
    ipfrow.dwForwardAge = INFINITE;

    ipfrow.dwForwardNextHop = inet_addr(gw.toStdString().c_str());
    ipfrow.dwForwardType = MIB_IPROUTE_TYPE_INDIRECT;	/* XXX - next hop != final dest */
    ipfrow.dwForwardProto = MIB_IPPROTO_NETMGMT;	/* XXX - MIB_PROTO_NETMGMT */

    // Set iface for route
    IPAddr dwGwAddr = inet_addr(gw.toStdString().c_str());
    if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop") && dwGwAddr == 0) {
        // On-link шлюз не определяет интерфейс. Нулевой адрес иначе выбирает VPN default route.
        if (ips.size() != 1) { return 0; }
        const auto destination = QHostAddress::parseSubnet(ips.first());
        if (destination.first.protocol() != QAbstractSocket::IPv4Protocol || destination.second != 32) { return 0; }
        const auto uplink = NetworkUtilities::getGatewayAndIfaceTo(destination.first).second;
        if (!uplink.isValid() || uplink.humanReadableName() == QStringLiteral(APP_WINDOWS_TUNNEL_INTERFACE)
            || uplink.humanReadableName() == QStringLiteral(APP_WINDOWS_XRAY_INTERFACE)) { return 0; }
        ipfrow.dwForwardIfIndex = uplink.index(); ipfrow.dwForwardType = MIB_IPROUTE_TYPE_DIRECT;
    } else if (GetBestInterface(dwGwAddr, &ipfrow.dwForwardIfIndex) != NO_ERROR) {
        qDebug() << "Router::routeAddList : GetBestInterface failed";
        return false;
    }

    // Get TAP iface metric to set it for new routes
    MIB_IPINTERFACE_ROW tap_iface;
    InitializeIpInterfaceEntry(&tap_iface);
    tap_iface.InterfaceIndex = ipfrow.dwForwardIfIndex;
    tap_iface.Family = AF_INET;
    dwStatus  = GetIpInterfaceEntry(&tap_iface);
    if (dwStatus == NO_ERROR){
        ipfrow.dwForwardMetric1 = tap_iface.Metric + 1;
    }
    else {
        qDebug() << "Router::routeAddList: failed GetIpInterfaceEntry(), Error:" << dwStatus;
        ipfrow.dwForwardMetric1 = 256;
    }
    ipfrow.dwForwardMetric2 = 0;
    ipfrow.dwForwardMetric3 = 0;
    ipfrow.dwForwardMetric4 = 0;
    ipfrow.dwForwardMetric5 = 0;

    for (int i = 0; i < ips.size(); ++i) {
        QString ipWithMask = ips.at(i);
        QString ip = NetworkUtilities::ipAddressFromIpWithSubnet(ipWithMask);

        if (!NetworkUtilities::checkIPv4Format(ip)) {
            qCritical().noquote() << "Critical, trying to add invalid route, ip: " << ip;
            continue;
        }

        QString mask = NetworkUtilities::netMaskFromIpWithSubnet(ipWithMask);

        // address
        ipfrow.dwForwardDest = inet_addr(ip.toStdString().c_str());

        // mask
        in_addr maskAddr {};
        if (inet_pton(AF_INET, mask.toStdString().c_str(), &maskAddr) != 1) { continue; }
        ipfrow.dwForwardMask = maskAddr.S_un.S_addr;

        dwStatus = CreateIpForwardEntry(&ipfrow);
        if (dwStatus == NO_ERROR){
            if (hop) {
                if (!WindowsRouteJournal::instance().remember(ipfrow)) {
                    // Только что созданная нами строка не остаётся без записи при ошибке сохранения.
                    const auto rollback = DeleteIpForwardEntry(&ipfrow);
                    qWarning() << "Hop route journal save failed; new route rollback:" << rollback;
                    continue;
                }
            } else { m_ipForwardRows.insert(ip, ipfrow); }
            success_count++;
        }
        else if (dwStatus == ERROR_OBJECT_ALREADY_EXISTS) {
            // Уже существующий маршрут не становится собственностью Hop и не удаляется при отключении.
            success_count++;
            qDebug() << "Router::routeAdd: warning, route already exist:" << ip << gw;
        }
        else {
            qDebug() << "Router::routeAdd: failed CreateIpForwardEntry(), Error:" << ip << gw << dwStatus;
        }
    }

    qDebug() << "Router::routeAddList finished, success: " << success_count << "/" << ips.size();

    if (m_ipForwardRows.size() > 500) suspendWcmSvc(true);

    return success_count;
}

bool RouterWin::clearSavedRoutes()
{
    if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop")) { return WindowsRouteJournal::instance().clear(); }
    for (auto it = m_ipForwardRows.begin(); it != m_ipForwardRows.end();) {
        const auto status = DeleteIpForwardEntry(&it.value());
        if (status == NO_ERROR || status == ERROR_NOT_FOUND || status == ERROR_FILE_NOT_FOUND) {
            it = m_ipForwardRows.erase(it);
        } else {
            // Ошибка не теряет запись: следующий вызов сможет повторить удаление.
            qWarning() << "Router::clearSavedRoutes failed, code:" << status;
            ++it;
        }
    }
    suspendWcmSvc(false);
    return m_ipForwardRows.isEmpty();
}

int RouterWin::routeDeleteList(const QString &gw, const QStringList &ips)
{
    if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop")) {
        auto &journal = WindowsRouteJournal::instance(); if (!journal.initialize()) { return 0; }
        const QHostAddress gateway(gw); if (gateway.protocol() != QAbstractSocket::IPv4Protocol) { return 0; }
        int removed = 0;
        for (const auto &row : journal.routes()) {
            for (const auto &value : ips) {
                const auto subnet = QHostAddress::parseSubnet(value.contains('/') ? value : value + QStringLiteral("/32"));
                if (subnet.first.protocol() != QAbstractSocket::IPv4Protocol || subnet.second < 0) { continue; }
                const DWORD mask = subnet.second == 0 ? 0 : 0xffffffffu << (32 - subnet.second);
                if (ntohl(row.dwForwardDest) == subnet.first.toIPv4Address() && ntohl(row.dwForwardMask) == mask
                    && ntohl(row.dwForwardNextHop) == gateway.toIPv4Address()) {
                    if (journal.remove(row)) { ++removed; }
                    break;
                }
            }
        }
        return removed;
    }
//    qDebug().noquote() << QString("ROUTE DELETE List: IPs size:%1, GW: %2")
//                          .arg(ips.size())
//                          .arg(gw);

//    qDebug().noquote() << QString("ROUTE DELETE List: IPs:\n%1")
//                          .arg(ips.join("\n"));

    PMIB_IPFORWARDTABLE pIpForwardTable = NULL;
    DWORD dwSize = 0;
    BOOL bOrder = FALSE;
    DWORD dwStatus = 0;
    ULONG gw_addr = inet_addr(gw.toStdString().c_str());

    // Find out how big our buffer needs to be.
    dwStatus = GetIpForwardTable(pIpForwardTable, &dwSize, bOrder);
    if (dwStatus == ERROR_INSUFFICIENT_BUFFER) {
        // Allocate the memory for the table
        if (!(pIpForwardTable = (PMIB_IPFORWARDTABLE) malloc(dwSize))) {
            qDebug() << "Malloc failed. Out of memory.";
            return 0;
        }
        // Now get the table.
        dwStatus = GetIpForwardTable(pIpForwardTable, &dwSize, bOrder);
    }

    if (dwStatus != ERROR_SUCCESS) {
        qDebug() << "getIpForwardTable failed.";
        if (pIpForwardTable)
            free(pIpForwardTable);
        return 0;
    }

    int success_count = 0;

    QMap<ULONG, QPair<QString,ULONG>> ipMap;
    for (int i = 0; i < ips.size(); ++i) {
        QString ipMask = ips.at(i);
        if (ipMask.isEmpty()) continue;
        QString ip = NetworkUtilities::ipAddressFromIpWithSubnet(ipMask);
        QString mask = NetworkUtilities::netMaskFromIpWithSubnet(ipMask);

        if (ip.isEmpty()) continue;

        in_addr maskAddr;
        inet_pton(AF_INET, mask.toStdString().c_str(), &maskAddr);

        ipMap.insert(inet_addr(ip.toStdString().c_str()), qMakePair(ipMask, maskAddr.S_un.S_addr));
    }

    for (int i = 0; i < (int) pIpForwardTable->dwNumEntries; i++) {
        MIB_IPFORWARDROW ipfrow = pIpForwardTable->table[i];

        if(ipMap.contains(ipfrow.dwForwardDest) &&
                ipMap.value(ipfrow.dwForwardDest).second == ipfrow.dwForwardMask &&
                ipfrow.dwForwardNextHop == gw_addr) {
            dwStatus = DeleteIpForwardEntry(&pIpForwardTable->table[i]);
            if (dwStatus == ERROR_SUCCESS) {
                m_ipForwardRows.remove(ipMap.value(ipfrow.dwForwardDest).first);
                success_count++;
            }
        }
    }

    // Free resources
    if (pIpForwardTable)
        free(pIpForwardTable);

    qDebug() << "Router::routeDeleteList finished, success: " << success_count << "/" << ips.size();

    return success_count;
}

bool RouterWin::flushDns()
{
    wchar_t systemDirectory[MAX_PATH] {};
    const auto length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) { return false; }
    QProcess process;
    // В Qt 6 program и аргументы передаются отдельно; строка с пробелом была именем несуществующего exe.
    process.start(QString::fromWCharArray(systemDirectory) + QStringLiteral("/ipconfig.exe"), { QStringLiteral("/flushdns") });
    if (!process.waitForFinished(5000)) {
        if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(1000); }
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

void RouterWin::resetIpStack()
{
//    {
//        QProcess p;
//        QString command = QString("ipconfig /release");
//        p.start(command);
//    }
    {
        QProcess p;
        QString command = QString("netsh int ip reset");
        p.start(command);
        p.waitForFinished();
    }
    {
        QProcess p;
        QString command = QString("netsh winsock reset");
        p.start(command);
        p.waitForFinished();
    }
}

bool RouterWin::createTun(const QString &dev, const QString &subnet)
{
    if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop") && dev != QStringLiteral(APP_WINDOWS_XRAY_INTERFACE)) { return false; }
    NET_LUID luid;
    DWORD res = ConvertInterfaceAliasToLuid(reinterpret_cast<const wchar_t*>(dev.utf16()), &luid);
    if (res != NO_ERROR) {
        qCritical() << "Failed to convert luid: " << res;
        return false;
    }

    HANDLE hEvent = CreateEvent(nullptr, true, false, nullptr);
    if (!hEvent) {
        qCritical() << "Failed to allocate event object";
        return false;
    }
    auto _guardEvent = qScopeGuard([hEvent](){ CloseHandle(hEvent); });

    struct {
        HANDLE hEvent;
        NET_LUID luid;
        const QString &subnet;
        bool found;
    } ctx = { .hEvent = hEvent, .luid = luid, .subnet = subnet, .found = false };

    auto cb = [](void *priv, MIB_UNICASTIPADDRESS_ROW *row, MIB_NOTIFICATION_TYPE NotificationType) {
        auto* c = reinterpret_cast<decltype(ctx)*>(priv);
        if (row != nullptr && row->InterfaceLuid.Value == c->luid.Value && row->Address.si_family == AF_INET) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(row->Address.Ipv4.sin_family, &row->Address.Ipv4.sin_addr, ip, INET_ADDRSTRLEN);
            if (c->subnet == ip && row->DadState == IpDadStatePreferred) {
                c->found = true;
                SetEvent(c->hEvent);
            }
        }
    };

    HANDLE hNotif;
    res = NotifyUnicastIpAddressChange(AF_INET, cb, &ctx, false, &hNotif);
    if (res != NO_ERROR) {
        qCritical() << "Failed to subscribe to interface change";
        return false;
    }
    auto _guardNotif = qScopeGuard([hNotif](){ CancelMibChangeNotify2(hNotif); });

    MIB_UNICASTIPADDRESS_ROW row;
    InitializeUnicastIpAddressEntry(&row);

    row.InterfaceLuid = luid;
    row.Address.si_family = AF_INET;

    inet_pton(AF_INET, subnet.toStdString().c_str(), &row.Address.Ipv4.sin_addr);

    row.OnLinkPrefixLength = 32;
    row.ValidLifetime = 0xffffffff;
    row.PreferredLifetime = 0xffffffff;
    row.DadState = IpDadStatePreferred;

    res = CreateUnicastIpAddressEntry(&row);
    if (res != NO_ERROR && res != ERROR_OBJECT_ALREADY_EXISTS) {
        qDebug() << "Failed to create IP address:" << res;
        return false;
    }

    // Повторное использование готового адреса не вызывает уведомление; состояние проверяем явно.
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (GetUnicastIpAddressEntry(&row) == NO_ERROR) {
            if (row.DadState == IpDadStatePreferred) { return true; }
            if (row.DadState == IpDadStateDuplicate) { return false; }
        }
        if (WaitForSingleObject(hEvent, 50) == WAIT_OBJECT_0) { return ctx.found; }
    }
    qCritical() << "Timeout of waiting for IP assignment for " << dev << " device";
    return false;
}

void RouterWin::suspendWcmSvc(bool suspend)
{
    // Hop не приостанавливает общесистемную службу при загрузке больших наборов маршрутов.
    if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop")) { return; }
    if (suspend == m_suspended) return;

    // Solve Windows bug (routes > 1000)
    DWORD wcmSvcPid = GetServicePid(std::wstring(L"wcmSvc").c_str());

    //ListProcessThreads(wcmSvcPid);
    BOOL ok = SuspendProcess(suspend, wcmSvcPid);
    if (ok) {
        m_suspended = suspend;
    }

    qDebug() << "RouterWin::routeAddList" <<
                (ok ? "succeed to" : "failed to") <<
                (suspend ? "suspend wcmSvc" : "resume wcmSvc");

}

DWORD RouterWin::GetServicePid(LPCWSTR serviceName)
{
    const auto hScm = OpenSCManagerW(nullptr, nullptr, NULL);
    const auto hSc = OpenServiceW(hScm, serviceName, SERVICE_QUERY_STATUS);

    SERVICE_STATUS_PROCESS ssp = {};
    DWORD bytesNeeded = 0;
    QueryServiceStatusEx(hSc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &bytesNeeded);

    CloseServiceHandle(hSc);
    CloseServiceHandle(hScm);

    return ssp.dwProcessId;
}

BOOL RouterWin::ListProcessThreads( DWORD dwOwnerPID )
{
  HANDLE hThreadSnap = INVALID_HANDLE_VALUE;
  THREADENTRY32 te32;

  // Take a snapshot of all running threads
  hThreadSnap = CreateToolhelp32Snapshot( TH32CS_SNAPTHREAD, 0 );
  if( hThreadSnap == INVALID_HANDLE_VALUE )
    return( FALSE );

  // Fill in the size of the structure before using it.
  te32.dwSize = sizeof(THREADENTRY32);

  // Retrieve information about the first thread,
  // and exit if unsuccessful
  if( !Thread32First( hThreadSnap, &te32 ) )
  {
    //printError( TEXT("Thread32First") ); // show cause of failure
    CloseHandle( hThreadSnap );          // clean the snapshot object
    return( FALSE );
  }

  // Now walk the thread list of the system,
  // and display information about each thread
  // associated with the specified process
  //HANDLE threadHandle;
  do
  {
    if( te32.th32OwnerProcessID == dwOwnerPID )
    {
        HANDLE threadHandle = OpenThread (PROCESS_QUERY_INFORMATION, FALSE, te32.th32ThreadID);
         qDebug() << "OpenThread GetLastError:"<< te32.th32ThreadID << GetLastError() << threadHandle;
        ULONG64 cycles = 0;
        BOOL ok = QueryThreadCycleTime(threadHandle, &cycles);
        qDebug() << "QueryThreadCycleTime GetLastError:" << ok << GetLastError();

        qDebug() << "Thread cycles:" << te32.th32ThreadID << cycles;
//      _tprintf( TEXT("\n\n     THREAD ID      = 0x%08X"), te32.th32ThreadID );
//      _tprintf( TEXT("\n     Base priority  = %d"), te32.tpBasePri );
//      _tprintf( TEXT("\n     Delta priority = %d"), te32.tpDeltaPri );
//      _tprintf( TEXT("\n"));

        CloseHandle(threadHandle);
    }
  } while( Thread32Next(hThreadSnap, &te32 ) );

  CloseHandle( hThreadSnap );
  return( TRUE );
}

BOOL RouterWin::EnableDebugPrivilege(VOID)
{
  HANDLE           hToken = NULL;
  TOKEN_PRIVILEGES priv;

  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken))
    return FALSE;

  if (!LookupPrivilegeValueW(NULL, SE_DEBUG_NAME, &priv.Privileges[0].Luid))
    return FALSE;

  priv.PrivilegeCount           = 1;
  priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

  return AdjustTokenPrivileges(hToken, FALSE, &priv, sizeof(priv), NULL, NULL);
}

BOOL RouterWin::InitNtFunctions(VOID)
{
  HMODULE hModule;

  hModule = GetModuleHandleW(L"ntdll.dll");
  if (hModule == NULL)
    return FALSE;

  //NtSuspendProcess = (decltype(NtSuspendProcess))GetProcAddress(hModule, "NtSuspendThread");
  NtSuspendProcess = (decltype(NtSuspendProcess))GetProcAddress(hModule, "NtSuspendProcess");
  if (NtSuspendProcess == NULL)
    return FALSE;

  //NtResumeProcess = (decltype(NtResumeProcess))GetProcAddress(hModule, "NtResumeThread");
  NtResumeProcess = (decltype(NtResumeProcess))GetProcAddress(hModule, "NtResumeProcess");
  if (NtResumeProcess == NULL)
    return FALSE;

  return TRUE;
}

BOOL RouterWin::SuspendProcess(BOOL fSuspend, DWORD dwProcessId)
{
    HANDLE pHandle = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, dwProcessId);
    if (pHandle == NULL) return false;

    bool ok = ((fSuspend ? NtSuspendProcess : NtResumeProcess)(pHandle) == STATUS_SUCCESS);
    CloseHandle(pHandle);

    return ok;
}

bool RouterWin::updateResolvers(const QString& ifname, const QList<QHostAddress>& resolvers)
{
    return m_dnsUtil->updateResolvers(ifname, resolvers);
}

bool RouterWin::restoreResolvers() {
    return m_dnsUtil->restoreResolvers();
}

QNetworkInterface RouterWin::findLoopbackIface()
{
    for (auto iface : QNetworkInterface::allInterfaces()) {
        if (iface.flags() & QNetworkInterface::IsLoopBack) {
            return iface;
        }
    }
    return {};
}

bool RouterWin::StopRoutingIpv6()
{
    qDebug() << "RouterWin::StopRoutingIpv6";

#if APP_WINDOWS_SCOPED_IPV6_FILTERS
    auto *firewall = WindowsFirewall::create(this);
    return firewall && firewall->setIpv6Blocked(true);
#else

    if (auto loopback = findLoopbackIface(); loopback.isValid()) {
        QFuture<bool> res = QtConcurrent::mappedReduced(kIpv6Subnets, [loopback](const QString &subnet) -> bool {
            int res = QProcess::execute("netsh", { "interface", "ipv6", "add", "route", subnet, QString("interface=%1").arg(loopback.index()), "metric=0", "store=active" });
            return res == 0;
        },
        [](bool &result, bool success) {
            result = result && success;
        }, true);

        res.waitForFinished();
        return res.result();
    }

    return false;
#endif
}

bool RouterWin::StartRoutingIpv6()
{
    qDebug() << "RouterWin::StartRoutingIpv6";

#if APP_WINDOWS_SCOPED_IPV6_FILTERS
    auto *firewall = WindowsFirewall::create(this);
    return firewall && firewall->setIpv6Blocked(false);
#else

    if (auto loopback = findLoopbackIface(); loopback.isValid()) {
        QFuture<bool> res = QtConcurrent::mappedReduced(kIpv6Subnets, [loopback](const QString &subnet) -> bool {
            int res = QProcess::execute("netsh", { "interface", "ipv6", "delete", "route", subnet, QString("interface=%1").arg(loopback.index()) });
            return res == 0;
        },
        [](bool &result, bool success) {
            result = result && success;
        }, true);
        res.waitForFinished();
        return res.result();
    }

    return false;
#endif
}

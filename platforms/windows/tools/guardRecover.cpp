#include <winsock2.h>
#include <windows.h>
#include <QCoreApplication>
#include <QTextStream>
#include <QDir>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QThread>
#include "windowsTrafficGuard.h"
#include "windowsRouteJournal.h"
#include "version.h"

namespace {
bool liveInstalledVmService()
{
    if (QDir::cleanPath(QCoreApplication::applicationDirPath()).compare(QStringLiteral("C:/Program Files/Hop Test"), Qt::CaseInsensitive) != 0) { return false; }
    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT); if (!manager) { return false; }
    const auto closeManager = qScopeGuard([&]() { CloseServiceHandle(manager); });
    const auto service = OpenServiceW(manager, L"Hop-service", SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS); if (!service) { return false; }
    const auto closeService = qScopeGuard([&]() { CloseServiceHandle(service); });
    DWORD required = 0; QueryServiceConfigW(service, nullptr, 0, &required);
    if (required < sizeof(QUERY_SERVICE_CONFIGW) || required > 65536) { return false; }
    QByteArray bytes(required, Qt::Uninitialized); auto *config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(bytes.data()); SERVICE_STATUS status {};
    if (!QueryServiceConfigW(service,config,required,&required) || !QueryServiceStatus(service,&status)) { return false; }
    const auto image = QDir::fromNativeSeparators(QString::fromWCharArray(config->lpBinaryPathName));
    return status.dwCurrentState == SERVICE_RUNNING
        && image.compare(QStringLiteral("\"C:/Program Files/Hop VM Install/Hop-service.exe\""),Qt::CaseInsensitive) == 0;
}
bool stopOwnedTunnel()
{
    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) { return false; }
    const auto closeManager = qScopeGuard([&]() { CloseServiceHandle(manager); });
    const auto service = OpenServiceW(manager, APP_WINDOWS_WG_SERVICE_NAME, SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS | SERVICE_STOP | DELETE);
    if (!service) { return GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST; }
    const auto closeService = qScopeGuard([&]() { CloseServiceHandle(service); });
    DWORD size = 0;
    QueryServiceConfigW(service, nullptr, 0, &size);
    if (size == 0 || size > 65536) { return false; }
    QByteArray bytes(static_cast<qsizetype>(size), Qt::Uninitialized);
    auto *config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(bytes.data());
    if (!QueryServiceConfigW(service, config, size, &size)) { return false; }
    const auto expected = QStringLiteral("\"") + QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + QStringLiteral("/Hop-service.exe"))
        + QStringLiteral("\" tunneldaemon --sealed-config \"");
    if (!QDir::fromNativeSeparators(QString::fromWCharArray(config->lpBinaryPathName)).startsWith(
            QDir::fromNativeSeparators(expected), Qt::CaseInsensitive)) { return false; }
    SERVICE_STATUS status {};
    ControlService(service, SERVICE_CONTROL_STOP, &status);
    QElapsedTimer timer;
    timer.start();
    do {
        if (!QueryServiceStatus(service, &status)) { return false; }
        if (status.dwCurrentState == SERVICE_STOPPED) { return DeleteService(service) || GetLastError() == ERROR_SERVICE_MARKED_FOR_DELETE; }
        QThread::msleep(100);
    } while (timer.elapsed() < 15000);
    return false;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream output(stdout);
    if (app.arguments().contains(QStringLiteral("--vm-test-only"))) {
        constexpr DWORD provider = 0x52534d42;
        const DWORD size = GetSystemFirmwareTable(provider, 0, nullptr, 0);
        if (size < 8 || size > 16 * 1024 * 1024) { return 2; }
        QByteArray bytes(static_cast<qsizetype>(size), Qt::Uninitialized);
        if (GetSystemFirmwareTable(provider, 0, bytes.data(), size) != size
            || !bytes.toLower().contains("virtualbox")) { return 2; }
        if (!app.arguments().contains(QStringLiteral("--vm-test-guard-only")) && liveInstalledVmService()) {
            output << "Live installed VM service retains its protection and route journal; cleanup deferred until uninstall" << Qt::endl;
            return 0;
        }
        if (!stopOwnedTunnel()) { output << "Owned VM tunnel cleanup failed" << Qt::endl; return 3; }
    } else if (app.arguments().contains(QStringLiteral("--stop-owned-hop-tunnel"))
               || app.arguments().contains(QStringLiteral("--uninstall-hop"))) {
        if (!stopOwnedTunnel()) { output << "Owned Hop tunnel cleanup failed" << Qt::endl; return 3; }
        if (app.arguments().contains(QStringLiteral("--stop-owned-hop-tunnel"))) {
            return WindowsRouteJournal::instance().initialize() ? 0 : 3;
        }
    } else if (!app.arguments().contains(QStringLiteral("--clear-hop-protection"))) {
        output << "Use --clear-hop-protection to restore direct access after a Hop service failure. Requires administrator rights." << Qt::endl;
        return 2;
    }
    const bool activeVmService = app.arguments().contains(QStringLiteral("--vm-test-only"))
        && app.arguments().contains(QStringLiteral("--vm-test-guard-only"));
    if (((app.arguments().contains(QStringLiteral("--vm-test-only")) && !activeVmService) || app.arguments().contains(QStringLiteral("--uninstall-hop")))
        && !WindowsRouteJournal::instance().initialize()) { output << "Owned route recovery failed" << Qt::endl; return 3; }
    if (!WindowsTrafficGuard::clear()) { return 3; }
    const auto remaining = WindowsTrafficGuard::filterCount();
    output << "Hop protection filters remaining: " << remaining << Qt::endl;
    return remaining == 0 ? 0 : 3;
}

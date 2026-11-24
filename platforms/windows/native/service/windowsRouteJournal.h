#pragma once
#include <winsock2.h>
#include <windows.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <QList>
#include <QString>

// Журнал содержит только маршруты, успешно созданные Hop, без учётных данных.
class WindowsRouteJournal {
public:
    static WindowsRouteJournal &instance();
    bool initialize();
    bool remember(const MIB_IPFORWARDROW &row);
    bool remove(const MIB_IPFORWARDROW &row);
    bool clear();
    DWORD createRoute(MIB_IPFORWARD_ROW2 *row);
    DWORD deleteRoute(const MIB_IPFORWARD_ROW2 *row);
    QList<MIB_IPFORWARDROW> routes() const;
private:
    struct Owned { MIB_IPFORWARDROW row; QString interfaceIdentity; };
    WindowsRouteJournal() = default;
    ~WindowsRouteJournal();
    bool openStorage();
    bool save();
    bool load();
    HANDLE m_directory = INVALID_HANDLE_VALUE;
    HANDLE m_lock = INVALID_HANDLE_VALUE;
    QString m_path, m_boot;
    QList<Owned> m_rows;
    bool m_initialized = false, m_ready = false;
};

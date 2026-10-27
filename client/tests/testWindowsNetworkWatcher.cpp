#include <QSignalSpy>
#include <QTest>
#include <thread>
#include "platforms/windows/windowsnetworkwatcher.h"

namespace {
DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS power {};
int subscriptions = 0, unsubscriptions = 0, wirelessClosed = 0;
bool wirelessOpens = false;
DWORD WINAPI subscribe(DWORD flags, HANDLE recipient, PHPOWERNOTIFY handle)
{
    if (flags != DEVICE_NOTIFY_CALLBACK) { return ERROR_INVALID_PARAMETER; }
    ++subscriptions; power = *static_cast<DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS *>(recipient);
    *handle = reinterpret_cast<HPOWERNOTIFY>(1); return ERROR_SUCCESS;
}
DWORD WINAPI unsubscribe(HPOWERNOTIFY) { ++unsubscriptions; return ERROR_SUCCESS; }
DWORD WINAPI openWireless(DWORD, PVOID, PDWORD version, PHANDLE handle)
{
    if (!wirelessOpens) { return ERROR_SERVICE_NOT_ACTIVE; }
    *version = 2; *handle = reinterpret_cast<HANDLE>(2); return ERROR_SUCCESS;
}
DWORD WINAPI registerWireless(HANDLE, DWORD, BOOL, WLAN_NOTIFICATION_CALLBACK, PVOID, PVOID, PDWORD)
{
    return ERROR_ACCESS_DENIED;
}
DWORD WINAPI closeWireless(HANDLE, PVOID) { ++wirelessClosed; return ERROR_SUCCESS; }
}

class TestWindowsNetworkWatcher : public QObject {
    Q_OBJECT
private slots:
    void init() { power = {}; subscriptions = unsubscriptions = wirelessClosed = 0; wirelessOpens = false; }
    void wakesWithoutWireless_data()
    {
        QTest::addColumn<bool>("opens");
        QTest::newRow("wireless-service-unavailable") << false;
        QTest::newRow("wireless-registration-failed") << true;
    }
    void wakesWithoutWireless()
    {
        QFETCH(bool, opens); wirelessOpens = opens;
        WindowsNetworkWatcher::NativeApi api { subscribe, unsubscribe, openWireless, registerWireless, closeWireless };
        {
            WindowsNetworkWatcher watcher(nullptr, api);
            QSignalSpy resumed(&watcher, &NetworkWatcherImpl::wakeup);
            watcher.initialize(); watcher.initialize(); watcher.start();
            QCOMPARE(subscriptions, 1);
            QVERIFY(power.Callback); QCOMPARE(power.Context, &watcher);
            // Системный callback приходит из другого потока, сигнал — только через Qt event loop.
            std::thread callback([]() { power.Callback(power.Context, PBT_APMRESUMEAUTOMATIC, nullptr); });
            callback.join(); QCOMPARE(resumed.count(), 0);
            QTRY_COMPARE(resumed.count(), 1);
            power.Callback(power.Context, PBT_APMRESUMESUSPEND, nullptr);
            power.Callback(power.Context, PBT_APMSUSPEND, nullptr);
            QCoreApplication::processEvents(); QCOMPARE(resumed.count(), 1);
            watcher.stop(); power.Callback(power.Context, PBT_APMRESUMEAUTOMATIC, nullptr);
            QCoreApplication::processEvents(); QCOMPARE(resumed.count(), 1);
        }
        QCOMPARE(unsubscriptions, 1); QCOMPARE(wirelessClosed, opens ? 1 : 0);
    }
    void queuedWakeIsDiscardedWhenWatcherIsDestroyed()
    {
        WindowsNetworkWatcher::NativeApi api { subscribe, unsubscribe, openWireless, registerWireless, closeWireless };
        auto *watcher = new WindowsNetworkWatcher(nullptr, api); watcher->initialize(); watcher->start();
        int count = 0; connect(watcher, &NetworkWatcherImpl::wakeup, this, [&]() { ++count; });
        power.Callback(power.Context, PBT_APMRESUMEAUTOMATIC, nullptr);
        delete watcher; QCoreApplication::processEvents();
        QCOMPARE(count, 0); QCOMPARE(unsubscriptions, 1);
    }
};
QTEST_GUILESS_MAIN(TestWindowsNetworkWatcher)
#include "testWindowsNetworkWatcher.moc"

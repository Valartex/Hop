#include <QJsonArray>
#include <QJsonDocument>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>
#include <QLocalServer>
#include <QThread>
#include <functional>
#include "vpnConnection.h"
#include "mozilla/localsocketcontroller.h"

#include "core/protocols/xrayProtocol.h"
#include "core/protocols/wireGuardProtocol.h"
#include "core/protocols/routedAwgProtocol.h"
#include "core/utils/ipcClient.h"
#include "rep_ipc_interface_source.h"
#include "version.h"
#include <QTcpServer>
#include <QTcpSocket>

// Заменяет привилегированную службу: ни один метод не меняет сеть компьютера.
class FakeLifecycleService : public IpcInterfaceSimpleSource {
public:
    int starts = 0, stops = 0, cleanupCalls = 0, guardClears = 0, guardApplies = 0;
    bool guardActive = false;
    bool failNextResolverRestore = false;
    std::function<void()> duringGuardClear;
    std::function<void()> duringGuardApply, duringXrayStart;
    std::function<void()> duringFlushDns;
    int createdProcesses = 0;
    bool startSucceeds = false;
    bool failNextGuardClear = false;
    int createPrivilegedProcess() override { ++createdProcesses; return -1; }
    int routeAddList(const QString &, const QStringList &) override { return 0; }
    bool clearSavedRoutes() override { return true; }
    bool routeDeleteList(const QString &, const QStringList &) override { return true; }
    bool flushDns() override {
        if (duringFlushDns) { const auto callback = std::move(duringFlushDns); duringFlushDns = {}; callback(); }
        return true;
    }
    void resetIpStack() override {}
    bool checkAndInstallDriver() override { return true; }
    QStringList getTapList() override { return {}; }
    void cleanUp() override {}
    void setLogsEnabled(bool) override {}
    void clearLogs() override {}
    bool createTun(const QString &, const QString &) override { return true; }
    bool deleteTun(const QString &) override { return true; }
    bool StartRoutingIpv6() override { return true; }
    bool StopRoutingIpv6() override { return true; }
    bool disableKillSwitch() override { ++cleanupCalls; return true; }
    bool disableAllTraffic() override { return true; }
    bool refreshKillSwitch(bool) override { return true; }
    bool addKillSwitchAllowedRange(const QStringList) override { return true; }
    bool resetKillSwitchAllowedRange(const QStringList) override { return true; }
    bool enablePeerTraffic(const QJsonObject &) override { return true; }
    bool enableKillSwitch(const QJsonObject &, int) override { return true; }
    bool updateResolvers(const QString &, const QList<QHostAddress> &) override { return true; }
    bool restoreResolvers() override {
        if (failNextResolverRestore) { failNextResolverRestore = false; return false; }
        return true;
    }
    bool xrayStart(const QString &) override {
        ++starts;
        if (duringXrayStart) { const auto callback = std::move(duringXrayStart); duringXrayStart = {}; callback(); }
        return startSucceeds;
    }
    bool xrayStop() override { ++stops; return true; }
    bool startNetworkCheck(const QString &, const QString &) override { return true; }
    bool stopNetworkCheck() override { return true; }
    bool applyTrafficGuard(const QJsonObject &, const QString &) override {
        ++guardApplies;
        if (duringGuardApply) { const auto callback = std::move(duringGuardApply); duringGuardApply = {}; callback(); }
        guardActive = true; return true;
    }
    bool clearTrafficGuard() override {
        ++guardClears;
        if (failNextGuardClear) { failNextGuardClear = false; return false; }
        if (duringGuardClear) {
            const auto callback = std::move(duringGuardClear); duringGuardClear = {}; callback();
        }
        guardActive = false; return true;
    }
    int trafficGuardFilterCount() override { return 0; }
    bool permitControlEndpoint(const QString &, int, bool) override { return true; }
};

// Имитирует только сообщения транспорта, не открывая системную службу или настоящий VPN pipe.
class FakeAwgController : public ControllerImpl {
public:
    int deactivations = 0, activations = 0;
    bool acknowledge = true;
    void initialize(const Device *, const Keys *) override { emit initialized(true, false, {}); }
    void activate(const QJsonObject &) override { ++activations; emit connected(QString()); }
    void deactivate() override {
        ++deactivations;
        if (acknowledge) { QTimer::singleShot(0, this, [this]() { emit disconnected(); }); }
    }
    void checkStatus() override {}
    void getBackendLogs(std::function<void(const QString &)> &&callback) override { callback(QString()); }
    void cleanupBackendLogs() override {}
};

class NestedStopProtocol : public VpnProtocol {
public:
    NestedStopProtocol() : VpnProtocol({}) {}
    int calls = 0, completeAfter = 1;
    std::function<void()> duringStop;
    ErrorCode start() override { return ErrorCode::NoError; }
    bool isCleanupComplete() const override { return calls >= completeAfter; }
    void stop() override {
        ++calls;
        if (duringStop) {
            const auto callback = std::move(duringStop); duringStop = {};
            QEventLoop wait; QTimer::singleShot(0,&wait,callback); QTimer::singleShot(20,&wait,&QEventLoop::quit); wait.exec();
        }
    }
};
class ControlledVpnConnection : public VpnConnection {
public:
    ControlledVpnConnection() : VpnConnection(nullptr,nullptr) {}
    void attach(const QSharedPointer<VpnProtocol> &protocol) { m_vpnProtocol = protocol; }
};

class CancelDuringStartProtocol : public NestedStopProtocol {
public:
    std::function<void()> duringStart;
    bool *destroyed;
    explicit CancelDuringStartProtocol(bool *flag) : destroyed(flag) {}
    ~CancelDuringStartProtocol() override { *destroyed = true; }
    ErrorCode start() override { duringStart(); return ErrorCode::XrayExecutableCrashed; }
};

class TestXrayProtocolLifecycle : public QObject {
    Q_OBJECT
    static QJsonObject config()
    {
        const QJsonObject xray {
            { QStringLiteral("inbounds"), QJsonArray { QJsonObject {
                { QStringLiteral("listen"), QStringLiteral("127.0.0.1") },
                { QStringLiteral("protocol"), QStringLiteral("socks") }, { QStringLiteral("port"), 10808 } } } }
        };
        return { { QStringLiteral("hostName"), QStringLiteral("127.0.0.1") },
                 { QStringLiteral("xray_config_data"), QJsonObject {
                     { QStringLiteral("config"), QString::fromUtf8(QJsonDocument(xray).toJson(QJsonDocument::Compact)) } } } };
    }
    void checkCleanup(bool failOnce)
    {
        FakeLifecycleService service;
        service.failNextResolverRestore = failOnce;
        QRemoteObjectHost host(QUrl(QStringLiteral("local:HopLifecycleTest-") + QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QVERIFY(host.enableRemoting(&service));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        const QSharedPointer<IpcInterfaceReplica> replica(node.acquire<IpcInterfaceReplica>());
        QVERIFY(replica->waitForSource(5000));
        auto &client = IpcClient::Instance();
        const auto original = client.m_interface;
        const auto restore = qScopeGuard([&]() { client.m_interface = original; });
        client.m_interface = replica;
        {
            auto invalidConfig = config();
            invalidConfig.insert(QStringLiteral("xray_config_data"), QJsonObject { { QStringLiteral("config"), QStringLiteral("{}") } });
            XrayProtocol invalid(invalidConfig);
            QCOMPARE(invalid.start(), ErrorCode::XrayExecutableCrashed);
            invalid.stop();
        }
        QCOMPARE(service.starts, 0);
        QCOMPARE(service.cleanupCalls, 0);
        {
            XrayProtocol protocol(config());
            protocol.stop();
            QCOMPARE(service.cleanupCalls, 0);
            QCOMPARE(protocol.start(), ErrorCode::XrayExecutableCrashed);
            QCOMPARE(service.starts, 1);
            QCOMPARE(service.stops, 1);
            QCOMPARE(service.cleanupCalls, 1);
            // Ошибка запуска очищает ресурсы, но сохраняет защиту до явного отключения.
            QCOMPARE(service.guardClears, 0);
            if (failOnce) { QCOMPARE(protocol.start(), ErrorCode::InternalError); }
            protocol.stop();
            QCOMPARE(service.stops, 1);
            QCOMPARE(service.cleanupCalls, failOnce ? 2 : 1);
            QCOMPARE(service.guardClears, 1);
            protocol.stop();
            QCOMPARE(service.stops, 1);
            QCOMPARE(service.cleanupCalls, failOnce ? 2 : 1);
            protocol.stop();
        }
        // Деструктор после успешной очистки не посылает повторные stop/reset.
        QCOMPARE(service.stops, 1);
        QCOMPARE(service.cleanupCalls, failOnce ? 2 : 1);
        QCOMPARE(service.guardClears, 1);
    }
private slots:
    void protocolStoppedDoesNotAcknowledgePendingOwnerCleanup()
    {
        FakeLifecycleService service;
        QRemoteObjectHost host(QUrl("local:Hop-Cancel-Owner-" + QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QVERIFY(host.enableRemoting(&service));
        QRemoteObjectNode node; QVERIFY(node.connectToNode(host.hostUrl()));
        const QSharedPointer<IpcInterfaceReplica> replica(node.acquire<IpcInterfaceReplica>());
        QVERIFY(replica->waitForSource(5000));
        auto &client = IpcClient::Instance(); const auto previous = client.m_interface;
        const auto restore = qScopeGuard([&]() { client.m_interface = previous; }); client.m_interface = replica;
        ControlledVpnConnection connection;
        const auto protocol = QSharedPointer<NestedStopProtocol>::create();
        protocol->completeAfter = 2; connection.attach(protocol); connection.createProtocolConnections();
        QSignalSpy states(&connection, &VpnConnection::connectionStateChanged);
        connection.disconnectFromVpn();
        QCOMPARE(connection.connectionState(), Vpn::Disconnecting);
        // Отложенная очистка протокола закончилась раньше контрольного таймера владельца.
        protocol->calls = 2; protocol->setConnectionState(Vpn::Disconnected);
        QCOMPARE(connection.connectionState(), Vpn::Disconnecting);
        QVERIFY(connection.vpnProtocol());
        for (const auto &event : states) { QVERIFY(event.first().value<Vpn::ConnectionState>() != Vpn::Disconnected); }
        connection.disconnectFromVpn();
        QVERIFY(connection.vpnProtocol().isNull());
        QCOMPARE(connection.connectionState(), Vpn::Disconnected);
    }

    void cancellationBeforeProtocolCreationStopsPreparation()
    {
        FakeLifecycleService service;
        QRemoteObjectHost host(QUrl("local:Hop-Cancel-Prepare-" + QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QVERIFY(host.enableRemoting(&service));
        QRemoteObjectNode node; QVERIFY(node.connectToNode(host.hostUrl()));
        const QSharedPointer<IpcInterfaceReplica> replica(node.acquire<IpcInterfaceReplica>());
        QVERIFY(replica->waitForSource(5000));
        auto &client = IpcClient::Instance(); const auto previous = client.m_interface;
        const auto restore = qScopeGuard([&]() { client.m_interface = previous; }); client.m_interface = replica;
        SecureQSettings settings("Hop-Cancel-Preparing", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); SecureAppSettingsRepository preferences(&settings);
        ControlledVpnConnection connection; connection.setRepositories(&servers, &preferences);
        connect(&connection, &VpnConnection::connectionStateChanged, &connection, [&](Vpn::ConnectionState state) {
            if (state == Vpn::Connecting) { connection.disconnectFromVpn(); }
        });
        connection.connectToVpn({}, DockerContainer::Xray, config());
        QVERIFY(connection.vpnProtocol().isNull());
        QCOMPARE(connection.connectionState(), Vpn::Disconnected);
        QCOMPARE(service.starts, 0); QCOMPARE(service.guardApplies, 0);
    }
    void cancelDuringConnectedNotificationDoesNotRestoreConnectedState()
    {
        FakeLifecycleService service;
        QRemoteObjectHost host(QUrl("local:Hop-Cancel-Connected-" + QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QVERIFY(host.enableRemoting(&service));
        QRemoteObjectNode node; QVERIFY(node.connectToNode(host.hostUrl()));
        const QSharedPointer<IpcInterfaceReplica> replica(node.acquire<IpcInterfaceReplica>());
        QVERIFY(replica->waitForSource(5000));
        auto &client = IpcClient::Instance(); const auto previous = client.m_interface;
        const auto restore = qScopeGuard([&]() { client.m_interface = previous; }); client.m_interface = replica;
        SecureQSettings settings("Hop-Cancel-State", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); SecureAppSettingsRepository preferences(&settings);
        ControlledVpnConnection connection; connection.setRepositories(&servers, &preferences);
        const auto protocol = QSharedPointer<NestedStopProtocol>::create(); connection.attach(protocol);
        connection.createProtocolConnections();
        // Отмена приходит со стороны клиента, пока он ждёт ответ отдельной службы.
        QTimer::singleShot(0, &connection, [&]() { connection.disconnectFromVpn(); });
        QSignalSpy states(&connection, &VpnConnection::connectionStateChanged);
        protocol->setConnectionState(Vpn::Connected);
        QCOMPARE(connection.connectionState(), Vpn::Disconnected);
        QVERIFY(connection.vpnProtocol().isNull());
        for (const auto &event : states) { QVERIFY(event.first().value<Vpn::ConnectionState>() != Vpn::Connected); }
    }
    void manualStopNeedsGuardReleaseAcknowledgement()
    {
        FakeLifecycleService service; service.guardActive = true; service.failNextGuardClear = true;
        QRemoteObjectHost host(QUrl("local:Hop-Cancel-Guard-Ack-" + QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QVERIFY(host.enableRemoting(&service));
        QRemoteObjectNode node; QVERIFY(node.connectToNode(host.hostUrl()));
        const QSharedPointer<IpcInterfaceReplica> replica(node.acquire<IpcInterfaceReplica>());
        QVERIFY(replica->waitForSource(5000));
        auto &client = IpcClient::Instance(); const auto previous = client.m_interface;
        const auto restore = qScopeGuard([&]() { client.m_interface = previous; }); client.m_interface = replica;
        XrayProtocol protocol(config()); protocol.m_guardOwned = true;
        protocol.stop(); QVERIFY(!protocol.isCleanupComplete()); QVERIFY(service.guardActive);
        protocol.stop(); QVERIFY(protocol.isCleanupComplete()); QVERIFY(!service.guardActive);
        QCOMPARE(service.guardClears, 2);
        auto *backend = new FakeAwgController;
        WireguardProtocol awg({}, backend, nullptr); awg.m_guardOwned = true;
        service.guardActive = true; service.failNextGuardClear = true;
        awg.stop(); QVERIFY(!awg.isCleanupComplete()); QVERIFY(service.guardActive);
        awg.stop(); QVERIFY(awg.isCleanupComplete()); QVERIFY(!service.guardActive);
    }
    void cancellationWaitsForInflightStart_data()
    {
        QTest::addColumn<bool>("duringGuard");
        QTest::addColumn<bool>("manual");
        QTest::newRow("cancel-guard") << true << true;
        QTest::newRow("cancel-xray-start") << false << true;
        QTest::newRow("retain-after-failure") << false << false;
    }
    void cancellationWaitsForInflightStart()
    {
        QFETCH(bool, duringGuard); QFETCH(bool, manual);
        FakeLifecycleService service; service.startSucceeds = true;
        QRemoteObjectHost host(QUrl("local:Hop-Cancel-Start-" + QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QVERIFY(host.enableRemoting(&service));
        QRemoteObjectNode node; QVERIFY(node.connectToNode(host.hostUrl()));
        const QSharedPointer<IpcInterfaceReplica> replica(node.acquire<IpcInterfaceReplica>());
        QVERIFY(replica->waitForSource(5000));
        auto &client = IpcClient::Instance(); const auto previous = client.m_interface;
        const auto restore = qScopeGuard([&]() { client.m_interface = previous; }); client.m_interface = replica;
        XrayProtocol protocol(config());
        const auto cancel = [&]() {
            if (manual) { protocol.stop(); protocol.stopAfterFailure(); }
            else { protocol.stopAfterFailure(); }
            QVERIFY(!protocol.isCleanupComplete());
            QCOMPARE(service.stops, 0); QCOMPARE(service.guardClears, 0);
        };
        if (duringGuard) { service.duringGuardApply = cancel; }
        else { service.duringXrayStart = cancel; }
        QCOMPARE(protocol.start(), ErrorCode::NoError);
        QVERIFY(protocol.isCleanupComplete());
        QCOMPARE(service.starts, duringGuard ? 0 : 1);
        QCOMPARE(service.createdProcesses, 0);
        QCOMPARE(service.guardClears, manual ? 1 : 0);
        QCOMPARE(service.guardActive, !manual);
        protocol.stop();
        QCOMPARE(service.guardClears, 1);
        QVERIFY(!service.guardActive);
    }
    void awgCancellationBeforeActivation()
    {
        FakeLifecycleService service;
        QRemoteObjectHost host(QUrl("local:Hop-Cancel-Awg-" + QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QVERIFY(host.enableRemoting(&service));
        QRemoteObjectNode node; QVERIFY(node.connectToNode(host.hostUrl()));
        const QSharedPointer<IpcInterfaceReplica> replica(node.acquire<IpcInterfaceReplica>());
        QVERIFY(replica->waitForSource(5000));
        auto &client = IpcClient::Instance(); const auto previous = client.m_interface;
        const auto restore = qScopeGuard([&]() { client.m_interface = previous; }); client.m_interface = replica;
        auto *backend = new FakeAwgController;
        WireguardProtocol protocol({{"protocol", "wireguard"}, {"hostName", "127.0.0.1"}}, backend, nullptr);
        service.duringGuardApply = [&]() { protocol.stop(); QVERIFY(!protocol.isCleanupComplete()); };
        QCOMPARE(protocol.start(), ErrorCode::NoError);
        QCOMPARE(backend->activations, 0);
        QCOMPARE(service.guardClears, 1);
        QVERIFY(protocol.isCleanupComplete());
        QVERIFY(!service.guardActive);
    }
    void cancelledStartKeepsProtocolAliveAndIgnoresItsLateError()
    {
        ControlledVpnConnection connection;
        bool destroyed = false;
        auto *protocol = new CancelDuringStartProtocol(&destroyed);
        protocol->duringStart = [&]() { connection.disconnectFromVpn(); QVERIFY(!destroyed); };
        connection.attach(QSharedPointer<VpnProtocol>(protocol));
        connection.m_attemptToken = "cancelled-attempt";
        QSignalSpy errors(&connection, &VpnConnection::vpnProtocolError);
        connection.startProtocol();
        QVERIFY(destroyed);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(connection.connectionState(), Vpn::Disconnected);
    }
    void routedAwgCleanupDoesNotReenterWhileWaitingForIpc()
    {
        FakeLifecycleService service;
        QRemoteObjectHost host(QUrl(QStringLiteral("local:Hop-Routed-Cleanup-") + QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QVERIFY(host.enableRemoting(&service));
        QRemoteObjectNode node; QVERIFY(node.connectToNode(host.hostUrl()));
        const QSharedPointer<IpcInterfaceReplica> replica(node.acquire<IpcInterfaceReplica>());
        QVERIFY(replica->waitForSource(5000));
        auto &client = IpcClient::Instance(); const auto original = client.m_interface;
        const auto restore = qScopeGuard([&]() { client.m_interface = original; }); client.m_interface = replica;
        auto transport = std::unique_ptr<WireguardProtocol>(new WireguardProtocol({}, new FakeAwgController, nullptr));
        transport->m_guardOwned = true;
        RoutedAwgProtocol protocol({}, std::move(transport), nullptr);
        bool nestedAttempt = false;
        service.duringGuardClear = [&]() {
            QTimer::singleShot(0, &protocol, [&]() { nestedAttempt = true; protocol.finishCleanup(); });
            QTest::qWait(30);
        };
        QSignalSpy states(&protocol, &VpnProtocol::connectionStateChanged);
        protocol.stop();
        QVERIFY(nestedAttempt);
        QVERIFY(protocol.isCleanupComplete());
        QCOMPARE(service.guardClears, 1);
        QCOMPARE(states.count(), 1);
        QCOMPARE(protocol.m_cleanupTimer.parent(), &protocol);
        QVERIFY(!protocol.m_cleanupTimer.isActive());
        const int guardApplies = service.guardApplies;
        protocol.stop();
        protocol.stopAfterFailure();
        protocol.stop();
        QCOMPARE(service.guardApplies, guardApplies);
        QCOMPARE(service.guardClears, 1);
        QVERIFY(!service.guardActive);
        QCOMPARE(states.count(), 1);
    }
    void trafficMetricsSeparateDirectionsAndSurviveCounterReset()
    {
        XrayProtocol protocol(config()); QVERIFY(protocol.configureTrafficMetrics());
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost, protocol.m_metricsPort));
        qint64 multiplier = 0;
        connect(&server, &QTcpServer::newConnection, &server, [&]() {
            while (auto *socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
                    if (!socket->readAll().contains("/debug/vars")) { return; }
                    const QJsonObject outbound {{"hop-vpn", QJsonObject{{"downlink", multiplier * 1000000}, {"uplink", multiplier * 500000}}},
                                                {"hop-direct", QJsonObject{{"downlink", multiplier * 250000}, {"uplink", multiplier * 125000}}}};
                    const auto body = QJsonDocument(QJsonObject{{"stats", QJsonObject{{"outbound", outbound}}}}).toJson(QJsonDocument::Compact);
                    socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
        QSignalSpy rates(&protocol, &VpnProtocol::trafficRatesChanged);
        protocol.setConnectionState(Vpn::Connected);
        protocol.pollTraffic(); QTRY_VERIFY(!rates.isEmpty()); QCOMPARE(rates.last().at(0).toDouble(), 0.0);
        ++multiplier; QTRY_VERIFY(rates.last().at(0).toDouble() > 0);
        const auto sample = rates.last();
        QVERIFY(sample.at(0).toDouble() < 100); QCOMPARE(sample.at(0).toDouble(), 2 * sample.at(1).toDouble());
        QCOMPARE(sample.at(1).toDouble(), 2 * sample.at(2).toDouble()); QCOMPARE(sample.at(2).toDouble(), 2 * sample.at(3).toDouble());
        const auto count = rates.count(); multiplier = 0; QTRY_VERIFY(rates.count() > count);
        for (const auto &rate : rates.last()) { QCOMPARE(rate.toDouble(), 0.0); }
        protocol.setConnectionState(Vpn::Disconnected);
    }
    void awgDestructionAfterParentCleanupDoesNotRecreateProtection()
    {
        FakeLifecycleService service;
        QRemoteObjectHost host(QUrl(QStringLiteral("local:Hop-Awg-Parent-Cleanup-") + QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QVERIFY(host.enableRemoting(&service));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        const QSharedPointer<IpcInterfaceReplica> replica(node.acquire<IpcInterfaceReplica>());
        QVERIFY(replica->waitForSource(5000));
        auto &client = IpcClient::Instance();
        const auto original = client.m_interface;
        const auto restore = qScopeGuard([&]() { client.m_interface = original; });
        client.m_interface = replica;
        {
            WireguardProtocol protocol({}, new FakeAwgController, nullptr);
            QCOMPARE(protocol.start(), ErrorCode::NoError);
            QVERIFY(service.guardActive);
            protocol.stopAfterFailure();
            QVERIFY(protocol.isCleanupComplete());
            QVERIFY(service.guardActive);
            // Общий контроллер снимает защиту только после ACK остановки обоих дочерних протоколов.
            auto cleared = replica->clearTrafficGuard();
            QVERIFY(cleared.waitForFinished(5000) && cleared.returnValue());
            QVERIFY(!service.guardActive);
        }
        QVERIFY2(!service.guardActive, "Destroyed AWG transport recreated the protection already released by its parent");
        QCOMPARE(service.guardClears, 1);
    }
    void awgDestructionAfterFailureKeepsProtection_data()
    {
        QTest::addColumn<bool>("acknowledge");
        QTest::newRow("acknowledged-stop") << true;
        QTest::newRow("missing-stop-ack") << false;
    }
    void awgDestructionAfterFailureKeepsProtection()
    {
        QFETCH(bool, acknowledge);
        FakeLifecycleService service;
        QRemoteObjectHost host(QUrl(QStringLiteral("local:Hop-Awg-Failure-Cleanup-") + QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QVERIFY(host.enableRemoting(&service));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        const QSharedPointer<IpcInterfaceReplica> replica(node.acquire<IpcInterfaceReplica>());
        QVERIFY(replica->waitForSource(5000));
        auto &client = IpcClient::Instance();
        const auto original = client.m_interface;
        const auto restore = qScopeGuard([&]() { client.m_interface = original; });
        client.m_interface = replica;
        {
            auto *controller = new FakeAwgController;
            controller->acknowledge = acknowledge;
            WireguardProtocol protocol({}, controller, nullptr);
            QCOMPARE(protocol.start(), ErrorCode::NoError);
            protocol.stopAfterFailure();
            QCOMPARE(protocol.isCleanupComplete(), acknowledge);
        }
        QVERIFY(service.guardActive);
        QCOMPARE(service.guardClears, 0);
    }
    void awgDisconnectWaitsForServiceAcknowledgement()
    {
        QLocalServer server;
        const auto pipe = QStringLiteral("Hop-Awg-Ack-Test-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(server.listen(pipe));
        QLocalSocket *peer = nullptr;
        QByteArray buffer;
        bool deactivateReceived = false;
        connect(&server, &QLocalServer::newConnection, &server, [&]() {
            peer = server.nextPendingConnection();
            connect(peer, &QLocalSocket::readyRead, &server, [&]() {
                buffer += peer->readAll();
                for (qsizetype end; (end = buffer.indexOf('\n')) >= 0;) {
                    const auto command = QJsonDocument::fromJson(buffer.left(end)).object();
                    buffer.remove(0, end + 1);
                    if (command.value(QStringLiteral("type")) == QStringLiteral("status")) {
                        peer->write("{\"type\":\"status\",\"connected\":false}\n");
                        peer->flush();
                    }
                    if (command.value(QStringLiteral("type")) == QStringLiteral("deactivate")) { deactivateReceived = true; }
                }
            });
        });
        LocalSocketController controller(pipe);
        QSignalSpy initialized(&controller, &ControllerImpl::initialized);
        QSignalSpy disconnected(&controller, &ControllerImpl::disconnected);
        controller.initialize(nullptr, nullptr);
        QTRY_COMPARE(initialized.count(), 1);
        controller.deactivate();
        QTRY_VERIFY(deactivateReceived);
        QCOMPARE(disconnected.count(), 0);
        peer->write("{\"type\":\"disconnected\"}\n");
        peer->flush();
        QTRY_COMPARE(disconnected.count(), 1);
        controller.activate(QJsonObject());
        QStringList failureEvents;
        connect(&controller, &LocalSocketController::backendUnavailable, &controller, [&]() { failureEvents.append(QStringLiteral("failure")); });
        connect(&controller, &ControllerImpl::disconnected, &controller, [&]() { failureEvents.append(QStringLiteral("disconnected")); });
        peer->write("{\"type\":\"backendFailure\",\"errorCode\":1}\n");
        peer->flush();
        QTRY_COMPARE(failureEvents.size(), 2);
        QCOMPARE(failureEvents, QStringList({ QStringLiteral("failure"), QStringLiteral("disconnected") }));
        peer->write("{\"type\":\"disconnected\"}\n");
        peer->flush();
        QTest::qWait(30);
        QCOMPARE(disconnected.count(), 2);
    }
    void cleanupIsIdempotent() { checkCleanup(false); }
    void nestedDisconnectDoesNotResetProtocolUnderTheFirstStop()
    {
        ControlledVpnConnection connection;
        const auto protocol = QSharedPointer<NestedStopProtocol>::create(); connection.attach(protocol);
        protocol->duringStop = [&]() { connection.disconnectFromVpn(); };
        connection.disconnectFromVpn();
        QCOMPARE(protocol->calls,1);
        QVERIFY(connection.vpnProtocol().isNull());
        QCOMPARE(connection.connectionState(),Vpn::Disconnected);
    }
    void cleanupTimerFollowsVpnThreadAndWaitsForCompletion()
    {
        QThread thread; thread.start();
        auto *connection = new ControlledVpnConnection; connection->moveToThread(&thread);
        QSharedPointer<NestedStopProtocol> protocol;
        const auto cleanup = qScopeGuard([&]() {
            QMetaObject::invokeMethod(connection,[&]() { protocol.reset(); delete connection; },Qt::BlockingQueuedConnection);
            thread.quit(); thread.wait();
        });
        bool timersFollowThread = false;
        QMetaObject::invokeMethod(connection,[&]() {
            const auto timers = connection->findChildren<QTimer *>(); timersFollowThread = timers.size() == 2;
            for (const auto *timer : timers) { timersFollowThread &= timer->thread() == &thread; }
            protocol = QSharedPointer<NestedStopProtocol>::create(); protocol->completeAfter = 2; connection->attach(protocol);
            connection->disconnectFromVpn();
        },Qt::BlockingQueuedConnection);
        QVERIFY(timersFollowThread);
        const auto complete = [&]() {
            bool result = false;
            QMetaObject::invokeMethod(connection,[&]() { result = protocol->calls == 2 && connection->vpnProtocol().isNull()
                && connection->connectionState() == Vpn::Disconnected; },Qt::BlockingQueuedConnection);
            return result;
        };
        QTRY_VERIFY_WITH_TIMEOUT(complete(),2000);
    }
    void oldDisconnectDoesNotClearAReplacementProtocol()
    {
        ControlledVpnConnection connection;
        const auto previous = QSharedPointer<NestedStopProtocol>::create();
        const auto replacement = QSharedPointer<NestedStopProtocol>::create(); connection.attach(previous);
        previous->duringStop = [&]() { connection.attach(replacement); connection.setConnectionState(Vpn::Connected); };
        connection.disconnectFromVpn();
        QCOMPARE(previous->calls,1);
        QCOMPARE(replacement->calls,0);
        QCOMPARE(connection.vpnProtocol().data(),replacement.data());
        QCOMPARE(connection.connectionState(),Vpn::Connected);
    }
    void failedCleanupCanRetryWithoutStoppingXrayTwice() { checkCleanup(true); }
    void destructionAfterFailureDoesNotReleaseProtection()
    {
        FakeLifecycleService service;
        QRemoteObjectHost host(QUrl(QStringLiteral("local:HopFaultLifecycle-") + QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QVERIFY(host.enableRemoting(&service));
        QRemoteObjectNode node;
        QVERIFY(node.connectToNode(host.hostUrl()));
        const QSharedPointer<IpcInterfaceReplica> replica(node.acquire<IpcInterfaceReplica>());
        QVERIFY(replica->waitForSource(5000));
        auto &client = IpcClient::Instance();
        const auto original = client.m_interface;
        const auto restore = qScopeGuard([&]() { client.m_interface = original; });
        client.m_interface = replica;
        {
            XrayProtocol protocol(config());
            QCOMPARE(protocol.start(), ErrorCode::XrayExecutableCrashed);
            protocol.stopAfterFailure();
        }
        QCOMPARE(service.stops, 1);
        QCOMPARE(service.cleanupCalls, 1);
        QCOMPARE(service.guardClears, 0);
    }
    void windowsDaemonPipeIsBranded()
    {
        const auto pipe = QStringLiteral(APP_WINDOWS_DAEMON_PIPE);
        QVERIFY(pipe.startsWith(QStringLiteral("\\\\.\\pipe\\")));
        if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop")) {
            QCOMPARE(pipe, QStringLiteral("\\\\.\\pipe\\Hop-VpnDaemon"));
        }
    }
    void repeatedConnectionsHandleWakeAndNetworkOnce()
    {
        FakeLifecycleService service;
        QRemoteObjectHost host(QUrl(QStringLiteral("local:HopLifecycleWake-") + QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QVERIFY(host.enableRemoting(&service));
        QRemoteObjectNode node; QVERIFY(node.connectToNode(host.hostUrl()));
        const QSharedPointer<IpcInterfaceReplica> replica(node.acquire<IpcInterfaceReplica>());
        QVERIFY(replica->waitForSource(5000));
        auto &client = IpcClient::Instance(); const auto original = client.m_interface;
        const auto restore = qScopeGuard([&]() { client.m_interface = original; }); client.m_interface = replica;
        VpnConnection connection(nullptr, nullptr);
        connection.m_attemptToken = QStringLiteral("synthetic-resume");
        for (int index = 0; index < 4; ++index) {
            connection.m_vpnProtocol = QSharedPointer<NestedStopProtocol>::create();
            connection.createProtocolConnections();
        }
        QSignalSpy recovery(&connection, &VpnConnection::attemptRecoveryRequested);
        emit service.wakeup();
        QTRY_VERIFY(recovery.count() >= 1); QTest::qWait(100);
        QCOMPARE(recovery.count(), 1);
        QCOMPARE(recovery.first().first().toString(), QStringLiteral("synthetic-resume"));
        emit service.networkChanged();
        QTRY_VERIFY(recovery.count() >= 2); QTest::qWait(100);
        QCOMPARE(recovery.count(), 2);
    }
};
QTEST_GUILESS_MAIN(TestXrayProtocolLifecycle)
#include "testXrayProtocolLifecycle.moc"

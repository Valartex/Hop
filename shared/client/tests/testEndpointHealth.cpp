#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>
#include "core/locations/endpointHealthController.h"
#include "core/controllers/selfhosted/importController.h"
#include "core/utils/networkUtilities.h"
#include "core/locations/refreshPreferences.h"
#include "core/locations/locationCoordinator.h"
#include "core/subscriptions/httpSubscriptionsController.h"
#include "ui/controllers/connectionsPageController.h"

namespace {
QJsonObject config(ImportController &importer, bool ok, int delay = 20)
{
    auto imported = importer.extractConfigFromData(QStringLiteral("vless://00000000-0000-4000-8000-000000000001@127.0.0.1:9?security=none&type=tcp#Test"));
    auto native = amnezia::NativeServerConfig::fromJson(imported.config);
    auto container = native.containerConfig(native.defaultContainer);
    auto *xray = container.getXrayProtocolConfig();
    auto json = QJsonDocument::fromJson(xray->clientConfig->nativeConfig.toUtf8()).object();
    json.insert(QStringLiteral("probeTestPass"), ok);
    json.insert(QStringLiteral("probeTestDelay"), delay);
    xray->clientConfig->nativeConfig = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
    native.updateContainerConfig(native.defaultContainer, container);
    native.subscription = { { QStringLiteral("sourceId"), QStringLiteral("test") } };
    return native.toJson();
}
QString helper() { return QCoreApplication::applicationDirPath() + QStringLiteral("/hop_fake_endpoint_probe.exe"); }
QJsonObject awgConfig(ImportController &importer)
{
    const auto key = QString::fromLatin1(QByteArray(32, 1).toBase64());
    const auto text = QStringLiteral("[Interface]\nPrivateKey = %1\nAddress = 10.77.0.2/32\nDNS = 10.77.0.1\nJc = 2\nJmin = 40\nJmax = 128\nS1 = 64\nS2 = 80\nS3 = 64\nS4 = 64\nH1 = 101001\nH2 = 202001\nH3 = 303001\nH4 = 404001\n[Peer]\nPublicKey = %1\nAllowedIPs = 10.77.0.1/32\nEndpoint = 192.0.2.1:51820\n").arg(key);
    auto native = amnezia::NativeServerConfig::fromJson(importer.extractConfigFromData(text).config);
    auto container = native.containerConfig(native.defaultContainer);
    if (auto *awg = container.getAwgProtocolConfig(); awg && awg->clientConfig) {
        awg->clientConfig->nativeConfig += QStringLiteral("\n# HopProbePass\n# HopProbeSlow\n");
        native.updateContainerConfig(native.defaultContainer, container);
    }
    native.subscription = { { QStringLiteral("sourceId"), QStringLiteral("awg-test") } };
    return native.toJson();
}
}

class TestEndpointHealth : public QObject {
    Q_OBJECT
private slots:
    void cachedApiLocationIsMeasuredWithoutChangingSubscription()
    {
        SecureQSettings settings("Hop-Cached-Api-Health", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); ImportController importer(&servers, nullptr);
        const auto native = amnezia::NativeServerConfig::fromJson(config(importer, true));
        amnezia::ApiV2ServerConfig api {};
        api.configVersion = serverConfigUtils::ConfigSource::AmneziaGateway;
        api.defaultContainer = native.defaultContainer; api.containers = native.containers;
        api.hostName = native.hostName; api.apiConfig.serverCountryCode = "nl";
        api.apiConfig.serviceType = "external-premium"; api.apiConfig.serviceProtocol = "vless";
        servers.addServer("api", api.toJson(), serverConfigUtils::ConfigType::ExternalPremium);
        const auto before = servers.apiV2Config("api")->toJson();
        QSignalSpy changed(&servers, &SecureServersRepository::serverEdited);
        hop::EndpointHealthController health(&servers, nullptr, helper());
        SecureAppSettingsRepository app(&settings);
        hop::LocationCatalog catalog(&servers, &settings);
        VpnConnection vpn(nullptr, nullptr);
        ConnectionController connection(&servers, &app, &vpn);
        hop::HttpSubscriptionsController subscriptions(&servers, &importer, &connection);
        hop::LocationCoordinator session(&catalog, &servers, &app, &connection, &vpn);
        hop::ConnectionsPageController actions(&servers, &subscriptions, &catalog, &session, &health, &connection);
        emit session.locationHealth("api|nl", "unreachable");
        QCOMPARE(actions.healthStatuses().value("api|nl").toMap().value("state").toString(), QStringLiteral("unreachable"));
        QVERIFY(health.canCheckLocation("api|nl"));
        QVERIFY(!health.canCheckLocation("api|de"));
        QVERIFY(!health.checkLocation("api|de"));
        QVERIFY(!health.statuses().contains("api|de"));
        QVERIFY(actions.checkLocation("api|nl"));
        QCOMPARE(actions.healthStatuses().value("api|nl").toMap().value("state").toString(), QStringLiteral("checking"));
        QTRY_VERIFY(!health.isBusy());
        QCOMPARE(actions.healthStatuses().value("api|nl").toMap().value("latencyMs").toInt(), 42);
        QCOMPARE(health.statuses().value("api|nl").toMap().value("latencyMs").toInt(), 42);
        QCOMPARE(health.history("api|nl").value("count").toInt(), 1);
        health.checkAll(); QTRY_VERIFY(!health.isBusy());
        QCOMPARE(health.history("api|nl").value("count").toInt(), 2);
        QCOMPARE(servers.apiV2Config("api")->toJson(), before);
        QCOMPARE(changed.count(), 0);
    }

    void collectionToggleDoesNotDisableScheduledStatusChecks()
    {
        SecureQSettings settings("Hop-Latency-Statistics", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); SecureAppSettingsRepository app(&settings);
        ImportController importer(&servers, &app);
        QVERIFY(servers.applyNativeSubscriptionSnapshot("test", {{"a", config(importer, true)}, {"b", config(importer, true)}}));
        VpnConnection vpn(nullptr, nullptr); ConnectionController connection(&servers, &app, &vpn);
        hop::HttpSubscriptionsController http(&servers, &importer, &connection, nullptr, "hop-missing-test-helper.exe");
        hop::LocationCatalog catalog(&servers, &settings);
        hop::LocationCoordinator session(&catalog, &servers, &app, &connection, &vpn, nullptr, "hop-missing-test-helper.exe", helper());
        hop::EndpointHealthController health(&servers, nullptr, helper());
        hop::RefreshPreferences preferences(&settings, &http, &session, &health);
        preferences.setSubscriptionMinutes(0); preferences.setHealthMinutes(1);
        auto *timer = health.findChild<QTimer *>("hopHealthRefreshTimer"); QVERIFY(timer);
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout")); QTRY_VERIFY(!health.isBusy());
        QCOMPARE(health.statuses().value("a").toMap().value("latencyMs").toInt(), 42);
        QCOMPARE(health.statuses().value("b").toMap().value("latencyMs").toInt(), 42);
        QCOMPARE(health.history("a").value("count").toInt(), 1);
        QCOMPARE(health.history("b").value("count").toInt(), 1);
        preferences.setCollectLatencyStatistics(false);
        QVERIFY(timer->isActive()); QCOMPARE(timer->interval(), 60000);
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout")); QTRY_VERIFY(!health.isBusy());
        QCOMPARE(health.history("a").value("count").toInt(), 1);
        QCOMPARE(health.history("b").value("count").toInt(), 1);
        preferences.setHealthMinutes(0); QVERIFY(!timer->isActive());
        preferences.setCollectLatencyStatistics(true); QVERIFY(!timer->isActive());
        QVERIFY(!health.isBusy());
    }
    void collectionRecordsManualAndConnectedSamplesAndPreservesHistory()
    {
        SecureQSettings settings("Hop-Latency-Statistics", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); SecureAppSettingsRepository app(&settings);
        ImportController importer(&servers, &app);
        QVERIFY(servers.applyNativeSubscriptionSnapshot("test", {{"a", config(importer, true)}, {"b", config(importer, true)}}));
        VpnConnection vpn(nullptr, nullptr); ConnectionController connection(&servers, &app, &vpn);
        hop::HttpSubscriptionsController http(&servers, &importer, &connection, nullptr, "hop-missing-test-helper.exe");
        hop::LocationCatalog catalog(&servers, &settings);
        hop::LocationCoordinator session(&catalog, &servers, &app, &connection, &vpn, nullptr, "hop-missing-test-helper.exe", helper());
        hop::EndpointHealthController health(&servers, nullptr, helper());
        // Машина состояний работает без исполнителей: системный VPN в тесте не запускается.
        QObject::disconnect(&session, &hop::SelectionSession::attemptRequested, &session, nullptr);
        QObject::disconnect(&session, &hop::SelectionSession::healthRequested, &session, nullptr);
        QObject::disconnect(&session, &hop::SelectionSession::disconnectRequested, &session, nullptr);
        QObject::disconnect(&session, &hop::SelectionSession::suspendRequested, &session, nullptr);
        QObject::connect(&session, &hop::LocationCoordinator::locationSampled, &health, &hop::EndpointHealthController::recordSample);
        hop::RefreshPreferences preferences(&settings, &http, &session, &health);
        preferences.setSubscriptionMinutes(0);
        QVERIFY(health.checkLocation("a")); QTRY_VERIFY(!health.isBusy());
        QVERIFY(health.checkLocation("b")); QTRY_VERIFY(!health.isBusy());
        QCOMPARE(health.history("a").value("count").toInt(), 1);
        QCOMPARE(health.history("b").value("count").toInt(), 1);
        QVERIFY(catalog.select("a")); session.start(); session.transportConnected(session.token());
        session.locationSampled("a", 100);
        QCOMPARE(health.history("a").value("count").toInt(), 2);
        preferences.setCollectLatencyStatistics(false);
        session.locationSampled("a", 130);
        QVERIFY(health.checkLocation("b")); QTRY_VERIFY(!health.isBusy());
        QCOMPARE(health.statuses().value("b").toMap().value("latencyMs").toInt(), 42);
        QCOMPARE(health.history("a").value("count").toInt(), 2);
        QCOMPARE(health.history("b").value("count").toInt(), 1);
        session.healthResult(session.token(), true, true, true, 130);
        QCOMPARE(session.state(), hop::SelectionSession::Healthy);
        preferences.setCollectLatencyStatistics(true);
        QVERIFY(!health.isBusy());
        session.locationSampled("a", 140);
        QCOMPARE(health.history("a").value("count").toInt(), 3);
        session.stop();
        QVERIFY(health.checkLocation("b")); QTRY_VERIFY(!health.isBusy());
        QCOMPARE(health.history("b").value("count").toInt(), 2);
        QCOMPARE(health.history("a").value("count").toInt(), 3);
    }
    void collectionPreferenceKeepsLegacyDisabledAndPersistsBoolean()
    {
        for (int legacyScope : {0, 1, 2, 3}) {
            SecureQSettings settings("Hop-Active-Statistics", "memory", nullptr, true, false);
            SecureServersRepository servers(&settings); SecureAppSettingsRepository app(&settings);
            ImportController importer(&servers, &app);
            QVERIFY(servers.applyNativeSubscriptionSnapshot("test", {{"a", config(importer, true)}, {"b", config(importer, true)}}));
            VpnConnection vpn(nullptr, nullptr); ConnectionController connection(&servers, &app, &vpn);
            hop::HttpSubscriptionsController http(&servers, &importer, &connection, nullptr, "hop-missing-test-helper.exe");
            hop::LocationCatalog catalog(&servers, &settings);
            hop::LocationCoordinator session(&catalog, &servers, &app, &connection, &vpn, nullptr, "hop-missing-test-helper.exe", helper());
            hop::EndpointHealthController health(&servers, nullptr, helper());
            settings.setValue("Conf/hopHealthScope", legacyScope);
            hop::RefreshPreferences preferences(&settings, &http, &session, &health);
            QCOMPARE(preferences.collectLatencyStatistics(), legacyScope != 3);
            const bool changed = !preferences.collectLatencyStatistics();
            preferences.setCollectLatencyStatistics(changed);
            hop::RefreshPreferences restored(&settings, &http, &session, &health);
            QCOMPARE(restored.collectLatencyStatistics(), changed);
        }
    }
    void historyScopeFiltersStorageButKeepsLiveResults()
    {
        SecureQSettings settings("Hop-Health-Scope", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); ImportController importer(&servers, nullptr);
        QVERIFY(servers.applyNativeSubscriptionSnapshot("test", {{"a", config(importer, true)}, {"b", config(importer, true)}}));
        hop::EndpointHealthController health(&servers, nullptr, helper());
        health.setHistorySelection({"a"}, false);
        health.checkLocations({"a", "b"}); QTRY_VERIFY(!health.isBusy());
        QCOMPARE(health.history("a").value("count").toInt(), 1);
        QCOMPARE(health.history("b").value("count").toInt(), 0);
        QCOMPARE(health.statuses().value("b").toMap().value("latencyMs").toInt(), 42);
        health.setHistorySelection({}, false);
        health.recordSample("a", 100);
        QCOMPARE(health.history("a").value("count").toInt(), 1);
        health.setHistorySelection({}, true); health.recordSample("b", 200);
        QCOMPARE(health.history("b").value("count").toInt(), 1);
    }
    void historyBoundsStatisticsAndPeriod() {
        hop::LocationHealthHistory history;
        history.record("a", 10, 1000); history.record("a", -1, 2000); history.record("a", 30, 3000);
        const auto stats = history.statistics("a");
        QCOMPARE(stats.value("count").toInt(), 3); QCOMPARE(stats.value("failures").toInt(), 1);
        QCOMPARE(stats.value("minimum").toInt(), 10); QCOMPARE(stats.value("maximum").toInt(), 30);
        QCOMPARE(stats.value("average").toDouble(), 20.0);
        QCOMPARE(history.statistics("a", 2500).value("count").toInt(), 1);
        QCOMPARE(history.statistics("missing").value("average").toInt(), -1);
        for (int index = 0; index < 300; ++index) { history.record("b", 42, 4000 + index); }
        QCOMPARE(history.statistics("b").value("count").toInt(), hop::LocationHealthHistory::perLocationLimit);
        history.retain({"a"}); QCOMPARE(history.count(), 3);
        history.record("c", 5, hop::LocationHealthHistory::retentionMs + 4000);
        QCOMPARE(history.count(), 1);
        for (int index = 0; index < hop::LocationHealthHistory::totalLimit + 10; ++index) {
            history.record(QString::number(index), 1, hop::LocationHealthHistory::retentionMs + 5000 + index);
        }
        QCOMPARE(history.count(), hop::LocationHealthHistory::totalLimit);
        history.retain({}); QCOMPARE(history.count(), 0);
    }
    void checksOneLocationAndAccumulatesOnlyFinishedMeasurements() {
        SecureQSettings settings("Hop-Location-Check", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); ImportController importer(&servers, nullptr);
        QVERIFY(servers.applyNativeSubscriptionSnapshot("test", { {"yes", config(importer, true)}, {"no", config(importer, false)}, {"slow", config(importer, true, 1500)} }));
        hop::EndpointHealthController health(&servers, nullptr, helper());
        QVERIFY(health.checkLocation("yes")); QTRY_VERIFY(!health.isBusy());
        QCOMPARE(health.statuses().size(), 1); QCOMPARE(health.history("yes").value("count").toInt(), 1);
        QVERIFY(health.checkLocation("no")); QTRY_VERIFY(!health.isBusy());
        QCOMPARE(health.history("no").value("failures").toInt(), 1);
        QVERIFY(health.checkLocation("yes")); QTRY_VERIFY(!health.isBusy());
        QCOMPARE(health.history("yes").value("count").toInt(), 2);
        QVERIFY(health.checkLocation("slow")); health.cancel();
        QCOMPARE(health.history("slow").value("count").toInt(), 0);
        QVERIFY(!health.checkLocation("missing"));
        health.retainHistory({"yes"}); QCOMPARE(health.history("no").value("count").toInt(), 0);
    }
    void scheduledCheckUsesBoundedProbeAndCanBeDisabled()
    {
        SecureQSettings settings("Hop-Scheduled-Health", "memory", nullptr, false, false);
        SecureServersRepository servers(&settings);
        ImportController importer(&servers, nullptr);
        QVERIFY(servers.applyNativeSubscriptionSnapshot("test", { { "yes", config(importer, true) } }));
        hop::EndpointHealthController health(&servers, nullptr, helper());
        health.setRefreshIntervalMinutes(5);
        auto *timer = health.findChild<QTimer *>("hopHealthRefreshTimer"); QVERIFY(timer);
        connect(&health, &hop::EndpointHealthController::scheduledCheckRequested, &health, &hop::EndpointHealthController::checkAll);
        QVERIFY(timer->isActive()); QCOMPARE(timer->interval(), 300000);
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout"));
        QTRY_COMPARE(health.statuses().value("yes").toMap().value("latencyMs").toInt(), 42);
        health.setRefreshIntervalMinutes(0); QVERIFY(!timer->isActive());
    }
    void windowsRouteSelectionDoesNotModifyNetwork()
    {
        const auto loopback = NetworkUtilities::getGatewayAndIfaceTo(QHostAddress::LocalHost);
        QVERIFY(loopback.second.isValid());
        QCOMPARE(loopback.first, QStringLiteral("0.0.0.0"));
        QVERIFY(NetworkUtilities::getGatewayAndIfaceTo(QHostAddress::LocalHostIPv6).first.isEmpty());
    }
    void resultsAndUnsupported()
    {
        SecureQSettings settings(QStringLiteral("Hop-HealthTests-") + QUuid::createUuid().toString(), "isolated", nullptr, false);
        const auto cleanup = qScopeGuard([&]() { settings.clearSettings(); });
        SecureServersRepository servers(&settings);
        ImportController importer(&servers, nullptr);
        QVERIFY(servers.applyNativeSubscriptionSnapshot("test", { { "yes", config(importer, true) }, { "no", config(importer, false) } }));
        hop::EndpointHealthController health(&servers, nullptr, helper());
        QVERIFY(health.available());
        health.checkAll();
        QVERIFY(health.isBusy());
        health.checkAll();
        QTRY_VERIFY_WITH_TIMEOUT(!health.isBusy(), 5000);
        QCOMPARE(health.statuses().value("yes").toMap().value("state").toString(), QStringLiteral("reachable"));
        QCOMPARE(health.statuses().value("yes").toMap().value("latencyMs").toInt(), 42);
        QCOMPARE(health.statuses().value("no").toMap().value("state").toString(), QStringLiteral("unreachable"));
        QCOMPARE(health.statuses().value("no").toMap().value("latencyMs").toInt(), -1);
        health.cancel();
        QVERIFY(!health.isBusy());
    }
    void subscriptionMutationCancelsOldGeneration()
    {
        SecureQSettings settings(QStringLiteral("Hop-HealthTests-") + QUuid::createUuid().toString(), "isolated", nullptr, false);
        const auto cleanup = qScopeGuard([&]() { settings.clearSettings(); });
        SecureServersRepository servers(&settings);
        ImportController importer(&servers, nullptr);
        QVector<QPair<QString, QJsonObject>> batch;
        for (int i = 0; i < 12; ++i) { batch.append({ QString::number(i), config(importer, true, 1500) }); }
        QVERIFY(servers.applyNativeSubscriptionSnapshot("test", batch));
        hop::EndpointHealthController health(&servers, nullptr, helper());
        health.checkAll();
        QTRY_COMPARE(health.findChildren<QProcess *>().size(), 2);
        QVERIFY(health.isBusy());
        QVERIFY(servers.applyNativeSubscriptionSnapshot("test", { { "new", config(importer, false) } }));
        QVERIFY(!health.isBusy());
        QVERIFY(health.statuses().isEmpty());
        QTest::qWait(100);
        QVERIFY(health.statuses().isEmpty());
        health.checkAll();
        QTRY_VERIFY_WITH_TIMEOUT(!health.isBusy(), 5000);
        QCOMPARE(health.statuses().size(), 1);
        QCOMPARE(health.statuses().value("new").toMap().value("state").toString(), QStringLiteral("unreachable"));
    }
    void missingHelperDoesNotProduceFalseFailure()
    {
        SecureQSettings settings(QStringLiteral("Hop-HealthTests-") + QUuid::createUuid().toString(), "isolated", nullptr, false);
        const auto cleanup = qScopeGuard([&]() { settings.clearSettings(); });
        SecureServersRepository servers(&settings);
        hop::EndpointHealthController health(&servers, nullptr, helper() + QStringLiteral(".missing"));
        QVERIFY(!health.available());
        health.checkAll();
        QVERIFY(!health.isBusy());
        QVERIFY(health.statuses().isEmpty());
    }
    void awgIsSerialAndCancelledBeforeWorkingTunnel()
    {
        SecureQSettings settings("Hop-AwgHealth", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings);
        ImportController importer(&servers, nullptr);
        const auto profile = awgConfig(importer);
        QVERIFY(amnezia::NativeServerConfig::fromJson(profile).containerConfig(amnezia::NativeServerConfig::fromJson(profile).defaultContainer).getAwgProtocolConfig());
        QVERIFY(servers.applyNativeSubscriptionSnapshot("awg-test", { { "a", profile }, { "b", profile }, { "c", profile } }));
        hop::EndpointHealthController health(&servers, nullptr, helper(), helper());
        health.checkAll();
        const auto running = [&]() { int count = 0; for (auto *process : health.findChildren<QProcess *>()) { count += process->state() != QProcess::NotRunning; } return count; };
        QTRY_COMPARE_WITH_TIMEOUT(running(), 1, 1000);
        QTest::qWait(100); QCOMPARE(running(), 1);
        health.setTunnelActive(true);
        QVERIFY(!health.isBusy()); QTRY_COMPARE(running(), 0);
        for (const auto &id : { "a", "b", "c" }) { QCOMPARE(health.statuses().value(id).toMap().value("state").toString(), QStringLiteral("deferred")); }
        health.checkAll(); QVERIFY(!health.isBusy()); QCOMPARE(running(), 0);
        health.setTunnelActive(false);
        health.checkAll();
        QTRY_VERIFY_WITH_TIMEOUT(!health.isBusy(), 7000);
        for (const auto &id : { "a", "b", "c" }) { QCOMPARE(health.statuses().value(id).toMap().value("state").toString(), QStringLiteral("reachable")); }
    }
};
QTEST_GUILESS_MAIN(TestEndpointHealth)
#include "testEndpointHealth.moc"

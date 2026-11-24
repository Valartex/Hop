#include <QTest>
#include <QSignalSpy>
#include <QUuid>
#include "core/locations/selectionSession.h"
#include "core/controllers/selfhosted/importController.h"

class TestSelectionSession : public QObject {
    Q_OBJECT
    static void populate(SecureServersRepository &servers)
    {
        ImportController importer(&servers, nullptr);
        auto value = importer.extractConfigFromData(QStringLiteral("vless://00000000-0000-4000-8000-000000000001@127.0.0.1:9?security=none&type=tcp#Test")).config;
        servers.addServer("a", value, serverConfigUtils::ConfigType::Native);
        servers.addServer("b", value, serverConfigUtils::ConfigType::Native);
    }
    static hop::SelectionSession::Timing timing() { return { 10000, 10000, 10000, 3, 0 }; }
private slots:
    void automaticStartsWithSelectedPoolMemberAndRetainsFailover()
    {
        SecureQSettings settings("Hop-Automatic-Selected", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        hop::LocationCatalog catalog(&servers, &settings);
        catalog.toggleLocation("a"); catalog.toggleLocation("b");
        catalog.setMode(hop::LocationCatalog::Automatic);
        QVERIFY(catalog.select("b"));
        const auto priorities = catalog.priorities();
        hop::SelectionSession session(&catalog, timing(), nullptr);
        QSignalSpy attempts(&session, &hop::SelectionSession::attemptRequested);
        QSignalSpy suspends(&session, &hop::SelectionSession::suspendRequested);
        session.start();
        QCOMPARE(attempts.count(), 1);
        QCOMPARE(session.activeId(), QStringLiteral("b"));
        session.transportConnected(session.token()); session.healthResult(session.token(), true);
        QCOMPARE(session.state(), hop::SelectionSession::Healthy);
        session.transportFailed(session.token(), "unavailable");
        QCOMPARE(suspends.count(), 1);
        QCOMPARE(attempts.count(), 1);
        session.suspended(session.token(), true);
        QTRY_COMPARE(attempts.count(), 2);
        QCOMPARE(session.activeId(), QStringLiteral("a"));
        QCOMPARE(catalog.mode(), hop::LocationCatalog::Automatic);
        QCOMPARE(catalog.priorities(), priorities);
        session.stop();
    }
    void automaticSelectionNeverBypassesPoolOrNetworkRestrictions()
    {
        SecureQSettings settings("Hop-Automatic-Selected-Eligibility", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        hop::LocationCatalog catalog(&servers, &settings);
        catalog.toggleLocation("a"); catalog.setMode(hop::LocationCatalog::Automatic);
        QVERIFY(catalog.select("b"));
        hop::SelectionSession session(&catalog, timing(), nullptr);
        session.start(); QCOMPARE(session.activeId(), QStringLiteral("a")); session.stop();
        catalog.toggleLocation("b"); catalog.setExcluded("b", true);
        session.start(); QCOMPARE(session.activeId(), QStringLiteral("a")); session.stop();
        catalog.setExcluded("b", false); catalog.setRestrictedLocation("b", true); catalog.setRestrictedMode(2);
        session.start(); QCOMPARE(session.activeId(), QStringLiteral("a")); session.stop();
    }
    void forcedRestrictedManualSelectionRequiresMark()
    {
        SecureQSettings settings("Hop-Restricted-Manual", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        hop::LocationCatalog catalog(&servers, &settings);
        QVERIFY(catalog.select("b"));
        catalog.setRestrictedMode(1);
        hop::SelectionSession session(&catalog, timing(), nullptr);
        QSignalSpy attempts(&session, &hop::SelectionSession::attemptRequested);
        session.start();
        QVERIFY(!session.running());
        QVERIFY(!session.status().isEmpty());
        QCOMPARE(attempts.count(), 0);
        QVERIFY(catalog.setRestrictedLocation("b", true));
        session.start();
        QCOMPARE(attempts.count(), 1);
        QCOMPARE(attempts.first().at(0).toString(), QStringLiteral("b"));
    }
    void restrictedFallbackUsesOnlyAllowedMarkedLocations()
    {
        SecureQSettings settings("Hop-Restricted-Fallback", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        hop::LocationCatalog catalog(&servers, &settings);
        catalog.toggleLocation("a"); catalog.toggleLocation("b"); catalog.setMode(hop::LocationCatalog::Automatic);
        QVERIFY(catalog.setRestrictedLocation("b", true)); catalog.setRestrictedMode(2);
        hop::SelectionSession session(&catalog, timing(), nullptr);
        QSignalSpy attempts(&session, &hop::SelectionSession::attemptRequested);
        session.start(); QCOMPARE(attempts.count(), 1);
        QCOMPARE(attempts.first().at(0).toString(), QStringLiteral("a")); QVERIFY(!session.restrictedActive());
        session.transportFailed(session.token(), "unavailable"); session.suspended(session.token(), true);
        QTRY_COMPARE(attempts.count(), 2);
        QCOMPARE(attempts.last().at(0).toString(), QStringLiteral("b")); QVERIFY(session.restrictedActive()); session.stop();
        catalog.setExcluded("b", true); session.start();
        session.transportFailed(session.token(), "unavailable"); session.suspended(session.token(), true);
        QTRY_COMPARE(session.state(), hop::SelectionSession::Backoff);
        QCOMPARE(attempts.count(), 3); QVERIFY(!session.restrictedActive()); session.stop();
    }
    void newNetworkRestoresNormalPoolAfterRestrictedFallback()
    {
        SecureQSettings settings("Hop-Restricted-Network", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        hop::LocationCatalog catalog(&servers, &settings);
        catalog.toggleLocation("a"); catalog.toggleLocation("b"); catalog.setMode(hop::LocationCatalog::Automatic);
        catalog.setRestrictedLocation("b", true); catalog.setRestrictedMode(2);
        hop::SelectionSession session(&catalog, timing(), nullptr); QSignalSpy attempts(&session, &hop::SelectionSession::attemptRequested);
        session.start(); session.transportFailed(session.token(), "unavailable"); session.suspended(session.token(), true);
        QTRY_COMPARE(attempts.count(), 2); QVERIFY(session.restrictedActive());
        session.transportConnected(session.token()); session.healthResult(session.token(), true);
        session.networkChanged(); QVERIFY(!session.restrictedActive()); session.suspended(session.token(), true);
        QTRY_COMPARE(attempts.count(), 3); QCOMPARE(attempts.last().at(0).toString(), QStringLiteral("a")); session.stop();
    }
    void forcedRestrictedModeAndMarksSurviveRestart()
    {
        SecureQSettings settings("Hop-Restricted-Persist", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        {
            hop::LocationCatalog catalog(&servers, &settings);
            catalog.toggleLocation("a"); catalog.toggleLocation("b"); catalog.setMode(hop::LocationCatalog::Automatic);
            QVERIFY(catalog.setRestrictedLocation("b", true)); catalog.setRestrictedMode(1);
        }
        hop::LocationCatalog catalog(&servers, &settings); QCOMPARE(catalog.restrictedMode(), 1);
        QCOMPARE(catalog.networkCandidateIds(true), QStringList { QStringLiteral("b") });
        hop::SelectionSession session(&catalog, timing(), nullptr);
        QSignalSpy attempts(&session, &hop::SelectionSession::attemptRequested);
        session.start(); QCOMPARE(attempts.count(), 1);
        QCOMPARE(attempts.first().at(0).toString(), QStringLiteral("b")); session.stop();
    }
    void manualRecoveryNeverFallsBackToAnotherLocationWhenOriginalDisappears()
    {
        SecureQSettings settings("Hop-Manual-Removed", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        hop::LocationCatalog catalog(&servers, &settings); QVERIFY(catalog.select("a"));
        auto retry = timing(); retry.retryMs = 10;
        hop::SelectionSession session(&catalog, retry, nullptr);
        QSignalSpy attempts(&session, &hop::SelectionSession::attemptRequested);
        session.start(); session.transportConnected(session.token()); session.healthResult(session.token(), true);
        servers.removeServer("a"); QCOMPARE(catalog.selectedId(), QStringLiteral("b"));
        session.transportFailed(session.token(), "removed location"); session.suspended(session.token(), true);
        QTRY_COMPARE(session.state(), hop::SelectionSession::Backoff);
        QTest::qWait(80); QCOMPARE(attempts.count(), 1); QVERIFY(session.running());
        session.stop(); session.start();
        QCOMPARE(attempts.count(), 2); QCOMPARE(attempts.last().at(0).toString(), QStringLiteral("b"));
        session.stop();
    }
    void savedPoolWithEveryLocationExcludedFailsBeforeTransport()
    {
        SecureQSettings settings("Hop-Excluded-Pool", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        {
            hop::LocationCatalog catalog(&servers, &settings);
            catalog.toggleLocation("a"); catalog.toggleLocation("b");
            catalog.setExcluded("a", true); catalog.setExcluded("b", true);
            catalog.setMode(hop::LocationCatalog::Automatic);
        }
        hop::LocationCatalog restored(&servers, &settings);
        QCOMPARE(restored.priorities().size(), 2); QVERIFY(restored.candidateIds().isEmpty());
        hop::SelectionSession session(&restored, timing(), nullptr);
        QSignalSpy attempts(&session, &hop::SelectionSession::attemptRequested);
        session.start();
        QVERIFY(!session.running()); QCOMPARE(session.state(), hop::SelectionSession::Stopped);
        QVERIFY(!session.status().isEmpty()); QCOMPARE(attempts.count(), 0);
        // Ручной выбор не меняет сохранённые исключения автоматического режима.
        restored.setMode(hop::LocationCatalog::Manual); session.start();
        QCOMPARE(attempts.count(), 1); QVERIFY(session.running()); session.stop();
        QVERIFY(restored.marks().value("a").toMap().value("excluded").toBool());
    }
    void allLocationsExcludedFailBeforePreflightEvenWhileOffline()
    {
        SecureQSettings settings("Hop-Excluded-All", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        hop::LocationCatalog catalog(&servers, &settings);
        catalog.setMode(hop::LocationCatalog::AllFastest);
        catalog.setExcluded("a", true); catalog.setExcluded("b", true);
        hop::SelectionSession session(&catalog, timing(), nullptr);
        QSignalSpy checks(&session, &hop::SelectionSession::candidateChecksRequested);
        session.setNetworkOnline(false); session.start();
        QVERIFY(!session.running()); QVERIFY(!session.status().isEmpty()); QCOMPARE(checks.count(), 0);
    }
    void initialDeadlineSurvivesOfflineAndNetworkChanges()
    {
        SecureQSettings settings("Hop-Initial-Offline", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        hop::LocationCatalog catalog(&servers, &settings);
        auto limited = timing(); limited.startupMs = 100;
        hop::SelectionSession session(&catalog, limited, nullptr);
        QSignalSpy suspends(&session, &hop::SelectionSession::suspendRequested);
        session.start(); session.transportConnected(session.token());
        const auto old = session.token();
        session.healthResult(old, false, false);
        session.setNetworkOnline(false); session.setNetworkOnline(true); session.setNetworkOnline(false);
        QTRY_VERIFY_WITH_TIMEOUT(!session.running(), 1000);
        QCOMPARE(session.state(), hop::SelectionSession::Stopped);
        QVERIFY(!session.status().isEmpty()); QCOMPARE(suspends.count(), 1);
        session.healthResult(old, true); session.transportConnected(old);
        QCOMPARE(session.state(), hop::SelectionSession::Stopped);
    }
    void initialDeadlineCoversAllLocationsPreflight()
    {
        SecureQSettings settings("Hop-Initial-Selecting", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        hop::LocationCatalog catalog(&servers, &settings); catalog.setMode(hop::LocationCatalog::AllFastest);
        auto limited = timing(); limited.startupMs = 100;
        hop::SelectionSession session(&catalog, limited, nullptr);
        session.start(); const auto old = session.token();
        QCOMPARE(session.state(), hop::SelectionSession::Selecting);
        QTRY_VERIFY_WITH_TIMEOUT(!session.running(), 1000);
        session.candidatesMeasured(old, {});
        QCOMPARE(session.state(), hop::SelectionSession::Stopped);
    }
    void initialDeadlineDoesNotStopPreviouslyVerifiedSession()
    {
        SecureQSettings settings("Hop-Initial-Verified", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        hop::LocationCatalog catalog(&servers, &settings); catalog.setMode(hop::LocationCatalog::Manual);
        auto limited = timing(); limited.startupMs = 100;
        hop::SelectionSession session(&catalog, limited, nullptr);
        session.start(); session.transportConnected(session.token()); session.healthResult(session.token(), true);
        QCOMPARE(session.state(), hop::SelectionSession::Healthy);
        session.setNetworkOnline(false); QTest::qWait(150);
        QVERIFY(session.running()); session.stop();
    }
    void manualConnectionRequiresReplyAndInitialFailureIsBounded()
    {
        SecureQSettings settings("Hop-Initial-Reply", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        hop::LocationCatalog catalog(&servers, &settings);
        auto initial = timing(); initial.startupMs = 150;
        hop::SelectionSession session(&catalog, initial, nullptr); session.setLatencyPolicy(60000, 200);
        QSignalSpy suspends(&session, &hop::SelectionSession::suspendRequested);
        session.start(); session.transportConnected(session.token()); QCOMPARE(session.state(), hop::SelectionSession::Verifying);
        const auto token = session.token();
        session.healthResult(token, false); session.healthResult(token, false); session.healthResult(token, false);
        QVERIFY(session.running()); QCOMPARE(session.state(), hop::SelectionSession::Suspending);
        QCOMPARE(suspends.count(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!session.running(), 1000);
        QCOMPARE(session.state(), hop::SelectionSession::Stopped); QVERIFY(!session.status().isEmpty());
        QCOMPARE(suspends.count(), 2);
        session.suspended(session.token(), false); QTRY_COMPARE(suspends.count(), 3);
        session.suspended(session.token(), true);
        session.start(); session.transportConnected(session.token()); session.healthResult(session.token(), true, true, true, 250);
        QCOMPARE(session.state(), hop::SelectionSession::Healthy);
        session.stop();
    }
    void allLocationsUseThresholdAndStableOrder()
    {
        SecureQSettings settings("Hop-All-Ordered", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        hop::LocationCatalog catalog(&servers, &settings);
        catalog.setMode(hop::LocationCatalog::AllOrdered);
        QCOMPARE(catalog.candidateIds(), QStringList({"a", "b"}));
        hop::SelectionSession session(&catalog, timing(), nullptr); session.setLatencyPolicy(30, 200);
        QSignalSpy requests(&session, &hop::SelectionSession::candidateChecksRequested);
        QSignalSpy attempts(&session, &hop::SelectionSession::attemptRequested);
        QSignalSpy refresh(&session, &hop::SelectionSession::subscriptionRefreshRequested);
        session.start(); QCOMPARE(requests.count(), 1); QCOMPARE(attempts.count(), 0);
        session.candidatesMeasured(session.token(), {{"a", QVariantMap{{"state", "reachable"}, {"latencyMs", 201}}},
                                                    {"b", QVariantMap{{"state", "reachable"}, {"latencyMs", 200}}}});
        QCOMPARE(session.activeId(), QStringLiteral("b"));
        session.transportConnected(session.token()); session.healthResult(session.token(), true, true, true, 200);
        QCOMPARE(session.state(), hop::SelectionSession::Healthy);
        session.healthResult(session.token(), false); QTest::qWait(40); session.healthResult(session.token(), false);
        QCOMPARE(session.state(), hop::SelectionSession::Suspending);
        session.suspended(session.token(), true); QTRY_COMPARE(requests.count(), 2);
        session.candidatesMeasured(session.token(), {{"a", QVariantMap{{"state", "reachable"}, {"latencyMs", 100}}}});
        QCOMPARE(session.activeId(), QStringLiteral("a"));
        session.stop();
        catalog.toggleMembership("a"); QCOMPARE(catalog.candidateIds(), QStringList({"b"}));
        QVERIFY(!catalog.marks().value("a").toMap().value("automaticMember").toBool());
        catalog.toggleMembership("a"); QCOMPARE(catalog.candidateIds(), QStringList({"a", "b"}));
        // Исключение из общего списка не должно мешать последующему добавлению в обычный пул.
        catalog.toggleMembership("a");
        catalog.setMode(hop::LocationCatalog::Automatic);
        QVERIFY(catalog.candidateIds().isEmpty());
        QVERIFY(catalog.toggleMembership("a"));
        QCOMPARE(catalog.candidateIds(), QStringList({"a"}));
    }
    void fastestLocationRequiresVerifiedThresholdAndIgnoresLateChecks()
    {
        SecureQSettings settings("Hop-All-Fastest", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        hop::LocationCatalog catalog(&servers, &settings); catalog.setMode(hop::LocationCatalog::AllFastest);
        hop::SelectionSession session(&catalog, timing(), nullptr); session.setLatencyPolicy(30, 200);
        session.start(); const auto old = session.token();
        session.candidatesMeasured(old, {{"a", QVariantMap{{"state", "reachable"}, {"latencyMs", 180}}},
                                        {"b", QVariantMap{{"state", "reachable"}, {"latencyMs", 70}}}});
        QCOMPARE(session.activeId(), QStringLiteral("b"));
        session.transportConnected(session.token()); session.healthResult(session.token(), true, true, true, 250);
        QCOMPARE(session.state(), hop::SelectionSession::Suspending);
        session.suspended(session.token(), true); QTRY_COMPARE(session.state(), hop::SelectionSession::Selecting);
        session.candidatesMeasured(old, {}); QCOMPARE(session.state(), hop::SelectionSession::Selecting);
        session.candidatesMeasured(session.token(), {{"a", QVariantMap{{"state", "reachable"}, {"latencyMs", 100}}}});
        QCOMPARE(session.activeId(), QStringLiteral("a"));
        session.stop();
        hop::LocationCatalog restored(&servers, &settings); QCOMPARE(restored.mode(), int(hop::LocationCatalog::AllFastest));
    }
    void latencyWindowResetsAtThresholdAndRefreshPrecedesCleanup()
    {
        SecureQSettings settings("Hop-Latency-Window", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        hop::LocationCatalog catalog(&servers, &settings);
        catalog.toggleLocation("a"); catalog.toggleLocation("b"); catalog.setMode(hop::LocationCatalog::Automatic);
        hop::SelectionSession session(&catalog, timing(), nullptr);
        session.setLatencyPolicy(80, 200); session.setRefreshBeforeFailover(true);
        QSignalSpy attempts(&session, &hop::SelectionSession::attemptRequested);
        QSignalSpy refresh(&session, &hop::SelectionSession::subscriptionRefreshRequested);
        QSignalSpy suspends(&session, &hop::SelectionSession::suspendRequested);
        session.start(); session.transportConnected(session.token());
        const auto first = session.token();
        session.healthResult(first, true, true, true, 201);
        QTest::qWait(90);
        session.healthResult(first, true, true, true, 200);
        QCOMPARE(refresh.count(), 0);
        session.healthResult(first, true, true, true, 300);
        QCOMPARE(refresh.count(), 0);
        QTest::qWait(90);
        session.healthResult(first, true, true, true, 300);
        QCOMPARE(session.state(), hop::SelectionSession::Refreshing);
        QCOMPARE(refresh.count(), 1); QCOMPARE(suspends.count(), 0);
        session.healthResult(first, true, true, true, 10);
        QCOMPARE(session.state(), hop::SelectionSession::Refreshing);
        session.subscriptionRefreshed(first, true);
        QCOMPARE(suspends.count(), 1);
        session.suspended(session.token(), true);
        QTRY_COMPARE(attempts.count(), 2);
        QCOMPARE(session.activeId(), QStringLiteral("a"));
        session.transportConnected(session.token());
        session.healthResult(session.token(), false);
        QTest::qWait(90); session.healthResult(session.token(), false);
        session.healthResult(session.token(), false);
        QCOMPARE(refresh.count(), 1);
        QCOMPARE(session.state(), hop::SelectionSession::Suspending);
        session.suspended(session.token(), true);
        QTRY_COMPARE(attempts.count(), 3);
        QCOMPARE(session.activeId(), QStringLiteral("b"));
        session.stop(); session.subscriptionRefreshed(first, true);
        QCOMPARE(session.state(), hop::SelectionSession::Stopped);
    }
    void refreshFailureContinuesPoolAndOfflineResetsLatencyWindow()
    {
        SecureQSettings settings("Hop-Refresh-Failure", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        hop::LocationCatalog catalog(&servers, &settings);
        catalog.toggleLocation("a"); catalog.toggleLocation("b"); catalog.setMode(hop::LocationCatalog::Automatic);
        hop::SelectionSession session(&catalog, timing(), nullptr);
        session.setLatencyPolicy(50, 200); session.setRefreshBeforeFailover(true);
        QSignalSpy attempts(&session, &hop::SelectionSession::attemptRequested);
        QSignalSpy refresh(&session, &hop::SelectionSession::subscriptionRefreshRequested);
        session.start(); session.transportConnected(session.token());
        session.healthResult(session.token(), true, true, true, 100);
        session.healthResult(session.token(), false); QTest::qWait(60);
        session.healthResult(session.token(), false, false);
        session.healthResult(session.token(), false);
        QCOMPARE(refresh.count(), 0);
        QTest::qWait(60); session.healthResult(session.token(), false);
        QCOMPARE(refresh.count(), 1);
        session.subscriptionRefreshed(session.token(), false);
        session.suspended(session.token(), false);
        QCOMPARE(refresh.count(), 1);
        session.suspended(session.token(), true);
        QCOMPARE(session.state(), hop::SelectionSession::Refreshing);
        QCOMPARE(refresh.count(), 2);
        session.subscriptionRefreshed(session.token(), false);
        QTRY_COMPARE(attempts.count(), 2);
        QCOMPARE(session.activeId(), QStringLiteral("b"));
        session.stop();
    }
    void detachedRefreshSuccessRetriesCurrentWithoutSecondCleanup()
    {
        SecureQSettings settings("Hop-Detached-Refresh", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        hop::LocationCatalog catalog(&servers, &settings);
        catalog.toggleLocation("a"); catalog.toggleLocation("b"); catalog.setMode(hop::LocationCatalog::Automatic);
        hop::SelectionSession session(&catalog, timing(), nullptr); session.setRefreshBeforeFailover(true);
        QSignalSpy attempts(&session, &hop::SelectionSession::attemptRequested);
        QSignalSpy refresh(&session, &hop::SelectionSession::subscriptionRefreshRequested);
        QSignalSpy suspends(&session, &hop::SelectionSession::suspendRequested);
        session.start(); session.transportConnected(session.token()); session.healthResult(session.token(), true);
        for (int index = 0; index < 3; ++index) { session.healthResult(session.token(), false); }
        const auto initial = session.token();
        session.subscriptionRefreshed(initial, false);
        QCOMPARE(suspends.count(), 1);
        session.suspended(session.token(), true);
        QCOMPARE(refresh.count(), 2); QCOMPARE(session.state(), hop::SelectionSession::Refreshing);
        session.subscriptionRefreshed(initial, true);
        QCOMPARE(session.state(), hop::SelectionSession::Refreshing);
        session.subscriptionRefreshed(session.token(), true);
        QTRY_COMPARE(attempts.count(), 2); QCOMPARE(session.activeId(), QStringLiteral("a"));
        QCOMPARE(suspends.count(), 1);
        session.stop();
    }
    void failureSwitchesOnlyAfterCleanupAndIgnoresOldResults()
    {
        SecureQSettings settings("Hop-Selection-" + QUuid::createUuid().toString(), "memory", nullptr, true, false);
        SecureServersRepository servers(&settings);
        populate(servers);
        hop::LocationCatalog catalog(&servers, &settings);
        catalog.toggleLocation("a"); catalog.toggleLocation("b"); catalog.setMode(hop::LocationCatalog::Automatic);
        hop::SelectionSession session(&catalog, timing(), nullptr);
        QSignalSpy attempts(&session, &hop::SelectionSession::attemptRequested);
        QSignalSpy suspends(&session, &hop::SelectionSession::suspendRequested);
        QSignalSpy disconnects(&session, &hop::SelectionSession::disconnectRequested);
        session.start();
        QCOMPARE(attempts.count(), 1);
        QCOMPARE(attempts.first().at(0).toString(), QStringLiteral("a"));
        const auto first = session.token();
        session.transportConnected(first);
        session.healthResult(first, true);
        QCOMPARE(session.state(), hop::SelectionSession::Healthy);
        session.healthResult(first, false); session.healthResult(first, false);
        QCOMPARE(suspends.count(), 0);
        session.healthResult(first, false);
        QCOMPARE(suspends.count(), 1);
        QCOMPARE(attempts.count(), 1);
        session.healthResult(first, true);
        session.transportConnected(first);
        QCOMPARE(session.state(), hop::SelectionSession::Suspending);
        const auto cleanupToken = session.token();
        session.suspended(cleanupToken, false);
        QCOMPARE(attempts.count(), 1);
        session.suspended(cleanupToken, true);
        QTRY_COMPARE(attempts.count(), 2);
        QCOMPARE(attempts.last().at(0).toString(), QStringLiteral("b"));
        QCOMPARE(disconnects.count(), 0);
        session.stop();
        QCOMPARE(disconnects.count(), 1);
        session.transportConnected(attempts.last().at(1).toString());
        QCOMPARE(session.state(), hop::SelectionSession::Stopped);
    }
    void offlineDoesNotRotateAndManualRetriesOnlyAfterCleanup()
    {
        SecureQSettings settings("Hop-Selection-" + QUuid::createUuid().toString(), "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        hop::LocationCatalog catalog(&servers, &settings);
        catalog.toggleLocation("a"); catalog.toggleLocation("b"); catalog.setMode(hop::LocationCatalog::Automatic);
        hop::SelectionSession session(&catalog, timing(), nullptr);
        QSignalSpy attempts(&session, &hop::SelectionSession::attemptRequested);
        QSignalSpy suspends(&session, &hop::SelectionSession::suspendRequested);
        session.start(); session.transportConnected(session.token());
        for (int index = 0; index < 6; ++index) { session.healthResult(session.token(), false, false); }
        QCOMPARE(attempts.count(), 1); QCOMPARE(suspends.count(), 0);
        session.healthResult(session.token(), true);
        catalog.setMode(hop::LocationCatalog::Manual);
        for (int index = 0; index < 6; ++index) { session.healthResult(session.token(), false); }
        QCOMPARE(attempts.count(), 1); QCOMPARE(suspends.count(), 1);
        QCOMPARE(session.state(), hop::SelectionSession::Suspending);
        session.suspended(session.token(), false);
        QCOMPARE(attempts.count(), 1);
        session.suspended(session.token(), true);
        QTRY_COMPARE(session.state(), hop::SelectionSession::Backoff);
        QVERIFY(session.running());
        QVERIFY(!session.status().isEmpty());
    }
    void manualTransportFailureRetriesSameLocationAndStopCancelsRetry()
    {
        SecureQSettings settings("Hop-Selection-" + QUuid::createUuid().toString(), "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        hop::LocationCatalog catalog(&servers, &settings);
        catalog.select("a"); catalog.toggleLocation("b"); catalog.setMode(hop::LocationCatalog::Manual);
        auto retryTiming = timing(); retryTiming.retryMs = 40;
        hop::SelectionSession session(&catalog, retryTiming, nullptr);
        QSignalSpy attempts(&session, &hop::SelectionSession::attemptRequested);
        QSignalSpy disconnects(&session, &hop::SelectionSession::disconnectRequested);
        session.start(); const auto first = session.token();
        QVERIFY(catalog.select("b"));
        session.transportFailed(first, "test transport failure");
        QCOMPARE(session.state(), hop::SelectionSession::Suspending);
        session.suspended(session.token(), true);
        QTRY_COMPARE(attempts.count(), 2);
        QCOMPARE(attempts.last().at(0).toString(), QStringLiteral("a"));
        QVERIFY(session.token() != first);
        session.transportConnected(session.token());
        QCOMPARE(session.state(), hop::SelectionSession::Verifying);
        session.healthResult(first, true);
        QCOMPARE(session.state(), hop::SelectionSession::Verifying);
        session.healthResult(session.token(), true);
        QCOMPARE(session.state(), hop::SelectionSession::Healthy);
        session.transportFailed(session.token(), "second failure");
        session.suspended(session.token(), true);
        QTRY_COMPARE(session.state(), hop::SelectionSession::Backoff);
        session.stop(); QTest::qWait(100);
        QCOMPARE(attempts.count(), 2);
        QCOMPARE(disconnects.count(), 1);
        QCOMPARE(session.state(), hop::SelectionSession::Stopped);
    }
    void networkRecoveryKeepsLocationAndWaitsForCleanup()
    {
        SecureQSettings settings("Hop-Selection-" + QUuid::createUuid().toString(), "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        hop::LocationCatalog catalog(&servers, &settings);
        catalog.toggleLocation("a"); catalog.toggleLocation("b"); catalog.setMode(hop::LocationCatalog::Automatic);
        hop::SelectionSession session(&catalog, timing(), nullptr);
        QSignalSpy attempts(&session, &hop::SelectionSession::attemptRequested);
        session.setNetworkOnline(false); session.start();
        QCOMPARE(attempts.count(), 0);
        session.setNetworkOnline(true);
        QCOMPARE(attempts.count(), 1);
        session.transportConnected(session.token()); session.healthResult(session.token(), true);
        catalog.movePriority(1, 0);
        session.restartCurrent();
        QCOMPARE(session.state(), hop::SelectionSession::Suspending);
        QCOMPARE(attempts.count(), 1);
        session.setNetworkOnline(false);
        session.suspended(session.token(), true);
        QTRY_COMPARE(session.state(), hop::SelectionSession::Backoff);
        QCOMPARE(attempts.count(), 1);
        session.setNetworkOnline(true);
        QCOMPARE(attempts.count(), 2);
        QCOMPARE(session.activeId(), QStringLiteral("a"));
    }

    void locationExcludedDuringPreparationIsNotAccepted()
    {
        SecureQSettings settings("Hop-Selection-" + QUuid::createUuid().toString(), "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        hop::LocationCatalog catalog(&servers, &settings);
        catalog.toggleLocation("a"); catalog.toggleLocation("b"); catalog.setMode(hop::LocationCatalog::Automatic);
        hop::SelectionSession session(&catalog, timing(), nullptr);
        QSignalSpy attempts(&session, &hop::SelectionSession::attemptRequested);
        QSignalSpy suspends(&session, &hop::SelectionSession::suspendRequested);
        session.start();
        const auto old = session.token();
        catalog.setExcluded("a", true);
        session.transportConnected(old);
        QCOMPARE(session.state(), hop::SelectionSession::Suspending);
        QCOMPARE(suspends.count(), 1);
        session.healthResult(old, true);
        QCOMPARE(session.state(), hop::SelectionSession::Suspending);
        session.suspended(session.token(), true);
        QTRY_COMPARE(attempts.count(), 2);
        QCOMPARE(session.activeId(), QStringLiteral("b"));
    }

    void poolEditsKeepWorkingLocationUnlessExcluded()
    {
        SecureQSettings settings("Hop-Selection-" + QUuid::createUuid().toString(), "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); populate(servers);
        hop::LocationCatalog catalog(&servers, &settings);
        catalog.toggleLocation("a"); catalog.toggleLocation("b"); catalog.setMode(hop::LocationCatalog::Automatic);
        hop::SelectionSession session(&catalog, timing(), nullptr);
        QSignalSpy suspends(&session, &hop::SelectionSession::suspendRequested);
        session.start(); session.transportConnected(session.token()); session.healthResult(session.token(), true);
        catalog.movePriority(1, 0);
        QCOMPARE(suspends.count(), 0);
        catalog.setExcluded("a", true);
        QCOMPARE(suspends.count(), 1);
    }
};
QTEST_GUILESS_MAIN(TestSelectionSession)
#include "testSelectionSession.moc"

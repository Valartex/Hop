#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDateTime>
#include <QJsonDocument>
#include "core/routing/routeDatasetsController.h"
#include "core/routing/trafficPolicyController.h"

class RouteDatasetsTest : public QObject {
    Q_OBJECT
    QString helper() const { return QCoreApplication::applicationDirPath() + QStringLiteral("/hop_fake_route_fetcher.exe"); }
    QByteArray cached(const hop::RouteDatasetsController &datasets, const QString &kind) const {
        QFile file(datasets.cachedPath(kind)); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
private slots:
    void ruleOrderIsDraftUntilOneCheckedApply() {
        struct FailureSettings : SecureQSettings {
            FailureSettings() : SecureQSettings("Hop-Order-Test", "memory", nullptr, true, false) {}
            bool fail = false;
            bool setValueChecked(const QString &key, const QVariant &value) override { return !fail && SecureQSettings::setValueChecked(key, value); }
        } settings;
        QTemporaryDir root; hop::RouteDatasetsController datasets(nullptr, root.path(), helper(), &settings);
        hop::TrafficPolicyController controller(&settings, &datasets); QTRY_VERIFY(controller.ready());
        for (const auto &ip : { "192.168.1.10/32", "192.168.1.0/24", "10.0.0.0/8" }) {
            QVERIFY(controller.addRule("ip", ip, "direct")); QTRY_VERIFY(!controller.busy());
        }
        const auto original = controller.rules(); const auto snapshot = controller.snapshot().toVariantMap();
        const auto saved = settings.value("Conf/hopTrafficPolicy").toByteArray();
        QSignalSpy published(&controller, &hop::TrafficPolicyController::policyChanged);
        QVERIFY(controller.moveRule(2, -1)); QVERIFY(controller.moveRule(1, -1));
        QVERIFY(controller.orderPending()); QVERIFY(!controller.busy());
        QCOMPARE(controller.rules().first().toMap().value("value").toString(), QStringLiteral("10.0.0.0/8"));
        QCOMPARE(controller.snapshot().toVariantMap(), snapshot);
        QCOMPARE(settings.value("Conf/hopTrafficPolicy").toByteArray(), saved); QCOMPARE(published.count(), 0);
        QVERIFY(!controller.addRule("ip", "1.0.0.1/32", "direct"));
        QVERIFY(!controller.setEnabled(false)); QVERIFY(!controller.removeRule(0));
        controller.discardOrder(); QVERIFY(!controller.orderPending()); QCOMPARE(controller.rules(), original);
        QCOMPARE(published.count(), 0);
        QVERIFY(controller.moveRule(0, 1)); QVERIFY(controller.moveRule(1, -1));
        QVERIFY(!controller.orderPending()); QCOMPARE(controller.rules(), original); QCOMPARE(published.count(), 0);
        QVERIFY(controller.moveRule(2, -1)); QVERIFY(controller.moveRule(1, -1));
        const auto reordered = controller.rules();
        settings.fail = true;
        QVERIFY(controller.applyOrder()); QTRY_VERIFY(!controller.busy());
        QVERIFY(controller.orderPending()); QVERIFY(!controller.lastError().isEmpty());
        QCOMPARE(controller.snapshot().toVariantMap(), snapshot); QCOMPARE(controller.rules(), reordered);
        QCOMPARE(settings.value("Conf/hopTrafficPolicy").toByteArray(), saved); QCOMPARE(published.count(), 0);
        settings.fail = false;
        QVERIFY(controller.applyOrder()); QTRY_VERIFY(!controller.busy());
        QVERIFY(!controller.orderPending()); QVERIFY(controller.lastError().isEmpty());
        QCOMPARE(controller.rules(), reordered); QCOMPARE(published.count(), 1);
        QVERIFY(controller.snapshot().toVariantMap() != snapshot);
        QVERIFY(!controller.applyOrder()); QCOMPARE(published.count(), 1);
        hop::RouteDatasetsController reopenedDatasets(nullptr, root.path(), helper(), &settings);
        hop::TrafficPolicyController reopened(&settings, &reopenedDatasets); QTRY_VERIFY(reopened.ready());
        QCOMPARE(reopened.rules(), reordered); QCOMPARE(reopened.snapshot(), controller.snapshot());
    }
    void invalidRegexKeepsThePublishedAndSavedPolicy() {
        QTemporaryDir root; SecureQSettings settings("Hop-Regex-Test", "memory", nullptr, true, false);
        hop::RouteDatasetsController datasets(nullptr, root.path(), helper(), &settings);
        hop::TrafficPolicyController policy(&settings, &datasets); QTRY_VERIFY(policy.ready());
        QVERIFY(policy.addRule("domain", "regexp:^example\\.com$", "block")); QTRY_VERIFY(!policy.busy());
        QVERIFY2(policy.lastError().isEmpty(), qPrintable(policy.lastError()));
        const auto before = policy.snapshot(); const auto saved = settings.value("Conf/hopTrafficPolicy").toByteArray();
        QVERIFY(policy.addRule("domain", "regexp:(?=unsupported)", "direct")); QTRY_VERIFY(!policy.busy());
        QVERIFY(!policy.lastError().isEmpty()); QCOMPARE(policy.snapshot(), before);
        QCOMPARE(settings.value("Conf/hopTrafficPolicy").toByteArray(), saved);
        hop::TrafficPolicyController reopened(&settings, &datasets); QTRY_VERIFY(reopened.ready());
        QCOMPARE(reopened.snapshot(), before);
    }
    void policyPublishesOnlyAfterCompileAndCheckedSave() {
        struct FailureSettings : SecureQSettings {
            FailureSettings() : SecureQSettings("Hop-Policy-Test", "memory", nullptr, true, false) {}
            bool fail = false;
            bool setValueChecked(const QString &key, const QVariant &value) override { return !fail && SecureQSettings::setValueChecked(key, value); }
        } settings;
        QTemporaryDir root; hop::RouteDatasetsController datasets(nullptr, root.path(), helper(), &settings);
        hop::TrafficPolicyController policy(&settings, &datasets);
        QTRY_VERIFY(policy.ready()); QVERIFY(policy.snapshot().isEmpty());
        QVERIFY(policy.addRule("domain", "example.com", "block")); QTRY_VERIFY(!policy.busy());
        QVERIFY2(policy.lastError().isEmpty(), qPrintable(policy.lastError()));
        const auto before = policy.snapshot(); QVERIFY(!before.isEmpty());
        QVERIFY(policy.addRule("ip", "invalid", "direct")); QTRY_VERIFY(!policy.busy());
        QVERIFY(!policy.lastError().isEmpty()); QCOMPARE(policy.snapshot(), before); QCOMPARE(policy.rules().size(), 1);
        settings.fail = true;
        QVERIFY(policy.setDefaultTarget("direct")); QTRY_VERIFY(!policy.busy());
        QVERIFY(!policy.lastError().isEmpty()); QCOMPARE(policy.snapshot(), before); QCOMPARE(policy.defaultTarget(), QStringLiteral("vpn"));
        settings.fail = false;
        hop::TrafficPolicyController reopened(&settings, &datasets); QTRY_VERIFY(reopened.ready());
        QCOMPARE(reopened.snapshot(), before);
    }
    void corruptSavedPolicyDoesNotAllowConnection() {
        QTemporaryDir root; SecureQSettings settings("Hop-Policy-Test", "memory", nullptr, true, false);
        settings.setValue("Conf/hopTrafficPolicy", QByteArrayLiteral("broken"));
        hop::RouteDatasetsController datasets(nullptr, root.path(), helper(), &settings);
        hop::TrafficPolicyController policy(&settings, &datasets); QTRY_VERIFY(!policy.busy());
        QVERIFY(!policy.ready()); QVERIFY(policy.enabled()); QVERIFY(!policy.lastError().isEmpty());
        QVERIFY(policy.setEnabled(false)); QTRY_VERIFY(!policy.busy()); QVERIFY(policy.ready()); QVERIFY(policy.snapshot().isEmpty());
    }
    void migrationIsSavedAndDoesNotOverwriteAnExistingPolicy() {
        QTemporaryDir root; SecureQSettings settings("Hop-Policy-Test", "memory", nullptr, true, false);
        hop::RouteDatasetsController datasets(nullptr, root.path(), helper(), &settings);
        const QJsonObject legacy { { "default", "vpn" }, { "rules", QJsonArray { QJsonObject {
            { "kind", "domain" }, { "value", "example.com" }, { "target", "direct" } } } } };
        hop::TrafficPolicyController migrated(&settings, &datasets, nullptr, legacy); QTRY_VERIFY(migrated.ready());
        QVERIFY(migrated.enabled()); QVERIFY(!migrated.snapshot().isEmpty());
        QVERIFY(!settings.value("Conf/hopTrafficPolicy").toByteArray().isEmpty());
        const QJsonObject other { { "default", "block" }, { "rules", QJsonArray() } };
        hop::TrafficPolicyController reopened(&settings, &datasets, nullptr, other); QTRY_VERIFY(reopened.ready());
        QCOMPARE(reopened.snapshot(), migrated.snapshot());
    }
    void datasetCannotRemoveAnActiveCategory() {
        QTemporaryDir root; SecureQSettings settings("Hop-Policy-Test", "memory", nullptr, true, false);
        hop::RouteDatasetsController datasets(nullptr, root.path(), helper(), &settings);
        hop::TrafficPolicyController policy(&settings, &datasets); QTRY_VERIFY(policy.ready());
        QVERIFY(datasets.download(QUrl("https://fixture.test/valid"), "geosite")); QTRY_VERIFY(!datasets.isBusy());
        QTRY_VERIFY(!policy.busy());
        QVERIFY(policy.addRule("geosite", "RU", "block")); QTRY_VERIFY(!policy.busy());
        QVERIFY2(policy.lastError().isEmpty(), qPrintable(policy.lastError()));
        const auto before = policy.snapshot(); const auto body = cached(datasets, "geosite");
        QVERIFY(datasets.download(QUrl("https://fixture.test/other"), "geosite")); QTRY_VERIFY(!datasets.isBusy());
        QVERIFY(!datasets.lastError().isEmpty()); QCOMPARE(cached(datasets, "geosite"), body); QCOMPARE(policy.snapshot(), before);
        QVERIFY(datasets.download(QUrl("https://fixture.test/bad-regex"), "geosite")); QTRY_VERIFY(!datasets.isBusy());
        QVERIFY(!datasets.lastError().isEmpty()); QCOMPARE(cached(datasets, "geosite"), body); QCOMPARE(policy.snapshot(), before);
    }
    void updateAnd304KeepVerifiedData() {
        QTemporaryDir root; QVERIFY(root.isValid());
        SecureQSettings settings("Hop-Routes-Test", "memory", nullptr, true, false);
        hop::RouteDatasetsController datasets(nullptr, root.path(), helper(), &settings);
        QSignalSpy updated(&datasets, &hop::RouteDatasetsController::datasetUpdated);
        QVERIFY(datasets.download(QUrl("https://fixture.test/valid"), "geosite"));
        QTRY_VERIFY_WITH_TIMEOUT(!datasets.isBusy(), 10000);
        QVERIFY2(datasets.lastError().isEmpty(), qPrintable(datasets.lastError()));
        QCOMPARE(updated.size(), 1); const auto hash = datasets.sha256(); const auto body = cached(datasets, "geosite");
        QVERIFY(!hash.isEmpty()); QVERIFY(!body.isEmpty());
        const auto source = datasets.sources().value("geosite").toMap();
        QCOMPARE(source.value("auto").toBool(), true);
        QVERIFY(source.value("next").toLongLong() > QDateTime::currentSecsSinceEpoch());
        QVERIFY(datasets.download(QUrl("https://fixture.test/valid"), "geosite"));
        QTRY_VERIFY_WITH_TIMEOUT(!datasets.isBusy(), 10000);
        QVERIFY2(datasets.lastError().isEmpty(), qPrintable(datasets.lastError()));
        QCOMPARE(updated.size(), 1); QCOMPARE(datasets.sha256(), hash); QCOMPARE(cached(datasets, "geosite"), body);
        const auto saved = QJsonDocument::fromJson(settings.value("Conf/hopRouteSources").toByteArray()).object();
        QCOMPARE(saved.value("geosite").toObject().value("sha256").toString(), hash);
    }
    void corruptAndTruncatedDoNotReplaceSnapshot() {
        QTemporaryDir root; SecureQSettings settings("Hop-Routes-Test", "memory", nullptr, true, false);
        hop::RouteDatasetsController datasets(nullptr, root.path(), helper(), &settings);
        QVERIFY(datasets.download(QUrl("https://fixture.test/valid"), "geosite"));
        QTRY_VERIFY_WITH_TIMEOUT(!datasets.isBusy(), 10000);
        const auto body = cached(datasets, "geosite"); const auto hash = datasets.sha256(); const auto source = datasets.sources();
        for (const auto &path : { "/bad", "/truncated", "/error" }) {
            QVERIFY(datasets.download(QUrl(QStringLiteral("https://fixture.test") + QLatin1String(path)), "geosite"));
            QTRY_VERIFY_WITH_TIMEOUT(!datasets.isBusy(), 10000);
            QVERIFY(!datasets.lastError().isEmpty()); QCOMPARE(cached(datasets, "geosite"), body);
            QCOMPARE(datasets.sha256(), hash); QCOMPARE(datasets.sources(), source);
        }
    }
    void localImportCancelsRemoteRefresh() {
        QTemporaryDir root; hop::RouteDatasetsController datasets(nullptr, root.path(), helper());
        QVERIFY(datasets.download(QUrl("https://fixture.test/valid"), "geosite")); QTRY_VERIFY(!datasets.isBusy());
        const auto file = QUrl::fromLocalFile(datasets.cachedPath("geosite"));
        QVERIFY(datasets.importFile(file, "geosite")); QTRY_VERIFY(!datasets.isBusy());
        QVERIFY(datasets.sources().isEmpty()); QVERIFY(datasets.lastError().isEmpty());
    }
    void persistedDueSetRefreshesAndCanBeDisabled() {
        QTemporaryDir root; SecureQSettings settings("Hop-Routes-Test", "memory", nullptr, true, false);
        settings.setValue("Conf/hopRouteSources", QByteArrayLiteral("{\"geosite\":{\"url\":\"https://fixture.test/valid\",\"next\":0,\"auto\":true}}"));
        hop::RouteDatasetsController datasets(nullptr, root.path(), helper(), &settings);
        datasets.refreshDue(); QTRY_VERIFY(!datasets.isBusy()); QVERIFY(datasets.lastError().isEmpty());
        QVERIFY(datasets.byteCount() > 0); QVERIFY(datasets.setAutoRefresh("geosite", false));
        QCOMPARE(datasets.sources().value("geosite").toMap().value("auto").toBool(), false);
        QVERIFY(!datasets.setAutoRefresh("unknown", true));
    }
    void protectedSourceWriteFailureIsVisible() {
        struct FailureSettings : SecureQSettings {
            FailureSettings() : SecureQSettings("Hop-Routes-Test", "memory", nullptr, true, false) {}
            bool setValueChecked(const QString &, const QVariant &) override { return false; }
        } settings;
        QTemporaryDir root; hop::RouteDatasetsController datasets(nullptr, root.path(), helper(), &settings);
        QVERIFY(datasets.download(QUrl("https://fixture.test/valid"), "geosite")); QTRY_VERIFY(!datasets.isBusy());
        QVERIFY(!datasets.lastError().isEmpty()); QVERIFY(datasets.sources().isEmpty());
        QVERIFY(datasets.byteCount() > 0); QVERIFY(!cached(datasets, "geosite").isEmpty());
    }
};
QTEST_GUILESS_MAIN(RouteDatasetsTest)
#include "testRouteDatasets.moc"

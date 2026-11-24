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
    void sourceQueueUpdatesDifferentSourcesIndependently() {
        QTemporaryDir root; SecureQSettings settings("Hop-Source-Queue", "memory", nullptr, true, false);
        hop::RouteDatasetsController datasets(nullptr, root.path(), helper(), &settings);
        const QJsonArray rules {
            QJsonObject { { "kind", "geosite-source" }, { "value", "https://fixture.test/multi" }, { "target", "direct" } },
            QJsonObject { { "kind", "geosite-source" }, { "value", "https://fixture.test/other" }, { "target", "block" } }
        };
        QSignalSpy updated(&datasets, &hop::RouteDatasetsController::datasetUpdated);
        datasets.setRuleSources(rules);
        QVERIFY(datasets.sourceState("geosite", "https://fixture.test/multi").value("updating").toBool());
        QVERIFY(datasets.sourceState("geosite", "https://fixture.test/other").value("updating").toBool());
        QTRY_COMPARE(updated.count(), 2);
        for (const auto &url : {"https://fixture.test/multi", "https://fixture.test/other"}) {
            QVERIFY(QFile::exists(datasets.sourcePath("geosite", url)));
            QVERIFY(!datasets.sourceState("geosite", url).value("updated").toString().isEmpty());
            QVERIFY(!datasets.sourceState("geosite", url).value("updating").toBool());
        }
    }

    void source304RetainsDataAndDoesNotRepublishPolicy() {
        QTemporaryDir root; SecureQSettings settings("Hop-Source-304", "memory", nullptr, true, false);
        hop::RouteDatasetsController datasets(nullptr, root.path(), helper(), &settings);
        hop::TrafficPolicyController policy(&settings, &datasets); QTRY_VERIFY(policy.ready());
        QVERIFY(policy.addRule("geosite-source", "https://fixture.test/valid", "direct"));
        QTRY_VERIFY(policy.ready() && !policy.busy() && !datasets.isBusy());
        const auto snapshot = policy.snapshot();
        const auto state = datasets.sourceState("geosite", "https://fixture.test/valid");
        QVERIFY(!state.value("etag").toString().isEmpty());
        QSignalSpy published(&policy, &hop::TrafficPolicyController::policyChanged);
        QVERIFY(policy.refreshRule(0)); QTRY_VERIFY(!policy.rules().first().toMap().value("updating").toBool());
        QCOMPARE(policy.snapshot(), snapshot); QCOMPARE(published.count(), 0);
        QCOMPARE(datasets.sourceState("geosite", "https://fixture.test/valid").value("sha256"), state.value("sha256"));
        QVERIFY(policy.rules().first().toMap().value("sourceError").toString().isEmpty());
    }

    void failedSourceRegistryWriteCanBeRetriedFromTheRetainedRule() {
        struct FailureSettings : SecureQSettings {
            FailureSettings() : SecureQSettings("Hop-Source-Registry", "memory", nullptr, true, false) {}
            bool fail = false;
            bool setValueChecked(const QString &key, const QVariant &value) override {
                return !(fail && key == "Conf/hopRouteSources") && SecureQSettings::setValueChecked(key, value);
            }
        } settings;
        QTemporaryDir root; hop::RouteDatasetsController datasets(nullptr, root.path(), helper(), &settings);
        hop::TrafficPolicyController policy(&settings, &datasets); QTRY_VERIFY(policy.ready());
        settings.fail = true;
        QVERIFY(policy.addRule("geosite-source", "https://fixture.test/valid", "direct"));
        QTRY_VERIFY(!policy.busy()); QCOMPARE(policy.rules().size(), 1);
        QVERIFY(!policy.rules().first().toMap().value("sourceError").toString().isEmpty());
        settings.fail = false;
        QVERIFY(policy.refreshRule(0)); QTRY_VERIFY(policy.ready() && !policy.busy() && !datasets.isBusy());
        QVERIFY(policy.rules().first().toMap().value("sourceError").toString().isEmpty());
        QVERIFY(!policy.rules().first().toMap().value("updated").toString().isEmpty());
    }

    void defaultDatasetsAreUsedAsWholeSources() {
        const auto input = qEnvironmentVariable("HOP_SOURCE_DATASET_FIXTURE");
        if (input.isEmpty()) { QSKIP("Public default DAT fixture was not provided"); }
        QTemporaryDir root; SecureQSettings settings("Hop-Real-Source", "memory", nullptr, true, false);
        hop::RouteDatasetsController datasets(nullptr, root.path(), helper(), &settings);
        hop::TrafficPolicyController policy(&settings, &datasets); QTRY_VERIFY(policy.ready());
        for (const auto &kind : {QStringLiteral("geoip"), QStringLiteral("geosite")}) {
            QVERIFY(policy.addRule(kind + "-source", QDir(input).filePath(kind + ".dat"), "direct"));
            QTRY_VERIFY_WITH_TIMEOUT(policy.ready() && !policy.busy() && !datasets.isBusy(), 30000);
            QVERIFY2(policy.lastError().isEmpty(), qPrintable(policy.lastError()));
        }
        const auto compiled = policy.snapshot().value("rules").toArray();
        QCOMPARE(compiled.size(), 2);
        QVERIFY(compiled[0].toObject().value("ip").toArray().size() > 1000);
        QVERIFY(compiled[1].toObject().value("domain").toArray().size() > 1000);
    }

    void automaticUpdatesOnlyRefreshEnabledSourceRules() {
        QTemporaryDir root; SecureQSettings settings("Hop-Source-Schedule", "memory", nullptr, true, false);
        {
            hop::RouteDatasetsController datasets(nullptr, root.path(), helper(), &settings);
            hop::TrafficPolicyController policy(&settings, &datasets); QTRY_VERIFY(policy.ready());
            QVERIFY(policy.addRule("geosite-source", "https://fixture.test/multi", "direct"));
            QTRY_VERIFY(policy.ready() && !policy.busy() && !datasets.isBusy());
            QVERIFY(policy.setRuleAutoUpdate(0, false));
            QVERIFY(policy.addRule("geosite-source", "https://fixture.test/other", "block"));
            QTRY_VERIFY(policy.ready() && !policy.busy() && !datasets.isBusy());
        }
        auto sources = QJsonDocument::fromJson(settings.value("Conf/hopRouteSources").toByteArray()).object();
        for (const auto &key : sources.keys()) {
            auto source = sources.value(key).toObject(); source.insert("next", 0); source.insert("updated", "2000-01-01T00:00:00Z"); sources.insert(key, source);
        }
        settings.setValue("Conf/hopRouteSources", QJsonDocument(sources).toJson(QJsonDocument::Compact));
        hop::RouteDatasetsController datasets(nullptr, root.path(), helper(), &settings);
        hop::TrafficPolicyController policy(&settings, &datasets); QTRY_VERIFY(policy.ready() && !policy.busy());
        datasets.refreshDue();
        QVERIFY(!policy.rules()[0].toMap().value("updating").toBool());
        QVERIFY(policy.rules()[1].toMap().value("updating").toBool());
        QTRY_VERIFY(!policy.rules()[1].toMap().value("updating").toBool() && !policy.busy());
        QCOMPARE(policy.rules()[0].toMap().value("updated").toString(), QStringLiteral("2000-01-01T00:00:00Z"));
        QVERIFY(policy.rules()[1].toMap().value("updated").toString() != "2000-01-01T00:00:00Z");
    }

    void sourceRuleIsVisibleImmediatelyAndIncludesEveryCategory() {
        QTemporaryDir root; SecureQSettings settings("Hop-Source-Rules", "memory", nullptr, true, false);
        hop::RouteDatasetsController datasets(nullptr, root.path(), helper(), &settings);
        hop::TrafficPolicyController policy(&settings, &datasets); QTRY_VERIFY(policy.ready());
        const auto source = QStringLiteral("https://fixture.test/multi");
        QVERIFY(policy.addRule("geosite-source", source, "direct"));
        QCOMPARE(policy.rules().size(), 1);
        QVERIFY(policy.rules().first().toMap().value("updating").toBool());
        QCOMPARE(policy.rules().first().toMap().value("value").toString(), source);
        QTRY_VERIFY_WITH_TIMEOUT(policy.ready() && !policy.busy() && !datasets.isBusy(), 15000);
        QVERIFY2(policy.lastError().isEmpty(), qPrintable(policy.lastError()));
        const auto first = policy.snapshot().value("rules").toArray().first().toObject().value("domain").toArray();
        QCOMPARE(first.size(), 2); QVERIFY(first.contains("full:ax")); QVERIFY(first.contains("full:by"));
        const auto date = policy.rules().first().toMap().value("updated").toString();
        QVERIFY(QDateTime::fromString(date, Qt::ISODate).isValid());
        QVERIFY(policy.addRule("geosite-source", "https://fixture.test/other", "block"));
        QTRY_VERIFY_WITH_TIMEOUT(policy.ready() && !policy.busy() && !datasets.isBusy(), 15000);
        QCOMPARE(policy.snapshot().value("rules").toArray().size(), 2);
        QVERIFY(datasets.sourcePath("geosite", source) != datasets.sourcePath("geosite", "https://fixture.test/other"));
        const auto snapshot = policy.snapshot(); QSignalSpy published(&policy, &hop::TrafficPolicyController::policyChanged);
        QVERIFY(policy.setRuleAutoUpdate(0, false)); QCOMPARE(published.count(), 0); QCOMPARE(policy.snapshot(), snapshot);
        QVERIFY(!policy.rules().first().toMap().value("autoUpdate").toBool());
        hop::RouteDatasetsController reopenedDatasets(nullptr, root.path(), helper(), &settings);
        hop::TrafficPolicyController reopened(&settings, &reopenedDatasets);
        QTRY_VERIFY(reopened.ready() && !reopened.busy());
        QCOMPARE(reopened.snapshot(), snapshot); QCOMPARE(reopened.rules(), policy.rules());
        QVERIFY(!reopenedDatasets.isBusy());
    }

    void localSourceRefreshPreservesLastSuccessfulDataOnFailure() {
        QTemporaryDir root; SecureQSettings settings("Hop-Local-Source", "memory", nullptr, true, false);
        const auto inputPath = root.filePath("source.dat"); QFile input(inputPath);
        QVERIFY(input.open(QIODevice::WriteOnly)); input.write(QByteArray::fromHex("0a0c0a0252551206080312026178")); input.close();
        hop::RouteDatasetsController datasets(nullptr, root.path() + "/cache", helper(), &settings);
        hop::TrafficPolicyController policy(&settings, &datasets); QTRY_VERIFY(policy.ready());
        QVERIFY(policy.addRule("geosite-source", QUrl::fromLocalFile(inputPath).toString(), "direct"));
        QTRY_VERIFY(policy.ready() && !policy.busy() && !datasets.isBusy());
        const auto source = policy.rules().first().toMap().value("value").toString();
        QCOMPARE(source, QDir::toNativeSeparators(inputPath));
        const auto before = policy.snapshot(); const auto updated = policy.rules().first().toMap().value("updated");
        QFile cache(datasets.sourcePath("geosite", source)); QVERIFY(cache.open(QIODevice::ReadOnly)); const auto bytes = cache.readAll(); cache.close();
        QVERIFY(input.open(QIODevice::WriteOnly | QIODevice::Truncate)); input.write("broken"); input.close();
        QVERIFY(policy.refreshRule(0)); QVERIFY(policy.rules().first().toMap().value("updating").toBool());
        QTRY_VERIFY(!policy.rules().first().toMap().value("updating").toBool());
        QVERIFY(!policy.rules().first().toMap().value("sourceError").toString().isEmpty());
        QCOMPARE(policy.snapshot(), before); QCOMPARE(policy.rules().first().toMap().value("updated"), updated);
        QVERIFY(cache.open(QIODevice::ReadOnly)); QCOMPARE(cache.readAll(), bytes); cache.close();
        QVERIFY(input.open(QIODevice::WriteOnly | QIODevice::Truncate)); input.write(QByteArray::fromHex("0a0c0a0255531206080312026279")); input.close();
        QVERIFY(policy.refreshRule(0)); QTRY_VERIFY(!datasets.isBusy() && !policy.busy());
        QTRY_VERIFY(policy.rules().first().toMap().value("sourceError").toString().isEmpty());
        QTRY_VERIFY(policy.snapshot() != before);
        QCOMPARE(policy.snapshot().value("rules").toArray().first().toObject().value("domain").toArray(), QJsonArray {"full:by"});
    }

    void initialFailureRetainsTheSourceRowAndAllowsRemoval() {
        QTemporaryDir root; SecureQSettings settings("Hop-Failed-Source", "memory", nullptr, true, false);
        hop::RouteDatasetsController datasets(nullptr, root.path(), helper(), &settings);
        hop::TrafficPolicyController policy(&settings, &datasets); QTRY_VERIFY(policy.ready());
        QVERIFY(policy.addRule("geoip-source", "https://fixture.test/error", "direct"));
        QTRY_VERIFY(!policy.busy() && !policy.rules().first().toMap().value("updating").toBool());
        QCOMPARE(policy.rules().size(), 1); QVERIFY(!policy.ready());
        QVERIFY(!policy.rules().first().toMap().value("sourceError").toString().isEmpty());
        QVERIFY(policy.rules().first().toMap().value("updated").toString().isEmpty());
        QVERIFY(policy.removeRule(0)); QTRY_VERIFY(!policy.busy());
        QVERIFY(policy.ready()); QVERIFY(policy.rules().isEmpty());
        datasets.refreshDue(); QVERIFY(!datasets.isBusy());
    }

    void categorySettingsAreResetWithoutImportingTheirContents() {
        QTemporaryDir root; SecureQSettings settings("Hop-Old-Categories", "memory", nullptr, true, false);
        const QJsonObject old { { "enabled", true }, { "policy", QJsonObject { { "default", "vpn" }, { "rules", QJsonArray {
            QJsonObject { { "kind", "geoip" }, { "value", "PRIVATE" }, { "target", "direct" } } } } } } };
        settings.setValue("Conf/hopTrafficPolicy", QJsonDocument(old).toJson(QJsonDocument::Compact));
        settings.setValue("Conf/unrelated", "unchanged");
        hop::RouteDatasetsController datasets(nullptr, root.path(), helper(), &settings);
        hop::TrafficPolicyController policy(&settings, &datasets); QTRY_VERIFY(policy.ready() && !policy.busy());
        QVERIFY(policy.rules().isEmpty()); QVERIFY(!policy.enabled()); QVERIFY(policy.snapshot().isEmpty());
        QCOMPARE(settings.value("Conf/unrelated").toString(), QStringLiteral("unchanged"));
    }

    void invalidSourceAndFailedPersistenceDoNotCreateRules() {
        struct FailureSettings : SecureQSettings {
            FailureSettings() : SecureQSettings("Hop-Source-Failure", "memory", nullptr, true, false) {}
            bool fail = false;
            bool setValueChecked(const QString &key, const QVariant &value) override { return !fail && SecureQSettings::setValueChecked(key, value); }
        } settings;
        QTemporaryDir root; hop::RouteDatasetsController datasets(nullptr, root.path(), helper(), &settings);
        hop::TrafficPolicyController policy(&settings, &datasets); QTRY_VERIFY(policy.ready());
        for (const auto &source : {"relative.dat", "https://user:secret@fixture.test/data", "https://fixture.test/data#fragment"}) {
            QVERIFY(!policy.addRule("geoip-source", source, "direct")); QVERIFY(policy.rules().isEmpty());
        }
        settings.fail = true;
        QVERIFY(!policy.addRule("geoip-source", "https://fixture.test/ip", "direct"));
        QVERIFY(policy.rules().isEmpty()); QVERIFY(!datasets.isBusy());
    }

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
        QSignalSpy restoreSelection(&policy, &hop::TrafficPolicyController::defaultTargetChanged);
        QVERIFY(policy.setDefaultTarget("direct")); QTRY_VERIFY(!policy.busy());
        QCOMPARE(restoreSelection.count(), 1);
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

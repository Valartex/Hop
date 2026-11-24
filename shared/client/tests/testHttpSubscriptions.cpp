#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>
#include <QProcess>
#include <QScopeGuard>
#include <QFile>
#include <QTextStream>
#include <cstdlib>
#include <memory>

#include "secureQSettings.h"
#include "core/controllers/selfhosted/importController.h"
#include "core/repositories/secureAppSettingsRepository.h"
#include "core/repositories/secureServersRepository.h"
#include "core/utils/serverConfigUtils.h"
#include "core/locations/resolvedEndpoint.h"
#include "core/subscriptions/httpSubscriptionsController.h"
#include "core/subscriptions/subscriptionNaming.h"
#include "core/subscriptions/subscriptionParser.h"
#include "core/utils/api/gatewayPayloadBuilder.h"
#include "core/utils/constants/apiKeys.h"
#include "core/controllers/connectionController.h"
#include "vpnConnection.h"
#include "ipc.h"

class SnapshotFailureSettings : public SecureQSettings {
public:
    SnapshotFailureSettings() : SecureQSettings(QStringLiteral("Hop-Snapshot-Failure"), QStringLiteral("memory"), nullptr, true, false) {}
    bool reject = false;
    bool setValueChecked(const QString &key, const QVariant &value) override {
        return reject ? false : SecureQSettings::setValueChecked(key, value);
    }
};

class SnapshotCrashSettings : public SecureQSettings {
public:
    SnapshotCrashSettings(const QString &organization, bool afterWrite)
        : SecureQSettings(organization, QStringLiteral("isolated"), nullptr, false), m_afterWrite(afterWrite) {}
    bool setValueChecked(const QString &key, const QVariant &value) override {
        if (key != QStringLiteral("Servers/serversList")) { return SecureQSettings::setValueChecked(key, value); }
        if (!m_afterWrite) { std::_Exit(91); }
        if (!SecureQSettings::setValueChecked(key, value)) { std::_Exit(92); }
        // Авария после sync, до публикации снимка репозиторием; деструкторы не выполняются.
        std::_Exit(91);
    }
private:
    bool m_afterWrite;
};

static QJsonObject syntheticSubscriptionConfig(ImportController &importer, const QString &source, const QString &name)
{
    const QString uri = QStringLiteral("vless://00000000-0000-4000-8000-000000000001@vpn.example:443?type=tcp&security=none#") + name;
    auto imported = importer.extractConfigFromData(uri);
    const QJsonObject metadata { { QStringLiteral("sourceId"), source },
        { QStringLiteral("sourceUrl"), QStringLiteral("https://provider.example/synthetic-subscription") },
        { QStringLiteral("sourceName"), QStringLiteral("Synthetic provider") } };
    imported.config.insert(QStringLiteral("hopSubscription"), metadata);
    return imported.config;
}

class TestHttpSubscriptions : public QObject {
    Q_OBJECT
private:
    std::unique_ptr<SecureQSettings> m_settings;
    std::unique_ptr<SecureServersRepository> m_servers;
    std::unique_ptr<SecureAppSettingsRepository> m_appSettings;
    std::unique_ptr<ImportController> m_importer;

    QJsonObject config(const QString &source, const QString &name)
    {
        return syntheticSubscriptionConfig(*m_importer, source, name);
    }

private slots:
    void providerContractVersionIsIndependentOfHopVersion()
    {
        const auto payload = GatewayPayloadBuilder(nullptr).build();
        QCOMPARE(payload.value(apiDefs::key::appVersion).toString(), QStringLiteral("5.0.3.1"));
        QCOMPARE(payload.value(apiDefs::key::cliName).toString(), QStringLiteral("AmneziaVPN"));
    }

    void subscriptionTitleDecodingRejectsMalformedOptionalMetadata()
    {
        QCOMPARE(hop::decodeSubscriptionTitle("BlancVPN"), QStringLiteral("BlancVPN"));
        QCOMPARE(hop::decodeSubscriptionTitle("base64:QmxhbmNWUE4="), QStringLiteral("BlancVPN"));
        const auto unicode = QString::fromUtf8("Моя подписка");
        QCOMPARE(hop::decodeSubscriptionTitle("base64:" + unicode.toUtf8().toBase64()), unicode);
        QVERIFY(hop::decodeSubscriptionTitle("base64:!broken").isEmpty());
        QVERIFY(hop::decodeSubscriptionTitle(QByteArray(1, char(0x80))).isEmpty());
        QVERIFY(hop::decodeSubscriptionTitle("name\r\ninjected").isEmpty());
        QVERIFY(hop::decodeSubscriptionTitle("base64:" + QByteArray("name\nvalue").toBase64()).isEmpty());
        QVERIFY(hop::decodeSubscriptionTitle(QByteArray(1025, 'a')).isEmpty());
    }
    void providerTitleUpdatesLegacyAnd304ButPreservesCustomNameAndFallback()
    {
        SecureQSettings settings("Hop-Title-Update", "memory", nullptr, false, false);
        SecureServersRepository servers(&settings); SecureAppSettingsRepository app(&settings);
        ImportController importer(&servers, &app);
        auto old = syntheticSubscriptionConfig(importer, "legacy", "Original");
        auto metadata = old.value("hopSubscription").toObject();
        metadata.insert("sourceUrl", "https://hop-title.invalid/title"); metadata.insert("sourceName", "hop-title.invalid");
        metadata.insert("entryName", "Original"); metadata.insert("etag", "v1");
        metadata.insert("nameKey", hop::parseSubscription(QByteArrayLiteral(
            "vless://00000000-0000-4000-8000-000000000001@vpn.example:443?type=tcp&security=none#Original"
        )).entries.first().nameKey);
        old.insert("hopSubscription", metadata); old.insert("description", "hop-title.invalid · Original");
        QVERIFY(servers.applyNativeSubscriptionSnapshot("legacy", {{"legacy-id", old}}));
        VpnConnection vpn(nullptr, nullptr); ConnectionController connection(&servers, &app, &vpn);
        hop::HttpSubscriptionsController subscriptions(&servers, &importer, &connection, nullptr, QCoreApplication::applicationFilePath());
        subscriptions.setRefreshIntervalMinutes(0);
        QSignalSpy imported(&subscriptions, &hop::HttpSubscriptionsController::subscriptionImported);
        QSignalSpy failed(&subscriptions, &hop::HttpSubscriptionsController::errorOccurred);
        QSignalSpy refreshed(&subscriptions, &hop::HttpSubscriptionsController::sourceRefreshFinished);
        QVERIFY(!subscriptions.refreshSource("missing-source"));
        QVERIFY(subscriptions.refreshSource("legacy"));
        QCOMPARE(subscriptions.sourceStatuses().value("legacy").toMap().value("state").toString(), QStringLiteral("loading"));
        QTRY_COMPARE(imported.count(), 1);
        QCOMPARE(refreshed.count(), 1); QCOMPARE(refreshed.first().at(0).toString(), QStringLiteral("legacy"));
        QVERIFY(refreshed.first().at(1).toBool());
        QCOMPARE(subscriptions.sourceStatuses().value("legacy").toMap().value("state").toString(), QStringLiteral("ready"));
        QCOMPARE(servers.orderedServerIds(), QVector<QString>{"legacy-id"});
        // Сохранённый ETag старого импорта не должен оставлять прежнюю конфигурацию.
        QCOMPARE(servers.nativeConfig("legacy-id")->subscription.value("sourceName").toString(), QStringLiteral("Provider title"));
        QCOMPARE(servers.nativeConfig("legacy-id")->description, QStringLiteral("Provider title · Original"));
        QCOMPARE(servers.nativeConfig("legacy-id")->subscription.value("sourceNameMode").toString(), QStringLiteral("automatic"));
        QVERIFY(subscriptions.refreshSource("legacy"));
        QTRY_COMPARE(imported.count(), 2);
        QCOMPARE(servers.nativeConfig("legacy-id")->subscription.value("sourceName").toString(), QStringLiteral("Updated provider title"));
        auto expired = *servers.nativeConfig("legacy-id");
        expired.subscription.insert("updatedAt", QDateTime::currentDateTimeUtc().addSecs(-3600).toString(Qt::ISODate));
        QVERIFY(servers.applyNativeSubscriptionSnapshot("legacy", {{"legacy-id", expired.toJson()}}));
        QVERIFY(subscriptions.importSubscription("https://hop-title.invalid/title", "My name"));
        subscriptions.setRefreshIntervalMinutes(1);
        auto *timer = subscriptions.findChild<QTimer *>("hopSubscriptionRefreshTimer"); QVERIFY(timer);
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout"));
        subscriptions.setRefreshIntervalMinutes(0);
        QTRY_COMPARE(imported.count(), 3);
        subscriptions.refreshSource("legacy"); QTRY_COMPARE(imported.count(), 4);
        QCOMPARE(servers.nativeConfig("legacy-id")->subscription.value("sourceName").toString(), QStringLiteral("My name"));
        QCOMPARE(servers.nativeConfig("legacy-id")->subscription.value("sourceNameMode").toString(), QStringLiteral("custom"));
        QVERIFY(subscriptions.importSubscription("https://hop-title.invalid/missing")); QTRY_COMPARE(imported.count(), 5);
        QVERIFY(subscriptions.importSubscription("https://hop-title.invalid/malformed")); QTRY_COMPARE(imported.count(), 6);
        for (const auto &id : servers.orderedServerIds()) {
            if (id == "legacy-id") { continue; }
            QCOMPARE(servers.nativeConfig(id)->subscription.value("sourceName").toString(), QStringLiteral("hop-title.invalid"));
        }
        QCOMPARE(failed.count(), 0);
    }
    void removeSubscriptionIsLocalAtomicAndBlockedDuringConnection()
    {
        SnapshotFailureSettings settings;
        SecureServersRepository servers(&settings); SecureAppSettingsRepository app(&settings);
        ImportController importer(&servers, &app);
        QVERIFY(servers.applyNativeSubscriptionSnapshot("remove", {{"a", syntheticSubscriptionConfig(importer, "remove", "A")},
            {"b", syntheticSubscriptionConfig(importer, "remove", "B")}}));
        QVERIFY(servers.applyNativeSubscriptionSnapshot("keep", {{"c", syntheticSubscriptionConfig(importer, "keep", "C")}}));
        VpnConnection vpn(nullptr, nullptr); ConnectionController connection(&servers, &app, &vpn);
        hop::HttpSubscriptionsController subscriptions(&servers, &importer, &connection, nullptr, "hop-nonexistent-test-helper.exe");
        subscriptions.setRefreshIntervalMinutes(0);
        const auto stored = settings.value("Servers/serversList");
        QSignalSpy published(&servers, &SecureServersRepository::serversBatchChanged);
        settings.reject = true;
        QVERIFY(!subscriptions.removeSource("remove"));
        QCOMPARE(settings.value("Servers/serversList"), stored); QCOMPARE(servers.orderedServerIds().size(), 3); QCOMPARE(published.count(), 0);
        settings.reject = false;
        connection.setConnectionState(Vpn::Connecting);
        QVERIFY(!subscriptions.removeSource("remove")); QCOMPARE(servers.orderedServerIds().size(), 3);
        connection.setConnectionState(Vpn::Disconnected);
        QVERIFY(!servers.removeServersChecked({"a", "missing"})); QCOMPARE(servers.orderedServerIds().size(), 3);
        QVERIFY(subscriptions.removeSource("remove"));
        QCOMPARE(servers.orderedServerIds(), QVector<QString>{"c"}); QCOMPARE(published.count(), 1);
        QCOMPARE(servers.defaultServerId(), QStringLiteral("c"));
        SecureServersRepository restored(&settings); QCOMPARE(restored.orderedServerIds(), servers.orderedServerIds());
        QVERIFY(!subscriptions.removeSource("remove"));
    }
    void subscriptionScheduleHonorsDisabledAndConfiguredInterval()
    {
        SecureQSettings settings("Hop-Subscription-Schedule", "memory", nullptr, false, false);
        SecureServersRepository servers(&settings);
        SecureAppSettingsRepository app(&settings);
        ImportController importer(&servers, &app);
        auto config = syntheticSubscriptionConfig(importer, "schedule", "Scheduled");
        auto metadata = config.value("hopSubscription").toObject();
        metadata.insert("updatedAt", QDateTime::currentDateTimeUtc().addSecs(-7200).toString(Qt::ISODate));
        config.insert("hopSubscription", metadata);
        QVERIFY(servers.applyNativeSubscriptionSnapshot("schedule", { { "scheduled", config } }));
        VpnConnection vpn(nullptr, nullptr);
        ConnectionController connection(&servers, &app, &vpn);
        // Несуществующий helper исключает сетевой запрос даже при ошибке теста.
        hop::HttpSubscriptionsController subscriptions(&servers, &importer, &connection, nullptr, "hop-nonexistent-test-helper.exe");
        auto *timer = subscriptions.findChild<QTimer *>("hopSubscriptionRefreshTimer"); QVERIFY(timer);
        subscriptions.setRefreshIntervalMinutes(0);
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout")); QVERIFY(!subscriptions.isBusy());
        subscriptions.setRefreshIntervalMinutes(360);
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout")); QVERIFY(!subscriptions.isBusy());
        subscriptions.setRefreshIntervalMinutes(60);
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout")); QVERIFY(subscriptions.isBusy());
    }
    void snapshotSurvivesProcessCrash_data()
    {
        QTest::addColumn<bool>("afterWrite");
        QTest::newRow("before-checked-write") << false;
        QTest::newRow("after-sync-before-publish") << true;
    }

    void snapshotSurvivesProcessCrash()
    {
        QFETCH(bool, afterWrite);
        const auto organization = QStringLiteral("Hop-Snapshot-Crash-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto cleanup = qScopeGuard([&]() { QSettings settings(organization, QStringLiteral("isolated")); settings.clear(); settings.sync(); });
        {
            SecureQSettings settings(organization, QStringLiteral("isolated"), nullptr, false);
            SecureServersRepository repository(&settings);
            QVERIFY(repository.applyNativeSubscriptionSnapshot(QStringLiteral("source"), {
                { QStringLiteral("old"), config(QStringLiteral("source"), QStringLiteral("Old")) } }));
        }
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), { QStringLiteral("--snapshot-crash"), organization,
            afterWrite ? QStringLiteral("after") : QStringLiteral("before") });
        QVERIFY(child.waitForFinished(15000));
        QCOMPARE(child.exitCode(), 91);
        SecureQSettings settings(organization, QStringLiteral("isolated"), nullptr, false);
        SecureServersRepository restored(&settings);
        const auto expected = afterWrite ? QStringLiteral("new") : QStringLiteral("old");
        QCOMPARE(restored.orderedServerIds(), QVector<QString> { expected });
        QCOMPARE(restored.defaultServerId(), expected);
        QVERIFY(restored.nativeConfig(expected).has_value());
        QCOMPARE(restored.nativeConfig(expected)->description, afterWrite ? QStringLiteral("New") : QStringLiteral("Old"));
    }

    void checkedSettingsWriteIsVisibleToFreshReader()
    {
        const auto organization = QStringLiteral("Hop-CheckedSettings-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        SecureQSettings writer(organization, QStringLiteral("isolated"), nullptr, false);
        QVERIFY(writer.setValueChecked(QStringLiteral("Servers/serversList"), QByteArrayLiteral("synthetic-check")));
        QSettings reader(organization, QStringLiteral("isolated"));
        reader.sync();
        QCOMPARE(reader.value(QStringLiteral("Servers/serversList")).toByteArray(), QByteArrayLiteral("synthetic-check"));
        writer.clearSettings();
    }

    void failedSnapshotWriteKeepsPublishedAndStoredServers()
    {
        SnapshotFailureSettings settings;
        SecureServersRepository servers(&settings);
        QVERIFY(servers.applyNativeSubscriptionSnapshot(QStringLiteral("source"), { { QStringLiteral("old"), config(QStringLiteral("source"), QStringLiteral("Old")) } }));
        const auto stored = settings.value(QStringLiteral("Servers/serversList"));
        QSignalSpy changed(&servers, &SecureServersRepository::serversBatchChanged);
        settings.reject = true;
        QVERIFY(!servers.applyNativeSubscriptionSnapshot(QStringLiteral("source"), { { QStringLiteral("new"), config(QStringLiteral("source"), QStringLiteral("New")) } }));
        QCOMPARE(servers.orderedServerIds(), QVector<QString> { QStringLiteral("old") });
        QCOMPARE(servers.defaultServerId(), QStringLiteral("old"));
        QCOMPARE(settings.value(QStringLiteral("Servers/serversList")), stored);
        QCOMPARE(changed.count(), 0);
        SecureServersRepository reopened(&settings);
        QCOMPARE(reopened.orderedServerIds(), servers.orderedServerIds());
    }

    void privilegedProcessAcceptsOnlyOwnedTunAndLoopbackProxy()
    {
#ifdef Q_OS_WIN
        const QStringList valid { QStringLiteral("-device"), QStringLiteral("tun://Hop-TUN"),
            QStringLiteral("-proxy"), QStringLiteral("socks5://user:password@127.0.0.1:51820") };
        QCOMPARE(amnezia::sanitizeArguments(amnezia::Tun2Socks, valid), valid);
        auto foreign = valid; foreign[1] = QStringLiteral("tun://AmneziaVPN-Xray");
        QVERIFY(amnezia::sanitizeArguments(amnezia::Tun2Socks, foreign).isEmpty());
        auto remote = valid; remote[3] = QStringLiteral("socks5://192.0.2.1:51820");
        QVERIFY(amnezia::sanitizeArguments(amnezia::Tun2Socks, remote).isEmpty());
        auto extra = valid; extra.append(QStringLiteral("-logfile")); extra.append(QStringLiteral("C:/foreign-file"));
        QVERIFY(amnezia::sanitizeArguments(amnezia::Tun2Socks, extra).isEmpty());
        QVERIFY(amnezia::sanitizeArguments(amnezia::OpenVPN, { QStringLiteral("--config"), QStringLiteral("C:/foreign.conf") }).isEmpty());
        QVERIFY(amnezia::permittedProcessPath(amnezia::CertUtil).isEmpty());
#endif
    }

    void resolvedTransportKeepsRealityAndSourceConfiguration()
    {
        const QJsonObject reality { { QStringLiteral("serverName"), QStringLiteral("cover.example") },
            { QStringLiteral("publicKey"), QStringLiteral("synthetic-public-key") }, { QStringLiteral("shortId"), QStringLiteral("abcd") } };
        const QJsonObject document { { QStringLiteral("outbounds"), QJsonArray { QJsonObject {
            { QStringLiteral("protocol"), QStringLiteral("vless") },
            { QStringLiteral("settings"), QJsonObject { { QStringLiteral("vnext"), QJsonArray { QJsonObject {
                { QStringLiteral("address"), QStringLiteral("vpn.example") }, { QStringLiteral("port"), 443 } } } } } },
            { QStringLiteral("streamSettings"), QJsonObject { { QStringLiteral("security"), QStringLiteral("reality") },
                { QStringLiteral("realitySettings"), reality } } } } } } };
        QJsonObject config { { QStringLiteral("hostName"), QStringLiteral("vpn.example") },
            { amnezia::ProtocolUtils::key_proto_config_data(amnezia::Proto::Xray), QJsonObject {
                { QStringLiteral("config"), QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact)) } } } };
        const auto original = config;
        QVERIFY(hop::applyResolvedEndpoint(config, amnezia::Proto::Xray, QStringLiteral("192.0.2.20")));
        QCOMPARE(config.value(QStringLiteral("hostName")).toString(), QStringLiteral("192.0.2.20"));
        const auto changed = QJsonDocument::fromJson(config.value(amnezia::ProtocolUtils::key_proto_config_data(amnezia::Proto::Xray))
            .toObject().value(QStringLiteral("config")).toString().toUtf8()).object().value(QStringLiteral("outbounds")).toArray().first().toObject();
        QCOMPARE(changed.value(QStringLiteral("streamSettings")).toObject().value(QStringLiteral("realitySettings")).toObject(), reality);
        QCOMPARE(original.value(QStringLiteral("hostName")).toString(), QStringLiteral("vpn.example"));
        auto rejected = original;
        QVERIFY(!hop::applyResolvedEndpoint(rejected, amnezia::Proto::Xray, QStringLiteral("192.0.2.0/24")));
        QCOMPARE(rejected, original);
        QJsonObject awg { { QStringLiteral("hostName"), QStringLiteral("vpn.example") },
            { amnezia::ProtocolUtils::key_proto_config_data(amnezia::Proto::Awg), QJsonObject {
                { QStringLiteral("hostName"), QStringLiteral("vpn.example") }, { QStringLiteral("port"), 51820 },
                { QStringLiteral("headerProtectionKey"), QStringLiteral("synthetic") } } } };
        QVERIFY(hop::applyResolvedEndpoint(awg, amnezia::Proto::Awg, QStringLiteral("192.0.2.21")));
        const auto wrapped = awg.value(amnezia::ProtocolUtils::key_proto_config_data(amnezia::Proto::Awg)).toObject();
        QCOMPARE(wrapped.value(QStringLiteral("hostName")).toString(), QStringLiteral("192.0.2.21"));
        QCOMPARE(wrapped.value(QStringLiteral("port")).toInt(), 51820);
        QCOMPARE(wrapped.value(QStringLiteral("headerProtectionKey")).toString(), QStringLiteral("synthetic"));
    }

    void wireguardSingleDnsIsPreserved()
    {
        const auto text = QStringLiteral("[Interface]\nPrivateKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\nAddress = 10.77.0.2/32\nDNS = 10.77.0.1\n[Peer]\nPublicKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\nAllowedIPs = 10.77.0.1/32\nEndpoint = 127.0.0.1:50000\n");
        const auto imported = m_importer->extractConfigFromData(text);
        QCOMPARE(imported.errorCode, ErrorCode::NoError);
        QCOMPARE(imported.config.value(QStringLiteral("dns1")).toString(), QStringLiteral("10.77.0.1"));
        const auto two = m_importer->extractConfigFromData(QString(text).replace(QStringLiteral("DNS = 10.77.0.1"), QStringLiteral("DNS= 10.77.0.1, 1.1.1.1")));
        QCOMPARE(two.errorCode, ErrorCode::NoError);
        QCOMPARE(two.config.value(QStringLiteral("dns2")).toString(), QStringLiteral("1.1.1.1"));
    }
    void inMemorySettingsNeverPersistSecrets()
    {
        const auto organization = QStringLiteral("Hop-MemorySettingsTest-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        {
            SecureQSettings memory(organization, QStringLiteral("isolated"), nullptr, true, false);
            memory.setValue(QStringLiteral("Servers/serversList"), QByteArrayLiteral("synthetic-secret"));
            QCOMPARE(memory.value(QStringLiteral("Servers/serversList")).toByteArray(), QByteArrayLiteral("synthetic-secret"));
            QVERIFY(memory.backupAppConfig().isEmpty());
            QSettings persisted(organization, QStringLiteral("isolated"));
            QVERIFY(persisted.allKeys().isEmpty());
            memory.remove(QStringLiteral("Servers/serversList"));
            QVERIFY(!memory.value(QStringLiteral("Servers/serversList")).isValid());
        }
        QSettings persisted(organization, QStringLiteral("isolated"));
        QVERIFY(persisted.allKeys().isEmpty());
    }
    void init()
    {
        const QString organization = QStringLiteral("Hop-SubscriptionTests-") + QUuid::createUuid().toString();
        m_settings = std::make_unique<SecureQSettings>(organization, QStringLiteral("isolated"), nullptr, false);
        m_servers = std::make_unique<SecureServersRepository>(m_settings.get());
        m_appSettings = std::make_unique<SecureAppSettingsRepository>(m_settings.get());
        m_importer = std::make_unique<ImportController>(m_servers.get(), m_appSettings.get());
    }

    void cleanup()
    {
        m_importer.reset();
        m_appSettings.reset();
        m_servers.reset();
        m_settings->clearSettings();
        m_settings.reset();
    }

    void batchUpdateEmitsOneSignal()
    {
        QSignalSpy changed(m_servers.get(), &SecureServersRepository::serversBatchChanged);
        QSignalSpy added(m_servers.get(), &SecureServersRepository::serverAdded);
        const auto first = config(QStringLiteral("source-one"), QStringLiteral("First"));
        const auto second = config(QStringLiteral("source-one"), QStringLiteral("Second"));
        QVERIFY(m_servers->applyNativeSubscriptionSnapshot(QStringLiteral("source-one"), {
            { QStringLiteral("first"), first }, { QStringLiteral("second"), second }
        }));
        QCOMPARE(m_servers->serversCount(), 2);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(added.count(), 0);
        QCOMPARE(m_servers->defaultServerId(), QStringLiteral("first"));
        const auto restored = m_servers->nativeConfig(QStringLiteral("first"));
        QVERIFY(restored.has_value());
        QCOMPARE(restored->toJson().value(QStringLiteral("hopSubscription")), first.value(QStringLiteral("hopSubscription")));
    }

    void malformedSnapshotDoesNotReplaceExistingData()
    {
        const auto original = config(QStringLiteral("source-one"), QStringLiteral("Original"));
        QVERIFY(m_servers->applyNativeSubscriptionSnapshot(QStringLiteral("source-one"), { { QStringLiteral("first"), original } }));
        QSignalSpy changed(m_servers.get(), &SecureServersRepository::serversBatchChanged);
        QVERIFY(!m_servers->applyNativeSubscriptionSnapshot(QStringLiteral("source-one"), {
            { QStringLiteral("first"), config(QStringLiteral("source-one"), QStringLiteral("Changed")) },
            { QStringLiteral("invalid"), QJsonObject() }
        }));
        QCOMPARE(changed.count(), 0);
        QCOMPARE(m_servers->serversCount(), 1);
        QCOMPARE(m_servers->nativeConfig(QStringLiteral("first"))->description, QStringLiteral("Original"));
    }

    void cannotReplaceAnotherSourcesServer()
    {
        QVERIFY(m_servers->applyNativeSubscriptionSnapshot(QStringLiteral("source-one"), {
            { QStringLiteral("shared-id"), config(QStringLiteral("source-one"), QStringLiteral("Original")) }
        }));
        QVERIFY(!m_servers->applyNativeSubscriptionSnapshot(QStringLiteral("source-two"), {
            { QStringLiteral("shared-id"), config(QStringLiteral("source-two"), QStringLiteral("Replacement")) }
        }));
        QCOMPARE(m_servers->nativeConfig(QStringLiteral("shared-id"))->description, QStringLiteral("Original"));
    }

    void removedActiveServerSurvivesUntilDisconnected()
    {
        const auto first = config(QStringLiteral("source-one"), QStringLiteral("First"));
        const auto second = config(QStringLiteral("source-one"), QStringLiteral("Second"));
        QVERIFY(m_servers->applyNativeSubscriptionSnapshot(QStringLiteral("source-one"), {
            { QStringLiteral("first"), first }, { QStringLiteral("second"), second }
        }));
        QVERIFY(m_servers->applyNativeSubscriptionSnapshot(QStringLiteral("source-one"), {
            { QStringLiteral("second"), second }
        }, QStringLiteral("first")));
        QCOMPARE(m_servers->defaultServerId(), QStringLiteral("first"));
        QVERIFY(m_servers->nativeConfig(QStringLiteral("first"))->subscription.value(QStringLiteral("retired")).toBool());
        QVERIFY(m_servers->applyNativeSubscriptionSnapshot(QStringLiteral("source-one"), { { QStringLiteral("second"), second } }));
        QCOMPARE(m_servers->serversCount(), 1);
        QCOMPARE(m_servers->defaultServerId(), QStringLiteral("second"));
    }
};

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.size() == 2 && arguments.at(1) == QStringLiteral("--provider-task")) {
        // Только синтетический загрузчик для проверки контроллера, без сетевых запросов.
        QFile input; if (!input.open(stdin, QIODevice::ReadOnly)) { return 95; }
        const auto request = QJsonDocument::fromJson(input.readLine(16384)).object();
        const QUrl url(request.value("url").toString());
        if (request.value("action") != "subscription" || url.host() != "hop-title.invalid") { return 95; }
        const bool unchanged = !request.value("etag").toString().isEmpty();
        const auto body = unchanged ? QByteArray() : QByteArrayLiteral("vless://00000000-0000-4000-8000-000000000001@127.0.0.1:9?security=none&type=tcp#Original\n");
        const QString title = url.path() == "/missing" ? QString() : url.path() == "/malformed" ? QStringLiteral("bad\nname")
            : unchanged ? QStringLiteral("Updated provider title") : QStringLiteral("Provider title");
        const QJsonObject response{{"code", 0}, {"body", QString::fromLatin1(body.toBase64())},
            {"etag", QString::fromLatin1(QByteArray("v1").toBase64())}, {"modified", ""}, {"unchanged", unchanged}, {"profileTitle", title}};
        QTextStream(stdout) << QJsonDocument(response).toJson(QJsonDocument::Compact) << Qt::endl;
        return 0;
    }
    if (arguments.size() == 4 && arguments.at(1) == QStringLiteral("--snapshot-crash")) {
        if (!arguments.at(2).startsWith(QStringLiteral("Hop-Snapshot-Crash-"))) { return 93; }
        SnapshotCrashSettings settings(arguments.at(2), arguments.at(3) == QStringLiteral("after"));
        SecureServersRepository servers(&settings);
        SecureAppSettingsRepository appSettings(&settings);
        ImportController importer(&servers, &appSettings);
        servers.applyNativeSubscriptionSnapshot(QStringLiteral("source"), {
            { QStringLiteral("new"), syntheticSubscriptionConfig(importer, QStringLiteral("source"), QStringLiteral("New")) } });
        return 94;
    }
    TestHttpSubscriptions test;
    return QTest::qExec(&test, argc, argv);
}
#include "testHttpSubscriptions.moc"

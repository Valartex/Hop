#include <QTest>
#include <QJsonDocument>
#include <QUuid>
#include "core/locations/locationCatalog.h"
#include "core/controllers/selfhosted/importController.h"
#include "core/locations/apiCatalogSnapshot.h"
#include "core/locations/locationCoordinator.h"
#include "core/locations/locationSearchModel.h"
#include "core/subscriptions/subscriptionNaming.h"
#include "vpnConnection.h"

class TestLocationCatalog : public QObject {
    Q_OBJECT
    static QJsonObject native(ImportController &importer, const QString &label, const QString &source)
    {
        auto value = importer.extractConfigFromData(QStringLiteral("vless://00000000-0000-4000-8000-000000000001@127.0.0.1:9?security=none&type=tcp#Test")).config;
        value.insert(QStringLiteral("description"), label);
        value.insert(QStringLiteral("hopSubscription"), QJsonObject { { QStringLiteral("sourceId"), source } });
        return value;
    }
private slots:
    void priorityColumnsKeepSubscriptionAndLocationSeparate()
    {
        SecureQSettings settings("Hop-Priority-Columns", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); ImportController importer(&servers, nullptr);
        auto config = native(importer, "Provider · Europe · Amsterdam", "source");
        config.insert("hopSubscription", QJsonObject{{"sourceId", "source"}, {"sourceName", "Provider"}});
        servers.addServer("a", config, serverConfigUtils::ConfigType::Native);
        auto other = native(importer, "Unrelated · Berlin", "other");
        other.insert("hopSubscription", QJsonObject{{"sourceId", "other"}, {"sourceName", "Second provider"}});
        servers.addServer("b", other, serverConfigUtils::ConfigType::Native);
        hop::LocationCatalog catalog(&servers, &settings);
        QVERIFY(catalog.toggleMembership("a")); QVERIFY(catalog.toggleMembership("b"));
        QVERIFY(catalog.toggleCountry("NL"));
        const auto rows = catalog.priorities();
        QCOMPARE(rows[0].toMap().value("serviceName").toString(), QStringLiteral("Provider"));
        QCOMPARE(rows[0].toMap().value("locationName").toString(), QStringLiteral("Europe · Amsterdam"));
        QCOMPARE(rows[1].toMap().value("serviceName").toString(), QStringLiteral("Second provider"));
        QCOMPARE(rows[1].toMap().value("locationName").toString(), QStringLiteral("Unrelated · Berlin"));
        QVERIFY(rows[2].toMap().value("serviceName").toString().isEmpty());
        QCOMPARE(rows[2].toMap().value("locationName").toString(), QString::fromUtf8("Страна: NL"));
        QCOMPARE(catalog.entries().first().name, QStringLiteral("Provider · Europe · Amsterdam"));
    }
    void rebuildingIndividualPoolUsesNewAdditionOrder()
    {
        SecureQSettings settings("Hop-Pool-Rebuild", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); ImportController importer(&servers, nullptr);
        for (const auto &id : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}) {
            servers.addServer(id, native(importer, id, "one"), serverConfigUtils::ConfigType::Native);
        }
        hop::LocationCatalog catalog(&servers, &settings); catalog.setMode(hop::LocationCatalog::Automatic);
        for (const auto &id : {"a", "b", "c"}) { QVERIFY(catalog.toggleMembership(id)); }
        QCOMPARE(catalog.candidateIds(), QStringList({"a", "b", "c"}));
        for (const auto &id : {"a", "b", "c"}) { QVERIFY(catalog.toggleMembership(id)); }
        QVERIFY(catalog.candidateIds().isEmpty());
        QVERIFY(catalog.priorities().isEmpty());
        for (const auto &id : {"c", "a", "b"}) { QVERIFY(catalog.toggleMembership(id)); }
        QCOMPARE(catalog.candidateIds(), QStringList({"c", "a", "b"}));
        QCOMPARE(catalog.marks().value("c").toMap().value("priority").toInt(), 1);
        QCOMPARE(catalog.marks().value("a").toMap().value("priority").toInt(), 2);
        QCOMPARE(catalog.marks().value("b").toMap().value("priority").toInt(), 3);
        QVERIFY(catalog.toggleMembership("a")); QVERIFY(catalog.toggleMembership("a"));
        QCOMPARE(catalog.candidateIds(), QStringList({"c", "b", "a"}));
        hop::LocationCatalog restored(&servers, &settings);
        QCOMPARE(restored.candidateIds(), catalog.candidateIds());
        QCOMPARE(restored.priorities(), catalog.priorities());
    }

    void legacyExcludedPoolEntriesDoNotRestoreOldPositions()
    {
        SecureQSettings settings("Hop-Pool-Legacy", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); ImportController importer(&servers, nullptr);
        for (const auto &id : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}) {
            servers.addServer(id, native(importer, id, "one"), serverConfigUtils::ConfigType::Native);
        }
        QJsonArray priorities;
        for (const auto &id : {"a", "b", "c"}) { priorities.append(QJsonObject{{"kind", "location"}, {"key", id}}); }
        settings.setValue("Conf/hopLocationPolicy", QJsonDocument(QJsonObject{{"version", 1}, {"mode", 2},
            {"priorities", priorities}, {"excluded", QJsonArray{"a", "b", "c"}}}).toJson(QJsonDocument::Compact));
        hop::LocationCatalog catalog(&servers, &settings);
        QVERIFY(catalog.toggleMembership("c"));
        QCOMPARE(catalog.candidateIds(), QStringList{"c"});
        QCOMPARE(catalog.priorities().size(), 1);
        QCOMPARE(catalog.marks().value("c").toMap().value("priority").toInt(), 1);
        QVERIFY(catalog.toggleMembership("b")); QVERIFY(catalog.toggleMembership("a"));
        QCOMPARE(catalog.candidateIds(), QStringList({"c", "b", "a"}));
        hop::LocationCatalog restored(&servers, &settings);
        QCOMPARE(restored.candidateIds(), catalog.candidateIds());
    }

    void editingIndividualMembershipKeepsCountryAndAllLocationPolicies()
    {
        SecureQSettings settings("Hop-Pool-Groups", "memory", nullptr, true, false);
        SecureServersRepository servers(&settings); ImportController importer(&servers, nullptr);
        servers.addServer("a", native(importer, "Amsterdam", "one"), serverConfigUtils::ConfigType::Native);
        servers.addServer("b", native(importer, "Berlin", "two"), serverConfigUtils::ConfigType::Native);
        hop::LocationCatalog catalog(&servers, &settings);
        QVERIFY(catalog.setCountry("a", "NL")); QVERIFY(catalog.toggleCountry("NL"));
        QVERIFY(catalog.toggleMembership("b")); catalog.setMode(hop::LocationCatalog::Automatic);
        const auto original = catalog.priorities();
        QVERIFY(catalog.toggleMembership("a"));
        QCOMPARE(catalog.candidateIds(), QStringList{"b"});
        QCOMPARE(catalog.priorities(), original);
        QVERIFY(catalog.toggleMembership("a"));
        QCOMPARE(catalog.candidateIds(), QStringList({"a", "b"}));
        QCOMPARE(catalog.priorities(), original);
        catalog.setMode(hop::LocationCatalog::AllOrdered);
        QVERIFY(catalog.toggleMembership("a")); QVERIFY(catalog.toggleMembership("b"));
        QVERIFY(catalog.candidateIds().isEmpty());
        QVERIFY(catalog.toggleMembership("b")); QVERIFY(catalog.toggleMembership("a"));
        QCOMPARE(catalog.candidateIds(), QStringList({"a", "b"}));
        QCOMPARE(catalog.priorities(), original);
    }

    void legacyPinnedModeMigratesToManualAndKeepsSelectedTargetAndExclusions()
    {
        SecureQSettings settings("Hop-Legacy-Pinned", "memory", nullptr, false, false);
        SecureServersRepository servers(&settings); ImportController importer(&servers, nullptr);
        servers.addServer("a", native(importer, "First", "one"), serverConfigUtils::ConfigType::Native);
        servers.addServer("b", native(importer, "Second", "one"), serverConfigUtils::ConfigType::Native);
        const QJsonObject legacy{{"version", 1}, {"mode", 1}, {"selected", "a"}, {"pinned", "b"},
            {"priorities", QJsonArray{QJsonObject{{"kind", "location"}, {"key", "a"}}}}, {"excluded", QJsonArray{"a"}}};
        settings.setValue("Conf/hopLocationPolicy", QJsonDocument(legacy).toJson(QJsonDocument::Compact));
        hop::LocationCatalog catalog(&servers, &settings);
        QCOMPARE(catalog.mode(), int(hop::LocationCatalog::Manual));
        QCOMPARE(catalog.selectedId(), QStringLiteral("b")); QCOMPARE(catalog.candidateIds(), QStringList{"b"});
        QVERIFY(catalog.marks().value("a").toMap().value("excluded").toBool()); QCOMPARE(catalog.priorities().size(), 1);
        const auto saved = QJsonDocument::fromJson(settings.value("Conf/hopLocationPolicy").toByteArray()).object();
        QCOMPARE(saved.value("mode").toInt(-1), 0); QVERIFY(!saved.contains("pinned"));
        hop::LocationCatalog restored(&servers, &settings); QCOMPARE(restored.selectedId(), QStringLiteral("b"));
        QCOMPARE(restored.mode(), int(hop::LocationCatalog::Manual));
    }
    void automaticModeIdentifiersDoNotShiftAfterRemovingPinned()
    {
        SecureQSettings settings("Hop-Stable-Modes", "memory", nullptr, false, false);
        SecureServersRepository servers(&settings);
        for (const int value : {2, 3, 4}) {
            settings.setValue("Conf/hopLocationPolicy", QJsonDocument(QJsonObject{{"version", 1}, {"mode", value}}).toJson(QJsonDocument::Compact));
            hop::LocationCatalog catalog(&servers, &settings); QCOMPARE(catalog.mode(), value);
        }
    }
    void legacySubscriptionHostnameIsNotGuessedFromDomain()
    {
        SecureQSettings settings("Hop-Legacy-Subscription-Name", "memory", nullptr, false, false);
        SecureServersRepository servers(&settings); ImportController importer(&servers, nullptr);
        auto value = native(importer, "old.withblancvpn.online · Amsterdam", "blanc");
        auto metadata = value.value("hopSubscription").toObject();
        metadata.insert("sourceUrl", "https://old.withblancvpn.online/s/synthetic");
        metadata.insert("sourceName", "old.withblancvpn.online");
        value.insert("hopSubscription", metadata);
        QVERIFY(servers.applyNativeSubscriptionSnapshot("blanc", {{"amsterdam", value}}));
        const auto stored = settings.value("Servers/serversList");
        hop::LocationCatalog catalog(&servers, &settings);
        QCOMPARE(catalog.services().value("blanc").toString(), QStringLiteral("old.withblancvpn.online"));
        QCOMPARE(catalog.serviceInfo("blanc").value("name").toString(), QStringLiteral("old.withblancvpn.online"));
        QVERIFY(catalog.selectedName().startsWith("old.withblancvpn.online · "));
        QCOMPARE(catalog.selectedId(), QStringLiteral("amsterdam"));
        QCOMPARE(catalog.entries().first().serviceId, QStringLiteral("blanc"));
        QCOMPARE(settings.value("Servers/serversList"), stored);
        metadata.insert("sourceName", "My provider");
        QCOMPARE(hop::subscriptionName(metadata), QStringLiteral("My provider"));
        metadata.insert("sourceName", "old.withblancvpn.online"); metadata.insert("sourceNameMode", "custom");
        QCOMPARE(hop::subscriptionName(metadata), QStringLiteral("old.withblancvpn.online"));
        metadata.insert("sourceNameMode", "automatic");
        QCOMPARE(hop::subscriptionName(metadata), QStringLiteral("old.withblancvpn.online"));
        QVERIFY(!hop::hasCustomSubscriptionName(metadata));
        QCOMPARE(hop::subscriptionName(QUrl("https://withblancvpn.online.example/s"), {}), QStringLiteral("withblancvpn.online.example"));
        QCOMPARE(hop::subscriptionName(QUrl("https://unknown.example/s"), {}), QStringLiteral("unknown.example"));
    }
    void searchPreservesSourceSelectionAndDisplayNames()
    {
        SecureQSettings settings("Hop-Search", "memory", nullptr, false, false);
        SecureServersRepository servers(&settings);
        ImportController importer(&servers, nullptr);
        const auto flag = QString::fromUcs4(U"\U0001f1f3\U0001f1f1");
        QVERIFY(servers.applyNativeSubscriptionSnapshot("source", {
            { "nl", native(importer, flag + QStringLiteral(" Амстердам, Нидерланды"), "source") },
            { "de", native(importer, QStringLiteral("Берлин, Германия"), "source") }
        }));
        hop::LocationCatalog catalog(&servers, &settings);
        hop::LocationSearchModel search(&catalog);
        QCOMPARE(search.rowCount(), 2);
        QVERIFY(!catalog.data(catalog.index(0), hop::LocationCatalog::Name).toString().contains(flag));
        QVERIFY(!catalog.data(catalog.index(0), hop::LocationCatalog::Description).toString().contains("NL"));
        search.setQuery(QStringLiteral("германия")); QCOMPARE(search.rowCount(), 1);
        QCOMPARE(search.sourceRow(0), 1); QVERIFY(catalog.selectRow(search.sourceRow(0)));
        QCOMPARE(catalog.selectedId(), QStringLiteral("de"));
        search.setQuery("NL"); QCOMPARE(search.rowCount(), 1); QCOMPARE(search.sourceRow(0), 0);
        search.setQuery("["); QCOMPARE(search.rowCount(), 0);
        search.setQuery(" "); QCOMPARE(search.rowCount(), 2);
    }
    void apiCatalogNormalizesAutoAndDuplicates()
    {
        QString error;
        const QJsonObject account { { "available_countries", QJsonArray {
            QJsonObject { { "server_country_code", "" } },
            QJsonObject { { "server_country_code", "nl" }, { "available_protocols", QJsonArray { "awg" } } },
            QJsonObject { { "server_country_code", "nl" }, { "available_protocols", QJsonArray { "vless" } } }
        } } };
        const auto result = hop::readApiCountries(account, error);
        QVERIFY(error.isEmpty()); QCOMPARE(result.size(), 1);
        QCOMPARE(result.first().toObject().value("available_protocols").toArray().size(), 2);
        QCOMPARE(result.first().toObject().value("server_country_name").toString(), QStringLiteral("nl"));
        QVERIFY(hop::readApiCountries(QJsonObject { { "available_countries", QJsonArray { 7 } } }, error).isEmpty());
        QVERIFY(!error.isEmpty());
        const auto limit = hop::providerFailureMessage(1108, false, 7, 7);
        QVERIFY(limit.contains("1108")); QVERIFY(limit.contains(QStringLiteral("7 из 7")));
    }
#ifdef Q_OS_WIN
    void providerCatalogUpdatesRowsAndExposesFailure()
    {
        SecureQSettings settings("Hop-Catalog-Process", "memory", nullptr, false, false);
        SecureServersRepository servers(&settings);
        SecureAppSettingsRepository appSettings(&settings);
        auto api = amnezia::ApiV2ServerConfig::fromJson(QJsonObject {
            { "configVersion", serverConfigUtils::ConfigSource::AmneziaGateway }, { "description", "Catalog subscription" },
            { "api_config", QJsonObject { { "service_type", "external-premium" }, { "service_protocol", "awg" } } },
            { "auth_data", QJsonObject { { "api_key", "synthetic" } } } });
        servers.addServer("api", api.toJson(), serverConfigUtils::ConfigType::ExternalPremium);
        hop::LocationCatalog catalog(&servers, &settings);
        VpnConnection vpn(nullptr, nullptr);
        ConnectionController connection(&servers, &appSettings, &vpn);
        hop::LocationCoordinator coordinator(&catalog, &servers, &appSettings, &connection, &vpn, nullptr,
            QCoreApplication::applicationDirPath() + "/hop_fake_provider.exe");
        coordinator.refreshCatalog("api");
        QTRY_COMPARE(catalog.rowCount(), 2);
        QTRY_COMPARE(coordinator.catalogStatuses().value("api").toMap().value("state").toString(), QStringLiteral("ready"));
        const auto info = catalog.serviceInfo("api");
        QCOMPARE(info.value("activeConfigurations").toInt(), 7);
        QCOMPARE(info.value("maxConfigurations").toInt(), 7);
        QVERIFY(!info.contains("auth_data"));
        api = *servers.apiV2Config("api"); api.description = "catalog-error";
        servers.editServer("api", api.toJson(), serverConfigUtils::ConfigType::ExternalPremium);
        coordinator.refreshCatalog("api");
        QTRY_COMPARE(coordinator.catalogStatuses().value("api").toMap().value("state").toString(), QStringLiteral("error"));
        QCOMPARE(catalog.rowCount(), 2);
        QVERIFY(!coordinator.catalogStatuses().value("api").toMap().value("message").toString().isEmpty());
    }
#endif
    void poolsExpandAcrossServicesAndSurviveRefresh()
    {
        SecureQSettings settings("Hop-Catalog-" + QUuid::createUuid().toString(), "memory", nullptr, true, false);
        SecureServersRepository servers(&settings);
        ImportController importer(&servers, nullptr);
        QVERIFY(servers.applyNativeSubscriptionSnapshot("one", { { "nl1", native(importer, QString::fromUtf8("🇳🇱 Amsterdam"), "one") } }));
        QVERIFY(servers.applyNativeSubscriptionSnapshot("two", { { "nl2", native(importer, QString::fromUtf8("🇳🇱 Rotterdam"), "two") },
            { "de", native(importer, QString::fromUtf8("🇩🇪 Berlin"), "two") } }));
        hop::LocationCatalog catalog(&servers, &settings);
        QCOMPARE(catalog.rowCount(), 3);
        QVERIFY(catalog.toggleCountry("nl"));
        QVERIFY(catalog.toggleLocation("de"));
        catalog.setMode(hop::LocationCatalog::Automatic);
        QCOMPARE(catalog.candidateIds(), QStringList({ "nl1", "nl2", "de" }));
        QCOMPARE(catalog.marks().value("nl2").toMap().value("priority").toInt(), 1);
        QVERIFY(catalog.setExcluded("nl1", true));
        QCOMPARE(catalog.candidateIds(), QStringList({ "nl2", "de" }));
        QVERIFY(catalog.movePriority(1, 0));
        QCOMPARE(catalog.candidateIds(), QStringList({ "de", "nl2" }));
        QVERIFY(servers.applyNativeSubscriptionSnapshot("one", { { "new", native(importer, QString::fromUtf8("🇳🇱 New"), "one") } }));
        QCOMPARE(catalog.candidateIds(), QStringList({ "de", "nl2", "new" }));
        hop::LocationCatalog restored(&servers, &settings);
        QCOMPARE(restored.candidateIds(), catalog.candidateIds());
        QVERIFY(restored.select("nl2"));
        restored.setMode(hop::LocationCatalog::Manual);
        QCOMPARE(restored.candidateIds(), QStringList({ "nl2" }));
        servers.removeServer("nl2");
        QVERIFY(!restored.candidateIds().contains("nl2"));
    }
    void countryMetadataIsExplicit()
    {
        QCOMPARE(hop::LocationCatalog::countryFromLabel(QStringLiteral("Amsterdam Germany fastest")), QString());
        QCOMPARE(hop::LocationCatalog::countryFromLabel(QString::fromUtf8("Blanc 🇬🇧 London")), QStringLiteral("GB"));
        SecureQSettings settings("Hop-Catalog-" + QUuid::createUuid().toString(), "memory", nullptr, true, false);
        SecureServersRepository servers(&settings);
        ImportController importer(&servers, nullptr);
        servers.addServer("unknown", native(importer, "Amsterdam", "one"), serverConfigUtils::ConfigType::Native);
        hop::LocationCatalog catalog(&servers, &settings);
        QVERIFY(catalog.entry("unknown")->country.isEmpty());
        QVERIFY(catalog.setCountry("unknown", "nl"));
        QCOMPARE(catalog.entry("unknown")->country, QStringLiteral("NL"));
        QVERIFY(!catalog.setCountry("unknown", "Netherlands"));
        QVERIFY(catalog.toggleCountry("NL"));
        catalog.setMode(hop::LocationCatalog::Automatic);
        QCOMPARE(catalog.candidateIds(), QStringList({ "unknown" }));
    }
    void apiLocationsUseProviderIdsAndDoNotIssueConfigurations()
    {
        SecureQSettings settings("Hop-Catalog-" + QUuid::createUuid().toString(), "memory", nullptr, true, false);
        SecureServersRepository servers(&settings);
        const QJsonArray countries { QJsonObject { { "server_country_code", "nl-worker-1" }, { "server_country_code_l10n", "nl" }, { "server_country_name", "Netherlands" } },
            QJsonObject { { "server_country_code", "de" }, { "server_country_code_l10n", "de" }, { "server_country_name", "Germany" } } };
        auto api = amnezia::ApiV2ServerConfig::fromJson(QJsonObject { { "config_version", serverConfigUtils::ConfigSource::AmneziaGateway },
            { "description", "Amnezia" }, { "api_config", QJsonObject { { "service_type", "external-premium" }, { "service_protocol", "awg" },
                { "server_country_code", "nl-worker-1" }, { "available_countries", countries } } },
            { "auth_data", QJsonObject { { "api_key", "synthetic" } } } });
        servers.addServer("api", api.toJson(), serverConfigUtils::ConfigType::ExternalPremium);
        QCOMPARE(servers.serversCount(), 1);
        hop::LocationCatalog catalog(&servers, &settings);
        QCOMPARE(catalog.rowCount(), 2);
        QCOMPARE(catalog.selectedId(), QStringLiteral("api|nl-worker-1"));
        QVERIFY(catalog.toggleCountry("NL"));
        catalog.setMode(hop::LocationCatalog::Automatic);
        QCOMPARE(catalog.candidateIds(), QStringList({ "api|nl-worker-1" }));
        QVERIFY(catalog.select("api|de"));
        QCOMPARE(servers.defaultServerId(), QStringLiteral("api"));
        QCOMPARE(servers.apiV2Config("api")->apiConfig.serverCountryCode, QStringLiteral("nl-worker-1"));
        QVERIFY(!servers.apiV2Config("api")->hasContainers());
        QVERIFY(catalog.toggleMembership("api|de"));
        QCOMPARE(catalog.priorities().last().toMap().value("serviceName").toString(), QStringLiteral("Amnezia"));
        QCOMPARE(catalog.priorities().last().toMap().value("locationName").toString(), QStringLiteral("Germany"));
    }
};
QTEST_GUILESS_MAIN(TestLocationCatalog)
#include "testLocationCatalog.moc"

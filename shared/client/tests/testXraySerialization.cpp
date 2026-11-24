#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QUuid>
#include <QTest>

#include "utils/testCoreController.h"
#include "core/models/serverDescription.h"
#include "core/utils/serialization/serialization.h"
#include "core/utils/utilities.h"
#include "secureQSettings.h"
#include "utils/testUtils.h"
#include "vpnConnection.h"

using namespace amnezia;
using namespace amnezia::test;

class TestXraySerialization : public QObject
{
    Q_OBJECT

private:
    TestCoreController *m_coreController;
    SecureQSettings *m_settings;

    QJsonObject extractXrayConfig(const QString &data, ConfigTypes configType, const QString &description = "") const
    {
        QJsonParseError parserErr;
        QJsonDocument jsonConf = QJsonDocument::fromJson(data.toLocal8Bit(), &parserErr);

        QJsonObject xrayVpnConfig;
        xrayVpnConfig[configKey::config] = jsonConf.toJson().constData();
        QJsonObject lastConfig;
        lastConfig[configKey::lastConfig] = jsonConf.toJson().constData();
        lastConfig[configKey::isThirdPartyConfig] = true;

        QJsonObject containers;
        if (configType == ConfigTypes::ShadowSocks) {
            containers.insert(configKey::ssxray, QJsonValue(lastConfig));
            containers.insert(configKey::container, QJsonValue(configKey::amneziaSsxray));
        } else {
            containers.insert(configKey::container, QJsonValue(configKey::amneziaXray));
            containers.insert(configKey::xray, QJsonValue(lastConfig));
        }

        QJsonArray arr;
        arr.push_back(containers);

        QString hostName;

        const static QRegularExpression hostNameRegExp("\"address\":\\s*\"([^\"]+)");
        QRegularExpressionMatch hostNameMatch = hostNameRegExp.match(data);
        if (hostNameMatch.hasMatch()) {
            hostName = hostNameMatch.captured(1);
        }

        QJsonObject config;
        config[configKey::containers] = arr;
        config[configKey::defaultContainer] =
                (configType == ConfigTypes::ShadowSocks) ? configKey::amneziaSsxray : configKey::amneziaXray;
        if (description.isEmpty()) {
            config[configKey::description] = m_coreController->m_serversRepository->nextAvailableServerName();
        } else {
            config[configKey::description] = description;
        }
        config[configKey::hostName] = hostName;

        return config;
    }

private slots:
    void initTestCase()
    {
        QString testOrg = "AmneziaVPN-Test-" + QUuid::createUuid().toString();
        m_settings = new SecureQSettings(testOrg, "amnezia-client", nullptr, false);

        auto vpnConnection = QSharedPointer<VpnConnection>::create(nullptr, nullptr);

        m_coreController = new TestCoreController(vpnConnection, m_settings, nullptr, this);
    }

    void cleanupTestCase()
    {
        m_settings->clearSettings();
        delete m_coreController;
        delete m_settings;
    }

    void init()
    {
        m_settings->clearSettings();
        if (m_coreController->m_serversModel) {
            m_coreController->m_serversModel->updateModel(QVector<ServerDescription>(), QString{});
        }
    }

    void xhttpImportKeepsExtraAndAlpn()
    {
        QString alias, error;
        const auto uri = QStringLiteral("vless://00000000-0000-4000-8000-000000000001@192.0.2.1:443?type=xhttp&security=tls&sni=example.com&alpn=h2&host=cdn.example.com&path=%2Fa%252Fb&mode=packet-up&extra=%7B%22noGRPCHeader%22%3Atrue%2C%22downloadSettings%22%3A%7B%22address%22%3A%22download.example.com%22%7D%7D");
        const auto config = serialization::vless::Deserialize(uri, &alias, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error)); QVERIFY(!config.isEmpty());
        const auto stream = config.value("outbounds").toArray().first().toObject().value("streamSettings").toObject();
        QCOMPARE(stream.value("network").toString(), QStringLiteral("xhttp"));
        const auto xhttp = stream.value("xhttpSettings").toObject();
        QCOMPARE(xhttp.value("path").toString(), QStringLiteral("/a%2Fb"));
        QCOMPARE(xhttp.value("mode").toString(), QStringLiteral("packet-up"));
        QCOMPARE(xhttp.value("extra").toObject().value("downloadSettings").toObject().value("address").toString(), QStringLiteral("download.example.com"));
        QCOMPARE(stream.value("tlsSettings").toObject().value("alpn").toArray(), QJsonArray { QStringLiteral("h2") });
        error.clear();
        QVERIFY(serialization::vless::Deserialize(QStringLiteral("vless://id@192.0.2.1:443?type=xhttp&extra=invalid"), &alias, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void websocketTlsImportKeepsFingerprint()
    {
        QString alias, error;
        const auto uri = QStringLiteral("vless://00000000-0000-4000-8000-000000000001@192.0.2.1:443?type=ws&security=tls&sni=example.com&path=%2Fws&fp=firefox");
        const auto config = serialization::vless::Deserialize(uri, &alias, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto stream = config.value("outbounds").toArray().first().toObject().value("streamSettings").toObject();
        QCOMPARE(stream.value("tlsSettings").toObject().value("fingerprint").toString(), QStringLiteral("firefox"));
    }

    void testVless()
    {
        const QString configData = getEnvValue("THIRD_PARTY_VLESS_IMPORT_DATA");
        if (!isEnvValueConfigured(configData)) {
            QSKIP("THIRD_PARTY_VLESS_IMPORT_DATA is not configured");
        }

        ImportController::ImportResult importResult;
        m_coreController->m_importCoreController->extractConfigFromData(configData);

        QString config = configData;
        QString prefix;
        QString errormsg;
        ConfigTypes configType = ConfigTypes::Invalid;

        if (config.startsWith("vless://")) {
            configType = ConfigTypes::Xray;
            importResult.config = extractXrayConfig(
                Utils::JsonToString(serialization::vless::Deserialize(config, &prefix, &errormsg), QJsonDocument::JsonFormat::Compact),
                configType, prefix);
            QVERIFY2(!importResult.config.empty(), "Config shouldn't be empty");
        } else {
            QSKIP("Config not starts with vless://");
        }

        QCOMPARE(importResult.config, config);
    }

    void testVmessNew()
    {
        const QString configData = getEnvValue("THIRD_PARTY_VMESS_NEW_IMPORT_DATA");
        if (!isEnvValueConfigured(configData)) {
            QSKIP("THIRD_PARTY_VMESS_NEW_IMPORT_DATA is not configured");
        }

        QString clientName = "Test Client (vmess_new deserialization)";

        ImportController::ImportResult importResult;
        
        m_coreController->m_importCoreController->extractConfigFromData(configData);

        QString config = configData;
        QString prefix;
        QString errormsg;
        ConfigTypes configType = ConfigTypes::Invalid;

        if (config.startsWith("vmess://") && config.contains("@")) {
            configType = ConfigTypes::Xray;
            importResult.config = extractXrayConfig(
                Utils::JsonToString(serialization::vmess_new::Deserialize(config, &prefix, &errormsg), QJsonDocument::JsonFormat::Compact),
                configType, prefix);
            QVERIFY2(!importResult.config.empty(), "Config shouldn't be empty");
        } else {
            QSKIP("Config not starts with vmess:// or not contain @");
        }

        QCOMPARE(importResult.config, config);
    }

    void testVmess()
    {
        const QString configData = getEnvValue("THIRD_PARTY_VMESS_IMPORT_DATA");
        if (!isEnvValueConfigured(configData)) {
            QSKIP("THIRD_PARTY_VMESS_IMPORT_DATA is not configured");
        }

        QString clientName = "Test Client (vmess deserialization)";

        ImportController::ImportResult importResult;

        m_coreController->m_importCoreController->extractConfigFromData(configData);

        QString config = configData;
        QString prefix;
        QString errormsg;
        ConfigTypes configType = ConfigTypes::Invalid;

        if (config.startsWith("vmess://")) {
            configType = ConfigTypes::Xray;
            importResult.config = extractXrayConfig(
                Utils::JsonToString(serialization::vmess::Deserialize(config, &prefix, &errormsg), QJsonDocument::JsonFormat::Compact),
                configType, prefix);
            QVERIFY2(!importResult.config.empty(), "Config shouldn't be empty");
        } else {
            QSKIP("Config not starts with vmess://");
        }

        QCOMPARE(importResult.config, config);
    }

    void testTrojan()
    {
        const QString configData = getEnvValue("THIRD_PARTY_TROJAN_IMPORT_DATA");
        if (!isEnvValueConfigured(configData)) {
            QSKIP("THIRD_PARTY_TROJAN_IMPORT_DATA is not configured");
        }

        QString clientName = "Test Client (trojan deserialization)";

        ImportController::ImportResult importResult;

        m_coreController->m_importCoreController->extractConfigFromData(configData);

        QString config = configData;
        QString prefix;
        QString errormsg;
        ConfigTypes configType = ConfigTypes::Invalid;

        if (config.startsWith("trojan://")) {
            configType = ConfigTypes::Xray;
            importResult.config = extractXrayConfig(
                Utils::JsonToString(serialization::trojan::Deserialize(config, &prefix, &errormsg), QJsonDocument::JsonFormat::Compact),
                configType, prefix);
            QVERIFY2(!importResult.config.empty(), "Config shouldn't be empty");
        } else {
            QSKIP("Config not starts with trojan://");
        }

        QCOMPARE(importResult.config, config);
    }

    void testSS()
    {
        const QString configData = getEnvValue("THIRD_PARTY_SHADOWSOCKS_IMPORT_DATA");
        if (!isEnvValueConfigured(configData)) {
            QSKIP("THIRD_PARTY_SHADOWSOCKS_IMPORT_DATA is not configured");
        }

        QString clientName = "Test Client (ss deserialization)";

        ImportController::ImportResult importResult;

        m_coreController->m_importCoreController->extractConfigFromData(configData);

        QString config = configData;
        QString prefix;
        QString errormsg;
        ConfigTypes configType = ConfigTypes::Invalid;

        if (config.startsWith("ss://") && !config.contains("plugin=")) {
            configType = ConfigTypes::ShadowSocks;
            importResult.config = extractXrayConfig(
                Utils::JsonToString(serialization::ss::Deserialize(config, &prefix, &errormsg), QJsonDocument::JsonFormat::Compact),
                configType, prefix);
            QVERIFY2(!importResult.config.empty(), "Config shouldn't be empty");
        } else {
            QSKIP("Config not starts with ss:// or contain plugin=");
        }

        QCOMPARE(importResult.config, config);
    }

    void testSSd()
    {
        const QString configData = getEnvValue("THIRD_PARTY_SHADOWSOCKS_SUBSCRIPTION_IMPORT_DATA");
        if (!isEnvValueConfigured(configData)) {
            QSKIP("THIRD_PARTY_SHADOWSOCKS_SUBSCRIPTION_IMPORT_DATA is not configured");
        }

        QString clientName = "Test Client (ssd deserialization)";

        ImportController::ImportResult importResult;

        m_coreController->m_importCoreController->extractConfigFromData(configData);

        QString config = configData;
        QString prefix;
        QString errormsg;
        ConfigTypes configType = ConfigTypes::Invalid;

        if (config.startsWith("ssd://")) {
            QStringList tmp;
            QList<std::pair<QString, QJsonObject>> servers = serialization::ssd::Deserialize(config, &prefix, &tmp);
            configType = ConfigTypes::ShadowSocks;
            // Took only first config from list
            if (!servers.isEmpty()) {
                importResult.config = extractXrayConfig(servers.first().first, configType);
            }
            if (!importResult.config.empty()) {
                importResult.configType = configType;
            }
            QVERIFY2(!importResult.config.empty(), "Config shouldn't be empty");
        } else {
            QSKIP("Config not starts with ssd://");
        }

        QCOMPARE(importResult.config, config);
    }
};

QTEST_MAIN(TestXraySerialization)
#include "testXraySerialization.moc"

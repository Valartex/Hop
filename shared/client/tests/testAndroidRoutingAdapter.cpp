#include <QtTest>
#include <QJsonDocument>
#include <QJsonArray>
#include "platforms/android/native/routingAdapter.h"
#include "core/routing/trafficPolicy.h"

class AndroidRoutingAdapterTest : public QObject {
    Q_OBJECT
private slots:
    void awgUsesOneXrayFrontendAndKeepsOriginalProfile()
    {
        const auto policy = hop::compileTrafficPolicy(QJsonObject { { "default", "vpn" }, { "rules", QJsonArray{} } });
        QVERIFY(policy.isValid());
        const QJsonObject source { { "protocol", "awg" }, { "hostName", "192.0.2.1" }, { "dns1", "10.8.0.1" },
            { "awg_config_data", QJsonObject { { "config", "synthetic profile" } } }, { "hopTrafficPolicy", hop::serializeTrafficPolicy(policy) } };
        const auto result = hop::prepareAndroidRouting(source);
        QVERIFY2(result.isValid(), qPrintable(result.error));
        QCOMPARE(result.configuration.value("protocol").toString(), QStringLiteral("xray"));
        QCOMPARE(result.configuration.value("hopAwgConfig").toString(), QStringLiteral("synthetic profile"));
        QCOMPARE(result.configuration.value("dns1").toString(), QStringLiteral("198.18.0.1"));
        const auto json = QJsonDocument::fromJson(result.configuration.value("xray_config_data").toObject().value("config").toString().toUtf8()).object();
        const auto vpn = json.value("outbounds").toArray().first().toObject();
        QCOMPARE(vpn.value("streamSettings").toObject().value("sockopt").toObject().value("interface").toString(), QStringLiteral("hop-awg"));
        QCOMPARE(json.value("routing").toObject().value("rules").toArray().last().toObject().value("outboundTag").toString(), QStringLiteral("hop-vpn"));
    }
    void missingProfileFailsBeforeStartingVpn()
    {
        const auto policy = hop::compileTrafficPolicy(QJsonObject { { "default", "vpn" } });
        const auto result = hop::prepareAndroidRouting(QJsonObject { { "protocol", "awg" }, { "dns1", "10.8.0.1" }, { "hopTrafficPolicy", hop::serializeTrafficPolicy(policy) } });
        QVERIFY(!result.isValid()); QVERIFY(!result.error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(AndroidRoutingAdapterTest)
#include "testAndroidRoutingAdapter.moc"

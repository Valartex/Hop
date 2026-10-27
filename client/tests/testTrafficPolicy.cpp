#include <QtTest>
#include <QJsonDocument>
#include <amnezia_xray.h>
#include "core/routing/trafficPolicy.h"
#include "core/routing/localSubnetRoutes.h"
#include "../../common/routing/privateRoutePolicy.h"

namespace {
QJsonObject rule(const QString &kind, const QString &value, const QString &target) {
    return { { "kind", kind }, { "value", value }, { "target", target } };
}
QJsonObject policy(QJsonArray rules, const QString &target = QStringLiteral("vpn")) {
    return { { "default", target }, { "rules", rules } };
}
QJsonObject base() {
    return QJsonDocument::fromJson(R"({"inbounds":[{"listen":"127.0.0.1","port":44972,"protocol":"socks","settings":{"udp":true}}],
        "outbounds":[{"tag":"old","protocol":"vless","settings":{"vnext":[{"address":"192.0.2.1","port":443,
        "users":[{"id":"00000000-0000-4000-8000-000000000001","encryption":"none"}]}]},
        "streamSettings":{"network":"tcp","security":"none"}}],"routing":{"rules":[{"outboundTag":"untrusted"}]}})").object();
}
}
class TrafficPolicyTest : public QObject {
    Q_OBJECT
private slots:
    void directPrivateRoutesPreserveOrderAndNeverPermitPublicOrIpv6()
    {
        const auto direct = [](const QJsonArray &ips) { return QJsonObject{{"type", "field"}, {"ip", ips}, {"outboundTag", "hop-direct"}}; };
        const QJsonObject snapshot{{"rules", QJsonArray{direct({"10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "0.0.0.0/0", "1.1.1.1/32", "fc00::/7"})}}};
        const auto allowed = hop::directPrivatePrefixes(snapshot);
        QCOMPARE(allowed.size(), 3);
        QVERIFY(hop::privatePrefixUsesSystemRoute("192.168.1.0/25", allowed));
        QVERIFY(!hop::privatePrefixUsesSystemRoute("192.0.0.0/8", allowed));
        QVERIFY(!hop::privatePrefixUsesSystemRoute("1.1.1.1/32", allowed));
        for (const auto &first : {QJsonObject{{"type", "field"}, {"ip", QJsonArray{"10.1.0.0/16"}}, {"outboundTag", "hop-block"}},
                                  QJsonObject{{"type", "field"}, {"domain", QJsonArray{"full:test.invalid"}}, {"outboundTag", "hop-direct"}}}) {
            QVERIFY(hop::directPrivatePrefixes(QJsonObject{{"rules", QJsonArray{first, direct({"10.0.0.0/8"})}}}).isEmpty());
        }
    }
    void lanCapturePreservesOriginalSubnetAndOverridesItWithTwoChildren() {
        QNetworkAddressEntry lan;
        lan.setIp(QHostAddress("192.168.1.55")); lan.setPrefixLength(24);
        QCOMPARE(hop::localSubnetCaptureRoutes({ lan, lan }),
            QStringList({ "192.168.1.0/25", "192.168.1.128/25" }));
        lan.setIp(QHostAddress("10.0.2.15"));
        QCOMPARE(hop::localSubnetCaptureRoutes({ lan }), QStringList({ "10.0.2.0/25", "10.0.2.128/25" }));
        for (const int prefix : { 0, 31, 32 }) {
            lan.setPrefixLength(prefix); QVERIFY(hop::localSubnetCaptureRoutes({ lan }).isEmpty());
        }
        lan.setIp(QHostAddress("127.0.0.1")); lan.setPrefixLength(8);
        QVERIFY(hop::localSubnetCaptureRoutes({ lan }).isEmpty());
        lan.setIp(QHostAddress("fe80::1")); lan.setPrefixLength(64);
        QVERIFY(hop::localSubnetCaptureRoutes({ lan }).isEmpty());
    }
    void regularExpressionsUseTheActualCoreBeforePublication() {
        for (const auto &pattern : { "regexp:[", "regexp:(?=lookahead)", "regexp:(a)\\1" }) {
            const auto compiled = hop::compileTrafficPolicy(policy({ rule("domain", pattern, "block") }));
            QVERIFY(!compiled.isValid()); QVERIFY(compiled.rules.isEmpty());
            QJsonObject snapshot { { "default", "vpn" }, { "rules", QJsonArray { QJsonObject {
                { "type", "field" }, { "outboundTag", "hop-block" }, { "domain", QJsonArray { pattern } }
            } } } };
            QVERIFY(!hop::deserializeTrafficPolicy(snapshot).isValid());
        }
        const auto valid = hop::compileTrafficPolicy(policy({ rule("domain", "regexp:^([a-z]+\\.)?example\\.com$", "block") }));
        QVERIFY2(valid.isValid(), qPrintable(valid.error));
        const auto badDat = hop::parseGeoData(QByteArray::fromHex("0a0b0a0252551205080112015b"), hop::GeoDataKind::GeoSite);
        QVERIFY2(badDat.isValid(), qPrintable(badDat.error));
        QVERIFY(!hop::compileTrafficPolicy(policy({ rule("geosite", "RU", "block") }), nullptr, &badDat).isValid());
        const auto badDomain = hop::parseGeoData(QByteArray::fromHex("0a100a025255120a08031206612f70617468"), hop::GeoDataKind::GeoSite);
        QVERIFY2(badDomain.isValid(), qPrintable(badDomain.error));
        QVERIFY(!hop::compileTrafficPolicy(policy({ rule("geosite", "RU", "block") }), nullptr, &badDomain).isValid());
    }
    void serializedSnapshotCannotAddAnOutboundOrTrustFlags() {
        const auto original = hop::compileTrafficPolicy(policy({ rule("domain", "example.com", "direct") }));
        auto snapshot = hop::serializeTrafficPolicy(original);
        snapshot.insert("needsDirect", false); snapshot.insert("hasDomains", false);
        const auto compiled = hop::deserializeTrafficPolicy(snapshot);
        QVERIFY(compiled.isValid()); QVERIFY(compiled.needsDirect); QVERIFY(compiled.hasDomains);
        auto rules = snapshot.value("rules").toArray(); auto changed = rules.first().toObject();
        changed.insert("outboundTag", "foreign-outbound"); rules[0] = changed; snapshot.insert("rules", rules);
        QVERIFY(!hop::deserializeTrafficPolicy(snapshot).isValid());
        changed.insert("outboundTag", "hop-vpn"); changed.insert("port", "443"); rules[0] = changed; snapshot.insert("rules", rules);
        QVERIFY(!hop::deserializeTrafficPolicy(snapshot).isValid());
    }
    void orderedRulesAndDefault() {
        const auto compiled = hop::compileTrafficPolicy(policy({ rule("domain", "FULL:invalid", "vpn") }));
        QVERIFY(!compiled.isValid());
        const auto valid = hop::compileTrafficPolicy(policy({ rule("domain", "example.com", "vpn"),
            rule("ip", "192.0.2.0/24", "direct"), rule("domain", "full:blocked.example", "block") }, "block"));
        QVERIFY2(valid.isValid(), qPrintable(valid.error));
        QVERIFY(valid.hasDomains); QVERIFY(valid.needsDirect);
        QCOMPARE(valid.defaultTarget, QStringLiteral("block")); QCOMPARE(valid.rules.size(), 3);
        QCOMPARE(valid.rules.at(0).toObject().value("domain").toArray().first().toString(), QStringLiteral("domain:example.com"));
        QCOMPARE(valid.rules.at(1).toObject().value("outboundTag").toString(), QStringLiteral("hop-direct"));
    }
    void malformedDoesNotPartiallyApply() {
        const auto valid = rule("domain", "example.com", "vpn");
        for (const auto &invalid : { rule("ip", "nonsense", "direct"), rule("domain", "https://example.com", "direct"),
            rule("domain", "a.example", "unknown"), rule("geosite", "missing", "block"), rule("domain", "*.example.com", "direct") }) {
            const auto compiled = hop::compileTrafficPolicy(policy({ valid, invalid }));
            QVERIFY(!compiled.isValid()); QVERIFY(compiled.rules.isEmpty());
        }
        QVERIFY(!hop::compileTrafficPolicy(QJsonObject { { "default", 5 } }).isValid());
        QVERIFY(!hop::compileTrafficPolicy(QJsonObject { { "rules", "invalid" } }).isValid());
    }
    void datPreservesPatterns() {
        // CATEGORY с regexp, domain и full: protobuf не превращается в снимок IP.
        const auto data = hop::parseGeoData(QByteArray::fromHex("0a260a025255120b08011207615b305d2e7275120b0802120765782e746573741206080312026178"), hop::GeoDataKind::GeoSite);
        QVERIFY2(data.isValid(), qPrintable(data.error));
        const auto compiled = hop::compileTrafficPolicy(policy({ rule("geosite", "RU", "block") }), nullptr, &data);
        QVERIFY2(compiled.isValid(), qPrintable(compiled.error));
        const auto patterns = compiled.rules.first().toObject().value("domain").toArray();
        QCOMPARE(patterns, QJsonArray({ "regexp:a[0].ru", "domain:ex.test", "full:ax" }));
    }
    void boundedInput() {
        QJsonArray rules;
        for (int index = 0; index < 513; ++index) { rules.append(rule("ip", "192.0.2.0/24", "vpn")); }
        QVERIFY(!hop::compileTrafficPolicy(policy(rules)).isValid());
        QVERIFY(!hop::compileTrafficPolicy(policy({ rule("domain", QString(4097, 'a'), "vpn") })).isValid());
    }
    void dnsAndImportedTransport() {
        auto source = base();
        auto outbounds = source.value("outbounds").toArray();
        auto outbound = outbounds.first().toObject();
        auto stream = outbound.value("streamSettings").toObject();
        const QJsonObject reality { { "serverName", "allowed.example" }, { "publicKey", "synthetic" }, { "shortId", "abcd" } };
        stream.insert("realitySettings", reality); outbound.insert("streamSettings", stream); outbounds[0] = outbound; source.insert("outbounds", outbounds);
        const auto compiled = hop::compileTrafficPolicy(policy({ rule("domain", "example.com", "block") }));
        QString error;
        const auto document = hop::applyXrayTrafficPolicy(source, compiled, "9.9.9.9", &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(document.value("outbounds").toArray().first().toObject().value("streamSettings").toObject().value("realitySettings").toObject(), reality);
        QCOMPARE(document.value("dns").toObject().value("servers").toArray(), QJsonArray({ "fakedns", "9.9.9.9" }));
        const auto routes = document.value("routing").toObject().value("rules").toArray();
        QCOMPARE(routes.first().toObject().value("inboundTag").toArray(), QJsonArray({ "hop-dns" }));
        QCOMPARE(routes.first().toObject().value("outboundTag").toString(), QStringLiteral("hop-vpn"));
        QCOMPARE(routes.at(1).toObject().value("outboundTag").toString(), QStringLiteral("hop-dns-out"));
        const auto dnsOut = document.value("outbounds").toArray().at(3).toObject();
        QCOMPARE(dnsOut.value("proxySettings").toObject().value("tag").toString(), QStringLiteral("hop-vpn"));
        QCOMPARE(dnsOut.value("settings").toObject().value("rewriteAddress").toString(), QStringLiteral("9.9.9.9"));
        QVERIFY(!QJsonDocument(document).toJson().contains("untrusted"));
        QVERIFY(hop::applyXrayTrafficPolicy(source, compiled, "localhost", &error).isEmpty());
    }
    void coreAcceptsGeneratedConfiguration() {
        for (const bool domains : { false, true }) {
            const auto compiled = hop::compileTrafficPolicy(policy(domains ? QJsonArray { rule("domain", "example.com", "block") }
                : QJsonArray { rule("ip", "192.0.2.0/24", "block") }));
            QString error;
            const auto config = hop::applyXrayTrafficPolicy(base(), compiled, "9.9.9.9", &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            auto bytes = QJsonDocument(config).toJson(QJsonDocument::Compact);
            auto *failure = amnezia_xray_configure(bytes.data());
            const auto detail = failure ? QString::fromUtf8(failure) : QString();
            if (failure) { amnezia_xray_free(failure); }
            QVERIFY2(detail.isEmpty(), qPrintable(detail));
            QCOMPARE(config.contains("fakedns"), domains);
        }
    }
};
QTEST_GUILESS_MAIN(TrafficPolicyTest)
#include "testTrafficPolicy.moc"

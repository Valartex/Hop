#include "routingAdapter.h"
#include <QJsonArray>
#include <QJsonDocument>
#include "core/routing/trafficPolicy.h"
#include "core/protocols/protocolUtils.h"
#include "core/utils/constants/configKeys.h"

namespace hop {
AndroidRoutingResult prepareAndroidRouting(const QJsonObject &configuration)
{
    auto result = configuration;
    if (!configuration.contains(QStringLiteral("hopTrafficPolicy"))) { return { result, {} }; }
    const auto policy = deserializeTrafficPolicy(configuration.value(QStringLiteral("hopTrafficPolicy")).toObject());
    if (!policy.isValid()) { return { {}, policy.error }; }
    const auto protocol = configuration.value(amnezia::configKey::vpnProto).toString();
    const bool awg = protocol == QStringLiteral("awg") || protocol == QStringLiteral("wireguard");
    const auto key = amnezia::ProtocolUtils::key_proto_config_data(protocol == QStringLiteral("ssxray") ? amnezia::Proto::SSXray : amnezia::Proto::Xray);
    QJsonObject base;
    if (awg) {
        const auto source = configuration.value(amnezia::ProtocolUtils::key_proto_config_data(protocol == QStringLiteral("awg") ? amnezia::Proto::Awg : amnezia::Proto::WireGuard)).toObject();
        const auto profile = source.value(amnezia::configKey::config).toString();
        if (profile.isEmpty()) { return { {}, QStringLiteral("AWG profile unavailable") }; }
        result.insert(QStringLiteral("hopAwgConfig"), profile);
        result.insert(amnezia::configKey::vpnProto, QStringLiteral("xray"));
        base = QJsonObject {
            { QStringLiteral("inbounds"), QJsonArray { QJsonObject { { QStringLiteral("listen"), QStringLiteral("127.0.0.1") },
                { QStringLiteral("port"), 0 }, { QStringLiteral("protocol"), QStringLiteral("socks") },
                { QStringLiteral("settings"), QJsonObject { { QStringLiteral("udp"), true } } } } } },
            { QStringLiteral("outbounds"), QJsonArray { QJsonObject { { QStringLiteral("protocol"), QStringLiteral("freedom") },
                { QStringLiteral("tag"), QStringLiteral("hop-vpn") },
                { QStringLiteral("streamSettings"), QJsonObject { { QStringLiteral("sockopt"), QJsonObject {
                    { QStringLiteral("interface"), QStringLiteral("hop-awg") } } } } } } } }
        };
    } else {
        base = QJsonDocument::fromJson(configuration.value(key).toObject().value(amnezia::configKey::config).toString().toUtf8()).object();
    }
    QString error;
    const auto compiled = applyXrayTrafficPolicy(base, policy, configuration.value(amnezia::configKey::dns1).toString(), &error);
    if (compiled.isEmpty()) { return { {}, error }; }
    result.insert(amnezia::ProtocolUtils::key_proto_config_data(amnezia::Proto::Xray), QJsonObject {
        { amnezia::configKey::config, QString::fromUtf8(QJsonDocument(compiled).toJson(QJsonDocument::Compact)) } });
    result.insert(amnezia::configKey::dns1, QStringLiteral("198.18.0.1"));
    result.insert(amnezia::configKey::dns2, QStringLiteral("198.18.0.1"));
    result.insert(QStringLiteral("splitTunnelType"), 0);
    return { result, {} };
}
}

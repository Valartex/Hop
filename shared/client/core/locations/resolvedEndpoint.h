#pragma once
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonArray>
#include "core/protocols/protocolUtils.h"
#include "core/utils/constants/configKeys.h"

namespace hop {
// Подменяется только адрес транспорта одной попытки. Исходная подписка и параметры TLS/Reality сохраняются.
inline bool applyResolvedEndpoint(QJsonObject &configuration, amnezia::Proto protocol, const QString &address)
{
    const QHostAddress resolved(address);
    if (resolved.protocol() != QAbstractSocket::IPv4Protocol || resolved.isMulticast() || resolved.isBroadcast() || resolved.isNull()) { return false; }
    const auto original = configuration.value(amnezia::configKey::hostName).toString();
    const auto key = amnezia::ProtocolUtils::key_proto_config_data(protocol);
    auto wrapped = configuration.value(key).toObject();
    if (protocol == amnezia::Proto::Awg || protocol == amnezia::Proto::WireGuard) {
        wrapped.insert(amnezia::configKey::hostName, address);
    } else if (protocol == amnezia::Proto::Xray) {
        auto document = QJsonDocument::fromJson(wrapped.value(amnezia::configKey::config).toString().toUtf8()).object();
        auto outbounds = document.value(QStringLiteral("outbounds")).toArray();
        bool changed = false;
        for (int index = 0; index < outbounds.size(); ++index) {
            auto outbound = outbounds.at(index).toObject();
            if (outbound.value(QStringLiteral("protocol")).toString() != QStringLiteral("vless")) { continue; }
            auto settings = outbound.value(QStringLiteral("settings")).toObject();
            auto peers = settings.value(QStringLiteral("vnext")).toArray();
            for (int peer = 0; peer < peers.size(); ++peer) {
                auto endpoint = peers.at(peer).toObject();
                if (endpoint.value(QStringLiteral("address")).toString().compare(original, Qt::CaseInsensitive) != 0) { continue; }
                endpoint.insert(QStringLiteral("address"), address); peers[peer] = endpoint; changed = true;
            }
            settings.insert(QStringLiteral("vnext"), peers); outbound.insert(QStringLiteral("settings"), settings);
            auto stream = outbound.value(QStringLiteral("streamSettings")).toObject();
            if (stream.value(QStringLiteral("security")).toString() == QStringLiteral("tls") && QHostAddress(original).isNull()) {
                auto tls = stream.value(QStringLiteral("tlsSettings")).toObject();
                if (tls.value(QStringLiteral("serverName")).toString().isEmpty()) { tls.insert(QStringLiteral("serverName"), original); }
                stream.insert(QStringLiteral("tlsSettings"), tls); outbound.insert(QStringLiteral("streamSettings"), stream);
            }
            outbounds[index] = outbound;
        }
        if (!changed) { return false; }
        document.insert(QStringLiteral("outbounds"), outbounds);
        wrapped.insert(amnezia::configKey::config, QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact)));
    } else { return false; }
    configuration.insert(key, wrapped);
    configuration.insert(amnezia::configKey::hostName, address);
    return true;
}
}

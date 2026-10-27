#pragma once
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

namespace hop {
// Только начальные безусловные IP-правила Direct допускают системную маршрутизацию RFC1918.
// Доменное правило или другое направление останавливает оптимизацию: порядок сохраняется.
inline QStringList directPrivatePrefixes(const QJsonObject &snapshot)
{
    QStringList result;
    const QList<QPair<QHostAddress, int>> privateNetworks = {
        {QHostAddress("10.0.0.0"), 8}, {QHostAddress("172.16.0.0"), 12}, {QHostAddress("192.168.0.0"), 16}};
    for (const auto &value : snapshot.value("rules").toArray()) {
        const auto rule = value.toObject();
        if (rule.value("outboundTag") != "hop-direct" || !rule.value("ip").isArray() || rule.contains("domain")) { break; }
        for (const auto &value : rule.value("ip").toArray()) {
            const auto prefix = QHostAddress::parseSubnet(value.toString());
            if (prefix.first.protocol() != QAbstractSocket::IPv4Protocol || prefix.second < 0) { continue; }
            for (const auto &network : privateNetworks) {
                if (prefix.second >= network.second && prefix.first.isInSubnet(network)) {
                    const auto normalized = prefix.first.toString() + '/' + QString::number(prefix.second);
                    if (!result.contains(normalized)) { result.append(normalized); }
                    // Не создаём тысячи WFP-фильтров из произвольного набора.
                    if (result.size() > 64) { return {}; }
                    break;
                }
            }
        }
    }
    return result;
}
inline bool privatePrefixUsesSystemRoute(const QString &text, const QStringList &allowed)
{
    const auto prefix = QHostAddress::parseSubnet(text);
    if (prefix.first.protocol() != QAbstractSocket::IPv4Protocol || prefix.second < 0) { return false; }
    for (const auto &text : allowed) {
        const auto network = QHostAddress::parseSubnet(text);
        if (network.second >= 0 && prefix.second >= network.second && prefix.first.isInSubnet(network)) { return true; }
    }
    return false;
}
}

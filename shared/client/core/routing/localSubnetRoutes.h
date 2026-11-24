#pragma once

#include <QHostAddress>
#include <QNetworkInterface>
#include <QStringList>

namespace hop {
inline QStringList localSubnetCaptureRoutes(const QList<QNetworkAddressEntry> &entries)
{
    QStringList routes;
    for (const auto &entry : entries) {
        const auto address = entry.ip();
        const int prefix = entry.prefixLength();
        if (address.protocol() != QAbstractSocket::IPv4Protocol || address.isLoopback()
            || address.isNull() || address.isMulticast() || prefix < 1 || prefix > 30) { continue; }
        const quint32 mask = 0xffffffffu << (32 - prefix);
        const quint32 network = address.toIPv4Address() & mask;
        const quint32 half = quint32(1) << (31 - prefix);
        // Две более точные строки перехватывают on-link трафик, не меняя исходный маршрут LAN.
        for (const auto destination : { network, network | half }) {
            const auto route = QHostAddress(destination).toString() + QLatin1Char('/') + QString::number(prefix + 1);
            if (!routes.contains(route)) { routes.append(route); }
        }
    }
    return routes;
}
}

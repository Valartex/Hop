#pragma once
#include <QJsonObject>
#include <QUrl>
#include <QStringDecoder>

namespace hop {
inline bool hasCustomSubscriptionName(const QJsonObject &metadata)
{
    const auto mode = metadata.value(QStringLiteral("sourceNameMode")).toString();
    if (mode == QStringLiteral("custom")) { return true; }
    if (mode == QStringLiteral("automatic")) { return false; }
    // Старые версии не отличали пользовательское имя от подставленного hostname.
    const auto stored = metadata.value(QStringLiteral("sourceName")).toString().trimmed();
    return !stored.isEmpty() && stored.compare(QUrl(metadata.value(QStringLiteral("sourceUrl")).toString()).host(), Qt::CaseInsensitive) != 0;
}

inline QString subscriptionName(const QUrl &url, const QString &stored)
{
    return stored.trimmed().isEmpty() ? url.host() : stored.trimmed();
}

inline QString subscriptionName(const QJsonObject &metadata)
{
    return subscriptionName(QUrl(metadata.value(QStringLiteral("sourceUrl")).toString()),
        metadata.value(QStringLiteral("sourceName")).toString());
}

inline QString validatedSubscriptionTitle(const QString &title)
{
    if (title.size() > 120) { return {}; }
    for (const auto ch : title) {
        if (ch.category() == QChar::Other_Control || ch == QChar(0x2028) || ch == QChar(0x2029)) { return {}; }
    }
    return title.trimmed();
}

inline QString decodeSubscriptionTitle(QByteArray header)
{
    if (header.size() > 1024 || header.contains('\r') || header.contains('\n')) { return {}; }
    header = header.trimmed();
    if (header.left(7).toLower() == QByteArrayLiteral("base64:")) {
        const auto decoded = QByteArray::fromBase64Encoding(header.mid(7), QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded) { return {}; }
        header = decoded.decoded;
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString title = decoder(header);
    return decoder.hasError() ? QString() : validatedSubscriptionTitle(title);
}
}

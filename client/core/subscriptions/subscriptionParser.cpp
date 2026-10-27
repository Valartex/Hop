#include "subscriptionParser.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QHash>
#include <QSet>
#include <QStringDecoder>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

namespace hop {
namespace {
QString message(const char *text)
{
    return QCoreApplication::translate("HopSubscriptions", text);
}

QString digest(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString decodeUtf8(const QByteArray &data, bool &valid)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString result = decoder(data);
    valid = !decoder.hasError();
    return result;
}
}

SubscriptionParseResult parseSubscription(const QByteArray &body)
{
    if (body.size() > maximumSubscriptionBytes) {
        return { {}, message("Подписка превышает допустимый размер") };
    }
    QByteArray data = body.trimmed();
    if (data.startsWith("\xef\xbb\xbf")) {
        data.remove(0, 3);
    }
    if (!data.startsWith("vless://")) {
        // Переводы строк допустимы внутри Base64, остальные символы проверяет декодер.
        QByteArray encoded;
        encoded.reserve(data.size());
        for (const char character : data) {
            if (character != ' ' && character != '\r' && character != '\n' && character != '\t') {
                encoded.append(character);
            }
        }
        const auto alphabet = encoded.contains('-') || encoded.contains('_')
            ? QByteArray::Base64UrlEncoding : QByteArray::Base64Encoding;
        const auto decoded = QByteArray::fromBase64Encoding(encoded, alphabet | QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded) {
            return { {}, message("Ожидается список VLESS или его Base64-представление") };
        }
        data = decoded.decoded;
    }
    bool validUtf8 = false;
    const auto text = decodeUtf8(data, validUtf8);
    if (!validUtf8) {
        return { {}, message("Подписка содержит некорректный UTF-8") };
    }

    SubscriptionParseResult result;
    QSet<QString> identicalUris;
    const auto lines = text.split(QLatin1Char('\n'));
    for (qsizetype lineNumber = 0; lineNumber < lines.size(); ++lineNumber) {
        const auto line = lines.at(lineNumber).trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const QUrl uri(line, QUrl::StrictMode);
        if (!line.startsWith("vless://") || !uri.isValid() || uri.host().isEmpty()
            || uri.userName().isEmpty() || !uri.password().isEmpty() || uri.port() < 1
            || line.size() > 65536 || line.contains(QChar::Null)) {
            return { {}, message("Некорректная запись VLESS в строке %1").arg(lineNumber + 1) };
        }
        if (identicalUris.contains(line)) {
            continue;
        }
        identicalUris.insert(line);
        if (result.entries.size() >= maximumSubscriptionEntries) {
            return { {}, message("В подписке слишком много записей") };
        }
        const QUrlQuery query(uri);
        QUrl connectionUri(uri);
        connectionUri.setFragment(QString());
        const QString name = uri.fragment(QUrl::FullyDecoded).trimmed();
        const QString transport = query.hasQueryItem(QStringLiteral("type"))
            ? query.queryItemValue(QStringLiteral("type")) : QStringLiteral("tcp");
        const QString security = query.hasQueryItem(QStringLiteral("security"))
            ? query.queryItemValue(QStringLiteral("security")) : QStringLiteral("none");
        SubscriptionEntry entry;
        entry.uri = line;
        entry.name = name.isEmpty() ? uri.host() : name;
        entry.connectionKey = digest(connectionUri.toString(QUrl::FullyEncoded));
        // Имя является дополнительным ключом сопоставления, а не постоянным ID.
        if (!name.isEmpty()) {
            entry.nameKey = digest(name + QChar::Null + transport + QChar::Null + security);
        }
        result.entries.append(entry);
    }
    if (result.entries.isEmpty()) {
        return { {}, message("Подписка не содержит серверов") };
    }
    return result;
}

SubscriptionMatchResult matchSubscriptionEntries(const QVector<SubscriptionEntry> &incoming,
                                                  const QVector<PreviousSubscriptionEntry> &previous)
{
    QHash<QString, QVector<QString>> byConnection;
    QHash<QString, QVector<QString>> byName;
    QHash<QString, int> incomingConnections;
    QHash<QString, int> incomingNames;
    QSet<QString> previousIds;
    for (const auto &entry : previous) {
        if (entry.serverId.isEmpty() || previousIds.contains(entry.serverId)) {
            return { {}, message("Повреждены идентификаторы сохранённой подписки") };
        }
        previousIds.insert(entry.serverId);
        byConnection[entry.connectionKey].append(entry.serverId);
        if (!entry.nameKey.isEmpty()) {
            byName[entry.nameKey].append(entry.serverId);
        }
    }
    for (const auto &entry : incoming) {
        ++incomingConnections[entry.connectionKey];
        if (!entry.nameKey.isEmpty()) {
            ++incomingNames[entry.nameKey];
        }
    }
    SubscriptionMatchResult result;
    QSet<QString> usedIds;
    for (const auto &entry : incoming) {
        QString id;
        const auto exact = byConnection.value(entry.connectionKey);
        const auto named = byName.value(entry.nameKey);
        // Точное соединение сохраняет ID при смене подписи провайдером.
        if (exact.size() == 1 && incomingConnections.value(entry.connectionKey) == 1) {
            id = exact.first();
        } else if (!entry.nameKey.isEmpty() && named.size() == 1 && incomingNames.value(entry.nameKey) == 1) {
            // Однозначная пара метаданных сохраняет ID при ротации ключей/адресов.
            id = named.first();
        } else if (!previous.isEmpty() && (!exact.isEmpty() || !named.isEmpty())) {
            return { {}, message("Невозможно однозначно сопоставить локации обновлённой подписки") };
        }
        if (id.isEmpty()) {
            id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        if (usedIds.contains(id)) {
            return { {}, message("Обновление неоднозначно сопоставляет несколько локаций с одной") };
        }
        usedIds.insert(id);
        result.serverIds.append(id);
    }
    return result;
}
} // namespace hop

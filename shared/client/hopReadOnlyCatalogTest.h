#pragma once
#include <QDataStream>
#include <QSettings>
#include "cryptoUtils.h"
#include "version.h"

namespace hop {
// Только VM-тест: читаем существующие данные без SecureServersRepository,
// поскольку его конструктор сохраняет defaultServerId. Ключи остаются в памяти.
inline QJsonArray readExistingApiProfiles()
{
    QSettings saved(QStringLiteral(ORGANIZATION_NAME), QStringLiteral(APPLICATION_NAME));
    auto encoded = saved.value(QStringLiteral("Servers/serversList")).toByteArray();
    if (encoded.isEmpty() || encoded.size() > 8 * 1024 * 1024) { return {}; }
    if (encoded.startsWith("EncData")) {
        const auto readKey = [](const QString &tag) {
            QKeychain::ReadPasswordJob job(QStringLiteral(CLIENT_KEYCHAIN_NAME_STR));
            job.setAutoDelete(false); job.setKey(tag);
            QEventLoop loop; QTimer deadline; deadline.setSingleShot(true);
            QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
            QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
            deadline.start(5000); job.start(); loop.exec();
            return deadline.isActive() && !job.error() ? job.binaryData() : QByteArray();
        };
        auto key = readKey(QStringLiteral("settingsKeyTag"));
        auto iv = readKey(QStringLiteral("settingsIvTag"));
        if (key.isEmpty() || iv.isEmpty()) { return {}; }
        auto decrypted = CryptoUtils::decryptAes256Cbc(encoded.mid(7), key, iv);
        key.fill(0); iv.fill(0);
        QVariant plain; QDataStream stream(&decrypted, QIODevice::ReadOnly); stream >> plain;
        if (stream.status() != QDataStream::Ok) { return {}; }
        encoded = plain.toByteArray(); decrypted.fill(0);
    }
    QJsonArray result;
    for (const auto &item : QJsonDocument::fromJson(encoded).array()) {
        if (item.isObject() && serverConfigUtils::isApiV2Subscription(serverConfigUtils::configTypeFromJson(item.toObject()))) { result.append(item); }
    }
    encoded.fill(0);
    return result;
}
}

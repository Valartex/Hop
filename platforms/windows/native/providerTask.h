#pragma once
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTextStream>
#include <QUuid>
#ifdef Q_OS_WIN
#include <io.h>
#include <fcntl.h>
#endif
#include "core/controllers/api/subscriptionController.h"
#include "core/utils/constants/apiKeys.h"
#include "core/subscriptions/subscriptionFetcher.h"
#include "core/locations/controlPlaneResolver.h"
#include "core/locations/providerRequest.h"

namespace hop {
// Внутренний подпроцесс. Секреты передаются только по анонимным stdin/stdout, без дисковых настроек.
inline int runProviderTask(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
        // Контракт SDK исключает адреса и ключи из trace; остальные сообщения не выводим.
        if (message.startsWith(QStringLiteral("agw trace:"))) { QTextStream(stderr) << message << Qt::endl; }
    });
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) { return 2; }
    const auto bytes = input.readLine(2 * 1024 * 1024 + 2);
    if (bytes.size() > 2 * 1024 * 1024) { return 2; }
    const auto request = QJsonDocument::fromJson(bytes).object();
    const auto action = request.value(QStringLiteral("action")).toString();
    if (action == QStringLiteral("resolve")) {
        const auto host = request.value(QStringLiteral("host")).toString();
        const auto addresses = host.size() <= 253 ? controlPlane::resolve(host) : QStringList();
        const QJsonObject response { { QStringLiteral("code"), addresses.isEmpty() ? 1 : 0 },
            { QStringLiteral("addresses"), QJsonArray::fromStringList(addresses) } };
        QTextStream(stdout) << QJsonDocument(response).toJson(QJsonDocument::Compact) << Qt::endl;
        return addresses.isEmpty() ? 3 : 0;
    }
    if (action == QStringLiteral("subscription") || action == QStringLiteral("routes")) {
        const bool routes = action == QStringLiteral("routes");
        SubscriptionFetcher fetcher(nullptr, true, {}, routes ? SubscriptionFetcher::Payload::RouteData : SubscriptionFetcher::Payload::Subscription);
        QByteArray routeBytes;
        QJsonObject result;
        QObject::connect(&fetcher, &SubscriptionFetcher::loaded, &app, [&](const QByteArray &body, const QByteArray &etag, const QByteArray &modified, bool unchanged) {
            result = { { QStringLiteral("code"), 0 }, { QStringLiteral("body"), routes ? QString() : QString::fromLatin1(body.toBase64()) },
                { QStringLiteral("etag"), QString::fromLatin1(etag.toBase64()) }, { QStringLiteral("modified"), QString::fromLatin1(modified.toBase64()) },
                { QStringLiteral("unchanged"), unchanged } };
            if (!routes) { result.insert(QStringLiteral("profileTitle"), fetcher.profileTitle()); }
            if (routes) { routeBytes = body; result.remove(QStringLiteral("body")); result.insert(QStringLiteral("bytes"), body.size()); }
            app.quit();
        });
        QObject::connect(&fetcher, &SubscriptionFetcher::failed, &app, [&](const QString &message, int retryAfter) {
            result = { { QStringLiteral("code"), 1 }, { QStringLiteral("retryAfter"), retryAfter },
                { QStringLiteral("detail"), message } }; app.quit();
        });
        if (!fetcher.fetch(QUrl(request.value(QStringLiteral("url")).toString()),
            QByteArray::fromBase64(request.value(QStringLiteral("etag")).toString().toLatin1()),
            QByteArray::fromBase64(request.value(QStringLiteral("modified")).toString().toLatin1()))) {
            QTextStream(stdout) << "{\"code\":2,\"detail\":\"request-start\"}" << Qt::endl;
            return 2;
        }
        app.exec();
        if (routes) {
#ifdef Q_OS_WIN
            _setmode(_fileno(stdout), _O_BINARY);
#endif
            QFile stream;
            if (!stream.open(stdout, QIODevice::WriteOnly)) { return 3; }
            const auto header = QJsonDocument(result).toJson(QJsonDocument::Compact) + '\n';
            if (stream.write(header) != header.size() || stream.write(routeBytes) != routeBytes.size() || !stream.flush()) { return 3; }
        } else { QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << Qt::endl; }
        return result.value(QStringLiteral("code")).toInt(1) == 0 ? 0 : 3;
    }
    const auto response = executeProviderRequest(request);
    QTextStream(stdout) << QJsonDocument(response).toJson(QJsonDocument::Compact) << Qt::endl;
    return response.value(QStringLiteral("code")).toInt(-1) == 0 ? 0 : 3;
}
}

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
#include "controlPlaneResolver.h"

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
    if (action != QStringLiteral("prepare") && action != QStringLiteral("catalog")) { return 2; }
    // Чистый Go resolver SDK отправляет запросы от Hop.exe; разрешение ограничено DNS-адресами этой сети.
    for (const auto &resolver : controlPlane::resolvers()) {
        controlPlane::permit(resolver, 53, true);
        controlPlane::permit(resolver, 53, false);
    }
    const auto json = request.value(QStringLiteral("server")).toObject();
    if (QUuid(request.value(QStringLiteral("installation")).toString()).isNull()
        || request.value(QStringLiteral("country")).toString().size() > 128) { return 2; }
    const auto kind = serverConfigUtils::configTypeFromJson(json);
    if (!serverConfigUtils::isApiV2Subscription(kind)) { return 2; }
    SecureQSettings settings(QStringLiteral("Hop-Provider-") + QUuid::createUuid().toString(), QStringLiteral("memory"), nullptr, true, false);
    SecureServersRepository servers(&settings);
    SecureAppSettingsRepository appSettings(&settings);
    const auto state = QByteArray::fromBase64(request.value(QStringLiteral("gatewayState")).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!state.isEmpty() && state.size() <= 256 * 1024 && QJsonDocument::fromJson(state).isObject()) {
        appSettings.writeGatewayProxyUrls(QStringLiteral("agw_state"), state);
    }
    settings.setValue(QStringLiteral("Conf/installationUuid"), request.value(QStringLiteral("installation")).toString());
    const auto endpoint = request.value(QStringLiteral("gateway")).toString();
    if (!endpoint.isEmpty()) { appSettings.setGatewayEndpoint(endpoint); }
    appSettings.setStrictKillSwitchEnabled(request.value(QStringLiteral("strict")).toBool());
    const auto id = servers.addServer(QStringLiteral("provider-task"), json, kind);
    if (servers.serversCount() != 1) { return 2; }
    SubscriptionController subscription(&servers, &appSettings);
    ErrorCode error = ErrorCode::InternalError;
    SubscriptionController::CaptchaInfo captcha;
    QJsonObject account;
    if (action == QStringLiteral("catalog")) { error = subscription.getAccountInfo(id, account); }
    else {
        const auto original = servers.apiV2Config(id);
        const auto country = request.value(QStringLiteral("country")).toString();
        error = country != original->apiConfig.serverCountryCode && !country.isEmpty()
            ? subscription.updateServiceFromGateway(id, country, true, &captcha)
            : subscription.validateAndUpdateConfig(id, original->hasContainers(), &captcha);
    }
    QJsonObject response { { QStringLiteral("code"), int(error) }, { QStringLiteral("captchaRequired"), captcha.isRequired } };
    const auto exported = appSettings.readGatewayProxyUrls(QStringLiteral("agw_state"));
    if (exported.size() <= 256 * 1024) { response.insert(QStringLiteral("gatewayState"), QString::fromLatin1(exported.toBase64())); }
    if (error == ErrorCode::NoError) {
        if (action == QStringLiteral("catalog")) { response.insert(QStringLiteral("account"), account); }
        else { response.insert(QStringLiteral("server"), servers.apiV2Config(id)->toJson()); }
    }
    QTextStream(stdout) << QJsonDocument(response).toJson(QJsonDocument::Compact) << Qt::endl;
    return error == ErrorCode::NoError ? 0 : 3;
}
}

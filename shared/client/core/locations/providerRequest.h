#pragma once
#include <QJsonDocument>
#include <QUuid>
#include "core/controllers/api/subscriptionController.h"
#include "core/utils/constants/apiKeys.h"
#include "controlPlaneResolver.h"

namespace hop {
// Одинаковая обработка профиля в изолированном Windows-процессе и Android-исполнителе.
inline QJsonObject executeProviderRequest(const QJsonObject &request)
{
    const auto action = request.value(QStringLiteral("action")).toString();
    if (action != QStringLiteral("prepare") && action != QStringLiteral("catalog")) { return QJsonObject { { QStringLiteral("code"), 2 } }; }
    // Чистый Go resolver SDK отправляет запросы от Hop.exe; разрешение ограничено DNS-адресами этой сети.
    for (const auto &resolver : controlPlane::resolvers()) {
        controlPlane::permit(resolver, 53, true);
        controlPlane::permit(resolver, 53, false);
    }
    const auto json = request.value(QStringLiteral("server")).toObject();
    if (QUuid(request.value(QStringLiteral("installation")).toString()).isNull()
        || request.value(QStringLiteral("country")).toString().size() > 128) { return QJsonObject { { QStringLiteral("code"), 2 } }; }
    const auto kind = serverConfigUtils::configTypeFromJson(json);
    if (!serverConfigUtils::isApiV2Subscription(kind)) { return QJsonObject { { QStringLiteral("code"), 2 } }; }
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
    if (servers.serversCount() != 1) { return QJsonObject { { QStringLiteral("code"), 2 } }; }
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
    return response;
}
}

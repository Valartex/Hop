#pragma once
#include <QProcess>
#include <QElapsedTimer>
#include "core/controllers/selfhosted/importController.h"
#include "core/subscriptions/subscriptionParser.h"
#include "awgFaultProbe.h"
#include "core/locations/controlPlaneResolver.h"

inline int probeProviderInVm(const QString &key, const QUrl &subscriptionUrl, QTextStream &output)
{
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});
    SecureQSettings settings("Hop-Provider-VM", "memory", nullptr, true, false);
    SecureServersRepository servers(&settings);
    SecureAppSettingsRepository appSettings(&settings);
    ImportController importer(&servers, &appSettings);
    const auto imported = importer.extractConfigFromData(key);
    if (imported.errorCode != ErrorCode::NoError) { return 9; }
    importer.importConfig(imported.config);
    const auto api = servers.apiV2Config(servers.serverIdAt(0));
    if (!api) { return 9; }
    if (!awgPhysicalRequest(output, QStringLiteral("Provider protected baseline"), QStringLiteral("1.0.0.1"), true)) { return 9; }
    const QJsonObject guard { { QStringLiteral("vpnServer"), QStringLiteral("192.0.2.99") }, { QStringLiteral("hopTransportPort"), 9 },
        { QStringLiteral("hopTransportProtocol"), QStringLiteral("udp") }, { QStringLiteral("killSwitchOption"), true },
        { QStringLiteral("splitTunnelType"), 0 } };
    const bool protectedNow = IpcClient::withInterface([&](QSharedPointer<IpcInterfaceReplica> iface) {
        auto reply = iface->applyTrafficGuard(guard, QString()); return reply.waitForFinished(5000) && reply.returnValue();
    }, []() { return false; });
    const auto cleanup = qScopeGuard([]() {
        IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) { auto reply = iface->clearTrafficGuard(); return reply.waitForFinished(5000); }, []() { return false; });
    });
    if (!protectedNow || !awgPhysicalRequest(output, QStringLiteral("Other process remains protected"), QStringLiteral("1.0.0.1"), false)) { return 9; }
    const auto run = [&](const QJsonObject &request, int timeout = 65000) {
        QProcess helper;
        helper.setProcessEnvironment(hop::controlPlane::environment());
        helper.start(QCoreApplication::applicationDirPath() + QStringLiteral("/Hop.exe"), { QStringLiteral("--provider-task") });
        if (!helper.waitForStarted(10000)) { return QJsonObject(); }
        helper.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n'); helper.closeWriteChannel();
        if (!helper.waitForFinished(timeout)) { helper.kill(); helper.waitForFinished(3000); return QJsonObject(); }
        const auto bytes = helper.readAllStandardOutput();
        const auto diagnostics = helper.readAllStandardError();
        if (!diagnostics.isEmpty() && diagnostics.size() <= 8192) { output << "Provider SDK diagnostics: " << QString::fromUtf8(diagnostics) << Qt::endl; }
        return bytes.size() <= 6 * 1024 * 1024 ? QJsonDocument::fromJson(bytes).object() : QJsonObject();
    };
    const auto connectivity = run(QJsonObject { { QStringLiteral("action"), QStringLiteral("subscription") },
        { QStringLiteral("url"), QStringLiteral("https://1.1.1.1/cdn-cgi/trace") } });
    output << "Provider literal HTTPS through protection: " << connectivity.value(QStringLiteral("code")).toInt(-1) << Qt::endl;
    const auto resolution = run(QJsonObject { { QStringLiteral("action"), QStringLiteral("resolve") },
        { QStringLiteral("host"), QStringLiteral("example.com") } }, 15000);
    output << "Provider domain resolution through protection: " << resolution.value(QStringLiteral("code")).toInt(-1)
        << "; address count: " << resolution.value(QStringLiteral("addresses")).toArray().size() << Qt::endl;
    QJsonObject accountRequest { { QStringLiteral("action"), QStringLiteral("catalog") }, { QStringLiteral("server"), api->toJson() },
        { QStringLiteral("installation"), QUuid::createUuid().toString(QUuid::WithoutBraces) } };
    const auto account = run(accountRequest);
    const auto countries = account.value(QStringLiteral("account")).toObject().value(apiDefs::key::availableCountries).toArray();
    output << "Provider API through retained protection: " << account.value(QStringLiteral("code")).toInt(-1) << "; countries: " << countries.size() << Qt::endl;
    accountRequest.insert(QStringLiteral("gatewayState"), account.value(QStringLiteral("gatewayState")));
    QElapsedTimer elapsed; elapsed.start();
    const auto warm = run(accountRequest, 15000);
    output << "Provider warm catalog: " << warm.value(QStringLiteral("code")).toInt(-1) << "; elapsed ms: " << elapsed.elapsed() << Qt::endl;
    const auto subscription = run(QJsonObject { { QStringLiteral("action"), QStringLiteral("subscription") }, { QStringLiteral("url"), subscriptionUrl.toString() } });
    const auto parsed = hop::parseSubscription(QByteArray::fromBase64(subscription.value(QStringLiteral("body")).toString().toLatin1()));
    output << "Provider subscription result: " << subscription.value(QStringLiteral("code")).toInt(-1)
        << "; detail: " << subscription.value(QStringLiteral("detail")).toString().left(256) << Qt::endl;
    output << "Provider subscription through retained protection: " << (parsed.isValid() ? "valid" : "failed") << "; entries: " << parsed.entries.size() << Qt::endl;
    if (resolution.value(QStringLiteral("code")).toInt(-1) != 0 || warm.value(QStringLiteral("code")).toInt(-1) != 0
        || account.value(QStringLiteral("code")).toInt(-1) != 0 || countries.isEmpty() || !parsed.isValid()
        || !awgPhysicalRequest(output, QStringLiteral("Other process still protected after API"), QStringLiteral("1.0.0.1"), false)) { return 9; }
    return 0;
}

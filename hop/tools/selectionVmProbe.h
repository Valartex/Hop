#pragma once
#include <QEventLoop>
#include <QJsonDocument>
#include <QTimer>
#include "core/locations/locationCoordinator.h"
#include "core/controllers/selfhosted/importController.h"
#include "awgFaultProbe.h"
#include "core/subscriptions/subscriptionFetcher.h"
#include "core/subscriptions/subscriptionParser.h"
#include <QUrlQuery>
#include "core/routing/trafficPolicy.h"

inline int probeSelectionInVm(const QString &profile, QTextStream &output, const QUrl &crossSubscription = {})
{
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});
    SecureQSettings settings(QStringLiteral("Hop-Selection-VM"), QStringLiteral("memory"), nullptr, true, false);
    SecureServersRepository servers(&settings);
    SecureAppSettingsRepository appSettings(&settings);
    appSettings.setPrimaryDns(QStringLiteral("1.1.1.1"));
    appSettings.setSecondaryDns(QStringLiteral("1.0.0.1"));
    ImportController importer(&servers, &appSettings);
    const auto imported = importer.extractConfigFromData(profile);
    if (imported.errorCode != ErrorCode::NoError) { return 8; }
    servers.addServer(QStringLiteral("first"), imported.config, serverConfigUtils::ConfigType::Native);
    auto backup = imported.config;
    if (!crossSubscription.isEmpty()) {
        hop::SubscriptionFetcher fetcher;
        QEventLoop download;
        QTimer deadline; deadline.setSingleShot(true); deadline.setInterval(25000);
        bool valid = false;
        QObject::connect(&fetcher, &hop::SubscriptionFetcher::failed, &download, &QEventLoop::quit);
        QObject::connect(&deadline, &QTimer::timeout, &download, &QEventLoop::quit);
        QObject::connect(&fetcher, &hop::SubscriptionFetcher::loaded, &download,
            [&](const QByteArray &body, const QByteArray &, const QByteArray &, bool) {
                const auto parsed = hop::parseSubscription(body);
                if (parsed.isValid()) {
                    for (const auto &entry : parsed.entries) {
                        if (QUrlQuery(QUrl(entry.uri)).queryItemValue(QStringLiteral("security")) != QStringLiteral("reality")) { continue; }
                        const auto result = importer.extractConfigFromData(entry.uri);
                        if (result.errorCode == ErrorCode::NoError) { backup = result.config; valid = true; break; }
                    }
                }
                download.quit();
            });
        if (!fetcher.fetch(crossSubscription)) { return 8; }
        deadline.start(); download.exec(QEventLoop::ExcludeUserInputEvents);
        if (!valid) { output << "Cross-protocol backup import failed" << Qt::endl; return 8; }
        output << "Cross-protocol pool: AmneziaWG 3.1 -> VLESS Reality" << Qt::endl;
    }
    servers.addServer(QStringLiteral("second"), backup, serverConfigUtils::ConfigType::Native);
    if (servers.serversCount() != 2) { return 8; }
    hop::LocationCatalog catalog(&servers, &settings);
    catalog.toggleLocation(QStringLiteral("first")); catalog.toggleLocation(QStringLiteral("second"));
    catalog.setMode(hop::LocationCatalog::Automatic);
    VpnConnection vpn(&servers, &appSettings);
    ConnectionController connection(&servers, &appSettings, &vpn);
    const bool routed = profile.contains(QStringLiteral("# HopRoutingSelectionTest"));
    if (routed) {
        const auto policy = hop::compileTrafficPolicy(QJsonObject { { "default", "vpn" }, { "rules", QJsonArray {
            QJsonObject { { "kind", "domain" }, { "value", "full:blocked.hop.test" }, { "target", "block" } },
            QJsonObject { { "kind", "ip" }, { "value", "1.0.0.1/32" }, { "target", "direct" } }
        } } });
        if (!policy.isValid()) { return 8; }
        connection.setTrafficPolicy(hop::serializeTrafficPolicy(policy), true);
    }
    hop::SelectionSession::Timing timing { 1000, 3000, 20000, 3, 100 };
    hop::LocationCoordinator coordinator(&catalog, &servers, &appSettings, &connection, &vpn, nullptr,
        QCoreApplication::applicationDirPath() + QStringLiteral("/Hop.exe"), {}, timing);
    QEventLoop wait;
    QTimer deadline, cleanup;
    deadline.setSingleShot(true); deadline.setInterval(80000);
    cleanup.setInterval(100);
    bool crashed = false, stopping = false, failed = false;
    int result = 8;
    QObject::connect(&coordinator, &hop::SelectionSession::changed, &wait, [&]() {
        output << "Selection state: " << int(coordinator.state()) << " / " << coordinator.activeId() << " / " << coordinator.status() << Qt::endl;
        if (coordinator.state() != hop::SelectionSession::Healthy || stopping) { return; }
        if (coordinator.activeId() == QStringLiteral("first") && !crashed) {
            crashed = true;
            output << "Selection first location: tunnel DNS verified" << Qt::endl;
            QTimer::singleShot(0, &wait, [&]() {
                if (!crashOwnedAwgTunnelForTest(output)) { failed = true; wait.quit(); }
            });
        } else if (coordinator.activeId() == QStringLiteral("second") && crashed) {
            output << "Selection backup location: tunnel DNS verified" << Qt::endl;
            stopping = true;
            QTimer::singleShot(0, &wait, [&]() {
                if (routed && !crossSubscription.isEmpty() && !awgPhysicalRequest(output, "Routed backup Direct endpoint", "1.0.0.1", true)) { failed = true; }
                coordinator.disconnectManually(); cleanup.start();
            });
        }
    });
    QObject::connect(&coordinator, &hop::SelectionSession::suspendRequested, &wait, [&](const QString &) {
        const int count = IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) {
            auto reply = iface->trafficGuardFilterCount();
            return reply.waitForFinished(5000) ? reply.returnValue() : -1;
        }, []() { return -1; });
        output << "Selection filters retained between locations: " << count << Qt::endl;
        if (count <= 0) { failed = true; wait.quit(); }
    });
    QObject::connect(&cleanup, &QTimer::timeout, &wait, [&]() {
        if (vpn.connectionState() != Vpn::Disconnected) { return; }
        const int count = IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) {
            auto reply = iface->trafficGuardFilterCount();
            return reply.waitForFinished(5000) ? reply.returnValue() : -1;
        }, []() { return -1; });
        output << "Selection filters after manual disconnect: " << count << Qt::endl;
        if (count == 0 && !failed) { result = 0; }
        wait.quit();
    });
    QObject::connect(&deadline, &QTimer::timeout, &wait, [&]() { output << "Selection VM deadline: " << coordinator.status() << Qt::endl; wait.quit(); });
    deadline.start(); coordinator.toggleConnection(); wait.exec(QEventLoop::ExcludeUserInputEvents);
    coordinator.disconnectManually();
    return result;
}

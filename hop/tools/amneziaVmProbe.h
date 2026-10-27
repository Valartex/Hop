#pragma once

#include <QEventLoop>
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QTextStream>
#include <QTimer>
#include <functional>
#include <QUdpSocket>
#include <QUuid>
#include <iphlpapi.h>
#include <netioapi.h>
#include "core/controllers/api/subscriptionController.h"
#include "core/controllers/connectionController.h"
#include "core/controllers/selfhosted/importController.h"
#include "core/utils/constants/configKeys.h"
#include "core/utils/containers/containerUtils.h"
#include "core/utils/networkUtilities.h"
#include "awgFaultProbe.h"
#include "core/routing/trafficPolicy.h"
#include "policySocketVmProbe.h"
#include <QRegularExpression>

// Только для гостевого теста. Ключи и ответ API остаются в памяти, настройки диска не используются.
inline int probeAmneziaInVm(const QString &key, QTextStream &output, HANDLE faultService = nullptr)
{
    const auto previousLogger = qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});
    const auto restoreLogger = qScopeGuard([&]() { qInstallMessageHandler(previousLogger); });
    SecureQSettings settings(QStringLiteral("Hop-VM-API-") + QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("memory"), nullptr, true, false);
    SecureServersRepository servers(&settings);
    SecureAppSettingsRepository appSettings(&settings);
    ImportController importer(&servers, &appSettings);
    const auto imported = importer.extractConfigFromData(key);
    if (imported.errorCode != ErrorCode::NoError) { output << "Amnezia key parse error: " << int(imported.errorCode) << Qt::endl; return 6; }
    importer.importConfig(imported.config);
    if (servers.serversCount() != 1) { output << "Amnezia API import did not create a profile" << Qt::endl; return 6; }
    const auto serverId = servers.serverIdAt(0);
    servers.setDefaultServer(serverId);
    SubscriptionController subscription(&servers, &appSettings);
    const bool synthetic = !servers.apiV2Config(serverId).has_value();
    const bool policyFront = key.contains(QStringLiteral("# HopAwgPolicyTest"));
    if (policyFront && !synthetic) { return 7; }
    const auto directPort = QRegularExpression(QStringLiteral("^# HopDirectPort=(\\d{1,5})$"), QRegularExpression::MultilineOption).match(key).captured(1).toInt();
    if (policyFront && (directPort < 1 || directPort > 65535)) { return 7; }

    if (faultService && !synthetic) { output << "AWG fault test requires the local fixture" << Qt::endl; return 7; }
    if (!synthetic) {
        subscription.setCurrentProtocol(serverId, QStringLiteral("awg"));
        SubscriptionController::CaptchaInfo captcha;
        const auto updated = subscription.validateAndUpdateConfig(serverId, false, &captcha);
        output << "Amnezia API bootstrap: " << int(updated) << "; captcha required: " << (captcha.isRequired ? "yes" : "no") << Qt::endl;
        if (updated != ErrorCode::NoError) { return 6; }
    } else { output << "AWG local fixture profile imported" << Qt::endl; }
    const auto api = servers.apiV2Config(serverId);
    // Отзываем только конфигурацию с новым UUID этой тестовой установки.
    const auto testUuid = appSettings.getInstallationUuid(true);
    const auto revoke = qScopeGuard([&]() {
        if (!api) { return; }
        const auto result = subscription.deactivateExternalDevice(serverId, testUuid, api->apiConfig.serverCountryCode);
        output << "Temporary test installation cleanup: " << int(result) << Qt::endl;
    });
    VpnConnection connection(&servers, &appSettings);
    ConnectionController controller(&servers, &appSettings, &connection);
    QJsonObject config;
    DockerContainer container = DockerContainer::None;
    const auto prepared = controller.prepareConnection(serverId, config, container);
    if (prepared != ErrorCode::NoError) { output << "AWG configuration preparation: " << int(prepared) << Qt::endl; return 6; }
    if (!ContainerUtils::isAwgContainer(container)) { output << "No AWG configuration" << Qt::endl; return 6; }
    if (faultService) {
        if (!awgPhysicalRequest(output, QStringLiteral("AWG protected baseline"), QStringLiteral("1.0.0.1"), true)) { return 7; }
        // Direct-проба не должна совпадать с защищённым адресом DNS VPN.
        config.insert(configKey::dns1, QStringLiteral("9.9.9.9"));
        config.insert(configKey::dns2, QStringLiteral("149.112.112.112"));
        appSettings.setSitesSplitTunnelingEnabled(true);
        appSettings.setRouteMode(amnezia::RouteMode::VpnAllExceptSites);
        appSettings.addVpnSite(amnezia::RouteMode::VpnAllExceptSites, QStringLiteral("1.1.1.1"));
    }
    const auto protocol = config.value(QStringLiteral("protocol")).toString();
    const auto data = config.value(protocol + QStringLiteral("_config_data")).toObject();
    output << "AWG 3.1 HeaderProtectionKey present: "
           << (!data.value(configKey::headerProtectionKey).toString().isEmpty() ? "yes" : "no") << Qt::endl;
    QString testIp = synthetic ? QStringLiteral("10.77.0.1") : QString();
    if (!synthetic) {
        for (const auto &address : QHostInfo::fromName(QStringLiteral("www.gstatic.com")).addresses()) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol) { testIp = address.toString(); break; }
        }
    }
    if (testIp.isEmpty()) { return 6; }
    QEventLoop wait;
    QTimer deadline;
    deadline.setSingleShot(true);
    deadline.setInterval(65000);
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy::NoProxy);
    const auto proxySecret = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto stopFront = qScopeGuard([&]() {
        if (!policyFront) { return; }
        IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) { auto reply = iface->xrayStop(); reply.waitForFinished(5000); });
    });

    QUdpSocket dns;
    dns.setProxy(QNetworkProxy::NoProxy);
    if (!dns.bind(QHostAddress(QHostAddress::AnyIPv4), 0)) {
        output << "UDP test socket bind failed: " << int(dns.error()) << Qt::endl;
        return 7;
    }
    const QHostAddress dnsTarget(synthetic ? QStringLiteral("10.77.0.1") : config.value(configKey::dns1).toString());
    bool requested = false, stopping = false, passed = true, finished = false, diagnosing = false;
    int step = 0;
    int exitCode = 7;
    std::function<void()> request;
    request = [&]() {
        network.clearConnectionCache();
        QNetworkRequest req(QUrl(synthetic ? (policyFront && step > 0 ? QStringLiteral("http://%1.hop.test:8080/generate_204").arg(step == 1 ? QStringLiteral("allowed") : QStringLiteral("blocked")) : QStringLiteral("http://10.77.0.1:8080/generate_204")) : step == 0 ? QStringLiteral("https://%1/generate_204").arg(testIp)
            : QStringLiteral("https://connectivitycheck.gstatic.com/generate_204")));
        if (policyFront && step == 3) {
            req.setUrl(QUrl(QStringLiteral("http://direct.hop.test:%1/generate_204").arg(directPort)));
            req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        }
        if (step == 0 && !synthetic) { req.setRawHeader("Host", "www.gstatic.com"); req.setPeerVerifyName(QStringLiteral("www.gstatic.com")); }
        req.setTransferTimeout(12000);
        auto *reply = network.get(req);
        QObject::connect(reply, &QNetworkReply::finished, &wait, [&, reply]() {
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            passed = passed && (policyFront && step == 2 ? status == 0 && reply->error() != QNetworkReply::NoError
                : status == 204 && reply->error() == QNetworkReply::NoError);
            output << "AWG " << (policyFront ? step == 0 ? "frontend IP" : step == 1 ? "frontend domain VPN" : step == 2 ? "frontend domain Block" : "frontend domain Direct" : synthetic ? "fixture TCP" : step == 0 ? "IPv4 HTTPS" : "DNS HTTPS") << ": HTTP " << status << ", Qt " << int(reply->error()) << Qt::endl;
            if (synthetic && !(policyFront && step == 2) && reply->error() != QNetworkReply::NoError) { output << "Fixture request error: " << reply->errorString() << Qt::endl; }
            reply->deleteLater();
            if (synthetic && !passed && !diagnosing) {
                diagnosing = true;
                const int index = NetworkUtilities::AdapterIndexTo(QHostAddress(testIp));
                output << "Fixture route interface: " << index << " / " << QNetworkInterface::interfaceFromIndex(index).humanReadableName() << Qt::endl;
                const bool cleared = IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) {
                    auto reply = iface->clearTrafficGuard();
                    return reply.waitForFinished(5000) && reply.returnValue();
                }, []() { return false; });
                output << "Diagnostic protection release: " << (cleared ? "yes" : "no") << Qt::endl;
                QTimer::singleShot(0, &wait, request);
                return;
            }
            if (policyFront && step == 3 && passed) {
                bool valid = true;
                const auto count = directSocketFiltersInVm(directPort, valid);
                output << "Direct live endpoint filters: " << count << "; six exact conditions: " << valid << Qt::endl;
                QNetworkAccessManager physical; physical.setProxy(QNetworkProxy::NoProxy);
                QNetworkRequest attempt(QUrl(QStringLiteral("http://10.0.2.2:%1/generate_204").arg(directPort))); attempt.setTransferTimeout(3000);
                QEventLoop blocked; auto *other = physical.get(attempt);
                QObject::connect(other, &QNetworkReply::finished, &blocked, &QEventLoop::quit); blocked.exec(QEventLoop::ExcludeUserInputEvents);
                const auto isolated = other->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 0 && other->error() == QNetworkReply::UnknownNetworkError;
                output << "Direct endpoint from other process remains blocked: " << isolated << Qt::endl;
                passed = count == 1 && valid && isolated;
                if (passed && faultService) {
                    stopping = true;
                    passed = TerminateProcess(faultService, 96) && WaitForSingleObject(faultService, 10000) == WAIT_OBJECT_0;
                    const auto after = directSocketFiltersInVm(directPort, valid);
                    output << "Direct endpoint filters after service crash: " << after << Qt::endl;
                    const int tunnelPermits = ownProtectionFiltersInVm(1500);
                    output << "Dynamic tunnel permits after service crash: " << tunnelPermits << Qt::endl;
                    passed &= tunnelPermits == 0;
                    QEventLoop denied;
                    auto *afterCrash = physical.get(attempt);
                    QObject::connect(afterCrash, &QNetworkReply::finished, &denied, &QEventLoop::quit); denied.exec(QEventLoop::ExcludeUserInputEvents);
                    const bool endpointBlocked = afterCrash->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 0
                        && afterCrash->error() == QNetworkReply::UnknownNetworkError;
                    output << "Physical Direct endpoint after service crash blocked by WFP: " << endpointBlocked << Qt::endl;
                    passed = passed && after == 0 && endpointBlocked;
                    exitCode = passed ? 0 : 7; finished = true; wait.quit(); return;
                }
                network.clearConnectionCache();
                QElapsedTimer released; released.start(); int remaining = count;
                while (remaining > 0 && released.elapsed() < 5000) {
                    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50); QThread::msleep(25);
                    remaining = directSocketFiltersInVm(directPort, valid);
                }
                output << "Direct endpoint filters after socket close: " << remaining << Qt::endl;
                passed = passed && remaining == 0;
            }
            if (++step < (policyFront ? 4 : synthetic ? 1 : 2) && passed) { QTimer::singleShot(0, &wait, request); return; }
            if (!passed) { finished = true; wait.quit(); return; }
            // Явный UDP DNS-запрос, без системного DNS-кэша.
            const auto packet = synthetic ? QByteArrayLiteral("hop-awg-echo") : QByteArray::fromHex("48500100000100000000000003777777076773746174696303636f6d0000010001");
            const auto written = dns.writeDatagram(packet, dnsTarget, synthetic ? 5353 : 53);
            output << "UDP request bytes: " << written << "; socket error: " << int(dns.error()) << Qt::endl;
        });
    };
    QObject::connect(&dns, &QUdpSocket::readyRead, &wait, [&]() {
        QByteArray packet(4096, Qt::Uninitialized);
        QHostAddress sender;
        quint16 port = 0;
        const auto size = dns.readDatagram(packet.data(), packet.size(), &sender, &port);
        const bool valid = synthetic ? size == 12 && sender == QHostAddress(QStringLiteral("10.77.0.1")) && port == 5353
            && packet.left(size) == QByteArrayLiteral("hop-awg-echo")
            : size >= 12 && sender == dnsTarget && port == 53
            && packet.left(2) == QByteArray::fromHex("4850") && (static_cast<unsigned char>(packet.at(2)) & 0x80)
            && (static_cast<unsigned char>(packet.at(3)) & 0x0f) == 0
            && (packet.at(6) != 0 || packet.at(7) != 0);
        if (!valid) { return; }
        output << (synthetic ? "AWG fixture UDP echo: valid response" : "AWG explicit UDP DNS: valid response") << Qt::endl;
        stopping = true;
        if (faultService) {
            // Не запускаем вложенный QtRO/HTTP-цикл из обработчика UDP-сокета.
            QTimer::singleShot(0, &wait, [&]() {
                exitCode = probeAwgGuardCrash(connection, faultService, output) ? 0 : 7;
                finished = true;
                wait.quit();
            });
            return;
        }
        connection.disconnectFromVpn();
    });
    QObject::connect(&connection, &VpnConnection::connectionStateChanged, &wait, [&](Vpn::ConnectionState state) {
        if (state == Vpn::ConnectionState::Connected && !requested) {
            requested = true;
            NET_LUID luid {};
            if (ConvertInterfaceAliasToLuid(L"Hop", &luid) == NO_ERROR) {
                MIB_UNICASTIPADDRESS_TABLE *addresses = nullptr;
                if (GetUnicastIpAddressTable(AF_INET, &addresses) == NO_ERROR) {
                    for (ULONG index = 0; index < addresses->NumEntries; ++index) {
                        const auto &row = addresses->Table[index];
                        if (row.InterfaceLuid.Value == luid.Value) {
                            output << "Hop IPv4 DAD state: " << int(row.DadState) << "; prefix: " << int(row.OnLinkPrefixLength) << Qt::endl;
                        }
                    }
                    FreeMibTable(addresses);
                }
                MIB_IPFORWARD_TABLE2 *routes = nullptr;
                if (GetIpForwardTable2(AF_INET, &routes) == NO_ERROR) {
                    int count = 0;
                    for (ULONG index = 0; index < routes->NumEntries; ++index) { count += routes->Table[index].InterfaceLuid.Value == luid.Value; }
                    output << "Hop IPv4 route count: " << count << Qt::endl;
                    FreeMibTable(routes);
                }
            }
            if (policyFront) {
                auto front = QJsonDocument::fromJson(R"({
                    "log":{"loglevel":"none"},
                    "inbounds":[{"listen":"127.0.0.1","port":44971,"protocol":"socks","settings":{"auth":"password","udp":true}}],
                    "outbounds":[{"tag":"hop-vpn","protocol":"freedom","sendThrough":"10.77.0.2","streamSettings":{"sockopt":{"interface":"Hop"}}}]
                })").object();
                auto inbounds = front.value(QStringLiteral("inbounds")).toArray();
                auto inbound = inbounds.first().toObject();
                auto settings = inbound.value(QStringLiteral("settings")).toObject();
                settings.insert(QStringLiteral("accounts"), QJsonArray { QJsonObject {
                    { QStringLiteral("user"), QStringLiteral("hop-test") }, { QStringLiteral("pass"), proxySecret } } });
                inbound.insert(QStringLiteral("settings"), settings); inbounds[0] = inbound;
                front.insert(QStringLiteral("inbounds"), inbounds);
                const auto compiled = hop::compileTrafficPolicy(QJsonObject { { QStringLiteral("default"), QStringLiteral("vpn") },
                    { QStringLiteral("rules"), QJsonArray { QJsonObject { { QStringLiteral("kind"), QStringLiteral("domain") },
                        { QStringLiteral("value"), QStringLiteral("full:blocked.hop.test") }, { QStringLiteral("target"), QStringLiteral("block") } },
                        QJsonObject { { QStringLiteral("kind"), QStringLiteral("domain") }, { QStringLiteral("value"), QStringLiteral("full:direct.hop.test") },
                            { QStringLiteral("target"), QStringLiteral("direct") } } } } });
                QString policyError;
                front = hop::applyXrayTrafficPolicy(front, compiled, QStringLiteral("10.77.0.1"), &policyError);
                if (!policyError.isEmpty()) { passed = false; finished = true; wait.quit(); return; }
                const auto ready = IpcClient::withInterface([&](QSharedPointer<IpcInterfaceReplica> iface) {
                    auto reply = iface->xrayStart(QString::fromUtf8(QJsonDocument(front).toJson(QJsonDocument::Compact)));
                    return reply.waitForFinished(10000) && reply.returnValue();
                }, []() { return false; });
                output << "AWG Xray policy frontend configured: " << ready << Qt::endl;
                if (!ready) { passed = false; finished = true; wait.quit(); return; }
                network.setProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy, QStringLiteral("127.0.0.1"), 44971, QStringLiteral("hop-test"), proxySecret));
            }
            QTimer::singleShot(1500, &wait, request);
        }
        if (state == Vpn::ConnectionState::Error && !stopping) { output << "AWG protocol failure: " << int(connection.lastError()) << Qt::endl; finished = true; wait.quit(); }
        if (state == Vpn::ConnectionState::Disconnected && stopping && !finished) { exitCode = passed ? 0 : 7; finished = true; wait.quit(); }
    });
    QObject::connect(&deadline, &QTimer::timeout, &wait, [&]() { output << "AWG test deadline exceeded" << Qt::endl; wait.quit(); });
    deadline.start();
    connection.connectToVpn(serverId, container, config);
    if (!finished) { wait.exec(QEventLoop::ExcludeUserInputEvents); }
    connection.disconnectFromVpn();
    return exitCode;
}

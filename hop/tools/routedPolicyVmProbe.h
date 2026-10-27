#pragma once
#include "amneziaVmProbe.h"
#include <QNetworkDatagram>
#include "physicalDnsVmProbe.h"

// Проверяет обычный TUN-путь приложения, без подмены его SOCKS-прокси в тесте.
inline int probeRoutedPolicyInVm(const QString &key, QTextStream &output)
{
    const auto previousLogger = qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});
    const auto restoreLogger = qScopeGuard([&]() { qInstallMessageHandler(previousLogger); });
    SecureQSettings settings("Hop-Routed-VM", "memory", nullptr, true, false);
    SecureServersRepository servers(&settings); SecureAppSettingsRepository appSettings(&settings);
    ImportController importer(&servers, &appSettings);
    const auto imported = importer.extractConfigFromData(key);
    if (imported.errorCode != ErrorCode::NoError) { return 7; }
    importer.importConfig(imported.config);
    if (servers.serversCount() != 1 || servers.apiV2Config(servers.serverIdAt(0))) { return 7; }
    const auto id = servers.serverIdAt(0); servers.setDefaultServer(id);
    VpnConnection connection(&servers, &appSettings); ConnectionController controller(&servers, &appSettings, &connection);
    QJsonObject config; DockerContainer container = DockerContainer::None;
    if (controller.prepareConnection(id, config, container) != ErrorCode::NoError || !ContainerUtils::isAwgContainer(container)) { return 7; }
    const int directPort = QRegularExpression(QStringLiteral("^# HopDirectPort=(\\d{1,5})$"), QRegularExpression::MultilineOption).match(key).captured(1).toInt();
    if (directPort < 1 || directPort > 65535) { return 7; }
    // Категория DAT с точным доменом того же адреса, что и разрешённый домен.
    const auto site = hop::parseGeoData(QByteArray::fromHex("0a1a0a025255121408031210626c6f636b65642e686f702e74657374"), hop::GeoDataKind::GeoSite);
    const auto compiled = hop::compileTrafficPolicy(QJsonObject { { "default", "vpn" }, { "rules", QJsonArray {
        QJsonObject { { "kind", "geosite" }, { "value", "RU" }, { "target", "block" } },
        QJsonObject { { "kind", "domain" }, { "value", "full:direct.hop.test" }, { "target", "direct" } }
    } } }, nullptr, &site);
    if (!compiled.isValid()) { output << "Routed policy compile failed: " << compiled.error << Qt::endl; return 7; }
    controller.setTrafficPolicy(hop::serializeTrafficPolicy(compiled), true);
    QEventLoop wait; QTimer deadline; deadline.setSingleShot(true); deadline.start(65000);
    bool connected = false, finished = false, passed = false, stopping = false;
    QObject::connect(&deadline, &QTimer::timeout, &wait, &QEventLoop::quit);
    QObject::connect(&connection, &VpnConnection::connectionStateChanged, &wait, [&](Vpn::ConnectionState state) {
        if (state == Vpn::Error) { output << "Routed protocol error: " << int(connection.lastError()) << Qt::endl; wait.quit(); }
        if (state == Vpn::Disconnected && stopping) { finished = true; wait.quit(); }
        if (state != Vpn::Connected || connected) { return; }
        connected = true;
        QTimer::singleShot(1200, &wait, [&]() {
            const auto alias = QNetworkInterface::interfaceFromIndex(NetworkUtilities::AdapterIndexTo(QHostAddress("10.77.0.1"))).humanReadableName();
            output << "Routed fixture IP interface: " << alias << Qt::endl;
            const auto endpointInterface = QNetworkInterface::interfaceFromIndex(NetworkUtilities::AdapterIndexTo(QHostAddress("10.0.2.2"))).humanReadableName();
            output << "AWG physical endpoint interface: " << endpointInterface << Qt::endl;
            if (endpointInterface == QStringLiteral("Hop") || endpointInterface == QStringLiteral("Hop-TUN") || endpointInterface.isEmpty()) {
                stopping = true; connection.disconnectFromVpn(); return;
            }
            NET_LUID hopLuid {}; ConvertInterfaceAliasToLuid(L"Hop", &hopLuid);
            SOCKADDR_INET target {}, source {}, selected {}; target.Ipv4.sin_family = AF_INET; source.Ipv4.sin_family = AF_INET;
            target.Ipv4.sin_addr.s_addr = htonl(QHostAddress("10.77.0.1").toIPv4Address()); source.Ipv4.sin_addr.s_addr = htonl(QHostAddress("10.77.0.2").toIPv4Address());
            MIB_IPFORWARD_ROW2 route {}; const auto routeResult = GetBestRoute2(&hopLuid, 0, &source, &target, 0, &route, &selected);
            output << "Bound AWG route: " << routeResult << "; prefix: " << int(route.DestinationPrefix.PrefixLength) << "; own interface: " << (route.InterfaceLuid.Value == hopLuid.Value) << Qt::endl;
            MIB_IF_ROW2 before {}; before.InterfaceLuid = hopLuid; GetIfEntry2(&before);
            passed = alias == QStringLiteral("Hop-TUN");
            QNetworkAccessManager network; network.setProxy(QNetworkProxy::NoProxy);
            const auto request = [&](const QUrl &url, bool allowed) {
                QNetworkRequest value(url); value.setTransferTimeout(9000); value.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
                QEventLoop done; auto *reply = network.get(value);
                QObject::connect(reply, &QNetworkReply::finished, &done, &QEventLoop::quit); done.exec(QEventLoop::ExcludeUserInputEvents);
                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); const auto error = reply->error();
                output << "TUN " << url.host() << ": HTTP " << status << ", Qt " << int(error) << Qt::endl;
                reply->deleteLater();
                return allowed ? status == 204 && error == QNetworkReply::NoError : status == 0 && error != QNetworkReply::NoError;
            };
            passed &= request(QUrl("http://10.77.0.1:8080/generate_204"), true);
            const auto addresses = QHostInfo::fromName("allowed.hop.test").addresses();
            const auto fake = addresses.isEmpty() ? QHostAddress() : addresses.first();
            output << "System DNS FakeDNS address: " << fake.toString() << Qt::endl;
            passed &= fake.isInSubnet(QHostAddress("198.18.0.0"), 15);
            passed &= request(QUrl("http://allowed.hop.test:8080/generate_204"), true);
            passed &= request(QUrl("http://blocked.hop.test:8080/generate_204"), false);
            passed &= request(QUrl(QStringLiteral("http://direct.hop.test:%1/generate_204").arg(directPort)), true);
            bool valid = true; const int live = directSocketFiltersInVm(directPort, valid);
            output << "TUN Direct exact socket filters: " << live << "; valid: " << valid << Qt::endl;
            passed &= live == 1 && valid;
            QProcess health;
            const auto checkHealth = [&]() {
                health.start(QCoreApplication::applicationDirPath() + QStringLiteral("/hop_tunnel_probe.exe"));
                if (!health.waitForStarted(3000)) { return QJsonObject(); }
                health.write("{\"interface\":\"Hop-TUN\",\"resolver\":\"198.18.0.1\"}\n"); health.closeWriteChannel();
                if (!health.waitForFinished(10000)) { health.kill(); health.waitForFinished(3000); return QJsonObject(); }
                return QJsonDocument::fromJson(health.readAllStandardOutput()).object();
            };
            const auto healthy = checkHealth();
            output << "TUN real transport DNS health: " << healthy.value("ok").toBool() << Qt::endl;
            passed &= healthy.value("supported").toBool() && healthy.value("ok").toBool();
            passed &= request(QUrl(QStringLiteral("http://direct.hop.test:%1/drop-next-health-dns").arg(directPort)), true);
            const auto retried = checkHealth();
            const bool survivesLostPacket = retried.value("ok").toBool() && retried.value("attempts").toInt() >= 2;
            output << "TUN health survives lost first DNS packet: " << survivesLostPacket << Qt::endl;
            passed &= survivesLostPacket;
            passed &= physicalDnsDeniedInVm(output);
            passed &= request(QUrl(QStringLiteral("http://direct.hop.test:%1/pause-dns").arg(directPort)), true);
            const auto unavailable = checkHealth();
            const bool detectsMissingDns = unavailable.value("supported").toBool() && !unavailable.value("ok").toBool();
            output << "TUN health detects missing real DNS reply: " << detectsMissingDns << Qt::endl; passed &= detectsMissingDns;
            QUdpSocket echo; echo.setProxy(QNetworkProxy::NoProxy); echo.bind(QHostAddress(QHostAddress::AnyIPv4), 0);
            const auto packet = QByteArrayLiteral("hop-awg-echo"); echo.writeDatagram(packet, QHostAddress("10.77.0.1"), 5353);
            QEventLoop echoed; QTimer timeout; timeout.setSingleShot(true); timeout.start(5000);
            QObject::connect(&echo, &QUdpSocket::readyRead, &echoed, &QEventLoop::quit);
            QObject::connect(&timeout, &QTimer::timeout, &echoed, &QEventLoop::quit); echoed.exec(QEventLoop::ExcludeUserInputEvents);
            const bool received = echo.hasPendingDatagrams() && echo.receiveDatagram().data() == packet;
            output << "TUN UDP through AWG: " << received << Qt::endl; passed &= received;
            MIB_IF_ROW2 after {}; after.InterfaceLuid = hopLuid; GetIfEntry2(&after);
            output << "AWG NIC packets out/in: " << (after.OutUcastPkts - before.OutUcastPkts) << "/" << (after.InUcastPkts - before.InUcastPkts) << Qt::endl;
            QLocalSocket status; status.connectToServer(QStringLiteral(APP_WINDOWS_DAEMON_PIPE));
            if (status.waitForConnected(2000)) {
                status.write("{\"type\":\"status\"}\n"); status.flush();
                if (status.waitForReadyRead(2000)) { const auto counters = QJsonDocument::fromJson(status.readLine(65536)).object();
                    output << "AWG transport tx/rx: " << counters.value("txBytes").toDouble() << "/" << counters.value("rxBytes").toDouble() << Qt::endl; }
            }
            network.clearConnectionCache(); stopping = true; connection.disconnectFromVpn();
        });
    });
    config.insert(configKey::dns1, "10.77.0.1"); config.insert(configKey::dns2, QString());
    config.insert("hopRoutingManaged", true); config.insert("hopTrafficPolicy", hop::serializeTrafficPolicy(compiled));
    connection.connectToVpn(id, container, config);
    if (!finished) { wait.exec(QEventLoop::ExcludeUserInputEvents); }
    connection.disconnectFromVpn();
    output << "Routed cleanup acknowledged: " << finished << Qt::endl;
    const int filters = IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) {
        auto reply = iface->trafficGuardFilterCount(); return reply.waitForFinished(3000) ? reply.returnValue() : -1;
    }, []() { return -1; });
    output << "Routed protection filters after manual disconnect: " << filters << Qt::endl;
    passed &= filters == 0;
    return passed && finished ? 0 : 7;
}

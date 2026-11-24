#pragma once
#include "routedPolicyVmProbe.h"
#include "core/subscriptions/subscriptionFetcher.h"
#include "core/subscriptions/subscriptionParser.h"
#include "core/utils/serialization/serialization.h"
#include <QUrlQuery>
#include <QRandomGenerator>
#include <QTcpSocket>
#include "cdnRoutingVmProbe.h"

inline bool probeVlessExternalUdpInVm(QTextStream &output)
{
    // Порт9953 поддерживается Quad9 и не попадает в DNS-перехват порта53 клиента.
    // Источник: https://docs.quad9.net/services/ . Только гостевой TUN, без TCP fallback.
    const QHostAddress remote(QStringLiteral("9.9.9.9"));
    const auto index = NetworkUtilities::AdapterIndexTo(remote);
    const auto adapter = QNetworkInterface::interfaceFromIndex(index);
    if (adapter.humanReadableName() != QStringLiteral("Hop-TUN")) { return false; }
    QHostAddress local;
    for (const auto &entry : adapter.addressEntries()) {
        if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) { local = entry.ip(); break; }
    }
    QUdpSocket socket; socket.setProxy(QNetworkProxy::NoProxy);
    if (local.isNull() || !socket.bind(local, 0)) { return false; }
    auto packet = QByteArray::fromHex("000001000001000000000000");
    const auto queryId = QRandomGenerator::system()->generate();
    packet[0] = char(queryId >> 8); packet[1] = char(queryId);
    const auto label = QByteArrayLiteral("hop-udp-") + QByteArray::number(QRandomGenerator::system()->generate64(), 16);
    packet.append(char(label.size())); packet.append(label);
    packet.append(QByteArray::fromHex("076578616d706c6503636f6d0000010001"));
    bool verified = false; QEventLoop wait; QTimer timeout; timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &wait, &QEventLoop::quit);
    QObject::connect(&socket, &QUdpSocket::readyRead, &wait, [&]() {
        while (socket.hasPendingDatagrams()) {
            const auto answer = socket.receiveDatagram(4096);
            const auto data = answer.data();
            if (answer.senderAddress() != remote || answer.senderPort() != 9953 || data.size() < packet.size()
                || data.left(2) != packet.left(2) || !(quint8(data[2]) & 0x80) || data.mid(4, 2) != packet.mid(4, 2)
                || data.mid(12, packet.size() - 12) != packet.mid(12)) { continue; }
            const auto status = quint8(data[3]) & 0x0f;
            if (status != 0 && status != 3) { continue; }
            verified = true; wait.quit(); return;
        }
    });
    timeout.start(6000);
    if (socket.writeDatagram(packet, remote, 9953) != packet.size()) { return false; }
    wait.exec(QEventLoop::ExcludeUserInputEvents);
    output << "VLESS external UDP9953 through own TUN: " << verified << Qt::endl;
    return verified;
}

inline int probeVlessPolicyInVm(const QUrl &url, QTextStream &output, bool smbOnly = false, bool cdnOnly = false)
{
    const auto previousLogger = qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});
    const auto restoreLogger = qScopeGuard([&]() { qInstallMessageHandler(previousLogger); });
    const auto smb = [&](const QString &phase) {
        // Только TCP-проверка: NTLM/пароли не передаются, содержимое общей папки не читается.
        QTcpSocket socket; socket.connectToHost(QStringLiteral("192.168.1.1"), 445);
        const bool connected = socket.waitForConnected(3000);
        output << "Router SMB TCP445 " << phase << ": " << connected << Qt::endl;
        socket.abort(); return connected;
    };
    if (smbOnly && !smb(QStringLiteral("before VPN"))) { return 7; }
    HopCdnProbeResult cdnBaseline;
    if (cdnOnly) {
        probeCdnDnsInVm(output, false);
        cdnBaseline = probeCdnImagesInVm(output, QStringLiteral("before VPN"));
        if (!cdnBaseline.successful) { output << "CDN baseline is unavailable; no Hop attribution" << Qt::endl; return 7; }
    }
    hop::SubscriptionFetcher fetcher; QEventLoop downloaded; QByteArray body;
    QObject::connect(&fetcher, &hop::SubscriptionFetcher::loaded, &downloaded,
        [&](const QByteArray &bytes, const QByteArray &, const QByteArray &, bool) { body = bytes; downloaded.quit(); });
    QObject::connect(&fetcher, &hop::SubscriptionFetcher::failed, &downloaded, [&](const QString &, int) { downloaded.quit(); });
    if (!fetcher.fetch(url)) { return 7; } downloaded.exec(QEventLoop::ExcludeUserInputEvents);
    const auto subscription = hop::parseSubscription(body); body.clear(); QString uri;
    if (!subscription.isValid()) { output << "VLESS policy subscription failed" << Qt::endl; return 7; }
    int checked = 0;
    for (const auto &entry : subscription.entries) {
        if (QUrlQuery(QUrl(entry.uri)).queryItemValue("security") != QStringLiteral("reality")) { continue; }
        if (smbOnly) { uri = entry.uri; break; }
        if (++checked > (cdnOnly ? 2 : 3)) { break; }
        const auto resolvedEndpoint = QHostAddress(NetworkUtilities::getIPAddress(QUrl(entry.uri).host()));
        output << "VLESS candidate endpoint uses inherited FakeDNS range: "
               << resolvedEndpoint.isInSubnet(QHostAddress("198.18.0.0"), 15) << Qt::endl;
        const auto resolvedWeb = QHostInfo::fromName("www.gstatic.com").addresses();
        bool inheritedFakeDns = false;
        for (const auto &address : resolvedWeb) { inheritedFakeDns |= address.isInSubnet(QHostAddress("198.18.0.0"), 15); }
        output << "VM system DNS returns inherited FakeDNS range: " << inheritedFakeDns << Qt::endl;
        QString label, failure;
        const auto candidate = amnezia::serialization::vless::Deserialize(entry.uri, &label, &failure);
        QProcess probe; probe.start(QCoreApplication::applicationDirPath() + "/hop_endpoint_probe.exe");
        if (!failure.isEmpty() || candidate.isEmpty() || !probe.waitForStarted(2000)) { return 7; }
        probe.write(QJsonDocument(candidate).toJson(QJsonDocument::Compact) + '\n'); probe.closeWriteChannel();
        const bool finished = probe.waitForFinished(13000);
        if (!finished) { probe.kill(); probe.waitForFinished(1000); }
        const auto result = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
        const bool reachable = finished && probe.exitCode() == 0 && result.value("ok").toBool();
        output << "VLESS preflight candidate " << checked << " reachable=" << reachable << Qt::endl;
        if (reachable) { uri = entry.uri; break; }
    }
    if (uri.isEmpty()) { return 7; }
    QString name, error; const auto xray = amnezia::serialization::vless::Deserialize(uri, &name, &error);
    if (xray.isEmpty() || !error.isEmpty()) { return 7; }
    QString testIp;
    for (const auto &address : smbOnly ? QList<QHostAddress>() : QHostInfo::fromName("www.gstatic.com").addresses()) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol) { testIp = address.toString(); break; }
    }
    if (!smbOnly && testIp.isEmpty()) { return 7; }
    const auto ip = hop::parseGeoData(QByteArray::fromHex("0a0e0a02525512080a04010000011020"), hop::GeoDataKind::GeoIp);
    const auto site = hop::parseGeoData(QByteArray::fromHex("0a1a0a025255121408031210626c6f636b65642e686f702e74657374"), hop::GeoDataKind::GeoSite);
    const QJsonArray rules = smbOnly ? QJsonArray {
        QJsonObject { { "kind", "ip" }, { "value", "10.0.2.0/24" }, { "target", "direct" } },
        QJsonObject { { "kind", "ip" }, { "value", "192.168.1.0/24" }, { "target", "direct" } }
    } : QJsonArray {
        QJsonObject { { "kind", "geoip" }, { "value", "RU" }, { "target", "direct" } },
        QJsonObject { { "kind", "geosite" }, { "value", "RU" }, { "target", "block" } }
    };
    auto compiled = hop::compileTrafficPolicy(QJsonObject { { "default", "vpn" }, { "rules", rules } }, &ip, &site);
    if (cdnOnly) {
        const auto fixtureRoot = QCoreApplication::applicationDirPath() + QStringLiteral("/cdn-routing-fixture/");
        QFile policyFile(fixtureRoot + QStringLiteral("policy.json"));
        if (!policyFile.open(QIODevice::ReadOnly)) { return 7; }
        const auto data = QJsonDocument::fromJson(policyFile.readAll()).object();
        const auto geoIp = hop::readGeoDataFile(fixtureRoot + QStringLiteral("geoip.dat"), hop::GeoDataKind::GeoIp);
        const auto geoSite = hop::readGeoDataFile(fixtureRoot + QStringLiteral("geosite.dat"), hop::GeoDataKind::GeoSite);
        compiled = hop::compileTrafficPolicy(data.value(QStringLiteral("policy")).toObject(), &geoIp, &geoSite);
        output << "CDN policy valid=" << compiled.isValid() << " rules=" << compiled.rules.size() << Qt::endl;
    }
    if (!compiled.isValid()) { return 7; }
    const QJsonObject config {
        { "hostName", QUrl(uri).host() }, { "dns1", "9.9.9.9" }, { "dns2", "8.8.8.8" },
        { "killSwitchOption", true }, { "splitTunnelType", 0 }, { "splitTunnelSites", QJsonArray() },
        { "hopTrafficPolicy", hop::serializeTrafficPolicy(compiled) },
        { "xray_config_data", QJsonObject { { "config", QString::fromUtf8(QJsonDocument(xray).toJson(QJsonDocument::Compact)) } } }
    };
    XrayProtocol protocol(config); QEventLoop wait; QTimer deadline; deadline.setSingleShot(true); deadline.start(70000);
    QObject::connect(&deadline, &QTimer::timeout, &wait, &QEventLoop::quit);
    bool started = false, finished = false, passed = false;
    const auto cleanup = qScopeGuard([&]() {
        protocol.stop(); IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) { auto reply = iface->clearSavedRoutes(); reply.waitForFinished(5000); });
    });
    QObject::connect(&protocol, &VpnProtocol::connectionStateChanged, &wait, [&](Vpn::ConnectionState state) {
        if (state == Vpn::Error) { output << "VLESS policy protocol failure: " << int(protocol.lastError()) << Qt::endl; wait.quit(); }
        if (state != Vpn::Connected || started) { return; } started = true;
        QTimer::singleShot(1000, &wait, [&]() {
            QNetworkAccessManager network; network.setProxy(QNetworkProxy::NoProxy);
            const auto request = [&](const QString &label, const QUrl &url, int expected, bool ipOnly = false) {
                QNetworkRequest value(url); value.setTransferTimeout(12000); value.setRawHeader("Connection", "close");
                if (ipOnly) { value.setRawHeader("Host", "www.gstatic.com"); value.setPeerVerifyName("www.gstatic.com"); }
                QEventLoop done; auto *reply = network.get(value);
                QObject::connect(reply, &QNetworkReply::finished, &done, &QEventLoop::quit); done.exec(QEventLoop::ExcludeUserInputEvents);
                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); const auto failure = reply->error();
                output << label << ": HTTP " << status << ", Qt " << int(failure) << Qt::endl; reply->deleteLater();
                return status == expected && (expected ? failure == QNetworkReply::NoError : failure != QNetworkReply::NoError);
            };
            if (cdnOnly) {
                const auto tunInterface = QNetworkInterface::interfaceFromIndex(NetworkUtilities::AdapterIndexTo(QHostAddress("198.18.0.1")));
                MIB_IPINTERFACE_ROW mtuRow {}; InitializeIpInterfaceEntry(&mtuRow); mtuRow.Family = AF_INET; mtuRow.InterfaceIndex = tunInterface.index();
                const int originalMtu = GetIpInterfaceEntry(&mtuRow) == NO_ERROR ? int(mtuRow.NlMtu) : 0;
                output << "CDN Windows TUN MTU=" << originalMtu << "; tun2socks stack default=1420" << Qt::endl;
                const bool originalDatagrams = probeCdnDatagramsInVm(output, QStringLiteral("original MTU"));
                probeCdnDnsInVm(output, true);
                VmCdnPacketCapture capture;
                output << "CDN VM packet capture started=" << capture.start() << Qt::endl;
                const auto tunneled = probeCdnImagesInVm(output, QStringLiteral("TUN Direct"));
                passed = tunneled.successful && tunneled.hashes == cdnBaseline.hashes;
                for (int round = 0; round < 4; ++round) {
                    const auto repeat = probeCdnImagesInVm(output, QStringLiteral("TUN Direct repeat %1").arg(round + 1));
                    passed &= repeat.successful && repeat.hashes == cdnBaseline.hashes;
                }
                const auto http1 = probeCdnImagesInVm(output, QStringLiteral("TUN Direct HTTP1"), QNetworkProxy::NoProxy, false);
                passed &= http1.successful && http1.hashes == cdnBaseline.hashes;
                const auto proxied = probeCdnImagesInVm(output, QStringLiteral("SOCKS Direct"), protocol.localProxy());
                passed &= proxied.successful && proxied.hashes == cdnBaseline.hashes;
                const auto setGuestMtu = [](int mtu) {
                    QProcess change;
                    change.start(QStringLiteral("C:/Windows/System32/netsh.exe"), {"interface", "ipv4", "set", "subinterface", "Hop-TUN", QStringLiteral("mtu=%1").arg(mtu), "store=active"});
                    return change.waitForFinished(5000) && change.exitStatus() == QProcess::NormalExit && change.exitCode() == 0;
                };
                if (originalMtu > 0 && originalMtu != 1420) {
                    const auto restoreMtu = qScopeGuard([&]() { output << "CDN original guest MTU restored=" << setGuestMtu(originalMtu) << Qt::endl; });
                    const bool changedMtu = setGuestMtu(1420);
                    output << "CDN guest MTU aligned=" << changedMtu << Qt::endl;
                    if (changedMtu) {
                        probeCdnDatagramsInVm(output, QStringLiteral("MTU1420"));
                        probeCdnImagesInVm(output, QStringLiteral("TUN MTU1420"));
                    }
                }
                if (originalMtu > 0 && originalMtu != 1420) { probeCdnDatagramsInVm(output, QStringLiteral("restored original MTU")); }
                if (!passed) {
                    // Только гостевая диагностика: отличаем отказ WFP от DNS/ядра, затем возвращаем защиту.
                    const bool disabled = protocol.updateTrafficProtection(false);
                    const auto withoutGuard = disabled ? probeCdnImagesInVm(output, QStringLiteral("TUN guard diagnostic")) : HopCdnProbeResult();
                    const bool restored = protocol.updateTrafficProtection(true);
                    output << "CDN guest guard disabled/restored=" << disabled << "/" << restored
                           << " loaded=" << (disabled && withoutGuard.successful) << Qt::endl;
                }
                output << "CDN VM packet counters=" << capture.counters() << Qt::endl;
                output << "CDN VM packet capture=" << capture.finish() << Qt::endl;
                passed &= originalMtu == 1420 && originalDatagrams;
            }
            else if (smbOnly) { passed = smb(QStringLiteral("with PRIVATE Direct")); }
            else {
            network.setProxy(protocol.localProxy());
            const bool proxyWorks = request("VLESS service SOCKS IP", QUrl(QStringLiteral("https://%1/generate_204").arg(testIp)), 204, true);
            network.clearConnectionCache(); network.setProxy(QNetworkProxy::NoProxy);
            output << "VLESS service SOCKS transport working: " << proxyWorks << Qt::endl;
            if (!proxyWorks) {
                // Диагностика только в VM: отличаем транспорт от фильтрации; защиту возвращаем до пакетных проверок.
                const bool disabled = protocol.updateTrafficProtection(false);
                network.setProxy(protocol.localProxy());
                const bool withoutGuard = disabled && request("VLESS service SOCKS without guard", QUrl(QStringLiteral("https://%1/generate_204").arg(testIp)), 204, true);
                network.clearConnectionCache(); network.setProxy(QNetworkProxy::NoProxy);
                const bool restored = protocol.updateTrafficProtection(true);
                output << "VM diagnostic guard disabled/restored; SOCKS working: " << disabled << "/" << restored << "; " << withoutGuard << Qt::endl;
                if (!restored) { wait.quit(); return; }
            }
            passed = request("VLESS policy VPN IP", QUrl(QStringLiteral("https://%1/generate_204").arg(testIp)), 204, true);
            passed &= request("VLESS policy VPN domain", QUrl("https://connectivitycheck.gstatic.com/generate_204"), 204);
            passed &= request("VLESS policy GeoSite Block", QUrl("http://blocked.hop.test/generate_204"), 0);
            const auto directInterface = QNetworkInterface::interfaceFromIndex(NetworkUtilities::AdapterIndexTo(QHostAddress("1.0.0.1"))).humanReadableName();
            output << "VLESS GeoIP classification interface: " << directInterface << Qt::endl;
            passed &= directInterface == QStringLiteral("Hop-TUN");
            output << "Prior VM Direct probe route interface: " << QNetworkInterface::interfaceFromIndex(NetworkUtilities::AdapterIndexTo(QHostAddress("1.1.1.1"))).humanReadableName() << Qt::endl;
            passed &= request("VLESS policy GeoIP Direct", QUrl("https://1.0.0.1/cdn-cgi/trace"), 200);
            QProcess health; health.start(QCoreApplication::applicationDirPath() + "/hop_tunnel_probe.exe");
            if (!health.waitForStarted(3000)) { passed = false; }
            else {
                health.write("{\"interface\":\"Hop-TUN\",\"resolver\":\"198.18.0.1\"}\n"); health.closeWriteChannel();
                if (!health.waitForFinished(10000)) { health.kill(); health.waitForFinished(3000); passed = false; }
                else { const bool healthy = QJsonDocument::fromJson(health.readAllStandardOutput()).object().value("ok").toBool();
                    output << "VLESS policy real DNS health: " << healthy << Qt::endl; passed &= healthy; }
            }
            passed &= probeVlessExternalUdpInVm(output);
            passed &= physicalDnsDeniedInVm(output);
            }
            network.clearConnectionCache(); protocol.stop();
            const int filters = IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) {
                auto reply = iface->trafficGuardFilterCount(); return reply.waitForFinished(3000) ? reply.returnValue() : -1;
            }, []() { return -1; });
            output << "VLESS policy filters after disconnect: " << filters << Qt::endl;
            passed &= filters == 0 && protocol.isCleanupComplete(); finished = true; wait.quit();
            if (smbOnly) { passed &= smb(QStringLiteral("after VPN")); }
        });
    });
    const auto result = protocol.start(); if (result != ErrorCode::NoError) { return 7; }
    if (!finished) { wait.exec(QEventLoop::ExcludeUserInputEvents); }
    return passed && finished ? 0 : 7;
}

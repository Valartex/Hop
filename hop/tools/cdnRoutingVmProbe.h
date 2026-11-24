#pragma once

#include <QCryptographicHash>
#include <QDnsLookup>
#include <QFile>
#include <QTemporaryDir>
#include <tlhelp32.h>

// Только публичные изображения WB, без cookies, авторизации и чтения профиля браузера.
struct HopCdnProbeResult {
    bool successful = true;
    QList<QByteArray> hashes;
};

inline int inspectCdnNetworkInVm(QTextStream &output)
{
    NET_LUID luid {};
    MIB_IPINTERFACE_ROW row {}; InitializeIpInterfaceEntry(&row); row.Family = AF_INET;
    if (ConvertInterfaceAliasToLuid(L"Hop-TUN", &luid) == NO_ERROR) {
        row.InterfaceLuid = luid;
        const auto code = GetIpInterfaceEntry(&row);
        output << "VM own Hop-TUN IP MTU query=" << code << " mtu=" << row.NlMtu << Qt::endl;
    }
    ULONG size = 0;
    if (GetAdaptersAddresses(AF_INET, 0, nullptr, nullptr, &size) == ERROR_BUFFER_OVERFLOW && size < 1024 * 1024) {
        QByteArray bytes(size, Qt::Uninitialized);
        auto *adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(bytes.data());
        if (GetAdaptersAddresses(AF_INET, 0, nullptr, adapters, &size) == NO_ERROR) {
            for (auto *adapter = adapters; adapter; adapter = adapter->Next) {
                if (adapter->OperStatus != IfOperStatusUp) { continue; }
                QStringList resolvers;
                for (auto *dns = adapter->FirstDnsServerAddress; dns; dns = dns->Next) {
                    if (dns->Address.lpSockaddr && dns->Address.lpSockaddr->sa_family == AF_INET) {
                        const auto address = reinterpret_cast<const sockaddr_in *>(dns->Address.lpSockaddr);
                        resolvers.append(QHostAddress(ntohl(address->sin_addr.s_addr)).toString());
                    }
                }
                output << "VM adapter=" << QString::fromWCharArray(adapter->FriendlyName) << " mtu=" << adapter->Mtu
                       << " DNS=" << resolvers.join(',') << Qt::endl;
            }
        }
    }
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) { return 7; }
    const auto close = qScopeGuard([&]() { CloseHandle(snapshot); });
    PROCESSENTRY32W process {}; process.dwSize = sizeof(process);
    if (!Process32FirstW(snapshot, &process)) { return 7; }
    do {
        if (QString::fromWCharArray(process.szExeFile).compare(QStringLiteral("Hop.exe"), Qt::CaseInsensitive) == 0) {
            output << "VM own network cleanup refuses a running Hop GUI" << Qt::endl; return 7;
        }
    } while (Process32NextW(snapshot, &process));
    const bool restored = IpcClient::withInterface([&](QSharedPointer<IpcInterfaceReplica> iface) {
        auto filters = iface->trafficGuardFilterCount();
        if (!filters.waitForFinished(3000) || filters.returnValue() != 0) { return false; }
        auto resolvers = iface->restoreResolvers(); const bool dns = resolvers.waitForFinished(5000) && resolvers.returnValue();
        auto routes = iface->clearSavedRoutes(); const bool cleared = routes.waitForFinished(5000) && routes.returnValue();
        auto flush = iface->flushDns(); const bool flushed = flush.waitForFinished(5000) && flush.returnValue();
        output << "VM own DNS/routes/cache cleanup=" << dns << "/" << cleared << "/" << flushed << Qt::endl;
        return dns && cleared && flushed;
    }, []() { return false; });
    return restored ? 0 : 7;
}

inline QMap<QString, QString> cdnTcpPeersInVm()
{
    ULONG size = 0;
    if (GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != ERROR_INSUFFICIENT_BUFFER || size > 1024 * 1024) { return {}; }
    QByteArray buffer(size, Qt::Uninitialized);
    if (GetExtendedTcpTable(buffer.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR) { return {}; }
    const auto *table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID *>(buffer.constData());
    QMap<QString, QString> result;
    for (DWORD index = 0; index < table->dwNumEntries; ++index) {
        const auto &row = table->table[index];
        if (ntohs(static_cast<u_short>(row.dwRemotePort)) != 443 || row.dwState != MIB_TCP_STATE_ESTAB) { continue; }
        const auto peer = QHostAddress(ntohl(row.dwRemoteAddr)).toString();
        result.insert(QStringLiteral("%1:%2:%3").arg(row.dwOwningPid).arg(row.dwLocalPort).arg(peer), peer);
    }
    return result;
}

inline bool probeCdnDatagramsInVm(QTextStream &output, const QString &phase)
{
    QFile fixture(QCoreApplication::applicationDirPath() + QStringLiteral("/cdn-routing-fixture/echo-port.json"));
    if (!fixture.open(QIODevice::ReadOnly)) { return false; }
    const int port = QJsonDocument::fromJson(fixture.readAll()).object().value("port").toInt();
    if (port < 1024 || port > 65535) { return false; }
    const QString gateway = QString::fromLatin1(amnezia::protocols::xray::defaultLocalAddr);
    const QStringList prefixes { QStringLiteral("10.0.2.2/32") };
    const bool captured = IpcClient::withInterface([&](QSharedPointer<IpcInterfaceReplica> iface) {
        auto route = iface->routeAddList(gateway, prefixes); return route.waitForFinished(5000) && route.returnValue() == 1;
    }, []() { return false; });
    if (!captured) { output << "CDN UDP fixture capture unavailable" << Qt::endl; return false; }
    const auto restoreRoute = qScopeGuard([&]() { IpcClient::withInterface([&](QSharedPointer<IpcInterfaceReplica> iface) {
        auto route = iface->routeDeleteList(gateway, prefixes); route.waitForFinished(5000);
    }); });
    bool passed = true;
    for (const int size : {1232, 1372, 1400, 1452, 4096}) {
        QUdpSocket socket; socket.setProxy(QNetworkProxy::NoProxy);
        const QByteArray payload(size, char(size % 251));
        bool same = false;
        if (socket.bind(QHostAddress(gateway), 0)) {
            QEventLoop done; QTimer deadline; deadline.setSingleShot(true);
            QObject::connect(&socket, &QUdpSocket::readyRead, &done, &QEventLoop::quit);
            QObject::connect(&deadline, &QTimer::timeout, &done, &QEventLoop::quit);
            deadline.start(1500);
            if (socket.writeDatagram(payload, QHostAddress("10.0.2.2"), quint16(port)) == payload.size()) {
                done.exec(QEventLoop::ExcludeUserInputEvents);
                same = socket.hasPendingDatagrams() && socket.receiveDatagram().data() == payload;
            }
        }
        output << "CDN UDP " << phase << " payload=" << size << " echo=" << same << Qt::endl;
        passed &= same;
    }
    return passed;
}

inline HopCdnProbeResult probeCdnImagesInVm(QTextStream &output, const QString &phase,
                                           const QNetworkProxy &proxy = QNetworkProxy::NoProxy, bool http2 = true)
{
    const QList<QUrl> images {
        QUrl(QStringLiteral("https://basket-41.wbbasket.ru/vol9856/part985640/985640668/images/c516x688/1.webp?picsUpdate=6")),
        QUrl(QStringLiteral("https://static-basket-01.wbbasket.ru/vol1/crm-bnrs/self_service_media/prod/2026-11-02/26ac8535-74f7-4a98-a644-cbf052f7eae3.webp"))
    };
    QNetworkAccessManager network; network.setProxy(proxy);
    HopCdnProbeResult result;
    for (const auto &url : images) {
        QNetworkRequest request(url);
        request.setTransferTimeout(8000);
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, http2);
        request.setRawHeader("User-Agent", "Hop routing diagnostics");
        QElapsedTimer elapsed; elapsed.start();
        const auto previousPeers = cdnTcpPeersInVm();
        QSet<QString> peers;
        const auto samplePeers = [&]() {
            const auto current = cdnTcpPeersInVm();
            for (auto it = current.cbegin(); it != current.cend(); ++it) { if (!previousPeers.contains(it.key())) { peers.insert(it.value()); } }
        };
        QEventLoop done;
        auto *reply = network.get(request);
        QTimer deadline; deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
        QObject::connect(reply, &QNetworkReply::finished, &done, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::metaDataChanged, &done, samplePeers);
        QTimer samples; samples.setInterval(50); QObject::connect(&samples, &QTimer::timeout, &done, samplePeers); samples.start();
        deadline.start(9000); done.exec(QEventLoop::ExcludeUserInputEvents);
        const auto body = reply->readAll();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool image = body.startsWith("RIFF") && body.mid(8, 4) == "WEBP";
        const bool successful = reply->error() == QNetworkReply::NoError && status == 200 && image;
        result.successful &= successful;
        result.hashes.append(successful ? QCryptographicHash::hash(body, QCryptographicHash::Sha256) : QByteArray());
        output << "CDN " << phase << " " << url.host() << ": HTTP=" << status
               << " Qt=" << int(reply->error()) << " bytes=" << body.size() << " WebP=" << image
               << " HTTP2=" << reply->attribute(QNetworkRequest::Http2WasUsedAttribute).toBool()
               << " ms=" << elapsed.elapsed() << " peers=" << QStringList(peers.values()).join(',') << " error=" << reply->errorString() << Qt::endl;
        reply->deleteLater();
    }
    network.clearConnectionCache();
    return result;
}

class VmCdnPacketCapture {
public:
    bool start() {
        const auto status = QString::fromUtf8(run({"status"})).toLower();
        const auto filters = QString::fromUtf8(run({"filter", "list"})).toLower();
        if (!m_ok || (!status.contains("not running") && !status.contains(QStringLiteral("не запущен")))
            || (!filters.contains("none") && !filters.contains(QStringLiteral("нет"))) || !m_temp.isValid()) { return false; }
        m_name = QStringLiteral("HopCdn-") + QUuid::createUuid().toString(QUuid::Id128);
        run({"filter", "add", m_name, "-t", "TCP", "-p", "443"});
        if (!m_ok) { m_name.clear(); return false; }
        run({"start", "--capture", "--pkt-size", "128", "--file-size", "16", "--file-name", m_temp.filePath("cdn.etl")});
        m_running = m_ok; return m_running;
    }
    QString finish() {
        if (!m_running) { return {}; }
        run({"stop"}); m_running = !m_ok;
        if (!m_ok) { return {}; }
        run({"etl2pcap", m_temp.filePath("cdn.etl"), "--out", m_temp.filePath("cdn.pcapng")});
        if (!m_ok) { return {}; }
        const auto target = QStringLiteral("//VBOXSVR/HopTestResults/") + m_name + QStringLiteral(".pcapng");
        return QFile::copy(m_temp.filePath("cdn.pcapng"), target) ? target : QString();
    }
    QString counters() { return QString::fromUtf8(run({"counters"})).left(4096); }
    ~VmCdnPacketCapture() {
        if (m_running) { run({"stop"}); }
        if (!m_name.isEmpty()) { run({"filter", "remove", m_name}); }
    }
private:
    QByteArray run(const QStringList &arguments) {
        QProcess process; process.start(QStringLiteral("C:/Windows/System32/pktmon.exe"), arguments);
        if (!process.waitForFinished(5000)) { process.kill(); process.waitForFinished(1000); m_ok = false; return {}; }
        m_ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
        return process.readAllStandardOutput();
    }
    QTemporaryDir m_temp;
    QString m_name;
    bool m_ok = true, m_running = false;
};

inline void probeCdnDnsInVm(QTextStream &output, bool vpn)
{
    QDnsLookup lookup(QDnsLookup::A, QStringLiteral("basket-41.wbbasket.ru"));
    // Тот же Quad9, но отдельный порт обходит перехват пользовательских DNS-запросов в FakeDNS.
    if (vpn) { lookup.setNameserver(QHostAddress(QStringLiteral("9.9.9.9")), 9953); }
    QEventLoop done; QTimer deadline; deadline.setSingleShot(true);
    QObject::connect(&lookup, &QDnsLookup::finished, &done, &QEventLoop::quit);
    QObject::connect(&deadline, &QTimer::timeout, &done, [&]() { lookup.abort(); done.quit(); });
    deadline.start(4000); lookup.lookup(); done.exec(QEventLoop::ExcludeUserInputEvents);
    QStringList addresses;
    for (const auto &record : lookup.hostAddressRecords()) { addresses.append(record.value().toString()); }
    output << "CDN DNS " << (vpn ? "VPN Quad9" : "before VPN system") << ": error=" << int(lookup.error())
           << " addresses=" << addresses.join(',') << Qt::endl;
}

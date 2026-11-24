#pragma once

#include "platforms/windows/native/service/windowsTrafficGuard.h"
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QRandomGenerator>
#include <QSet>

// Изолированная запись WFP-событий VM; отдельный прогон проверяет обмен без её включения.
class VmUdpDropLog {
public:
    explicit VmUdpDropLog(bool requested = true) {
        if (!requested) { return; }
        if (FwpmEngineOpen0(nullptr, RPC_C_AUTHN_WINNT, nullptr, nullptr, &engine) != ERROR_SUCCESS) { return; }
        FWP_VALUE0 enabled {}; enabled.type = FWP_UINT32; enabled.uint32 = 1;
        if (FwpmEngineGetOption0(engine, FWPM_ENGINE_COLLECT_NET_EVENTS, &previous) != ERROR_SUCCESS
            || FwpmEngineSetOption0(engine, FWPM_ENGINE_COLLECT_NET_EVENTS, &enabled) != ERROR_SUCCESS) { return; }
        collecting = true; GetSystemTimeAsFileTime(&start);
    }
    ~VmUdpDropLog() {
        if (collecting && previous) { FwpmEngineSetOption0(engine, FWPM_ENGINE_COLLECT_NET_EVENTS, previous); }
        if (previous) { FwpmFreeMemory0(reinterpret_cast<void **>(&previous)); }
        if (engine) { FwpmEngineClose0(engine); }
    }
    void report(int port, QTextStream &output) {
        if (!collecting) { output << "VM UDP WFP event collection unavailable" << Qt::endl; return; }
        FWPM_NET_EVENT_ENUM_TEMPLATE0 options {}; options.startTime = start; GetSystemTimeAsFileTime(&options.endTime);
        HANDLE enumeration = nullptr;
        if (FwpmNetEventCreateEnumHandle0(engine, &options, &enumeration) != ERROR_SUCCESS) { return; }
        const auto close = qScopeGuard([&]() { FwpmNetEventDestroyEnumHandle0(engine, enumeration); });
        FWPM_NET_EVENT0 **events = nullptr; UINT32 count = 0;
        if (FwpmNetEventEnum0(engine, enumeration, 1024, &events, &count) != ERROR_SUCCESS) { return; }
        const auto release = qScopeGuard([&]() { if (events) { FwpmFreeMemory0(reinterpret_cast<void **>(&events)); } });
        int drops = 0;
        for (UINT32 index = 0; index < count; ++index) {
            const auto &event = *events[index];
            if (event.type != FWPM_NET_EVENT_TYPE_CLASSIFY_DROP || event.header.ipProtocol != IPPROTO_UDP
                || event.header.remotePort != port || !event.classifyDrop) { continue; }
            ++drops;
            if (drops > 8) { continue; }
            FWPM_FILTER0 *filter = nullptr;
            const auto found = FwpmFilterGetById0(engine, event.classifyDrop->filterId, &filter);
            output << "VM UDP WFP drop layer=" << event.classifyDrop->layerId << " filter=" << event.classifyDrop->filterId
                   << " name=" << (found == ERROR_SUCCESS && filter && filter->displayData.name ? QString::fromWCharArray(filter->displayData.name) : QStringLiteral("unavailable"))
                   << " localPort=" << event.header.localPort << Qt::endl;
            if (filter) { FwpmFreeMemory0(reinterpret_cast<void **>(&filter)); }
        }
        output << "VM UDP WFP drops=" << drops << Qt::endl;
        start = options.endTime;
    }
private:
    HANDLE engine = nullptr;
    FWP_VALUE0 *previous = nullptr;
    FILETIME start {};
    bool collecting = false;
};

inline int probeDirectUdpLeaseInVm(const QString &extra, QTextStream &output, XrayProtocol *protocol = nullptr)
{
    // Вызывается после проверки VirtualBox в main. Фильтры меняются только в гостевой ОС.
    const auto match = QRegularExpression(QStringLiteral("# HopDirectPort=(\\d+)")).match(extra);
    const int port = match.captured(1).toInt();
    if (!match.hasMatch() || port < 1024 || port > 65534) { return 7; }
    const auto uplink = NetworkUtilities::getGatewayAndIfaceTo(QHostAddress(protocol ? "1.1.1.1" : "10.0.2.2")).second;
    QHostAddress source;
    for (const auto &entry : uplink.addressEntries()) {
        if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !entry.ip().isLoopback()) { source = entry.ip(); break; }
    }
    if (source.isNull() || uplink.humanReadableName() == QStringLiteral("Hop-TUN")) { return 7; }
    const auto cleanup = qScopeGuard([&]() {
        WindowsTrafficGuard::endDirectPolicy();
        if (protocol) { protocol->updateTrafficProtection(true); } else { WindowsTrafficGuard::clear(); }
    });
    const QJsonObject config { { "killSwitchOption", true }, { "splitTunnelType", 0 },
        { "vpnServer", "10.0.2.2" }, { "hopTransportPort", 443 }, { "hopTransportProtocol", "tcp" } };
    if ((!protocol && !WindowsTrafficGuard::apply(config, {})) || !WindowsTrafficGuard::beginDirectPolicy(uplink.index(), source.toString())) { return 7; }
    QUdpSocket socket; socket.setProxy(QNetworkProxy::NoProxy);
    if (!socket.bind(protocol ? QHostAddress(amnezia::protocols::xray::defaultLocalAddr) : source, 0)) { return 7; }
    const auto lease = protocol ? 0 : WindowsTrafficGuard::openDirectSocket(source.toString(), "10.0.2.2", port, IPPROTO_UDP);
    if (!protocol && !lease) { return 7; }
    const auto closeLease = qScopeGuard([&]() { socket.close(); WindowsTrafficGuard::closeDirectSocket(lease); });
    VmUdpDropLog dropLog(!extra.contains(QStringLiteral("# HopUdpNoEvents")));
    if (protocol) {
        // Новый сокет постороннего приложения на физическом интерфейсе не должен получить разрешение ответа TUN.
        QUdpSocket physical; physical.setProxy(QNetworkProxy::NoProxy);
        if (!physical.bind(source, 0)) { return 7; }
        const DWORD index = htonl(uplink.index());
        if (setsockopt(SOCKET(physical.socketDescriptor()), IPPROTO_IP, IP_UNICAST_IF,
                       reinterpret_cast<const char *>(&index), sizeof(index)) != 0) { return 7; }
        physical.writeDatagram(QByteArrayLiteral("hop-udp-lease-physical-control"), QHostAddress("10.0.2.2"), quint16(port));
        const bool leaked = physical.waitForReadyRead(300);
        output << "TUN UDP guard denies new physical socket=" << !leaked << Qt::endl;
        if (leaked) { return 7; }
    }
    bool passed = true;
    for (int phase = 0; phase < 3; ++phase) {
        if (phase == 2 && !(protocol ? protocol->updateTrafficProtection(false) : WindowsTrafficGuard::clear())) { return 7; }
        const auto nonce = QByteArrayLiteral("hop-udp-lease-") + QByteArray::number(QRandomGenerator::system()->generate64(), 16);
        QSet<int> received;
        QEventLoop wait; QTimer deadline; deadline.setSingleShot(true);
        QTimer churn; churn.setInterval(20);
        int mutations = 0, failures = 0;
        QObject::connect(&churn, &QTimer::timeout, &wait, [&]() {
            const auto transient = WindowsTrafficGuard::openDirectSocket(source.toString(), "10.0.2.2", port + 1, IPPROTO_UDP);
            if (transient) { WindowsTrafficGuard::closeDirectSocket(transient); ++mutations; } else { ++failures; }
        });
        QObject::connect(&socket, &QUdpSocket::readyRead, &wait, [&]() {
            while (socket.hasPendingDatagrams()) {
                const auto datagram = socket.receiveDatagram();
                const auto data = datagram.data();
                if (datagram.senderAddress() != QHostAddress("10.0.2.2") || datagram.senderPort() != port
                    || data.size() != nonce.size() + 1 || !data.startsWith(nonce)) { continue; }
                received.insert(quint8(data.back()));
                if (received.size() == 64) { wait.quit(); }
            }
        });
        QObject::connect(&deadline, &QTimer::timeout, &wait, &QEventLoop::quit);
        deadline.start(1800);
        if (phase == 1) { churn.start(); }
        if (socket.writeDatagram(nonce, QHostAddress("10.0.2.2"), quint16(port)) != nonce.size()) { return 7; }
        wait.exec(QEventLoop::ExcludeUserInputEvents);
        churn.stop();
        output << (protocol ? "TUN UDP" : "Direct UDP") << " receive-only phase=" << phase << " received=" << received.size()
               << "/64 filterMutations=" << mutations << " mutationErrors=" << failures << Qt::endl;
        dropLog.report(port, output);
        passed &= received.size() == 64 && failures == 0;
    }
    return passed ? 0 : 7;
}

inline int probeTunUdpLeaseInVm(const QString &extra, QTextStream &output)
{
    // Агент запускает консольную копию, но IPC может обслуживать установленная служба VM.
    QFile installed(QStringLiteral("C:/Program Files/Hop VM Install/Hop-service.exe"));
    QFile staged(QCoreApplication::applicationDirPath() + QStringLiteral("/Hop-service.exe"));
    if (!installed.open(QIODevice::ReadOnly) || !staged.open(QIODevice::ReadOnly)
        || installed.size() > 64 * 1024 * 1024 || staged.size() > 64 * 1024 * 1024) { return 7; }
    const auto installedHash = QCryptographicHash::hash(installed.readAll(), QCryptographicHash::Sha256);
    const auto stagedHash = QCryptographicHash::hash(staged.readAll(), QCryptographicHash::Sha256);
    output << "TUN UDP installed service SHA256=" << installedHash.toHex() << " matches stage=" << (installedHash == stagedHash) << Qt::endl;
    if (installedHash != stagedHash) { return 7; }
    // Синтетический VLESS-выход не используется: тестовый UDP направляется только Direct к loopback хоста VM.
    QString label, error;
    const auto xray = amnezia::serialization::vless::Deserialize(
        QStringLiteral("vless://00000000-0000-4000-8000-000000000001@1.1.1.1:443?encryption=none&security=none&type=tcp"), &label, &error);
    if (xray.isEmpty() || !error.isEmpty()) { return 7; }
    const auto policy = hop::compileTrafficPolicy(QJsonObject { { "default", "vpn" }, { "rules", QJsonArray {
        QJsonObject { { "kind", "domain" }, { "value", "full:blocked.hop.test" }, { "target", "block" } },
        QJsonObject { { "kind", "ip" }, { "value", "10.0.2.2/32" }, { "target", "direct" } }
    } } });
    if (!policy.isValid()) { return 7; }
    const QJsonObject config { { "hostName", "1.1.1.1" }, { "dns1", "9.9.9.9" }, { "dns2", "8.8.8.8" },
        { "killSwitchOption", true }, { "splitTunnelType", 0 }, { "splitTunnelSites", QJsonArray() },
        { "hopTrafficPolicy", hop::serializeTrafficPolicy(policy) },
        { "xray_config_data", QJsonObject { { "config", QString::fromUtf8(QJsonDocument(xray).toJson(QJsonDocument::Compact)) } } } };
    XrayProtocol protocol(config); QEventLoop wait; QTimer deadline; deadline.setSingleShot(true);
    bool started = false; int result = 7;
    const auto cleanup = qScopeGuard([&]() { protocol.stop(); });
    QObject::connect(&deadline, &QTimer::timeout, &wait, &QEventLoop::quit);
    QObject::connect(&protocol, &VpnProtocol::connectionStateChanged, &wait, [&](Vpn::ConnectionState state) {
        if (state == Vpn::Error) { output << "TUN UDP protocol error=" << int(protocol.lastError()) << Qt::endl; wait.quit(); }
        if (state != Vpn::Connected || started) { return; }
        started = true;
        QTimer::singleShot(500, &wait, [&]() {
            const auto gateway = QString::fromLatin1(amnezia::protocols::xray::defaultLocalAddr);
            const QStringList prefixes { QStringLiteral("10.0.2.2/32") };
            const bool routed = IpcClient::withInterface([&](QSharedPointer<IpcInterfaceReplica> iface) {
                auto reply = iface->routeAddList(gateway, prefixes); return reply.waitForFinished(5000) && reply.returnValue() == 1;
            }, []() { return false; });
            if (routed) { result = probeDirectUdpLeaseInVm(extra, output, &protocol); }
            IpcClient::withInterface([&](QSharedPointer<IpcInterfaceReplica> iface) {
                auto reply = iface->routeDeleteList(gateway, prefixes); reply.waitForFinished(5000);
            });
            protocol.stop(); wait.quit();
        });
    });
    deadline.start(25000);
    if (protocol.start() != ErrorCode::NoError) { return 7; }
    wait.exec(QEventLoop::ExcludeUserInputEvents);
    output << "TUN UDP cleanup=" << protocol.isCleanupComplete() << Qt::endl;
    return result;
}

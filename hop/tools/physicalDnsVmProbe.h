#pragma once
#include <QNetworkInterface>
#include <QTemporaryDir>
#include <QThread>
#include <QtEndian>
#include <array>
#include "core/utils/networkUtilities.h"

class VmPhysicalDnsCapture {
public:
    explicit VmPhysicalDnsCapture(const QNetworkInterface &physical) : m_physical(physical) {}
    ~VmPhysicalDnsCapture() {
        if (m_capturing) { run({ "stop" }); }
        for (const auto &name : m_filters) { run({ "filter", "remove", name }); }
    }
    bool initialize() {
        const auto status = run({ "status" });
        const auto text = QString::fromUtf8(status).toLower();
        // Не перехватываем уже запущенный пользователем сеанс или чужие фильтры.
        if (!m_ok || (!text.contains(QStringLiteral("не запущен")) && !text.contains(QStringLiteral("not running")))) { return false; }
        const auto filters = QString::fromUtf8(run({ "filter", "list" })).toLower();
        if (!m_ok || (!filters.contains(QStringLiteral("нет")) && !filters.contains(QStringLiteral("none")))) { return false; }
        const auto components = QJsonDocument::fromJson(run({ "list", "--json" })).array();
        if (!m_ok) { return false; }
        for (const auto &group : components) {
            for (const auto &value : group.toObject().value("Components").toArray()) {
                const auto component = value.toObject();
                // Сопоставление выполняется по точному ifIndex аппаратного компонента.
                bool same = false;
                for (const auto &property : component.value("Properties").toArray()) {
                    const auto item = property.toObject(); same |= item.value("Name") == QStringLiteral("ifIndex")
                        && item.value("Value").toInt() == m_physical.index();
                }
                if (same && component.value("DriverName").toString().compare(QStringLiteral("wfplwfs.sys"),Qt::CaseInsensitive) != 0) {
                    m_component = component.value("Id").toInt(); break;
                }
            }
            if (m_component > 0) { break; }
        }
        if (m_component <= 0 || !m_temp.isValid()) { return false; }
        const auto suffix = QUuid::createUuid().toString(QUuid::Id128);
        for (const auto port : { 53,853,11944 }) {
            const auto name = QStringLiteral("HopDnsVm-") + suffix + QLatin1Char('-') + QString::number(port);
            run({ "filter", "add", name, "-i", port == 11944 ? "ff02::1" : "1.1.1.1", "-t", "UDP", "-p", QString::number(port) });
            if (!m_ok) { return false; } m_filters.append(name);
        }
        const auto etl = m_temp.filePath("dns.etl");
        run({ "start", "--capture", "--comp", QString::number(m_component), "--pkt-size", "128", "--file-size", "8", "--file-name", etl });
        m_capturing = m_ok; return m_capturing;
    }
    std::array<int,3> finish(const QByteArray &first, const QByteArray &second, const QByteArray &third) {
        if (!m_capturing) { return { -1,-1,-1 }; }
        QThread::msleep(500); run({ "stop" }); m_capturing = !m_ok;
        if (!m_ok) { return { -1,-1,-1 }; }
        const auto pcap = m_temp.filePath("dns.pcapng");
        run({ "etl2pcap", m_temp.filePath("dns.etl"), "--out", pcap });
        QFile file(pcap);
        if (!m_ok || !file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) { return { -1,-1,-1 }; }
        const auto bytes = file.readAll(); std::array<int,3> counts {}; qsizetype offset = 0;
        if (bytes.size() < 28 || bytes.left(4) != QByteArray::fromHex("0a0d0d0a")
            || qFromLittleEndian<quint32>(bytes.constData() + 8) != 0x1a2b3c4d) { return { -1,-1,-1 }; }
        // Считаем только данные Enhanced Packet Blocks; служебные строки не являются пакетами.
        while (offset + 12 <= bytes.size()) {
            const auto *block = bytes.constData() + offset;
            const auto type = qFromLittleEndian<quint32>(block), length = qFromLittleEndian<quint32>(block + 4);
            if (length < 12 || length % 4 || length > quint64(bytes.size() - offset)) { return { -1,-1,-1 }; }
            if (type == 6 && length >= 32) {
                const auto captured = qFromLittleEndian<quint32>(block + 20);
                if (captured > length - 32) { return { -1,-1,-1 }; }
                const auto packet = QByteArrayView(block + 28,captured);
                counts[0] += packet.contains(QByteArrayView(first)); counts[1] += packet.contains(QByteArrayView(second));
                counts[2] += packet.contains(QByteArrayView(third));
            }
            offset += length;
        }
        return offset == bytes.size() ? counts : std::array<int,3> { -1,-1,-1 };
    }
private:
    QByteArray run(const QStringList &arguments) {
        QProcess process; process.start(QStringLiteral("C:/Windows/System32/pktmon.exe"),arguments);
        if (!process.waitForFinished(5000)) { process.kill(); process.waitForFinished(2000); m_ok = false; return {}; }
        const auto bytes = process.readAllStandardOutput();
        m_ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 && bytes.size() <= 256 * 1024;
        return bytes;
    }
    QNetworkInterface m_physical; QTemporaryDir m_temp; QStringList m_filters;
    bool m_ok = false, m_capturing = false; int m_component = 0;
};

// Только VM-проба: принудительно выбирает физический исходный IPv4 и интерфейс.
// Успехом считается именно WSAEACCES, а не отсутствие ответа сервера/маршрута.
inline bool physicalDnsDeniedInVm(QTextStream &output, bool protectedTraffic = true)
{
    // Таблица маршрутов уже может направлять NAT-подсеть в TUN; выбираем сам NIC,
    // к которому назначен адрес NAT-сети стенда, независимо от текущего маршрута.
    QNetworkInterface physical;
    for (const auto &candidate : QNetworkInterface::allInterfaces()) {
        if (!(candidate.flags() & QNetworkInterface::IsUp) || candidate.flags() & QNetworkInterface::IsLoopBack
            || candidate.humanReadableName().startsWith("Hop")) { continue; }
        bool nat = false;
        for (const auto &entry : candidate.addressEntries()) {
            nat |= entry.ip().protocol() == QAbstractSocket::IPv4Protocol && entry.prefixLength() > 0
                && QHostAddress("10.0.2.2").isInSubnet(entry.ip(), entry.prefixLength());
        }
        if (nat) {
            if (physical.isValid()) { output << "Ambiguous VM NAT interface" << Qt::endl; return false; }
            physical = candidate;
        }
    }
    if (!physical.isValid()) { output << "VM NAT interface unavailable" << Qt::endl; return false; }
    QHostAddress local;
    for (const auto &entry : physical.addressEntries()) {
        if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !entry.ip().isLoopback()) { local = entry.ip(); break; }
    }
    if (local.isNull()) { return false; }
    WSADATA data {}; if (WSAStartup(MAKEWORD(2,2),&data) != 0) { return false; }
    const auto cleanup = qScopeGuard([]() { WSACleanup(); });
    VmPhysicalDnsCapture capture(physical);
    if (!capture.initialize()) { output << "Physical DNS packet capture unavailable (existing session/filter or unsupported environment)" << Qt::endl; return false; }
    const auto sentinel53 = QByteArray::fromHex("7a110100000100000000000003686f7003696e760000010001");
    auto sentinel853 = sentinel53; sentinel853[1] = char(0x12);
    const auto sentinel6 = QByteArrayLiteral("hop-ipv6-egress-sentinel");
    bool denied = true;
    for (const auto protocol : { IPPROTO_UDP, IPPROTO_TCP }) {
        if (!protectedTraffic && protocol == IPPROTO_TCP) { continue; }
        for (const auto port : { 53, 853 }) {
            const auto socket = ::socket(AF_INET,protocol == IPPROTO_UDP ? SOCK_DGRAM : SOCK_STREAM,protocol);
            if (socket == INVALID_SOCKET) { return false; }
            const auto close = qScopeGuard([&]() { closesocket(socket); });
            sockaddr_in source {}; source.sin_family = AF_INET; source.sin_addr.s_addr = htonl(local.toIPv4Address());
            const DWORD iface = htonl(physical.index()); u_long nonblocking = 1;
            if (::bind(socket,reinterpret_cast<sockaddr *>(&source),sizeof(source)) != 0
                || setsockopt(socket,IPPROTO_IP,IP_UNICAST_IF,reinterpret_cast<const char *>(&iface),sizeof(iface)) != 0
                || ioctlsocket(socket,FIONBIO,&nonblocking) != 0) { return false; }
            sockaddr_in destination {}; destination.sin_family = AF_INET; destination.sin_port = htons(port);
            destination.sin_addr.s_addr = htonl(QHostAddress("1.1.1.1").toIPv4Address());
            const int connected = ::connect(socket,reinterpret_cast<sockaddr *>(&destination),sizeof(destination));
            int error = connected == 0 ? 0 : WSAGetLastError();
            if (protocol == IPPROTO_UDP && error == 0) {
                // UDP connect не посылает пакет: ALE-проверка может наступить лишь при send.
                const auto &sentinel = port == 53 ? sentinel53 : sentinel853;
                error = ::send(socket,sentinel.constData(),int(sentinel.size()),0) == SOCKET_ERROR ? WSAGetLastError() : 0;
            }
            if (error == WSAEWOULDBLOCK) {
                fd_set writable, exceptional; FD_ZERO(&writable); FD_ZERO(&exceptional); FD_SET(socket,&writable); FD_SET(socket,&exceptional);
                timeval timeout { 2,0 };
                const int ready = select(0,nullptr,&writable,&exceptional,&timeout); int size = sizeof(error);
                if (ready <= 0 || getsockopt(socket,SOL_SOCKET,SO_ERROR,reinterpret_cast<char *>(&error),&size) != 0) { error = WSAETIMEDOUT; }
            }
            output << "Physical " << (protocol == IPPROTO_UDP ? "UDP" : "TCP") << " DNS port " << port << ": WSA " << error << Qt::endl;
            if (protocol == IPPROTO_TCP) { denied &= error == WSAEACCES; }
            else { denied &= error == 0 || (protectedTraffic && error == WSAEACCES); }
        }
    }
    QHostAddress ipv6;
    for (const auto &entry : physical.addressEntries()) {
        if (entry.ip().protocol() == QAbstractSocket::IPv6Protocol && entry.ip().isLinkLocal()) { ipv6 = entry.ip(); break; }
    }
    if (ipv6.isNull()) { output << "Physical IPv6 link-local unavailable; IPv6 capture not accepted" << Qt::endl; return false; }
    const auto socket6 = ::socket(AF_INET6,SOCK_DGRAM,IPPROTO_UDP);
    if (socket6 == INVALID_SOCKET) { return false; }
    const auto close6 = qScopeGuard([&]() { closesocket(socket6); });
    sockaddr_in6 local6 {}; local6.sin6_family = AF_INET6; local6.sin6_scope_id = physical.index();
    const auto localBytes = ipv6.toIPv6Address(); memcpy(&local6.sin6_addr,localBytes.c,16);
    const DWORD iface6 = physical.index();
    if (::bind(socket6,reinterpret_cast<sockaddr *>(&local6),sizeof(local6)) != 0
        || setsockopt(socket6,IPPROTO_IPV6,IPV6_MULTICAST_IF,reinterpret_cast<const char *>(&iface6),sizeof(iface6)) != 0) { return false; }
    sockaddr_in6 target6 {}; target6.sin6_family = AF_INET6; target6.sin6_scope_id = physical.index(); target6.sin6_port = htons(11944);
    const auto remoteBytes = QHostAddress("ff02::1").toIPv6Address(); memcpy(&target6.sin6_addr,remoteBytes.c,16);
    const int sent6 = ::sendto(socket6,sentinel6.constData(),int(sentinel6.size()),0,reinterpret_cast<sockaddr *>(&target6),sizeof(target6));
    const int error6 = sent6 == SOCKET_ERROR ? WSAGetLastError() : 0;
    output << "Physical IPv6 UDP link-local: WSA " << error6 << Qt::endl;
    denied &= error6 == 0 || (protectedTraffic && error6 == WSAEACCES);
    const auto counts = capture.finish(sentinel53,sentinel853,sentinel6);
    output << "Physical NIC captured DNS sentinel packets 53/853: " << counts[0] << "/" << counts[1]
        << "; protected=" << protectedTraffic << Qt::endl;
    output << "Physical NIC captured IPv6 link-local sentinel packets: " << counts[2] << "; protected=" << protectedTraffic << Qt::endl;
    return denied && (protectedTraffic ? counts[0] == 0 && counts[1] == 0 && counts[2] == 0 : counts[0] > 0 && counts[1] > 0 && counts[2] > 0);
}

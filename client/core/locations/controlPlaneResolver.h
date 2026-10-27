#pragma once
#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#endif
#include <QDateTime>
#include <QEventLoop>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>
#include <QUdpSocket>
#ifdef Q_OS_WIN
#include "core/utils/ipcClient.h"
#endif

namespace hop::controlPlane {
inline QProcessEnvironment environment()
{
    auto result = QProcessEnvironment::systemEnvironment();
    const auto existing = result.value(QStringLiteral("GODEBUG"));
    result.insert(QStringLiteral("GODEBUG"), existing + (existing.isEmpty() ? QString() : QStringLiteral(",")) + QStringLiteral("netdns=go"));
    return result;
}
inline bool permit(const QString &address, int port, bool udp = false)
{
#ifdef Q_OS_WIN
    return IpcClient::withInterface([&](QSharedPointer<IpcInterfaceReplica> iface) {
        auto reply = iface->permitControlEndpoint(address, port, udp);
        return reply.waitForFinished(3000) && reply.returnValue();
    }, []() { return false; });
#else
    Q_UNUSED(address); Q_UNUSED(port); Q_UNUSED(udp); return false;
#endif
}
inline QStringList resolvers()
{
    QStringList result;
#ifdef Q_OS_WIN
    ULONG size = 16384;
    QByteArray buffer(static_cast<qsizetype>(size), Qt::Uninitialized);
    auto *adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());
    ULONG error = GetAdaptersAddresses(AF_INET, 0, nullptr, adapters, &size);
    if (error == ERROR_BUFFER_OVERFLOW && size <= 1024 * 1024) {
        buffer.resize(size); adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());
        error = GetAdaptersAddresses(AF_INET, 0, nullptr, adapters, &size);
    }
    if (error != NO_ERROR) { return result; }
    for (auto *adapter = adapters; adapter && result.size() < 16; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp) { continue; }
        for (auto *server = adapter->FirstDnsServerAddress; server && result.size() < 16; server = server->Next) {
            if (!server->Address.lpSockaddr || server->Address.lpSockaddr->sa_family != AF_INET) { continue; }
            const auto *address = reinterpret_cast<const sockaddr_in *>(server->Address.lpSockaddr);
            const auto text = QHostAddress(ntohl(address->sin_addr.s_addr)).toString();
            if (!result.contains(text)) { result.append(text); }
        }
    }
#endif
    return result;
}
inline quint16 word(const QByteArray &bytes, qsizetype position)
{
    return static_cast<quint16>((static_cast<unsigned char>(bytes.at(position)) << 8) | static_cast<unsigned char>(bytes.at(position + 1)));
}
inline bool skipName(const QByteArray &bytes, qsizetype &position)
{
    for (int labels = 0; labels < 128 && position < bytes.size(); ++labels) {
        const auto size = static_cast<unsigned char>(bytes.at(position++));
        if (!size) { return true; }
        if ((size & 0xc0) == 0xc0) { if (position >= bytes.size()) { return false; } ++position; return true; }
        if (size > 63 || position + size > bytes.size()) { return false; }
        position += size;
    }
    return false;
}
inline QStringList parseAnswer(const QByteArray &bytes)
{
    QStringList result;
    if (bytes.size() < 12 || !(static_cast<unsigned char>(bytes.at(2)) & 0x80) || (bytes.at(3) & 0x0f)) { return result; }
    const auto questions = word(bytes, 4), answers = word(bytes, 6);
    if (questions > 8 || answers > 128) { return result; }
    qsizetype position = 12;
    for (int index = 0; index < questions; ++index) {
        if (!skipName(bytes, position) || position + 4 > bytes.size()) { return {}; } position += 4;
    }
    for (int index = 0; index < answers; ++index) {
        if (!skipName(bytes, position) || position + 10 > bytes.size()) { return {}; }
        const auto type = word(bytes, position), klass = word(bytes, position + 2), length = word(bytes, position + 8);
        position += 10;
        if (position + length > bytes.size()) { return {}; }
        if (type == 1 && klass == 1 && length == 4) {
            const auto value = (quint32(word(bytes, position)) << 16) | word(bytes, position + 2);
            const auto text = QHostAddress(value).toString();
            if (!result.contains(text) && result.size() < 16) { result.append(text); }
        }
        position += length;
    }
    return result;
}

inline QStringList resolve(const QString &host)
{
    const QHostAddress literal(host);
    if (!literal.isNull()) { return { literal.toString() }; }
    const auto encoded = QUrl::toAce(host);
    if (encoded.isEmpty() || encoded.size() > 253) { return {}; }
    QByteArray packet = QByteArray::fromHex("000001000001000000000000");
    const auto id = QRandomGenerator::system()->generate();
    packet[0] = static_cast<char>(id >> 8); packet[1] = static_cast<char>(id);
    for (const auto &label : encoded.split('.')) {
        if (label.isEmpty() || label.size() > 63) { return {}; }
        packet.append(static_cast<char>(label.size())); packet.append(label);
    }
    packet.append(QByteArray::fromHex("0000010001"));
    const auto servers = resolvers();
    for (const auto &server : servers.mid(0, 4)) {
        permit(server, 53, true);
        QUdpSocket socket;
        socket.setProxy(QNetworkProxy::NoProxy);
        if (!socket.bind(QHostAddress(QHostAddress::AnyIPv4), 0)) { continue; }
        QEventLoop wait;
        QByteArray answer;
        QObject::connect(&socket, &QUdpSocket::readyRead, &wait, [&]() {
            while (socket.hasPendingDatagrams()) {
                QByteArray bytes(4096, Qt::Uninitialized); QHostAddress sender; quint16 port = 0;
                const auto size = socket.readDatagram(bytes.data(), bytes.size(), &sender, &port);
                if (size < 12 || sender != QHostAddress(server) || port != 53 || bytes.left(2) != packet.left(2)) { continue; }
                bytes.resize(size); answer = bytes; wait.quit(); return;
            }
        });
        QTimer::singleShot(1750, &wait, &QEventLoop::quit);
        if (socket.writeDatagram(packet, QHostAddress(server), 53) != packet.size()) { continue; }
        wait.exec(QEventLoop::ExcludeUserInputEvents);
        const auto addresses = parseAnswer(answer);
        if (!addresses.isEmpty()) { return addresses; }
    }
    return {};
}
}

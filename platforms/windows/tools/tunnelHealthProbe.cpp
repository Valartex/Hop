#include <winsock2.h>
#include <ws2tcpip.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QRandomGenerator>
#include <QTextStream>
#include <QTimer>
#include <QUdpSocket>

// Проверяет путь через собственный TUN. Никаких маршрутов, драйверов или исключений firewall не создаёт.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) { return 2; }
    const auto bytes = input.readLine(1026);
    const auto request = QJsonDocument::fromJson(bytes).object();
    const auto alias = request.value(QStringLiteral("interface")).toString();
    const QHostAddress resolver(request.value(QStringLiteral("resolver")).toString());
    if (bytes.size() > 1024 || (alias != QStringLiteral("Hop") && alias != QStringLiteral("Hop-TUN"))
        || resolver.protocol() != QAbstractSocket::IPv4Protocol || resolver.isLoopback() || resolver.isMulticast()) { return 2; }
    const auto finishUnsupported = []() {
        QTextStream(stdout) << "{\"supported\":false,\"ok\":false,\"latencyMs\":-1}" << Qt::endl;
        return 4;
    };
    NET_LUID luid {};
    if (ConvertInterfaceAliasToLuid(reinterpret_cast<PCWSTR>(alias.utf16()), &luid) != NO_ERROR) { return finishUnsupported(); }
    QHostAddress source;
    for (const auto &adapter : QNetworkInterface::allInterfaces()) {
        if (adapter.humanReadableName() != alias) { continue; }
        for (const auto &address : adapter.addressEntries()) {
            if (address.ip().protocol() == QAbstractSocket::IPv4Protocol) { source = address.ip(); break; }
        }
    }
    if (source.isNull()) { return finishUnsupported(); }
    SOCKADDR_INET destination {}, local {}, selected {};
    destination.Ipv4.sin_family = AF_INET;
    destination.Ipv4.sin_addr.s_addr = htonl(resolver.toIPv4Address());
    local.Ipv4.sin_family = AF_INET;
    local.Ipv4.sin_addr.s_addr = htonl(source.toIPv4Address());
    MIB_IPFORWARD_ROW2 route {};
    if (GetBestRoute2(nullptr, 0, &local, &destination, 0, &route, &selected) != NO_ERROR
        || route.InterfaceLuid.Value != luid.Value) { return finishUnsupported(); }
    QUdpSocket socket;
    socket.setProxy(QNetworkProxy::NoProxy);
    if (!socket.bind(source, 0)) { return finishUnsupported(); }
    const quint16 queryId = static_cast<quint16>(QRandomGenerator::system()->generate());
    auto packet = QByteArray::fromHex("0000010000010000000000000a686f702d6865616c7468076578616d706c6503636f6d0000010001");
    packet[0] = static_cast<char>(queryId >> 8); packet[1] = static_cast<char>(queryId & 0xff);
    QElapsedTimer elapsed; elapsed.start();
    bool done = false;
    int replyCount = 0, lastRcode = -1;
    bool expectedSender = false, expectedId = false;
    QTimer retry;
    retry.setInterval(1000);
    int attempts = 0;
    const auto finish = [&](bool ok) {
        if (done) { return; } done = true; retry.stop();
        QTextStream(stdout) << QJsonDocument(QJsonObject { { QStringLiteral("supported"), true }, { QStringLiteral("ok"), ok },
            { QStringLiteral("latencyMs"), ok ? int(elapsed.elapsed()) : -1 },
            { QStringLiteral("attempts"), attempts }, { QStringLiteral("replyCount"), replyCount }, { QStringLiteral("rcode"), lastRcode },
            { QStringLiteral("expectedSender"), expectedSender }, { QStringLiteral("expectedId"), expectedId } }).toJson(QJsonDocument::Compact) << Qt::endl;
        app.exit(ok ? 0 : 3);
    };
    QObject::connect(&socket, &QUdpSocket::readyRead, &app, [&]() {
        while (socket.hasPendingDatagrams()) {
            QByteArray answer(4096, Qt::Uninitialized);
            QHostAddress sender; quint16 port = 0;
            const auto size = socket.readDatagram(answer.data(), answer.size(), &sender, &port);
            ++replyCount;
            expectedSender = sender == resolver && port == 53;
            expectedId = answer.left(2) == packet.left(2);
            if (size >= 12) { lastRcode = static_cast<unsigned char>(answer.at(3)) & 0x0f; }
            if (size < packet.size() || sender != resolver || port != 53 || answer.left(2) != packet.left(2)
                || !(static_cast<unsigned char>(answer.at(2)) & 0x80)
                || answer.mid(4, 2) != packet.mid(4, 2) || answer.mid(12, packet.size() - 12) != packet.mid(12)
                || ((static_cast<unsigned char>(answer.at(3)) & 0x0f) != 0 && (static_cast<unsigned char>(answer.at(3)) & 0x0f) != 3)) { continue; }
            finish(true); return;
        }
    });
    const auto send = [&]() {
        ++attempts;
        if (socket.writeDatagram(packet, resolver, 53) != packet.size()) { finish(false); }
        if (attempts >= 3) { retry.stop(); }
    };
    // Повторяем тот же запрос: потеря первого UDP-пакета не означает отказ локации.
    QObject::connect(&retry, &QTimer::timeout, &app, send);
    QTimer::singleShot(8000, &app, [&]() { finish(false); });
    QTimer::singleShot(0, &app, [&]() { retry.start(); send(); });
    return app.exec();
}

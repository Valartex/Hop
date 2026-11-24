#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTextStream>
#include <QTimer>
#include <amnezia_xray.h>

// Один процесс проверяет один VLESS-выход. Конфигурация передаётся только через stdin.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) { return 2; }
    if (app.arguments().contains(QStringLiteral("--validate-domain-patterns"))) {
        // Конструктор маршрутизатора использует тот же RE2/Go regexp, что и рабочее ядро.
        // Без запуска ядра, входов и сетевых запросов; секретов в аргументах нет.
        const auto bytes = input.read(32 * 1024 * 1024 + 1);
        const auto document = QJsonDocument::fromJson(bytes);
        if (bytes.size() > 32 * 1024 * 1024 || !document.isArray() || document.array().isEmpty()) { return 2; }
        for (const auto &value : document.array()) {
            if (!value.isString() || !value.toString().startsWith(QStringLiteral("regexp:"))
                || value.toString().size() > 4096 || value.toString().contains(QChar::Null)) { return 2; }
        }
        const QJsonObject config {
            { "log", QJsonObject { { "loglevel", "none" } } },
            { "outbounds", QJsonArray { QJsonObject { { "tag", "check" }, { "protocol", "blackhole" } } } },
            { "routing", QJsonObject { { "rules", QJsonArray { QJsonObject {
                { "type", "field" }, { "domain", document.array() }, { "outboundTag", "check" }
            } } } } }
        };
        auto serialized = QJsonDocument(config).toJson(QJsonDocument::Compact);
        amnezia_xray_setloghandler([](char *, void *) {}, nullptr);
        auto *failure = amnezia_xray_configure(serialized.data());
        const bool valid = !failure;
        if (failure) { amnezia_xray_free(failure); }
        QTextStream(stdout) << QJsonDocument(QJsonObject { { "valid", valid } }).toJson(QJsonDocument::Compact) << Qt::endl;
        return 0;
    }
    const auto bytes = input.readLine(65538);
    const auto supplied = QJsonDocument::fromJson(bytes).object();
    const auto outbounds = supplied.value(QStringLiteral("outbounds")).toArray();
    if (bytes.size() > 65536 || outbounds.isEmpty()
        || outbounds.first().toObject().value(QStringLiteral("protocol")).toString() != QStringLiteral("vless")) { return 2; }
    QTcpServer reservation;
    if (!reservation.listen(QHostAddress::LocalHost, 0)) { return 2; }
    const auto port = reservation.serverPort();
    reservation.close();
    const auto randomSecret = []() {
        QByteArray secret;
        for (int i = 0; i < 4; ++i) { secret.append(QByteArray::number(QRandomGenerator::system()->generate64(), 16)); }
        return QString::fromLatin1(secret);
    };
    const auto user = randomSecret(), password = randomSecret();
    auto outbound = outbounds.first().toObject();
    outbound.insert(QStringLiteral("tag"), QStringLiteral("probe"));
    const QJsonObject config {
        { QStringLiteral("outbounds"), QJsonArray { outbound } },
        { QStringLiteral("inbounds"), QJsonArray { QJsonObject {
            { QStringLiteral("listen"), QStringLiteral("127.0.0.1") }, { QStringLiteral("port"), port },
            { QStringLiteral("protocol"), QStringLiteral("socks") },
            { QStringLiteral("settings"), QJsonObject { { QStringLiteral("auth"), QStringLiteral("password") },
                { QStringLiteral("accounts"), QJsonArray { QJsonObject { { QStringLiteral("user"), user }, { QStringLiteral("pass"), password } } } },
                { QStringLiteral("udp"), false } } } } } },
        { QStringLiteral("log"), QJsonObject { { QStringLiteral("loglevel"), QStringLiteral("none") } } }
    };
    QNetworkAccessManager network;
    QTimer deadline;
    deadline.setSingleShot(true);
    bool running = false, finished = false;
    const auto cleanup = qScopeGuard([&]() {
        if (running) { if (auto *error = amnezia_xray_stop()) { amnezia_xray_free(error); } }
    });
    const auto finish = [&](bool ok, qint64 milliseconds) {
        if (finished) { return; }
        finished = true;
        QTextStream(stdout) << QJsonDocument(QJsonObject { { QStringLiteral("ok"), ok },
            { QStringLiteral("latencyMs"), ok ? milliseconds : -1 } }).toJson(QJsonDocument::Compact) << Qt::endl;
        app.exit(ok ? 0 : 3);
    };
    QObject::connect(&deadline, &QTimer::timeout, &app, [&]() { finish(false, -1); });
    QTimer::singleShot(0, &app, [&]() {
        amnezia_xray_setloghandler([](char *, void *) {}, nullptr);
        auto serialized = QJsonDocument(config).toJson(QJsonDocument::Compact);
        if (auto *error = amnezia_xray_configure(serialized.data())) { amnezia_xray_free(error); finish(false, -1); return; }
        if (auto *error = amnezia_xray_start()) { amnezia_xray_free(error); finish(false, -1); return; }
        running = true;
        network.setProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy, QStringLiteral("127.0.0.1"), port, user, password));
        QNetworkRequest request(QUrl(QStringLiteral("https://www.gstatic.com/generate_204")));
        request.setTransferTimeout(10000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        QElapsedTimer elapsed;
        elapsed.start();
        auto *reply = network.get(request);
        QObject::connect(reply, &QNetworkReply::finished, &app, [&, reply, elapsed]() {
            const bool ok = reply->error() == QNetworkReply::NoError
                && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 204;
            reply->deleteLater();
            finish(ok, elapsed.elapsed());
        });
    });
    deadline.start(12000);
    return app.exec();
}

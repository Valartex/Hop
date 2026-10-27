#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QProcess>
#include <QQueue>
#include <QHash>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTextStream>
#include <QTimer>
#include <QUrlQuery>
#include <amnezia_xray.h>
#include <algorithm>
#include <functional>

#include "core/subscriptions/subscriptionFetcher.h"
#include "core/subscriptions/subscriptionParser.h"
#include "core/utils/serialization/serialization.h"

namespace {
class EndpointBatch : public QObject {
public:
    EndpointBatch(QVector<QByteArray> configs, std::function<void(const QString &, int)> finish, QObject *parent)
        : QObject(parent), m_total(configs.size()), m_finish(std::move(finish))
    {
        for (auto &config : configs) { m_queue.enqueue(std::move(config)); }
        next();
    }
private:
    void next()
    {
        while (m_active < 2 && !m_queue.isEmpty()) {
            const auto config = m_queue.dequeue();
            auto *process = new QProcess(this);
            ++m_active;
            process->setProgram(QCoreApplication::applicationDirPath() + QStringLiteral("/hop_endpoint_probe.exe"));
            connect(process, &QProcess::started, this, [process, config]() { process->write(config + '\n'); process->closeWriteChannel(); });
            connect(process, &QProcess::readyReadStandardError, this, [process]() { process->readAllStandardError(); });
            connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) { completed(process, code, status); });
            connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) { completed(process, -1, QProcess::CrashExit); }
            });
            QTimer::singleShot(15000, process, [process]() { if (process->state() != QProcess::NotRunning) { process->kill(); } });
            process->start();
        }
        if (m_active == 0 && m_queue.isEmpty()) {
            std::sort(m_latencies.begin(), m_latencies.end());
            const int median = m_latencies.isEmpty() ? -1 : m_latencies.at(m_latencies.size() / 2);
            m_finish(QStringLiteral("Endpoint HTTPS batch complete: %1 tested, %2 reachable, %3 no response, median %4 ms")
                         .arg(m_total).arg(m_latencies.size()).arg(m_total - m_latencies.size()).arg(median),
                     m_latencies.isEmpty() ? 11 : 0);
        }
    }
    void completed(QProcess *process, int code, QProcess::ExitStatus status)
    {
        disconnect(process, nullptr, this, nullptr);
        const auto response = QJsonDocument::fromJson(process->readAllStandardOutput()).object();
        if (code == 0 && status == QProcess::NormalExit && response.value(QStringLiteral("ok")).toBool()) {
            m_latencies.append(response.value(QStringLiteral("latencyMs")).toInt());
        }
        process->deleteLater();
        --m_active;
        next();
    }
    qsizetype m_total;
    int m_active = 0;
    QQueue<QByteArray> m_queue;
    QVector<int> m_latencies;
    std::function<void(const QString &, int)> m_finish;
};
}

// Проверка транспорта не создаёт TUN, системных маршрутов или служб.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) { return 2; }
    const QUrl subscriptionUrl(QString::fromUtf8(input.readLine(16385)).trimmed(), QUrl::StrictMode);
    QTextStream output(stdout);
    hop::SubscriptionFetcher fetcher;
    QNetworkAccessManager network;
    QTimer deadline;
    deadline.setSingleShot(true);
    bool running = false;
    const auto shutdown = qScopeGuard([&running]() {
        if (running) {
            if (auto *error = amnezia_xray_stop()) { amnezia_xray_free(error); }
        }
    });
    const auto finish = [&app, &output](const QString &message, int code) {
        output << message << Qt::endl;
        app.exit(code);
    };
    QObject::connect(&deadline, &QTimer::timeout, &app, [&]() { finish(QStringLiteral("Transport deadline exceeded"), 3); });
    QObject::connect(&fetcher, &hop::SubscriptionFetcher::failed, &app, [&](const QString &, int) {
        finish(QStringLiteral("Subscription fetch failed"), 4);
    });
    QObject::connect(&fetcher, &hop::SubscriptionFetcher::loaded, &app, [&](const QByteArray &body, const QByteArray &,
                                                                         const QByteArray &, bool) {
        const auto parsed = hop::parseSubscription(body);
        if (!parsed.isValid()) { finish(QStringLiteral("Subscription validation failed"), 5); return; }
        if (app.arguments().contains(QStringLiteral("--health-all"))) {
            if (parsed.entries.size() > 128) { finish(QStringLiteral("Batch probe limit is 128 endpoints"), 14); return; }
            QVector<QByteArray> configs;
            for (const auto &entry : parsed.entries) {
                QString alias, error;
                const auto config = amnezia::serialization::vless::Deserialize(entry.uri, &alias, &error);
                if (config.isEmpty() || !error.isEmpty()) { finish(QStringLiteral("Batch VLESS conversion failed"), 7); return; }
                configs.append(QJsonDocument(config).toJson(QJsonDocument::Compact));
            }
            deadline.start(10 * 60 * 1000);
            new EndpointBatch(std::move(configs), finish, &app);
            return;
        }
        QString uri;
        for (const auto &entry : parsed.entries) {
            if (QUrlQuery(QUrl(entry.uri)).queryItemValue(QStringLiteral("security")) == QStringLiteral("reality")) {
                uri = entry.uri;
                break;
            }
        }
        if (uri.isEmpty()) { finish(QStringLiteral("No Reality endpoint"), 6); return; }
        QString alias, parseError;
        auto config = amnezia::serialization::vless::Deserialize(uri, &alias, &parseError);
        if (config.isEmpty() || !parseError.isEmpty()) { finish(QStringLiteral("VLESS conversion failed"), 7); return; }
        if (app.arguments().contains(QStringLiteral("--health-helper"))) {
            auto *helper = new QProcess(&app);
            helper->setProgram(QCoreApplication::applicationDirPath() + QStringLiteral("/hop_endpoint_probe.exe"));
            QObject::connect(helper, &QProcess::started, &app, [helper, config]() {
                helper->write(QJsonDocument(config).toJson(QJsonDocument::Compact) + '\n');
                helper->closeWriteChannel();
            });
            QObject::connect(helper, &QProcess::finished, &app, [&, helper](int code, QProcess::ExitStatus status) {
                const auto response = QJsonDocument::fromJson(helper->readAllStandardOutput()).object();
                const bool ok = code == 0 && status == QProcess::NormalExit && response.value(QStringLiteral("ok")).toBool();
                finish(ok ? QStringLiteral("Authenticated endpoint helper HTTPS OK: %1 ms").arg(response.value(QStringLiteral("latencyMs")).toInt())
                          : QStringLiteral("Endpoint helper HTTPS failed"), ok ? 0 : 12);
            });
            QObject::connect(helper, &QProcess::errorOccurred, &app, [&](QProcess::ProcessError) { finish(QStringLiteral("Endpoint helper process failed"), 13); });
            helper->start();
            return;
        }
        QTcpServer reservation;
        if (!reservation.listen(QHostAddress::LocalHost, 0)) { finish(QStringLiteral("Local port unavailable"), 8); return; }
        const auto port = reservation.serverPort();
        reservation.close();
        config.insert(QStringLiteral("inbounds"), QJsonArray { QJsonObject {
            { QStringLiteral("listen"), QStringLiteral("127.0.0.1") },
            { QStringLiteral("port"), port },
            { QStringLiteral("protocol"), QStringLiteral("socks") },
            { QStringLiteral("settings"), QJsonObject { { QStringLiteral("auth"), QStringLiteral("noauth") },
                                                        { QStringLiteral("udp"), false } } }
        } });
        config.insert(QStringLiteral("log"), QJsonObject { { QStringLiteral("loglevel"), QStringLiteral("none") } });
        amnezia_xray_setloghandler([](char *, void *) {}, nullptr);
        auto bytes = QJsonDocument(config).toJson(QJsonDocument::Compact);
        if (auto *error = amnezia_xray_configure(bytes.data())) {
            amnezia_xray_free(error);
            finish(QStringLiteral("Xray configuration failed"), 9);
            return;
        }
        if (auto *error = amnezia_xray_start()) {
            amnezia_xray_free(error);
            finish(QStringLiteral("Xray start failed"), 10);
            return;
        }
        running = true;
        network.setProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy, QStringLiteral("127.0.0.1"), port));
        QNetworkRequest request(QUrl(QStringLiteral("https://www.gstatic.com/generate_204")));
        request.setTransferTimeout(12000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        auto *reply = network.get(request);
        QElapsedTimer elapsed;
        elapsed.start();
        QObject::connect(reply, &QNetworkReply::finished, &app, [&, reply, elapsed]() {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool success = reply->error() == QNetworkReply::NoError && status == 204;
            reply->deleteLater();
            finish(success ? QStringLiteral("VLESS Reality HTTPS OK: HTTP 204, %1 ms").arg(elapsed.elapsed())
                           : QStringLiteral("VLESS Reality HTTPS failed: HTTP %1, Qt %2").arg(status).arg(int(reply->error())),
                   success ? 0 : 11);
        });
    });
    if (!fetcher.fetch(subscriptionUrl)) { return 2; }
    deadline.start(40000);
    return app.exec();
}

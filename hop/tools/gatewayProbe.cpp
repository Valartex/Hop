#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTextStream>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <agw.h>

// Только чтение каталога или сведений подписки; конфигурации VPN этим инструментом не выдаются.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) { return 2; }
    const auto bytes = input.read(1024 * 1024 + 1);
    if (bytes.size() > 1024 * 1024) { return 2; }
    const auto request = QJsonDocument::fromJson(bytes).object();
    auto config = request.value(QStringLiteral("gateway")).toObject();
    const auto payload = request.value(QStringLiteral("payload")).toObject();
    const auto endpoint = request.value(QStringLiteral("endpoint")).toString(QStringLiteral("v1/services"));
    if (endpoint != QStringLiteral("v1/services") && endpoint != QStringLiteral("v1/account_info")) { return 2; }
    config.insert(QStringLiteral("request_timeout_msecs"), 5000);
    config.insert(QStringLiteral("proxy_storage_timeout_msecs"), 3000);
    const auto client = agw_client_create(QJsonDocument(config).toJson(QJsonDocument::Compact).constData(), nullptr);
    if (!client) { return 2; }
    const auto cancel = agw_cancel_create();
    const auto cleanup = qScopeGuard([&]() { agw_client_destroy(client); agw_cancel_destroy(cancel); });
    std::mutex mutex;
    std::condition_variable condition;
    bool done = false;
    std::thread timeout([&]() {
        std::unique_lock lock(mutex);
        if (!condition.wait_for(lock, std::chrono::seconds(40), [&]() { return done; })) { agw_cancel_cancel(cancel); }
    });
    const QJsonObject options {
        { QStringLiteral("service_type"), payload.value(QStringLiteral("service_type")) },
        { QStringLiteral("user_country_code"), payload.value(QStringLiteral("user_country_code")) }
    };
    auto result = agw_post(client, endpoint.toUtf8().constData(), QJsonDocument(payload).toJson(QJsonDocument::Compact).constData(),
                           QJsonDocument(options).toJson(QJsonDocument::Compact).constData(), cancel);
    { const std::lock_guard lock(mutex); done = true; }
    condition.notify_one();
    timeout.join();
    const auto releaseResult = qScopeGuard([&]() { agw_result_free(&result); });
    const auto response = result.body ? QJsonDocument::fromJson(QByteArray(result.body, static_cast<qsizetype>(result.body_len))).object() : QJsonObject();
    QTextStream output(stdout);
    output << "Gateway code: " << result.code << "; JSON object: " << (!response.isEmpty() ? "yes" : "no")
           << "; services: " << response.value(QStringLiteral("services")).toArray().size()
           << "; countries: " << response.value(QStringLiteral("available_countries")).toArray().size()
           << "; active devices: " << response.value(QStringLiteral("active_device_count")).toInt(-1)
           << "; max devices: " << response.value(QStringLiteral("max_device_count")).toInt(-1)
           << "; issued configs: " << response.value(QStringLiteral("issued_configs")).toInt(-1) << Qt::endl;
    // Ни тело ответа, ни trace, ни ключи не выводятся и не сохраняются.
    return result.code == AGW_OK && !response.isEmpty() ? 0 : 3;
}

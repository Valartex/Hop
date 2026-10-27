#include "httpSubscriptionsController.h"
#include "subscriptionParser.h"
#include "subscriptionNaming.h"

#include <QJsonObject>
#include <QSet>
#include <QUuid>
#include <QRandomGenerator>
#include <algorithm>

#include "core/controllers/connectionController.h"
#include "core/controllers/selfhosted/importController.h"
#include "core/repositories/secureServersRepository.h"
#include "core/utils/constants/configKeys.h"
#include "core/utils/serverConfigUtils.h"

namespace hop {
namespace {
const QString metadataKey = QStringLiteral("hopSubscription");
}

HttpSubscriptionsController::HttpSubscriptionsController(SecureServersRepository *servers, ImportController *importer,
                                                         ConnectionController *connection, QObject *parent, const QString &helperPath)
    : QObject(parent), m_servers(servers), m_importer(importer), m_connection(connection), m_fetcher(nullptr, false, helperPath)
{
    m_connectionActive = m_connection->isConnected();
    connect(&m_fetcher, &SubscriptionFetcher::loaded, this, &HttpSubscriptionsController::consume);
    connect(&m_fetcher, &SubscriptionFetcher::failed, this, &HttpSubscriptionsController::fail);
    connect(m_servers, &SecureServersRepository::serversBatchChanged, this, &HttpSubscriptionsController::sourcesChanged);
    connect(m_servers, &SecureServersRepository::serverRemoved, this, &HttpSubscriptionsController::sourcesChanged);
    connect(m_connection, &ConnectionController::connectionStateChanged, this, [this](Vpn::ConnectionState state) {
        m_connectionActive = state != Vpn::ConnectionState::Disconnected;
        if (state == Vpn::ConnectionState::Disconnected) {
            QTimer::singleShot(0, this, &HttpSubscriptionsController::removeRetired);
        }
    });
    m_refreshTimer.setInterval(60000);
    m_refreshTimer.setObjectName(QStringLiteral("hopSubscriptionRefreshTimer"));
    m_refreshTimer.setParent(this);
    connect(&m_refreshTimer, &QTimer::timeout, this, &HttpSubscriptionsController::refreshDue);
    m_refreshTimer.start();
    QTimer::singleShot(1500, this, [this]() { removeRetired(); refreshDue(); });
}

bool HttpSubscriptionsController::removeSource(const QString &id)
{
    if (isBusy() || m_connectionActive) { return false; }
    const auto existing = sources();
    if (std::none_of(existing.cbegin(), existing.cend(), [&](const Source &source) { return source.id == id; })) { return false; }
    if (!m_servers->applyNativeSubscriptionSnapshot(id, {})) { return false; }
    m_nextAttempt.remove(id); m_failures.remove(id); m_sourceStatuses.remove(id); emit stateChanged(); return true;
}

HttpSubscriptionsController::~HttpSubscriptionsController()
{
    disconnect(&m_fetcher, nullptr, this, nullptr);
}

bool HttpSubscriptionsController::isBusy() const
{
    return m_fetcher.isBusy() || !m_queue.isEmpty() || !m_current.id.isEmpty();
}

QVector<HttpSubscriptionsController::Source> HttpSubscriptionsController::sources() const
{
    QVector<Source> result;
    QHash<QString, qsizetype> indices;
    for (const auto &id : m_servers->orderedServerIds()) {
        const auto server = m_servers->nativeConfig(id);
        if (!server || server->subscription.isEmpty()) { continue; }
        const auto &metadata = server->subscription;
        Source source;
        source.id = metadata.value(QStringLiteral("sourceId")).toString();
        source.url = QUrl(metadata.value(QStringLiteral("sourceUrl")).toString(), QUrl::StrictMode);
        source.name = subscriptionName(metadata);
        source.customName = hasCustomSubscriptionName(metadata);
        source.etag = metadata.value(QStringLiteral("etag")).toString().toLatin1();
        source.lastModified = metadata.value(QStringLiteral("lastModified")).toString().toLatin1();
        source.updatedAt = QDateTime::fromString(metadata.value(QStringLiteral("updatedAt")).toString(), Qt::ISODate);
        if (source.id.isEmpty() || source.url.scheme() != QStringLiteral("https")) { continue; }
        if (!indices.contains(source.id)) {
            indices.insert(source.id, result.size());
            result.append(source);
        } else if (source.updatedAt > result.at(indices.value(source.id)).updatedAt) {
            result[indices.value(source.id)] = source;
        }
    }
    return result;
}

int HttpSubscriptionsController::sourceCount() const
{
    return static_cast<int>(sources().size());
}

bool HttpSubscriptionsController::importSubscription(const QString &text, const QString &name)
{
    const QUrl url(text.trimmed(), QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != QStringLiteral("https") || url.host().isEmpty()
        || !url.userInfo().isEmpty() || url.hasFragment() || text.size() > 16384) {
        emit errorOccurred(tr("Укажите корректную HTTPS-ссылку подписки"));
        return false;
    }
    const auto knownSources = sources();
    Source source;
    for (const auto &known : knownSources) {
        if (known.url == url) { source = known; break; }
    }
    if (source.id.isEmpty()) {
        if (knownSources.size() >= 64) {
            emit errorOccurred(tr("Достигнут предел числа подписок"));
            return false;
        }
        source.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        source.url = url;
        source.customName = !name.trimmed().isEmpty();
        source.name = subscriptionName(url, name.trimmed().left(120));
    } else if (!name.trimmed().isEmpty()) {
        source.name = name.trimmed().left(120);
        source.customName = true;
    }
    source.userInitiated = true;
    if (!enqueue(source)) {
        emit errorOccurred(tr("Очередь обновления подписок заполнена"));
        return false;
    }
    return true;
}

bool HttpSubscriptionsController::enqueue(const Source &source)
{
    const auto merge = [](Source &pending, const Source &incoming) {
        // Фоновое обновление не отменяет имя, только что введённое пользователем.
        if (incoming.userInitiated || !pending.userInitiated) {
            pending.name = incoming.name;
            pending.customName = incoming.customName;
        }
        pending.userInitiated = pending.userInitiated || incoming.userInitiated;
    };
    if (m_current.id == source.id || m_current.url == source.url) {
        merge(m_current, source);
        return true;
    }
    for (auto &queued : m_queue) {
        if (queued.id == source.id || queued.url == source.url) {
            merge(queued, source);
            return true;
        }
    }
    if (m_queue.size() >= 64) { return false; }
    m_queue.enqueue(source);
    m_sourceStatuses[source.id] = QVariantMap{{"state", "loading"}, {"message", tr("Ожидание обновления…")}};
    emit stateChanged();
    QTimer::singleShot(0, this, &HttpSubscriptionsController::processNext);
    return true;
}

void HttpSubscriptionsController::processNext()
{
    if (m_fetcher.isBusy() || !m_current.id.isEmpty() || m_queue.isEmpty()) { return; }
    m_current = m_queue.dequeue();
    m_sourceStatuses[m_current.id] = QVariantMap{{"state", "loading"}, {"message", tr("Обновление подписки…")}};
    m_lastError.clear();
    if (!m_fetcher.fetch(m_current.url, m_current.etag, m_current.lastModified)) {
        fail(tr("Не удалось начать загрузку подписки"));
    }
    emit stateChanged();
}

void HttpSubscriptionsController::refreshAll()
{
    for (auto source : sources()) {
        source.userInitiated = true;
        enqueue(source);
    }
}

bool HttpSubscriptionsController::refreshSource(const QString &id, bool userInitiated)
{
    for (auto source : sources()) {
        if (source.id != id) { continue; }
        source.userInitiated = userInitiated;
        return enqueue(source);
    }
    return false;
}

void HttpSubscriptionsController::refreshDue()
{
    if (m_refreshMinutes <= 0) { return; }
    const auto now = QDateTime::currentDateTimeUtc();
    for (const auto &source : sources()) {
        if ((!source.updatedAt.isValid() || source.updatedAt.secsTo(now) >= m_refreshMinutes * 60)
            && m_nextAttempt.value(source.id) <= now) {
            enqueue(source);
        }
    }
}

void HttpSubscriptionsController::fail(const QString &message, int retryAfterSeconds)
{
    m_lastError = message;
    const auto sourceId = m_current.id;
    m_sourceStatuses[sourceId] = QVariantMap{{"state", "error"}, {"message", message}};
    const int failures = qMin(m_failures.value(m_current.id) + 1, 8);
    m_failures[m_current.id] = failures;
    const int delay = qMax(retryAfterSeconds, qMin(3600, 30 * (1 << (failures - 1))))
        + static_cast<int>(QRandomGenerator::global()->bounded(15u));
    m_nextAttempt[m_current.id] = QDateTime::currentDateTimeUtc().addSecs(delay);
    // При фоновом отказе не прерываем работу пользователя всплывающим окном.
    if (m_current.userInitiated) { emit errorOccurred(message); }
    m_current = {};
    emit stateChanged();
    emit sourceRefreshFinished(sourceId, false);
    QTimer::singleShot(0, this, &HttpSubscriptionsController::processNext);
}

void HttpSubscriptionsController::consume(const QByteArray &body, const QByteArray &etag,
                                         const QByteArray &lastModified, bool notModified)
{
    if (!m_current.customName && !m_fetcher.profileTitle().isEmpty()) { m_current.name = m_fetcher.profileTitle(); }
    QVector<PreviousSubscriptionEntry> previous;
    QHash<QString, amnezia::NativeServerConfig> previousServers;
    for (const auto &id : m_servers->orderedServerIds()) {
        const auto server = m_servers->nativeConfig(id);
        if (!server || server->subscription.value(QStringLiteral("sourceId")).toString() != m_current.id) { continue; }
        previousServers.insert(id, *server);
        if (!server->subscription.value(QStringLiteral("retired")).toBool()) {
            previous.append({ id, server->subscription.value(QStringLiteral("connectionKey")).toString(),
                              server->subscription.value(QStringLiteral("nameKey")).toString() });
        }
    }
    QVector<QPair<QString, QJsonObject>> snapshot;
    const auto now = QDateTime::currentDateTimeUtc();
    if (notModified) {
        for (const auto &id : m_servers->orderedServerIds()) {
            if (!previousServers.contains(id)) { continue; }
            auto server = previousServers.value(id);
            const QString oldDescription = server.subscription.value(QStringLiteral("sourceName")).toString()
                + QStringLiteral(" · ") + server.subscription.value(QStringLiteral("entryName")).toString();
            if (server.description == oldDescription) {
                server.description = m_current.name + QStringLiteral(" · ")
                    + server.subscription.value(QStringLiteral("entryName")).toString();
            }
            server.subscription.insert(QStringLiteral("sourceName"), m_current.name);
            server.subscription.insert(QStringLiteral("sourceNameMode"), m_current.customName ? QStringLiteral("custom") : QStringLiteral("automatic"));
            if (!etag.isEmpty()) { server.subscription.insert(QStringLiteral("etag"), QString::fromLatin1(etag)); }
            if (!lastModified.isEmpty()) {
                server.subscription.insert(QStringLiteral("lastModified"), QString::fromLatin1(lastModified));
            }
            server.subscription.insert(QStringLiteral("updatedAt"), now.toString(Qt::ISODate));
            snapshot.append({ id, server.toJson() });
        }
        if (snapshot.isEmpty()) { fail(tr("Сервер вернул 304 без сохранённой версии подписки")); return; }
    } else {
        const auto parsed = parseSubscription(body);
        if (!parsed.isValid()) { fail(parsed.error); return; }
        const auto matched = matchSubscriptionEntries(parsed.entries, previous);
        if (!matched.error.isEmpty()) { fail(matched.error); return; }
        for (qsizetype index = 0; index < parsed.entries.size(); ++index) {
            const auto &entry = parsed.entries.at(index);
            const auto imported = m_importer->extractConfigFromData(entry.uri);
            if (imported.errorCode != amnezia::ErrorCode::NoError
                || serverConfigUtils::configTypeFromJson(imported.config) != serverConfigUtils::ConfigType::Native) {
                fail(tr("Не удалось преобразовать запись подписки %1").arg(index + 1));
                return;
            }
            auto config = imported.config;
            const QString id = matched.serverIds.at(index);
            QString description = m_current.name + QStringLiteral(" · ") + entry.name;
            if (previousServers.contains(id)) {
                const auto &old = previousServers.value(id);
                const auto oldDescription = old.subscription.value(QStringLiteral("sourceName")).toString()
                    + QStringLiteral(" · ") + old.subscription.value(QStringLiteral("entryName")).toString();
                if (old.description != oldDescription) { description = old.description; }
            }
            config.insert(QString(amnezia::configKey::description), description);
            QJsonObject metadata;
            metadata.insert(QStringLiteral("sourceId"), m_current.id);
            metadata.insert(QStringLiteral("sourceUrl"), m_current.url.toString(QUrl::FullyEncoded));
            metadata.insert(QStringLiteral("sourceName"), m_current.name);
            metadata.insert(QStringLiteral("sourceNameMode"), m_current.customName ? QStringLiteral("custom") : QStringLiteral("automatic"));
            metadata.insert(QStringLiteral("entryName"), entry.name);
            metadata.insert(QStringLiteral("connectionKey"), entry.connectionKey);
            metadata.insert(QStringLiteral("nameKey"), entry.nameKey);
            metadata.insert(QStringLiteral("etag"), QString::fromLatin1(etag));
            metadata.insert(QStringLiteral("lastModified"), QString::fromLatin1(lastModified));
            metadata.insert(QStringLiteral("updatedAt"), now.toString(Qt::ISODate));
            config.insert(metadataKey, metadata);
            snapshot.append({ id, config });
        }
    }
    // Работающий туннель сохраняется, даже если провайдер удалил его из подписки.
    const QString protectedId = m_connectionActive ? m_servers->defaultServerId() : QString();
    if (!m_servers->applyNativeSubscriptionSnapshot(m_current.id, snapshot, protectedId)) {
        fail(tr("Не удалось применить обновление подписки"));
        return;
    }
    const bool userInitiated = m_current.userInitiated;
    const QString name = m_current.name;
    const auto sourceId = m_current.id;
    m_sourceStatuses[sourceId] = QVariantMap{{"state", "ready"}, {"message", tr("Обновлено %1").arg(QDateTime::currentDateTime().toString("HH:mm"))},
        {"updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    m_nextAttempt.remove(m_current.id);
    m_failures.remove(m_current.id);
    m_current = {};
    emit stateChanged();
    emit sourcesChanged();
    emit sourceRefreshFinished(sourceId, true);
    if (userInitiated) {
        emit finished(tr("Подписка %1 обновлена: %2 записей").arg(name, QString::number(snapshot.size())));
        emit subscriptionImported();
    }
    QTimer::singleShot(0, this, &HttpSubscriptionsController::processNext);
}

void HttpSubscriptionsController::removeRetired()
{
    if (m_connectionActive) { return; }
    for (const auto &source : sources()) {
        QVector<QPair<QString, QJsonObject>> snapshot;
        bool hasRetired = false;
        for (const auto &id : m_servers->orderedServerIds()) {
            const auto server = m_servers->nativeConfig(id);
            if (!server || server->subscription.value(QStringLiteral("sourceId")).toString() != source.id) { continue; }
            if (server->subscription.value(QStringLiteral("retired")).toBool()) {
                hasRetired = true;
            } else {
                snapshot.append({ id, server->toJson() });
            }
        }
        if (hasRetired) { m_servers->applyNativeSubscriptionSnapshot(source.id, snapshot); }
    }
}
} // namespace hop

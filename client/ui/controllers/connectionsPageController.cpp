#include "connectionsPageController.h"
#include "core/controllers/connectionController.h"
#include "core/locations/endpointHealthController.h"
#include "core/locations/locationCoordinator.h"
#include "core/subscriptions/httpSubscriptionsController.h"
#include <utility>

namespace hop {
ConnectionsPageController::ConnectionsPageController(SecureServersRepository *servers, HttpSubscriptionsController *subscriptions,
    LocationCatalog *catalog, LocationCoordinator *session, EndpointHealthController *health, ConnectionController *connection, QObject *parent)
    : QObject(parent), m_servers(servers), m_subscriptions(subscriptions), m_catalog(catalog), m_session(session), m_health(health), m_connection(connection)
{
    connect(catalog, &LocationCatalog::catalogChanged, this, &ConnectionsPageController::changed);
    connect(session, &SelectionSession::changed, this, [this]() {
        if (!m_session->running() && !m_candidateToken.isEmpty()) {
            m_candidateToken.clear(); m_health->cancel();
        }
        emit changed();
    });
    connect(session, &LocationCoordinator::statusesChanged, this, &ConnectionsPageController::changed);
    connect(session, &LocationCoordinator::catalogRefreshChanged, this, &ConnectionsPageController::changed);
    connect(health, &EndpointHealthController::changed, this, [this]() {
        // Начавшийся отдельный замер заменяет старое наблюдение отключённого туннеля.
        const auto statuses = m_health->statuses();
        for (auto it = statuses.cbegin(); it != statuses.cend(); ++it) {
            if (it.value().toMap().value("state") == "checking") { m_session->forgetInactiveHealth(it.key()); }
        }
        emit changed();
        if (!m_candidateToken.isEmpty() && !m_health->isBusy()) {
            const auto token = std::exchange(m_candidateToken, {});
            m_session->candidatesMeasured(token, healthStatuses());
        }
    });
    connect(session, &SelectionSession::candidateChecksRequested, this, [this](const QStringList &ids, const QString &token) {
        // После остановки туннеля Kill Switch сохраняется: отдельные сетевые замеры
        // не должны превращать заблокированный исходящий трафик в «отказ сервера».
        if (m_catalog->mode() == LocationCatalog::AllOrdered || !m_session->activeId().isEmpty()) {
            m_session->candidatesMeasured(token, healthStatuses()); return;
        }
        m_candidateToken = token;
        m_health->checkLocations(ids);
        QTimer::singleShot(0, this, [this, token]() {
            if (m_candidateToken == token && !m_health->isBusy()) {
                m_candidateToken.clear(); m_session->candidatesMeasured(token, healthStatuses());
            }
        });
        QTimer::singleShot(55000, this, [this, token]() {
            if (m_candidateToken == token) {
                m_candidateToken.clear();
                // Незавершённые пробы не должны продолжаться после включения защиты нового туннеля.
                m_health->cancel();
                m_session->candidatesMeasured(token, healthStatuses());
            }
        });
    });
    connect(subscriptions, &HttpSubscriptionsController::stateChanged, this, &ConnectionsPageController::changed);
    session->setRefreshBeforeFailover(true);
    connect(session, &SelectionSession::subscriptionRefreshRequested, this, [this](const QString &id, const QString &token) {
        const auto info = m_catalog->serviceInfo(id);
        m_recoveryToken = token;
        m_recoveryApi = info.value("kind") == "api";
        m_recoverySource = info.value(m_recoveryApi ? "serverId" : "id").toString();
        if (m_recoveryApi) { m_session->refreshCatalog(m_recoverySource); }
        else if (info.value("kind") != "http" || !m_subscriptions->refreshSource(m_recoverySource, false)) {
            m_session->subscriptionRefreshed(token, false, false);
        }
    });
    connect(subscriptions, &HttpSubscriptionsController::sourceRefreshFinished, this, [this](const QString &id, bool successful) {
        if (!m_recoveryApi && id == m_recoverySource) { m_session->subscriptionRefreshed(m_recoveryToken, successful); }
    });
    connect(session, &LocationCoordinator::catalogRefreshFinished, this, [this](const QString &id, bool successful) {
        if (m_recoveryApi && id == m_recoverySource) { m_session->subscriptionRefreshed(m_recoveryToken, successful); }
    });
    m_connectionActive = connection->isConnected();
    connect(connection, &ConnectionController::connectionStateChanged, this, [this](Vpn::ConnectionState state) {
        m_connectionActive = state != Vpn::Disconnected; emit changed();
    });
}
bool ConnectionsPageController::editingLocked() const
{
    return m_connectionActive || m_session->running()
        || m_session->refreshing() || m_subscriptions->isBusy();
}
QVariantMap ConnectionsPageController::healthStatuses() const
{
    auto result = m_health->statuses();
    const auto session = m_session->statuses();
    for (auto it = session.cbegin(); it != session.cend(); ++it) { result[it.key()] = it.value(); }
    return result;
}
QVariantMap ConnectionsPageController::serviceActions() const
{
    QVariantMap result;
    const auto services = m_catalog->services();
    for (auto it = services.cbegin(); it != services.cend(); ++it) {
        const auto kind = m_catalog->serviceInfo(it.key()).value("kind").toString();
        result[it.key()] = QVariantMap{{"canRefresh", !m_subscriptions->isBusy() && !m_session->refreshing() && (kind == "http" || kind == "api")},
                                      {"canRemove", !editingLocked()}};
    }
    return result;
}
QVariantMap ConnectionsPageController::serviceStatuses() const
{
    QVariantMap result;
    const auto http = m_subscriptions->sourceStatuses();
    const auto api = m_session->catalogStatuses();
    const auto services = m_catalog->services();
    for (auto it = services.cbegin(); it != services.cend(); ++it) {
        const auto info = m_catalog->serviceInfo(it.key());
        auto status = info.value("kind") == "api" ? api.value(info.value("serverId").toString()).toMap()
            : http.value(info.value("id").toString()).toMap();
        if (status.isEmpty() && !info.value("updatedAt").toString().isEmpty()) {
            const auto time = QDateTime::fromString(info.value("updatedAt").toString(), Qt::ISODate).toLocalTime();
            if (time.isValid()) { status = {{"state", "ready"}, {"message", tr("Обновлено %1").arg(time.toString("dd.MM HH:mm"))}}; }
        }
        result[it.key()] = status;
    }
    return result;
}
QVariantMap ConnectionsPageController::locationActions() const
{
    QVariantMap result;
    const auto statuses = m_health->statuses();
    for (const auto &entry : m_catalog->entries()) {
        const auto state = statuses.value(entry.id).toMap().value("state").toString();
        const bool current = m_session->running() && m_connection->isConnected() && m_session->activeId() == entry.id;
        const bool canCheck = current || (entry.enabled && m_health->canCheckLocation(entry.id) && state != "checking");
        const auto notice = current || canCheck ? QString() : entry.api && m_connection->isConnected()
            ? tr("При включённом VPN проверяется активная локация. Отдельный AWG-замер с тем же ключом может прервать соединение.")
            : entry.api ? tr("Для этой локации нет сохранённой конфигурации. Получение нового профиля может занять слот подписки.")
            : state == "deferred" ? tr("Проверка AmneziaWG доступна после отключения VPN, чтобы не прервать рабочее соединение.") : QString();
        result[entry.id] = QVariantMap{{"canCheck", canCheck}, {"notice", notice}};
    }
    return result;
}
bool ConnectionsPageController::fail(const QString &message) { m_error = message; emit errorChanged(); return false; }
bool ConnectionsPageController::checkLocation(const QString &id)
{
    m_error.clear(); emit errorChanged();
    const auto entry = m_catalog->entry(id);
    if (!entry) { return fail(tr("Локация больше не доступна.")); }
    const bool current = m_session->running() && m_connection->isConnected() && m_session->activeId() == id;
    const bool accepted = current ? m_session->checkCurrentLocation(id) : m_health->checkLocation(id);
    if (accepted) { return true; }
    const auto notice = locationActions().value(id).toMap().value("notice").toString();
    return fail(notice.isEmpty() ? tr("Для этой локации нет доступной проверки.") : notice);
}
void ConnectionsPageController::checkAllLocations()
{
    QStringList ids;
    for (const auto &entry : m_catalog->entries()) {
        if (m_session->running() && m_connection->isConnected() && m_session->activeId() == entry.id) {
            m_session->checkCurrentLocation(entry.id);
        } else if (entry.enabled && m_health->canCheckLocation(entry.id)) { ids.append(entry.id); }
    }
    m_health->checkLocations(ids);
}
void ConnectionsPageController::refreshAll() { m_subscriptions->refreshAll(); m_session->refreshCatalogs(); }
bool ConnectionsPageController::refreshService(const QString &id)
{
    if (!serviceActions().value(id).toMap().value("canRefresh").toBool()) { return fail(tr("Обновление подписки сейчас недоступно.")); }
    const auto info = m_catalog->serviceInfo(id);
    if (info.value("kind") == "http") { m_subscriptions->refreshSource(info.value("id").toString()); }
    else { m_session->refreshCatalog(info.value("serverId").toString()); }
    return true;
}
bool ConnectionsPageController::removeService(const QString &id)
{
    if (editingLocked()) { return fail(tr("Отключите VPN и дождитесь завершения обновления подписок перед удалением.")); }
    const auto info = m_catalog->serviceInfo(id);
    if (info.isEmpty()) { return fail(tr("Подписка больше не доступна.")); }
    const bool removed = info.value("kind") == "http" ? m_subscriptions->removeSource(info.value("id").toString())
        : m_servers->removeServersChecked({info.value("serverId").toString()});
    if (!removed) { return fail(tr("Не удалось сохранить изменения. Подписка не удалена.")); }
    m_error.clear(); emit errorChanged(); return true;
}
}

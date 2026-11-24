#include "selectionSession.h"
#include <algorithm>
#include <stdexcept>
#include <climits>
#include <QDebug>

namespace hop {
SelectionSession::SelectionSession(LocationCatalog *catalog, QObject *parent) : SelectionSession(catalog, Timing(), parent) {}
SelectionSession::SelectionSession(LocationCatalog *catalog, Timing timing, QObject *parent)
    : QObject(parent), m_catalog(catalog), m_timing(timing)
{
    if (!catalog || timing.healthMs < 1 || timing.retryMs < 1 || timing.attemptMs < 1 || timing.failures < 1 || timing.switchMs < 0 || timing.startupMs < 1) {
        throw std::invalid_argument("Invalid selection session timing");
    }
    m_healthTimer.setSingleShot(true);
    m_attemptTimer.setSingleShot(true);
    m_retryTimer.setSingleShot(true);
    m_startupTimer.setSingleShot(true);
    connect(&m_startupTimer, &QTimer::timeout, this, [this]() {
        if (m_running) {
            qWarning() << "Hop initial connection timed out";
            stopWithFailure(tr("Не удалось установить VPN-соединение за 3 минуты. Проверьте доступность локаций и журнал подключения"));
        }
    });
    connect(catalog, &LocationCatalog::policyChanged, this, [this]() {
        if (m_running && m_state == Healthy && m_catalog->automatic()
            && !m_catalog->candidateIds().contains(m_active)) { failed(tr("Локация исключена из автопула")); }
    });
    connect(&m_healthTimer, &QTimer::timeout, this, [this]() {
        if (m_running && (m_state == Healthy || m_state == Verifying)) { emit healthRequested(m_active, token()); }
    });
    connect(&m_attemptTimer, &QTimer::timeout, this, [this]() {
        if (m_state == Selecting) { candidatesMeasured(token(), {}); }
        else if (m_state == Refreshing) { subscriptionRefreshed(token(), false); }
        else if (m_running) { failed(tr("Истекло время подключения")); }
    });
    connect(&m_retryTimer, &QTimer::timeout, this, [this]() {
        if (!m_running) { return; }
        if (m_state == Suspending) { emit suspendRequested(token()); }
        else if (m_state == Backoff) { m_attempted.clear(); m_refreshedServices.clear(); beginNext(); }
    });
}
void SelectionSession::setState(State state, const QString &message)
{
    m_state = state; m_status = message;
    if (state == Healthy || state == Stopped) { m_startupTimer.stop(); }
    qInfo() << "Hop selection state" << int(state) << "running" << m_running << "attempt" << m_generation;
    emit changed();
}
void SelectionSession::start()
{
    if (m_running) { return; }
    if (!m_catalog->automatic() && m_catalog->restrictedMode() == 1
        && !m_catalog->selectedId().isEmpty() && !m_catalog->isRestrictedLocation(m_catalog->selectedId())) {
        setState(Stopped, tr("Отметьте выбранную локацию для обхода белых списков или выберите отмеченную."));
        return;
    }
    m_running = true;
    m_restrictedPhase = m_catalog->restrictedMode() == 1;
    m_active.clear();
    m_startupTimer.start(m_timing.startupMs);
    m_attempted.clear();
    m_refreshedServices.clear(); m_badLatency.invalidate();
    m_refreshDetachPending = false; m_refreshDetached = false;
    m_retryRound = 0;
    // Ручной выбор задаёт первую попытку в пуле, не меняя его порядок и исключения.
    m_preferred = m_catalog->mode() == LocationCatalog::Automatic ? m_catalog->selectedId() : QString();
    m_lastHealthy.clear();
    beginNext();
}
void SelectionSession::stop()
{
    m_running = false;
    m_refreshedServices.clear(); m_badLatency.invalidate();
    m_refreshDetachPending = false; m_refreshDetached = false;
    ++m_generation;
    m_healthTimer.stop(); m_attemptTimer.stop(); m_retryTimer.stop();
    m_attempted.clear();
    m_active.clear();
    m_preferred.clear();
    setState(Stopped, {});
    emit disconnectRequested();
}
QStringList SelectionSession::networkCandidates() const
{
    if (!m_catalog->automatic() && !m_active.isEmpty()) {
        // Ручные повторы сохраняют исходную локацию, даже если выбор в каталоге уже изменился.
        const auto target = m_catalog->entry(m_active);
        return target && target->enabled && (m_catalog->restrictedMode() == 0 || m_catalog->isRestrictedLocation(m_active) == m_restrictedPhase)
            ? QStringList { m_active } : QStringList {};
    }
    return m_catalog->networkCandidateIds(m_restrictedPhase);
}

void SelectionSession::beginNext()
{
    if (!m_running) { return; }
    m_candidates = networkCandidates();
    if (m_candidates.isEmpty() && !m_restrictedPhase && m_catalog->restrictedMode() == 2) {
        m_restrictedPhase = true;
        m_candidates = networkCandidates();
    }
    if (m_catalog->automatic() && m_candidates.isEmpty()) {
        stopWithFailure(m_catalog->allLocations()
            ? tr("Нет разрешённых локаций для автоматического подключения. Разрешите локацию или выберите ручной режим")
            : tr("В автопуле нет разрешённых локаций. Добавьте локацию через меню «…» или выберите ручной режим"));
        return;
    }
    if (!m_networkOnline) { setState(Backoff, tr("Ожидание сети")); m_retryTimer.start(m_timing.retryMs); return; }
    if (m_catalog->allLocations()) {
        ++m_generation;
        setState(Selecting, tr("Проверка локаций перед выбором…"));
        m_attemptTimer.start(60000);
        emit candidateChecksRequested(m_candidates, token());
        return;
    }
    openNext();
}
void SelectionSession::candidatesMeasured(const QString &value, const QVariantMap &statuses)
{
    if (!m_running || value != token() || m_state != Selecting) { return; }
    m_attemptTimer.stop();
    if (!m_networkOnline) { beginNext(); return; }
    const auto measured = [&statuses](const QString &id) {
        const auto status = statuses.value(id).toMap();
        return status.value("state") == "reachable" ? status.value("latencyMs", -1).toInt() : -1;
    };
    if (m_catalog->mode() == LocationCatalog::AllOrdered && !m_lastHealthy.isEmpty()) {
        const auto index = m_candidates.indexOf(m_lastHealthy);
        if (index >= 0) { std::rotate(m_candidates.begin(), m_candidates.begin() + index + 1, m_candidates.end()); }
    }
    m_candidates.erase(std::remove_if(m_candidates.begin(), m_candidates.end(), [&](const QString &id) {
        const auto state = statuses.value(id).toMap().value("state").toString();
        return state == "unreachable" || (measured(id) > m_latencyThresholdMs && m_latencyThresholdMs > 0);
    }), m_candidates.end());
    if (m_catalog->mode() == LocationCatalog::AllFastest) {
        std::stable_sort(m_candidates.begin(), m_candidates.end(), [&](const QString &left, const QString &right) {
            const auto first = measured(left), second = measured(right);
            return (first < 0 ? INT_MAX : first) < (second < 0 ? INT_MAX : second);
        });
    }
    openNext();
}
void SelectionSession::openNext()
{
    QString next;
    auto candidates = m_candidates;
    const auto current = m_catalog->candidateIds();
    candidates.removeIf([&](const QString &id) {
        const auto location = m_catalog->entry(id);
        return m_catalog->automatic() ? !current.contains(id) : !location || !location->enabled;
    });
    if (candidates.contains(m_preferred) && !m_attempted.contains(m_preferred)) { next = m_preferred; }
    m_preferred.clear();
    if (next.isEmpty()) { for (const auto &id : candidates) { if (!m_attempted.contains(id)) { next = id; break; } } }
    if (next.isEmpty()) {
        if (!m_restrictedPhase && m_catalog->restrictedMode() == 2) {
            m_restrictedPhase = true;
            const auto restricted = networkCandidates();
            m_restrictedPhase = false;
            if (!restricted.isEmpty()) {
                m_restrictedPhase = true; m_candidates = restricted; m_preferred.clear();
                beginNext(); return;
            }
        }
        const auto message = !m_catalog->automatic()
            ? tr("Выбранная локация недоступна. Ожидание повторного подключения")
            : m_catalog->allLocations() ? tr("Нет локаций с допустимым временем ответа. Ожидание повторной проверки")
            : (candidates.isEmpty() ? tr("Добавьте локации или страны в автопул") : tr("В пуле нет доступных локаций. Ожидание повторной проверки"));
        setState(Backoff, message);
        const int multiplier = 1 << m_retryRound;
        m_retryRound = std::min(m_retryRound + 1, 3u);
        m_retryTimer.start(std::min(m_timing.retryMs * static_cast<qint64>(multiplier), qint64(300000)));
        return;
    }
    m_active = next;
    m_attempted.insert(next);
    m_failures = 0;
    m_badLatency.invalidate();
    m_refreshDetachPending = false; m_refreshDetached = false;
    ++m_generation;
    m_attemptTimer.start(m_timing.attemptMs);
    setState(Preparing, tr("Подключение к %1").arg(m_catalog->entry(next)->name));
    emit attemptRequested(next, token());
}
void SelectionSession::setNetworkOnline(bool online)
{
    if (m_networkOnline == online) { return; }
    m_networkOnline = online;
    qInfo() << "Hop network online" << online << "selection state" << int(m_state);
    m_badLatency.invalidate();
    if (!m_running) { return; }
    if (!online && (m_state == Preparing || m_state == Verifying)) { m_attemptTimer.stop(); }
    if (online && m_state == Backoff) { m_retryTimer.stop(); beginNext(); }
    else if (online && (m_state == Preparing || m_state == Verifying)) { m_attemptTimer.start(m_timing.attemptMs); }
}
void SelectionSession::networkChanged()
{
    m_badLatency.invalidate(); m_attempted.clear(); m_lastHealthy.clear(); m_preferred.clear();
    if (m_catalog->restrictedMode() == 2) { m_restrictedPhase = false; }
    if (!m_running || m_state == Suspending) { return; }
    m_healthTimer.stop(); m_attemptTimer.stop(); m_retryTimer.stop();
    ++m_generation;
    setState(Suspending, tr("Изменилась сеть; повторный выбор подключения"));
    emit suspendRequested(token());
}

void SelectionSession::restartCurrent()
{
    if (!m_running || (m_state != Healthy && m_state != Verifying)) { return; }
    m_healthTimer.stop(); m_attemptTimer.stop();
    m_preferred = m_active;
    m_attempted.clear();
    ++m_generation;
    setState(Suspending, tr("Восстановление локации после изменения сети"));
    emit suspendRequested(token());
}
void SelectionSession::transportConnected(const QString &value)
{
    if (!m_running || value != token() || m_state != Preparing) { return; }
    if (m_catalog->automatic() && !m_catalog->candidateIds().contains(m_active)) {
        failed(tr("Локация исключена из автопула")); return;
    }
    setState(Verifying, tr("Проверка связи через туннель"));
    emit healthRequested(m_active, token());
}
void SelectionSession::transportStarting(const QString &value)
{
    if (m_running && value == token() && m_state == Preparing) { m_attemptTimer.start(qMin(30000, m_timing.attemptMs)); }
}
void SelectionSession::retarget(const QString &value, const QString &id)
{
    if (!m_running || value != token() || m_state != Preparing || !m_catalog->entry(id)) { return; }
    m_attempted.remove(m_active);
    m_attempted.insert(id);
    m_active = id;
    emit changed();
}
void SelectionSession::transportFailed(const QString &value, const QString &reason)
{
    if (!m_running || value != token() || m_state == Suspending || m_state == Backoff || m_state == Refreshing) { return; }
    failed(reason);
}
void SelectionSession::armHealth() { if (m_running) { m_healthTimer.start(m_timing.healthMs); } }
void SelectionSession::setLatencyPolicy(int durationMs, int thresholdMs)
{
    m_latencyDurationMs = qMax(0, durationMs); m_latencyThresholdMs = qMax(0, thresholdMs);
    m_badLatency.invalidate();
}
void SelectionSession::healthResult(const QString &value, bool healthy, bool online, bool supported, int latencyMs)
{
    if (!m_running || value != token() || (m_state != Verifying && m_state != Healthy)) { return; }
    if (!online) { m_failures = 0; m_badLatency.invalidate(); m_attemptTimer.stop(); setState(m_state, tr("Ожидание сети")); armHealth(); return; }
    if (!supported) {
        emit locationHealth(m_active, QStringLiteral("unsupported"));
        if (m_catalog->automatic()) { failed(tr("Для этой конфигурации нет проверяемого маршрута DNS через VPN")); }
        else { m_attemptTimer.stop(); setState(Healthy, tr("Автоматическая проверка этой конфигурации недоступна")); }
        return;
    }
    if (m_state == Verifying && m_catalog->allLocations() && (!healthy || latencyMs < 0 || latencyMs > m_latencyThresholdMs)) {
        m_skipRefresh = true;
        failed(tr("Время ответа локации не соответствует порогу"));
        m_skipRefresh = false;
        return;
    }
    // Созданный интерфейс AWG ещё не подтверждает обмен с удалённым сервером.
    // Начальную проверку ограничивает таймер; окно задержки относится к рабочему туннелю.
    if (m_state == Verifying && !healthy) {
        if (++m_failures >= m_timing.failures) { failed(tr("Сервер не отвечает через VPN. Проверьте доступность или конфигурацию локации")); }
        else { armHealth(); }
        return;
    }
    if (m_latencyDurationMs > 0 && m_latencyThresholdMs > 0 && (!healthy || latencyMs > m_latencyThresholdMs)) {
        if (!m_badLatency.isValid()) { m_badLatency.start(); }
        m_attemptTimer.stop();
        if (m_badLatency.elapsed() < m_latencyDurationMs) {
            setState(healthy ? Healthy : m_state, tr("Задержка выше порога; наблюдение перед переключением")); armHealth(); return;
        }
        failed(tr("Локация не отвечает или задержка длительно превышает порог"));
        return;
    }
    if (healthy) {
        if (m_catalog->automatic() && !m_catalog->candidateIds().contains(m_active)) {
            failed(tr("Локация исключена из автопула")); return;
        }
        m_failures = 0; m_retryRound = 0; m_attempted.clear(); m_attemptTimer.stop();
        m_badLatency.invalidate(); m_refreshedServices.clear();
        m_lastHealthy = m_active;
        emit locationHealth(m_active, QStringLiteral("reachable"));
        setState(Healthy, {});
        armHealth();
        return;
    }
    if (++m_failures < m_timing.failures) { armHealth(); return; }
    emit locationHealth(m_active, QStringLiteral("unreachable"));
    failed(tr("Локация перестала отвечать"));
}
void SelectionSession::failed(const QString &reason)
{
    m_attemptTimer.stop(); m_healthTimer.stop();
    const auto entry = m_catalog->entry(m_active);
    if (!m_skipRefresh && m_running && m_networkOnline && m_refreshBeforeFailover && m_catalog->automatic()
        && entry && !m_refreshedServices.contains(entry->serviceId)) {
        m_refreshedServices.insert(entry->serviceId);
        m_failureReason = reason;
        setState(Refreshing, tr("Проверка обновления подписки перед переключением…"));
        m_attemptTimer.start(95000);
        emit subscriptionRefreshRequested(m_active, token());
        return;
    }
    m_attempted.insert(m_active);
    if (!m_networkOnline) { m_attempted.remove(m_active); m_preferred = m_active; }
    emit locationHealth(m_active, QStringLiteral("unreachable"));
    ++m_generation;
    setState(Suspending, reason);
    emit suspendRequested(token());
}
void SelectionSession::stopWithFailure(const QString &reason)
{
    m_running = false;
    m_startupTimer.stop(); m_attemptTimer.stop(); m_healthTimer.stop(); m_retryTimer.stop();
    m_refreshDetachPending = false; m_refreshDetached = false;
    m_badLatency.invalidate();
    ++m_generation;
    setState(Stopped, reason);
    // Ошибка не является ручным отключением: исполнитель сначала подтверждает очистку с сохранением защиты.
    emit suspendRequested(token());
}
void SelectionSession::subscriptionRefreshed(const QString &value, bool successful, bool allowDetachedRetry)
{
    if (!m_running || value != token() || m_state != Refreshing) { return; }
    m_attemptTimer.stop();
    const auto entry = m_catalog->entry(m_active);
    if (!successful && !m_refreshDetached && allowDetachedRetry && m_networkOnline) {
        m_refreshDetachPending = true;
        ++m_generation;
        setState(Suspending, tr("Повтор обновления без сбойного туннеля; защита сохраняется"));
        emit suspendRequested(token());
        return;
    }
    if (m_refreshDetached) {
        m_refreshDetached = false;
        if (successful && entry && entry->enabled) { m_preferred = m_active; m_attempted.remove(m_active); }
        else { m_attempted.insert(m_active); emit locationHealth(m_active, QStringLiteral("unreachable")); }
        setState(Suspending, successful ? tr("Подписка обновлена; подключение") : m_failureReason);
        suspended(token(), true);
        return;
    }
    if (successful && entry && entry->enabled) {
        m_preferred = m_active; m_attempted.remove(m_active);
        ++m_generation;
        setState(Suspending, tr("Подписка обновлена; повторное подключение к текущей локации"));
        emit suspendRequested(token());
    } else { failed(m_failureReason); }
}
void SelectionSession::suspended(const QString &value, bool cleaned)
{
    if (!m_running && value == token() && m_state == Stopped && !m_status.isEmpty()) {
        // Ошибка ручного подключения тоже требует подтверждённой очистки,
        // но сохраняет защиту до явного отключения пользователя.
        if (!cleaned) {
            QTimer::singleShot(2000, this, [this, value]() {
                if (!m_running && value == token() && m_state == Stopped && !m_status.isEmpty()) { emit suspendRequested(value); }
            });
        }
        return;
    }
    if (!m_running || value != token() || m_state != Suspending) { return; }
    if (!cleaned) { setState(Suspending, tr("Ожидание очистки туннеля; защита сохраняется")); m_retryTimer.start(2000); return; }
    if (m_refreshDetachPending) {
        m_refreshDetachPending = false; m_refreshDetached = true;
        setState(Refreshing, tr("Повторное обновление подписки; VPN-трафик заблокирован"));
        m_attemptTimer.start(95000);
        emit subscriptionRefreshRequested(m_active, token());
        return;
    }
    // Продолжаем после ACK, чтобы сигналы старого протокола не попали в новую попытку.
    QTimer::singleShot(m_timing.switchMs, this, [this, value]() { if (m_running && value == token() && m_state == Suspending) { beginNext(); } });
}
}

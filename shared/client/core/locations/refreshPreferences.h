#pragma once
#include "secureQSettings.h"
#include "locationCoordinator.h"
#include "endpointHealthController.h"
#include "core/subscriptions/httpSubscriptionsController.h"

namespace hop {
// Пользовательское расписание отдельно от контроля исправности активного туннеля.
class RefreshPreferences : public QObject {
    Q_OBJECT
    Q_PROPERTY(int subscriptionMinutes READ subscriptionMinutes WRITE setSubscriptionMinutes NOTIFY changed)
    Q_PROPERTY(int healthMinutes READ healthMinutes WRITE setHealthMinutes NOTIFY changed)
    Q_PROPERTY(bool collectLatencyStatistics READ collectLatencyStatistics WRITE setCollectLatencyStatistics NOTIFY changed)
    Q_PROPERTY(int latencySeconds READ latencySeconds WRITE setLatencySeconds NOTIFY changed)
    Q_PROPERTY(int latencyThreshold READ latencyThreshold WRITE setLatencyThreshold NOTIFY changed)
public:
    RefreshPreferences(SecureQSettings *settings, HttpSubscriptionsController *http, LocationCoordinator *api,
        EndpointHealthController *health, QObject *parent = nullptr)
        : QObject(parent), m_settings(settings), m_http(http), m_api(api), m_health(health) {
        m_subscription = normalize(settings->value("Conf/hopSubscriptionMinutes", 360).toInt(), true);
        m_healthMinutes = normalize(settings->value("Conf/hopHealthMinutes", 0).toInt(), false);
        m_collectLatencyStatistics = settings->value("Conf/hopCollectLatencyStatistics",
            settings->value("Conf/hopHealthScope", 0).toInt() != 3).toBool();
        m_latencySeconds = qBound(15, settings->value("Conf/hopLatencySeconds", 60).toInt(), 3600);
        m_latencyThreshold = qBound(1, settings->value("Conf/hopLatencyThreshold", 1500).toInt(), 10000);
        connect(health, &EndpointHealthController::scheduledCheckRequested, this, [this]() {
            QStringList ids;
            for (const auto &location : m_api->catalog()->entries()) {
                if (location.enabled) { ids.append(location.id); }
            }
            if (m_api->running()) {
                if (ids.removeAll(m_api->activeId())) { m_api->checkCurrentLocation(m_api->activeId()); }
            }
            m_health->checkLocations(ids);
        });
        apply();
    }
    int subscriptionMinutes() const { return m_subscription; }
    int healthMinutes() const { return m_healthMinutes; }
    bool collectLatencyStatistics() const { return m_collectLatencyStatistics; }
    int latencySeconds() const { return m_latencySeconds; }
    int latencyThreshold() const { return m_latencyThreshold; }
    void setLatencySeconds(int value) {
        if (value < 15 || value > 3600 || value == m_latencySeconds || !m_settings->setValueChecked("Conf/hopLatencySeconds", value)) { return; }
        m_latencySeconds = value; applyLatency(); emit changed();
    }
    void setLatencyThreshold(int value) {
        if (value < 1 || value > 10000 || value == m_latencyThreshold || !m_settings->setValueChecked("Conf/hopLatencyThreshold", value)) { return; }
        m_latencyThreshold = value; applyLatency(); emit changed();
    }
    void setCollectLatencyStatistics(bool value) {
        if (value == m_collectLatencyStatistics || !m_settings->setValueChecked("Conf/hopCollectLatencyStatistics", value)) { return; }
        m_collectLatencyStatistics = value; applyCollection(); emit changed();
    }
    void setSubscriptionMinutes(int value) {
        value = normalize(value, true);
        if (value == m_subscription || !m_settings->setValueChecked("Conf/hopSubscriptionMinutes", value)) { return; }
        m_subscription = value; apply(); emit changed();
    }
    void setHealthMinutes(int value) {
        value = normalize(value, false);
        if (value == m_healthMinutes || !m_settings->setValueChecked("Conf/hopHealthMinutes", value)) { return; }
        m_healthMinutes = value; apply(); emit changed();
    }
signals:
    void changed();
private:
    static int normalize(int value, bool subscription) {
        const QList<int> allowed = subscription ? QList<int>{0, 15, 30, 60, 180, 360, 720, 1440} : QList<int>{0, 1, 5, 10, 30, 60};
        return allowed.contains(value) ? value : subscription ? 360 : 0;
    }
    void apply() {
        m_http->setRefreshIntervalMinutes(m_subscription);
        m_api->setRefreshIntervalMinutes(m_subscription);
        m_health->setRefreshIntervalMinutes(m_healthMinutes);
        applyCollection();
        applyLatency();
    }
    void applyLatency() { m_api->setLatencyPolicy(m_latencySeconds * 1000, m_latencyThreshold); }
    void applyCollection() { m_health->setHistorySelection({}, m_collectLatencyStatistics); }
    SecureQSettings *m_settings;
    HttpSubscriptionsController *m_http;
    LocationCoordinator *m_api;
    EndpointHealthController *m_health;
    int m_subscription = 360, m_healthMinutes = 0;
    bool m_collectLatencyStatistics = true;
    int m_latencySeconds = 60, m_latencyThreshold = 1500;
};
}

#pragma once
#include <QPointer>
#include <QProcess>
#include <QElapsedTimer>
#include "selectionSession.h"
#include "core/controllers/connectionController.h"
#include "core/repositories/secureAppSettingsRepository.h"

namespace hop {
class LocationCoordinator : public SelectionSession {
    Q_OBJECT
    Q_PROPERTY(QVariantMap statuses READ statuses NOTIFY statusesChanged)
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY catalogRefreshChanged)
    Q_PROPERTY(QVariantMap catalogStatuses READ catalogStatuses NOTIFY catalogRefreshChanged)
    Q_PROPERTY(QVariantMap trafficRates READ trafficRates NOTIFY trafficRatesChanged)
public:
    LocationCoordinator(LocationCatalog *catalog, SecureServersRepository *servers, SecureAppSettingsRepository *appSettings,
                        ConnectionController *connection, VpnConnection *vpn, QObject *parent = nullptr,
                        const QString &providerPath = QString(), const QString &probePath = QString(),
                        SelectionSession::Timing timing = SelectionSession::Timing());
    ~LocationCoordinator() override;
    QVariantMap statuses() const { return m_statuses; }
    LocationCatalog *catalog() const { return m_catalog; }
    void setRefreshIntervalMinutes(int minutes) {
        m_catalogTimer.stop();
        if (minutes > 0) { m_catalogTimer.start(qBound(1, minutes, 1440) * 60000); }
    }
    Q_INVOKABLE void toggleConnection();
    Q_INVOKABLE void disconnectManually();
    Q_INVOKABLE void refreshCatalogs();
    Q_INVOKABLE void refreshCatalog(const QString &serviceId);
    Q_INVOKABLE bool checkCurrentLocation(const QString &id);
    void forgetInactiveHealth(const QString &id);
    QVariantMap catalogStatuses() const { return m_catalogStatuses; }
    bool refreshing() const { return m_catalogProcess || !m_catalogQueue.isEmpty(); }
    QVariantMap trafficRates() const { return m_trafficRates; }
    bool handlesAutomaticRecovery() const { return running(); }
signals:
    void statusesChanged();
    void catalogRefreshChanged();
    void locationSampled(const QString &id, int latency);
    void catalogRefreshFinished(const QString &id, bool successful);
    void preparationFailed(const QString &message);
    void trafficRatesChanged();
private:
    void attempt(const QString &id, const QString &token);
    void open(const QString &id, const QString &token, const QString &resolvedEndpoint = QString());
    void check(const QString &id, const QString &token);
    void cancelProcesses();
    void drainCatalog();
    void saveGatewayState(const QJsonObject &response);
    LocationCatalog *m_catalog;
    SecureServersRepository *m_servers;
    SecureAppSettingsRepository *m_appSettings;
    ConnectionController *m_connection;
    VpnConnection *m_vpn;
    QString m_providerPath, m_probePath, m_resolver, m_interface;
    QPointer<QProcess> m_provider, m_probe;
    QPointer<QProcess> m_catalogProcess;
    QStringList m_catalogQueue;
    QString m_catalogCurrent;
    QString m_reportedPreparationError;
    QTimer m_catalogTimer;
    QTimer m_statusTimer;
    QElapsedTimer m_clock;
    QHash<QString, qint64> m_sampledAt;
    QVariantMap m_statuses;
    QVariantMap m_catalogStatuses;
    QVariantMap m_trafficRates;
    bool m_internalDisconnect = false;
    bool m_transportBusy = false;
};
}

#pragma once
#include <QObject>
#include <QVariantMap>
class SecureServersRepository;
class ConnectionController;
namespace hop {
class HttpSubscriptionsController;
class LocationCatalog;
class LocationCoordinator;
class EndpointHealthController;
// Решения о действиях страницы не зависят от QML и окон.
class ConnectionsPageController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap healthStatuses READ healthStatuses NOTIFY changed)
    Q_PROPERTY(QVariantMap serviceActions READ serviceActions NOTIFY changed)
    Q_PROPERTY(QVariantMap serviceStatuses READ serviceStatuses NOTIFY changed)
    Q_PROPERTY(QVariantMap locationActions READ locationActions NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    ConnectionsPageController(SecureServersRepository *, HttpSubscriptionsController *, LocationCatalog *,
                              LocationCoordinator *, EndpointHealthController *, ConnectionController *, QObject *parent = nullptr);
    QVariantMap healthStatuses() const;
    QVariantMap serviceActions() const;
    QVariantMap serviceStatuses() const;
    QVariantMap locationActions() const;
    QString lastError() const { return m_error; }
    Q_INVOKABLE bool checkLocation(const QString &id);
    Q_INVOKABLE void checkAllLocations();
    Q_INVOKABLE void refreshAll();
    Q_INVOKABLE bool refreshService(const QString &id);
    Q_INVOKABLE bool removeService(const QString &id);
signals:
    void changed();
    void errorChanged();
private:
    bool editingLocked() const;
    bool fail(const QString &message);
    SecureServersRepository *m_servers;
    HttpSubscriptionsController *m_subscriptions;
    LocationCatalog *m_catalog;
    LocationCoordinator *m_session;
    EndpointHealthController *m_health;
    ConnectionController *m_connection;
    QString m_error;
    bool m_connectionActive = false;
    QString m_recoverySource, m_recoveryToken;
    bool m_recoveryApi = false;
    QString m_candidateToken;
};
}

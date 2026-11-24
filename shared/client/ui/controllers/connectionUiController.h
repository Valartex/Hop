#ifndef CONNECTIONUICONTROLLER_H
#define CONNECTIONUICONTROLLER_H

#include <QObject>
#ifdef Q_OS_WIN
#include <QRemoteObjectNode>
#include <QPointer>
#include "rep_ipc_interface_replica.h"
#endif

#include "core/controllers/connectionController.h"
#include "core/utils/errorCodes.h"
#include "core/utils/routeModes.h"
#include "core/utils/commonStructs.h"
#include "core/protocols/vpnProtocol.h"
#include "core/controllers/serversController.h"
namespace hop { class LocationCoordinator; }

class ConnectionUiController : public QObject
{
    Q_OBJECT

public:
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionStateChanged)
    Q_PROPERTY(bool isConnectionInProgress READ isConnectionInProgress NOTIFY connectionStateChanged)
    Q_PROPERTY(bool isDisconnecting READ isDisconnecting NOTIFY connectionStateChanged)
    Q_PROPERTY(bool isCancelling READ isCancelling NOTIFY connectionStateChanged)
    Q_PROPERTY(QString connectionStateText READ connectionStateText NOTIFY connectionStateChanged)
    Q_PROPERTY(bool trafficProtectionActive READ trafficProtectionActive NOTIFY trafficProtectionChanged)
    Q_PROPERTY(bool protectionServiceAvailable READ protectionServiceAvailable NOTIFY trafficProtectionChanged)

    explicit ConnectionUiController(ConnectionController* connectionController,
                                    ServersController* serversController,
                                    QObject *parent = nullptr);

    ~ConnectionUiController() = default;

    bool isConnected() const;
    bool isConnectionInProgress() const;
    bool isDisconnecting() const { return m_disconnectRequested || m_state == Vpn::Disconnecting; }
    bool isCancelling() const { return m_cancelRequested; }
    QString connectionStateText() const;
    void setLocationCoordinator(hop::LocationCoordinator *coordinator) { m_locationCoordinator = coordinator; }
    bool trafficProtectionActive() const { return m_trafficProtectionActive; }
    bool protectionServiceAvailable() const { return m_protectionServiceAvailable; }

public slots:
    void toggleConnection();

    void openConnection();
    void closeConnection();
    void releaseTrafficProtection();

    bool isRevokeBlockedDuringActiveConnection(const QString &serverId, int containerIndex, const QString &clientId) const;

    ErrorCode getLastConnectionError();
    void onConnectionStateChanged(Vpn::ConnectionState state);

    void onTranslationsUpdated();

signals:
    void connectionStateChanged();
    void trafficProtectionChanged();
    void protectionRecoveryRequired();

    void connectionErrorOccurred(ErrorCode errorCode);

    void connectButtonClicked();
    void preparingConfig();
    void prepareConfig();
    void unsupportedConnectDrawerRequested();
    void noInstalledContainers();

private:
    void updateConnectionStateText();
    void notifyConnectionBlocked(ErrorCode errorCode);
    void refreshTrafficProtection();
    bool m_trafficProtectionActive = false;
    bool m_protectionServiceAvailable = false;
#ifdef Q_OS_WIN
    QRemoteObjectNode m_guardNode;
    QSharedPointer<IpcInterfaceReplica> m_guardReplica;
    QPointer<QRemoteObjectPendingCallWatcher> m_guardRequest;
    bool m_guardRefreshPending = false;
#endif

    ConnectionController* m_connectionController;
    hop::LocationCoordinator* m_locationCoordinator = nullptr;
    ServersController* m_serversController;

    bool m_isConnected = false;
    bool m_isConnectionInProgress = false;
    bool m_disconnectRequested = false;
    bool m_cancelRequested = false;
    QString m_connectionStateText = tr("Connect");

    Vpn::ConnectionState m_state;
};

#endif

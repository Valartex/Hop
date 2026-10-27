#include "connectionUiController.h"

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS) || defined(MACOS_NE)
    #include <QGuiApplication>
#else
    #include <QApplication>
#endif

#include "amneziaApplication.h"
#include "core/controllers/serversController.h"
#include "core/models/containerConfig.h"
#include "core/utils/containerEnum.h"
#include "ipc.h"
#include "version.h"
#include <QTimer>
#include "core/locations/locationCoordinator.h"
#ifdef Q_OS_WIN
#include <QRemoteObjectPendingCallWatcher>
#endif

ConnectionUiController::ConnectionUiController(ConnectionController* connectionController,
                                                ServersController* serversController,
                                                QObject *parent)
    : QObject(parent),
      m_connectionController(connectionController),
      m_serversController(serversController)
{
    connect(m_connectionController, &ConnectionController::connectionStateChanged, this, &ConnectionUiController::onConnectionStateChanged);

    connect(this, &ConnectionUiController::connectButtonClicked, this, &ConnectionUiController::toggleConnection, Qt::QueuedConnection);

    m_state = Vpn::ConnectionState::Disconnected;
#ifdef Q_OS_WIN
    if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop")) {
        m_guardNode.connectToNode(QUrl(QStringLiteral("local:") + amnezia::getIpcServiceUrl()));
        m_guardReplica.reset(m_guardNode.acquire<IpcInterfaceReplica>());
        auto *timer = new QTimer(this);
        timer->setInterval(3000);
        connect(timer, &QTimer::timeout, this, &ConnectionUiController::refreshTrafficProtection);
        timer->start();
        connect(m_guardReplica.data(), &QRemoteObjectReplica::stateChanged, this, [this]() { refreshTrafficProtection(); });
    }
#endif
}

void ConnectionUiController::refreshTrafficProtection()
{
#ifdef Q_OS_WIN
    const bool available = m_guardReplica && m_guardReplica->isReplicaValid();
    if (m_protectionServiceAvailable != available) {
        m_protectionServiceAvailable = available;
        emit trafficProtectionChanged();
    }
    if (!available) { return; }
    if (m_guardRequest) { m_guardRefreshPending = true; return; }
    // Проверка асинхронная: недоступная служба не задерживает интерфейс.
    auto *request = new QRemoteObjectPendingCallWatcher(m_guardReplica->trafficGuardFilterCount(), this);
    m_guardRequest = request;
    connect(request, &QRemoteObjectPendingCallWatcher::finished, this, [this, request]() {
        if (m_guardRequest != request) { request->deleteLater(); return; }
        const QRemoteObjectPendingReply<int> result(*request);
        if (result.error() == QRemoteObjectPendingCall::NoError && result.returnValue() >= 0) {
            const bool active = result.returnValue() > 0;
            if (active != m_trafficProtectionActive) {
                m_trafficProtectionActive = active;
                emit trafficProtectionChanged();
            }
        }
        m_guardRequest = nullptr;
        request->deleteLater();
        if (m_guardRefreshPending) {
            m_guardRefreshPending = false;
            QTimer::singleShot(0, this, &ConnectionUiController::refreshTrafficProtection);
        }
    });
    // После разрыва QtRO ответ может не прийти: освобождаем только локальный наблюдатель.
    QTimer::singleShot(5000, request, [this, request]() {
        if (m_guardRequest != request) { request->deleteLater(); return; }
        m_guardRequest = nullptr;
        request->deleteLater();
        if (m_guardRefreshPending) {
            m_guardRefreshPending = false;
            QTimer::singleShot(0, this, &ConnectionUiController::refreshTrafficProtection);
        }
    });
#endif
}

void ConnectionUiController::releaseTrafficProtection()
{
    if (m_isConnected || m_isConnectionInProgress) { return; }
    if (!m_protectionServiceAvailable) { emit protectionRecoveryRequired(); return; }
    closeConnection();
    QTimer::singleShot(500, this, &ConnectionUiController::refreshTrafficProtection);
}

void ConnectionUiController::openConnection()
{
    if (isDisconnecting()) { return; }
    if (m_locationCoordinator) { if (!m_locationCoordinator->running()) { m_locationCoordinator->toggleConnection(); } return; }
    const QString serverId = m_serversController->getDefaultServerId();
    if (serverId.isEmpty()) {
        m_connectionController->setConnectionState(Vpn::ConnectionState::Disconnected);
        return;
    }

    ErrorCode errorCode = m_connectionController->openConnection(serverId);

    if (errorCode != ErrorCode::NoError) {
        notifyConnectionBlocked(errorCode);
        return;
    }
}

void ConnectionUiController::closeConnection()
{
    if (m_disconnectRequested) { return; }
    // Отображаем отмену до синхронной остановки процессов и очереди запросов службе.
    m_cancelRequested = (m_locationCoordinator && m_locationCoordinator->running() && !m_locationCoordinator->verified())
        || (!m_isConnected && m_isConnectionInProgress);
    m_disconnectRequested = true;
    emit connectionStateChanged();
    if (m_locationCoordinator) { m_locationCoordinator->disconnectManually(); return; }
    m_connectionController->closeConnection();
}

ErrorCode ConnectionUiController::getLastConnectionError()
{
    return m_connectionController->lastConnectionError();
}

void ConnectionUiController::onConnectionStateChanged(Vpn::ConnectionState state)
{
    m_state = state;
    if (state == Vpn::Disconnected || state == Vpn::Error || state == Vpn::Unknown) {
        m_disconnectRequested = false;
        m_cancelRequested = false;
    }

    m_isConnected = false;
    switch (state) {
    case Vpn::ConnectionState::Connected: {
        amnApp->networkManager()->clearConnectionCache();

        m_isConnectionInProgress = false;
        m_isConnected = true;
        break;
    }
    case Vpn::ConnectionState::Connecting: {
        m_isConnectionInProgress = true;
        break;
    }
    case Vpn::ConnectionState::Reconnecting: {
        m_isConnectionInProgress = true;
        break;
    }
    case Vpn::ConnectionState::Disconnected: {
        m_isConnectionInProgress = false;
        break;
    }
    case Vpn::ConnectionState::Disconnecting: {
        m_isConnectionInProgress = true;
        break;
    }
    case Vpn::ConnectionState::Preparing: {
        m_isConnectionInProgress = true;
        break;
    }
    case Vpn::ConnectionState::Error: {
        m_isConnectionInProgress = false;
        if (!m_locationCoordinator || !m_locationCoordinator->handlesAutomaticRecovery()) { emit connectionErrorOccurred(getLastConnectionError()); }
        break;
    }
    case Vpn::ConnectionState::Unknown: {
        m_isConnectionInProgress = false;
        if (!m_locationCoordinator || !m_locationCoordinator->handlesAutomaticRecovery()) { emit connectionErrorOccurred(getLastConnectionError()); }
        break;
    }
    }
    updateConnectionStateText();
    emit connectionStateChanged();
    // После ACK отключения не ждём очередного трёхсекундного опроса guard.
    refreshTrafficProtection();
}

void ConnectionUiController::onTranslationsUpdated()
{
    // Смена языка не является подтверждением завершения отмены от транспорта.
    updateConnectionStateText();
    emit connectionStateChanged();
}

void ConnectionUiController::updateConnectionStateText()
{
    switch (m_state) {
    case Vpn::Connected: m_connectionStateText = tr("Connected"); break;
    case Vpn::Connecting: m_connectionStateText = tr("Connecting..."); break;
    case Vpn::Reconnecting: m_connectionStateText = tr("Reconnecting..."); break;
    case Vpn::Disconnecting: m_connectionStateText = tr("Disconnecting..."); break;
    case Vpn::Preparing: m_connectionStateText = tr("Preparing..."); break;
    default: m_connectionStateText = tr("Connect"); break;
    }
}

QString ConnectionUiController::connectionStateText() const
{
    return m_connectionStateText;
}

void ConnectionUiController::toggleConnection()
{
    if (isDisconnecting()) { return; }
    if (m_locationCoordinator) {
        if (m_locationCoordinator->running() || m_isConnected || m_isConnectionInProgress) { closeConnection(); }
        else { m_locationCoordinator->toggleConnection(); }
        return;
    }
    if (m_state == Vpn::ConnectionState::Preparing) {
        emit preparingConfig();
        return;
    }

    if (isConnectionInProgress()) {
        closeConnection();
    } else if (isConnected()) {
        closeConnection();
    } else {
        const QString serverId = m_serversController->getDefaultServerId();
        if (serverId.isEmpty()) {
            return;
        }

        const ErrorCode errorCode = m_connectionController->isConnectionSupported(serverId);
        if (errorCode != ErrorCode::NoError) {
            notifyConnectionBlocked(errorCode);
            return;
        }

        emit prepareConfig();
    }
}

void ConnectionUiController::notifyConnectionBlocked(ErrorCode errorCode)
{
    if (errorCode == ErrorCode::LegacyApiV1NotSupportedError) {
        emit unsupportedConnectDrawerRequested();
        return;
    }

    if (errorCode == ErrorCode::NoInstalledContainersError) {
        emit noInstalledContainers();
        return;
    }

    emit connectionErrorOccurred(errorCode);
}

bool ConnectionUiController::isConnectionInProgress() const
{
    return m_isConnectionInProgress || m_disconnectRequested;
}

bool ConnectionUiController::isConnected() const
{
    return m_isConnected;
}

bool ConnectionUiController::isRevokeBlockedDuringActiveConnection(const QString &serverId, int containerIndex,
                                                                   const QString &clientId) const
{
    if (clientId.isEmpty() || (!isConnected() && !isConnectionInProgress())) {
        return false;
    }

    if (m_serversController->getDefaultServerId() != serverId) {
        return false;
    }

    if (static_cast<int>(m_serversController->getDefaultContainer(serverId)) != containerIndex) {
        return false;
    }

    const auto adminConfig = m_serversController->selfHostedAdminConfig(serverId);
    if (!adminConfig.has_value()) {
        return false;
    }

    const QString connectionClientId =
            adminConfig->containerConfig(static_cast<DockerContainer>(containerIndex)).protocolConfig.clientId();
    if (connectionClientId.isEmpty()) {
        return false;
    }

    return connectionClientId == clientId || connectionClientId.contains(clientId);
}

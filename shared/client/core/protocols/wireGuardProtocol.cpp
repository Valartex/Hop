#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QTcpSocket>
#include <QThread>
#include <QEventLoop>
#include "version.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include "core/utils/ipcClient.h"
#endif

#include "wireGuardProtocol.h"
#include <QScopeGuard>
#include "core/utils/networkUtilities.h"

#include "mozilla/localsocketcontroller.h"

WireguardProtocol::WireguardProtocol(const QJsonObject &configuration, QObject *parent)
    : WireguardProtocol(configuration, new LocalSocketController(), parent)
{
}

WireguardProtocol::WireguardProtocol(const QJsonObject &configuration, ControllerImpl *controller, QObject *parent)
    : VpnProtocol(configuration, parent)
{
    m_impl.reset(controller);
    if (auto *localController = qobject_cast<LocalSocketController *>(controller)) {
        connect(localController, &LocalSocketController::backendUnavailable, this, [this]() {
            m_backendReady = false;
#ifdef Q_OS_WIN
            if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop") && m_started) {
                // Потеря IPC не является подтверждением завершения очистки туннеля.
                m_releaseGuard = false;
                m_preserveProtection = true;
                m_deactivationRequested = false;
            }
#endif
        });
    }
    connect(m_impl.get(), &ControllerImpl::initialized, this, [this](bool ready, bool, const QDateTime &) { m_backendReady = ready; });
    connect(m_impl.get(), &ControllerImpl::connected, this,
            [this](const QString &pubkey, const QDateTime &connectionTimestamp) {
#ifdef Q_OS_WIN
                if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop") && (!m_started || m_deactivationRequested)) { return; }
                if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop") && !applyTrafficProtection(QStringLiteral(APP_WINDOWS_TUNNEL_INTERFACE))) {
                    stopAfterFailure();
                    setLastError(ErrorCode::InternalError);
                    return;
                }
#endif
                setConnectionState(Vpn::ConnectionState::Connected);
            });
    connect(m_impl.get(), &ControllerImpl::statusUpdated, this,
            [this](const QString& serverIpv4Gateway,
                   const QString& deviceIpv4Address, uint64_t txBytes,
                   uint64_t rxBytes) {
                const QString previousGateway = m_vpnGateway;
                const QString previousLocal = m_vpnLocalAddress;

                if (!serverIpv4Gateway.isEmpty()) {
                    m_vpnGateway = serverIpv4Gateway;
                }
                if (!deviceIpv4Address.isEmpty()) {
                    m_vpnLocalAddress = deviceIpv4Address;
                }

                if ((!m_vpnGateway.isEmpty() && m_vpnGateway != previousGateway) ||
                    (!m_vpnLocalAddress.isEmpty() && m_vpnLocalAddress != previousLocal)) {
                    emit tunnelAddressesUpdated(m_vpnGateway, m_vpnLocalAddress);
                }
            });

    connect(m_impl.get(), &ControllerImpl::disconnected, this,
            [this]() {
                const bool failed = m_started && !m_releaseGuard && !m_deactivationRequested;
                m_started = false;
                if (m_releaseGuard) { releaseTrafficProtection(); }
#ifdef Q_OS_WIN
                if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop") && failed) {
                    m_preserveProtection = true;
                    setLastError(ErrorCode::AmneziaServiceConnectionFailed);
                    return;
                }
#endif
                setConnectionState(Vpn::ConnectionState::Disconnected);
            });
    m_impl->initialize(nullptr, nullptr);
}

WireguardProtocol::~WireguardProtocol()
{
#ifdef Q_OS_WIN
    if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop")) {
        // Остановленный дочерний транспорт не создаёт защиту заново: общий контроллер мог уже снять её после ACK обоих туннелей.
        // При аварии сохраняем существующие фильтры; незавершённый транспорт по-прежнему останавливаем ниже.
        if (m_preserveProtection && !m_started) { return; }
        if (m_preserveProtection) { stopAfterFailure(); }
        else { WireguardProtocol::stop(); }
        if (m_started) {
            QEventLoop wait;
            connect(m_impl.get(), &ControllerImpl::disconnected, &wait, &QEventLoop::quit);
            QTimer::singleShot(2000, &wait, &QEventLoop::quit);
            wait.exec(QEventLoop::ExcludeUserInputEvents);
        }
        return;
    }
#endif
    WireguardProtocol::stop();
    QThread::msleep(200);
}

void WireguardProtocol::stop()
{
    m_stopRequested = true;
    m_preserveProtection = false;
    m_releaseGuard = true;
#ifdef Q_OS_WIN
    if (m_starting && QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop")) { return; }
    if (QStringLiteral(APPLICATION_NAME) != QStringLiteral("Hop") || m_started) { stopMzImpl(); }
#else
    stopMzImpl();
#endif
    if (!m_started) { releaseTrafficProtection(); }
    return;
}

void WireguardProtocol::stopAfterFailure()
{
    if (m_stopRequested && m_releaseGuard) { stop(); return; }
    m_stopRequested = true;
    m_preserveProtection = true;
    m_releaseGuard = false;
#ifdef Q_OS_WIN
    if (m_starting && QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop")) { return; }
    if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop")) {
        if (m_guardOwned && !applyTrafficProtection(QString())) { qWarning() << "Failed to retain protection without tunnel permission"; }
        if (m_started) {
            stopMzImpl();
            QEventLoop wait;
            connect(m_impl.get(), &ControllerImpl::disconnected, &wait, &QEventLoop::quit);
            QTimer::singleShot(2000, &wait, &QEventLoop::quit);
            if (m_started) { wait.exec(QEventLoop::ExcludeUserInputEvents); }
        }
        return;
    }
#endif
    stopMzImpl();
}

bool WireguardProtocol::isCleanupComplete() const
{
    if (m_started || m_starting || (m_releaseGuard && m_guardOwned)) { return false; }
#ifdef Q_OS_WIN
    if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop") && !m_backendReady) {
        // После потери IPC отсутствие собственных ресурсов проверяем независимо от сигнала disconnected.
        const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!manager) { return false; }
        const auto service = OpenServiceW(manager, APP_WINDOWS_WG_SERVICE_NAME, SERVICE_QUERY_STATUS);
        const auto error = GetLastError();
        CloseServiceHandle(manager);
        if (!service) { return error == ERROR_SERVICE_DOES_NOT_EXIST; }
        SERVICE_STATUS status {};
        const bool stopped = QueryServiceStatus(service, &status) && status.dwCurrentState == SERVICE_STOPPED;
        CloseServiceHandle(service);
        return stopped;
    }
#endif
    return true;
}

bool WireguardProtocol::applyTrafficProtection(const QString &tunnelAlias)
{
#ifdef Q_OS_WIN
    return IpcClient::withInterface([&](QSharedPointer<IpcInterfaceReplica> iface) {
        auto reply = iface->applyTrafficGuard(m_guardConfig, tunnelAlias);
        return reply.waitForFinished(5000) && reply.returnValue();
    }, []() { return false; });
#else
    return true;
#endif
}

bool WireguardProtocol::updateTrafficProtection(bool enabled)
{
#ifdef Q_OS_WIN
    const auto previous = m_guardConfig.value(configKey::killSwitchOption);
    m_guardConfig.insert(configKey::killSwitchOption, enabled);
    const bool applied = applyTrafficProtection(isConnected() ? QStringLiteral(APP_WINDOWS_TUNNEL_INTERFACE) : QString());
    if (!applied) { m_guardConfig.insert(configKey::killSwitchOption, previous); }
    else { m_rawConfig.insert(configKey::killSwitchOption, enabled); m_guardOwned = enabled; }
    return applied;
#else
    return false;
#endif
}

void WireguardProtocol::releaseTrafficProtection()
{
#ifdef Q_OS_WIN
    if (!m_guardOwned) { return; }
    const bool cleared = IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) {
        auto reply = iface->clearTrafficGuard();
        return reply.waitForFinished(5000) && reply.returnValue();
    }, []() { return false; });
    if (cleared) { m_guardOwned = false; }
#endif
}

ErrorCode WireguardProtocol::startMzImpl()
{
#ifdef Q_OS_WIN
    if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop") && (m_started || m_starting)) { return ErrorCode::InternalError; }
    m_starting = true; m_stopRequested = false;
    const auto starting = qScopeGuard([this]() {
        m_starting = false;
        if (m_stopRequested) { if (m_releaseGuard) { stop(); } else { stopAfterFailure(); } }
    });
    if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop") && !m_backendReady) {
        QEventLoop wait;
        connect(m_impl.get(), &ControllerImpl::initialized, &wait, &QEventLoop::quit);
        QTimer::singleShot(5000, &wait, &QEventLoop::quit);
        wait.exec(QEventLoop::ExcludeUserInputEvents);
        if (!m_backendReady) { return ErrorCode::AmneziaServiceConnectionFailed; }
    }
#endif
    if (m_stopRequested) { return ErrorCode::NoError; }
    QString protocolName = m_rawConfig.value("protocol").toString();
    QJsonObject vpnConfigData = m_rawConfig.value(protocolName + "_config_data").toObject();
    vpnConfigData[configKey::hostName] = NetworkUtilities::getIPAddress(vpnConfigData.value(configKey::hostName).toString());
    m_rawConfig.insert(protocolName + "_config_data", vpnConfigData);
    m_rawConfig[configKey::hostName] = NetworkUtilities::getIPAddress(m_rawConfig[configKey::hostName].toString());

#ifdef Q_OS_WIN
    if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop")) {
        m_guardConfig = m_rawConfig;
        m_guardConfig.insert(QStringLiteral("vpnServer"), m_rawConfig.value(configKey::hostName));
        m_guardConfig.insert(QStringLiteral("hopTransportPort"), vpnConfigData.value(configKey::port));
        m_guardConfig.insert(QStringLiteral("hopTransportProtocol"), QStringLiteral("udp"));
        m_guardOwned = true;
        m_releaseGuard = false;
        m_preserveProtection = false;
        if (!applyTrafficProtection(QString())) { m_preserveProtection = true; return ErrorCode::InternalError; }
    }
#endif

    if (m_stopRequested) { return ErrorCode::NoError; }
    m_started = true;
    m_deactivationRequested = false;
    m_impl->activate(m_rawConfig);
    return ErrorCode::NoError;
}

ErrorCode WireguardProtocol::stopMzImpl()
{
#ifdef Q_OS_WIN
    if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop") && m_deactivationRequested) { return ErrorCode::NoError; }
#endif
    m_deactivationRequested = true;
    m_impl->deactivate();
    return ErrorCode::NoError;
}


ErrorCode WireguardProtocol::start()
{
    return startMzImpl();
}

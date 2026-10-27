#include "ipcserver.h"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <limits>

#include "logger.h"
#include "router.h"
#include "killswitch.h"
#include "xray.h"

#ifdef Q_OS_WIN
    #include "tapcontroller_win.h"
    #include "windowsTrafficGuard.h"
    #include "windowsIpcPeer.h"
#endif


IpcServer::IpcServer(QObject *parent) : IpcInterfaceSource(parent)
{
    connect(&m_pingHelper, &PingHelper::connectionLose, this, &IpcServer::connectionLose);
}

bool IpcServer::applyTrafficGuard(const QJsonObject &config, const QString &tunnelAlias)
{
#ifdef Q_OS_WIN
    return WindowsTrafficGuard::apply(config, tunnelAlias);
#else
    return false;
#endif
}

bool IpcServer::clearTrafficGuard()
{
#ifdef Q_OS_WIN
    return WindowsTrafficGuard::clear();
#else
    return true;
#endif
}

int IpcServer::trafficGuardFilterCount()
{
#ifdef Q_OS_WIN
    return WindowsTrafficGuard::filterCount();
#else
    return 0;
#endif
}

bool IpcServer::permitControlEndpoint(const QString &address, int port, bool udp)
{
#ifdef Q_OS_WIN
    return WindowsTrafficGuard::permitControlEndpoint(address, port, udp);
#else
    return false;
#endif
}

int IpcServer::createPrivilegedProcess()
{
#ifdef Q_OS_WIN
    if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop")) {
        // Дескрипторы не удерживают сами себя через shared-pointer в сигнальных лямбдах.
        if (m_processes.size() >= 8 || m_localpid == std::numeric_limits<int>::max()) { return -1; }
        const int id = ++m_localpid;
        ProcessDescriptor descriptor(this);
        descriptor.localServer->setSocketOptions(QLocalServer::WorldAccessOption);
        if (!descriptor.localServer->listen(amnezia::getIpcProcessUrl(id))) { return -1; }
        m_processes.insert(id, descriptor);
        const auto retire = [this, id]() {
            QTimer::singleShot(5000, this, [this, id]() {
                const auto found = m_processes.constFind(id);
                if (found == m_processes.constEnd() || found->ipcProcess->isRunning()) { return; }
                found->localServer->close();
                m_processes.remove(id);
            });
        };
        connect(descriptor.localServer.data(), &QLocalServer::newConnection, this, [this, id, retire]() {
            const auto found = m_processes.constFind(id);
            if (found == m_processes.constEnd()) { return; }
            while (found->localServer->hasPendingConnections()) {
                auto *socket = found->localServer->nextPendingConnection();
                if (!isTrustedHopIpcPeer(socket)) { socket->close(); socket->deleteLater(); continue; }
                connect(socket, &QLocalSocket::disconnected, this, [this, id, retire]() {
                    const auto found = m_processes.constFind(id);
                    if (found != m_processes.constEnd()) { found->ipcProcess->stopOwnedProcess(); }
                    retire();
                });
                found->serverNode->addHostSideConnection(socket);
                found->serverNode->enableRemoting(found->ipcProcess.data());
            }
        });
        connect(descriptor.ipcProcess.data(), &IpcServerProcess::finished, this, retire);
        connect(descriptor.ipcProcess.data(), &IpcServerProcess::errorOccurred, this, retire);
        QTimer::singleShot(30000, this, [this, id]() {
            const auto found = m_processes.constFind(id);
            if (found != m_processes.constEnd() && !found->ipcProcess->isRunning()) { found->localServer->close(); m_processes.remove(id); }
        });
        return id;
    }
#endif
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::createPrivilegedProcess";
#endif

    m_localpid++;

    ProcessDescriptor pd(this);

    pd.localServer->setSocketOptions(QLocalServer::WorldAccessOption);

    if (!pd.localServer->listen(amnezia::getIpcProcessUrl(m_localpid))) {
        qDebug() << QString("Unable to start the server: %1.").arg(pd.localServer->errorString());
        return -1;
    }

    // Make sure any connections are handed to QtRO
    QObject::connect(pd.localServer.data(), &QLocalServer::newConnection, this, [pd]() {
        qDebug() << "IpcServer new connection";
        if (pd.serverNode) {
            pd.serverNode->addHostSideConnection(pd.localServer->nextPendingConnection());
            pd.serverNode->enableRemoting(pd.ipcProcess.data());
        }
    });

    QObject::connect(pd.serverNode.data(), &QRemoteObjectHost::error, this,
                     [pd](QRemoteObjectNode::ErrorCode errorCode) { qDebug() << "QRemoteObjectHost::error" << errorCode; });

    QObject::connect(pd.serverNode.data(), &QRemoteObjectHost::destroyed, this, [pd]() { qDebug() << "QRemoteObjectHost::destroyed"; });

    m_processes.insert(m_localpid, pd);

    return m_localpid;
}

int IpcServer::routeAddList(const QString &gw, const QStringList &ips)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::routeAddList";
#endif

    return Router::routeAddList(gw, ips);
}

bool IpcServer::clearSavedRoutes()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::clearSavedRoutes";
#endif

    return Router::clearSavedRoutes();
}

bool IpcServer::routeDeleteList(const QString &gw, const QStringList &ips)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::routeDeleteList";
#endif

    return Router::routeDeleteList(gw, ips);
}

bool IpcServer::flushDns()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::flushDns";
#endif

    return Router::flushDns();
}

void IpcServer::resetIpStack()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::resetIpStack";
#endif

    Router::resetIpStack();
}

bool IpcServer::checkAndInstallDriver()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::checkAndInstallDriver";
#endif

#ifdef Q_OS_WIN
    return TapController::checkAndSetup();
#else
    return true;
#endif
}

QStringList IpcServer::getTapList()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::getTapList";
#endif

#ifdef Q_OS_WIN
    return TapController::getTapList();
#else
    return QStringList();
#endif
}

void IpcServer::cleanUp()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::cleanUp";
#endif

    Logger::deInit();
    Logger::cleanUp();
}

void IpcServer::clearLogs()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::clearLogs";
#endif

    Logger::clearLogs(true);
}

bool IpcServer::createTun(const QString &dev, const QString &subnet)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::createTun";
#endif

    return Router::createTun(dev, subnet);
}

bool IpcServer::deleteTun(const QString &dev)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::deleteTun";
#endif

    return Router::deleteTun(dev);
}

bool IpcServer::updateResolvers(const QString &ifname, const QList<QHostAddress> &resolvers)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::updateResolvers";
#endif

    return Router::updateResolvers(ifname, resolvers);
}

bool IpcServer::restoreResolvers()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::restoreResolvers";
#endif

    return Router::restoreResolvers();
}

bool IpcServer::StartRoutingIpv6()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::StartRoutingIpv6";
#endif

    return Router::StartRoutingIpv6();
}

bool IpcServer::StopRoutingIpv6()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::StopRoutingIpv6";
#endif

    return Router::StopRoutingIpv6();
}

void IpcServer::setLogsEnabled(bool enabled)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::setLogsEnabled";
#endif

    if (enabled) {
        Logger::init(true);
    } else {
        Logger::deInit();
    }
}

bool IpcServer::startNetworkCheck(const QString& serverIpv4Gateway, const QString& deviceIpv4Address)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::startNetworkCheck";
#endif

    m_pingHelper.start(serverIpv4Gateway, deviceIpv4Address);
    return true;
}

bool IpcServer::stopNetworkCheck()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::stopNetworkCheck";
#endif

    m_pingHelper.stop();
    return true;
}

bool IpcServer::resetKillSwitchAllowedRange(QStringList ranges)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::resetKillSwitchAllowedRange";
#endif

    return KillSwitch::instance()->resetAllowedRange(ranges);
}

bool IpcServer::addKillSwitchAllowedRange(QStringList ranges)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::addKillSwitchAllowedRange";
#endif

    return KillSwitch::instance()->addAllowedRange(ranges);
}

bool IpcServer::disableAllTraffic()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::disableAllTraffic";
#endif

    return KillSwitch::instance()->disableAllTraffic();
}

bool IpcServer::enableKillSwitch(const QJsonObject &configStr, int vpnAdapterIndex)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::enableKillSwitch";
#endif

    return KillSwitch::instance()->enableKillSwitch(configStr, vpnAdapterIndex);
}

bool IpcServer::disableKillSwitch()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::disableKillSwitch";
#endif

    return KillSwitch::instance()->disableKillSwitch();
}

bool IpcServer::enablePeerTraffic(const QJsonObject &configStr)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::enablePeerTraffic";
#endif

    return KillSwitch::instance()->enablePeerTraffic(configStr);
}

bool IpcServer::refreshKillSwitch(bool enabled)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::refreshKillSwitch";
#endif

    return KillSwitch::instance()->refresh(enabled);
}

bool IpcServer::xrayStart(const QString& cfg)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::xrayStart";
#endif

    return Xray::getInstance().startXray(cfg);
}

bool IpcServer::xrayStop()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::xrayStop";
#endif

    return Xray::getInstance().stopXray();
}

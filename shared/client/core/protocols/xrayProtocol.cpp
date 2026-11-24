#include "xrayProtocol.h"

#include "core/models/protocols/xrayProtocolConfig.h"
#include "core/protocols/protocolUtils.h"
#include "core/utils/constants/configKeys.h"
#include "core/utils/constants/protocolConstants.h"
#include "core/utils/ipcClient.h"
#include "core/utils/networkUtilities.h"
#include "core/utils/serialization/serialization.h"
#include "ipc.h"
#include "version.h"
#include "core/routing/trafficPolicy.h"
#include "core/routing/localSubnetRoutes.h"
#include "../../../common/routing/privateRoutePolicy.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QTimer>
#include <QScopeGuard>
#include <QJsonObject>
#include <QTcpServer>
#include <QNetworkInterface>
#include <QtCore/qlogging.h>
#include <QtCore/qobjectdefs.h>
#include <QtCore/qprocess.h>

#include <exception>

#ifdef Q_OS_MACOS
static const QString tunName = "utun22";
#elif defined(Q_OS_WIN)
static const QString tunName = QStringLiteral(APP_WINDOWS_XRAY_INTERFACE);
#else
static const QString tunName = "tun2";
#endif

XrayProtocol::XrayProtocol(const QJsonObject &configuration, QObject *parent) : VpnProtocol(configuration, parent)
{
    m_metricsNetwork = new QNetworkAccessManager(this);
    m_metricsNetwork->setProxy(QNetworkProxy::NoProxy);
    m_metricsTimer.setInterval(1000);
    connect(&m_metricsTimer, &QTimer::timeout, this, &XrayProtocol::pollTraffic);
    connect(this, &VpnProtocol::connectionStateChanged, this, [this](Vpn::ConnectionState state) {
        if (state == Vpn::Connected && m_metricsPort) { m_metricsBaseline = false; m_metricsTimer.start(); }
        else {
            m_metricsTimer.stop(); m_metricsBaseline = false;
            if (m_metricsReply) { m_metricsReply->abort(); }
        }
    });
    m_retryTimer.setSingleShot(true);
    m_retryTimer.setInterval(tun2socksRetryDelayMs);
    connect(&m_retryTimer, &QTimer::timeout, this, [this]() {
        if (!m_sessionActive || m_stopping) { return; }
        const auto error = startTun2Socks();
        if (!m_stopRequested && error != ErrorCode::NoError) { stopAfterFailure(); setLastError(error); }
    });
    m_vpnGateway = amnezia::protocols::xray::defaultLocalAddr;
    m_vpnLocalAddress = amnezia::protocols::xray::defaultLocalAddr;
    m_routeGateway = NetworkUtilities::getGatewayAndIface().first;

    m_routeMode = static_cast<amnezia::RouteMode>(configuration.value(amnezia::configKey::splitTunnelType).toInt());
    m_remoteAddress = NetworkUtilities::getIPAddress(m_rawConfig.value(amnezia::configKey::hostName).toString());
#ifdef Q_OS_WIN
    // Используем выбранный Windows маршрут к серверу, а не последний адаптер с gateway.
    m_routeGateway = NetworkUtilities::getGatewayAndIfaceTo(QHostAddress(m_remoteAddress)).first;
#endif

    const QString primaryDns = configuration.value(amnezia::configKey::dns1).toString();
    m_dnsServers.push_back(QHostAddress(primaryDns));
    if (primaryDns != amnezia::protocols::dns::amneziaDnsIp) {
        const QString secondaryDns = configuration.value(amnezia::configKey::dns2).toString();
        m_dnsServers.push_back(QHostAddress(secondaryDns));
    }

    QJsonObject xrayConfiguration = configuration.value(ProtocolUtils::key_proto_config_data(Proto::Xray)).toObject();
    if (xrayConfiguration.isEmpty()) {
        xrayConfiguration = configuration.value(ProtocolUtils::key_proto_config_data(Proto::SSXray)).toObject();
    }

    if (xrayConfiguration.isEmpty()) {
        qWarning() << "Xray config wrapper is empty";
        m_xrayConfig = {};
    }

    m_xrayConfig = QJsonDocument::fromJson(xrayConfiguration.value(amnezia::configKey::config).toString().toUtf8()).object();
    if (m_xrayConfig.isEmpty()) {
        qWarning() << "Xray config string is not a valid JSON object";
        m_xrayConfig = {};
    }
#ifdef Q_OS_WIN
    if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop") && configuration.contains(QStringLiteral("hopTrafficPolicy"))) {
        const auto policy = hop::deserializeTrafficPolicy(configuration.value(QStringLiteral("hopTrafficPolicy")).toObject());
        QString error;
        m_xrayConfig = hop::applyXrayTrafficPolicy(m_xrayConfig, policy, primaryDns, &error);
        if (!error.isEmpty()) { qWarning() << "Hop routing policy rejected"; m_xrayConfig = {}; }
        // Реальный DNS задаётся в ядре; системные запросы всегда проходят через общий TUN.
        m_dnsServers = { QHostAddress(QStringLiteral("198.18.0.1")) };
        m_rawConfig.insert(configKey::dns1, QStringLiteral("198.18.0.1"));
        m_rawConfig.insert(configKey::dns2, QString());
    }
#endif
}

XrayProtocol::~XrayProtocol()
{
    qDebug() << "XrayProtocol::~XrayProtocol()";
    cleanup(!m_preserveProtection);
}

QJsonObject XrayProtocol::trafficGuardConfig() const
{
    auto config = m_rawConfig;
    config.insert(QStringLiteral("vpnServer"), m_remoteAddress);
    const auto outbounds = m_xrayConfig.value(QStringLiteral("outbounds")).toArray();
    for (const auto &value : outbounds) {
        const auto outbound = value.toObject();
        if (outbound.value(QStringLiteral("protocol")).toString() != QStringLiteral("vless")) { continue; }
        const auto servers = outbound.value(QStringLiteral("settings")).toObject().value(QStringLiteral("vnext")).toArray();
        if (!servers.isEmpty()) { config.insert(QStringLiteral("hopTransportPort"), servers.first().toObject().value(QStringLiteral("port"))); }
        break;
    }
    return config;
}

QNetworkProxy XrayProtocol::localProxy() const
{
    return QNetworkProxy(QNetworkProxy::Socks5Proxy, QStringLiteral("127.0.0.1"),
                         static_cast<quint16>(m_socksPort), m_socksUser, m_socksPassword);
}

bool XrayProtocol::updateTrafficProtection(bool enabled)
{
#ifdef Q_OS_WIN
    const auto previous = m_rawConfig.value(configKey::killSwitchOption);
    m_rawConfig.insert(configKey::killSwitchOption, enabled);
    const bool applied = IpcClient::withInterface([&](QSharedPointer<IpcInterfaceReplica> iface) {
        auto result = iface->applyTrafficGuard(trafficGuardConfig(), isConnected() ? tunName : QString());
        return result.waitForFinished(5000) && result.returnValue();
    }, []() { return false; });
    if (!applied) { m_rawConfig.insert(configKey::killSwitchOption, previous); }
    else { m_guardOwned = enabled; }
    return applied;
#else
    return false;
#endif
}

ErrorCode XrayProtocol::start()
{
    qDebug() << "XrayProtocol::start()";
    if (m_sessionActive || m_cleanupPending || m_stopping || m_operationDepth) { return ErrorCode::InternalError; }
    m_stopRequested = false; m_releaseRequested = false;
    ++m_operationDepth;
    const auto operation = qScopeGuard([this]() { finishOperation(); });
    if (m_xrayConfig.isEmpty()) { return ErrorCode::XrayExecutableCrashed; }

    // Inject SOCKS5 auth into the inbound before starting xray.
    // Re-uses existing credentials if the config already has them (e.g. imported config).
    amnezia::serialization::inbounds::InboundCredentials creds;
    try {
        creds = amnezia::serialization::inbounds::EnsureInboundAuth(m_xrayConfig);
    } catch (const std::exception &e) {
        qCritical() << "EnsureInboundAuth failed:" << e.what();
        return ErrorCode::InternalError;
    }
    if (creds.port < 1 || creds.port > 65535 || creds.username.isEmpty() || creds.password.isEmpty()) {
        return ErrorCode::InternalError;
    }
    m_socksUser = creds.username;
    m_socksPassword = creds.password;
    m_socksPort = creds.port;
    if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop")) { configureTrafficMetrics(); }

    QString xrayConfigStr = QJsonDocument(m_xrayConfig).toJson(QJsonDocument::Compact);
    if (xrayConfigStr.isEmpty()) {
        qCritical() << "Xray config is empty";
        return ErrorCode::XrayExecutableCrashed;
    }

    xrayConfigStr = amnezia::sanitizeNativeConfig(xrayConfigStr);

    const auto result = IpcClient::withInterface(
            [&](QSharedPointer<IpcInterfaceReplica> iface) {
                disconnect(m_serviceConnection);
                m_serviceConnection = connect(iface.data(), &QRemoteObjectReplica::stateChanged, this,
                    [this](QRemoteObjectReplica::State state, QRemoteObjectReplica::State) {
                        if (state != QRemoteObjectReplica::Valid && m_sessionActive && !m_stopping) {
                            stopAfterFailure();
                            setLastError(ErrorCode::AmneziaServiceConnectionFailed);
                        }
                    }, Qt::QueuedConnection);
                // Даже при тайм-ауте ответа служба могла успеть создать ресурсы.
                m_sessionActive = true;
                m_cleanupPending = true;
                m_xrayStopPending = true;
#ifdef Q_OS_WIN
                if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop")) {
                    m_guardOwned = true;
                    m_preserveProtection = false;
                    auto protection = iface->applyTrafficGuard(trafficGuardConfig(), QString());
                    if (!protection.waitForFinished() || !protection.returnValue()) { return ErrorCode::InternalError; }
                }
#endif
                if (m_stopRequested) { return ErrorCode::NoError; }
                auto xrayStart = iface->xrayStart(xrayConfigStr);
                if (!xrayStart.waitForFinished() || !xrayStart.returnValue()) {
                    qCritical() << "Failed to start xray";
                    return ErrorCode::XrayExecutableCrashed;
                }
                if (m_stopRequested) { return ErrorCode::NoError; }
                return startTun2Socks();
            },
            []() { return ErrorCode::AmneziaServiceConnectionFailed; });
    if (m_stopRequested) { return ErrorCode::NoError; }
    if (result != ErrorCode::NoError) { stopAfterFailure(); }
    return result;
}

bool XrayProtocol::configureTrafficMetrics()
{
    // Только loopback, порт выбирается заново для каждой сессии ядра.
    QTcpServer reservation;
    if (!reservation.listen(QHostAddress::LocalHost, 0)) { m_metricsPort = 0; return false; }
    m_metricsPort = reservation.serverPort();
    m_xrayConfig.insert("metrics", QJsonObject{{"tag", "hop-metrics"}, {"listen", QStringLiteral("127.0.0.1:%1").arg(m_metricsPort)}});
    m_xrayConfig.insert("stats", QJsonObject());
    auto policy = m_xrayConfig.value("policy").toObject();
    auto system = policy.value("system").toObject();
    system.insert("statsOutboundUplink", true); system.insert("statsOutboundDownlink", true);
    policy.insert("system", system); m_xrayConfig.insert("policy", policy);
    return true;
}

void XrayProtocol::pollTraffic()
{
    if (m_metricsReply || !isConnected() || !m_metricsPort) { return; }
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/debug/vars").arg(m_metricsPort)));
    request.setTransferTimeout(1500);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto *reply = m_metricsNetwork->get(request); m_metricsReply = reply;
    reply->setReadBufferSize(262145);
    connect(reply, &QNetworkReply::readyRead, this, [reply]() {
        if (reply->bytesAvailable() > 262144) { reply->abort(); }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_metricsReply == reply) { m_metricsReply = nullptr; }
        reply->deleteLater();
        if (!isConnected()) { return; }
        const auto bytes = reply->readAll();
        const auto root = bytes.size() <= 262144 ? QJsonDocument::fromJson(bytes).object() : QJsonObject();
        const auto outbound = root.value("stats").toObject().value("outbound").toObject();
        if (reply->error() != QNetworkReply::NoError || !outbound.contains("hop-vpn")) {
            m_metricsBaseline = false; emit trafficRatesChanged(-1, -1, -1, -1); return;
        }
        const auto vpn = outbound.value("hop-vpn").toObject(), direct = outbound.value("hop-direct").toObject();
        const std::array<qint64, 4> counters {vpn.value("downlink").toInteger(), vpn.value("uplink").toInteger(),
                                            direct.value("downlink").toInteger(), direct.value("uplink").toInteger()};
        std::array<double, 4> rates {};
        const auto elapsed = m_metricsBaseline ? m_metricsClock.elapsed() : 0;
        if (elapsed > 0) {
            for (size_t index = 0; index < counters.size(); ++index) {
                rates[index] = counters[index] >= m_previousCounters[index] ? double(counters[index] - m_previousCounters[index]) * 0.008 / elapsed : 0;
            }
        }
        m_previousCounters = counters; m_metricsClock.restart(); m_metricsBaseline = true;
        emit trafficRatesChanged(rates[0], rates[1], rates[2], rates[3]);
    });
}

void XrayProtocol::stop()
{
    m_stopRequested = true; m_releaseRequested = true;
    m_preserveProtection = false; m_sessionActive = false;
    m_retryTimer.stop();
    // Уже отправленный IPC сначала завершается: stop не должен обгонять start на стороне службы.
    if (m_operationDepth) { m_cleanupPending = true; return; }
    cleanup(true);
}

void XrayProtocol::stopAfterFailure()
{
    m_stopRequested = true;
    m_preserveProtection = !m_releaseRequested; m_sessionActive = false;
    m_retryTimer.stop();
    if (m_operationDepth) { m_cleanupPending = true; return; }
    cleanup(m_releaseRequested);
}

void XrayProtocol::finishOperation()
{
    --m_operationDepth;
    if (!m_operationDepth && m_stopRequested) { cleanup(m_releaseRequested); }
}

void XrayProtocol::cleanup(bool releaseProtection)
{
    if (m_stopping || (!m_sessionActive && !m_cleanupPending && !m_tun2socksProcess && !(releaseProtection && m_guardOwned))) { return; }
    if (!m_sessionActive && !m_cleanupPending && !m_tun2socksProcess) {
        if (releaseProtection && m_guardOwned) {
            const bool cleared = IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) {
                auto result = iface->clearTrafficGuard();
                return result.waitForFinished() && result.returnValue();
            }, []() { return false; });
            if (cleared) { m_guardOwned = false; }
        }
        return;
    }
    m_stopping = true;
    const auto stoppingGuard = qScopeGuard([this]() { m_stopping = false; });
    m_sessionActive = false;
    ++m_processGeneration;
    m_retryTimer.stop();
    m_tun2socksRetryCount = 0;
    if (m_tun2socksProcess) { m_tun2socksProcess->blockSignals(true); }
    qDebug() << "XrayProtocol::stop()";

    const bool cleaned = IpcClient::withInterface([this, releaseProtection](QSharedPointer<IpcInterfaceReplica> iface) {
        bool succeeded = true;
        if (!releaseProtection && m_guardOwned && QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop")) {
            auto protection = iface->applyTrafficGuard(trafficGuardConfig(), QString());
            if (!protection.waitForFinished(5000) || !protection.returnValue()) { succeeded = false; }
        }
        auto disableKillSwitch = iface->disableKillSwitch();
        if (!disableKillSwitch.waitForFinished() || !disableKillSwitch.returnValue()) {
            succeeded = false;
            qWarning() << "Failed to disable killswitch";
        }

        auto StartRoutingIpv6 = iface->StartRoutingIpv6();
        if (!StartRoutingIpv6.waitForFinished() || !StartRoutingIpv6.returnValue()) {
            succeeded = false;
            qWarning() << "Failed to start routing ipv6";
        }

        auto restoreResolvers = iface->restoreResolvers();
        if (!restoreResolvers.waitForFinished() || !restoreResolvers.returnValue()) {
            succeeded = false;
            qWarning() << "Failed to restore resolvers";
        }

        auto deleteTun = iface->deleteTun(tunName);
        if (!deleteTun.waitForFinished() || !deleteTun.returnValue()) {
            succeeded = false;
            qWarning() << "Failed to delete tun";
        }

        if (m_xrayStopPending) {
            auto xrayStop = iface->xrayStop();
            if (!xrayStop.waitForFinished() || !xrayStop.returnValue()) {
                succeeded = false;
                qWarning() << "Failed to stop xray";
            } else { m_xrayStopPending = false; }
        }
        if (succeeded && releaseProtection && m_guardOwned) {
            auto protection = iface->clearTrafficGuard();
            if (!protection.waitForFinished() || !protection.returnValue()) { succeeded = false; }
            else { m_guardOwned = false; }
        }
        return succeeded;
    }, []() { return false; });
    m_cleanupPending = !cleaned;

    if (m_tun2socksProcess) {
        m_tun2socksProcess->blockSignals(true);

#ifndef Q_OS_WIN
        m_tun2socksProcess->terminate();
        auto waitForFinished = m_tun2socksProcess->waitForFinished(1000);
        if (!waitForFinished.waitForFinished() || !waitForFinished.returnValue()) {
            qWarning() << "Failed to terminate tun2socks. Killing the process...";
            m_tun2socksProcess->kill();
        }
#else
        // terminate does not do anything useful on Windows
        // so just kill the process
        m_tun2socksProcess->kill();
#endif

        m_tun2socksProcess->close();
        m_tun2socksProcess.reset();
    }

    setConnectionState(Vpn::ConnectionState::Disconnected);
}

ErrorCode XrayProtocol::startTun2Socks()
{
    if (m_stopRequested || !m_sessionActive) { return ErrorCode::NoError; }
    ++m_operationDepth;
    const auto operation = qScopeGuard([this]() { finishOperation(); });
    const auto generation = ++m_processGeneration;
    m_tun2socksProcess = IpcClient::CreatePrivilegedProcess();
    if (!m_tun2socksProcess || !m_tun2socksProcess->waitForSource(1000)) {
        return ErrorCode::AmneziaServiceConnectionFailed;
    }

    if (m_stopRequested) { return ErrorCode::NoError; }
    const QString proxyUrl = QString("socks5://%1:%2@127.0.0.1:%3").arg(m_socksUser, m_socksPassword, QString::number(m_socksPort));

    m_tun2socksProcess->setProgram(PermittedProcess::Tun2Socks);
    m_tun2socksProcess->setArguments({ "-device", QString("tun://%1").arg(tunName), "-proxy", proxyUrl });

    connect(
            m_tun2socksProcess.data(), &IpcProcessInterfaceReplica::readyReadStandardError, this, 
            [this, generation]() {
                if (!m_sessionActive || m_stopping || !m_tun2socksProcess || generation != m_processGeneration) { return; }
                auto readAllStandardError = m_tun2socksProcess->readAllStandardError();
                if (!readAllStandardError.waitForFinished()) {
                    qWarning() << "Failed to read output from tun2socks";
                    return;
                }

                const QString line = readAllStandardError.returnValue();

                if (!line.contains("[TCP]") && !line.contains("[UDP]")) {
                    QString safeLine = line;
                    safeLine.replace(QRegularExpression(QStringLiteral("socks5://[^@\\s]+@")),
                                     QStringLiteral("socks5://[redacted]@"));
                    qDebug() << "[tun2socks]:" << safeLine;
                }

                if (line.contains("[STACK] tun://") && line.contains("<-> socks5://")) {
                    disconnect(m_tun2socksProcess.data(), &IpcProcessInterfaceReplica::readyReadStandardOutput, this, nullptr);

                    const auto result = setupRouting();
                    if (m_stopRequested || !m_sessionActive) { return; }
                    if (result != ErrorCode::NoError) { stopAfterFailure(); setLastError(result); }
                    else { setConnectionState(Vpn::ConnectionState::Connected); }
                }
            },
            Qt::QueuedConnection);

    connect(
            m_tun2socksProcess.data(), &IpcProcessInterfaceReplica::finished, this,
            [this, generation](int exitCode, QProcess::ExitStatus exitStatus) {
                if (!m_sessionActive || m_stopping || generation != m_processGeneration) { return; }
                // Check stdout for "resource busy" — the TUN device was not yet released
                // by the previous tun2socks instance. Retry after a short delay.
                bool resourceBusy = false;
                if (m_tun2socksProcess) {
                    auto readOut = m_tun2socksProcess->readAllStandardOutput();
                    if (readOut.waitForFinished()) {
                        resourceBusy = readOut.returnValue().contains("resource busy");
                    }
                }

                if (resourceBusy && m_tun2socksRetryCount < maxTun2SocksRetries) {
                    m_tun2socksRetryCount++;
                    qWarning() << QString("Tun2socks: TUN resource busy, retrying (%1/%2) in %3ms...")
                                      .arg(m_tun2socksRetryCount)
                                      .arg(maxTun2SocksRetries)
                                      .arg(tun2socksRetryDelayMs);
                    m_retryTimer.start();
                    return;
                }

                m_tun2socksRetryCount = 0;

                if (exitStatus == QProcess::ExitStatus::CrashExit) {
                    qCritical() << "Tun2socks process crashed!";
                } else {
                    qCritical() << QString("Tun2socks process was closed with %1 exit code").arg(exitCode);
                }
                stopAfterFailure();
                setLastError(ErrorCode::Tun2SockExecutableCrashed);
            },
            Qt::QueuedConnection);

    m_tun2socksProcess->start();
    return ErrorCode::NoError;
}

ErrorCode XrayProtocol::setupRouting()
{
    if (m_stopRequested || !m_sessionActive) { return ErrorCode::NoError; }
    ++m_operationDepth;
    const auto operation = qScopeGuard([this]() { finishOperation(); });
    return IpcClient::withInterface(
            [this](QSharedPointer<IpcInterfaceReplica> iface) -> ErrorCode {
#ifdef Q_OS_WIN
                const int inetAdapterIndex = NetworkUtilities::AdapterIndexTo(QHostAddress(m_remoteAddress));
                auto localCapture = QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop")
                    && m_rawConfig.contains(QStringLiteral("hopTrafficPolicy"))
                    ? hop::localSubnetCaptureRoutes(QNetworkInterface::interfaceFromIndex(inetAdapterIndex).addressEntries()) : QStringList();
                const auto systemDirect = hop::directPrivatePrefixes(m_rawConfig.value(QStringLiteral("hopTrafficPolicy")).toObject());
                localCapture.removeIf([&](const QString &prefix) { return hop::privatePrefixUsesSystemRoute(prefix, systemDirect); });
                if (QHostAddress(m_remoteAddress).protocol() == QAbstractSocket::IPv4Protocol
                    && !m_rawConfig.value(QStringLiteral("hopPolicyAwg")).toBool()) {
                    // Маршрут к серверу создаётся до маршрутов VPN, чтобы транспорт не попал в собственный TUN.
                    if (m_stopRequested) { return ErrorCode::NoError; }
                    auto bypass = iface->routeAddList(m_routeGateway, { m_remoteAddress + QStringLiteral("/32") });
                    if (!bypass.waitForFinished() || bypass.returnValue() != 1) {
                        qCritical() << "Failed to preserve the uplink route to the VPN server";
                        return ErrorCode::InternalError;
                    }
                }
                if (m_rawConfig.value(QStringLiteral("hopPolicyAwg")).toBool()) {
                    // Обходной маршрут уже принадлежит native AWG. Повтор с on-link gateway 0
                    // выбирал собственный default route AWG и замыкал внешний UDP в туннель.
                    const auto uplink = QNetworkInterface::interfaceFromIndex(inetAdapterIndex).humanReadableName();
                    if (uplink.isEmpty() || uplink == QStringLiteral(APP_WINDOWS_TUNNEL_INTERFACE) || uplink == tunName) {
                        return ErrorCode::InternalError;
                    }
                }
#endif
                if (m_stopRequested) { return ErrorCode::NoError; }
                auto createTun = iface->createTun(tunName, amnezia::protocols::xray::defaultLocalAddr);
                if (!createTun.waitForFinished() || !createTun.returnValue()) {
                    qCritical() << "Failed to assign IP address for TUN";
                    return ErrorCode::InternalError;
                }

#ifdef Q_OS_WIN
                if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop")) {
                    if (m_stopRequested) { return ErrorCode::NoError; }
                    auto protection = iface->applyTrafficGuard(trafficGuardConfig(), tunName);
                    if (!protection.waitForFinished() || !protection.returnValue()) { return ErrorCode::InternalError; }
                }
#endif

                if (m_stopRequested) { return ErrorCode::NoError; }

                auto updateResolvers = iface->updateResolvers(tunName, m_dnsServers);
                if (!updateResolvers.waitForFinished() || !updateResolvers.returnValue()) {
                    qCritical() << "Failed to set DNS resolvers for TUN";
                    return ErrorCode::InternalError;
                }

#ifdef Q_OS_WIN
                int vpnAdapterIndex = -1;
                QList<QNetworkInterface> netInterfaces = QNetworkInterface::allInterfaces();
                for (auto &netInterface : netInterfaces) {
                    for (auto &address : netInterface.addressEntries()) {
                        if (netInterface.humanReadableName() == tunName && m_vpnLocalAddress == address.ip().toString())
                            vpnAdapterIndex = netInterface.index();
                    }
                }
#else
                static const int vpnAdapterIndex = 0;
#endif
                const auto killSwitchOption = m_rawConfig.value(configKey::killSwitchOption);
                const bool killSwitchEnabled = killSwitchOption.isUndefined() || (killSwitchOption.isBool()
                    ? killSwitchOption.toBool() : QVariant(killSwitchOption.toString()).toBool());
                if (killSwitchEnabled
#ifdef Q_OS_WIN
                    && QStringLiteral(APPLICATION_NAME) != QStringLiteral("Hop")
#endif
                ) {
                    if (vpnAdapterIndex != -1) {
                        QJsonObject config = m_rawConfig;
                        config.insert("vpnServer", m_remoteAddress);

                        auto enableKillSwitch = IpcClient::Interface()->enableKillSwitch(config, vpnAdapterIndex);
                        if (!enableKillSwitch.waitForFinished() || !enableKillSwitch.returnValue()) {
                            qCritical() << "Failed to enable killswitch";
                            return ErrorCode::InternalError;
                        }
                    } else
                        qWarning() << "Failed to get vpnAdapterIndex. Killswitch disabled";
                }

                const bool hopWindows =
#ifdef Q_OS_WIN
                    QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop");
#else
                    false;
#endif
                if (m_routeMode == amnezia::RouteMode::VpnAllSites || (hopWindows && m_routeMode == amnezia::RouteMode::VpnAllExceptSites)) {
                    static const QStringList subnets = { "1.0.0.0/8",  "2.0.0.0/7",  "4.0.0.0/6",  "8.0.0.0/5",
                                                         "16.0.0.0/4", "32.0.0.0/3", "64.0.0.0/2", "128.0.0.0/1" };

                    if (m_stopRequested) { return ErrorCode::NoError; }

                    auto routeAddList = iface->routeAddList(m_vpnGateway, subnets);
                    if (!routeAddList.waitForFinished() || routeAddList.returnValue() != subnets.count()) {
                        qCritical() << "Failed to set routes for TUN";
                        return ErrorCode::InternalError;
                    }
                }
                if (hopWindows) {
#ifdef Q_OS_WIN
                    if (!localCapture.isEmpty()) {
                        if (m_stopRequested) { return ErrorCode::NoError; }
                        auto capture = iface->routeAddList(m_vpnGateway, localCapture);
                        if (!capture.waitForFinished() || capture.returnValue() != localCapture.size()) {
                            qCritical() << "Failed to capture uplink LAN for Hop routing policy";
                            return ErrorCode::InternalError;
                        }
                    }
#endif
                    QStringList sites;
                    for (const auto &value : m_rawConfig.value(configKey::splitTunnelSites).toArray()) { sites.append(value.toString()); }
                    if (!sites.isEmpty() && m_routeMode != amnezia::RouteMode::VpnAllSites) {
                        if (m_stopRequested) { return ErrorCode::NoError; }
                        auto routes = iface->routeAddList(m_routeMode == amnezia::RouteMode::VpnAllExceptSites ? m_routeGateway : m_vpnGateway, sites);
                        if (!routes.waitForFinished() || routes.returnValue() != sites.size()) { return ErrorCode::InternalError; }
                    }
                    QStringList dns;
                    for (const auto &address : m_dnsServers) { if (!address.isNull()) { dns.append(address.toString()); } }
                    if (!dns.isEmpty()) {
                        if (m_stopRequested) { return ErrorCode::NoError; }
                        auto routes = iface->routeAddList(m_vpnGateway, dns);
                        if (!routes.waitForFinished() || routes.returnValue() != dns.size()) { return ErrorCode::InternalError; }
                    }
                }

                if (!hopWindows || !killSwitchEnabled) {
                    if (m_stopRequested) { return ErrorCode::NoError; }
                    auto StopRoutingIpv6 = iface->StopRoutingIpv6();
                    if (!StopRoutingIpv6.waitForFinished() || !StopRoutingIpv6.returnValue()) {
                        qCritical() << "Failed to disable IPv6 routing";
                        return ErrorCode::InternalError;
                    }
                }

#ifdef Q_OS_WIN
                if (QStringLiteral(APPLICATION_NAME) != QStringLiteral("Hop") && inetAdapterIndex != -1 && vpnAdapterIndex != -1) {
                    QJsonObject config = m_rawConfig;
                    config.insert("inetAdapterIndex", inetAdapterIndex);
                    config.insert("vpnAdapterIndex", vpnAdapterIndex);
                    config.insert("vpnGateway", m_vpnGateway);
                    config.insert("vpnServer", m_remoteAddress);

                    if (m_stopRequested) { return ErrorCode::NoError; }

                    auto enablePeerTraffic = iface->enablePeerTraffic(config);
                    if (!enablePeerTraffic.waitForFinished() || !enablePeerTraffic.returnValue()) {
                        qCritical() << "Failed to enable peer traffic";
                        return ErrorCode::InternalError;
                    }
                } else if (QStringLiteral(APPLICATION_NAME) != QStringLiteral("Hop"))
                    qWarning() << "Failed to get adapter indexes. Split-tunneling disabled";
#endif
                return ErrorCode::NoError;
            },
            []() { return ErrorCode::AmneziaServiceConnectionFailed; });
}

#include "routedAwgProtocol.h"
#include "wireGuardProtocol.h"
#include "xrayProtocol.h"
#include "protocolUtils.h"
#include "core/utils/constants/configKeys.h"
#include "core/utils/ipcClient.h"
#include "core/utils/networkUtilities.h"
#include "core/routing/trafficPolicy.h"
#include "version.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopedValueRollback>

namespace {
std::unique_ptr<WireguardProtocol> makeTransport(QJsonObject configuration)
{
    configuration.remove(QStringLiteral("hopTrafficPolicy"));
    configuration.insert(QStringLiteral("hopPolicyTransport"), true);
    return std::make_unique<WireguardProtocol>(configuration);
}
}

RoutedAwgProtocol::RoutedAwgProtocol(const QJsonObject &configuration, QObject *parent)
    : RoutedAwgProtocol(configuration, makeTransport(configuration), parent)
{
}

RoutedAwgProtocol::RoutedAwgProtocol(const QJsonObject &configuration, std::unique_ptr<WireguardProtocol> transport, QObject *parent)
    : VpnProtocol(configuration, parent), m_transport(std::move(transport)), m_cleanupTimer(this)
{
    connect(m_transport.get(), &VpnProtocol::connectionStateChanged, this, [this](Vpn::ConnectionState state) {
        if (m_stopping) { return; }
        if (state == Vpn::Connected) { startFrontend(); }
        else if (state == Vpn::Error || (state == Vpn::Disconnected && m_started)) {
            fail(m_transport->lastError() == ErrorCode::NoError ? ErrorCode::AmneziaServiceConnectionFailed : m_transport->lastError());
        }
    });
    connect(m_transport.get(), &VpnProtocol::bytesChanged, this, &VpnProtocol::bytesChanged);
    m_cleanupTimer.setInterval(200);
    m_cleanupTimer.setSingleShot(true);
    connect(&m_cleanupTimer, &QTimer::timeout, this, &RoutedAwgProtocol::finishCleanup);
}

RoutedAwgProtocol::~RoutedAwgProtocol()
{
    m_cleanupTimer.stop();
    disconnect(m_transport.get(), nullptr, this, nullptr);
    if (m_frontend) { disconnect(m_frontend.get(), nullptr, this, nullptr); }
    // Без подтверждённой очистки обоих детей деструктор не снимает защиту.
    if (!m_clean) { m_stopping = true; if (m_frontend) { m_frontend->stopAfterFailure(); } m_transport->stopAfterFailure(); }
}

ErrorCode RoutedAwgProtocol::start()
{
    if (m_started || !m_clean) { return ErrorCode::InternalError; }
    const auto policy = hop::deserializeTrafficPolicy(m_rawConfig.value(QStringLiteral("hopTrafficPolicy")).toObject());
    if (!policy.isValid()) { return ErrorCode::InternalError; }
    m_frontend.reset(); m_cleanupAttempts = 0;
    m_started = true; m_clean = false; m_stopping = false; m_release = false;
    setConnectionState(Vpn::Connecting);
    const auto result = m_transport->start();
    if (result != ErrorCode::NoError) { fail(result); }
    return result;
}

void RoutedAwgProtocol::startFrontend()
{
    if (m_frontend || !m_started || m_stopping) { return; }
    const auto protocol = m_rawConfig.value(configKey::vpnProto).toString();
    const auto data = m_rawConfig.value(protocol + QStringLiteral("_config_data")).toObject();
    const QHostAddress local(data.value(configKey::clientIp).toString().section(QLatin1Char('/'), 0, 0));
    const auto remote = NetworkUtilities::getIPAddress(m_rawConfig.value(configKey::hostName).toString());
    if (local.protocol() != QAbstractSocket::IPv4Protocol || QHostAddress(remote).isNull()) { fail(ErrorCode::InternalError); return; }
    const QJsonObject xray {
        { QStringLiteral("inbounds"), QJsonArray { QJsonObject { { QStringLiteral("listen"), QStringLiteral("127.0.0.1") },
            { QStringLiteral("port"), 0 }, { QStringLiteral("protocol"), QStringLiteral("socks") },
            { QStringLiteral("settings"), QJsonObject { { QStringLiteral("udp"), true } } } } } },
        { QStringLiteral("outbounds"), QJsonArray { QJsonObject { { QStringLiteral("tag"), QStringLiteral("hop-vpn") },
            { QStringLiteral("protocol"), QStringLiteral("freedom") }, { QStringLiteral("sendThrough"), local.toString() },
            { QStringLiteral("streamSettings"), QJsonObject { { QStringLiteral("sockopt"), QJsonObject {
                { QStringLiteral("interface"), QStringLiteral(APP_WINDOWS_TUNNEL_INTERFACE) } } } } } } } },
        { QStringLiteral("hopPhysicalEndpoint"), remote }
    };
    auto config = m_rawConfig;
    config.insert(QStringLiteral("hopPolicyAwg"), true);
    config.insert(QStringLiteral("hopTransportPort"), data.value(configKey::port));
    config.insert(QStringLiteral("hopTransportProtocol"), QStringLiteral("udp"));
    config.insert(ProtocolUtils::key_proto_config_data(Proto::Xray), QJsonObject { { configKey::config,
        QString::fromUtf8(QJsonDocument(xray).toJson(QJsonDocument::Compact)) } });
    m_frontend = std::make_unique<XrayProtocol>(config);
    connect(m_frontend.get(), &VpnProtocol::trafficRatesChanged, this, &VpnProtocol::trafficRatesChanged);
    connect(m_frontend.get(), &VpnProtocol::connectionStateChanged, this, [this](Vpn::ConnectionState state) {
        if (m_stopping) { return; }
        if (state == Vpn::Connected) {
            m_vpnGateway = m_frontend->vpnGateway(); m_vpnLocalAddress = m_frontend->vpnLocalAddress();
            m_routeGateway = m_frontend->routeGateway();
            emit tunnelAddressesUpdated(m_vpnGateway, m_vpnLocalAddress); setConnectionState(Vpn::Connected);
        } else if (state == Vpn::Error || state == Vpn::Disconnected) {
            fail(m_frontend->lastError() == ErrorCode::NoError ? ErrorCode::XrayExecutableCrashed : m_frontend->lastError());
        }
    });
    const auto result = m_frontend->start();
    if (result != ErrorCode::NoError) { fail(result); }
}

void RoutedAwgProtocol::fail(ErrorCode error) { cleanup(false); setLastError(error); }
void RoutedAwgProtocol::stop() { cleanup(true); }
void RoutedAwgProtocol::stopAfterFailure() { cleanup(false); }
bool RoutedAwgProtocol::isCleanupComplete() const { return m_clean; }
bool RoutedAwgProtocol::updateTrafficProtection(bool enabled)
{
    const bool applied = m_frontend ? m_frontend->updateTrafficProtection(enabled) : m_transport->updateTrafficProtection(enabled);
    if (applied) { m_rawConfig.insert(configKey::killSwitchOption, enabled); }
    return applied;
}
void RoutedAwgProtocol::cleanup(bool release)
{
    if (m_clean && (m_release || !release)) { return; }
    m_release = m_release || release; m_started = false; m_stopping = true;
    finishCleanup();
}
void RoutedAwgProtocol::finishCleanup()
{
    if (!m_stopping || m_cleanupInProgress) { return; }
    {
        // IPC ожидает ответ с обработкой событий: таймер и повторный stop не должны входить в очистку рекурсивно.
        QScopedValueRollback<bool> cleaning(m_cleanupInProgress, true);
        m_cleanupTimer.stop();
        if (m_frontend) { m_frontend->stopAfterFailure(); }
        m_transport->stopAfterFailure();
        bool complete = (!m_frontend || m_frontend->isCleanupComplete()) && m_transport->isCleanupComplete();
        if (complete && m_release) {
            complete = IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) {
                auto reply = iface->clearTrafficGuard(); return reply.waitForFinished(5000) && reply.returnValue();
            }, []() { return false; });
        }
        if (!complete) {
            ++m_cleanupAttempts; m_cleanupTimer.setInterval(m_cleanupAttempts < 50 ? 200 : 1000); m_cleanupTimer.start(); return;
        }
        m_clean = true;
    }
    setConnectionState(Vpn::Disconnected);
}

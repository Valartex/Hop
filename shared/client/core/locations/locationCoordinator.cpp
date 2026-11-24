#include "locationCoordinator.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkInformation>
#include <QScopedValueRollback>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif
#include "core/utils/constants/apiKeys.h"
#include "core/utils/constants/configKeys.h"
#include "controlPlaneResolver.h"
#include "apiCatalogSnapshot.h"

namespace hop {
namespace {
#ifdef Q_OS_ANDROID
QString physicalNetworkIdentity()
{
    return QJniObject::callStaticObjectMethod("org/amnezia/vpn/HopNetworkTasks", "networkIdentity", "()Ljava/lang/String;").toString();
}
#endif

bool hasNetworkForVpn()
{
#ifdef Q_OS_ANDROID
    // Ограниченная сеть может не пройти проверку интернета Android и всё же пропускать трафик к VPN.
    return !physicalNetworkIdentity().isEmpty();
#else
    const auto *network = QNetworkInformation::instance();
    return !network || network->reachability() != QNetworkInformation::Reachability::Disconnected;
#endif
}
}

LocationCoordinator::LocationCoordinator(LocationCatalog *catalog, SecureServersRepository *servers, SecureAppSettingsRepository *appSettings,
    ConnectionController *connection, VpnConnection *vpn, QObject *parent, const QString &providerPath, const QString &probePath, SelectionSession::Timing timing)
    : SelectionSession(catalog, timing, parent), m_catalog(catalog), m_servers(servers), m_appSettings(appSettings), m_connection(connection), m_vpn(vpn),
      m_providerPath(providerPath.isEmpty() ? platformTaskPath(QStringLiteral("provider")) : providerPath),
      m_probePath(probePath.isEmpty() ? platformTaskPath(QStringLiteral("hop_tunnel_probe")) : probePath)
{
#ifndef Q_OS_ANDROID
    QNetworkInformation::loadDefaultBackend();
    if (auto *network = QNetworkInformation::instance()) {
        const auto update = [this]() { setNetworkOnline(hasNetworkForVpn()); };
        connect(network, &QNetworkInformation::reachabilityChanged, this, update);
        update();
    }
#endif
    m_clock.start();
#ifdef Q_OS_ANDROID
    m_networkIdentity = physicalNetworkIdentity();
    setNetworkOnline(!m_networkIdentity.isEmpty());
    m_networkChangeTimer.setInterval(2000);
    connect(&m_networkChangeTimer, &QTimer::timeout, this, [this]() {
        const auto current = physicalNetworkIdentity();
        if (current == m_networkIdentity) { return; }
        m_networkIdentity = current;
        setNetworkOnline(!current.isEmpty());
        emit physicalNetworkChanged();
        networkChanged();
    });
    m_networkChangeTimer.start();
#endif
    connect(vpn, &VpnConnection::trafficRatesChanged, this, [this](double down, double up, double directDown, double directUp) {
        if (!running() || !m_connection->isConnected()) { return; }
        m_trafficRates = {{"vpnDown", down}, {"vpnUp", up}, {"directDown", directDown}, {"directUp", directUp}};
        emit trafficRatesChanged();
    }, Qt::QueuedConnection);
    connect(connection, &ConnectionController::connectionStateChanged, this, [this](Vpn::ConnectionState state) {
        if (state != Vpn::Connected) { m_trafficRates.clear(); emit trafficRatesChanged(); }
    });
    m_statusTimer.setInterval(30000);
    connect(&m_statusTimer, &QTimer::timeout, this, [this]() {
        bool updated = false;
        for (auto it = m_statuses.begin(); it != m_statuses.end(); ++it) {
            auto result = it.value().toMap();
            const auto state = result.value(QStringLiteral("state")).toString();
            if ((state == QStringLiteral("reachable") || state == QStringLiteral("unreachable"))
                && m_clock.elapsed() - m_sampledAt.value(it.key()) >= 120000) {
                result.insert(QStringLiteral("state"), QStringLiteral("stale")); it.value() = result; updated = true;
            }
        }
        if (updated) { emit statusesChanged(); }
    });
    m_statusTimer.start();
    connect(catalog, &LocationCatalog::catalogChanged, this, [this]() {
        for (auto it = m_statuses.begin(); it != m_statuses.end();) {
            if (!m_catalog->entry(it.key())) { m_sampledAt.remove(it.key()); it = m_statuses.erase(it); }
            else { ++it; }
        }
        emit statusesChanged();
    });
    m_catalogTimer.setInterval(6 * 60 * 60 * 1000);
    connect(&m_catalogTimer, &QTimer::timeout, this, &LocationCoordinator::refreshCatalogs);
    m_catalogTimer.start();
    QTimer::singleShot(2000, this, [this]() { if (m_catalogTimer.isActive()) { refreshCatalogs(); } });
    connect(servers, &SecureServersRepository::serverAdded, this, [this](const QString &id) {
        if (m_servers->apiV2Config(id) && !m_catalogQueue.contains(id)) { m_catalogQueue.append(id); emit catalogRefreshChanged(); drainCatalog(); }
    });
    connect(this, &SelectionSession::changed, this, [this]() { QTimer::singleShot(0, this, &LocationCoordinator::drainCatalog); });
    connect(this, &SelectionSession::attemptRequested, this, &LocationCoordinator::attempt);
    connect(this, &SelectionSession::healthRequested, this, &LocationCoordinator::check);
    connect(this, &SelectionSession::suspendRequested, this, [this](const QString &value) {
        cancelProcesses();
        QMetaObject::invokeMethod(m_vpn, "suspendForSelection", Qt::QueuedConnection, Q_ARG(QString, value));
    });
    connect(vpn, &VpnConnection::selectionSuspended, this, &SelectionSession::suspended, Qt::QueuedConnection);
    connect(vpn, &VpnConnection::attemptRecoveryRequested, this, [this](const QString &value) {
        if (running() && value == token()) { restartCurrent(); }
    }, Qt::QueuedConnection);
    connect(vpn, &VpnConnection::attemptStateChanged, this, [this](const QString &value, Vpn::ConnectionState state) {
        if (state == Vpn::Connected) { transportConnected(value); }
        else if (state == Vpn::Error) { transportFailed(value, tr("Ошибка VPN: %1").arg(int(m_connection->lastConnectionError()))); }
        else if (state == Vpn::Disconnected) { transportFailed(value, tr("VPN завершил подключение без установленного соединения")); }
    }, Qt::QueuedConnection);
    connect(this, &SelectionSession::disconnectRequested, this, [this]() {
        cancelProcesses();
        QScopedValueRollback<bool> internal(m_internalDisconnect, true);
        m_connection->closeConnection();
    });
    connect(this, &SelectionSession::changed, this, [this]() {
        if (!running() && state() == Stopped && !status().isEmpty() && status() != m_reportedPreparationError) {
            m_reportedPreparationError = status(); emit preparationFailed(status());
        }
    });
    connect(connection, &ConnectionController::closeConnectionRequested, this, [this]() {
        if (!m_internalDisconnect && running()) { stop(); }
    });
    connect(connection, &ConnectionController::connectionStateChanged, this, [this](Vpn::ConnectionState state) {
        m_transportBusy = state == Vpn::Preparing || state == Vpn::Connecting
            || state == Vpn::Disconnecting || state == Vpn::Reconnecting;
    });
    connect(this, &SelectionSession::locationHealth, this, [this](const QString &id, const QString &state) {
        m_sampledAt.insert(id, m_clock.elapsed());
        auto result = m_statuses.value(id).toMap(); result.insert(QStringLiteral("state"), state);
        m_statuses.insert(id, result); emit statusesChanged();
    });
}
LocationCoordinator::~LocationCoordinator() { cancelProcesses(); }
void LocationCoordinator::cancelProcesses()
{
    if (m_catalogProcess && !m_catalogCurrent.isEmpty() && !m_catalogQueue.contains(m_catalogCurrent)) { m_catalogQueue.prepend(m_catalogCurrent); }
    for (auto process : { m_provider, m_probe, m_catalogProcess }) {
        if (!process) { continue; }
        disconnect(process, nullptr, this, nullptr);
        process->kill(); process->waitForFinished(1000); process->deleteLater();
    }
    m_provider = nullptr; m_probe = nullptr; m_catalogProcess = nullptr; m_catalogCurrent.clear(); emit catalogRefreshChanged();
}
void LocationCoordinator::toggleConnection()
{
    if (running() || m_connection->isConnected() || m_transportBusy) { disconnectManually(); }
    else { m_reportedPreparationError.clear(); start(); }
}
void LocationCoordinator::disconnectManually() { stop(); }
void LocationCoordinator::saveGatewayState(const QJsonObject &response)
{
    const auto state = QByteArray::fromBase64(response.value(QStringLiteral("gatewayState")).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!state.isEmpty() && state.size() <= 256 * 1024 && QJsonDocument::fromJson(state).isObject()) {
        m_appSettings->writeGatewayProxyUrls(QStringLiteral("agw_state"), state);
    }
}

void LocationCoordinator::refreshCatalogs()
{
    for (const auto &id : m_servers->orderedServerIds()) {
        if (m_servers->apiV2Config(id) && id != m_catalogCurrent && !m_catalogQueue.contains(id)) { m_catalogQueue.append(id); }
    }
    emit catalogRefreshChanged();
    drainCatalog();
}

void LocationCoordinator::refreshCatalog(const QString &id)
{
    if (m_servers->apiV2Config(id) && id != m_catalogCurrent && !m_catalogQueue.contains(id)) { m_catalogQueue.append(id); }
    emit catalogRefreshChanged();
    drainCatalog();
}

void LocationCoordinator::drainCatalog()
{
    if (!hasNetworkForVpn()) { return; }
    if (m_catalogProcess || m_provider || state() == Preparing || state() == Suspending || state() == Verifying || state() == Selecting || m_catalogQueue.isEmpty()) { return; }
    const auto id = m_catalogQueue.takeFirst();
    const auto api = m_servers->apiV2Config(id);
    if (!api) { QTimer::singleShot(0, this, &LocationCoordinator::drainCatalog); emit catalogRefreshChanged(); return; }
    const auto auth = QCryptographicHash::hash(QJsonDocument(api->authData.toJson()).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
    const QJsonObject request { { QStringLiteral("action"), QStringLiteral("catalog") }, { QStringLiteral("server"), api->toJson() },
        { QStringLiteral("installation"), m_appSettings->getInstallationUuid(true) }, { QStringLiteral("gateway"), m_appSettings->getGatewayEndpoint() },
        { QStringLiteral("strict"), m_appSettings->isStrictKillSwitchEnabled() },
        { QStringLiteral("gatewayState"), QString::fromLatin1(m_appSettings->readGatewayProxyUrls(QStringLiteral("agw_state")).toBase64()) } };
    auto *process = new PlatformTask(this); m_catalogProcess = process; m_catalogCurrent = id;
    m_catalogStatuses.insert(id, QVariantMap { { QStringLiteral("state"), QStringLiteral("loading") },
        { QStringLiteral("message"), tr("Загрузка локаций…") } });
    process->setProgram(m_providerPath); process->setArguments({ QStringLiteral("--provider-task") });
    process->setProcessEnvironment(controlPlane::environment());
    const auto input = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    const auto collected = QSharedPointer<QByteArray>::create();
    connect(process, &QProcess::started, this, [process, input]() { process->write(input); process->closeWriteChannel(); });
    connect(process, &QProcess::readyReadStandardError, this, [process]() { process->readAllStandardError(); });
    connect(process, &QProcess::readyReadStandardOutput, this, [process, collected]() {
        const auto bytes = process->readAllStandardOutput();
        if (collected->size() + bytes.size() > 2 * 1024 * 1024) { collected->clear(); process->kill(); }
        else { collected->append(bytes); }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, id](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) { return; }
        if (m_catalogProcess == process) { m_catalogProcess = nullptr; m_catalogCurrent.clear(); }
        m_catalogStatuses.insert(id, QVariantMap { { QStringLiteral("state"), QStringLiteral("error") },
            { QStringLiteral("message"), tr("Не удалось запустить обновление локаций.") } });
        process->deleteLater(); emit catalogRefreshChanged(); emit catalogRefreshFinished(id, false); QTimer::singleShot(500, this, &LocationCoordinator::drainCatalog);
    });
    connect(process, &QProcess::finished, this, [this, process, collected, id, auth](int code, QProcess::ExitStatus status) {
        collected->append(process->readAllStandardOutput());
        if (m_catalogProcess == process) { m_catalogProcess = nullptr; m_catalogCurrent.clear(); }
        process->deleteLater();
        const auto response = collected->size() <= 2 * 1024 * 1024 ? QJsonDocument::fromJson(*collected).object() : QJsonObject();
        if (status == QProcess::NormalExit) { saveGatewayState(response); }
        auto current = m_servers->apiV2Config(id);
        const auto account = response.value(QStringLiteral("account")).toObject();
        QString catalogError;
        const auto countries = readApiCountries(account, catalogError);
        bool valid = code == 0 && status == QProcess::NormalExit && response.value(QStringLiteral("code")).toInt(-1) == 0
            && current && !countries.isEmpty() && countries.size() <= 2048
            && QCryptographicHash::hash(QJsonDocument(current->authData.toJson()).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256) == auth;
        valid = valid && catalogError.isEmpty();
        if (valid) {
            current->apiConfig.availableCountries = countries;
            if (account.value(apiDefs::key::activeDeviceCount).isDouble()) { current->apiConfig.activeDeviceCount = account.value(apiDefs::key::activeDeviceCount).toInt(); }
            if (account.value(apiDefs::key::maxDeviceCount).isDouble()) { current->apiConfig.maxDeviceCount = account.value(apiDefs::key::maxDeviceCount).toInt(); }
            const auto end = account.value(apiDefs::key::subscriptionEndDate).toString();
            if (!end.isEmpty()) { current->apiConfig.subscription.endDate = end; }
            m_servers->editServer(id, current->toJson(), m_servers->serverKind(id));
        }
        const auto responseCode = response.value(QStringLiteral("code")).toInt(-1);
        const auto message = valid ? tr("Локации обновлены: %1").arg(countries.size())
            : responseCode < 0 ? tr("Не удалось получить список локаций. Прежний список сохранён; повторите обновление.")
            : responseCode > 0 ? tr("Не удалось обновить локации, код %1. Прежний список сохранён.").arg(responseCode)
            : !catalogError.isEmpty() ? catalogError : tr("Подписка изменилась во время обновления. Повторите обновление.");
        m_catalogStatuses.insert(id, QVariantMap { { QStringLiteral("state"), valid ? QStringLiteral("ready") : QStringLiteral("error") },
            { QStringLiteral("message"), message }, { QStringLiteral("code"), responseCode },
            { QStringLiteral("updatedAt"), valid ? QDateTime::currentDateTimeUtc().toString(Qt::ISODate) : QString() } });
        emit catalogRefreshChanged();
        emit catalogRefreshFinished(id, valid);
        QTimer::singleShot(500, this, &LocationCoordinator::drainCatalog);
    });
    QTimer::singleShot(90000, process, [process]() { if (process->state() != QProcess::NotRunning) { process->kill(); } });
    emit catalogRefreshChanged(); process->start();
}

void LocationCoordinator::attempt(const QString &id, const QString &value)
{
    cancelProcesses();
    const auto location = m_catalog->entry(id);
    if (!location || !location->enabled) { transportFailed(value, tr("Локация удалена или недоступна")); return; }
    if (!m_connection->isServiceReady()) { transportFailed(value, tr("Служба Hop недоступна")); return; }
    m_catalog->select(id);
    m_statuses.insert(id, QVariantMap { { QStringLiteral("state"), QStringLiteral("checking") }, { QStringLiteral("latencyMs"), -1 } });
    emit statusesChanged();
    const auto api = m_servers->apiV2Config(location->serverId);
    if (!api) { open(id, value); return; }
    const auto original = QJsonDocument(api->toJson()).toJson(QJsonDocument::Compact);
    const auto fingerprint = QCryptographicHash::hash(original, QCryptographicHash::Sha256);
    const QJsonObject request { { QStringLiteral("action"), QStringLiteral("prepare") }, { QStringLiteral("server"), api->toJson() },
        { QStringLiteral("country"), location->apiCountry }, { QStringLiteral("installation"), m_appSettings->getInstallationUuid(true) },
        { QStringLiteral("gateway"), m_appSettings->getGatewayEndpoint() }, { QStringLiteral("strict"), m_appSettings->isStrictKillSwitchEnabled() },
        { QStringLiteral("gatewayState"), QString::fromLatin1(m_appSettings->readGatewayProxyUrls(QStringLiteral("agw_state")).toBase64()) } };
    auto *process = new PlatformTask(this); m_provider = process;
    process->setProgram(m_providerPath); process->setArguments({ QStringLiteral("--provider-task") });
    process->setProcessEnvironment(controlPlane::environment());
    const auto bytes = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    const auto collected = QSharedPointer<QByteArray>::create();
    connect(process, &QProcess::started, this, [process, bytes]() { process->write(bytes); process->closeWriteChannel(); });
    connect(process, &QProcess::readyReadStandardError, this, [process]() { process->readAllStandardError(); });
    connect(process, &QProcess::readyReadStandardOutput, this, [process, collected]() {
        const auto bytes = process->readAllStandardOutput();
        if (collected->size() + bytes.size() > 2 * 1024 * 1024) { collected->clear(); process->kill(); }
        else { collected->append(bytes); }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, value](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) { return; }
        if (m_provider == process) { m_provider = nullptr; }
        process->deleteLater(); transportFailed(value, tr("Не удалось запустить подготовку конфигурации"));
    });
    connect(process, &QProcess::finished, this, [this, process, collected, location = *location, value, fingerprint](int exitCode, QProcess::ExitStatus status) {
        collected->append(process->readAllStandardOutput());
        if (m_provider == process) { m_provider = nullptr; }
        process->deleteLater();
        if (!running() || value != token()) { return; }
        const auto result = collected->size() <= 2 * 1024 * 1024 ? QJsonDocument::fromJson(*collected).object() : QJsonObject();
        if (status == QProcess::NormalExit) { saveGatewayState(result); }
        if (exitCode != 0 || status != QProcess::NormalExit || result.value(QStringLiteral("code")).toInt(-1) != 0) {
            const auto current = m_servers->apiV2Config(location.serverId);
            const auto message = providerFailureMessage(result.value(QStringLiteral("code")).toInt(-1),
                result.value(QStringLiteral("captchaRequired")).toBool(), current ? current->apiConfig.activeDeviceCount : 0,
                current ? current->apiConfig.maxDeviceCount : 0);
            if (message != m_reportedPreparationError) {
                m_reportedPreparationError = message;
                emit preparationFailed(message);
            }
            if (result.value(QStringLiteral("code")).toInt(-1) == int(ErrorCode::ApiConfigLimitError)) {
                m_statuses.insert(location.id, QVariantMap { { QStringLiteral("state"), QStringLiteral("configuration-limit") },
                    { QStringLiteral("latencyMs"), -1 } });
                emit statusesChanged();
                if (!m_catalog->automatic()) {
                    stopWithFailure(message);
                    return;
                }
            }
            transportFailed(value, message);
            return;
        }
        m_reportedPreparationError.clear();
        const auto current = m_servers->apiV2Config(location.serverId);
        if (!current || QCryptographicHash::hash(QJsonDocument(current->toJson()).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256) != fingerprint) {
            transportFailed(value, tr("Подписка изменилась во время подготовки")); return;
        }
        const auto server = result.value(QStringLiteral("server")).toObject();
        if (!serverConfigUtils::isConfigFormatVersionSupported(server) || serverConfigUtils::configTypeFromJson(server) != m_servers->serverKind(location.serverId)) {
            transportFailed(value, tr("Сервис вернул неподдерживаемую конфигурацию")); return;
        }
        m_servers->editServer(location.serverId, server, m_servers->serverKind(location.serverId));
        QString target = location.id;
        if (!m_catalog->entry(target)) {
            const auto api = m_servers->apiV2Config(location.serverId);
            for (const auto &candidate : m_catalog->entries()) {
                if (api && candidate.serverId == location.serverId && candidate.apiCountry == api->apiConfig.serverCountryCode) { target = candidate.id; break; }
            }
            retarget(value, target);
        }
        open(target, value);
    });
    QTimer::singleShot(90000, process, [process]() { if (process->state() != QProcess::NotRunning) { process->kill(); } });
    process->start();
}

void LocationCoordinator::open(const QString &id, const QString &value, const QString &resolvedEndpoint)
{
    if (!running() || value != token()) { return; }
    const auto location = m_catalog->entry(id);
    if (!location || !m_catalog->select(id)) { transportFailed(value, tr("Локация больше недоступна")); return; }
    QJsonObject config;
    DockerContainer container = DockerContainer::None;
    const auto prepared = m_connection->prepareConnection(location->serverId, config, container);
    if (prepared != ErrorCode::NoError) { transportFailed(value, tr("Подготовка VPN не удалась, код %1").arg(int(prepared))); return; }
    const auto host = config.value(configKey::hostName).toString();
    if (resolvedEndpoint.isEmpty() && QHostAddress(host).isNull()) {
        auto *process = new PlatformTask(this); m_provider = process;
        process->setProgram(m_providerPath); process->setArguments({ QStringLiteral("--provider-task") });
        const auto input = QJsonDocument(QJsonObject { { QStringLiteral("action"), QStringLiteral("resolve") },
            { QStringLiteral("host"), host } }).toJson(QJsonDocument::Compact) + '\n';
        const auto output = QSharedPointer<QByteArray>::create();
        connect(process, &QProcess::started, this, [process, input]() { process->write(input); process->closeWriteChannel(); });
        connect(process, &QProcess::readyReadStandardError, this, [process]() { process->readAllStandardError(); });
        connect(process, &QProcess::readyReadStandardOutput, this, [process, output]() {
            const auto bytes = process->readAllStandardOutput();
            if (bytes.size() + output->size() > 1024) { output->clear(); process->kill(); }
            else { output->append(bytes); }
        });
        connect(process, &QProcess::errorOccurred, this, [this, process, value](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart) { return; }
            if (m_provider == process) { m_provider = nullptr; }
            process->deleteLater(); transportFailed(value, tr("Не удалось запустить определение адреса VPN"));
        });
        connect(process, &QProcess::finished, this, [this, process, output, id, value, host](int code, QProcess::ExitStatus status) {
            output->append(process->readAllStandardOutput());
            if (m_provider == process) { m_provider = nullptr; }
            process->deleteLater();
            if (!running() || value != token()) { return; }
            const auto response = output->size() <= 1024 ? QJsonDocument::fromJson(*output).object() : QJsonObject();
            const auto addresses = response.value(QStringLiteral("addresses")).toArray();
            const QHostAddress address(addresses.isEmpty() ? QString() : addresses.first().toString());
            const auto current = m_catalog->entry(id);
            QJsonObject config; DockerContainer container = DockerContainer::None;
            if (code != 0 || status != QProcess::NormalExit || response.value(QStringLiteral("code")).toInt(-1) != 0
                || address.protocol() != QAbstractSocket::IPv4Protocol || !current
                || m_connection->prepareConnection(current->serverId, config, container) != ErrorCode::NoError
                || config.value(configKey::hostName).toString() != host) {
                transportFailed(value, tr("Не удалось определить актуальный IPv4-адрес VPN")); return;
            }
            open(id, value, address.toString());
        });
        QTimer::singleShot(20000, process, [process]() { if (process->state() != QProcess::NotRunning) { process->kill(); } });
        process->start();
        return;
    }
    m_resolver = config.value(configKey::dns1).toString();
    const auto protocol = ContainerUtils::defaultProtocol(container);
    m_interface = protocol == Proto::Awg || protocol == Proto::WireGuard ? QStringLiteral("Hop") : QStringLiteral("Hop-TUN");
    if (m_connection->usesRoutingFrontend()) { m_interface = QStringLiteral("Hop-TUN"); m_resolver = QStringLiteral("198.18.0.1"); }
    transportStarting(value);
    const auto opened = m_connection->openConnection(location->serverId, value, resolvedEndpoint);
    if (opened != ErrorCode::NoError) { transportFailed(value, tr("Подключение не запущено, код %1").arg(int(opened))); }
}

bool LocationCoordinator::checkCurrentLocation(const QString &id)
{
    if (!running() || activeId() != id || (state() != Healthy && state() != Verifying)) { return false; }
    check(id, token()); return true;
}

void LocationCoordinator::forgetInactiveHealth(const QString &id)
{
    if (running() && activeId() == id) { return; }
    m_sampledAt.remove(id);
    if (m_statuses.remove(id)) { emit statusesChanged(); }
}

void LocationCoordinator::check(const QString &id, const QString &value)
{
    if (!running() || value != token() || m_probe) { return; }
    if (!hasNetworkForVpn()) {
        healthResult(value, false, false); return;
    }
    if (!platformTaskAvailable(m_probePath)) { healthResult(value, false, true, false); return; }
    auto *process = new PlatformTask(this); m_probe = process;
    process->setProgram(m_probePath);
    const auto bytes = QJsonDocument(QJsonObject { { QStringLiteral("interface"), m_interface },
        { QStringLiteral("resolver"), m_resolver },
        { QStringLiteral("https"), m_interface == QStringLiteral("Hop-TUN") && !m_connection->usesRoutingFrontend() }
    }).toJson(QJsonDocument::Compact) + '\n';
    connect(process, &QProcess::started, this, [process, bytes]() { process->write(bytes); process->closeWriteChannel(); });
    connect(process, &QProcess::readyReadStandardError, this, [process]() { process->readAllStandardError(); });
    connect(process, &QProcess::errorOccurred, this, [this, process, value](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) { return; }
        if (m_probe == process) { m_probe = nullptr; }
        process->deleteLater(); healthResult(value, false, true, false);
    });
    connect(process, &QProcess::finished, this, [this, process, id, value](int exitCode, QProcess::ExitStatus status) {
        const auto bytes = process->readAllStandardOutput();
        if (m_probe == process) { m_probe = nullptr; }
        process->deleteLater();
        if (!running() || value != token()) { return; }
        const auto result = bytes.size() <= 1024 ? QJsonDocument::fromJson(bytes).object() : QJsonObject();
        const bool supported = exitCode != 4 || !result.contains(QStringLiteral("supported")) || result.value(QStringLiteral("supported")).toBool();
        const int latency = result.value(QStringLiteral("latencyMs")).toInt(-1);
        const bool healthy = exitCode == 0 && status == QProcess::NormalExit && result.value(QStringLiteral("ok")).toBool()
            && latency >= 0 && latency <= 10000;
        m_sampledAt.insert(id, m_clock.elapsed());
        m_statuses.insert(id, QVariantMap { { QStringLiteral("state"), !supported ? QStringLiteral("unsupported") : healthy ? QStringLiteral("reachable") : QStringLiteral("unreachable") },
            { QStringLiteral("latencyMs"), healthy ? latency : -1 } });
        emit statusesChanged();
        if (supported) { emit locationSampled(id, healthy ? latency : -1); }
        healthResult(value, healthy, true, supported, healthy ? latency : -1);
    });
    QTimer::singleShot(10000, process, [process]() { if (process->state() != QProcess::NotRunning) { process->kill(); } });
    process->start();
}
}

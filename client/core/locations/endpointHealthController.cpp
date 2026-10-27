#include "endpointHealthController.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QUrl>

namespace hop {
EndpointHealthController::EndpointHealthController(SecureServersRepository *servers, QObject *parent, const QString &probePath, const QString &awgProbePath, const QString &historyDirectory)
    : QObject(parent), m_servers(servers),
      m_probePath(probePath.isEmpty() ? QCoreApplication::applicationDirPath() + QStringLiteral("/hop_endpoint_probe.exe") : probePath),
      m_awgProbePath(!awgProbePath.isEmpty() ? awgProbePath : probePath.isEmpty()
          ? QCoreApplication::applicationDirPath() + QStringLiteral("/hop_awg_probe.exe") : probePath + QStringLiteral(".awg"))
{
    if (!historyDirectory.isEmpty()) {
        m_store = new LocationHistoryStore(historyDirectory, this);
        connect(m_store, &LocationHistoryStore::changed, this, &EndpointHealthController::historyChanged);
    }
    m_clock.start();
    m_autoRefresh.setObjectName(QStringLiteral("hopHealthRefreshTimer"));
    m_autoRefresh.setParent(this);
    connect(&m_autoRefresh, &QTimer::timeout, this, &EndpointHealthController::scheduledCheckRequested);
    m_freshness.setInterval(30000);
    connect(&m_freshness, &QTimer::timeout, this, [this]() {
        bool updated = false;
        for (auto &result : m_results) {
            if ((result.state == QStringLiteral("reachable") || result.state == QStringLiteral("unreachable"))
                && m_clock.elapsed() - result.sampledAt >= 120000) {
                result.state = QStringLiteral("stale"); updated = true;
            }
        }
        if (updated) { emit changed(); }
    });
    connect(servers, &SecureServersRepository::serversBatchChanged, this, &EndpointHealthController::invalidate);
    connect(servers, &SecureServersRepository::serverAdded, this, &EndpointHealthController::invalidate);
    connect(servers, &SecureServersRepository::serverEdited, this, &EndpointHealthController::invalidate);
    connect(servers, &SecureServersRepository::serverRemoved, this, &EndpointHealthController::invalidate);
}

EndpointHealthController::~EndpointHealthController() { cancel(); }
bool EndpointHealthController::available() const { return QFileInfo::exists(m_probePath) || QFileInfo::exists(m_awgProbePath); }
bool EndpointHealthController::canCheckLocation(const QString &id) const
{
    bool awg = false;
    return !probeConfiguration(id, awg).isEmpty() && (!awg || !m_tunnelActive)
        && QFileInfo::exists(awg ? m_awgProbePath : m_probePath);
}

QVariantMap EndpointHealthController::statuses() const
{
    QVariantMap result;
    for (auto it = m_results.cbegin(); it != m_results.cend(); ++it) {
        result.insert(it.key(), QVariantMap { { QStringLiteral("state"), it->state },
                                              { QStringLiteral("latencyMs"), it->milliseconds } });
    }
    return result;
}

QVariantMap EndpointHealthController::history(const QString &id, int minutes)
{
    const auto span = minutes > 0 ? qMin(minutes, 10080) * qint64(60000) : LocationHealthHistory::retentionMs;
    const auto until = QDateTime::currentMSecsSinceEpoch();
    const auto since = until - span;
    return m_store ? m_store->statistics(id, since, until) : m_history.statistics(id, since, until);
}
void EndpointHealthController::recordSample(const QString &id, int latency)
{
    if (!m_historyAll && !m_historyIds.contains(id)) { return; }
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (m_store) { m_store->record(id, latency, now); } else { m_history.record(id, latency, now); emit historyChanged(); }
}
bool EndpointHealthController::checkLocation(const QString &id)
{
    const bool accepted = enqueueCheck(id); dispatch(); emit changed(); return accepted;
}
bool EndpointHealthController::enqueueCheck(const QString &id)
{
    const auto native = m_servers->nativeConfig(id);
    const auto separator = id.indexOf('|');
    if (!native && (separator <= 0 || !m_servers->apiV2Config(id.left(separator)))) { return false; }
    if (m_queue.contains(id) || m_active.values().contains(id)) { return true; }
    bool awg = false; const auto config = probeConfiguration(id, awg);
    // Для страны без выданного профиля не создаём ни конфигурацию, ни ложный замер.
    if (!native && config.isEmpty()) { return false; }
    if (config.isEmpty() || !QFileInfo::exists(awg ? m_awgProbePath : m_probePath)) {
        m_results[id] = { QStringLiteral("unsupported") }; emit changed(); return false;
    }
    if (awg && m_tunnelActive) { m_results[id] = { QStringLiteral("deferred") }; emit changed(); return false; }
    m_queue.enqueue(id); m_results[id] = { QStringLiteral("checking") }; return true;
}

void EndpointHealthController::cancel()
{
    m_queue.clear();
    const auto processes = m_active.keys();
    m_active.clear();
    m_awgActive.clear();
    for (auto *process : processes) {
        disconnect(process, nullptr, this, nullptr);
        process->kill();
        process->waitForFinished(1000);
        process->deleteLater();
    }
    for (auto &result : m_results) {
        if (result.state == QStringLiteral("checking")) { result = { QStringLiteral("unknown") }; }
    }
    emit changed();
}

void EndpointHealthController::invalidate()
{
    cancel();
    m_results.clear();
    m_freshness.stop();
    emit changed();
}

void EndpointHealthController::checkAll()
{
    QStringList ids;
    for (const auto &id : m_servers->orderedServerIds()) {
        const auto api = m_servers->apiV2Config(id);
        ids.append(api ? id + '|' + QString::fromLatin1(QUrl::toPercentEncoding(api->apiConfig.serverCountryCode)) : id);
    }
    checkLocations(ids);
}

void EndpointHealthController::checkLocations(const QStringList &ids)
{
    if (isBusy() || !available()) { return; }
    for (const auto &id : ids) { enqueueCheck(id); }
    dispatch();
    emit changed();
}

void EndpointHealthController::dispatch()
{
    while (m_active.size() < 2 && !m_queue.isEmpty()) {
        const auto id = m_queue.head();
        bool awg = false;
        const auto config = probeConfiguration(id, awg);
        if (awg && !m_awgActive.isEmpty()) { return; }
        m_queue.dequeue();
        if (config.isEmpty()) { continue; }
        if (awg && m_tunnelActive) { m_results[id] = { QStringLiteral("deferred") }; continue; }
        auto *process = new QProcess(this);
        m_active.insert(process, id);
        if (awg) { m_awgActive.insert(process); }
        process->setProgram(awg ? m_awgProbePath : m_probePath);
        process->setProcessChannelMode(QProcess::SeparateChannels);
        connect(process, &QProcess::started, this, [process, config]() {
            process->write(config + '\n');
            process->closeWriteChannel();
        });
        connect(process, &QProcess::readyReadStandardError, this, [process]() { process->readAllStandardError(); });
        connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) { completed(process, code, status); });
        connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) { completed(process, -1, QProcess::CrashExit); }
        });
        QTimer::singleShot(15000, process, [this, process]() { if (m_active.contains(process)) { process->kill(); } });
        process->start();
    }
}

void EndpointHealthController::completed(QProcess *process, int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!m_active.contains(process)) { return; }
    const auto id = m_active.take(process);
    m_awgActive.remove(process);
    const auto output = process->readAllStandardOutput();
    const auto response = output.size() <= 1024 ? QJsonDocument::fromJson(output).object() : QJsonObject();
    const auto latency = response.value(QStringLiteral("latencyMs")).toInt(-1);
    const bool ok = exitCode == 0 && exitStatus == QProcess::NormalExit
        && response.value(QStringLiteral("ok")).toBool() && latency >= 0 && latency <= 15000;
    const bool unsupported = exitStatus == QProcess::NormalExit && exitCode == 4 && response.value(QStringLiteral("unsupported")).toBool();
    m_results[id] = { ok ? QStringLiteral("reachable") : unsupported ? QStringLiteral("unsupported") : QStringLiteral("unreachable"), ok ? latency : -1, m_clock.elapsed() };
    if (!unsupported) { recordSample(id, ok ? latency : -1); }
    disconnect(process, nullptr, this, nullptr);
    process->deleteLater();
    m_freshness.start();
    dispatch();
    emit changed();
}

QByteArray EndpointHealthController::probeConfiguration(const QString &id, bool &awg) const
{
    awg = false;
    const auto native = m_servers->nativeConfig(id);
    amnezia::ContainerConfig container;
    if (native) {
        if (native->subscription.value(QStringLiteral("retired")).toBool()) { return {}; }
        container = native->containerConfig(native->defaultContainer);
    } else {
        const auto separator = id.indexOf('|');
        if (separator <= 0) { return {}; }
        const auto api = m_servers->apiV2Config(id.left(separator));
        if (!api || !api->hasContainers() || api->apiConfig.isSubscriptionExpired()
            || api->apiConfig.serverCountryCode != QUrl::fromPercentEncoding(id.mid(separator + 1).toLatin1())) { return {}; }
        container = api->containerConfig(api->defaultContainer);
        auto text = container.protocolConfig.nativeConfig();
        const auto dns = api->getDnsPair(QStringLiteral("1.1.1.1"), QStringLiteral("8.8.8.8"));
        text.replace(QStringLiteral("$PRIMARY_DNS"), dns.first);
        text.replace(QStringLiteral("$SECONDARY_DNS"), dns.second);
        container.protocolConfig.setNativeConfig(text);
    }
    QByteArray config;
    if (const auto *protocol = container.getAwgProtocolConfig(); protocol && protocol->clientConfig) {
        awg = true;
        if (protocol->clientConfig->nativeConfig.size() > 65536) { return {}; }
        config = QJsonDocument(QJsonObject { { QStringLiteral("config"), protocol->clientConfig->nativeConfig } }).toJson(QJsonDocument::Compact);
    } else if (const auto *protocol = container.getXrayProtocolConfig(); protocol && protocol->clientConfig) {
        if (protocol->clientConfig->nativeConfig.size() > 65536) { return {}; }
        const auto document = QJsonDocument::fromJson(protocol->clientConfig->nativeConfig.toUtf8());
        const auto outbounds = document.object().value(QStringLiteral("outbounds")).toArray();
        if (outbounds.isEmpty() || outbounds.first().toObject().value(QStringLiteral("protocol")).toString() != QStringLiteral("vless")) { return {}; }
        config = document.toJson(QJsonDocument::Compact);
    }
    return config.size() <= 65536 ? config : QByteArray();
}

void EndpointHealthController::setTunnelActive(bool active)
{
    if (m_tunnelActive == active) { return; }
    m_tunnelActive = active;
    if (active) {
        // Второе AWG-соединение с тем же ключом может перенести endpoint и оборвать рабочий туннель.
        // Сначала завершаем диагностический процесс, затем разрешаем запуск основной VPN-сессии.
        const auto processes = m_awgActive.values();
        for (auto *process : processes) {
            const auto id = m_active.take(process); m_awgActive.remove(process);
            disconnect(process, nullptr, this, nullptr); process->kill(); process->waitForFinished(1000); process->deleteLater();
            m_results[id] = { QStringLiteral("deferred") };
        }
        for (auto it = m_queue.begin(); it != m_queue.end();) {
            bool awg = false; probeConfiguration(*it, awg);
            if (awg) { m_results[*it] = { QStringLiteral("deferred") }; it = m_queue.erase(it); }
            else { ++it; }
        }
    } else {
        for (auto &result : m_results) { if (result.state == QStringLiteral("deferred")) { result = { QStringLiteral("unknown") }; } }
    }
    dispatch(); emit changed();
}
} // namespace hop

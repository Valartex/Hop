#include "trafficPolicyController.h"
#include <QJsonDocument>
#include <QPointer>
#include <QtConcurrentRun>

namespace hop {
namespace { const auto policyKey = QStringLiteral("Conf/hopTrafficPolicy"); }
TrafficPolicyController::TrafficPolicyController(SecureQSettings *settings, RouteDatasetsController *datasets, QObject *parent, const QJsonObject &legacyPolicy)
    : QObject(parent), m_settings(settings), m_datasets(datasets)
{
    const auto bytes = settings->value(policyKey).toByteArray();
    const auto document = bytes.size() <= 2 * 1024 * 1024 ? QJsonDocument::fromJson(bytes) : QJsonDocument();
    const auto saved = document.object();
    const bool corrupt = !bytes.isEmpty() && (!document.isObject() || !saved.value(QStringLiteral("enabled")).isBool()
        || !saved.value(QStringLiteral("policy")).isObject());
    m_policy = saved.value(QStringLiteral("policy")).toObject();
    if (m_policy.isEmpty()) { m_policy = QJsonObject { { QStringLiteral("default"), QStringLiteral("vpn") }, { QStringLiteral("rules"), QJsonArray() } }; }
    m_enabled = saved.value(QStringLiteral("enabled")).toBool();
    const bool migrate = bytes.isEmpty() && !legacyPolicy.isEmpty();
    if (migrate) { m_policy = legacyPolicy; m_enabled = true; }
    if (corrupt) { m_enabled = true; m_policy.insert(QStringLiteral("default"), QStringLiteral("invalid")); }
    connect(&m_watcher, &QFutureWatcher<CompiledTrafficPolicy>::finished, this, [this]() {
        auto compiled = m_watcher.result();
        if (!compiled.isValid()) { m_error = compiled.error; }
        else {
            const QJsonObject saved { { QStringLiteral("enabled"), m_pendingEnabled }, { QStringLiteral("policy"), m_pending } };
            if (m_persist && !m_settings->setValueChecked(policyKey, QJsonDocument(saved).toJson(QJsonDocument::Compact))) {
                m_error = tr("Не удалось сохранить маршруты; прежняя политика оставлена");
            } else {
                const auto previous = snapshot(); const bool wasReady = m_ready;
                m_policy = m_pending; m_enabled = m_pendingEnabled; m_compiled = std::move(compiled); m_ready = true; m_error.clear();
                if (m_applyingOrder) { m_orderDraft = {}; m_orderPending = false; }
                if (!wasReady || previous != snapshot()) { emit policyChanged(); }
            }
        }
        m_applyingOrder = false; m_busy = false; emit changed();
        if (m_refreshPending) { m_refreshPending = false; compile(m_policy, m_enabled, false); }
    });
    connect(datasets, &RouteDatasetsController::datasetUpdated, this, [this](const QString &) {
        if (m_busy) { m_refreshPending = true; } else { compile(m_policy, m_enabled, false); }
    });
    datasets->setValidator([self = QPointer<TrafficPolicyController>(this)](const GeoDataSet &data) -> QString {
        if (!self) { return {}; }
        const auto kind = data.kind == GeoDataKind::GeoIp ? QStringLiteral("geoip") : QStringLiteral("geosite");
        const auto otherKind = data.kind == GeoDataKind::GeoIp ? QStringLiteral("geosite") : QStringLiteral("geoip");
        // Обновление набора может закончиться во время компиляции нового правила.
        // Проверяем действующий и ожидающий снимки до атомарной замены DAT.
        for (const auto &candidate : { qMakePair(self->m_policy, self->m_enabled),
                                      qMakePair(self->m_pending, self->m_busy && self->m_pendingEnabled) }) {
            if (!candidate.second) { continue; }
            bool needed = false, needsOther = false;
            for (const auto &value : candidate.first.value(QStringLiteral("rules")).toArray()) {
                needed |= value.toObject().value(QStringLiteral("kind")) == kind;
                needsOther |= value.toObject().value(QStringLiteral("kind")) == otherKind;
            }
            if (!needed) { continue; }
            const auto other = needsOther ? readGeoDataFile(self->m_datasets->cachedPath(otherKind),
                data.kind == GeoDataKind::GeoIp ? GeoDataKind::GeoSite : GeoDataKind::GeoIp) : GeoDataSet();
            const auto compiled = compileTrafficPolicy(candidate.first, data.kind == GeoDataKind::GeoIp ? &data : &other,
                data.kind == GeoDataKind::GeoSite ? &data : &other);
            if (!compiled.isValid()) { return tr("Обновление DAT не применяется: %1").arg(compiled.error); }
        }
        return {};
    });
    compile(m_policy, m_enabled, migrate);
}
TrafficPolicyController::~TrafficPolicyController() { disconnect(&m_watcher, nullptr, this, nullptr); }
void TrafficPolicyController::compile(const QJsonObject &policy, bool enabled, bool persist)
{
    if (m_busy) { return; }
    m_pending = policy; m_pendingEnabled = enabled; m_persist = persist; m_busy = true; m_error.clear(); emit changed();
    const auto ipPath = m_datasets->cachedPath(QStringLiteral("geoip")), sitePath = m_datasets->cachedPath(QStringLiteral("geosite"));
    m_watcher.setFuture(QtConcurrent::run([policy, enabled, ipPath, sitePath]() {
        if (!enabled) { return CompiledTrafficPolicy(); }
        bool needsIp = false, needsSite = false;
        for (const auto &value : policy.value(QStringLiteral("rules")).toArray()) {
            const auto kind = value.toObject().value(QStringLiteral("kind")).toString();
            needsIp |= kind == QStringLiteral("geoip"); needsSite |= kind == QStringLiteral("geosite");
        }
        const auto ip = needsIp ? readGeoDataFile(ipPath, GeoDataKind::GeoIp) : GeoDataSet();
        const auto site = needsSite ? readGeoDataFile(sitePath, GeoDataKind::GeoSite) : GeoDataSet();
        return compileTrafficPolicy(policy, needsIp ? &ip : nullptr, needsSite ? &site : nullptr);
    }));
}
QJsonObject TrafficPolicyController::snapshot() const {
    // Пустая политика VPN остаётся в быстром нативном AWG-пути.
    return m_enabled && m_ready && (!m_compiled.rules.isEmpty() || m_compiled.defaultTarget != QStringLiteral("vpn"))
        ? serializeTrafficPolicy(m_compiled) : QJsonObject();
}
bool TrafficPolicyController::apply(const QJsonObject &policy, bool enabled) {
    if (m_busy || m_orderPending) { return false; }
    compile(policy, enabled, true); return true;
}
bool TrafficPolicyController::addRule(const QString &kind, const QString &value, const QString &target) {
    auto policy = m_policy; auto list = policy.value(QStringLiteral("rules")).toArray();
    list.append(QJsonObject { { "kind", kind }, { "value", value.trimmed() }, { "target", target } }); policy.insert(QStringLiteral("rules"), list);
    return apply(policy, true);
}
bool TrafficPolicyController::removeRule(int row) {
    auto policy = m_policy; auto list = policy.value(QStringLiteral("rules")).toArray();
    if (row < 0 || row >= list.size()) { return false; } list.removeAt(row); policy.insert(QStringLiteral("rules"), list); return apply(policy, m_enabled);
}
bool TrafficPolicyController::moveRule(int row, int delta) {
    if (m_busy || !m_ready) { return false; }
    const auto original = m_policy.value(QStringLiteral("rules")).toArray();
    auto list = m_orderPending ? m_orderDraft : original; const auto next = row + delta;
    if ((delta != -1 && delta != 1) || row < 0 || row >= list.size() || next < 0 || next >= list.size()) { return false; }
    const auto value = list.at(row); list.removeAt(row); list.insert(next, value);
    // Стрелки меняют только черновик: действующая политика и VPN сохраняются до применения.
    m_orderPending = list != original; m_orderDraft = m_orderPending ? list : QJsonArray();
    m_error.clear(); emit changed(); return true;
}
bool TrafficPolicyController::applyOrder() {
    if (m_busy || !m_orderPending) { return false; }
    auto policy = m_policy; policy.insert(QStringLiteral("rules"), m_orderDraft);
    m_applyingOrder = true; compile(policy, m_enabled, true); return true;
}
void TrafficPolicyController::discardOrder() {
    if (m_busy || !m_orderPending) { return; }
    m_orderDraft = {}; m_orderPending = false; m_error.clear(); emit changed();
}
bool TrafficPolicyController::setDefaultTarget(const QString &target) { auto policy = m_policy; policy.insert(QStringLiteral("default"), target); return apply(policy, true); }
bool TrafficPolicyController::setEnabled(bool enabled) { return apply(m_policy, enabled); }
}

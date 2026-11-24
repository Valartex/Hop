#include "trafficPolicyController.h"
#include <QJsonDocument>
#include <QPointer>
#include <QFile>
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
    bool resetCategories = false;
    if (!corrupt && !bytes.isEmpty()) {
        for (const auto &value : m_policy.value(QStringLiteral("rules")).toArray()) {
            const auto kind = value.toObject().value(QStringLiteral("kind")).toString();
            resetCategories |= kind == QStringLiteral("geoip") || kind == QStringLiteral("geosite");
        }
        // Пользователь разрешил сброс прежних категорий вместо расширения их до всего источника.
        if (resetCategories) {
            m_policy = QJsonObject { { QStringLiteral("default"), QStringLiteral("vpn") }, { QStringLiteral("rules"), QJsonArray() } };
            m_enabled = false;
        }
    }
    if (corrupt) { m_enabled = true; m_policy.insert(QStringLiteral("default"), QStringLiteral("invalid")); }
    m_notifiedRules = rules();
    m_notifiedDefaultTarget = defaultTarget();
    connect(this, &TrafficPolicyController::changed, this, [this]() {
        const auto currentRules = rules();
        if (currentRules != m_notifiedRules) {
            m_notifiedRules = currentRules;
            emit rulesChanged();
        }
        const auto currentDefaultTarget = defaultTarget();
        if (currentDefaultTarget != m_notifiedDefaultTarget || (!m_busy && !m_error.isEmpty())) {
            m_notifiedDefaultTarget = currentDefaultTarget;
            emit defaultTargetChanged();
        }
    });
    connect(&m_watcher, &QFutureWatcher<CompiledTrafficPolicy>::finished, this, [this]() {
        auto compiled = m_watcher.result();
        if (!compiled.isValid()) {
            bool awaitingSource = false;
            for (const auto &value : m_pending.value(QStringLiteral("rules")).toArray()) {
                const auto rule = value.toObject(); const auto kind = geoSourceKind(rule.value("kind").toString());
                const auto source = rule.value("value").toString();
                awaitingSource |= !kind.isEmpty() && !QFile::exists(m_datasets->sourcePath(kind, source))
                    && m_datasets->sourceState(kind, source).value("updating").toBool();
            }
            m_error = awaitingSource ? QString() : compiled.error;
        }
        else {
            const QJsonObject saved { { QStringLiteral("enabled"), m_pendingEnabled }, { QStringLiteral("policy"), m_pending } };
            if (m_persist && !m_settings->setValueChecked(policyKey, QJsonDocument(saved).toJson(QJsonDocument::Compact))) {
                m_error = tr("Не удалось сохранить маршруты; прежняя политика оставлена");
            } else {
                const auto previous = snapshot(); const bool wasReady = m_ready;
                m_policy = m_pending; m_enabled = m_pendingEnabled; m_compiled = std::move(compiled); m_ready = true; m_error.clear();
                m_datasets->setRuleSources(m_policy.value(QStringLiteral("rules")).toArray());
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
    connect(datasets, &RouteDatasetsController::changed, this, &TrafficPolicyController::changed);
    datasets->setSourceValidator([self = QPointer<TrafficPolicyController>(this)](const QString &key, const GeoDataSet &data) -> QString {
        if (!self) { return {}; }
        QJsonArray rules;
        for (const auto &candidate : { self->m_policy, self->m_pending }) {
            for (const auto &value : candidate.value(QStringLiteral("rules")).toArray()) {
                const auto rule = value.toObject();
                if (geoDataSourceId(geoSourceKind(rule.value(QStringLiteral("kind")).toString()),
                                    rule.value(QStringLiteral("value")).toString()) == key && !rules.contains(value)) { rules.append(value); }
            }
        }
        const QMap<QString, GeoDataSet> source { { key, data } };
        return compileTrafficPolicy(QJsonObject { { "default", "vpn" }, { "rules", rules } }, nullptr, nullptr, &source).error;
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
    datasets->setRuleSources(m_policy.value(QStringLiteral("rules")).toArray());
    compile(m_policy, m_enabled, migrate || resetCategories);
}
TrafficPolicyController::~TrafficPolicyController() { disconnect(&m_watcher, nullptr, this, nullptr); }
void TrafficPolicyController::compile(const QJsonObject &policy, bool enabled, bool persist)
{
    if (m_busy) { return; }
    m_pending = policy; m_pendingEnabled = enabled; m_persist = persist; m_busy = true; m_error.clear(); emit changed();
    const auto ipPath = m_datasets->cachedPath(QStringLiteral("geoip")), sitePath = m_datasets->cachedPath(QStringLiteral("geosite"));
    QMap<QString, QPair<QString, GeoDataKind>> sourcePaths;
    for (const auto &value : policy.value(QStringLiteral("rules")).toArray()) {
        const auto rule = value.toObject(); const auto kind = geoSourceKind(rule.value(QStringLiteral("kind")).toString());
        if (kind.isEmpty()) { continue; }
        const auto text = rule.value(QStringLiteral("value")).toString();
        sourcePaths.insert(geoDataSourceId(kind, text), { m_datasets->sourcePath(kind, text), kind == QStringLiteral("geoip") ? GeoDataKind::GeoIp : GeoDataKind::GeoSite });
    }
    m_watcher.setFuture(QtConcurrent::run([policy, enabled, ipPath, sitePath, sourcePaths]() {
        if (!enabled) { return CompiledTrafficPolicy(); }
        bool needsIp = false, needsSite = false;
        for (const auto &value : policy.value(QStringLiteral("rules")).toArray()) {
            const auto kind = value.toObject().value(QStringLiteral("kind")).toString();
            needsIp |= kind == QStringLiteral("geoip"); needsSite |= kind == QStringLiteral("geosite");
        }
        const auto ip = needsIp ? readGeoDataFile(ipPath, GeoDataKind::GeoIp) : GeoDataSet();
        const auto site = needsSite ? readGeoDataFile(sitePath, GeoDataKind::GeoSite) : GeoDataSet();
        QMap<QString, GeoDataSet> sources;
        for (auto it = sourcePaths.cbegin(); it != sourcePaths.cend(); ++it) { sources.insert(it.key(), readGeoDataFile(it.value().first, it.value().second)); }
        return compileTrafficPolicy(policy, needsIp ? &ip : nullptr, needsSite ? &site : nullptr, &sources);
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
    const auto sourceKind = geoSourceKind(kind);
    if (!sourceKind.isEmpty()) {
        const auto source = normalizeGeoDataSource(value);
        if (source.isEmpty() || (target != QStringLiteral("vpn") && target != QStringLiteral("direct") && target != QStringLiteral("block"))
            || list.size() >= 512) { m_error = tr("Укажите HTTPS-ссылку или абсолютный путь к файлу DAT"); emit changed(); return false; }
        for (const auto &entry : list) {
            const auto existing = entry.toObject();
            if (existing.value("kind") == kind && existing.value("value") == source && existing.value("target") == target) {
                m_error = tr("Этот источник с таким направлением уже добавлен"); emit changed(); return false;
            }
        }
        list.append(QJsonObject { { "kind", kind }, { "value", source }, { "target", target }, { "autoUpdate", true } });
        policy.insert(QStringLiteral("rules"), list);
        if (!saveSourcePolicy(policy, true)) { return false; }
        m_datasets->refreshSource(sourceKind, source);
        return true;
    }
    list.append(QJsonObject { { "kind", kind }, { "value", value.trimmed() }, { "target", target } }); policy.insert(QStringLiteral("rules"), list);
    return apply(policy, true);
}
bool TrafficPolicyController::removeRule(int row) {
    auto policy = m_policy; auto list = policy.value(QStringLiteral("rules")).toArray();
    if (row < 0 || row >= list.size()) { return false; }
    const bool source = !geoSourceKind(list.at(row).toObject().value("kind").toString()).isEmpty();
    list.removeAt(row); policy.insert(QStringLiteral("rules"), list);
    return source ? saveSourcePolicy(policy, m_enabled) : apply(policy, m_enabled);
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
bool TrafficPolicyController::setDefaultTarget(const QString &target)
{
    if (m_enabled && m_ready && !m_busy && !m_orderPending && m_error.isEmpty() && target == defaultTarget()) { return true; }
    auto policy = m_policy; policy.insert(QStringLiteral("default"), target); return apply(policy, true);
}
bool TrafficPolicyController::setEnabled(bool enabled) { return apply(m_policy, enabled); }

QVariantList TrafficPolicyController::rules() const
{
    auto list = (m_orderPending ? m_orderDraft : m_policy.value(QStringLiteral("rules")).toArray()).toVariantList();
    for (auto &value : list) {
        auto rule = value.toMap(); const auto kind = geoSourceKind(rule.value(QStringLiteral("kind")).toString());
        if (kind.isEmpty()) { continue; }
        const auto state = m_datasets->sourceState(kind, rule.value(QStringLiteral("value")).toString());
        rule.insert(QStringLiteral("sourceRule"), true);
        rule.insert(QStringLiteral("autoUpdate"), rule.value(QStringLiteral("autoUpdate"), true));
        rule.insert(QStringLiteral("updating"), state.value(QStringLiteral("updating")));
        rule.insert(QStringLiteral("updated"), state.value(QStringLiteral("updated")));
        rule.insert(QStringLiteral("sourceError"), state.value(QStringLiteral("error")));
        value = rule;
    }
    return list;
}

bool TrafficPolicyController::saveSourcePolicy(const QJsonObject &policy, bool enabled)
{
    if (m_busy || m_orderPending) { return false; }
    const auto bytes = QJsonDocument(QJsonObject { { "enabled", enabled }, { "policy", policy } }).toJson(QJsonDocument::Compact);
    if (bytes.size() > 2 * 1024 * 1024 || !m_settings->setValueChecked(policyKey, bytes)) {
        m_error = tr("Не удалось сохранить маршруты; прежняя политика оставлена"); emit changed(); return false;
    }
    m_policy = policy; m_enabled = enabled; m_ready = false; m_error.clear();
    m_datasets->setRuleSources(policy.value(QStringLiteral("rules")).toArray());
    emit changed(); compile(policy, enabled, false);
    return true;
}

bool TrafficPolicyController::setRuleAutoUpdate(int row, bool enabled)
{
    if (m_busy || m_orderPending) { return false; }
    auto list = m_policy.value(QStringLiteral("rules")).toArray();
    if (row < 0 || row >= list.size()) { return false; }
    auto rule = list.at(row).toObject();
    if (geoSourceKind(rule.value("kind").toString()).isEmpty()) { return false; }
    rule.insert(QStringLiteral("autoUpdate"), enabled); list[row] = rule;
    auto policy = m_policy; policy.insert(QStringLiteral("rules"), list);
    const auto saved = QJsonDocument(QJsonObject { { "enabled", m_enabled }, { "policy", policy } }).toJson(QJsonDocument::Compact);
    if (!m_settings->setValueChecked(policyKey, saved)) { m_error = tr("Не удалось сохранить расписание DAT"); emit changed(); return false; }
    m_policy = policy; m_datasets->setRuleSources(list); m_error.clear(); emit changed(); return true;
}

bool TrafficPolicyController::refreshRule(int row)
{
    if (m_busy || m_orderPending) { return false; }
    const auto list = m_policy.value(QStringLiteral("rules")).toArray();
    if (row < 0 || row >= list.size()) { return false; }
    const auto rule = list.at(row).toObject();
    return m_datasets->refreshSource(geoSourceKind(rule.value("kind").toString()), rule.value("value").toString());
}
}

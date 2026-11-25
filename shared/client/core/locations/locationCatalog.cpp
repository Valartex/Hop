#include "locationCatalog.h"
#include <QJsonDocument>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <QLocale>
#include <QRegularExpression>
#include "core/utils/constants/apiKeys.h"
#include "core/utils/containers/containerUtils.h"
#include "core/subscriptions/subscriptionNaming.h"

namespace hop {
namespace {
const auto policyKey = QStringLiteral("Conf/hopLocationPolicy");
const auto serviceExpansionKey = QStringLiteral("Conf/hopServiceExpansion");
constexpr int legacyPinnedMode = 1; // Старое значение не переиспользуется другими режимами.
QString displayLocationName(QString name)
{
    // Windows выводит emoji-флаги буквами; название страны уже есть в подписи.
    static const QRegularExpression flags(QStringLiteral("[\\x{1F1E6}-\\x{1F1FF}]{2}\\s*"));
    return name.remove(flags).trimmed();
}
}
QString LocationCatalog::iso(const QString &value)
{
    const auto code = value.trimmed().toUpper();
    return code.size() == 2 && code.at(0) >= QLatin1Char('A') && code.at(0) <= QLatin1Char('Z')
        && code.at(1) >= QLatin1Char('A') && code.at(1) <= QLatin1Char('Z') ? code : QString();
}

QString LocationCatalog::countryFromLabel(const QString &label)
{
    const auto points = label.toUcs4();
    for (qsizetype index = 0; index + 1 < points.size(); ++index) {
        const auto first = points.at(index), second = points.at(index + 1);
        if (first >= 0x1f1e6 && first <= 0x1f1ff && second >= 0x1f1e6 && second <= 0x1f1ff) {
            return QString(QChar(static_cast<ushort>('A' + first - 0x1f1e6)))
                + QChar(static_cast<ushort>('A' + second - 0x1f1e6));
        }
    }
    // Названия городов и свободный текст не используются для угадывания страны.
    return {};
}

LocationCatalog::LocationCatalog(SecureServersRepository *servers, SecureQSettings *settings, QObject *parent)
    : QAbstractListModel(parent), m_servers(servers), m_settings(settings)
{
    const auto expansionBytes = settings->value(serviceExpansionKey).toByteArray();
    if (expansionBytes.size() <= 1024 * 1024) {
        const auto expansion = QJsonDocument::fromJson(expansionBytes).object();
        for (auto it = expansion.begin(); it != expansion.end() && m_serviceExpansion.size() < 4096; ++it) {
            if (!it.key().isEmpty() && it.key().size() <= 512 && it.value().isBool()) {
                m_serviceExpansion.insert(it.key(), it.value().toBool());
            }
        }
    }
    bool migratedMode = false;
    const auto bytes = settings->value(policyKey).toByteArray();
    if (bytes.size() <= 1024 * 1024) {
        const auto saved = QJsonDocument::fromJson(bytes).object();
        m_mode = saved.value(QStringLiteral("mode")).toInt(Manual);
        m_restrictedMode = qBound(0, saved.value(QStringLiteral("restrictedMode")).toInt(), 2);
        for (const auto &value : saved.value(QStringLiteral("restrictedLocations")).toArray()) {
            const auto id = value.toString();
            if (!id.isEmpty() && id.size() <= 512 && m_restrictedLocations.size() < 16384) { m_restrictedLocations.insert(id); }
        }
        migratedMode = m_mode == legacyPinnedMode;
        if (m_mode != Manual && m_mode != Automatic && m_mode != AllOrdered && m_mode != AllFastest) { m_mode = Manual; }
        m_selected = saved.value(QStringLiteral("selected")).toString().left(512);
        if (migratedMode) {
            const auto previousTarget = saved.value(QStringLiteral("pinned")).toString().left(512);
            if (!previousTarget.isEmpty()) { m_selected = previousTarget; }
        }
        QSet<QString> unique;
        for (const auto &value : saved.value(QStringLiteral("priorities")).toArray()) {
            if (m_priorities.size() >= 4096) { break; }
            const auto item = value.toObject();
            const auto kind = item.value(QStringLiteral("kind")).toString();
            const auto key = item.value(QStringLiteral("key")).toString();
            if ((kind != QStringLiteral("location") && kind != QStringLiteral("country")) || key.isEmpty() || key.size() > 512
                || (kind == QStringLiteral("country") && iso(key) != key) || unique.contains(kind + QLatin1Char(':') + key)) { continue; }
            unique.insert(kind + QLatin1Char(':') + key);
            m_priorities.append(QJsonObject { { QStringLiteral("kind"), kind }, { QStringLiteral("key"), key } });
        }
        for (const auto &value : saved.value(QStringLiteral("excluded")).toArray()) {
            const auto id = value.toString();
            if (!id.isEmpty() && id.size() <= 512 && m_excluded.size() < 16384) { m_excluded.insert(id); }
        }
    }
    connect(servers, &SecureServersRepository::serversBatchChanged, this, &LocationCatalog::rebuild);
    connect(servers, &SecureServersRepository::serverAdded, this, &LocationCatalog::rebuild);
    connect(servers, &SecureServersRepository::serverEdited, this, &LocationCatalog::rebuild);
    connect(servers, &SecureServersRepository::serverRemoved, this, &LocationCatalog::rebuild);
    connect(servers, &SecureServersRepository::defaultServerChanged, this, [this](const QString &id) {
        const auto current = entry(m_selected);
        if (current && current->serverId == id) { return; }
        for (const auto &location : m_entries) {
            if (location.serverId == id) { m_selected = location.id; emit selectionChanged(); return; }
        }
    });
    rebuild();
    if (migratedMode) { save(); }
}

bool LocationCatalog::setServiceExpanded(const QString &serviceId, bool expanded)
{
    if (serviceId.isEmpty() || serviceId.size() > 512) { return false; }
    if (m_serviceExpansion.contains(serviceId)) {
        if (m_serviceExpansion.value(serviceId).toBool() == expanded) { return true; }
    } else if (m_serviceExpansion.size() >= 4096) { return false; }
    auto expansion = m_serviceExpansion;
    expansion.insert(serviceId, expanded);
    if (!m_settings->setValueChecked(serviceExpansionKey,
        QJsonDocument(QJsonObject::fromVariantMap(expansion)).toJson(QJsonDocument::Compact))) { return false; }
    m_serviceExpansion = expansion;
    emit serviceExpansionChanged();
    return true;
}

int LocationCatalog::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_entries.size(); }
QHash<int, QByteArray> LocationCatalog::roleNames() const
{
    return { { Id, "serverId" }, { Name, "name" }, { Description, "serverDescription" },
             { FromApi, "isServerFromGatewayApi" }, { Expired, "isSubscriptionExpired" }, { Expiring, "isSubscriptionExpiringSoon" },
             { ServiceName, "serviceName" }, { Country, "countryCode" }, { ServiceId, "serviceId" } };
}
QVariant LocationCatalog::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) { return {}; }
    const auto &location = m_entries.at(index.row());
    switch (role) {
    case Id: return location.id;
    case Name: return displayLocationName(location.name);
    case Description: return location.protocol;
    case ServiceName: return m_serviceNames.value(location.serviceId);
    case ServiceId: return location.serviceId;
    case Country: return location.country;
    case FromApi: return location.api;
    case Expired: return location.api && !location.enabled;
    case Expiring: return false;
    default: return {};
    }
}
std::optional<LocationCatalog::Entry> LocationCatalog::entry(const QString &id) const
{
    for (const auto &location : m_entries) { if (location.id == id) { return location; } }
    return std::nullopt;
}
QString LocationCatalog::selectedName() const { const auto current = entry(m_selected); return current ? displayLocationName(current->name) : QString(); }
QString LocationCatalog::selectedProtocol() const { const auto current = entry(m_selected); return current ? current->protocol : QString(); }
int LocationCatalog::apiServiceCount() const
{
    QSet<QString> services;
    for (const auto &location : m_entries) { if (location.api) { services.insert(location.serviceId); } }
    return services.size();
}

QVariantMap LocationCatalog::serviceInfo(const QString &locationId) const
{
    auto location = entry(locationId);
    if (!location) {
        for (const auto &candidate : m_entries) {
            if (candidate.serverId == locationId || candidate.serviceId == locationId
                || (candidate.api && locationId.startsWith(candidate.serverId + QLatin1Char('|')))) { location = candidate; break; }
        }
    }
    if (!location) { return {}; }
    int count = 0;
    for (const auto &item : m_entries) { if (item.serviceId == location->serviceId) { ++count; } }
    QVariantMap result { { QStringLiteral("id"), location->serviceId }, { QStringLiteral("serverId"), location->serverId },
        { QStringLiteral("name"), m_serviceNames.value(location->serviceId) }, { QStringLiteral("locations"), count },
        { QStringLiteral("protocol"), location->protocol }, { QStringLiteral("location"), location->name } };
    if (const auto api = m_servers->apiV2Config(location->serverId)) {
        result.insert(QStringLiteral("kind"), QStringLiteral("api"));
        result.insert(QStringLiteral("activeConfigurations"), api->apiConfig.activeDeviceCount);
        result.insert(QStringLiteral("maxConfigurations"), api->apiConfig.maxDeviceCount);
        result.insert(QStringLiteral("validUntil"), api->apiConfig.subscription.endDate);
        result.insert(QStringLiteral("hasConfiguration"), api->hasContainers() && api->apiConfig.serverCountryCode == location->apiCountry);
    } else if (const auto native = m_servers->nativeConfig(location->serverId)) {
        const auto metadata = native->subscription;
        result.insert(QStringLiteral("kind"), metadata.value(QStringLiteral("sourceId")).toString().isEmpty() ? QStringLiteral("native") : QStringLiteral("http"));
        result.insert(QStringLiteral("updatedAt"), metadata.value(QStringLiteral("updatedAt")).toString());
        result.insert(QStringLiteral("host"), QUrl(metadata.value(QStringLiteral("sourceUrl")).toString()).host());
    } else { result.insert(QStringLiteral("kind"), QStringLiteral("native")); }
    return result;
}

void LocationCatalog::rebuild()
{
    bool migrated = false;
    beginResetModel();
    m_entries.clear();
    m_serviceNames.clear();
    auto protocolNames = ContainerUtils::containerHumanNames();
    protocolNames.insert(DockerContainer::Xray, QStringLiteral("VLESS"));
    for (const auto &id : m_servers->orderedServerIds()) {
        if (const auto api = m_servers->apiV2Config(id)) {
            const auto label = api->displayName.isEmpty() ? api->description : api->displayName;
            m_serviceNames.insert(id, label);
            const auto actualCountry = api->apiConfig.serverCountryCode;
            const auto actualId = id + QLatin1Char('|') + QString::fromLatin1(QUrl::toPercentEncoding(actualCountry));
            const auto bootstrap = id + QLatin1Char('|');
            if (!actualCountry.isEmpty()) {
                if (m_selected == bootstrap || (m_selected.startsWith(id + QLatin1Char('|')) && m_apiCountries.contains(id)
                    && m_apiCountries.value(id) != actualCountry)) { m_selected = actualId; migrated = true; }
                for (int row = 0; row < m_priorities.size(); ++row) {
                    auto item = m_priorities.at(row).toObject();
                    if (item.value(QStringLiteral("kind")) == QStringLiteral("location") && item.value(QStringLiteral("key")) == bootstrap) {
                        item.insert(QStringLiteral("key"), actualId); m_priorities[row] = item; migrated = true;
                    }
                }
                if (m_excluded.remove(bootstrap)) { m_excluded.insert(actualId); migrated = true; }
                if (m_restrictedLocations.remove(bootstrap)) { m_restrictedLocations.insert(actualId); migrated = true; }
            }
            m_apiCountries.insert(id, actualCountry);
            QJsonArray countries = api->apiConfig.availableCountries;
            if (countries.isEmpty()) { countries.append(QJsonObject { { apiDefs::key::serverCountryCode, api->apiConfig.serverCountryCode },
                { apiDefs::key::serverCountryCodeL10n, api->apiConfig.serverCountryCodeL10n },
                { apiDefs::key::serverCountryName, api->apiConfig.serverCountryName } }); }
            QSet<QString> used;
            for (const auto &value : countries) {
                const auto country = value.toObject();
                const auto code = country.value(apiDefs::key::serverCountryCode).toString();
                if (code.size() > 128 || used.contains(code)) { continue; }
                used.insert(code);
                const auto locationId = id + QLatin1Char('|') + QString::fromLatin1(QUrl::toPercentEncoding(code));
                const auto name = country.value(apiDefs::key::serverCountryName).toString();
                auto isoCountry = iso(country.value(apiDefs::key::serverCountryCodeL10n).toString());
                if (isoCountry.isEmpty()) { isoCountry = iso(code); }
                m_entries.append({ locationId, id, id, label + (name.isEmpty() ? QString() : QStringLiteral(" · ") + name),
                    api->serviceProtocol() == QStringLiteral("awg") ? QStringLiteral("AmneziaWG 3.1")
                        : api->serviceProtocol() == QStringLiteral("vless") ? QStringLiteral("VLESS") : api->serviceProtocol(),
                    isoCountry, code, !api->apiConfig.isSubscriptionExpired(), true });
            }
        } else if (const auto native = m_servers->nativeConfig(id)) {
            const auto label = native->displayName.isEmpty() ? native->description : native->displayName;
            const auto service = native->subscription.value(QStringLiteral("sourceId")).toString(id);
            const auto serviceName = subscriptionName(native->subscription);
            m_serviceNames.insert(service, serviceName.isEmpty() ? tr("Конфигурации") : serviceName);
            auto protocolName = protocolNames.value(native->defaultContainer, ContainerUtils::containerToString(native->defaultContainer));
            const auto container = native->containerConfig(native->defaultContainer);
            if (const auto *xray = container.getXrayProtocolConfig(); xray && xray->serverConfig.security == QStringLiteral("reality")) {
                protocolName = QStringLiteral("VLESS / Reality");
            }
            m_entries.append({ id, id, native->subscription.value(QStringLiteral("sourceId")).toString(id), label,
                protocolName, countryFromLabel(label), {},
                !native->subscription.value(QStringLiteral("retired")).toBool()
                    && (ContainerUtils::isAwgContainer(native->defaultContainer)
                        || ContainerUtils::defaultProtocol(native->defaultContainer) == Proto::WireGuard
                        || ContainerUtils::defaultProtocol(native->defaultContainer) == Proto::Xray), false });
        } else {
            m_serviceNames.insert(id, tr("Собственные серверы"));
            QString label;
            DockerContainer container = DockerContainer::None;
            if (const auto admin = m_servers->selfHostedAdminConfig(id)) { label = admin->description; container = admin->defaultContainer; }
            else if (const auto user = m_servers->selfHostedUserConfig(id)) { label = user->description; container = user->defaultContainer; }
            else { continue; }
            m_entries.append({ id, id, id, label, protocolNames.value(container, ContainerUtils::containerToString(container)), countryFromLabel(label), {},
                ContainerUtils::isAwgContainer(container) || ContainerUtils::defaultProtocol(container) == Proto::Xray, false });
        }
    }
    // Текущая API-страна выбирается по её точному идентификатору провайдера.
    if (!entry(m_selected)) {
        m_selected.clear();
        const auto defaultId = m_servers->defaultServerId();
        const auto api = m_servers->apiV2Config(defaultId);
        for (const auto &location : m_entries) {
            if (location.serverId == defaultId && (!api || location.apiCountry == api->apiConfig.serverCountryCode)) { m_selected = location.id; break; }
        }
        if (m_selected.isEmpty() && !m_entries.isEmpty()) { m_selected = m_entries.first().id; }
    }
    endResetModel();
    if (migrated) { save(); }
    emit catalogChanged();
    emit selectionChanged();
    emit policyChanged();
}

void LocationCatalog::save()
{
    QJsonArray excluded;
    for (const auto &id : m_excluded) { excluded.append(id); }
    QJsonArray restricted;
    for (const auto &id : m_restrictedLocations) { restricted.append(id); }
    m_settings->setValue(policyKey, QJsonDocument(QJsonObject { { QStringLiteral("version"), 1 }, { QStringLiteral("mode"), m_mode },
        { QStringLiteral("selected"), m_selected }, { QStringLiteral("priorities"), m_priorities },
        { QStringLiteral("restrictedMode"), m_restrictedMode }, { QStringLiteral("restrictedLocations"), restricted },
        { QStringLiteral("excluded"), excluded } }).toJson(QJsonDocument::Compact));
    emit policyChanged();
}
bool LocationCatalog::selectRow(int row) { return row >= 0 && row < m_entries.size() && select(m_entries.at(row).id); }
bool LocationCatalog::select(const QString &id)
{
    const auto target = entry(id);
    if (!target || !target->enabled) { return false; }
    m_selected = id;
    m_servers->setDefaultServer(target->serverId);
    save();
    emit selectionChanged();
    return true;
}
void LocationCatalog::setMode(int value)
{
    if (value == legacyPinnedMode) { value = Manual; }
    if ((value != Manual && value != Automatic && value != AllOrdered && value != AllFastest) || value == m_mode) { return; }
    m_mode = value;
    save();
}
bool LocationCatalog::toggle(const QString &kind, const QString &key)
{
    for (int row = 0; row < m_priorities.size(); ++row) {
        const auto item = m_priorities.at(row).toObject();
        if (item.value(QStringLiteral("kind")) == kind && item.value(QStringLiteral("key")) == key) { m_priorities.removeAt(row); save(); return true; }
    }
    if (m_priorities.size() >= 4096) { return false; }
    m_priorities.append(QJsonObject { { QStringLiteral("kind"), kind }, { QStringLiteral("key"), key } });
    save();
    return true;
}
bool LocationCatalog::toggleLocation(const QString &id) { return entry(id) && toggle(QStringLiteral("location"), id); }
bool LocationCatalog::toggleMembership(const QString &id)
{
    if (!entry(id)) { return false; }
    if (allLocations()) { return setExcluded(id, !m_excluded.contains(id)); }
    const auto rank = marks().value(id).toMap().value(QStringLiteral("priority")).toInt();
    const bool removing = !m_excluded.contains(id) && rank > 0;
    if (removing && m_excluded.size() >= 16384) { return false; }
    QJsonArray priorities;
    bool individual = false;
    for (const auto &value : m_priorities) {
        const auto item = value.toObject();
        if (item.value(QStringLiteral("kind")) == QStringLiteral("location")) {
            const auto key = item.value(QStringLiteral("key")).toString();
            if (key == id) { individual = true; continue; }
            // Убираем и скрытые позиции, сохранённые прежними версиями после исключения.
            if (m_excluded.contains(key)) { continue; }
        }
        priorities.append(value);
    }
    if (!removing && (individual || !rank)) {
        if (priorities.size() >= 4096) { return false; }
        priorities.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("location")}, {QStringLiteral("key"), id}});
    }
    // Страна остаётся единым элементом пула; повторно добавленная отдельная локация идёт в конец.
    if (removing) { m_excluded.insert(id); } else { m_excluded.remove(id); }
    m_priorities = priorities;
    save();
    return true;
}
bool LocationCatalog::toggleCountry(const QString &code) { const auto value = iso(code); return !value.isEmpty() && toggle(QStringLiteral("country"), value); }
bool LocationCatalog::setExcluded(const QString &id, bool excluded)
{
    if (!entry(id) || (excluded && m_excluded.size() >= 16384)) { return false; }
    if (excluded) { m_excluded.insert(id); } else { m_excluded.remove(id); }
    save();
    return true;
}
bool LocationCatalog::movePriority(int from, int to)
{
    if (from < 0 || to < 0 || from >= m_priorities.size() || to >= m_priorities.size()) { return false; }
    const auto value = m_priorities.takeAt(from);
    m_priorities.insert(to, value);
    save();
    return true;
}
bool LocationCatalog::removePriority(int row) { if (row < 0 || row >= m_priorities.size()) { return false; } m_priorities.removeAt(row); save(); return true; }
QVariantList LocationCatalog::priorities() const
{
    QVariantList result;
    for (const auto &value : m_priorities) {
        auto item = value.toObject();
        const auto key = item.value(QStringLiteral("key")).toString();
        const auto location = entry(key);
        const bool country = item.value(QStringLiteral("kind")) == QStringLiteral("country");
        const auto label = country ? tr("Страна: %1").arg(key) : location ? location->name : tr("Удалённая локация");
        const auto serviceName = country ? QString() : location ? m_serviceNames.value(location->serviceId) : QStringLiteral("—");
        auto locationName = label;
        const auto prefix = serviceName + QStringLiteral(" · ");
        if (!country && location && !serviceName.isEmpty() && locationName.startsWith(prefix)) {
            locationName.remove(0, prefix.size());
        }
        item.insert(QStringLiteral("label"), label);
        item.insert(QStringLiteral("serviceName"), serviceName);
        item.insert(QStringLiteral("locationName"), displayLocationName(locationName));
        result.append(item.toVariantMap());
    }
    return result;
}
QStringList LocationCatalog::candidateIds() const
{
    QStringList result;
    if (!automatic()) {
        const auto target = entry(m_selected);
        if (target && target->enabled) { result.append(target->id); }
        return result;
    }
    QHash<QString, QStringList> byCountry;
    QSet<QString> enabled, added;
    for (const auto &location : m_entries) {
        if (!location.enabled || m_excluded.contains(location.id)) { continue; }
        if (allLocations()) { result.append(location.id); }
        enabled.insert(location.id);
        if (!location.country.isEmpty()) { byCountry[location.country].append(location.id); }
    }
    if (allLocations()) { return result; }
    for (const auto &value : m_priorities) {
        const auto item = value.toObject();
        const auto key = item.value(QStringLiteral("key")).toString();
        const auto ids = item.value(QStringLiteral("kind")) == QStringLiteral("country") ? byCountry.value(key)
            : enabled.contains(key) ? QStringList { key } : QStringList();
        for (const auto &id : ids) {
            if (!added.contains(id)) { result.append(id); added.insert(id); }
        }
    }
    return result;
}
void LocationCatalog::setRestrictedMode(int mode)
{
    if (mode < 0 || mode > 2 || mode == m_restrictedMode) { return; }
    m_restrictedMode = mode; save();
}

bool LocationCatalog::setRestrictedLocation(const QString &id, bool enabled)
{
    if (!entry(id) || (enabled && m_restrictedLocations.size() >= 16384)) { return false; }
    if (enabled) { m_restrictedLocations.insert(id); } else { m_restrictedLocations.remove(id); }
    save(); return true;
}

QStringList LocationCatalog::networkCandidateIds(bool restricted) const
{
    auto ids = candidateIds();
    if (m_restrictedMode == 0) { return ids; }
    ids.removeIf([&](const QString &id) { return m_restrictedLocations.contains(id) != restricted; });
    return ids;
}

QVariantMap LocationCatalog::marks() const
{
    QVariantMap result;
    QHash<QString, int> locationRanks, countryRanks;
    for (int row = 0; row < m_priorities.size(); ++row) {
        const auto item = m_priorities.at(row).toObject();
        auto &ranks = item.value(QStringLiteral("kind")) == QStringLiteral("location") ? locationRanks : countryRanks;
        ranks.insert(item.value(QStringLiteral("key")).toString(), row + 1);
    }
    for (const auto &location : m_entries) {
        const int direct = locationRanks.value(location.id), country = countryRanks.value(location.country);
        const int rank = direct && country ? std::min(direct, country) : std::max(direct, country);
        result.insert(location.id, QVariantMap { { QStringLiteral("country"), location.country }, { QStringLiteral("priority"), rank },
            { QStringLiteral("automaticMember"), !m_excluded.contains(location.id) && (allLocations() || rank > 0) },
            { QStringLiteral("allLocations"), allLocations() },
            { QStringLiteral("restrictedNetwork"), m_restrictedLocations.contains(location.id) },
            { QStringLiteral("excluded"), m_excluded.contains(location.id) }, { QStringLiteral("serverId"), location.serverId },
            { QStringLiteral("serviceName"), m_serviceNames.value(location.serviceId) },
            { QStringLiteral("name"), displayLocationName(location.name) }, { QStringLiteral("protocol"), location.protocol },
            { QStringLiteral("api"), location.api }, { QStringLiteral("serviceId"), location.serviceId },
            { QStringLiteral("available"), location.enabled } });
    }
    return result;
}
QVariantList LocationCatalog::countries() const
{
    QSet<QString> unique;
    for (const auto &location : m_entries) { if (!location.country.isEmpty() && location.enabled) { unique.insert(location.country); } }
    auto codes = unique.values();
    std::sort(codes.begin(), codes.end());
    QVariantList result;
    for (const auto &code : codes) {
        const auto territory = QLocale::codeToTerritory(code);
        const auto name = territory == QLocale::AnyTerritory ? code : QLocale::territoryToString(territory);
        result.append(QVariantMap { { QStringLiteral("code"), code }, { QStringLiteral("label"), name + QStringLiteral(" · ") + code } });
    }
    return result;
}
}

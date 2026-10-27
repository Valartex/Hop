#include <winsock2.h>
#include <windows.h>
#include <fwpmu.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QHostAddress>
#include <QJsonArray>
#include <QScopeGuard>
#include <QDateTime>
#include <QElapsedTimer>
#include <QTimer>
#include <QHash>
#include <QNetworkInterface>
#include <mutex>
#include <QUuid>
#include <array>
#include <cstring>
#include "windowsTrafficGuard.h"
#include "version.h"
#include "../../common/routing/privateRoutePolicy.h"

namespace {
const GUID providerKey = QUuid(QStringLiteral("{dd311cd5-4193-46cb-8edc-353bf4efa9dd}"));
const GUID layerKey = QUuid(QStringLiteral("{bbf57b62-61a8-4288-9c28-6b6f67af24d3}"));

bool checked(DWORD code, const char *operation)
{
    if (code == ERROR_SUCCESS) { return true; }
    qWarning() << "Hop traffic guard:" << operation << "failed:" << Qt::hex << code;
    return false;
}

struct Engine {
    HANDLE handle = nullptr;
    explicit Engine(bool dynamic = false) {
        FWPM_SESSION0 session {};
        session.flags = dynamic ? FWPM_SESSION_FLAG_DYNAMIC : 0;
        checked(FwpmEngineOpen0(nullptr, RPC_C_AUTHN_WINNT, nullptr, &session, &handle), "open");
    }
    ~Engine() { if (handle) { FwpmEngineClose0(handle); } }
};

struct TunnelPermits {
    HANDLE engine = nullptr;
    void clear() { if (engine) { FwpmEngineClose0(engine); engine = nullptr; } }
    ~TunnelPermits() { clear(); }
};
TunnelPermits &tunnelPermits() { static TunnelPermits permits; return permits; }

struct ControlLeases {
    struct Lease { HANDLE engine; qint64 expires; };
    QHash<QString, Lease> active;
    QTimer timer;
    QElapsedTimer clock;
    ControlLeases() {
        clock.start();
        timer.setInterval(1000);
        QObject::connect(&timer, &QTimer::timeout, QCoreApplication::instance(), [this]() {
            const auto now = clock.elapsed();
            for (auto it = active.begin(); it != active.end();) {
                if (it->expires <= now) { FwpmEngineClose0(it->engine); it = active.erase(it); }
                else { ++it; }
            }
            if (active.isEmpty()) { timer.stop(); }
        });
    }
    void clear() {
        timer.stop();
        for (const auto &lease : active) { FwpmEngineClose0(lease.engine); }
        active.clear();
    }
    ~ControlLeases() { clear(); }
};
ControlLeases &controlLeases() { static ControlLeases leases; return leases; }

bool ownFilters(HANDLE engine, QList<UINT64> &ids, bool persistentOnly = false)
{
    HANDLE enumeration = nullptr;
    if (!checked(FwpmFilterCreateEnumHandle0(engine, nullptr, &enumeration), "enumerate handle")) { return false; }
    const auto cleanup = qScopeGuard([&]() { FwpmFilterDestroyEnumHandle0(engine, enumeration); });
    for (;;) {
        FWPM_FILTER0 **filters = nullptr;
        UINT32 count = 0;
        if (!checked(FwpmFilterEnum0(engine, enumeration, 256, &filters, &count), "enumerate")) { return false; }
        const auto release = qScopeGuard([&]() { if (filters) { FwpmFreeMemory0(reinterpret_cast<void **>(&filters)); } });
        for (UINT32 index = 0; index < count; ++index) {
            const auto &filter = *filters[index];
            if (filter.providerKey && IsEqualGUID(*filter.providerKey, providerKey)
                && IsEqualGUID(filter.subLayerKey, layerKey)
                && (!persistentOnly || (filter.flags & FWPM_FILTER_FLAG_PERSISTENT))) { ids.append(filter.filterId); }
        }
        if (count < 256) { return true; }
    }
}

bool removeFilters(HANDLE engine)
{
    QList<UINT64> ids;
    if (!ownFilters(engine, ids, true)) { return false; }
    for (const auto id : ids) {
        if (!checked(FwpmFilterDeleteById0(engine, id), "delete")) { return false; }
    }
    return true;
}

bool ensureObjects(HANDLE engine)
{
    FWPM_PROVIDER0 provider {};
    provider.providerKey = providerKey;
    provider.flags = FWPM_PROVIDER_FLAG_PERSISTENT;
    provider.displayData.name = const_cast<wchar_t *>(L"Hop traffic protection");
    // При перезапуске BFE постоянные фильтры связаны с автоматически запускаемой службой.
    std::wstring service = QStringLiteral(SERVICE_NAME).toStdWString();
    provider.serviceName = service.data();
    const auto result = FwpmProviderAdd0(engine, &provider, nullptr);
    if (result != FWP_E_ALREADY_EXISTS && !checked(result, "provider")) { return false; }
    FWPM_SUBLAYER0 layer {};
    layer.subLayerKey = layerKey;
    layer.providerKey = const_cast<GUID *>(&providerKey);
    layer.displayData.name = const_cast<wchar_t *>(L"Hop persistent guard");
    layer.flags = FWPM_SUBLAYER_FLAG_PERSISTENT;
    layer.weight = 0xfffe;
    const auto layerResult = FwpmSubLayerAdd0(engine, &layer, nullptr);
    return layerResult == FWP_E_ALREADY_EXISTS || checked(layerResult, "sublayer");
}

struct Prefix {
    QHostAddress address;
    int length;
};
bool parsePrefix(const QString &text, Prefix &prefix)
{
    if (text.contains(QLatin1Char('%'))) { return false; }
    if (text.contains(QLatin1Char('/'))) {
        const auto parsed = QHostAddress::parseSubnet(text);
        if (parsed.second < 0) { return false; }
        prefix = { parsed.first, parsed.second };
    } else {
        const QHostAddress address(text);
        if (address.isNull()) { return false; }
        prefix = { address, address.protocol() == QAbstractSocket::IPv4Protocol ? 32 : 128 };
    }
    return prefix.address.protocol() == QAbstractSocket::IPv4Protocol
        || prefix.address.protocol() == QAbstractSocket::IPv6Protocol;
}

FWPM_FILTER_CONDITION0 uintCondition(const GUID &key, UINT32 value, FWP_DATA_TYPE type = FWP_UINT32)
{
    FWPM_FILTER_CONDITION0 condition {};
    condition.fieldKey = key;
    condition.matchType = FWP_MATCH_EQUAL;
    condition.conditionValue.type = type;
    if (type == FWP_UINT16) { condition.conditionValue.uint16 = static_cast<UINT16>(value); }
    else if (type == FWP_UINT8) { condition.conditionValue.uint8 = static_cast<UINT8>(value); }
    else { condition.conditionValue.uint32 = value; }
    return condition;
}

bool add(HANDLE engine, bool v6, bool permit, UINT64 weight,
         QList<FWPM_FILTER_CONDITION0> conditions = {}, const Prefix *prefix = nullptr, bool persistent = true, UINT64 *filterId = nullptr)
{
    FWP_V4_ADDR_AND_MASK v4 {};
    FWP_V6_ADDR_AND_MASK v6Mask {};
    if (prefix) {
        FWPM_FILTER_CONDITION0 address {};
        address.fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;
        address.matchType = FWP_MATCH_EQUAL;
        if (v6) {
            const auto bytes = prefix->address.toIPv6Address();
            std::memcpy(v6Mask.addr, bytes.c, 16);
            v6Mask.prefixLength = static_cast<UINT8>(prefix->length);
            address.conditionValue.type = FWP_V6_ADDR_MASK;
            address.conditionValue.v6AddrMask = &v6Mask;
        } else {
            v4.addr = prefix->address.toIPv4Address();
            v4.mask = prefix->length == 0 ? 0 : 0xffffffffu << (32 - prefix->length);
            address.conditionValue.type = FWP_V4_ADDR_MASK;
            address.conditionValue.v4AddrMask = &v4;
        }
        conditions.append(address);
    }
    FWPM_FILTER0 filter {};
    filter.providerKey = const_cast<GUID *>(&providerKey);
    filter.subLayerKey = layerKey;
    filter.layerKey = v6 ? FWPM_LAYER_ALE_AUTH_CONNECT_V6 : FWPM_LAYER_ALE_AUTH_CONNECT_V4;
    filter.displayData.name = const_cast<wchar_t *>(L"Hop traffic protection");
    filter.flags = persistent ? FWPM_FILTER_FLAG_PERSISTENT : 0;
    filter.weight.type = FWP_UINT64;
    filter.weight.uint64 = &weight;
    filter.action.type = permit ? FWP_ACTION_PERMIT : FWP_ACTION_BLOCK;
    // Разрешение мягкое: не отменяет блокировки Windows Firewall и других VPN.
    filter.numFilterConditions = static_cast<UINT32>(conditions.size());
    filter.filterCondition = conditions.data();
    return checked(FwpmFilterAdd0(engine, &filter, nullptr, filterId), "add filter");
}
}

bool WindowsTrafficGuard::apply(const QJsonObject &config, const QString &tunnelAlias)
{
    if (QStringLiteral(APPLICATION_NAME) != QStringLiteral("Hop")) { return false; }
    if (!tunnelAlias.isEmpty() && tunnelAlias != QStringLiteral(APP_WINDOWS_XRAY_INTERFACE)
        && tunnelAlias != QStringLiteral(APP_WINDOWS_TUNNEL_INTERFACE)) { return false; }
    const auto option = config.value(QStringLiteral("killSwitchOption"));
    if (!option.isUndefined() && !option.isBool() && (!option.isString()
        || (option.toString() != QStringLiteral("true") && option.toString() != QStringLiteral("false")))) { return false; }
    const bool enabled = option.isUndefined() || (option.isBool() ? option.toBool() : option.toString() == QStringLiteral("true"));
    if (!enabled) { return clear(); }
    const auto modeValue = config.value(QStringLiteral("splitTunnelType"));
    const int mode = modeValue.isUndefined() ? 0 : modeValue.toInt(-1);
    if ((!modeValue.isUndefined() && (!modeValue.isDouble() || modeValue.toDouble() != mode)) || mode < 0 || mode > 2) { return false; }
    QList<Prefix> sites, dns;
    const auto siteValue = config.value(QStringLiteral("splitTunnelSites"));
    if (!siteValue.isUndefined() && !siteValue.isArray()) { return false; }
    const auto siteArray = siteValue.toArray();
    if (siteArray.size() > 65536) { return false; }
    for (const auto &value : siteArray) {
        Prefix prefix;
        if (!value.isString() || !parsePrefix(value.toString(), prefix)) { return false; }
        sites.append(prefix);
    }
    for (const auto *key : { "dns1", "dns2" }) {
        const auto text = config.value(QLatin1String(key)).toString();
        if (text.isEmpty()) { continue; }
        if (text.contains(QLatin1Char('/'))) { return false; }
        Prefix prefix;
        if (!parsePrefix(text, prefix) || (prefix.length != 32 && prefix.length != 128)) { return false; }
        dns.append(prefix);
    }
    Prefix server;
    if (!parsePrefix(config.value(QStringLiteral("vpnServer")).toString(), server)
        || server.length != (server.address.protocol() == QAbstractSocket::IPv4Protocol ? 32 : 128)) { return false; }
    const int port = config.value(QStringLiteral("hopTransportPort")).toInt();
    if (port < 1 || port > 65535 || config.value(QStringLiteral("hopTransportPort")).toDouble() != port) { return false; }
    const auto transport = config.value(QStringLiteral("hopTransportProtocol")).toString(QStringLiteral("tcp"));
    if (transport != QStringLiteral("tcp") && transport != QStringLiteral("udp")) { return false; }
    NET_LUID luid {};
    if (!tunnelAlias.isEmpty() && ConvertInterfaceAliasToLuid(reinterpret_cast<PCWSTR>(tunnelAlias.utf16()), &luid) != NO_ERROR) {
        return false;
    }
    NET_LUID additional {};
    if (config.value(QStringLiteral("hopPolicyAwg")).toBool() && !tunnelAlias.isEmpty()
        && tunnelAlias == QStringLiteral(APP_WINDOWS_XRAY_INTERFACE)
        && ConvertInterfaceAliasToLuid(L"Hop", &additional) != NO_ERROR) { return false; }
    // Старый интерфейс теряет разрешение до замены защиты; ошибка оставляет блокировку.
    tunnelPermits().clear();
    Engine engine;
    if (!engine.handle || !checked(FwpmTransactionBegin0(engine.handle, 0), "begin")) { return false; }
    bool committed = false;
    const auto rollback = qScopeGuard([&]() { if (!committed) { FwpmTransactionAbort0(engine.handle); } });
    if (!ensureObjects(engine.handle) || !removeFilters(engine.handle)) { return false; }
    FWP_BYTE_BLOB *appId = nullptr;
    const auto path = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    if (!checked(FwpmGetAppIdFromFileName0(reinterpret_cast<PCWSTR>(path.utf16()), &appId), "service identity")) { return false; }
    const auto releaseApp = qScopeGuard([&]() { FwpmFreeMemory0(reinterpret_cast<void **>(&appId)); });
    auto identity = uintCondition(FWPM_CONDITION_ALE_APP_ID, 0);
    identity.conditionValue.type = FWP_BYTE_BLOB_TYPE;
    identity.conditionValue.byteBlob = appId;
    if (!add(engine.handle, server.address.protocol() == QAbstractSocket::IPv6Protocol, true, 2000,
             { identity, uintCondition(FWPM_CONDITION_IP_REMOTE_PORT, port, FWP_UINT16),
               uintCondition(FWPM_CONDITION_IP_PROTOCOL, transport == QStringLiteral("udp") ? IPPROTO_UDP : IPPROTO_TCP, FWP_UINT8) }, &server)) { return false; }
    for (const bool v6 : { false, true }) {
        auto loopback = uintCondition(FWPM_CONDITION_FLAGS, FWP_CONDITION_FLAG_IS_LOOPBACK);
        loopback.matchType = FWP_MATCH_FLAGS_ALL_SET;
        if (!add(engine.handle, v6, true, 2100, { loopback })) { return false; }
        // DHCP нужен для восстановления uplink после смены сети, остальной UDP исключений не имеет.
        if (!add(engine.handle, v6, true, 2000,
                 { uintCondition(FWPM_CONDITION_IP_PROTOCOL, IPPROTO_UDP, FWP_UINT8),
                   uintCondition(FWPM_CONDITION_IP_LOCAL_PORT, v6 ? 546 : 68, FWP_UINT16),
                   uintCondition(FWPM_CONDITION_IP_REMOTE_PORT, v6 ? 547 : 67, FWP_UINT16) })) { return false; }
        // Системный DNS не должен уходить на физический адаптер через Direct-исключение.
        for (const auto dnsPort : { 53, 853 }) {
            if (!add(engine.handle, v6, false, 1300,
                     { uintCondition(FWPM_CONDITION_IP_REMOTE_PORT, dnsPort, FWP_UINT16) })) { return false; }
        }
        if (mode != 1 && !add(engine.handle, v6, false, 100)) { return false; }
    }
    for (const auto &prefix : sites) {
        if (mode != 0 && !add(engine.handle, prefix.address.protocol() == QAbstractSocket::IPv6Protocol,
                             mode == 2, mode == 2 ? 1100 : 100, {}, &prefix)) { return false; }
    }
    for (const auto &text : hop::directPrivatePrefixes(config.value(QStringLiteral("hopTrafficPolicy")).toObject())) {
        Prefix prefix;
        if (!parsePrefix(text, prefix) || !add(engine.handle, false, true, 1100, {}, &prefix)) { return false; }
    }
    // DNS VPN всегда защищён, даже при режиме «только выбранные сайты».
    for (const auto &prefix : dns) {
        if (!add(engine.handle, prefix.address.protocol() == QAbstractSocket::IPv6Protocol, false, 1200, {}, &prefix)) { return false; }
    }
    committed = checked(FwpmTransactionCommit0(engine.handle), "commit");
    if (!committed) { return false; }
    if (!luid.Value && !additional.Value) { return true; }
    Engine permits(true);
    if (!permits.handle || !checked(FwpmTransactionBegin0(permits.handle, 0), "begin tunnel permits")) { return false; }
    bool permitsCommitted = false;
    const auto abortPermits = qScopeGuard([&]() { if (!permitsCommitted) { FwpmTransactionAbort0(permits.handle); } });
    for (const bool v6 : { false, true }) {
        for (auto *interfaceLuid : { &luid, &additional }) {
            if (!interfaceLuid->Value) { continue; }
            // Проверяем реальный путь пакета, а не адаптер, которому принадлежит bind-адрес NAT.
            auto condition = uintCondition(FWPM_CONDITION_IP_NEXTHOP_INTERFACE, 0);
            condition.conditionValue.type = FWP_UINT64;
            condition.conditionValue.uint64 = &interfaceLuid->Value;
            const bool serviceOnly = interfaceLuid == &additional || (tunnelAlias == QStringLiteral(APP_WINDOWS_TUNNEL_INTERFACE)
                && config.value(QStringLiteral("hopPolicyTransport")).toBool());
            if (!add(permits.handle, v6, true, 1500, serviceOnly ? QList<FWPM_FILTER_CONDITION0> { condition, identity }
                : QList<FWPM_FILTER_CONDITION0> { condition }, nullptr, false)) { return false; }
        }
    }
    permitsCommitted = checked(FwpmTransactionCommit0(permits.handle), "commit tunnel permits");
    if (!permitsCommitted) { return false; }
    tunnelPermits().engine = permits.handle; permits.handle = nullptr;
    return true;
}

bool WindowsTrafficGuard::clear()
{
    Engine engine;
    if (!engine.handle || !checked(FwpmTransactionBegin0(engine.handle, 0), "begin clear")) { return false; }
    bool committed = false;
    const auto rollback = qScopeGuard([&]() { if (!committed) { FwpmTransactionAbort0(engine.handle); } });
    if (!removeFilters(engine.handle)) { return false; }
    committed = checked(FwpmTransactionCommit0(engine.handle), "commit clear");
    if (committed) { tunnelPermits().clear(); controlLeases().clear(); }
    return committed;
}

int WindowsTrafficGuard::filterCount()
{
    Engine engine;
    QList<UINT64> ids;
    return engine.handle && ownFilters(engine.handle, ids, true) ? static_cast<int>(ids.size()) : -1;
}

bool WindowsTrafficGuard::permitControlEndpoint(const QString &address, int port, bool udp)
{
    const QHostAddress host(address);
    if (QStringLiteral(APPLICATION_NAME) != QStringLiteral("Hop") || host.isNull() || host.isMulticast() || host.isBroadcast()
        || address.contains(QLatin1Char('/')) || address.contains(QLatin1Char('%')) || port < 1 || port > 65535 || (udp && port != 53)) { return false; }
    auto &leases = controlLeases();
    const auto key = address + QLatin1Char(':') + QString::number(port) + (udp ? QLatin1String("/udp") : QLatin1String("/tcp"));
    if (!leases.active.contains(key) && leases.active.size() >= 64) { return false; }
    Engine objects;
    if (!objects.handle || !ensureObjects(objects.handle)) { return false; }
    Engine engine(true);
    if (!engine.handle) { return false; }
    FWP_BYTE_BLOB *appId = nullptr;
    const auto path = QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + QStringLiteral("/Hop.exe"));
    if (!checked(FwpmGetAppIdFromFileName0(reinterpret_cast<PCWSTR>(path.utf16()), &appId), "control identity")) { return false; }
    const auto releaseApp = qScopeGuard([&]() { FwpmFreeMemory0(reinterpret_cast<void **>(&appId)); });
    auto identity = uintCondition(FWPM_CONDITION_ALE_APP_ID, 0);
    identity.conditionValue.type = FWP_BYTE_BLOB_TYPE; identity.conditionValue.byteBlob = appId;
    const bool v6 = host.protocol() == QAbstractSocket::IPv6Protocol;
    const Prefix prefix { host, v6 ? 128 : 32 };
    if (!add(engine.handle, v6, true, 2500, { identity,
        uintCondition(FWPM_CONDITION_IP_PROTOCOL, udp ? IPPROTO_UDP : IPPROTO_TCP, FWP_UINT8),
        uintCondition(FWPM_CONDITION_IP_REMOTE_PORT, port, FWP_UINT16) }, &prefix, false)) { return false; }
    if (leases.active.contains(key)) { FwpmEngineClose0(leases.active.value(key).engine); }
    leases.active.insert(key, { engine.handle, leases.clock.elapsed() + 45000 });
    engine.handle = nullptr;
    if (!leases.timer.isActive()) { leases.timer.start(); }
    return true;
}

namespace {
struct DirectSocketLeases {
    std::mutex mutex;
    HANDLE engine = nullptr;
    NET_LUID interfaceLuid {};
    QHostAddress source;
    QHash<quint64, UINT64> filters;
    quint64 sequence = 0;
    bool enabled = false;
    void close() {
        enabled = false; filters.clear();
        if (engine) { FwpmEngineClose0(engine); engine = nullptr; }
    }
    ~DirectSocketLeases() { close(); }
};
DirectSocketLeases &directSocketLeases() { static DirectSocketLeases leases; return leases; }
}
bool WindowsTrafficGuard::beginDirectPolicy(int physicalInterface, const QString &physicalAddress)
{
    const auto iface = QNetworkInterface::interfaceFromIndex(physicalInterface);
    const QHostAddress address(physicalAddress);
    if (QStringLiteral(APPLICATION_NAME) != QStringLiteral("Hop") || !iface.isValid()
        || iface.humanReadableName() == QStringLiteral(APP_WINDOWS_TUNNEL_INTERFACE)
        || iface.humanReadableName() == QStringLiteral(APP_WINDOWS_XRAY_INTERFACE)
        || address.protocol() != QAbstractSocket::IPv4Protocol || address.isLoopback() || address.isNull()
        || address.isMulticast() || address.isBroadcast()) { return false; }
    bool belongs = false;
    for (const auto &entry : iface.addressEntries()) { belongs |= entry.ip() == address; }
    NET_LUID luid {};
    if (!belongs || ConvertInterfaceIndexToLuid(physicalInterface, &luid) != NO_ERROR) { return false; }
    auto &leases = directSocketLeases(); std::lock_guard<std::mutex> lock(leases.mutex);
    leases.close(); leases.source = address; leases.interfaceLuid = luid; leases.enabled = true;
    return true;
}
void WindowsTrafficGuard::endDirectPolicy()
{
    auto &leases = directSocketLeases(); std::lock_guard<std::mutex> lock(leases.mutex); leases.close();
}
quint64 WindowsTrafficGuard::openDirectSocket(const QString &source, const QString &destination, int port, int protocol)
{
    const QHostAddress address(destination), local(source);
    if (address.protocol() != QAbstractSocket::IPv4Protocol || address.isNull() || address.isLoopback()
        || address.isMulticast() || address.isBroadcast() || destination.contains(QLatin1Char('%'))
        || port < 1 || port > 65535 || port == 53 || port == 853 || (protocol != IPPROTO_TCP && protocol != IPPROTO_UDP)) { return 0; }
    auto &leases = directSocketLeases(); std::lock_guard<std::mutex> lock(leases.mutex);
    if (!leases.enabled || local != leases.source || leases.filters.size() >= 512) { return 0; }
    if (!leases.engine) {
        Engine engine(true);
        if (!engine.handle || !ensureObjects(engine.handle)) { return 0; }
        leases.engine = engine.handle; engine.handle = nullptr;
    }
    FWP_BYTE_BLOB *appId = nullptr;
    const auto path = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    if (!checked(FwpmGetAppIdFromFileName0(reinterpret_cast<PCWSTR>(path.utf16()), &appId), "Direct socket identity")) { return 0; }
    const auto releaseApp = qScopeGuard([&]() { FwpmFreeMemory0(reinterpret_cast<void **>(&appId)); });
    auto identity = uintCondition(FWPM_CONDITION_ALE_APP_ID, 0);
    identity.conditionValue.type = FWP_BYTE_BLOB_TYPE; identity.conditionValue.byteBlob = appId;
    auto localInterface = uintCondition(FWPM_CONDITION_IP_LOCAL_INTERFACE, 0);
    localInterface.conditionValue.type = FWP_UINT64; localInterface.conditionValue.uint64 = &leases.interfaceLuid.Value;
    const Prefix endpoint { address, 32 };
    UINT64 filterId = 0;
    if (!add(leases.engine, false, true, 1100, { identity, localInterface,
        uintCondition(FWPM_CONDITION_IP_LOCAL_ADDRESS, local.toIPv4Address()),
        uintCondition(FWPM_CONDITION_IP_REMOTE_PORT, port, FWP_UINT16),
        uintCondition(FWPM_CONDITION_IP_PROTOCOL, protocol, FWP_UINT8) }, &endpoint, false, &filterId)) { return 0; }
    const auto lease = ++leases.sequence;
    leases.filters.insert(lease, filterId); return lease;
}
void WindowsTrafficGuard::closeDirectSocket(quint64 lease)
{
    auto &leases = directSocketLeases(); std::lock_guard<std::mutex> lock(leases.mutex);
    const auto found = leases.filters.find(lease);
    if (found == leases.filters.end()) { return; }
    const auto result = FwpmFilterDeleteById0(leases.engine, found.value());
    if (result != ERROR_SUCCESS && result != FWP_E_FILTER_NOT_FOUND) {
        // Ошибка удаления не оставляет разрешение после закрытия сокета.
        leases.close(); return;
    }
    leases.filters.erase(found);
}

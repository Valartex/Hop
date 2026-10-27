#pragma once
#include <fwpmu.h>
#include <QUuid>
#include <QScopeGuard>

inline int ownProtectionFiltersInVm(UINT64 dynamicWeight = 0)
{
    const GUID provider = QUuid(QStringLiteral("{dd311cd5-4193-46cb-8edc-353bf4efa9dd}"));
    HANDLE engine = nullptr;
    if (FwpmEngineOpen0(nullptr, RPC_C_AUTHN_WINNT, nullptr, nullptr, &engine) != ERROR_SUCCESS) { return -1; }
    const auto closeEngine = qScopeGuard([&]() { FwpmEngineClose0(engine); });
    HANDLE enumeration = nullptr;
    if (FwpmFilterCreateEnumHandle0(engine, nullptr, &enumeration) != ERROR_SUCCESS) { return -1; }
    const auto closeEnumeration = qScopeGuard([&]() { FwpmFilterDestroyEnumHandle0(engine, enumeration); });
    int found = 0;
    for (int page = 0; page < 128; ++page) {
        FWPM_FILTER0 **filters = nullptr; UINT32 count = 0;
        if (FwpmFilterEnum0(engine, enumeration, 256, &filters, &count) != ERROR_SUCCESS) { return -1; }
        const auto release = qScopeGuard([&]() { FwpmFreeMemory0(reinterpret_cast<void **>(&filters)); });
        for (UINT32 index = 0; index < count; ++index) {
            const auto &filter = *filters[index];
            if (filter.providerKey && IsEqualGUID(*filter.providerKey, provider)
                && (!dynamicWeight || (!(filter.flags & FWPM_FILTER_FLAG_PERSISTENT)
                    && filter.weight.type == FWP_UINT64 && *filter.weight.uint64 == dynamicWeight))) { ++found; }
        }
        if (count < 256) { return found; }
    }
    return -1;
}

// Читается только собственный подслой. Шесть условий не дают расширить Direct на другой процесс/endpoint.
inline int directSocketFiltersInVm(quint16 expectedPort, bool &valid)
{
    const GUID provider = QUuid(QStringLiteral("{dd311cd5-4193-46cb-8edc-353bf4efa9dd}"));
    HANDLE engine = nullptr;
    if (FwpmEngineOpen0(nullptr, RPC_C_AUTHN_WINNT, nullptr, nullptr, &engine) != ERROR_SUCCESS) { return -1; }
    const auto closeEngine = qScopeGuard([&](){FwpmEngineClose0(engine);});
    HANDLE enumeration = nullptr;
    if (FwpmFilterCreateEnumHandle0(engine, nullptr, &enumeration) != ERROR_SUCCESS) { return -1; }
    const auto closeEnumeration = qScopeGuard([&](){FwpmFilterDestroyEnumHandle0(engine,enumeration);});
    int found = 0;
    for (int page = 0; page < 128; ++page) {
        FWPM_FILTER0 **filters = nullptr; UINT32 count = 0;
        if (FwpmFilterEnum0(engine, enumeration, 256, &filters, &count) != ERROR_SUCCESS) { return -1; }
        const auto release = qScopeGuard([&](){FwpmFreeMemory0(reinterpret_cast<void **>(&filters));});
        for (UINT32 index = 0; index < count; ++index) {
            const auto &filter = *filters[index];
            if (!filter.providerKey || !IsEqualGUID(*filter.providerKey, provider) || (filter.flags & FWPM_FILTER_FLAG_PERSISTENT)) { continue; }
            if (filter.weight.type != FWP_UINT64 || *filter.weight.uint64 != 1100) { continue; }
            bool identity = false, address = false, port = false, protocol = false, source = false, interfaceMatched = false;
            for (UINT32 field = 0; field < filter.numFilterConditions; ++field) {
                const auto &entry = filter.filterCondition[field]; const auto &value = entry.conditionValue;
                if (entry.fieldKey == FWPM_CONDITION_ALE_APP_ID && value.type == FWP_BYTE_BLOB_TYPE) {
                    const auto *blob = value.byteBlob;
                    const auto name = QString::fromWCharArray(reinterpret_cast<const wchar_t *>(blob->data), blob->size/sizeof(wchar_t)).remove(QChar::Null);
                    identity = name.endsWith(QStringLiteral("\\hop-service.exe"),Qt::CaseInsensitive);
                }
                if (entry.fieldKey == FWPM_CONDITION_IP_REMOTE_ADDRESS && value.type == FWP_V4_ADDR_MASK) {
                    address = value.v4AddrMask->mask == 0xffffffff && value.v4AddrMask->addr == QHostAddress("10.0.2.2").toIPv4Address();
                }
                if (entry.fieldKey == FWPM_CONDITION_IP_REMOTE_PORT && value.type == FWP_UINT16) { port = value.uint16 == expectedPort; }
                if (entry.fieldKey == FWPM_CONDITION_IP_PROTOCOL && value.type == FWP_UINT8) { protocol = value.uint8 == IPPROTO_TCP; }
                if (entry.fieldKey == FWPM_CONDITION_IP_LOCAL_ADDRESS && value.type == FWP_UINT32) { source = value.uint32 != 0; }
                if (entry.fieldKey == FWPM_CONDITION_IP_LOCAL_INTERFACE && value.type == FWP_UINT64) { interfaceMatched = *value.uint64 != 0; }
            }
            if (!identity) { continue; }
            ++found; valid = valid && address && port && protocol && source && interfaceMatched && filter.numFilterConditions == 6;
        }
        if (count < 256) { return found; }
    }
    return -1;
}

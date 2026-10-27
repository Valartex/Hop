#pragma once
#include <fwpmu.h>
#include <QElapsedTimer>
#include "providerVmProbe.h"

inline int inspectControlLeases(bool &valid, int &persistent)
{
    valid = true; persistent = 0;
    HANDLE engine = nullptr, enumeration = nullptr;
    if (FwpmEngineOpen0(nullptr, RPC_C_AUTHN_WINNT, nullptr, nullptr, &engine) != ERROR_SUCCESS) { return -1; }
    const auto close = qScopeGuard([&]() { if (enumeration) { FwpmFilterDestroyEnumHandle0(engine, enumeration); } FwpmEngineClose0(engine); });
    if (FwpmFilterCreateEnumHandle0(engine, nullptr, &enumeration) != ERROR_SUCCESS) { return -1; }
    const GUID provider = QUuid(QStringLiteral("{dd311cd5-4193-46cb-8edc-353bf4efa9dd}"));
    int dynamic = 0;
    for (;;) {
        FWPM_FILTER0 **filters = nullptr; UINT32 count = 0;
        if (FwpmFilterEnum0(engine, enumeration, 256, &filters, &count) != ERROR_SUCCESS) { return -1; }
        const auto free = qScopeGuard([&]() { if (filters) { FwpmFreeMemory0(reinterpret_cast<void **>(&filters)); } });
        for (UINT32 index = 0; index < count; ++index) {
            const auto &filter = *filters[index];
            if (!filter.providerKey || !IsEqualGUID(*filter.providerKey, provider)) { continue; }
            if (filter.flags & FWPM_FILTER_FLAG_PERSISTENT) { ++persistent; continue; }
            ++dynamic;
            bool address = false, port = false, protocol = false, identity = false;
            for (UINT32 condition = 0; condition < filter.numFilterConditions; ++condition) {
                const auto &entry = filter.filterCondition[condition];
                if (entry.matchType != FWP_MATCH_EQUAL) { valid = false; }
                if (IsEqualGUID(entry.fieldKey, FWPM_CONDITION_IP_REMOTE_ADDRESS) && entry.conditionValue.type == FWP_V4_ADDR_MASK) {
                    address = entry.conditionValue.v4AddrMask->mask == 0xffffffff && entry.conditionValue.v4AddrMask->addr == 0x01010101;
                }
                if (IsEqualGUID(entry.fieldKey, FWPM_CONDITION_IP_REMOTE_PORT)) { port = entry.conditionValue.type == FWP_UINT16 && entry.conditionValue.uint16 == 443; }
                if (IsEqualGUID(entry.fieldKey, FWPM_CONDITION_IP_PROTOCOL)) { protocol = entry.conditionValue.type == FWP_UINT8 && entry.conditionValue.uint8 == IPPROTO_TCP; }
                if (IsEqualGUID(entry.fieldKey, FWPM_CONDITION_ALE_APP_ID) && entry.conditionValue.type == FWP_BYTE_BLOB_TYPE) {
                    const auto *blob = entry.conditionValue.byteBlob;
                    const auto name = QString::fromWCharArray(reinterpret_cast<const wchar_t *>(blob->data), blob->size / sizeof(wchar_t)).remove(QChar::Null);
                    identity = name.endsWith(QStringLiteral("\\hop.exe"), Qt::CaseInsensitive);
                }
            }
            valid = valid && address && port && protocol && identity && filter.numFilterConditions == 4;
        }
        if (count < 256) { return dynamic; }
    }
}

inline int probeControlLeaseInVm(QTextStream &output, HANDLE serviceProcess)
{
    const QJsonObject guard { { QStringLiteral("vpnServer"), QStringLiteral("192.0.2.99") }, { QStringLiteral("hopTransportPort"), 9 },
        { QStringLiteral("hopTransportProtocol"), QStringLiteral("udp") }, { QStringLiteral("killSwitchOption"), true }, { QStringLiteral("splitTunnelType"), 0 } };
    const bool applied = IpcClient::withInterface([&](QSharedPointer<IpcInterfaceReplica> iface) {
        auto reply = iface->applyTrafficGuard(guard, QString()); return reply.waitForFinished(5000) && reply.returnValue();
    }, []() { return false; });
    if (!applied || !serviceProcess || !hop::controlPlane::permit(QStringLiteral("1.1.1.1"), 443)) { return 10; }
    bool valid = false; int persistent = 0;
    const auto count = inspectControlLeases(valid, persistent);
    output << "Control lease: dynamic=" << count << ", persistent=" << persistent << ", exact app/IP/port/protocol=" << valid << Qt::endl;
    if (count != 1 || !valid || persistent <= 0 || hop::controlPlane::permit(QStringLiteral("1.1.1.1"), 8443, true)
        || !awgPhysicalRequest(output, QStringLiteral("Same endpoint remains blocked for another process"), QStringLiteral("1.1.1.1"), false)) { return 10; }
    // Повторное применение политики не удаляет чужую динамическую сессию и не меняет область разрешения.
    const bool replaced = IpcClient::withInterface([&](QSharedPointer<IpcInterfaceReplica> iface) {
        auto reply = iface->applyTrafficGuard(guard, QString()); return reply.waitForFinished(5000) && reply.returnValue();
    }, []() { return false; });
    if (!replaced || inspectControlLeases(valid, persistent) != 1 || !valid) { return 10; }
    QEventLoop expiration;
    QTimer::singleShot(47000, &expiration, &QEventLoop::quit);
    expiration.exec(QEventLoop::ExcludeUserInputEvents);
    const auto expired = inspectControlLeases(valid, persistent);
    output << "Control lease after TTL: dynamic=" << expired << ", persistent=" << persistent << Qt::endl;
    if (expired != 0 || persistent <= 0 || !hop::controlPlane::permit(QStringLiteral("1.1.1.1"), 443)) { return 10; }
    if (!TerminateProcess(serviceProcess, 1) || WaitForSingleObject(serviceProcess, 10000) != WAIT_OBJECT_0) { return 10; }
    QEventLoop settle;
    QTimer::singleShot(1500, &settle, &QEventLoop::quit); settle.exec();
    const auto crashed = inspectControlLeases(valid, persistent);
    output << "Control lease after own service crash: dynamic=" << crashed << ", persistent=" << persistent << Qt::endl;
    return crashed == 0 && persistent > 0 && awgPhysicalRequest(output, QStringLiteral("Protection remains after control lease crash"), QStringLiteral("1.1.1.1"), false) ? 0 : 10;
}

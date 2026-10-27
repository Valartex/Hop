#pragma once
#include "awgFaultProbe.h"
#include <fwpmu.h>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>

// Только тестовая VM: файл содержит идентификаторы фильтров Hop, но не VPN-профиль.
inline QString bootGuardStatePath()
{
    return QStringLiteral("//VBOXSVR/HopTestResults/HopJobs/boot-guard-state.json");
}

inline QJsonObject bootGuardSnapshot(bool includeDisabled = false)
{
    HANDLE engine = nullptr, enumeration = nullptr;
    if (FwpmEngineOpen0(nullptr, RPC_C_AUTHN_WINNT, nullptr, nullptr, &engine) != ERROR_SUCCESS) { return {}; }
    const auto close = qScopeGuard([&]() {
        if (enumeration) { FwpmFilterDestroyEnumHandle0(engine, enumeration); }
        FwpmEngineClose0(engine);
    });
    const GUID provider = QUuid(QStringLiteral("{dd311cd5-4193-46cb-8edc-353bf4efa9dd}"));
    const GUID layer = QUuid(QStringLiteral("{bbf57b62-61a8-4288-9c28-6b6f67af24d3}"));
    FWPM_FILTER_ENUM_TEMPLATE0 options {};
    options.providerKey = const_cast<GUID *>(&provider);
    options.actionMask = 0xffffffff;
    options.flags = FWP_FILTER_ENUM_FLAG_INCLUDE_DISABLED;
    QStringList keys;
    int blocked = 0, disabled = 0, dynamic = 0;
    for (const auto &filterLayer : { FWPM_LAYER_ALE_AUTH_CONNECT_V4, FWPM_LAYER_ALE_AUTH_CONNECT_V6 }) {
    options.layerKey = filterLayer;
    options.flags = includeDisabled ? FWP_FILTER_ENUM_FLAG_INCLUDE_DISABLED : 0;
    if (FwpmFilterCreateEnumHandle0(engine, &options, &enumeration) != ERROR_SUCCESS) { return {}; }
    for (;;) {
        FWPM_FILTER0 **filters = nullptr; UINT32 count = 0;
        if (FwpmFilterEnum0(engine, enumeration, 256, &filters, &count) != ERROR_SUCCESS) { return {}; }
        const auto release = qScopeGuard([&]() { if (filters) { FwpmFreeMemory0(reinterpret_cast<void **>(&filters)); } });
        for (UINT32 index = 0; index < count; ++index) {
            const auto &filter = *filters[index];
            if (!filter.providerKey || !IsEqualGUID(*filter.providerKey, provider) || !IsEqualGUID(filter.subLayerKey, layer)) { continue; }
            if (!(filter.flags & FWPM_FILTER_FLAG_PERSISTENT)) { ++dynamic; continue; }
            keys.append(QUuid(filter.filterKey).toString(QUuid::WithoutBraces));
            blocked += filter.action.type == FWP_ACTION_BLOCK;
            disabled += (filter.flags & FWPM_FILTER_FLAG_DISABLED) != 0;
        }
        if (count < 256) { break; }
    }
    FwpmFilterDestroyEnumHandle0(engine, enumeration); enumeration = nullptr;
    }
    std::sort(keys.begin(), keys.end());
    return { { "keys", QJsonArray::fromStringList(keys) }, { "blocked", blocked },
             { "disabled", disabled }, { "dynamic", dynamic } };
}

inline bool saveBootGuardState(const QJsonObject &state)
{
    QSaveFile file(bootGuardStatePath());
    const auto bytes = QJsonDocument(state).toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

inline QJsonArray guardRegistryStorageInventory(const QJsonArray &keys)
{
    QJsonArray result;
    for (const auto *name : { L"Filter", L"Provider", L"SubLayer" }) {
        const auto path = std::wstring(L"SYSTEM\\CurrentControlSet\\Services\\BFE\\Parameters\\Policy\\Persistent\\") + name;
        HKEY key = nullptr;
        const auto opened = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, &key);
        QJsonObject entry { { "kind", QString::fromWCharArray(name) }, { "openStatus", int(opened) } };
        if (opened == ERROR_SUCCESS) {
            DWORD subkeys = 0, values = 0;
            RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr);
            int own = 0;
            for (const auto &value : keys) {
                const auto guid = QUuid(value.toString()).toString(); DWORD size = 0, type = 0;
                if (RegQueryValueExW(key, reinterpret_cast<LPCWSTR>(guid.utf16()), nullptr, &type, nullptr, &size) == ERROR_SUCCESS) { ++own; }
            }
            entry.insert("values", int(values)); entry.insert("subkeys", int(subkeys)); entry.insert("ownFilterValues", own);
            RegCloseKey(key);
        }
        result.append(entry);
    }
    return result;
}

// Только чтение метаданных тестового гостя. Содержимое ключей, URL и командные строки не выводятся.
inline int probeBootDiagnosticsInVm(QTextStream &output)
{
    const auto script = QString::fromLatin1(R"Hop(
$ErrorActionPreference = 'Stop'
$result = [ordered]@{}
$select = Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\Select'
$result.controlSet = @{ current=$select.Current; default=$select.Default; lastKnownGood=$select.LastKnownGood; failed=$select.Failed }
$result.ownProcesses = @(Get-CimInstance Win32_Process -Filter "Name='Hop.exe' OR Name='Hop-service.exe' OR Name='hop_guard_recover.exe'" | ForEach-Object { @{ name=$_.Name; pid=$_.ProcessId; created=$_.CreationDate.ToUniversalTime().ToString('o') } })
$result.ownRunEntries = @()
$bfe = Get-Service -Name 'BFE'
$result.bfe = @{ status=[string]$bfe.Status; canStop=$bfe.CanStop; dependents=@($bfe.DependentServices | ForEach-Object { @{ name=$_.ServiceName; status=[string]$_.Status; canStop=$_.CanStop } }) }
foreach ($key in @('HKCU:\Software\Microsoft\Windows\CurrentVersion\Run','HKLM:\Software\Microsoft\Windows\CurrentVersion\Run')) {
    if (Test-Path -LiteralPath $key) {
        $values = Get-ItemProperty -LiteralPath $key
        $result.ownRunEntries += @($values.PSObject.Properties | Where-Object { $_.Name -eq 'Hop' -or [string]$_.Value -match '(?i)\\Hop\.exe' } | ForEach-Object { @{ hive=$key.Substring(0,4); name=$_.Name } })
    }
}
$result.ownAudit = @()
try {
    $events = @(Get-WinEvent -FilterHashtable @{ LogName='Security'; Id=5441,5447; StartTime=[DateTime]::Now.AddHours(-3) } -MaxEvents 3000 -ErrorAction Stop)
    $result.auditAvailable = $true
    foreach ($event in $events) {
        $data = @{}; ([xml]$event.ToXml()).Event.EventData.Data | ForEach-Object { $data[$_.Name] = [string]$_.'#text' }
        if ($data.ProviderKey -eq '{dd311cd5-4193-46cb-8edc-353bf4efa9dd}') {
            $result.ownAudit += @{ time=$event.TimeCreated.ToUniversalTime().ToString('o'); id=$event.Id; pid=$data.ProcessId; change=$data.ChangeType; key=$data.FilterKey; type=$data.FilterType }
        }
    }
} catch { $result.auditAvailable = $false; $result.auditError = $_.FullyQualifiedErrorId.Split(',')[0] }
$result | ConvertTo-Json -Depth 5 -Compress
)Hop");
    QProcess command;
    command.start(QStringLiteral("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"),
        { QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"), script });
    if (!command.waitForFinished(15000)) { command.kill(); command.waitForFinished(2000); return 14; }
    output << "Boot diagnostic metadata: " << QString::fromUtf8(command.readAllStandardOutput().left(32000)) << Qt::endl;
    output << "Boot diagnostic exit: " << command.exitCode() << Qt::endl;
    return command.exitCode() == 0 ? 0 : 14;
}

// Наблюдение выполняется ДО обычной начальной очистки тестового probe.
inline QJsonObject inspectPreparedBootGuard(QTextStream &output)
{
    QFile file(bootGuardStatePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly) || file.size() > 65536) { return {}; }
    auto state = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    if (state.value("phase") != QStringLiteral("prepared")) { return {}; }
    const auto age = QDateTime::fromString(state.value("preparedAtUtc").toString(), Qt::ISODateWithMs).secsTo(QDateTime::currentDateTimeUtc());
    if (age < 0 || age > 6 * 60 * 60 || state.value("nonce").toString().size() != 32) { return {}; }
    const auto snapshot = bootGuardSnapshot();
    const auto all = bootGuardSnapshot(true);
    output << "Post-boot own registry storage metadata: " << QJsonDocument(guardRegistryStorageInventory(state.value("keys").toArray())).toJson(QJsonDocument::Compact) << Qt::endl;
    output << "Post-boot own filters including disabled: " << all.value("keys").toArray().size()
        << "; disabled=" << all.value("disabled").toInt(-1) << Qt::endl;
    HANDLE engine = nullptr;
    if (FwpmEngineOpen0(nullptr, RPC_C_AUTHN_WINNT, nullptr, nullptr, &engine) == ERROR_SUCCESS) {
        const GUID provider = QUuid(QStringLiteral("{dd311cd5-4193-46cb-8edc-353bf4efa9dd}"));
        FWPM_PROVIDER0 *data = nullptr;
        const auto status = FwpmProviderGetByKey0(engine, &provider, &data);
        output << "Post-boot provider lookup: " << status;
        if (data) {
            output << "; flags=" << data->flags << "; service=" << (data->serviceName ? QString::fromWCharArray(data->serviceName) : QStringLiteral("<none>"));
            FwpmFreeMemory0(reinterpret_cast<void **>(&data));
        }
        output << Qt::endl; FwpmEngineClose0(engine);
    }
    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    const auto service = manager ? OpenServiceW(manager, L"Hop-service", SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG) : nullptr;
    if (service) {
        SERVICE_STATUS status {}; DWORD size = 0; QueryServiceConfigW(service, nullptr, 0, &size);
        QByteArray buffer(size <= 65536 ? size : 0, Qt::Uninitialized);
        auto *configuration = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(buffer.data());
        if (!buffer.isEmpty() && QueryServiceConfigW(service, configuration, DWORD(buffer.size()), &size) && QueryServiceStatus(service, &status)) {
            output << "Post-boot Hop SCM state/start/win32/service error: " << status.dwCurrentState << "/" << configuration->dwStartType
                << "/" << status.dwWin32ExitCode << "/" << status.dwServiceSpecificExitCode << Qt::endl;
        }
        CloseServiceHandle(service);
    } else { output << "Post-boot Hop SCM lookup error: " << GetLastError() << Qt::endl; }
    if (manager) { CloseServiceHandle(manager); }
    const bool rebooted = double(GetTickCount64()) < state.value("uptimeMs").toDouble();
    const bool retained = rebooted && !snapshot.isEmpty() && !state.value("keys").toArray().isEmpty()
        && snapshot.value("keys") == state.value("keys") && snapshot.value("disabled").toInt(-1) == 0
        && snapshot.value("blocked").toInt() == state.value("blocked").toInt();
    output << "Post-boot original Hop filter keys retained and enabled: " << retained
        << "; reboot observed=" << rebooted << "; filters=" << snapshot.value("keys").toArray().size() << Qt::endl;
    // Положительный Direct и отрицательный VPN-путь проверяются до удаления фильтров.
    const bool direct = retained && awgPhysicalRequest(output, "Post-boot Direct", "1.1.1.1", true);
    const bool blocked = retained && awgPhysicalRequest(output, "Post-boot protected physical path", "1.0.0.1", false);
    state.insert("phase", "observed"); state.insert("retained", retained);
    state.insert("allPostBootFilters", all);
    state.insert("direct", direct); state.insert("protected", blocked);
    state.insert("observedAtUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!saveBootGuardState(state)) { return {}; }
    return state;
}

inline int prepareBootGuardInVm(QTextStream &output)
{
    const QJsonObject guard { { "vpnServer", "192.0.2.99" }, { "hopTransportPort", 443 },
        { "killSwitchOption", true }, { "splitTunnelType", 2 }, { "splitTunnelSites", QJsonArray { "1.1.1.1/32" } } };
    const auto clear = qScopeGuard([]() { IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) {
        auto reply = iface->clearTrafficGuard(); reply.waitForFinished(5000);
    }); });
    const bool applied = IpcClient::withInterface([&](QSharedPointer<IpcInterfaceReplica> iface) {
        auto reply = iface->applyTrafficGuard(guard, QString()); return reply.waitForFinished(5000) && reply.returnValue();
    }, []() { return false; });
    if (!applied || !awgPhysicalRequest(output, "Pre-boot Direct", "1.1.1.1", true)
        || !awgPhysicalRequest(output, "Pre-boot protected physical path", "1.0.0.1", false)) { return 14; }
    // Отдельный диагностический повтор: отличаем немедленный сброс после commit от устоявшейся политики.
    // Пауза не считается исправлением первоначального отрицательного результата.
    QEventLoop settle; QTimer::singleShot(30000, &settle, &QEventLoop::quit); settle.exec(QEventLoop::ExcludeUserInputEvents);
    auto state = bootGuardSnapshot();
    if (state.value("keys").toArray().isEmpty() || state.value("blocked").toInt() < 1
        || state.value("disabled").toInt(-1) != 0 || state.value("dynamic").toInt(-1) != 0) { return 14; }
    state.insert("phase", "prepared"); state.insert("nonce", QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-'));
    state.insert("uptimeMs", double(GetTickCount64()));
    state.insert("settledMs", 30000);
    state.insert("preparedAtUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!saveBootGuardState(state)) { return 14; }
    output << "Hop boot guard prepared; bounded wait for test VM restart" << Qt::endl;
    QEventLoop wait; QTimer::singleShot(60000, &wait, &QEventLoop::quit); wait.exec(QEventLoop::ExcludeUserInputEvents);
    // Если перезапуска не было, обычная очистка выполняется здесь и агентом в finally.
    state.insert("phase", "expired"); saveBootGuardState(state);
    return 14;
}

inline int probeGuardScmRestartInVm(QTextStream &output)
{
    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) { return 14; }
    const auto closeManager = qScopeGuard([&]() { CloseServiceHandle(manager); });
    const auto service = OpenServiceW(manager, L"Hop-service", SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS | SERVICE_STOP | SERVICE_START);
    if (!service) { return 14; }
    const auto closeService = qScopeGuard([&]() { CloseServiceHandle(service); });
    DWORD size = 0; QueryServiceConfigW(service, nullptr, 0, &size);
    if (size < sizeof(QUERY_SERVICE_CONFIGW) || size > 65536) { return 14; }
    QByteArray buffer(size, Qt::Uninitialized); auto *config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(buffer.data());
    if (!QueryServiceConfigW(service, config, size, &size) || config->dwStartType != SERVICE_AUTO_START
        || QDir::fromNativeSeparators(QString::fromWCharArray(config->lpBinaryPathName)).compare(
            QStringLiteral("\"C:/Program Files/Hop VM Install/Hop-service.exe\""), Qt::CaseInsensitive) != 0) { return 14; }
    const auto waitState = [&](DWORD wanted) {
        QElapsedTimer timer; timer.start();
        do {
            SERVICE_STATUS status {};
            if (!QueryServiceStatus(service, &status)) { return false; }
            if (status.dwCurrentState == wanted) { return true; }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50); QThread::msleep(50);
        } while (timer.elapsed() < 15000);
        return false;
    };
    bool restartNeeded = false;
    const auto cleanup = qScopeGuard([&]() {
        if (restartNeeded) { StartServiceW(service, 0, nullptr); waitState(SERVICE_RUNNING); }
        IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) { auto reply = iface->clearTrafficGuard(); reply.waitForFinished(5000); });
    });
    const QJsonObject guard { { "vpnServer", "192.0.2.99" }, { "hopTransportPort", 443 },
        { "killSwitchOption", true }, { "splitTunnelType", 2 }, { "splitTunnelSites", QJsonArray { "1.1.1.1/32" } } };
    if (!IpcClient::withInterface([&](QSharedPointer<IpcInterfaceReplica> iface) {
        auto reply = iface->applyTrafficGuard(guard, QString()); return reply.waitForFinished(5000) && reply.returnValue();
    }, []() { return false; })) { return 14; }
    const auto before = bootGuardSnapshot(true);
    if (before.value("keys").toArray().isEmpty()) { return 14; }
    output << "Own guard registry storage metadata: " << QJsonDocument(guardRegistryStorageInventory(before.value("keys").toArray())).toJson(QJsonDocument::Compact) << Qt::endl;
    SERVICE_STATUS status {}; restartNeeded = true;
    const auto stopped = ControlService(service, SERVICE_CONTROL_STOP, &status);
    if (!stopped && GetLastError() != ERROR_BROKEN_PIPE) { return 14; }
    if (!waitState(SERVICE_STOPPED)) { return 14; }
    const auto afterStop = bootGuardSnapshot(true);
    const bool retainedOnStop = before.value("keys") == afterStop.value("keys") && afterStop.value("disabled").toInt(-1) == 0;
    output << "Own SCM stop retained guard: " << retainedOnStop << Qt::endl;
    if (!StartServiceW(service, 0, nullptr) || !waitState(SERVICE_RUNNING)) { return 14; }
    restartNeeded = false;
    const auto afterStart = bootGuardSnapshot(true);
    const bool retainedOnStart = before.value("keys") == afterStart.value("keys") && afterStart.value("disabled").toInt(-1) == 0;
    output << "Own SCM start retained guard: " << retainedOnStart << Qt::endl;
    const bool direct = awgPhysicalRequest(output, "SCM restart Direct", "1.1.1.1", true);
    const bool blocked = awgPhysicalRequest(output, "SCM restart protected path", "1.0.0.1", false);
    return retainedOnStop && retainedOnStart && direct && blocked ? 0 : 14;
}

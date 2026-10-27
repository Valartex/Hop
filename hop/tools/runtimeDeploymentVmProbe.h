#pragma once
#include <QSaveFile>

// Только замена клиента, службы и контрольного измерителя внутри проверенной VirtualBox VM.
// Служба/каталог уже существуют; установщик, регистрация службы и сетевые настройки не меняются.
inline int stageRuntimeInVm(HANDLE consoleService, QTextStream &output)
{
    const auto root = QStringLiteral("C:/Program Files/Hop VM Install");
    if (!consoleService || ownProtectionFiltersInVm() != 0) { return 7; }
    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) { return 7; }
    const auto closeManager = qScopeGuard([&]() { CloseServiceHandle(manager); });
    const auto service = OpenServiceW(manager, L"Hop-service", SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP);
    if (!service) { return 7; }
    const auto closeService = qScopeGuard([&]() { CloseServiceHandle(service); });
    DWORD size = 0; QueryServiceConfigW(service, nullptr, 0, &size);
    if (size < sizeof(QUERY_SERVICE_CONFIGW) || size > 65536) { return 7; }
    QByteArray configBytes(size, Qt::Uninitialized); auto *config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(configBytes.data());
    if (!QueryServiceConfigW(service, config, size, &size) || config->dwStartType != SERVICE_AUTO_START
        || QDir::fromNativeSeparators(QString::fromWCharArray(config->lpBinaryPathName)).compare(
            QLatin1Char('"') + root + QStringLiteral("/Hop-service.exe\""), Qt::CaseInsensitive) != 0) { return 7; }
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) { return 7; }
    PROCESSENTRY32W entry {}; entry.dwSize = sizeof(entry);
    bool guiRunning = false;
    if (Process32FirstW(snapshot, &entry)) { do {
        if (QString::fromWCharArray(entry.szExeFile).compare("Hop.exe", Qt::CaseInsensitive)) { continue; }
        const auto handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!handle) { guiRunning = true; break; }
        wchar_t path[32768]; DWORD length = 32768;
        const bool read = QueryFullProcessImageNameW(handle, 0, path, &length); CloseHandle(handle);
        if (!read || QDir::fromNativeSeparators(QString::fromWCharArray(path, length)).compare(root + "/Hop.exe", Qt::CaseInsensitive) == 0) { guiRunning = true; break; }
    } while (Process32NextW(snapshot, &entry)); } else { guiRunning = true; }
    CloseHandle(snapshot);
    if (guiRunning) { output << "Runtime staging refuses an active GUI" << Qt::endl; return 7; }
    // Откат изолированной NAT-пробы: экспериментальные weak-host флаги не входят в клиент.
    NET_LUID tunLuid {};
    if (ConvertInterfaceAliasToLuid(L"Hop-TUN", &tunLuid) == NO_ERROR) {
        MIB_IPINTERFACE_ROW tunRow {}; InitializeIpInterfaceEntry(&tunRow);
        tunRow.Family = AF_INET; tunRow.InterfaceLuid = tunLuid;
        if (GetIpInterfaceEntry(&tunRow) == NO_ERROR && (tunRow.WeakHostSend || tunRow.WeakHostReceive)) {
            tunRow.WeakHostSend = false; tunRow.WeakHostReceive = false; tunRow.SitePrefixLength = 0;
            if (SetIpInterfaceEntry(&tunRow) != NO_ERROR) { return 7; }
            output << "VM own Hop-TUN strong-host defaults restored" << Qt::endl;
        }
    }
    QFile manifest(QCoreApplication::applicationDirPath() + "/BUILD-MANIFEST.json");
    if (!manifest.open(QIODevice::ReadOnly)) { return 7; }
    const auto hashes = QJsonDocument::fromJson(manifest.readAll()).object().value("hashes").toObject();
    struct File { QString name; QByteArray previous, candidate; };
    QList<File> files;
    for (const auto &name : { QStringLiteral("Hop.exe"), QStringLiteral("Hop-service.exe"), QStringLiteral("hop_tunnel_probe.exe") }) {
        QFile source(QCoreApplication::applicationDirPath() + '/' + name), target(root + '/' + name);
        if (!source.open(QIODevice::ReadOnly) || !target.open(QIODevice::ReadOnly)
            || source.size() > 64 * 1024 * 1024 || target.size() > 64 * 1024 * 1024) { return 7; }
        const auto candidate = source.readAll();
        if (QCryptographicHash::hash(candidate, QCryptographicHash::Sha256).toHex() != hashes.value(name).toString().toLatin1()) { return 7; }
        files.append({ name, target.readAll(), candidate });
    }
    if (!TerminateProcess(consoleService, 95) || WaitForSingleObject(consoleService, 5000) != WAIT_OBJECT_0) { return 7; }
    const auto waitState = [&](DWORD state) {
        for (int attempt = 0; attempt < 150; ++attempt) {
            SERVICE_STATUS status {}; if (!QueryServiceStatus(service, &status)) { return false; }
            if (status.dwCurrentState == state) { return true; }
            QThread::msleep(100);
        }
        return false;
    };
    SERVICE_STATUS status {}; ControlService(service, SERVICE_CONTROL_STOP, &status);
    if (!waitState(SERVICE_STOPPED)) { return 7; }
    const auto write = [&](const QString &name, const QByteArray &bytes) {
        QSaveFile file(root + '/' + name);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
    };
    bool accepted = false;
    const auto restore = qScopeGuard([&]() {
        if (accepted) { return; }
        ControlService(service, SERVICE_CONTROL_STOP, &status); waitState(SERVICE_STOPPED);
        bool restored = true;
        for (const auto &file : files) { restored = write(file.name, file.previous) && restored; }
        StartServiceW(service, 0, nullptr);
        output << "Previous VM runtime restored/running: " << restored << "/" << waitState(SERVICE_RUNNING) << Qt::endl;
    });
    for (const auto &file : files) { if (!write(file.name, file.candidate)) { return 7; } }
    if (!StartServiceW(service, 0, nullptr) || !waitState(SERVICE_RUNNING)) { return 7; }
    for (const auto &file : files) {
        QFile installed(root + '/' + file.name);
        if (!installed.open(QIODevice::ReadOnly) || installed.readAll() != file.candidate) { return 7; }
        output << "VM candidate " << file.name << " SHA256 " << QCryptographicHash::hash(file.candidate, QCryptographicHash::Sha256).toHex() << Qt::endl;
    }
    accepted = ownProtectionFiltersInVm() == 0;
    output << "VM client runtime staged; service running; protection clear: " << accepted << Qt::endl;
    return accepted ? 0 : 7;
}

#pragma once
#ifdef Q_OS_WIN
#include <windows.h>
#include <QCoreApplication>
#include <QDir>
#include <QLocalSocket>
#include <QScopeGuard>
#include <array>
#include "version.h"

// WorldAccess нужен обычному GUI, но не даёт постороннему EXE управлять службой.
inline bool isTrustedHopIpcPeer(QLocalSocket *socket)
{
    if (QStringLiteral(APPLICATION_NAME) != QStringLiteral("Hop")) { return true; }
    if (!socket || socket->socketDescriptor() == -1) { return false; }
    ULONG pid = 0;
    if (!GetNamedPipeClientProcessId(reinterpret_cast<HANDLE>(socket->socketDescriptor()), &pid) || !pid) { return false; }
    const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) { return false; }
    const auto close = qScopeGuard([&]() { CloseHandle(process); });
    std::array<wchar_t, 32768> path {}; DWORD length = path.size();
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &length)) { return false; }
    const auto image = QDir::cleanPath(QDir::fromNativeSeparators(QString::fromWCharArray(path.data(), length)));
    const auto client = QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/Hop.exe"));
    if (image.compare(client, Qt::CaseInsensitive) == 0) { return true; }
    static const bool vm = []() {
        constexpr DWORD provider = 0x52534d42;
        const DWORD size = GetSystemFirmwareTable(provider, 0, nullptr, 0);
        if (size < 8 || size > 16 * 1024 * 1024) { return false; }
        QByteArray firmware(size, Qt::Uninitialized);
        return GetSystemFirmwareTable(provider, 0, firmware.data(), size) == size && firmware.toLower().contains("virtualbox");
    }();
    // Единственный дополнительный клиент — защищённый каталог ограниченного VM-агента.
    return vm && image.compare(QStringLiteral("C:/Program Files/Hop Test/hop_windows_vpn_probe.exe"), Qt::CaseInsensitive) == 0;
}
#endif

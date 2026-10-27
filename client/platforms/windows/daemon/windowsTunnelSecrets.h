#pragma once

#include <windows.h>
#include <wincrypt.h>
#include <sddl.h>
#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>
#include <QUuid>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")

// Конфигурация живёт в защищённом временном файле только до старта дочерней службы.
// Машинный DPAPI позволяет прочитать её LocalSystem; ACL не даёт читать файл обычным пользователям.
class WindowsTunnelSecrets {
public:
    WindowsTunnelSecrets() = default;
    WindowsTunnelSecrets(const WindowsTunnelSecrets &) = delete;
    WindowsTunnelSecrets &operator=(const WindowsTunnelSecrets &) = delete;
    ~WindowsTunnelSecrets() { if (m_file != INVALID_HANDLE_VALUE) { CloseHandle(m_file); } }

    bool seal(const QString &config)
    {
        if (m_file != INVALID_HANDLE_VALUE || config.isEmpty() || config.size() > 32768) { return false; }
        auto plain = config.toUtf8();
        const auto wipe = qScopeGuard([&]() { SecureZeroMemory(plain.data(), static_cast<SIZE_T>(plain.size())); });
        auto entropyBytes = QByteArrayLiteral("Hop/TunnelConfig/v1");
        DATA_BLOB input { static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE *>(plain.data()) };
        DATA_BLOB entropy { static_cast<DWORD>(entropyBytes.size()), reinterpret_cast<BYTE *>(entropyBytes.data()) };
        DATA_BLOB encrypted {};
        if (!CryptProtectData(&input, L"Hop tunnel", &entropy, nullptr, nullptr,
                              CRYPTPROTECT_LOCAL_MACHINE | CRYPTPROTECT_UI_FORBIDDEN, &encrypted)) { return false; }
        const auto release = qScopeGuard([&]() { LocalFree(encrypted.pbData); });
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)", SDDL_REVISION_1,
                                                                  &descriptor, nullptr)) { return false; }
        const auto releaseDescriptor = qScopeGuard([&]() { LocalFree(descriptor); });
        SECURITY_ATTRIBUTES security { sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE };
        m_path = QDir::toNativeSeparators(QDir::tempPath() + QStringLiteral("/Hop-tunnel-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".bin"));
        m_file = CreateFileW(reinterpret_cast<LPCWSTR>(m_path.utf16()), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
                             &security, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (m_file == INVALID_HANDLE_VALUE) { return false; }
        DWORD written = 0;
        return WriteFile(m_file, encrypted.pbData, encrypted.cbData, &written, nullptr)
            && written == encrypted.cbData && FlushFileBuffers(m_file);
    }

    QString path() const { return m_path; }

    static QString unseal(const QString &path)
    {
        const QFileInfo info(path);
        if (!info.isAbsolute() || !info.fileName().startsWith(QStringLiteral("Hop-tunnel-"))
            || !info.fileName().endsWith(QStringLiteral(".bin"))) { return {}; }
        const auto file = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE) { return {}; }
        const auto close = qScopeGuard([&]() { CloseHandle(file); });
        BY_HANDLE_FILE_INFORMATION details {};
        if (!GetFileInformationByHandle(file, &details) || (details.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            || details.nFileSizeHigh != 0 || details.nFileSizeLow == 0 || details.nFileSizeLow > 65536) { return {}; }
        QByteArray bytes(static_cast<qsizetype>(details.nFileSizeLow), Qt::Uninitialized);
        DWORD read = 0;
        if (!ReadFile(file, bytes.data(), details.nFileSizeLow, &read, nullptr) || read != details.nFileSizeLow) { return {}; }
        auto entropyBytes = QByteArrayLiteral("Hop/TunnelConfig/v1");
        DATA_BLOB encrypted { static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE *>(bytes.data()) };
        DATA_BLOB entropy { static_cast<DWORD>(entropyBytes.size()), reinterpret_cast<BYTE *>(entropyBytes.data()) };
        DATA_BLOB plain {};
        if (!CryptUnprotectData(&encrypted, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &plain)) { return {}; }
        const auto release = qScopeGuard([&]() { SecureZeroMemory(plain.pbData, plain.cbData); LocalFree(plain.pbData); });
        if (plain.cbData > 32768) { return {}; }
        return QString::fromUtf8(reinterpret_cast<char *>(plain.pbData), plain.cbData);
    }
private:
    HANDLE m_file = INVALID_HANDLE_VALUE;
    QString m_path;
};

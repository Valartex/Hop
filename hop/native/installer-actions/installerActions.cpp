#include <windows.h>
#include <msi.h>
#include <msiquery.h>
#include <string>
#include <vector>

namespace {
constexpr auto serviceName = L"Hop-service";
constexpr auto rollbackProperty = L"HopRestoreServiceOnRollback";

struct ServiceHandle {
    SC_HANDLE value;
    ~ServiceHandle() { if (value) { CloseServiceHandle(value); } }
};

std::wstring property(MSIHANDLE installation, const wchar_t *name)
{
    DWORD size = 0;
    const auto status = MsiGetPropertyW(installation, name, L"", &size);
    if (status != ERROR_SUCCESS && status != ERROR_MORE_DATA) { return {}; }
    if (size > 32767) { return {}; }
    std::wstring result(size + 1, L'\0');
    ++size;
    if (MsiGetPropertyW(installation, name, result.data(), &size) != ERROR_SUCCESS) { return {}; }
    result.resize(size);
    return result;
}

void log(MSIHANDLE installation, const wchar_t *message)
{
    const auto record = MsiCreateRecord(0);
    if (record) { MsiRecordSetStringW(record, 0, message); MsiProcessMessage(installation, INSTALLMESSAGE_INFO, record); MsiCloseHandle(record); }
}

std::wstring canonicalFile(std::wstring path)
{
    if (path.size() > 2 && path.front() == L'"' && path.back() == L'"') { path = path.substr(1, path.size() - 2); }
    if (path.size() < 4 || path[1] != L':' || path[2] != L'\\' || path.find(L'"') != std::wstring::npos) { return {}; }
    std::vector<wchar_t> buffer(32768);
    const auto size = GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (!size || size >= buffer.size()) { return {}; }
    std::wstring result(buffer.data(), size);
    const auto slash = result.find_last_of(L'\\');
    if (slash == std::wstring::npos || _wcsicmp(result.c_str() + slash + 1, L"Hop-service.exe")) { return {}; }
    const auto attributes = GetFileAttributesW(result.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) { return {}; }
    return result;
}

bool matchesService(SC_HANDLE service, const std::wstring &expected)
{
    DWORD required = 0;
    QueryServiceConfigW(service, nullptr, 0, &required);
    if (required < sizeof(QUERY_SERVICE_CONFIGW) || required > 65536) { return false; }
    std::vector<BYTE> bytes(required);
    auto *config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(bytes.data());
    if (!QueryServiceConfigW(service, config, required, &required) || config->dwServiceType != SERVICE_WIN32_OWN_PROCESS) { return false; }
    const auto image = canonicalFile(config->lpBinaryPathName);
    return !expected.empty() && !image.empty() && _wcsicmp(image.c_str(), expected.c_str()) == 0;
}
}

extern "C" __declspec(dllexport) UINT __stdcall PrepareHopServiceRollback(MSIHANDLE installation)
{
    try {
        // Планируется до удаления прежнего продукта; пустые данные означают «не запускать».
        if (MsiSetPropertyW(installation, rollbackProperty, L"") != ERROR_SUCCESS) { return ERROR_INSTALL_FAILURE; }
        auto folder = property(installation, L"INSTALLFOLDER");
        if (!folder.empty() && folder.back() != L'\\') { folder += L'\\'; }
        const auto expected = canonicalFile(folder + L"Hop-service.exe");
        ServiceHandle manager { OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT) };
        if (!manager.value) { return ERROR_INSTALL_FAILURE; }
        ServiceHandle service { OpenServiceW(manager.value, serviceName, SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS) };
        if (!service.value || !matchesService(service.value, expected)) {
            log(installation, L"Hop rollback preparation refused a missing or differently located service");
            return ERROR_INSTALL_FAILURE;
        }
        SERVICE_STATUS status {};
        if (!QueryServiceStatus(service.value, &status)) { return ERROR_INSTALL_FAILURE; }
        if (status.dwCurrentState != SERVICE_RUNNING) { return ERROR_SUCCESS; }
        log(installation, L"Hop rollback service restart prepared for the verified running service");
        return MsiSetPropertyW(installation, rollbackProperty, expected.c_str());
    } catch (...) { return ERROR_INSTALL_FAILURE; }
}

extern "C" __declspec(dllexport) UINT __stdcall RestoreHopService(MSIHANDLE installation)
{
    try {
        const auto data = property(installation, L"CustomActionData");
        if (data.empty()) { return ERROR_SUCCESS; }
        const auto expected = canonicalFile(data);
        ServiceHandle manager { OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT) };
        if (!manager.value) { return ERROR_INSTALL_FAILURE; }
        ServiceHandle service { OpenServiceW(manager.value, serviceName, SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS | SERVICE_START) };
        if (!service.value || !matchesService(service.value, expected)) {
            log(installation, L"Hop rollback restart refused a missing or differently located service");
            return ERROR_INSTALL_FAILURE;
        }
        SERVICE_STATUS status {};
        if (!QueryServiceStatus(service.value, &status)) { return ERROR_INSTALL_FAILURE; }
        if (status.dwCurrentState == SERVICE_STOPPED
            && !StartServiceW(service.value, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) { return ERROR_INSTALL_FAILURE; }
        const auto started = GetTickCount64();
        do {
            if (!QueryServiceStatus(service.value, &status)) { return ERROR_INSTALL_FAILURE; }
            if (status.dwCurrentState == SERVICE_RUNNING) {
                log(installation, L"Hop rollback service restart completed for the verified restored service");
                return ERROR_SUCCESS;
            }
            if (status.dwCurrentState != SERVICE_START_PENDING) { return ERROR_INSTALL_FAILURE; }
            Sleep(100);
        } while (GetTickCount64() - started < 15000);
        return ERROR_INSTALL_FAILURE;
    } catch (...) { return ERROR_INSTALL_FAILURE; }
}

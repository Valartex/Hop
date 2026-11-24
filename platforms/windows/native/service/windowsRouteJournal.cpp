#include "windowsRouteJournal.h"
#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>
#include <netioapi.h>
#include <QDir>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QScopeGuard>
#include <QUuid>
#include <array>
#include <algorithm>
#include "version.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")

namespace {
bool sameKey(const MIB_IPFORWARDROW &a, const MIB_IPFORWARDROW &b) {
    return a.dwForwardDest == b.dwForwardDest && a.dwForwardMask == b.dwForwardMask
        && a.dwForwardNextHop == b.dwForwardNextHop && a.dwForwardIfIndex == b.dwForwardIfIndex
        && a.dwForwardPolicy == b.dwForwardPolicy;
}
bool sameRow(const MIB_IPFORWARDROW &a, const MIB_IPFORWARDROW &b) {
    return sameKey(a,b) && a.dwForwardType == b.dwForwardType && a.dwForwardProto == b.dwForwardProto
        && a.dwForwardMetric1 == b.dwForwardMetric1 && a.dwForwardMetric2 == b.dwForwardMetric2
        && a.dwForwardMetric3 == b.dwForwardMetric3 && a.dwForwardMetric4 == b.dwForwardMetric4 && a.dwForwardMetric5 == b.dwForwardMetric5;
}
bool requestedKey(const MIB_IPFORWARDROW &key, const MIB_IPFORWARDROW &row, bool onlink) {
    if (sameKey(key,row)) { return true; }
    // Legacy API представляет on-link через локальный адрес, API2 — нулевым next-hop.
    return onlink && !key.dwForwardNextHop && row.dwForwardType == MIB_IPROUTE_TYPE_DIRECT
        && key.dwForwardDest == row.dwForwardDest && key.dwForwardMask == row.dwForwardMask
        && key.dwForwardIfIndex == row.dwForwardIfIndex && key.dwForwardPolicy == row.dwForwardPolicy;
}
bool currentRow(const MIB_IPFORWARDROW &key, MIB_IPFORWARDROW &row, bool &exists, bool onlink = false) {
    ULONG size = 0; exists = false;
    if (GetIpForwardTable(nullptr, &size, FALSE) != ERROR_INSUFFICIENT_BUFFER || size > 32 * 1024 * 1024) { return false; }
    QByteArray bytes(size, Qt::Uninitialized); auto *table = reinterpret_cast<MIB_IPFORWARDTABLE *>(bytes.data());
    if (GetIpForwardTable(table, &size, FALSE) != NO_ERROR) { return false; }
    for (DWORD index = 0; index < table->dwNumEntries; ++index) {
        if (requestedKey(key,table->table[index],onlink)) {
            if (exists) { return false; }
            row = table->table[index]; exists = true;
        }
    }
    return true;
}
QString interfaceIdentity(DWORD index) {
    NET_LUID luid {}; GUID guid {};
    if (ConvertInterfaceIndexToLuid(index, &luid) != NO_ERROR || ConvertInterfaceLuidToGuid(&luid, &guid) != NO_ERROR) { return {}; }
    return QString::number(luid.Value) + QLatin1Char('/') + QUuid(guid).toString(QUuid::WithoutBraces);
}
bool legacyKey(const MIB_IPFORWARD_ROW2 &source, MIB_IPFORWARDROW &key) {
    if (source.DestinationPrefix.Prefix.si_family != AF_INET || source.DestinationPrefix.PrefixLength > 32
        || source.NextHop.si_family != AF_INET) { return false; }
    key = {}; key.dwForwardDest = source.DestinationPrefix.Prefix.Ipv4.sin_addr.s_addr;
    const auto length = source.DestinationPrefix.PrefixLength;
    key.dwForwardMask = htonl(length == 0 ? 0 : 0xffffffffu << (32 - length));
    key.dwForwardNextHop = source.NextHop.Ipv4.sin_addr.s_addr;
    key.dwForwardIfIndex = source.InterfaceIndex;
    return source.InterfaceLuid.Value ? ConvertInterfaceLuidToIndex(&source.InterfaceLuid, &key.dwForwardIfIndex) == NO_ERROR : key.dwForwardIfIndex != 0;
}
}
WindowsRouteJournal &WindowsRouteJournal::instance() { static WindowsRouteJournal journal; return journal; }
WindowsRouteJournal::~WindowsRouteJournal() {
    if (m_lock != INVALID_HANDLE_VALUE) { CloseHandle(m_lock); }
    if (m_directory != INVALID_HANDLE_VALUE) { CloseHandle(m_directory); }
}
QList<MIB_IPFORWARDROW> WindowsRouteJournal::routes() const {
    QList<MIB_IPFORWARDROW> result; for (const auto &owned : m_rows) { result.append(owned.row); } return result;
}
bool WindowsRouteJournal::openStorage()
{
    PWSTR programData = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &programData) != S_OK) { return false; }
    const auto releasePath = qScopeGuard([&]() { CoTaskMemFree(programData); });
    const auto directory = QDir::toNativeSeparators(QString::fromWCharArray(programData) + QStringLiteral("/Hop"));
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;GA;;;SY)(A;OICI;GA;;;BA)", SDDL_REVISION_1, &descriptor, nullptr)) { return false; }
    const auto releaseDescriptor = qScopeGuard([&]() { LocalFree(descriptor); });
    SECURITY_ATTRIBUTES security { sizeof(security), descriptor, FALSE };
    if (!CreateDirectoryW(reinterpret_cast<LPCWSTR>(directory.utf16()), &security) && GetLastError() != ERROR_ALREADY_EXISTS) { return false; }
    m_directory = CreateFileW(reinterpret_cast<LPCWSTR>(directory.utf16()), READ_CONTROL | WRITE_DAC | FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (m_directory == INVALID_HANDLE_VALUE) { return false; }
    BY_HANDLE_FILE_INFORMATION info {};
    if (!GetFileInformationByHandle(m_directory, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { return false; }
    PSID owner = nullptr; PSECURITY_DESCRIPTOR existing = nullptr;
    if (GetSecurityInfo(m_directory, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &existing) != ERROR_SUCCESS) { return false; }
    const auto freeExisting = qScopeGuard([&]() { LocalFree(existing); });
    if (!IsWellKnownSid(owner, WinLocalSystemSid) && !IsWellKnownSid(owner, WinBuiltinAdministratorsSid)) { return false; }
    PACL acl = nullptr; BOOL present = FALSE, defaulted = FALSE;
    if (!GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) || !present
        || SetSecurityInfo(m_directory, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                           nullptr, nullptr, acl, nullptr) != ERROR_SUCCESS) { return false; }
    const auto lockPath = directory + QStringLiteral("\\routes.lock");
    m_lock = CreateFileW(reinterpret_cast<LPCWSTR>(lockPath.utf16()), GENERIC_READ | GENERIC_WRITE, 0, &security, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (m_lock == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(m_lock, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { return false; }
    // Volatile HKLM nonce исчезает при новом запуске ядра Windows.
    HKEY bootKey = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Hop\\RouteJournalBoot", 0, nullptr, REG_OPTION_VOLATILE,
        KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, &security, &bootKey, nullptr) != ERROR_SUCCESS) { return false; }
    const auto closeKey = qScopeGuard([&]() { RegCloseKey(bootKey); });
    std::array<wchar_t, 64> nonce {}; DWORD type = 0, size = sizeof(nonce);
    if (RegQueryValueExW(bootKey, L"Nonce", nullptr, &type, reinterpret_cast<BYTE *>(nonce.data()), &size) == ERROR_SUCCESS
        && type == REG_SZ && size >= sizeof(wchar_t) && size <= sizeof(nonce) && nonce[(size / sizeof(wchar_t)) - 1] == 0) {
        m_boot = QString::fromWCharArray(nonce.data()); if (QUuid(m_boot).isNull()) { return false; }
    } else {
        m_boot = QUuid::createUuid().toString();
        if (RegSetValueExW(bootKey, L"Nonce", 0, REG_SZ, reinterpret_cast<const BYTE *>(m_boot.utf16()), (m_boot.size() + 1) * sizeof(wchar_t)) != ERROR_SUCCESS) { return false; }
    }
    m_path = directory + QStringLiteral("\\routes-v1.json"); return true;
}
bool WindowsRouteJournal::save()
{
    if (m_rows.size() > 4096) { return false; }
    QJsonArray rows;
    for (const auto &owned : m_rows) {
        const auto &row = owned.row; QJsonArray fields;
        for (const DWORD field : { row.dwForwardDest,row.dwForwardMask,row.dwForwardPolicy,row.dwForwardNextHop,row.dwForwardIfIndex,
            row.dwForwardType,row.dwForwardProto,row.dwForwardMetric1,row.dwForwardMetric2,row.dwForwardMetric3,row.dwForwardMetric4,row.dwForwardMetric5 }) { fields.append(double(field)); }
        rows.append(QJsonObject { { "interface", owned.interfaceIdentity }, { "row", fields } });
    }
    const auto bytes = QJsonDocument(QJsonObject { { "version", 1 }, { "boot", m_boot }, { "routes", rows } }).toJson(QJsonDocument::Compact);
    QSaveFile file(m_path); file.setDirectWriteFallback(false);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
bool WindowsRouteJournal::load()
{
    const auto file = CreateFileW(reinterpret_cast<LPCWSTR>(m_path.utf16()), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) { return GetLastError() == ERROR_FILE_NOT_FOUND; }
    const auto close = qScopeGuard([&]() { CloseHandle(file); }); BY_HANDLE_FILE_INFORMATION info {};
    if (!GetFileInformationByHandle(file, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        || info.nFileSizeHigh || !info.nFileSizeLow || info.nFileSizeLow > 1024 * 1024) { return false; }
    QByteArray bytes(info.nFileSizeLow, Qt::Uninitialized); DWORD read = 0;
    if (!ReadFile(file, bytes.data(), info.nFileSizeLow, &read, nullptr) || read != info.nFileSizeLow) { return false; }
    const auto document = QJsonDocument::fromJson(bytes); if (!document.isObject()) { return false; }
    const auto data = document.object();
    if (data.value("version").toInt() != 1 || !data.value("boot").isString() || !data.value("routes").isArray()) { return false; }
    if (data.value("boot").toString() != m_boot) { return true; }
    const auto rows = data.value("routes").toArray(); if (rows.size() > 4096) { return false; }
    QList<Owned> parsed;
    for (const auto &entry : rows) {
        const auto object = entry.toObject(); const auto fields = object.value("row").toArray(); if (fields.size() != 12) { return false; }
        std::array<DWORD,12> numbers {};
        for (int index = 0; index < 12; ++index) {
            const double number = fields[index].toDouble(-1);
            if (!fields[index].isDouble() || number < 0 || number > 4294967295.0 || number != double(DWORD(number))) { return false; }
            numbers[index] = DWORD(number);
        }
        MIB_IPFORWARDROW row {}; row.dwForwardDest=numbers[0]; row.dwForwardMask=numbers[1]; row.dwForwardPolicy=numbers[2];
        row.dwForwardNextHop=numbers[3]; row.dwForwardIfIndex=numbers[4]; row.dwForwardType=numbers[5]; row.dwForwardProto=numbers[6];
        row.dwForwardMetric1=numbers[7]; row.dwForwardMetric2=numbers[8]; row.dwForwardMetric3=numbers[9]; row.dwForwardMetric4=numbers[10]; row.dwForwardMetric5=numbers[11]; row.dwForwardAge=INFINITE;
        const auto identity = object.value("interface").toString();
        if (row.dwForwardProto != MIB_IPPROTO_NETMGMT || identity.isEmpty()) { return false; }
        // Исчезнувший/заменённый интерфейс не даёт права удалять запись с повторно использованным index.
        if (identity == interfaceIdentity(row.dwForwardIfIndex)) { parsed.append({ row, identity }); }
    }
    m_rows = parsed; return true;
}
bool WindowsRouteJournal::initialize()
{
    if (m_initialized) { return m_ready; }
    m_initialized = true;
    if (QStringLiteral(APPLICATION_NAME) != QStringLiteral("Hop") || !openStorage() || !load()) { return false; }
    m_ready = true; m_ready = clear(); return m_ready;
}
bool WindowsRouteJournal::remember(const MIB_IPFORWARDROW &created)
{
    if (!initialize()) { return false; }
    MIB_IPFORWARDROW row {}; bool exists = false;
    if (!currentRow(created,row,exists) || !exists || row.dwForwardMetric1 != created.dwForwardMetric1) { return false; }
    const auto identity = interfaceIdentity(row.dwForwardIfIndex); if (identity.isEmpty()) { return false; }
    m_rows.append({ row, identity }); if (save()) { return true; }
    m_rows.removeLast(); return false;
}
bool WindowsRouteJournal::remove(const MIB_IPFORWARDROW &owned)
{
    if (!initialize()) { return false; }
    auto found = std::find_if(m_rows.begin(),m_rows.end(),[&](const auto &entry) { return sameRow(entry.row,owned); });
    if (found == m_rows.end()) { return false; }
    MIB_IPFORWARDROW current {}; bool exists = false;
    if (!currentRow(owned,current,exists)) { return false; }
    if (exists && sameRow(owned,current) && found->interfaceIdentity == interfaceIdentity(owned.dwForwardIfIndex)) {
        const auto status = DeleteIpForwardEntry(&current);
        if (status != NO_ERROR && status != ERROR_NOT_FOUND && status != ERROR_FILE_NOT_FOUND) { return false; }
    }
    // Чужое изменение снимает наше владение, но не удаляется.
    m_rows.erase(found); return save();
}
bool WindowsRouteJournal::clear()
{
    if (!initialize()) { return false; }
    bool ok = true; for (const auto &row : routes()) { ok = remove(row) && ok; }
    return save() && ok;
}
DWORD WindowsRouteJournal::createRoute(MIB_IPFORWARD_ROW2 *row)
{
    if (QStringLiteral(APPLICATION_NAME) != QStringLiteral("Hop") || row->DestinationPrefix.Prefix.si_family != AF_INET) { return CreateIpForwardEntry2(row); }
    if (!initialize()) { return ERROR_WRITE_FAULT; }
    const auto status = CreateIpForwardEntry2(row); if (status != NO_ERROR) { return status; }
    MIB_IPFORWARDROW key {}, observed {}; bool exists = false;
    const bool converted = legacyKey(*row,key);
    const bool captured = converted && currentRow(key,observed,exists, !key.dwForwardNextHop);
    const bool recorded = captured && exists && remember(observed);
    if (recorded) { return NO_ERROR; }
    qWarning() << "Hop route2 journal capture:" << converted << captured << exists
        << "if" << key.dwForwardIfIndex << "prefix" << row->DestinationPrefix.PrefixLength
        << "metric" << row->Metric;
    DeleteIpForwardEntry2(row); return ERROR_WRITE_FAULT;
}
DWORD WindowsRouteJournal::deleteRoute(const MIB_IPFORWARD_ROW2 *row)
{
    if (QStringLiteral(APPLICATION_NAME) != QStringLiteral("Hop") || row->DestinationPrefix.Prefix.si_family != AF_INET) { return DeleteIpForwardEntry2(row); }
    if (!initialize()) { return ERROR_WRITE_FAULT; }
    MIB_IPFORWARDROW key {}; if (!legacyKey(*row,key)) { return row->InterfaceLuid.Value || row->InterfaceIndex ? ERROR_INVALID_PARAMETER : NO_ERROR; }
    const auto ownedRoutes = routes();
    const auto matches = std::count_if(ownedRoutes.cbegin(),ownedRoutes.cend(),[&](const auto &owned) { return requestedKey(key,owned,!key.dwForwardNextHop); });
    if (matches > 1) { return ERROR_INVALID_DATA; }
    for (const auto &owned : ownedRoutes) { if (requestedKey(key,owned,!key.dwForwardNextHop)) { return remove(owned) ? NO_ERROR : ERROR_WRITE_FAULT; } }
    // Уже существовавший маршрут никогда не присваивался Hop.
    return NO_ERROR;
}

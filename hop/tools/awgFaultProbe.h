#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QScopeGuard>
#include <QTextStream>
#include <QThread>
#include <array>
#include <tlhelp32.h>
#include "core/utils/ipcClient.h"
#include "vpnConnection.h"

// Только для VirtualBox-пробы, проверившей путь собственного процесса службы.
inline bool awgPhysicalRequest(QTextStream &output, const QString &label, const QString &address, bool allowed, bool retryTimeout = true)
{
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy::NoProxy);
    QNetworkRequest request(QUrl(QStringLiteral("https://") + address + QStringLiteral("/cdn-cgi/trace")));
    request.setTransferTimeout(8000);
    QEventLoop wait;
    auto *reply = network.get(request);
    QObject::connect(reply, &QNetworkReply::finished, &wait, &QEventLoop::quit);
    wait.exec(QEventLoop::ExcludeUserInputEvents);
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto error = reply->error();
    output << label << ": HTTP " << status << ", Qt " << int(error) << Qt::endl;
    if (allowed && retryTimeout && error == QNetworkReply::TimeoutError) {
        return awgPhysicalRequest(output, label + QStringLiteral(" (timeout retry)"), address, true, false);
    }
    return allowed ? status == 200 && error == QNetworkReply::NoError : status == 0 && error == QNetworkReply::UnknownNetworkError;
}

inline bool crashOwnedAwgTunnelForTest(QTextStream &output)
{
    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) { return false; }
    const auto closeManager = qScopeGuard([&]() { CloseServiceHandle(manager); });
    const auto tunnel = OpenServiceW(manager, L"AmneziaWGTunnel$Hop", SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
    if (!tunnel) { return false; }
    const auto closeTunnel = qScopeGuard([&]() { CloseServiceHandle(tunnel); });
    DWORD required = 0;
    QueryServiceConfigW(tunnel, nullptr, 0, &required);
    if (required < sizeof(QUERY_SERVICE_CONFIGW) || required > 65536) { return false; }
    QByteArray config(static_cast<qsizetype>(required), Qt::Uninitialized);
    auto *serviceConfig = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(config.data());
    if (!QueryServiceConfigW(tunnel, serviceConfig, required, &required)) { return false; }
    const auto ownImage = QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/Hop-service.exe"));
    const auto command = QDir::fromNativeSeparators(QString::fromWCharArray(serviceConfig->lpBinaryPathName));
    if (!command.startsWith(QLatin1Char('"') + ownImage + QStringLiteral("\" tunneldaemon --sealed-config \""), Qt::CaseInsensitive)) { return false; }
    SERVICE_STATUS_PROCESS status {};
    if (!QueryServiceStatusEx(tunnel, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &required)
        || status.dwCurrentState != SERVICE_RUNNING || !status.dwProcessId) { return false; }
    const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, status.dwProcessId);
    if (!process) { return false; }
    const auto closeProcess = qScopeGuard([&]() { CloseHandle(process); });
    std::array<wchar_t, 32768> image {};
    DWORD imageLength = static_cast<DWORD>(image.size());
    if (!QueryFullProcessImageNameW(process, 0, image.data(), &imageLength)
        || QDir::cleanPath(QDir::fromNativeSeparators(QString::fromWCharArray(image.data(), imageLength))).compare(ownImage, Qt::CaseInsensitive) != 0
        ) { return false; }
    // Снимок относится к проверенному SCM-процессу Hop, чужие процессы не затрагиваются.
    const auto modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, status.dwProcessId);
    if (modules == INVALID_HANDLE_VALUE) { return false; }
    const auto closeModules = qScopeGuard([&]() { CloseHandle(modules); });
    MODULEENTRY32W module {};
    module.dwSize = sizeof(module);
    bool awgLoaded = false, xrayLoaded = false;
    if (!Module32FirstW(modules, &module)) { return false; }
    do {
        const auto name = QString::fromWCharArray(module.szModule);
        awgLoaded |= name.compare(QStringLiteral("tunnel.dll"), Qt::CaseInsensitive) == 0;
        xrayLoaded |= name.compare(QStringLiteral("amnezia_xray.dll"), Qt::CaseInsensitive) == 0;
    } while (Module32NextW(modules, &module));
    output << "AWG child modules: tunnel=" << awgLoaded << ", xray=" << xrayLoaded << Qt::endl;
    if (!awgLoaded || xrayLoaded) { return false; }
    if (!TerminateProcess(process, 93) || WaitForSingleObject(process, 10000) != WAIT_OBJECT_0) { return false; }
    return true;
}

inline bool probeAwgGuardCrash(VpnConnection &connection, HANDLE mainService, QTextStream &output)
{
    if (!mainService || !awgPhysicalRequest(output, QStringLiteral("AWG Direct before crash"), QStringLiteral("1.1.1.1"), true)
        || !crashOwnedAwgTunnelForTest(output)) { return false; }
    QElapsedTimer detection;
    detection.start();
    while (connection.lastError() == ErrorCode::NoError && detection.elapsed() < 8000) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
        QThread::msleep(25);
    }
    output << "AWG tunnel crash detected: " << int(connection.lastError()) << Qt::endl;
    if (connection.lastError() == ErrorCode::NoError) { return false; }
    const int count = IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) {
        auto reply = iface->trafficGuardFilterCount();
        return reply.waitForFinished(5000) ? reply.returnValue() : -1;
    }, []() { return -1; });
    output << "AWG persistent filters after tunnel crash: " << count << Qt::endl;
    if (count <= 0 || !awgPhysicalRequest(output, QStringLiteral("AWG protected after tunnel crash"), QStringLiteral("1.0.0.1"), false)
        || !awgPhysicalRequest(output, QStringLiteral("AWG Direct after tunnel crash"), QStringLiteral("1.1.1.1"), true)) { return false; }
    if (!TerminateProcess(mainService, 94) || WaitForSingleObject(mainService, 10000) != WAIT_OBJECT_0) { return false; }
    if (!awgPhysicalRequest(output, QStringLiteral("AWG protected after service crash"), QStringLiteral("1.0.0.1"), false)
        || !awgPhysicalRequest(output, QStringLiteral("AWG Direct after service crash"), QStringLiteral("1.1.1.1"), true)) { return false; }
    QProcess recovery;
    recovery.start(QCoreApplication::applicationDirPath() + QStringLiteral("/hop_guard_recover.exe"), { QStringLiteral("--vm-test-only") });
    if (!recovery.waitForFinished(20000) || recovery.exitCode() != 0) { return false; }
    output << "AWG own VM protection recovery: passed" << Qt::endl;
    return awgPhysicalRequest(output, QStringLiteral("AWG physical network after recovery"), QStringLiteral("1.0.0.1"), true);
}

#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QRemoteObjectNode>
#include <QScopeGuard>
#include <QTextStream>
#include <QThread>
#include <windows.h>
#include "core/utils/ipcClient.h"
#include "ipc.h"
#include "version.h"

// Вызывается только из теста, уже проверившего VirtualBox и права гостевого агента.
inline bool probeWindowsScm(QTextStream &output)
{
    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    if (!manager) { output << "SCM open failed: " << GetLastError() << Qt::endl; return false; }
    const auto closeManager = qScopeGuard([&]() { CloseServiceHandle(manager); });
    const auto name = QStringLiteral(SERVICE_NAME);
    const auto existing = OpenServiceW(manager, reinterpret_cast<LPCWSTR>(name.utf16()), SERVICE_QUERY_STATUS);
    if (existing) {
        CloseServiceHandle(existing);
        output << "SCM test refuses to replace an existing Hop service" << Qt::endl;
        return false;
    }
    if (GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST) { return false; }
    const auto binary = QStringLiteral("\"") + QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + QStringLiteral("/Hop-service.exe")) + QStringLiteral("\"");
    const auto service = CreateServiceW(manager, reinterpret_cast<LPCWSTR>(name.utf16()), L"Hop VM validation",
        SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE, SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, reinterpret_cast<LPCWSTR>(binary.utf16()), nullptr, nullptr, nullptr, nullptr, nullptr);
    if (!service) { output << "SCM create failed: " << GetLastError() << Qt::endl; return false; }
    const auto waitState = [&](DWORD wanted) {
        QElapsedTimer timer;
        timer.start();
        do {
            SERVICE_STATUS status {};
            if (!QueryServiceStatus(service, &status)) { return false; }
            if (status.dwCurrentState == wanted) { return true; }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(50);
        } while (timer.elapsed() < 15000);
        return false;
    };
    bool removed = false;
    const auto cleanup = qScopeGuard([&]() {
        SERVICE_STATUS status {};
        ControlService(service, SERVICE_CONTROL_STOP, &status);
        waitState(SERVICE_STOPPED);
        if (!removed) { DeleteService(service); }
        CloseServiceHandle(service);
    });
    if (!StartServiceW(service, 0, nullptr) || !waitState(SERVICE_RUNNING)) {
        output << "SCM start failed: " << GetLastError() << Qt::endl;
        return false;
    }
    QRemoteObjectNode node;
    node.connectToNode(QUrl(QStringLiteral("local:") + amnezia::getIpcServiceUrl()));
    const QSharedPointer<IpcInterfaceReplica> replica(node.acquire<IpcInterfaceReplica>());
    if (!replica->waitForSource(10000)) { output << "SCM IPC did not become available" << Qt::endl; return false; }
    auto count = replica->trafficGuardFilterCount();
    if (!count.waitForFinished(5000) || count.returnValue() != 0) { return false; }
    output << "SCM auto-start service: running, private IPC available, protection clear" << Qt::endl;
    SERVICE_STATUS status {};
    if (!ControlService(service, SERVICE_CONTROL_STOP, &status)) {
        const auto error = GetLastError();
        const bool queried = QueryServiceStatus(service, &status);
        output << "SCM stop failed: " << error << ", state " << status.dwCurrentState
               << ", accepted " << status.dwControlsAccepted << ", service error " << status.dwWin32ExitCode << Qt::endl;
        // QtService может закрыть канал SCM раньше ответа ControlService; состояние проверяем независимо.
        if (error != ERROR_BROKEN_PIPE || !queried || status.dwCurrentState != SERVICE_STOPPED || status.dwWin32ExitCode != NO_ERROR) { return false; }
    }
    if (!waitState(SERVICE_STOPPED)) { output << "SCM stop deadline exceeded" << Qt::endl; return false; }
    output << "SCM service stopped" << Qt::endl;
    removed = DeleteService(service);
    if (removed) { output << "SCM test service removed" << Qt::endl; }
    return removed;
}

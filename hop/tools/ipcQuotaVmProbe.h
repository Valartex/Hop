#pragma once
#include <QRemoteObjectNode>
#include "rep_ipc_process_interface_replica.h"
#include "core/utils/ipcClient.h"
#include "ipc.h"

inline int probeIpcScopeInVm(QTextStream &output)
{
    const int id = IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) {
        auto reply = iface->createPrivilegedProcess(); return reply.waitForFinished(5000) ? reply.returnValue() : -1;
    }, []() { return -1; });
    if (id <= 0) { return 11; }
    QLocalSocket trusted; trusted.connectToServer(QStringLiteral(APP_WINDOWS_DAEMON_PIPE));
    if (!trusted.waitForConnected(3000)) { return 11; }
    trusted.write("{\"type\":\"status\"}\n"); trusted.flush();
    if (!trusted.waitForReadyRead(3000) || QJsonDocument::fromJson(trusted.readLine(65536)).object().value("type") != QStringLiteral("status")) { return 11; }
    output << "Trusted client: control and native daemon IPC available" << Qt::endl;
    for (const auto &argument : { QString(), QString::number(id), QStringLiteral("daemon") }) {
        QProcess process; process.start(QCoreApplication::applicationDirPath() + "/hop_foreign_ipc_probe.exe",
            argument.isEmpty() ? QStringList() : QStringList { argument });
        if (!process.waitForFinished(5000)) { process.kill(); process.waitForFinished(1000); return 11; }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) { return 11; }
    }
    output << "Untrusted executable: control, process and native daemon IPC rejected" << Qt::endl;
    return 0;
}

inline int probeIpcQuotaInVm(QTextStream &output)
{
    const auto create = []() {
        return IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) {
            auto reply = iface->createPrivilegedProcess(); return reply.waitForFinished(5000) ? reply.returnValue() : -2;
        }, []() { return -2; });
    };
    QSet<int> ids;
    int first = -1;
    for (int index = 0; index < 8; ++index) {
        const int id = create();
        if (id <= 0 || ids.contains(id)) { return 11; }
        ids.insert(id); if (first < 0) { first = id; }
    }
    if (create() != -1) { return 11; }
    output << "Privileged IPC quota: 8, overflow rejected" << Qt::endl;
    QRemoteObjectNode node;
    if (!node.connectToNode(QUrl(QStringLiteral("local:") + amnezia::getIpcProcessUrl(first)))) { return 11; }
    QScopedPointer<IpcProcessInterfaceReplica> process(node.acquire<IpcProcessInterfaceReplica>());
    if (!process || !process->waitForSource(5000)) { output << "Privileged IPC source initialization failed" << Qt::endl; return 11; }
    process->setProgram(amnezia::CertUtil);
    process->setNativeArguments(QStringLiteral("-generateSSTFromWU ignored-file"));
    process->start();
    auto started = process->waitForStarted(1000);
    if (!started.waitForFinished(5000) || started.returnValue()) { output << "Privileged IPC rejection check failed" << Qt::endl; return 11; }
    output << "Privileged IPC foreign program/native arguments: not started" << Qt::endl;
    QEventLoop cleanup;
    QTimer::singleShot(32000, &cleanup, &QEventLoop::quit); cleanup.exec();
    const auto next = create();
    output << "Privileged IPC idle descriptors after timeout: slot available=" << (next > 0) << Qt::endl;
    return next > 0 ? 0 : 11;
}

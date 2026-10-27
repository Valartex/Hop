#include <QCoreApplication>
#include <QRemoteObjectNode>
#include <QLocalSocket>
#include "rep_ipc_interface_replica.h"
#include "rep_ipc_process_interface_replica.h"
#include "ipc.h"
#include "version.h"

// Помощник лишь читает handshake. Ни один привилегированный метод не вызывается.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().value(1) == QStringLiteral("daemon")) {
        QLocalSocket socket; socket.connectToServer(QStringLiteral(APP_WINDOWS_DAEMON_PIPE));
        if (!socket.waitForConnected(2000)) { return 0; }
        socket.write("{\"type\":\"status\"}\n"); socket.flush();
        return socket.waitForReadyRead(2000) && !socket.readAll().isEmpty() ? 5 : 0;
    }
    QRemoteObjectNode node;
    const int id = app.arguments().value(1).toInt();
    node.connectToNode(QUrl(QStringLiteral("local:") + (id > 0 ? amnezia::getIpcProcessUrl(id) : amnezia::getIpcServiceUrl())));
    if (id > 0) { QScopedPointer<IpcProcessInterfaceReplica> replica(node.acquire<IpcProcessInterfaceReplica>()); return replica->waitForSource(2000) ? 5 : 0; }
    QScopedPointer<IpcInterfaceReplica> replica(node.acquire<IpcInterfaceReplica>()); return replica->waitForSource(2000) ? 5 : 0;
}

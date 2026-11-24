#include "core/locations/platformTask.h"

#include <QEventLoop>
#include <QFutureWatcher>
#include <QJniObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTimer>
#include <QtConcurrent>
#include "core/locations/providerRequest.h"

namespace hop {
PlatformTask::~PlatformTask() { ++m_generation; }

void PlatformTask::start(OpenMode)
{
    if (m_state != NotRunning) { return; }
    m_input.clear(); m_output.clear(); m_submitted = false;
    m_state = Running; m_exitCode = -1; m_exitStatus = NormalExit;
    ++m_generation;
    QTimer::singleShot(0, this, [this, generation = m_generation]() {
        if (generation == m_generation && m_state == Running) { QMetaObject::invokeMethod(this, "started", Qt::DirectConnection); }
    });
}

qint64 PlatformTask::write(const QByteArray &bytes)
{
    if (m_state != Running || m_submitted || m_input.size() + bytes.size() > 2 * 1024 * 1024) { return -1; }
    m_input.append(bytes);
    return bytes.size();
}

void PlatformTask::closeWriteChannel()
{
    if (m_state != Running || m_submitted) { return; }
    m_submitted = true;
    const auto document = QJsonDocument::fromJson(m_input);
    const auto request = document.isArray() ? QJsonObject { { QStringLiteral("patterns"), document.array() } } : document.object();
    m_input.clear();
    const auto role = program();
    const auto flags = arguments();
    auto *watcher = new QFutureWatcher<QJsonObject>(this);
    connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [this, watcher, generation = m_generation]() {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation || m_state != Running) { return; }
        m_output = QJsonDocument(result).toJson(QJsonDocument::Compact);
        m_exitCode = result.value(QStringLiteral("code")).toInt(result.value(QStringLiteral("ok")).toBool() ? 0 : 3);
        m_state = NotRunning;
        QMetaObject::invokeMethod(this, "readyReadStandardOutput", Qt::DirectConnection);
        emit finished(m_exitCode, NormalExit);
    });
    watcher->setFuture(QtConcurrent::run([request, role, flags]() {
        if (flags.contains(QStringLiteral("--provider-task"))) {
            if (request.value(QStringLiteral("action")) != QStringLiteral("resolve")) { return executeProviderRequest(request); }
        }
        const auto action = QJniObject::fromString(flags.contains(QStringLiteral("--validate-domain-patterns"))
            ? QStringLiteral("validate") : role);
        const auto input = QJniObject::fromString(QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)));
        const auto result = QJniObject::callStaticObjectMethod(
            "org/amnezia/vpn/HopNetworkTasks", "execute", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
            action.object<jstring>(), input.object<jstring>());
        return result.isValid() ? QJsonDocument::fromJson(result.toString().toUtf8()).object()
            : QJsonObject { { QStringLiteral("code"), 3 } };
    }));
}

void PlatformTask::kill()
{
    if (m_state == NotRunning) { return; }
    if (program().endsWith(QStringLiteral("hop_awg_probe")) || program().endsWith(QStringLiteral("hop_endpoint_probe"))) {
        QJniObject::callStaticMethod<jboolean>("org/amnezia/vpn/HopDiagnosticsClient", "cancelDiagnostics", "()Z");
    }
    ++m_generation; m_state = NotRunning; m_exitCode = -1; m_exitStatus = CrashExit;
    emit finished(m_exitCode, m_exitStatus);
}

QByteArray PlatformTask::readAllStandardOutput()
{
    QByteArray result; result.swap(m_output); return result;
}

bool PlatformTask::waitForStarted(int) { return m_state != NotRunning; }

bool PlatformTask::waitForFinished(int milliseconds)
{
    if (m_state == NotRunning) { return true; }
    QEventLoop loop;
    QTimer timer; timer.setSingleShot(true);
    connect(this, &QProcess::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(milliseconds); loop.exec();
    return m_state == NotRunning;
}
}

#pragma once

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>

namespace hop {
inline QString platformTaskPath(const QString &name)
{
#ifdef Q_OS_ANDROID
    return QStringLiteral("android:") + name;
#else
    return name == QStringLiteral("provider") ? QCoreApplication::applicationFilePath()
        : QCoreApplication::applicationDirPath() + QLatin1Char('/') + name + QStringLiteral(".exe");
#endif
}

inline bool platformTaskAvailable(const QString &program)
{
#ifdef Q_OS_ANDROID
    return program.startsWith(QStringLiteral("android:"));
#else
    return QFileInfo::exists(program);
#endif
}

#ifndef Q_OS_ANDROID
using PlatformTask = QProcess;
#else
// Контракт короткой задачи общий; на Android исполняемые файлы не запускаются.
class PlatformTask : public QProcess {
public:
    explicit PlatformTask(QObject *parent = nullptr) : QProcess(parent) {}
    ~PlatformTask() override;
    void start(OpenMode mode = ReadWrite);
    qint64 write(const QByteArray &bytes);
    void closeWriteChannel();
    void kill();
    ProcessState state() const { return m_state; }
    QByteArray readAllStandardOutput();
    QByteArray readAllStandardError() { return {}; }
    bool waitForStarted(int milliseconds = 30000);
    bool waitForFinished(int milliseconds = 30000);
    ExitStatus exitStatus() const { return m_exitStatus; }
    int exitCode() const { return m_exitCode; }
private:
    QByteArray m_input, m_output;
    ProcessState m_state = NotRunning;
    ExitStatus m_exitStatus = NormalExit;
    int m_exitCode = -1;
    quint64 m_generation = 0;
    bool m_submitted = false;
};
#endif
}

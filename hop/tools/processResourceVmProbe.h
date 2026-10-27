#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <algorithm>

// Только наблюдение: учитываются точные пути установленного Hop, без argv и чужих процессов.
class InstalledHopResourceSample
{
public:
    explicit InstalledHopResourceSample(const QString &directory) : m_directory(QDir::cleanPath(directory))
    {
        GetSystemTimeAsFileTime(&m_startedAt);
        m_elapsed.start();
    }
    ~InstalledHopResourceSample() { for (const auto &process : m_processes) { CloseHandle(process.handle); } }
    void sample()
    {
        const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) { incomplete("snapshot", GetLastError()); return; }
        PROCESSENTRY32W entry {}; entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                const auto name = QString::fromWCharArray(entry.szExeFile).toLower();
                if (name != "hop.exe" && name != "hop-service.exe" && name != "tun2socks.exe"
                    && name != "hop_awg_probe.exe" && name != "hop_endpoint_probe.exe"
                    && name != "hop_tunnel_probe.exe" && name != "hop_guard_recover.exe") { continue; }
                if (m_processes.contains(entry.th32ProcessID)) { continue; }
                const auto handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, entry.th32ProcessID);
                if (!handle) { incomplete("open", GetLastError()); continue; }
                wchar_t image[32768]; DWORD size = 32768;
                const bool matches = QueryFullProcessImageNameW(handle, 0, image, &size)
                    && QDir::cleanPath(QDir::fromNativeSeparators(QString::fromWCharArray(image, size))).compare(
                        m_directory + '/' + name, Qt::CaseInsensitive) == 0;
                if (!matches) { CloseHandle(handle); continue; }
                FILETIME created {}, ended {}, kernel {}, user {};
                if (!GetProcessTimes(handle, &created, &ended, &kernel, &user)) { incomplete("new process times", GetLastError()); CloseHandle(handle); continue; }
                const auto current = ticks(kernel) + ticks(user);
                // Процессы, созданные во время прогона, учитываются от начала своей жизни.
                const auto baseline = ticks(created) >= ticks(m_startedAt) ? 0 : current;
                m_processes.insert(entry.th32ProcessID, { handle, name, baseline, current, 0, 0 });
            } while (Process32NextW(snapshot, &entry));
        } else { incomplete("enumerate", GetLastError()); }
        CloseHandle(snapshot);
        quint64 working = 0, privateBytes = 0;
        for (auto &process : m_processes) {
            FILETIME created {}, ended {}, kernel {}, user {};
            if (GetProcessTimes(process.handle, &created, &ended, &kernel, &user)) { process.current = ticks(kernel) + ticks(user); }
            else { incomplete("process times", GetLastError()); }
            if (WaitForSingleObject(process.handle, 0) == WAIT_OBJECT_0) { continue; }
            PROCESS_MEMORY_COUNTERS_EX memory {}; memory.cb = sizeof(memory);
            if (!GetProcessMemoryInfo(process.handle, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory))) {
                incomplete("memory", GetLastError()); continue;
            }
            working += memory.WorkingSetSize; privateBytes += memory.PrivateUsage;
            process.working = std::max(process.working, quint64(memory.WorkingSetSize));
            process.privateBytes = std::max(process.privateBytes, quint64(memory.PrivateUsage));
        }
        m_working = std::max(m_working, working); m_privateBytes = std::max(m_privateBytes, privateBytes); ++m_samples;
    }
    QJsonObject result() const
    {
        QJsonArray processes; quint64 cpu = 0;
        for (const auto &process : m_processes) {
            const auto used = process.current >= process.baseline ? process.current - process.baseline : 0;
            cpu += used;
            processes.append(QJsonObject { { "image", process.name }, { "cpuMs", double(used) / 10000 },
                { "peakWorkingMiB", double(process.working) / 1048576 }, { "peakPrivateMiB", double(process.privateBytes) / 1048576 } });
        }
        return { { "complete", m_complete && m_samples > 1 }, { "issues", QJsonArray::fromStringList(m_issues) }, { "samples", m_samples }, { "elapsedMs", m_elapsed.elapsed() },
            { "peakSimultaneousWorkingMiB", double(m_working) / 1048576 }, { "peakSimultaneousPrivateMiB", double(m_privateBytes) / 1048576 },
            { "averageCpuPercentOneCore", double(cpu) / 100 / std::max(qint64(1), m_elapsed.elapsed()) }, { "processes", processes } };
    }
private:
    struct Process { HANDLE handle; QString name; quint64 baseline, current, working, privateBytes; };
    static quint64 ticks(const FILETIME &time) { return (quint64(time.dwHighDateTime) << 32) | time.dwLowDateTime; }
    void incomplete(const QString &operation, DWORD error) {
        m_complete = false; const auto issue = operation + ':' + QString::number(error);
        if (!m_issues.contains(issue) && m_issues.size() < 16) { m_issues.append(issue); }
    }
    QString m_directory;
    FILETIME m_startedAt {};
    QElapsedTimer m_elapsed;
    QMap<DWORD, Process> m_processes;
    quint64 m_working = 0, m_privateBytes = 0;
    int m_samples = 0;
    bool m_complete = true;
    QStringList m_issues;
};

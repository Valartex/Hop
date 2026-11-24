#pragma once

#include <QElapsedTimer>
#include <QHash>
#include "core/locations/platformTask.h"
#include <QQueue>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include "core/repositories/secureServersRepository.h"
#include "locationHealthHistory.h"
#include "locationHistoryStore.h"

namespace hop {
class EndpointHealthController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap statuses READ statuses NOTIFY changed)
    Q_PROPERTY(bool busy READ isBusy NOTIFY changed)
    Q_PROPERTY(bool available READ available CONSTANT)
public:
    explicit EndpointHealthController(SecureServersRepository *servers, QObject *parent = nullptr,
                                      const QString &probePath = QString(), const QString &awgProbePath = QString(), const QString &historyDirectory = QString());
    ~EndpointHealthController() override;
    QVariantMap statuses() const;
    bool isBusy() const { return !m_queue.isEmpty() || !m_active.isEmpty(); }
    bool available() const;
    bool canCheckLocation(const QString &id) const;
    Q_INVOKABLE void checkAll();
    void checkLocations(const QStringList &ids);
    void setHistorySelection(const QSet<QString> &ids, bool all) { m_historyIds = ids; m_historyAll = all; }
    Q_INVOKABLE bool checkLocation(const QString &id);
    Q_INVOKABLE QVariantMap history(const QString &id, int minutes = 0);
    void recordSample(const QString &id, int latency);
    void retainHistory(const QSet<QString> &ids) { m_history.retain(ids); if (m_store) { m_store->retain(ids); } emit historyChanged(); }
    Q_INVOKABLE void cancel();
    void resetNetworkHealth() { invalidate(); }
    void setTunnelActive(bool active);
    void setRefreshIntervalMinutes(int minutes) {
        m_autoRefresh.stop();
        if (minutes > 0) { m_autoRefresh.start(qBound(1, minutes, 60) * 60000); }
    }
signals:
    void changed();
    void historyChanged();
    void scheduledCheckRequested();
private:
    struct Result { QString state; int milliseconds = -1; qint64 sampledAt = -1; };
    bool enqueueCheck(const QString &id);
    void dispatch();
    void invalidate();
    void completed(PlatformTask *process, int exitCode, QProcess::ExitStatus exitStatus);
    QByteArray probeConfiguration(const QString &id, bool &awg) const;
    SecureServersRepository *m_servers;
    QString m_probePath;
    QString m_awgProbePath;
    bool m_tunnelActive = false;
    QElapsedTimer m_clock;
    QTimer m_freshness;
    QTimer m_autoRefresh;
    QQueue<QString> m_queue;
    QHash<PlatformTask *, QString> m_active;
    QSet<PlatformTask *> m_awgActive;
    QHash<QString, Result> m_results;
    LocationHealthHistory m_history;
    LocationHistoryStore *m_store = nullptr;
    QSet<QString> m_historyIds;
    bool m_historyAll = true;
};
} // namespace hop

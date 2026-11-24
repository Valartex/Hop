#pragma once
#include "locationHealthHistory.h"
#include <QObject>
#include <QFutureWatcher>
#include <QTimer>

namespace hop {
// Файловые операции исполняются последовательно в пуле, вне потока интерфейса.
class LocationHistoryStore : public QObject {
    Q_OBJECT
public:
    explicit LocationHistoryStore(QString directory, QObject *parent = nullptr);
    ~LocationHistoryStore() override;
    void record(const QString &id, int latency, qint64 at);
    void retain(const QSet<QString> &ids);
    QVariantMap statistics(const QString &id, qint64 since, qint64 until = 0);
    void flush();
    bool busy() const { return m_taskActive; }
signals:
    void changed();
private:
    using Points = QList<LocationHealthHistory::Sample>;
    struct Result { QString id, error; Points points; QHash<QString, Points> failed; };
    static Result process(const QString &directory, const QHash<QString, Points> &batch,
                          const QString &id, const QSet<QString> &retained, bool prune);
    void start();
    QString m_directory, m_requestedId, m_cachedId, m_error;
    Points m_cached;
    QHash<QString, Points> m_pending, m_inflight;
    QSet<QString> m_retained;
    bool m_prune = false, m_reload = false;
    bool m_taskActive = false;
    bool m_retentionKnown = false;
    qint64 m_lastCleanup = 0;
    int m_pendingCount = 0;
    LocationHealthHistory m_recent;
    QTimer m_flushTimer;
    QFutureWatcher<Result> m_watcher;
};
}

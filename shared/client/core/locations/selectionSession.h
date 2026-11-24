#pragma once

#include <QObject>
#include <QSet>
#include <QTimer>
#include <QElapsedTimer>
#include "locationCatalog.h"

namespace hop {
// Машина состояний не обращается к сети. Исполнитель подтверждает операции по токену.
class SelectionSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString activeId READ activeId NOTIFY changed)
    Q_PROPERTY(bool verified READ verified NOTIFY changed)
    Q_PROPERTY(bool restrictedActive READ restrictedActive NOTIFY changed)
public:
    enum State { Stopped, Preparing, Verifying, Healthy, Suspending, Backoff, Refreshing, Selecting };
    Q_ENUM(State)
    struct Timing { int healthMs = 15000; int retryMs = 30000; int attemptMs = 150000; unsigned failures = 3; int switchMs = 1000; int startupMs = 180000; };
    explicit SelectionSession(LocationCatalog *catalog, QObject *parent = nullptr);
    SelectionSession(LocationCatalog *catalog, Timing timing, QObject *parent);
    bool running() const { return m_running; }
    QString status() const { return m_status; }
    QString activeId() const { return m_active; }
    State state() const { return m_state; }
    bool verified() const { return m_running && m_state == Healthy; }
    bool restrictedActive() const { return m_running && m_restrictedPhase; }
    QString token() const { return QString::number(m_generation); }
    void start();
    void stop();
    void suspended(const QString &token, bool cleaned);
    void transportConnected(const QString &token);
    void transportStarting(const QString &token);
    void retarget(const QString &token, const QString &id);
    void transportFailed(const QString &token, const QString &reason);
    void healthResult(const QString &token, bool healthy, bool networkOnline = true, bool supported = true, int latencyMs = -1);
    void setLatencyPolicy(int durationMs, int thresholdMs);
    void setRefreshBeforeFailover(bool enabled) { m_refreshBeforeFailover = enabled; }
    void subscriptionRefreshed(const QString &token, bool successful, bool allowDetachedRetry = true);
    void candidatesMeasured(const QString &token, const QVariantMap &statuses);
    void setNetworkOnline(bool online);
    void networkChanged();
    void restartCurrent();
signals:
    void changed();
    void attemptRequested(const QString &id, const QString &token);
    void suspendRequested(const QString &token);
    void disconnectRequested();
    void healthRequested(const QString &id, const QString &token);
    void locationHealth(const QString &id, const QString &state);
    void subscriptionRefreshRequested(const QString &id, const QString &token);
    void candidateChecksRequested(const QStringList &ids, const QString &token);
protected:
    void stopWithFailure(const QString &reason);
private:
    QStringList networkCandidates() const;
    void beginNext();
    void openNext();
    void failed(const QString &reason);
    void setState(State state, const QString &message);
    void armHealth();
    LocationCatalog *m_catalog;
    Timing m_timing;
    State m_state = Stopped;
    bool m_running = false;
    bool m_networkOnline = true;
    bool m_restrictedPhase = false;
    QString m_preferred;
    QString m_active, m_status;
    quint64 m_generation = 0;
    unsigned m_failures = 0;
    unsigned m_retryRound = 0;
    QSet<QString> m_attempted;
    QSet<QString> m_refreshedServices;
    bool m_refreshBeforeFailover = false;
    bool m_refreshDetachPending = false, m_refreshDetached = false;
    QString m_failureReason;
    QStringList m_candidates;
    QString m_lastHealthy;
    bool m_skipRefresh = false;
    int m_latencyDurationMs = 0, m_latencyThresholdMs = 0;
    QElapsedTimer m_badLatency;
    QTimer m_healthTimer, m_attemptTimer, m_retryTimer, m_startupTimer;
};
}

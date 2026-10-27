#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QTimer>

#include "subscriptionFetcher.h"

class SecureServersRepository;
class ImportController;
class ConnectionController;

namespace hop {

class HttpSubscriptionsController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ isBusy NOTIFY stateChanged)
    Q_PROPERTY(int sourceCount READ sourceCount NOTIFY sourcesChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)

public:
    HttpSubscriptionsController(SecureServersRepository *servers, ImportController *importer,
                                ConnectionController *connection, QObject *parent = nullptr, const QString &helperPath = QString());
    ~HttpSubscriptionsController() override;
    bool isBusy() const;
    int sourceCount() const;
    QString lastError() const { return m_lastError; }
    QVariantMap sourceStatuses() const { return m_sourceStatuses; }
    void setRefreshIntervalMinutes(int minutes) { m_refreshMinutes = qBound(0, minutes, 1440); }

public slots:
    bool importSubscription(const QString &url, const QString &name = QString());
    void refreshAll();
    bool refreshSource(const QString &id, bool userInitiated = true);
    bool removeSource(const QString &id);

signals:
    void stateChanged();
    void sourcesChanged();
    void subscriptionImported();
    void finished(const QString &message);
    void errorOccurred(const QString &message);
    void sourceRefreshFinished(const QString &id, bool successful);

private:
    struct Source {
        QString id;
        QUrl url;
        QString name;
        QByteArray etag;
        QByteArray lastModified;
        QDateTime updatedAt;
        bool userInitiated = false;
        bool customName = false;
    };
    QVector<Source> sources() const;
    bool enqueue(const Source &source);
    void processNext();
    void consume(const QByteArray &body, const QByteArray &etag, const QByteArray &lastModified, bool notModified);
    void fail(const QString &message, int retryAfterSeconds = 0);
    void refreshDue();
    void removeRetired();

    SecureServersRepository *m_servers;
    ImportController *m_importer;
    ConnectionController *m_connection;
    SubscriptionFetcher m_fetcher;
    QTimer m_refreshTimer;
    int m_refreshMinutes = 360;
    QQueue<Source> m_queue;
    Source m_current;
    QString m_lastError;
    bool m_connectionActive = false;
    QHash<QString, QDateTime> m_nextAttempt;
    QHash<QString, int> m_failures;
    QVariantMap m_sourceStatuses;
};

} // namespace hop

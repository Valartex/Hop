#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include "core/locations/platformTask.h"

namespace hop {

class SubscriptionFetcher : public QObject {
    Q_OBJECT
public:
    enum class Payload { Subscription, RouteData };
    explicit SubscriptionFetcher(QObject *parent = nullptr, bool controlPlane = false, const QString &helperPath = QString(),
                                 Payload payload = Payload::Subscription);
    ~SubscriptionFetcher() override;
    bool fetch(const QUrl &url, const QByteArray &etag = {}, const QByteArray &lastModified = {});
    bool isBusy() const { return !m_reply.isNull() || !m_helper.isNull(); }
    QString profileTitle() const { return m_profileTitle; }

signals:
    void loaded(const QByteArray &body, const QByteArray &etag, const QByteArray &lastModified, bool notModified);
    void failed(const QString &message, int retryAfterSeconds);

private:
    bool fetchInternal(const QUrl &url, const QByteArray &etag, const QByteArray &lastModified, int redirects);
    bool fetchInHelper(const QUrl &url, const QByteArray &etag, const QByteArray &lastModified);
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_deadline;
    QByteArray m_body;
    QString m_localError;
    QString m_profileTitle;
    bool m_controlPlane = false;
    bool m_routeData = false;
    qsizetype m_limit = 4 * 1024 * 1024;
    QString m_helperPath;
    QPointer<PlatformTask> m_helper;
};

} // namespace hop

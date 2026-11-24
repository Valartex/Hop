#include "subscriptionFetcher.h"
#include "subscriptionParser.h"
#include "subscriptionNaming.h"

#include <QDateTime>
#include <QNetworkRequest>
#include <utility>
#include <QJsonDocument>
#include <QRegularExpression>
#include "core/locations/controlPlaneResolver.h"

namespace hop {

SubscriptionFetcher::SubscriptionFetcher(QObject *parent, bool controlPlane, const QString &helperPath, Payload payload)
    : QObject(parent), m_controlPlane(controlPlane), m_helperPath(helperPath)
{
    m_routeData = payload == Payload::RouteData;
    m_limit = m_routeData ? 64 * 1024 * 1024 : maximumSubscriptionBytes;
    if (m_controlPlane) { m_network.setProxy(QNetworkProxy::NoProxy); }
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(m_routeData ? 45000 : 20000);
    connect(&m_deadline, &QTimer::timeout, this, [this]() {
        if (m_helper) { m_helper->kill(); }
        if (m_reply) {
            m_localError = tr("Превышено время загрузки подписки");
            m_reply->abort();
        }
    });
}

SubscriptionFetcher::~SubscriptionFetcher()
{
    m_deadline.stop();
    if (m_reply) { disconnect(m_reply, nullptr, this, nullptr); }
    if (m_helper) { disconnect(m_helper, nullptr, this, nullptr); m_helper->kill(); m_helper->waitForFinished(1000); }
}

bool SubscriptionFetcher::fetch(const QUrl &url, const QByteArray &etag, const QByteArray &lastModified)
{
    if (isBusy()) { return false; }
    m_profileTitle.clear();
    return fetchInternal(url, etag, lastModified, 0);
}

bool SubscriptionFetcher::fetchInternal(const QUrl &url, const QByteArray &etag, const QByteArray &lastModified, int redirects)
{
    if (isBusy() || !url.isValid() || url.scheme() != QStringLiteral("https")
        || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasFragment()) {
        return false;
    }
    const auto validHeader = [](const QByteArray &header) {
        return header.size() <= 1024 && !header.contains('\r') && !header.contains('\n');
    };
    if (!validHeader(etag) || !validHeader(lastModified)) {
        return false;
    }
    if (!m_helperPath.isEmpty()) { return fetchInHelper(url, etag, lastModified); }
    QNetworkRequest request(url);
#ifdef Q_OS_WIN
    if (m_controlPlane) {
        const auto addresses = controlPlane::resolve(url.host());
        if (addresses.isEmpty()) { return false; }
        auto direct = url; direct.setHost(addresses.first());
        controlPlane::permit(addresses.first(), url.port(443));
        request.setUrl(direct); request.setPeerVerifyName(url.host());
        request.setRawHeader("Host", url.authority(QUrl::FullyEncoded).toLatin1());
        // В HTTP/2 :authority строится из URL, поэтому для соединения с IP используем исходный Host в HTTP/1.1.
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    }
#endif
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (m_controlPlane) { request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy); }
    request.setMaximumRedirectsAllowed(5);
    request.setTransferTimeout(15000);
    request.setRawHeader("User-Agent", "Hop/0.1");
    request.setRawHeader("Accept", "text/plain, application/octet-stream");
    request.setRawHeader("Accept-Encoding", "identity");
    if (!etag.isEmpty()) { request.setRawHeader("If-None-Match", etag); }
    if (!lastModified.isEmpty()) { request.setRawHeader("If-Modified-Since", lastModified); }

    m_body.clear();
    m_localError.clear();
    m_reply = m_network.get(request);
    m_reply->setReadBufferSize(m_limit + 1);
    connect(m_reply, &QIODevice::readyRead, this, [this]() {
        if (!m_reply) { return; }
        m_body.append(m_reply->read(m_limit + 1 - m_body.size()));
        if (m_body.size() > m_limit) {
            m_localError = tr("Подписка превышает допустимый размер");
            m_reply->abort();
        }
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, validHeader, url, etag, lastModified, redirects, conditional = !etag.isEmpty() || !lastModified.isEmpty()]() {
        m_deadline.stop();
        QNetworkReply *reply = m_reply.data();
        if (!reply) { return; }
        m_reply.clear();
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (m_controlPlane && (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)) {
            const auto next = url.resolved(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
            if (redirects >= 5 || !fetchInternal(next, etag, lastModified, redirects + 1)) { emit failed(tr("Недопустимое перенаправление подписки"), 0); }
            return;
        }
        const bool notModified = status == 304 && conditional;
        QString failure = m_localError;
        if (failure.isEmpty() && (reply->error() != QNetworkReply::NoError
                                 || reply->url().scheme() != QStringLiteral("https")
                                 || (status != 200 && !notModified))) {
            // errorString может содержать секретный URL, поэтому его не показываем.
            failure = tr("Не удалось загрузить подписку (HTTP %1, код %2)").arg(status).arg(int(reply->error()));
        }
        if (!failure.isEmpty()) {
            bool numeric = false;
            int retryAfter = reply->rawHeader("Retry-After").toInt(&numeric);
            if (!numeric) {
                const auto date = QDateTime::fromString(QString::fromLatin1(reply->rawHeader("Retry-After")), Qt::RFC2822Date);
                retryAfter = date.isValid() ? static_cast<int>(qBound(qint64(0), QDateTime::currentDateTimeUtc().secsTo(date), qint64(86400))) : 0;
            }
            emit failed(failure, qBound(0, retryAfter, 86400));
            return;
        }
        const QByteArray body = std::move(m_body);
        const auto responseEtag = reply->rawHeader("ETag");
        const auto responseModified = reply->rawHeader("Last-Modified");
        if (!m_routeData) { m_profileTitle = decodeSubscriptionTitle(reply->rawHeader("profile-title")); }
        emit loaded(body, validHeader(responseEtag) ? responseEtag : QByteArray(),
                    validHeader(responseModified) ? responseModified : QByteArray(), notModified);
    });
    m_deadline.start();
    return true;
}

bool SubscriptionFetcher::fetchInHelper(const QUrl &url, const QByteArray &etag, const QByteArray &modified)
{
    auto *process = new PlatformTask(this); m_helper = process;
    process->setProgram(m_helperPath); process->setArguments({ QStringLiteral("--provider-task") });
    process->setProcessEnvironment(controlPlane::environment());
    const QJsonObject request { { QStringLiteral("action"), m_routeData ? QStringLiteral("routes") : QStringLiteral("subscription") }, { QStringLiteral("url"), url.toString(QUrl::FullyEncoded) },
        { QStringLiteral("etag"), QString::fromLatin1(etag.toBase64()) }, { QStringLiteral("modified"), QString::fromLatin1(modified.toBase64()) } };
    const auto input = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    const auto output = QSharedPointer<QByteArray>::create();
    connect(process, &QProcess::started, this, [process, input]() { process->write(input); process->closeWriteChannel(); });
    connect(process, &QProcess::readyReadStandardError, this, [process]() { process->readAllStandardError(); });
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, output]() {
        const auto chunk = process->readAllStandardOutput();
        if (output->size() + chunk.size() > (m_routeData ? m_limit + 4096 : 6 * 1024 * 1024)) { output->clear(); process->kill(); }
        else { output->append(chunk); }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_helper != process) { return; }
        m_deadline.stop(); m_helper = nullptr; process->deleteLater();
        emit failed(tr("Не удалось запустить загрузку подписки"), 0);
    });
    connect(process, &QProcess::finished, this, [this, process, output](int code, QProcess::ExitStatus status) {
        output->append(process->readAllStandardOutput());
        m_deadline.stop(); m_helper = nullptr; process->deleteLater();
        const auto newline = m_routeData ? output->indexOf('\n') : -1;
        const bool framed = m_routeData && newline >= 0 && newline <= 4096 && output->size() <= m_limit + 4096;
        const auto response = m_routeData ? (framed ? QJsonDocument::fromJson(output->left(newline)).object() : QJsonObject())
            : (output->size() <= 6 * 1024 * 1024 ? QJsonDocument::fromJson(*output).object() : QJsonObject());
        if (code != 0 || status != QProcess::NormalExit || response.value(QStringLiteral("code")).toInt(-1) != 0) {
            // Из подпроцесса передаём только известную стадию или числовые коды, без URL и текста сервера.
            const auto detail = response.value(QStringLiteral("detail")).toString();
            const auto codes = QRegularExpression(QStringLiteral("\\(HTTP ([0-9]{1,3}), код ([0-9]{1,3})\\)")).match(detail);
            const auto message = detail == QStringLiteral("request-start") ? tr("Не удалось определить адрес подписки")
                : codes.hasMatch() ? tr("Не удалось загрузить подписку (HTTP %1, код %2)").arg(codes.captured(1), codes.captured(2))
                : tr("Не удалось загрузить подписку");
            emit failed(message, qBound(0, response.value(QStringLiteral("retryAfter")).toInt(), 86400)); return;
        }
        const auto body = m_routeData ? output->mid(newline + 1)
            : QByteArray::fromBase64(response.value(QStringLiteral("body")).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (m_routeData && (response.value(QStringLiteral("bytes")).toDouble(-1) != body.size()
            || (response.value(QStringLiteral("unchanged")).toBool() && !body.isEmpty()))) {
            emit failed(tr("Некорректный ответ загрузчика DAT"), 0); return;
        }
        const auto etag = QByteArray::fromBase64(response.value(QStringLiteral("etag")).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        const auto modified = QByteArray::fromBase64(response.value(QStringLiteral("modified")).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (body.size() > m_limit || etag.size() > 1024 || modified.size() > 1024
            || etag.contains('\r') || etag.contains('\n') || modified.contains('\r') || modified.contains('\n')) {
            emit failed(tr("Некорректный ответ загрузчика подписки"), 0); return;
        }
        if (!m_routeData) { m_profileTitle = validatedSubscriptionTitle(response.value(QStringLiteral("profileTitle")).toString()); }
        emit loaded(body, etag, modified, response.value(QStringLiteral("unchanged")).toBool());
    });
    m_deadline.start(45000); process->start(); return true;
}
} // namespace hop

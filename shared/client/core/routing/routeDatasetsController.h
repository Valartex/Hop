#pragma once

#include "geoData.h"
#include "core/subscriptions/subscriptionFetcher.h"
#include "secureQSettings.h"
#include <QFutureWatcher>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QSet>
#include <QJsonArray>
#include <QHash>
#include <functional>

namespace hop {
class RouteDatasetsController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ isBusy NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    Q_PROPERTY(QVariantList categories READ categories NOTIFY changed)
    Q_PROPERTY(QString sha256 READ sha256 NOTIFY changed)
    Q_PROPERTY(qsizetype byteCount READ byteCount NOTIFY changed)
    Q_PROPERTY(QVariantMap sources READ sources NOTIFY changed)
public:
    explicit RouteDatasetsController(QObject *parent = nullptr, const QString &cacheRoot = QString(),
                                     const QString &helperPath = QString(), SecureQSettings *settings = nullptr);
    ~RouteDatasetsController() override;
    bool isBusy() const { return m_busy; }
    QString lastError() const { return m_error; }
    QString kind() const { return m_data.kind == GeoDataKind::GeoIp ? QStringLiteral("geoip") : QStringLiteral("geosite"); }
    QVariantList categories() const;
    QString sha256() const { return m_sha256; }
    qsizetype byteCount() const { return m_data.bytes.size(); }
    QVariantMap sources() const { return m_sources.toVariantMap(); }
    Q_INVOKABLE bool loadCached(const QString &kind);
    Q_INVOKABLE bool importFile(const QUrl &file, const QString &kind);
    Q_INVOKABLE bool download(const QUrl &url, const QString &kind);
    Q_INVOKABLE bool setAutoRefresh(const QString &kind, bool enabled);
    Q_INVOKABLE void refreshDue();
    void setValidator(std::function<QString(const GeoDataSet &)> validator) { m_validator = std::move(validator); }
    QString cachedPath(const QString &kind) const;
    const GeoDataSet &data() const { return m_data; }
    QString defaultSource(const QString &kind) const;
    QString sourcePath(const QString &kind, const QString &source) const;
    QVariantMap sourceState(const QString &kind, const QString &source) const;
    void setRuleSources(const QJsonArray &rules);
    bool refreshSource(const QString &kind, const QString &source);
    void setSourceValidator(std::function<QString(const QString &, const GeoDataSet &)> validator) { m_sourceValidator = std::move(validator); }
signals:
    void changed();
    void datasetUpdated(const QString &kind);
private:
    bool begin(const QString &kind, GeoDataKind &parsed, const QString &key = {});
    QString pathForKey(const QString &key) const;
    void startNextSource();
    bool startDownload(const QUrl &url, const QString &kind, bool background);
    bool saveSources(const QJsonObject &next);
    void parse(QByteArray bytes, GeoDataKind kind, bool save);
    void finishError(const QString &message, int retryAfter = 0);
    QString m_cacheRoot;
    GeoDataSet m_data;
    QString m_error, m_sha256, m_pendingKind, m_pendingKey;
    QJsonObject m_sources, m_pendingSource;
    SecureQSettings *m_settings;
    bool m_busy = false, m_localImport = false, m_background = false;
    SubscriptionFetcher m_fetcher;
    QTimer m_refreshTimer;
    QFutureWatcher<GeoDataSet> m_parser;
    std::function<QString(const GeoDataSet &)> m_validator;
    std::function<QString(const QString &, const GeoDataSet &)> m_sourceValidator;
    QSet<QString> m_ruleSources, m_autoSources, m_legacyKinds;
    QStringList m_sourceQueue;
    QHash<QString, QString> m_sourceErrors;
    bool m_sourcesBound = false;
};
} // namespace hop

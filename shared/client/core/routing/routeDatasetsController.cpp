#include "routeDatasetsController.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtConcurrentRun>

namespace hop {
namespace {
const auto sourcesKey = QStringLiteral("Conf/hopRouteSources");
bool validKind(const QString &kind) { return kind == QStringLiteral("geoip") || kind == QStringLiteral("geosite"); }
bool validSource(const QUrl &url) {
    return url.isValid() && url.scheme() == QStringLiteral("https") && !url.host().isEmpty()
        && url.userInfo().isEmpty() && !url.hasFragment() && url.toEncoded().size() <= 16384;
}
QString digest(const QByteArray &data) { return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()); }
}
RouteDatasetsController::RouteDatasetsController(QObject *parent, const QString &cacheRoot, const QString &helperPath, SecureQSettings *settings)
    : QObject(parent), m_cacheRoot(cacheRoot.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/routes") : cacheRoot), m_settings(settings),
      m_fetcher(this, false, helperPath, SubscriptionFetcher::Payload::RouteData)
{
    if (settings) {
        const auto bytes = settings->value(sourcesKey).toByteArray();
        const auto saved = bytes.size() <= 4 * 1024 * 1024 ? QJsonDocument::fromJson(bytes).object() : QJsonObject();
        for (auto it = saved.begin(); it != saved.end(); ++it) {
            const auto source = it.value().toObject();
            const auto value = source.value(QStringLiteral("url")).toString();
            if ((validKind(it.key()) && validSource(QUrl(value)))
                || (!value.isEmpty() && geoDataSourceId(source.value(QStringLiteral("kind")).toString(), value) == it.key())) {
                m_sources.insert(it.key(), source);
            }
        }
    }
    connect(&m_fetcher, &SubscriptionFetcher::failed, this, &RouteDatasetsController::finishError);
    connect(&m_fetcher, &SubscriptionFetcher::loaded, this, [this](const QByteArray &bytes, const QByteArray &etag,
        const QByteArray &modified, bool unchanged) {
        m_pendingSource.insert(QStringLiteral("etag"), QString::fromLatin1(etag.toBase64()));
        m_pendingSource.insert(QStringLiteral("modified"), QString::fromLatin1(modified.toBase64()));
        const auto parsed = m_pendingKind == QStringLiteral("geoip") ? GeoDataKind::GeoIp : GeoDataKind::GeoSite;
        if (unchanged) {
            QFile file(pathForKey(m_pendingKey));
            if (!file.open(QIODevice::ReadOnly) || file.size() > maximumGeoDataBytes) {
                finishError(tr("Сервер вернул 304, но сохранённый DAT недоступен")); return;
            }
            const auto cached = file.read(maximumGeoDataBytes + 1);
            if (digest(cached) != m_pendingSource.value(QStringLiteral("sha256")).toString()) {
                finishError(tr("Кэш DAT изменился во время обновления")); return;
            }
            parse(cached, parsed, false);
        } else { parse(bytes, parsed, true); }
    });
    m_refreshTimer.setInterval(60000);
    connect(&m_refreshTimer, &QTimer::timeout, this, &RouteDatasetsController::refreshDue);
    m_refreshTimer.start();
    QTimer::singleShot(2000, this, &RouteDatasetsController::refreshDue);
}

RouteDatasetsController::~RouteDatasetsController() { disconnect(&m_parser, nullptr, this, nullptr); }
QVariantList RouteDatasetsController::categories() const {
    QVariantList result;
    result.reserve(m_data.categories.size());
    for (const auto &category : m_data.categories) {
        result.append(QVariantMap { { QStringLiteral("code"), category.code }, { QStringLiteral("entries"), category.entries },
            { QStringLiteral("reverseMatch"), category.reverseMatch } });
    }
    return result;
}
QString RouteDatasetsController::cachedPath(const QString &kind) const {
    return validKind(kind) ? QDir(m_cacheRoot).filePath(kind + QStringLiteral(".dat")) : QString();
}
bool RouteDatasetsController::begin(const QString &kind, GeoDataKind &parsed, const QString &key) {
    if (m_busy) { return false; }
    if (!validKind(kind)) { finishError(tr("Выберите GeoIP или GeoSite")); return false; }
    parsed = kind == QStringLiteral("geoip") ? GeoDataKind::GeoIp : GeoDataKind::GeoSite;
    m_busy = true; m_error.clear(); m_pendingKind = kind; m_pendingKey = key.isEmpty() ? kind : key;
    m_pendingSource = {}; m_localImport = false; m_background = false;
    emit changed(); return true;
}
bool RouteDatasetsController::saveSources(const QJsonObject &next) {
    const auto bytes = QJsonDocument(next).toJson(QJsonDocument::Compact);
    if (bytes.size() > 4 * 1024 * 1024 || (m_settings && !m_settings->setValueChecked(sourcesKey, bytes))) { return false; }
    m_sources = next; return true;
}
bool RouteDatasetsController::setAutoRefresh(const QString &kind, bool enabled) {
    if (m_busy || !validKind(kind) || !m_sources.contains(kind)) { return false; }
    auto next = m_sources; auto source = next.value(kind).toObject();
    source.insert(QStringLiteral("auto"), enabled); next.insert(kind, source);
    if (!saveSources(next)) { m_error = tr("Не удалось сохранить расписание DAT"); emit changed(); return false; }
    emit changed(); return true;
}
void RouteDatasetsController::refreshDue() {
    if (m_busy) { return; }
    const auto now = QDateTime::currentSecsSinceEpoch();
    for (const auto &key : m_autoSources) {
        const auto source = m_sources.value(key).toObject();
        if (source.value(QStringLiteral("next")).toDouble() <= now) {
            refreshSource(source.value(QStringLiteral("kind")).toString(), source.value(QStringLiteral("url")).toString());
        }
    }
    if (!m_sourceQueue.isEmpty()) { startNextSource(); return; }
    for (const auto &name : { QStringLiteral("geoip"), QStringLiteral("geosite") }) {
        if (m_sourcesBound && !m_legacyKinds.contains(name)) { continue; }
        const auto source = m_sources.value(name).toObject();
        if (!source.isEmpty() && source.value(QStringLiteral("auto")).toBool(true)
            && source.value(QStringLiteral("next")).toDouble() <= now) {
            startDownload(QUrl(source.value(QStringLiteral("url")).toString()), name, true); return;
        }
    }
}
void RouteDatasetsController::finishError(const QString &message, int retryAfter) {
    const auto previous = m_sources.value(m_pendingKey).toObject();
    if (!m_pendingSource.isEmpty() && previous.value(QStringLiteral("url")) == m_pendingSource.value(QStringLiteral("url"))) {
        auto next = m_sources; auto source = previous;
        const auto failures = qBound(0, source.value(QStringLiteral("failures")).toInt(), 7) + 1;
        source.insert(QStringLiteral("failures"), failures);
        source.insert(QStringLiteral("next"), QDateTime::currentSecsSinceEpoch() + qMax(qBound(0, retryAfter, 86400), qMin(86400, 300 * (1 << (failures - 1)))));
        next.insert(m_pendingKey, source); saveSources(next);
    }
    if (m_pendingKey != m_pendingKind && !m_pendingKey.isEmpty()) { m_sourceErrors.insert(m_pendingKey, message); }
    m_pendingSource = {}; m_pendingKind.clear(); m_pendingKey.clear(); m_error = message; m_busy = false; emit changed();
    QTimer::singleShot(0, this, &RouteDatasetsController::startNextSource);
}
void RouteDatasetsController::parse(QByteArray bytes, GeoDataKind kind, bool save) {
    connect(&m_parser, &QFutureWatcher<GeoDataSet>::finished, this, [this, save]() {
        disconnect(&m_parser, nullptr, this, nullptr);
        auto result = m_parser.result();
        if (!result.isValid()) { finishError(result.error); return; }
        if (save) {
            const auto error = m_pendingKey == m_pendingKind ? (m_validator ? m_validator(result) : QString())
                : (m_sourceValidator ? m_sourceValidator(m_pendingKey, result) : QString());
            if (!error.isEmpty()) { finishError(error); return; }
        }
        if (save) {
            QSaveFile file(pathForKey(m_pendingKey));
            if (!QDir().mkpath(m_cacheRoot) || !file.open(QIODevice::WriteOnly)
                || file.write(result.bytes) != result.bytes.size() || !file.commit()) {
                finishError(tr("Не удалось сохранить DAT; предыдущий набор оставлен")); return;
            }
        }
        const auto hash = digest(result.bytes);
        auto next = m_sources;
        if (m_localImport) { next.remove(m_pendingKind); }
        if (!m_pendingSource.isEmpty()) {
            m_pendingSource.insert(QStringLiteral("sha256"), hash); m_pendingSource.insert(QStringLiteral("failures"), 0);
            m_pendingSource.insert(QStringLiteral("updated"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
            m_pendingSource.insert(QStringLiteral("next"), QDateTime::currentSecsSinceEpoch() + 86400);
            next.insert(m_pendingKey, m_pendingSource);
        }
        const bool sourceSaved = next == m_sources || saveSources(next);
        if (!sourceSaved) { m_error = tr("DAT сохранён, но расписание обновлений сохранить не удалось"); }
        const auto updatedKind = m_pendingKey;
        if (!m_background || result.kind == m_data.kind || m_data.bytes.isEmpty()) { m_sha256 = hash; m_data = std::move(result); }
        if (sourceSaved) { m_sourceErrors.remove(m_pendingKey); }
        else if (m_pendingKey != m_pendingKind) { m_sourceErrors.insert(m_pendingKey, m_error); }
        m_pendingSource = {}; m_pendingKind.clear(); m_pendingKey.clear(); m_busy = false; emit changed();
        if (save) { emit datasetUpdated(updatedKind); }
        QTimer::singleShot(0, this, &RouteDatasetsController::startNextSource);
    });
    m_parser.setFuture(QtConcurrent::run([bytes = std::move(bytes), kind]() mutable { return parseGeoData(std::move(bytes), kind); }));
}
bool RouteDatasetsController::loadCached(const QString &kind) {
    GeoDataKind parsed;
    if (!begin(kind, parsed)) { return false; }
    QFile file(cachedPath(kind));
    if (!file.exists()) { finishError(tr("Сохранённого набора нет. Загрузите DAT по ссылке или выберите файл.")); return false; }
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumGeoDataBytes) { finishError(tr("Кэш DAT недоступен или слишком велик")); return false; }
    parse(file.read(maximumGeoDataBytes + 1), parsed, false); return true;
}
bool RouteDatasetsController::importFile(const QUrl &fileUrl, const QString &kind) {
    GeoDataKind parsed;
    if (!begin(kind, parsed)) { return false; }
    auto fileName = fileUrl.toLocalFile();
#ifdef Q_OS_ANDROID
    // Системный выбор файла Android выдаёт URI с временным разрешением на чтение.
    if (fileUrl.scheme() == QStringLiteral("content") && !fileUrl.host().isEmpty()
        && fileUrl.userInfo().isEmpty() && !fileUrl.hasFragment()) {
        fileName = fileUrl.toString(QUrl::FullyEncoded);
    }
#endif
    if (fileName.isEmpty()) { finishError(tr("Выберите локальный файл DAT")); return false; }
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumGeoDataBytes) { finishError(tr("Файл DAT недоступен или слишком велик")); return false; }
    m_localImport = true; parse(file.read(maximumGeoDataBytes + 1), parsed, true); return true;
}
bool RouteDatasetsController::download(const QUrl &url, const QString &kind) { return startDownload(url, kind, false); }
bool RouteDatasetsController::startDownload(const QUrl &url, const QString &kind, bool background) {
    GeoDataKind parsed;
    if (!begin(kind, parsed)) { return false; }
    if (!validSource(url)) { finishError(tr("Нужна корректная HTTPS-ссылка на DAT")); return false; }
    m_background = background;
    const auto previous = m_sources.value(kind).toObject();
    m_pendingSource = previous.value(QStringLiteral("url")).toString() == url.toString(QUrl::FullyEncoded) ? previous : QJsonObject();
    m_pendingSource.insert(QStringLiteral("url"), url.toString(QUrl::FullyEncoded));
    if (!m_pendingSource.contains(QStringLiteral("auto"))) { m_pendingSource.insert(QStringLiteral("auto"), true); }
    QByteArray etag, modified;
    QFile cached(cachedPath(kind));
    if (cached.open(QIODevice::ReadOnly) && cached.size() <= maximumGeoDataBytes) {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (hash.addData(&cached) && QString::fromLatin1(hash.result().toHex()) == m_pendingSource.value(QStringLiteral("sha256")).toString()) {
            etag = QByteArray::fromBase64(m_pendingSource.value(QStringLiteral("etag")).toString().toLatin1());
            modified = QByteArray::fromBase64(m_pendingSource.value(QStringLiteral("modified")).toString().toLatin1());
        }
    }
    if (!m_fetcher.fetch(url, etag, modified)) { finishError(tr("Не удалось начать загрузку DAT")); return false; }
    return true;
}
QString RouteDatasetsController::defaultSource(const QString &kind) const
{
    if (!validKind(kind)) { return {}; }
    const auto previous = m_sources.value(kind).toObject().value(QStringLiteral("url")).toString();
    return previous.isEmpty() ? QStringLiteral("https://github.com/kepler103b/geo-for-ru-only/releases/download/latest/")
        + kind + QStringLiteral(".dat") : previous;
}

QString RouteDatasetsController::pathForKey(const QString &key) const
{
    return key.isEmpty() ? QString() : QDir(m_cacheRoot).filePath(key + QStringLiteral(".dat"));
}

QString RouteDatasetsController::sourcePath(const QString &kind, const QString &source) const
{
    return pathForKey(geoDataSourceId(kind, source));
}

QVariantMap RouteDatasetsController::sourceState(const QString &kind, const QString &source) const
{
    const auto key = geoDataSourceId(kind, source);
    auto state = m_sources.value(key).toObject().toVariantMap();
    state.insert(QStringLiteral("updating"), !key.isEmpty() && (m_pendingKey == key || m_sourceQueue.contains(key)));
    state.insert(QStringLiteral("error"), m_sourceErrors.value(key));
    return state;
}

void RouteDatasetsController::setRuleSources(const QJsonArray &rules)
{
    m_sourcesBound = true; m_ruleSources.clear(); m_autoSources.clear(); m_legacyKinds.clear();
    auto next = m_sources;
    for (const auto &value : rules) {
        const auto rule = value.toObject();
        const auto ruleKind = rule.value(QStringLiteral("kind")).toString();
        const auto kind = geoSourceKind(ruleKind);
        if (validKind(ruleKind)) { m_legacyKinds.insert(ruleKind); }
        if (kind.isEmpty()) { continue; }
        const auto source = normalizeGeoDataSource(rule.value(QStringLiteral("value")).toString());
        const auto key = geoDataSourceId(kind, source);
        if (key.isEmpty()) { continue; }
        m_ruleSources.insert(key);
        if (rule.value(QStringLiteral("autoUpdate")).toBool(true)) { m_autoSources.insert(key); }
        if (!next.contains(key)) {
            next.insert(key, QJsonObject { { QStringLiteral("kind"), kind }, { QStringLiteral("url"), source },
                { QStringLiteral("next"), 0 } });
        }
    }
    if (next != m_sources && !saveSources(next)) {
        m_error = tr("Не удалось сохранить источники маршрутов");
        for (const auto &key : m_ruleSources) { if (!m_sources.contains(key)) { m_sourceErrors.insert(key, m_error); } }
        emit changed(); return;
    }
    for (qsizetype index = m_sourceQueue.size(); index-- > 0;) {
        if (!m_ruleSources.contains(m_sourceQueue[index])) { m_sourceQueue.removeAt(index); }
    }
    for (const auto &key : m_ruleSources) {
        if (!QFile::exists(pathForKey(key))) {
            const auto source = m_sources.value(key).toObject();
            refreshSource(source.value(QStringLiteral("kind")).toString(), source.value(QStringLiteral("url")).toString());
        }
    }
}

bool RouteDatasetsController::refreshSource(const QString &kind, const QString &source)
{
    const auto key = geoDataSourceId(kind, source);
    if (key.isEmpty() || !m_ruleSources.contains(key)) { return false; }
    if (!m_sources.contains(key)) {
        auto next = m_sources;
        next.insert(key, QJsonObject { { "kind", kind }, { "url", normalizeGeoDataSource(source) }, { "next", 0 } });
        if (!saveSources(next)) { m_sourceErrors.insert(key, tr("Не удалось сохранить источники маршрутов")); emit changed(); return false; }
    }
    if (m_pendingKey == key || m_sourceQueue.contains(key)) { return true; }
    m_sourceErrors.remove(key); m_sourceQueue.append(key); emit changed();
    QTimer::singleShot(0, this, &RouteDatasetsController::startNextSource);
    return true;
}

void RouteDatasetsController::startNextSource()
{
    if (m_busy || m_sourceQueue.isEmpty()) { return; }
    const auto key = m_sourceQueue.takeFirst();
    if (!m_ruleSources.contains(key)) { startNextSource(); return; }
    const auto source = m_sources.value(key).toObject();
    const auto kind = source.value(QStringLiteral("kind")).toString();
    const auto value = source.value(QStringLiteral("url")).toString();
    GeoDataKind parsed;
    if (!begin(kind, parsed, key)) { return; }
    m_background = true; m_pendingSource = source;
    const QUrl url(value);
    if (url.scheme() == QStringLiteral("https")) {
        QByteArray etag, modified;
        QFile cached(pathForKey(key));
        if (cached.open(QIODevice::ReadOnly) && cached.size() <= maximumGeoDataBytes) {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (hash.addData(&cached) && QString::fromLatin1(hash.result().toHex()) == source.value(QStringLiteral("sha256")).toString()) {
                etag = QByteArray::fromBase64(source.value(QStringLiteral("etag")).toString().toLatin1());
                modified = QByteArray::fromBase64(source.value(QStringLiteral("modified")).toString().toLatin1());
            }
        }
        if (!m_fetcher.fetch(url, etag, modified) && m_busy) { finishError(tr("Не удалось начать загрузку DAT")); }
    } else {
        QFile file(value);
        if (!file.open(QIODevice::ReadOnly) || file.size() > maximumGeoDataBytes) {
            finishError(tr("Файл DAT недоступен или слишком велик")); return;
        }
        parse(file.read(maximumGeoDataBytes + 1), parsed, true);
    }
}
} // namespace hop

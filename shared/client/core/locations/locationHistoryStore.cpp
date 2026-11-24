#include "locationHistoryStore.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QtEndian>
#include <algorithm>

namespace hop {
namespace {
constexpr qint64 fileLimit = 2 * 1024 * 1024;
constexpr qint64 directoryLimit = 256 * 1024 * 1024;
const QByteArray magic("HOPHST01");
QString fileName(const QString &id) { return QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".hph"; }
quint32 checksum(qint64 at, int latency) { return quint32(at) ^ quint32(quint64(at) >> 32) ^ quint32(latency) ^ 0x48504f31u; }
QByteArray encode(const QList<LocationHealthHistory::Sample> &points) {
    QByteArray bytes(points.size() * 16, Qt::Uninitialized);
    auto *out = reinterpret_cast<uchar *>(bytes.data());
    for (const auto &point : points) {
        qToLittleEndian<qint64>(point.at, out); qToLittleEndian<qint32>(point.latency, out + 8);
        qToLittleEndian<quint32>(checksum(point.at, point.latency), out + 12); out += 16;
    }
    return bytes;
}
QList<LocationHealthHistory::Sample> decode(const QByteArray &bytes, qint64 now) {
    QList<LocationHealthHistory::Sample> points;
    if (!bytes.startsWith(magic)) { return points; }
    for (qsizetype offset = magic.size(); offset + 16 <= bytes.size(); offset += 16) {
        const auto *in = reinterpret_cast<const uchar *>(bytes.constData() + offset);
        const auto at = qFromLittleEndian<qint64>(in); const int latency = qFromLittleEndian<qint32>(in + 8);
        if (qFromLittleEndian<quint32>(in + 12) != checksum(at, latency) || latency < -1 || latency > 15000) { continue; }
        if (at >= now - LocationHealthHistory::retentionMs && at <= now + 60000) { points.append({at, latency}); }
    }
    return points;
}
bool owned(const QFileInfo &file) {
    const auto name = file.fileName();
    if (file.isSymLink() || !file.isFile() || name.size() != 68 || !name.endsWith(".hph")) { return false; }
    return std::all_of(name.cbegin(), name.cbegin() + 64, [](QChar ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); });
}
}
LocationHistoryStore::LocationHistoryStore(QString directory, QObject *parent) : QObject(parent), m_directory(std::move(directory))
{
    m_flushTimer.setInterval(60000);
    connect(&m_flushTimer, &QTimer::timeout, this, &LocationHistoryStore::flush);
    connect(&m_watcher, &QFutureWatcher<Result>::finished, this, [this]() {
        const auto result = m_watcher.result();
        m_taskActive = false;
        m_error = result.error;
        {
            for (auto it = result.failed.cbegin(); it != result.failed.cend(); ++it) {
                if (m_prune && !m_retained.contains(it.key())) { continue; }
                auto points = it.value(); points.append(m_pending.value(it.key())); m_pending[it.key()] = points;
            }
        }
        m_inflight.clear();
        m_pendingCount = 0; for (const auto &points : m_pending) { m_pendingCount += points.size(); }
        m_cachedId = result.id; m_cached = result.points;
        emit changed();
        if (m_reload || m_prune || m_requestedId != m_cachedId) { start(); }
    });
    m_flushTimer.start();
}
LocationHistoryStore::~LocationHistoryStore()
{
    m_flushTimer.stop();
    disconnect(&m_watcher, nullptr, this, nullptr);
    m_watcher.waitForFinished();
    if (!m_inflight.isEmpty() && m_watcher.future().isValid()) {
        const auto failed = m_watcher.result().failed;
        for (auto it = failed.cbegin(); it != failed.cend(); ++it) { auto points = it.value(); points.append(m_pending.value(it.key())); m_pending[it.key()] = points; }
    }
    // При штатном выходе дописываем только оставшийся небольшой пакет.
    if (!m_pending.isEmpty() || m_prune) { process(m_directory, m_pending, {}, m_retained, m_prune); }
}
void LocationHistoryStore::record(const QString &id, int latency, qint64 at)
{
    if (id.isEmpty() || id.size() > 1024 || latency < -1 || latency > 15000 || at <= 0) { return; }
    m_recent.record(id, latency, at);
    // Ограничение очереди защищает память при отказе диска; ошибка видна в статистике.
    if (m_pendingCount >= 8192) { m_error = tr("Не удалось сохранить историю: очередь записи заполнена."); emit changed(); return; }
    m_pending[id].append({at, latency}); ++m_pendingCount;
    emit changed();
}
void LocationHistoryStore::retain(const QSet<QString> &ids)
{
    m_retentionKnown = true;
    m_lastCleanup = QDateTime::currentMSecsSinceEpoch();
    m_recent.retain(ids); m_retained = ids; m_prune = true;
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (!ids.contains(it.key())) { m_pendingCount -= it->size(); it = m_pending.erase(it); } else { ++it; }
    }
    if (!ids.contains(m_cachedId)) { m_cached.clear(); }
    start();
}
QVariantMap LocationHistoryStore::statistics(const QString &id, qint64 since, qint64 until)
{
    if (m_requestedId != id) { m_requestedId = id; m_reload = true; start(); }
    Points points;
    if (m_cachedId == id) { points = m_cached; points.append(m_inflight.value(id)); points.append(m_pending.value(id)); }
    else { points = m_recent.samples(id); }
    auto result = LocationHealthHistory::summarize(points, since, until);
    result["loading"] = m_cachedId != id;
    result["storageError"] = m_error;
    return result;
}
void LocationHistoryStore::flush()
{
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (m_retentionKnown && now - m_lastCleanup >= 3600000) { m_prune = true; m_lastCleanup = now; }
    start();
}
void LocationHistoryStore::start()
{
    if (m_taskActive || (m_pending.isEmpty() && !m_reload && !m_prune)) { return; }
    m_inflight = std::move(m_pending); m_pending.clear(); m_pendingCount = 0;
    const auto directory = m_directory, id = m_requestedId;
    const auto batch = m_inflight; const auto retained = m_retained; const bool prune = m_prune;
    m_reload = false; m_prune = false;
    m_taskActive = true;
    m_watcher.setFuture(QtConcurrent::run([directory, batch, id, retained, prune]() { return process(directory, batch, id, retained, prune); }));
}
LocationHistoryStore::Result LocationHistoryStore::process(const QString &directory, const QHash<QString, Points> &batch,
    const QString &id, const QSet<QString> &retained, bool prune)
{
    Result result; result.id = id;
    QDir dir(directory);
    if (directory.isEmpty() || !dir.mkpath(".")) { result.error = tr("Не удалось открыть каталог истории."); result.failed = batch; return result; }
    const auto now = QDateTime::currentMSecsSinceEpoch();
    QSet<QString> names; for (const auto &key : retained) { names.insert(fileName(key)); }
    qint64 total = 0;
    for (const auto &file : dir.entryInfoList({"*.hph"}, QDir::Files | QDir::NoSymLinks)) {
        if (!owned(file)) { continue; }
        if ((prune && !names.contains(file.fileName())) || file.lastModified().toMSecsSinceEpoch() < now - LocationHealthHistory::retentionMs) {
            if (!QFile::remove(file.absoluteFilePath())) { result.error = tr("Не удалось удалить устаревшую историю."); }
        } else { total += file.size(); }
    }
    for (auto it = batch.cbegin(); it != batch.cend(); ++it) {
        const auto path = dir.filePath(fileName(it.key()));
        QFile file(path); const QFileInfo info(path);
        const auto bytes = encode(it.value());
        bool ok = !info.isSymLink() && info.size() <= fileLimit && total + bytes.size() + magic.size() <= directoryLimit;
        if (ok && file.open(QIODevice::ReadWrite)) {
            const auto header = file.read(magic.size());
            const auto first = file.read(16);
            const auto oldest = first.size() == 16 ? qFromLittleEndian<qint64>(reinterpret_cast<const uchar *>(first.constData())) : now;
            // Суточный запас исключает перезапись недельного файла на каждом минутном замере.
            // Выдача статистики всегда отсекает всё старше семи дней.
            const bool compact = header != magic || (file.size() - magic.size()) % 16 != 0
                || oldest < now - LocationHealthHistory::retentionMs - 86400000 || file.size() + bytes.size() > fileLimit;
            const auto oldSize = file.size();
            if (compact) {
                file.seek(0); auto points = decode(file.read(fileLimit), now); file.close();
                points.append(it.value());
                const auto maximum = (fileLimit - magic.size()) / 16;
                if (points.size() > maximum) { points.remove(0, points.size() - maximum); result.error = tr("История ограничена размером файла; часть старых замеров удалена."); }
                QSaveFile replacement(path); const auto data = magic + encode(points);
                ok = replacement.open(QIODevice::WriteOnly) && replacement.write(data) == data.size() && replacement.commit();
                if (ok) { total += data.size() - oldSize; }
            } else {
                file.seek(file.size()); const auto offset = file.pos();
                ok = file.write(bytes) == bytes.size() && file.flush();
                if (!ok) { file.resize(offset); } else { total += bytes.size(); }
            }
        } else { ok = false; }
        if (!ok) { result.error = tr("Не удалось сохранить историю (диск недоступен или превышен лимит 256 МиБ)."); result.failed.insert(it.key(), it.value()); }
        // При частичном отказе не повторяем успешно записанные пакеты.
    }
    if (!id.isEmpty()) {
        QFile file(dir.filePath(fileName(id)));
        const QFileInfo info(file);
        if (info.exists()) {
            if (!info.isSymLink() && info.size() <= fileLimit && file.open(QIODevice::ReadOnly)) {
                result.points = decode(file.read(fileLimit), now);
            } else {
                result.error = tr("Не удалось прочитать историю локации.");
            }
        }
    }
    return result;
}
}

#pragma once
#include <QHash>
#include <QList>
#include <QSet>
#include <QVariantMap>
#include <limits>
#include <climits>

namespace hop {
class LocationHealthHistory {
public:
    static constexpr int perLocationLimit = 240;
    static constexpr int totalLimit = 8192;
    static constexpr qint64 retentionMs = 7 * qint64(24 * 60 * 60 * 1000);
    struct Sample { qint64 at; int latency; };
    QList<Sample> samples(const QString &id) const { return m_samples.value(id); }
    void record(const QString &id, int latency, qint64 at) {
        if (id.isEmpty() || id.size() > 1024 || latency < -1 || latency > 15000 || at <= 0) { return; }
        for (auto it = m_samples.begin(); it != m_samples.end();) {
            while (!it->isEmpty() && it->first().at < at - retentionMs) { it->removeFirst(); --m_count; }
            if (it->isEmpty()) { it = m_samples.erase(it); } else { ++it; }
        }
        auto &points = m_samples[id];
        if (points.size() == perLocationLimit) { points.removeFirst(); --m_count; }
        points.append({at, latency}); ++m_count;
        while (m_count > totalLimit) {
            auto oldest = m_samples.end();
            for (auto it = m_samples.begin(); it != m_samples.end(); ++it) {
                if (oldest == m_samples.end() || it->first().at < oldest->first().at) { oldest = it; }
            }
            oldest->removeFirst(); --m_count;
            if (oldest->isEmpty()) { m_samples.erase(oldest); }
        }
    }
    void retain(const QSet<QString> &ids) {
        for (auto it = m_samples.begin(); it != m_samples.end();) {
            if (!ids.contains(it.key())) { m_count -= it->size(); it = m_samples.erase(it); } else { ++it; }
        }
    }
    QVariantMap statistics(const QString &id, qint64 since = 0, qint64 until = 0) const {
        return summarize(m_samples.value(id), since, until);
    }
    static QVariantMap summarize(const QList<Sample> &points, qint64 since = 0, qint64 until = 0) {
        QVariantList samples; int count = 0, successful = 0, failures = 0, minimum = (std::numeric_limits<int>::max)(), maximum = 0;
        qint64 sum = 0;
        qint64 first = (std::numeric_limits<qint64>::max)(), last = 0;
        for (const auto &point : points) {
            if (point.at < since || (until > 0 && point.at > until)) { continue; }
            ++count; first = qMin(first, point.at); last = qMax(last, point.at);
            if (point.latency < 0) { ++failures; }
            else { ++successful; sum += point.latency; minimum = qMin(minimum, point.latency); maximum = qMax(maximum, point.latency); }
        }
        const auto start = since > 0 ? since : count ? first : 0;
        const auto end = qMax(start + 1, until > 0 ? until : last);
        const bool aggregated = count > perLocationLimit;
        qint64 interval = 0;
        if (aggregated) {
            const auto required = (end - start + perLocationLimit - 1) / perLocationLimit;
            for (qint64 step : {1LL, 1000LL, 5000LL, 10000LL, 15000LL, 30000LL, 60000LL, 120000LL,
                               300000LL, 600000LL, 900000LL, 1800000LL, 3600000LL, 7200000LL}) {
                interval = step; if (step >= required) { break; }
            }
            interval = qMax(interval, required);
        }
        struct Bucket { int count = 0, successes = 0, failures = 0, minimum = INT_MAX, maximum = -1; qint64 sum = 0; };
        const int bucketCount = aggregated ? int((end - start + interval - 1) / interval) : 0;
        QList<Bucket> buckets(bucketCount);
        for (const auto &point : points) {
            if (point.at < since || (until > 0 && point.at > until)) { continue; }
            if (!aggregated) {
                samples.append(QVariantMap{{"at", point.at}, {"from", point.at}, {"to", point.at},
                    {"latencyMs", point.latency}, {"ok", point.latency >= 0}, {"count", 1},
                    {"failures", point.latency < 0 ? 1 : 0}, {"minimum", point.latency}, {"maximum", point.latency}});
                continue;
            }
            auto &bucket = buckets[qBound(0, int((point.at - start) / interval), bucketCount - 1)];
            ++bucket.count;
            if (point.latency < 0) { ++bucket.failures; }
            else { ++bucket.successes; bucket.sum += point.latency; bucket.minimum = qMin(bucket.minimum, point.latency); bucket.maximum = qMax(bucket.maximum, point.latency); }
        }
        // Смешанный интервал сохраняет успешные ответы и число сбоев независимо друг от друга.
        for (int index = 0; index < buckets.size(); ++index) {
            const auto &bucket = buckets[index]; if (!bucket.count) { continue; }
            const auto from = start + index * interval, to = qMin(end, from + interval);
            samples.append(QVariantMap{{"at", from + (to - from) / 2}, {"from", from}, {"to", to}, {"bucket", index},
                {"latencyMs", bucket.successes ? double(bucket.sum) / bucket.successes : -1.0}, {"ok", bucket.successes > 0},
                {"count", bucket.count}, {"failures", bucket.failures}, {"minimum", bucket.successes ? bucket.minimum : -1}, {"maximum", bucket.maximum}});
        }
        return { {"samples", samples}, {"count", count}, {"failures", failures},
            {"minimum", successful ? minimum : -1}, {"maximum", successful ? maximum : -1},
            {"average", successful ? double(sum) / successful : -1.0}, {"windowStart", start}, {"windowEnd", end},
            {"aggregated", aggregated}, {"intervalMs", interval} };
    }
    int count() const { return m_count; }
private:
    QHash<QString, QList<Sample>> m_samples;
    int m_count = 0;
};
}

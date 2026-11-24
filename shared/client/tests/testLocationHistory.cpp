#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QtEndian>
#include "core/locations/locationHistoryStore.h"

namespace {
QString pathFor(const QString &root, const QString &id) {
    return root + "/" + QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".hph";
}
QByteArray fixture(int count, qint64 start, int step = 60000) {
    QByteArray bytes("HOPHST01");
    for (int index = 0; index < count; ++index) {
        const qint64 at = start + qint64(index) * step; const int latency = index % 10 == 0 ? -1 : 100;
        uchar data[16];
        qToLittleEndian<qint64>(at, data); qToLittleEndian<qint32>(latency, data + 8);
        qToLittleEndian<quint32>(quint32(at) ^ quint32(quint64(at) >> 32) ^ quint32(latency) ^ 0x48504f31u, data + 12);
        bytes.append(reinterpret_cast<const char *>(data), 16);
    }
    return bytes;
}
}
class TestLocationHistory : public QObject {
    Q_OBJECT
private slots:
    void timeBucketsPreserveSuccessfulResponsesFailuresAndPeaks() {
        const qint64 end = 2000000000000LL, start = end - hop::LocationHealthHistory::retentionMs;
        QList<hop::LocationHealthHistory::Sample> points;
        for (int index = 0; index < 12134; ++index) {
            const auto at = start + (end - start) * index / 12133;
            points.append({at, index == 6000 ? 7282 : index % 41 == 0 ? -1 : 59 + index % 400});
        }
        for (int minutes : {15, 60, 360, 1440, 10080}) {
            const auto since = end - qint64(minutes) * 60000;
            int count = 0, failures = 0, maximum = -1; qint64 sum = 0;
            for (const auto &point : points) {
                if (point.at < since) continue;
                ++count; if (point.latency < 0) ++failures; else { sum += point.latency; maximum = qMax(maximum, point.latency); }
            }
            const auto result = hop::LocationHealthHistory::summarize(points, since, end);
            QCOMPARE(result.value("windowStart").toLongLong(), since); QCOMPARE(result.value("windowEnd").toLongLong(), end);
            QCOMPARE(result.value("count").toInt(), count); QCOMPARE(result.value("failures").toInt(), failures);
            QCOMPARE(result.value("maximum").toInt(), maximum);
            QCOMPARE(result.value("average").toDouble(), double(sum) / (count - failures));
            const auto samples = result.value("samples").toList(); QVERIFY(samples.size() <= 240);
            int represented = 0, missed = 0, representedMaximum = -1;
            for (const auto &value : samples) {
                const auto sample = value.toMap(); represented += sample.value("count").toInt(); missed += sample.value("failures").toInt();
                representedMaximum = qMax(representedMaximum, sample.value("maximum").toInt());
                QVERIFY(sample.value("from").toLongLong() >= since); QVERIFY(sample.value("to").toLongLong() <= end);
                if (sample.value("failures").toInt() < sample.value("count").toInt()) QVERIFY(sample.value("ok").toBool());
            }
            QCOMPARE(represented, count); QCOMPARE(missed, failures); QCOMPARE(representedMaximum, maximum);
        }
    }
    void timeBucketsDoNotFillUnmeasuredDaysOrLoseAllFailureIntervals() {
        const qint64 start = 2000000000000LL, end = start + hop::LocationHealthHistory::retentionMs;
        QList<hop::LocationHealthHistory::Sample> points;
        for (int index = 0; index < 300; ++index) points.append({start + index * 1000, 100});
        for (int index = 0; index < 300; ++index) points.append({end - 300000 + index * 1000, -1});
        const auto result = hop::LocationHealthHistory::summarize(points, start, end);
        const auto samples = result.value("samples").toList();
        QCOMPARE(samples.size(), 2);
        QVERIFY(samples.first().toMap().value("ok").toBool());
        QVERIFY(!samples.last().toMap().value("ok").toBool());
        QCOMPARE(samples.last().toMap().value("failures").toInt(), 300);
        QVERIFY(samples.last().toMap().value("bucket").toInt() - samples.first().toMap().value("bucket").toInt() > 100);
        QCOMPARE(result.value("count").toInt(), 600); QCOMPARE(result.value("failures").toInt(), 300);
    }
    void batchedWriteRestartAndPrune() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto now = QDateTime::currentMSecsSinceEpoch();
        {
            hop::LocationHistoryStore store(directory.path());
            store.record("location", 25, now); store.record("location", -1, now + 1);
            QVERIFY(!QFile::exists(pathFor(directory.path(), "location")));
            QCOMPARE(store.statistics("location", 0).value("count").toInt(), 2);
            QTRY_VERIFY(!store.busy());
            QTRY_VERIFY(!store.statistics("location", 0).value("loading").toBool());
            QCOMPARE(store.statistics("location", 0).value("failures").toInt(), 1);
        }
        hop::LocationHistoryStore restored(directory.path());
        restored.statistics("location", 0);
        QTRY_VERIFY(!restored.busy());
        QCOMPARE(restored.statistics("location", 0).value("count").toInt(), 2);
        restored.retain({}); QTRY_VERIFY(!restored.busy());
        QVERIFY(!QFile::exists(pathFor(directory.path(), "location")));
    }
    void shutdownFlushAndCorruptTailRecovery() {
        QTemporaryDir directory; const auto now = QDateTime::currentMSecsSinceEpoch();
        { hop::LocationHistoryStore store(directory.path()); store.record("a", 40, now); }
        QFile file(pathFor(directory.path(), "a")); QVERIFY(file.open(QIODevice::Append)); file.write("broken"); file.close();
        {
            hop::LocationHistoryStore store(directory.path());
            store.record("a", 60, now + 1); store.flush(); QTRY_VERIFY(!store.busy());
            store.statistics("a", 0); QTRY_VERIFY(!store.busy());
            const auto statistics = store.statistics("a", 0);
            QCOMPARE(statistics.value("count").toInt(), 2); QCOMPARE(statistics.value("average").toDouble(), 50.0);
        }
        QCOMPARE(QFileInfo(file).size(), 8 + 2 * 16);
    }
    void samplingDuringLoadAndLocationChangeDoesNotDuplicatePoints() {
        QTemporaryDir directory; const auto now = QDateTime::currentMSecsSinceEpoch();
        hop::LocationHistoryStore store(directory.path());
        store.record("a", 10, now); store.statistics("a", 0);
        store.record("a", 20, now + 1); store.record("b", 30, now + 2);
        store.statistics("b", 0);
        QTRY_VERIFY(!store.busy());
        QTRY_VERIFY(!store.statistics("b", 0).value("loading").toBool());
        QCOMPARE(store.statistics("b", 0).value("count").toInt(), 1);
        store.statistics("a", 0); QTRY_VERIFY(!store.busy());
        QCOMPARE(store.statistics("a", 0).value("count").toInt(), 2);
        store.flush(); QTRY_VERIFY(!store.busy());
        QCOMPARE(store.statistics("a", 0).value("count").toInt(), 2);
    }
    void sevenDaysExactStatisticsAndBoundedGraph() {
        QTemporaryDir directory; const auto now = QDateTime::currentMSecsSinceEpoch();
        const auto data = fixture(10080, now - hop::LocationHealthHistory::retentionMs + 60000);
        QElapsedTimer timer; timer.start();
        for (int index = 0; index < 115; ++index) {
            QFile file(pathFor(directory.path(), QString::number(index))); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(data), data.size());
        }
        qint64 total = 0; for (const auto &file : QDir(directory.path()).entryInfoList(QDir::Files)) { total += file.size(); }
        QCOMPARE(total, qint64(115) * (8 + 10080 * 16));
        hop::LocationHistoryStore store(directory.path());
        store.statistics("0", 0); QTRY_VERIFY(!store.busy());
        const auto statistics = store.statistics("0", 0);
        QCOMPARE(statistics.value("count").toInt(), 10080);
        QCOMPARE(statistics.value("failures").toInt(), 1008);
        QCOMPARE(statistics.value("average").toDouble(), 100.0);
        const auto graph = statistics.value("samples").toList();
        QVERIFY(graph.size() <= 240); QVERIFY(graph.size() > 100);
        int represented = 0, failures = 0;
        for (const auto &value : graph) {
            const auto bucket = value.toMap(); represented += bucket.value("count").toInt(); failures += bucket.value("failures").toInt();
            QVERIFY(bucket.value("ok").toBool());
        }
        QCOMPARE(represented, 10080); QCOMPARE(failures, 1008);
        qInfo() << "115 locations / seven days bytes:" << total << "fixture creation and query wall ms:" << timer.elapsed();
    }
    void expiredAndMalformedRecordsExcludedAndErrorsVisible() {
        QTemporaryDir directory; const auto now = QDateTime::currentMSecsSinceEpoch();
        QFile file(pathFor(directory.path(), "a")); QVERIFY(file.open(QIODevice::WriteOnly));
        auto data = fixture(3, now - hop::LocationHealthHistory::retentionMs - 60000);
        data[8 + 16 + 12] = char(data[8 + 16 + 12] ^ 1);
        file.write(data); file.close();
        hop::LocationHistoryStore store(directory.path()); store.statistics("a", 0); QTRY_VERIFY(!store.busy());
        QCOMPARE(store.statistics("a", 0).value("count").toInt(), 1);
        const auto badRoot = directory.path() + "/file";
        QFile blocked(badRoot); QVERIFY(blocked.open(QIODevice::WriteOnly)); blocked.close();
        hop::LocationHistoryStore failed(badRoot); failed.record("a", 10, now); failed.flush(); QTRY_VERIFY(!failed.busy());
        QVERIFY(!failed.statistics("a", 0).value("storageError").toString().isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestLocationHistory)
#include "testLocationHistory.moc"

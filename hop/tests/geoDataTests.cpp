#include "core/routing/geoData.h"
#include "core/routing/routeDatasetsController.h"
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QByteArray varint(quint64 value)
{
    QByteArray bytes;
    do {
        const auto byte = static_cast<char>((value & 127) | (value >= 128 ? 128 : 0));
        bytes.append(byte);
        value >>= 7;
    } while (value);
    return bytes;
}
QByteArray binary(int field, const QByteArray &value) { return varint((field << 3) | 2) + varint(value.size()) + value; }
QByteArray integer(int field, quint64 value) { return varint(field << 3) + varint(value); }
QByteArray category(const QByteArray &code, const QByteArray &entry)
{
    return binary(1, binary(1, code) + binary(2, entry));
}
QByteArray cidr(const QByteArray &ip, int prefix) { return binary(1, ip) + integer(2, prefix); }
QByteArray domain(int type, const QByteArray &text) { return integer(1, type) + binary(2, text); }
}

class GeoDataTests : public QObject {
    Q_OBJECT
private slots:
    void ipv4AndIpv6()
    {
        const auto bytes = binary(1, binary(1, "ru") + binary(2, cidr(QByteArray::fromHex("c0000200"), 24))
                                  + binary(2, cidr(QByteArray::fromHex("20010db8000000000000000000000000"), 32)));
        const auto data = hop::parseGeoData(bytes, hop::GeoDataKind::GeoIp);
        QVERIFY2(data.isValid(), qPrintable(data.error));
        QCOMPARE(data.categories.size(), 1);
        QCOMPARE(data.categories.first().code, QStringLiteral("RU"));
        QCOMPARE(data.categories.first().entries, 2);
        QString error;
        const auto rule = hop::geoCategoryXrayRule(data, "ru", "direct", &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(rule.value("ip").toArray(), (QJsonArray { "192.0.2.0/24", "2001:db8::/32" }));
        QCOMPARE(rule.value("outboundTag").toString(), QStringLiteral("direct"));
    }
    void domainTypes()
    {
        QByteArray records = binary(1, "category-ru");
        for (int type = 0; type < 4; ++type) { records += binary(2, domain(type, "example.org")); }
        const auto data = hop::parseGeoData(binary(1, records), hop::GeoDataKind::GeoSite);
        QVERIFY(data.isValid());
        const auto rule = hop::geoCategoryXrayRule(data, "CATEGORY-RU", "vpn", nullptr);
        QCOMPARE(rule.value("domain").toArray(), (QJsonArray { "keyword:example.org", "regexp:example.org",
                                                            "domain:example.org", "full:example.org" }));
    }
    void malformed_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<bool>("geoip");
        QTest::newRow("empty") << QByteArray() << true;
        QTest::newRow("truncated-varint") << QByteArray::fromHex("80") << true;
        QTest::newRow("overflow-varint") << QByteArray::fromHex("ffffffffffffffffff02") << true;
        QTest::newRow("zero-field") << QByteArray::fromHex("0000") << true;
        QTest::newRow("group") << QByteArray::fromHex("0b0c") << true;
        QTest::newRow("bad-length") << QByteArray::fromHex("0affffffffffffffffff01") << true;
        QTest::newRow("ip-length") << category("RU", cidr(QByteArray(5, '\0'), 24)) << true;
        QTest::newRow("ipv4-prefix") << category("RU", cidr(QByteArray(4, '\0'), 33)) << true;
        QTest::newRow("ipv6-prefix") << category("RU", cidr(QByteArray(16, '\0'), 129)) << true;
        QTest::newRow("no-ip") << category("RU", integer(2, 24)) << true;
        QTest::newRow("duplicate-field") << category("RU", cidr(QByteArray(4, '\0'), 24) + integer(2, 16)) << true;
        QTest::newRow("duplicate-category") << category("RU", cidr(QByteArray(4, '\0'), 0)) + category("ru", cidr(QByteArray(4, '\0'), 0)) << true;
        QTest::newRow("bad-code") << category("../RU", cidr(QByteArray(4, '\0'), 24)) << true;
        QTest::newRow("wrong-kind") << category("RU", domain(2, "example.org")) << true;
        QTest::newRow("wrong-kind-site") << category("RU", cidr(QByteArray(4, '\0'), 24)) << false;
        QTest::newRow("unknown-domain-type") << category("RU", domain(4, "example.org")) << false;
        QTest::newRow("bad-utf8") << category("RU", domain(2, QByteArray::fromHex("c328"))) << false;
        QTest::newRow("empty-domain") << category("RU", domain(2, "")) << false;
        QTest::newRow("large-domain") << category("RU", domain(2, QByteArray(4097, 'a'))) << false;
        QTest::newRow("nul-domain") << category("RU", domain(2, QByteArray("a\0b", 3))) << false;
    }
    void malformed()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(bool, geoip);
        const auto data = hop::parseGeoData(bytes, geoip ? hop::GeoDataKind::GeoIp : hop::GeoDataKind::GeoSite);
        QVERIFY(!data.isValid());
        QVERIFY(!data.error.isEmpty());
        QVERIFY(data.categories.isEmpty());
    }
    void reverseMatchAndEmptyDoNotBroaden()
    {
        const auto reversed = hop::parseGeoData(binary(1, binary(1, "RU") + binary(2, cidr(QByteArray(4, '\0'), 0))
                                                          + integer(3, 1)), hop::GeoDataKind::GeoIp);
        QVERIFY(reversed.isValid());
        QVERIFY(reversed.categories.first().reverseMatch);
        QString error;
        QVERIFY(hop::geoCategoryXrayRule(reversed, "RU", "direct", &error).isEmpty());
        QVERIFY(!error.isEmpty());
        const auto empty = hop::parseGeoData(binary(1, binary(1, "EMPTY")), hop::GeoDataKind::GeoSite);
        QVERIFY(empty.isValid());
        QVERIFY(hop::geoCategoryXrayRule(empty, "EMPTY", "direct", &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(hop::geoCategoryXrayRule(reversed, "MISSING", "direct", &error).isEmpty());
    }
    void defaultsUnknownFieldsAndFile()
    {
        const auto raw = category("DEFAULT", binary(1, QByteArray(4, '\0')) + binary(99, "future"))
            + integer(50, 1234) + QByteArray::fromHex("99030000000000000000");
        QTemporaryFile file;
        QVERIFY(file.open());
        QCOMPARE(file.write(raw), raw.size());
        file.close();
        const auto data = hop::readGeoDataFile(file.fileName(), hop::GeoDataKind::GeoIp);
        QVERIFY2(data.isValid(), qPrintable(data.error));
        QCOMPARE(hop::geoCategoryXrayRule(data, "DEFAULT", "vpn", nullptr).value("ip").toArray(), QJsonArray { "0.0.0.0/0" });
        QVERIFY(!hop::readGeoDataFile(file.fileName() + ".missing", hop::GeoDataKind::GeoIp).isValid());
    }
    void cachedImportIsAtomicAndFailurePreservesData()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto raw = category("TEST", cidr(QByteArray::fromHex("c0000200"), 24));
        QTemporaryFile input;
        QVERIFY(input.open());
        QCOMPARE(input.write(raw), raw.size());
        input.close();
        const auto fileUrl = QUrl::fromLocalFile(input.fileName());
        hop::RouteDatasetsController controller(nullptr, directory.path());
        QVERIFY(controller.importFile(fileUrl, "geoip"));
        QVERIFY(controller.isBusy());
        QVERIFY(!controller.importFile(fileUrl, "geoip"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 5000);
        QVERIFY2(controller.lastError().isEmpty(), qPrintable(controller.lastError()));
        QCOMPARE(controller.categories().size(), 1);
        QCOMPARE(controller.sha256(), QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex()));
        QFile cache(QDir(directory.path()).filePath("geoip.dat"));
        QVERIFY(cache.open(QIODevice::ReadOnly));
        QCOMPARE(cache.readAll(), raw);
        cache.close();
        QVERIFY(input.open());
        input.resize(0);
        input.write("broken");
        input.close();
        QVERIFY(controller.importFile(fileUrl, "geoip"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 5000);
        QVERIFY(!controller.lastError().isEmpty());
        QCOMPARE(controller.data().bytes, raw);
        QVERIFY(cache.open(QIODevice::ReadOnly));
        QCOMPARE(cache.readAll(), raw);
        cache.close();
        hop::RouteDatasetsController restored(nullptr, directory.path());
        QVERIFY(restored.loadCached("geoip"));
        QTRY_VERIFY_WITH_TIMEOUT(!restored.isBusy(), 5000);
        QCOMPARE(restored.data().bytes, raw);
        QVERIFY(!restored.download(QUrl("http://example.org/test.dat"), "geoip"));
        QVERIFY(!restored.isBusy());
        QCOMPARE(restored.data().bytes, raw);
    }
    void persistenceFailureDoesNotReplaceValidatedData()
    {
        QTemporaryDir directory;
        QTemporaryFile input;
        QVERIFY(input.open());
        input.write(category("TEST", cidr(QByteArray(4, '\0'), 0)));
        input.close();
        const auto blocked = QDir(directory.path()).filePath("file-instead-of-directory");
        QFile blocker(blocked);
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        hop::RouteDatasetsController controller(nullptr, blocked);
        QVERIFY(controller.importFile(QUrl::fromLocalFile(input.fileName()), "geoip"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 5000);
        QVERIFY(!controller.lastError().isEmpty());
        QVERIFY(controller.categories().isEmpty());
    }
};
QTEST_GUILESS_MAIN(GeoDataTests)
#include "geoDataTests.moc"

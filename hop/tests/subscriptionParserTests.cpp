#include "core/subscriptions/subscriptionParser.h"

#include <QTest>

class SubscriptionParserTests : public QObject {
    Q_OBJECT
private slots:
    void acceptsPlainAndBase64();
    void rejectsInvalidResponseWithoutPartialResults();
    void rejectsInvalidUtf8AndOversizedResponse();
    void retainsTransportParameters();
    void preservesIdentityOnRenameAndKeyRotation();
    void rejectsAmbiguousUpdate();
};

namespace {
QByteArray example(const QByteArray &name = "Germany", const QByteArray &user = "00000000-0000-4000-8000-000000000001")
{
    return "vless://" + user + "@vpn.example:443?security=reality&type=tcp&sni=example.com&pbk=example-key&sid=00#" + name;
}
}

void SubscriptionParserTests::acceptsPlainAndBase64()
{
    const QByteArray body = example() + "\r\n" + example("Netherlands") + "\n";
    for (const auto &variant : { body, body.toBase64(), body.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals) }) {
        const auto parsed = hop::parseSubscription(variant);
        QVERIFY2(parsed.isValid(), qPrintable(parsed.error));
        QCOMPARE(parsed.entries.size(), 2);
    }
    QCOMPARE(hop::parseSubscription(example() + "\n" + example()).entries.size(), 1);
}

void SubscriptionParserTests::rejectsInvalidResponseWithoutPartialResults()
{
    for (const auto &body : { QByteArray("<html>Unauthorized</html>"), QByteArray(),
                            example() + "\nnot-a-profile", QByteArray("vless://user@:443#invalid") }) {
        const auto parsed = hop::parseSubscription(body);
        QVERIFY(!parsed.isValid());
        QVERIFY(parsed.entries.isEmpty());
        QVERIFY(!parsed.error.contains("00000000"));
        QVERIFY(!parsed.error.contains("vpn.example"));
    }
}

void SubscriptionParserTests::rejectsInvalidUtf8AndOversizedResponse()
{
    const auto invalidUtf8 = hop::parseSubscription(example() + char(0xff));
    QVERIFY(!invalidUtf8.isValid());
    const auto oversized = hop::parseSubscription(QByteArray(hop::maximumSubscriptionBytes + 1, 'a'));
    QVERIFY(!oversized.isValid());
}

void SubscriptionParserTests::retainsTransportParameters()
{
    const QByteArray ws = "vless://user@vpn.example:443?security=tls&type=ws&host=front.example&path=%2Ftransport%3Ftoken%3Dfake#WS";
    const auto parsed = hop::parseSubscription(ws);
    QVERIFY(parsed.isValid());
    QCOMPARE(parsed.entries.first().uri.toUtf8(), ws);
}

void SubscriptionParserTests::preservesIdentityOnRenameAndKeyRotation()
{
    const auto original = hop::parseSubscription(example()).entries.first();
    const QVector<hop::PreviousSubscriptionEntry> previous {
        { "persistent-location-id", original.connectionKey, original.nameKey }
    };
    const auto renamed = hop::parseSubscription(example("NewName")).entries;
    QCOMPARE(hop::matchSubscriptionEntries(renamed, previous).serverIds.first(), "persistent-location-id");
    const auto rotated = hop::parseSubscription(example("Germany", "00000000-0000-4000-8000-000000000002")).entries;
    QCOMPARE(hop::matchSubscriptionEntries(rotated, previous).serverIds.first(), "persistent-location-id");
}

void SubscriptionParserTests::rejectsAmbiguousUpdate()
{
    const auto oldEntry = hop::parseSubscription(example()).entries.first();
    const QVector<hop::PreviousSubscriptionEntry> previous {
        { "first-id", oldEntry.connectionKey, oldEntry.nameKey },
        { "second-id", oldEntry.connectionKey, oldEntry.nameKey }
    };
    const auto matched = hop::matchSubscriptionEntries({ oldEntry }, previous);
    QVERIFY(!matched.error.isEmpty());
    QVERIFY(matched.serverIds.isEmpty());
}

QTEST_GUILESS_MAIN(SubscriptionParserTests)
#include "subscriptionParserTests.moc"

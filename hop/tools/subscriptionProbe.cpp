#include "core/subscriptions/subscriptionParser.h"
#include "core/subscriptions/subscriptionFetcher.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) { return 1; }
    QTextStream output(stdout);
    if (app.arguments().contains(QStringLiteral("--fetch"))) {
        hop::SubscriptionFetcher fetcher;
        QObject::connect(&fetcher, &hop::SubscriptionFetcher::loaded, &app,
            [&](const QByteArray &body, const QByteArray &, const QByteArray &, bool) {
                const auto parsed = hop::parseSubscription(body);
                output << "Valid entries: " << parsed.entries.size() << Qt::endl;
                app.exit(parsed.isValid() ? 0 : 1);
            });
        QObject::connect(&fetcher, &hop::SubscriptionFetcher::failed, &app,
            [&](const QString &, int) { output << "Fetch failed\n"; app.exit(1); });
        if (!fetcher.fetch(QUrl(QString::fromUtf8(input.read(16385)).trimmed(), QUrl::StrictMode))) {
            output << "Fetch rejected\n";
            return 1;
        }
        return app.exec();
    }
    const auto result = hop::parseSubscription(input.read(hop::maximumSubscriptionBytes + 1));
    if (!result.isValid()) {
        output << "Subscription rejected\n";
        return 1;
    }
    output << "Valid entries: " << result.entries.size() << '\n';
    // Проба выводит только число записей, без ключей, URL и названий.
    return 0;
}

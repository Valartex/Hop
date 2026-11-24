#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>

// Только тест очереди: не создаёт сокеты и не проверяет VPN.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) { return 2; }
    const auto config = QJsonDocument::fromJson(input.readLine(65538)).object();
    const auto awg = config.value(QStringLiteral("config")).toString();
    const bool ok = config.value(QStringLiteral("probeTestPass")).toBool() || awg.contains(QStringLiteral("# HopProbePass"));
    const int delay = awg.contains(QStringLiteral("# HopProbeSlow")) ? 1500 : config.value(QStringLiteral("probeTestDelay")).toInt(20);
    QTimer::singleShot(delay, &app, [&]() {
        QTextStream(stdout) << (ok ? "{\"ok\":true,\"latencyMs\":42}" : "{\"ok\":false,\"latencyMs\":-1}") << Qt::endl;
        app.exit(ok ? 0 : 3);
    });
    return app.exec();
}

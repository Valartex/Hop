#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#ifdef Q_OS_WIN
#include <io.h>
#include <fcntl.h>
#endif
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().mid(1) != QStringList { QStringLiteral("--provider-task") }) { return 2; }
    QFile input; if (!input.open(stdin, QIODevice::ReadOnly)) { return 2; }
    const auto request = QJsonDocument::fromJson(input.readLine(65536)).object();
    if (request.value("action") != QStringLiteral("routes")) { return 2; }
    const auto path = QUrl(request.value("url").toString()).path();
    auto body = path == QStringLiteral("/ip") ? QByteArray::fromHex("0a0e0a02525512080a040a4d00011018")
        : QByteArray::fromHex("0a0c0a0252551206080312026178");
    bool unchanged = path == QStringLiteral("/valid") && !request.value("etag").toString().isEmpty();
    if (unchanged) { body.clear(); }
    if (path == QStringLiteral("/bad")) { body = "not a DAT"; }
    if (path == QStringLiteral("/multi")) { body.append(QByteArray::fromHex("0a0c0a0255531206080312026279")); }
    if (path == QStringLiteral("/bad-regex")) { body = QByteArray::fromHex("0a0b0a0252551205080112015b"); }
    if (path == QStringLiteral("/other")) { body.replace("RU", "US"); }
    QJsonObject response { { "code", path == QStringLiteral("/error") ? 1 : 0 }, { "retryAfter", 120 },
        { "etag", QString::fromLatin1(QByteArrayLiteral("fixture-v1").toBase64()) }, { "unchanged", unchanged },
        { "bytes", body.size() + (path == QStringLiteral("/truncated") ? 1 : 0) } };
#ifdef Q_OS_WIN
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    QFile output; if (!output.open(stdout, QIODevice::WriteOnly)) { return 2; }
    output.write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n'); output.write(body); output.flush();
    return path == QStringLiteral("/error") ? 3 : 0;
}

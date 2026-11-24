#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QFile input; if (!input.open(stdin, QIODevice::ReadOnly)) { return 2; }
    const auto request = QJsonDocument::fromJson(input.readLine(65536)).object();
    const auto description = request.value("server").toObject().value("description").toString();
    if (request.value("action") != "catalog") { return 2; }
    if (description == "catalog-error") { QTextStream(stdout) << "{\"code\":17}" << Qt::endl; return 3; }
    const QJsonArray countries {
        QJsonObject { { "server_country_code", "" }, { "server_country_name", "Auto" } },
        QJsonObject { { "server_country_code", "nl" }, { "server_country_code_l10n", "NL" }, { "server_country_name", "Netherlands" }, { "available_protocols", QJsonArray { "awg" } } },
        QJsonObject { { "server_country_code", "de" }, { "server_country_code_l10n", "DE" }, { "server_country_name", "Germany" } },
        QJsonObject { { "server_country_code", "nl" }, { "server_country_code_l10n", "NL" }, { "server_country_name", "Netherlands" }, { "available_protocols", QJsonArray { "vless" } } }
    };
    const QJsonObject result { { "code", 0 }, { "account", QJsonObject { { "available_countries", countries },
        { "active_device_count", 7 }, { "max_device_count", 7 }, { "subscription_end_date", "2026-12-18T00:00:00Z" } } } };
    QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << Qt::endl;
    return 0;
}

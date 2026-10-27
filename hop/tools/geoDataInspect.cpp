#include "core/routing/geoData.h"
#include "core/routing/routeDatasetsController.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QTextStream>
#include <QTemporaryDir>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream output(stdout);
    const auto args = app.arguments();
    if ((args.size() != 3 && (args.size() != 4 || args.at(2) != "--download"))
        || (args.at(1) != "--geoip" && args.at(1) != "--geosite")) {
        output << "Usage: hop_geodata_inspect --geoip|--geosite path.dat | --download https://..." << Qt::endl;
        return 2;
    }
    const auto print = [&output](const hop::GeoDataSet &data) {
        output << "SHA256 " << QCryptographicHash::hash(data.bytes, QCryptographicHash::Sha256).toHex() << Qt::endl;
        for (const auto &category : data.categories) {
            output << category.code << " " << category.entries << (category.reverseMatch ? " reverse_match" : "") << Qt::endl;
        }
    };
    if (args.size() == 4) {
        QTemporaryDir cache;
        if (!cache.isValid()) { return 2; }
        hop::RouteDatasetsController controller(nullptr, cache.path());
        QObject::connect(&controller, &hop::RouteDatasetsController::changed, &app, [&]() {
            if (controller.isBusy()) { return; }
            if (!controller.lastError().isEmpty()) { output << controller.lastError() << Qt::endl; app.exit(1); return; }
            print(controller.data());
            app.exit(0);
        });
        if (!controller.download(QUrl(args.at(3)), args.at(1).mid(2))) { return 2; }
        return app.exec();
    }
    const auto data = hop::readGeoDataFile(args.at(2), args.at(1) == "--geoip" ? hop::GeoDataKind::GeoIp : hop::GeoDataKind::GeoSite);
    if (!data.isValid()) { output << data.error << Qt::endl; return 1; }
    print(data);
    return 0;
}

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace hop {
enum class GeoDataKind { GeoIp, GeoSite };
constexpr qsizetype maximumGeoDataBytes = 64 * 1024 * 1024;

struct GeoCategory {
    QString code;
    qsizetype offset = 0;
    qsizetype size = 0;
    int entries = 0;
    bool reverseMatch = false;
};

// Индекс хранит смещения в исходном буфере; строки миллионов правил не создаются заранее.
struct GeoDataSet {
    GeoDataKind kind = GeoDataKind::GeoIp;
    QByteArray bytes;
    QVector<GeoCategory> categories;
    QString error;
    bool isValid() const { return error.isEmpty() && !categories.isEmpty(); }
};

GeoDataSet parseGeoData(QByteArray bytes, GeoDataKind kind);
GeoDataSet readGeoDataFile(const QString &path, GeoDataKind kind);
QString geoSourceKind(const QString &ruleKind);
QString normalizeGeoDataSource(const QString &source);
QString geoDataSourceId(const QString &kind, const QString &source);
QJsonObject geoDataSetXrayRule(const GeoDataSet &data, const QString &outboundTag, QString *error);

// Компилятор не изменяет действующее подключение. Применение выполняется отдельным контроллером.
QJsonObject geoCategoryXrayRule(const GeoDataSet &data, const QString &code,
                              const QString &outboundTag, QString *error);
} // namespace hop

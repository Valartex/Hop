#pragma once

#include "geoData.h"
#include <QMap>
#include <QJsonArray>
#include <QJsonObject>

namespace hop {
struct CompiledTrafficPolicy {
    QJsonArray rules;
    QString defaultTarget = QStringLiteral("vpn");
    QString error;
    bool hasDomains = false;
    bool needsDirect = false;
    bool isValid() const { return error.isEmpty(); }
};

// Правила идут сверху вниз. Некорректное правило отменяет весь новый снимок.
CompiledTrafficPolicy compileTrafficPolicy(const QJsonObject &policy, const GeoDataSet *geoIp = nullptr,
                                          const GeoDataSet *geoSite = nullptr,
                                          const QMap<QString, GeoDataSet> *sources = nullptr);
// В конфигурацию не попадают пути DAT: проверенные значения встраиваются в память ядра.
QJsonObject serializeTrafficPolicy(const CompiledTrafficPolicy &policy);
CompiledTrafficPolicy deserializeTrafficPolicy(const QJsonObject &snapshot);
QJsonObject applyXrayTrafficPolicy(const QJsonObject &base, const CompiledTrafficPolicy &policy,
                                  const QString &dnsAddress, QString *error);
} // namespace hop

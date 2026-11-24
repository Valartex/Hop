#pragma once
#include <QJsonObject>
#include <QString>

namespace hop {
struct AndroidRoutingResult {
    QJsonObject configuration;
    QString error;
    bool isValid() const { return error.isEmpty() && !configuration.isEmpty(); }
};
AndroidRoutingResult prepareAndroidRouting(const QJsonObject &configuration);
}

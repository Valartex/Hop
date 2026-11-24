#pragma once
#include <QGuiApplication>
#include <QObject>
#include <QStyleHints>

namespace hop {
// Системная цветовая схема Android передаётся общим QML-компонентам.
class AndroidAppearance final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
public:
    explicit AndroidAppearance(QObject *parent = nullptr) : QObject(parent) {
        connect(qGuiApp->styleHints(), &QStyleHints::colorSchemeChanged, this, &AndroidAppearance::changed);
    }
    bool dark() const { return qGuiApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark; }
signals:
    void changed();
};
}

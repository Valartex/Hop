#pragma once
#include <QApplication>
#include <QFontDatabase>
#include <QIcon>
#include <QPalette>
#include <QQuickStyle>
#include <QStyleHints>

namespace hop {
inline QPalette desktopPalette(Qt::ColorScheme scheme)
{
    const bool dark = scheme != Qt::ColorScheme::Light;
    const auto text = QColor(dark ? "#eef2f5" : "#18262f");
    const auto background = QColor(dark ? "#101418" : "#f4f7f8");
    const auto surface = QColor(dark ? "#1a2028" : "#ffffff");
    const auto muted = QColor(dark ? "#9aabb9" : "#506775");
    QPalette colors;
    colors.setColor(QPalette::Window, background);
    colors.setColor(QPalette::WindowText, text);
    colors.setColor(QPalette::Base, QColor(dark ? "#131a21" : "#ffffff"));
    colors.setColor(QPalette::AlternateBase, surface);
    colors.setColor(QPalette::Text, text);
    colors.setColor(QPalette::Button, surface);
    colors.setColor(QPalette::ButtonText, text);
    colors.setColor(QPalette::Highlight, QColor(dark ? "#63d6cf" : "#087f7a"));
    colors.setColor(QPalette::Accent, colors.color(QPalette::Highlight));
    colors.setColor(QPalette::HighlightedText, background);
    colors.setColor(QPalette::PlaceholderText, muted);
    colors.setColor(QPalette::Light, QColor(dark ? "#26333f" : "#dbeceb"));
    colors.setColor(QPalette::Mid, QColor(dark ? "#35424e" : "#b7c6cc"));
    colors.setColor(QPalette::Dark, muted);
    colors.setColor(QPalette::ToolTipBase, surface);
    colors.setColor(QPalette::ToolTipText, text);
    colors.setColor(QPalette::Link, colors.color(QPalette::Highlight));
    for (const auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText}) {
        colors.setColor(QPalette::Disabled, role, muted);
    }
    return colors;
}

// Одинаковая палитра окон и контролов в приложении и визуальных регрессиях.
inline void applyDesktopStyle()
{
    QQuickStyle::setStyle("Fusion");
    // Китайский интерфейс не зависит от дополнительных языковых шрифтов Windows.
    static const int cjkFont = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/hop-cjk-ui.ttf"));
    if (cjkFont >= 0) {
        const auto families = QFontDatabase::applicationFontFamilies(cjkFont);
        if (!families.isEmpty()) {
            QFontDatabase::addApplicationFallbackFontFamily(QChar::Script_Han, families.first());
            QFontDatabase::addApplicationFallbackFontFamily(QChar::Script_Common, families.first());
        }
    }
    QApplication::setFont(QFont(QStringLiteral("Segoe UI"), 10));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/images/hop/hop.ico")));
    // Настройка ОС остаётся источником темы; переопределение используется только в тестах.
    static const auto appearanceConnection = QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
        qApp, [](Qt::ColorScheme scheme) { QApplication::setPalette(desktopPalette(scheme)); });
    Q_UNUSED(appearanceConnection);
    QApplication::setPalette(desktopPalette(QGuiApplication::styleHints()->colorScheme()));
}
}

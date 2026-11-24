#pragma once
#include <QApplication>
#include <QFontDatabase>
#include <QIcon>
#include <QPalette>
#include <QQuickStyle>

namespace hop {
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
    QPalette colors;
    colors.setColor(QPalette::Window, QColor("#101418"));
    colors.setColor(QPalette::WindowText, QColor("#eef2f5"));
    colors.setColor(QPalette::Base, QColor("#131a21"));
    colors.setColor(QPalette::AlternateBase, QColor("#1a2028"));
    colors.setColor(QPalette::Text, QColor("#eef2f5"));
    colors.setColor(QPalette::Button, QColor("#1a2028"));
    colors.setColor(QPalette::ButtonText, QColor("#eef2f5"));
    colors.setColor(QPalette::Highlight, QColor("#63d6cf"));
    colors.setColor(QPalette::Accent, QColor("#63d6cf"));
    colors.setColor(QPalette::HighlightedText, QColor("#101418"));
    colors.setColor(QPalette::PlaceholderText, QColor("#9aabb9"));
    colors.setColor(QPalette::Disabled, QPalette::Text, QColor("#878b91"));
    colors.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#878b91"));
    QApplication::setPalette(colors);
}
}

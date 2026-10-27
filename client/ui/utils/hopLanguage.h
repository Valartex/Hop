#pragma once

#include <QLocale>
#include <QStringList>

namespace hop {
inline QLocale supportedLanguage(const QLocale &locale)
{
    switch (locale.language()) {
    case QLocale::Russian: return QLocale(QLocale::Russian);
    case QLocale::Chinese: return QLocale(QLocale::Chinese, QLocale::SimplifiedChineseScript, QLocale::China);
    default: return QLocale(QLocale::English);
    }
}

inline QLocale systemUiLanguage(const QStringList &uiLanguages = QLocale::system().uiLanguages())
{
    // Язык интерфейса Windows может отличаться от регионального формата даты и чисел.
    return supportedLanguage(uiLanguages.isEmpty() ? QLocale::system() : QLocale(uiLanguages.first()));
}
}

#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QHash>
#include "core/utils/constants/apiKeys.h"
#include "core/utils/errorCodes.h"

namespace hop {
inline QJsonArray readApiCountries(const QJsonObject &account, QString &error)
{
    error.clear();
    const auto value = account.value(apiDefs::key::availableCountries);
    if (!value.isArray() || value.toArray().isEmpty() || value.toArray().size() > 2048) {
        error = QObject::tr("Сервис не вернул список локаций. Прежний список сохранён.");
        return {};
    }
    QJsonArray result;
    QHash<QString, int> rows;
    for (const auto &item : value.toArray()) {
        if (!item.isObject()) { error = QObject::tr("Сервис вернул некорректную локацию."); return {}; }
        auto country = item.toObject();
        const auto codeValue = country.value(apiDefs::key::serverCountryCode);
        const auto nameValue = country.value(apiDefs::key::serverCountryName);
        const auto protocols = country.value(apiDefs::key::availableProtocols);
        if (!codeValue.isString() || codeValue.toString().size() > 128
            || (!nameValue.isUndefined() && (!nameValue.isString() || nameValue.toString().size() > 256))
            || (!protocols.isUndefined() && (!protocols.isArray() || protocols.toArray().size() > 16))) {
            error = QObject::tr("Формат списка локаций не поддерживается. Прежний список сохранён."); return {};
        }
        const auto code = codeValue.toString();
        // Пустой код обозначает автовыбор сервиса, а не отдельную страну.
        if (code.isEmpty()) { continue; }
        QSet<QString> known;
        QJsonArray merged;
        if (rows.contains(code)) {
            const auto previous = result.at(rows.value(code)).toObject();
            for (const auto &protocol : previous.value(apiDefs::key::availableProtocols).toArray()) {
                known.insert(protocol.toString()); merged.append(protocol);
            }
            if (country.value(apiDefs::key::serverCountryName).toString().isEmpty()) {
                country.insert(apiDefs::key::serverCountryName, previous.value(apiDefs::key::serverCountryName));
            }
        }
        for (const auto &protocol : protocols.toArray()) {
            if (!protocol.isString() || protocol.toString().isEmpty() || protocol.toString().size() > 32) {
                error = QObject::tr("Сервис вернул некорректный протокол локации."); return {};
            }
            if (!known.contains(protocol.toString())) { known.insert(protocol.toString()); merged.append(protocol); }
        }
        country.insert(apiDefs::key::availableProtocols, merged);
        if (merged.size() > 16) { error = QObject::tr("Список протоколов локации слишком велик."); return {}; }
        if (country.value(apiDefs::key::serverCountryName).toString().isEmpty()) { country.insert(apiDefs::key::serverCountryName, code); }
        if (rows.contains(code)) { result.replace(rows.value(code), country); }
        else { rows.insert(code, result.size()); result.append(country); }
    }
    if (result.isEmpty()) { error = QObject::tr("Сервис не вернул отдельных локаций. Обновите подписку позже."); }
    return result;
}

inline QString providerFailureMessage(int code, bool captcha = false, int used = 0, int maximum = 0)
{
    if (captcha) { return QObject::tr("Сервис запрашивает CAPTCHA. Требуется подтверждение у провайдера."); }
    if (code == int(amnezia::ErrorCode::ApiConfigLimitError)) {
        const auto count = maximum > 0 ? QObject::tr(" Занято %1 из %2.").arg(used).arg(maximum) : QString();
        return QObject::tr("Исчерпан лимит конфигураций подписки (1108).") + count
            + QObject::tr(" Освободите ненужную конфигурацию у провайдера или импортируйте готовый конфигурационный файл. Hop ничего не удаляет автоматически.");
    }
    if (code <= 0) { return QObject::tr("Не удалось получить ответ сервиса. Проверьте сеть и обновите подписку."); }
    return QObject::tr("Сервис отказал в подготовке подключения, код %1.").arg(code);
}
}

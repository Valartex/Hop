#include <QCoreApplication>
#include "geoData.h"

#include <QByteArrayView>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QSet>
#include <QStringDecoder>
#include <algorithm>
#include <limits>
#include <utility>

namespace hop {
namespace {
constexpr int maximumCategories = 8192;
constexpr int maximumEntries = 2000000;

struct Field {
    quint32 number = 0;
    quint8 wire = 0;
    quint64 integer = 0;
    QByteArrayView bytes;
    qsizetype offset = 0;
};

class Reader {
public:
    explicit Reader(QByteArrayView data) : m_data(data) {}
    bool atEnd() const { return m_position == m_data.size(); }
    bool next(Field &field)
    {
        field = {};
        quint64 key = 0;
        if (!varint(key) || key > std::numeric_limits<quint32>::max() || (key >> 3) == 0) { return false; }
        field.number = static_cast<quint32>(key >> 3);
        field.wire = static_cast<quint8>(key & 7);
        if (field.wire == 0) { return varint(field.integer); }
        quint64 size = 0;
        if (field.wire == 1) { size = 8; }
        else if (field.wire == 5) { size = 4; }
        else if (field.wire == 2) { if (!varint(size)) { return false; } }
        else { return false; }
        if (size > static_cast<quint64>(m_data.size() - m_position)) { return false; }
        field.offset = m_position;
        field.bytes = m_data.sliced(m_position, static_cast<qsizetype>(size));
        m_position += static_cast<qsizetype>(size);
        return true;
    }

private:
    bool varint(quint64 &value)
    {
        value = 0;
        for (int i = 0; i < 10 && m_position < m_data.size(); ++i) {
            const auto byte = static_cast<quint8>(m_data[m_position++]);
            if (i == 9 && byte > 1) { return false; }
            value |= static_cast<quint64>(byte & 127) << (7 * i);
            if ((byte & 128) == 0) { return true; }
        }
        return false;
    }
    QByteArrayView m_data;
    qsizetype m_position = 0;
};

bool utf8(QByteArrayView bytes, QString &text, qsizetype maxSize)
{
    if (bytes.isEmpty() || bytes.size() > maxSize) { return false; }
    QStringDecoder decoder(QStringDecoder::Utf8);
    text = decoder(bytes);
    return !decoder.hasError() && !text.contains(QChar::Null);
}

bool validCode(const QString &code)
{
    return std::all_of(code.cbegin(), code.cend(), [](QChar character) {
        const auto value = character.unicode();
        return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z')
            || (value >= '0' && value <= '9') || value == '-' || value == '_';
    });
}

bool readItem(QByteArrayView bytes, GeoDataKind kind, QString *value = nullptr)
{
    Reader reader(bytes);
    Field field;
    bool seenFirst = false, seenSecond = false;
    QByteArrayView ip;
    quint64 prefix = 0, type = 0;
    QString domain;
    while (!reader.atEnd()) {
        if (!reader.next(field)) { return false; }
        if (field.number == 1) {
            if (seenFirst) { return false; }
            seenFirst = true;
            if (kind == GeoDataKind::GeoIp) {
                if (field.wire != 2 || (field.bytes.size() != 4 && field.bytes.size() != 16)) { return false; }
                ip = field.bytes;
            } else {
                if (field.wire != 0 || field.integer > 3) { return false; }
                type = field.integer;
            }
        } else if (field.number == 2) {
            if (seenSecond) { return false; }
            seenSecond = true;
            if (kind == GeoDataKind::GeoIp) {
                if (field.wire != 0 || field.integer > 128) { return false; }
                prefix = field.integer;
            } else if (field.wire != 2 || !utf8(field.bytes, domain, 4096)) { return false; }
        }
    }
    if (kind == GeoDataKind::GeoIp) {
        if (!seenFirst || prefix > static_cast<quint64>(ip.size() * 8)) { return false; }
        if (value) {
            QHostAddress address;
            if (ip.size() == 4) {
                quint32 integer = 0;
                for (auto byte : ip) { integer = (integer << 8) | static_cast<quint8>(byte); }
                address.setAddress(integer);
            } else {
                Q_IPV6ADDR ipv6;
                std::copy(ip.begin(), ip.end(), ipv6.c);
                address.setAddress(ipv6);
            }
            *value = address.toString() + QLatin1Char('/') + QString::number(prefix);
        }
        return true;
    }
    if (!seenSecond) { return false; }
    if (value) {
        static const char *const prefixes[] = { "keyword:", "regexp:", "domain:", "full:" };
        *value = QString::fromLatin1(prefixes[type]) + domain;
    }
    return true;
}

bool readCategory(QByteArrayView bytes, GeoDataKind kind, GeoCategory &category, int &totalEntries)
{
    Reader reader(bytes);
    Field field;
    bool seenCode = false, seenReverse = false;
    while (!reader.atEnd()) {
        if (!reader.next(field)) { return false; }
        if (field.number == 1) {
            if (seenCode || field.wire != 2 || !utf8(field.bytes, category.code, 128) || !validCode(category.code)) { return false; }
            seenCode = true;
            category.code = category.code.toUpper();
        } else if (field.number == 2) {
            if (field.wire != 2 || ++totalEntries > maximumEntries || !readItem(field.bytes, kind)) { return false; }
            ++category.entries;
        } else if (field.number == 3 && kind == GeoDataKind::GeoIp) {
            if (seenReverse || field.wire != 0 || field.integer > 1) { return false; }
            seenReverse = true;
            category.reverseMatch = field.integer != 0;
        }
    }
    return seenCode;
}
} // namespace

GeoDataSet parseGeoData(QByteArray bytes, GeoDataKind kind)
{
    GeoDataSet result;
    result.kind = kind;
    if (bytes.isEmpty() || bytes.size() > maximumGeoDataBytes) {
        result.error = QCoreApplication::translate("HopRoutingError", "Пустой файл или превышен предел 64 МиБ");
        return result;
    }
    Reader reader(bytes);
    Field field;
    QSet<QString> codes;
    int entries = 0;
    while (!reader.atEnd()) {
        if (!reader.next(field)) { result.error = QCoreApplication::translate("HopRoutingError", "Повреждённый protobuf в DAT"); break; }
        if (field.number != 1) { continue; }
        GeoCategory category;
        category.offset = field.offset;
        category.size = field.bytes.size();
        if (field.wire != 2 || result.categories.size() >= maximumCategories
            || !readCategory(field.bytes, kind, category, entries) || codes.contains(category.code)) {
            result.error = QCoreApplication::translate("HopRoutingError", "Некорректная категория или выбран неверный тип DAT");
            break;
        }
        codes.insert(category.code);
        result.categories.append(std::move(category));
    }
    if (result.error.isEmpty() && result.categories.isEmpty()) { result.error = QCoreApplication::translate("HopRoutingError", "В DAT нет категорий"); }
    if (!result.error.isEmpty()) { result.categories.clear(); return result; }
    result.bytes = std::move(bytes);
    return result;
}

GeoDataSet readGeoDataFile(const QString &path, GeoDataKind kind)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumGeoDataBytes) {
        GeoDataSet result;
        result.kind = kind;
        result.error = QCoreApplication::translate("HopRoutingError", "Файл недоступен или превышает 64 МиБ");
        return result;
    }
    return parseGeoData(file.read(maximumGeoDataBytes + 1), kind);
}

QJsonObject geoCategoryXrayRule(const GeoDataSet &data, const QString &code, const QString &outboundTag, QString *error)
{
    if (error) { error->clear(); }
    const auto fail = [error](const QString &message) { if (error) { *error = message; } return QJsonObject(); };
    if (!data.isValid() || outboundTag.isEmpty()) { return fail(QCoreApplication::translate("HopRoutingError", "Нет проверенного набора или выходного маршрута")); }
    const auto category = std::find_if(data.categories.cbegin(), data.categories.cend(), [&](const auto &item) {
        return item.code.compare(code, Qt::CaseInsensitive) == 0;
    });
    if (category == data.categories.cend() || category->entries == 0) { return fail(QCoreApplication::translate("HopRoutingError", "Категория отсутствует или пуста")); }
    if (category->offset < 0 || category->size < 0 || category->offset > data.bytes.size()
        || category->size > data.bytes.size() - category->offset) { return fail(QCoreApplication::translate("HopRoutingError", "Повреждён индекс DAT")); }
    // Дополнение множества нельзя заменять перечислением исходных CIDR.
    if (category->reverseMatch) { return fail(QCoreApplication::translate("HopRoutingError", "reverse_match требует отдельной политики дополнения множества")); }
    Reader reader(QByteArrayView(data.bytes).sliced(category->offset, category->size));
    Field field;
    QJsonArray values;
    while (!reader.atEnd()) {
        if (!reader.next(field)) { return fail(QCoreApplication::translate("HopRoutingError", "Повреждён индекс DAT")); }
        if (field.number == 2) {
            QString value;
            if (!readItem(field.bytes, data.kind, &value)) { return fail(QCoreApplication::translate("HopRoutingError", "Повреждено правило DAT")); }
            values.append(value);
        }
    }
    return { { QStringLiteral("type"), QStringLiteral("field") }, { QStringLiteral("outboundTag"), outboundTag },
             { data.kind == GeoDataKind::GeoIp ? QStringLiteral("ip") : QStringLiteral("domain"), values } };
}
} // namespace hop

#pragma once

#include <QAbstractListModel>
#include <QJsonArray>
#include <QVariantList>
#include <QVariantMap>
#include <QSet>
#include "core/repositories/secureServersRepository.h"
#include "secureQSettings.h"

namespace hop {
class LocationCatalog : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedProtocol READ selectedProtocol NOTIFY selectionChanged)
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY policyChanged)
    Q_PROPERTY(QVariantList priorities READ priorities NOTIFY policyChanged)
    Q_PROPERTY(QVariantMap marks READ marks NOTIFY policyChanged)
    Q_PROPERTY(QVariantList countries READ countries NOTIFY catalogChanged)
    Q_PROPERTY(int apiServiceCount READ apiServiceCount NOTIFY catalogChanged)
    Q_PROPERTY(QVariantMap services READ services NOTIFY catalogChanged)
public:
    enum Mode { Manual = 0, Automatic = 2, AllOrdered = 3, AllFastest = 4 };
    Q_ENUM(Mode)
    enum Role { Id = Qt::UserRole + 1, Name, Description, FromApi, Expired, Expiring, ServiceName, Country, ServiceId };
    struct Entry {
        QString id, serverId, serviceId, name, protocol, country, apiCountry;
        bool enabled = true;
        bool api = false;
    };
    explicit LocationCatalog(SecureServersRepository *servers, SecureQSettings *settings, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVector<Entry> entries() const { return m_entries; }
    std::optional<Entry> entry(const QString &id) const;
    QString selectedId() const { return m_selected; }
    QString selectedName() const;
    QString selectedProtocol() const;
    int mode() const { return m_mode; }
    bool automatic() const { return m_mode >= Automatic; }
    bool allLocations() const { return m_mode == AllOrdered || m_mode == AllFastest; }
    QVariantList priorities() const;
    QVariantMap marks() const;
    QVariantMap services() const { QVariantMap result; for (auto it = m_serviceNames.cbegin(); it != m_serviceNames.cend(); ++it) { result.insert(it.key(), it.value()); } return result; }
    QVariantList countries() const;
    int apiServiceCount() const;
    QStringList candidateIds() const;
    Q_INVOKABLE bool selectRow(int row);
    Q_INVOKABLE bool select(const QString &id);
    Q_INVOKABLE void setMode(int mode);
    Q_INVOKABLE bool toggleLocation(const QString &id);
    Q_INVOKABLE bool toggleMembership(const QString &id);
    Q_INVOKABLE bool toggleCountry(const QString &code);
    Q_INVOKABLE bool setExcluded(const QString &id, bool excluded);
    Q_INVOKABLE bool movePriority(int from, int to);
    Q_INVOKABLE bool removePriority(int row);
    Q_INVOKABLE bool setCountry(const QString &id, const QString &code);
    Q_INVOKABLE QVariantMap serviceInfo(const QString &locationId) const;
    static QString countryFromLabel(const QString &label);
signals:
    void selectionChanged();
    void policyChanged();
    void catalogChanged();
private:
    void rebuild();
    void save();
    bool toggle(const QString &kind, const QString &key);
    static QString iso(const QString &value);
    SecureServersRepository *m_servers;
    SecureQSettings *m_settings;
    QVector<Entry> m_entries;
    QString m_selected;
    int m_mode = Manual;
    QJsonArray m_priorities;
    QSet<QString> m_excluded;
    QHash<QString, QString> m_countries;
    QHash<QString, QString> m_apiCountries;
    QHash<QString, QString> m_serviceNames;
};
}

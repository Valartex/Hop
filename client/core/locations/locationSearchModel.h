#pragma once
#include <QSortFilterProxyModel>
#include "locationCatalog.h"

namespace hop {
class LocationSearchModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
public:
    explicit LocationSearchModel(LocationCatalog *catalog, QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) { setSourceModel(catalog); }
    QString query() const { return m_query; }
    void setQuery(const QString &value) {
        const auto query = value.trimmed().left(256);
        if (m_query == query) { return; }
        beginFilterChange(); m_query = query; endFilterChange(Direction::Rows); emit queryChanged();
    }
    Q_INVOKABLE int sourceRow(int row) const { return mapToSource(index(row, 0)).row(); }
signals:
    void queryChanged();
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override {
        if (m_query.isEmpty()) { return true; }
        const auto source = sourceModel()->index(row, 0, parent);
        for (int role : { LocationCatalog::Name, LocationCatalog::Description, LocationCatalog::ServiceName, LocationCatalog::Country }) {
            if (source.data(role).toString().contains(m_query, Qt::CaseInsensitive)) { return true; }
        }
        return false;
    }
private:
    QString m_query;
};
}

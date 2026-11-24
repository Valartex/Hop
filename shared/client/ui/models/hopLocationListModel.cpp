#include "hopLocationListModel.h"
#include <QCoreApplication>
#include <QQmlEngine>
#include <QTimer>

namespace hop {
namespace {
void registerLocationListModel() { qmlRegisterType<LocationListModel>("Hop.Models", 1, 0, "HopLocationListModel"); }
Q_COREAPP_STARTUP_FUNCTION(registerLocationListModel)
}

QHash<int, QByteArray> LocationListModel::roleNames() const
{
    auto roles = m_sourceRoles;
    for (auto it = roles.begin(); it != roles.end();) {
        if (it.value() == "serviceId") { it = roles.erase(it); } else { ++it; }
    }
    roles.insert(SourceIndex, "sourceIndex"); roles.insert(ServiceId, "serviceId");
    roles.insert(ServiceHeader, "serviceHeader"); roles.insert(RowExtent, "rowExtent");
    return roles;
}
QVariant LocationListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size() || index.row() < 0) { return {}; }
    const auto &row = m_rows[index.row()];
    switch (role) {
    case SourceIndex: return row.source;
    case ServiceId: return row.service;
    case ServiceHeader: return row.header;
    case RowExtent: return row.height;
    }
    if (!row.header && m_source && row.source >= 0 && row.source < m_source->rowCount()) {
        return m_source->data(m_source->index(row.source, 0), role);
    }
    const auto name = m_sourceRoles.value(role);
    return name.startsWith("is") ? QVariant(false) : QVariant(QString());
}
void LocationListModel::setSourceModel(QAbstractItemModel *source)
{
    if (source == m_source) { return; }
    for (const auto &connection : m_connections) { disconnect(connection); }
    m_connections.clear(); m_source = source;
    if (source) {
        m_connections << connect(source, &QAbstractItemModel::modelReset, this, &LocationListModel::scheduleRebuild)
                      << connect(source, &QAbstractItemModel::rowsInserted, this, &LocationListModel::scheduleRebuild)
                      << connect(source, &QAbstractItemModel::rowsRemoved, this, &LocationListModel::scheduleRebuild)
                      << connect(source, &QAbstractItemModel::rowsMoved, this, &LocationListModel::scheduleRebuild)
                      << connect(source, &QAbstractItemModel::layoutChanged, this, &LocationListModel::scheduleRebuild)
                      << connect(source, &QAbstractItemModel::dataChanged, this, &LocationListModel::scheduleRebuild)
                      << connect(source, &QObject::destroyed, this, [this]() {
            m_source = nullptr; rebuild(true); emit sourceModelChanged();
        });
    }
    rebuild(true); emit sourceModelChanged();
}
void LocationListModel::setExpandedServices(const QVariantMap &value) { if (m_expanded == value) return; m_expanded = value; rebuild(false); emit optionsChanged(); }
void LocationListModel::setMarks(const QVariantMap &value) { if (m_marks == value) return; m_marks = value; rebuild(false); emit optionsChanged(); }
void LocationListModel::setGrouping(bool value) { if (m_grouping == value) return; m_grouping = value; rebuild(false); emit optionsChanged(); }
void LocationListModel::setSearching(bool value) { if (m_searching == value) return; m_searching = value; rebuild(false); emit optionsChanged(); }
void LocationListModel::setCollapsedByDefault(bool value) { if (m_collapsedByDefault == value) return; m_collapsedByDefault = value; rebuild(false); emit optionsChanged(); }
void LocationListModel::scheduleRebuild()
{
    if (m_scheduled) return;
    m_scheduled = true;
    QTimer::singleShot(0, this, [this]() { m_scheduled = false; rebuild(true); });
}
void LocationListModel::rebuild(bool reset)
{
    const auto roles = m_source ? m_source->roleNames() : QHash<int, QByteArray>();
    QList<Row> rows;
    qreal extent = 0;
    QString previous;
    if (m_source) {
        const auto serviceRole = roles.key("serviceId", -1), idRole = roles.key("serverId", -1);
        for (int source = 0; source < m_source->rowCount(); ++source) {
            const auto index = m_source->index(source, 0);
            const auto service = m_grouping ? index.data(serviceRole).toString() : QString();
            if (m_grouping && (source == 0 || service != previous)) {
                rows.append({source, service, true, 64}); extent += 64;
            }
            previous = service;
            const bool expanded = !m_grouping || service.isEmpty() || m_searching || m_expanded.value(service, !m_collapsedByDefault).toBool();
            if (!expanded) continue;
            const auto marks = m_marks.value(index.data(idRole).toString()).toMap();
            const qreal height = m_grouping && marks.value("automaticMember").toBool() && !marks.value("excluded").toBool() ? 88 : 72;
            rows.append({source, service, false, height}); extent += height;
        }
    }
    if (reset || roles != m_sourceRoles) {
        beginResetModel(); m_sourceRoles = roles; m_rows = rows; endResetModel();
    } else {
        // Меняем только середину списка: заголовок сворачиваемой группы остаётся на месте.
        int prefix = 0, suffix = 0;
        while (prefix < m_rows.size() && prefix < rows.size() && m_rows[prefix].sameItem(rows[prefix])) ++prefix;
        while (suffix < m_rows.size() - prefix && suffix < rows.size() - prefix
               && m_rows[m_rows.size() - 1 - suffix].sameItem(rows[rows.size() - 1 - suffix])) ++suffix;
        const int removed = m_rows.size() - prefix - suffix, added = rows.size() - prefix - suffix;
        if (removed) {
            beginRemoveRows({}, prefix, prefix + removed - 1); m_rows.remove(prefix, removed); endRemoveRows();
        }
        if (added) {
            beginInsertRows({}, prefix, prefix + added - 1);
            for (int offset = 0; offset < added; ++offset) m_rows.insert(prefix + offset, rows[prefix + offset]);
            endInsertRows();
        }
        m_rows = rows;
        if (!m_rows.isEmpty()) emit dataChanged(index(0), index(m_rows.size() - 1));
    }
    if (m_contentHeight != extent) { m_contentHeight = extent; emit contentHeightChanged(); }
}
}

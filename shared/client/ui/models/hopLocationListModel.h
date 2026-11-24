#pragma once
#include <QAbstractListModel>
#include <QPointer>
#include <QVariantMap>

namespace hop {
class LocationListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *sourceModel READ sourceModel WRITE setSourceModel NOTIFY sourceModelChanged)
    Q_PROPERTY(QVariantMap expandedServices READ expandedServices WRITE setExpandedServices NOTIFY optionsChanged)
    Q_PROPERTY(QVariantMap marks READ marks WRITE setMarks NOTIFY optionsChanged)
    Q_PROPERTY(bool grouping READ grouping WRITE setGrouping NOTIFY optionsChanged)
    Q_PROPERTY(bool searching READ searching WRITE setSearching NOTIFY optionsChanged)
    Q_PROPERTY(bool collapsedByDefault READ collapsedByDefault WRITE setCollapsedByDefault NOTIFY optionsChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentHeightChanged)
public:
    explicit LocationListModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_rows.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QAbstractItemModel *sourceModel() const { return m_source; }
    void setSourceModel(QAbstractItemModel *source);
    QVariantMap expandedServices() const { return m_expanded; }
    void setExpandedServices(const QVariantMap &value);
    QVariantMap marks() const { return m_marks; }
    void setMarks(const QVariantMap &value);
    bool grouping() const { return m_grouping; }
    void setGrouping(bool value);
    bool searching() const { return m_searching; }
    void setSearching(bool value);
    bool collapsedByDefault() const { return m_collapsedByDefault; }
    void setCollapsedByDefault(bool value);
    qreal contentHeight() const { return m_contentHeight; }
signals:
    void sourceModelChanged();
    void optionsChanged();
    void contentHeightChanged();
private:
    enum Role { SourceIndex = Qt::UserRole + 4096, ServiceId, ServiceHeader, RowExtent };
    struct Row {
        int source = -1;
        QString service;
        bool header = false;
        qreal height = 0;
        bool sameItem(const Row &other) const { return source == other.source && service == other.service && header == other.header; }
    };
    void scheduleRebuild();
    void rebuild(bool reset);
    QPointer<QAbstractItemModel> m_source;
    QList<QMetaObject::Connection> m_connections;
    QHash<int, QByteArray> m_sourceRoles;
    QList<Row> m_rows;
    QVariantMap m_expanded, m_marks;
    bool m_grouping = false, m_searching = false, m_collapsedByDefault = false, m_scheduled = false;
    qreal m_contentHeight = 0;
};
}

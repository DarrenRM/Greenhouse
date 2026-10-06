#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QIcon>
#include <QList>
#include <QString>

#include "WindowInfo.h"

class WindowListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum RowType {
        SectionHeaderRow,
        WindowRow
    };

    enum WindowRoles {
        RowTypeRole = Qt::UserRole + 1,
        TitleRole,
        SubtitleRole,
        IconRole,
        IconKeyRole,
        IsActiveRole,
        IsSavedRole,
        StableIdRole,
        SectionCountRole
    };

    explicit WindowListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QHash<int, QByteArray> roleNames() const override;

public slots:
    void updateWindows(const QList<WindowInfo>& windows);

private:
    struct Row {
        RowType type = WindowRow;
        QString key;
        QString sectionTitle;
        int sectionCount = 0;
        WindowInfo window;
    };

    QList<Row> buildRows(const QList<WindowInfo>& windows) const;
    bool hasSameStructure(const QList<Row>& rows) const;
    QString subtitleForWindow(const WindowInfo& window) const;

    QList<Row> m_rows;
    QHash<QString, int> m_monitorOrdinals;
};

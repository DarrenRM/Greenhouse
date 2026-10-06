#pragma once

#include <QPersistentModelIndex>
#include <QStyledItemDelegate>

class WindowListDelegate final : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit WindowListDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
    bool editorEvent(QEvent* event,
                     QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;

signals:
    void removeRequested(const QString& entryId, const QString& title);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setHoveredIndex(const QModelIndex& index);

    QPersistentModelIndex m_hoveredIndex;
};

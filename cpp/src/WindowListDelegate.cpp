#include "../include/WindowListDelegate.h"

#include "../include/WindowListModel.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QCursor>
#include <QDebug>
#include <QEvent>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QToolTip>

namespace {
constexpr int kHeaderHeight = 38;
constexpr int kWindowRowHeight = 54;
constexpr int kHorizontalPadding = 12;
constexpr int kIconSize = 32;
constexpr int kIconTextGap = 10;
constexpr int kRemoveButtonSize = 30;

QRect removeButtonRect(const QRect& rowRect)
{
    return QRect(rowRect.right() - kHorizontalPadding - kRemoveButtonSize + 1,
                 rowRect.top() + (rowRect.height() - kRemoveButtonSize) / 2,
                 kRemoveButtonSize,
                 kRemoveButtonSize);
}

void drawRemoveIcon(QPainter* painter, const QRect& buttonRect, bool buttonHovered)
{
    if (buttonHovered) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(QStringLiteral("#3b2427")));
        painter->drawRoundedRect(buttonRect, 4, 4);
    }

    const QColor iconColor = buttonHovered
        ? QColor(QStringLiteral("#d06a6a"))
        : QColor(QStringLiteral("#a85a5a"));
    QPen pen(iconColor, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    const QRectF canRect(buttonRect.center().x() - 6.0,
                         buttonRect.center().y() - 4.0,
                         12.0,
                         11.0);
    painter->drawRoundedRect(canRect, 1.5, 1.5);
    painter->drawLine(QPointF(canRect.left() - 1.5, canRect.top() - 3.0),
                      QPointF(canRect.right() + 1.5, canRect.top() - 3.0));
    painter->drawLine(QPointF(canRect.center().x() - 3.0, canRect.top() - 5.5),
                      QPointF(canRect.center().x() + 3.0, canRect.top() - 5.5));
    painter->drawLine(QPointF(canRect.center().x() - 3.0, canRect.top() - 5.5),
                      QPointF(canRect.center().x() - 2.0, canRect.top() - 3.0));
    painter->drawLine(QPointF(canRect.center().x() + 3.0, canRect.top() - 5.5),
                      QPointF(canRect.center().x() + 2.0, canRect.top() - 3.0));
    painter->drawLine(QPointF(canRect.left() + 4.0, canRect.top() + 2.5),
                      QPointF(canRect.left() + 4.0, canRect.bottom() - 2.5));
    painter->drawLine(QPointF(canRect.right() - 4.0, canRect.top() + 2.5),
                      QPointF(canRect.right() - 4.0, canRect.bottom() - 2.5));
}
}

WindowListDelegate::WindowListDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
    if (auto* view = qobject_cast<QAbstractItemView*>(parent)) {
        view->viewport()->installEventFilter(this);
    }
}

void WindowListDelegate::paint(QPainter* painter,
                               const QStyleOptionViewItem& option,
                               const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    const auto rowType = static_cast<WindowListModel::RowType>(
        index.data(WindowListModel::RowTypeRole).toInt());
    if (rowType == WindowListModel::SectionHeaderRow) {
        painter->fillRect(option.rect, option.palette.alternateBase());

        QFont headerFont = option.font;
        headerFont.setBold(true);
        painter->setFont(headerFont);
        painter->setPen(option.palette.text().color());

        const QString title = index.data(WindowListModel::TitleRole).toString();
        const QString count = QString::number(index.data(WindowListModel::SectionCountRole).toInt());
        const QRect textRect = option.rect.adjusted(kHorizontalPadding, 0, -kHorizontalPadding, 0);
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, title);

        painter->setFont(option.font);
        painter->setPen(option.palette.placeholderText().color());
        const int countLeft = textRect.left() + QFontMetrics(headerFont).horizontalAdvance(title) + 10;
        const QRect countRect(countLeft,
                              textRect.top(),
                              qMax(0, textRect.right() - countLeft + 1),
                              textRect.height());
        painter->drawText(countRect, Qt::AlignVCenter | Qt::AlignLeft, count);

        painter->setPen(option.palette.mid().color());
        painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
        painter->restore();
        return;
    }

    const bool isActive = index.data(WindowListModel::IsActiveRole).toBool();
    const bool isSaved = index.data(WindowListModel::IsSavedRole).toBool();
    const QString title = index.data(WindowListModel::TitleRole).toString();
    const QString subtitle = index.data(WindowListModel::SubtitleRole).toString();
    QIcon icon = qvariant_cast<QIcon>(index.data(WindowListModel::IconRole));
    if (icon.isNull()) {
        icon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
    }

    const QRect iconRect(option.rect.left() + kHorizontalPadding,
                         option.rect.top() + (option.rect.height() - kIconSize) / 2,
                         kIconSize,
                         kIconSize);
    painter->setOpacity(isActive ? 1.0 : 0.55);
    icon.paint(painter, iconRect, Qt::AlignCenter, QIcon::Normal, QIcon::Off);
    painter->setOpacity(1.0);

    const int textLeft = iconRect.right() + kIconTextGap;
    const int trailingSpace = isSaved ? kRemoveButtonSize + kHorizontalPadding : 0;
    const QRect contentRect(textLeft,
                            option.rect.top(),
                            qMax(0, option.rect.right() - textLeft - kHorizontalPadding - trailingSpace + 1),
                            option.rect.height());

    const QColor titleColor = isActive
        ? option.palette.text().color()
        : option.palette.placeholderText().color();
    painter->setPen(titleColor);
    painter->setFont(option.font);

    if (isSaved && !subtitle.isEmpty()) {
        const QRect titleRect(contentRect.left(), contentRect.top() + 6, contentRect.width(), 22);
        const QString elidedTitle = painter->fontMetrics().elidedText(
            title, Qt::ElideRight, titleRect.width());
        painter->drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft, elidedTitle);

        QFont subtitleFont = option.font;
        subtitleFont.setPointSizeF(qMax(8.0, option.font.pointSizeF() - 1.0));
        painter->setFont(subtitleFont);
        painter->setPen(option.palette.placeholderText().color());
        const QRect subtitleRect(contentRect.left(), contentRect.top() + 27, contentRect.width(), 20);
        const QString elidedSubtitle = painter->fontMetrics().elidedText(
            subtitle, Qt::ElideRight, subtitleRect.width());
        painter->drawText(subtitleRect, Qt::AlignVCenter | Qt::AlignLeft, elidedSubtitle);
    } else {
        const QString elidedTitle = painter->fontMetrics().elidedText(
            title, Qt::ElideRight, contentRect.width());
        painter->drawText(contentRect, Qt::AlignVCenter | Qt::AlignLeft, elidedTitle);
    }

    const bool isHovered = m_hoveredIndex == index;
    if (isSaved && isHovered) {
        const QRect buttonRect = removeButtonRect(option.rect);
        const bool buttonHovered = option.widget &&
                                   buttonRect.contains(option.widget->mapFromGlobal(QCursor::pos()));
        drawRemoveIcon(painter, buttonRect, buttonHovered);
    }

    painter->restore();
}

QSize WindowListDelegate::sizeHint(const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const
{
    const auto rowType = static_cast<WindowListModel::RowType>(
        index.data(WindowListModel::RowTypeRole).toInt());
    return QSize(0, rowType == WindowListModel::SectionHeaderRow ? kHeaderHeight : kWindowRowHeight);
}

bool WindowListDelegate::editorEvent(QEvent* event,
                                     QAbstractItemModel* model,
                                     const QStyleOptionViewItem& option,
                                     const QModelIndex& index)
{
    Q_UNUSED(model);

    const bool isWindowRow = index.data(WindowListModel::RowTypeRole).toInt() ==
                             WindowListModel::WindowRow;
    const bool isSaved = index.data(WindowListModel::IsSavedRole).toBool();
    if (!isWindowRow || !isSaved) {
        return false;
    }

    const QRect buttonRect = removeButtonRect(option.rect);
    if (event->type() == QEvent::ToolTip) {
        const auto* helpEvent = static_cast<QHelpEvent*>(event);
        if (buttonRect.contains(helpEvent->pos())) {
            QToolTip::showText(helpEvent->globalPos(),
                               tr("Remove from saved layout"),
                               const_cast<QWidget*>(option.widget),
                               buttonRect);
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton &&
            buttonRect.contains(mouseEvent->position().toPoint())) {
            const QString entryId = index.data(WindowListModel::StableIdRole).toString();
            const QString title = index.data(WindowListModel::TitleRole).toString();
            qInfo() << "Remove icon clicked for saved layout entry:" << entryId << title;
            emit removeRequested(entryId, title);
            return true;
        }
    }

    return false;
}

bool WindowListDelegate::eventFilter(QObject* watched, QEvent* event)
{
    auto* view = qobject_cast<QAbstractItemView*>(parent());
    if (!view || watched != view->viewport()) {
        return QStyledItemDelegate::eventFilter(watched, event);
    }

    if (event->type() == QEvent::MouseMove) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        setHoveredIndex(view->indexAt(mouseEvent->position().toPoint()));
    } else if (event->type() == QEvent::Leave) {
        setHoveredIndex(QModelIndex());
    }

    return QStyledItemDelegate::eventFilter(watched, event);
}

void WindowListDelegate::setHoveredIndex(const QModelIndex& index)
{
    const QPersistentModelIndex nextIndex(index);
    if (m_hoveredIndex == nextIndex) {
        return;
    }

    auto* view = qobject_cast<QAbstractItemView*>(parent());
    const QPersistentModelIndex previousIndex = m_hoveredIndex;
    m_hoveredIndex = nextIndex;

    if (!view) {
        return;
    }
    if (previousIndex.isValid()) {
        view->viewport()->update(view->visualRect(previousIndex));
    }
    if (m_hoveredIndex.isValid()) {
        view->viewport()->update(view->visualRect(m_hoveredIndex));
    }
}

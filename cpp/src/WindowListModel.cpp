#include "../include/WindowListModel.h"

#include <QDebug>
#include <QHash>
#include <QPalette>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace {
struct SavedMonitorInfo {
    QString key;
    QString deviceName;
    int workLeft = 0;
    int workTop = 0;
    int workWidth = 0;
    int workHeight = 0;
    int displayNumber = 100000;
    bool hasWorkArea = false;
};

int displayNumber(const QString& deviceName)
{
    static const QRegularExpression displayNumber(QStringLiteral("DISPLAY(\\d+)$"),
                                                   QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = displayNumber.match(deviceName);
    return match.hasMatch() ? match.captured(1).toInt() : 100000;
}

QString monitorKey(const WindowInfo& window)
{
    if (!window.savedMonitorStableId.isEmpty()) {
        return QStringLiteral("stable:%1").arg(window.savedMonitorStableId.toLower());
    }
    if (!window.savedMonitorDeviceName.isEmpty()) {
        return QStringLiteral("device:%1").arg(window.savedMonitorDeviceName.toLower());
    }
    return QString();
}

QHash<QString, int> monitorOrdinalsForWindows(const QList<WindowInfo>& windows)
{
    QHash<QString, SavedMonitorInfo> byKey;
    for (const WindowInfo& window : windows) {
        if (!window.isSaved) {
            continue;
        }

        const QString key = monitorKey(window);
        if (key.isEmpty()) {
            continue;
        }

        SavedMonitorInfo info = byKey.value(key);
        if (info.key.isEmpty()) {
            info.key = key;
            info.deviceName = window.savedMonitorDeviceName;
            info.displayNumber = displayNumber(window.savedMonitorDeviceName);
        }

        if (!info.hasWorkArea && window.savedMonitorWorkWidth > 0 && window.savedMonitorWorkHeight > 0) {
            info.workLeft = window.savedMonitorWorkLeft;
            info.workTop = window.savedMonitorWorkTop;
            info.workWidth = window.savedMonitorWorkWidth;
            info.workHeight = window.savedMonitorWorkHeight;
            info.hasWorkArea = true;
        }

        byKey.insert(key, info);
    }

    QList<SavedMonitorInfo> monitors = byKey.values();
    std::sort(monitors.begin(), monitors.end(), [](const SavedMonitorInfo& left,
                                                   const SavedMonitorInfo& right) {
        if (left.hasWorkArea != right.hasWorkArea) return left.hasWorkArea;
        if (left.hasWorkArea && right.hasWorkArea) {
            if (left.workLeft != right.workLeft) return left.workLeft < right.workLeft;
            if (left.workTop != right.workTop) return left.workTop < right.workTop;
            if (left.workWidth != right.workWidth) return left.workWidth < right.workWidth;
            if (left.workHeight != right.workHeight) return left.workHeight < right.workHeight;
        }
        if (left.displayNumber != right.displayNumber) return left.displayNumber < right.displayNumber;
        return left.deviceName.compare(right.deviceName, Qt::CaseInsensitive) < 0;
    });

    QHash<QString, int> ordinals;
    for (int index = 0; index < monitors.size(); ++index) {
        ordinals.insert(monitors.at(index).key, index + 1);
    }
    return ordinals;
}

int monitorOrdinal(const WindowInfo& window, const QHash<QString, int>& ordinals)
{
    const QString key = monitorKey(window);
    return key.isEmpty() ? 100000 : ordinals.value(key, 100000);
}

QString monitorLabel(const WindowInfo& window, const QHash<QString, int>& ordinals)
{
    const int ordinal = monitorOrdinal(window, ordinals);
    if (ordinal != 100000) {
        return QStringLiteral("Monitor %1").arg(ordinal);
    }
    if (window.savedMonitorDeviceName.isEmpty()) {
        return QStringLiteral("Unknown monitor");
    }
    return displayNumber(window.savedMonitorDeviceName) != 100000
        ? QStringLiteral("Saved monitor")
        : window.savedMonitorDeviceName;
}

QString windowTitle(const WindowInfo& window)
{
    return window.title.isEmpty() ? window.processName : window.title;
}

QString fallbackStableId(const WindowInfo& window)
{
    if (!window.stableId.isEmpty()) {
        return window.stableId;
    }
    return QStringLiteral("hwnd:%1").arg(reinterpret_cast<quintptr>(window.hwnd));
}
}

WindowListModel::WindowListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

QList<WindowListModel::Row> WindowListModel::buildRows(const QList<WindowInfo>& windows) const
{
    QList<WindowInfo> saved;
    QList<WindowInfo> other;
    for (const WindowInfo& window : windows) {
        (window.isSaved ? saved : other).append(window);
    }

    const QHash<QString, int> monitorOrdinals = monitorOrdinalsForWindows(windows);
    std::sort(saved.begin(), saved.end(), [&monitorOrdinals](const WindowInfo& left,
                                                             const WindowInfo& right) {
        const int leftMonitor = monitorOrdinal(left, monitorOrdinals);
        const int rightMonitor = monitorOrdinal(right, monitorOrdinals);
        if (leftMonitor != rightMonitor) return leftMonitor < rightMonitor;
        const int monitorCompare = left.savedMonitorDeviceName.compare(
            right.savedMonitorDeviceName, Qt::CaseInsensitive);
        if (monitorCompare != 0) return monitorCompare < 0;
        if (left.savedTop != right.savedTop) return left.savedTop < right.savedTop;
        if (left.savedLeft != right.savedLeft) return left.savedLeft < right.savedLeft;
        return windowTitle(left).compare(windowTitle(right), Qt::CaseInsensitive) < 0;
    });

    std::sort(other.begin(), other.end(), [](const WindowInfo& left, const WindowInfo& right) {
        const int processCompare = left.processName.compare(right.processName, Qt::CaseInsensitive);
        if (processCompare != 0) return processCompare < 0;
        return fallbackStableId(left) < fallbackStableId(right);
    });

    QList<Row> rows;
    Row savedHeader;
    savedHeader.type = SectionHeaderRow;
    savedHeader.key = QStringLiteral("section:saved");
    savedHeader.sectionTitle = QStringLiteral("Saved layout");
    savedHeader.sectionCount = saved.size();
    rows.append(savedHeader);

    for (const WindowInfo& window : saved) {
        Row row;
        row.type = WindowRow;
        row.key = QStringLiteral("saved:%1").arg(fallbackStableId(window));
        row.window = window;
        rows.append(row);
    }

    Row otherHeader;
    otherHeader.type = SectionHeaderRow;
    otherHeader.key = QStringLiteral("section:other");
    otherHeader.sectionTitle = QStringLiteral("Other open windows");
    otherHeader.sectionCount = other.size();
    rows.append(otherHeader);

    for (const WindowInfo& window : other) {
        Row row;
        row.type = WindowRow;
        row.key = QStringLiteral("other:%1").arg(fallbackStableId(window));
        row.window = window;
        rows.append(row);
    }

    return rows;
}

bool WindowListModel::hasSameStructure(const QList<Row>& rows) const
{
    if (rows.size() != m_rows.size()) {
        return false;
    }
    for (int row = 0; row < rows.size(); ++row) {
        if (rows.at(row).type != m_rows.at(row).type || rows.at(row).key != m_rows.at(row).key) {
            return false;
        }
    }
    return true;
}

QString WindowListModel::subtitleForWindow(const WindowInfo& window) const
{
    if (!window.isSaved) {
        return QString();
    }

    QStringList details;
    if (!window.isActive) {
        details.append(window.savedHasRunningCandidates
                           ? QStringLiteral("Not matched")
                           : QStringLiteral("Not running"));
    }
    details.append(monitorLabel(window, m_monitorOrdinals));
    if (window.savedIsMinimized) {
        details.append(QStringLiteral("Minimized"));
    } else if (window.savedIsMaximized) {
        details.append(QStringLiteral("Maximized"));
    } else {
        details.append(QStringLiteral("Windowed"));
    }
    return details.join(QStringLiteral("  |  "));
}

void WindowListModel::updateWindows(const QList<WindowInfo>& windows)
{
    QHash<QString, int> monitorOrdinals = monitorOrdinalsForWindows(windows);
    QList<Row> rows = buildRows(windows);
    if (hasSameStructure(rows)) {
        m_monitorOrdinals = std::move(monitorOrdinals);
        m_rows = std::move(rows);
        if (!m_rows.isEmpty()) {
            emit dataChanged(index(0, 0), index(m_rows.size() - 1, 0));
        }
    } else {
        beginResetModel();
        m_monitorOrdinals = std::move(monitorOrdinals);
        m_rows = std::move(rows);
        endResetModel();
    }
    qInfo() << "WindowListModel updated with" << windows.size() << "windows in" << m_rows.size() << "rows.";
}

int WindowListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant WindowListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return QVariant();
    }

    const Row& row = m_rows.at(index.row());
    if (role == RowTypeRole) return row.type;
    if (role == StableIdRole) {
        return row.type == WindowRow ? row.window.stableId : row.key;
    }

    if (row.type == SectionHeaderRow) {
        if (role == Qt::DisplayRole || role == TitleRole) return row.sectionTitle;
        if (role == SectionCountRole) return row.sectionCount;
        if (role == Qt::AccessibleTextRole) {
            return QStringLiteral("%1, %2 windows").arg(row.sectionTitle).arg(row.sectionCount);
        }
        return QVariant();
    }

    const WindowInfo& window = row.window;
    switch (role) {
        case Qt::DisplayRole:
        case TitleRole:
            return windowTitle(window);
        case SubtitleRole:
            return subtitleForWindow(window);
        case Qt::DecorationRole:
        case IconRole:
            return window.icon;
        case IconKeyRole:
            return window.iconKey;
        case IsActiveRole:
            return window.isActive;
        case IsSavedRole:
            return window.isSaved;
        case Qt::ForegroundRole:
            return window.isActive ? QVariant() : QPalette().brush(QPalette::PlaceholderText);
        case Qt::AccessibleTextRole: {
            const QString subtitle = subtitleForWindow(window);
            return subtitle.isEmpty()
                ? windowTitle(window)
                : QStringLiteral("%1, %2").arg(windowTitle(window), subtitle);
        }
        default:
            return QVariant();
    }
}

Qt::ItemFlags WindowListModel::flags(const QModelIndex& index) const
{
    if (!index.isValid() || m_rows.at(index.row()).type == SectionHeaderRow) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled;
}

QHash<int, QByteArray> WindowListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[RowTypeRole] = "rowType";
    roles[TitleRole] = "title";
    roles[SubtitleRole] = "subtitle";
    roles[IconRole] = "icon";
    roles[IconKeyRole] = "iconKey";
    roles[IsActiveRole] = "isActive";
    roles[IsSavedRole] = "isSaved";
    roles[StableIdRole] = "stableId";
    roles[SectionCountRole] = "sectionCount";
    return roles;
}

#include "../include/LayoutGeometry.h"

#include <QtMath>

namespace {
const MonitorSnapshot* findByDevice(const QList<MonitorSnapshot>& monitors, const QString& deviceName)
{
    for (const MonitorSnapshot& monitor : monitors) {
        if (monitor.deviceName.compare(deviceName, Qt::CaseInsensitive) == 0) {
            return &monitor;
        }
    }
    return nullptr;
}

const MonitorSnapshot* findUniqueByStableId(const QList<MonitorSnapshot>& monitors,
                                            const QString& stableId)
{
    const MonitorSnapshot* match = nullptr;
    for (const MonitorSnapshot& monitor : monitors) {
        if (monitor.stableId.compare(stableId, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (match) {
            return nullptr;
        }
        match = &monitor;
    }
    return match;
}

const MonitorSnapshot* findUniqueBySavedTopology(const QList<MonitorSnapshot>& monitors,
                                                 const WindowPosition& savedPosition)
{
    const MonitorSnapshot* match = nullptr;
    for (const MonitorSnapshot& monitor : monitors) {
        if (monitor.workArea.left() != savedPosition.monitorWorkLeft ||
            monitor.workArea.top() != savedPosition.monitorWorkTop) {
            continue;
        }
        if (savedPosition.monitorWorkWidth > 0 &&
            monitor.workArea.width() != savedPosition.monitorWorkWidth) {
            continue;
        }
        if (savedPosition.monitorWorkHeight > 0 &&
            monitor.workArea.height() != savedPosition.monitorWorkHeight) {
            continue;
        }
        if (qAbs(monitor.dpiScale - savedPosition.dpiScale) > 0.01) {
            continue;
        }
        if (match) {
            return nullptr;
        }
        match = &monitor;
    }
    return match;
}

const MonitorSnapshot* findNearestToPoint(const QList<MonitorSnapshot>& monitors, const QPoint& point)
{
    if (monitors.isEmpty()) {
        return nullptr;
    }

    const MonitorSnapshot* best = &monitors.first();
    int bestDistance = INT_MAX;
    for (const MonitorSnapshot& monitor : monitors) {
        if (monitor.workArea.contains(point)) {
            return &monitor;
        }

        QPoint center = monitor.workArea.center();
        int dx = center.x() - point.x();
        int dy = center.y() - point.y();
        int distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = &monitor;
        }
    }
    return best;
}
}

namespace LayoutGeometry {

QRect captureRectForState(const QRect& currentScreenRect,
                          const QRect& normalWorkspaceRect,
                          bool isMaximized,
                          bool isMinimized,
                          const QRect& monitorArea,
                          const QRect& workArea)
{
    if ((!isMaximized && !isMinimized) || !normalWorkspaceRect.isValid()) {
        return currentScreenRect;
    }

    // WINDOWPLACEMENT stores ordinary top-level windows in workspace
    // coordinates. Convert those coordinates to the virtual-screen space
    // consumed by the monitor and window-positioning APIs.
    return normalWorkspaceRect.translated(workArea.left() - monitorArea.left(),
                                          workArea.top() - monitorArea.top());
}

QRect safeLegacyNormalRect(const QRect& currentNormalScreenRect,
                           const QRect& targetWorkArea)
{
    if (!targetWorkArea.isValid()) {
        return currentNormalScreenRect;
    }

    const bool currentIsUseful = currentNormalScreenRect.isValid() &&
        currentNormalScreenRect.width() < targetWorkArea.width() * 95 / 100 &&
        currentNormalScreenRect.height() < targetWorkArea.height() * 95 / 100;

    int width = currentIsUseful
        ? qMin(currentNormalScreenRect.width(), targetWorkArea.width())
        : qMax(640, targetWorkArea.width() * 4 / 5);
    int height = currentIsUseful
        ? qMin(currentNormalScreenRect.height(), targetWorkArea.height())
        : qMax(480, targetWorkArea.height() * 4 / 5);
    width = qMin(width, targetWorkArea.width());
    height = qMin(height, targetWorkArea.height());

    const int left = targetWorkArea.left() + (targetWorkArea.width() - width) / 2;
    const int top = targetWorkArea.top() + (targetWorkArea.height() - height) / 2;
    return QRect(left, top, width, height);
}

RestorePlan planRestore(const WindowPosition& savedPosition,
                        const QList<MonitorSnapshot>& monitors)
{
    RestorePlan plan;
    plan.maximize = savedPosition.isMaximized;

    if (savedPosition.width <= 0 || savedPosition.height <= 0) {
        plan.reason = QStringLiteral("Saved size is invalid.");
        return plan;
    }

    const MonitorSnapshot* targetMonitor = nullptr;
    if (savedPosition.isMonitorRelative && !savedPosition.monitorDeviceName.isEmpty()) {
        if (!savedPosition.monitorStableId.isEmpty()) {
            targetMonitor = findUniqueByStableId(monitors, savedPosition.monitorStableId);
            if (targetMonitor) {
                plan.monitorMatchReason = QStringLiteral("stable monitor identity");
            }
        }
        if (!targetMonitor) {
            targetMonitor = findByDevice(monitors, savedPosition.monitorDeviceName);
            if (targetMonitor &&
                (savedPosition.monitorStableId.isEmpty() || targetMonitor->stableId.isEmpty() ||
                 targetMonitor->stableId.compare(savedPosition.monitorStableId, Qt::CaseInsensitive) == 0)) {
                plan.monitorMatchReason = QStringLiteral("display device name");
            } else {
                targetMonitor = nullptr;
            }
        }
        if (!targetMonitor) {
            targetMonitor = findUniqueBySavedTopology(monitors, savedPosition);
            if (targetMonitor) {
                plan.monitorMatchReason = QStringLiteral("saved monitor topology");
            }
        }
        if (!targetMonitor) {
            plan.reason = QStringLiteral("Saved monitor could not be identified uniquely.");
            return plan;
        }
    } else {
        targetMonitor = findNearestToPoint(monitors, QPoint(savedPosition.left, savedPosition.top));
        if (!targetMonitor) {
            plan.reason = QStringLiteral("No monitors are available.");
            return plan;
        }
        plan.monitorMatchReason = QStringLiteral("nearest monitor for legacy coordinates");
    }

    int left = qRound(static_cast<double>(savedPosition.left) * targetMonitor->dpiScale);
    int top = qRound(static_cast<double>(savedPosition.top) * targetMonitor->dpiScale);
    int width = qRound(static_cast<double>(savedPosition.width) * targetMonitor->dpiScale);
    int height = qRound(static_cast<double>(savedPosition.height) * targetMonitor->dpiScale);

    if (savedPosition.isMonitorRelative) {
        left += targetMonitor->workArea.left();
        top += targetMonitor->workArea.top();
    }

    plan.targetRect = QRect(left, top, width, height);
    plan.targetDeviceName = targetMonitor->deviceName;
    plan.targetStableId = targetMonitor->stableId;
    plan.canRestore = true;
    return plan;
}

QRect normalRectForRestore(const WindowPosition& savedPosition,
                           const QRect& plannedRect,
                           const QRect& currentNormalScreenRect,
                           const QRect& targetWorkArea)
{
    if (!savedPosition.isMaximized) {
        return plannedRect;
    }
    if (!savedPosition.hasNormalBounds) {
        return safeLegacyNormalRect(currentNormalScreenRect, targetWorkArea);
    }
    // Windows can retain normal bounds from another display while the window
    // is maximized. Place the normal rectangle on the intended display before
    // maximizing, including after a resolution or DPI change.
    if (!targetWorkArea.contains(plannedRect)) {
        return safeLegacyNormalRect(plannedRect, targetWorkArea);
    }
    return plannedRect;
}

}

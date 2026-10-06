#pragma once

#include <QList>
#include <QRect>
#include <QString>

#include "WindowPosition.h"

struct MonitorSnapshot {
    QString deviceName;
    QRect workArea;
    double dpiScale = 1.0;
    QString stableId;
};

struct RestorePlan {
    bool canRestore = false;
    QRect targetRect;
    QString reason;
    bool maximize = false;
    QString targetDeviceName;
    QString targetStableId;
    QString monitorMatchReason;
};

namespace LayoutGeometry {
QRect captureRectForState(const QRect& currentScreenRect,
                          const QRect& normalWorkspaceRect,
                          bool isMaximized,
                          bool isMinimized,
                          const QRect& monitorArea,
                          const QRect& workArea);
QRect safeLegacyNormalRect(const QRect& currentNormalScreenRect,
                           const QRect& targetWorkArea);
QRect normalRectForRestore(const WindowPosition& savedPosition,
                           const QRect& plannedRect,
                           const QRect& currentNormalScreenRect,
                           const QRect& targetWorkArea);
RestorePlan planRestore(const WindowPosition& savedPosition,
                        const QList<MonitorSnapshot>& monitors);
}

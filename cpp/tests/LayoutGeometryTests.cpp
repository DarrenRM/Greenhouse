#include "../include/LayoutGeometry.h"

#include <QCoreApplication>
#include <QDebug>

namespace {
int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        qCritical() << "FAIL:" << message;
        ++failures;
    }
}

WindowPosition savedOnSecondMonitor()
{
    WindowPosition pos;
    pos.left = 100;
    pos.top = 80;
    pos.width = 900;
    pos.height = 700;
    pos.dpiScale = 1.0;
    pos.monitorDeviceName = QStringLiteral("\\\\.\\DISPLAY2");
    pos.monitorWorkLeft = 1920;
    pos.monitorWorkTop = 0;
    pos.isMonitorRelative = true;
    return pos;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QList<MonitorSnapshot> normalSetup = {
        {QStringLiteral("\\\\.\\DISPLAY1"), QRect(0, 0, 1920, 1040), 1.0},
        {QStringLiteral("\\\\.\\DISPLAY2"), QRect(1920, 0, 2560, 1400), 1.0},
    };

    WindowPosition pos = savedOnSecondMonitor();

    const QRect maximizedScreenRect(-8, -8, 1936, 1056);
    const QRect normalWorkspaceRect(100, 80, 900, 700);
    const QRect monitorArea(0, 0, 1920, 1080);
    const QRect topTaskbarWorkArea(0, 40, 1920, 1040);
    expect(LayoutGeometry::captureRectForState(maximizedScreenRect,
                                                normalWorkspaceRect,
                                                true,
                                                false,
                                                monitorArea,
                                                topTaskbarWorkArea) == QRect(100, 120, 900, 700),
           "maximized capture should save restored bounds converted from workspace to screen coordinates");
    expect(LayoutGeometry::captureRectForState(QRect(20, 30, 800, 600),
                                                normalWorkspaceRect,
                                                false,
                                                false,
                                                monitorArea,
                                                topTaskbarWorkArea) == QRect(20, 30, 800, 600),
           "normal capture should keep the current screen rectangle");

    expect(LayoutGeometry::safeLegacyNormalRect(QRect(-8, -8, 1936, 1056),
                                                 QRect(0, 0, 1920, 1040)) ==
               QRect(192, 104, 1536, 832),
           "legacy maximized bounds should fall back to a centered 80 percent normal rectangle");
    expect(LayoutGeometry::safeLegacyNormalRect(QRect(4000, 500, 1000, 700),
                                                 QRect(0, 0, 1920, 1040)) ==
               QRect(460, 170, 1000, 700),
           "a useful current normal size should be preserved and centered on the target monitor");

    RestorePlan plan = LayoutGeometry::planRestore(pos, normalSetup);
    expect(plan.canRestore, "monitor-relative layout should restore when saved monitor exists");
    expect(plan.targetRect == QRect(2020, 80, 900, 700), "target rect should be monitor work-area relative");

    QList<MonitorSnapshot> dpiChangedSetup = {
        {QStringLiteral("\\\\.\\DISPLAY1"), QRect(0, 0, 1920, 1040), 1.0},
        {QStringLiteral("\\\\.\\DISPLAY2"), QRect(3840, 0, 1600, 2500), 1.25},
    };
    plan = LayoutGeometry::planRestore(pos, dpiChangedSetup);
    expect(plan.canRestore, "layout should restore after monitor position and DPI changes");
    expect(plan.targetRect == QRect(3965, 100, 1125, 875), "target rect should scale using current monitor DPI");

    QList<MonitorSnapshot> missingSecondMonitor = {
        {QStringLiteral("\\\\.\\DISPLAY1"), QRect(0, 0, 1920, 1040), 1.0},
    };
    plan = LayoutGeometry::planRestore(pos, missingSecondMonitor);
    expect(!plan.canRestore, "layout should not restore onto the wrong monitor when saved monitor is missing");

    QList<MonitorSnapshot> renumberedSetup = {
        {QStringLiteral("\\\\.\\DISPLAY6"), QRect(0, 0, 1920, 1040), 1.0},
        {QStringLiteral("\\\\.\\DISPLAY7"), QRect(1920, 0, 2560, 1400), 1.0},
    };
    plan = LayoutGeometry::planRestore(pos, renumberedSetup);
    expect(plan.canRestore, "renumbered monitors should resolve from the saved topology");
    expect(plan.targetDeviceName == QStringLiteral("\\\\.\\DISPLAY7"),
           "renumbered restore should select the monitor occupying the saved topology slot");
    expect(plan.monitorMatchReason == QStringLiteral("saved monitor topology"),
           "renumbered restore should explain its topology fallback");

    pos.monitorStableId = QStringLiteral("physical-monitor-b");
    QList<MonitorSnapshot> movedAndRenumberedSetup = {
        {QStringLiteral("\\\\.\\DISPLAY6"), QRect(1920, 0, 1920, 1040), 1.0, QStringLiteral("physical-monitor-a")},
        {QStringLiteral("\\\\.\\DISPLAY7"), QRect(0, 0, 2560, 1400), 1.0, QStringLiteral("physical-monitor-b")},
    };
    plan = LayoutGeometry::planRestore(pos, movedAndRenumberedSetup);
    expect(plan.canRestore && plan.targetDeviceName == QStringLiteral("\\\\.\\DISPLAY7"),
           "stable monitor identity should survive renumbering and topology changes");
    expect(plan.monitorMatchReason == QStringLiteral("stable monitor identity"),
           "stable identity should be preferred over volatile display names");
    pos.monitorStableId.clear();

    pos.isMaximized = true;
    plan = LayoutGeometry::planRestore(pos, normalSetup);
    expect(plan.canRestore && plan.maximize, "saved maximized state should be carried into the restore plan");

    WindowPosition legacy;
    legacy.left = 2000;
    legacy.top = 100;
    legacy.width = 800;
    legacy.height = 600;
    legacy.isMonitorRelative = false;
    plan = LayoutGeometry::planRestore(legacy, normalSetup);
    expect(plan.canRestore, "legacy absolute layouts should still restore");
    expect(plan.targetRect == QRect(2000, 100, 800, 600), "legacy absolute layout should keep absolute coordinates");

    // Actual secondary Brave layout: a maximized slot bound to the right-hand
    // monitor, but with stale normal bounds located on the main display.
    const QRect rightWorkArea(3840, 438, 1920, 1040);
    WindowPosition braveSide;
    braveSide.left = -2444;
    braveSide.top = 548;
    braveSide.width = 524;
    braveSide.height = 94;
    braveSide.isMaximized = true;
    braveSide.isMonitorRelative = true;
    braveSide.monitorDeviceName = QStringLiteral("side");
    const QList<MonitorSnapshot> morningSetup = {
        {QStringLiteral("main"), QRect(0, 0, 3840, 2090), 1.75},
        {QStringLiteral("side"), rightWorkArea, 1.0}
    };
    plan = LayoutGeometry::planRestore(braveSide, morningSetup);
    expect(plan.canRestore && plan.targetRect == QRect(1396, 986, 524, 94),
           "morning Brave fixture should reproduce the stale rectangle on the main monitor");
    const QRect currentBraveNormal(1396, 986, 524, 94);
    QRect repaired = LayoutGeometry::normalRectForRestore(
        braveSide, plan.targetRect, currentBraveNormal, rightWorkArea);
    expect(rightWorkArea.contains(repaired),
           "legacy maximized Brave must move inside its saved monitor before maximizing");
    braveSide.hasNormalBounds = true;
    repaired = LayoutGeometry::normalRectForRestore(
        braveSide, plan.targetRect, currentBraveNormal, rightWorkArea);
    expect(rightWorkArea.contains(repaired),
           "even marked normal bounds must not maximize Brave on the wrong monitor");
    const QRect validNormal(4000, 500, 1000, 700);
    expect(LayoutGeometry::normalRectForRestore(braveSide, validNormal, {}, rightWorkArea) == validNormal,
           "valid saved normal bounds should retain their exact position and size");
    const QRect smallerLeftMonitor(-1600, -300, 1600, 900);
    repaired = LayoutGeometry::normalRectForRestore(
        braveSide, QRect(-1800, -400, 2400, 1400), {}, smallerLeftMonitor);
    expect(smallerLeftMonitor.contains(repaired),
           "maximized restore should handle negative origins and a smaller target display");
    braveSide.isMaximized = false;
    expect(LayoutGeometry::normalRectForRestore(braveSide, plan.targetRect, {}, rightWorkArea) == plan.targetRect,
           "ordinary spanning windows should retain their saved geometry");

    if (failures > 0) {
        qCritical() << failures << "layout geometry test(s) failed.";
        return 1;
    }

    qInfo() << "All layout geometry tests passed.";
    return 0;
}

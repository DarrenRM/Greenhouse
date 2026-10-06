#include "../include/WindowMatcher.h"

#include <QCoreApplication>
#include <QDebug>
#include <QHash>

namespace {
int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        qCritical() << "FAIL:" << message;
        ++failures;
    }
}

HWND hwnd(quintptr value)
{
    return reinterpret_cast<HWND>(value);
}

WindowInfo brave(HWND handle, const QString& title)
{
    WindowInfo info;
    info.hwnd = handle;
    info.title = title;
    info.processName = QStringLiteral("brave.exe");
    info.processPath = QStringLiteral("C:\\Program Files\\BraveSoftware\\Brave-Browser\\Application\\brave.exe");
    info.className = QStringLiteral("Chrome_WidgetWin_1");
    info.isActive = true;
    return info;
}

WindowInfo discord(HWND handle, const QString& title)
{
    WindowInfo info;
    info.hwnd = handle;
    info.title = title;
    info.processName = QStringLiteral("Discord.exe");
    info.processPath = QStringLiteral("C:\\Users\\Darren\\AppData\\Local\\Discord\\app-1.0.9236\\Discord.exe");
    info.className = QStringLiteral("Chrome_WidgetWin_1");
    info.isActive = true;
    return info;
}

WindowInfo vscode(HWND handle, const QString& title)
{
    WindowInfo info;
    info.hwnd = handle;
    info.title = title;
    info.processName = QStringLiteral("Code.exe");
    info.processPath = QStringLiteral("C:\\Users\\Darren\\AppData\\Local\\Programs\\Microsoft VS Code\\Code.exe");
    info.className = QStringLiteral("Chrome_WidgetWin_1");
    info.isActive = true;
    return info;
}

WindowPosition positionOn(const QString& monitor, int left, int top, int width, int height, bool maximized)
{
    WindowPosition position;
    position.monitorDeviceName = monitor;
    position.left = left;
    position.top = top;
    position.width = width;
    position.height = height;
    position.isMaximized = maximized;
    position.isMonitorRelative = true;
    return position;
}

SavedWindowEntry saved(const QString& id, const WindowInfo& info, HWND lastHwnd, const WindowPosition& position)
{
    SavedWindowEntry entry;
    entry.id = id;
    entry.info = info;
    entry.currentHwnd = lastHwnd;
    entry.position = position;
    return entry;
}

HWND assignmentFor(const QList<WindowMatcher::Assignment>& assignments, const QString& id)
{
    for (const WindowMatcher::Assignment& assignment : assignments) {
        if (assignment.entryId == id) {
            return assignment.hwnd;
        }
    }
    return nullptr;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QString mainMonitor = QStringLiteral("\\\\.\\DISPLAY2");
    const QString sideMonitor = QStringLiteral("\\\\.\\DISPLAY1");

    SavedWindowEntry savedMain = saved(
        QStringLiteral("window:main-brave"),
        brave(hwnd(0x620752), QStringLiteral("New Tab - Brave")),
        hwnd(0x620752),
        positionOn(mainMonitor, -7, -7, 2208, 1208, true));

    SavedWindowEntry savedSide = saved(
        QStringLiteral("window:side-brave"),
        brave(hwnd(0x20678), QStringLiteral("Enshrouded Quick Stack Station Range Difference - Grok - Brave")),
        hwnd(0x20678),
        positionOn(sideMonitor, -8, -8, 1936, 1056, true));

    QList<WindowInfo> twoChangedBraveWindows = {
        brave(hwnd(0x404b8), QStringLiteral("What the Totem Nerfs Mean - YouTube - Brave")),
        brave(hwnd(0x2430462), QStringLiteral("You Make Me Wait - YouTube - Brave")),
    };

    QHash<HWND, WindowPosition> bothCurrentlyOnMain;
    bothCurrentlyOnMain.insert(hwnd(0x404b8), positionOn(mainMonitor, -7, -7, 2208, 1208, true));
    bothCurrentlyOnMain.insert(hwnd(0x2430462), positionOn(mainMonitor, -7, -7, 2208, 1208, true));

    QList<WindowMatcher::Assignment> assignments =
        WindowMatcher::matchSavedWindows({savedMain, savedSide}, twoChangedBraveWindows, bothCurrentlyOnMain);

    expect(assignments.isEmpty(),
           "two changed Brave windows without fingerprints should not be guessed by geometry");

    QList<WindowInfo> oneChangedBraveWindow = {
        brave(hwnd(0x404b8), QStringLiteral("What the Totem Nerfs Mean - YouTube - Brave")),
    };
    assignments = WindowMatcher::matchSavedWindows({savedMain, savedSide}, oneChangedBraveWindow, bothCurrentlyOnMain);
    expect(assignments.isEmpty(),
           "one live Brave window should not be guessed when the saved layout has two Brave slots");

    QList<WindowInfo> oneExactTitle = {
        brave(hwnd(0x404b8), QStringLiteral("What the Totem Nerfs Mean - YouTube - Brave")),
        brave(hwnd(0x2430462), QStringLiteral("Enshrouded Quick Stack Station Range Difference - Grok - Brave")),
    };
    assignments = WindowMatcher::matchSavedWindows({savedMain, savedSide}, oneExactTitle, bothCurrentlyOnMain);
    expect(assignmentFor(assignments, QStringLiteral("window:side-brave")) == hwnd(0x2430462),
           "exact Brave title should beat geometry fallback");

    savedMain.browserFingerprint = BrowserFingerprinting::fromTabTitles(
        savedMain.info,
        {
            QStringLiteral("Codex - Pinned"),
            QStringLiteral("Project Console"),
            QStringLiteral("Release Checklist")
        },
        QStringLiteral("Project Console"));
    savedSide.browserFingerprint = BrowserFingerprinting::fromTabTitles(
        savedSide.info,
        {
            QStringLiteral("Grok - Pinned"),
            QStringLiteral("Enshrouded Quick Stack Station Range Difference"),
            QStringLiteral("Interactive Map")
        },
        QStringLiteral("Enshrouded Quick Stack Station Range Difference"));

    QHash<HWND, BrowserFingerprint> currentFingerprints;
    currentFingerprints.insert(
        hwnd(0x404b8),
        BrowserFingerprinting::fromTabTitles(
            twoChangedBraveWindows.at(0),
            {
                QStringLiteral("Grok - Pinned"),
                QStringLiteral("Enshrouded Quick Stack Station Range Difference"),
                QStringLiteral("Interactive Map")
            },
            QStringLiteral("Interactive Map")));
    currentFingerprints.insert(
        hwnd(0x2430462),
        BrowserFingerprinting::fromTabTitles(
            twoChangedBraveWindows.at(1),
            {
                QStringLiteral("Codex - Pinned"),
                QStringLiteral("Project Console"),
                QStringLiteral("Release Checklist")
            },
            QStringLiteral("Release Checklist")));

    assignments = WindowMatcher::matchSavedWindows(
        {savedMain, savedSide},
        twoChangedBraveWindows,
        bothCurrentlyOnMain,
        currentFingerprints);
    expect(assignmentFor(assignments, QStringLiteral("window:main-brave")) == hwnd(0x2430462),
           "main Brave slot should follow its tab fingerprint, not current monitor geometry");
    expect(assignmentFor(assignments, QStringLiteral("window:side-brave")) == hwnd(0x404b8),
           "secondary Brave slot should follow its tab fingerprint, not current monitor geometry");

    SavedWindowEntry ambiguousMain = saved(
        QStringLiteral("window:ambiguous-main"),
        brave(hwnd(0x1111), QStringLiteral("Saved Main - Brave")),
        hwnd(0x1111),
        positionOn(mainMonitor, -7, -7, 2208, 1208, true));
    SavedWindowEntry ambiguousSide = saved(
        QStringLiteral("window:ambiguous-side"),
        brave(hwnd(0x2222), QStringLiteral("Saved Side - Brave")),
        hwnd(0x2222),
        positionOn(sideMonitor, -8, -8, 1936, 1056, true));
    ambiguousMain.browserFingerprint = BrowserFingerprinting::fromTabTitles(
        ambiguousMain.info,
        {QStringLiteral("Shared Pin - Pinned"), QStringLiteral("Shared Work")},
        QStringLiteral("Shared Work"));
    ambiguousSide.browserFingerprint = ambiguousMain.browserFingerprint;

    QList<WindowInfo> ambiguousCurrent = {
        brave(hwnd(0x3333), QStringLiteral("Current A - Brave")),
        brave(hwnd(0x4444), QStringLiteral("Current B - Brave")),
    };
    QHash<HWND, BrowserFingerprint> ambiguousFingerprints;
    ambiguousFingerprints.insert(hwnd(0x3333), ambiguousMain.browserFingerprint);
    ambiguousFingerprints.insert(hwnd(0x4444), ambiguousMain.browserFingerprint);
    assignments = WindowMatcher::matchSavedWindows(
        {ambiguousMain, ambiguousSide},
        ambiguousCurrent,
        {},
        ambiguousFingerprints);
    expect(assignments.isEmpty(),
           "duplicate Brave windows with identical fingerprints should fail closed");

    SavedWindowEntry trustedMain = savedMain;
    trustedMain.id = QStringLiteral("window:trusted-main");
    trustedMain.currentHwnd = hwnd(0x404b8);
    trustedMain.currentHwndTrusted = true;
    trustedMain.browserFingerprint = BrowserFingerprint();
    assignments = WindowMatcher::matchSavedWindows(
        {trustedMain},
        {brave(hwnd(0x404b8), QStringLiteral("Completely Different Active Tab - Brave"))},
        bothCurrentlyOnMain);
    expect(assignmentFor(assignments, QStringLiteral("window:trusted-main")) == hwnd(0x404b8),
           "browser HWNDs observed in the current Greenhouse session should remain usable while live");

    SavedWindowEntry trustedLegacyMain = savedMain;
    trustedLegacyMain.browserFingerprint = BrowserFingerprint();
    trustedLegacyMain.currentHwnd = hwnd(0x404b8);
    trustedLegacyMain.currentHwndTrusted = true;

    SavedWindowEntry trustedLegacySide = savedSide;
    trustedLegacySide.browserFingerprint = BrowserFingerprint();
    trustedLegacySide.currentHwnd = hwnd(0x2430462);
    trustedLegacySide.currentHwndTrusted = true;

    assignments = WindowMatcher::matchSavedWindows(
        {trustedLegacyMain, trustedLegacySide},
        twoChangedBraveWindows,
        bothCurrentlyOnMain);
    expect(assignments.isEmpty(),
           "trusted HWNDs alone must not restore duplicate browser windows without fingerprints");

    SavedWindowEntry savedDiscord = saved(
        QStringLiteral("window:discord"),
        discord(hwnd(0x9075e), QStringLiteral("#general | Shrub Hut 3B - Discord")),
        hwnd(0x9075e),
        positionOn(sideMonitor, 416, 45, 1400, 875, false));

    QList<WindowInfo> currentDiscord = {
        discord(hwnd(0x9075e), QStringLiteral("#general | Okay, now what? - Discord")),
    };
    assignments = WindowMatcher::matchSavedWindows({savedDiscord}, currentDiscord, {});
    expect(assignmentFor(assignments, QStringLiteral("window:discord")) == hwnd(0x9075e),
           "Discord should remain title-flexible");

    SavedWindowEntry savedCode = saved(
        QStringLiteral("window:vscode"),
        vscode(hwnd(0xc5065c),
               QStringLiteral("Release Notes: 1.124.0 - greenhouse - Visual Studio Code")),
        hwnd(0xc5065c),
        positionOn(sideMonitor, -8, -8, 1936, 1056, true));
    QList<WindowInfo> restartedCode = {
        vscode(hwnd(0x750046), QStringLiteral("greenhouse - Visual Studio Code")),
    };
    assignments = WindowMatcher::matchSavedWindows({savedCode}, restartedCode, {});
    expect(assignmentFor(assignments, QStringLiteral("window:vscode")) == hwnd(0x750046),
           "a single restarted VS Code window should rematch after its title and HWND change");

    QList<WindowInfo> twoCodeWindows = {
        vscode(hwnd(0x750046), QStringLiteral("greenhouse - Visual Studio Code")),
        vscode(hwnd(0x750047), QStringLiteral("todo - Visual Studio Code")),
    };
    assignments = WindowMatcher::matchSavedWindows({savedCode}, twoCodeWindows, {});
    expect(assignments.isEmpty(),
           "one saved VS Code slot must not guess between two running VS Code windows");

    SavedWindowEntry savedCodeTwo = saved(
        QStringLiteral("window:vscode-two"),
        vscode(hwnd(0xc5065d), QStringLiteral("notes - Visual Studio Code")),
        hwnd(0xc5065d),
        positionOn(mainMonitor, 10, 10, 1200, 900, false));
    assignments = WindowMatcher::matchSavedWindows({savedCode, savedCodeTwo}, restartedCode, {});
    expect(assignments.isEmpty(),
           "one running VS Code window must not guess between two saved VS Code slots");

    assignments = WindowMatcher::matchSavedWindows({savedCode, savedCodeTwo}, twoCodeWindows, {});
    expect(assignments.isEmpty(),
           "changed titles across duplicate VS Code windows must remain unmatched without durable identity");

    if (failures > 0) {
        qCritical() << failures << "window matcher test(s) failed.";
        return 1;
    }

    qInfo() << "All window matcher tests passed.";
    return 0;
}

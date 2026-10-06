#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <windows.h>

#include "BrowserFingerprint.h"
#include "SavedWindowEntry.h"
#include "WindowInfo.h"
#include "WindowPosition.h"

namespace WindowMatcher {

struct Assignment {
    QString entryId;
    HWND hwnd = nullptr;
    QString reason;
    int score = 0;
};

bool sameExecutableAndClass(const WindowInfo& saved, const WindowInfo& current);
bool matchesSavedWindow(const WindowInfo& saved, const WindowInfo& current);
bool isBrowserLikeWindow(const WindowInfo& info);

QList<Assignment> matchSavedWindows(const QList<SavedWindowEntry>& savedEntries,
                                    const QList<WindowInfo>& currentWindows,
                                    const QHash<HWND, WindowPosition>& currentPositions,
                                    const QHash<HWND, BrowserFingerprint>& currentFingerprints = QHash<HWND, BrowserFingerprint>());

} // namespace WindowMatcher

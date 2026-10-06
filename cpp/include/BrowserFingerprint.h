#pragma once

#include <QString>
#include <QStringList>
#include <windows.h>

#include "WindowInfo.h"

struct BrowserFingerprint {
    bool isValid = false;
    QString browserFamily;
    QString activeTitleHash;
    QStringList tabTitleHashes;
    QStringList pinnedTabTitleHashes;
    int tabCount = 0;
    int pinnedTabCount = 0;

    bool hasTabSignals() const
    {
        return isValid && (!tabTitleHashes.isEmpty() || !pinnedTabTitleHashes.isEmpty());
    }
};

namespace BrowserFingerprinting {

bool isSupportedBrowser(const WindowInfo& info);
QString browserFamilyForProcess(const QString& processName);
QString normalizeTitle(QString title);
QString hashNormalizedTitle(const QString& normalizedTitle);
BrowserFingerprint fromTabTitles(const WindowInfo& info,
                                  const QStringList& tabTitles,
                                  const QString& selectedTabTitle = QString());
BrowserFingerprint collectForWindow(HWND hwnd, const WindowInfo& info);

} // namespace BrowserFingerprinting

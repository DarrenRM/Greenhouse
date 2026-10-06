#include "../include/WindowMatcher.h"

#include <QSet>
#include <algorithm>
#include <cmath>

namespace {

quintptr hwndValue(HWND hwnd)
{
    return reinterpret_cast<quintptr>(hwnd);
}

QString normalizedProcessName(const WindowInfo& info)
{
    return info.processName.toLower();
}

QString windowGroupKey(const WindowInfo& info)
{
    const QString processIdentity = info.processPath.isEmpty()
        ? info.processName.toLower()
        : info.processPath.toLower();
    return QStringLiteral("%1|%2").arg(processIdentity, info.className);
}

int positiveScore(int value)
{
    return value > 0 ? value : 0;
}

bool isTitleFlexibleProcess(const WindowInfo& info)
{
    const QString processName = normalizedProcessName(info);
    return processName == QLatin1String("spotify.exe") ||
           processName == QLatin1String("discord.exe");
}

bool titleMatches(const WindowInfo& saved, const WindowInfo& current)
{
    return !saved.title.isEmpty() && saved.title == current.title;
}

QSet<QString> stringSet(const QStringList& values)
{
    QSet<QString> set;
    for (const QString& value : values) {
        if (!value.isEmpty()) {
            set.insert(value);
        }
    }
    return set;
}

int overlapCount(const QStringList& a, const QStringList& b)
{
    const QSet<QString> left = stringSet(a);
    const QSet<QString> right = stringSet(b);
    int overlap = 0;
    for (const QString& value : left) {
        if (right.contains(value)) {
            ++overlap;
        }
    }
    return overlap;
}

int unionCount(const QStringList& a, const QStringList& b)
{
    QSet<QString> set = stringSet(a);
    for (const QString& value : b) {
        if (!value.isEmpty()) {
            set.insert(value);
        }
    }
    return set.size();
}

int browserFingerprintScore(const BrowserFingerprint& saved, const BrowserFingerprint& current)
{
    if (!saved.hasTabSignals() || !current.hasTabSignals()) {
        return -1;
    }
    if (!saved.browserFamily.isEmpty() &&
        !current.browserFamily.isEmpty() &&
        saved.browserFamily != current.browserFamily) {
        return -1;
    }

    const int tabOverlap = overlapCount(saved.tabTitleHashes, current.tabTitleHashes);
    const int pinnedOverlap = overlapCount(saved.pinnedTabTitleHashes, current.pinnedTabTitleHashes);
    const bool activeMatches = !saved.activeTitleHash.isEmpty() &&
                               saved.activeTitleHash == current.activeTitleHash;

    if (pinnedOverlap == 0 && tabOverlap < 2) {
        return -1;
    }

    int score = 0;
    score += pinnedOverlap * 15000;
    score += tabOverlap * 8000;
    if (activeMatches) {
        score += 12000;
    }

    const int tabUnion = unionCount(saved.tabTitleHashes, current.tabTitleHashes);
    if (tabUnion > 0) {
        score += (24000 * tabOverlap) / tabUnion;
    }

    score += positiveScore(3000 - (std::abs(saved.tabCount - current.tabCount) * 350));
    score += positiveScore(2500 - (std::abs(saved.pinnedTabCount - current.pinnedTabCount) * 750));

    return score >= 24000 ? score : -1;
}

int geometryAffinityScore(const WindowPosition& saved, const WindowPosition& current)
{
    int score = 0;

    if (!saved.monitorDeviceName.isEmpty() &&
        saved.monitorDeviceName.compare(current.monitorDeviceName, Qt::CaseInsensitive) == 0) {
        score += 1200;
    }

    if (saved.isMaximized == current.isMaximized) {
        score += 250;
    }

    if (saved.width > 0 && current.width > 0) {
        int sizeDelta = std::abs(saved.width - current.width) + std::abs(saved.height - current.height);
        score += positiveScore(250 - (sizeDelta / 20));
    }

    int positionDelta = std::abs(saved.left - current.left) + std::abs(saved.top - current.top);
    score += positiveScore(300 - (positionDelta / 10));

    return score;
}

int singletonBrowserFallbackScore(const SavedWindowEntry& entry,
                                  const WindowInfo& current,
                                  const QHash<HWND, WindowPosition>& currentPositions)
{
    if (!WindowMatcher::sameExecutableAndClass(entry.info, current)) {
        return -1;
    }

    int score = 1000;
    if (titleMatches(entry.info, current)) {
        score += 100000;
    }
    if (entry.currentHwnd && entry.currentHwnd == current.hwnd) {
        score += 10000;
    }

    const auto posIt = currentPositions.constFind(current.hwnd);
    if (posIt != currentPositions.constEnd()) {
        score += geometryAffinityScore(entry.position, posIt.value());
    }

    return score;
}

int savedBrowserGroupCount(const QList<SavedWindowEntry>& entries, const SavedWindowEntry& targetEntry)
{
    int count = 0;
    const QString targetGroupKey = windowGroupKey(targetEntry.info);
    for (const SavedWindowEntry& entry : entries) {
        if (WindowMatcher::isBrowserLikeWindow(entry.info) &&
            windowGroupKey(entry.info) == targetGroupKey) {
            ++count;
        }
    }
    return count;
}

int currentBrowserGroupCount(const QList<WindowInfo>& windows, const SavedWindowEntry& targetEntry)
{
    int count = 0;
    const QString targetGroupKey = windowGroupKey(targetEntry.info);
    for (const WindowInfo& current : windows) {
        if (WindowMatcher::isBrowserLikeWindow(current) &&
            windowGroupKey(current) == targetGroupKey) {
            ++count;
        }
    }
    return count;
}

int exactTitleCandidateCount(const QList<WindowInfo>& windows,
                             const SavedWindowEntry& targetEntry,
                             const QSet<HWND>& assignedHwnds)
{
    int count = 0;
    for (const WindowInfo& current : windows) {
        if (!assignedHwnds.contains(current.hwnd) &&
            WindowMatcher::sameExecutableAndClass(targetEntry.info, current) &&
            titleMatches(targetEntry.info, current)) {
            ++count;
        }
    }
    return count;
}

struct CandidatePair {
    QString entryId;
    HWND hwnd = nullptr;
    int score = 0;
    QString reason;
};

} // namespace

namespace WindowMatcher {

bool sameExecutableAndClass(const WindowInfo& saved, const WindowInfo& current)
{
    const bool samePath = !saved.processPath.isEmpty() &&
                          !current.processPath.isEmpty() &&
                          saved.processPath.compare(current.processPath, Qt::CaseInsensitive) == 0;
    const bool sameName = !saved.processName.isEmpty() &&
                          saved.processName.compare(current.processName, Qt::CaseInsensitive) == 0;

    return (samePath || sameName) &&
           !saved.className.isEmpty() &&
           saved.className == current.className;
}

bool matchesSavedWindow(const WindowInfo& saved, const WindowInfo& current)
{
    if (!sameExecutableAndClass(saved, current)) {
        return false;
    }

    if (isTitleFlexibleProcess(saved)) {
        return true;
    }

    return titleMatches(saved, current);
}

bool isBrowserLikeWindow(const WindowInfo& info)
{
    const QString processName = normalizedProcessName(info);
    return processName == QLatin1String("brave.exe") ||
           processName == QLatin1String("chrome.exe") ||
           processName == QLatin1String("msedge.exe") ||
           processName == QLatin1String("firefox.exe") ||
           processName == QLatin1String("opera.exe") ||
           processName == QLatin1String("vivaldi.exe");
}

QList<Assignment> matchSavedWindows(const QList<SavedWindowEntry>& savedEntries,
                                    const QList<WindowInfo>& currentWindows,
                                    const QHash<HWND, WindowPosition>& currentPositions,
                                    const QHash<HWND, BrowserFingerprint>& currentFingerprints)
{
    QList<SavedWindowEntry> entries = savedEntries;
    std::sort(entries.begin(), entries.end(), [](const SavedWindowEntry& a, const SavedWindowEntry& b) {
        return a.id < b.id;
    });

    QList<WindowInfo> windows;
    for (const WindowInfo& current : currentWindows) {
        if (current.isActive && current.hwnd) {
            windows.append(current);
        }
    }
    std::sort(windows.begin(), windows.end(), [](const WindowInfo& a, const WindowInfo& b) {
        return hwndValue(a.hwnd) < hwndValue(b.hwnd);
    });

    QList<Assignment> assignments;
    QSet<QString> assignedEntries;
    QSet<HWND> assignedHwnds;

    auto assign = [&](const SavedWindowEntry& entry, HWND hwnd, const QString& reason, int score) {
        assignments.append({entry.id, hwnd, reason, score});
        assignedEntries.insert(entry.id);
        assignedHwnds.insert(hwnd);
    };

    auto findCurrentByHwnd = [&](HWND hwnd) -> const WindowInfo* {
        for (const WindowInfo& current : windows) {
            if (current.hwnd == hwnd) {
                return &current;
            }
        }
        return nullptr;
    };

    for (const SavedWindowEntry& entry : qAsConst(entries)) {
        if (!entry.currentHwnd) {
            continue;
        }

        const WindowInfo* current = findCurrentByHwnd(entry.currentHwnd);
        if (!current || assignedHwnds.contains(current->hwnd) ||
            !sameExecutableAndClass(entry.info, *current)) {
            continue;
        }

        const bool browserWindow = isBrowserLikeWindow(entry.info);
        const int fingerprintScore = browserFingerprintScore(
            entry.browserFingerprint,
            currentFingerprints.value(current->hwnd));
        const bool savedIdentityMatches = matchesSavedWindow(entry.info, *current);
        const bool duplicateBrowserGroup = browserWindow &&
                                           savedBrowserGroupCount(entries, entry) > 1 &&
                                           currentBrowserGroupCount(windows, entry) > 1;
        const bool uniqueExactBrowserTitle = browserWindow &&
                                             savedIdentityMatches &&
                                             exactTitleCandidateCount(windows, entry, assignedHwnds) == 1;
        const bool browserIdentityIsSafe = !browserWindow ||
                                           !duplicateBrowserGroup ||
                                           fingerprintScore > 0 ||
                                           uniqueExactBrowserTitle;

        if ((savedIdentityMatches && browserIdentityIsSafe) ||
            (!browserWindow && entry.currentHwndTrusted) ||
            (entry.currentHwndTrusted && browserIdentityIsSafe) ||
            fingerprintScore > 0) {
            assign(entry,
                   current->hwnd,
                   savedIdentityMatches
                       ? QStringLiteral("current hwnd and saved identity")
                       : (browserWindow && fingerprintScore > 0
                              ? QStringLiteral("current hwnd and browser tab fingerprint")
                              : (entry.currentHwndTrusted
                                     ? QStringLiteral("trusted current hwnd and same app/class")
                                     : QStringLiteral("current hwnd and same app/class"))),
                   200000 + positiveScore(fingerprintScore));
        }
    }

    for (const SavedWindowEntry& entry : qAsConst(entries)) {
        if (assignedEntries.contains(entry.id)) {
            continue;
        }

        QList<CandidatePair> candidates;
        for (const WindowInfo& current : qAsConst(windows)) {
            if (assignedHwnds.contains(current.hwnd) || !matchesSavedWindow(entry.info, current)) {
                continue;
            }

            int score = titleMatches(entry.info, current) ? 150000 : 120000;
            if (entry.currentHwnd == current.hwnd) {
                score += 10000;
            }
            const int fingerprintScore = browserFingerprintScore(
                entry.browserFingerprint,
                currentFingerprints.value(current.hwnd));
            if (fingerprintScore > 0) {
                score += fingerprintScore;
            }

            candidates.append({entry.id, current.hwnd, score, QStringLiteral("saved identity")});
        }

        if (candidates.isEmpty()) {
            continue;
        }

        std::sort(candidates.begin(), candidates.end(), [](const CandidatePair& a, const CandidatePair& b) {
            if (a.score != b.score) {
                return a.score > b.score;
            }
            return hwndValue(a.hwnd) < hwndValue(b.hwnd);
        });

        const CandidatePair best = candidates.first();
        const bool ambiguousIdentity = candidates.size() > 1 &&
                                       best.score < candidates.at(1).score + 9000;
        if (!ambiguousIdentity) {
            assign(entry, best.hwnd, best.reason, best.score);
        }
    }

    // Titles and HWNDs are volatile for many desktop applications. Accept an
    // app/class fallback only when the remaining relationship is exactly 1:1.
    // This reconnects a restarted editor safely without guessing among
    // multiple windows from the same application.
    for (const SavedWindowEntry& entry : qAsConst(entries)) {
        if (assignedEntries.contains(entry.id) || isBrowserLikeWindow(entry.info)) {
            continue;
        }

        int unmatchedSameAppEntries = 0;
        for (const SavedWindowEntry& otherEntry : qAsConst(entries)) {
            if (!assignedEntries.contains(otherEntry.id) &&
                !isBrowserLikeWindow(otherEntry.info) &&
                sameExecutableAndClass(entry.info, otherEntry.info)) {
                ++unmatchedSameAppEntries;
            }
        }

        QList<WindowInfo> sameAppCandidates;
        for (const WindowInfo& current : qAsConst(windows)) {
            if (!assignedHwnds.contains(current.hwnd) &&
                sameExecutableAndClass(entry.info, current)) {
                sameAppCandidates.append(current);
            }
        }

        if (unmatchedSameAppEntries == 1 && sameAppCandidates.size() == 1) {
            const WindowInfo current = sameAppCandidates.first();
            int score = 50000;
            const auto posIt = currentPositions.constFind(current.hwnd);
            if (posIt != currentPositions.constEnd()) {
                score += geometryAffinityScore(entry.position, posIt.value());
            }
            assign(entry,
                   current.hwnd,
                   QStringLiteral("unique app/class singleton"),
                   score);
        }
    }

    QHash<QString, SavedWindowEntry> entryById;
    for (const SavedWindowEntry& entry : qAsConst(entries)) {
        entryById.insert(entry.id, entry);
    }

    QList<CandidatePair> browserFingerprintPairs;
    for (const SavedWindowEntry& entry : qAsConst(entries)) {
        if (assignedEntries.contains(entry.id) || !isBrowserLikeWindow(entry.info)) {
            continue;
        }

        for (const WindowInfo& current : qAsConst(windows)) {
            if (assignedHwnds.contains(current.hwnd) ||
                !sameExecutableAndClass(entry.info, current)) {
                continue;
            }

            const int fingerprintScore = browserFingerprintScore(
                entry.browserFingerprint,
                currentFingerprints.value(current.hwnd));
            if (fingerprintScore <= 0) {
                continue;
            }

            int score = 100000 + fingerprintScore;
            if (entry.currentHwnd == current.hwnd) {
                score += 10000;
            }
            const auto posIt = currentPositions.constFind(current.hwnd);
            if (posIt != currentPositions.constEnd()) {
                score += geometryAffinityScore(entry.position, posIt.value()) / 10;
            }
            browserFingerprintPairs.append({
                entry.id,
                current.hwnd,
                score,
                QStringLiteral("browser tab fingerprint")
            });
        }
    }

    std::sort(browserFingerprintPairs.begin(), browserFingerprintPairs.end(), [](const CandidatePair& a, const CandidatePair& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        if (a.entryId != b.entryId) {
            return a.entryId < b.entryId;
        }
        return hwndValue(a.hwnd) < hwndValue(b.hwnd);
    });

    auto secondBestEntryScore = [&](const CandidatePair& bestPair) {
        int second = -1;
        for (const CandidatePair& pair : qAsConst(browserFingerprintPairs)) {
            if (pair.entryId == bestPair.entryId &&
                pair.hwnd != bestPair.hwnd &&
                !assignedHwnds.contains(pair.hwnd)) {
                second = pair.score > second ? pair.score : second;
            }
        }
        return second;
    };

    auto secondBestHwndScore = [&](const CandidatePair& bestPair) {
        int second = -1;
        for (const CandidatePair& pair : qAsConst(browserFingerprintPairs)) {
            if (pair.hwnd == bestPair.hwnd &&
                pair.entryId != bestPair.entryId &&
                !assignedEntries.contains(pair.entryId)) {
                second = pair.score > second ? pair.score : second;
            }
        }
        return second;
    };

    for (const CandidatePair& pair : qAsConst(browserFingerprintPairs)) {
        if (assignedEntries.contains(pair.entryId) || assignedHwnds.contains(pair.hwnd)) {
            continue;
        }

        const int entryRunnerUp = secondBestEntryScore(pair);
        const int hwndRunnerUp = secondBestHwndScore(pair);
        if ((entryRunnerUp >= 0 && pair.score < entryRunnerUp + 9000) ||
            (hwndRunnerUp >= 0 && pair.score < hwndRunnerUp + 9000)) {
            continue;
        }

        assign(entryById.value(pair.entryId), pair.hwnd, pair.reason, pair.score);
    }

    for (const SavedWindowEntry& entry : qAsConst(entries)) {
        if (assignedEntries.contains(entry.id) || !isBrowserLikeWindow(entry.info)) {
            continue;
        }

        int unmatchedSameBrowserEntries = 0;
        for (const SavedWindowEntry& otherEntry : qAsConst(entries)) {
            if (!assignedEntries.contains(otherEntry.id) &&
                isBrowserLikeWindow(otherEntry.info) &&
                sameExecutableAndClass(entry.info, otherEntry.info)) {
                ++unmatchedSameBrowserEntries;
            }
        }

        QList<WindowInfo> sameBrowserCandidates;
        for (const WindowInfo& current : qAsConst(windows)) {
            if (!assignedHwnds.contains(current.hwnd) &&
                sameExecutableAndClass(entry.info, current)) {
                sameBrowserCandidates.append(current);
            }
        }

        if (unmatchedSameBrowserEntries == 1 && sameBrowserCandidates.size() == 1) {
            const WindowInfo current = sameBrowserCandidates.first();
            const int score = singletonBrowserFallbackScore(entry, current, currentPositions);
            if (score > 0) {
                assign(entry,
                       current.hwnd,
                       QStringLiteral("single browser window by app/class"),
                       score);
            }
        }
    }

    return assignments;
}

} // namespace WindowMatcher

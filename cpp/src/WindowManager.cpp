#include "../include/WindowManager.h"
#include "../include/BrowserFingerprint.h"
#include "../include/LayoutGeometry.h"
#include "../include/Win32Helper.h"
#include "../include/WindowMatcher.h"
#include <windows.h>
#include <dwmapi.h>
#include <psapi.h>
#include <shellapi.h> // For SHGetFileInfo
#include <ShellScalingApi.h> // For GetDpiForMonitor

#include <QDebug>
#include <QCryptographicHash>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <cmath>
#include <stdexcept> // For error handling if needed

// Define minimum Windows version for DPI functions (Win 8.1)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0603
#endif

// Helper function to format GetLastError messages
QString GetLastErrorString(DWORD errorCode)
{
    LPWSTR buffer = nullptr;
    DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&buffer, 0, NULL);

    QString message;
    if (size > 0) {
        message = QString::fromWCharArray(buffer, size);
        LocalFree(buffer);
        // Remove trailing newline characters if present
        while (!message.isEmpty() && (message.endsWith(QLatin1Char('\n')) || message.endsWith(QLatin1Char('\r')))) {
            message.chop(1);
        }
    } else {
        message = QStringLiteral("Unknown error code.");
    }
    return message;
}

static bool setMaximizedStateWithoutForegroundCommand(HWND hwnd)
{
    if (!IsWindow(hwnd)) {
        return false;
    }

    const HWND foregroundBefore = GetForegroundWindow();

    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);
    if (!GetWindowPlacement(hwnd, &placement)) {
        return false;
    }

    placement.flags &= ~WPF_ASYNCWINDOWPLACEMENT;
    placement.showCmd = SW_SHOWMAXIMIZED;
    const BOOL applied = SetWindowPlacement(hwnd, &placement);

    const HWND foregroundAfter = GetForegroundWindow();
    if (foregroundBefore != foregroundAfter) {
        qInfo() << "Maximized state update changed foreground for HWND:" << hwnd
                << "previous foreground:" << foregroundBefore
                << "new foreground:" << foregroundAfter;
    }

    return applied != FALSE;
}

struct ActiveMonitor {
    HMONITOR handle = nullptr;
    MONITORINFOEXW info = {};
    MonitorSnapshot snapshot;
};

static QHash<QString, QString> stableMonitorIdsByDeviceName()
{
    QHash<QString, QString> ids;
    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    QVector<DISPLAYCONFIG_PATH_INFO> paths;
    QVector<DISPLAYCONFIG_MODE_INFO> modes;
    LONG result = ERROR_INSUFFICIENT_BUFFER;
    for (int attempt = 0; attempt < 3 && result == ERROR_INSUFFICIENT_BUFFER; ++attempt) {
        result = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
        if (result != ERROR_SUCCESS) {
            qWarning() << "GetDisplayConfigBufferSizes failed:" << result;
            return ids;
        }

        paths.resize(static_cast<qsizetype>(pathCount));
        modes.resize(static_cast<qsizetype>(modeCount));
        result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,
                                    &pathCount,
                                    paths.data(),
                                    &modeCount,
                                    modes.data(),
                                    nullptr);
    }
    if (result != ERROR_SUCCESS) {
        qWarning() << "QueryDisplayConfig failed after retries:" << result;
        return ids;
    }

    for (UINT32 index = 0; index < pathCount; ++index) {
        const DISPLAYCONFIG_PATH_INFO& path = paths.at(static_cast<qsizetype>(index));

        DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id = path.sourceInfo.id;

        DISPLAYCONFIG_TARGET_DEVICE_NAME target = {};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = path.targetInfo.adapterId;
        target.header.id = path.targetInfo.id;

        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
            DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) {
            continue;
        }

        const QString deviceName = QString::fromWCharArray(source.viewGdiDeviceName);
        const QString stableId = QString::fromWCharArray(target.monitorDevicePath);
        if (!deviceName.isEmpty() && !stableId.isEmpty()) {
            ids.insert(deviceName.toLower(), stableId);
        }
    }
    return ids;
}

struct ActiveMonitorEnumeration {
    QList<ActiveMonitor> monitors;
    QHash<QString, QString> stableIds;
};

BOOL CALLBACK CollectActiveMonitorProc(HMONITOR monitor, HDC, LPRECT, LPARAM lParam)
{
    auto* enumeration = reinterpret_cast<ActiveMonitorEnumeration*>(lParam);
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        return TRUE;
    }

    ActiveMonitor active;
    active.handle = monitor;
    active.info = info;
    active.snapshot.deviceName = QString::fromWCharArray(info.szDevice);
    active.snapshot.workArea = QRect(info.rcWork.left,
                                     info.rcWork.top,
                                     info.rcWork.right - info.rcWork.left,
                                     info.rcWork.bottom - info.rcWork.top);
    UINT dpiX = 96;
    UINT dpiY = 96;
    if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
        active.snapshot.dpiScale = static_cast<double>(dpiX) / 96.0;
    }
    active.snapshot.stableId = enumeration->stableIds.value(active.snapshot.deviceName.toLower());
    enumeration->monitors.append(active);
    return TRUE;
}

static QList<ActiveMonitor> enumerateActiveMonitors()
{
    ActiveMonitorEnumeration enumeration;
    enumeration.stableIds = stableMonitorIdsByDeviceName();
    if (!EnumDisplayMonitors(nullptr,
                             nullptr,
                             CollectActiveMonitorProc,
                             reinterpret_cast<LPARAM>(&enumeration))) {
        qWarning() << "EnumDisplayMonitors failed:" << GetLastError();
    }
    return enumeration.monitors;
}

static const ActiveMonitor* findActiveMonitor(const QList<ActiveMonitor>& monitors,
                                              const QString& deviceName)
{
    for (const ActiveMonitor& monitor : monitors) {
        if (monitor.snapshot.deviceName.compare(deviceName, Qt::CaseInsensitive) == 0) {
            return &monitor;
        }
    }
    return nullptr;
}

static QRect currentNormalScreenRect(HWND hwnd)
{
    RECT currentRect = {};
    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);
    if (!GetWindowRect(hwnd, &currentRect) || !GetWindowPlacement(hwnd, &placement)) {
        return QRect();
    }

    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)) {
        return QRect();
    }

    return LayoutGeometry::captureRectForState(
        QRect(currentRect.left,
              currentRect.top,
              currentRect.right - currentRect.left,
              currentRect.bottom - currentRect.top),
        QRect(placement.rcNormalPosition.left,
              placement.rcNormalPosition.top,
              placement.rcNormalPosition.right - placement.rcNormalPosition.left,
              placement.rcNormalPosition.bottom - placement.rcNormalPosition.top),
        placement.showCmd == SW_SHOWMAXIMIZED,
        placement.showCmd == SW_SHOWMINIMIZED || placement.showCmd == SW_MINIMIZE,
        QRect(monitorInfo.rcMonitor.left,
              monitorInfo.rcMonitor.top,
              monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
              monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top),
        QRect(monitorInfo.rcWork.left,
              monitorInfo.rcWork.top,
              monitorInfo.rcWork.right - monitorInfo.rcWork.left,
              monitorInfo.rcWork.bottom - monitorInfo.rcWork.top));
}

// Constructor
WindowManager::WindowManager(QObject *parent)
    : QObject(parent)
{
    qInfo() << "WindowManager initialized";
    // Initialization code here (e.g., load initial saved positions later)
}

// Load initial data (called once at startup)
void WindowManager::loadInitialData(const QList<SavedWindowEntry>& entries) {
     m_savedEntries.clear();
     m_hwndToEntryId.clear();
     m_lastReconciliationStatus.clear();

     for (SavedWindowEntry entry : entries) {
         if (entry.id.isEmpty() || entry.id.at(0).isDigit()) {
             entry.id = makeStableWindowId(entry.info);
         }
         entry.info.isActive = false;
         entry.isActive = false;
         entry.currentHwndTrusted = false;
         m_savedEntries.insert(entry.id, entry);
     }

     rebuildLegacySavedMaps();
     qInfo() << "WindowManager loaded initial data:" << m_savedEntries.size() << "layout entries.";
}

// Destructor
WindowManager::~WindowManager() {
    qInfo() << "WindowManager destroyed";
}

// --- Public Methods ---

void WindowManager::enumerateWindows(bool collectBrowserFingerprints) {
    m_enumResult.clear(); // Clear previous results before enumerating
    EnumWindows(&WindowManager::EnumWindowsProc, reinterpret_cast<LPARAM>(this));

    reconcileSavedEntriesWithCurrentWindows(collectBrowserFingerprints);

    for (WindowInfo& info : m_enumResult) {
        info.isSaved = false;
        info.savedHasRunningCandidates = false;
        info.savedMonitorDeviceName.clear();
        info.savedMonitorStableId.clear();
        info.savedMonitorWorkLeft = 0;
        info.savedMonitorWorkTop = 0;
        info.savedMonitorWorkWidth = 0;
        info.savedMonitorWorkHeight = 0;
        info.savedIsMaximized = false;
        info.savedIsMinimized = false;
    }

    for (const SavedWindowEntry& entry : qAsConst(m_savedEntries)) {
        if (!entry.isActive || !entry.currentHwnd) {
            continue;
        }
        for (WindowInfo& info : m_enumResult) {
            if (info.hwnd == entry.currentHwnd) {
                info.stableId = entry.id;
                info.isSaved = true;
                info.savedMonitorDeviceName = entry.position.monitorDeviceName;
                info.savedMonitorStableId = entry.position.monitorStableId;
                info.savedMonitorWorkLeft = entry.position.monitorWorkLeft;
                info.savedMonitorWorkTop = entry.position.monitorWorkTop;
                info.savedMonitorWorkWidth = entry.position.monitorWorkWidth;
                info.savedMonitorWorkHeight = entry.position.monitorWorkHeight;
                info.savedLeft = entry.position.left;
                info.savedTop = entry.position.top;
                info.savedIsMaximized = entry.position.isMaximized;
                info.savedIsMinimized = entry.position.isMinimized;
                break;
            }
        }
    }

    for (const SavedWindowEntry& entry : qAsConst(m_savedEntries)) {
        if (!entry.isActive) {
            WindowInfo inactiveInfo = entry.info;
            inactiveInfo.hwnd = entry.currentHwnd;
            inactiveInfo.stableId = entry.id;
            inactiveInfo.isActive = false;
            inactiveInfo.isSaved = true;
            inactiveInfo.savedHasRunningCandidates = entry.info.savedHasRunningCandidates;
            inactiveInfo.savedMonitorDeviceName = entry.position.monitorDeviceName;
            inactiveInfo.savedMonitorStableId = entry.position.monitorStableId;
            inactiveInfo.savedMonitorWorkLeft = entry.position.monitorWorkLeft;
            inactiveInfo.savedMonitorWorkTop = entry.position.monitorWorkTop;
            inactiveInfo.savedMonitorWorkWidth = entry.position.monitorWorkWidth;
            inactiveInfo.savedMonitorWorkHeight = entry.position.monitorWorkHeight;
            inactiveInfo.savedLeft = entry.position.left;
            inactiveInfo.savedTop = entry.position.top;
            inactiveInfo.savedIsMaximized = entry.position.isMaximized;
            inactiveInfo.savedIsMinimized = entry.position.isMinimized;
            m_enumResult.append(inactiveInfo);
        }
    }

    for (WindowInfo& windowInfo : m_enumResult) {
        windowInfo.iconKey = iconKeyForWindow(windowInfo);
        if (m_iconCache.contains(windowInfo.iconKey)) {
            windowInfo.icon = m_iconCache.value(windowInfo.iconKey);
        } else {
            windowInfo.icon = loadWindowIcon(windowInfo);
            if (windowInfo.icon.isNull()) {
                windowInfo.icon = defaultWindowIcon();
            }
            m_iconCache.insert(windowInfo.iconKey, windowInfo.icon);
        }
    }

    emit windowsEnumerated(m_enumResult);
    qInfo() << "Enumerated" << m_enumResult.size() << "interesting windows with cached icons.";
}

// --- Private Helper Methods ---

// Static callback function for EnumWindows
BOOL CALLBACK WindowManager::EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    // Cast lParam back to the WindowManager instance pointer
    WindowManager* instance = reinterpret_cast<WindowManager*>(lParam);
    if (instance == nullptr) {
        return FALSE; // Stop enumeration if instance is somehow null
    }

    // Check if the window is interesting using the instance method
    if (instance->isWindowInteresting(hwnd)) {
        // Get window info using the instance method
        WindowInfo info = instance->getWindowInfo(hwnd);
        // Add to the temporary list in the instance
        instance->m_enumResult.append(info);
    }

    return TRUE; // Continue enumeration
}

// Check if a window should be tracked
bool WindowManager::isWindowInteresting(HWND hwnd) {
    if (!IsWindow(hwnd)) {
        return false;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId == GetCurrentProcessId()) {
        return false;
    }
    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) {
        return false;
    }
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) {
        return false;
    }
    // Skip invisible windows
    if (!IsWindowVisible(hwnd)) {
        return false;
    }
    // Skip windows without titles
    if (GetWindowTextLengthW(hwnd) == 0) {
        return false;
    }
    // Add more conditions if needed (e.g., skip tool windows, etc.)

    // Example: Skip specific known problematic windows (like Task Manager when elevated?)
    // QString className = Win32Helper::GetWindowClassNameString(hwnd);
    // if (className == QLatin1String("TaskmgrSecurityUIAccessWnd")) {
    //     return false;
    // }

    return true;
}

// Get combined information for a window
WindowInfo WindowManager::getWindowInfo(HWND hwnd) {
    WindowInfo info;
    info.hwnd = hwnd;
    info.title = Win32Helper::GetWindowTextString(hwnd);
    info.className = Win32Helper::GetWindowClassNameString(hwnd);
    info.processName = Win32Helper::GetProcessName(hwnd);
    info.processPath = Win32Helper::GetProcessPath(hwnd);
    info.stableId = QStringLiteral("hwnd:%1").arg(reinterpret_cast<quintptr>(hwnd));
    info.iconKey = iconKeyForWindow(info);
    info.isActive = true; // Assume active for now, state checking comes later

    return info;
}

// --- Getters for Saved Data ---
const QHash<HWND, WindowPosition>& WindowManager::getSavedPositions() const {
    return m_savedPositions;
}

const QHash<HWND, WindowInfo>& WindowManager::getSavedWindowInfo() const {
    return m_savedWindowInfo;
}

QList<SavedWindowEntry> WindowManager::getSavedEntries() const {
    return m_savedEntries.values();
}

bool WindowManager::areAllSavedMonitorsAvailable() const {
    if (m_savedEntries.isEmpty()) {
        return false;
    }

    QList<MonitorSnapshot> snapshots;
    for (const ActiveMonitor& monitor : enumerateActiveMonitors()) {
        snapshots.append(monitor.snapshot);
    }
    if (snapshots.isEmpty()) {
        return false;
    }

    for (const SavedWindowEntry& entry : m_savedEntries) {
        if (!LayoutGeometry::planRestore(entry.position, snapshots).canRestore) {
            return false;
        }
    }
    return true;
}

const QList<WindowInfo>& WindowManager::getLastEnumeratedWindows() const {
    // Returns the result stored during the last call to enumerateWindows()
    // Note: This might be slightly out of date depending on timing.
    return m_enumResult;
}

// --- Implementations for Position Saving --- 

double WindowManager::getMonitorDpi(HWND hwnd) {
    if (!hwnd) return 1.0;
    if (!IsWindow(hwnd)) { // Added check
         // qWarning() << "getMonitorDpi: Invalid window handle:" << hwnd;
         return 1.0;
    }
    
    // Geometry is captured and restored in the per-monitor-aware Greenhouse
    // thread's coordinate space. Use the physical monitor DPI rather than the
    // target application's awareness-dependent GetDpiForWindow value.
    HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (hMonitor) {
        UINT dpiX, dpiY;
        // GetDpiForMonitor requires Windows 8.1+
        HRESULT hr = GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
        if (SUCCEEDED(hr)) {
            return static_cast<double>(dpiX) / 96.0;
        }
         qWarning() << "GetDpiForMonitor failed for HWND:" << hwnd;
    }

    // Final fallback if everything fails
    return 1.0; 
}

WindowPosition WindowManager::getWindowPositionInfo(HWND hwnd) {
     WindowPosition posInfo;
     if (!IsWindow(hwnd)) {
         qWarning() << "getWindowPositionInfo called with invalid HWND:" << hwnd;
         return posInfo; // Return default (empty)
     }

     RECT currentRect = {};
     if (!GetWindowRect(hwnd, &currentRect)) {
         DWORD error = GetLastError();
         qWarning() << "GetWindowRect failed for HWND:" << hwnd << "Error:" << error 
                    << "(" << GetLastErrorString(error) << ")";
         return posInfo;
     }

     WINDOWPLACEMENT placement = {};
     placement.length = sizeof(placement);
     const bool hasPlacement = GetWindowPlacement(hwnd, &placement) != FALSE;
     if (hasPlacement) {
          posInfo.isMaximized = placement.showCmd == SW_SHOWMAXIMIZED;
          posInfo.isMinimized = placement.showCmd == SW_SHOWMINIMIZED || placement.showCmd == SW_MINIMIZE;
     }

     HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
     MONITORINFOEXW monitorInfo = {};
     monitorInfo.cbSize = sizeof(monitorInfo);
     if (monitor && GetMonitorInfoW(monitor, &monitorInfo)) {
         const QRect currentScreenRect(currentRect.left,
                                       currentRect.top,
                                       currentRect.right - currentRect.left,
                                       currentRect.bottom - currentRect.top);
         const QRect normalWorkspaceRect(placement.rcNormalPosition.left,
                                         placement.rcNormalPosition.top,
                                         placement.rcNormalPosition.right - placement.rcNormalPosition.left,
                                         placement.rcNormalPosition.bottom - placement.rcNormalPosition.top);
         const QRect monitorArea(monitorInfo.rcMonitor.left,
                                 monitorInfo.rcMonitor.top,
                                 monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                                 monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top);
         const QRect workArea(monitorInfo.rcWork.left,
                              monitorInfo.rcWork.top,
                              monitorInfo.rcWork.right - monitorInfo.rcWork.left,
                              monitorInfo.rcWork.bottom - monitorInfo.rcWork.top);
         const QRect rect = LayoutGeometry::captureRectForState(currentScreenRect,
                                                                 normalWorkspaceRect,
                                                                 posInfo.isMaximized,
                                                                 posInfo.isMinimized,
                                                                 monitorArea,
                                                                 workArea);

         posInfo.dpiScale = getMonitorDpi(hwnd);
         posInfo.monitorDeviceName = QString::fromWCharArray(monitorInfo.szDevice);
         posInfo.monitorWorkLeft = monitorInfo.rcWork.left;
         posInfo.monitorWorkTop = monitorInfo.rcWork.top;
         posInfo.monitorWorkWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
         posInfo.monitorWorkHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
         posInfo.monitorStableId = stableMonitorIdsByDeviceName().value(posInfo.monitorDeviceName.toLower());
         posInfo.isMonitorRelative = true;
         posInfo.hasNormalBounds = (!posInfo.isMaximized && !posInfo.isMinimized) ||
                                   (hasPlacement && normalWorkspaceRect.isValid());
         posInfo.left = static_cast<int>(round(static_cast<double>(rect.left() - monitorInfo.rcWork.left) / posInfo.dpiScale));
         posInfo.top = static_cast<int>(round(static_cast<double>(rect.top() - monitorInfo.rcWork.top) / posInfo.dpiScale));
         posInfo.width = static_cast<int>(round(static_cast<double>(rect.width()) / posInfo.dpiScale));
         posInfo.height = static_cast<int>(round(static_cast<double>(rect.height()) / posInfo.dpiScale));
     } else {
         // Legacy fallback: normalize absolute virtual-screen coordinates.
         posInfo.dpiScale = 1.0;
         posInfo.left = currentRect.left;
         posInfo.top = currentRect.top;
         posInfo.width = currentRect.right - currentRect.left;
         posInfo.height = currentRect.bottom - currentRect.top;
         posInfo.hasNormalBounds = !posInfo.isMaximized && !posInfo.isMinimized;
     }

     return posInfo;
}

void WindowManager::saveWindowPosition(HWND hwnd) {
    if (!IsWindow(hwnd)) { // Check if window is still valid
        qWarning() << "Attempted to save position for invalid HWND:" << hwnd;
        return;
    }

    WindowPosition currentPos = getWindowPositionInfo(hwnd);
    if (currentPos.width <= 0 || currentPos.height <= 0) { // Basic sanity check
         qWarning() << "Attempted to save invalid position for HWND:" << hwnd;
         return;
    }
    
    // Also get the full window info
    WindowInfo currentInfo = getWindowInfo(hwnd);

    // Store in memory. Some apps can expose multiple windows with the same
    // process/class/title, so keep duplicate identities as separate entries.
    const QString baseId = makeStableWindowId(currentInfo);
    QString id = m_hwndToEntryId.value(hwnd);
    if (id.isEmpty() || !m_savedEntries.contains(id)) {
        id = baseId;
        int duplicateIndex = 2;
        while (m_savedEntries.contains(id) && m_savedEntries.value(id).currentHwnd != hwnd) {
            id = QStringLiteral("%1#%2").arg(baseId).arg(duplicateIndex++);
        }
    }

    SavedWindowEntry entry;
    entry.id = id;
    entry.info = currentInfo;
    entry.position = currentPos;
    if (WindowMatcher::isBrowserLikeWindow(currentInfo)) {
        entry.browserFingerprint = BrowserFingerprinting::collectForWindow(hwnd, currentInfo);
        if (entry.browserFingerprint.hasTabSignals()) {
            m_browserFingerprintCache.insert(hwnd, entry.browserFingerprint);
        }
        qInfo() << "Saved browser fingerprint for HWND:" << hwnd
                << "valid:" << entry.browserFingerprint.isValid
                << "tabs:" << entry.browserFingerprint.tabCount
                << "pinned:" << entry.browserFingerprint.pinnedTabCount;
    }
    entry.currentHwnd = hwnd;
    entry.currentHwndTrusted = true;
    entry.isActive = true;
    m_savedEntries.insert(id, entry);
    rebuildLegacySavedMaps();
    qInfo() << "Saved position and info for HWND:" << hwnd << "Title:" << currentInfo.title;

    // Emit signal
    emit positionSaved(hwnd, currentPos);
}

int WindowManager::saveAllCurrentWindowPositions() {
    enumerateWindows(false);
    QList<SavedWindowEntry> entries = snapshotCurrentWindowsForCapture();
    if (entries.isEmpty()) {
        qInfo() << "Captured 0 active windows into the layout.";
        return 0;
    }

    for (SavedWindowEntry& entry : entries) {
        if (WindowMatcher::isBrowserLikeWindow(entry.info)) {
            entry.browserFingerprint = BrowserFingerprinting::collectForWindow(
                entry.currentHwnd,
                entry.info);
        }
    }
    const int savedCount = replaceSavedLayout(entries);
    qInfo() << "Captured" << savedCount << "active windows into the layout.";
    return savedCount;
}

QList<SavedWindowEntry> WindowManager::snapshotCurrentWindowsForCapture()
{
    QList<SavedWindowEntry> entries;
    QSet<QString> usedIds;

    for (const WindowInfo& currentInfo : qAsConst(m_enumResult)) {
        if (!currentInfo.isActive || !currentInfo.hwnd || !IsWindow(currentInfo.hwnd)) {
            continue;
        }

        const WindowPosition position = getWindowPositionInfo(currentInfo.hwnd);
        if (position.width <= 0 || position.height <= 0) {
            qWarning() << "Skipping invalid capture position for HWND:" << currentInfo.hwnd;
            continue;
        }

        const QString baseId = makeStableWindowId(currentInfo);
        QString id = baseId;
        int duplicateIndex = 2;
        while (usedIds.contains(id)) {
            id = QStringLiteral("%1#%2").arg(baseId).arg(duplicateIndex++);
        }
        usedIds.insert(id);

        SavedWindowEntry entry;
        entry.id = id;
        entry.info = currentInfo;
        entry.position = position;
        entry.currentHwnd = currentInfo.hwnd;
        entry.currentHwndTrusted = true;
        entry.isActive = true;
        entries.append(entry);
    }

    qInfo() << "Snapshotted" << entries.size()
            << "window geometries for asynchronous capture.";
    return entries;
}

int WindowManager::replaceSavedLayout(const QList<SavedWindowEntry>& entries)
{
    m_savedEntries.clear();
    m_savedPositions.clear();
    m_savedWindowInfo.clear();
    m_hwndToEntryId.clear();
    m_lastReconciliationStatus.clear();

    int savedCount = 0;
    for (SavedWindowEntry entry : entries) {
        if (entry.id.isEmpty() || !entry.currentHwnd || !IsWindow(entry.currentHwnd) ||
            entry.position.width <= 0 || entry.position.height <= 0) {
            continue;
        }
        entry.info.hwnd = entry.currentHwnd;
        entry.info.isActive = true;
        entry.currentHwndTrusted = true;
        entry.isActive = true;
        m_savedEntries.insert(entry.id, entry);
        if (entry.browserFingerprint.hasTabSignals()) {
            m_browserFingerprintCache.insert(entry.currentHwnd, entry.browserFingerprint);
        }
        emit positionSaved(entry.currentHwnd, entry.position);
        ++savedCount;
    }

    rebuildLegacySavedMaps();
    qInfo() << "Applied captured layout with" << savedCount << "windows.";
    return savedCount;
}

void WindowManager::updateBrowserFingerprintCache(
    const QHash<HWND, BrowserFingerprint>& fingerprints)
{
    int cachedCount = 0;
    for (auto it = fingerprints.constBegin(); it != fingerprints.constEnd(); ++it) {
        if (!it.key() || !IsWindow(it.key()) || !it.value().hasTabSignals()) {
            continue;
        }
        m_browserFingerprintCache.insert(it.key(), it.value());
        ++cachedCount;
    }
    qInfo() << "Updated browser identity cache with" << cachedCount << "windows.";
}

void WindowManager::removeSavedPosition(HWND hwnd) {
    const QString id = m_hwndToEntryId.value(hwnd);
    if (!id.isEmpty()) {
        removeSavedEntry(id);
    }
}

bool WindowManager::removeSavedEntry(const QString& entryId) {
    auto it = m_savedEntries.find(entryId);
    if (it == m_savedEntries.end()) {
        return false;
    }

    const HWND hwnd = it->currentHwnd;
    const QString title = it->info.title;
    m_savedEntries.erase(it);
    m_lastReconciliationStatus.remove(entryId);
    rebuildLegacySavedMaps();
    qInfo() << "Removed saved layout entry:"
            << entryId
            << "HWND:" << hwnd
            << "title:" << title;
    emit positionRemoved(hwnd);
    return true;
}


// --- Other Placeholders ---

bool WindowManager::restoreWindowPosition(HWND hwnd) {
    if (!m_savedPositions.contains(hwnd)) {
        QString id = m_hwndToEntryId.value(hwnd);
        if (!id.isEmpty() && m_savedEntries.contains(id)) {
            rebuildLegacySavedMaps();
        }
    }

    if (!m_savedPositions.contains(hwnd)) {
        qWarning() << "restoreWindowPosition called for HWND with no saved position:" << hwnd;
        return false;
    }
    if (!IsWindow(hwnd)) {
        qWarning() << "restoreWindowPosition called for invalid/closed HWND:" << hwnd;
        return false;
    }

    const WindowPosition savedPos = m_savedPositions.value(hwnd);
    const QList<ActiveMonitor> activeMonitors = enumerateActiveMonitors();
    QList<MonitorSnapshot> monitorSnapshots;
    for (const ActiveMonitor& monitor : activeMonitors) {
        monitorSnapshots.append(monitor.snapshot);
    }

    const RestorePlan restorePlan = LayoutGeometry::planRestore(savedPos, monitorSnapshots);
    if (!restorePlan.canRestore) {
        QStringList availableMonitors;
        for (const MonitorSnapshot& monitor : monitorSnapshots) {
            availableMonitors.append(QStringLiteral("%1 work=%2,%3 %4x%5 dpi=%6")
                                         .arg(monitor.deviceName)
                                         .arg(monitor.workArea.left())
                                         .arg(monitor.workArea.top())
                                         .arg(monitor.workArea.width())
                                         .arg(monitor.workArea.height())
                                         .arg(monitor.dpiScale));
        }
        qWarning() << "Skipping restore for HWND" << hwnd << ":" << restorePlan.reason
                   << "saved monitor:" << savedPos.monitorDeviceName
                   << "saved stable id:" << savedPos.monitorStableId
                   << "available monitors:" << availableMonitors;
        return false;
    }

    const ActiveMonitor* targetMonitor = findActiveMonitor(activeMonitors, restorePlan.targetDeviceName);
    if (!targetMonitor) {
        qWarning() << "Resolved monitor disappeared before restore for HWND:" << hwnd
                   << restorePlan.targetDeviceName;
        return false;
    }

    const double targetDpiScale = targetMonitor->snapshot.dpiScale;
    const QRect targetWorkArea = targetMonitor->snapshot.workArea;

    const bool stableIdentityChanged = !targetMonitor->snapshot.stableId.isEmpty() &&
        savedPos.monitorStableId != targetMonitor->snapshot.stableId;
    if (savedPos.isMonitorRelative &&
        (savedPos.monitorDeviceName.compare(targetMonitor->snapshot.deviceName, Qt::CaseInsensitive) != 0 ||
         stableIdentityChanged ||
         savedPos.monitorWorkLeft != targetWorkArea.left() ||
         savedPos.monitorWorkTop != targetWorkArea.top() ||
         savedPos.monitorWorkWidth != targetWorkArea.width() ||
         savedPos.monitorWorkHeight != targetWorkArea.height())) {
        WindowPosition migrated = savedPos;
        migrated.monitorDeviceName = targetMonitor->snapshot.deviceName;
        if (!targetMonitor->snapshot.stableId.isEmpty()) {
            migrated.monitorStableId = targetMonitor->snapshot.stableId;
        }
        migrated.monitorWorkLeft = targetWorkArea.left();
        migrated.monitorWorkTop = targetWorkArea.top();
        migrated.monitorWorkWidth = targetWorkArea.width();
        migrated.monitorWorkHeight = targetWorkArea.height();
        m_savedPositions.insert(hwnd, migrated);
        const QString entryId = m_hwndToEntryId.value(hwnd);
        if (!entryId.isEmpty() && m_savedEntries.contains(entryId)) {
            m_savedEntries[entryId].position = migrated;
            m_savedMetadataChangedDuringRestore = true;
        }
        qInfo() << "Migrated saved monitor binding for HWND:" << hwnd
                << savedPos.monitorDeviceName << "->" << migrated.monitorDeviceName
                << "via" << restorePlan.monitorMatchReason;
    }

    qInfo() << "Resolved target monitor for HWND:" << hwnd
            << restorePlan.targetDeviceName << "via" << restorePlan.monitorMatchReason;
    const QRect targetRect = LayoutGeometry::normalRectForRestore(
        savedPos, restorePlan.targetRect,
        savedPos.isMaximized && !savedPos.hasNormalBounds ? currentNormalScreenRect(hwnd) : QRect(),
        targetWorkArea);
    if (targetRect != restorePlan.targetRect) {
        qInfo() << "Repaired maximized normal bounds for HWND:" << hwnd
                << restorePlan.targetRect << "->" << targetRect
                << "target monitor:" << restorePlan.targetDeviceName;
    }

    const int targetLeft = targetRect.left();
    const int targetTop = targetRect.top();
    const int targetWidth = targetRect.width();
    const int targetHeight = targetRect.height();
    const double currentDpiScale = getMonitorDpi(hwnd);
    const bool crossesDpiScale = std::abs(currentDpiScale - targetDpiScale) > 0.01;
    const bool restoreAsMaximized = savedPos.isMaximized;
    const bool restoreAsMinimized = savedPos.isMinimized;

    if (IsZoomed(hwnd) || IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    } else {
        SetWindowPos(hwnd,
                     nullptr,
                     0,
                     0,
                     0,
                     0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    auto warnSetWindowPosFailure = [hwnd](const QString& action) {
        DWORD error = GetLastError();
        qWarning() << "SetWindowPos failed while" << action << "for HWND:" << hwnd << "Error:" << error
                   << "(" << GetLastErrorString(error) << ")";
    };

    if (crossesDpiScale) {
        if (!SetWindowPos(hwnd, nullptr, targetLeft, targetTop, 0, 0,
                          SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_NOSIZE)) {
            warnSetWindowPosFailure(QStringLiteral("moving window to target monitor"));
            return false;
        }

        qInfo() << "Moved HWND" << hwnd << "to target monitor before final resize for DPI transition"
                << currentDpiScale << "->" << targetDpiScale;
    }

    if (!SetWindowPos(hwnd, nullptr, targetLeft, targetTop, targetWidth, targetHeight,
                      SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE)) {
        warnSetWindowPosFailure(QStringLiteral("applying final rectangle"));
        return false;
    }

    if (restoreAsMaximized) {
        if (!setMaximizedStateWithoutForegroundCommand(hwnd) || !IsZoomed(hwnd)) {
            qWarning() << "Could not apply maximized state for HWND:" << hwnd;
            return false;
        }
        if (MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL) != targetMonitor->handle) {
            qWarning() << "Maximized window did not reach its saved monitor for HWND:" << hwnd
                       << "expected:" << restorePlan.targetDeviceName;
            return false;
        }
        qInfo() << "Verified maximized target monitor for HWND:" << hwnd
                << restorePlan.targetDeviceName;
    } else if (restoreAsMinimized) {
        ShowWindow(hwnd, SW_SHOWMINNOACTIVE);
        if (!IsIconic(hwnd)) {
            qWarning() << "Could not apply minimized state for HWND:" << hwnd;
            return false;
        }
    }

    qInfo() << "Restored position for HWND:" << hwnd << "normal bounds ("
            << targetLeft << "," << targetTop << ")" << targetWidth << "x" << targetHeight
            << "DPI" << currentDpiScale << "->" << targetDpiScale
            << "state:" << (restoreAsMaximized ? "maximized" :
                             (restoreAsMinimized ? "minimized" : "normal"));
    return true;
}

void WindowManager::restoreAllSavedWindows() {
    enumerateWindows();
    const QList<SavedWindowEntry> entries = getSavedEntries();
    int restoredCount = 0;
    int unmatchedCount = 0;
    int failedCount = 0;
    m_savedMetadataChangedDuringRestore = false;

    for (const SavedWindowEntry& entry : entries) {
        if (entry.isActive && entry.currentHwnd) {
            qInfo() << "Restoring saved window entry:" << entry.id
                    << "process:" << entry.info.processName
                    << "saved title:" << entry.info.title
                    << "target monitor:" << entry.position.monitorDeviceName
                    << "current HWND:" << entry.currentHwnd;
            if (restoreWindowPosition(entry.currentHwnd)) {
                ++restoredCount;
            } else {
                ++failedCount;
            }
        } else {
            ++unmatchedCount;
            QStringList candidateTitles;
            for (const WindowInfo& currentInfo : qAsConst(m_enumResult)) {
                if (!currentInfo.isActive || !IsWindow(currentInfo.hwnd)) {
                    continue;
                }
                if (WindowMatcher::sameExecutableAndClass(entry.info, currentInfo)) {
                    candidateTitles.append(QStringLiteral("%1:%2")
                        .arg(reinterpret_cast<quintptr>(currentInfo.hwnd), 0, 16)
                        .arg(currentInfo.title));
                }
            }

            qWarning() << "Saved window is not currently matched; skipping this restore pass:"
                       << entry.info.processName << entry.info.title << entry.id
                       << "candidates:" << candidateTitles;
        }
    }

    if (m_savedMetadataChangedDuringRestore) {
        emit savedLayoutMetadataChanged();
    }

    qInfo() << "Restore pass complete. Restored" << restoredCount
            << "of" << entries.size() << "saved windows; unmatched:" << unmatchedCount
            << "failed:" << failedCount;
    emit restoreCompleted(restoredCount, entries.size(), unmatchedCount, failedCount);
}

// --- Icon Loading ---
QIcon iconFromOwnedHandle(HICON hIcon) {
    if (!hIcon) {
        return QIcon();
    }
    QPixmap pixmap = QPixmap::fromImage(QImage::fromHICON(hIcon));
    DestroyIcon(hIcon);
    return QIcon(pixmap);
}

QString WindowManager::iconKeyForWindow(const WindowInfo& info) const {
    if (!info.processPath.isEmpty()) {
        return QStringLiteral("path:%1").arg(info.processPath.toLower());
    }
    if (!info.processName.isEmpty()) {
        return QStringLiteral("process:%1").arg(info.processName.toLower());
    }
    return info.stableId.isEmpty()
        ? QStringLiteral("hwnd:%1").arg(reinterpret_cast<quintptr>(info.hwnd))
        : info.stableId;
}

QIcon WindowManager::loadWindowIcon(const WindowInfo& info) {
    if (!info.processPath.isEmpty()) {
        SHFILEINFOW fileInfo = {};
        DWORD_PTR result = SHGetFileInfoW(
            reinterpret_cast<LPCWSTR>(info.processPath.utf16()),
            FILE_ATTRIBUTE_NORMAL,
            &fileInfo,
            sizeof(fileInfo),
            SHGFI_ICON | SHGFI_SMALLICON);
        if (result == 0) {
            result = SHGetFileInfoW(
                reinterpret_cast<LPCWSTR>(info.processPath.utf16()),
                FILE_ATTRIBUTE_NORMAL,
                &fileInfo,
                sizeof(fileInfo),
                SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
        }
        if (result != 0 && fileInfo.hIcon) {
            return iconFromOwnedHandle(fileInfo.hIcon);
        }
    }

    if (info.hwnd && IsWindow(info.hwnd)) {
        DWORD_PTR iconResult = 0;
        SendMessageTimeoutW(info.hwnd,
                            WM_GETICON,
                            ICON_SMALL2,
                            0,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK,
                            50,
                            &iconResult);
        HICON windowIcon = reinterpret_cast<HICON>(iconResult);
        if (!windowIcon) {
            windowIcon = reinterpret_cast<HICON>(
                GetClassLongPtrW(info.hwnd, GCLP_HICONSM));
        }
        if (windowIcon) {
            return iconFromOwnedHandle(CopyIcon(windowIcon));
        }
    }

    return QIcon();
}

QIcon WindowManager::defaultWindowIcon() {
    if (m_defaultWindowIcon.isNull()) {
        HICON sharedIcon = LoadIconW(nullptr, IDI_APPLICATION);
        if (sharedIcon) {
            m_defaultWindowIcon = iconFromOwnedHandle(CopyIcon(sharedIcon));
        }
    }
    return m_defaultWindowIcon;
}

HWND WindowManager::findMatchingWindow(const WindowInfo& savedInfo) {
    HWND matchedHwnd = nullptr;
    for (const auto& currentInfo : m_enumResult) {
        if (matchesSavedWindow(savedInfo, currentInfo)) {
            matchedHwnd = currentInfo.hwnd;
            break; // Found first match
        }
    }

    if (matchedHwnd) {
         qInfo() << "Found matching window for saved title:" << savedInfo.title << "HWND:" << matchedHwnd;
    }

    return matchedHwnd; 
}

QString WindowManager::makeStableWindowId(const WindowInfo& info) const {
    QString identity = QStringLiteral("%1|%2|%3")
        .arg(info.processPath.toLower(), info.className, info.title);

    if (info.processPath.isEmpty()) {
        identity = QStringLiteral("%1|%2|%3")
            .arg(info.processName.toLower(), info.className, info.title);
    }

    QByteArray hash = QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("window:%1").arg(QString::fromLatin1(hash.left(24)));
}

bool WindowManager::matchesSavedWindow(const WindowInfo& saved, const WindowInfo& current) const {
    return WindowMatcher::matchesSavedWindow(saved, current);
}

void WindowManager::rebuildLegacySavedMaps() {
    m_savedPositions.clear();
    m_savedWindowInfo.clear();
    m_hwndToEntryId.clear();

    for (auto it = m_savedEntries.begin(); it != m_savedEntries.end(); ++it) {
        SavedWindowEntry& entry = it.value();
        HWND hwnd = entry.currentHwnd;
        entry.info.hwnd = hwnd;
        entry.info.isActive = entry.isActive;
        if (hwnd) {
            m_savedPositions.insert(hwnd, entry.position);
            m_savedWindowInfo.insert(hwnd, entry.info);
            m_hwndToEntryId.insert(hwnd, it.key());
        }
    }
}

void WindowManager::reconcileSavedEntriesWithCurrentWindows(bool collectBrowserFingerprints) {
    QHash<HWND, WindowInfo> currentByHwnd;
    QHash<HWND, WindowPosition> currentPositions;
    QHash<HWND, BrowserFingerprint> currentFingerprints;

    for (const WindowInfo& currentInfo : qAsConst(m_enumResult)) {
        if (!currentInfo.isActive || !currentInfo.hwnd || !IsWindow(currentInfo.hwnd)) {
            continue;
        }

        currentByHwnd.insert(currentInfo.hwnd, currentInfo);
        currentPositions.insert(currentInfo.hwnd, getWindowPositionInfo(currentInfo.hwnd));
    }

    if (collectBrowserFingerprints) {
        for (const WindowInfo& currentInfo : qAsConst(m_enumResult)) {
            if (!currentInfo.isActive ||
                !currentInfo.hwnd ||
                !IsWindow(currentInfo.hwnd) ||
                !WindowMatcher::isBrowserLikeWindow(currentInfo)) {
                continue;
            }
            const BrowserFingerprint fingerprint =
                BrowserFingerprinting::collectForWindow(currentInfo.hwnd, currentInfo);
            if (fingerprint.hasTabSignals()) {
                m_browserFingerprintCache.insert(currentInfo.hwnd, fingerprint);
            }
        }
    }

    for (const WindowInfo& currentInfo : qAsConst(m_enumResult)) {
        const auto fingerprint = m_browserFingerprintCache.constFind(currentInfo.hwnd);
        if (fingerprint != m_browserFingerprintCache.constEnd()) {
            currentFingerprints.insert(currentInfo.hwnd, fingerprint.value());
        }
    }

    for (auto it = m_browserFingerprintCache.begin(); it != m_browserFingerprintCache.end();) {
        if (!IsWindow(it.key())) {
            it = m_browserFingerprintCache.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = m_savedEntries.begin(); it != m_savedEntries.end(); ++it) {
        SavedWindowEntry& entry = it.value();
        entry.isActive = false;
        entry.info.isActive = false;
        entry.info.savedHasRunningCandidates = false;
        if (entry.currentHwnd && !IsWindow(entry.currentHwnd)) {
            entry.currentHwndTrusted = false;
        }
    }

    const QList<WindowMatcher::Assignment> assignments =
        WindowMatcher::matchSavedWindows(m_savedEntries.values(), m_enumResult, currentPositions, currentFingerprints);

    QHash<QString, WindowMatcher::Assignment> assignmentByEntry;
    for (const WindowMatcher::Assignment& assignment : assignments) {
        assignmentByEntry.insert(assignment.entryId, assignment);
    }

    for (const WindowMatcher::Assignment& assignment : assignments) {
        if (!m_savedEntries.contains(assignment.entryId) ||
            !currentByHwnd.contains(assignment.hwnd)) {
            continue;
        }

        SavedWindowEntry& entry = m_savedEntries[assignment.entryId];
        const WindowInfo currentInfo = currentByHwnd.value(assignment.hwnd);
        if (entry.currentHwnd != currentInfo.hwnd) {
            qInfo() << "Remapped saved window from stale HWND" << entry.currentHwnd
                    << "to current HWND" << currentInfo.hwnd
                    << "reason:" << assignment.reason
                    << "score:" << assignment.score
                    << "saved title:" << entry.info.title
                    << "current title:" << currentInfo.title;
        }

        entry.currentHwnd = currentInfo.hwnd;
        entry.currentHwndTrusted = true;
        entry.info.hwnd = currentInfo.hwnd;
        entry.info.isActive = true;
        entry.isActive = true;
    }

    for (auto it = m_savedEntries.begin(); it != m_savedEntries.end(); ++it) {
        SavedWindowEntry& entry = it.value();
        if (!entry.isActive) {
            for (const WindowInfo& currentInfo : qAsConst(m_enumResult)) {
                if (currentInfo.isActive &&
                    WindowMatcher::sameExecutableAndClass(entry.info, currentInfo)) {
                    entry.info.savedHasRunningCandidates = true;
                    break;
                }
            }
        }

        QString status;
        int matchScore = 0;
        if (entry.isActive) {
            const WindowMatcher::Assignment assignment = assignmentByEntry.value(it.key());
            status = QStringLiteral("running via %1").arg(assignment.reason);
            matchScore = assignment.score;
        } else if (entry.info.savedHasRunningCandidates) {
            status = QStringLiteral("not matched: same app/class is running but identity is ambiguous");
        } else {
            status = QStringLiteral("not running: no same app/class candidate");
        }

        if (m_lastReconciliationStatus.value(it.key()) != status) {
            qInfo() << "Saved window reconciliation status:"
                    << it.key()
                    << status
                    << "score:" << matchScore
                    << "saved title:" << entry.info.title;
            m_lastReconciliationStatus.insert(it.key(), status);
        }
    }

    rebuildLegacySavedMaps();
}

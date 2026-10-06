#pragma once

#include <QObject>
#include <QList>
#include <QHash>
#include <QIcon>
#include <windows.h>

#include "WindowInfo.h"
#include "WindowPosition.h"
#include "SavedWindowEntry.h"

class WindowManager : public QObject {
    Q_OBJECT

public:
    explicit WindowManager(QObject *parent = nullptr);
    ~WindowManager() override;

    // Load initial data
    void loadInitialData(const QList<SavedWindowEntry>& entries);

    // Public methods to be called by the GUI or other components
    Q_INVOKABLE void enumerateWindows(bool collectBrowserFingerprints = false);
    Q_INVOKABLE void saveWindowPosition(HWND hwnd);
    Q_INVOKABLE int saveAllCurrentWindowPositions();
    Q_INVOKABLE bool restoreWindowPosition(HWND hwnd);
    Q_INVOKABLE void restoreAllSavedWindows();
    Q_INVOKABLE void removeSavedPosition(HWND hwnd);
    Q_INVOKABLE bool removeSavedEntry(const QString& entryId);

    // Helper method now public for use by MainWindow state check
    HWND findMatchingWindow(const WindowInfo& savedInfo);

    // Potentially add getters for saved positions if needed by other classes
    const QHash<HWND, WindowPosition>& getSavedPositions() const;
    const QHash<HWND, WindowInfo>& getSavedWindowInfo() const;
    QList<SavedWindowEntry> getSavedEntries() const;
    bool areAllSavedMonitorsAvailable() const;
    const QList<WindowInfo>& getLastEnumeratedWindows() const; // Added getter for last enum result
    WindowInfo getWindowInfo(HWND hwnd); // Made public for state check
    QList<SavedWindowEntry> snapshotCurrentWindowsForCapture();
    int replaceSavedLayout(const QList<SavedWindowEntry>& entries);
    void updateBrowserFingerprintCache(const QHash<HWND, BrowserFingerprint>& fingerprints);

signals:
    void windowsEnumerated(const QList<WindowInfo>& windows);
    void positionSaved(HWND hwnd, const WindowPosition& pos); // Signal when a position is successfully saved
    void positionRemoved(HWND hwnd); // Signal when a position is removed
    void savedLayoutMetadataChanged();
    void restoreCompleted(int restored, int total, int unmatched, int failed);

private:
    // Internal helper methods (implementations in .cpp)
    bool isWindowInteresting(HWND hwnd);
    WindowPosition getWindowPositionInfo(HWND hwnd);
    double getMonitorDpi(HWND hwnd);
    QString iconKeyForWindow(const WindowInfo& info) const;
    QIcon loadWindowIcon(const WindowInfo& info);
    QIcon defaultWindowIcon();

    // --- Win32 Specific Helpers --- 
    static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam); // Callback for EnumWindows
    // Member variables
    QHash<HWND, WindowInfo> m_currentWindows; // Maybe store current windows briefly?
    QHash<HWND, WindowPosition> m_savedPositions; // In-memory store of saved positions
    QHash<HWND, WindowInfo> m_savedWindowInfo; // Store info for saved windows
    QHash<QString, SavedWindowEntry> m_savedEntries;
    QHash<HWND, QString> m_hwndToEntryId;
    QHash<QString, QIcon> m_iconCache;
    QHash<HWND, BrowserFingerprint> m_browserFingerprintCache;
    QHash<QString, QString> m_lastReconciliationStatus;
    QIcon m_defaultWindowIcon;
    QList<WindowInfo> m_enumResult; // Temporary storage during enumeration
    bool m_savedMetadataChangedDuringRestore = false;

    // Add cache for icons later (QCache<HWND, QIcon>)
    QString makeStableWindowId(const WindowInfo& info) const;
    bool matchesSavedWindow(const WindowInfo& saved, const WindowInfo& current) const;
    void rebuildLegacySavedMaps();
    void reconcileSavedEntriesWithCurrentWindows(bool collectBrowserFingerprints);
};

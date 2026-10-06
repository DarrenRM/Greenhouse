#pragma once

#include <QMainWindow>
#include <QHash>
#include <QList>
#include <QSystemTrayIcon> // Include for tray icon

#include "BrowserFingerprint.h"
#include "SavedWindowEntry.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QListView;
class QPushButton;
class WindowListModel;
class BlackoutController;
class WindowManager;
class SettingsManager;
class QTimer;
class QCloseEvent; // Forward declare for event handlers
class QMenu;       // Forward declare for tray menu
class SettingsDialog; // Forward declare SettingsDialog
class QAction; // Forward declare QAction
struct WindowPosition; // Forward declare for slot signature

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(WindowManager* windowManager, SettingsManager* settingsManager, QWidget *parent = nullptr);
    ~MainWindow();

protected:
    // Event handlers (need to be declared as protected)
    void closeEvent(QCloseEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private slots:
    void onRefreshClicked();
    void onCaptureLayoutClicked();
    void onRestoreAllClicked();
    void onSettingsClicked();
    void onRemoveSavedWindowRequested(const QString& entryId, const QString& title);
    void onCheckWindowStateTimeout(); // Timer slot
    void handlePositionSaved(HWND hwnd, const WindowPosition& pos); // Slot to update model on save
    void handlePositionRemoved(HWND hwnd); // Slot to update model on remove
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason); // Tray activation slot
    void showWindowFromTray(); // Slot to show window from tray
    void quitApplication(); // Slot to quit application

private:
    void setupUi();
    void setupConnections();
    void setupTrayIcon(); // Helper for tray icon setup
    bool confirmCaptureLayout(int existingCount,
                              int currentCount,
                              int existingMonitorCount,
                              int currentMonitorCount);
    void refreshWindowList();
    void restoreSavedLayoutPass();
    void finishCaptureLayout(QList<SavedWindowEntry> entries, qint64 fingerprintElapsedMs);
    void setCaptureInProgress(bool inProgress);
    void beginBrowserIdentityPreparation(const QString& reason, bool restoreAfterPreparation);
    void finishBrowserIdentityPreparation(QHash<HWND, BrowserFingerprint> fingerprints,
                                          qint64 fingerprintElapsedMs,
                                          QString reason,
                                          bool restoreAfterPreparation);
    bool shouldPrepareBrowserIdentity() const;
    void setRestoreInProgress(bool inProgress);
    void attemptDisplayRecovery(quint64 generation, int attempt);

    // UI Elements
    QListView *m_windowListView = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_captureButton = nullptr;
    QPushButton *m_restoreAllButton = nullptr;
    QPushButton *m_settingsButton = nullptr;
    QPushButton *m_blackoutButton = nullptr;

    // Models and Managers
    WindowListModel *m_windowListModel = nullptr;
    WindowManager *m_windowManager; // Pointer to the manager instance
    SettingsManager *m_settingsManager; // Pointer to settings manager
    BlackoutController *m_blackoutController = nullptr;

    // State
    QTimer *m_stateCheckTimer = nullptr; // Renamed m_windowStateTimer
    bool m_captureInProgress = false;
    bool m_restoreInProgress = false;
    bool m_browserIdentityInProgress = false;
    bool m_backgroundRestoreInProgress = false;
    qint64 m_lastBrowserIdentityRefreshMs = 0;
    quint64 m_displayChangeGeneration = 0;
    // Tray Icon
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    // Add missing QAction members
    QAction *m_showAction = nullptr;
    QAction *m_settingsAction = nullptr;
    QAction *m_quitAction = nullptr;
}; 

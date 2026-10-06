#include "../include/MainWindow.h"
#include "../include/WindowManager.h"
#include "../include/SettingsManager.h"
#include "../include/WindowListModel.h"
#include "../include/WindowListDelegate.h"
#include "../include/SettingsDialog.h"
#include "../include/WindowMatcher.h"
#include "../include/BrowserFingerprint.h"
#include "../include/BlackoutController.h"

// Qt Includes for UI
#include <QListView>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QAbstractButton>
#include <QDebug>
#include <QTimer>
#include <QMessageBox>
#include <QSet>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QCloseEvent>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QIcon>
#include <QPointer>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <windows.h>

#include <thread>

namespace {

QString monitorNameForWindow(HWND hwnd)
{
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!monitor) {
        return QString();
    }

    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        return QString();
    }

    return QString::fromWCharArray(info.szDevice);
}

int savedMonitorCount(const QList<SavedWindowEntry>& entries)
{
    QSet<QString> monitors;
    for (const SavedWindowEntry& entry : entries) {
        if (!entry.position.monitorDeviceName.isEmpty()) {
            monitors.insert(entry.position.monitorDeviceName.toLower());
        }
    }
    return monitors.size();
}

QList<WindowInfo> activeWindowsOnly(const QList<WindowInfo>& windows)
{
    QList<WindowInfo> active;
    for (const WindowInfo& window : windows) {
        if (window.isActive && window.hwnd && IsWindow(window.hwnd)) {
            active.append(window);
        }
    }
    return active;
}

int activeMonitorCount(const QList<WindowInfo>& windows)
{
    QSet<QString> monitors;
    for (const WindowInfo& window : windows) {
        const QString monitorName = monitorNameForWindow(window.hwnd);
        if (!monitorName.isEmpty()) {
            monitors.insert(monitorName.toLower());
        }
    }
    return monitors.size();
}

QString countText(int count, const QString& singular, const QString& plural)
{
    return QStringLiteral("%1 %2").arg(count).arg(count == 1 ? singular : plural);
}

QString browserGroupKey(const SavedWindowEntry& entry)
{
    const QString processIdentity = entry.info.processPath.isEmpty()
        ? entry.info.processName.toLower()
        : entry.info.processPath.toLower();
    return QStringLiteral("%1|%2").arg(processIdentity, entry.info.className);
}

QString browserDisplayName(const QString& processName)
{
    const QString lower = processName.toLower();
    if (lower == QLatin1String("brave.exe")) return QStringLiteral("Brave");
    if (lower == QLatin1String("chrome.exe")) return QStringLiteral("Chrome");
    if (lower == QLatin1String("msedge.exe")) return QStringLiteral("Edge");
    if (lower == QLatin1String("firefox.exe")) return QStringLiteral("Firefox");
    if (lower == QLatin1String("opera.exe")) return QStringLiteral("Opera");
    if (lower == QLatin1String("vivaldi.exe")) return QStringLiteral("Vivaldi");
    return processName;
}

QStringList duplicateBrowserGroupsMissingFingerprints(const QList<SavedWindowEntry>& entries)
{
    QHash<QString, int> browserCounts;
    QHash<QString, int> missingFingerprintCounts;
    QHash<QString, QString> browserNames;

    for (const SavedWindowEntry& entry : entries) {
        if (!WindowMatcher::isBrowserLikeWindow(entry.info)) {
            continue;
        }

        const QString key = browserGroupKey(entry);
        browserCounts[key] += 1;
        browserNames[key] = browserDisplayName(entry.info.processName);
        if (!entry.browserFingerprint.hasTabSignals()) {
            missingFingerprintCounts[key] += 1;
        }
    }

    QStringList warnings;
    for (auto it = browserCounts.constBegin(); it != browserCounts.constEnd(); ++it) {
        const QString key = it.key();
        const int total = it.value();
        const int missing = missingFingerprintCounts.value(key);
        if (total > 1 && missing > 0) {
            warnings.append(QStringLiteral("%1: %2 saved windows, %3 without browser identity")
                                .arg(browserNames.value(key),
                                     QString::number(total),
                                     QString::number(missing)));
        }
    }
    warnings.sort(Qt::CaseInsensitive);
    return warnings;
}

QList<WindowInfo> browserWindowsFrom(const QList<WindowInfo>& windows)
{
    QList<WindowInfo> browserWindows;
    for (const WindowInfo& info : windows) {
        if (info.isActive && info.hwnd && IsWindow(info.hwnd) &&
            WindowMatcher::isBrowserLikeWindow(info)) {
            browserWindows.append(info);
        }
    }
    return browserWindows;
}

QIcon lightbulbIcon(const QColor& color)
{
    QIcon icon;
    for (const int size : {16, 20, 24, 32, 40, 48}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(size / 24.0, size / 24.0);
        painter.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);

        // Glass: a circle (center 12,9.5 r6.5) opening into a neck at x=9..15.
        QPainterPath bulb;
        bulb.moveTo(9.0, 17.0);
        bulb.lineTo(9.0, 15.3);
        bulb.arcTo(QRectF(5.5, 3.0, 13.0, 13.0), 242.5, -305.0);
        bulb.lineTo(15.0, 17.0);
        bulb.closeSubpath();
        painter.drawPath(bulb);

        // Screw base.
        painter.drawLine(QPointF(9.8, 19.3), QPointF(14.2, 19.3));
        painter.drawLine(QPointF(10.8, 21.4), QPointF(13.2, 21.4));

        // Highlight on the glass.
        painter.drawArc(QRectF(8.3, 5.8, 7.4, 7.4), 100 * 16, 70 * 16);
        icon.addPixmap(pixmap);
    }
    return icon;
}

} // namespace

MainWindow::MainWindow(WindowManager *windowManager,
                     SettingsManager *settingsManager,
                     QWidget *parent)
    : QMainWindow(parent),
      m_windowManager(windowManager),
      m_settingsManager(settingsManager)
{
    Q_ASSERT(m_windowManager != nullptr);
    Q_ASSERT(m_settingsManager != nullptr);

    // Create the model (MainWindow takes ownership if parented)
    m_windowListModel = new WindowListModel(this);
    m_blackoutController = new BlackoutController(this);

    setupUi();
    setupTrayIcon();
    setupConnections();

    // Set initial window properties
    setWindowTitle(tr("Greenhouse - Window Position Manager"));
    setWindowIcon(QApplication::windowIcon());
    resize(720, 560); // Match original Python app size

    m_stateCheckTimer = new QTimer(this);
    connect(m_stateCheckTimer, &QTimer::timeout, this, &MainWindow::onCheckWindowStateTimeout);
    m_stateCheckTimer->start(15000);
    qInfo() << "Started window reconciliation timer.";

    // Initial population
    onRefreshClicked(); 
}

MainWindow::~MainWindow() {
    // Tray icon might need explicit hiding or destruction 
    // if not parented correctly, but usually Qt handles it.
    // if (m_trayIcon) m_trayIcon->hide();
    qInfo() << "MainWindow destroyed";
    // Qt handles child widget deletion (m_windowListModel, UI elements)
}

void MainWindow::setupUi() {
    // --- Main Layout --- 
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    setCentralWidget(centralWidget);

    // --- Window List View --- 
    m_windowListView = new QListView(centralWidget);
    m_windowListView->setModel(m_windowListModel);
    auto* windowListDelegate = new WindowListDelegate(m_windowListView);
    m_windowListView->setItemDelegate(windowListDelegate);
    m_windowListView->setSelectionMode(QAbstractItemView::NoSelection);
    m_windowListView->setFocusPolicy(Qt::NoFocus);
    m_windowListView->setMouseTracking(true);
    m_windowListView->viewport()->setMouseTracking(true);
    m_windowListView->viewport()->setAttribute(Qt::WA_Hover, true);
    m_windowListView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_windowListView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_windowListView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_windowListView->setResizeMode(QListView::Adjust);
    m_windowListView->setWordWrap(false);
    m_windowListView->setSpacing(0);
    mainLayout->addWidget(m_windowListView);

    // --- Button Layout --- 
    QWidget *buttonWidget = new QWidget(centralWidget);
    QHBoxLayout *buttonLayout = new QHBoxLayout(buttonWidget);
    buttonLayout->setContentsMargins(0, 0, 0, 0); // Remove margins for tight packing
    
    m_refreshButton = new QPushButton(tr("Refresh"));
    m_captureButton = new QPushButton(tr("Capture Layout"));
    m_restoreAllButton = new QPushButton(tr("Restore Layout"));
    m_settingsButton = new QPushButton(tr("Settings"), buttonWidget);

    buttonLayout->addWidget(m_refreshButton);
    buttonLayout->addWidget(m_captureButton);
    buttonLayout->addWidget(m_restoreAllButton);
    buttonLayout->addWidget(m_settingsButton);
    buttonLayout->addStretch(); // Push buttons to the left

    m_blackoutButton = new QPushButton(buttonWidget);
    m_blackoutButton->setIcon(lightbulbIcon(palette().color(QPalette::ButtonText)));
    m_blackoutButton->setIconSize(QSize(20, 20));
    m_blackoutButton->setFixedWidth(m_blackoutButton->sizeHint().height() + 8);
    m_blackoutButton->setAccessibleName(tr("Blackout"));
    m_blackoutButton->setToolTip(tr("Blackout: turn every screen black and dim the monitors.\n"
                                    "Move the mouse or press any key to wake."));
    buttonLayout->addWidget(m_blackoutButton);

    mainLayout->addWidget(buttonWidget);

    qInfo() << "MainWindow UI setup complete.";
}

void MainWindow::setupConnections() {
    qInfo() << "Setting up MainWindow connections...";

    // --- Connect WindowManager Signals to Model --- 
    // When WindowManager finishes enumerating, update the model
    connect(m_windowManager, &WindowManager::windowsEnumerated,
            m_windowListModel, &WindowListModel::updateWindows);
    connect(m_windowManager, &WindowManager::savedLayoutMetadataChanged, this, [this]() {
        m_settingsManager->saveSavedWindowEntries(m_windowManager->getSavedEntries());
        qInfo() << "Persisted migrated monitor identities after restore.";
    });
    connect(m_windowManager,
            &WindowManager::restoreCompleted,
             this,
             [this](int restored, int total, int unmatched, int failed) {
                if (m_backgroundRestoreInProgress) {
                    if (m_trayIcon) {
                        const QString message = unmatched == 0 && failed == 0
                            ? tr("Restored %1 after the display configuration stabilized.")
                                  .arg(countText(restored, tr("window"), tr("windows")))
                            : tr("Display recovery restored %1 of %2 saved windows. See the log for details.")
                                  .arg(restored)
                                  .arg(total);
                        m_trayIcon->showMessage(tr("Greenhouse"),
                                                message,
                                                unmatched == 0 && failed == 0
                                                    ? QSystemTrayIcon::Information
                                                    : QSystemTrayIcon::Warning,
                                                3000);
                    }
                    return;
                }
                if (unmatched == 0 && failed == 0) {
                    return;
                }
                QMessageBox::warning(
                    this,
                    tr("Restore Incomplete"),
                    tr("Greenhouse restored %1 of %2 saved windows.\n\n"
                       "%3 could not be matched to a running window.\n"
                       "%4 could not be moved to their saved monitor.\n\n"
                       "No uncertain window assignments were made. Details were written to the log.")
                        .arg(restored)
                        .arg(total)
                        .arg(unmatched)
                        .arg(failed));
            });

    auto* windowListDelegate = qobject_cast<WindowListDelegate*>(m_windowListView->itemDelegate());
    if (windowListDelegate) {
        connect(windowListDelegate,
                &WindowListDelegate::removeRequested,
                this,
                &MainWindow::onRemoveSavedWindowRequested);
    }

    // --- Connect UI Buttons to MainWindow Slots --- 
    connect(m_refreshButton, &QPushButton::clicked,
            this, &MainWindow::onRefreshClicked);
    connect(m_captureButton, &QPushButton::clicked,
            this, &MainWindow::onCaptureLayoutClicked);
    connect(m_restoreAllButton, &QPushButton::clicked,
            this, &MainWindow::onRestoreAllClicked);
    connect(m_settingsButton, &QPushButton::clicked,
            this, &MainWindow::onSettingsClicked);
    connect(m_blackoutButton, &QPushButton::clicked,
            m_blackoutController, &BlackoutController::start);

    // Connect Tray Icon signals
    if (m_trayIcon) {
      connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayIconActivated);
      if (m_showAction) connect(m_showAction, &QAction::triggered, this, &MainWindow::showWindowFromTray);
      if (m_settingsAction) connect(m_settingsAction, &QAction::triggered, this, &MainWindow::onSettingsClicked);
      if (m_quitAction) connect(m_quitAction, &QAction::triggered, this, &MainWindow::quitApplication);
    } else {
      qWarning() << "Tray icon or actions not initialized, skipping tray connections.";
    }

    qInfo() << "MainWindow connections setup complete.";
}

void MainWindow::setupTrayIcon() {
    // Check if tray icons are supported
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qWarning() << "System tray not available on this system.";
        return;
    }

    // Create Actions
    m_showAction = new QAction(tr("&Show Window"), this);

    m_settingsAction = new QAction(tr("&Settings"), this);

    m_quitAction = new QAction(tr("&Exit"), this);

    // Create Menu
    m_trayMenu = new QMenu(this);
    m_trayMenu->addAction(m_showAction);
    m_trayMenu->addAction(m_settingsAction);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(m_quitAction);

    // Create Tray Icon
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(QApplication::windowIcon());
    m_trayIcon->setToolTip(tr("Greenhouse Window Manager"));
    m_trayIcon->setContextMenu(m_trayMenu);

    m_trayIcon->show(); // Make the icon visible
    qInfo() << "System tray icon setup complete.";
}

// Override close event to hide to tray instead of quitting
void MainWindow::closeEvent(QCloseEvent *event) {
    // Check if tray icon exists and is visible
    if (m_trayIcon && m_trayIcon->isVisible()) {
        qInfo() << "Close event: Hiding window to tray.";
        hide(); // Hide the main window
        // Optional: Show a notification bubble
        // m_trayIcon->showMessage(tr("Minimized to Tray"), 
        //                         tr("Greenhouse is running in the background."),
        //                         QSystemTrayIcon::Information, 3000);
        event->ignore(); // Ignore the event, preventing the window from closing
    } else {
        qInfo() << "Close event: Quitting application (no tray icon).";
        quitApplication(); // Quit if tray isn't available/visible
        event->accept(); // Accept the close event
    }
}

// --- Tray Icon Slots --- 

void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason) {
    // Show window on double-click or trigger
    switch (reason) {
        case QSystemTrayIcon::Trigger: // Single click (platform dependent)
        case QSystemTrayIcon::DoubleClick:
            showWindowFromTray();
            break;
        case QSystemTrayIcon::MiddleClick:
            // Optional: Implement middle-click action
            break;
        default:
            break; // Ignore other reasons (Context menu handled by setContextMenu)
    }
}

void MainWindow::showWindowFromTray() {
    qInfo() << "Showing window from tray.";
    show();          // Make the window visible
    raise();         // Raise it above other windows
    activateWindow(); // Give it focus
}

void MainWindow::quitApplication() {
    qInfo() << "Quit action triggered. Shutting down.";
    if(m_trayIcon) m_trayIcon->hide(); // Hide tray icon before quitting
    QApplication::quit();
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
    Q_UNUSED(eventType);
    MSG *msg = static_cast<MSG *>(message);
    if (msg && msg->message == WM_DISPLAYCHANGE) {
        const quint64 generation = ++m_displayChangeGeneration;
        qInfo() << "Display configuration changed; waiting for the saved monitor set to stabilize.";
        QTimer::singleShot(3500, this, [this, generation]() {
            attemptDisplayRecovery(generation, 0);
        });
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

// --- Placeholder Slot Implementations ---

void MainWindow::onRefreshClicked() {
    qInfo() << "Refresh button clicked";
    if (m_windowManager) {
        refreshWindowList();
        // Icon loading should happen automatically via signals/slots
        // in the WindowManager enumerate implementation eventually
    }
}

bool MainWindow::confirmCaptureLayout(int existingCount,
                                      int currentCount,
                                      int existingMonitorCount,
                                      int currentMonitorCount)
{
    QMessageBox dialog(this);
    dialog.setIcon(QMessageBox::Warning);
    dialog.setWindowTitle(tr("Replace Saved Layout?"));
    dialog.setText(tr("Capture Layout replaces the restore layout Greenhouse uses."));

    QStringList details;
    if (existingCount > 0) {
        details << tr("Saved now: %1 across %2.")
                       .arg(countText(existingCount, tr("window"), tr("windows")),
                            countText(existingMonitorCount, tr("monitor"), tr("monitors")));
        details << tr("Current desktop: %1 across %2.")
                       .arg(countText(currentCount, tr("window"), tr("windows")),
                            countText(currentMonitorCount, tr("monitor"), tr("monitors")));
    } else {
        details << tr("No saved layout exists yet.");
        details << tr("Current desktop: %1 across %2.")
                       .arg(countText(currentCount, tr("window"), tr("windows")),
                            countText(currentMonitorCount, tr("monitor"), tr("monitors")));
    }

    details << tr("Only replace the saved layout when every app is exactly where you want Restore Layout to put it.");

    if (existingCount > 0 && currentCount < existingCount) {
        details << tr("Warning: fewer windows are visible now than in the saved layout. Capturing may replace a complete layout with a partial one.");
    }
    if (existingMonitorCount > 0 && currentMonitorCount < existingMonitorCount) {
        details << tr("Warning: fewer monitors have visible captured windows now than in the saved layout.");
    }
    if (currentCount == 0) {
        details << tr("No capturable windows are visible right now.");
    }

    dialog.setInformativeText(details.join(QStringLiteral("\n\n")));
    QAbstractButton *replaceButton = dialog.addButton(tr("Replace Saved Layout"), QMessageBox::DestructiveRole);
    QAbstractButton *cancelButton = dialog.addButton(QMessageBox::Cancel);
    dialog.setDefaultButton(qobject_cast<QPushButton*>(cancelButton));
    dialog.setEscapeButton(cancelButton);

    dialog.exec();
    return dialog.clickedButton() == replaceButton;
}

void MainWindow::onCaptureLayoutClicked() {
    qInfo() << "Capture Layout button clicked";
    if (!m_windowManager || !m_settingsManager || m_captureInProgress || m_restoreInProgress) return;

    m_stateCheckTimer->stop();
    m_windowManager->enumerateWindows(false);

    const QList<SavedWindowEntry> existingEntries = m_windowManager->getSavedEntries();
    const QList<WindowInfo> activeWindows = activeWindowsOnly(m_windowManager->getLastEnumeratedWindows());
    if (!confirmCaptureLayout(existingEntries.size(),
                              activeWindows.size(),
                              savedMonitorCount(existingEntries),
                              activeMonitorCount(activeWindows))) {
        qInfo() << "Capture Layout cancelled by user.";
        m_stateCheckTimer->start(15000);
        return;
    }

    QList<SavedWindowEntry> captureEntries =
        m_windowManager->snapshotCurrentWindowsForCapture();
    if (captureEntries.isEmpty()) {
        m_stateCheckTimer->start(15000);
        QMessageBox::warning(this,
                             tr("Capture Layout"),
                             tr("No windows were captured. Greenhouse did not find any visible app windows to save."));
        return;
    }

    setCaptureInProgress(true);
    QPointer<MainWindow> windowGuard(this);
    QPointer<QCoreApplication> appGuard(QCoreApplication::instance());
    std::thread([windowGuard, appGuard, captureEntries = std::move(captureEntries)]() mutable {
        QElapsedTimer timer;
        timer.start();
        for (SavedWindowEntry& entry : captureEntries) {
            if (!WindowMatcher::isBrowserLikeWindow(entry.info)) {
                continue;
            }
            entry.browserFingerprint = BrowserFingerprinting::collectForWindow(
                entry.currentHwnd,
                entry.info);
        }
        const qint64 elapsedMs = timer.elapsed();

        if (!appGuard) {
            return;
        }
        QMetaObject::invokeMethod(
            appGuard.data(),
            [windowGuard, captureEntries = std::move(captureEntries), elapsedMs]() mutable {
                if (windowGuard) {
                    windowGuard->finishCaptureLayout(std::move(captureEntries), elapsedMs);
                }
            },
            Qt::QueuedConnection);
    }).detach();
}

void MainWindow::finishCaptureLayout(QList<SavedWindowEntry> entries,
                                     qint64 fingerprintElapsedMs)
{
    if (!m_windowManager || !m_settingsManager) {
        setCaptureInProgress(false);
        return;
    }

    const int savedCount = m_windowManager->replaceSavedLayout(entries);
    m_windowManager->enumerateWindows(false);
    qInfo() << "Asynchronous capture completed in" << fingerprintElapsedMs
            << "ms with" << savedCount << "windows.";

    const QStringList browserFingerprintWarnings =
        duplicateBrowserGroupsMissingFingerprints(m_windowManager->getSavedEntries());
    if (!browserFingerprintWarnings.isEmpty()) {
        QMessageBox::warning(
            this,
            tr("Browser Identity Incomplete"),
            tr("Greenhouse captured the layout, but one or more duplicate browser windows could not be identified safely.\n\n"
               "These windows may be skipped during Restore Layout to avoid swapping them:\n\n%1\n\n"
               "Try leaving each browser window visible and focused at least once, then capture again.")
                .arg(browserFingerprintWarnings.join(QStringLiteral("\n"))));
    }

    m_settingsManager->saveSavedWindowEntries(m_windowManager->getSavedEntries());
    if (m_trayIcon) {
        m_trayIcon->showMessage(tr("Greenhouse"),
                                tr("Captured %1. Previous layout was backed up.")
                                    .arg(countText(savedCount, tr("window"), tr("windows"))),
                                QSystemTrayIcon::Information,
                                2500);
    }
    setCaptureInProgress(false);
}

void MainWindow::setCaptureInProgress(bool inProgress)
{
    m_captureInProgress = inProgress;
    m_refreshButton->setEnabled(!inProgress);
    m_captureButton->setEnabled(!inProgress);
    m_restoreAllButton->setEnabled(!inProgress);
    m_captureButton->setText(inProgress ? tr("Capturing...") : tr("Capture Layout"));

    if (inProgress) {
        m_stateCheckTimer->stop();
    } else {
        m_stateCheckTimer->start(15000);
    }
}

void MainWindow::onRestoreAllClicked() {
    qInfo() << "Restore All button clicked";
    if (!m_windowManager || m_captureInProgress || m_restoreInProgress) return;
    m_backgroundRestoreInProgress = false;

    if (m_windowManager->getSavedEntries().isEmpty()) {
        qInfo() << "No saved positions to restore.";
        // Optional: Show a message box to the user
        // QMessageBox::information(this, tr("Restore All"), tr("No window positions have been saved yet."));
        return;
    }

    const QStringList browserFingerprintWarnings =
        duplicateBrowserGroupsMissingFingerprints(m_windowManager->getSavedEntries());
    if (!browserFingerprintWarnings.isEmpty()) {
        QMessageBox::warning(
            this,
            tr("Browser Layout Needs Recapture"),
            tr("This saved layout cannot safely identify duplicate browser windows yet.\n\n"
               "Greenhouse will skip these browser windows instead of risking another swap:\n\n%1\n\n"
               "Arrange them where they belong, then use Capture Layout once to save browser fingerprints.")
                .arg(browserFingerprintWarnings.join(QStringLiteral("\n"))));
    }

    m_stateCheckTimer->stop();
    m_windowManager->enumerateWindows(false);
    beginBrowserIdentityPreparation(QStringLiteral("restore"), true);
}

void MainWindow::beginBrowserIdentityPreparation(const QString& reason,
                                                 bool restoreAfterPreparation)
{
    if (!m_windowManager || m_browserIdentityInProgress) {
        if (restoreAfterPreparation) {
            setRestoreInProgress(false);
            m_backgroundRestoreInProgress = false;
            m_stateCheckTimer->start(15000);
        }
        return;
    }

    const QList<WindowInfo> browserWindows =
        browserWindowsFrom(m_windowManager->getLastEnumeratedWindows());
    if (browserWindows.isEmpty()) {
        if (restoreAfterPreparation) {
            qInfo() << "Attempting to restore saved layout without browser identity preparation.";
            restoreSavedLayoutPass();
            setRestoreInProgress(false);
        }
        m_stateCheckTimer->start(15000);
        return;
    }

    m_browserIdentityInProgress = true;
    m_lastBrowserIdentityRefreshMs = QDateTime::currentMSecsSinceEpoch();
    if (restoreAfterPreparation) {
        setRestoreInProgress(true);
    }

    QPointer<MainWindow> windowGuard(this);
    QPointer<QCoreApplication> appGuard(QCoreApplication::instance());
    std::thread([windowGuard,
                 appGuard,
                 browserWindows,
                 reason,
                 restoreAfterPreparation]() mutable {
        QElapsedTimer timer;
        timer.start();
        QHash<HWND, BrowserFingerprint> fingerprints;
        for (const WindowInfo& info : browserWindows) {
            const BrowserFingerprint fingerprint =
                BrowserFingerprinting::collectForWindow(info.hwnd, info);
            if (fingerprint.hasTabSignals()) {
                fingerprints.insert(info.hwnd, fingerprint);
            }
        }
        const qint64 elapsedMs = timer.elapsed();

        if (!appGuard) {
            return;
        }
        QMetaObject::invokeMethod(
            appGuard.data(),
            [windowGuard,
             fingerprints = std::move(fingerprints),
             elapsedMs,
             reason,
             restoreAfterPreparation]() mutable {
                if (windowGuard) {
                    windowGuard->finishBrowserIdentityPreparation(std::move(fingerprints),
                                                                  elapsedMs,
                                                                  reason,
                                                                  restoreAfterPreparation);
                }
            },
            Qt::QueuedConnection);
    }).detach();
}

void MainWindow::finishBrowserIdentityPreparation(
    QHash<HWND, BrowserFingerprint> fingerprints,
    qint64 fingerprintElapsedMs,
    QString reason,
    bool restoreAfterPreparation)
{
    m_browserIdentityInProgress = false;
    if (!m_windowManager) {
        if (restoreAfterPreparation) {
            setRestoreInProgress(false);
            m_backgroundRestoreInProgress = false;
        }
        return;
    }

    m_windowManager->updateBrowserFingerprintCache(fingerprints);
    qInfo() << "Browser identity preparation for" << reason << "completed in"
            << fingerprintElapsedMs << "ms with" << fingerprints.size() << "windows.";
    if (restoreAfterPreparation) {
        qInfo() << "Attempting to restore saved layout.";
        restoreSavedLayoutPass();
        setRestoreInProgress(false);
    } else {
        m_windowManager->enumerateWindows(false);
        if (!m_captureInProgress && !m_restoreInProgress) {
            m_stateCheckTimer->start(15000);
        }
    }
}

bool MainWindow::shouldPrepareBrowserIdentity() const
{
    if (!m_windowManager || m_browserIdentityInProgress ||
        m_captureInProgress || m_restoreInProgress) {
        return false;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastBrowserIdentityRefreshMs > 0 &&
        now - m_lastBrowserIdentityRefreshMs < 30000) {
        return false;
    }

    const QList<SavedWindowEntry> entries = m_windowManager->getSavedEntries();
    for (const SavedWindowEntry& entry : entries) {
        if (entry.isActive ||
            !entry.browserFingerprint.hasTabSignals() ||
            !WindowMatcher::isBrowserLikeWindow(entry.info)) {
            continue;
        }

        if (entry.info.savedHasRunningCandidates) {
            return true;
        }

        for (const WindowInfo& current : m_windowManager->getLastEnumeratedWindows()) {
            if (current.isActive &&
                WindowMatcher::sameExecutableAndClass(entry.info, current)) {
                return true;
            }
        }
    }
    return false;
}

void MainWindow::setRestoreInProgress(bool inProgress)
{
    m_restoreInProgress = inProgress;
    m_refreshButton->setEnabled(!inProgress);
    m_captureButton->setEnabled(!inProgress);
    m_restoreAllButton->setEnabled(!inProgress);
    m_restoreAllButton->setText(inProgress ? tr("Restoring...") : tr("Restore Layout"));

    if (inProgress) {
        m_stateCheckTimer->stop();
    } else {
        m_stateCheckTimer->start(15000);
    }
}

void MainWindow::onSettingsClicked() {
    qInfo() << "Settings button clicked";
    if (!m_settingsManager) return;

    // Create and execute the dialog modally
    SettingsDialog dialog(m_settingsManager, this); 
    dialog.exec(); // Show modally, waits until dialog is closed
    qInfo() << "SettingsDialog closed.";
    // Settings are saved within the dialog's onSaveClicked slot
}

void MainWindow::refreshWindowList() {
    if (!m_windowManager) {
        return;
    }

    m_windowManager->enumerateWindows();
    if (shouldPrepareBrowserIdentity()) {
        beginBrowserIdentityPreparation(QStringLiteral("refresh"), false);
    }
}

void MainWindow::onRemoveSavedWindowRequested(const QString& entryId, const QString& title) {
    if (!m_windowManager || !m_settingsManager || entryId.isEmpty()) {
        return;
    }

    QMessageBox dialog(this);
    dialog.setIcon(QMessageBox::Question);
    dialog.setWindowTitle(tr("Remove from Saved Layout"));
    dialog.setText(tr("Remove \"%1\" from the saved layout?").arg(title));
    dialog.setInformativeText(
        tr("Greenhouse will no longer restore this window. The previous layout will be backed up."));
    QAbstractButton* removeButton = dialog.addButton(tr("Remove"), QMessageBox::DestructiveRole);
    QAbstractButton* cancelButton = dialog.addButton(QMessageBox::Cancel);
    dialog.setDefaultButton(qobject_cast<QPushButton*>(cancelButton));
    dialog.setEscapeButton(cancelButton);
    dialog.exec();

    if (dialog.clickedButton() != removeButton) {
        return;
    }

    if (!m_windowManager->removeSavedEntry(entryId)) {
        qWarning() << "Saved layout entry disappeared before removal:" << entryId;
        refreshWindowList();
        return;
    }

    m_settingsManager->saveSavedWindowEntries(m_windowManager->getSavedEntries());
    refreshWindowList();
    qInfo() << "Removed saved layout entry from UI:" << entryId << title;
}

void MainWindow::restoreSavedLayoutPass() {
    if (!m_windowManager) {
        return;
    }

    m_windowManager->restoreAllSavedWindows();
    m_backgroundRestoreInProgress = false;
}

void MainWindow::attemptDisplayRecovery(quint64 generation, int attempt) {
    if (generation != m_displayChangeGeneration || !m_windowManager) {
        return;
    }

    constexpr int maxAttempts = 5;
    if (m_captureInProgress || m_restoreInProgress || m_browserIdentityInProgress ||
        !m_windowManager->areAllSavedMonitorsAvailable()) {
        if (attempt + 1 < maxAttempts) {
            QTimer::singleShot(3000, this, [this, generation, attempt]() {
                attemptDisplayRecovery(generation, attempt + 1);
            });
        } else {
            qInfo() << "Display recovery stopped because the complete saved monitor set did not stabilize.";
            refreshWindowList();
        }
        return;
    }

    qInfo() << "Saved monitor set is available; restoring layout after display change.";
    m_backgroundRestoreInProgress = true;
    m_stateCheckTimer->stop();
    m_windowManager->enumerateWindows(false);
    beginBrowserIdentityPreparation(QStringLiteral("display recovery"), true);
}

// --- Window State Checking Slot --- 
void MainWindow::onCheckWindowStateTimeout() {
    if (!m_windowManager || !m_windowListModel || !m_settingsManager ||
        m_captureInProgress || m_restoreInProgress) return;

    refreshWindowList();
}

// Slot to handle position saved signal from WindowManager
// (Currently just triggers a settings save, but could update model if needed)
void MainWindow::handlePositionSaved(HWND hwnd, const WindowPosition& pos) {
    Q_UNUSED(hwnd);
    Q_UNUSED(pos);
}

// Slot to handle position removed signal from WindowManager
// (Currently just triggers a settings save, but could update model if needed)
void MainWindow::handlePositionRemoved(HWND hwnd) {
    Q_UNUSED(hwnd);
} 

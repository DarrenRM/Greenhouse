#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMutex>
#include <QPalette>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTextStream>
#include <QWidget>
#include <windows.h>
#include <dwmapi.h>
#include <ShellScalingApi.h>
#include "MainWindow.h"
#include "WindowManager.h"
#include "SettingsManager.h"

namespace {
QString gLogPath;
QMutex gLogMutex;
qint64 gLogBytes = 0;
constexpr qint64 kMaxLogBytes = 5 * 1024 * 1024;
constexpr int kLogArchiveCount = 4;

void rotateLogFiles()
{
    for (int index = kLogArchiveCount; index >= 1; --index) {
        const QString destination = QStringLiteral("%1.%2").arg(gLogPath).arg(index);
        const QString source = index == 1
            ? gLogPath
            : QStringLiteral("%1.%2").arg(gLogPath).arg(index - 1);
        QFile::remove(destination);
        if (QFile::exists(source)) {
            QFile::rename(source, destination);
        }
    }
    gLogBytes = 0;
}

QColor windowsColor(int index)
{
    const COLORREF color = GetSysColor(index);
    return QColor(GetRValue(color), GetGValue(color), GetBValue(color));
}

QColor blendColors(const QColor& foreground, const QColor& background, qreal foregroundWeight)
{
    const qreal backgroundWeight = 1.0 - foregroundWeight;
    return QColor::fromRgbF(
        foreground.redF() * foregroundWeight + background.redF() * backgroundWeight,
        foreground.greenF() * foregroundWeight + background.greenF() * backgroundWeight,
        foreground.blueF() * foregroundWeight + background.blueF() * backgroundWeight);
}

QPalette systemThemePalette(Qt::ColorScheme scheme)
{
    QPalette palette;
    const QColor systemHighlight = windowsColor(COLOR_HIGHLIGHT);
    QColor highlight = systemHighlight;
    QColor highlightedText = windowsColor(COLOR_HIGHLIGHTTEXT);

    if (scheme == Qt::ColorScheme::Dark) {
        highlight = blendColors(systemHighlight, QColor(QStringLiteral("#2b2b2b")), 0.42);
        highlightedText = QColor(QStringLiteral("#f7f7f7"));
        palette.setColor(QPalette::Window, QColor(QStringLiteral("#202020")));
        palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#f5f5f5")));
        palette.setColor(QPalette::Base, QColor(QStringLiteral("#191919")));
        palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#242424")));
        palette.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#2b2b2b")));
        palette.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#f5f5f5")));
        palette.setColor(QPalette::Text, QColor(QStringLiteral("#f5f5f5")));
        palette.setColor(QPalette::Button, QColor(QStringLiteral("#2b2b2b")));
        palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#f5f5f5")));
        palette.setColor(QPalette::BrightText, Qt::white);
        palette.setColor(QPalette::Light, QColor(QStringLiteral("#3b3b3b")));
        palette.setColor(QPalette::Midlight, QColor(QStringLiteral("#333333")));
        palette.setColor(QPalette::Mid, QColor(QStringLiteral("#2a2a2a")));
        palette.setColor(QPalette::Dark, QColor(QStringLiteral("#161616")));
        palette.setColor(QPalette::Shadow, Qt::black);
        palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#a0a0a0")));
        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(QStringLiteral("#808080")));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor(QStringLiteral("#808080")));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(QStringLiteral("#808080")));
    } else {
        palette.setColor(QPalette::Window, QColor(QStringLiteral("#f3f3f3")));
        palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#1a1a1a")));
        palette.setColor(QPalette::Base, Qt::white);
        palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#f7f7f7")));
        palette.setColor(QPalette::ToolTipBase, Qt::white);
        palette.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#1a1a1a")));
        palette.setColor(QPalette::Text, QColor(QStringLiteral("#1a1a1a")));
        palette.setColor(QPalette::Button, QColor(QStringLiteral("#fbfbfb")));
        palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#1a1a1a")));
        palette.setColor(QPalette::BrightText, Qt::white);
        palette.setColor(QPalette::Light, Qt::white);
        palette.setColor(QPalette::Midlight, QColor(QStringLiteral("#e5e5e5")));
        palette.setColor(QPalette::Mid, QColor(QStringLiteral("#d1d1d1")));
        palette.setColor(QPalette::Dark, QColor(QStringLiteral("#a0a0a0")));
        palette.setColor(QPalette::Shadow, QColor(QStringLiteral("#707070")));
        palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#6b6b6b")));
        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(QStringLiteral("#8a8a8a")));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor(QStringLiteral("#8a8a8a")));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(QStringLiteral("#8a8a8a")));
    }

    palette.setColor(QPalette::Highlight, highlight);
    palette.setColor(QPalette::HighlightedText, highlightedText);
    palette.setColor(QPalette::Link, highlight);
    palette.setColor(QPalette::LinkVisited, highlight.darker(115));
    return palette;
}

class WindowsThemeEventFilter final : public QObject {
public:
    explicit WindowsThemeEventFilter(bool dark, QObject* parent = nullptr)
        : QObject(parent), m_dark(dark)
    {
    }

    void setDark(bool dark)
    {
        m_dark = dark;
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            applyTitleBar(widget);
        }
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if ((event->type() == QEvent::Show || event->type() == QEvent::WinIdChange) &&
            watched->isWidgetType()) {
            QWidget* widget = static_cast<QWidget*>(watched);
            if (widget->isWindow()) {
                applyTitleBar(widget);
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void applyTitleBar(QWidget* widget) const
    {
        if (!widget || !widget->isWindow()) {
            return;
        }
        const BOOL enabled = m_dark ? TRUE : FALSE;
        DwmSetWindowAttribute(reinterpret_cast<HWND>(widget->winId()),
                              DWMWA_USE_IMMERSIVE_DARK_MODE,
                              &enabled,
                              sizeof(enabled));
    }

    bool m_dark = false;
};

void fileMessageHandler(QtMsgType type, const QMessageLogContext&, const QString& message)
{
    QMutexLocker locker(&gLogMutex);
    const qint64 estimatedBytes = static_cast<qint64>(message.size()) * 4 + 64;
    if (gLogBytes + estimatedBytes > kMaxLogBytes) {
        rotateLogFiles();
    }
    QFile file(gLogPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    const char* level = "INFO";
    if (type == QtWarningMsg) level = "WARN";
    else if (type == QtCriticalMsg) level = "ERROR";
    else if (type == QtFatalMsg) level = "FATAL";

    QTextStream out(&file);
    out << QDateTime::currentDateTime().toString(Qt::ISODate)
        << " [" << level << "] " << message << '\n';
    out.flush();
    gLogBytes = file.size();
}

void installFileLogger()
{
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (logDir.isEmpty()) {
        logDir = QDir::homePath() + "/.greenhouse";
    }
    QDir().mkpath(logDir);
    gLogPath = logDir + "/greenhouse.log";
    gLogBytes = QFileInfo(gLogPath).size();
    if (gLogBytes >= kMaxLogBytes) {
        rotateLogFiles();
    }
    qInstallMessageHandler(fileMessageHandler);
}

void enablePerMonitorDpiAwareness()
{
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    }
}

void enableWindowsSystemTheme()
{
    const QByteArray platform = qgetenv("QT_QPA_PLATFORM");
    if (platform.isEmpty() || platform == QByteArrayLiteral("windows")) {
        // Qt's Windows darkmode=2 option follows the Windows app theme and
        // supplies a matching widget palette in addition to the dark title bar.
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("windows:darkmode=2"));
    }
}
}

int main(int argc, char *argv[]) {
    enablePerMonitorDpiAwareness();
    enableWindowsSystemTheme();

    QApplication app(argc, argv);
    QApplication::setOrganizationName("Greenhouse");
    QApplication::setApplicationName("Greenhouse");
    QApplication::setWindowIcon(QIcon(":/icons/greenhouse.png"));
    installFileLogger();
    qInfo() << "Greenhouse build:" << __DATE__ << __TIME__
            << "executable:" << QCoreApplication::applicationFilePath();

    app.styleHints()->setColorScheme(Qt::ColorScheme::Unknown);
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setPalette(systemThemePalette(app.styleHints()->colorScheme()));
    WindowsThemeEventFilter themeEventFilter(
        app.styleHints()->colorScheme() == Qt::ColorScheme::Dark,
        &app);
    app.installEventFilter(&themeEventFilter);
    qInfo() << "System theme initialized. Qt style:"
            << (app.style() ? app.style()->objectName() : QStringLiteral("unknown"))
            << "color scheme:" << app.styleHints()->colorScheme()
            << "platform:" << qEnvironmentVariable("QT_QPA_PLATFORM");
    QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged, &app,
                     [&app, &themeEventFilter](Qt::ColorScheme scheme) {
        app.setPalette(systemThemePalette(scheme));
        themeEventFilter.setDark(scheme == Qt::ColorScheme::Dark);
        qInfo() << "Windows color scheme changed:" << scheme;
    });
    
    // Create managers first
    WindowManager windowManager;
    SettingsManager settingsManager;

    windowManager.loadInitialData(settingsManager.loadSavedWindowEntries());
    
    // Create and show the main window
    MainWindow mainWindow(&windowManager, &settingsManager);
    mainWindow.show();

    return app.exec();
}

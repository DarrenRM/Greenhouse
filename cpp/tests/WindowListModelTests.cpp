#include <QCoreApplication>
#include <QString>

#include "WindowListModel.h"

namespace {
int fail(const QString& message)
{
    qCritical().noquote() << message;
    return 1;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    WindowListModel model;

    WindowInfo saved;
    saved.stableId = QStringLiteral("saved-browser");
    saved.processName = QStringLiteral("brave.exe");
    saved.title = QStringLiteral("Project Notes - Brave");
    saved.isSaved = true;
    saved.savedMonitorDeviceName = QStringLiteral("\\\\.\\DISPLAY2");
    saved.savedMonitorWorkLeft = 0;
    saved.savedMonitorWorkTop = 0;
    saved.savedMonitorWorkWidth = 1920;
    saved.savedMonitorWorkHeight = 1040;
    saved.savedTop = 0;
    saved.savedIsMaximized = true;

    WindowInfo missing;
    missing.stableId = QStringLiteral("saved-missing");
    missing.processName = QStringLiteral("Discord.exe");
    missing.title = QStringLiteral("Discord");
    missing.isSaved = true;
    missing.isActive = false;
    missing.savedHasRunningCandidates = true;
    missing.savedMonitorDeviceName = QStringLiteral("\\\\.\\DISPLAY2");
    missing.savedMonitorWorkLeft = 0;
    missing.savedMonitorWorkTop = 0;
    missing.savedMonitorWorkWidth = 1920;
    missing.savedMonitorWorkHeight = 1040;
    missing.savedTop = 100;

    WindowInfo unsaved;
    unsaved.stableId = QStringLiteral("open-code");
    unsaved.processName = QStringLiteral("Code.exe");
    unsaved.title = QStringLiteral("greenhouse - Visual Studio Code");

    model.updateWindows({ unsaved, missing, saved });

    if (model.rowCount() != 5) {
        return fail(QStringLiteral("Expected two section headers and three window rows."));
    }

    const QModelIndex savedHeader = model.index(0, 0);
    if (savedHeader.data(WindowListModel::RowTypeRole).toInt() != WindowListModel::SectionHeaderRow ||
        savedHeader.data(WindowListModel::TitleRole).toString() != QStringLiteral("Saved layout") ||
        savedHeader.data(WindowListModel::SectionCountRole).toInt() != 2) {
        return fail(QStringLiteral("Saved layout header is incorrect."));
    }

    const QModelIndex savedIndex = model.index(1, 0);
    const QString savedTitle = savedIndex.data(WindowListModel::TitleRole).toString();
    const QString savedSubtitle = savedIndex.data(WindowListModel::SubtitleRole).toString();
    if (savedTitle != QStringLiteral("Project Notes - Brave") ||
        savedSubtitle != QStringLiteral("Monitor 1  |  Maximized")) {
        return fail(QStringLiteral("Unexpected saved row: %1 / %2").arg(savedTitle, savedSubtitle));
    }
    if (savedTitle.contains(QStringLiteral("brave.exe"), Qt::CaseInsensitive)) {
        return fail(QStringLiteral("Saved row exposed its executable name."));
    }
    if (model.flags(savedIndex).testFlag(Qt::ItemIsSelectable)) {
        return fail(QStringLiteral("Window rows must remain non-selectable."));
    }
    if (savedIndex.data(WindowListModel::StableIdRole).toString() !=
        QStringLiteral("saved-browser")) {
        return fail(QStringLiteral("Saved rows must expose their durable entry ID for removal."));
    }

    const QModelIndex missingIndex = model.index(2, 0);
    if (missingIndex.data(WindowListModel::SubtitleRole).toString() !=
        QStringLiteral("Not matched  |  Monitor 1  |  Windowed")) {
        return fail(QStringLiteral("Missing saved row status is incorrect."));
    }

    const QModelIndex otherHeader = model.index(3, 0);
    if (otherHeader.data(WindowListModel::TitleRole).toString() !=
            QStringLiteral("Other open windows") ||
        otherHeader.data(WindowListModel::SectionCountRole).toInt() != 1) {
        return fail(QStringLiteral("Other open windows header is incorrect."));
    }

    const QModelIndex unsavedIndex = model.index(4, 0);
    if (unsavedIndex.data(WindowListModel::TitleRole).toString() !=
            QStringLiteral("greenhouse - Visual Studio Code") ||
        !unsavedIndex.data(WindowListModel::SubtitleRole).toString().isEmpty()) {
        return fail(QStringLiteral("Unsaved row should contain only its title."));
    }

    WindowInfo leftMonitor;
    leftMonitor.stableId = QStringLiteral("left-monitor-app");
    leftMonitor.title = QStringLiteral("Side monitor app");
    leftMonitor.isSaved = true;
    leftMonitor.savedMonitorDeviceName = QStringLiteral("\\\\.\\DISPLAY6");
    leftMonitor.savedMonitorWorkLeft = 0;
    leftMonitor.savedMonitorWorkTop = 0;
    leftMonitor.savedMonitorWorkWidth = 1920;
    leftMonitor.savedMonitorWorkHeight = 1040;

    WindowInfo rightMonitor;
    rightMonitor.stableId = QStringLiteral("right-monitor-app");
    rightMonitor.title = QStringLiteral("Main monitor app");
    rightMonitor.isSaved = true;
    rightMonitor.savedMonitorDeviceName = QStringLiteral("\\\\.\\DISPLAY7");
    rightMonitor.savedMonitorWorkLeft = 1920;
    rightMonitor.savedMonitorWorkTop = 0;
    rightMonitor.savedMonitorWorkWidth = 3840;
    rightMonitor.savedMonitorWorkHeight = 2080;

    model.updateWindows({ rightMonitor, leftMonitor });
    if (model.rowCount() != 4) {
        return fail(QStringLiteral("Renumbered monitor scenario should have two headers and two saved rows."));
    }
    if (model.index(1, 0).data(WindowListModel::TitleRole).toString() !=
            QStringLiteral("Side monitor app") ||
        model.index(1, 0).data(WindowListModel::SubtitleRole).toString() !=
            QStringLiteral("Monitor 1  |  Windowed")) {
        return fail(QStringLiteral("DISPLAY6 should be shown as the first saved monitor, not Monitor 6."));
    }
    if (model.index(2, 0).data(WindowListModel::TitleRole).toString() !=
            QStringLiteral("Main monitor app") ||
        model.index(2, 0).data(WindowListModel::SubtitleRole).toString() !=
            QStringLiteral("Monitor 2  |  Windowed")) {
        return fail(QStringLiteral("DISPLAY7 should be shown as the second saved monitor, not Monitor 7."));
    }

    return 0;
}

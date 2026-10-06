#include "../include/SettingsManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <nlohmann/json.hpp>

#include <fstream>

namespace {
int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        qCritical() << "FAIL:" << message;
        ++failures;
    }
}

SavedWindowEntry entryWithTitle(const QString& id, const QString& title)
{
    SavedWindowEntry entry;
    entry.id = id;
    entry.info.title = title;
    entry.info.processName = QStringLiteral("Example.exe");
    entry.info.processPath = QStringLiteral("C:\\Example\\Example.exe");
    entry.info.className = QStringLiteral("ExampleWindow");
    entry.position.left = 10;
    entry.position.top = 20;
    entry.position.width = 800;
    entry.position.height = 600;
    entry.position.monitorDeviceName = QStringLiteral("\\\\.\\DISPLAY1");
    entry.position.monitorStableId = QStringLiteral("physical-monitor-one");
    entry.position.monitorWorkWidth = 1920;
    entry.position.monitorWorkHeight = 1040;
    entry.position.isMonitorRelative = true;
    entry.position.hasNormalBounds = true;
    return entry;
}

int backupFileCount(const QString& backupDirectory)
{
    QDir dir(backupDirectory);
    return dir.entryList(QStringList() << QStringLiteral("saved_positions_*.json"),
                         QDir::Files).size();
}

QString firstSavedTitle(const QString& path)
{
    std::ifstream fileStream(path.toStdString());
    if (!fileStream.is_open()) {
        return QString();
    }

    nlohmann::json json;
    fileStream >> json;
    if (!json.contains("windows") || !json["windows"].is_object() || json["windows"].empty()) {
        return QString();
    }

    const auto first = json["windows"].begin();
    return QString::fromStdString(first.value()["info"]["title"].get<std::string>());
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tempDir;
    expect(tempDir.isValid(), "temporary settings directory should be valid");
    const QString settingsPath = QDir(tempDir.path()).filePath(QStringLiteral("saved_positions.json"));
    SettingsManager manager(settingsPath);

    manager.saveSavedWindowEntries({entryWithTitle(QStringLiteral("window:one"), QStringLiteral("Layout One"))});
    expect(QFile::exists(settingsPath), "initial save should create settings file");
    expect(backupFileCount(manager.getLayoutBackupDirectoryPath()) == 0,
           "initial save should not create a backup when no previous layout exists");

    manager.saveSavedWindowEntries({entryWithTitle(QStringLiteral("window:one"), QStringLiteral("Layout Two"))});
    expect(backupFileCount(manager.getLayoutBackupDirectoryPath()) == 1,
           "second save should create one timestamped backup");
    expect(firstSavedTitle(settingsPath + QStringLiteral(".last_good")) == QStringLiteral("Layout One"),
           "last_good should contain the previous layout, not the new one");
    expect(firstSavedTitle(settingsPath) == QStringLiteral("Layout Two"),
           "current settings file should contain the new layout");

    manager.saveSavedWindowEntries({entryWithTitle(QStringLiteral("window:one"), QStringLiteral("Layout Three"))});
    expect(backupFileCount(manager.getLayoutBackupDirectoryPath()) == 2,
           "third save should keep another timestamped backup");
    expect(firstSavedTitle(settingsPath + QStringLiteral(".last_good")) == QStringLiteral("Layout Two"),
           "last_good should advance to the layout that was current before the latest save");

    manager.saveSavedWindowEntries({
        entryWithTitle(QStringLiteral("window:replacement"), QStringLiteral("Replacement Layout"))
    });
    const QList<SavedWindowEntry> replacementEntries = manager.loadSavedWindowEntries();
    expect(replacementEntries.size() == 1 &&
               replacementEntries.first().id == QStringLiteral("window:replacement"),
           "saving a new capture should replace stale layout entries rather than append them");

    SavedWindowEntry braveEntry = entryWithTitle(QStringLiteral("window:brave"), QStringLiteral("Browser Layout"));
    braveEntry.info.processName = QStringLiteral("brave.exe");
    braveEntry.info.processPath = QStringLiteral("C:\\Program Files\\BraveSoftware\\Brave-Browser\\Application\\brave.exe");
    braveEntry.info.className = QStringLiteral("Chrome_WidgetWin_1");
    braveEntry.browserFingerprint = BrowserFingerprinting::fromTabTitles(
        braveEntry.info,
        {
            QStringLiteral("Codex - Pinned"),
            QStringLiteral("Project Console"),
            QStringLiteral("Release Checklist")
        },
        QStringLiteral("Project Console"));
    manager.saveSavedWindowEntries({braveEntry});
    const QList<SavedWindowEntry> loadedEntries = manager.loadSavedWindowEntries();
    expect(loadedEntries.size() == 1,
           "loading a saved browser layout should return one entry");
    expect(!loadedEntries.isEmpty() && loadedEntries.first().browserFingerprint.isValid,
           "browser fingerprints should persist through settings save/load");
    expect(!loadedEntries.isEmpty() &&
               loadedEntries.first().browserFingerprint.tabTitleHashes ==
                   braveEntry.browserFingerprint.tabTitleHashes,
           "browser fingerprint tab hashes should survive serialization");
    expect(!loadedEntries.isEmpty() &&
               loadedEntries.first().position.monitorStableId == QStringLiteral("physical-monitor-one"),
           "stable monitor identity should survive serialization");
    expect(!loadedEntries.isEmpty() &&
                loadedEntries.first().position.monitorWorkWidth == 1920 &&
                loadedEntries.first().position.monitorWorkHeight == 1040,
           "monitor work-area dimensions should survive serialization");
    expect(!loadedEntries.isEmpty() && loadedEntries.first().position.hasNormalBounds,
           "normal-bounds provenance should survive serialization");

    {
        std::ifstream input(settingsPath.toStdString());
        nlohmann::json legacyJson;
        input >> legacyJson;
        legacyJson["schemaVersion"] = 3;
        for (auto& [id, window] : legacyJson["windows"].items()) {
            Q_UNUSED(id);
            window["position"].erase("hasNormalBounds");
        }
        std::ofstream output(settingsPath.toStdString(), std::ios::trunc);
        output << legacyJson.dump(2);
    }
    const QList<SavedWindowEntry> legacyEntries = manager.loadSavedWindowEntries();
    expect(!legacyEntries.isEmpty() && !legacyEntries.first().position.hasNormalBounds,
           "layouts from before schema 4 should use conservative legacy maximized handling");

    if (failures > 0) {
        qCritical() << failures << "settings manager test(s) failed.";
        return 1;
    }

    qInfo() << "All settings manager tests passed.";
    return 0;
}

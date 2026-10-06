#pragma once

#include <QObject>
#include <QString>
#include <QHash>
#include <QList>
#include <windows.h>

#include "WindowInfo.h"
#include "WindowPosition.h"
#include "SavedWindowEntry.h"

// nlohmann::json is included via WindowInfo/Position headers

class SettingsManager : public QObject {
    Q_OBJECT

public:
    explicit SettingsManager(QObject *parent = nullptr);
    explicit SettingsManager(const QString& settingsFilePath, QObject *parent = nullptr);

    // --- Startup Setting --- 
    void setStartup(bool enabled);
    bool getStartup() const;

    // --- Window List Persistence --- 
    // Using QHash for potentially faster lookups based on HWND
    // We need both Info (for matching) and Position
    // Consider storing a combined struct if more logical
    QHash<HWND, WindowPosition> loadWindowList(QHash<HWND, WindowInfo>& outWindowInfo); // Returns positions, populates info
    void saveWindowList(const QHash<HWND, WindowPosition>& positions, const QHash<HWND, WindowInfo>& infos);
    QList<SavedWindowEntry> loadSavedWindowEntries();
    void saveSavedWindowEntries(const QList<SavedWindowEntry>& entries);

    // --- File Path --- 
    QString getSettingsFilePath() const;
    QString getLayoutBackupDirectoryPath() const;

private:
    QString m_settingsFilePath;
    static QString defaultSettingsFilePath();
    void ensureSettingsDirectoryExists();
    bool backupCurrentSavedLayout();
    void pruneSavedLayoutBackups(int maxBackups);

    // No need for custom toJson/fromJson helpers here if using NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE
}; 

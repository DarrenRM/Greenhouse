#include "../include/SettingsManager.h"

#include <QSettings>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>
#include <fstream> // For reading/writing json file

// Include nlohmann/json via WindowInfo/Position headers

// Custom JSON serialization functions for WindowInfo
// These need to be defined before they are used in load/save
namespace nlohmann {
    template <>
    struct adl_serializer<WindowInfo> {
        static void to_json(json& j, const WindowInfo& info) {
            j = json{
                {"title", info.title.toStdString()},
                {"processName", info.processName.toStdString()},
                {"processPath", info.processPath.toStdString()},
                {"className", info.className.toStdString()},
                {"isActive", info.isActive} // Serialize isActive flag
            };
        }

        static void from_json(const json& j, WindowInfo& info) {
            // Explicitly get std::string and convert to QString
            info.title = QString::fromStdString(j.at("title").get<std::string>());
            info.processName = QString::fromStdString(j.at("processName").get<std::string>());
            info.processPath = QString::fromStdString(j.at("processPath").get<std::string>());
            info.className = QString::fromStdString(j.at("className").get<std::string>());
            j.at("isActive").get_to(info.isActive); // Bool conversion is fine
            // HWND is set separately during load
            // QIcon is loaded separately
        }
    };

    /* // We also need conversion for QString specifically // MOVED TO WindowInfo.h
    template <>
    struct adl_serializer<QString> {
        inline static void to_json(json& j, const QString& s) {
            j = s.toStdString();
        }

        inline static void from_json(const json& j, QString& s) {
            s = QString::fromStdString(j.get<std::string>());
        }
    }; */
} // namespace nlohmann

// No need for `using json = nlohmann::json;` if using full namespace

QString SettingsManager::defaultSettingsFilePath()
{
    // Determine settings path using QStandardPaths
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataPath.isEmpty()) {
        // Fallback if AppDataLocation isn't available (shouldn't happen on Windows usually)
        dataPath = QDir::homePath() + "/.greenhouse";
        qWarning() << "Could not determine AppDataLocation, using fallback:" << dataPath;
    } else {
        // Append our specific subfolder/file if needed
        dataPath += "/Greenhouse"; // Use application name
    }

    return dataPath + "/saved_positions.json";
}

// Constructor: Initialize file path
SettingsManager::SettingsManager(QObject *parent)
    : SettingsManager(defaultSettingsFilePath(), parent)
{
}

SettingsManager::SettingsManager(const QString& settingsFilePath, QObject *parent)
    : QObject(parent),
      m_settingsFilePath(settingsFilePath)
{
    ensureSettingsDirectoryExists();
    qInfo() << "Settings file path set to:" << m_settingsFilePath;
}

// Ensure the directory for the settings file exists
void SettingsManager::ensureSettingsDirectoryExists() {
    QDir dir(QFileInfo(m_settingsFilePath).path());
    if (!dir.exists()) {
        if (dir.mkpath(".")) {
            qInfo() << "Created settings directory:" << dir.path();
        } else {
            qWarning() << "Failed to create settings directory:" << dir.path();
        }
    }
}

// Get the full path to the settings file
QString SettingsManager::getSettingsFilePath() const {
    return m_settingsFilePath;
}

QString SettingsManager::getLayoutBackupDirectoryPath() const {
    return QFileInfo(m_settingsFilePath).dir().filePath(QStringLiteral("backups"));
}

// --- Startup Setting --- 

void SettingsManager::setStartup(bool enabled) {
    // Use QSettings to interact with the registry
    // HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run
    QSettings settings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                       QSettings::NativeFormat);

    QString appName = QCoreApplication::applicationName(); // Use the application name defined elsewhere
    if (appName.isEmpty()) {
        appName = "Greenhouse"; // Fallback name
        qWarning() << "Application name not set, using default 'Greenhouse' for startup key.";
    }

    if (enabled) {
        QString appPath = QCoreApplication::applicationFilePath();
        // Ensure path is quoted correctly for registry
        settings.setValue(appName, QString("\"%1\"").arg(QDir::toNativeSeparators(appPath)));
        qInfo() << "Set startup enabled in registry for:" << appName << "Path:" << appPath;
    } else {
        settings.remove(appName);
        qInfo() << "Set startup disabled in registry for:" << appName;
    }
}

bool SettingsManager::getStartup() const {
    QSettings settings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                       QSettings::NativeFormat);
    QString appName = QCoreApplication::applicationName();
    if (appName.isEmpty()) {
        appName = "Greenhouse";
    }
    return settings.contains(appName);
}

// --- Window List Persistence --- 

// No custom to_json needed if using NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE

// Load window list from JSON file
QHash<HWND, WindowPosition> SettingsManager::loadWindowList(QHash<HWND, WindowInfo>& outWindowInfo) {
    outWindowInfo.clear();
    QHash<HWND, WindowPosition> loadedPositions;

    std::ifstream fileStream(m_settingsFilePath.toStdString());
    if (!fileStream.is_open()) {
        qWarning() << "Could not open settings file for reading:" << m_settingsFilePath;
        return loadedPositions; // Return empty hash
    }

    try {
        nlohmann::json j;
        fileStream >> j;

        if (j.contains("windows") && j["windows"].is_object()) {
            for (auto& [key, value] : j["windows"].items()) {
                try {
                    uintptr_t hwnd_val = std::stoull(key);
                    HWND hwnd = reinterpret_cast<HWND>(hwnd_val);

                    if (value.contains("info") && value["info"].is_object()) {
                        // Use automatic deserialization
                        WindowInfo info = value["info"].get<WindowInfo>();
                        info.hwnd = hwnd; // Assign HWND from key
                        outWindowInfo[hwnd] = info;
                    }

                    if (value.contains("position") && value["position"].is_object()) {
                        // Use automatic deserialization
                        WindowPosition pos = value["position"].get<WindowPosition>();
                        loadedPositions[hwnd] = pos;
                    }
                } catch (const std::exception& parseEx) {
                    qWarning() << "Error parsing entry for key" << QString::fromStdString(key) << ":" << parseEx.what();
                }
            }
        }
    } catch (const nlohmann::json::parse_error& e) {
        qWarning() << "Failed to parse settings file:" << m_settingsFilePath << "Error:" << e.what();
    } catch (const std::exception& e) {
        qCritical() << "Unexpected error reading settings file:" << e.what();
    }

    qInfo() << "Loaded" << loadedPositions.size() << "window positions from" << m_settingsFilePath;
    return loadedPositions;
}

// Save window list to JSON file
void SettingsManager::saveWindowList(const QHash<HWND, WindowPosition>& positions, const QHash<HWND, WindowInfo>& infos) {
    nlohmann::json windowsJson = nlohmann::json::object(); // Explicitly create an object

    for (auto it = positions.constBegin(); it != positions.constEnd(); ++it) {
        HWND hwnd = it.key();
        const WindowPosition& pos = it.value();

        if (infos.contains(hwnd)) {
             const WindowInfo& info = infos.value(hwnd);
             // Convert HWND to string for key
             std::string hwndStr = std::to_string(reinterpret_cast<uintptr_t>(hwnd));
             // Use automatic serialization
             windowsJson[hwndStr] = {
                 {"info", info},
                 {"position", pos}
             };
        } else {
            qWarning() << "Skipping save for HWND" << hwnd << "as matching WindowInfo was not provided.";
        }
    }

    nlohmann::json finalJson;
    finalJson["windows"] = windowsJson;
    // Could add other top-level settings here
    // finalJson["settings"] = { ... }; 

    try {
        QString tempPath = m_settingsFilePath + ".tmp";
        std::ofstream fileStream(tempPath.toStdString());
        if (!fileStream.is_open()) {
            qWarning() << "Could not open temp settings file for writing:" << tempPath;
            return;
        }
        fileStream << finalJson.dump(4); // Pretty print with 4 spaces
        fileStream.close();
        QFile::remove(m_settingsFilePath);
        if (!QFile::rename(tempPath, m_settingsFilePath)) {
            qWarning() << "Could not replace settings file with temp file:" << tempPath;
            return;
        }
        qInfo() << "Saved" << windowsJson.size() << "window positions to" << m_settingsFilePath;
    } catch (const std::exception& e) {
        qCritical() << "Error writing settings file:" << e.what();
    }
} 

static QString jsonStringValue(const nlohmann::json& object,
                               std::initializer_list<const char*> names,
                               const QString& fallback = QString())
{
    for (const char* name : names) {
        if (object.contains(name) && object[name].is_string()) {
            return QString::fromStdString(object[name].get<std::string>());
        }
    }
    return fallback;
}

static bool jsonBoolValue(const nlohmann::json& object,
                          std::initializer_list<const char*> names,
                          bool fallback = false)
{
    for (const char* name : names) {
        if (object.contains(name) && object[name].is_boolean()) {
            return object[name].get<bool>();
        }
    }
    return fallback;
}

static int jsonIntValue(const nlohmann::json& object,
                        std::initializer_list<const char*> names,
                        int fallback = 0)
{
    for (const char* name : names) {
        if (object.contains(name) && object[name].is_number_integer()) {
            return object[name].get<int>();
        }
    }
    return fallback;
}

static double jsonDoubleValue(const nlohmann::json& object,
                              std::initializer_list<const char*> names,
                              double fallback = 1.0)
{
    for (const char* name : names) {
        if (object.contains(name) && object[name].is_number()) {
            return object[name].get<double>();
        }
    }
    return fallback;
}

static QStringList jsonStringListValue(const nlohmann::json& object,
                                       std::initializer_list<const char*> names)
{
    for (const char* name : names) {
        if (!object.contains(name) || !object[name].is_array()) {
            continue;
        }

        QStringList values;
        for (const auto& value : object[name]) {
            if (value.is_string()) {
                values.append(QString::fromStdString(value.get<std::string>()));
            }
        }
        return values;
    }
    return {};
}

static uintptr_t jsonUintPtrValue(const nlohmann::json& object,
                                  std::initializer_list<const char*> names,
                                  uintptr_t fallback = 0)
{
    for (const char* name : names) {
        if (object.contains(name) && object[name].is_number_unsigned()) {
            return object[name].get<uintptr_t>();
        }
        if (object.contains(name) && object[name].is_number_integer()) {
            return static_cast<uintptr_t>(object[name].get<long long>());
        }
    }
    return fallback;
}

static int countSavedWindowsInFile(const QString& path)
{
    std::ifstream fileStream(path.toStdString());
    if (!fileStream.is_open()) {
        return -1;
    }

    try {
        nlohmann::json j;
        fileStream >> j;
        if (j.contains("windows") && j["windows"].is_object()) {
            return static_cast<int>(j["windows"].size());
        }
    } catch (const std::exception&) {
        return -1;
    }

    return -1;
}

static nlohmann::json stringListToJson(const QStringList& values)
{
    nlohmann::json array = nlohmann::json::array();
    for (const QString& value : values) {
        array.push_back(value.toStdString());
    }
    return array;
}

static nlohmann::json browserFingerprintToJson(const BrowserFingerprint& fingerprint)
{
    return {
        {"isValid", fingerprint.isValid},
        {"browserFamily", fingerprint.browserFamily.toStdString()},
        {"activeTitleHash", fingerprint.activeTitleHash.toStdString()},
        {"tabTitleHashes", stringListToJson(fingerprint.tabTitleHashes)},
        {"pinnedTabTitleHashes", stringListToJson(fingerprint.pinnedTabTitleHashes)},
        {"tabCount", fingerprint.tabCount},
        {"pinnedTabCount", fingerprint.pinnedTabCount}
    };
}

static BrowserFingerprint browserFingerprintFromJson(const nlohmann::json& object)
{
    BrowserFingerprint fingerprint;
    if (!object.is_object()) {
        return fingerprint;
    }

    fingerprint.isValid = jsonBoolValue(object, {"isValid", "is_valid"}, false);
    fingerprint.browserFamily = jsonStringValue(object, {"browserFamily", "browser_family"});
    fingerprint.activeTitleHash = jsonStringValue(object, {"activeTitleHash", "active_title_hash"});
    fingerprint.tabTitleHashes = jsonStringListValue(object, {"tabTitleHashes", "tab_title_hashes"});
    fingerprint.pinnedTabTitleHashes = jsonStringListValue(object, {"pinnedTabTitleHashes", "pinned_tab_title_hashes"});
    fingerprint.tabCount = jsonIntValue(object, {"tabCount", "tab_count"}, fingerprint.tabTitleHashes.size());
    fingerprint.pinnedTabCount = jsonIntValue(object, {"pinnedTabCount", "pinned_tab_count"}, fingerprint.pinnedTabTitleHashes.size());
    fingerprint.isValid = fingerprint.isValid && !fingerprint.browserFamily.isEmpty() &&
                          !fingerprint.tabTitleHashes.isEmpty();
    return fingerprint;
}

bool SettingsManager::backupCurrentSavedLayout()
{
    if (!QFile::exists(m_settingsFilePath)) {
        return false;
    }

    const int savedWindowCount = countSavedWindowsInFile(m_settingsFilePath);
    if (savedWindowCount <= 0) {
        qWarning() << "Skipping saved layout backup because current file has no valid windows:"
                   << m_settingsFilePath;
        return false;
    }

    QDir backupDir(getLayoutBackupDirectoryPath());
    if (!backupDir.exists() && !backupDir.mkpath(QStringLiteral("."))) {
        qWarning() << "Could not create saved layout backup directory:" << backupDir.path();
        return false;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    QString backupFileName = QStringLiteral("saved_positions_%1.json").arg(timestamp);
    QString backupPath = backupDir.filePath(backupFileName);
    int suffix = 2;
    while (QFile::exists(backupPath)) {
        backupFileName = QStringLiteral("saved_positions_%1_%2.json").arg(timestamp).arg(suffix++);
        backupPath = backupDir.filePath(backupFileName);
    }

    if (!QFile::copy(m_settingsFilePath, backupPath)) {
        qWarning() << "Could not create timestamped saved layout backup:" << backupPath;
        return false;
    }

    const QString lastGoodPath = m_settingsFilePath + QStringLiteral(".last_good");
    QFile::remove(lastGoodPath);
    if (!QFile::copy(m_settingsFilePath, lastGoodPath)) {
        qWarning() << "Could not update last-good saved layout backup:" << lastGoodPath;
    }

    pruneSavedLayoutBackups(20);
    qInfo() << "Backed up current saved layout with" << savedWindowCount
            << "windows to" << backupPath;
    return true;
}

void SettingsManager::pruneSavedLayoutBackups(int maxBackups)
{
    QDir backupDir(getLayoutBackupDirectoryPath());
    if (!backupDir.exists()) {
        return;
    }

    const QFileInfoList backups = backupDir.entryInfoList(
        QStringList() << QStringLiteral("saved_positions_*.json"),
        QDir::Files,
        QDir::Time);

    for (int i = maxBackups; i < backups.size(); ++i) {
        if (!QFile::remove(backups.at(i).absoluteFilePath())) {
            qWarning() << "Could not prune old saved layout backup:"
                       << backups.at(i).absoluteFilePath();
        }
    }
}

QList<SavedWindowEntry> SettingsManager::loadSavedWindowEntries()
{
    QList<SavedWindowEntry> entries;

    std::ifstream fileStream(m_settingsFilePath.toStdString());
    if (!fileStream.is_open()) {
        qWarning() << "Could not open settings file for reading:" << m_settingsFilePath;
        return entries;
    }

    try {
        nlohmann::json j;
        fileStream >> j;

        if (!j.contains("windows") || !j["windows"].is_object()) {
            return entries;
        }

        for (auto& [key, value] : j["windows"].items()) {
            if (!value.is_object() || !value.contains("info") || !value["info"].is_object() ||
                !value.contains("position") || !value["position"].is_object()) {
                continue;
            }

            const auto& infoJson = value["info"];
            const auto& posJson = value["position"];

            SavedWindowEntry entry;
            entry.id = jsonStringValue(value, {"id"}, QString::fromStdString(key));
            entry.info.title = jsonStringValue(infoJson, {"title"});
            entry.info.processName = jsonStringValue(infoJson, {"processName", "process_name"});
            entry.info.processPath = jsonStringValue(infoJson, {"processPath", "process_path"});
            entry.info.className = jsonStringValue(infoJson, {"className", "class_name"});
            entry.info.isActive = false;
            entry.isActive = jsonBoolValue(infoJson, {"isActive", "is_active"}, false);

            try {
                uintptr_t hwndVal = std::stoull(key);
                entry.currentHwnd = reinterpret_cast<HWND>(hwndVal);
                entry.info.hwnd = entry.currentHwnd;
            } catch (...) {
                uintptr_t hwndVal = jsonUintPtrValue(value, {"lastHwnd"}, 0);
                entry.currentHwnd = reinterpret_cast<HWND>(hwndVal);
                entry.info.hwnd = entry.currentHwnd;
            }

            entry.position.left = jsonIntValue(posJson, {"left"});
            entry.position.top = jsonIntValue(posJson, {"top"});
            entry.position.width = jsonIntValue(posJson, {"width"});
            entry.position.height = jsonIntValue(posJson, {"height"});
            entry.position.dpiScale = jsonDoubleValue(posJson, {"dpiScale", "dpi_scale"}, 1.0);
            entry.position.monitorDeviceName = jsonStringValue(posJson, {"monitorDeviceName", "monitor_device_name"});
            entry.position.monitorStableId = jsonStringValue(posJson, {"monitorStableId", "monitor_stable_id"});
            entry.position.monitorWorkLeft = jsonIntValue(posJson, {"monitorWorkLeft", "monitor_work_left"});
            entry.position.monitorWorkTop = jsonIntValue(posJson, {"monitorWorkTop", "monitor_work_top"});
            entry.position.monitorWorkWidth = jsonIntValue(posJson, {"monitorWorkWidth", "monitor_work_width"});
            entry.position.monitorWorkHeight = jsonIntValue(posJson, {"monitorWorkHeight", "monitor_work_height"});
            entry.position.isMonitorRelative = jsonBoolValue(posJson, {"isMonitorRelative", "is_monitor_relative"}, false);
            entry.position.hasNormalBounds = jsonBoolValue(posJson, {"hasNormalBounds", "has_normal_bounds"}, false);
            entry.position.isMaximized = jsonBoolValue(posJson, {"isMaximized", "is_maximized"}, false);
            entry.position.isMinimized = jsonBoolValue(posJson, {"isMinimized", "is_minimized"}, false);

            if (value.contains("browserFingerprint")) {
                entry.browserFingerprint = browserFingerprintFromJson(value["browserFingerprint"]);
            }

            if (entry.position.width > 0 && entry.position.height > 0) {
                entries.append(entry);
            }
        }
    } catch (const std::exception& e) {
        qWarning() << "Failed to load saved layout entries:" << e.what();
    }

    qInfo() << "Loaded" << entries.size() << "saved layout entries from" << m_settingsFilePath;
    return entries;
}

void SettingsManager::saveSavedWindowEntries(const QList<SavedWindowEntry>& entries)
{
    nlohmann::json windowsJson = nlohmann::json::object();

    for (const SavedWindowEntry& entry : entries) {
        if (entry.id.isEmpty()) {
            continue;
        }

        windowsJson[entry.id.toStdString()] = {
            {"id", entry.id.toStdString()},
            {"lastHwnd", reinterpret_cast<uintptr_t>(entry.currentHwnd)},
            {"browserFingerprint", browserFingerprintToJson(entry.browserFingerprint)},
            {"info", {
                {"title", entry.info.title.toStdString()},
                {"processName", entry.info.processName.toStdString()},
                {"processPath", entry.info.processPath.toStdString()},
                {"className", entry.info.className.toStdString()},
                {"isActive", false}
            }},
            {"position", {
                {"left", entry.position.left},
                {"top", entry.position.top},
                {"width", entry.position.width},
                {"height", entry.position.height},
                {"dpiScale", entry.position.dpiScale},
                {"monitorDeviceName", entry.position.monitorDeviceName.toStdString()},
                {"monitorStableId", entry.position.monitorStableId.toStdString()},
                {"monitorWorkLeft", entry.position.monitorWorkLeft},
                {"monitorWorkTop", entry.position.monitorWorkTop},
                {"monitorWorkWidth", entry.position.monitorWorkWidth},
                {"monitorWorkHeight", entry.position.monitorWorkHeight},
                {"isMonitorRelative", entry.position.isMonitorRelative},
                {"hasNormalBounds", entry.position.hasNormalBounds},
                {"isMaximized", entry.position.isMaximized},
                {"isMinimized", entry.position.isMinimized}
            }}
        };
    }

    nlohmann::json finalJson;
    finalJson["schemaVersion"] = 4;
    finalJson["windows"] = windowsJson;

    try {
        backupCurrentSavedLayout();

        QSaveFile file(m_settingsFilePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qWarning() << "Could not open settings file for atomic writing:" << m_settingsFilePath
                       << file.errorString();
            return;
        }

        const QByteArray payload = QByteArray::fromStdString(finalJson.dump(4));
        if (file.write(payload) != payload.size()) {
            qWarning() << "Could not write complete saved layout file:" << m_settingsFilePath
                       << file.errorString();
            return;
        }

        if (!file.commit()) {
            qWarning() << "Could not commit saved layout file:" << m_settingsFilePath
                       << file.errorString();
            return;
        }

        qInfo() << "Saved" << entries.size() << "saved layout entries to" << m_settingsFilePath;
    } catch (const std::exception& e) {
        qCritical() << "Error writing saved layout entries:" << e.what();
    }
}

#pragma once

#include <windows.h>
#include <QString>
#include <QIcon>
#include <nlohmann/json.hpp>

// Forward declare to avoid include loop if needed elsewhere
struct WindowPosition;

struct WindowInfo {
    HWND hwnd = nullptr;
    QString title;
    QString processName;
    QString processPath;
    QString className;
    bool isActive = true;
    QIcon icon;
    QString stableId;
    QString iconKey;
    bool isSaved = false;
    bool savedHasRunningCandidates = false;
    QString savedMonitorDeviceName;
    QString savedMonitorStableId;
    int savedMonitorWorkLeft = 0;
    int savedMonitorWorkTop = 0;
    int savedMonitorWorkWidth = 0;
    int savedMonitorWorkHeight = 0;
    int savedLeft = 0;
    int savedTop = 0;
    bool savedIsMaximized = false;
    bool savedIsMinimized = false;

    // Add constructors, comparison operators etc. as needed
};

// Remove the macro as we need custom serialization for QString
// NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WindowInfo, title, processName, processPath, className, isActive)

// Define QString serialization inline in the header
namespace nlohmann {
    template <>
    struct adl_serializer<QString> {
        inline static void to_json(json& j, const QString& s) {
            j = s.toStdString();
        }
        inline static void from_json(const json& j, QString& s) {
            s = QString::fromStdString(j.get<std::string>());
        }
    };
} // namespace nlohmann 

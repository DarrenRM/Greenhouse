#pragma once

#include <QRect> // Include QRect for potential use
#include <QString>
#include <nlohmann/json.hpp> // Needed for JSON macro

struct WindowPosition {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    double dpiScale = 1.0;
    QString monitorDeviceName;
    QString monitorStableId;
    int monitorWorkLeft = 0;
    int monitorWorkTop = 0;
    int monitorWorkWidth = 0;
    int monitorWorkHeight = 0;
    bool isMonitorRelative = false;
    bool hasNormalBounds = false;
    bool isMaximized = false;
    bool isMinimized = false;

    // Optional: Convenience function to return a QRect
    // QRect toQtRect() const {
    //     return QRect(left, top, width, height);
    // }
};

// Helper macro for nlohmann::json serialization
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WindowPosition, left, top, width, height, dpiScale) 

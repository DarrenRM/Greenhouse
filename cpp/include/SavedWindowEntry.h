#pragma once

#include <windows.h>
#include <QString>
#include <nlohmann/json.hpp>

#include "BrowserFingerprint.h"
#include "WindowInfo.h"
#include "WindowPosition.h"

struct SavedWindowEntry {
    QString id;
    WindowInfo info;
    WindowPosition position;
    BrowserFingerprint browserFingerprint;
    HWND currentHwnd = nullptr;
    bool currentHwndTrusted = false;
    bool isActive = false;
};

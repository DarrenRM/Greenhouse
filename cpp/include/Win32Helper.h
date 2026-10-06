#pragma once

#include <windows.h>
#include <QString>
#include <vector>
#include <string>

// Forward declaration if needed
struct WindowInfo;

namespace Win32Helper {

    // Example wrapper functions (implementations will be in .cpp)
    QString GetWindowTextString(HWND hwnd);
    QString GetWindowClassNameString(HWND hwnd);
    QString GetProcessName(HWND hwnd);
    QString GetProcessPath(HWND hwnd);

    // Add more helper functions as needed...

} // namespace Win32Helper 
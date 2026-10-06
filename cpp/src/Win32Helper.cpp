#include "../include/Win32Helper.h"
#include <psapi.h> // For GetModuleFileNameEx, GetModuleBaseName
#include <stdexcept> // For std::runtime_error
#include <QDebug>    // For qWarning, qCritical

namespace Win32Helper {

    // --- Implementation of GetWindowTextString ---
    QString GetWindowTextString(HWND hwnd) {
        int length = GetWindowTextLengthW(hwnd);
        // Check if GetWindowTextLengthW failed (though it usually returns 0 for errors too)
        // If hwnd is invalid, length is 0. If other error, it returns 0 and sets LastError.
        if (length == 0) {
            DWORD lastError = GetLastError();
            if (lastError != 0 && hwnd != NULL && !IsWindow(hwnd)) { // Check if handle itself is invalid
                 qWarning() << "GetWindowTextLengthW: Invalid window handle:" << hwnd;
                 return QString();
            }
            // Otherwise, it might just be an empty title, which is fine.
            return QString(); 
        }
        
        std::wstring buffer(length + 1, L'\0');
        int copied = GetWindowTextW(hwnd, &buffer[0], length + 1);
        if (copied == 0 && GetLastError() != 0) { // Check for error only if copied is 0 and error is set
             qWarning() << "GetWindowTextW failed for HWND:" << hwnd << "Error:" << GetLastError();
             return QString(); // Return empty on failure
        }
        return QString::fromStdWString(buffer.c_str());
    }

    // --- Implementation of GetWindowClassNameString ---
    QString GetWindowClassNameString(HWND hwnd) {
        wchar_t buffer[256]; 
        if (GetClassNameW(hwnd, buffer, sizeof(buffer) / sizeof(wchar_t)) == 0) {
            DWORD lastError = GetLastError();
            if (lastError != 0) { // Only warn if there was an actual error
                 qWarning() << "GetClassNameW failed for HWND:" << hwnd << "Error:" << lastError;
            }
            return QString(); 
        }
        return QString::fromWCharArray(buffer);
    }

    // --- Helper to get process handle with necessary access rights ---
    HANDLE GetProcessHandleForInfo(HWND hwnd) {
        DWORD processId;
        if (GetWindowThreadProcessId(hwnd, &processId) == 0) {
             qWarning() << "GetWindowThreadProcessId failed for HWND:" << hwnd << "Error:" << GetLastError();
             return NULL;
        }
        if (processId == 0) {
            // Not necessarily an error, might be a system process etc.
            return NULL;
        }
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, processId);
        if (hProcess == NULL) {
            // Common error: Access Denied (5) if target process is elevated and we are not.
             // qWarning() << "OpenProcess failed for PID:" << processId << "Error:" << GetLastError();
        }
        return hProcess;
    }

    // --- Implementation of GetProcessName ---
    QString GetProcessName(HWND hwnd) {
        HANDLE hProcess = GetProcessHandleForInfo(hwnd);
        if (hProcess == NULL) {
            return QStringLiteral("Unknown");
        }

        wchar_t processNameBuffer[MAX_PATH];
        QString processNameStr = QStringLiteral("Unknown"); // Default

        if (K32GetModuleBaseNameW(hProcess, NULL, processNameBuffer, sizeof(processNameBuffer) / sizeof(wchar_t)) != 0) {
            processNameStr = QString::fromWCharArray(processNameBuffer);
        } else {
            DWORD lastError = GetLastError();
            // Don't warn excessively, OpenProcess failure is common for protected processes
            // qWarning() << "K32GetModuleBaseNameW failed for HWND:" << hwnd << "Error:" << lastError;
        }

        CloseHandle(hProcess);
        return processNameStr;
    }

    // --- Implementation of GetProcessPath ---
    QString GetProcessPath(HWND hwnd) {
        HANDLE hProcess = GetProcessHandleForInfo(hwnd);
        if (hProcess == NULL) {
            return QString();
        }

        wchar_t processPathBuffer[MAX_PATH];
        QString processPathStr;

        // Use QueryFullProcessImageName for better compatibility (Vista+)
        DWORD bufferSize = sizeof(processPathBuffer) / sizeof(wchar_t);
        if (QueryFullProcessImageNameW(hProcess, 0, processPathBuffer, &bufferSize)) {
             processPathStr = QString::fromWCharArray(processPathBuffer);
        // Fallback to K32GetModuleFileNameExW if needed
        //} else if (K32GetModuleFileNameExW(hProcess, NULL, processPathBuffer, sizeof(processPathBuffer) / sizeof(wchar_t))) {
        //     processPathStr = QString::fromWCharArray(processPathBuffer);
        } else {
            DWORD lastError = GetLastError();
            // Don't warn excessively, OpenProcess failure is common
             // qWarning() << "QueryFullProcessImageNameW failed for HWND:" << hwnd << "Error:" << lastError;
        }

        CloseHandle(hProcess);
        return processPathStr;
    }

} // namespace Win32Helper 
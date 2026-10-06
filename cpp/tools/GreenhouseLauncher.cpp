#include <windows.h>
#include <cwchar>
#include <cstdio>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    wchar_t modulePath[MAX_PATH];
    DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        MessageBoxW(nullptr, L"Could not find Greenhouse launcher path.", L"Greenhouse", MB_ICONERROR);
        return 1;
    }

    wchar_t* lastSlash = wcsrchr(modulePath, L'\\');
    if (!lastSlash) {
        MessageBoxW(nullptr, L"Could not resolve Greenhouse project folder.", L"Greenhouse", MB_ICONERROR);
        return 1;
    }
    *lastSlash = L'\0';

    wchar_t appPath[MAX_PATH];
    if (swprintf_s(appPath, L"%s\\dist\\Greenhouse\\Greenhouse.exe", modulePath) < 0) {
        MessageBoxW(nullptr, L"Greenhouse path is too long.", L"Greenhouse", MB_ICONERROR);
        return 1;
    }

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};

    BOOL created = CreateProcessW(
        appPath,
        nullptr,
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);

    if (!created) {
        MessageBoxW(nullptr, L"Could not open dist\\Greenhouse\\Greenhouse.exe. Rebuild the app package.", L"Greenhouse", MB_ICONERROR);
        return 1;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return 0;
}

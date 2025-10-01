#include <windows.h>
#include <tlhelp32.h>
#include <cwchar>
#include <string>

static bool IsLauncherCoLocatedWithRagnaPH() {
    wchar_t modulePath[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, modulePath, MAX_PATH);
    if (!len || len >= MAX_PATH) return false;

    wchar_t* lastSlash = wcsrchr(modulePath, L'\\');
    if (!lastSlash) return false;

    *lastSlash = L'\0';

    std::wstring expectedExecutablePath = modulePath;
    expectedExecutablePath += L"\\RagnaPH.exe";

    DWORD attrs = GetFileAttributesW(expectedExecutablePath.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static bool LaunchedByRagnaPHLauncher() {
    DWORD parentId = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return IsLauncherCoLocatedWithRagnaPH();
    }

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD selfId = GetCurrentProcessId();
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == selfId) {
                parentId = pe.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    if (!parentId) {
        return IsLauncherCoLocatedWithRagnaPH();
    }

    HANDLE hParent = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parentId);
    if (!hParent) {
        return IsLauncherCoLocatedWithRagnaPH();
    }

    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    bool ok = false;
    if (QueryFullProcessImageNameW(hParent, 0, path, &size)) {
        const wchar_t* fname = wcsrchr(path, L'\\');
        fname = fname ? fname + 1 : path;
        if (_wcsicmp(fname, L"RagnaPHLauncher.exe") == 0 ||
            _wcsicmp(fname, L"RagnaPH Launcher.exe") == 0) {
            ok = true;
        }
    }
    CloseHandle(hParent);
    if (ok) return true;

    return IsLauncherCoLocatedWithRagnaPH();
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    if (!LaunchedByRagnaPHLauncher()) {
        MessageBoxW(NULL, L"Please start the game using RagnaPH Launcher", L"SafePlay", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!SetEnvironmentVariableW(L"SAFEPLAY_LAUNCHED", L"1")) {
        DWORD err = GetLastError();
        wchar_t message[256];
        swprintf(message, 256, L"Could not prepare protected environment.\n(Error code: %lu)", err);
        MessageBoxW(NULL, message, L"SafePlay", MB_OK | MB_ICONERROR);
        return 1;
    }

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi{};
    wchar_t cmd[] = L"RagnaPH.exe";

    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        MessageBoxW(NULL, L"Could not start RagnaPH.exe", L"SafePlay", MB_OK | MB_ICONERROR);
        return 1;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}

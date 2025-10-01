#include <windows.h>
#include <tlhelp32.h>
#include <cwchar>

static bool LaunchedByRagnaPHLauncher() {
    DWORD parentId = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

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

    if (!parentId) return false;

    HANDLE hParent = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parentId);
    if (!hParent) return false;

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
    return ok;
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

    wchar_t eventName[64];
    swprintf(eventName, 64, L"SafePlayLaunchReady_%lu", pi.dwProcessId);
    HANDLE hLaunchReadyEvent = CreateEventW(NULL, TRUE, FALSE, eventName);
    if (hLaunchReadyEvent) {
        const DWORD kLaunchAckTimeoutMs = 15000;
        DWORD waitResult = WaitForSingleObject(hLaunchReadyEvent, kLaunchAckTimeoutMs);
        if (waitResult == WAIT_OBJECT_0) {
            OutputDebugStringW(L"SafePlayLauncher: Launch acknowledgment received.\n");
        } else if (waitResult == WAIT_TIMEOUT) {
            OutputDebugStringW(L"SafePlayLauncher: Launch acknowledgment timed out.\n");
        } else {
            wchar_t debugMsg[128];
            swprintf(debugMsg, 128, L"SafePlayLauncher: Launch acknowledgment wait failed (code %lu).\n", GetLastError());
            OutputDebugStringW(debugMsg);
        }
        CloseHandle(hLaunchReadyEvent);
    } else {
        wchar_t debugMsg[128];
        swprintf(debugMsg, 128, L"SafePlayLauncher: Failed to create launch event (code %lu).\n", GetLastError());
        OutputDebugStringW(debugMsg);
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}

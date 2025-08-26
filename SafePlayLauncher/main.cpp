#include <windows.h>
#include <tlhelp32.h>

static bool IsLaunchedByRagnaPHLauncher() {
    DWORD pid = GetCurrentProcessId();
    DWORD ppid = 0;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe{ sizeof(pe) };
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                ppid = pe.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }

    bool ok = false;
    if (ppid != 0) {
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                if (pe.th32ProcessID == ppid) {
                    ok = (_wcsicmp(pe.szExeFile, L"RagnaPH Launcher.exe") == 0);
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
    }

    CloseHandle(snap);
    return ok;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    if (!IsLaunchedByRagnaPHLauncher()) {
        MessageBoxW(NULL, L"Please start the game using the RagnaPH Launcher.", L"SafePlay", MB_OK | MB_ICONERROR);
        return 1;
    }

    SetEnvironmentVariableW(L"SAFEPLAY_LAUNCHED", L"1");

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

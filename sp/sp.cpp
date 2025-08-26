#include <windows.h>
#include <shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")

static HMODULE gModule;

static DWORD WINAPI LaunchClient(LPVOID) {
    char base[MAX_PATH];
    if (!GetModuleFileNameA(gModule, base, MAX_PATH))
        return 0;
    PathRemoveFileSpecA(base);

    char exe[MAX_PATH];
    lstrcpynA(exe, base, MAX_PATH);
    lstrcatA(exe, "\\RagnaPH.exe");

    char cmd[MAX_PATH * 2];
    wsprintfA(cmd, "\"%s\" -1sak1", exe);

    STARTUPINFOA si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        MessageBoxA(NULL, "Failed to start RagnaPH.exe", "sp.dll", MB_OK | MB_ICONERROR);
        return 0;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}

BOOL APIENTRY DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        gModule = hinst;
        DisableThreadLibraryCalls(hinst);
        CreateThread(NULL, 0, LaunchClient, NULL, 0, NULL);
    }
    return TRUE;
}

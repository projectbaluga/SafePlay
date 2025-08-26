#include <windows.h>

static DWORD WINAPI LaunchClient(LPVOID) {
    STARTUPINFOA si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    char cmd[] = "RagnaPH.exe -1sak1";
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
        DisableThreadLibraryCalls(hinst);
        CreateThread(NULL, 0, LaunchClient, NULL, 0, NULL);
    }
    return TRUE;
}

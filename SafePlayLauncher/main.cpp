#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
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

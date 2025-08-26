#include <windows.h>
#include <shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    char path[MAX_PATH];
    if (!GetModuleFileNameA(NULL, path, MAX_PATH))
        return 1;
    PathRemoveFileSpecA(path);
    lstrcatA(path, "\\sp.dll");

    HMODULE lib = LoadLibraryA(path);
    if (!lib) {
        MessageBoxA(NULL, "Failed to load sp.dll", "SafePlay", MB_OK | MB_ICONERROR);
        return 1;
    }
    return 0;
}

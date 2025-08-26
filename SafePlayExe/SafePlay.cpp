#include <windows.h>

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    HMODULE lib = LoadLibraryA("sp.dll");
    if (!lib) {
        MessageBoxA(NULL, "Failed to load sp.dll", "SafePlay", MB_OK | MB_ICONERROR);
        return 1;
    }
    return 0;
}

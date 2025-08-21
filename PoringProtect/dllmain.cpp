#include "pch.h"
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")
#include <commctrl.h>
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Msimg32.lib")
#include "clientinfo.h"

// --- Virtual clientinfo.xml handling ---
struct MemoryFile {
    const char* data;
    size_t size;
    size_t pos;
};

static MemoryFile gClientInfoFile{ kClientInfoXml, sizeof(kClientInfoXml) - 1, 0 };
static HANDLE gClientInfoHandle = (HANDLE)&gClientInfoFile;

// Original API pointers
using CreateFileW_t = HANDLE (WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileA_t = HANDLE (WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using ReadFile_t = BOOL (WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using GetFileSize_t = DWORD (WINAPI*)(HANDLE, LPDWORD);
using SetFilePointer_t = DWORD (WINAPI*)(HANDLE, LONG, PLONG, DWORD);
using CloseHandle_t = BOOL (WINAPI*)(HANDLE);
using GetFileAttributesW_t = DWORD (WINAPI*)(LPCWSTR);
using GetFileAttributesA_t = DWORD (WINAPI*)(LPCSTR);

static CreateFileW_t        RealCreateFileW;
static CreateFileA_t        RealCreateFileA;
static ReadFile_t           RealReadFile;
static GetFileSize_t        RealGetFileSize;
static SetFilePointer_t     RealSetFilePointer;
static CloseHandle_t        RealCloseHandle;
static GetFileAttributesW_t RealGetFileAttributesW;
static GetFileAttributesA_t RealGetFileAttributesA;

// Helper to patch IAT entries in the host module
static void HookIAT(const char* dll, const char* name, void* hook, void** orig) {
    HMODULE base = GetModuleHandleW(NULL);
    if (!base) return;

    BYTE* mod = (BYTE*)base;
    auto dos = (IMAGE_DOS_HEADER*)mod;
    auto nt  = (IMAGE_NT_HEADERS*)(mod + dos->e_lfanew);
    auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(mod + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

    for (; imp->Name; ++imp) {
        const char* modName = (const char*)(mod + imp->Name);
        if (_stricmp(modName, dll)) continue;
        auto thunk = (IMAGE_THUNK_DATA*)(mod + imp->FirstThunk);
        auto origThunk = (IMAGE_THUNK_DATA*)(mod + imp->OriginalFirstThunk);
        for (; origThunk->u1.Function; ++origThunk, ++thunk) {
            auto byName = (IMAGE_IMPORT_BY_NAME*)(mod + origThunk->u1.AddressOfData);
            if (strcmp((char*)byName->Name, name) == 0) {
                DWORD old;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
                *orig = (void*)thunk->u1.Function;
                thunk->u1.Function = (ULONG_PTR)hook;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
                return;
            }
        }
    }
}

// Hooked file APIs serving embedded clientinfo
static HANDLE WINAPI HookedCreateFileW(LPCWSTR name, DWORD access, DWORD share,
    LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    const wchar_t* fname = wcsrchr(name, L'\\');
    fname = fname ? fname + 1 : name;
    if (_wcsicmp(fname, L"clientinfo.xml") == 0) {
        gClientInfoFile.pos = 0;
        return gClientInfoHandle;
    }
    return RealCreateFileW(name, access, share, sa, disp, flags, tmpl);
}

static HANDLE WINAPI HookedCreateFileA(LPCSTR name, DWORD access, DWORD share,
    LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    const char* fname = strrchr(name, '\\');
    fname = fname ? fname + 1 : name;
    if (_stricmp(fname, "clientinfo.xml") == 0) {
        gClientInfoFile.pos = 0;
        return gClientInfoHandle;
    }
    return RealCreateFileA(name, access, share, sa, disp, flags, tmpl);
}

static BOOL WINAPI HookedReadFile(HANDLE h, LPVOID buf, DWORD toRead, LPDWORD read, LPOVERLAPPED ov) {
    if (h == gClientInfoHandle) {
        DWORD remain = (DWORD)(gClientInfoFile.size - gClientInfoFile.pos);
        DWORD n = min(toRead, remain);
        memcpy(buf, gClientInfoFile.data + gClientInfoFile.pos, n);
        gClientInfoFile.pos += n;
        if (read) *read = n;
        return TRUE;
    }
    return RealReadFile(h, buf, toRead, read, ov);
}

static DWORD WINAPI HookedGetFileSize(HANDLE h, LPDWORD high) {
    if (h == gClientInfoHandle) {
        if (high) *high = 0;
        return (DWORD)gClientInfoFile.size;
    }
    return RealGetFileSize(h, high);
}

static DWORD WINAPI HookedSetFilePointer(HANDLE h, LONG move, PLONG high, DWORD method) {
    if (h == gClientInfoHandle) {
        size_t pos = gClientInfoFile.pos;
        if (method == FILE_BEGIN) pos = move;
        else if (method == FILE_CURRENT) pos += move;
        else if (method == FILE_END) pos = gClientInfoFile.size + move;
        if (pos > gClientInfoFile.size) pos = gClientInfoFile.size;
        gClientInfoFile.pos = pos;
        if (high) *high = 0;
        return (DWORD)pos;
    }
    return RealSetFilePointer(h, move, high, method);
}

static BOOL WINAPI HookedCloseHandle(HANDLE h) {
    if (h == gClientInfoHandle) return TRUE;
    return RealCloseHandle(h);
}

static DWORD WINAPI HookedGetFileAttributesW(LPCWSTR name) {
    const wchar_t* fname = wcsrchr(name, L'\\');
    fname = fname ? fname + 1 : name;
    if (_wcsicmp(fname, L"clientinfo.xml") == 0)
        return FILE_ATTRIBUTE_ARCHIVE;
    return RealGetFileAttributesW(name);
}

static DWORD WINAPI HookedGetFileAttributesA(LPCSTR name) {
    const char* fname = strrchr(name, '\\');
    fname = fname ? fname + 1 : name;
    if (_stricmp(fname, "clientinfo.xml") == 0)
        return FILE_ATTRIBUTE_ARCHIVE;
    return RealGetFileAttributesA(name);
}

// --- Configuration ---
static const wchar_t* bannedExes[] = {
    L"cheatengine.exe",    L"openkore.exe",     L"ollydbg.exe",    L"x64dbg.exe",
    L"artmoney.exe",       L"winhex.exe",       L"mhs.exe",        L"proz.exe",
    L"ragnarokcheat.exe",  L"botter.exe",       L"trainer.exe",    L"debugger.exe",
    L"packeteditor.exe",   L"godmode.exe",      L"speedhack.exe",  L"4rtools.exe",
    L"ragnarokbot.exe",    L"ragnabot.exe",     L"probot.exe",     L"robot.exe",
    L"gamehacker.exe",     L"fiddle.exe",       L"rocheat.exe",     L"packetproxy.exe",
    L"xragnarok.exe",      L"irocheat.exe",     L"rohelper.exe",    L"ragnarokhelper.exe",
    L"speedbot.exe",       L"multihack.exe",    L"gmcheat.exe",    L"gameguardian.exe",
    L"zcheat.exe",         L"fakeclient.exe",   L"ragnarokcheatbot.exe"
};

static const wchar_t* bannedWindowTitles[] = {
    L"Cheat Engine", L"ArtMoney", L"Game Hacker", L"Memory Viewer"
};

static const wchar_t* bannedModules[] = {
    L"cheatengine-i386.dll", L"cheatengine-x64_86.dll",
    L"winhex.dll",           L"packeteditor.dll"
};

static const char* bannedMemPatterns[] = {
    "4RTOOLS", "4RTools", "_4RTools"
};

struct ClientConfig {
    std::string address;
    int port;
};

static ClientConfig LoadClientInfoVirtual() {
    ClientConfig cfg{};
    std::string xml = kClientInfoXml;
    size_t start, end;
    if ((start = xml.find("<address>")) != std::string::npos &&
        (end = xml.find("</address>", start)) != std::string::npos) {
        cfg.address = xml.substr(start + 9, end - (start + 9));
    }
    if ((start = xml.find("<port>")) != std::string::npos &&
        (end = xml.find("</port>", start)) != std::string::npos) {
        cfg.port = std::stoi(xml.substr(start + 6, end - (start + 6)));
    }
    return cfg;
}

static ClientConfig gClientConfig;

static bool VerifyDataIni() {
    char path[MAX_PATH];
    if (!GetModuleFileNameA(NULL, path, MAX_PATH))
        return false;
    PathRemoveFileSpecA(path);
    strcat_s(path, "\\Data.ini");

    char buf[MAX_PATH];
    if (GetPrivateProfileStringA("Data", "0", "", buf, sizeof(buf), path) == 0 || _stricmp(buf, "RagnaPH.grf") != 0)
        return false;
    if (GetPrivateProfileStringA("Data", "1", "", buf, sizeof(buf), path) == 0 || _stricmp(buf, "en.grf") != 0)
        return false;
    if (GetPrivateProfileStringA("Data", "2", "", buf, sizeof(buf), path) == 0 || _stricmp(buf, "data.grf") != 0)
        return false;
    return true;
}

// Function prototypes
DWORD WINAPI ProtectionThread(LPVOID lpParam);
DWORD WINAPI ShowErrorAndExit(LPVOID lpParam);
const wchar_t* GetDetectedCheatTool(DWORD pid);

// Show an error message and terminate the game
DWORD WINAPI ShowErrorAndExit(LPVOID lpParam)
{
    const wchar_t* tool = (const wchar_t*)lpParam;
    wchar_t msg[256];
    _snwprintf_s(msg, _countof(msg), _TRUNCATE,
        L"Cheating tool detected: %s\nThe game will now close.", tool);
    MessageBoxW(NULL, msg, L"RagnaPH Anti-Cheat", MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
    ExitProcess(0);
    return 0;
}

struct PopupData {
    std::wstring status;
    DWORD startTime;
    int finalX;
    int finalY;
    BYTE finalAlpha;
};

static const int POPUP_WIDTH  = 300;
static const int POPUP_HEIGHT = 120;
static const int POPUP_MARGIN = 24;
static const int POPUP_RADIUS = 16;

static LRESULT CALLBACK PopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PopupData* data = (PopupData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE:
        data = (PopupData*)((CREATESTRUCTW*)lParam)->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)data);
        return TRUE;
    case WM_CREATE:
        SetTimer(hwnd, 1, 15, NULL); // entry animation
        return 0;
    case WM_TIMER:
        if (wParam == 1) {
            DWORD now = GetTickCount();
            float t = (now - data->startTime) / 250.0f;
            if (t >= 1.0f) {
                t = 1.0f;
                KillTimer(hwnd, 1);
                SetTimer(hwnd, 2, 5000, NULL); // auto-close
            }
            int y = data->finalY + (int)((1.0f - t) * POPUP_HEIGHT);
            BYTE a = (BYTE)(data->finalAlpha * t);
            SetLayeredWindowAttributes(hwnd, 0, a, LWA_ALPHA);
            SetWindowPos(hwnd, NULL, data->finalX, y, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        } else if (wParam == 2) {
            KillTimer(hwnd, 2);
            data->startTime = GetTickCount();
            SetTimer(hwnd, 3, 15, NULL); // fade-out
        } else if (wParam == 3) {
            DWORD now = GetTickCount();
            float t = (now - data->startTime) / 150.0f;
            if (t >= 1.0f) {
                DestroyWindow(hwnd);
            } else {
                BYTE a = (BYTE)(data->finalAlpha * (1.0f - t));
                SetLayeredWindowAttributes(hwnd, 0, a, LWA_ALPHA);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        data->startTime = GetTickCount();
        SetTimer(hwnd, 3, 15, NULL);
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        TRIVERTEX vert[2] = {
            { rc.left, rc.top,   0x2B * 256, 0x2B * 256, 0x2B * 256, 0xFFFF },
            { rc.right, rc.bottom, 0x1F * 256, 0x1F * 256, 0x1F * 256, 0xFFFF }
        };
        GRADIENT_RECT gRect = { 0,1 };
        GradientFill(hdc, vert, 2, &gRect, 1, GRADIENT_FILL_RECT_V);

        int iconSize = 32;
        int circleX = 16;
        int circleY = (POPUP_HEIGHT - iconSize) / 2;
        HBRUSH badge = CreateSolidBrush(RGB(0x3A, 0x3A, 0x3A));
        HGDIOBJ old = SelectObject(hdc, badge);
        Ellipse(hdc, circleX, circleY, circleX + iconSize, circleY + iconSize);
        SelectObject(hdc, old);
        DeleteObject(badge);

        HICON icon = LoadIconW(NULL, MAKEINTRESOURCEW(32518)); // IDI_SHIELD
        DrawIconEx(hdc, circleX, circleY, icon, iconSize, iconSize, 0, NULL, DI_NORMAL);

        SetBkMode(hdc, TRANSPARENT);
        int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
        HFONT titleFont = CreateFontW(-MulDiv(14, dpi, 72), 0, 0, 0, FW_SEMIBOLD,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
        HFONT subFont = CreateFontW(-MulDiv(11, dpi, 72), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");

        int textX = circleX + iconSize + 16;
        int textWidth = rc.right - textX - 16;

        RECT calc{ 0,0,textWidth,0 };
        SelectObject(hdc, titleFont);
        DrawTextW(hdc, L"RagnaPH Anti-Cheat", -1, &calc, DT_CALCRECT);
        int titleH = calc.bottom;
        SelectObject(hdc, subFont);
        calc = { 0,0,textWidth,0 };
        DrawTextW(hdc, data->status.c_str(), -1, &calc, DT_CALCRECT);
        int subH = calc.bottom;

        int textHeight = titleH + 8 + subH;
        int textY = (POPUP_HEIGHT - textHeight) / 2;

        RECT rcTitle{ textX, textY, textX + textWidth, textY + titleH };
        SelectObject(hdc, titleFont);
        SetTextColor(hdc, RGB(255, 255, 255));
        DrawTextW(hdc, L"RagnaPH Anti-Cheat", -1, &rcTitle, DT_LEFT | DT_TOP);

        RECT rcSub{ textX, textY + titleH + 8, textX + textWidth, textY + titleH + 8 + subH };
        SelectObject(hdc, subFont);
        SetTextColor(hdc, RGB(0xCF, 0xCF, 0xCF));
        DrawTextW(hdc, data->status.c_str(), -1, &rcSub, DT_LEFT | DT_TOP);

        DeleteObject(titleFont);
        DeleteObject(subFont);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Show a temporary popup notification in the bottom-right corner
static void ShowStatusPopup(const wchar_t* text)
{
    WNDCLASSW wc{ 0 };
    wc.lpfnWndProc = PopupWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"RagnaPopupWnd";
    wc.style = CS_DROPSHADOW;
    RegisterClassW(&wc);

    PopupData data{};
    data.status = text;
    data.startTime = GetTickCount();
    data.finalAlpha = (BYTE)(255 * 85 / 100); // 85% opacity

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    data.finalX = screenW - POPUP_WIDTH - POPUP_MARGIN;
    data.finalY = screenH - POPUP_HEIGHT - POPUP_MARGIN;

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        wc.lpszClassName, L"", WS_POPUP,
        data.finalX, data.finalY + POPUP_HEIGHT, POPUP_WIDTH, POPUP_HEIGHT,
        NULL, NULL, wc.hInstance, &data);
    if (!hwnd) return;

    HRGN rgn = CreateRoundRectRgn(0, 0, POPUP_WIDTH, POPUP_HEIGHT,
        POPUP_RADIUS * 2, POPUP_RADIUS * 2);
    SetWindowRgn(hwnd, rgn, FALSE);

    SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

// DLL entry point
DWORD WINAPI ProtectionThread(LPVOID lpParam); // Declare your ProtectionThread

// --- MODIFIED DLLMAIN --- //
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        ShowStatusPopup(L"Loading...");

        gClientConfig = LoadClientInfoVirtual();

        if (!VerifyDataIni()) {
            MessageBoxW(NULL, L"DATA.ini is missing or invalid.", L"RagnaPH Anti-Cheat", MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
            return FALSE;
        }

        // Redirect file access to the embedded clientinfo.xml
        HookIAT("KERNEL32.dll", "CreateFileW", (void*)HookedCreateFileW, (void**)&RealCreateFileW);
        HookIAT("KERNEL32.dll", "CreateFileA", (void*)HookedCreateFileA, (void**)&RealCreateFileA);
        HookIAT("KERNEL32.dll", "ReadFile", (void*)HookedReadFile, (void**)&RealReadFile);
        HookIAT("KERNEL32.dll", "GetFileSize", (void*)HookedGetFileSize, (void**)&RealGetFileSize);
        HookIAT("KERNEL32.dll", "SetFilePointer", (void*)HookedSetFilePointer, (void**)&RealSetFilePointer);
        HookIAT("KERNEL32.dll", "CloseHandle", (void*)HookedCloseHandle, (void**)&RealCloseHandle);
        HookIAT("KERNEL32.dll", "GetFileAttributesW", (void*)HookedGetFileAttributesW, (void**)&RealGetFileAttributesW);
        HookIAT("KERNEL32.dll", "GetFileAttributesA", (void*)HookedGetFileAttributesA, (void**)&RealGetFileAttributesA);

        // Directly start anti-cheat thread, no opensetup checks
        HANDLE hThread = CreateThread(NULL, 0, ProtectionThread, NULL, 0, NULL);
        if (!hThread) return FALSE;
        CloseHandle(hThread);
    }
    return TRUE;
}

// At the end of your dllmain.cpp:
extern "C" __declspec(dllexport) void RagnaPH_Entry() {}

// Main loop: scan all processes periodically
DWORD WINAPI ProtectionThread(LPVOID)
{
    while (true) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe;
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(snap, &pe)) {
                do {
                    const wchar_t* detected = GetDetectedCheatTool(pe.th32ProcessID);
                    if (detected) {
                        CreateThread(NULL, 0, ShowErrorAndExit, (LPVOID)detected, 0, NULL);
                        Sleep(3000);
                        ExitProcess(0);
                    }
                } while (Process32NextW(snap, &pe));
            }
            CloseHandle(snap);
        }
        Sleep(5000);
    }
    return 0;
}

// Detection logic: returns name if banned tool is detected
const wchar_t* GetDetectedCheatTool(DWORD pid)
{
    if (pid == GetCurrentProcessId() || pid < 100)
        return nullptr;

    // 1) File-scan the EXE for internal signatures
    wchar_t exePath[MAX_PATH] = { 0 };
    DWORD size = MAX_PATH;
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProc && QueryFullProcessImageNameW(hProc, 0, exePath, &size)) {
        CloseHandle(hProc);
        HANDLE hFile = CreateFileW(exePath, GENERIC_READ, FILE_SHARE_READ, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            HANDLE hMap = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
            if (hMap) {
                LPVOID view = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
                if (view) {
                    size_t fileSize = GetFileSize(hFile, NULL);
                    const char* data = (const char*)view;
                    for (auto pat : bannedMemPatterns) {
                        if (fileSize && std::search(data, data + fileSize,
                            pat, pat + strlen(pat))
                            != data + fileSize) {
                            UnmapViewOfFile(view);
                            CloseHandle(hMap);
                            CloseHandle(hFile);
                            return L"4RTools";
                        }
                    }
                    UnmapViewOfFile(view);
                }
                CloseHandle(hMap);
            }
            CloseHandle(hFile);
        }
    }
    else if (hProc) {
        CloseHandle(hProc);
    }

    // 2) Executable name check
    hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProc) {
        wchar_t exeName[MAX_PATH]; DWORD exeLen = MAX_PATH;
        if (QueryFullProcessImageNameW(hProc, 0, exeName, &exeLen)) {
            const wchar_t* name = wcsrchr(exeName, L'\\') ? wcsrchr(exeName, L'\\') + 1 : exeName;
            for (auto bad : bannedExes) {
                if (_wcsicmp(name, bad) == 0 || wcsstr(name, bad)) {
                    CloseHandle(hProc);
                    return bad;
                }
            }
        }
        CloseHandle(hProc);
    }

    // 3) Window-title check
    struct WinParam { DWORD pid; const wchar_t* found; } wp{ pid, nullptr };
    EnumWindows([](HWND hwnd, LPARAM lp) {
        WinParam* p = (WinParam*)lp;
        DWORD winPid;
        GetWindowThreadProcessId(hwnd, &winPid);
        if (winPid == p->pid) {
            wchar_t title[256] = { 0 };
            GetWindowTextW(hwnd, title, _countof(title));
            for (auto bad : bannedWindowTitles) {
                if (wcsstr(title, bad)) {
                    p->found = bad;
                    return FALSE;
                }
            }
        }
        return TRUE;
        }, (LPARAM)&wp);
    if (wp.found) return wp.found;

    // 4) Module enumeration check
    HANDLE modSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (modSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me; me.dwSize = sizeof(me);
        if (Module32FirstW(modSnap, &me)) {
            do {
                for (auto bad : bannedModules) {
                    if (_wcsicmp(me.szModule, bad) == 0 || wcsstr(me.szModule, bad)) {
                        CloseHandle(modSnap);
                        return bad;
                    }
                }
            } while (Module32NextW(modSnap, &me));
        }
        CloseHandle(modSnap);
    }

    return nullptr;
}

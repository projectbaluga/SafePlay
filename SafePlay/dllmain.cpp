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
#include <gdiplus.h>
#pragma comment(lib, "Gdiplus.lib")
#include <objidl.h>
#pragma comment(lib, "Ole32.lib")
#include <shellapi.h>
#pragma comment(lib, "Shell32.lib")
#include "clientinfo.h"
#include "resource.h"

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
static ULONG_PTR gGdiplusToken;
HMODULE g_hModule = nullptr;
static HANDLE g_hProgressDone = NULL;

static Gdiplus::Bitmap* LoadSafePlayLogo() {
    wchar_t base[MAX_PATH];
    GetModuleFileNameW(g_hModule, base, MAX_PATH);
    PathRemoveFileSpecW(base);

    wchar_t cand[3][MAX_PATH];
    // 1) Same folder as the DLL
    swprintf(cand[0], MAX_PATH, L"%s\\SafePlay.png", base);
    // 2) assets\SafePlay.png next to the DLL
    swprintf(cand[1], MAX_PATH, L"%s\\assets\\SafePlay.png", base);
    // 3) Current working dir (game root)
    swprintf(cand[2], MAX_PATH, L".\\SafePlay.png");

    for (int i = 0; i < 3; ++i) {
        if (PathFileExistsW(cand[i])) {
            auto* bmp = Gdiplus::Bitmap::FromFile(cand[i]);
            if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) return bmp;
            delete bmp;
        }
    }
    // Fallback: load from embedded PNG resource
    HRSRC hRes = FindResourceW(g_hModule, MAKEINTRESOURCEW(IDB_SAFEPLAY_LOGO), L"PNG");
    if (hRes) {
        HGLOBAL hData = LoadResource(g_hModule, hRes);
        if (hData) {
            void* pData = LockResource(hData);
            DWORD size = SizeofResource(g_hModule, hRes);
            if (pData && size) {
                HGLOBAL hBuffer = GlobalAlloc(GMEM_MOVEABLE, size);
                if (hBuffer) {
                    void* pBuffer = GlobalLock(hBuffer);
                    memcpy(pBuffer, pData, size);
                    GlobalUnlock(hBuffer);
                    IStream* pStream = nullptr;
                    if (SUCCEEDED(CreateStreamOnHGlobal(hBuffer, TRUE, &pStream))) {
                        auto* bmp = Gdiplus::Bitmap::FromStream(pStream);
                        pStream->Release();
                        if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) return bmp;
                        delete bmp;
                    }
                }
            }
        }
    }

    return nullptr;
}

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
    int port = 0;
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
    if (GetPrivateProfileStringA("Data", "0", "", buf, sizeof(buf), path) == 0 || _stricmp(buf, "data.grf") != 0)
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
    MessageBoxW(NULL, msg, L"SafePlay", MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
    ExitProcess(0);
    return 0;
}

struct PopupData {
    std::wstring status;
    DWORD startTime;
    int finalX;
    int finalY;
    BYTE finalAlpha;
    int progress;
    RECT progressRect;
    Gdiplus::Bitmap* logo;
};

static const int POPUP_WIDTH  = 300;
static const int POPUP_HEIGHT = 120;
static const int POPUP_MARGIN = 24;
static const int POPUP_RADIUS = 16;
static const int PROGRESS_STEP = 2;

static LRESULT CALLBACK PopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PopupData* data = (PopupData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE:
        data = (PopupData*)((CREATESTRUCTW*)lParam)->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)data);
        return TRUE;
    case WM_CREATE:
    {
        SetTimer(hwnd, 1, 15, NULL); // entry animation
        SetTimer(hwnd, 2, 50, NULL); // progress update (~20 FPS)

        // Pre-calc progress bar rect so we can invalidate only that area later
        HDC hdc = GetDC(hwnd);
        RECT rc;
        GetClientRect(hwnd, &rc);

        // DPI-aware metrics
        int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
        auto D = [&](int px) { return MulDiv(px, dpi, 96); };

        const int padX      = D(20);          // horizontal padding
        const int barH      = D(14);          // bar height
        const float anchorY = 0.90f;          // place bar around 68% of popup height

        // Compute rect
        int barWidth = (rc.right - rc.left) - padX * 2;
        int barX     = rc.left + padX;
        int barCY    = rc.top + int((rc.bottom - rc.top) * anchorY); // center line y
        int barY     = barCY - barH / 2;

        // Store for invalidation + paint
        data->progressRect = { barX, barY, barX + barWidth, barY + barH };
        ReleaseDC(hwnd, hdc);

        // Load SafePlay logo from various locations
        data->logo = LoadSafePlayLogo();
        return 0;
    }
    case WM_TIMER:
        if (wParam == 1) {
            DWORD now = GetTickCount();
            float t = (now - data->startTime) / 250.0f;
            if (t >= 1.0f) {
                t = 1.0f;
                KillTimer(hwnd, 1);
            }
            int y = data->finalY + (int)((1.0f - t) * POPUP_HEIGHT);
            BYTE a = (BYTE)(data->finalAlpha * t);
            SetLayeredWindowAttributes(hwnd, 0, a, LWA_ALPHA);
            SetWindowPos(hwnd, NULL, data->finalX, y, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        } else if (wParam == 2) {
            data->progress = min(100, data->progress + PROGRESS_STEP);
            InvalidateRect(hwnd, &data->progressRect, FALSE);
            if (data->progress >= 100) {
                KillTimer(hwnd, 2);
                data->startTime = GetTickCount();
                SetTimer(hwnd, 3, 15, NULL); // fade-out
            }
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
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;
        HDC memDC = CreateCompatibleDC(hdc);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = width;
        bi.bmiHeader.biHeight = -height;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP memBmp = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        Gdiplus::Graphics gfx(memDC);
        gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        gfx.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

        Gdiplus::SolidBrush white(Gdiplus::Color(0xFF, 0xFF, 0xFF, 0xFF));
        gfx.FillRectangle(&white, Gdiplus::RectF(0, 0, (Gdiplus::REAL)width, (Gdiplus::REAL)height));

        if (data->logo) {
            gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            gfx.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            gfx.DrawImage(data->logo, Gdiplus::RectF(0, 0, (Gdiplus::REAL)width, (Gdiplus::REAL)height));
        }

        const int barX = data->progressRect.left;
        const int barY = data->progressRect.top;
        const int barWidth  = data->progressRect.right - data->progressRect.left;
        const int barHeight = data->progressRect.bottom - data->progressRect.top;

        Gdiplus::GraphicsPath trackPath;
        Gdiplus::REAL x = (Gdiplus::REAL)barX;
        Gdiplus::REAL y = (Gdiplus::REAL)barY;
        Gdiplus::REAL w = (Gdiplus::REAL)barWidth;
        Gdiplus::REAL h = (Gdiplus::REAL)barHeight;
        Gdiplus::REAL r = (Gdiplus::REAL)barHeight / 2;
        trackPath.AddArc(x, y, r*2, r*2, 180, 90);
        trackPath.AddArc(x + w - r*2, y, r*2, r*2, 270, 90);
        trackPath.AddArc(x + w - r*2, y + h - r*2, r*2, r*2, 0, 90);
        trackPath.AddArc(x, y + h - r*2, r*2, r*2, 90, 90);
        trackPath.CloseFigure();

        Gdiplus::SolidBrush trackBrush(Gdiplus::Color(0xFF, 0x3A, 0x3A, 0x3A));
        gfx.FillPath(&trackBrush, &trackPath);
        Gdiplus::Pen outlinePen(Gdiplus::Color(0x80, 0x00, 0x00, 0x00), 1.0f);

        int fillWidth = barWidth * data->progress / 100;
        if (fillWidth > 0) {
            Gdiplus::GraphicsPath fillPath;
            Gdiplus::REAL fw = (Gdiplus::REAL)fillWidth;
            Gdiplus::REAL fr = min(r, fw / 2);
            fillPath.AddArc(x, y, fr*2, fr*2, 180, 90);
            fillPath.AddArc(x + fw - fr*2, y, fr*2, fr*2, 270, 90);
            fillPath.AddArc(x + fw - fr*2, y + h - fr*2, fr*2, fr*2, 0, 90);
            fillPath.AddArc(x, y + h - fr*2, fr*2, fr*2, 90, 90);
            fillPath.CloseFigure();
            Gdiplus::LinearGradientBrush pbrush(
                Gdiplus::Point(barX, barY),
                Gdiplus::Point(barX + fillWidth, barY),
                Gdiplus::Color(0xFF, 0x4A, 0x90, 0xE2),
                Gdiplus::Color(0xFF, 0x35, 0x7A, 0xBD));
            gfx.FillPath(&pbrush, &fillPath);
            gfx.DrawPath(&outlinePen, &fillPath);
        }

        gfx.DrawPath(&outlinePen, &trackPath);

        // Use GDI+ for crisp text rendering
        std::wstring pct = std::to_wstring(data->progress) + L"%";
        gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
        Gdiplus::FontFamily fontFamily(L"Segoe UI");
        Gdiplus::Font font(&fontFamily, 10.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF textRect((Gdiplus::REAL)barX, (Gdiplus::REAL)barY,
            (Gdiplus::REAL)barWidth, (Gdiplus::REAL)barHeight);
        // subtle text shadow for contrast
        Gdiplus::RectF textRectShadow((Gdiplus::REAL)barX, (Gdiplus::REAL)barY + 1,
            (Gdiplus::REAL)barWidth, (Gdiplus::REAL)barHeight);
        Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(255, 0, 0, 0));
        gfx.DrawString(pct.c_str(), -1, &font, textRectShadow, &format, &shadowBrush);
        // main text
        Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
        gfx.DrawString(pct.c_str(), -1, &font, textRect, &format, &textBrush);

        // Blit only the updated region to screen
        BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        AlphaBlend(hdc, ps.rcPaint.left, ps.rcPaint.top,
            ps.rcPaint.right - ps.rcPaint.left,
            ps.rcPaint.bottom - ps.rcPaint.top,
            memDC, ps.rcPaint.left, ps.rcPaint.top,
            ps.rcPaint.right - ps.rcPaint.left,
            ps.rcPaint.bottom - ps.rcPaint.top, bf);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        if (data && data->logo) {
            delete data->logo;
            data->logo = nullptr;
        }
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
    data.progress = 0;
    data.logo = nullptr;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    data.finalX = screenW - POPUP_WIDTH - POPUP_MARGIN;
    data.finalY = screenH - POPUP_HEIGHT - POPUP_MARGIN;

    // Create a click-through, non-activating popup so it doesn't steal focus
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED |
        WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
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

// Run loading popup on a separate thread so game can initialize concurrently
static DWORD WINAPI LoadingPopupThread(LPVOID) {
    ShowStatusPopup(L"Loading...");
    if (g_hProgressDone) {
        SetEvent(g_hProgressDone);
    }
    return 0;
}

static DWORD WINAPI LaunchGameThread(LPVOID) {
    WaitForSingleObject(g_hProgressDone, INFINITE);
    ShellExecuteW(NULL, L"open", L"RagnaPH.exe", NULL, NULL, SW_SHOWNORMAL);
    return 0;
}

// --- MODIFIED DLLMAIN --- //
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);

        Gdiplus::GdiplusStartupInput gdiplusStartupInput;
        Gdiplus::GdiplusStartup(&gGdiplusToken, &gdiplusStartupInput, NULL);

        g_hProgressDone = CreateEvent(NULL, TRUE, FALSE, NULL);
        // Show loading popup without blocking game startup
        HANDLE hPopup = CreateThread(NULL, 0, LoadingPopupThread, NULL, 0, NULL);
        HANDLE hLaunch = CreateThread(NULL, 0, LaunchGameThread, NULL, 0, NULL);
        if (hPopup) CloseHandle(hPopup);
        if (hLaunch) CloseHandle(hLaunch);

        gClientConfig = LoadClientInfoVirtual();

        if (!VerifyDataIni()) {
            MessageBoxW(NULL, L"DATA.ini is missing or invalid.", L"SafePlay", MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
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
    } else if (reason == DLL_PROCESS_DETACH) {
        Gdiplus::GdiplusShutdown(gGdiplusToken);
        if (g_hProgressDone) {
            CloseHandle(g_hProgressDone);
            g_hProgressDone = NULL;
        }
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

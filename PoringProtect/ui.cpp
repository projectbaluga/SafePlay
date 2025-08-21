#include "pch.h"
#include <windows.h>
#include <gdiplus.h>
#include <string>
#pragma comment(lib, "Gdiplus.lib")
using namespace Gdiplus;

// Helper to build rounded rectangles with GDI+
static void AddRoundRect(GraphicsPath& path, const Rect& r, int radius) {
    int d = radius * 2;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();
}

// Placeholder to be hooked into the game later
void TriggerAuto() {
    // TODO: implement integration with Ragnarok client
}

// ---------------- Loader Window ----------------
struct LoaderData {
    int   progress;
    DWORD startTime;
    int   finalX;
    int   finalY;
    BYTE  finalAlpha;
};

static LRESULT CALLBACK LoaderProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LoaderData* data = reinterpret_cast<LoaderData*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        data = reinterpret_cast<LoaderData*>(((CREATESTRUCTW*)lParam)->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)data);
        return TRUE;
    case WM_CREATE:
        SetTimer(hwnd, 1, 15, NULL);  // animation
        SetTimer(hwnd, 2, 50, NULL);  // progress
        return 0;
    case WM_TIMER:
        if (wParam == 1) {
            DWORD now = GetTickCount();
            float t = (now - data->startTime) / 250.0f;
            if (t >= 1.0f) { t = 1.0f; KillTimer(hwnd, 1); }
            int y = data->finalY + (int)((1.0f - t) * 120);
            BYTE a = (BYTE)(data->finalAlpha * t);
            SetLayeredWindowAttributes(hwnd, 0, a, LWA_ALPHA);
            SetWindowPos(hwnd, NULL, data->finalX, y, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        } else if (wParam == 2) {
            data->progress = min(100, data->progress + 2);
            InvalidateRect(hwnd, NULL, FALSE);
            if (data->progress >= 100) {
                KillTimer(hwnd, 2);
                data->startTime = GetTickCount();
                SetTimer(hwnd, 3, 15, NULL);  // fade-out
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
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        Graphics g(hdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        RECT rc; GetClientRect(hwnd, &rc);
        SolidBrush bg(Color(0xFF, 0x2B, 0x2B, 0x2B));
        g.FillRectangle(&bg, rc.left, rc.top, rc.right, rc.bottom);

        FontFamily ff(L"Segoe UI");
        Font title(&ff, 14, FontStyleBold, UnitPoint);
        Font sub(&ff, 11, FontStyleRegular, UnitPoint);
        SolidBrush white(Color(0xFF, 0xFF, 0xFF, 0xFF));
        SolidBrush gray(Color(0xFF, 0xCF, 0xCF, 0xCF));
        g.DrawString(L"RagnaPH Anti-Cheat", -1, &title, PointF(64.f, 28.f), &white);
        g.DrawString(L"Loading...", -1, &sub, PointF(64.f, 52.f), &gray);

        int barX = 64, barY = 80, barW = rc.right - barX - 32, barH = 12;
        GraphicsPath track; AddRoundRect(track, Rect(barX, barY, barW, barH), 6);
        SolidBrush trackBrush(Color(0xFF, 0x3A, 0x3A, 0x3A));
        g.FillPath(&trackBrush, &track);
        int fillW = barW * data->progress / 100;
        if (fillW > 0) {
            GraphicsPath fill; AddRoundRect(fill, Rect(barX, barY, fillW, barH), 6);
            LinearGradientBrush br(Rect(barX, barY, fillW, barH),
                Color(0xFF, 0x4A, 0x90, 0xE2),
                Color(0xFF, 0x35, 0x7A, 0xBD),
                LinearGradientModeHorizontal);
            g.FillPath(&br, &fill);
        }
        std::wstring pct = std::to_wstring(data->progress) + L"%";
        Font pctFont(&ff, 10, FontStyleRegular, UnitPoint);
        RectF pctRect((REAL)barX, (REAL)barY, (REAL)barW, (REAL)barH);
        StringFormat sf; sf.SetAlignment(StringAlignmentCenter); sf.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(pct.c_str(), -1, &pctFont, pctRect, &sf, &gray);
        EndPaint(hwnd, &ps);
        return 0; }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void CreateLoaderWindow() {
    WNDCLASSW wc{}; wc.lpfnWndProc = LoaderProc; wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"RagnaLoaderWnd"; wc.style = CS_DROPSHADOW;
    RegisterClassW(&wc);

    LoaderData data{}; data.progress = 0; data.startTime = GetTickCount();
    data.finalAlpha = (BYTE)(255 * 85 / 100);
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    const int width = 300, height = 120, margin = 24;
    data.finalX = screenW - width - margin;
    data.finalY = screenH - height - margin;

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"", WS_POPUP,
        data.finalX, data.finalY + height, width, height,
        NULL, NULL, wc.hInstance, &data);
    if (!hwnd) return;

    HRGN rgn = CreateRoundRectRgn(0, 0, width, height, 32, 32);
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

// ---------------- Control Window ----------------
struct ControlData {
    RECT btnRect;
    bool hover;
};

static LRESULT CALLBACK ControlProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ControlData* data = reinterpret_cast<ControlData*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        data = new ControlData{};
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)data);
        return TRUE;
    case WM_CREATE: {
        RECT rc; GetClientRect(hwnd, &rc);
        int w = 120, h = 32;
        data->btnRect.left = (rc.right - w) / 2;
        data->btnRect.top = (rc.bottom - h) / 2;
        data->btnRect.right = data->btnRect.left + w;
        data->btnRect.bottom = data->btnRect.top + h;
        return 0; }
    case WM_MOUSEMOVE: {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        bool inside = PtInRect(&data->btnRect, pt);
        if (inside != data->hover) {
            data->hover = inside;
            InvalidateRect(hwnd, &data->btnRect, FALSE);
        }
        return 0; }
    case WM_LBUTTONUP: {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (PtInRect(&data->btnRect, pt)) TriggerAuto();
        return 0; }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        Graphics g(hdc); g.SetSmoothingMode(SmoothingModeAntiAlias);
        RECT rc; GetClientRect(hwnd, &rc);
        SolidBrush bg(Color(0xFF, 0x2B, 0x2B, 0x2B));
        g.FillRectangle(&bg, rc.left, rc.top, rc.right, rc.bottom);

        FontFamily ff(L"Segoe UI");
        Font title(&ff, 14, FontStyleBold, UnitPoint);
        SolidBrush white(Color(0xFF, 0xFF, 0xFF, 0xFF));
        g.DrawString(L"RagnaPH Anti-Cheat", -1, &title, PointF(16.f, 12.f), &white);

        Color btnCol = data->hover ? Color(0xFF, 0x5A, 0xA0, 0xF2) : Color(0xFF, 0x4A, 0x90, 0xE2);
        GraphicsPath path; AddRoundRect(path, Rect(data->btnRect.left, data->btnRect.top,
            data->btnRect.right - data->btnRect.left,
            data->btnRect.bottom - data->btnRect.top), 6);
        SolidBrush btnBrush(btnCol);
        g.FillPath(&btnBrush, &path);

        Font btnFont(&ff, 12, FontStyleRegular, UnitPoint);
        RectF r((REAL)data->btnRect.left, (REAL)data->btnRect.top,
            (REAL)(data->btnRect.right - data->btnRect.left),
            (REAL)(data->btnRect.bottom - data->btnRect.top));
        StringFormat sf; sf.SetAlignment(StringAlignmentCenter); sf.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(L"Enable Auto", -1, &btnFont, r, &sf, &white);
        EndPaint(hwnd, &ps);
        return 0; }
    case WM_DESTROY:
        delete data;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void CreateControlWindow() {
    WNDCLASSW wc{}; wc.lpfnWndProc = ControlProc; wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"RagnaControlWnd"; wc.style = CS_DROPSHADOW;
    RegisterClassW(&wc);

    const int width = 240, height = 120;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = screenW - width - 24;
    int y = screenH - height - 24;

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        wc.lpszClassName, L"", WS_POPUP,
        x, y, width, height, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) return;

    HRGN rgn = CreateRoundRectRgn(0, 0, width, height, 32, 32);
    SetWindowRgn(hwnd, rgn, FALSE);
    SetLayeredWindowAttributes(hwnd, 0, (BYTE)(255 * 90 / 100), LWA_ALPHA);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

// Thread that runs loader then control window
static DWORD WINAPI UIThread(LPVOID) {
    GdiplusStartupInput gsi; ULONG_PTR token;
    GdiplusStartup(&token, &gsi, NULL);
    CreateLoaderWindow();
    CreateControlWindow();
    GdiplusShutdown(token);
    return 0;
}

// Entry for external callers (used by DllMain)
void StartUIThread() {
    HANDLE h = CreateThread(NULL, 0, UIThread, NULL, 0, NULL);
    if (h) CloseHandle(h);
}

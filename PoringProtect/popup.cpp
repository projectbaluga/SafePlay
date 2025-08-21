#include "pch.h"
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

// Configuration
static const int WINDOW_WIDTH  = 300;
static const int WINDOW_HEIGHT = 150;
static const int MARGIN        = 24;

enum TimerIds { IDT_ANIMATE = 1, IDT_PROGRESS, IDT_CLOSE };

struct UIState {
    int progress = 0;
    bool hover   = false;
    int  alpha   = 0;   // 0–255 for fade-in
    int  x = 0, y = 0;
    int  finalX = 0, finalY = 0;
    bool clicked = false;
} g_state;

static LRESULT CALLBACK PopupProc(HWND, UINT, WPARAM, LPARAM);
static void Render(HWND);
static void TriggerAuto();

// Helper: create rounded rectangle path
static void RoundRectPath(GraphicsPath& path, RectF rc, float radius) {
    path.AddArc(rc.X, rc.Y, radius*2, radius*2, 180, 90);
    path.AddArc(rc.GetRight() - radius*2, rc.Y, radius*2, radius*2, 270, 90);
    path.AddArc(rc.GetRight() - radius*2, rc.GetBottom() - radius*2, radius*2, radius*2, 0, 90);
    path.AddArc(rc.X, rc.GetBottom() - radius*2, radius*2, radius*2, 90, 90);
    path.CloseFigure();
}

// Rendering
static void Render(HWND hWnd) {
    HDC hdc = GetDC(hWnd);
    HDC memDC = CreateCompatibleDC(hdc);

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = WINDOW_WIDTH;
    bi.bmiHeader.biHeight = -WINDOW_HEIGHT; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &pvBits, NULL, 0);
    SelectObject(memDC, hBitmap);

    Graphics g(memDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    // Drop shadow
    RectF shadowRc(4.f, 4.f, (REAL)WINDOW_WIDTH, (REAL)WINDOW_HEIGHT);
    GraphicsPath shadowPath;
    RoundRectPath(shadowPath, shadowRc, 12.f);
    SolidBrush shadowBrush(Color(80, 0, 0, 0));
    g.FillPath(&shadowBrush, &shadowPath);

    // Background
    RectF bgRc(0.f, 0.f, (REAL)WINDOW_WIDTH, (REAL)WINDOW_HEIGHT);
    GraphicsPath bgPath;
    RoundRectPath(bgPath, bgRc, 12.f);
    SolidBrush bgBrush(Color(230, 32, 32, 32));
    g.FillPath(&bgBrush, &bgPath);

    // Title
    FontFamily ffTitle(L"Segoe UI Semibold");
    Font fTitle(&ffTitle, 14, FontStyleRegular, UnitPoint);
    SolidBrush whiteBrush(Color::White);
    g.DrawString(L"RagnaPH Anti-Cheat", -1, &fTitle, PointF(20.f, 20.f), &whiteBrush);

    // Subtitle
    FontFamily ffSub(L"Segoe UI");
    Font fSub(&ffSub, 11, FontStyleRegular, UnitPoint);
    SolidBrush grayBrush(Color(200, 200, 200, 200));
    g.DrawString(L"Loading...", -1, &fSub, PointF(20.f, 50.f), &grayBrush);

    // Progress bar background
    RectF barBg(20.f, 78.f, 260.f, 12.f);
    GraphicsPath barBgPath;
    RoundRectPath(barBgPath, barBg, 6.f);
    SolidBrush barBgBrush(Color(80, 255, 255, 255));
    g.FillPath(&barBgBrush, &barBgPath);

    // Progress fill
    RectF barFill = barBg;
    barFill.Width = barFill.Width * (g_state.progress / 100.0f);
    if (barFill.Width > 0) {
        GraphicsPath barFillPath;
        RoundRectPath(barFillPath, barFill, 6.f);
        SolidBrush barFillBrush(Color(255, 74, 144, 226));
        g.FillPath(&barFillBrush, &barFillPath);
    }

    // Button
    RectF btnRc(90.f, 100.f, 120.f, 36.f);
    GraphicsPath btnPath;
    RoundRectPath(btnPath, btnRc, 8.f);
    Color btnColor = g_state.hover ? Color(255, 90, 160, 242) : Color(255, 74, 144, 226);
    SolidBrush btnBrush(btnColor);
    g.FillPath(&btnBrush, &btnPath);

    SolidBrush btnTextBrush(Color::White);
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"Enable Auto", -1, &ffSub, btnRc, &fmt, &btnTextBrush);

    POINT ptSrc = {0,0};
    SIZE sizeWnd = {WINDOW_WIDTH, WINDOW_HEIGHT};
    POINT ptDst = {g_state.x, g_state.y};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, (BYTE)g_state.alpha, AC_SRC_ALPHA};
    UpdateLayeredWindow(hWnd, hdc, &ptDst, &sizeWnd, memDC, &ptSrc, 0, &blend, ULW_ALPHA);

    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(hWnd, hdc);
}

// Window procedure
static LRESULT CALLBACK PopupProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        SetTimer(hWnd, IDT_ANIMATE, 15, NULL);
        SetTimer(hWnd, IDT_PROGRESS, 50, NULL);
        SetTimer(hWnd, IDT_CLOSE, 5000, NULL);
        Render(hWnd);
        return 0;
    case WM_TIMER:
        if (wParam == IDT_ANIMATE) {
            g_state.alpha = min(255, g_state.alpha + 25);
            g_state.x = max(g_state.finalX, g_state.x - 20);
            if (g_state.alpha == 255 && g_state.x == g_state.finalX)
                KillTimer(hWnd, IDT_ANIMATE);
            Render(hWnd);
        } else if (wParam == IDT_PROGRESS) {
            g_state.progress = min(100, g_state.progress + 1);
            Render(hWnd);
        } else if (wParam == IDT_CLOSE) {
            DestroyWindow(hWnd);
        }
        return 0;
    case WM_MOUSEMOVE: {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        Rect btn(90, 100, 120, 36);
        bool hover = btn.Contains(pt);
        if (hover != g_state.hover) {
            g_state.hover = hover;
            Render(hWnd);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        Rect btn(90, 100, 120, 36);
        if (btn.Contains(pt)) {
            g_state.clicked = true;
            TriggerAuto();
            DestroyWindow(hWnd);
        }
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hWnd, IDT_ANIMATE);
        KillTimer(hWnd, IDT_PROGRESS);
        KillTimer(hWnd, IDT_CLOSE);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// Placeholder for in-game automation trigger
static void TriggerAuto() {
    // TODO: Send "@auto" in-game
}

// Thread entry that creates the popup window
DWORD WINAPI PopupThread(LPVOID) {
    GdiplusStartupInput gdiInput;
    ULONG_PTR gdiToken;
    GdiplusStartup(&gdiToken, &gdiInput, NULL);

    RECT wa;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
    g_state.finalX = wa.right - WINDOW_WIDTH - MARGIN;
    g_state.finalY = wa.bottom - WINDOW_HEIGHT - MARGIN;
    g_state.x = wa.right;
    g_state.y = g_state.finalY;

    WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
    wc.lpfnWndProc = PopupProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"RagnaPopupWnd";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassEx(&wc);

    HWND hWnd = CreateWindowEx(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                               wc.lpszClassName, L"", WS_POPUP,
                               g_state.x, g_state.y,
                               WINDOW_WIDTH, WINDOW_HEIGHT,
                               NULL, NULL, wc.hInstance, NULL);

    ShowWindow(hWnd, SW_SHOW);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(gdiToken);
    return 0;
}


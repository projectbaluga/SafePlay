#include "pch.h"
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <algorithm>
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

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

// DLL entry point
DWORD WINAPI ProtectionThread(LPVOID lpParam); // Declare your ProtectionThread

// --- MODIFIED DLLMAIN --- //
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

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

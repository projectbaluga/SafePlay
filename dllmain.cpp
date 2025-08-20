#include <windows.h>
// Core hooking includes
#include <unordered_map>
#include <vector>
#include <string>
#include <algorithm>
#include <tlhelp32.h>
#include <cstring>
#include <cstdio>
#include "MinHook.h"
#include "resource.h"

std::vector<uint8_t> g_clientinfo;
struct MemFile { std::vector<uint8_t> buf; size_t pos = 0; DWORD attrs = FILE_ATTRIBUTE_ARCHIVE; };
static std::unordered_map<HANDLE, MemFile> g_open;
static CRITICAL_SECTION g_lock;

// --- Anti-cheat configuration ---
static const wchar_t* g_bannedExes[] = {
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

static const wchar_t* g_bannedWindowTitles[] = {
    L"Cheat Engine", L"ArtMoney", L"Game Hacker", L"Memory Viewer"
};

static const wchar_t* g_bannedModules[] = {
    L"cheatengine-i386.dll", L"cheatengine-x64_86.dll",
    L"winhex.dll",           L"packeteditor.dll"
};

static const char* g_bannedMemPatterns[] = {
    "4RTOOLS", "4RTools", "_4RTools"
};

static bool load_rcdata(HMODULE mod, int id) {
    HRSRC rc = FindResourceW(mod, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!rc) return false;
    HGLOBAL h = LoadResource(mod, rc);
    if (!h) return false;
    DWORD sz = SizeofResource(mod, rc);
    void* ptr = LockResource(h);
    g_clientinfo.assign(static_cast<uint8_t*>(ptr), static_cast<uint8_t*>(ptr) + sz);
    return true;
}

static bool match_clientinfo(const wchar_t* path) {
    if (!path) return false;
    const wchar_t* base = wcsrchr(path, L'\\');
    base = base ? base + 1 : path;
    return _wcsicmp(base, L"clientinfo.xml") == 0;
}

static bool match_clientinfo(const char* path) {
    if (!path) return false;
    const char* base = strrchr(path, '\\');
    base = base ? base + 1 : path;
    return _stricmp(base, "clientinfo.xml") == 0;
}

using PFN_CreateFileW = HANDLE (WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using PFN_CreateFileA = HANDLE (WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using PFN_ReadFile = BOOL (WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using PFN_SetFilePointer = DWORD (WINAPI*)(HANDLE, LONG, PLONG, DWORD);
using PFN_GetFileSize = DWORD (WINAPI*)(HANDLE, LPDWORD);
using PFN_CloseHandle = BOOL (WINAPI*)(HANDLE);
using PFN_GetFileAttributesW = DWORD (WINAPI*)(LPCWSTR);
using PFN_GetFileAttributesA = DWORD (WINAPI*)(LPCSTR);

static PFN_CreateFileW fpCreateFileW = nullptr;
static PFN_CreateFileA fpCreateFileA = nullptr;
static PFN_ReadFile fpReadFile = nullptr;
static PFN_SetFilePointer fpSetFilePointer = nullptr;
static PFN_GetFileSize fpGetFileSize = nullptr;
static PFN_CloseHandle fpCloseHandle = nullptr;
static PFN_GetFileAttributesW fpGetFileAttributesW = nullptr;
static PFN_GetFileAttributesA fpGetFileAttributesA = nullptr;

template<typename T>
static void HookApi(const char* name, T detour, T* original) {
    const wchar_t* mods[] = { L"kernel32.dll", L"KernelBase.dll" };
    for (auto mname : mods) {
        HMODULE m = GetModuleHandleW(mname);
        if (!m) continue;
        auto p = reinterpret_cast<T>(GetProcAddress(m, name));
        if (p) MH_CreateHook(reinterpret_cast<LPVOID>(p), detour, reinterpret_cast<LPVOID*>(original));
    }
}

static HANDLE WINAPI DetourCreateFileW(LPCWSTR name, DWORD access, DWORD share,
    LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    if (match_clientinfo(name)) {
        OutputDebugStringW(L"[PP] CreateFileW clientinfo.xml\n");
        HANDLE h = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (h) {
            EnterCriticalSection(&g_lock);
            g_open[h] = MemFile{ g_clientinfo, 0, FILE_ATTRIBUTE_ARCHIVE };
            LeaveCriticalSection(&g_lock);
        }
        return h;
    }
    return fpCreateFileW(name, access, share, sa, disp, flags, tmpl);
}

static HANDLE WINAPI DetourCreateFileA(LPCSTR name, DWORD access, DWORD share,
    LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    if (match_clientinfo(name)) {
        OutputDebugStringA("[PP] CreateFileA clientinfo.xml\n");
        HANDLE h = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (h) {
            EnterCriticalSection(&g_lock);
            g_open[h] = MemFile{ g_clientinfo, 0, FILE_ATTRIBUTE_ARCHIVE };
            LeaveCriticalSection(&g_lock);
        }
        return h;
    }
    return fpCreateFileA(name, access, share, sa, disp, flags, tmpl);
}

static BOOL WINAPI DetourReadFile(HANDLE h, LPVOID buf, DWORD toRead, LPDWORD read, LPOVERLAPPED ov) {
    EnterCriticalSection(&g_lock);
    auto it = g_open.find(h);
    if (it != g_open.end()) {
        MemFile& mf = it->second;
        DWORD remain = static_cast<DWORD>(mf.buf.size() - mf.pos);
        DWORD copy = (toRead < remain) ? toRead : remain;
        if (copy && buf) memcpy(buf, mf.buf.data() + mf.pos, copy);
        mf.pos += copy;
        if (read) *read = copy;
        LeaveCriticalSection(&g_lock);
        return TRUE;
    }
    LeaveCriticalSection(&g_lock);
    return fpReadFile(h, buf, toRead, read, ov);
}

static DWORD WINAPI DetourSetFilePointer(HANDLE h, LONG dist, PLONG high, DWORD method) {
    EnterCriticalSection(&g_lock);
    auto it = g_open.find(h);
    if (it != g_open.end()) {
        MemFile& mf = it->second;
        long long move = static_cast<long long>(dist);
        if (high) move |= (static_cast<long long>(*high) << 32);
        long long base = 0;
        if (method == FILE_BEGIN) base = 0;
        else if (method == FILE_CURRENT) base = static_cast<long long>(mf.pos);
        else if (method == FILE_END) base = static_cast<long long>(mf.buf.size());
        long long newpos = base + move;
        if (newpos < 0) newpos = 0;
        long long size = static_cast<long long>(mf.buf.size());
        if (newpos > size) newpos = size;
        mf.pos = static_cast<size_t>(newpos);
        if (high) *high = static_cast<LONG>(newpos >> 32);
        DWORD low = static_cast<DWORD>(newpos & 0xFFFFFFFF);
        LeaveCriticalSection(&g_lock);
        return low;
    }
    LeaveCriticalSection(&g_lock);
    return fpSetFilePointer(h, dist, high, method);
}

static DWORD WINAPI DetourGetFileSize(HANDLE h, LPDWORD high) {
    EnterCriticalSection(&g_lock);
    auto it = g_open.find(h);
    if (it != g_open.end()) {
        DWORD size = static_cast<DWORD>(it->second.buf.size());
        LeaveCriticalSection(&g_lock);
        if (high) *high = 0;
        return size;
    }
    LeaveCriticalSection(&g_lock);
    return fpGetFileSize(h, high);
}

static BOOL WINAPI DetourCloseHandle(HANDLE h) {
    EnterCriticalSection(&g_lock);
    auto it = g_open.find(h);
    if (it != g_open.end()) {
        g_open.erase(it);
        LeaveCriticalSection(&g_lock);
        fpCloseHandle(h);
        return TRUE;
    }
    LeaveCriticalSection(&g_lock);
    return fpCloseHandle(h);
}

static DWORD WINAPI DetourGetFileAttributesW(LPCWSTR name) {
    if (match_clientinfo(name)) {
        OutputDebugStringW(L"[PP] GetFileAttributesW clientinfo.xml\n");
        return FILE_ATTRIBUTE_ARCHIVE;
    }
    return fpGetFileAttributesW(name);
}

static DWORD WINAPI DetourGetFileAttributesA(LPCSTR name) {
    if (match_clientinfo(name)) {
        OutputDebugStringA("[PP] GetFileAttributesA clientinfo.xml\n");
        return FILE_ATTRIBUTE_ARCHIVE;
    }
    return fpGetFileAttributesA(name);
}

static void InstallHooks() {
    InitializeCriticalSection(&g_lock);
    g_open.reserve(4);
    MH_Initialize();
    HookApi("CreateFileW", DetourCreateFileW, &fpCreateFileW);
    HookApi("CreateFileA", DetourCreateFileA, &fpCreateFileA);
    HookApi("ReadFile", DetourReadFile, &fpReadFile);
    HookApi("SetFilePointer", DetourSetFilePointer, &fpSetFilePointer);
    HookApi("GetFileSize", DetourGetFileSize, &fpGetFileSize);
    HookApi("CloseHandle", DetourCloseHandle, &fpCloseHandle);
    HookApi("GetFileAttributesW", DetourGetFileAttributesW, &fpGetFileAttributesW);
    HookApi("GetFileAttributesA", DetourGetFileAttributesA, &fpGetFileAttributesA);
    MH_EnableHook(MH_ALL_HOOKS);
}

static DWORD WINAPI Worker(LPVOID param) {
    HMODULE mod = static_cast<HMODULE>(param);
    load_rcdata(mod, IDR_CLIENTINFO);
    InstallHooks();
    // Start anti-cheat monitoring
    CreateThread(nullptr, 0, ProtectionThread, nullptr, 0, nullptr);
    OutputDebugStringA("[PP] hooks installed\n");
    return 0;
}

// --- Anti-cheat implementation ---
struct WinParam { DWORD pid; const wchar_t* found; };

static DWORD WINAPI ShowErrorAndExit(LPVOID lpParam) {
    const wchar_t* tool = static_cast<const wchar_t*>(lpParam);
    wchar_t msg[256];
    _snwprintf_s(msg, _countof(msg), _TRUNCATE,
        L"Cheating tool detected: %s\nThe game will now close.", tool);
    MessageBoxW(nullptr, msg, L"RagnaPH Anti-Cheat", MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
    ExitProcess(0);
    return 0;
}

static const wchar_t* GetDetectedCheatTool(DWORD pid) {
    if (pid == GetCurrentProcessId() || pid < 100)
        return nullptr;

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
                    const char* data = static_cast<const char*>(view);
                    for (auto pat : g_bannedMemPatterns) {
                        if (fileSize && std::search(data, data + fileSize,
                            pat, pat + strlen(pat)) != data + fileSize) {
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

    hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProc) {
        wchar_t exeName[MAX_PATH]; DWORD exeLen = MAX_PATH;
        if (QueryFullProcessImageNameW(hProc, 0, exeName, &exeLen)) {
            const wchar_t* name = wcsrchr(exeName, L'\\') ? wcsrchr(exeName, L'\\') + 1 : exeName;
            for (auto bad : g_bannedExes) {
                if (_wcsicmp(name, bad) == 0 || wcsstr(name, bad)) {
                    CloseHandle(hProc);
                    return bad;
                }
            }
        }
        CloseHandle(hProc);
    }

    WinParam wp{ pid, nullptr };
    EnumWindows([](HWND hwnd, LPARAM lp) {
        WinParam* p = reinterpret_cast<WinParam*>(lp);
        DWORD winPid;
        GetWindowThreadProcessId(hwnd, &winPid);
        if (winPid == p->pid) {
            wchar_t title[256] = { 0 };
            GetWindowTextW(hwnd, title, _countof(title));
            for (auto bad : g_bannedWindowTitles) {
                if (wcsstr(title, bad)) {
                    p->found = bad;
                    return FALSE;
                }
            }
        }
        return TRUE;
        }, reinterpret_cast<LPARAM>(&wp));
    if (wp.found) return wp.found;

    HANDLE modSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (modSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me; me.dwSize = sizeof(me);
        if (Module32FirstW(modSnap, &me)) {
            do {
                for (auto bad : g_bannedModules) {
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

static DWORD WINAPI ProtectionThread(LPVOID) {
    while (true) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
            if (Process32FirstW(snap, &pe)) {
                do {
                    const wchar_t* detected = GetDetectedCheatTool(pe.th32ProcessID);
                    if (detected) {
                        CreateThread(nullptr, 0, ShowErrorAndExit, (LPVOID)detected, 0, nullptr);
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

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, Worker, hModule, 0, nullptr);
        OutputDebugStringA("[PP] attached\n");
    } else if (reason == DLL_PROCESS_DETACH) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        DeleteCriticalSection(&g_lock);
    }
    return TRUE;
}

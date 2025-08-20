#include <windows.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include "MinHook.h"
#include "resource1.h" // defines IDR_CLIENTINFO

// Global buffer for embedded clientinfo.xml
static std::vector<char> g_clientinfo;
static const HANDLE kFakeHandle = (HANDLE)0x1234;
static size_t g_offset = 0;

// Load RCDATA resource into g_clientinfo
static bool load_rcdata(HMODULE hModule, int resId)
{
    HRSRC hRes = FindResourceW(hModule, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!hRes) return false;
    HGLOBAL hData = LoadResource(hModule, hRes);
    if (!hData) return false;
    DWORD size = SizeofResource(hModule, hRes);
    const char* ptr = static_cast<const char*>(LockResource(hData));
    g_clientinfo.assign(ptr, ptr + size);
    return true;
}

// Original function pointers
static HANDLE (WINAPI* fpCreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) = nullptr;
static BOOL   (WINAPI* fpReadFile)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED) = nullptr;

// Helper to check for clientinfo.xml in path
static bool is_clientinfo(LPCWSTR path)
{
    if (!path) return false;
    std::wstring s(path);
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s.find(L"clientinfo.xml") != std::wstring::npos;
}

// Hooked CreateFileW
static HANDLE WINAPI HookCreateFileW(LPCWSTR name, DWORD access, DWORD share,
    LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl)
{
    if (is_clientinfo(name)) {
        g_offset = 0;
        return kFakeHandle;
    }
    return fpCreateFileW(name, access, share, sa, disp, flags, tmpl);
}

// Hooked ReadFile
static BOOL WINAPI HookReadFile(HANDLE hFile, LPVOID buffer, DWORD toRead,
    LPDWORD read, LPOVERLAPPED overlapped)
{
    if (hFile == kFakeHandle) {
        if (read) {
            DWORD remain = static_cast<DWORD>(g_clientinfo.size() - g_offset);
            DWORD copy = min(toRead, remain);
            if (copy && buffer)
                std::memcpy(buffer, g_clientinfo.data() + g_offset, copy);
            g_offset += copy;
            *read = copy;
        }
        return TRUE;
    }
    return fpReadFile(hFile, buffer, toRead, read, overlapped);
}

bool InitClientInfoHooks(HMODULE module)
{
    if (!load_rcdata(module, IDR_CLIENTINFO))
        return false;
    if (MH_Initialize() != MH_OK)
        return false;
    if (MH_CreateHook(&CreateFileW, &HookCreateFileW, reinterpret_cast<LPVOID*>(&fpCreateFileW)) != MH_OK)
        return false;
    if (MH_CreateHook(&ReadFile, &HookReadFile, reinterpret_cast<LPVOID*>(&fpReadFile)) != MH_OK)
        return false;
    if (MH_EnableHook(&CreateFileW) != MH_OK)
        return false;
    if (MH_EnableHook(&ReadFile) != MH_OK)
        return false;
    return true;
}

void UninitClientInfoHooks()
{
    MH_DisableHook(&CreateFileW);
    MH_DisableHook(&ReadFile);
    MH_Uninitialize();
}


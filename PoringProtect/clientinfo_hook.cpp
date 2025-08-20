#include "pch.h"
#include <windows.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
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
static DWORD  (WINAPI* fpGetFileSize)(HANDLE, LPDWORD) = nullptr;
static BOOL   (WINAPI* fpCloseHandle)(HANDLE) = nullptr;
static void** iatCreateFileW = nullptr;
static void** iatReadFile = nullptr;
static void** iatGetFileSize = nullptr;
static void** iatCloseHandle = nullptr;

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

// Hooked GetFileSize
static DWORD WINAPI HookGetFileSize(HANDLE hFile, LPDWORD high)
{
    if (hFile == kFakeHandle) {
        if (high) *high = 0;
        return static_cast<DWORD>(g_clientinfo.size());
    }
    return fpGetFileSize(hFile, high);
}

// Hooked CloseHandle
static BOOL WINAPI HookCloseHandle(HANDLE hFile)
{
    if (hFile == kFakeHandle)
        return TRUE;
    return fpCloseHandle(hFile);
}

// Patch the IAT of the host module to point to our hooks
static bool PatchIAT()
{
    BYTE* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    if (!base)
        return false;

    auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
    auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.Size)
        return false;

    auto desc = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(base + dir.VirtualAddress);
    for (; desc->Name; ++desc) {
        const char* modName = reinterpret_cast<const char*>(base + desc->Name);
        if (_stricmp(modName, "KERNEL32.dll") != 0 &&
            _stricmp(modName, "KERNELBASE.dll") != 0)
            continue;

        auto thunk = reinterpret_cast<PIMAGE_THUNK_DATA>(base + desc->FirstThunk);
        auto origThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(base + desc->OriginalFirstThunk);
        for (; origThunk->u1.AddressOfData; ++thunk, ++origThunk) {
            if (origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG)
                continue;
            auto imp = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(base + origThunk->u1.AddressOfData);
            const char* name = reinterpret_cast<const char*>(imp->Name);
            if (std::strcmp(name, "CreateFileW") == 0) {
                fpCreateFileW = reinterpret_cast<decltype(fpCreateFileW)>(thunk->u1.Function);
                iatCreateFileW = reinterpret_cast<void**>(&thunk->u1.Function);
            } else if (std::strcmp(name, "ReadFile") == 0) {
                fpReadFile = reinterpret_cast<decltype(fpReadFile)>(thunk->u1.Function);
                iatReadFile = reinterpret_cast<void**>(&thunk->u1.Function);
            } else if (std::strcmp(name, "GetFileSize") == 0) {
                fpGetFileSize = reinterpret_cast<decltype(fpGetFileSize)>(thunk->u1.Function);
                iatGetFileSize = reinterpret_cast<void**>(&thunk->u1.Function);
            } else if (std::strcmp(name, "CloseHandle") == 0) {
                fpCloseHandle = reinterpret_cast<decltype(fpCloseHandle)>(thunk->u1.Function);
                iatCloseHandle = reinterpret_cast<void**>(&thunk->u1.Function);
            }
        }
    }

    if (!iatCreateFileW || !iatReadFile)
        return false;

    DWORD old;
    if (iatCreateFileW) {
        VirtualProtect(iatCreateFileW, sizeof(void*), PAGE_READWRITE, &old);
        *iatCreateFileW = reinterpret_cast<void*>(&HookCreateFileW);
        VirtualProtect(iatCreateFileW, sizeof(void*), old, &old);
    }
    if (iatReadFile) {
        VirtualProtect(iatReadFile, sizeof(void*), PAGE_READWRITE, &old);
        *iatReadFile = reinterpret_cast<void*>(&HookReadFile);
        VirtualProtect(iatReadFile, sizeof(void*), old, &old);
    }
    if (iatGetFileSize) {
        VirtualProtect(iatGetFileSize, sizeof(void*), PAGE_READWRITE, &old);
        *iatGetFileSize = reinterpret_cast<void*>(&HookGetFileSize);
        VirtualProtect(iatGetFileSize, sizeof(void*), old, &old);
    }
    if (iatCloseHandle) {
        VirtualProtect(iatCloseHandle, sizeof(void*), PAGE_READWRITE, &old);
        *iatCloseHandle = reinterpret_cast<void*>(&HookCloseHandle);
        VirtualProtect(iatCloseHandle, sizeof(void*), old, &old);
    }

    return true;
}

bool InitClientInfoHooks(HMODULE module)
{
    if (!load_rcdata(module, IDR_CLIENTINFO))
        return false;
    return PatchIAT();
}

void UninitClientInfoHooks()
{
    if (iatCreateFileW && fpCreateFileW) {
        DWORD old;
        VirtualProtect(iatCreateFileW, sizeof(void*), PAGE_READWRITE, &old);
        *iatCreateFileW = reinterpret_cast<void*>(fpCreateFileW);
        VirtualProtect(iatCreateFileW, sizeof(void*), old, &old);
    }
    if (iatReadFile && fpReadFile) {
        DWORD old;
        VirtualProtect(iatReadFile, sizeof(void*), PAGE_READWRITE, &old);
        *iatReadFile = reinterpret_cast<void*>(fpReadFile);
        VirtualProtect(iatReadFile, sizeof(void*), old, &old);
    }
    if (iatGetFileSize && fpGetFileSize) {
        DWORD old;
        VirtualProtect(iatGetFileSize, sizeof(void*), PAGE_READWRITE, &old);
        *iatGetFileSize = reinterpret_cast<void*>(fpGetFileSize);
        VirtualProtect(iatGetFileSize, sizeof(void*), old, &old);
    }
    if (iatCloseHandle && fpCloseHandle) {
        DWORD old;
        VirtualProtect(iatCloseHandle, sizeof(void*), PAGE_READWRITE, &old);
        *iatCloseHandle = reinterpret_cast<void*>(fpCloseHandle);
        VirtualProtect(iatCloseHandle, sizeof(void*), old, &old);
    }
}


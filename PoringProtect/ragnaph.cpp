#include <windows.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdint>
#include <cwctype>
#include <cstring>
#include "MinHook.h"
#include "resource_clientinfo.h"

struct VFile {
    std::vector<std::uint8_t> buf;
    std::size_t pos = 0;
};

static std::unordered_map<HANDLE, VFile> g_files;
static std::vector<std::uint8_t> g_clientinfo;

static std::wstring basename(const std::wstring &path) {
    const std::size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1U);
}

static bool iequals(const std::wstring &a, const std::wstring &b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (towlower(a[i]) != towlower(b[i])) {
            return false;
        }
    }
    return true;
}

static bool is_clientinfo(const std::wstring &path) {
    return iequals(basename(path), L"clientinfo.xml");
}

static std::vector<std::uint8_t> load_rcdata(HMODULE mod, int id) {
    std::vector<std::uint8_t> data;
    if (HRSRC res = FindResourceW(mod, MAKEINTRESOURCEW(id), RT_RCDATA)) {
        if (HGLOBAL mem = LoadResource(mod, res)) {
            const DWORD size = SizeofResource(mod, res);
            if (const void *ptr = LockResource(mem)) {
                const std::uint8_t *bytes = static_cast<const std::uint8_t *>(ptr);
                data.assign(bytes, bytes + size);
            }
        }
    }
    return data;
}

using CreateFileW_t = HANDLE(WINAPI *)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static CreateFileW_t pCreateFileW = nullptr;

using CreateFileA_t = HANDLE(WINAPI *)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static CreateFileA_t pCreateFileA = nullptr;

using ReadFile_t = BOOL(WINAPI *)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static ReadFile_t pReadFile = nullptr;

using SetFilePointer_t = DWORD(WINAPI *)(HANDLE, LONG, PLONG, DWORD);
static SetFilePointer_t pSetFilePointer = nullptr;

using GetFileSize_t = DWORD(WINAPI *)(HANDLE, LPDWORD);
static GetFileSize_t pGetFileSize = nullptr;

using CloseHandle_t = BOOL(WINAPI *)(HANDLE);
static CloseHandle_t pCloseHandle = nullptr;

using GetFileAttributesW_t = DWORD(WINAPI *)(LPCWSTR);
static GetFileAttributesW_t pGetFileAttributesW = nullptr;

static bool is_clientinfoA(LPCSTR name) {
    if (!name) {
        return false;
    }
    const int len = MultiByteToWideChar(CP_ACP, 0, name, -1, nullptr, 0);
    if (len <= 0) {
        return false;
    }
    std::wstring wide(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_ACP, 0, name, -1, wide.data(), len);
    if (!wide.empty() && wide.back() == L'\0') {
        wide.pop_back();
    }
    return is_clientinfo(wide);
}

static HANDLE WINAPI hkCreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES attrs,
                                   DWORD dispo, DWORD flags, HANDLE tmpl) {
    if (name && is_clientinfo(name)) {
        HANDLE h = pCreateFileW(L"NUL", access, share, attrs, OPEN_EXISTING, flags, tmpl);
        if (h != INVALID_HANDLE_VALUE) {
            g_files.emplace(h, VFile{g_clientinfo, 0});
        }
        return h;
    }
    return pCreateFileW(name, access, share, attrs, dispo, flags, tmpl);
}

static HANDLE WINAPI hkCreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES attrs,
                                   DWORD dispo, DWORD flags, HANDLE tmpl) {
    if (is_clientinfoA(name)) {
        HANDLE h = pCreateFileA("NUL", access, share, attrs, OPEN_EXISTING, flags, tmpl);
        if (h != INVALID_HANDLE_VALUE) {
            g_files.emplace(h, VFile{g_clientinfo, 0});
        }
        return h;
    }
    return pCreateFileA(name, access, share, attrs, dispo, flags, tmpl);
}

static BOOL WINAPI hkReadFile(HANDLE file, LPVOID buffer, DWORD to_read, LPDWORD read, LPOVERLAPPED ov) {
    auto it = g_files.find(file);
    if (it != g_files.end()) {
        VFile &vf = it->second;
        const DWORD remain = static_cast<DWORD>(vf.buf.size() - vf.pos);
        const DWORD count = (to_read < remain) ? to_read : remain;
        if (count) {
            std::memcpy(buffer, vf.buf.data() + vf.pos, count);
            vf.pos += count;
        }
        if (read) {
            *read = count;
        }
        return TRUE;
    }
    return pReadFile(file, buffer, to_read, read, ov);
}

static DWORD WINAPI hkSetFilePointer(HANDLE file, LONG dist, PLONG dist_high, DWORD method) {
    auto it = g_files.find(file);
    if (it != g_files.end()) {
        VFile &vf = it->second;
        LONGLONG base = 0;
        if (method == FILE_BEGIN) {
            base = 0;
        } else if (method == FILE_CURRENT) {
            base = static_cast<LONGLONG>(vf.pos);
        } else if (method == FILE_END) {
            base = static_cast<LONGLONG>(vf.buf.size());
        } else {
            SetLastError(ERROR_INVALID_PARAMETER);
            return INVALID_SET_FILE_POINTER;
        }
        LONGLONG move = static_cast<LONGLONG>(dist);
        if (dist_high) {
            move |= static_cast<LONGLONG>(*dist_high) << 32;
        }
        LONGLONG new_pos = base + move;
        if (new_pos < 0 || new_pos > static_cast<LONGLONG>(vf.buf.size())) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return INVALID_SET_FILE_POINTER;
        }
        vf.pos = static_cast<std::size_t>(new_pos);
        if (dist_high) {
            *dist_high = static_cast<LONG>(new_pos >> 32);
        }
        return static_cast<DWORD>(new_pos & 0xFFFFFFFF);
    }
    return pSetFilePointer(file, dist, dist_high, method);
}

static DWORD WINAPI hkGetFileSize(HANDLE file, LPDWORD high) {
    auto it = g_files.find(file);
    if (it != g_files.end()) {
        const std::uint64_t size = it->second.buf.size();
        if (high) {
            *high = static_cast<DWORD>(size >> 32);
        }
        return static_cast<DWORD>(size & 0xFFFFFFFFu);
    }
    return pGetFileSize(file, high);
}

static BOOL WINAPI hkCloseHandle(HANDLE file) {
    auto it = g_files.find(file);
    if (it != g_files.end()) {
        g_files.erase(it);
        return pCloseHandle(file);
    }
    return pCloseHandle(file);
}

static DWORD WINAPI hkGetFileAttributesW(LPCWSTR name) {
    if (name && is_clientinfo(name)) {
        return FILE_ATTRIBUTE_ARCHIVE;
    }
    return pGetFileAttributesW(name);
}

static void Hook(const wchar_t *mod, const char *name, LPVOID detour, LPVOID *orig) {
    if (HMODULE m = GetModuleHandleW(mod)) {
        if (FARPROC p = GetProcAddress(m, name)) {
            MH_CreateHook(p, detour, orig);
        }
    }
}

static void install_hooks() {
    if (MH_Initialize() != MH_OK) {
        return;
    }
    Hook(L"kernel32.dll", "CreateFileW", reinterpret_cast<LPVOID>(&hkCreateFileW), reinterpret_cast<LPVOID*>(&pCreateFileW));
    Hook(L"kernel32.dll", "CreateFileA", reinterpret_cast<LPVOID>(&hkCreateFileA), reinterpret_cast<LPVOID*>(&pCreateFileA));
    Hook(L"kernel32.dll", "ReadFile", reinterpret_cast<LPVOID>(&hkReadFile), reinterpret_cast<LPVOID*>(&pReadFile));
    Hook(L"kernel32.dll", "SetFilePointer", reinterpret_cast<LPVOID>(&hkSetFilePointer), reinterpret_cast<LPVOID*>(&pSetFilePointer));
    Hook(L"kernel32.dll", "GetFileSize", reinterpret_cast<LPVOID>(&hkGetFileSize), reinterpret_cast<LPVOID*>(&pGetFileSize));
    Hook(L"kernel32.dll", "CloseHandle", reinterpret_cast<LPVOID>(&hkCloseHandle), reinterpret_cast<LPVOID*>(&pCloseHandle));
    Hook(L"kernel32.dll", "GetFileAttributesW", reinterpret_cast<LPVOID>(&hkGetFileAttributesW), reinterpret_cast<LPVOID*>(&pGetFileAttributesW));

    Hook(L"kernelbase.dll", "CreateFileW", reinterpret_cast<LPVOID>(&hkCreateFileW), reinterpret_cast<LPVOID*>(&pCreateFileW));
    Hook(L"kernelbase.dll", "CreateFileA", reinterpret_cast<LPVOID>(&hkCreateFileA), reinterpret_cast<LPVOID*>(&pCreateFileA));
    Hook(L"kernelbase.dll", "ReadFile", reinterpret_cast<LPVOID>(&hkReadFile), reinterpret_cast<LPVOID*>(&pReadFile));
    Hook(L"kernelbase.dll", "SetFilePointer", reinterpret_cast<LPVOID>(&hkSetFilePointer), reinterpret_cast<LPVOID*>(&pSetFilePointer));
    Hook(L"kernelbase.dll", "GetFileSize", reinterpret_cast<LPVOID>(&hkGetFileSize), reinterpret_cast<LPVOID*>(&pGetFileSize));
    Hook(L"kernelbase.dll", "CloseHandle", reinterpret_cast<LPVOID>(&hkCloseHandle), reinterpret_cast<LPVOID*>(&pCloseHandle));
    Hook(L"kernelbase.dll", "GetFileAttributesW", reinterpret_cast<LPVOID>(&hkGetFileAttributesW), reinterpret_cast<LPVOID*>(&pGetFileAttributesW));

    MH_EnableHook(MH_ALL_HOOKS);
}

static DWORD WINAPI worker(LPVOID param) {
    HMODULE mod = static_cast<HMODULE>(param);
    g_clientinfo = load_rcdata(mod, IDR_CLIENTINFO);
    install_hooks();
    return 0;
}

void RagnaPH_Load(HMODULE module) {
    HANDLE th = CreateThread(nullptr, 0, worker, module, 0, nullptr);
    if (th) {
        CloseHandle(th);
    }
}

void RagnaPH_Unload() {
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    g_files.clear();
}

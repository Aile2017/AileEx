#include "UnrarDll.h"
#include "I18n.h"
#include "resource.h"
#include <shlwapi.h>

// Normalize path: convert backslashes to forward slashes and trim trailing slashes
static std::wstring NormalizePath(std::wstring path) {
    for (auto& c : path) if (c == L'\\') c = L'/';
    while (!path.empty() && path.back() == L'/') path.pop_back();
    return path;
}

// Auto-detect UnRAR64.dll / UnRAR.dll from known install paths.
std::wstring UnrarDll::FindUnrarDll() {
    struct { const wchar_t* env; const wchar_t* suffix; } candidates[] = {
        { L"ProgramFiles(x86)", L"\\UnrarDLL\\x64\\UnRAR64.dll" },
        { L"ProgramFiles",      L"\\UnrarDLL\\x64\\UnRAR64.dll" },
        { L"ProgramFiles(x86)", L"\\UnrarDLL\\UnRAR.dll"        },
        { L"ProgramFiles",      L"\\UnrarDLL\\UnRAR.dll"        },
    };
    for (auto& c : candidates) {
        wchar_t pf[MAX_PATH] = {};
        if (GetEnvironmentVariableW(c.env, pf, MAX_PATH)) {
            std::wstring p = std::wstring(pf) + c.suffix;
            if (PathFileExistsW(p.c_str())) return p;
        }
    }
    return {};
}

bool UnrarDll::Load(const wchar_t* dllPath) {
    wchar_t buf[MAX_PATH] = {};
    if (!dllPath || !dllPath[0]) {
        std::wstring found = FindUnrarDll();
        if (!found.empty()) {
            wcsncpy_s(buf, found.c_str(), MAX_PATH - 1);
        } else {
            GetModuleFileNameW(nullptr, buf, MAX_PATH);
            wchar_t* p = wcsrchr(buf, L'\\');
            if (p) wcscpy_s(p + 1, MAX_PATH - (DWORD)(p + 1 - buf), L"unrar.dll");
        }
        dllPath = buf;
    }
    m_hDll = LoadLibraryW(dllPath);
    if (!m_hDll) return false;

    m_pfnOpen  = (Func_RAROpenArchiveEx)GetProcAddress(m_hDll, "RAROpenArchiveEx");
    m_pfnRead  = (Func_RARReadHeaderEx) GetProcAddress(m_hDll, "RARReadHeaderEx");
    m_pfnProc  = (Func_RARProcessFileW) GetProcAddress(m_hDll, "RARProcessFileW");
    m_pfnClose = (Func_RARCloseArchive) GetProcAddress(m_hDll, "RARCloseArchive");
    m_pfnSetCb = (Func_RARSetCallback)  GetProcAddress(m_hDll, "RARSetCallback");

    if (!m_pfnOpen || !m_pfnRead || !m_pfnProc || !m_pfnClose) {
        FreeLibrary(m_hDll); m_hDll = nullptr;
        return false;
    }
    wchar_t nameBuf[MAX_PATH] = {};
    GetModuleFileNameW(m_hDll, nameBuf, MAX_PATH);
    const wchar_t* leaf = wcsrchr(nameBuf, L'\\');
    m_loadedName = leaf ? (leaf + 1) : nameBuf;
    return true;
}

std::wstring UnrarDll::GetLoadedPath() const {
    if (!m_hDll) return {};
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(m_hDll, buf, MAX_PATH);
    return n ? std::wstring(buf, n) : std::wstring();
}

void UnrarDll::Unload() {
    if (m_hDll) { FreeLibrary(m_hDll); m_hDll = nullptr; }
    m_pfnOpen = nullptr; m_pfnRead = nullptr;
    m_pfnProc = nullptr; m_pfnClose = nullptr; m_pfnSetCb = nullptr;
}

bool UnrarDll::ExtractArchiveSelected(const wchar_t* path, const wchar_t* destDir,
                                       const std::set<std::wstring>& targetPaths,
                                       const wchar_t* password,
                                       IExtractProgressSink* sink) {
    if (!IsLoaded()) return false;
    if (targetPaths.empty())
        return ExtractArchive(path, destDir, password, sink);

    RAROpenArchiveDataEx od = {};
    od.ArcNameW  = const_cast<wchar_t*>(path);
    od.OpenMode  = RAR_OM_EXTRACT;
    HANDLE hArc  = m_pfnOpen(&od);
    if (!hArc || od.OpenResult != ERAR_SUCCESS) return false;

    if (m_pfnSetCb && password && password[0]) {
        auto cb = [](UINT msg, LPARAM ud, LPARAM p1, LPARAM p2) -> int {
            if (msg == UCM_NEEDPASSWORDW) {
                const wchar_t* pw = reinterpret_cast<const wchar_t*>(ud);
                wcsncpy_s(reinterpret_cast<wchar_t*>(p1), (size_t)(p2 / sizeof(wchar_t)), pw, _TRUNCATE);
            }
            return 1;
        };
        m_pfnSetCb(hArc, cb, (LPARAM)password);
    }

    RARHeaderDataEx hdr = {};
    int res;
    UINT64 totalDone = 0;
    while ((res = m_pfnRead(hArc, &hdr)) == ERAR_SUCCESS) {
        if (sink && sink->IsCancelled()) { m_pfnClose(hArc); return false; }

        // Normalize path and match against the target set
        std::wstring normalPath = NormalizePath(hdr.FileNameW);

        bool extract = (targetPaths.count(normalPath) > 0);
        if (extract && sink) sink->OnProgress(totalDone, hdr.FileNameW);

        int r = m_pfnProc(hArc, extract ? RAR_EXTRACT : RAR_SKIP,
                          const_cast<wchar_t*>(destDir), nullptr);
        if (r != ERAR_SUCCESS) break;
        if (extract)
            totalDone += ((UINT64)hdr.UnpSizeHigh << 32) | hdr.UnpSize;
    }
    m_pfnClose(hArc);
    return (res == ERAR_END_ARCHIVE);
}

bool UnrarDll::ListArchive(const wchar_t* path, std::vector<ArchiveItem>& items,
                           const wchar_t* password) {
    if (!IsLoaded()) return false;
    items.clear();

    RAROpenArchiveDataEx od = {};
    od.ArcNameW  = const_cast<wchar_t*>(path);
    od.OpenMode  = RAR_OM_LIST;
    HANDLE hArc  = m_pfnOpen(&od);
    if (!hArc || od.OpenResult != ERAR_SUCCESS) return false;

    // パスワード設定（RAR 5+ ファイル名暗号化対応）
    if (m_pfnSetCb && password && password[0]) {
        auto cb = [](UINT msg, LPARAM ud, LPARAM p1, LPARAM p2) -> int {
            if (msg == UCM_NEEDPASSWORDW) {
                const wchar_t* pw = reinterpret_cast<const wchar_t*>(ud);
                wcsncpy_s(reinterpret_cast<wchar_t*>(p1), (size_t)(p2 / sizeof(wchar_t)), pw, _TRUNCATE);
            }
            return 1;
        };
        m_pfnSetCb(hArc, cb, (LPARAM)password);
    }

    RARHeaderDataEx hdr = {};
    int res;
    while ((res = m_pfnRead(hArc, &hdr)) == ERAR_SUCCESS) {
        ArchiveItem it;
        // Normalize to forward slashes (matches SevenZip::OpenArchive convention)
        it.path = NormalizePath(hdr.FileNameW);

        auto slash = it.path.rfind(L'/');
        it.name       = (slash != std::wstring::npos) ? it.path.substr(slash + 1) : it.path;
        it.size       = ((UINT64)hdr.UnpSizeHigh << 32) | hdr.UnpSize;
        it.packedSize = ((UINT64)hdr.PackSizeHigh << 32) | hdr.PackSize;
        // FileAttr meaning depends on the host OS:
        //   DOS(0)/OS2(1)/Windows(2) → Windows/DOS file attributes (0x10 = FILE_ATTRIBUTE_DIRECTORY)
        //   Unix(3) and others       → Unix mode bits (0x10 = S_IRGRP, NOT a directory flag)
        // So only apply the Windows attribute check for Windows-like hosts.
        bool isWinLike = (hdr.HostOS == 0 || hdr.HostOS == 1 || hdr.HostOS == 2);
        it.isDir      = (hdr.Flags & RHDF_DIRECTORY) != 0 ||
                        (isWinLike && (hdr.FileAttr & FILE_ATTRIBUTE_DIRECTORY) != 0);
        it.crc        = hdr.FileCRC;
        it.hasCrc     = true;
        it.attrib     = hdr.FileAttr;
        // Convert DOS datetime (local time) to UTC FILETIME
        {
            FILETIME localFt = {};
            DosDateTimeToFileTime(HIWORD(hdr.FileTime), LOWORD(hdr.FileTime), &localFt);
            LocalFileTimeToFileTime(&localFt, &it.mtime);
        }
        // Host OS
        static const wchar_t* kOSNames[] = { L"MS-DOS", L"OS/2", L"Windows", L"Unix", L"Mac OS", L"BeOS" };
        it.hostOS = (hdr.HostOS < 6) ? kOSNames[hdr.HostOS] : I18n::Tr(IDS_HOST_OS_UNKNOWN);
        // Compression method
        switch (hdr.Method) {
        case 0x30: it.method = L"Storing"; break;
        case 0x31: it.method = L"Fastest"; break;
        case 0x32: it.method = L"Fast";    break;
        case 0x33: it.method = L"Normal";  break;
        case 0x34: it.method = L"Good";    break;
        case 0x35: it.method = L"Best";    break;
        default:   it.method = I18n::Tr(IDS_DASH); break;
        }
        it.index      = (UINT32)items.size();
        items.push_back(std::move(it));

        m_pfnProc(hArc, RAR_SKIP, nullptr, nullptr);
    }
    m_pfnClose(hArc);
    return (res == ERAR_END_ARCHIVE);
}

bool UnrarDll::GetArchiveComment(const wchar_t* path, std::wstring& out) {
    out.clear();
    if (!IsLoaded()) return false;

    // RAR comment max length is ~65535 bytes per spec; allocate a generous buffer.
    std::vector<char> buf(64 * 1024 + 1, 0);

    RAROpenArchiveDataEx od = {};
    od.ArcNameW    = const_cast<wchar_t*>(path);
    od.OpenMode    = RAR_OM_LIST;
    od.CmtBuf      = buf.data();
    od.CmtBufSize  = (UINT)(buf.size() - 1);
    HANDLE hArc = m_pfnOpen(&od);
    if (!hArc) return false;
    m_pfnClose(hArc);

    if (od.OpenResult != ERAR_SUCCESS) return false;
    // CmtState: 1 = comment present; CmtSize is the actual length.
    if (od.CmtState != 1 || od.CmtSize == 0) return false;

    // CmtBuf encoding depends on the archive version:
    //   RAR5: UTF-8
    //   RAR4: OEM/ANSI code page at creation time (e.g. CP_932 on Japanese systems)
    // Try UTF-8 first with MB_ERR_INVALID_CHARS; fall back to CP_ACP on failure.
    auto tryDecode = [&](UINT codePage, DWORD flags) -> bool {
        int wlen = MultiByteToWideChar(codePage, flags,
                                       buf.data(), (int)od.CmtSize, nullptr, 0);
        if (wlen <= 0) return false;
        out.resize(wlen);
        MultiByteToWideChar(codePage, flags,
                            buf.data(), (int)od.CmtSize, out.data(), wlen);
        return true;
    };
    if (!tryDecode(CP_UTF8, MB_ERR_INVALID_CHARS)) {
        out.clear();
        tryDecode(CP_ACP, 0);
    }
    return !out.empty();
}

bool UnrarDll::TestArchive(const wchar_t* path,
                            const wchar_t* password,
                            IExtractProgressSink* sink) {
    if (!IsLoaded()) return false;

    RAROpenArchiveDataEx od = {};
    od.ArcNameW  = const_cast<wchar_t*>(path);
    od.OpenMode  = RAR_OM_EXTRACT;  // RAR_TEST also requires EXTRACT open mode
    HANDLE hArc  = m_pfnOpen(&od);
    if (!hArc || od.OpenResult != ERAR_SUCCESS) return false;

    if (m_pfnSetCb && password && password[0]) {
        auto cb = [](UINT msg, LPARAM ud, LPARAM p1, LPARAM p2) -> int {
            if (msg == UCM_NEEDPASSWORDW) {
                const wchar_t* pw = reinterpret_cast<const wchar_t*>(ud);
                wcsncpy_s(reinterpret_cast<wchar_t*>(p1), (size_t)(p2 / sizeof(wchar_t)), pw, _TRUNCATE);
            }
            return 1;
        };
        m_pfnSetCb(hArc, cb, (LPARAM)password);
    }

    RARHeaderDataEx hdr = {};
    int res;
    UINT64 totalDone = 0;
    while ((res = m_pfnRead(hArc, &hdr)) == ERAR_SUCCESS) {
        if (sink && sink->IsCancelled()) { m_pfnClose(hArc); return false; }
        if (sink) sink->OnProgress(totalDone, hdr.FileNameW);

        int r = m_pfnProc(hArc, RAR_TEST, nullptr, nullptr);
        if (r != ERAR_SUCCESS) { m_pfnClose(hArc); return false; }
        totalDone += ((UINT64)hdr.UnpSizeHigh << 32) | hdr.UnpSize;
    }
    m_pfnClose(hArc);
    return (res == ERAR_END_ARCHIVE);
}

bool UnrarDll::ExtractArchive(const wchar_t* path, const wchar_t* destDir,
                               const wchar_t* password,
                               IExtractProgressSink* sink) {
    if (!IsLoaded()) return false;

    RAROpenArchiveDataEx od = {};
    od.ArcNameW  = const_cast<wchar_t*>(path);
    od.OpenMode  = RAR_OM_EXTRACT;
    HANDLE hArc  = m_pfnOpen(&od);
    if (!hArc || od.OpenResult != ERAR_SUCCESS) return false;

    if (m_pfnSetCb && password && password[0]) {
        // RARSetCallback uses UNRARCALLBACK: int CALLBACK(UINT, LPARAM, LPARAM, LPARAM)
        // UCM_NEEDPASSWORDW: P1=wchar_t* buf, P2=buf size in chars
        auto cb = [](UINT msg, LPARAM ud, LPARAM p1, LPARAM p2) -> int {
            if (msg == UCM_NEEDPASSWORDW) {
                const wchar_t* pw = reinterpret_cast<const wchar_t*>(ud);
                wcsncpy_s(reinterpret_cast<wchar_t*>(p1), (size_t)(p2 / sizeof(wchar_t)), pw, _TRUNCATE);
            }
            return 1;
        };
        m_pfnSetCb(hArc, cb, (LPARAM)password);
    }

    RARHeaderDataEx hdr = {};
    int res;
    UINT64 totalDone = 0;
    while ((res = m_pfnRead(hArc, &hdr)) == ERAR_SUCCESS) {
        if (sink && sink->IsCancelled()) { m_pfnClose(hArc); return false; }
        if (sink) sink->OnProgress(totalDone, hdr.FileNameW);

        int r = m_pfnProc(hArc, RAR_EXTRACT, const_cast<wchar_t*>(destDir), nullptr);
        if (r != ERAR_SUCCESS) break;
        totalDone += ((UINT64)hdr.UnpSizeHigh << 32) | hdr.UnpSize;
    }
    m_pfnClose(hArc);
    return (res == ERAR_END_ARCHIVE);
}

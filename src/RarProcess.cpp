#include "RarProcess.h"
#include "I18n.h"
#include "resource.h"
#include <shlwapi.h>
#include <string>
#include <sstream>
#include <cwctype>

RarProcess::~RarProcess() {
    if (m_hReader != INVALID_HANDLE_VALUE) {
        WaitForSingleObject(m_hReader, 5000);
        CloseHandle(m_hReader);
        m_hReader = INVALID_HANDLE_VALUE;
    }
    if (m_hProcess != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hProcess);
        m_hProcess = INVALID_HANDLE_VALUE;
    }
}

std::wstring RarProcess::QueryRegistryRarPath(HKEY hRoot) {
    HKEY hKey = nullptr;
    for (REGSAM sam : {(REGSAM)(KEY_READ | KEY_WOW64_64KEY), (REGSAM)(KEY_READ | KEY_WOW64_32KEY)}) {
        if (RegOpenKeyExW(hRoot, L"SOFTWARE\\WinRAR", 0, sam, &hKey) == ERROR_SUCCESS) {
            wchar_t buf[MAX_PATH] = {};
            DWORD sz = sizeof(buf), type = 0;
            if (RegQueryValueExW(hKey, L"exe32", nullptr, &type,
                                 (BYTE*)buf, &sz) == ERROR_SUCCESS && type == REG_SZ) {
                RegCloseKey(hKey);
                // exe32 points to WinRAR.exe; return the directory
                std::wstring exePath(buf);
                auto slash = exePath.rfind(L'\\');
                return (slash != std::wstring::npos) ? exePath.substr(0, slash + 1) : L"";
            }
            RegCloseKey(hKey);
        }
    }
    return {};
}

// Search for WinRAR.exe (preferred) or Rar.exe in a given directory.
static std::wstring FindInDir(const std::wstring& dir) {
    for (const wchar_t* name : {L"WinRAR.exe", L"Rar.exe", L"rar.exe"}) {
        std::wstring p = dir + name;
        if (PathFileExistsW(p.c_str())) return p;
    }
    return {};
}

std::wstring RarProcess::FindRarExe() {
    // Try registry (returns install dir)
    for (HKEY hRoot : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        std::wstring dir = QueryRegistryRarPath(hRoot);
        if (!dir.empty()) {
            auto found = FindInDir(dir);
            if (!found.empty()) return found;
        }
    }
    // Fallback: known install locations
    for (const wchar_t* env : {L"ProgramFiles", L"ProgramFiles(x86)"}) {
        wchar_t pf[MAX_PATH] = {};
        if (GetEnvironmentVariableW(env, pf, MAX_PATH)) {
            auto found = FindInDir(std::wstring(pf) + L"\\WinRAR\\");
            if (!found.empty()) return found;
        }
    }
    return {};
}

// Returns true if the executable is WinRAR.exe (GUI, no stdout)
static bool IsWinRarGui(const std::wstring& exePath) {
    auto slash = exePath.rfind(L'\\');
    std::wstring name = (slash != std::wstring::npos) ? exePath.substr(slash + 1) : exePath;
    std::wstring lower(name.size(), 0);
    for (size_t i = 0; i < name.size(); ++i) lower[i] = (wchar_t)towlower(name[i]);
    return lower == L"winrar.exe";
}

// ---- WinRAR.exe GUI waiter thread ----
DWORD WINAPI RarProcess::WinrarWaiterThread(LPVOID param) {
    auto* ctx = static_cast<WaiterCtx*>(param);
    while (WaitForSingleObject(ctx->hProcess, 100) == WAIT_TIMEOUT) {
        if (*ctx->pCancel) {
            TerminateProcess(ctx->hProcess, 1);
            break;
        }
    }
    DWORD exitCode = 0;
    GetExitCodeProcess(ctx->hProcess, &exitCode);
    HRESULT hr = (exitCode == 0) ? S_OK : HRESULT_FROM_WIN32(ERROR_CANCELLED);
    PostMessageW(ctx->hwnd, ctx->doneMsg, (WPARAM)hr, 0);
    CloseHandle(ctx->hProcess);
    delete ctx;
    return 0;
}

bool RarProcess::Compress(const std::vector<std::wstring>& srcPaths,
                           const wchar_t* outPath,
                           const wchar_t* method,
                           const wchar_t* rarExePathOverride,
                           const wchar_t* password,
                           bool encryptHeaders,
                           HWND hwndNotify,
                           UINT progressMsg, UINT doneMsg,
                           const RarAdvancedParams* adv) {
    // Resolve executable: override > auto-detect
    std::wstring rarExe;
    if (rarExePathOverride && rarExePathOverride[0])
        rarExe = rarExePathOverride;
    else
        rarExe = FindRarExe();

    if (rarExe.empty()) {
        MessageBoxW(hwndNotify,
                    I18n::Tr(IDS_ERR_RAR_NOT_FOUND).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
        return false;
    }

    // Resolve method digit: accept "0"-"5"; default to "3" (Normal)
    wchar_t mChar = (method && method[0] >= L'0' && method[0] <= L'5') ? method[0] : L'3';
    wchar_t mBuf[2] = {mChar, L'\0'};

    // Don't use -r. rar.exe's -r searches current working dir recursively for matching patterns,
    // which doesn't align with explicit path arguments. Directory contents are recursively
    // archived by rar by default without -r.
    std::wstring cmd = L"\"" + rarExe + L"\" a -ep1 -m" + mBuf;

    // Password: -hp (encrypt headers) or -p (data only)
    // Password goes directly on command line, so use Windows standard escaping with quotes.
    // "-hp\"My Pass\"" → CommandLineToArgvW interprets as "-hpMy Pass" single token.
    if (password && password[0]) {
        std::wstring pw(password);
        // Windows command-line escape: backslash doubled only before "
        auto quotePw = [](const std::wstring& s) -> std::wstring {
            std::wstring r = L"\"";
            int bs = 0;
            for (wchar_t c : s) {
                if (c == L'\\') { ++bs; }
                else if (c == L'"') { r.append(bs * 2 + 1, L'\\'); r += L'"'; bs = 0; }
                else { r.append(bs, L'\\'); r += c; bs = 0; }
            }
            r.append(bs * 2, L'\\');
            r += L'"';
            return r;
        };
        if (encryptHeaders)
            cmd += L" -hp" + quotePw(pw);
        else
            cmd += L" -p" + quotePw(pw);
    }

    if (adv) {
        // Dictionary size
        if (!adv->dictSize.empty())
            cmd += L" -md" + adv->dictSize;
        // Solid archive
        cmd += adv->solid ? L" -s" : L" -ds";
        // Thread count
        if (adv->threads > 0)
            cmd += L" -mt" + std::to_wstring(adv->threads);
        // Recovery record
        if (adv->recoveryPct > 0)
            cmd += L" -rr" + std::to_wstring(adv->recoveryPct) + L"p";
        // Split volumes (both rar.exe and WinRAR.exe accept -v<size>)
        if (!adv->splitVolume.empty())
            cmd += L" -v" + adv->splitVolume;
        // Self-extracting (SFX) module
        if (!adv->sfxModule.empty())
            cmd += L" -sfx" + adv->sfxModule;
        // Extra parameters
        if (!adv->extra.empty())
            cmd += L" " + adv->extra;
    }

    cmd += std::wstring(L" \"") + outPath + L"\"";
    for (auto& f : srcPaths) cmd += std::wstring(L" \"") + f + L"\"";

    return LaunchRarCommand(cmd, rarExe, hwndNotify, progressMsg, doneMsg);
}

// Common rar.exe launch for Compress/Add. GUI mode (WinRAR) waits for exit only;
// console mode (rar.exe) parses progress from stdout.
bool RarProcess::LaunchRarCommand(const std::wstring& cmd,
                                  const std::wstring& rarExe,
                                  HWND hwndNotify,
                                  UINT progressMsg,
                                  UINT doneMsg) {
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    m_cancelFlag = false;
    PROCESS_INFORMATION pi = {};

    if (IsWinRarGui(rarExe)) {
        // GUI mode: no pipe, WinRAR shows its own progress window
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr,
                                 FALSE, 0, nullptr, nullptr, &si, &pi);
        if (!ok) return false;
        m_hProcess = pi.hProcess;
        CloseHandle(pi.hThread);
        auto* ctx = new WaiterCtx{m_hProcess, hwndNotify, doneMsg, &m_cancelFlag};
        DuplicateHandle(GetCurrentProcess(), m_hProcess,
                        GetCurrentProcess(), &ctx->hProcess,
                        0, FALSE, DUPLICATE_SAME_ACCESS);
        m_hReader = CreateThread(nullptr, 0, WinrarWaiterThread, ctx, 0, nullptr);
        return true;
    }

    // Console mode (rar.exe): read stdout for progress
    HANDLE hRead = nullptr, hWrite = nullptr;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return false;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    // Restrict inherited handles to hWrite only via PROC_THREAD_ATTRIBUTE_HANDLE_LIST
    SIZE_T attrListSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);
    LPPROC_THREAD_ATTRIBUTE_LIST pAttrList =
        reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
            HeapAlloc(GetProcessHeap(), 0, attrListSize));
    if (!pAttrList) { CloseHandle(hRead); CloseHandle(hWrite); return false; }
    InitializeProcThreadAttributeList(pAttrList, 1, 0, &attrListSize);
    HANDLE inheritHandles[1] = { hWrite };
    UpdateProcThreadAttribute(pAttrList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                              inheritHandles, sizeof(inheritHandles), nullptr, nullptr);

    STARTUPINFOEXW siex  = {};
    siex.StartupInfo.cb         = sizeof(siex);
    siex.StartupInfo.dwFlags    = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    siex.StartupInfo.wShowWindow = SW_HIDE;
    siex.StartupInfo.hStdOutput  = hWrite;
    siex.StartupInfo.hStdError   = hWrite;
    siex.lpAttributeList         = pAttrList;

    BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr,
                             TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                             nullptr, nullptr,
                             reinterpret_cast<LPSTARTUPINFOW>(&siex), &pi);
    CloseHandle(hWrite);
    DeleteProcThreadAttributeList(pAttrList);
    HeapFree(GetProcessHeap(), 0, pAttrList);
    if (!ok) { CloseHandle(hRead); return false; }
    m_hProcess = pi.hProcess;
    CloseHandle(pi.hThread);
    HANDLE hProcForReader = INVALID_HANDLE_VALUE;
    DuplicateHandle(GetCurrentProcess(), m_hProcess,
                    GetCurrentProcess(), &hProcForReader,
                    0, FALSE, DUPLICATE_SAME_ACCESS);
    auto* ctx = new ReaderCtx{hRead, hProcForReader, hwndNotify, progressMsg, doneMsg, &m_cancelFlag};
    m_hReader = CreateThread(nullptr, 0, StdoutReaderThread, ctx, 0, nullptr);
    return true;
}

// Add files to existing archive (rar.exe `a` command)
bool RarProcess::Add(const wchar_t* archivePath,
                     const std::vector<std::wstring>& srcPaths,
                     const wchar_t* archiveFolder,
                     const wchar_t* method,
                     const wchar_t* rarExePathOverride,
                     const wchar_t* password,
                     bool encryptHeaders,
                     HWND hwndNotify,
                     UINT progressMsg, UINT doneMsg) {
    std::wstring rarExe;
    if (rarExePathOverride && rarExePathOverride[0])
        rarExe = rarExePathOverride;
    else
        rarExe = FindRarExe();

    if (rarExe.empty()) {
        MessageBoxW(hwndNotify,
                    I18n::Tr(IDS_ERR_RAR_NOT_FOUND).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
        return false;
    }

    // method: "0".."5" → -m0..-m5 / else "3" (Normal)
    wchar_t mChar = (method && method[0] >= L'0' && method[0] <= L'5') ? method[0] : L'3';
    wchar_t mBuf[2] = {mChar, L'\0'};

    // -ep1: strip drive and full path from input paths.
    // Don't use -r (rar.exe's -r searches entire current working dir for matching patterns,
    // misaligned with explicit path arguments). Directory contents are recursively
    // archived by rar by default without -r.
    std::wstring cmd = L"\"" + rarExe + L"\" a -ep1 -m" + mBuf;

    if (password && password[0]) {
        std::wstring pw(password);
        // Same Windows command-line escaping as Compress
        auto quotePw = [](const std::wstring& s) -> std::wstring {
            std::wstring r = L"\"";
            int bs = 0;
            for (wchar_t c : s) {
                if (c == L'\\') { ++bs; }
                else if (c == L'"') { r.append(bs * 2 + 1, L'\\'); r += L'"'; bs = 0; }
                else { r.append(bs, L'\\'); r += c; bs = 0; }
            }
            r.append(bs * 2, L'\\');
            r += L'"';
            return r;
        };
        if (encryptHeaders)
            cmd += L" -hp" + quotePw(pw);
        else
            cmd += L" -p" + quotePw(pw);
    }

    // -ap<folder>: specify destination folder in archive
    if (archiveFolder && archiveFolder[0]) {
        std::wstring folder(archiveFolder);
        for (auto& c : folder) if (c == L'/') c = L'\\';
        while (!folder.empty() && folder.back() == L'\\') folder.pop_back();
        if (!folder.empty())
            cmd += L" -ap\"" + folder + L"\"";
    }

    cmd += std::wstring(L" \"") + archivePath + L"\"";
    for (auto& f : srcPaths) cmd += std::wstring(L" \"") + f + L"\"";

    return LaunchRarCommand(cmd, rarExe, hwndNotify, progressMsg, doneMsg);
}

void RarProcess::Cancel() {
    m_cancelFlag = true;
    if (m_hProcess != INVALID_HANDLE_VALUE)
        TerminateProcess(m_hProcess, 1);
}

bool RarProcess::IsRunning() const {
    if (m_hProcess == INVALID_HANDLE_VALUE) return false;
    return WaitForSingleObject(m_hProcess, 0) == WAIT_TIMEOUT;
}

int RarProcess::ParsePercent(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && line[i] == ' ') ++i;
    if (i >= line.size() || !isdigit((unsigned char)line[i])) return -1;
    int pct = 0;
    while (i < line.size() && isdigit((unsigned char)line[i]))
        pct = pct * 10 + (line[i++] - '0');
    if (i < line.size() && line[i] == '%') return pct > 100 ? 100 : pct;
    return -1;
}

DWORD WINAPI RarProcess::StdoutReaderThread(LPVOID param) {
    auto* ctx = static_cast<ReaderCtx*>(param);

    char   buf[4096];
    DWORD  read = 0;
    std::string line;

    while (ReadFile(ctx->hPipe, buf, sizeof(buf), &read, nullptr) && read > 0) {
        if (*ctx->pCancel) break;
        for (DWORD i = 0; i < read; ++i) {
            if (buf[i] == '\n' || buf[i] == '\r') {
                if (!line.empty()) {
                    int pct = ParsePercent(line);
                    if (pct >= 0) {
                        size_t space = line.find('%');
                        std::wstring fname;
                        if (space != std::string::npos) {
                            std::string rest = line.substr(space + 1);
                            while (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
                            int wlen = MultiByteToWideChar(CP_ACP, 0, rest.c_str(), -1, nullptr, 0);
                            if (wlen > 1) {
                                fname.resize(wlen - 1);
                                MultiByteToWideChar(CP_ACP, 0, rest.c_str(), -1, fname.data(), wlen);
                            }
                        }
                        wchar_t* copy = _wcsdup(fname.c_str());
                        PostMessageW(ctx->hwnd, ctx->progressMsg, (WPARAM)pct, (LPARAM)copy);
                    }
                    line.clear();
                }
            } else {
                line += buf[i];
            }
        }
    }

    CloseHandle(ctx->hPipe);

    // Wait for rar.exe to exit, convert ExitCode to HRESULT, and notify
    // (pipe EOF arrives after the child exits, but wait anyway to be safe)
    HRESULT hr = S_OK;
    if (ctx->hProcess != INVALID_HANDLE_VALUE) {
        WaitForSingleObject(ctx->hProcess, INFINITE);
        DWORD exitCode = 0;
        if (GetExitCodeProcess(ctx->hProcess, &exitCode) && exitCode != 0)
            hr = E_FAIL;
        CloseHandle(ctx->hProcess);
    }
    PostMessageW(ctx->hwnd, ctx->doneMsg, (WPARAM)hr, 0);
    delete ctx;
    return 0;
}

// ---- Delete (rar d) ----
// Set archive-wide comment via `rar.exe c -z<tempfile> archive`.
// Comment written to temp file in OEM code page (rar.exe's default comment encoding).
// Avoid -sc<charset>c flag (version-dependent, low compatibility).
// Pass empty comment file to delete existing comment.
bool RarProcess::SetComment(const wchar_t* archivePath,
                            const std::wstring& comment,
                            const wchar_t* rarExePathOverride,
                            HWND hwndNotify,
                            UINT doneMsg) {
    std::wstring rarExe;
    if (rarExePathOverride && rarExePathOverride[0])
        rarExe = rarExePathOverride;
    else
        rarExe = FindRarExe();

    if (rarExe.empty()) {
        MessageBoxW(hwndNotify,
                    I18n::Tr(IDS_ERR_RAR_NOT_FOUND).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
        return false;
    }

    // Write comment to temp file in UTF-8 (no BOM)
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    wchar_t tempFile[MAX_PATH];
    swprintf_s(tempFile, L"%saileex_cmt_%llu.txt", tempDir,
               (unsigned long long)GetTickCount64());

    HANDLE hCmt = CreateFileW(tempFile, GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hCmt == INVALID_HANDLE_VALUE) return false;
    if (!comment.empty()) {
        int len = WideCharToMultiByte(CP_OEMCP, 0,
                                       comment.c_str(), (int)comment.size(),
                                       nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            std::vector<char> oem(len);
            WideCharToMultiByte(CP_OEMCP, 0,
                                comment.c_str(), (int)comment.size(),
                                oem.data(), len, nullptr, nullptr);
            DWORD written = 0;
            WriteFile(hCmt, oem.data(), (DWORD)oem.size(), &written, nullptr);
        }
    }
    CloseHandle(hCmt);

    // rar.exe c -z<file> archive (charset specification omitted, defer to rar.exe's default OEM interpretation)
    std::wstring cmd = L"\"" + rarExe + L"\" c -z\"" + tempFile + L"\" \"" +
                       archivePath + L"\"";

    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    m_cancelFlag = false;
    PROCESS_INFORMATION pi = {};
    STARTUPINFOW si = {};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    DWORD flags = IsWinRarGui(rarExe) ? 0u : (DWORD)CREATE_NO_WINDOW;

    BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr,
                             FALSE, flags, nullptr, nullptr, &si, &pi);
    if (!ok) {
        DeleteFileW(tempFile);
        return false;
    }
    m_hProcess = pi.hProcess;
    CloseHandle(pi.hThread);

    // Exit-monitoring thread: clean temp comment file and post doneMsg when done.
    struct CtxWithTemp {
        HANDLE       hProcess;
        HWND         hwnd;
        UINT         doneMsg;
        volatile bool* pCancel;
        wchar_t      tempPath[MAX_PATH];
    };
    auto* ctx = new CtxWithTemp{};
    DuplicateHandle(GetCurrentProcess(), m_hProcess,
                    GetCurrentProcess(), &ctx->hProcess,
                    0, FALSE, DUPLICATE_SAME_ACCESS);
    ctx->hwnd      = hwndNotify;
    ctx->doneMsg   = doneMsg;
    ctx->pCancel   = &m_cancelFlag;
    wcscpy_s(ctx->tempPath, tempFile);

    m_hReader = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
        auto* c = static_cast<CtxWithTemp*>(p);
        WaitForSingleObject(c->hProcess, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess(c->hProcess, &exitCode);
        CloseHandle(c->hProcess);
        DeleteFileW(c->tempPath);
        HRESULT hr = (exitCode == 0) ? S_OK : E_FAIL;
        PostMessageW(c->hwnd, c->doneMsg, (WPARAM)hr, 0);
        delete c;
        return 0;
    }, ctx, 0, nullptr);
    return true;
}

// `d` command has minimal progress output, so skip stdout parsing and wait-for-exit only.
// `-y` skips confirmation prompts; `-r` recursively deletes folder contents.
bool RarProcess::Delete(const wchar_t* archivePath,
                         const std::vector<std::wstring>& itemPaths,
                         const wchar_t* rarExePathOverride,
                         HWND hwndNotify,
                         UINT doneMsg) {
    std::wstring rarExe;
    if (rarExePathOverride && rarExePathOverride[0])
        rarExe = rarExePathOverride;
    else
        rarExe = FindRarExe();

    if (rarExe.empty()) {
        MessageBoxW(hwndNotify,
                    I18n::Tr(IDS_ERR_RAR_NOT_FOUND).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
        return false;
    }

    std::wstring cmd = L"\"" + rarExe + L"\" d -y -r \"" + archivePath + L"\"";
    for (const auto& p : itemPaths)
        cmd += std::wstring(L" \"") + p + L"\"";

    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    m_cancelFlag = false;
    PROCESS_INFORMATION pi = {};

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    DWORD flags = IsWinRarGui(rarExe) ? 0u : (DWORD)CREATE_NO_WINDOW;

    BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr,
                             FALSE, flags, nullptr, nullptr, &si, &pi);
    if (!ok) return false;

    m_hProcess = pi.hProcess;
    CloseHandle(pi.hThread);

    auto* ctx = new WaiterCtx{m_hProcess, hwndNotify, doneMsg, &m_cancelFlag};
    DuplicateHandle(GetCurrentProcess(), m_hProcess,
                    GetCurrentProcess(), &ctx->hProcess,
                    0, FALSE, DUPLICATE_SAME_ACCESS);
    m_hReader = CreateThread(nullptr, 0, WinrarWaiterThread, ctx, 0, nullptr);
    return true;
}
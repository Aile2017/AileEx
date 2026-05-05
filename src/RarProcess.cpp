#include "RarProcess.h"
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
                    L"WinRAR.exe / Rar.exe が見つかりません。\n設定でパスを確認してください。",
                    L"AileEx", MB_ICONERROR);
        return false;
    }

    // Resolve method digit: accept "0"-"5"; default to "3" (Normal)
    wchar_t mChar = (method && method[0] >= L'0' && method[0] <= L'5') ? method[0] : L'3';
    wchar_t mBuf[2] = {mChar, L'\0'};

    std::wstring cmd = L"\"" + rarExe + L"\" a -ep1 -r -m" + mBuf;

    // パスワード: -hp (ヘッダ含む暗号化) または -p (データのみ)
    // パスワードはコマンドライン直結なので Windows 標準エスケープで引用符包み。
    // "-hp\"My Pass\"" → CommandLineToArgvW が "-hpMy Pass" 1 トークンとして解釈。
    if (password && password[0]) {
        std::wstring pw(password);
        // Windows コマンドライン用エスケープ: バックスラッシュは " 直前でのみ 2 倍化
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
        // 辞書サイズ
        if (!adv->dictSize.empty())
            cmd += L" -md" + adv->dictSize;
        // ソリッドアーカイブ
        cmd += adv->solid ? L" -s" : L" -ds";
        // スレッド数
        if (adv->threads > 0)
            cmd += L" -mt" + std::to_wstring(adv->threads);
        // リカバリレコード
        if (adv->recoveryPct > 0)
            cmd += L" -rr" + std::to_wstring(adv->recoveryPct) + L"p";
        // 分割ボリューム (コンソール版のみ有効)
        if (!adv->splitVolume.empty())
            cmd += L" -v" + adv->splitVolume;
        // 追加パラメーター
        if (!adv->extra.empty())
            cmd += L" " + adv->extra;
    }

    cmd += std::wstring(L" \"") + outPath + L"\"";
    for (auto& f : srcPaths) cmd += std::wstring(L" \"") + f + L"\"";

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
        // Duplicate handle so waiter thread can close it independently
        DuplicateHandle(GetCurrentProcess(), m_hProcess,
                        GetCurrentProcess(), &ctx->hProcess,
                        0, FALSE, DUPLICATE_SAME_ACCESS);
        m_hReader = CreateThread(nullptr, 0, WinrarWaiterThread, ctx, 0, nullptr);
    } else {
        // Console mode (rar.exe): read stdout for progress
        HANDLE hRead = nullptr, hWrite = nullptr;
        SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
        if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return false;
        SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

        // PROC_THREAD_ATTRIBUTE_HANDLE_LIST で継承ハンドルを hWrite だけに絞る
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
        auto* ctx = new ReaderCtx{hRead, hwndNotify, progressMsg, doneMsg, &m_cancelFlag};
        m_hReader = CreateThread(nullptr, 0, StdoutReaderThread, ctx, 0, nullptr);
    }
    return true;
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
    PostMessageW(ctx->hwnd, ctx->doneMsg, 0, 0);
    delete ctx;
    return 0;
}
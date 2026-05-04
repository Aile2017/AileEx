#include "RarProcess.h"
#include "resource.h"
#include <shlwapi.h>
#include <string>
#include <sstream>

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
    // Try 64-bit registry view then 32-bit
    for (REGSAM sam : {(REGSAM)(KEY_READ | KEY_WOW64_64KEY), (REGSAM)(KEY_READ | KEY_WOW64_32KEY)}) {
        if (RegOpenKeyExW(hRoot, L"SOFTWARE\\WinRAR", 0, sam, &hKey) == ERROR_SUCCESS) {
            wchar_t buf[MAX_PATH] = {};
            DWORD   sz   = sizeof(buf);
            DWORD   type = 0;
            if (RegQueryValueExW(hKey, L"exe32", nullptr, &type,
                                 (BYTE*)buf, &sz) == ERROR_SUCCESS && type == REG_SZ) {
                RegCloseKey(hKey);
                return buf;
            }
            RegCloseKey(hKey);
        }
    }
    return {};
}

std::wstring RarProcess::FindRarExe() {
    std::wstring path = QueryRegistryRarPath(HKEY_LOCAL_MACHINE);
    if (!path.empty()) return path;
    path = QueryRegistryRarPath(HKEY_CURRENT_USER);
    if (!path.empty()) return path;

    // Fallback: check common install locations
    for (const wchar_t* env : {L"ProgramFiles", L"ProgramFiles(x86)"}) {
        wchar_t pf[MAX_PATH] = {};
        if (GetEnvironmentVariableW(env, pf, MAX_PATH)) {
            std::wstring candidate = std::wstring(pf) + L"\\WinRAR\\rar.exe";
            if (PathFileExistsW(candidate.c_str())) return candidate;
        }
    }
    return {};
}

bool RarProcess::Compress(const std::vector<std::wstring>& srcPaths,
                           const wchar_t* outPath,
                           const wchar_t* method, HWND hwndNotify,
                           UINT progressMsg, UINT doneMsg) {
    std::wstring rarExe = FindRarExe();
    if (rarExe.empty()) {
        MessageBoxW(hwndNotify,
                    L"rar.exe / WinRAR が見つかりません。\n設定でパスを確認してください。",
                    L"AileEx", MB_ICONERROR);
        return false;
    }

    // Resolve method digit: accept "0"-"5"; default to "3" (Normal)
    wchar_t mChar = (method && method[0] >= L'0' && method[0] <= L'5') ? method[0] : L'3';
    wchar_t mBuf[2] = {mChar, L'\0'};

    std::wstring cmd = L"\"" + rarExe + L"\" a -ep1 -r -m" + mBuf + L" \"" + outPath + L"\"";
    for (auto& f : srcPaths) cmd += L" \"" + f + L"\"";

    // Create pipe for stdout
    HANDLE hRead = nullptr, hWrite = nullptr;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return false;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput  = hWrite;
    si.hStdError   = hWrite;

    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr,
                             TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);

    if (!ok) { CloseHandle(hRead); return false; }

    m_hProcess   = pi.hProcess;
    m_cancelFlag = false;
    CloseHandle(pi.hThread);

    auto* ctx = new ReaderCtx{hRead, hwndNotify, progressMsg, doneMsg, &m_cancelFlag};
    m_hReader = CreateThread(nullptr, 0, StdoutReaderThread, ctx, 0, nullptr);
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
    // WinRAR stdout: " 50%  filename.ext"
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
                        // Post progress; filename is the rest of the line after "XX%  "
                        size_t space = line.find('%');
                        std::wstring fname;
                        if (space != std::string::npos) {
                            // skip "% " or "%  "
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

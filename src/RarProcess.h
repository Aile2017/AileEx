#pragma once
#include <windows.h>
#include <string>
#include <vector>

class RarProcess {
public:
    ~RarProcess();

    // Auto-detect rar.exe path from registry (HKLM then HKCU fallback).
    // Returns empty string if not found.
    static std::wstring FindRarExe();

    // Spawn rar.exe to compress files. Posts WM_APP_PROGRESS / WM_APP_DONE to hwndNotify.
    // method: "0"=Store .. "5"=Best (maps to rar.exe -m0..-m5). nullptr/empty → "3" (Normal).
    bool Compress(const std::vector<std::wstring>& srcPaths,
                  const wchar_t* outPath,
                  const wchar_t* method,
                  HWND hwndNotify,
                  UINT progressMsg,
                  UINT doneMsg);

    void Cancel();
    bool IsRunning() const;

private:
    static std::wstring QueryRegistryRarPath(HKEY hRoot);

    struct ReaderCtx {
        HANDLE hPipe;
        HWND   hwnd;
        UINT   progressMsg;
        UINT   doneMsg;
        volatile bool* pCancel;
    };
    static DWORD WINAPI StdoutReaderThread(LPVOID param);
    static int ParsePercent(const std::string& line);

    HANDLE        m_hProcess   = INVALID_HANDLE_VALUE;
    HANDLE        m_hReader    = INVALID_HANDLE_VALUE;
    volatile bool m_cancelFlag = false;
};

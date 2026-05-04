#pragma once
#include <windows.h>
#include <string>
#include <vector>

class RarProcess {
public:
    ~RarProcess();

    // Auto-detect WinRAR.exe or Rar.exe from registry / known paths.
    // Prefers WinRAR.exe; falls back to Rar.exe in the same directory.
    static std::wstring FindRarExe();

    // Spawn the RAR executable to compress files.
    // rarExePathOverride: if non-empty, use this path instead of auto-detect.
    // method: "0"=Store .. "5"=Best (maps to -m0..-m5). nullptr/empty → "3" (Normal).
    // - WinRAR.exe (GUI): shows its own progress window; no stdout parsing.
    // - Rar.exe (console): posts WM_APP_PROGRESS / WM_APP_DONE to hwndNotify.
    bool Compress(const std::vector<std::wstring>& srcPaths,
                  const wchar_t* outPath,
                  const wchar_t* method,
                  const wchar_t* rarExePathOverride,
                  HWND hwndNotify,
                  UINT progressMsg,
                  UINT doneMsg);

    void Cancel();
    bool IsRunning() const;

private:
    // Returns the WinRAR install directory from registry (empty if not found).
    static std::wstring QueryRegistryRarPath(HKEY hRoot);

    struct ReaderCtx {
        HANDLE hPipe;
        HWND   hwnd;
        UINT   progressMsg;
        UINT   doneMsg;
        volatile bool* pCancel;
    };

    struct WaiterCtx {
        HANDLE hProcess;
        HWND   hwnd;
        UINT   doneMsg;
        volatile bool* pCancel;
    };

    static DWORD WINAPI StdoutReaderThread(LPVOID param);
    static DWORD WINAPI WinrarWaiterThread(LPVOID param);
    static int ParsePercent(const std::string& line);

    HANDLE        m_hProcess   = INVALID_HANDLE_VALUE;
    HANDLE        m_hReader    = INVALID_HANDLE_VALUE;
    volatile bool m_cancelFlag = false;
};

#pragma once
#include <windows.h>
#include <string>
#include <vector>

// RAR 専用詳細圧縮オプション。RarProcess::Compress() の末尾引数として渡す。
struct RarAdvancedParams {
    std::wstring dictSize;    // "" = auto; "128k","1m","1024m" → -md<n>
    bool         solid       = true;   // ソリッドアーカイブ: true=-s / false=-ds
    int          threads     = 0;      // 0 = auto; n>0 → -mt<n>
    int          recoveryPct = 0;      // 0 = none; n>0 → -rr<n>p
    std::wstring splitVolume; // "" = none; "10m","700m" → -v<size>
    std::wstring extra;       // free-form params (末尾に追記)
};

class RarProcess {
public:
    ~RarProcess();

    // Auto-detect WinRAR.exe or Rar.exe from registry / known paths.
    // Prefers WinRAR.exe; falls back to Rar.exe in the same directory.
    static std::wstring FindRarExe();

    // Spawn the RAR executable to compress files.
    // rarExePathOverride: if non-empty, use this path instead of auto-detect.
    // method: "0"=Store .. "5"=Best (maps to -m0..-m5). nullptr/empty → "3" (Normal).
    // password/encryptHeaders: nullptr/false = no encryption; encryptHeaders=true → -hp (ヘッダ含む暗号化)
    // adv: optional advanced params (nullptr = use defaults).
    // - WinRAR.exe (GUI): shows its own progress window; no stdout parsing.
    // - Rar.exe (console): posts WM_APP_PROGRESS / WM_APP_DONE to hwndNotify.
    bool Compress(const std::vector<std::wstring>& srcPaths,
                  const wchar_t* outPath,
                  const wchar_t* method,
                  const wchar_t* rarExePathOverride,
                  const wchar_t* password,
                  bool encryptHeaders,
                  HWND hwndNotify,
                  UINT progressMsg,
                  UINT doneMsg,
                  const RarAdvancedParams* adv = nullptr);

    void Cancel();
    bool IsRunning() const;

private:
    // Returns the WinRAR install directory from registry (empty if not found).
    static std::wstring QueryRegistryRarPath(HKEY hRoot);

    struct ReaderCtx {
        HANDLE hPipe;
        HANDLE hProcess;   // プロセス終了待ちと ExitCode 取得用（スレッドが CloseHandle する）
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

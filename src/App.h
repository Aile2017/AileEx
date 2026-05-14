#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include "Settings.h"
#include "SevenZip.h"
#include "UnrarDll.h"

class App {
public:
    static App& Instance();

    bool Init(HINSTANCE hInst);
    void Shutdown();

    HINSTANCE GetInstance() const { return m_hInst; }
    Settings& GetSettings()       { return m_settings; }
    SevenZip& Get7z()             { return m_sevenZip; }
    UnrarDll& GetUnrar()          { return m_unrar; }

    // Reload DLLs after settings change.
    void ReloadDlls();

    // Called from WinMain after arg parsing.
    int RunBrowseMode(const std::vector<std::wstring>& archivePaths, int nCmdShow);
    int RunCompressMode(const std::vector<std::wstring>& filePaths, int nCmdShow,
                        const std::wstring& destDir = L"");
    int RunExtractDialogMode(const std::wstring& archivePath, int nCmdShow,
                             const std::wstring& destDir = L"");
    int RunEmpty(int nCmdShow);

private:
    App() = default;
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    HINSTANCE m_hInst   = nullptr;
    Settings  m_settings;
    SevenZip  m_sevenZip;
    UnrarDll  m_unrar;
};

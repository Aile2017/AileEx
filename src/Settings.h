#pragma once
#include <windows.h>
#include <string>
#include <vector>

class Settings {
public:
    static constexpr size_t kMaxMru = 10;

    void Load();
    void Save() const;

    const std::wstring& GetRarExtractor() const    { return m_rarExtractor; }
    void SetRarExtractor(const wchar_t* v)          { m_rarExtractor = v; }

    const std::wstring& GetRarExePath() const       { return m_rarExePath; }
    void SetRarExePath(const wchar_t* v)            { m_rarExePath = v; }

    const std::wstring& GetDefaultOutputDir() const { return m_defaultOutputDir; }
    void SetDefaultOutputDir(const wchar_t* v)      { m_defaultOutputDir = v; }

    const std::wstring& GetDefaultFormat() const    { return m_defaultFormat; }
    void SetDefaultFormat(const wchar_t* v)         { m_defaultFormat = v; }

    int  GetCompressionLevel() const                { return m_compressionLevel; }
    void SetCompressionLevel(int v)                 { m_compressionLevel = v; }

    int  GetRarLevel() const                        { return m_rarLevel; }
    void SetRarLevel(int v)                         { m_rarLevel = v; }

    // Advanced compress options (last-used values)
    const std::wstring& GetAdvDictSize() const      { return m_advDictSize; }
    const std::wstring& GetAdvWordSize() const      { return m_advWordSize; }
    const std::wstring& GetAdvSolidBlock() const    { return m_advSolidBlock; }
    const std::wstring& GetAdvThreads() const       { return m_advThreads; }
    const std::wstring& GetAdvExtra() const         { return m_advExtra; }
    void SetAdvDictSize(const wchar_t* v)           { m_advDictSize   = v; }
    void SetAdvWordSize(const wchar_t* v)           { m_advWordSize   = v; }
    void SetAdvSolidBlock(const wchar_t* v)         { m_advSolidBlock = v; }
    void SetAdvThreads(const wchar_t* v)            { m_advThreads    = v; }
    void SetAdvExtra(const wchar_t* v)              { m_advExtra      = v; }

    // RAR advanced options (last-used values)
    const std::wstring& GetRarAdvDictSize() const   { return m_rarAdvDictSize; }
    bool                GetRarAdvSolid() const      { return m_rarAdvSolid; }
    int                 GetRarAdvThreads() const    { return m_rarAdvThreads; }
    int                 GetRarAdvRecovery() const   { return m_rarAdvRecovery; }
    const std::wstring& GetRarAdvVolume() const     { return m_rarAdvVolume; }
    const std::wstring& GetRarAdvExtra() const      { return m_rarAdvExtra; }
    void SetRarAdvDictSize(const wchar_t* v)        { m_rarAdvDictSize  = v; }
    void SetRarAdvSolid(bool v)                     { m_rarAdvSolid     = v; }
    void SetRarAdvThreads(int v)                    { m_rarAdvThreads   = v; }
    void SetRarAdvRecovery(int v)                   { m_rarAdvRecovery  = v; }
    void SetRarAdvVolume(const wchar_t* v)          { m_rarAdvVolume    = v; }
    void SetRarAdvExtra(const wchar_t* v)           { m_rarAdvExtra     = v; }

    // Window placement
    int  GetWindowX() const          { return m_windowX; }
    int  GetWindowY() const          { return m_windowY; }
    int  GetWindowW() const          { return m_windowW; }
    int  GetWindowH() const          { return m_windowH; }
    bool GetWindowMaximized() const  { return m_windowMaximized; }
    int  GetSplitterPos() const      { return m_splitterPos; }
    bool GetTreeVisible() const      { return m_treeVisible; }
    bool GetToolbarVisible() const   { return m_toolbarVisible; }
    void SetWindowPlacement(int x, int y, int w, int h, bool maximized) {
        m_windowX = x; m_windowY = y; m_windowW = w; m_windowH = h;
        m_windowMaximized = maximized;
    }
    void SetSplitterPos(int v)       { m_splitterPos = v; }
    void SetTreeVisible(bool v)      { m_treeVisible = v; }
    void SetToolbarVisible(bool v)   { m_toolbarVisible = v; }

    const std::wstring& Get7zDllPath() const        { return m_7zDllPath; }
    void Set7zDllPath(const wchar_t* v)             { m_7zDllPath = v; }

    const std::wstring& GetUnrarDllPath() const     { return m_unrarDllPath; }
    void SetUnrarDllPath(const wchar_t* v)          { m_unrarDllPath = v; }

    // MRU (最近使ったアーカイブ) — 先頭が最新。重複は大文字小文字無視で除去。
    const std::vector<std::wstring>& GetMruPaths() const { return m_mruPaths; }
    void AddMru(const std::wstring& path);
    void RemoveMru(const std::wstring& path);

private:
    mutable wchar_t m_iniPath[MAX_PATH] = {};

    std::wstring m_rarExtractor    = L"7z";
    std::wstring m_rarExePath;
    std::wstring m_defaultOutputDir;
    std::wstring m_defaultFormat   = L"7z";
    int          m_compressionLevel = 5;
    int          m_rarLevel         = 3;
    std::wstring m_advDictSize;
    std::wstring m_advWordSize;
    std::wstring m_advSolidBlock;
    std::wstring m_advThreads;
    std::wstring m_advExtra;
    // RAR advanced
    std::wstring m_rarAdvDictSize;
    bool         m_rarAdvSolid    = true;
    int          m_rarAdvThreads  = 0;
    int          m_rarAdvRecovery = 0;
    std::wstring m_rarAdvVolume;
    std::wstring m_rarAdvExtra;
    int          m_windowX          = -1;   // -1 = use CW_USEDEFAULT
    int          m_windowY          = -1;
    int          m_windowW          = 900;
    int          m_windowH          = 600;
    bool         m_windowMaximized  = false;
    int          m_splitterPos      = 220;
    bool         m_treeVisible      = true;
    bool         m_toolbarVisible   = true;
    std::wstring m_7zDllPath;
    std::wstring m_unrarDllPath;
    std::vector<std::wstring> m_mruPaths;

    std::wstring ReadStr(const wchar_t* section, const wchar_t* key, const wchar_t* def) const;
    void         WriteStr(const wchar_t* section, const wchar_t* key, const wchar_t* val) const;
    void         BuildIniPath() const;
};

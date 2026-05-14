#include "Settings.h"
#include <shlwapi.h>
#include <algorithm>

void Settings::BuildIniPath() const {
    GetModuleFileNameW(nullptr, m_iniPath, MAX_PATH);
    PathRenameExtensionW(m_iniPath, L".ini");
}

void Settings::Load() {
    BuildIniPath();
    m_rarExtractor     = ReadStr(L"General", L"RarExtractor",     L"7z");
    m_rarExePath       = ReadStr(L"General", L"RarExePath",       L"");
    m_defaultOutputDir = ReadStr(L"General", L"DefaultOutputDir", L"");
    m_defaultFormat    = ReadStr(L"General", L"DefaultFormat",    L"7z");
    m_7zDllPath        = ReadStr(L"General", L"7zDllPath",        L"");
    m_unrarDllPath     = ReadStr(L"General", L"UnrarDllPath",     L"");
    m_defaultSfxMode   = ReadStr(L"General", L"DefaultSfxMode",   L"");
    m_fontName         = ReadStr(L"General", L"FontName",         L"Segoe UI");

    wchar_t buf[16] = {};
    GetPrivateProfileStringW(L"General", L"CompressionLevel", L"5", buf, 16, m_iniPath);
    m_compressionLevel = _wtoi(buf);
    if (m_compressionLevel < 0 || m_compressionLevel > 9) m_compressionLevel = 5;

    GetPrivateProfileStringW(L"General", L"RarLevel", L"3", buf, 16, m_iniPath);
    m_rarLevel = _wtoi(buf);
    if (m_rarLevel < 0 || m_rarLevel > 5) m_rarLevel = 3;

    GetPrivateProfileStringW(L"General", L"MkDir", L"2", buf, 16, m_iniPath);
    m_mkDir = _wtoi(buf);
    if (m_mkDir < 0 || m_mkDir > 3) m_mkDir = 2;

    // Advanced compress options
    m_advDictSize   = ReadStr(L"AdvancedCompress", L"DictSize",   L"");
    m_advWordSize   = ReadStr(L"AdvancedCompress", L"WordSize",   L"");
    m_advSolidBlock = ReadStr(L"AdvancedCompress", L"SolidBlock", L"");
    m_advThreads    = ReadStr(L"AdvancedCompress", L"Threads",    L"");
    m_advExtra      = ReadStr(L"AdvancedCompress", L"Extra",      L"");
    m_advVolume     = ReadStr(L"AdvancedCompress", L"Volume",     L"");

    // RAR advanced compress options
    m_rarAdvDictSize  = ReadStr(L"RarAdvanced", L"DictSize",  L"");
    m_rarAdvVolume    = ReadStr(L"RarAdvanced", L"Volume",    L"");
    m_rarAdvExtra     = ReadStr(L"RarAdvanced", L"Extra",     L"");
    GetPrivateProfileStringW(L"RarAdvanced", L"Solid",    L"1", buf, 16, m_iniPath); m_rarAdvSolid    = _wtoi(buf) != 0;
    GetPrivateProfileStringW(L"RarAdvanced", L"Threads",  L"0", buf, 16, m_iniPath); m_rarAdvThreads  = _wtoi(buf);
    GetPrivateProfileStringW(L"RarAdvanced", L"Recovery", L"0", buf, 16, m_iniPath); m_rarAdvRecovery = _wtoi(buf);

    // Window placement
    GetPrivateProfileStringW(L"Window", L"X",         L"-1",    buf, 16, m_iniPath); m_windowX        = _wtoi(buf);
    GetPrivateProfileStringW(L"Window", L"Y",         L"-1",    buf, 16, m_iniPath); m_windowY        = _wtoi(buf);
    GetPrivateProfileStringW(L"Window", L"W",         L"900",   buf, 16, m_iniPath); m_windowW        = _wtoi(buf);
    GetPrivateProfileStringW(L"Window", L"H",         L"600",   buf, 16, m_iniPath); m_windowH        = _wtoi(buf);
    GetPrivateProfileStringW(L"Window", L"Maximized", L"0",     buf, 16, m_iniPath); m_windowMaximized = _wtoi(buf) != 0;
    GetPrivateProfileStringW(L"Window", L"Splitter",  L"220",   buf, 16, m_iniPath); m_splitterPos    = _wtoi(buf);
    GetPrivateProfileStringW(L"Window", L"TreeVisible", L"1",   buf, 16, m_iniPath); m_treeVisible    = _wtoi(buf) != 0;
    GetPrivateProfileStringW(L"Window", L"ToolbarVisible", L"1", buf, 16, m_iniPath); m_toolbarVisible = _wtoi(buf) != 0;
    if (m_windowW < 400) m_windowW = 400;
    if (m_windowH < 300) m_windowH = 300;
    if (m_splitterPos < 80) m_splitterPos = 80;

    // MRU — Path0 is the most recent. Stop as soon as an empty entry is encountered.
    m_mruPaths.clear();
    for (size_t i = 0; i < kMaxMru; ++i) {
        wchar_t key[16];
        swprintf_s(key, L"Path%zu", i);
        std::wstring v = ReadStr(L"Mru", key, L"");
        if (v.empty()) break;
        m_mruPaths.push_back(std::move(v));
    }
}

void Settings::Save() const {
    // Guard against writing to an empty path if Save() is called before Load()
    if (!m_iniPath[0]) BuildIniPath();
    WriteStr(L"General", L"RarExtractor",     m_rarExtractor.c_str());
    WriteStr(L"General", L"RarExePath",       m_rarExePath.c_str());
    WriteStr(L"General", L"DefaultOutputDir", m_defaultOutputDir.c_str());
    WriteStr(L"General", L"DefaultFormat",    m_defaultFormat.c_str());
    WriteStr(L"General", L"7zDllPath",        m_7zDllPath.c_str());
    WriteStr(L"General", L"UnrarDllPath",     m_unrarDllPath.c_str());
    WriteStr(L"General", L"DefaultSfxMode",   m_defaultSfxMode.c_str());
    WriteStr(L"General", L"FontName",         m_fontName.c_str());

    wchar_t buf[16] = {};
    _itow_s(m_compressionLevel, buf, 10);
    WriteStr(L"General", L"CompressionLevel", buf);

    _itow_s(m_rarLevel, buf, 10);
    WriteStr(L"General", L"RarLevel", buf);

    _itow_s(m_mkDir, buf, 10);
    WriteStr(L"General", L"MkDir", buf);

    // Advanced compress options
    WriteStr(L"AdvancedCompress", L"DictSize",   m_advDictSize.c_str());
    WriteStr(L"AdvancedCompress", L"WordSize",   m_advWordSize.c_str());
    WriteStr(L"AdvancedCompress", L"SolidBlock", m_advSolidBlock.c_str());
    WriteStr(L"AdvancedCompress", L"Threads",    m_advThreads.c_str());
    WriteStr(L"AdvancedCompress", L"Extra",      m_advExtra.c_str());
    WriteStr(L"AdvancedCompress", L"Volume",     m_advVolume.c_str());

    // RAR advanced compress options
    WriteStr(L"RarAdvanced", L"DictSize",  m_rarAdvDictSize.c_str());
    WriteStr(L"RarAdvanced", L"Volume",    m_rarAdvVolume.c_str());
    WriteStr(L"RarAdvanced", L"Extra",     m_rarAdvExtra.c_str());
    _itow_s(m_rarAdvSolid    ? 1 : 0, buf, 10); WriteStr(L"RarAdvanced", L"Solid",    buf);
    _itow_s(m_rarAdvThreads,           buf, 10); WriteStr(L"RarAdvanced", L"Threads",  buf);
    _itow_s(m_rarAdvRecovery,          buf, 10); WriteStr(L"RarAdvanced", L"Recovery", buf);

    // Window placement
    _itow_s(m_windowX,  buf, 10); WriteStr(L"Window", L"X",         buf);
    _itow_s(m_windowY,  buf, 10); WriteStr(L"Window", L"Y",         buf);
    _itow_s(m_windowW,  buf, 10); WriteStr(L"Window", L"W",         buf);
    _itow_s(m_windowH,  buf, 10); WriteStr(L"Window", L"H",         buf);
    WriteStr(L"Window", L"Maximized", m_windowMaximized ? L"1" : L"0");
    _itow_s(m_splitterPos, buf, 10); WriteStr(L"Window", L"Splitter", buf);
    WriteStr(L"Window", L"TreeVisible", m_treeVisible ? L"1" : L"0");
    WriteStr(L"Window", L"ToolbarVisible", m_toolbarVisible ? L"1" : L"0");

    // MRU — pass nullptr to WritePrivateProfileStringW to delete obsolete keys from the ini.
    for (size_t i = 0; i < kMaxMru; ++i) {
        wchar_t key[16];
        swprintf_s(key, L"Path%zu", i);
        const wchar_t* val = (i < m_mruPaths.size()) ? m_mruPaths[i].c_str() : nullptr;
        WritePrivateProfileStringW(L"Mru", key, val, m_iniPath);
    }
}

void Settings::AddMru(const std::wstring& path) {
    auto eq = [&](const std::wstring& s) {
        return _wcsicmp(s.c_str(), path.c_str()) == 0;
    };
    auto it = std::find_if(m_mruPaths.begin(), m_mruPaths.end(), eq);
    if (it != m_mruPaths.end()) m_mruPaths.erase(it);
    m_mruPaths.insert(m_mruPaths.begin(), path);
    if (m_mruPaths.size() > kMaxMru) m_mruPaths.resize(kMaxMru);
}

void Settings::RemoveMru(const std::wstring& path) {
    auto eq = [&](const std::wstring& s) {
        return _wcsicmp(s.c_str(), path.c_str()) == 0;
    };
    auto it = std::find_if(m_mruPaths.begin(), m_mruPaths.end(), eq);
    if (it != m_mruPaths.end()) m_mruPaths.erase(it);
}

std::wstring Settings::ReadStr(const wchar_t* section, const wchar_t* key, const wchar_t* def) const {
    wchar_t buf[MAX_PATH * 2] = {};
    GetPrivateProfileStringW(section, key, def, buf, MAX_PATH * 2, m_iniPath);
    return buf;
}

void Settings::WriteStr(const wchar_t* section, const wchar_t* key, const wchar_t* val) const {
    WritePrivateProfileStringW(section, key, val, m_iniPath);
}

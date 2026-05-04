#pragma once
#include <windows.h>
#include <string>

class Settings {
public:
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

    const std::wstring& Get7zDllPath() const        { return m_7zDllPath; }
    void Set7zDllPath(const wchar_t* v)             { m_7zDllPath = v; }

    const std::wstring& GetUnrarDllPath() const     { return m_unrarDllPath; }
    void SetUnrarDllPath(const wchar_t* v)          { m_unrarDllPath = v; }

private:
    wchar_t m_iniPath[MAX_PATH] = {};

    std::wstring m_rarExtractor    = L"7z";
    std::wstring m_rarExePath;
    std::wstring m_defaultOutputDir;
    std::wstring m_defaultFormat   = L"7z";
    int          m_compressionLevel = 5;
    std::wstring m_7zDllPath;
    std::wstring m_unrarDllPath;

    std::wstring ReadStr(const wchar_t* section, const wchar_t* key, const wchar_t* def) const;
    void         WriteStr(const wchar_t* section, const wchar_t* key, const wchar_t* val) const;
    void         BuildIniPath();
};

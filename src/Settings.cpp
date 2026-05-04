#include "Settings.h"
#include <shlwapi.h>

void Settings::BuildIniPath() {
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

    wchar_t buf[16] = {};
    GetPrivateProfileStringW(L"General", L"CompressionLevel", L"5", buf, 16, m_iniPath);
    m_compressionLevel = _wtoi(buf);
    if (m_compressionLevel < 0 || m_compressionLevel > 9) m_compressionLevel = 5;
}

void Settings::Save() const {
    WriteStr(L"General", L"RarExtractor",     m_rarExtractor.c_str());
    WriteStr(L"General", L"RarExePath",       m_rarExePath.c_str());
    WriteStr(L"General", L"DefaultOutputDir", m_defaultOutputDir.c_str());
    WriteStr(L"General", L"DefaultFormat",    m_defaultFormat.c_str());
    WriteStr(L"General", L"7zDllPath",        m_7zDllPath.c_str());
    WriteStr(L"General", L"UnrarDllPath",     m_unrarDllPath.c_str());

    wchar_t buf[16] = {};
    _itow_s(m_compressionLevel, buf, 10);
    WriteStr(L"General", L"CompressionLevel", buf);
}

std::wstring Settings::ReadStr(const wchar_t* section, const wchar_t* key, const wchar_t* def) const {
    wchar_t buf[MAX_PATH * 2] = {};
    GetPrivateProfileStringW(section, key, def, buf, MAX_PATH * 2, m_iniPath);
    return buf;
}

void Settings::WriteStr(const wchar_t* section, const wchar_t* key, const wchar_t* val) const {
    WritePrivateProfileStringW(section, key, val, m_iniPath);
}

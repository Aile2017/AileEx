#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "SevenZip.h"  // WritableFormat

class Settings;

class CompressDlg {
public:
    struct Params {
        std::vector<std::wstring> inputFiles;
        std::wstring outputPath;
        std::wstring format   = L"7z";   // "7z","zip","tar","gz","bz2","xz","rar"
        std::wstring method   = L"lzma";
        int          level    = 5;
        int          rarLevel = 3;       // RAR compression level 0-5 (-m0..-m5)
        std::wstring password;
        bool         encryptHeaders = false;
        // Advanced options (shown in the sub-dialog)
        std::wstring dictSize;    // "" = auto; "64k","1m","32m"
        std::wstring wordSize;    // "" = auto; "32","64","273"
        std::wstring solidBlock;  // "" = default; "off","1m" (7z only)
        std::wstring threads;     // "" = auto; "4","8"
        std::wstring extra;       // free-form "key=value" pairs
        std::wstring volumeSize;  // "" = no split; "100m","1g" 等 (7z/zip のみ)
        // RAR-specific advanced options
        std::wstring rarDictSize;    // "" = auto; "128k","1m","4g"
        bool         rarSolid       = true;
        int          rarThreads     = 0;
        int          rarRecoveryPct = 0;
        std::wstring rarSplitVolume;
        std::wstring rarExtra;

        // outputPath / inputFiles / password / encryptHeaders は対象外
        // （ユーザ操作毎に変わる値で、永続化対象ではない）
        void LoadFromSettings(const Settings& s);
        void SaveToSettings(Settings& s) const;
    };

    // Returns true if user clicked OK.
    // encoderNames: lowercased encoder names from SevenZip::GetEncoderNames().
    // writableFormats: writable formats from SevenZip::GetWritableFormats().
    //                 nullptr or empty = use static fallback list.
    bool Show(HWND hwndParent, Params& params,
              const std::vector<std::wstring>* encoderNames = nullptr,
              const std::vector<WritableFormat>* writableFormats = nullptr);

private:
    static INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
    INT_PTR HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void OnInit(HWND hwnd);
    void OnFormatChange(HWND hwnd);
    void OnBrowseOutput(HWND hwnd);
    void OnAdvanced(HWND hwnd);
    bool OnOK(HWND hwnd);

    HWND   m_hwnd = nullptr;
    Params m_params;
    const std::vector<std::wstring>* m_encoderNames = nullptr;  // not owned
    std::vector<WritableFormat>      m_writableFormats;          // owned copy（ポインタ安定性のため）
};

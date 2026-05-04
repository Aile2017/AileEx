#pragma once
#include <windows.h>
#include <string>
#include <vector>

class CompressDlg {
public:
    struct Params {
        std::vector<std::wstring> inputFiles;
        std::wstring outputPath;
        std::wstring format   = L"7z";   // "7z","zip","tar","gz","bz2","xz","rar"
        std::wstring method   = L"lzma";
        int          level    = 5;
        std::wstring password;
        bool         encryptHeaders = false;
    };

    // Returns true if user clicked OK.
    bool Show(HWND hwndParent, Params& params);

private:
    static INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
    INT_PTR HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void OnInit(HWND hwnd);
    void OnFormatChange(HWND hwnd);
    void OnBrowseOutput(HWND hwnd);
    bool OnOK(HWND hwnd);

    HWND   m_hwnd = nullptr;
    Params m_params;
};

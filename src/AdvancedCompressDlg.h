#pragma once
#include <windows.h>
#include <string>

// 詳細圧縮設定ダイアログ。
// 辞書サイズ・ワードサイズ・ソリッドブロック・スレッド数・追加パラメーターを編集する。
class AdvancedCompressDlg {
public:
    struct Params {
        std::wstring dictSize;    // "" = auto; "64k","1m","32m","512m","1g"
        std::wstring wordSize;    // "" = auto; "8","32","64","273"
        std::wstring solidBlock;  // "" = default; "off","1m","4g" (7z only)
        std::wstring threads;     // "" = auto; "1","4","8"
        std::wstring extra;       // free-form "key=value" pairs
        std::wstring volumeSize;  // "" = no split; "10m","100m","1g" 等で分割サイズ
    };

    // format: 現在選択中の圧縮形式 ("7z","zip" など)
    bool Show(HWND hwndParent, const wchar_t* format, Params& params);

private:
    static INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
    INT_PTR HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void OnInit(HWND hwnd);
    bool OnOK(HWND hwnd);

    HWND         m_hwnd   = nullptr;
    Params       m_params;
    std::wstring m_format;
};

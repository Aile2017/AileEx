#pragma once
#include <windows.h>

class SettingsDlg {
public:
    void Show(HWND hwndParent);

private:
    static INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
    INT_PTR HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void OnInit(HWND hwnd);
    void OnBrowseDir(HWND hwnd);
    bool OnOK(HWND hwnd);

    HWND m_hwnd = nullptr;
};

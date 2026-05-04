#include "SettingsDlg.h"
#include "App.h"
#include "resource.h"
#include <shlobj.h>
#include <commdlg.h>

void SettingsDlg::Show(HWND hwndParent) {
    DialogBoxParamW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_SETTINGS),
        hwndParent, DlgProc, (LPARAM)this);
}

INT_PTR CALLBACK SettingsDlg::DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    SettingsDlg* self = nullptr;
    if (msg == WM_INITDIALOG) {
        self = reinterpret_cast<SettingsDlg*>(lp);
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)self);
    } else {
        self = reinterpret_cast<SettingsDlg*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    }
    if (self) return self->HandleMsg(hwnd, msg, wp, lp);
    return FALSE;
}

INT_PTR SettingsDlg::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG:
        m_hwnd = hwnd;
        OnInit(hwnd);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BROWSE_DIR:
            OnBrowseDir(hwnd);
            break;
        case IDC_BROWSE_7Z: {
            wchar_t path[MAX_PATH] = {};
            GetDlgItemTextW(hwnd, IDC_7Z_DLL_PATH, path, MAX_PATH);
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hwnd;
            ofn.lpstrFilter = L"DLL ファイル\0*.dll\0";
            ofn.lpstrFile   = path;
            ofn.nMaxFile    = MAX_PATH;
            ofn.Flags       = OFN_FILEMUSTEXIST;
            ofn.lpstrTitle  = L"7z.dll を選択";
            if (GetOpenFileNameW(&ofn))
                SetDlgItemTextW(hwnd, IDC_7Z_DLL_PATH, path);
            break;
        }
        case IDC_BROWSE_UNRAR: {
            wchar_t path[MAX_PATH] = {};
            GetDlgItemTextW(hwnd, IDC_UNRAR_DLL_PATH, path, MAX_PATH);
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hwnd;
            ofn.lpstrFilter = L"DLL ファイル\0*.dll\0";
            ofn.lpstrFile   = path;
            ofn.nMaxFile    = MAX_PATH;
            ofn.Flags       = OFN_FILEMUSTEXIST;
            ofn.lpstrTitle  = L"unrar.dll を選択";
            if (GetOpenFileNameW(&ofn))
                SetDlgItemTextW(hwnd, IDC_UNRAR_DLL_PATH, path);
            break;
        }
        case IDOK:
            if (OnOK(hwnd)) EndDialog(hwnd, IDOK);
            break;
        case IDCANCEL:
            EndDialog(hwnd, IDCANCEL);
            break;
        }
        return TRUE;
    }
    return FALSE;
}

void SettingsDlg::OnInit(HWND hwnd) {
    Settings& s = App::Instance().GetSettings();

    // RAR extractor combo
    HWND hExt = GetDlgItem(hwnd, IDC_RAR_EXTRACTOR);
    SendMessageW(hExt, CB_ADDSTRING, 0, (LPARAM)L"7z.dll (7-Zip)");
    SendMessageW(hExt, CB_ADDSTRING, 0, (LPARAM)L"unrar.dll (UnRAR)");
    SendMessageW(hExt, CB_SETCURSEL,
                 (s.GetRarExtractor() == L"unrar") ? 1 : 0, 0);

    // Default output dir
    SetDlgItemTextW(hwnd, IDC_DEFAULT_DIR, s.GetDefaultOutputDir().c_str());

    // DLL paths
    SetDlgItemTextW(hwnd, IDC_7Z_DLL_PATH,    s.Get7zDllPath().c_str());
    SetDlgItemTextW(hwnd, IDC_UNRAR_DLL_PATH, s.GetUnrarDllPath().c_str());
}

void SettingsDlg::OnBrowseDir(HWND hwnd) {
    wchar_t path[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_DEFAULT_DIR, path, MAX_PATH);

    BROWSEINFOW bi = {};
    bi.hwndOwner  = hwnd;
    bi.lpszTitle  = L"デフォルト出力先を選択";
    bi.ulFlags    = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn       = nullptr;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        SHGetPathFromIDListW(pidl, path);
        CoTaskMemFree(pidl);
        SetDlgItemTextW(hwnd, IDC_DEFAULT_DIR, path);
    }
}

bool SettingsDlg::OnOK(HWND hwnd) {
    Settings& s = App::Instance().GetSettings();

    HWND hExt = GetDlgItem(hwnd, IDC_RAR_EXTRACTOR);
    int  sel  = (int)SendMessageW(hExt, CB_GETCURSEL, 0, 0);
    s.SetRarExtractor(sel == 1 ? L"unrar" : L"7z");

    wchar_t buf[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_DEFAULT_DIR,    buf, MAX_PATH); s.SetDefaultOutputDir(buf);
    GetDlgItemTextW(hwnd, IDC_7Z_DLL_PATH,    buf, MAX_PATH); s.Set7zDllPath(buf);
    GetDlgItemTextW(hwnd, IDC_UNRAR_DLL_PATH, buf, MAX_PATH); s.SetUnrarDllPath(buf);

    s.Save();
    App::Instance().ReloadDlls();
    return true;
}

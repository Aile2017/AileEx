#include "SettingsDlg.h"
#include "App.h"
#include "resource.h"
#include "SevenZip.h"
#include "UnrarDll.h"
#include "RarProcess.h"
#include <shlobj.h>
#include <shobjidl_core.h>
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
        case IDC_BROWSE_RAR: {
            wchar_t path[MAX_PATH] = {};
            GetDlgItemTextW(hwnd, IDC_RAR_EXE_PATH, path, MAX_PATH);
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hwnd;
            ofn.lpstrFilter = L"実行ファイル\0*.exe\0";
            ofn.lpstrFile   = path;
            ofn.nMaxFile    = MAX_PATH;
            ofn.Flags       = OFN_FILEMUSTEXIST;
            ofn.lpstrTitle  = L"WinRAR.exe / Rar.exe を選択";
            if (GetOpenFileNameW(&ofn))
                SetDlgItemTextW(hwnd, IDC_RAR_EXE_PATH, path);
            break;
        }
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

    // DLL / exe paths: show saved value, or auto-detect if empty
    auto resolve = [](const std::wstring& saved, const std::wstring& detected) {
        return saved.empty() ? detected : saved;
    };
    SetDlgItemTextW(hwnd, IDC_7Z_DLL_PATH,    resolve(s.Get7zDllPath(),    SevenZip::Find7zDll()).c_str());
    SetDlgItemTextW(hwnd, IDC_UNRAR_DLL_PATH, resolve(s.GetUnrarDllPath(), UnrarDll::FindUnrarDll()).c_str());
    SetDlgItemTextW(hwnd, IDC_RAR_EXE_PATH,   resolve(s.GetRarExePath(),   RarProcess::FindRarExe()).c_str());
}

void SettingsDlg::OnBrowseDir(HWND hwnd) {
    wchar_t path[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_DEFAULT_DIR, path, MAX_PATH);

    {
        IFileOpenDialog* pfd = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&pfd)))) {
            FILEOPENDIALOGOPTIONS opts = 0;
            pfd->GetOptions(&opts);
            pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
            pfd->SetTitle(L"デフォルト出力先を選択");
            // 現在の入力値を初期フォルダとして設定
            if (path[0]) {
                IShellItem* psi = nullptr;
                if (SUCCEEDED(SHCreateItemFromParsingName(path, nullptr, IID_PPV_ARGS(&psi)))) {
                    pfd->SetFolder(psi);
                    psi->Release();
                }
            }
            if (SUCCEEDED(pfd->Show(hwnd))) {
                IShellItem* psi = nullptr;
                if (SUCCEEDED(pfd->GetResult(&psi))) {
                    PWSTR psz = nullptr;
                    psi->GetDisplayName(SIGDN_FILESYSPATH, &psz);
                    if (psz) {
                        wcsncpy_s(path, psz, MAX_PATH - 1);
                        CoTaskMemFree(psz);
                        SetDlgItemTextW(hwnd, IDC_DEFAULT_DIR, path);
                    }
                    psi->Release();
                }
            }
            pfd->Release();
        }
    }
}

bool SettingsDlg::OnOK(HWND hwnd) {
    Settings& s = App::Instance().GetSettings();

    HWND hExt = GetDlgItem(hwnd, IDC_RAR_EXTRACTOR);
    int  sel  = (int)SendMessageW(hExt, CB_GETCURSEL, 0, 0);
    s.SetRarExtractor(sel == 1 ? L"unrar" : L"7z");

    wchar_t buf[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_DEFAULT_DIR, buf, MAX_PATH);
    s.SetDefaultOutputDir(buf);

    // パスが空のまま（自動検出）だった場合、表示値が自動検出結果と一致するなら
    // 空文字列を保存して次回も自動検出が機能するようにする。
    // ユーザーが手動で値を変更した場合はそのまま保存する。
    auto saveAutoPath = [&hwnd, &s](int ctlId, const std::wstring& currentSaved,
                                    const std::wstring& autoDetected,
                                    void (Settings::*setter)(const wchar_t*)) {
        wchar_t b[MAX_PATH] = {};
        GetDlgItemTextW(hwnd, ctlId, b, MAX_PATH);
        bool unchanged = currentSaved.empty() && !autoDetected.empty() && autoDetected == b;
        (s.*setter)(unchanged ? L"" : b);
    };
    saveAutoPath(IDC_7Z_DLL_PATH,    s.Get7zDllPath(),    SevenZip::Find7zDll(),    &Settings::Set7zDllPath);
    saveAutoPath(IDC_UNRAR_DLL_PATH, s.GetUnrarDllPath(), UnrarDll::FindUnrarDll(), &Settings::SetUnrarDllPath);
    saveAutoPath(IDC_RAR_EXE_PATH,   s.GetRarExePath(),   RarProcess::FindRarExe(), &Settings::SetRarExePath);

    s.Save();
    App::Instance().ReloadDlls();
    return true;
}

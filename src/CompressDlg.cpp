#include "CompressDlg.h"
#include "resource.h"
#include <shlobj.h>
#include <commctrl.h>
#include <commdlg.h>

struct FormatEntry {
    const wchar_t* label;
    const wchar_t* id;
};

static const FormatEntry kFormats[] = {
    {L"7-Zip (.7z)",  L"7z"},
    {L"ZIP (.zip)",   L"zip"},
    {L"TAR (.tar)",   L"tar"},
    {L"GZip (.gz)",   L"gz"},
    {L"BZip2 (.bz2)", L"bz2"},
    {L"XZ (.xz)",     L"xz"},
    {L"RAR (.rar)",   L"rar"},
};

struct MethodEntry { const wchar_t* label; const wchar_t* id; };

static const MethodEntry kMethods7z[] = {
    {L"LZMA2 (既定)",  L"lzma2"},
    {L"LZMA",          L"lzma"},
    {L"PPMd",          L"ppmd"},
    {L"BZip2",         L"bzip2"},
    {L"Deflate",       L"deflate"},
    {L"Zstandard",     L"zstd"},
};
static const MethodEntry kMethodsZip[] = {
    {L"Deflate (既定)", L"deflate"},
    {L"BZip2",          L"bzip2"},
    {L"LZMA",           L"lzma"},
    {L"Zstandard",      L"zstd"},
    {L"Store",          L"store"},
};
// rar.exe -m0..-m5
static const MethodEntry kMethodsRar[] = {
    {L"Store",          L"0"},
    {L"Fastest",        L"1"},
    {L"Fast",           L"2"},
    {L"Normal (既定)", L"3"},
    {L"Good",           L"4"},
    {L"Best",           L"5"},
};

bool CompressDlg::Show(HWND hwndParent, Params& params) {
    m_params = params;
    INT_PTR result = DialogBoxParamW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_COMPRESS),
        hwndParent, DlgProc, (LPARAM)this);
    if (result == IDOK) {
        params = m_params;
        return true;
    }
    return false;
}

INT_PTR CALLBACK CompressDlg::DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    CompressDlg* self = nullptr;
    if (msg == WM_INITDIALOG) {
        self = reinterpret_cast<CompressDlg*>(lp);
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)self);
    } else {
        self = reinterpret_cast<CompressDlg*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    }
    if (self) return self->HandleMsg(hwnd, msg, wp, lp);
    return FALSE;
}

INT_PTR CompressDlg::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG:
        m_hwnd = hwnd;
        OnInit(hwnd);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_FORMAT:
            if (HIWORD(wp) == CBN_SELCHANGE) OnFormatChange(hwnd);
            break;
        case IDC_BROWSE:
            OnBrowseOutput(hwnd);
            break;
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

void CompressDlg::OnInit(HWND hwnd) {
    // Populate format combo
    HWND hFmt = GetDlgItem(hwnd, IDC_FORMAT);
    for (auto& f : kFormats) {
        int idx = (int)SendMessageW(hFmt, CB_ADDSTRING, 0, (LPARAM)f.label);
        SendMessageW(hFmt, CB_SETITEMDATA, idx, (LPARAM)f.id);
        if (m_params.format == f.id) SendMessageW(hFmt, CB_SETCURSEL, idx, 0);
    }
    if (SendMessageW(hFmt, CB_GETCURSEL, 0, 0) == CB_ERR)
        SendMessageW(hFmt, CB_SETCURSEL, 0, 0);

    // Populate level combo
    HWND hLevel = GetDlgItem(hwnd, IDC_LEVEL);
    const wchar_t* levels[] = {
        L"0 – 無圧縮", L"1 – 最高速", L"3 – 高速",
        L"5 – 標準",   L"7 – 最大",   L"9 – 超圧縮"
    };
    const int levelVals[] = {0, 1, 3, 5, 7, 9};
    for (int i = 0; i < 6; ++i) {
        int idx = (int)SendMessageW(hLevel, CB_ADDSTRING, 0, (LPARAM)levels[i]);
        SendMessageW(hLevel, CB_SETITEMDATA, idx, levelVals[i]);
        if (m_params.level == levelVals[i]) SendMessageW(hLevel, CB_SETCURSEL, idx, 0);
    }
    if (SendMessageW(hLevel, CB_GETCURSEL, 0, 0) == CB_ERR)
        SendMessageW(hLevel, CB_SETCURSEL, 3, 0);  // default = 5 (index 3)

    // Output path
    SetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, m_params.outputPath.c_str());

    // Password
    SetDlgItemTextW(hwnd, IDC_PASSWORD, m_params.password.c_str());

    // Encrypt header checkbox
    CheckDlgButton(hwnd, IDC_ENCRYPT_HDR,
                   m_params.encryptHeaders ? BST_CHECKED : BST_UNCHECKED);

    OnFormatChange(hwnd);
}

void CompressDlg::OnFormatChange(HWND hwnd) {
    HWND hFmt    = GetDlgItem(hwnd, IDC_FORMAT);
    HWND hMethod = GetDlgItem(hwnd, IDC_METHOD);
    int  sel     = (int)SendMessageW(hFmt, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) return;

    const wchar_t* fmtId = (const wchar_t*)SendMessageW(hFmt, CB_GETITEMDATA, sel, 0);

    // Update output path extension to match the selected format.
    // For gz/bz2/xz, use .tar.X when multiple inputs or a directory are selected
    // (SevenZip::Compress will auto-wrap in tar at compression time).
    wchar_t outPath[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, outPath, MAX_PATH);
    if (outPath[0] && fmtId) {
        bool isStream = (wcscmp(fmtId, L"gz")  == 0 ||
                         wcscmp(fmtId, L"bz2") == 0 ||
                         wcscmp(fmtId, L"xz")  == 0);
        bool needsTar = false;
        if (isStream) {
            needsTar = m_params.inputFiles.size() > 1;
            if (!needsTar && m_params.inputFiles.size() == 1) {
                DWORD attrs = GetFileAttributesW(m_params.inputFiles[0].c_str());
                needsTar = (attrs != INVALID_FILE_ATTRIBUTES &&
                            (attrs & FILE_ATTRIBUTE_DIRECTORY));
            }
        }
        std::wstring ext = needsTar
            ? (std::wstring(L".tar.") + fmtId)
            : (std::wstring(L".") + fmtId);

        // Strip existing archive extension from the path, including any .tar prefix
        wchar_t* dot = wcsrchr(outPath, L'.');
        if (dot && !wcschr(dot, L'\\') && !wcschr(dot, L'/')) {
            *dot = L'\0';  // remove last extension
        }
        // Remove .tar suffix so switching between formats stays clean
        size_t blen = wcslen(outPath);
        if (blen >= 4 && _wcsicmp(outPath + blen - 4, L".tar") == 0) {
            outPath[blen - 4] = L'\0';
        }
        std::wstring newPath(outPath);
        newPath += ext;
        wcsncpy_s(outPath, newPath.c_str(), MAX_PATH - 1);
        SetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, outPath);
    }

    SendMessageW(hMethod, CB_RESETCONTENT, 0, 0);
    bool is7z  = (fmtId && wcscmp(fmtId, L"7z")  == 0);
    bool isZip = (fmtId && wcscmp(fmtId, L"zip") == 0);
    bool isRar = (fmtId && wcscmp(fmtId, L"rar") == 0);

    if (isRar) {
        // RAR compression level is controlled via -m0..-m5; map to method combo
        for (int i = 0; i < (int)_countof(kMethodsRar); ++i) {
            int idx = (int)SendMessageW(hMethod, CB_ADDSTRING, 0, (LPARAM)kMethodsRar[i].label);
            SendMessageW(hMethod, CB_SETITEMDATA, idx, (LPARAM)kMethodsRar[i].id);
            if (m_params.method == kMethodsRar[i].id)
                SendMessageW(hMethod, CB_SETCURSEL, idx, 0);
        }
        if (SendMessageW(hMethod, CB_GETCURSEL, 0, 0) == CB_ERR)
            SendMessageW(hMethod, CB_SETCURSEL, 3, 0);  // default = Normal
        EnableWindow(hMethod, TRUE);
        EnableWindow(GetDlgItem(hwnd, IDC_LEVEL), FALSE);  // level is meaningless for RAR
        EnableWindow(GetDlgItem(hwnd, IDC_PASSWORD), TRUE);
        EnableWindow(GetDlgItem(hwnd, IDC_ENCRYPT_HDR), FALSE);
        return;
    }
    EnableWindow(GetDlgItem(hwnd, IDC_LEVEL), TRUE);
    EnableWindow(hMethod, TRUE);
    EnableWindow(GetDlgItem(hwnd, IDC_PASSWORD), TRUE);
    EnableWindow(GetDlgItem(hwnd, IDC_ENCRYPT_HDR), is7z);

    const MethodEntry* methods = is7z ? kMethods7z : kMethodsZip;
    int count = is7z ? (int)_countof(kMethods7z) : (int)_countof(kMethodsZip);
    for (int i = 0; i < count; ++i) {
        int idx = (int)SendMessageW(hMethod, CB_ADDSTRING, 0, (LPARAM)methods[i].label);
        SendMessageW(hMethod, CB_SETITEMDATA, idx, (LPARAM)methods[i].id);
        if (m_params.method == methods[i].id) SendMessageW(hMethod, CB_SETCURSEL, idx, 0);
    }
    if (SendMessageW(hMethod, CB_GETCURSEL, 0, 0) == CB_ERR)
        SendMessageW(hMethod, CB_SETCURSEL, 0, 0);
}

void CompressDlg::OnBrowseOutput(HWND hwnd) {
    wchar_t path[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, path, MAX_PATH);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = hwnd;
    ofn.lpstrFilter  = L"すべてのファイル\0*.*\0";
    ofn.lpstrFile    = path;
    ofn.nMaxFile     = MAX_PATH;
    ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle   = L"出力ファイル名";
    if (GetSaveFileNameW(&ofn))
        SetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, path);
}

bool CompressDlg::OnOK(HWND hwnd) {
    // Read output path
    wchar_t path[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_OUTPUT_PATH, path, MAX_PATH);
    if (!path[0]) {
        MessageBoxW(hwnd, L"出力先を指定してください。", L"AileEx", MB_ICONWARNING);
        return false;
    }
    m_params.outputPath = path;

    // Read format
    HWND hFmt = GetDlgItem(hwnd, IDC_FORMAT);
    int  sel  = (int)SendMessageW(hFmt, CB_GETCURSEL, 0, 0);
    if (sel != CB_ERR) {
        const wchar_t* fmtId = (const wchar_t*)SendMessageW(hFmt, CB_GETITEMDATA, sel, 0);
        if (fmtId) m_params.format = fmtId;
    }

    // Read level
    HWND hLevel = GetDlgItem(hwnd, IDC_LEVEL);
    int  lsel   = (int)SendMessageW(hLevel, CB_GETCURSEL, 0, 0);
    if (lsel != CB_ERR) {
        m_params.level = (int)SendMessageW(hLevel, CB_GETITEMDATA, lsel, 0);
    }

    // Read method
    HWND hMethod = GetDlgItem(hwnd, IDC_METHOD);
    int  msel    = (int)SendMessageW(hMethod, CB_GETCURSEL, 0, 0);
    if (msel != CB_ERR) {
        const wchar_t* mId = (const wchar_t*)SendMessageW(hMethod, CB_GETITEMDATA, msel, 0);
        if (mId) m_params.method = mId;
    }

    // Read password
    wchar_t pw[256] = {};
    GetDlgItemTextW(hwnd, IDC_PASSWORD, pw, 256);
    m_params.password = pw;

    m_params.encryptHeaders = (IsDlgButtonChecked(hwnd, IDC_ENCRYPT_HDR) == BST_CHECKED);

    return true;
}

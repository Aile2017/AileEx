#include "AdvancedCompressDlg.h"
#include "resource.h"
#include <commctrl.h>

struct ComboEntry {
    const wchar_t* label;
    const wchar_t* val;
};

// ---- 辞書サイズ ----
static const ComboEntry kDictSizes[] = {
    {L"\u81ea\u52d5",      L""},
    {L"64 KB",     L"64k"},
    {L"128 KB",    L"128k"},
    {L"256 KB",    L"256k"},
    {L"512 KB",    L"512k"},
    {L"1 MB",      L"1m"},
    {L"2 MB",      L"2m"},
    {L"4 MB",      L"4m"},
    {L"8 MB",      L"8m"},
    {L"16 MB",     L"16m"},
    {L"32 MB",     L"32m"},
    {L"64 MB",     L"64m"},
    {L"128 MB",    L"128m"},
    {L"256 MB",    L"256m"},
    {L"512 MB",    L"512m"},
    {L"1 GB",      L"1g"},
};

// ---- \u30ef\u30fc\u30c9\u30b5\u30a4\u30ba (fast bytes) ----
static const ComboEntry kWordSizes[] = {
    {L"\u81ea\u52d5",  L""},
    {L"8",     L"8"},
    {L"12",    L"12"},
    {L"16",    L"16"},
    {L"24",    L"24"},
    {L"32",    L"32"},
    {L"48",    L"48"},
    {L"64",    L"64"},
    {L"96",    L"96"},
    {L"128",   L"128"},
    {L"273",   L"273"},
};

// ---- \u30bd\u30ea\u30c3\u30c9\u30d6\u30ed\u30c3\u30af\u30b5\u30a4\u30ba (7z only) ----
static const ComboEntry kSolidBlocks[] = {
    {L"\u65e2\u5b9a",       L""},
    {L"\u975e\u30bd\u30ea\u30c3\u30c9", L"off"},
    {L"1 MB",      L"1m"},
    {L"4 MB",      L"4m"},
    {L"16 MB",     L"16m"},
    {L"64 MB",     L"64m"},
    {L"256 MB",    L"256m"},
    {L"1 GB",      L"1g"},
    {L"4 GB",      L"4g"},
    {L"16 GB",     L"16g"},
    {L"64 GB",     L"64g"},
};

// ---- \u30b9\u30ec\u30c3\u30c9\u6570 ----
static const ComboEntry kThreads[] = {
    {L"\u81ea\u52d5", L""},
    {L"1",   L"1"},
    {L"2",   L"2"},
    {L"3",   L"3"},
    {L"4",   L"4"},
    {L"6",   L"6"},
    {L"8",   L"8"},
    {L"12",  L"12"},
    {L"16",  L"16"},
    {L"24",  L"24"},
    {L"32",  L"32"},
};

// ---- \u5206\u5272\u30dc\u30ea\u30e5\u30fc\u30e0 ----
static const ComboEntry kVolumes[] = {
    {L"\u306a\u3057",        L""},
    {L"1 MB",       L"1m"},
    {L"10 MB",      L"10m"},
    {L"50 MB",      L"50m"},
    {L"100 MB",     L"100m"},
    {L"200 MB",     L"200m"},
    {L"700 MB (CD)", L"700m"},
    {L"1 GB",        L"1g"},
    {L"4480 MB (DVD)", L"4480m"},
};

// ---- Helper: ComboBox \u306e\u521d\u671f\u5316\u3068\u9078\u629e ----
static void FillCombo(HWND hCombo, const ComboEntry* arr, int count,
                      const std::wstring& curVal) {
    int sel = 0;
    for (int i = 0; i < count; i++) {
        int idx = (int)SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)arr[i].label);
        SendMessageW(hCombo, CB_SETITEMDATA, idx, (LPARAM)arr[i].val);
        if (curVal == arr[i].val) sel = i;
    }
    SendMessageW(hCombo, CB_SETCURSEL, sel, 0);
}

// ---- AdvancedCompressDlg ----

bool AdvancedCompressDlg::Show(HWND hwndParent, const wchar_t* format, Params& params) {
    m_params = params;
    m_format = format ? format : L"";

    INT_PTR ret = DialogBoxParamW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_COMPRESS_ADV),
        hwndParent,
        DlgProc,
        reinterpret_cast<LPARAM>(this));

    if (ret == IDOK) {
        params = m_params;
        return true;
    }
    return false;
}

INT_PTR CALLBACK AdvancedCompressDlg::DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AdvancedCompressDlg* self = nullptr;
    if (msg == WM_INITDIALOG) {
        self = reinterpret_cast<AdvancedCompressDlg*>(lp);
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<AdvancedCompressDlg*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    }
    if (!self) return FALSE;
    return self->HandleMsg(hwnd, msg, wp, lp);
}

INT_PTR AdvancedCompressDlg::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG:
        OnInit(hwnd);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK:
            if (OnOK(hwnd)) EndDialog(hwnd, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void AdvancedCompressDlg::OnInit(HWND hwnd) {
    FillCombo(GetDlgItem(hwnd, IDC_ADV_DICT),
              kDictSizes, (int)_countof(kDictSizes), m_params.dictSize);

    FillCombo(GetDlgItem(hwnd, IDC_ADV_WORD),
              kWordSizes, (int)_countof(kWordSizes), m_params.wordSize);

    // \u30bd\u30ea\u30c3\u30c9\u30d6\u30ed\u30c3\u30af (7z \u306e\u307f\u6709\u52b9)
    {
        HWND hSolid = GetDlgItem(hwnd, IDC_ADV_SOLID);
        FillCombo(hSolid, kSolidBlocks, (int)_countof(kSolidBlocks), m_params.solidBlock);
        bool is7z = (m_format == L"7z");
        EnableWindow(hSolid, is7z ? TRUE : FALSE);
    }

    // \u30b9\u30ec\u30c3\u30c9\u6570: CPU \u8ad6\u7406\u30b3\u30a2\u6570\u3092\u4e0a\u9650\u3068\u3057\u3066\u8ffd\u52a0
    {
        HWND hThreads = GetDlgItem(hwnd, IDC_ADV_THREADS);
        SYSTEM_INFO si = {};
        GetSystemInfo(&si);
        int maxCpu = (int)si.dwNumberOfProcessors;

        int sel = 0;
        for (int i = 0; i < (int)_countof(kThreads); i++) {
            int n = (i == 0) ? 0 : _wtoi(kThreads[i].val);
            if (i > 0 && n > maxCpu) break;
            int idx = (int)SendMessageW(hThreads, CB_ADDSTRING, 0, (LPARAM)kThreads[i].label);
            SendMessageW(hThreads, CB_SETITEMDATA, idx, (LPARAM)kThreads[i].val);
            if (m_params.threads == kThreads[i].val) sel = (int)idx;
        }
        SendMessageW(hThreads, CB_SETCURSEL, sel, 0);
    }

    // 分割ボリューム (7z/zip 等で有効。gz/bz2/xz/tar では Compress 内で無視される)
    {
        HWND hVol = GetDlgItem(hwnd, IDC_ADV_VOLUME);
        FillCombo(hVol, kVolumes, (int)_countof(kVolumes), m_params.volumeSize);
        bool splittable = (m_format == L"7z" || m_format == L"zip");
        EnableWindow(hVol, splittable ? TRUE : FALSE);
    }

    SetDlgItemTextW(hwnd, IDC_ADV_PARAMS, m_params.extra.c_str());
}

bool AdvancedCompressDlg::OnOK(HWND hwnd) {
    auto getComboVal = [&](int ctrlId) -> std::wstring {
        HWND h = GetDlgItem(hwnd, ctrlId);
        int sel = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
        if (sel == CB_ERR) return L"";
        const wchar_t* v = (const wchar_t*)SendMessageW(h, CB_GETITEMDATA, sel, 0);
        return v ? v : L"";
    };

    m_params.dictSize   = getComboVal(IDC_ADV_DICT);
    m_params.wordSize   = getComboVal(IDC_ADV_WORD);
    m_params.solidBlock = getComboVal(IDC_ADV_SOLID);
    m_params.threads    = getComboVal(IDC_ADV_THREADS);
    m_params.volumeSize = getComboVal(IDC_ADV_VOLUME);

    wchar_t buf[512] = {};
    GetDlgItemTextW(hwnd, IDC_ADV_PARAMS, buf, 512);
    m_params.extra = buf;

    return true;
}

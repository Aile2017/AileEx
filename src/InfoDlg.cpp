#include "InfoDlg.h"
#include "resource.h"
#include <commctrl.h>
#include <string>

// ---- Formatting helpers ----

static std::wstring FormatFileTime(const FILETIME& ft) {
    if (ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0)
        return L"―";
    FILETIME local = {};
    FileTimeToLocalFileTime(&ft, &local);
    SYSTEMTIME st = {};
    FileTimeToSystemTime(&local, &st);
    wchar_t buf[32];
    swprintf_s(buf, L"%04d/%02d/%02d %02d:%02d:%02d",
               st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);
    return buf;
}

static std::wstring FormatSize(UINT64 size) {
    wchar_t buf[128];
    if (size >= 1ULL << 30)
        swprintf_s(buf, L"%llu バイト (%.2f GB)", (unsigned long long)size, (double)size / (1ULL << 30));
    else if (size >= 1ULL << 20)
        swprintf_s(buf, L"%llu バイト (%.2f MB)", (unsigned long long)size, (double)size / (1ULL << 20));
    else if (size >= 1ULL << 10)
        swprintf_s(buf, L"%llu バイト (%.2f KB)", (unsigned long long)size, (double)size / (1ULL << 10));
    else
        swprintf_s(buf, L"%llu バイト", (unsigned long long)size);
    return buf;
}

static std::wstring FormatAttrib(UINT32 attrib) {
    if (attrib == 0) return L"―";
    std::wstring s;
    if (attrib & FILE_ATTRIBUTE_DIRECTORY)  s += L"D";
    if (attrib & FILE_ATTRIBUTE_ARCHIVE)    s += L"A";
    if (attrib & FILE_ATTRIBUTE_READONLY)   s += L"R";
    if (attrib & FILE_ATTRIBUTE_HIDDEN)     s += L"H";
    if (attrib & FILE_ATTRIBUTE_SYSTEM)     s += L"S";
    if (attrib & FILE_ATTRIBUTE_COMPRESSED) s += L"C";
    if (attrib & FILE_ATTRIBUTE_ENCRYPTED)  s += L"E";
    wchar_t hex[16];
    swprintf_s(hex, L" (0x%08X)", attrib);
    return s.empty() ? std::wstring(hex + 1) : s + hex;
}

static void AddRow(HWND hList, int row, const wchar_t* key, const wchar_t* value) {
    LVITEMW lvi = {};
    lvi.mask     = LVIF_TEXT;
    lvi.iItem    = row;
    lvi.iSubItem = 0;
    lvi.pszText  = const_cast<wchar_t*>(key);
    ListView_InsertItem(hList, &lvi);
    ListView_SetItemText(hList, row, 1, const_cast<wchar_t*>(value));
}

// ---- Dialog ----

void InfoDlg::Show(HWND parent, const ArchiveItem& item) {
    m_item = &item;
    DialogBoxParamW(GetModuleHandleW(nullptr),
                    MAKEINTRESOURCEW(IDD_INFO),
                    parent, DlgProc, (LPARAM)this);
}

INT_PTR CALLBACK InfoDlg::DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    InfoDlg* self = nullptr;
    if (msg == WM_INITDIALOG) {
        self = reinterpret_cast<InfoDlg*>(lp);
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)self);
    } else {
        self = reinterpret_cast<InfoDlg*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    }
    if (self) return self->HandleMsg(hwnd, msg, wp, lp);
    return FALSE;
}

INT_PTR InfoDlg::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG:
        m_hwnd = hwnd;
        OnInit(hwnd);
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL)
            EndDialog(hwnd, 0);
        return TRUE;
    }
    return FALSE;
}

void InfoDlg::OnInit(HWND hwnd) {
    const ArchiveItem& it = *m_item;

    // Update title to show filename
    std::wstring title = L"ファイル情報 - " + it.name;
    SetWindowTextW(hwnd, title.c_str());

    HWND hList = GetDlgItem(hwnd, IDC_INFO_LIST);

    // Columns
    LVCOLUMNW lvc = {};
    lvc.mask    = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    lvc.fmt     = LVCFMT_LEFT;
    lvc.cx      = 130;
    lvc.pszText = const_cast<wchar_t*>(L"項目");
    ListView_InsertColumn(hList, 0, &lvc);
    lvc.cx      = 190;
    lvc.pszText = const_cast<wchar_t*>(L"値");
    ListView_InsertColumn(hList, 1, &lvc);

    ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    int row = 0;

    // Basic identity
    AddRow(hList, row++, L"ファイル名",         it.name.c_str());
    AddRow(hList, row++, L"アーカイブ内パス",    it.path.c_str());

    // Type
    std::wstring typeStr;
    if (it.isDir) {
        typeStr = L"フォルダ";
    } else {
        auto dot = it.name.rfind(L'.');
        typeStr = (dot != std::wstring::npos && dot + 1 < it.name.size())
                  ? it.name.substr(dot + 1) + L" ファイル"
                  : L"ファイル";
    }
    AddRow(hList, row++, L"種類", typeStr.c_str());

    // Sizes
    AddRow(hList, row++, L"元のサイズ",
           it.isDir ? L"―" : FormatSize(it.size).c_str());
    AddRow(hList, row++, L"圧縮後サイズ",
           (it.isDir || it.packedSize == 0) ? L"―" : FormatSize(it.packedSize).c_str());

    // Ratio
    std::wstring ratio = L"―";
    if (!it.isDir && it.size > 0) {
        wchar_t buf[32];
        double r = 100.0 - (double)it.packedSize / (double)it.size * 100.0;
        swprintf_s(buf, L"%.1f%%", r);
        ratio = buf;
    }
    AddRow(hList, row++, L"圧縮率", ratio.c_str());

    // Method
    AddRow(hList, row++, L"圧縮メソッド",
           it.method.empty() ? L"―" : it.method.c_str());

    // CRC
    if (it.hasCrc) {
        wchar_t crcBuf[16];
        swprintf_s(crcBuf, L"%08X", it.crc);
        AddRow(hList, row++, L"CRC-32", crcBuf);
    } else {
        AddRow(hList, row++, L"CRC-32", L"―");
    }

    // Encryption
    AddRow(hList, row++, L"暗号化", it.encrypted ? L"あり" : L"なし");

    // File attributes
    AddRow(hList, row++, L"ファイル属性", FormatAttrib(it.attrib).c_str());

    // Host OS
    AddRow(hList, row++, L"ホスト OS",
           it.hostOS.empty() ? L"―" : it.hostOS.c_str());

    // Timestamps
    AddRow(hList, row++, L"更新日時",           FormatFileTime(it.mtime).c_str());
    AddRow(hList, row++, L"作成日時",           FormatFileTime(it.ctime).c_str());
    AddRow(hList, row++, L"最終アクセス日時",   FormatFileTime(it.atime).c_str());

    // Comment
    AddRow(hList, row++, L"コメント",
           it.comment.empty() ? L"―" : it.comment.c_str());
}

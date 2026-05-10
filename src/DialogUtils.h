#pragma once
#include "I18n.h"
#include "resource.h"
#include <windows.h>
#include <commctrl.h>
#include <shobjidl_core.h>
#include <commdlg.h>
#include <string>
#include <vector>

// ---- Formatting helpers for Info and Properties dialogs ----

inline std::wstring FormatFileTime(const FILETIME& ft) {
    if (ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0)
        return I18n::Tr(IDS_DASH);
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

inline std::wstring FormatSize(UINT64 size) {
    if (size >= 1ULL << 30)
        return I18n::TrFmt(IDS_FMT_SIZE_GB, (unsigned long long)size, (double)size / (1ULL << 30));
    if (size >= 1ULL << 20)
        return I18n::TrFmt(IDS_FMT_SIZE_MB, (unsigned long long)size, (double)size / (1ULL << 20));
    if (size >= 1ULL << 10)
        return I18n::TrFmt(IDS_FMT_SIZE_KB, (unsigned long long)size, (double)size / (1ULL << 10));
    return I18n::TrFmt(IDS_FMT_SIZE_BYTES, (unsigned long long)size);
}

inline void AddRow(HWND hList, int& row, const wchar_t* key, const wchar_t* value) {
    LVITEMW lvi = {};
    lvi.mask     = LVIF_TEXT;
    lvi.iItem    = row;
    lvi.iSubItem = 0;
    lvi.pszText  = const_cast<wchar_t*>(key);
    ListView_InsertItem(hList, &lvi);
    ListView_SetItemText(hList, row, 1, const_cast<wchar_t*>(value));
    ++row;
}

// ---- ComboBox initialization helpers ----

struct ComboEntry {
    UINT labelId;              // Resource ID; 0 = use hardcodedLabel
    const wchar_t* hardcodedLabel;
    const wchar_t* val;
};

inline void FillCombo(HWND hCombo, const ComboEntry* arr, int count,
                      const std::wstring& curVal) {
    int sel = 0;
    for (int i = 0; i < count; i++) {
        std::wstring label;
        if (arr[i].labelId != 0) {
            label = I18n::Tr(arr[i].labelId);
        } else if (arr[i].hardcodedLabel) {
            label = arr[i].hardcodedLabel;
        } else {
            continue;
        }
        int idx = (int)SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)label.c_str());
        SendMessageW(hCombo, CB_SETITEMDATA, idx, (LPARAM)arr[i].val);
        if (curVal == arr[i].val) sel = i;
    }
    SendMessageW(hCombo, CB_SETCURSEL, sel, 0);
}

inline void FillThreadCombo(HWND hCombo, const ComboEntry* arr, int count,
                            const std::wstring& curVal) {
    SYSTEM_INFO si = {};
    GetSystemInfo(&si);
    int maxCpu = (int)si.dwNumberOfProcessors;
    int sel = 0;
    for (int i = 0; i < count; i++) {
        int n = _wtoi(arr[i].val);
        if (i > 0 && n > maxCpu) break;
        std::wstring label;
        if (arr[i].labelId != 0) {
            label = I18n::Tr(arr[i].labelId);
        } else if (arr[i].hardcodedLabel) {
            label = arr[i].hardcodedLabel;
        } else {
            continue;
        }
        int idx = (int)SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)label.c_str());
        SendMessageW(hCombo, CB_SETITEMDATA, idx, (LPARAM)arr[i].val);
        if (curVal == arr[i].val) sel = (int)idx;
    }
    SendMessageW(hCombo, CB_SETCURSEL, sel, 0);
}

// ---- File and folder selection dialogs ----

inline std::vector<std::wstring> BrowseMultipleFiles(HWND hwnd, UINT titleId) {
    wchar_t buf[32768] = {};
    std::wstring title = I18n::Tr(titleId);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = _countof(buf);
    ofn.lpstrTitle  = title.c_str();
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

    std::vector<std::wstring> files;
    if (!GetOpenFileNameW(&ofn)) return files;

    const wchar_t* p = buf;
    std::wstring dir = p;
    p += dir.size() + 1;
    if (*p == L'\0') {
        files.push_back(dir);
    } else {
        while (*p) {
            std::wstring name = p;
            files.push_back(dir + L'\\' + name);
            p += name.size() + 1;
        }
    }
    return files;
}

inline bool BrowseFolderDialog(HWND hwnd, UINT titleId,
                               wchar_t* inOutPath, size_t maxPath) {
    IFileOpenDialog* pfd = nullptr;
    if (!SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))))
        return false;
    FILEOPENDIALOGOPTIONS opts = 0;
    pfd->GetOptions(&opts);
    pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    pfd->SetTitle(I18n::Tr(titleId).c_str());
    if (inOutPath && inOutPath[0]) {
        IShellItem* psi = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(inOutPath, nullptr, IID_PPV_ARGS(&psi)))) {
            pfd->SetFolder(psi);
            psi->Release();
        }
    }
    bool ok = false;
    if (SUCCEEDED(pfd->Show(hwnd))) {
        IShellItem* psi = nullptr;
        if (SUCCEEDED(pfd->GetResult(&psi))) {
            PWSTR psz = nullptr;
            psi->GetDisplayName(SIGDN_FILESYSPATH, &psz);
            if (psz) {
                wcsncpy_s(inOutPath, maxPath, psz, maxPath - 1);
                CoTaskMemFree(psz);
                ok = true;
            }
            psi->Release();
        }
    }
    pfd->Release();
    return ok;
}

template<typename T>
INT_PTR CALLBACK StandardDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    T* self = nullptr;
    if (msg == WM_INITDIALOG) {
        self = reinterpret_cast<T*>(lp);
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)self);
    } else {
        self = reinterpret_cast<T*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    }
    if (!self) return FALSE;
    return self->HandleMsg(hwnd, msg, wp, lp);
}

// ---- Single file browse dialog ----
// Pass OFN_FILEMUSTEXIST for open, OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST for save.
// Set saveDialog=true to call GetSaveFileNameW instead of GetOpenFileNameW.
inline bool BrowseForFile(HWND hwnd, UINT titleId, UINT filterId, DWORD flags,
                          wchar_t* inOutPath, size_t maxPath, bool saveDialog = false) {
    std::wstring filter = I18n::TrFilter(filterId);
    std::wstring title  = I18n::Tr(titleId);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile   = inOutPath;
    ofn.nMaxFile    = (DWORD)maxPath;
    ofn.Flags       = flags;
    ofn.lpstrTitle  = title.c_str();
    return (saveDialog ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn)) != FALSE;
}

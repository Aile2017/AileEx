#include "ProgressDlg.h"
#include "resource.h"
#include <commctrl.h>

void ProgressDlg::Show(HWND hwndParent, const wchar_t* title) {
    m_hwndParent = hwndParent;
    m_hwnd = CreateDialogParamW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_PROGRESS),
        hwndParent, DlgProc, (LPARAM)this);

    if (!m_hwnd) return;

    if (title) SetWindowTextW(m_hwnd, title);

    m_hwndPB    = GetDlgItem(m_hwnd, IDC_PROGRESS_BAR);
    m_hwndLabel = GetDlgItem(m_hwnd, IDC_PROGRESS_FILE);
    m_hwndCancel = GetDlgItem(m_hwnd, IDC_CANCEL);

    SendMessageW(m_hwndPB, PBM_SETRANGE32, 0, 100);
    SendMessageW(m_hwndPB, PBM_SETPOS, 0, 0);

    if (hwndParent) EnableWindow(hwndParent, FALSE);
    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
}

void ProgressDlg::SetTotal(UINT64 total) {
    m_total = total;
}

void ProgressDlg::SetProgress(int pct, const wchar_t* filename) {
    if (!m_hwnd) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    SendMessageW(m_hwndPB, PBM_SETPOS, pct, 0);
    if (filename && m_hwndLabel)
        SetWindowTextW(m_hwndLabel, filename);
}

void ProgressDlg::SetDone(HRESULT hr) {
    if (!m_hwnd) return;
    SendMessageW(m_hwndPB, PBM_SETPOS, 100, 0);
    const wchar_t* msg = SUCCEEDED(hr) ? L"完了" : (hr == E_ABORT ? L"キャンセルされました" : L"エラーが発生しました");
    if (m_hwndLabel) SetWindowTextW(m_hwndLabel, msg);
}

void ProgressDlg::Dismiss() {
    if (!m_hwnd) return;
    if (m_hwndParent) EnableWindow(m_hwndParent, TRUE);
    DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
    if (m_hwndParent) SetForegroundWindow(m_hwndParent);
}

INT_PTR CALLBACK ProgressDlg::DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ProgressDlg* self = nullptr;
    if (msg == WM_INITDIALOG) {
        self = reinterpret_cast<ProgressDlg*>(lp);
        SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)self);
    } else {
        self = reinterpret_cast<ProgressDlg*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    }
    if (self) return self->HandleMsg(hwnd, msg, wp, lp);
    return FALSE;
}

INT_PTR ProgressDlg::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG:
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wp) == IDC_CANCEL) {
            if (m_sink) m_sink->SetCancelled(true);
            if (m_hwndCancel) EnableWindow(m_hwndCancel, FALSE);
            if (m_hwndLabel) SetWindowTextW(m_hwndLabel, L"キャンセル中...");
        }
        return TRUE;

    case WM_CLOSE:
        if (m_sink) m_sink->SetCancelled(true);
        return TRUE;
    }
    return FALSE;
}

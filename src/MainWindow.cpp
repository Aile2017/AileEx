#include "MainWindow.h"
#include "App.h"
#include "CompressDlg.h"
#include "CompressHelper.h"
#include "CommentDlg.h"
#include "DialogUtils.h"
#include "I18n.h"
#include "InfoDlg.h"
#include "PropertiesDlg.h"
#include "ProgressDlg.h"
#include "RarProcess.h"
#include "SettingsDlg.h"
#include "resource.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl_core.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <windowsx.h>
#include <map>
#include <commctrl.h>
#include <algorithm>
#include <set>

#pragma comment(lib, "version.lib")

namespace {
// Force foreground for cases like launcher-spawned processes where parent already exited.
// SetForegroundWindow alone is restricted and demoted, so attach to foreground app's thread,
// apply TopMost briefly to push Z-order, then call.
void ForceForeground(HWND hwnd) {
    HWND  fg    = GetForegroundWindow();
    DWORD fgTid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD myTid = GetCurrentThreadId();
    bool  attach = (fgTid && fgTid != myTid);

    if (attach) AttachThreadInput(myTid, fgTid, TRUE);
    SetWindowPos(hwnd, HWND_TOPMOST,   0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    if (attach) AttachThreadInput(myTid, fgTid, FALSE);
}

// Return top-level entry count from archive m_items (unique first path components)
int CountTopLevelEntries(const std::vector<ArchiveItem>& items) {
    std::set<std::wstring> tops;
    for (const auto& item : items) {
        if (item.path.empty()) continue;
        auto slash = item.path.find(L'/');
        tops.insert(slash != std::wstring::npos ? item.path.substr(0, slash) : item.path);
    }
    return (int)tops.size();
}

// Generate subfolder name from archive path (strip compound extensions: archive.tar.gz → archive,
// archive.7z.001 → archive)
std::wstring ArchiveBaseName(const std::wstring& archivePath) {
    static const wchar_t* kExts[] = {
        L".7z", L".zip", L".rar", L".tar", L".gz", L".bz2", L".xz",
        L".cab", L".iso", L".jar", L".wim", L".lzh", L".lzma", L".arj",
        L".zst", L".lz4", L".lz5", L".br", L".liz", nullptr
    };
    std::wstring name = PathFindFileNameW(archivePath.c_str());
    bool stripped = true;
    while (stripped) {
        stripped = false;
        // Strip all-digit trailing extensions (.001 etc.)
        auto dot = name.rfind(L'.');
        if (dot != std::wstring::npos && dot + 1 < name.size()) {
            bool allDigits = true;
            for (size_t i = dot + 1; i < name.size(); ++i)
                if (!iswdigit(name[i])) { allDigits = false; break; }
            if (allDigits) {
                name = name.substr(0, dot);
                stripped = true;
                continue;
            }
        }
        for (int i = 0; kExts[i]; ++i) {
            size_t elen = wcslen(kExts[i]);
            if (name.size() <= elen) continue;
            std::wstring tail = name.substr(name.size() - elen);
            for (auto& c : tail) c = (wchar_t)towlower(c);
            if (tail == kExts[i]) {
                name = name.substr(0, name.size() - elen);
                stripped = true;
                break;
            }
        }
    }
    return name.empty() ? L"archive" : name;
}

// Determine if subfolder should be created based on MkDir policy
// mkDir: 0=no / 1=single file only / 2=multiple entries / 3=always
bool ShouldCreateSubfolder(int mkDir, const std::vector<ArchiveItem>& items) {
    if (mkDir == 0) return false;
    if (mkDir == 3) return true;
    int topCount = CountTopLevelEntries(items);
    if (mkDir == 2) return topCount >= 2;
    // mkDir == 1: single top-level entry that is a file (not directory)
    if (topCount != 1) return false;
    // If single top-level is directory, archive has folder structure, so not needed
    for (const auto& item : items) {
        if (item.isDir && item.path.find(L'/') == std::wstring::npos)
            return false; // Top-level directory exists
    }
    return true;
}

} // namespace

bool MainWindow::RegisterClass(HINSTANCE hInst) {
    WNDCLASSEXW wc  = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    wc.lpszClassName = ClassName();
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_AILEEX));
    wc.hIconSm       = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_AILEEX));
    return RegisterClassExW(&wc) != 0;
}

bool MainWindow::Create(HINSTANCE hInst, int nCmdShow) {
    auto& s = App::Instance().GetSettings();
    m_treeWidth   = s.GetSplitterPos();
    m_treeVisible = s.GetTreeVisible();
    m_toolbarVisible = s.GetToolbarVisible();

    int wx = s.GetWindowX(), wy = s.GetWindowY();
    int ww = s.GetWindowW(), wh = s.GetWindowH();
    if (wx < 0) { wx = CW_USEDEFAULT; wy = CW_USEDEFAULT; }

    HMENU hMenu = LoadMenuW(hInst, MAKEINTRESOURCEW(IDR_MAIN_MENU));
    HWND hwnd = CreateWindowExW(
        0, ClassName(), L"AileEx",
        WS_OVERLAPPEDWINDOW,
        wx, wy, ww, wh,
        nullptr, hMenu, hInst, this);

    if (!hwnd) return false;
    // If maximized was saved and caller did not request a specific show command, honour it
    if (s.GetWindowMaximized() && nCmdShow == SW_SHOWDEFAULT)
        nCmdShow = SW_SHOWMAXIMIZED;
    ShowWindow(hwnd, nCmdShow);
    ForceForeground(hwnd);
    UpdateWindow(hwnd);
    return true;
}

void MainWindow::OpenArchive(const wchar_t* path) {
    // Delete previously unwrapped split temp file (prevent leak on replace)
    if (!m_effectiveArchivePath.empty() &&
        _wcsicmp(m_effectiveArchivePath.c_str(), m_archivePath.c_str()) != 0) {
        DeleteFileW(m_effectiveArchivePath.c_str());
    }
    m_archivePath = path;
    m_effectiveArchivePath = path;
    m_isReadOnly = false;
    m_items.clear();

    App& app = App::Instance();

    // Determine if this is a RAR file
    const wchar_t* dotPos = wcsrchr(path, L'.');
    bool isRar = (dotPos && _wcsicmp(dotPos + 1, L"rar") == 0);

    // Determine primary backend
    bool preferUnrar = isRar &&
                       app.GetSettings().GetRarExtractor() == L"unrar" &&
                       app.GetUnrar().IsLoaded();

    HRESULT hr = E_FAIL;
    m_openedWithUnrar = false;

    if (preferUnrar) {
        // Try unrar first, then fall back to 7z
        if (app.GetUnrar().ListArchive(path, m_items)) {
            hr = S_OK;
            m_openedWithUnrar = true;
        } else if (app.Get7z().IsLoaded()) {
            hr = app.Get7z().OpenArchive(path, m_items, nullptr, &m_effectiveArchivePath);
        }
    } else {
        // Try 7z first
        if (app.Get7z().IsLoaded()) {
            hr = app.Get7z().OpenArchive(path, m_items, nullptr, &m_effectiveArchivePath);
        }
        // If 7z failed for a RAR file, try unrar as fallback
        if (FAILED(hr) && isRar && app.GetUnrar().IsLoaded()) {
            m_items.clear();
            if (app.GetUnrar().ListArchive(path, m_items)) {
                hr = S_OK;
                m_openedWithUnrar = true;
            }
        }
    }

    // On open failure: may be encrypted header, prompt for password and retry.
    // Respect the user's preferred backend (preferUnrar) and fall back to the other.
    if (FAILED(hr)) {
        std::wstring pw = PromptPassword();
        if (!pw.empty()) {
            m_items.clear();
            if (preferUnrar) {
                // User prefers unrar → try unrar first, then 7z
                if (app.GetUnrar().ListArchive(path, m_items, pw.c_str())) {
                    hr = S_OK;
                    m_openedWithUnrar = true;
                }
                if (FAILED(hr) && app.Get7z().IsLoaded()) {
                    m_items.clear();
                    hr = app.Get7z().OpenArchive(path, m_items, pw.c_str(), &m_effectiveArchivePath);
                }
            } else {
                // User prefers 7z → try 7z first, then unrar as fallback
                if (app.Get7z().IsLoaded()) {
                    hr = app.Get7z().OpenArchive(path, m_items, pw.c_str(), &m_effectiveArchivePath);
                }
                if (FAILED(hr) && isRar && app.GetUnrar().IsLoaded()) {
                    m_items.clear();
                    if (app.GetUnrar().ListArchive(path, m_items, pw.c_str())) {
                        hr = S_OK;
                        m_openedWithUnrar = true;
                    }
                }
            }
        }
    }

    // Detect split auto-unwrap → treat as read-only
    if (SUCCEEDED(hr) &&
        _wcsicmp(m_effectiveArchivePath.c_str(), m_archivePath.c_str()) != 0) {
        m_isReadOnly = true;
    }

    if (FAILED(hr)) {
        std::wstring msg = I18n::Tr(IDS_ERR_OPEN_ARCHIVE);
        if (!app.Get7z().IsLoaded() && !app.GetUnrar().IsLoaded())
            msg += I18n::Tr(IDS_ERR_OPEN_ARCHIVE_7Z_UNRAR);
        else if (!app.Get7z().IsLoaded())
            msg += I18n::Tr(IDS_ERR_OPEN_ARCHIVE_7Z);
        ShowError(msg.c_str(), hr);
        return;
    }

    // Update MRU — normalize relative paths and mixed cases ("../" etc.) via GetFullPathNameW.
    {
        wchar_t full[MAX_PATH] = {};
        if (GetFullPathNameW(path, MAX_PATH, full, nullptr) == 0)
            wcsncpy_s(full, path, MAX_PATH - 1);
        auto& s = app.GetSettings();
        s.AddMru(full);
        s.Save();
        RebuildMruMenu();
    }

    // Update title
    const wchar_t* leaf = wcsrchr(path, L'\\');
    std::wstring title = std::wstring(L"AileEx - ") + (leaf ? leaf + 1 : path);
    SetWindowTextW(m_hwnd, title.c_str());

    // Update status
    {
        const std::wstring& dllName = m_openedWithUnrar
            ? app.GetUnrar().GetLoadedName()
            : app.Get7z().GetLoadedName();
        std::wstring status = I18n::TrFmt(IDS_FMT_STATUS_ENTRIES,
                                          m_items.size(), dllName.c_str());
        SetWindowTextW(m_hStatus, status.c_str());
    }

    PopulateTree();
    PopulateList(L"");
}

// ---- WndProc dispatch ----

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMsg(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool MainWindow::PreTranslateMessage(const MSG& msg) {
    // Enter on a focused ListView item → folder navigation or extraction
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
        HWND hFocus = GetFocus();
        if (hFocus == m_hListView || IsChild(m_hListView, hFocus)) {
            OnListDblClick();
            return true;
        }
    }
    return false;
}

LRESULT MainWindow::HandleMsg(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        OnCreate(m_hwnd);
        return 0;

    case WM_SIZE:
        OnSize(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_DROPFILES:
        OnDropFiles((HDROP)wp);
        return 0;

    case WM_COMMAND:
        OnCommand(LOWORD(wp));
        return 0;

    case WM_CONTEXTMENU:
        if ((HWND)wp == m_hListView)
            OnContextMenu((HWND)wp, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_INITMENUPOPUP:
        // HIWORD(lp) != 0 means system menu (title bar right-click etc.) — skip
        if (HIWORD(lp) == 0)
            OnInitMenuPopup((HMENU)wp);
        break;

    case WM_NOTIFY: {
        auto* hdr = reinterpret_cast<NMHDR*>(lp);
        if (hdr->hwndFrom == m_hTreeView && hdr->code == TVN_SELCHANGED)
            OnTreeSelChanged();
        if (hdr->hwndFrom == m_hListView && hdr->code == NM_DBLCLK)
            OnListDblClick();
        if (hdr->hwndFrom == m_hListView && hdr->code == LVN_COLUMNCLICK) {
            auto* nm = reinterpret_cast<NMLISTVIEW*>(lp);
            OnColumnClick(nm->iSubItem);
        }
        if (hdr->code == TTN_GETDISPINFOW) {
            auto* pdi = reinterpret_cast<NMTTDISPINFOW*>(lp);
            UINT id = 0;
            switch (pdi->hdr.idFrom) {
            case ID_EXTRACT:      id = IDS_TIP_EXTRACT;  break;
            case ID_OPEN_ASSOC:   id = IDS_TIP_VIEW;     break;
            case ID_ADD:          id = IDS_TIP_ADD;      break;
            case ID_INFO:         id = IDS_TIP_INFO;     break;
            case ID_TEST:         id = IDS_TIP_TEST;     break;
            case ID_SETTINGS_DLG: id = IDS_TIP_SETTINGS; break;
            }
            if (id) {
                std::wstring s = I18n::Tr(id);
                wcsncpy_s(pdi->szText, s.c_str(), _countof(pdi->szText) - 1);
                pdi->lpszText = pdi->szText;
            }
        }
        return 0;
    }

    case WM_SETCURSOR: {
        if (m_treeVisible && (HWND)wp == m_hwnd) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(m_hwnd, &pt);
            if (pt.x >= m_treeWidth && pt.x < m_treeWidth + kSplitterW) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        int x = (int)(short)LOWORD(lp);
        if (m_treeVisible && x >= m_treeWidth && x < m_treeWidth + kSplitterW) {
            m_draggingSplitter = true;
            SetCapture(m_hwnd);
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE: {
        int x = (int)(short)LOWORD(lp);
        if (m_draggingSplitter) {
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            int newW = x;
            if (newW < kTreeMinW) newW = kTreeMinW;
            if (newW > rc.right - kListMinW - kSplitterW) newW = rc.right - kListMinW - kSplitterW;
            if (newW != m_treeWidth) {
                m_treeWidth = newW;
                ResizePanes(rc.right, rc.bottom);
            }
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            return 0;
        }
        if (m_treeVisible && x >= m_treeWidth && x < m_treeWidth + kSplitterW) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            return 0;
        }
        break;
    }

    case WM_LBUTTONUP:
        if (m_draggingSplitter) {
            m_draggingSplitter = false;
            ReleaseCapture();
            return 0;
        }
        break;

    case WM_CAPTURECHANGED:
        if (m_draggingSplitter) {
            m_draggingSplitter = false;
        }
        break;

    case WM_APP_PROGRESS:
        // fallback: normally unreachable because the inner loop absorbs WM_APP_PROGRESS/DONE
        OnProgress((int)wp, (wchar_t*)lp);
        return 0;

    case WM_APP_DONE:
        // fallback: normally unreachable because the inner loop absorbs WM_APP_PROGRESS/DONE
        OnDone((HRESULT)wp);
        return 0;

    case WM_DESTROY: {
        // Save window placement and splitter position
        {
            WINDOWPLACEMENT wp = {};
            wp.length = sizeof(wp);
            GetWindowPlacement(m_hwnd, &wp);
            bool maximized = (wp.showCmd == SW_SHOWMAXIMIZED);
            RECT& r = wp.rcNormalPosition;
            auto& s = App::Instance().GetSettings();
            s.SetWindowPlacement((int)r.left, (int)r.top,
                                 (int)(r.right - r.left), (int)(r.bottom - r.top),
                                 maximized);
            s.SetSplitterPos(m_treeWidth);
            s.SetTreeVisible(m_treeVisible);
            s.SetToolbarVisible(m_toolbarVisible);
            s.Save();
        }
        // Delete session temp dir tree (files opened via browse mode)
        if (!m_tempViewDir.empty()) {
            SHFILEOPSTRUCTW fop = {};
            std::wstring dir = m_tempViewDir;
            dir += L'\0';  // double-null required by SHFileOperation
            fop.wFunc  = FO_DELETE;
            fop.pFrom  = dir.c_str();
            fop.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
            SHFileOperationW(&fop);
        }
        // split auto-unwrap: clean up any temporary file created
        if (!m_effectiveArchivePath.empty() &&
            _wcsicmp(m_effectiveArchivePath.c_str(), m_archivePath.c_str()) != 0) {
            DeleteFileW(m_effectiveArchivePath.c_str());
        }
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

// ---- Control creation ----

// Forward WM_DROPFILES from child (ListView/TreeView) to parent.
// Without this, ListView drops are ignored (even though parent does DragAcceptFiles,
// child controls don't receive the message).
static LRESULT CALLBACK ChildDropForwardProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                              UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/) {
    if (msg == WM_DROPFILES) {
        // Parent handles DragFinish, so child does nothing.
        SendMessageW(GetParent(hwnd), WM_DROPFILES, wp, lp);
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void MainWindow::OnCreate(HWND hwnd) {
    CreateControls(hwnd);
    DragAcceptFiles(hwnd, TRUE);
    if (m_hListView) {
        DragAcceptFiles(m_hListView, TRUE);
        SetWindowSubclass(m_hListView, ChildDropForwardProc, 1, 0);
    }
    if (m_hTreeView) {
        DragAcceptFiles(m_hTreeView, TRUE);
        SetWindowSubclass(m_hTreeView, ChildDropForwardProc, 1, 0);
    }

    // Find and cache MRU submenu handle.
    // Once cached, rebuilding contents keeps HMENU itself valid.
    if (HMENU hMenuBar = GetMenu(hwnd)) {
        int topCount = GetMenuItemCount(hMenuBar);
        for (int i = 0; i < topCount && !m_hMruMenu; ++i) {
            HMENU hPopup = GetSubMenu(hMenuBar, i);
            if (!hPopup) continue;
            int n = GetMenuItemCount(hPopup);
            for (int j = 0; j < n && !m_hMruMenu; ++j) {
                HMENU hSub = GetSubMenu(hPopup, j);
                if (!hSub) continue;
                int subCount = GetMenuItemCount(hSub);
                for (int k = 0; k < subCount; ++k) {
                    if (GetMenuItemID(hSub, k) == IDM_FILE_MRU_PH) {
                        m_hMruMenu = hSub;
                        break;
                    }
                }
            }
        }
    }
    RebuildMruMenu();
}

void MainWindow::CreateControls(HWND hwnd) {
    HINSTANCE hInst = App::Instance().GetInstance();

    // Toolbar
    m_hToolbar = CreateWindowExW(0, TOOLBARCLASSNAME, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER | CCS_NORESIZE,
        0, 0, 0, kToolbarH, hwnd, nullptr, hInst, nullptr);
    SendMessageW(m_hToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(m_hToolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(32, 32));
    SendMessageW(m_hToolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(36, 36));
    SendMessageW(m_hToolbar, TB_SETPADDING,    0, MAKELPARAM(2, 2));

    // Load toolbar bitmaps
    TBADDBITMAP ab = {};
    ab.hInst = hInst;
    
    ab.nID = IDB_TOOLBAR_EXTRACT;
    int idxExtract = (int)SendMessageW(m_hToolbar, TB_ADDBITMAP, 1, (LPARAM)&ab);
    
    ab.nID = IDB_TOOLBAR_OPEN;
    int idxOpen = (int)SendMessageW(m_hToolbar, TB_ADDBITMAP, 1, (LPARAM)&ab);
    
    ab.nID = IDB_TOOLBAR_ADD;
    int idxAdd = (int)SendMessageW(m_hToolbar, TB_ADDBITMAP, 1, (LPARAM)&ab);
    
    ab.nID = IDB_TOOLBAR_INFO;
    int idxInfo = (int)SendMessageW(m_hToolbar, TB_ADDBITMAP, 1, (LPARAM)&ab);
    
    ab.nID = IDB_TOOLBAR_SETTINGS;
    int idxSettings = (int)SendMessageW(m_hToolbar, TB_ADDBITMAP, 1, (LPARAM)&ab);

    ab.nID = IDB_TOOLBAR_TEST;
    int idxTest = (int)SendMessageW(m_hToolbar, TB_ADDBITMAP, 1, (LPARAM)&ab);

    TBBUTTON btns[] = {
        {idxExtract, ID_EXTRACT,      TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
        {idxOpen,    ID_OPEN_ASSOC,   TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
        {idxAdd,     ID_ADD,          TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
        {idxInfo,    ID_INFO,         TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
        {idxTest,    ID_TEST,         TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
        {0,          0,               0,               BTNS_SEP,    {}, 0, 0},
        {idxSettings, ID_SETTINGS_DLG, TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
    };
    SendMessageW(m_hToolbar, TB_ADDBUTTONS, _countof(btns), (LPARAM)btns);
    SendMessageW(m_hToolbar, TB_AUTOSIZE, 0, 0);

    // Hide immediately at startup if hidden in settings
    if (!m_toolbarVisible)
        ShowWindow(m_hToolbar, SW_HIDE);

    // Status bar
    m_hStatus = CreateWindowExW(0, STATUSCLASSNAME, L"",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, hwnd, nullptr, hInst, nullptr);

    // TreeView (left pane). WS_VISIBLE is not added if hidden in settings.
    DWORD treeStyle = WS_CHILD | WS_TABSTOP | TVS_HASLINES | TVS_LINESATROOT |
                      TVS_HASBUTTONS | TVS_SHOWSELALWAYS;
    if (m_treeVisible) treeStyle |= WS_VISIBLE;
    m_hTreeView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEW, nullptr,
        treeStyle,
        0, 0, 0, 0, hwnd, nullptr, hInst, nullptr);

    // ListView (right pane)
    m_hListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, hwnd, nullptr, hInst, nullptr);
    ListView_SetExtendedListViewStyle(m_hListView,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_HEADERDRAGDROP);

    // ListView columns
    struct ColDef { UINT nameId; int width; };
    const ColDef cols[] = {
        {IDS_COL_NAME,     220},
        {IDS_COL_SIZE,     90},
        {IDS_COL_PACKED,   90},
        {IDS_COL_TYPE,     80},
        {IDS_COL_MODIFIED, 140},
    };
    for (int i = 0; i < (int)_countof(cols); ++i) {
        std::wstring name = I18n::Tr(cols[i].nameId);
        LVCOLUMNW lvc = {};
        lvc.mask    = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        lvc.fmt     = (i == 0) ? LVCFMT_LEFT : LVCFMT_RIGHT;
        lvc.cx      = cols[i].width;
        lvc.pszText = name.data();
        ListView_InsertColumn(m_hListView, i, &lvc);
    }

    // Get system image list (small icons)
    SHFILEINFOW sfi = {};
    m_hSysImageList = (HIMAGELIST)SHGetFileInfoW(L"C:\\", 0, &sfi, sizeof(sfi),
                                                  SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
    if (m_hSysImageList) {
        TreeView_SetImageList(m_hTreeView, m_hSysImageList, TVSIL_NORMAL);
        ListView_SetImageList(m_hListView, m_hSysImageList, LVSIL_SMALL);
    }
}

void MainWindow::OnSize(int cx, int cy) {
    ResizePanes(cx, cy);
}

void MainWindow::ResizePanes(int cx, int cy) {
    if (!m_hToolbar) return;

    // Toolbar
    int tbH = 0;
    if (m_toolbarVisible) {
        SendMessageW(m_hToolbar, TB_AUTOSIZE, 0, 0);
        RECT rcTB = {};
        GetWindowRect(m_hToolbar, &rcTB);
        tbH = rcTB.bottom - rcTB.top;
        SetWindowPos(m_hToolbar, nullptr, 0, 0, cx, tbH, SWP_NOZORDER);
    }

    // Status bar
    SetWindowPos(m_hStatus, nullptr, 0, cy - kStatusH, cx, kStatusH, SWP_NOZORDER);

    int contentTop = tbH;
    int contentH   = cy - tbH - kStatusH;
    if (contentH < 0) contentH = 0;

    if (m_treeVisible) {
        // TreeView (left)
        SetWindowPos(m_hTreeView, nullptr, 0, contentTop, m_treeWidth, contentH, SWP_NOZORDER);

        // ListView (right)
        int lvX = m_treeWidth + kSplitterW;
        SetWindowPos(m_hListView, nullptr, lvX, contentTop, cx - lvX, contentH, SWP_NOZORDER);
    } else {
        // When tree is hidden, ListView takes full width. Tree itself assumed SW_HIDE'd.
        SetWindowPos(m_hListView, nullptr, 0, contentTop, cx, contentH, SWP_NOZORDER);
    }
}

// ---- Drag-and-drop ----

void MainWindow::OnDropFiles(HDROP hDrop) {
    UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::wstring> archives, regular;

    for (UINT i = 0; i < count; ++i) {
        UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
        std::wstring path(len, L'\0');
        DragQueryFileW(hDrop, i, path.data(), len + 1);

        const wchar_t* dot = wcsrchr(path.c_str(), L'.');
        bool isArchive = false;
        if (dot) {
            auto& sz7 = App::Instance().Get7z();
            isArchive = sz7.IsLoaded() && sz7.IsArchiveExt(dot + 1);
        }
        (isArchive ? archives : regular).push_back(std::move(path));
    }
    DragFinish(hDrop);

    if (!archives.empty()) {
        OpenArchive(archives[0].c_str()); // open first archive
    } else if (!regular.empty()) {
        // If archive currently open and writable, let user choose add vs. create new
        bool canAdd = !m_archivePath.empty() && !m_isReadOnly;
        bool addToCurrent = false;
        if (canAdd) {
            // Show only 1-2 filenames for specificity
            std::wstring sample;
            for (size_t i = 0; i < regular.size() && i < 2; ++i) {
                auto leaf = regular[i];
                auto sl = leaf.find_last_of(L"\\/");
                if (sl != std::wstring::npos) leaf = leaf.substr(sl + 1);
                sample += L"  " + leaf + L"\n";
            }
            if (regular.size() > 2) sample += I18n::Tr(IDS_DND_ELLIPSIS);

            wchar_t arcLeaf[MAX_PATH];
            {
                std::wstring a = m_archivePath;
                auto sl = a.find_last_of(L"\\/");
                wcscpy_s(arcLeaf, (sl != std::wstring::npos) ? a.substr(sl + 1).c_str() : a.c_str());
            }
            std::wstring msg = I18n::TrFmt(IDS_FMT_DND_PROMPT, sample.c_str(), arcLeaf);
            int r = MessageBoxW(m_hwnd, msg.c_str(), I18n::Tr(IDS_APP_TITLE).c_str(),
                                MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1);
            if (r == IDCANCEL) return;
            addToCurrent = (r == IDYES);
        }

        if (addToCurrent) {
            AddFilesToCurrentArchive(std::move(regular));
        } else {
            CompressDlg::Params params;
            params.inputFiles = std::move(regular);
            params.LoadFromSettings(App::Instance().GetSettings());

            CompressDlg dlg;
            auto& sz7 = App::Instance().Get7z();
            const auto* enc = sz7.IsLoaded() ? &sz7.GetEncoderNames() : nullptr;
            const auto* wf  = sz7.IsLoaded() ? &sz7.GetWritableFormats() : nullptr;
            if (dlg.Show(m_hwnd, params, enc, wf)) {
                auto& s = App::Instance().GetSettings();
                params.SaveToSettings(s);
                s.Save();
                OnCompress(params);
            }
        }
    }
}

// ---- Commands ----

void MainWindow::OnCommand(WORD id) {
    switch (id) {
    case ID_EXTRACT:
        OnExtract();
        break;
    case ID_EXTRACT_SELECTED:
        OnExtractSelected();
        break;
    case ID_OPEN_ASSOC:
        OnOpenAssoc();
        break;
    case ID_ADD:
        OnAddFiles();
        break;
    case ID_ADD_TO_CURRENT:
        OnAddFilesToCurrentArchive();
        break;
    case ID_TEST:
        OnTest();
        break;
    case ID_INFO:
        OnInfo();
        break;
    case ID_ARCHIVE_COMMENT:
        OnArchiveComment();
        break;
    case ID_DELETE:
        OnDelete();
        break;
    case ID_SETTINGS_DLG: {
        SettingsDlg dlg;
        dlg.Show(m_hwnd);
        break;
    }
    case ID_CLOSE:
        CloseArchive();
        break;
    case IDM_FILE_OPEN:
        OnFileOpen();
        break;
    case IDM_FILE_PROPERTIES:
        OnArchiveProperties();
        break;
    case IDM_FILE_EXIT:
        DestroyWindow(m_hwnd);
        break;
    case IDM_VIEW_TREE:
        OnToggleTree();
        break;
    case IDM_VIEW_TOOLBAR:
        OnToggleToolbar();
        break;
    case IDM_HELP_ABOUT:
        OnAbout();
        break;
    default:
        if (id >= IDM_FILE_MRU_BASE && id <= IDM_FILE_MRU_LAST)
            OnMruOpen(id - IDM_FILE_MRU_BASE);
        break;
    }
}

void MainWindow::OnTreeSelChanged() {
    std::wstring folder = SelectedFolderPath();
    PopulateList(folder);
}

void MainWindow::OnListDblClick() {
    int sel = ListView_GetNextItem(m_hListView, -1, LVNI_SELECTED);
    if (sel < 0) return;

    LVITEMW lvi = {};
    lvi.iItem = sel;
    lvi.mask  = LVIF_PARAM;
    ListView_GetItem(m_hListView, &lvi);
    UINT32 arcIdx = (UINT32)lvi.lParam;

    // Handle ".." (parent directory)
    if (arcIdx == UINT32_MAX) {
        if (m_currentFolderPath.empty()) return;
        
        // Find parent folder path
        size_t lastSlash = m_currentFolderPath.rfind(L'/');
        std::wstring parentPath = (lastSlash != std::wstring::npos) ? 
            m_currentFolderPath.substr(0, lastSlash) : L"";
        
        // Find parent folder in m_folderPaths and navigate
        for (int i = 0; i < (int)m_folderPaths.size(); ++i) {
            if (m_folderPaths[i] == parentPath) {
                // Navigate via TreeView (same as folder navigation)
                std::function<HTREEITEM(HTREEITEM)> findItem = [&](HTREEITEM h) -> HTREEITEM {
                    while (h) {
                        TVITEMW tvi = {}; tvi.hItem = h; tvi.mask = TVIF_PARAM;
                        TreeView_GetItem(m_hTreeView, &tvi);
                        if ((int)tvi.lParam == i) return h;
                        if (HTREEITEM child = TreeView_GetChild(m_hTreeView, h)) {
                            if (HTREEITEM found = findItem(child)) return found;
                        }
                        h = TreeView_GetNextSibling(m_hTreeView, h);
                    }
                    return nullptr;
                };
                HTREEITEM hRoot = TreeView_GetRoot(m_hTreeView);
                HTREEITEM hFound = findItem(hRoot);
                if (hFound) {
                    TreeView_EnsureVisible(m_hTreeView, hFound);
                    TreeView_SelectItem(m_hTreeView, hFound);
                }
                break;
            }
        }
        return;
    }

    // Helper to resolve folder path index and use for tree selection
    auto navigateToFolderIndex = [&](int fpIdx) {
        std::function<HTREEITEM(HTREEITEM)> findItem = [&](HTREEITEM h) -> HTREEITEM {
            while (h) {
                TVITEMW tvi2 = {}; tvi2.hItem = h; tvi2.mask = TVIF_PARAM;
                TreeView_GetItem(m_hTreeView, &tvi2);
                if ((int)tvi2.lParam == fpIdx) return h;
                if (HTREEITEM child = TreeView_GetChild(m_hTreeView, h)) {
                    if (HTREEITEM found = findItem(child)) return found;
                }
                h = TreeView_GetNextSibling(m_hTreeView, h);
            }
            return nullptr;
        };
        HTREEITEM hRoot  = TreeView_GetRoot(m_hTreeView);
        HTREEITEM hFound = findItem(hRoot);
        if (hFound) {
            TreeView_EnsureVisible(m_hTreeView, hFound);
            TreeView_SelectItem(m_hTreeView, hFound);
        }
    };

    if (arcIdx < (UINT32)m_items.size() && m_items[arcIdx].isDir) {
        // Folder with actual entry in m_items
        const std::wstring& targetPath = m_items[arcIdx].path;
        for (int i = 0; i < (int)m_folderPaths.size(); ++i) {
            if (m_folderPaths[i] == targetPath) {
                navigateToFolderIndex(i);
                break;
            }
        }
    } else if (arcIdx >= (UINT32)m_items.size()) {
        // Virtual folder (entries omitted by unrar.dll etc.)
        int fpIdx = (int)(arcIdx - (UINT32)m_items.size());
        if (fpIdx < (int)m_folderPaths.size())
            navigateToFolderIndex(fpIdx);
    } else {
        // File → open extract dialog
        OnExtract();
    }
}

void MainWindow::OnOpenAssoc() {
    if (m_archivePath.empty()) return;
    if (m_openedWithUnrar) {
        MessageBoxW(m_hwnd, I18n::Tr(IDS_VIEW_REQUIRES_7Z).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION);
        return;
    }

    App& app = App::Instance();
    if (!Ensure7zLoaded()) return;
    int sel = ListView_GetNextItem(m_hListView, -1, LVNI_SELECTED);
    if (sel < 0) {
        MessageBoxW(m_hwnd, I18n::Tr(IDS_INFO_SELECT_FILE).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION);
        return;
    }

    LVITEMW lvi = {};
    lvi.iItem = sel;
    lvi.mask  = LVIF_PARAM;
    ListView_GetItem(m_hListView, &lvi);
    UINT32 idx = (UINT32)lvi.lParam;
    if (idx >= (UINT32)m_items.size()) return;

    const ArchiveItem& it = m_items[idx];
    if (it.isDir) {
        MessageBoxW(m_hwnd, I18n::Tr(IDS_INFO_FOLDERS_NOT_VIEWABLE).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION);
        return;
    }

    // Create a session-unique temp dir on first use (deleted on exit)
    if (m_tempViewDir.empty()) {
        wchar_t base[MAX_PATH] = {}, buf[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, base);
        GetTempFileNameW(base, L"aex", 0, buf);
        DeleteFileW(buf);  // GetTempFileName creates a file; we want a dir
        m_tempViewDir = std::wstring(buf) + L"\\";
        SHCreateDirectoryExW(nullptr, m_tempViewDir.c_str(), nullptr);
    }
    const std::wstring& tempDir = m_tempViewDir;

    // Extract single file to temp dir
    std::vector<UINT32> indices = { idx };
    HRESULT hr = app.Get7z().Extract(m_effectiveArchivePath.c_str(), indices,
                                      tempDir.c_str(), nullptr, nullptr);
    if (FAILED(hr)) {
        ShowError(I18n::Tr(IDS_ERR_EXTRACT_FILE_FAILED).c_str(), hr);
        return;
    }

    // Build local path (archive path uses '/', convert to '\')
    std::wstring relPath = it.path;
    for (auto& c : relPath) if (c == L'/') c = L'\\';
    std::wstring localPath = tempDir + relPath;

    // Open with associated application
    HINSTANCE hi = ShellExecuteW(m_hwnd, L"open", localPath.c_str(),
                                  nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)hi <= 32) {
        MessageBoxW(m_hwnd,
                    I18n::TrFmt(IDS_FMT_NO_ASSOC_APP, localPath.c_str()).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONWARNING);
    }
}

void MainWindow::OnExtract() {
    if (m_archivePath.empty()) return;
    RunExtraction({}, {});
}

void MainWindow::OnExtractSelected() {
    if (m_archivePath.empty()) return;

    // Resolve lParam to real archive index.
    // - lParam < m_items.size()  : real entry (directory → extract contents too)
    // - lParam >= m_items.size() : virtual folder (extract m_folderPaths contents)
    std::set<UINT32> indexSet;
    int item = -1;
    while ((item = ListView_GetNextItem(m_hListView, item, LVNI_SELECTED)) != -1) {
        LVITEMW lvi = {};
        lvi.iItem = item;
        lvi.mask  = LVIF_PARAM;
        ListView_GetItem(m_hListView, &lvi);
        UINT32 lp = (UINT32)lvi.lParam;

        std::wstring folder;
        if (lp < (UINT32)m_items.size()) {
            indexSet.insert(lp);
            if (m_items[lp].isDir) folder = m_items[lp].path;
        } else {
            int fpIdx = (int)(lp - (UINT32)m_items.size());
            if (fpIdx >= 0 && fpIdx < (int)m_folderPaths.size())
                folder = m_folderPaths[fpIdx];
        }
        if (!folder.empty()) {
            std::wstring prefix = folder + L"\\";
            for (UINT32 j = 0; j < (UINT32)m_items.size(); ++j) {
                if (m_items[j].path.size() >= prefix.size() &&
                    m_items[j].path.compare(0, prefix.size(), prefix) == 0)
                    indexSet.insert(j);
            }
        }
    }
    if (indexSet.empty()) {
        MessageBoxW(m_hwnd, I18n::Tr(IDS_INFO_NO_FILES_SELECTED).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION);
        return;
    }

    std::vector<UINT32> indices(indexSet.begin(), indexSet.end());
    std::set<std::wstring> rarTargetPaths;
    for (UINT32 idx : indices) rarTargetPaths.insert(m_items[idx].path);

    RunExtraction(std::move(indices), std::move(rarTargetPaths));
}

void MainWindow::RunExtraction(std::vector<UINT32> indices, std::set<std::wstring> rarTargetPaths) {
    App& app = App::Instance();
    bool useUnrar = m_openedWithUnrar;

    if (!Ensure7zLoaded(useUnrar)) return;

    wchar_t destDir[MAX_PATH] = {};
    if (!BrowseFolderDialog(m_hwnd, IDS_TITLE_SELECT_DEST_FOLDER, destDir, MAX_PATH)) return;

    // Evaluate MkDir policy based on full archive structure
    std::wstring finalDest = destDir;
    {
        int mkDir = app.GetSettings().GetMkDir();
        if (ShouldCreateSubfolder(mkDir, m_items))
            finalDest = std::wstring(destDir) + L"\\" + ArchiveBaseName(m_archivePath);
    }

    ProgressDlg progDlg;
    progDlg.Show(m_hwnd, I18n::Tr(IDS_PROGRESS_EXTRACTING).c_str());

    auto* sink = new ProgressPostSink(m_hwnd, WM_APP_PROGRESS, WM_APP_DONE);
    m_pSink    = sink;
    progDlg.SetSink(sink);

    auto archivePath = m_effectiveArchivePath;

    if (useUnrar) {
        auto& unrar = app.GetUnrar();
        if (rarTargetPaths.empty()) {
            m_worker.Start([&unrar, archivePath, destDir = finalDest, sink]() -> HRESULT {
                SHCreateDirectoryExW(nullptr, destDir.c_str(), nullptr);
                bool ok = unrar.ExtractArchive(archivePath.c_str(), destDir.c_str(), nullptr, sink);
                return ok ? S_OK : E_FAIL;
            }, m_hwnd, WM_APP_DONE);
        } else {
            m_worker.Start([&unrar, archivePath, destDir = finalDest, rarTargetPaths, sink]() -> HRESULT {
                SHCreateDirectoryExW(nullptr, destDir.c_str(), nullptr);
                bool ok = unrar.ExtractArchiveSelected(archivePath.c_str(), destDir.c_str(),
                                                       rarTargetPaths, nullptr, sink);
                return ok ? S_OK : E_FAIL;
            }, m_hwnd, WM_APP_DONE);
        }
    } else {
        auto& sz = app.Get7z();
        m_worker.Start([&sz, archivePath, indices, destDir = finalDest, sink]() -> HRESULT {
            return sz.Extract(archivePath.c_str(), indices, destDir.c_str(), nullptr, sink);
        }, m_hwnd, WM_APP_DONE);
    }

    HRESULT hrDone = progDlg.RunMessageLoop();
    m_worker.Wait();
    delete sink;
    m_pSink = nullptr;

    if (FAILED(hrDone) && hrDone != E_ABORT)
        ShowError(I18n::Tr(IDS_ERR_EXTRACT_FAILED).c_str(), hrDone);
}

void MainWindow::OnContextMenu(HWND /*hwndFrom*/, int x, int y) {
    if (m_archivePath.empty()) return;

    bool readOnly = m_openedWithUnrar || m_isReadOnly;
    int selCount  = ListView_GetSelectedCount(m_hListView);

    HMENU hMenu = CreatePopupMenu();
    std::wstring sExtractSel = I18n::Tr(IDS_CTX_EXTRACT_SELECTED);
    std::wstring sOpenAssoc  = I18n::Tr(IDS_CTX_OPEN_ASSOC);
    std::wstring sTest       = I18n::Tr(IDS_CTX_TEST);
    std::wstring sInfo       = I18n::Tr(IDS_CTX_INFO);
    std::wstring sDelete     = I18n::Tr(IDS_CTX_DELETE);
    AppendMenuW(hMenu, MF_STRING | (selCount > 0 ? MF_ENABLED : MF_GRAYED),
                ID_EXTRACT_SELECTED, sExtractSel.c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING | (!readOnly && selCount > 0 ? MF_ENABLED : MF_GRAYED),
                ID_OPEN_ASSOC, sOpenAssoc.c_str());
    AppendMenuW(hMenu, MF_STRING | MF_ENABLED, ID_TEST, sTest.c_str());
    AppendMenuW(hMenu, MF_STRING | (selCount > 0 ? MF_ENABLED : MF_GRAYED),
                ID_INFO, sInfo.c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING | (!readOnly && selCount > 0 ? MF_ENABLED : MF_GRAYED),
                ID_DELETE, sDelete.c_str());

    // When called from keyboard (x==-1, y==-1), use cursor position
    if (x == -1 && y == -1) {
        POINT pt = {};
        GetCursorPos(&pt);
        x = pt.x; y = pt.y;
    }

    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, x, y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);
}


void MainWindow::OnTest() {
    if (m_archivePath.empty()) {
        MessageBoxW(m_hwnd, I18n::Tr(IDS_INFO_NO_ARCHIVE_TO_TEST).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION);
        return;
    }

    App& app = App::Instance();
    bool useUnrar = m_openedWithUnrar;
    if (!Ensure7zLoaded(useUnrar)) return;

    ProgressDlg progDlg;
    progDlg.Show(m_hwnd, I18n::Tr(IDS_PROGRESS_TESTING).c_str());

    auto* sink = new ProgressPostSink(m_hwnd, WM_APP_PROGRESS, WM_APP_DONE);
    m_pSink    = sink;
    progDlg.SetSink(sink);

    auto archivePath = m_effectiveArchivePath;

    if (useUnrar) {
        auto& unrar = app.GetUnrar();
        m_worker.Start([&unrar, archivePath, sink]() -> HRESULT {
            return unrar.TestArchive(archivePath.c_str(), nullptr, sink) ? S_OK : E_FAIL;
        }, m_hwnd, WM_APP_DONE);
    } else {
        auto& sz = app.Get7z();
        m_worker.Start([&sz, archivePath, sink]() -> HRESULT {
            return sz.Test(archivePath.c_str(), nullptr, sink);
        }, m_hwnd, WM_APP_DONE);
    }

    HRESULT hrDone = progDlg.RunMessageLoop();
    m_worker.Wait();
    // unrar.dll's TestArchive returns false (= E_FAIL) even on cancel,
    // so check sink's cancel flag and normalize to E_ABORT equivalent.
    bool wasCancelled = sink->IsCancelled();
    delete sink;
    m_pSink = nullptr;

    if (hrDone == E_ABORT || wasCancelled) {
        // Silent on cancel
    } else if (FAILED(hrDone)) {
        ShowError(I18n::Tr(IDS_TEST_FAILED).c_str(), hrDone);
    } else {
        MessageBoxW(m_hwnd, I18n::Tr(IDS_TEST_OK).c_str(),
                    I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION);
    }
}

void MainWindow::OnFileOpen() {
    IFileOpenDialog* pfd = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&pfd))))
        return;

    // IDS_FILTER_ARCHIVE / IDS_FILTER_ALL_FILES stored as "label|pattern|" for OFN,
    // split by '|' and repass to COMDLG_FILTERSPEC.
    auto split = [](const std::wstring& s, std::wstring& a, std::wstring& b) {
        auto p = s.find(L'|');
        if (p == std::wstring::npos) { a = s; b.clear(); return; }
        a = s.substr(0, p);
        auto e = s.find(L'|', p + 1);
        b = (e == std::wstring::npos) ? s.substr(p + 1) : s.substr(p + 1, e - p - 1);
    };
    std::wstring archiveLabel, archivePat, allLabel, allPat;
    split(I18n::Tr(IDS_FILTER_ARCHIVE),   archiveLabel, archivePat);
    split(I18n::Tr(IDS_FILTER_ALL_FILES), allLabel,     allPat);
    COMDLG_FILTERSPEC filter[] = {
        { archiveLabel.c_str(), archivePat.c_str() },
        { allLabel.c_str(),     allPat.c_str()     },
    };
    pfd->SetFileTypes((UINT)_countof(filter), filter);
    pfd->SetTitle(I18n::Tr(IDS_TITLE_OPEN_ARCHIVE).c_str());

    if (SUCCEEDED(pfd->Show(m_hwnd))) {
        IShellItem* psi = nullptr;
        if (SUCCEEDED(pfd->GetResult(&psi))) {
            PWSTR psz = nullptr;
            if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                OpenArchive(psz);
                CoTaskMemFree(psz);
            }
            psi->Release();
        }
    }
    pfd->Release();
}

// Extract VS_VERSION_INFO FileVersion string from file.
// Returns empty string if unavailable.
static std::wstring GetFileVersionString(const wchar_t* path) {
    if (!path || !path[0]) return {};
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &handle);
    if (!size) return {};
    std::vector<BYTE> buf(size);
    if (!GetFileVersionInfoW(path, handle, size, buf.data())) return {};

    // Get language code from translation table and query StringFileInfo\xxxx\FileVersion.
    // Most third-party DLLs/EXEs store a display string like "26.00ZSv1.5.7R1".
    struct LangCp { WORD lang; WORD cp; };
    LangCp* trans = nullptr;
    UINT len = 0;
    if (VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation",
                       (void**)&trans, &len) && trans && len >= sizeof(LangCp)) {
        wchar_t key[80];
        swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileVersion",
                   trans[0].lang, trans[0].cp);
        wchar_t* val = nullptr;
        UINT vlen = 0;
        if (VerQueryValueW(buf.data(), key, (void**)&val, &vlen) && val && vlen > 0) {
            std::wstring s = val;
            // Trim trailing control characters and spaces
            while (!s.empty() && (s.back() == L' ' || s.back() == L'\0')) s.pop_back();
            if (!s.empty()) return s;
        }
    }

    // Fallback: numeric fields from VS_FIXEDFILEINFO
    VS_FIXEDFILEINFO* ffi = nullptr;
    if (VerQueryValueW(buf.data(), L"\\", (void**)&ffi, &len) && ffi) {
        wchar_t out[64];
        swprintf_s(out, L"%u.%u.%u.%u",
                   HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
                   HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
        return out;
    }
    return {};
}

// Extract the leaf name from a path (backslash-separated)
static std::wstring LeafName(const std::wstring& path) {
    auto pos = path.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? path : path.substr(pos + 1);
}

static INT_PTR CALLBACK AboutDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM /*lp*/) {
    if (msg == WM_INITDIALOG) {
        App& app = App::Instance();

        struct Entry { std::wstring name; std::wstring path; };
        std::vector<Entry> entries;

        auto& sz = app.Get7z();
        if (sz.IsLoaded()) {
            std::wstring p = sz.GetLoadedPath();
            entries.push_back({ LeafName(p), p });
        }
        auto& ur = app.GetUnrar();
        if (ur.IsLoaded()) {
            std::wstring p = ur.GetLoadedPath();
            entries.push_back({ LeafName(p), p });
        }
        // RAR exe: if setting is empty, auto-detect (registry + known paths) as fallback,
        // so About dialog shows it even if Settings not configured.
        std::wstring rarExe = app.GetSettings().GetRarExePath();
        if (rarExe.empty() || !PathFileExistsW(rarExe.c_str()))
            rarExe = RarProcess::FindRarExe();
        if (!rarExe.empty() && PathFileExistsW(rarExe.c_str()))
            entries.push_back({ LeafName(rarExe), rarExe });

        // Get versions + align by max name column width
        size_t maxName = 0;
        std::vector<std::wstring> versions;
        versions.reserve(entries.size());
        for (auto& e : entries) {
            if (e.name.size() > maxName) maxName = e.name.size();
            versions.push_back(GetFileVersionString(e.path.c_str()));
        }

        std::wstring text;
        for (size_t i = 0; i < entries.size(); ++i) {
            text += entries[i].name;
            // Pad right of name column to align versions
            text.append(maxName + 2 - entries[i].name.size(), L' ');
            text += versions[i].empty() ? I18n::Tr(IDS_ABOUT_NO_VERSION) : versions[i];
            text += L"\r\n";
        }
        if (entries.empty())
            text = I18n::Tr(IDS_ABOUT_NOT_LOADED);

        SetDlgItemTextW(hwnd, IDC_ABOUT_LIST, text.c_str());

        // Use monospace font to align version display cleanly
        HFONT hMono = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        if (hMono) {
            SendDlgItemMessageW(hwnd, IDC_ABOUT_LIST, WM_SETFONT, (WPARAM)hMono, TRUE);
            // Free when dialog is destroyed
            SetPropW(hwnd, L"AboutMonoFont", hMono);
        }

        // Make title label slightly larger
        HFONT hTitle = CreateFontW(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                   L"Segoe UI");
        if (hTitle) {
            SendDlgItemMessageW(hwnd, IDC_ABOUT_TITLE, WM_SETFONT, (WPARAM)hTitle, TRUE);
            SetPropW(hwnd, L"AboutTitleFont", hTitle);
        }
        return TRUE;
    }
    if (msg == WM_COMMAND) {
        WORD id = LOWORD(wp);
        if (id == IDOK || id == IDCANCEL) {
            EndDialog(hwnd, id);
            return TRUE;
        }
    }
    if (msg == WM_DESTROY) {
        if (HFONT f = (HFONT)GetPropW(hwnd, L"AboutMonoFont"))  { DeleteObject(f); RemovePropW(hwnd, L"AboutMonoFont"); }
        if (HFONT f = (HFONT)GetPropW(hwnd, L"AboutTitleFont")) { DeleteObject(f); RemovePropW(hwnd, L"AboutTitleFont"); }
    }
    return FALSE;
}

void MainWindow::OnAbout() {
    DialogBoxParamW(GetModuleHandleW(nullptr),
                    MAKEINTRESOURCEW(IDD_ABOUT),
                    m_hwnd, AboutDlgProc, 0);
}

void MainWindow::OnMruOpen(int idx) {
    auto& settings = App::Instance().GetSettings();
    const auto& mru = settings.GetMruPaths();
    if (idx < 0 || idx >= (int)mru.size()) return;

    std::wstring path = mru[idx];   // Copy because OpenArchive's AddMru reorders it
    if (!PathFileExistsW(path.c_str())) {
        std::wstring msg = I18n::TrFmt(IDS_FMT_FILE_NOT_FOUND, path.c_str());
        MessageBoxW(m_hwnd, msg.c_str(), I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONWARNING);
        settings.RemoveMru(path);
        settings.Save();
        RebuildMruMenu();
        return;
    }
    OpenArchive(path.c_str());
}

void MainWindow::RebuildMruMenu() {
    if (!m_hMruMenu) return;

    // Delete all existing items
    while (DeleteMenu(m_hMruMenu, 0, MF_BYPOSITION)) {}

    const auto& mru = App::Instance().GetSettings().GetMruPaths();
    if (mru.empty()) {
        AppendMenuW(m_hMruMenu, MF_STRING | MF_GRAYED, IDM_FILE_MRU_PH,
                    I18n::Tr(IDS_MRU_NO_HISTORY).c_str());
    } else {
        for (size_t i = 0; i < mru.size(); ++i) {
            // First 9 show accelerators &1..&9. 10+ indent-aligned.
            wchar_t prefix[8];
            if (i < 9)
                swprintf_s(prefix, L"&%zu  ", i + 1);
            else
                swprintf_s(prefix, L"     ");
            // & is underlined in menus, so double-escape it
            std::wstring label = prefix;
            for (wchar_t c : mru[i]) {
                if (c == L'&') label += L"&&";
                else label += c;
            }
            AppendMenuW(m_hMruMenu, MF_STRING,
                        IDM_FILE_MRU_BASE + (UINT)i, label.c_str());
        }
    }
    DrawMenuBar(m_hwnd);
}

void MainWindow::OnToggleTree() {
    m_treeVisible = !m_treeVisible;
    if (m_hTreeView)
        ShowWindow(m_hTreeView, m_treeVisible ? SW_SHOW : SW_HIDE);
    RECT rc = {};
    GetClientRect(m_hwnd, &rc);
    ResizePanes(rc.right, rc.bottom);
}

void MainWindow::OnToggleToolbar() {
    m_toolbarVisible = !m_toolbarVisible;
    if (m_hToolbar)
        ShowWindow(m_hToolbar, m_toolbarVisible ? SW_SHOW : SW_HIDE);
    RECT rc = {};
    GetClientRect(m_hwnd, &rc);
    ResizePanes(rc.right, rc.bottom);
}

// Update enabled/disabled state just before the menu is shown. WM_INITMENUPOPUP fires per popup,
// so EnableMenuItem returns -1 without side effects when an ID is not in this popup.
// Safe to call for all commands every time.
void MainWindow::OnInitMenuPopup(HMENU hMenu) {
    bool hasArchive = !m_archivePath.empty();
    bool readOnly   = m_openedWithUnrar || m_isReadOnly;
    int  selCount   = m_hListView ? ListView_GetSelectedCount(m_hListView) : 0;

    auto setEnabled = [hMenu](UINT id, bool enabled) {
        EnableMenuItem(hMenu, id, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
    };

    setEnabled(ID_CLOSE,      hasArchive);
    setEnabled(ID_EXTRACT,    hasArchive);
    setEnabled(ID_TEST,       hasArchive);
    setEnabled(ID_OPEN_ASSOC, hasArchive && !readOnly);
    setEnabled(ID_INFO,       selCount > 0);
    setEnabled(IDM_FILE_PROPERTIES, hasArchive);
    setEnabled(ID_ARCHIVE_COMMENT, hasArchive);
    setEnabled(ID_ADD_TO_CURRENT, hasArchive && !m_isReadOnly);
    setEnabled(ID_DELETE,     hasArchive && !readOnly && selCount > 0);

    CheckMenuItem(hMenu, IDM_VIEW_TREE,
                  MF_BYCOMMAND | (m_treeVisible ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, IDM_VIEW_TOOLBAR,
                  MF_BYCOMMAND | (m_toolbarVisible ? MF_CHECKED : MF_UNCHECKED));
}

void MainWindow::CloseArchive() {
    if (m_archivePath.empty()) return;
    // Clean up any temporary file created by split auto-unwrap
    if (!m_effectiveArchivePath.empty() &&
        _wcsicmp(m_effectiveArchivePath.c_str(), m_archivePath.c_str()) != 0) {
        DeleteFileW(m_effectiveArchivePath.c_str());
    }
    m_archivePath.clear();
    m_effectiveArchivePath.clear();
    m_items.clear();
    m_folderPaths.clear();
    m_openedWithUnrar = false;
    m_isReadOnly = false;

    if (m_hTreeView) TreeView_DeleteAllItems(m_hTreeView);
    if (m_hListView) ListView_DeleteAllItems(m_hListView);

    SetWindowTextW(m_hwnd, L"AileEx");
    if (m_hStatus) SetWindowTextW(m_hStatus, L"");
}

void MainWindow::OnProgress(int pct, wchar_t* filename) {
    wchar_t status[512];
    swprintf_s(status, L"%d%%  %s", pct, filename ? filename : L"");
    SetWindowTextW(m_hStatus, status);
    free(filename);
}

void MainWindow::OnDone(HRESULT hr) {
    if (FAILED(hr) && hr != E_ABORT) {
        ShowError(I18n::Tr(IDS_OP_FAILED).c_str(), hr);
    }
    SetWindowTextW(m_hStatus, I18n::Tr(IDS_DONE).c_str());
}

// ---- Compress flow ----

void MainWindow::OnAddFiles() {
    auto files = BrowseMultipleFiles(m_hwnd, IDS_TITLE_SELECT_COMPRESS);
    if (files.empty()) return;

    CompressDlg::Params params;
    params.inputFiles = std::move(files);
    params.LoadFromSettings(App::Instance().GetSettings());

    CompressDlg dlg;
    auto& sz7 = App::Instance().Get7z();
    const auto* enc = sz7.IsLoaded() ? &sz7.GetEncoderNames() : nullptr;
    const auto* wf  = sz7.IsLoaded() ? &sz7.GetWritableFormats() : nullptr;
    if (dlg.Show(m_hwnd, params, enc, wf)) {
        auto& s = App::Instance().GetSettings();
        params.SaveToSettings(s);
        s.Save();
        OnCompress(params);
    }
}

// Open a file picker and add the selected files to the current archive.
void MainWindow::OnAddFilesToCurrentArchive() {
    if (m_archivePath.empty() || m_isReadOnly) return;

    auto files = BrowseMultipleFiles(m_hwnd, IDS_TITLE_SELECT_ADD);
    if (files.empty()) return;

    AddFilesToCurrentArchive(std::move(files));
}

// Worker-driven file addition to the archive. RAR uses rar.exe `a`; everything else uses 7z.dll AddToArchive.
void MainWindow::AddFilesToCurrentArchive(std::vector<std::wstring> srcPaths) {
    if (m_archivePath.empty() || m_isReadOnly || srcPaths.empty()) return;

    App& app = App::Instance();

    // The operative path (after split auto-unwrap temp file) is read-only,
    // so use m_archivePath directly here (already guarded by m_isReadOnly).
    const std::wstring archivePath = m_archivePath;
    const std::wstring archiveFolder = SelectedFolderPath();  // "" means archive root

    // RAR detection: extension is .rar, or the archive was opened via unrar.dll
    bool isRar = m_openedWithUnrar;
    if (!isRar) {
        const wchar_t* dot = wcsrchr(archivePath.c_str(), L'.');
        if (dot && _wcsicmp(dot + 1, L"rar") == 0) isRar = true;
    }

    ProgressDlg progDlg;
    progDlg.Show(m_hwnd, I18n::Tr(IDS_PROGRESS_ADDING).c_str());

    auto* sink = new ProgressPostSink(m_hwnd, WM_APP_PROGRESS, WM_APP_DONE);
    m_pSink = sink;
    progDlg.SetSink(sink);

    HRESULT hrDone = S_OK;

    if (isRar) {
        // RAR: add via rar.exe `a`. No SFX/split (targeting an existing archive).
        const auto& s = app.GetSettings();
        wchar_t levelBuf[2] = { (wchar_t)(L'0' + (s.GetRarLevel() & 7)), L'\0' };
        if (s.GetRarLevel() < 0 || s.GetRarLevel() > 5) levelBuf[0] = L'3';
        std::wstring rarExe = s.GetRarExePath();

        RarProcess rar;
        if (!rar.Add(archivePath.c_str(), srcPaths,
                     archiveFolder.empty() ? nullptr : archiveFolder.c_str(),
                     levelBuf,
                     rarExe.empty() ? nullptr : rarExe.c_str(),
                     nullptr, false,
                     m_hwnd, WM_APP_PROGRESS, WM_APP_DONE)) {
            progDlg.Dismiss();
            delete sink; m_pSink = nullptr;
            return;
        }
        hrDone = progDlg.RunMessageLoop([&]{ rar.Cancel(); });
    } else {
        // 7z/zip/tar etc.: SevenZip::AddToArchive
        if (!app.Get7z().IsLoaded()) {
            progDlg.Dismiss();
            delete sink; m_pSink = nullptr;
            ShowError(I18n::Tr(IDS_ERR_7Z_NOT_LOADED).c_str());
            return;
        }
        auto& sz = app.Get7z();

        int  level  = app.GetSettings().GetCompressionLevel();
        // Let the format decide the method (empty string → 7z.dll default)
        m_worker.Start([&sz, archivePath, srcPaths, archiveFolder, level, sink]() -> HRESULT {
            return sz.AddToArchive(archivePath.c_str(), srcPaths,
                                   archiveFolder.empty() ? nullptr : archiveFolder.c_str(),
                                   nullptr, level, L"", sink);
        }, m_hwnd, WM_APP_DONE);
        hrDone = progDlg.RunMessageLoop();
        m_worker.Wait();
    }

    delete sink;
    m_pSink = nullptr;

    if (FAILED(hrDone) && hrDone != E_ABORT) {
        ShowError(I18n::Tr(IDS_ERR_ADD_FAILED).c_str(), hrDone);
        return;
    }
    if (hrDone == E_ABORT) return;

    // Success → reload the archive and reselect the target folder
    OpenArchive(archivePath.c_str());
    if (!archiveFolder.empty()) SelectTreeFolder(archiveFolder);
}

void MainWindow::OnInfo() {
    int sel = ListView_GetNextItem(m_hListView, -1, LVNI_SELECTED);
    if (sel < 0) return;

    LVITEMW lvi = {};
    lvi.iItem = sel;
    lvi.mask  = LVIF_PARAM;
    ListView_GetItem(m_hListView, &lvi);
    UINT32 arcIdx = (UINT32)lvi.lParam;
    if (arcIdx >= (UINT32)m_items.size()) return;

    InfoDlg dlg;
    dlg.Show(m_hwnd, m_items[arcIdx]);
}

void MainWindow::OnArchiveProperties() {
    if (m_archivePath.empty()) return;

    // Pass the operative path (temp file after split auto-unwrap) to 7z.dll if present.
    const std::wstring& target = m_effectiveArchivePath.empty()
                                 ? m_archivePath
                                 : m_effectiveArchivePath;

    ArchiveProperties props;
    bool haveProps = false;

    // Archives opened via unrar.dll are unlikely to be readable by 7z.dll (e.g. dll without RAR support).
    // Try anyway; if it fails, fall back to displaying info from items.
    auto& sz7 = App::Instance().Get7z();
    if (sz7.IsLoaded()) {
        HRESULT hr = sz7.GetArchiveProperties(target.c_str(), nullptr, props);
        if (SUCCEEDED(hr)) haveProps = true;
    }

    const wchar_t* fallback = m_openedWithUnrar ? L"RAR" : L"";
    PropertiesDlg dlg;
    dlg.Show(m_hwnd, m_archivePath, m_items,
             haveProps ? &props : nullptr, fallback);
}

void MainWindow::OnArchiveComment() {
    if (m_archivePath.empty()) return;

    const std::wstring& target = m_effectiveArchivePath.empty()
                                 ? m_archivePath
                                 : m_effectiveArchivePath;

    std::wstring comment;
    App& app = App::Instance();

    if (m_openedWithUnrar && app.GetUnrar().IsLoaded()) {
        app.GetUnrar().GetArchiveComment(target.c_str(), comment);
    }
    if (comment.empty() && app.Get7z().IsLoaded()) {
        app.Get7z().GetArchiveComment(target.c_str(), nullptr, comment);
    }

    // Editability check: temp files from split auto-unwrap are read-only.
    // ZIP can be edited directly without 7z.dll. RAR requires rar.exe. Others (7z/tar/iso etc.) are not editable.
    const wchar_t* dot = wcsrchr(m_archivePath.c_str(), L'.');
    std::wstring ext = dot ? std::wstring(dot + 1) : L"";
    for (auto& c : ext) c = (wchar_t)towlower(c);

    bool isZip = (ext == L"zip");
    bool isRar = (ext == L"rar" || m_openedWithUnrar);

    bool readOnly = m_isReadOnly || (!isZip && !isRar);

    std::wstring leaf = m_archivePath;
    auto sl = leaf.find_last_of(L"\\/");
    if (sl != std::wstring::npos) leaf = leaf.substr(sl + 1);

    std::wstring edited;
    CommentDlg dlg;
    if (!dlg.Show(m_hwnd, leaf, comment, readOnly, edited)) return;

    // Save processing
    HRESULT hrSave = E_FAIL;
    if (isZip) {
        if (app.Get7z().IsLoaded()) {
            hrSave = app.Get7z().SetZipArchiveComment(m_archivePath.c_str(), edited);
        } else {
            ShowError(I18n::Tr(IDS_ERR_ZIP_COMMENT_NEEDS_7Z).c_str());
            return;
        }
    } else if (isRar) {
        // RAR: apply via rar.exe c -z. Wait for completion via WM_APP_DONE.
        ProgressDlg progDlg;
        progDlg.Show(m_hwnd, I18n::Tr(IDS_PROGRESS_SAVING_COMMENT).c_str());
        auto* sink = new ProgressPostSink(m_hwnd, WM_APP_PROGRESS, WM_APP_DONE);
        m_pSink = sink;
        progDlg.SetSink(sink);

        const std::wstring rarExe = app.GetSettings().GetRarExePath();
        RarProcess rar;
        if (!rar.SetComment(m_archivePath.c_str(), edited,
                            rarExe.empty() ? nullptr : rarExe.c_str(),
                            m_hwnd, WM_APP_DONE)) {
            progDlg.Dismiss();
            delete sink; m_pSink = nullptr;
            ShowError(I18n::Tr(IDS_ERR_RAR_LAUNCH).c_str());
            return;
        }
        hrSave = progDlg.RunMessageLoop([&]{ rar.Cancel(); });
        delete sink; m_pSink = nullptr;
    }

    if (FAILED(hrSave) && hrSave != E_ABORT) {
        ShowError(I18n::Tr(IDS_ERR_COMMENT_SAVE_FAILED).c_str(), hrSave);
        return;
    }
    if (hrSave == E_ABORT) return;

    // Success → reopen the archive (OpenArchive resets tree selection to root,
    //           but comment editing is folder-position-independent so that is fine)
    OpenArchive(m_archivePath.c_str());
}

void MainWindow::OnDelete() {
    if (m_archivePath.empty() || m_openedWithUnrar || m_isReadOnly) return;

    // Resolve the ListView selection to a real entry set.
    //  - Real entry (lParam < m_items.size()): use that index.
    //    If a folder, also add all entries below it ("path/" prefix match).
    //  - Virtual folder (lParam >= m_items.size()): resolve children from m_folderPaths.
    std::set<UINT32> indexSet;
    std::set<std::wstring> folderPaths;  // display paths used in the RAR path
    int item = -1;
    while ((item = ListView_GetNextItem(m_hListView, item, LVNI_SELECTED)) != -1) {
        LVITEMW lvi = {};
        lvi.iItem = item;
        lvi.mask  = LVIF_PARAM;
        ListView_GetItem(m_hListView, &lvi);
        UINT32 lp = (UINT32)lvi.lParam;

        std::wstring folder;
        if (lp < (UINT32)m_items.size()) {
            indexSet.insert(lp);
            const auto& it = m_items[lp];
            if (it.isDir) folder = it.path;
            else          folderPaths.insert(it.path);
        } else {
            int fpIdx = (int)(lp - (UINT32)m_items.size());
            if (fpIdx >= 0 && fpIdx < (int)m_folderPaths.size())
                folder = m_folderPaths[fpIdx];
        }

        if (!folder.empty()) {
            folderPaths.insert(folder);
            std::wstring prefix = folder + L"\\";
            for (UINT32 j = 0; j < (UINT32)m_items.size(); ++j) {
                if (m_items[j].path.size() > prefix.size() &&
                    m_items[j].path.compare(0, prefix.size(), prefix) == 0) {
                    indexSet.insert(j);
                }
            }
        }
    }
    if (indexSet.empty() && folderPaths.empty()) return;

    // Confirm — show the original ListView selection count (more intuitive than the expanded count)
    int origCount = ListView_GetSelectedCount(m_hListView);
    std::wstring msg = I18n::TrFmt(IDS_FMT_DELETE_CONFIRM, origCount);
    if (MessageBoxW(m_hwnd, msg.c_str(), I18n::Tr(IDS_TITLE_DELETE_CONFIRM).c_str(),
                    MB_YESNO | MB_ICONWARNING) != IDYES)
        return;

    App& app = App::Instance();
    const wchar_t* dot = wcsrchr(m_archivePath.c_str(), L'.');
    bool isRar = (dot && _wcsicmp(dot + 1, L"rar") == 0);

    ProgressDlg progDlg;
    progDlg.Show(m_hwnd, I18n::Tr(IDS_PROGRESS_DELETING).c_str());

    auto* sink = new ProgressPostSink(m_hwnd, WM_APP_PROGRESS, WM_APP_DONE);
    m_pSink = sink;
    progDlg.SetSink(sink);

    HRESULT hrDone = S_OK;
    auto archivePath = m_effectiveArchivePath;

    if (isRar) {
        // RAR: delete including folders via rar.exe d -y -r
        std::vector<std::wstring> rarPaths;
        rarPaths.reserve(folderPaths.size());
        for (const auto& p : folderPaths) {
            std::wstring bs = p;
            for (auto& c : bs) if (c == L'/') c = L'\\';
            rarPaths.push_back(std::move(bs));
        }
        RarProcess proc;
        bool ok = proc.Delete(archivePath.c_str(), rarPaths,
                              app.GetSettings().GetRarExePath().c_str(),
                              m_hwnd, WM_APP_DONE);
        if (!ok) {
            progDlg.Dismiss();
            delete sink; m_pSink = nullptr;
            return;
        }
        hrDone = progDlg.RunMessageLoop();
        // proc destructor waits for reader/process handles and releases them
    } else {
        if (!app.Get7z().IsLoaded()) {
            progDlg.Dismiss();
            delete sink; m_pSink = nullptr;
            ShowError(I18n::Tr(IDS_ERR_7Z_NOT_LOADED).c_str());
            return;
        }
        std::vector<UINT32> deleteIndices(indexSet.begin(), indexSet.end());
        auto& sz = app.Get7z();
        m_worker.Start([&sz, archivePath, deleteIndices, sink]() -> HRESULT {
            return sz.DeleteItems(archivePath.c_str(), deleteIndices, nullptr, sink);
        }, m_hwnd, WM_APP_DONE);
        hrDone = progDlg.RunMessageLoop();
        m_worker.Wait();
    }

    delete sink;
    m_pSink = nullptr;

    if (FAILED(hrDone) && hrDone != E_ABORT) {
        ShowError(I18n::Tr(IDS_ERR_DELETE_FAILED).c_str(), hrDone);
        return;
    }
    if (hrDone == E_ABORT) return;

    // Success → reload the archive
    OpenArchive(archivePath.c_str());
}

void MainWindow::OnCompress(CompressDlg::Params& params) {
    if (params.inputFiles.empty() || params.outputPath.empty()) return;

    auto  inputs  = params.inputFiles;
    auto  outPath = params.outputPath;
    auto  format  = params.format;
    int   level   = params.level;
    auto  method  = params.method;
    auto  pw      = params.password;

    ProgressDlg progDlg;
    progDlg.Show(m_hwnd, I18n::Tr(IDS_PROGRESS_COMPRESSING).c_str());

    auto* sink = new ProgressPostSink(m_hwnd, WM_APP_PROGRESS, WM_APP_DONE);
    m_pSink = sink;
    progDlg.SetSink(sink);

    HRESULT hrDone = S_OK;

    if (format == L"rar") {
        hrDone = RunRarCompressSync(m_hwnd, params,
                                    App::Instance().GetSettings().GetRarExePath().c_str(),
                                    progDlg, sink);
        if (hrDone == E_FAIL) {
            // progDlg was already dismissed internally on launch failure
            delete sink; m_pSink = nullptr;
            return;
        }
    } else {
        if (!App::Instance().Get7z().IsLoaded()) {
            progDlg.Dismiss();
            delete sink; m_pSink = nullptr;
            ShowError(I18n::Tr(IDS_ERR_7Z_NOT_LOADED).c_str());
            return;
        }
        auto& sz = App::Instance().Get7z();

        // Resolve the 7z SFX module (search in the same folder as 7z.dll if specified)
        std::wstring sfxModulePath;
        if (!params.sfxMode.empty()) {
            sfxModulePath = Resolve7zSfxModulePath(
                sz.GetLoadedPath().c_str(), params.sfxMode.c_str());
            if (sfxModulePath.empty()) {
                progDlg.Dismiss();
                delete sink; m_pSink = nullptr;
                const wchar_t* leaf = (params.sfxMode == L"console") ? L"7zCon.sfx" : L"7z.sfx";
                std::wstring msg = I18n::TrFmt(IDS_FMT_SFX_NOT_FOUND_7Z, leaf);
                MessageBoxW(m_hwnd, msg.c_str(), I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
                return;
            }
        }

        auto advDict    = params.dictSize;
        auto advWord    = params.wordSize;
        auto advSolid   = params.solidBlock;
        auto advThreads = params.threads;
        auto advExtra   = params.extra;
        auto advVolume  = params.volumeSize;
        bool encHdr     = params.encryptHeaders;
        m_worker.Start([&sz, inputs, outPath, format, level, method, pw, sink,
                        advDict, advWord, advSolid, advThreads, advExtra, advVolume,
                        sfxModulePath, encHdr]() -> HRESULT {
            CompressAdvanced adv;
            adv.dictSize      = advDict;
            adv.wordSize      = advWord;
            adv.solidBlock    = advSolid;
            adv.threads       = advThreads;
            adv.extra         = advExtra;
            adv.volumeSize    = advVolume;
            adv.sfxModulePath = sfxModulePath;
            return sz.Compress(inputs, outPath.c_str(), format.c_str(),
                               level, method.c_str(), pw.empty() ? nullptr : pw.c_str(),
                               sink, &adv, encHdr);
        }, m_hwnd, WM_APP_DONE);
        hrDone = progDlg.RunMessageLoop();
        m_worker.Wait();
    }

    delete sink;
    m_pSink = nullptr;

    if (FAILED(hrDone) && hrDone != E_ABORT)
        ShowError(I18n::Tr(IDS_ERR_COMPRESS_FAILED).c_str(), hrDone);
}

// ---- Tree and List population ----

static int GetIconIndex(const std::wstring& name, bool isDir);  // forward decl

void MainWindow::PopulateTree() {
    TreeView_DeleteAllItems(m_hTreeView);
    m_folderPaths.clear();

    // Build a set of paths that are definitively files (isDir=false).
    // These must never appear as folder nodes in the tree, even if some archive
    // format incorrectly sets isDir=true for the same path.
    std::set<std::wstring> filePaths;
    for (auto& it : m_items) {
        if (!it.isDir) filePaths.insert(it.path);
    }

    // Collect all unique folder paths from items
    std::set<std::wstring> folderSet;
    folderSet.insert(L"");  // root (index 0)
    for (auto& it : m_items) {
        // Add explicit directory entry (skip if same path is also a file entry)
        if (it.isDir && !it.path.empty() && !filePaths.count(it.path))
            folderSet.insert(it.path);
        // Add all ancestor paths so implicit folders (archives without dir entries) work too
        std::wstring p = it.path;
        auto pos = p.rfind(L'/');
        while (pos != std::wstring::npos) {
            p = p.substr(0, pos);
            if (!filePaths.count(p))
                folderSet.insert(p);
            pos = p.rfind(L'/');
        }
    }

    // m_folderPaths[0] == "" (root), rest sorted alphabetically
    m_folderPaths.assign(folderSet.begin(), folderSet.end());

    // Build HTREEITEM map: folderPath → HTREEITEM
    std::map<std::wstring, HTREEITEM> treeItems;

    const wchar_t* leaf = wcsrchr(m_archivePath.c_str(), L'\\');
    std::wstring rootName = leaf ? (leaf + 1) : m_archivePath;

    // Icon indices: archive file icon for root, closed/open folder icons for sub-nodes
    int icoArchive = GetIconIndex(m_archivePath, false);
    if (m_iconIndexFolder < 0)
        m_iconIndexFolder = GetIconIndex(L"folder", true);
    int icoFolder  = m_iconIndexFolder;

    TV_INSERTSTRUCTW tvi = {};
    tvi.hInsertAfter      = TVI_LAST;
    tvi.item.mask         = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;

    tvi.hParent           = TVI_ROOT;
    tvi.item.pszText      = const_cast<wchar_t*>(rootName.c_str());
    tvi.item.lParam       = 0;  // index into m_folderPaths
    tvi.item.iImage       = icoArchive;
    tvi.item.iSelectedImage = icoArchive;
    HTREEITEM hRoot       = TreeView_InsertItem(m_hTreeView, &tvi);
    treeItems[L""]        = hRoot;

    // Insert sub-folders in sorted order (parents guaranteed to appear before children)
    for (int i = 1; i < (int)m_folderPaths.size(); ++i) {
        const std::wstring& fp = m_folderPaths[i];

        // Parent path
        std::wstring parentPath;
        auto slash = fp.rfind(L'/');
        if (slash != std::wstring::npos) parentPath = fp.substr(0, slash);

        HTREEITEM hParent = hRoot;
        auto it2 = treeItems.find(parentPath);
        if (it2 != treeItems.end()) hParent = it2->second;

        // Leaf name for display
        const wchar_t* displayName = fp.c_str();
        if (slash != std::wstring::npos) displayName += slash + 1;

        tvi.hParent             = hParent;
        tvi.item.pszText        = const_cast<wchar_t*>(displayName);
        tvi.item.lParam         = (LPARAM)i;
        tvi.item.iImage         = icoFolder;
        tvi.item.iSelectedImage = icoFolder;
        HTREEITEM hItem         = TreeView_InsertItem(m_hTreeView, &tvi);
        treeItems[fp]           = hItem;
    }

    TreeView_Expand(m_hTreeView, hRoot, TVE_EXPAND);
    TreeView_SelectItem(m_hTreeView, hRoot);
    SetFocus(m_hTreeView);
}

static std::wstring FormatFileSize(UINT64 bytes) {
    if (bytes == 0) return L"";
    wchar_t buf[64];
    if (bytes >= 1024ULL * 1024 * 1024)
        swprintf_s(buf, L"%.1f GB", bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024ULL * 1024)
        swprintf_s(buf, L"%.1f MB", bytes / (1024.0 * 1024));
    else if (bytes >= 1024ULL)
        swprintf_s(buf, L"%.1f KB", bytes / 1024.0);
    else
        swprintf_s(buf, L"%llu B", bytes);
    return buf;
}

// Returns the system image list icon index for a given filename.
// Uses SHGFI_USEFILEATTRIBUTES so no filesystem access is needed.
static int GetIconIndex(const std::wstring& name, bool isDir) {
    DWORD attr = isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    SHFILEINFOW sfi = {};
    SHGetFileInfoW(name.c_str(), attr, &sfi, sizeof(sfi),
                   SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
    return sfi.iIcon;
}

void MainWindow::PopulateList(const std::wstring& folderPath) {
    ListView_DeleteAllItems(m_hListView);
    m_currentFolderPath = folderPath;  // Store current folder

    // Add ".." (parent directory) at the beginning if not at root
    if (!folderPath.empty()) {
        int row = 0;
        LVITEMW lvi = {};
        lvi.mask     = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        lvi.iItem    = row;
        lvi.iSubItem = 0;
        // Use UINT32_MAX as special marker for ".."
        lvi.lParam   = UINT32_MAX;
        lvi.iImage   = (m_iconIndexFolder < 0) ? 
            (m_iconIndexFolder = GetIconIndex(L"folder", true)) : m_iconIndexFolder;
        const wchar_t* parentText = L"..";
        lvi.pszText  = const_cast<wchar_t*>(parentText);
        ListView_InsertItem(m_hListView, &lvi);
        ListView_SetItemText(m_hListView, row, 1, const_cast<wchar_t*>(L""));
        ListView_SetItemText(m_hListView, row, 2, const_cast<wchar_t*>(L""));
        std::wstring folderType = I18n::Tr(IDS_TYPE_FOLDER);
        ListView_SetItemText(m_hListView, row, 3, folderType.data());
        ListView_SetItemText(m_hListView, row, 4, const_cast<wchar_t*>(L""));
    }

    // Collect items belonging to this folder, split into dirs and files
    struct Row { const ArchiveItem* it; };
    std::vector<Row> dirs, files;
    std::set<std::wstring> explicitDirPaths;  // folder paths actually present in m_items
    for (auto& it : m_items) {
        std::wstring itemDir;
        auto pos = it.path.rfind(L'/');
        if (pos != std::wstring::npos) itemDir = it.path.substr(0, pos);
        if (itemDir != folderPath) continue;
        if (it.name.empty()) continue;
        if (it.isDir) {
            dirs.push_back({&it});
            explicitDirPaths.insert(it.path);
        } else {
            files.push_back({&it});
        }
    }

    // Sort each group by the current sort column/direction (folders always first)
    int  sc  = m_sortCol;
    bool asc = m_sortAsc;
    auto cmp = [sc, asc](const Row& a, const Row& b) -> bool {
        int result = 0;
        switch (sc) {
        case 1: // Size
            result = (a.it->size < b.it->size) ? -1 : (a.it->size > b.it->size) ? 1 : 0;
            break;
        case 2: // Compressed
            result = (a.it->packedSize < b.it->packedSize) ? -1 : (a.it->packedSize > b.it->packedSize) ? 1 : 0;
            break;
        case 3: // Type
            result = _wcsicmp(a.it->method.c_str(), b.it->method.c_str());
            break;
        case 4: // Modified
            result = CompareFileTime(&a.it->mtime, &b.it->mtime);
            break;
        default: // Name
            result = _wcsicmp(a.it->name.c_str(), b.it->name.c_str());
        }
        return asc ? (result < 0) : (result > 0);
    };
    std::sort(dirs.begin(),  dirs.end(),  cmp);
    std::sort(files.begin(), files.end(), cmp);

    // Merge: folders first, then files
    std::vector<Row> rows;
    rows.insert(rows.end(), dirs.begin(),  dirs.end());
    rows.insert(rows.end(), files.begin(), files.end());

    // For archives where folder entries are omitted (e.g. unrar.dll):
    // search m_folderPaths for immediate child folders of folderPath; add any that have no
    // real entry in m_items as virtual folder rows prepended before real folder rows.
    // Identified by lParam = m_items.size() + m_folderPaths index.
    struct VirtualDirRow { std::wstring name; int fpIdx; };
    std::vector<VirtualDirRow> virtualDirs;
    for (int i = 1; i < (int)m_folderPaths.size(); ++i) {
        const std::wstring& fp = m_folderPaths[i];
        // Check whether fp is a direct child of folderPath
        std::wstring parentPath;
        auto slash = fp.rfind(L'/');
        if (slash != std::wstring::npos) parentPath = fp.substr(0, slash);
        if (parentPath != folderPath) continue;
        // Skip if a real entry already exists
        if (explicitDirPaths.count(fp)) continue;
        std::wstring leafName = (slash != std::wstring::npos) ? fp.substr(slash + 1) : fp;
        if (!leafName.empty())
            virtualDirs.push_back({std::move(leafName), i});
    }
    // Sort by name (fixed ascending regardless of sort column; placed together at the top with real folders)
    std::sort(virtualDirs.begin(), virtualDirs.end(),
        [](const VirtualDirRow& a, const VirtualDirRow& b) {
            return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        });

    if (m_iconIndexFolder < 0)
        m_iconIndexFolder = GetIconIndex(L"folder", true);
    int icoFolder = m_iconIndexFolder;

    // Insert virtual folders first
    for (auto& vd : virtualDirs) {
        int row = ListView_GetItemCount(m_hListView);

        LVITEMW lvi = {};
        lvi.mask     = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        lvi.iItem    = row;
        lvi.iSubItem = 0;
        // Identify as virtual folder via an offset outside the m_items range
        lvi.lParam   = (LPARAM)((UINT32)m_items.size() + (UINT32)vd.fpIdx);
        lvi.iImage   = icoFolder;
        lvi.pszText  = const_cast<wchar_t*>(vd.name.c_str());
        ListView_InsertItem(m_hListView, &lvi);

        ListView_SetItemText(m_hListView, row, 1, const_cast<wchar_t*>(L""));
        ListView_SetItemText(m_hListView, row, 2, const_cast<wchar_t*>(L""));
        std::wstring folderType = I18n::Tr(IDS_TYPE_FOLDER);
        ListView_SetItemText(m_hListView, row, 3, folderType.data());
        ListView_SetItemText(m_hListView, row, 4, const_cast<wchar_t*>(L""));
    }

    for (auto& r : rows) {
        const ArchiveItem& it = *r.it;
        int row = ListView_GetItemCount(m_hListView);
        int iconIdx = GetIconIndex(it.name, it.isDir);

        LVITEMW lvi = {};
        lvi.mask     = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        lvi.iItem    = row;
        lvi.iSubItem = 0;
        lvi.lParam   = (LPARAM)it.index;
        lvi.iImage   = iconIdx;
        lvi.pszText  = const_cast<wchar_t*>(it.name.c_str());
        ListView_InsertItem(m_hListView, &lvi);

        // Size column
        std::wstring sizeStr = it.isDir ? L"" : FormatFileSize(it.size);
        ListView_SetItemText(m_hListView, row, 1, const_cast<wchar_t*>(sizeStr.c_str()));

        // Packed size
        std::wstring packedStr = it.isDir ? L"" : FormatFileSize(it.packedSize);
        ListView_SetItemText(m_hListView, row, 2, const_cast<wchar_t*>(packedStr.c_str()));

        // Type
        std::wstring typeStr = it.isDir ? I18n::Tr(IDS_TYPE_FOLDER)
                             : (!it.method.empty() ? it.method : I18n::Tr(IDS_TYPE_FILE));
        ListView_SetItemText(m_hListView, row, 3, const_cast<wchar_t*>(typeStr.c_str()));

        // Date
        if (it.mtime.dwLowDateTime || it.mtime.dwHighDateTime) {
            FILETIME local = {};
            FileTimeToLocalFileTime(&it.mtime, &local);
            SYSTEMTIME st = {};
            FileTimeToSystemTime(&local, &st);
            wchar_t dateStr[64] = {};
            swprintf_s(dateStr, L"%04d/%02d/%02d %02d:%02d",
                       st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
            ListView_SetItemText(m_hListView, row, 4, dateStr);
        }
    }

    // If items exist but nothing is selected, place the focus cursor on the first item (no selection)
    if (ListView_GetItemCount(m_hListView) > 0 &&
        ListView_GetNextItem(m_hListView, -1, LVNI_SELECTED) < 0) {
        ListView_SetItemState(m_hListView, 0, LVIS_FOCUSED, LVIS_FOCUSED);
        ListView_EnsureVisible(m_hListView, 0, FALSE);
    }
}

void MainWindow::UpdateSortHeader() {
    HWND hHeader = ListView_GetHeader(m_hListView);
    if (!hHeader) return;
    int nCols = Header_GetItemCount(hHeader);
    for (int i = 0; i < nCols; ++i) {
        HDITEMW hdi = {};
        hdi.mask = HDI_FORMAT;
        Header_GetItem(hHeader, i, &hdi);
        hdi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == m_sortCol)
            hdi.fmt |= (m_sortAsc ? HDF_SORTUP : HDF_SORTDOWN);
        Header_SetItem(hHeader, i, &hdi);
    }
}

void MainWindow::OnColumnClick(int col) {
    if (m_sortCol == col)
        m_sortAsc = !m_sortAsc;
    else {
        m_sortCol = col;
        m_sortAsc = true;
    }
    UpdateSortHeader();
    PopulateList(SelectedFolderPath());
}

void MainWindow::SelectTreeFolder(const std::wstring& folderPath) {
    if (!m_hTreeView) return;
    int targetIdx = -1;
    for (int i = 0; i < (int)m_folderPaths.size(); ++i) {
        if (m_folderPaths[i] == folderPath) { targetIdx = i; break; }
    }
    if (targetIdx < 0) return;

    std::function<HTREEITEM(HTREEITEM)> findItem = [&](HTREEITEM h) -> HTREEITEM {
        while (h) {
            TVITEMW tvi = {}; tvi.hItem = h; tvi.mask = TVIF_PARAM;
            TreeView_GetItem(m_hTreeView, &tvi);
            if ((int)tvi.lParam == targetIdx) return h;
            if (HTREEITEM child = TreeView_GetChild(m_hTreeView, h)) {
                if (HTREEITEM found = findItem(child)) return found;
            }
            h = TreeView_GetNextSibling(m_hTreeView, h);
        }
        return nullptr;
    };
    HTREEITEM hRoot = TreeView_GetRoot(m_hTreeView);
    if (HTREEITEM hFound = findItem(hRoot)) {
        TreeView_EnsureVisible(m_hTreeView, hFound);
        TreeView_SelectItem(m_hTreeView, hFound);
    }
}

std::wstring MainWindow::SelectedFolderPath() const {
    HTREEITEM hSel = TreeView_GetSelection(m_hTreeView);
    if (!hSel) return L"";

    TVITEMW tvi = {};
    tvi.hItem = hSel;
    tvi.mask  = TVIF_PARAM;
    TreeView_GetItem(m_hTreeView, &tvi);

    int idx = (int)tvi.lParam;
    if (idx >= 0 && idx < (int)m_folderPaths.size())
        return m_folderPaths[idx];
    return L"";
}

void MainWindow::ShowError(const wchar_t* msg, HRESULT hr) {
    std::wstring text = msg;
    if (hr) {
        wchar_t hrStr[32];
        swprintf_s(hrStr, L"  (0x%08X)", (unsigned)hr);
        text += hrStr;
    }
    MessageBoxW(m_hwnd, text.c_str(), L"AileEx", MB_ICONERROR);
}

bool MainWindow::Ensure7zLoaded(bool useUnrar) {
    if (!useUnrar && !App::Instance().Get7z().IsLoaded()) {
        ShowError(I18n::Tr(IDS_ERR_7Z_NOT_LOADED).c_str());
        return false;
    }
    return true;
}

// Show a password input dialog and return the entered string.
// Returns an empty string if cancelled.
std::wstring MainWindow::PromptPassword() {
    struct PwDlg {
        std::wstring result;
        static INT_PTR CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
            if (msg == WM_INITDIALOG) {
                SetWindowLongPtrW(hwnd, DWLP_USER, lp);
                return TRUE;
            }
            auto* self = reinterpret_cast<PwDlg*>(GetWindowLongPtrW(hwnd, DWLP_USER));
            if (msg == WM_COMMAND) {
                if (LOWORD(wp) == IDOK) {
                    wchar_t buf[512] = {};
                    GetDlgItemTextW(hwnd, IDC_PASSWORD_INPUT, buf, 512);
                    if (self) self->result = buf;
                    EndDialog(hwnd, IDOK);
                } else if (LOWORD(wp) == IDCANCEL) {
                    EndDialog(hwnd, IDCANCEL);
                }
            }
            return FALSE;
        }
    };
    PwDlg dlg;
    INT_PTR res = DialogBoxParamW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_PASSWORD),
        m_hwnd, PwDlg::Proc, (LPARAM)&dlg);
    return (res == IDOK) ? dlg.result : L"";
}

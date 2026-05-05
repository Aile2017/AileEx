#include "MainWindow.h"
#include "App.h"
#include "CompressDlg.h"
#include "CompressHelper.h"
#include "InfoDlg.h"
#include "ProgressDlg.h"
#include "RarProcess.h"
#include "SettingsDlg.h"
#include "resource.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl_core.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <map>
#include <commctrl.h>
#include <algorithm>
#include <set>

#pragma comment(lib, "version.lib")

namespace {
// ランチャー経由起動などで親プロセスが既に終了しているケース向けの
// フォアグラウンド奪取。SetForegroundWindow 単独では制限で降格されるので、
// フォアグラウンドアプリのスレッドにアタッチした上で TopMost を一瞬付けて
// Z オーダーを押し出してから呼ぶ。
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
}

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
    m_archivePath = path;
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
            hr = app.Get7z().OpenArchive(path, m_items);
        }
    } else {
        // Try 7z first
        if (app.Get7z().IsLoaded()) {
            hr = app.Get7z().OpenArchive(path, m_items);
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

    // 7z Open 失敗時：暗号化ヘッダの可能性があるためパスワードを促してリトライ
    if (FAILED(hr) && !m_openedWithUnrar && app.Get7z().IsLoaded()) {
        std::wstring pw = PromptPassword();
        if (!pw.empty()) {
            m_items.clear();
            hr = app.Get7z().OpenArchive(path, m_items, pw.c_str());
        }
    }

    if (FAILED(hr)) {
        std::wstring msg = L"アーカイブを開けませんでした。";
        if (!app.Get7z().IsLoaded() && !app.GetUnrar().IsLoaded())
            msg += L"\n7z.dll / unrar.dll が読み込まれていません。";
        else if (!app.Get7z().IsLoaded())
            msg += L"\n7z.dll が読み込まれていません。";
        ShowError(msg.c_str(), hr);
        return;
    }

    // MRU 更新 — 相対パスや混在ケース ("../" 等) は GetFullPathNameW で正規化。
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
        wchar_t status[512];
        swprintf_s(status, L"%zu 個のエントリ  [%s]", m_items.size(), dllName.c_str());
        SetWindowTextW(m_hStatus, status);
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
    // ListView フォーカス中に Enter → フォルダナビゲート or 展開
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

    case WM_INITMENUPOPUP:
        // HIWORD(lp) != 0 はシステムメニュー (タイトルバー右クリック等) のため対象外
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
        // fallback: 内側ループが WM_APP_PROGRESS/DONE を吸収するため通常は到達しない
        OnProgress((int)wp, (wchar_t*)lp);
        return 0;

    case WM_APP_DONE:
        // fallback: 内側ループが WM_APP_PROGRESS/DONE を吸収するため通常は到達しない
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
        // Delete session temp dir tree (files opened via [閲覧])
        if (!m_tempViewDir.empty()) {
            SHFILEOPSTRUCTW fop = {};
            std::wstring dir = m_tempViewDir;
            dir += L'\0';  // double-null required by SHFileOperation
            fop.wFunc  = FO_DELETE;
            fop.pFrom  = dir.c_str();
            fop.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
            SHFileOperationW(&fop);
        }
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

// ---- Control creation ----

void MainWindow::OnCreate(HWND hwnd) {
    CreateControls(hwnd);
    DragAcceptFiles(hwnd, TRUE);

    // 最近使ったアーカイブのサブメニューハンドルを探してキャッシュ。
    // 一度キャッシュすれば中身を再構築しても HMENU 自体は有効。
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
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBSTYLE_FLAT | CCS_NODIVIDER | CCS_NORESIZE,
        0, 0, 0, kToolbarH, hwnd, nullptr, hInst, nullptr);
    SendMessageW(m_hToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(m_hToolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(48, 36));

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

    TBBUTTON btns[] = {
        {idxExtract, ID_EXTRACT,      TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
        {idxOpen,    ID_OPEN_ASSOC,   TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
        {idxAdd,     ID_ADD,          TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
        {idxInfo,    ID_INFO,         TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
        {0,          0,               0,               BTNS_SEP,    {}, 0, 0},
        {idxSettings, ID_SETTINGS_DLG, TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, 0},
    };
    SendMessageW(m_hToolbar, TB_ADDBUTTONS, _countof(btns), (LPARAM)btns);
    SendMessageW(m_hToolbar, TB_AUTOSIZE, 0, 0);

    // 設定で非表示なら起動直後に隠す
    if (!m_toolbarVisible)
        ShowWindow(m_hToolbar, SW_HIDE);

    // Status bar
    m_hStatus = CreateWindowExW(0, STATUSCLASSNAME, L"",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, hwnd, nullptr, hInst, nullptr);

    // TreeView (left pane). 設定で非表示なら WS_VISIBLE を付与しない。
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
    struct ColDef { const wchar_t* name; int width; };
    ColDef cols[] = {
        {L"名前",     220},
        {L"サイズ",   90},
        {L"圧縮後",   90},
        {L"種類",     80},
        {L"更新日時", 140},
    };
    for (int i = 0; i < (int)_countof(cols); ++i) {
        LVCOLUMNW lvc = {};
        lvc.mask    = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        lvc.fmt     = (i == 0) ? LVCFMT_LEFT : LVCFMT_RIGHT;
        lvc.cx      = cols[i].width;
        lvc.pszText = const_cast<wchar_t*>(cols[i].name);
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
        // ツリー非表示時は ListView をフル幅化。ツリー本体は SW_HIDE 済み想定。
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

// ---- Commands ----

void MainWindow::OnCommand(WORD id) {
    switch (id) {
    case ID_EXTRACT:
        OnExtract();
        break;
    case ID_OPEN_ASSOC:
        OnOpenAssoc();
        break;
    case ID_ADD:
        OnAddFiles();
        break;
    case ID_TEST:
        OnTest();
        break;
    case ID_INFO:
        OnInfo();
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

    // フォルダパスインデックスを解決してツリー選択に使うヘルパー
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
        // m_items に実エントリがあるフォルダ
        const std::wstring& targetPath = m_items[arcIdx].path;
        for (int i = 0; i < (int)m_folderPaths.size(); ++i) {
            if (m_folderPaths[i] == targetPath) {
                navigateToFolderIndex(i);
                break;
            }
        }
    } else if (arcIdx >= (UINT32)m_items.size()) {
        // 仮想フォルダ（unrar.dll 等でエントリが省略されたフォルダ）
        int fpIdx = (int)(arcIdx - (UINT32)m_items.size());
        if (fpIdx < (int)m_folderPaths.size())
            navigateToFolderIndex(fpIdx);
    } else {
        // ファイル → 展開ダイアログを開く
        OnExtract();
    }
}

void MainWindow::OnOpenAssoc() {
    if (m_archivePath.empty()) return;
    if (m_openedWithUnrar) {
        MessageBoxW(m_hwnd,
            L"閲覧機能は 7z.dll 使用時のみ利用できます。\n"
            L"設定で RAR 展開エンジンを「7z.dll」に切り替えてください。",
            L"AileEx", MB_ICONINFORMATION);
        return;
    }

    App& app = App::Instance();
    if (!app.Get7z().IsLoaded()) {
        ShowError(L"7z.dll が読み込まれていません。");
        return;
    }

    // Get single selected item
    int sel = ListView_GetNextItem(m_hListView, -1, LVNI_SELECTED);
    if (sel < 0) {
        MessageBoxW(m_hwnd, L"ファイルを選択してください。", L"AileEx", MB_ICONINFORMATION);
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
        MessageBoxW(m_hwnd, L"フォルダは閲覧できません。ファイルを選択してください。",
                    L"AileEx", MB_ICONINFORMATION);
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
    HRESULT hr = app.Get7z().Extract(m_archivePath.c_str(), indices,
                                      tempDir.c_str(), nullptr, nullptr);
    if (FAILED(hr)) {
        ShowError(L"ファイルの取り出しに失敗しました。", hr);
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
            (L"関連付けられたアプリケーションが見つかりませんでした。\n" + localPath).c_str(),
            L"AileEx", MB_ICONWARNING);
    }
}

void MainWindow::OnExtract() {
    if (m_archivePath.empty()) return;

    App& app = App::Instance();
    bool useUnrar = m_openedWithUnrar;

    if (!useUnrar && !app.Get7z().IsLoaded()) {
        ShowError(L"7z.dll が読み込まれていません。");
        return;
    }

    // Ask destination folder
    wchar_t destDir[MAX_PATH] = {};
    {
        IFileOpenDialog* pfd = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&pfd)))) {
            FILEOPENDIALOGOPTIONS opts = 0;
            pfd->GetOptions(&opts);
            pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
            pfd->SetTitle(L"展開先フォルダを選択してください");
            if (SUCCEEDED(pfd->Show(m_hwnd))) {
                IShellItem* psi = nullptr;
                if (SUCCEEDED(pfd->GetResult(&psi))) {
                    PWSTR psz = nullptr;
                    psi->GetDisplayName(SIGDN_FILESYSPATH, &psz);
                    if (psz) { wcsncpy_s(destDir, psz, MAX_PATH - 1); CoTaskMemFree(psz); }
                    psi->Release();
                }
            }
            pfd->Release();
        }
    }
    if (!destDir[0]) return;

    // Collect selected indices (empty = all; ignored by unrar path)
    std::vector<UINT32> indices;
    int item = -1;
    while ((item = ListView_GetNextItem(m_hListView, item, LVNI_SELECTED)) != -1) {
        LVITEMW lvi = {};
        lvi.iItem = item;
        lvi.mask  = LVIF_PARAM;
        ListView_GetItem(m_hListView, &lvi);
        indices.push_back((UINT32)lvi.lParam);
    }

    ProgressDlg progDlg;
    progDlg.Show(m_hwnd, L"展開中...");

    auto* sink = new ProgressPostSink(m_hwnd, WM_APP_PROGRESS, WM_APP_DONE);
    m_pSink    = sink;
    progDlg.SetSink(sink);

    auto archivePath = m_archivePath;

    if (useUnrar) {
        auto& unrar = app.GetUnrar();
        m_worker.Start([&unrar, archivePath, destDir = std::wstring(destDir), sink]() -> HRESULT {
            bool ok = unrar.ExtractArchive(archivePath.c_str(), destDir.c_str(), nullptr, sink);
            return ok ? S_OK : E_FAIL;
        }, m_hwnd, WM_APP_DONE);
    } else {
        auto& sz = app.Get7z();
        m_worker.Start([&sz, archivePath, indices, destDir = std::wstring(destDir), sink]() -> HRESULT {
            return sz.Extract(archivePath.c_str(), indices, destDir.c_str(),
                              nullptr, sink);
        }, m_hwnd, WM_APP_DONE);
    }

    HRESULT hrDone = progDlg.RunMessageLoop();
    m_worker.Wait();
    delete sink;
    m_pSink = nullptr;

    if (FAILED(hrDone) && hrDone != E_ABORT)
        ShowError(L"展開に失敗しました。", hrDone);
}

void MainWindow::OnTest() {
    if (m_archivePath.empty()) {
        MessageBoxW(m_hwnd, L"テスト対象のアーカイブがありません。",
                    L"AileEx", MB_ICONINFORMATION);
        return;
    }

    App& app = App::Instance();
    bool useUnrar = m_openedWithUnrar;
    if (!useUnrar && !app.Get7z().IsLoaded()) {
        ShowError(L"7z.dll が読み込まれていません。");
        return;
    }

    ProgressDlg progDlg;
    progDlg.Show(m_hwnd, L"テスト中...");

    auto* sink = new ProgressPostSink(m_hwnd, WM_APP_PROGRESS, WM_APP_DONE);
    m_pSink    = sink;
    progDlg.SetSink(sink);

    auto archivePath = m_archivePath;

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
    // unrar.dll の TestArchive はキャンセル時も false (= E_FAIL) を返してしまうため、
    // sink のキャンセルフラグを見て E_ABORT 相当に正規化する。
    bool wasCancelled = sink->IsCancelled();
    delete sink;
    m_pSink = nullptr;

    if (hrDone == E_ABORT || wasCancelled) {
        // キャンセル時は無音
    } else if (FAILED(hrDone)) {
        ShowError(L"テストに失敗しました。", hrDone);
    } else {
        MessageBoxW(m_hwnd, L"アーカイブの整合性を確認しました。",
                    L"AileEx", MB_ICONINFORMATION);
    }
}

void MainWindow::OnFileOpen() {
    IFileOpenDialog* pfd = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&pfd))))
        return;

    COMDLG_FILTERSPEC filter[] = {
        { L"アーカイブファイル",
          L"*.7z;*.zip;*.rar;*.tar;*.gz;*.bz2;*.xz;*.cab;*.iso;*.jar;*.wim;*.lzma;*.lzh;*.arj" },
        { L"すべてのファイル", L"*.*" },
    };
    pfd->SetFileTypes((UINT)_countof(filter), filter);
    pfd->SetTitle(L"アーカイブファイルを開く");

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

// ファイルから VS_VERSION_INFO の FileVersion 文字列を取り出す。
// 取得できない場合は空文字列。
static std::wstring GetFileVersionString(const wchar_t* path) {
    if (!path || !path[0]) return {};
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &handle);
    if (!size) return {};
    std::vector<BYTE> buf(size);
    if (!GetFileVersionInfoW(path, handle, size, buf.data())) return {};

    // 翻訳テーブルから言語コードを取得し StringFileInfo\xxxx\FileVersion を引く。
    // 一般のサードパーティ製 DLL/EXE は "26.00ZSv1.5.7R1" のような表示用文字列を入れている。
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
            // 末尾の制御文字や空白を整理
            while (!s.empty() && (s.back() == L' ' || s.back() == L'\0')) s.pop_back();
            if (!s.empty()) return s;
        }
    }

    // フォールバック: VS_FIXEDFILEINFO の数値フィールド
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

// パスから leaf 名のみを抜き出す (バックスラッシュ区切り)
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
        // RAR exe: 設定が空の場合は auto-detect（レジストリ + 既知パス）に
        // フォールバックすることで Settings 未設定でも About ダイアログに表示する。
        std::wstring rarExe = app.GetSettings().GetRarExePath();
        if (rarExe.empty() || !PathFileExistsW(rarExe.c_str()))
            rarExe = RarProcess::FindRarExe();
        if (!rarExe.empty() && PathFileExistsW(rarExe.c_str()))
            entries.push_back({ LeafName(rarExe), rarExe });

        // バージョン取得 + 名前列の最大幅で揃える
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
            // 名前列の右にパディングしてバージョンを揃える
            text.append(maxName + 2 - entries[i].name.size(), L' ');
            text += versions[i].empty() ? L"(バージョン情報なし)" : versions[i];
            text += L"\r\n";
        }
        if (entries.empty())
            text = L"(コンポーネントが読み込まれていません)";

        SetDlgItemTextW(hwnd, IDC_ABOUT_LIST, text.c_str());

        // 等幅フォントでバージョン表示を綺麗に揃える
        HFONT hMono = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        if (hMono) {
            SendDlgItemMessageW(hwnd, IDC_ABOUT_LIST, WM_SETFONT, (WPARAM)hMono, TRUE);
            // ダイアログ破棄時に解放
            SetPropW(hwnd, L"AboutMonoFont", hMono);
        }

        // タイトルラベルを少し大きめにする
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

    std::wstring path = mru[idx];   // OpenArchive 内で AddMru されると並び替わるためコピー
    if (!PathFileExistsW(path.c_str())) {
        std::wstring msg = L"ファイルが見つかりません:\n" + path + L"\n\n履歴から削除します。";
        MessageBoxW(m_hwnd, msg.c_str(), L"AileEx", MB_ICONWARNING);
        settings.RemoveMru(path);
        settings.Save();
        RebuildMruMenu();
        return;
    }
    OpenArchive(path.c_str());
}

void MainWindow::RebuildMruMenu() {
    if (!m_hMruMenu) return;

    // 既存項目を全削除
    while (DeleteMenu(m_hMruMenu, 0, MF_BYPOSITION)) {}

    const auto& mru = App::Instance().GetSettings().GetMruPaths();
    if (mru.empty()) {
        AppendMenuW(m_hMruMenu, MF_STRING | MF_GRAYED, IDM_FILE_MRU_PH, L"(履歴なし)");
    } else {
        for (size_t i = 0; i < mru.size(); ++i) {
            // 先頭 9 件は &1..&9 でアクセラレータ表示。10 件目以降はインデント揃え。
            wchar_t prefix[8];
            if (i < 9)
                swprintf_s(prefix, L"&%zu  ", i + 1);
            else
                swprintf_s(prefix, L"     ");
            // & はメニュー上で下線扱いなので二重化してエスケープ
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

// メニュー表示直前に有効/無効状態を更新する。WM_INITMENUPOPUP は popup 単位で
// 発火するため、対象 ID がこの popup に含まれない場合 EnableMenuItem は -1 を返
// すだけで副作用なし。全コマンドを毎回呼んで問題ない。
void MainWindow::OnInitMenuPopup(HMENU hMenu) {
    bool hasArchive = !m_archivePath.empty();
    bool readOnly   = m_openedWithUnrar;
    int  selCount   = m_hListView ? ListView_GetSelectedCount(m_hListView) : 0;

    auto setEnabled = [hMenu](UINT id, bool enabled) {
        EnableMenuItem(hMenu, id, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
    };

    setEnabled(ID_CLOSE,      hasArchive);
    setEnabled(ID_EXTRACT,    hasArchive);
    setEnabled(ID_TEST,       hasArchive);
    setEnabled(ID_OPEN_ASSOC, hasArchive && !readOnly);
    setEnabled(ID_INFO,       selCount > 0);
    setEnabled(ID_DELETE,     hasArchive && !readOnly && selCount > 0);

    CheckMenuItem(hMenu, IDM_VIEW_TREE,
                  MF_BYCOMMAND | (m_treeVisible ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, IDM_VIEW_TOOLBAR,
                  MF_BYCOMMAND | (m_toolbarVisible ? MF_CHECKED : MF_UNCHECKED));
}

void MainWindow::CloseArchive() {
    if (m_archivePath.empty()) return;
    m_archivePath.clear();
    m_items.clear();
    m_folderPaths.clear();
    m_openedWithUnrar = false;

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
        ShowError(L"操作に失敗しました。", hr);
    }
    SetWindowTextW(m_hStatus, L"完了");
}

// ---- Compress flow ----

void MainWindow::OnAddFiles() {
    // Open multi-select file picker
    wchar_t buf[32768] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = m_hwnd;
    ofn.lpstrFile    = buf;
    ofn.nMaxFile     = _countof(buf);
    ofn.lpstrTitle   = L"圧縮するファイルを選択";
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return;

    // Parse multi-select result: first string is directory, rest are filenames
    std::vector<std::wstring> files;
    const wchar_t* p = buf;
    std::wstring dir = p;
    p += dir.size() + 1;
    if (*p == L'\0') {
        // Only one file selected
        files.push_back(dir);
    } else {
        while (*p) {
            std::wstring name = p;
            files.push_back(dir + L'\\' + name);
            p += name.size() + 1;
        }
    }

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

void MainWindow::OnDelete() {
    if (m_archivePath.empty() || m_openedWithUnrar) return;

    // ListView 選択を実エントリ集合に解決する。
    //  - 実エントリ (lParam < m_items.size())：そのインデックスを採用
    //    フォルダなら配下の全エントリも追加（"path/" プレフィクスマッチ）
    //  - 仮想フォルダ (lParam >= m_items.size())：m_folderPaths から配下を解決
    std::set<UINT32> indexSet;
    std::set<std::wstring> folderPaths;  // RAR 経路で使う表示パス
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
            std::wstring prefix = folder + L"/";
            for (UINT32 j = 0; j < (UINT32)m_items.size(); ++j) {
                if (m_items[j].path.size() > prefix.size() &&
                    m_items[j].path.compare(0, prefix.size(), prefix) == 0) {
                    indexSet.insert(j);
                }
            }
        }
    }
    if (indexSet.empty() && folderPaths.empty()) return;

    // 確認 — 元の ListView 選択数を提示（フォルダ展開後の数より直感的）
    int origCount = ListView_GetSelectedCount(m_hListView);
    wchar_t msg[256];
    swprintf_s(msg,
               L"選択した %d 個の項目をアーカイブから削除します。\n"
               L"（フォルダは配下も含めて削除されます）\n\nよろしいですか？",
               origCount);
    if (MessageBoxW(m_hwnd, msg, L"削除確認", MB_YESNO | MB_ICONWARNING) != IDYES)
        return;

    App& app = App::Instance();
    const wchar_t* dot = wcsrchr(m_archivePath.c_str(), L'.');
    bool isRar = (dot && _wcsicmp(dot + 1, L"rar") == 0);

    ProgressDlg progDlg;
    progDlg.Show(m_hwnd, L"削除中...");

    auto* sink = new ProgressPostSink(m_hwnd, WM_APP_PROGRESS, WM_APP_DONE);
    m_pSink = sink;
    progDlg.SetSink(sink);

    HRESULT hrDone = S_OK;
    auto archivePath = m_archivePath;

    if (isRar) {
        // RAR: rar.exe d -y -r でフォルダ含めて削除
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
        // proc のデストラクタが reader/process ハンドルを待機・解放
    } else {
        if (!app.Get7z().IsLoaded()) {
            progDlg.Dismiss();
            delete sink; m_pSink = nullptr;
            ShowError(L"7z.dll が読み込まれていません。");
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
        ShowError(L"削除に失敗しました。", hrDone);
        return;
    }
    if (hrDone == E_ABORT) return;

    // 成功 → アーカイブを再読込
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
    progDlg.Show(m_hwnd, L"圧縮中...");

    auto* sink = new ProgressPostSink(m_hwnd, WM_APP_PROGRESS, WM_APP_DONE);
    m_pSink = sink;
    progDlg.SetSink(sink);

    HRESULT hrDone = S_OK;

    if (format == L"rar") {
        hrDone = RunRarCompressSync(m_hwnd, params,
                                    App::Instance().GetSettings().GetRarExePath().c_str(),
                                    progDlg, sink);
        if (hrDone == E_FAIL) {
            // 起動失敗時は progDlg を内部で Dismiss 済み
            delete sink; m_pSink = nullptr;
            return;
        }
    } else {
        if (!App::Instance().Get7z().IsLoaded()) {
            progDlg.Dismiss();
            delete sink; m_pSink = nullptr;
            ShowError(L"7z.dll が読み込まれていません。");
            return;
        }
        auto& sz = App::Instance().Get7z();
        auto advDict    = params.dictSize;
        auto advWord    = params.wordSize;
        auto advSolid   = params.solidBlock;
        auto advThreads = params.threads;
        auto advExtra   = params.extra;
        bool encHdr     = params.encryptHeaders;
        m_worker.Start([&sz, inputs, outPath, format, level, method, pw, sink,
                        advDict, advWord, advSolid, advThreads, advExtra, encHdr]() -> HRESULT {
            CompressAdvanced adv;
            adv.dictSize   = advDict;
            adv.wordSize   = advWord;
            adv.solidBlock = advSolid;
            adv.threads    = advThreads;
            adv.extra      = advExtra;
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
        ShowError(L"圧縮に失敗しました。", hrDone);
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

    // Collect items belonging to this folder, split into dirs and files
    struct Row { const ArchiveItem* it; };
    std::vector<Row> dirs, files;
    std::set<std::wstring> explicitDirPaths;  // m_items に実在するフォルダパス
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
        case 1: // サイズ
            result = (a.it->size < b.it->size) ? -1 : (a.it->size > b.it->size) ? 1 : 0;
            break;
        case 2: // 圧縮後
            result = (a.it->packedSize < b.it->packedSize) ? -1 : (a.it->packedSize > b.it->packedSize) ? 1 : 0;
            break;
        case 3: // 種類
            result = _wcsicmp(a.it->method.c_str(), b.it->method.c_str());
            break;
        case 4: // 更新日時
            result = CompareFileTime(&a.it->mtime, &b.it->mtime);
            break;
        default: // 名前
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

    // unrar.dll 等でフォルダエントリが省略されているアーカイブ向け:
    // m_folderPaths から folderPath の直下フォルダを探し、m_items に実エントリが
    // ないものは仮想フォルダ行として先頭（実フォルダ行の前）に追加する。
    // lParam = m_items.size() + m_folderPaths インデックス で識別。
    struct VirtualDirRow { std::wstring name; int fpIdx; };
    std::vector<VirtualDirRow> virtualDirs;
    for (int i = 1; i < (int)m_folderPaths.size(); ++i) {
        const std::wstring& fp = m_folderPaths[i];
        // fp が folderPath の直接の子か確認
        std::wstring parentPath;
        auto slash = fp.rfind(L'/');
        if (slash != std::wstring::npos) parentPath = fp.substr(0, slash);
        if (parentPath != folderPath) continue;
        // 実エントリが既にある場合はスキップ
        if (explicitDirPaths.count(fp)) continue;
        std::wstring leafName = (slash != std::wstring::npos) ? fp.substr(slash + 1) : fp;
        if (!leafName.empty())
            virtualDirs.push_back({std::move(leafName), i});
    }
    // 名前順ソート（ソートカラムに依らず名前昇順固定で十分; 実フォルダ群とまとめて先頭に置く）
    std::sort(virtualDirs.begin(), virtualDirs.end(),
        [](const VirtualDirRow& a, const VirtualDirRow& b) {
            return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        });

    if (m_iconIndexFolder < 0)
        m_iconIndexFolder = GetIconIndex(L"folder", true);
    int icoFolder = m_iconIndexFolder;

    // 仮想フォルダを先に挿入
    for (auto& vd : virtualDirs) {
        int row = ListView_GetItemCount(m_hListView);

        LVITEMW lvi = {};
        lvi.mask     = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        lvi.iItem    = row;
        lvi.iSubItem = 0;
        // m_items 範囲外のオフセットで仮想フォルダと識別
        lvi.lParam   = (LPARAM)((UINT32)m_items.size() + (UINT32)vd.fpIdx);
        lvi.iImage   = icoFolder;
        lvi.pszText  = const_cast<wchar_t*>(vd.name.c_str());
        ListView_InsertItem(m_hListView, &lvi);

        ListView_SetItemText(m_hListView, row, 1, const_cast<wchar_t*>(L""));
        ListView_SetItemText(m_hListView, row, 2, const_cast<wchar_t*>(L""));
        ListView_SetItemText(m_hListView, row, 3, const_cast<wchar_t*>(L"フォルダ"));
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
        std::wstring typeStr = it.isDir ? L"フォルダ" : (!it.method.empty() ? it.method : L"ファイル");
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

    // アイテムが存在し、かつ何も選択されていない場合は先頭にフォーカスカーソルを置く
    if (ListView_GetItemCount(m_hListView) > 0 &&
        ListView_GetNextItem(m_hListView, -1, LVNI_SELECTED) < 0) {
        ListView_SetItemState(m_hListView, 0, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
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

// パスワード入力ダイアログを表示し、入力された文字列を返す。
// キャンセルされた場合は空文字列を返す。
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

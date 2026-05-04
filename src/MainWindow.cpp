#include "MainWindow.h"
#include "App.h"
#include "CompressDlg.h"
#include "InfoDlg.h"
#include "ProgressDlg.h"
#include "SettingsDlg.h"
#include "resource.h"
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <map>
#include <commctrl.h>
#include <algorithm>
#include <set>

bool MainWindow::RegisterClass(HINSTANCE hInst) {
    WNDCLASSEXW wc  = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    wc.lpszClassName = ClassName();
    wc.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm       = LoadIconW(nullptr, IDI_APPLICATION);
    return RegisterClassExW(&wc) != 0;
}

bool MainWindow::Create(HINSTANCE hInst, int nCmdShow) {
    HWND hwnd = CreateWindowExW(
        0, ClassName(), L"AileEx",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 600,
        nullptr, nullptr, hInst, this);

    if (!hwnd) return false;
    ShowWindow(hwnd, nCmdShow);
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

    if (FAILED(hr)) {
        std::wstring msg = L"アーカイブを開けませんでした。";
        if (!app.Get7z().IsLoaded() && !app.GetUnrar().IsLoaded())
            msg += L"\n7z.dll / unrar.dll が読み込まれていません。";
        else if (!app.Get7z().IsLoaded())
            msg += L"\n7z.dll が読み込まれていません。";
        ShowError(msg.c_str(), hr);
        return;
    }

    // Update title
    const wchar_t* leaf = wcsrchr(path, L'\\');
    std::wstring title = std::wstring(L"AileEx - ") + (leaf ? leaf + 1 : path);
    SetWindowTextW(m_hwnd, title.c_str());

    // Update status
    wchar_t status[256];
    swprintf_s(status, L"%zu 個のエントリ", m_items.size());
    SetWindowTextW(m_hStatus, status);

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

    case WM_NOTIFY: {
        auto* hdr = reinterpret_cast<NMHDR*>(lp);
        if (hdr->hwndFrom == m_hTreeView && hdr->code == TVN_SELCHANGED)
            OnTreeSelChanged();
        if (hdr->hwndFrom == m_hListView && hdr->code == NM_DBLCLK)
            OnListDblClick();
        return 0;
    }

    case WM_APP_PROGRESS:
        OnProgress((int)wp, (wchar_t*)lp);
        return 0;

    case WM_APP_DONE:
        OnDone((HRESULT)wp);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

// ---- Control creation ----

void MainWindow::OnCreate(HWND hwnd) {
    CreateControls(hwnd);
    DragAcceptFiles(hwnd, TRUE);
}

void MainWindow::CreateControls(HWND hwnd) {
    HINSTANCE hInst = App::Instance().GetInstance();

    // Toolbar
    m_hToolbar = CreateWindowExW(0, TOOLBARCLASSNAME, nullptr,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | CCS_NODIVIDER | CCS_NORESIZE,
        0, 0, 0, kToolbarH, hwnd, nullptr, hInst, nullptr);
    SendMessageW(m_hToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(m_hToolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(0, 0));

    TBBUTTON btns[] = {
        {I_IMAGENONE, ID_EXTRACT,     TBSTATE_ENABLED, BTNS_BUTTON | BTNS_SHOWTEXT, {}, 0, (INT_PTR)L"展開"},
        {I_IMAGENONE, ID_ADD,         TBSTATE_ENABLED, BTNS_BUTTON | BTNS_SHOWTEXT, {}, 0, (INT_PTR)L"追加"},
        {I_IMAGENONE, ID_INFO,        TBSTATE_ENABLED, BTNS_BUTTON | BTNS_SHOWTEXT, {}, 0, (INT_PTR)L"情報"},
        {I_IMAGENONE, 0,              0,                BTNS_SEP,                   {}, 0, 0},
        {I_IMAGENONE, ID_SETTINGS_DLG,TBSTATE_ENABLED, BTNS_BUTTON | BTNS_SHOWTEXT, {}, 0, (INT_PTR)L"設定"},
    };
    SendMessageW(m_hToolbar, TB_ADDBUTTONS, _countof(btns), (LPARAM)btns);
    SendMessageW(m_hToolbar, TB_AUTOSIZE, 0, 0);

    // Status bar
    m_hStatus = CreateWindowExW(0, STATUSCLASSNAME, L"",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, hwnd, nullptr, hInst, nullptr);

    // TreeView (left pane)
    m_hTreeView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEW, nullptr,
        WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT |
        TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
        0, 0, 0, 0, hwnd, nullptr, hInst, nullptr);

    // ListView (right pane)
    m_hListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, nullptr,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
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
    SendMessageW(m_hToolbar, TB_AUTOSIZE, 0, 0);
    RECT rcTB = {};
    GetWindowRect(m_hToolbar, &rcTB);
    int tbH = rcTB.bottom - rcTB.top;
    SetWindowPos(m_hToolbar, nullptr, 0, 0, cx, tbH, SWP_NOZORDER);

    // Status bar
    SetWindowPos(m_hStatus, nullptr, 0, cy - kStatusH, cx, kStatusH, SWP_NOZORDER);

    int contentTop = tbH;
    int contentH   = cy - tbH - kStatusH;
    if (contentH < 0) contentH = 0;

    // TreeView (left)
    SetWindowPos(m_hTreeView, nullptr, 0, contentTop, kTreeWidth, contentH, SWP_NOZORDER);

    // ListView (right)
    int lvX = kTreeWidth + 2;
    SetWindowPos(m_hListView, nullptr, lvX, contentTop, cx - lvX, contentH, SWP_NOZORDER);
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
            const wchar_t* kExts[] = {
                L"7z",L"zip",L"rar",L"tar",L"gz",L"bz2",
                L"xz",L"cab",L"iso",L"lzh",nullptr
            };
            for (int j = 0; kExts[j]; ++j)
                if (_wcsicmp(dot + 1, kExts[j]) == 0) { isArchive = true; break; }
        }
        (isArchive ? archives : regular).push_back(std::move(path));
    }
    DragFinish(hDrop);

    if (!archives.empty()) {
        OpenArchive(archives[0].c_str()); // open first archive
    } else if (!regular.empty()) {
        CompressDlg::Params params;
        params.inputFiles = std::move(regular);
        params.format     = App::Instance().GetSettings().GetDefaultFormat();
        params.level      = App::Instance().GetSettings().GetCompressionLevel();

        CompressDlg dlg;
        if (dlg.Show(m_hwnd, params))
            OnCompress(params);
    }
}

// ---- Commands ----

void MainWindow::OnCommand(WORD id) {
    switch (id) {
    case ID_EXTRACT:
        OnExtract();
        break;
    case ID_ADD:
        OnAddFiles();
        break;
    case ID_INFO:
        OnInfo();
        break;
    case ID_SETTINGS_DLG: {
        SettingsDlg dlg;
        dlg.Show(m_hwnd);
        break;
    }
    case ID_CLOSE:
        DestroyWindow(m_hwnd);
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

    if (arcIdx < (UINT32)m_items.size() && m_items[arcIdx].isDir) {
        // Find the matching folder index and select it in the tree
        const std::wstring& targetPath = m_items[arcIdx].path;
        for (int i = 0; i < (int)m_folderPaths.size(); ++i) {
            if (m_folderPaths[i] == targetPath) {
                // Walk the TreeView to find the HTREEITEM with lParam==i
                std::function<HTREEITEM(HTREEITEM)> findItem = [&](HTREEITEM h) -> HTREEITEM {
                    while (h) {
                        TVITEMW tvi2 = {}; tvi2.hItem = h; tvi2.mask = TVIF_PARAM;
                        TreeView_GetItem(m_hTreeView, &tvi2);
                        if ((int)tvi2.lParam == i) return h;
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
    BROWSEINFOW bi = {};
    bi.hwndOwner  = m_hwnd;
    bi.lpszTitle  = L"展開先フォルダを選択してください";
    bi.ulFlags    = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    SHGetPathFromIDListW(pidl, destDir);
    CoTaskMemFree(pidl);

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

    HRESULT hrDone = S_OK;
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_APP_DONE) {
            hrDone = (HRESULT)msg.wParam;
            progDlg.SetDone(hrDone);
            progDlg.Dismiss();
            break;
        }
        if (msg.message == WM_APP_PROGRESS) {
            progDlg.SetProgress((int)msg.wParam, (wchar_t*)msg.lParam);
            free((wchar_t*)msg.lParam);
            continue;
        }
        if (!IsDialogMessageW(progDlg.Hwnd(), &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    m_worker.Wait();
    delete sink;
    m_pSink = nullptr;

    if (FAILED(hrDone) && hrDone != E_ABORT)
        ShowError(L"展開に失敗しました。", hrDone);
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
    params.format     = App::Instance().GetSettings().GetDefaultFormat();
    params.level      = App::Instance().GetSettings().GetCompressionLevel();

    CompressDlg dlg;
    if (dlg.Show(m_hwnd, params))
        OnCompress(params);
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

    auto runMsgLoop = [&](auto cancelFn) {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_APP_DONE) {
                hrDone = (HRESULT)msg.wParam;
                progDlg.SetDone(hrDone);
                progDlg.Dismiss();
                break;
            }
            if (msg.message == WM_APP_PROGRESS) {
                if (sink->IsCancelled()) cancelFn();
                progDlg.SetProgress((int)msg.wParam, (wchar_t*)msg.lParam);
                free((wchar_t*)msg.lParam);
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    };

    if (format == L"rar") {
        RarProcess rarProc;
        bool started = rarProc.Compress(inputs, outPath.c_str(), method.c_str(),
                                        m_hwnd, WM_APP_PROGRESS, WM_APP_DONE);
        if (!started) {
            progDlg.Dismiss();
            delete sink; m_pSink = nullptr;
            return;
        }
        runMsgLoop([&]{ rarProc.Cancel(); });
    } else {
        if (!App::Instance().Get7z().IsLoaded()) {
            progDlg.Dismiss();
            delete sink; m_pSink = nullptr;
            ShowError(L"7z.dll が読み込まれていません。");
            return;
        }
        auto& sz = App::Instance().Get7z();
        m_worker.Start([&sz, inputs, outPath, format, level, method, pw, sink]() -> HRESULT {
            return sz.Compress(inputs, outPath.c_str(), format.c_str(),
                               level, method.c_str(), pw.empty() ? nullptr : pw.c_str(), sink);
        }, m_hwnd, WM_APP_DONE);
        runMsgLoop([]{});
        m_worker.Wait();
    }

    delete sink;
    m_pSink = nullptr;

    if (FAILED(hrDone) && hrDone != E_ABORT)
        ShowError(L"圧縮に失敗しました。", hrDone);
}

// ---- Tree and List population ----

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

    TV_INSERTSTRUCTW tvi = {};
    tvi.hInsertAfter   = TVI_LAST;
    tvi.item.mask      = TVIF_TEXT | TVIF_PARAM;

    tvi.hParent        = TVI_ROOT;
    tvi.item.pszText   = const_cast<wchar_t*>(rootName.c_str());
    tvi.item.lParam    = 0;  // index into m_folderPaths
    HTREEITEM hRoot    = TreeView_InsertItem(m_hTreeView, &tvi);
    treeItems[L""]     = hRoot;

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

        tvi.hParent       = hParent;
        tvi.item.pszText  = const_cast<wchar_t*>(displayName);
        tvi.item.lParam   = (LPARAM)i;
        HTREEITEM hItem   = TreeView_InsertItem(m_hTreeView, &tvi);
        treeItems[fp]     = hItem;
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

void MainWindow::PopulateList(const std::wstring& folderPath) {
    ListView_DeleteAllItems(m_hListView);

    for (auto& it : m_items) {
        // Parent folder of this item
        std::wstring itemDir;
        auto pos = it.path.rfind(L'/');
        if (pos != std::wstring::npos) itemDir = it.path.substr(0, pos);

        if (itemDir != folderPath) continue;
        if (it.name.empty()) continue;  // skip items with no name

        int row = ListView_GetItemCount(m_hListView);

        LVITEMW lvi = {};
        lvi.mask     = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem    = row;
        lvi.iSubItem = 0;
        lvi.lParam   = (LPARAM)it.index;
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

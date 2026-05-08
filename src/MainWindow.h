#pragma once
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include "ArchiveItem.h"
#include "WorkerThread.h"
#include "CompressDlg.h"
#include "RarProcess.h"

class MainWindow {
public:
    bool Create(HINSTANCE hInst, int nCmdShow);
    void OpenArchive(const wchar_t* path);
    HWND Hwnd() const { return m_hwnd; }
    // メッセージループで TranslateAccelerator / IsDialogMessage より先に呼ぶ。
    // 消費した場合 true を返す。
    bool PreTranslateMessage(const MSG& msg);

    static const wchar_t* ClassName() { return L"AileEx_MainWnd"; }
    static bool RegisterClass(HINSTANCE hInst);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMsg(UINT msg, WPARAM wp, LPARAM lp);

    void OnCreate(HWND hwnd);
    void OnSize(int cx, int cy);
    void OnDropFiles(HDROP hDrop);
    void OnCommand(WORD id);
    void OnTreeSelChanged();
    void OnListDblClick();
    void OnExtract();
    void OnExtractSelected();
    void OnContextMenu(HWND hwndFrom, int x, int y);
    void OnTest();
    void OnOpenAssoc();
    void OnAddFiles();
    void OnInfo();
    void OnDelete();
    void OnFileOpen();
    void OnAbout();
    void OnToggleTree();
    void OnToggleToolbar();
    void OnInitMenuPopup(HMENU hMenu);
    void OnMruOpen(int idx);
    void RebuildMruMenu();
    void CloseArchive();  // 開いているアーカイブを閉じてビューを空にする (アプリ終了ではない)
    void OnCompress(CompressDlg::Params& params);
    void OnProgress(int pct, wchar_t* filename);  // takes ownership of filename
    void OnDone(HRESULT hr);

    void OnColumnClick(int col);
    void UpdateSortHeader();
    void CreateControls(HWND hwnd);
    void ResizePanes(int cx, int cy);
    void PopulateTree();
    void PopulateList(const std::wstring& folderPath);
    std::wstring SelectedFolderPath() const;
    void ShowError(const wchar_t* msg, HRESULT hr = 0);
    // Returns entered password, or empty string if user cancelled.
    std::wstring PromptPassword();

    HWND        m_hwnd         = nullptr;
    HWND        m_hToolbar     = nullptr;
    HWND        m_hTreeView    = nullptr;
    HWND        m_hListView    = nullptr;
    HWND        m_hStatus      = nullptr;
    HIMAGELIST  m_hSysImageList = nullptr;

    std::wstring             m_archivePath;          // 表示用の元パス (例: xx.001)
    std::wstring             m_effectiveArchivePath; // 操作実体のパス (split 自動アンラップ時のみ m_archivePath と異なる)
    bool                     m_openedWithUnrar = false;
    bool                     m_isReadOnly      = false;  // split 自動アンラップ等で書込操作を禁止
    std::vector<ArchiveItem> m_items;
    std::vector<std::wstring> m_folderPaths;  // sorted; index matches TreeView lParam
    std::wstring             m_currentFolderPath; // currently displayed folder in ListView
    WorkerThread             m_worker;
    ProgressPostSink*        m_pSink = nullptr;
    std::wstring             m_tempViewDir;   // session temp dir; deleted on exit
    int                      m_sortCol = 0;   // 0=名前, 1=サイズ, 2=圧縮後, 3=種類, 4=更新日時
    bool                     m_sortAsc = true;
    int                      m_treeWidth = 220;      // current splitter position
    bool                     m_draggingSplitter = false;
    bool                     m_treeVisible = true;   // 表示メニューでトグル可能
    bool                     m_toolbarVisible = true; // 表示メニューでトグル可能
    int                      m_iconIndexFolder = -1; // cached folder icon index
    HMENU                    m_hMruMenu = nullptr;   // 最近使ったアーカイブのサブメニュー

    static constexpr int kSplitterW = 5;
    static constexpr int kTreeMinW  = 80;
    static constexpr int kListMinW  = 80;
    static constexpr int kToolbarH  = 40;
    static constexpr int kStatusH   = 22;
};

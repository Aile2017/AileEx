#pragma once
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include <set>
#include "ArchiveItem.h"
#include "WorkerThread.h"
#include "CompressDlg.h"
#include "RarProcess.h"

class MainWindow {
public:
    bool Create(HINSTANCE hInst, int nCmdShow);
    void OpenArchive(const wchar_t* path);
    // Show the extract-destination dialog and perform extraction immediately.
    // Called after OpenArchive when -x option is given; skips the list view entirely.
    // presetDest: if non-empty, skip the folder picker and extract directly to this path.
    void TriggerExtract(const std::wstring& presetDest = L"");
    HWND Hwnd() const { return m_hwnd; }
    // Call before TranslateAccelerator / IsDialogMessage in the message loop.
    // Returns true if the message was consumed.
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
    void OnExtract(const std::wstring& presetDest = L"");
    void OnExtractSelected(const std::wstring& presetDest = L"");
    // Toolbar extract: extract selected items if any are selected, otherwise extract all.
    void OnExtractSmart();
    // Common extraction driver. indices empty = extract all (7z path).
    // rarTargetPaths empty = extract all (unrar path).
    // presetDest: if non-empty, skip the folder picker and extract directly to this path.
    void RunExtraction(std::vector<UINT32> indices, std::set<std::wstring> rarTargetPaths,
                       std::wstring presetDest = L"");
    void OnContextMenu(HWND hwndFrom, int x, int y);
    void OnTest();
    void OnOpenAssoc();
    void OnAddFiles();
    void OnAddFilesToCurrentArchive();
    // Worker-driven file addition to the currently open archive. Shows a file picker if `srcPaths` is empty.
    void AddFilesToCurrentArchive(std::vector<std::wstring> srcPaths);
    void OnInfo();
    void OnArchiveProperties();
    void OnArchiveComment();
    void OnDelete();
    void OnFileOpen();
    void OnAbout();
    void OnToggleTree();
    void OnToggleToolbar();
    void OnInitMenuPopup(HMENU hMenu);
    void OnMruOpen(int idx);
    void RebuildMruMenu();
    void CloseArchive();  // Close the open archive and clear the view (does not quit the app)
    void OnCompress(CompressDlg::Params& params, bool openAfterCompress = false);
    void OnProgress(int pct, wchar_t* filename);  // takes ownership of filename
    void OnDone(HRESULT hr);

    void OnColumnClick(int col);
    void UpdateSortHeader();
    void CreateControls(HWND hwnd);
    void ResizePanes(int cx, int cy);
    void PopulateTree();
    void PopulateList(const std::wstring& folderPath);
    std::wstring SelectedFolderPath() const;
    // Search `m_folderPaths` for `folderPath` and select it in the tree. Does nothing if not found.
    void SelectTreeFolder(const std::wstring& folderPath);
    void ShowError(const wchar_t* msg, HRESULT hr = 0);
    // Returns false and shows error if 7z is required but not loaded.
    bool Ensure7zLoaded(bool useUnrar = false);
    // Returns entered password, or empty string if user cancelled.
    std::wstring PromptPassword();
    void ApplyFontToControls();
    // Refresh the "Extract to:" edit box to reflect the current archive + settings state.
    void UpdateExtractDestEdit();

    HWND        m_hwnd         = nullptr;
    HWND        m_hToolbar     = nullptr;
    HWND        m_hExtractLabel  = nullptr;  // "Extract to:" label in toolbar row
    HWND        m_hExtractEdit   = nullptr;  // path edit box in toolbar row
    HWND        m_hExtractBrowse = nullptr;  // [...] browse button in toolbar row
    HWND        m_hTreeView    = nullptr;
    HWND        m_hListView    = nullptr;
    HWND        m_hStatus      = nullptr;
    HIMAGELIST  m_hSysImageList = nullptr;
    HIMAGELIST  m_hToolbarImages = nullptr;  // down-scaled toolbar icons
    HFONT       m_hFont        = nullptr;

    std::wstring             m_archivePath;          // Display path (e.g. xx.001)
    std::wstring             m_effectiveArchivePath; // Operative path (differs from m_archivePath only when a split archive is auto-unwrapped)
    std::wstring             m_password;             // Password used to open the current archive (empty if none)
    bool                     m_openedWithUnrar = false;
    bool                     m_isReadOnly      = false;  // Write operations disabled (e.g. split auto-unwrap)
    std::vector<ArchiveItem> m_items;
    std::vector<std::wstring> m_folderPaths;  // sorted; index matches TreeView lParam
    std::wstring             m_currentFolderPath; // currently displayed folder in ListView
    WorkerThread             m_worker;
    ProgressPostSink*        m_pSink = nullptr;
    std::wstring             m_tempViewDir;   // session temp dir; deleted on exit
    int                      m_sortCol = 0;   // 0=Name, 1=Size, 2=Compressed, 3=Type, 4=Modified
    bool                     m_sortAsc = true;
    int                      m_treeWidth = 220;      // current splitter position
    bool                     m_draggingSplitter = false;
    bool                     m_treeVisible = true;   // Toggled from the View menu
    bool                     m_toolbarVisible = true; // Toggled from the View menu
    int                      m_iconIndexFolder = -1; // cached folder icon index
    HMENU                    m_hMruMenu = nullptr;   // Submenu for recently used archives

    static constexpr int kSplitterW = 3;
    static constexpr int kTreeMinW  = 80;
    static constexpr int kListMinW  = 80;
    static constexpr int kToolbarH  = 38;  // 24px icon + 10 vertical padding + frame
    static constexpr int kStatusH   = 22;
};

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
    void OnOpenAssoc();
    void OnAddFiles();
    void OnInfo();
    void OnCompress(CompressDlg::Params& params);
    void OnProgress(int pct, wchar_t* filename);  // takes ownership of filename
    void OnDone(HRESULT hr);

    void CreateControls(HWND hwnd);
    void ResizePanes(int cx, int cy);
    void PopulateTree();
    void PopulateList(const std::wstring& folderPath);
    std::wstring SelectedFolderPath() const;
    void ShowError(const wchar_t* msg, HRESULT hr = 0);

    HWND        m_hwnd         = nullptr;
    HWND        m_hToolbar     = nullptr;
    HWND        m_hTreeView    = nullptr;
    HWND        m_hListView    = nullptr;
    HWND        m_hStatus      = nullptr;
    HIMAGELIST  m_hSysImageList = nullptr;

    std::wstring             m_archivePath;
    bool                     m_openedWithUnrar = false;
    std::vector<ArchiveItem> m_items;
    std::vector<std::wstring> m_folderPaths;  // sorted; index matches TreeView lParam
    WorkerThread             m_worker;
    ProgressPostSink*        m_pSink = nullptr;
    std::wstring             m_tempViewDir;   // session temp dir; deleted on exit

    static constexpr int kTreeWidth = 220;
    static constexpr int kToolbarH  = 28;
    static constexpr int kStatusH   = 22;
};

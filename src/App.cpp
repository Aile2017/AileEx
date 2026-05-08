#include "App.h"
#include "MainWindow.h"
#include "CompressDlg.h"
#include "CompressHelper.h"
#include "ProgressDlg.h"
#include "WorkerThread.h"
#include "resource.h"
#include <commctrl.h>

App& App::Instance() {
    static App inst;
    return inst;
}

bool App::Init(HINSTANCE hInst) {
    m_hInst = hInst;

    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);

    m_settings.Load();

    if (!m_sevenZip.Load(m_settings.Get7zDllPath().empty()
                         ? nullptr
                         : m_settings.Get7zDllPath().c_str())) {
        // Non-fatal: user can still use RAR mode.
    }

    if (!m_unrar.Load(m_settings.GetUnrarDllPath().empty()
                      ? nullptr
                      : m_settings.GetUnrarDllPath().c_str())) {
        // Non-fatal.
    }

    if (!MainWindow::RegisterClass(hInst)) return false;

    return true;
}

void App::Shutdown() {
    m_sevenZip.Unload();
    m_unrar.Unload();
    m_settings.Save();
}

void App::ReloadDlls() {
    m_sevenZip.Unload();
    m_unrar.Unload();

    m_sevenZip.Load(m_settings.Get7zDllPath().empty()
                    ? nullptr : m_settings.Get7zDllPath().c_str());
    m_unrar.Load(m_settings.GetUnrarDllPath().empty()
                 ? nullptr : m_settings.GetUnrarDllPath().c_str());
}

int App::RunBrowseMode(const std::vector<std::wstring>& archivePaths, int nCmdShow) {
    MainWindow wnd;
    if (!wnd.Create(m_hInst, nCmdShow)) return 1;

    for (auto& p : archivePaths)
        wnd.OpenArchive(p.c_str());

    ACCEL accelTable[] = {
        { FVIRTKEY,              VK_F5,     ID_EXTRACT },
        { FVIRTKEY | FCONTROL, (WORD)'E',  ID_EXTRACT },
        { FVIRTKEY | FCONTROL, (WORD)'A',  ID_ADD },
        { FVIRTKEY | FCONTROL, (WORD)'O',  IDM_FILE_OPEN },
        { FVIRTKEY | FCONTROL, (WORD)'T',  ID_TEST },
        { FVIRTKEY,              VK_DELETE, ID_DELETE     },
        { FVIRTKEY | FCONTROL, VK_F4,     ID_CLOSE      },  // 閉じる: アーカイブを閉じる
        // VK_RETURN は ListView/TreeView 内でコンテキストに応じて処理するためここでは定義しない
        { FVIRTKEY,              VK_ESCAPE, IDM_FILE_EXIT },  // 終了: アプリ終了
    };
    HACCEL hAccel = CreateAcceleratorTable(accelTable, _countof(accelTable));

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        bool consumed = wnd.PreTranslateMessage(msg) ||
                        TranslateAccelerator(wnd.Hwnd(), hAccel, &msg);
        // IsDialogMessageW は Tab ナビゲーション専用に絞る。
        // WM_SYSKEYDOWN を渡すと Alt+F 等のメニューニーモニックを内部で消費してしまい、
        // 「Alt 単独で一度メニューを有効化してから F」の二段操作が必要になる。
        if (!consumed && msg.message == WM_KEYDOWN && msg.wParam == VK_TAB) {
            consumed = IsDialogMessageW(wnd.Hwnd(), &msg) != 0;
        }
        if (!consumed) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (hAccel) DestroyAcceleratorTable(hAccel);
    return (int)msg.wParam;
}

int App::RunCompressMode(const std::vector<std::wstring>& filePaths, int nCmdShow) {
    MainWindow wnd;
    if (!wnd.Create(m_hInst, nCmdShow)) return 1;

    CompressDlg::Params params;
    params.inputFiles = filePaths;
    params.outputPath = m_settings.GetDefaultOutputDir();
    params.LoadFromSettings(m_settings);

    CompressDlg dlg;
    const auto* enc = m_sevenZip.IsLoaded() ? &m_sevenZip.GetEncoderNames() : nullptr;
    const auto* wf  = m_sevenZip.IsLoaded() ? &m_sevenZip.GetWritableFormats() : nullptr;
    if (!dlg.Show(wnd.Hwnd(), params, enc, wf)) {
        return 0;
    }
    params.SaveToSettings(m_settings);
    m_settings.Save();

    ProgressDlg progDlg;
    progDlg.Show(wnd.Hwnd(), L"圧縮中...");

    if (params.format == L"rar") {
        auto* sink = new ProgressPostSink(wnd.Hwnd(), WM_APP_PROGRESS, WM_APP_DONE);
        RunRarCompressSync(wnd.Hwnd(), params,
                           m_settings.GetRarExePath().c_str(),
                           progDlg, sink);
        delete sink;
    } else {
        auto* sink = new ProgressPostSink(wnd.Hwnd(), WM_APP_PROGRESS, WM_APP_DONE);
        auto& sz   = m_sevenZip;
        progDlg.SetSink(sink);

        WorkerThread worker;
        worker.Start([&sz, params, sink]() -> HRESULT {
            const wchar_t* pw = params.password.empty() ? nullptr : params.password.c_str();
            CompressAdvanced adv;
            adv.dictSize   = params.dictSize;
            adv.wordSize   = params.wordSize;
            adv.solidBlock = params.solidBlock;
            adv.threads    = params.threads;
            adv.extra      = params.extra;
            adv.volumeSize = params.volumeSize;
            return sz.Compress(params.inputFiles, params.outputPath.c_str(),
                               params.format.c_str(), params.level,
                               params.method.c_str(), pw, sink, &adv,
                               params.encryptHeaders);
        }, wnd.Hwnd(), WM_APP_DONE);

        progDlg.RunMessageLoop();
        worker.Wait();
        delete sink;
    }
    return 0;
}

int App::RunEmpty(int nCmdShow) {
    return RunBrowseMode({}, nCmdShow);
}

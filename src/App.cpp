#include "App.h"
#include "MainWindow.h"
#include "CompressDlg.h"
#include "CompressHelper.h"
#include "I18n.h"
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
        { FVIRTKEY | FCONTROL, (WORD)'U',  ID_ADD_TO_CURRENT },
        { FVIRTKEY | FCONTROL, (WORD)'O',  IDM_FILE_OPEN },
        { FVIRTKEY | FCONTROL, (WORD)'T',  ID_TEST },
        { FVIRTKEY,              VK_DELETE, ID_DELETE     },
        { FVIRTKEY | FCONTROL, VK_F4,     ID_CLOSE      },  // Close: close the archive
        { FVIRTKEY | FALT,     VK_RETURN, IDM_FILE_PROPERTIES }, // Alt+Enter: archive properties
        // VK_RETURN is handled contextually inside ListView/TreeView so not defined here
        { FVIRTKEY,              VK_ESCAPE, IDM_FILE_EXIT },  // Exit: quit the application
    };
    HACCEL hAccel = CreateAcceleratorTable(accelTable, _countof(accelTable));

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        bool consumed = wnd.PreTranslateMessage(msg) ||
                        TranslateAccelerator(wnd.Hwnd(), hAccel, &msg);
        // IsDialogMessageW is restricted to Tab navigation only.
        // Passing WM_SYSKEYDOWN causes it to internally consume Alt+F and similar menu mnemonics,
        // requiring a two-step operation (Alt alone to activate menu, then F) instead of Alt+F directly.
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

int App::RunCompressMode(const std::vector<std::wstring>& filePaths, int nCmdShow,
                         const std::wstring& destDir) {
    MainWindow wnd;
    if (!wnd.Create(m_hInst, nCmdShow)) return 1;

    CompressDlg::Params params;
    params.inputFiles = filePaths;
    params.outputPath = m_settings.GetDefaultOutputDir();
    params.LoadFromSettings(m_settings);
    if (!destDir.empty())
        params.outputPath = destDir;

    CompressDlg dlg;
    const auto* enc = m_sevenZip.IsLoaded() ? &m_sevenZip.GetEncoderNames() : nullptr;
    const auto* wf  = m_sevenZip.IsLoaded() ? &m_sevenZip.GetWritableFormats() : nullptr;
    if (!dlg.Show(wnd.Hwnd(), params, enc, wf)) {
        return 0;
    }
    params.SaveToSettings(m_settings);
    m_settings.Save();

    ProgressDlg progDlg;
    progDlg.Show(wnd.Hwnd(), I18n::Tr(IDS_PROGRESS_COMPRESSING).c_str());

    if (params.format == L"rar") {
        auto* sink = new ProgressPostSink(wnd.Hwnd(), WM_APP_PROGRESS, WM_APP_DONE);
        RunRarCompressSync(wnd.Hwnd(), params,
                           m_settings.GetRarExePath().c_str(),
                           progDlg, sink);
        delete sink;
    } else {
        // Resolve 7z SFX module (search same folder as 7z.dll if specified)
        std::wstring sfxModulePath;
        if (!params.sfxMode.empty()) {
            sfxModulePath = Resolve7zSfxModulePath(
                m_sevenZip.GetLoadedPath().c_str(), params.sfxMode.c_str());
            if (sfxModulePath.empty()) {
                progDlg.Dismiss();
                const wchar_t* leaf = (params.sfxMode == L"console") ? L"7zCon.sfx" : L"7z.sfx";
                std::wstring msg = I18n::TrFmt(IDS_FMT_SFX_NOT_FOUND_7Z, leaf);
                MessageBoxW(wnd.Hwnd(), msg.c_str(), I18n::Tr(IDS_APP_TITLE).c_str(), MB_ICONERROR);
                return 0;
            }
        }

        auto* sink = new ProgressPostSink(wnd.Hwnd(), WM_APP_PROGRESS, WM_APP_DONE);
        auto& sz   = m_sevenZip;
        progDlg.SetSink(sink);

        WorkerThread worker;
        worker.Start([&sz, params, sink, sfxModulePath]() -> HRESULT {
            const wchar_t* pw = params.password.empty() ? nullptr : params.password.c_str();
            CompressAdvanced adv;
            adv.dictSize      = params.dictSize;
            adv.wordSize      = params.wordSize;
            adv.solidBlock    = params.solidBlock;
            adv.threads       = params.threads;
            adv.extra         = params.extra;
            adv.volumeSize    = params.volumeSize;
            adv.sfxModulePath = sfxModulePath;
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

int App::RunExtractDialogMode(const std::wstring& archivePath, int nCmdShow,
                               const std::wstring& destDir) {
    MainWindow wnd;
    // SW_HIDE: suppress list window; only the extract folder picker and progress dialog appear.
    if (!wnd.Create(m_hInst, SW_HIDE)) return 1;
    wnd.OpenArchive(archivePath.c_str());
    wnd.TriggerExtract(destDir);
    return 0;
}

int App::RunEmpty(int nCmdShow) {
    return RunBrowseMode({}, nCmdShow);
}

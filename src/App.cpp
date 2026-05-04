#include "App.h"
#include "MainWindow.h"
#include "CompressDlg.h"
#include "ProgressDlg.h"
#include "WorkerThread.h"
#include "RarProcess.h"
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
        { FVIRTKEY,              VK_RETURN, ID_EXTRACT },
        { FVIRTKEY,              VK_ESCAPE, ID_CLOSE   },
    };
    HACCEL hAccel = CreateAcceleratorTable(accelTable, _countof(accelTable));

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!TranslateAccelerator(wnd.Hwnd(), hAccel, &msg) &&
            !IsDialogMessageW(wnd.Hwnd(), &msg)) {
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
    params.inputFiles    = filePaths;
    params.format        = m_settings.GetDefaultFormat();
    params.level         = m_settings.GetCompressionLevel();
    params.rarLevel      = m_settings.GetRarLevel();
    params.dictSize      = m_settings.GetAdvDictSize();
    params.wordSize      = m_settings.GetAdvWordSize();
    params.solidBlock    = m_settings.GetAdvSolidBlock();
    params.threads       = m_settings.GetAdvThreads();
    params.extra         = m_settings.GetAdvExtra();
    params.rarDictSize    = m_settings.GetRarAdvDictSize();
    params.rarSolid       = m_settings.GetRarAdvSolid();
    params.rarThreads     = m_settings.GetRarAdvThreads();
    params.rarRecoveryPct = m_settings.GetRarAdvRecovery();
    params.rarSplitVolume = m_settings.GetRarAdvVolume();
    params.rarExtra       = m_settings.GetRarAdvExtra();
    params.outputPath    = m_settings.GetDefaultOutputDir();

    CompressDlg dlg;
    const auto* enc = m_sevenZip.IsLoaded() ? &m_sevenZip.GetEncoderNames() : nullptr;
    if (!dlg.Show(wnd.Hwnd(), params, enc)) {
        return 0;
    }
    // Remember last-used settings
    m_settings.SetCompressionLevel(params.level);
    m_settings.SetRarLevel(params.rarLevel);
    m_settings.SetDefaultFormat(params.format.c_str());
    m_settings.SetAdvDictSize(params.dictSize.c_str());
    m_settings.SetAdvWordSize(params.wordSize.c_str());
    m_settings.SetAdvSolidBlock(params.solidBlock.c_str());
    m_settings.SetAdvThreads(params.threads.c_str());
    m_settings.SetAdvExtra(params.extra.c_str());
    m_settings.SetRarAdvDictSize(params.rarDictSize.c_str());
    m_settings.SetRarAdvSolid(params.rarSolid);
    m_settings.SetRarAdvThreads(params.rarThreads);
    m_settings.SetRarAdvRecovery(params.rarRecoveryPct);
    m_settings.SetRarAdvVolume(params.rarSplitVolume.c_str());
    m_settings.SetRarAdvExtra(params.rarExtra.c_str());
    m_settings.Save();

    ProgressDlg progDlg;
    progDlg.Show(wnd.Hwnd(), L"圧縮中...");

    if (params.format == L"rar") {
        RarAdvancedParams rarAdv;
        rarAdv.dictSize    = params.rarDictSize;
        rarAdv.solid       = params.rarSolid;
        rarAdv.threads     = params.rarThreads;
        rarAdv.recoveryPct = params.rarRecoveryPct;
        rarAdv.splitVolume = params.rarSplitVolume;
        rarAdv.extra       = params.rarExtra;
        RarProcess rar;
        const wchar_t* rarPw = params.password.empty() ? nullptr : params.password.c_str();
        if (!rar.Compress(params.inputFiles, params.outputPath.c_str(),
                          params.method.c_str(),
                          m_settings.GetRarExePath().c_str(),
                          rarPw, params.encryptHeaders,
                          wnd.Hwnd(), WM_APP_PROGRESS, WM_APP_DONE, &rarAdv)) {
            progDlg.Dismiss();
            return 0;
        }
        MSG msg = {};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_APP_DONE) {
                progDlg.SetDone(S_OK);
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
            return sz.Compress(params.inputFiles, params.outputPath.c_str(),
                               params.format.c_str(), params.level,
                               params.method.c_str(), pw, sink, &adv);
        }, wnd.Hwnd(), WM_APP_DONE);

        MSG msg = {};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_APP_DONE) {
                progDlg.SetDone((HRESULT)msg.wParam);
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
        worker.Wait();
        delete sink;
    }
    return 0;
}

int App::RunEmpty(int nCmdShow) {
    return RunBrowseMode({}, nCmdShow);
}

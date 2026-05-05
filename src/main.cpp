#include <windows.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include "App.h"

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    // Belt-and-suspenders alongside manifest: enables PerMonitorV2 on older loaders
    typedef BOOL (WINAPI* FnSetDpiCtx)(DPI_AWARENESS_CONTEXT);
    if (auto fn = (FnSetDpiCtx)GetProcAddress(GetModuleHandleW(L"user32"), "SetProcessDpiAwarenessContext"))
        fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // SevenZip のロードを先に行い、拡張子判定に使えるようにする
    App& app = App::Instance();
    if (!app.Init(hInst)) {
        MessageBoxW(nullptr, L"初期化に失敗しました。", L"AileEx", MB_ICONERROR);
        return 1;
    }

    // Parse command-line arguments
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    std::vector<std::wstring> archiveFiles, regularFiles;
    auto& sz7 = app.Get7z();
    for (int i = 1; i < argc; ++i) {
        const wchar_t* dot = wcsrchr(argv[i], L'.');
        bool isArc = dot && sz7.IsLoaded() && sz7.IsArchiveExt(dot + 1);
        if (isArc)
            archiveFiles.push_back(argv[i]);
        else
            regularFiles.push_back(argv[i]);
    }
    if (argv) LocalFree(argv);

    int result;
    if (!archiveFiles.empty()) {
        result = app.RunBrowseMode(archiveFiles, nCmdShow);
    } else if (!regularFiles.empty()) {
        result = app.RunCompressMode(regularFiles, nCmdShow);
    } else {
        result = app.RunEmpty(nCmdShow);
    }

    app.Shutdown();
    return result;
}

#include <windows.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include "App.h"

static const wchar_t* kArchiveExts[] = {
    L"7z", L"zip", L"rar", L"tar", L"gz", L"bz2", L"xz",
    L"cab", L"iso", L"jar", L"wim", L"lzma", L"lzh", L"arj",
    nullptr
};

static bool IsArchiveExtension(const wchar_t* path) {
    const wchar_t* dot = wcsrchr(path, L'.');
    if (!dot) return false;
    for (int i = 0; kArchiveExts[i]; ++i)
        if (_wcsicmp(dot + 1, kArchiveExts[i]) == 0) return true;
    return false;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    // Belt-and-suspenders alongside manifest: enables PerMonitorV2 on older loaders
    typedef BOOL (WINAPI* FnSetDpiCtx)(DPI_AWARENESS_CONTEXT);
    if (auto fn = (FnSetDpiCtx)GetProcAddress(GetModuleHandleW(L"user32"), "SetProcessDpiAwarenessContext"))
        fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // Parse command-line arguments
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    std::vector<std::wstring> archiveFiles, regularFiles;
    for (int i = 1; i < argc; ++i) {
        if (IsArchiveExtension(argv[i]))
            archiveFiles.push_back(argv[i]);
        else
            regularFiles.push_back(argv[i]);
    }
    if (argv) LocalFree(argv);

    App& app = App::Instance();
    if (!app.Init(hInst)) {
        MessageBoxW(nullptr, L"初期化に失敗しました。", L"AileEx", MB_ICONERROR);
        return 1;
    }

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

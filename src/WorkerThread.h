#pragma once
#include <windows.h>
#include <functional>
#include <string>

// Progress sink interface: DLL callbacks call into this, which posts WM_APP_PROGRESS.
class IExtractProgressSink {
public:
    virtual void OnSetTotal(UINT64 total) = 0;
    virtual void OnProgress(UINT64 done, const wchar_t* currentFile) = 0;
    virtual bool IsCancelled() const = 0;
    virtual ~IExtractProgressSink() = default;
};

// Concrete sink: bridges DLL callbacks → PostMessage(hwnd, WM_APP_PROGRESS, pct, (LPARAM)_wcsdup(file))
// WM_APP_PROGRESS: wParam = 0-100 percent, lParam = wchar_t* (caller must free())
class ProgressPostSink : public IExtractProgressSink {
public:
    ProgressPostSink(HWND hwnd, UINT progressMsg, UINT doneMsg);

    void OnSetTotal(UINT64 total) override;
    void OnProgress(UINT64 done, const wchar_t* currentFile) override;
    bool IsCancelled() const override;
    void SetCancelled(bool v);

private:
    HWND         m_hwnd;
    UINT         m_progressMsg;
    UINT         m_doneMsg;
    UINT64       m_total       = 0;
    volatile bool m_cancelled  = false;
};

// Generic worker: runs a std::function<HRESULT()> on a new thread.
// Posts WM_APP_DONE(wParam=HRESULT) when the task finishes.
class WorkerThread {
public:
    using Task = std::function<HRESULT()>;

    ~WorkerThread();

    void Start(Task task, HWND hwndNotify, UINT doneMsg);
    void RequestCancel();
    bool IsRunning() const;
    void Wait();

private:
    struct Ctx {
        Task  task;
        HWND  hwnd;
        UINT  doneMsg;
    };
    static DWORD WINAPI ThreadProc(LPVOID param);

    HANDLE m_hThread = INVALID_HANDLE_VALUE;
};

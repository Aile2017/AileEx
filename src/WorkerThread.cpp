#include "WorkerThread.h"
#include "resource.h"
#include <cstdlib>

// ---- ProgressPostSink ----

ProgressPostSink::ProgressPostSink(HWND hwnd, UINT progressMsg, UINT doneMsg)
    : m_hwnd(hwnd), m_progressMsg(progressMsg), m_doneMsg(doneMsg)
{}

void ProgressPostSink::OnSetTotal(UINT64 total) {
    m_total = total;
}

void ProgressPostSink::OnProgress(UINT64 done, const wchar_t* currentFile) {
    if (!m_hwnd) return;
    // ~50 ms throttle. Without this, thousands of WM_APP_PROGRESS messages per second
    // overflow the message queue, causing WM_LBUTTONDOWN on the cancel button to be
    // delayed for seconds (= appears unresponsive).
    DWORD now = GetTickCount();
    if (now - m_lastPostTick < 50) return;
    m_lastPostTick = now;

    int pct = (m_total > 0) ? (int)(done * 100 / m_total) : 0;
    if (pct > 100) pct = 100;
    // Only allocate _wcsdup after PostMessage succeeds to avoid leaks on queue overflow.
    wchar_t* copy = currentFile ? _wcsdup(currentFile) : nullptr;
    if (!PostMessageW(m_hwnd, m_progressMsg, (WPARAM)pct, (LPARAM)copy)) {
        // PostMessage failed (e.g., window destroyed, queue full): free the copy.
        free(copy);
    }
}

bool ProgressPostSink::IsCancelled() const {
    return m_cancelled;
}

void ProgressPostSink::SetCancelled(bool v) {
    m_cancelled = v;
}

// ---- WorkerThread ----

WorkerThread::~WorkerThread() {
    Wait();
}

void WorkerThread::Start(Task task, HWND hwndNotify, UINT doneMsg) {
    Wait();
    auto* ctx = new Ctx{std::move(task), hwndNotify, doneMsg};
    m_hThread = CreateThread(nullptr, 0, ThreadProc, ctx, 0, nullptr);
}

void WorkerThread::RequestCancel() {
    // Cancellation is handled via the ProgressPostSink::SetCancelled flag.
    // WorkerThread itself has no direct cancel — the task lambda checks the sink.
}

bool WorkerThread::IsRunning() const {
    if (m_hThread == INVALID_HANDLE_VALUE) return false;
    return WaitForSingleObject(m_hThread, 0) == WAIT_TIMEOUT;
}

void WorkerThread::Wait() {
    if (m_hThread != INVALID_HANDLE_VALUE) {
        WaitForSingleObject(m_hThread, INFINITE);
        CloseHandle(m_hThread);
        m_hThread = INVALID_HANDLE_VALUE;
    }
}

DWORD WINAPI WorkerThread::ThreadProc(LPVOID param) {
    auto* ctx = static_cast<Ctx*>(param);
    HRESULT hr = ctx->task();
    PostMessageW(ctx->hwnd, ctx->doneMsg, (WPARAM)hr, 0);
    delete ctx;
    return 0;
}

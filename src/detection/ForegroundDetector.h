#pragma once

#include "utilities/NonCopyable.h"
#include "detection/ProcessHelper.h"
#include <functional>
#include <string>
#include <windows.h>

namespace prismshift {

// Event-driven foreground tracker.
//
// Windows does not guarantee that EVENT_SYSTEM_FOREGROUND is delivered every
// time the foreground changes. Popups, overlays, secure-desktop transitions and
// windows that are destroyed while focused can all leave the last event stale,
// which is why a purely event-driven tracker can get stuck on the wrong profile.
//
// This detector therefore combines three cheap signals:
//   1. EVENT_SYSTEM_FOREGROUND (debounced so bursts collapse into one check).
//   2. EVENT_OBJECT_DESTROY / EVENT_OBJECT_HIDE for the currently tracked
//      window (the usual way a popup/overlay disappears).
//   3. Refresh(), called from a low-frequency watchdog by the application.
//
// Refresh() is O(1) while nothing changes: it only calls GetForegroundWindow,
// compares the handle and validates the owning process id. Process resolution
// only happens when the handle changes.
class ForegroundDetector : public NonCopyable {
public:
    using ForegroundCallback = std::function<void(HWND hwnd, const ProcessInfo& process)>;

    ForegroundDetector();
    ~ForegroundDetector();

    bool Start(ForegroundCallback callback);
    void Stop();

    [[nodiscard]] bool IsRunning() const { return m_foregroundHook != nullptr; }

    // Cheap reconciliation pass. Reports the foreground only when the window or
    // its process actually changed since the last report.
    void Refresh();

    // Re-evaluates and reports the current foreground even if it has not
    // changed. Used at startup, after resuming from pause, and after a tracked
    // window disappears.
    void ForceRefresh();

    // Backwards-compatible name for an authoritative, immediate re-check.
    void CheckCurrentForeground() { ForceRefresh(); }

    [[nodiscard]] HWND GetLastHwnd() const { return m_lastHwnd; }
    [[nodiscard]] DWORD GetLastPid() const { return m_lastPid; }
    [[nodiscard]] const std::wstring& GetLastExeName() const { return m_lastExeName; }

private:
    static constexpr UINT_PTR TIMER_SETTLE = 0x4C4D; // 'LM'
    static constexpr UINT SETTLE_DELAY_MS = 150;
    static constexpr UINT RETRY_DELAY_MS = 400;

    static void CALLBACK WinEventProc(
        HWINEVENTHOOK hWinEventHook,
        DWORD event,
        HWND hwnd,
        LONG idObject,
        LONG idChild,
        DWORD idEventThread,
        DWORD dwmsEventTime
    );

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    bool CreateMessageWindow();
    void DestroyMessageWindow();

    void ScheduleRefresh(UINT delayMs);
    void RefreshInternal(bool forceNotify);
    static bool IsOwnProcessWindow(HWND hwnd);
    static bool IsTransientOverlay(HWND hwnd);

    HWINEVENTHOOK m_foregroundHook = nullptr;
    HWINEVENTHOOK m_destroyHook = nullptr;
    HWINEVENTHOOK m_hideHook = nullptr;

    HWND m_messageHwnd = nullptr;
    bool m_classRegistered = false;

    ForegroundCallback m_callback;
    HWND m_lastHwnd = nullptr;
    DWORD m_lastPid = 0;
    std::wstring m_lastExeName;

    static inline ForegroundDetector* s_instance = nullptr;
};

} // namespace prismshift

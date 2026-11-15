#include "detection/ForegroundDetector.h"
#include "utilities/Logger.h"
#include <cwchar>
#include <iterator>

namespace prismshift {

namespace {
constexpr const wchar_t* DETECTOR_WINDOW_CLASS = L"Lumetra_ForegroundDetectorClass";

bool IsShellWindow(HWND hwnd) {
    wchar_t className[64] = {};
    if (GetClassNameW(hwnd, className, static_cast<int>(std::size(className))) == 0) {
        return false;
    }

    return wcscmp(className, L"Progman") == 0 ||
           wcscmp(className, L"WorkerW") == 0 ||
           wcscmp(className, L"Shell_TrayWnd") == 0 ||
           wcscmp(className, L"Shell_SecondaryTrayWnd") == 0;
}
} // namespace

ForegroundDetector::ForegroundDetector() {
    s_instance = this;
}

ForegroundDetector::~ForegroundDetector() {
    Stop();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool ForegroundDetector::Start(ForegroundCallback callback) {
    if (m_foregroundHook) {
        return true;
    }

    m_callback = std::move(callback);
    m_lastHwnd = nullptr;
    m_lastPid = 0;
    m_lastExeName.clear();

    // The detector owns a hidden message-only window for its debounce timer.
    // It lives on the same thread as the application message loop.
    CreateMessageWindow();

    // Out-of-context hook: no DLL injection, anti-cheat friendly.
    m_foregroundHook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND,
        EVENT_SYSTEM_FOREGROUND,
        nullptr,
        &WinEventProc,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );

    if (!m_foregroundHook) {
        Logger::Error(L"Failed to register WinEvent hook for EVENT_SYSTEM_FOREGROUND. Error: " +
                      std::to_wstring(GetLastError()));
        DestroyMessageWindow();
        m_callback = nullptr;
        return false;
    }

    // A popup/overlay almost always destroys or hides its window when it is
    // dismissed. Watching those events for the currently tracked window lets us
    // recover immediately instead of waiting for a foreground event that
    // Windows may never send. These hooks are optional; the watchdog still
    // covers us if they cannot be installed.
    m_destroyHook = SetWinEventHook(
        EVENT_OBJECT_DESTROY,
        EVENT_OBJECT_DESTROY,
        nullptr,
        &WinEventProc,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );

    m_hideHook = SetWinEventHook(
        EVENT_OBJECT_HIDE,
        EVENT_OBJECT_HIDE,
        nullptr,
        &WinEventProc,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );

    if (!m_destroyHook || !m_hideHook) {
        Logger::Warn(L"ForegroundDetector could not install window teardown hooks; "
                     L"relying on the foreground watchdog for popup recovery.");
    }

    Logger::Info(L"Event-driven foreground window detector started successfully.");
    return true;
}

void ForegroundDetector::Stop() {
    if (m_foregroundHook) {
        UnhookWinEvent(m_foregroundHook);
        m_foregroundHook = nullptr;
    }
    if (m_destroyHook) {
        UnhookWinEvent(m_destroyHook);
        m_destroyHook = nullptr;
    }
    if (m_hideHook) {
        UnhookWinEvent(m_hideHook);
        m_hideHook = nullptr;
    }

    DestroyMessageWindow();

    if (m_callback) {
        Logger::Info(L"Foreground window detector stopped.");
    }

    m_callback = nullptr;
    m_lastHwnd = nullptr;
    m_lastPid = 0;
    m_lastExeName.clear();
}

void ForegroundDetector::Refresh() {
    RefreshInternal(false);
}

void ForegroundDetector::ForceRefresh() {
    RefreshInternal(true);
}

void CALLBACK ForegroundDetector::WinEventProc(
    HWINEVENTHOOK /*hWinEventHook*/,
    DWORD event,
    HWND hwnd,
    LONG idObject,
    LONG idChild,
    DWORD /*idEventThread*/,
    DWORD /*dwmsEventTime*/
) {
    if (!s_instance) {
        return;
    }

    switch (event) {
        case EVENT_SYSTEM_FOREGROUND:
            if (idObject == OBJID_WINDOW && idChild == CHILDID_SELF) {
                // Coalesce bursts (focus flicker, popup open/close, alt-tab).
                s_instance->ScheduleRefresh(SETTLE_DELAY_MS);
            }
            break;

        case EVENT_OBJECT_DESTROY:
        case EVENT_OBJECT_HIDE:
            if (idObject == OBJID_WINDOW && idChild == CHILDID_SELF &&
                hwnd != nullptr && hwnd == s_instance->m_lastHwnd) {
                // The window we currently consider foreground is going away.
                // Give Windows a moment to hand focus back, then re-read it.
                s_instance->ScheduleRefresh(SETTLE_DELAY_MS);
            }
            break;

        default:
            break;
    }
}

void ForegroundDetector::RefreshInternal(bool forceNotify) {
    if (!m_callback) {
        return;
    }

    HWND hwnd = GetForegroundWindow();
    if (!hwnd || !IsWindow(hwnd)) {
        // Foreground is momentarily undefined while a window is torn down or a
        // desktop transition is in progress. Do not flap to the default
        // profile; retry shortly and let the watchdog act as a backstop.
        ScheduleRefresh(RETRY_DELAY_MS);
        return;
    }

    // Fast path: the tracked window is still foreground. Validate the owning
    // process id cheaply (no OpenProcess) to guard against HWND reuse after a
    // teardown. This keeps the 1 Hz watchdog effectively free while idle.
    if (hwnd == m_lastHwnd && !forceNotify) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == m_lastPid) {
            return;
        }
    }

    // Mirror WINEVENT_SKIPOWNPROCESS. This also prevents the watchdog from
    // cancelling a live profile preview while the user is in Lumetra's UI.
    if (IsOwnProcessWindow(hwnd)) {
        return;
    }

    // Overlays, notification surfaces and menus are not a real application
    // context. Keep the profile of the app the user was actually using.
    if (IsTransientOverlay(hwnd)) {
        ScheduleRefresh(RETRY_DELAY_MS);
        return;
    }

    ProcessInfo info = ProcessHelper::GetProcessInfoFromWindow(hwnd);
    if (!info.IsValid()) {
        // Unidentified or protected system process: treat as unmanaged.
        info.pid = 0;
        info.exeName = L"unmanaged";
    }

    const bool sameWindow = (hwnd == m_lastHwnd);
    const bool sameProcess = (info.pid == m_lastPid && info.exeName == m_lastExeName);

    if (sameWindow && sameProcess && !forceNotify) {
        return;
    }

    m_lastHwnd = hwnd;
    m_lastPid = info.pid;
    m_lastExeName = info.exeName;

    // A different window belonging to the same application (dialog, owned
    // popup, etc.) does not require a profile switch.
    if (sameProcess && !forceNotify) {
        return;
    }

    m_callback(hwnd, info);
}

bool ForegroundDetector::IsOwnProcessWindow(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid != 0 && pid == GetCurrentProcessId();
}

bool ForegroundDetector::IsTransientOverlay(HWND hwnd) {
    // The shell desktop/taskbar are real foreground targets (they map to the
    // default profile). Never classify them as transient.
    if (IsShellWindow(hwnd)) {
        return false;
    }

    if (!IsWindowVisible(hwnd)) {
        return true;
    }

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect) || IsRectEmpty(&rect)) {
        return true;
    }

    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const bool toolWindow = (exStyle & WS_EX_TOOLWINDOW) != 0;
    const bool noActivate = (exStyle & WS_EX_NOACTIVATE) != 0;
    const bool layered = (exStyle & WS_EX_LAYERED) != 0;
    const bool transparent = (exStyle & WS_EX_TRANSPARENT) != 0;

    // Notification surfaces, overlays and on-screen widgets. WS_EX_NOACTIVATE
    // windows cannot be a normal user focus target.
    if (noActivate) {
        return true;
    }

    // Overlay windows are typically layered/transparent tool windows (Steam,
    // Discord, game HUDs, notification surfaces). A plain tool window is left
    // alone so legitimate utility apps still get their profile.
    if (toolWindow && (layered || transparent)) {
        return true;
    }

    return false;
}

void ForegroundDetector::ScheduleRefresh(UINT delayMs) {
    if (!m_callback) {
        return;
    }

    if (m_messageHwnd) {
        // SetTimer with the same id replaces the pending timer, so a burst of
        // events results in a single check after things settle.
        SetTimer(m_messageHwnd, TIMER_SETTLE, delayMs, nullptr);
    } else {
        RefreshInternal(false);
    }
}

bool ForegroundDetector::CreateMessageWindow() {
    if (m_messageHwnd) {
        return true;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = DETECTOR_WINDOW_CLASS;

    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        Logger::Warn(L"ForegroundDetector: failed to register timer window class.");
        return false;
    }
    m_classRegistered = true;

    m_messageHwnd = CreateWindowExW(
        0,
        DETECTOR_WINDOW_CLASS,
        L"LumetraForegroundDetector",
        0, 0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        wc.hInstance,
        this
    );

    if (!m_messageHwnd) {
        Logger::Warn(L"ForegroundDetector: failed to create timer window.");
        return false;
    }

    return true;
}

void ForegroundDetector::DestroyMessageWindow() {
    if (m_messageHwnd) {
        KillTimer(m_messageHwnd, TIMER_SETTLE);
        DestroyWindow(m_messageHwnd);
        m_messageHwnd = nullptr;
    }

    if (m_classRegistered) {
        UnregisterClassW(DETECTOR_WINDOW_CLASS, GetModuleHandleW(nullptr));
        m_classRegistered = false;
    }
}

LRESULT CALLBACK ForegroundDetector::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (s_instance) {
        return s_instance->HandleMessage(hwnd, uMsg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

LRESULT ForegroundDetector::HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_TIMER && wParam == TIMER_SETTLE) {
        KillTimer(hwnd, TIMER_SETTLE);
        RefreshInternal(false);
        return 0;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

} // namespace prismshift

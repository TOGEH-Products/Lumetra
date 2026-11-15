#include "ui/TrayIcon.h"
#include "utilities/Logger.h"
#include "utilities/AppIcon.h"

namespace prismshift {

TrayIcon::TrayIcon() = default;

TrayIcon::~TrayIcon() {
    Remove();
}

bool TrayIcon::Initialize(HWND hwndNotify, HINSTANCE /*hInstance*/) {
    if (m_added) {
        return true;
    }

    m_hwndNotify = hwndNotify;

    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = hwndNotify;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon = LoadAppIcon();
    wcscpy_s(m_nid.szTip, L"Lumetra - Per-Game Color Profiles");

    if (Shell_NotifyIconW(NIM_ADD, &m_nid)) {
        m_added = true;
        Logger::Info(L"System tray icon initialized.");
        return true;
    }

    Logger::Warn(L"Failed to register system tray icon.");
    return false;
}

void TrayIcon::Remove() {
    if (m_added) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_added = false;
        Logger::Info(L"System tray icon removed.");
    }
}

void TrayIcon::SetTooltip(const std::wstring& tooltip) {
    if (!m_added) return;
    wcsncpy_s(m_nid.szTip, tooltip.c_str(), _TRUNCATE);
    m_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void TrayIcon::ShowBalloonTip(const std::wstring& title, const std::wstring& msg) {
    if (!m_added) return;
    m_nid.uFlags = NIF_INFO;
    wcsncpy_s(m_nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(m_nid.szInfo, msg.c_str(), _TRUNCATE);
    m_nid.dwInfoFlags = NIIF_INFO;
    m_nid.uTimeout = 3000;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void TrayIcon::ShowContextMenu(HWND hwnd, bool isPaused, bool startupEnabled) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    // Header info item (disabled)
    AppendMenuW(hMenu, MF_STRING | MF_DISABLED | MF_GRAYED, IDM_TRAY_TITLE, L"Lumetra v0.2.0");

    // Switching status item
    std::wstring statusStr = isPaused ? L"● Switching: PAUSED" : L"● Automatic Switching: ON";
    AppendMenuW(hMenu, MF_STRING | MF_DISABLED | MF_GRAYED, IDM_TRAY_STATUS, statusStr.c_str());

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // Primary action: Open panel
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_OPEN, L"&Open Lumetra");
    SetMenuDefaultItem(hMenu, IDM_TRAY_OPEN, FALSE);

    // Toggle pause/resume
    std::wstring pauseStr = isPaused ? L"&Resume Switching" : L"&Pause Switching";
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_TOGGLE_PAUSE, pauseStr.c_str());

    std::wstring startupStr = startupEnabled ? L"Disable Start with Windows" : L"Start with Windows";
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_TOGGLE_STARTUP, startupStr.c_str());

    // Display rescue reset
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_RESET_DISPLAY, L"&Reset Display Colors");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // Exit application
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"E&xit");

    // Required for proper menu dismissal when user clicks away
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
}

} // namespace prismshift

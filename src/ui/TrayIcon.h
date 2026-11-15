#pragma once

#include "utilities/NonCopyable.h"
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <functional>

namespace prismshift {

constexpr UINT WM_TRAYICON = WM_USER + 100;

enum TrayCommand : UINT {
    IDM_TRAY_TITLE         = 2000,
    IDM_TRAY_STATUS        = 2001,
    IDM_TRAY_OPEN          = 2002,
    IDM_TRAY_TOGGLE_PAUSE  = 2003,
    IDM_TRAY_RESET_DISPLAY = 2004,
    IDM_TRAY_EXIT          = 2005,
    IDM_TRAY_TOGGLE_STARTUP = 2006
};

class TrayIcon : public NonCopyable {
public:
    TrayIcon();
    ~TrayIcon();

    bool Initialize(HWND hwndNotify, HINSTANCE hInstance);
    void Remove();

    void ShowContextMenu(HWND hwnd, bool isPaused, bool startupEnabled);
    void ShowBalloonTip(const std::wstring& title, const std::wstring& msg);

    void SetTooltip(const std::wstring& tooltip);

private:
    NOTIFYICONDATAW m_nid{};
    HWND m_hwndNotify = nullptr;
    bool m_added = false;
};

} // namespace prismshift

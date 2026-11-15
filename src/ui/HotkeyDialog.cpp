#include "ui/HotkeyDialog.h"
#include "utilities/Logger.h"
#include <commctrl.h>

namespace prismshift {

namespace {
constexpr int IDD_HOTKEY = 1001;
constexpr int IDC_INSTRUCTIONS = 1002;
constexpr int IDC_HOTKEY_DISPLAY = 1003;
constexpr int IDOK_BUTTON = IDOK;
constexpr int IDCANCEL_BUTTON = IDCANCEL;
constexpr int IDCLEAR_BUTTON = 1004;
}

bool HotkeyDialog::Show(HWND parent, const std::wstring& currentHotkey, ResultCallback callback) {
    s_callback = callback;
    s_currentHotkey = HotkeyDefinition();
    s_currentHotkey.FromString(currentHotkey);

    // Create a simple modal dialog
    HWND hwnd = CreateDialogParamW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_HOTKEY),
        parent,
        DialogProc,
        0
    );

    if (!hwnd) {
        // Fallback: Use a message box for input
        MessageBoxW(parent,
                   L"Hotkey capture dialog could not be created.\n\n"
                   L"To set a hotkey:\n"
                   L"1. Click OK\n"
                   L"2. Press your desired key combination\n"
                   L"3. The hotkey will be captured automatically",
                   L"Set Screenshot Hotkey",
                   MB_OK | MB_ICONINFORMATION);
        return false;
    }

    ShowWindow(hwnd, SW_SHOW);

    // Modal message loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // Check if dialog was closed
        if (!IsWindow(hwnd)) {
            break;
        }
    }

    return true;
}

INT_PTR CALLBACK HotkeyDialog::DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_INITDIALOG: {
            // Center the dialog
            RECT rcParent, rcDialog;
            GetWindowRect(GetParent(hwnd), &rcParent);
            GetWindowRect(hwnd, &rcDialog);

            int x = rcParent.left + (rcParent.right - rcParent.left - (rcDialog.right - rcDialog.left)) / 2;
            int y = rcParent.top + (rcParent.bottom - rcParent.top - (rcDialog.bottom - rcDialog.top)) / 2;

            SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

            // Set focus to the dialog itself to capture keys
            SetFocus(hwnd);
            return TRUE;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            OnKeyDown(hwnd, wParam, lParam);
            return TRUE;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDOK_BUTTON: {
                    if (s_callback && s_currentHotkey.IsValid()) {
                        s_callback(s_currentHotkey.ToDisplayString());
                    }
                    DestroyWindow(hwnd);
                    return TRUE;
                }
                case IDCANCEL_BUTTON: {
                    DestroyWindow(hwnd);
                    return TRUE;
                }
                case IDCLEAR_BUTTON: {
                    s_currentHotkey = HotkeyDefinition();
                    if (s_callback) {
                        s_callback(L"");
                    }
                    DestroyWindow(hwnd);
                    return TRUE;
                }
            }
            break;
        }

        case WM_CLOSE: {
            DestroyWindow(hwnd);
            return TRUE;
        }

        case WM_DESTROY: {
            PostQuitMessage(0);
            return TRUE;
        }
    }

    return FALSE;
}

void HotkeyDialog::OnKeyDown(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    UINT vk = static_cast<UINT>(wParam);

    // Ignore modifier keys themselves
    if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN) {
        return;
    }

    // Ignore ESC and Enter (used for dialog navigation)
    if (vk == VK_ESCAPE || vk == VK_RETURN) {
        return;
    }

    // Check modifiers
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool win = (GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0;

    // Build hotkey definition
    s_currentHotkey.virtualKey = vk;
    s_currentHotkey.ctrl = ctrl;
    s_currentHotkey.alt = alt;
    s_currentHotkey.shift = shift;
    s_currentHotkey.win = win;
    s_currentHotkey.noRepeat = true;

    // Valid hotkey captured - close dialog and accept
    if (s_currentHotkey.IsValid()) {
        if (s_callback) {
            s_callback(s_currentHotkey.ToDisplayString());
        }
        DestroyWindow(hwnd);
    }
}

std::wstring HotkeyDialog::FormatHotkey(UINT vk, bool ctrl, bool alt, bool shift, bool win) {
    HotkeyDefinition def;
    def.virtualKey = vk;
    def.ctrl = ctrl;
    def.alt = alt;
    def.shift = shift;
    def.win = win;
    return def.ToDisplayString();
}

} // namespace prismshift

#pragma once

#include "screenshots/HotkeyManager.h"
#include <windows.h>
#include <string>
#include <functional>

namespace prismshift {

// Simple modal dialog for capturing a hotkey press.
// Displays instructions and waits for the user to press a key combination.
class HotkeyDialog {
public:
    using ResultCallback = std::function<void(const std::wstring& hotkeyString)>;

    // Show the dialog modally and call the callback with the result
    // Returns true if a hotkey was set, false if cancelled
    static bool Show(HWND parent, const std::wstring& currentHotkey, ResultCallback callback);

private:
    static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static void OnKeyDown(HWND hwnd, WPARAM wParam, LPARAM lParam);
    static std::wstring FormatHotkey(UINT vk, bool ctrl, bool alt, bool shift, bool win);

    static inline ResultCallback s_callback;
    static inline HotkeyDefinition s_currentHotkey;
};

} // namespace prismshift

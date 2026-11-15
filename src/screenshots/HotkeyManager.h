#pragma once

#include "utilities/NonCopyable.h"
#include <windows.h>
#include <string>
#include <optional>
#include <functional>
#include <map>
#include <vector>

namespace prismshift {

// Hotkey definition with modifiers and virtual key code
struct HotkeyDefinition {
    UINT virtualKey = 0;  // VK_* code (e.g., VK_F12, 'S', VK_SNAPSHOT)
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    bool win = false;
    bool noRepeat = true; // Prevent repeated triggers when held

    std::wstring profileId; // Empty for global, or profile ID for per-game hotkeys

    [[nodiscard]] bool IsValid() const;
    [[nodiscard]] UINT ToWin32Modifiers() const;
    [[nodiscard]] std::wstring ToDisplayString() const;

    // Parse from string like "Ctrl+Shift+F12" or "Win+Print Screen"
    bool FromString(const std::wstring& str);

    bool operator==(const HotkeyDefinition& other) const {
        return virtualKey == other.virtualKey &&
               ctrl == other.ctrl &&
               alt == other.alt &&
               shift == other.shift &&
               win == other.win;
    }
};

// Manages global and per-profile screenshot hotkeys.
// Uses Win32 RegisterHotKey to intercept keys system-wide without polling.
class HotkeyManager : public NonCopyable {
public:
    using HotkeyCallback = std::function<void(const HotkeyDefinition&)>;

    struct HotkeyRegistration {
        int id = 0;
        HotkeyDefinition definition;
        HotkeyCallback callback;
        std::wstring profileId;
    };

    HotkeyManager();
    ~HotkeyManager();

    // Initialize with the message window that will receive WM_HOTKEY messages
    bool Initialize(HWND messageWindow);
    void Shutdown();

    [[nodiscard]] bool IsInitialized() const { return m_initialized; }

    // Register a hotkey and return its ID (nullopt on failure)
    std::optional<int> RegisterHotkey(const HotkeyDefinition& definition, HotkeyCallback callback);

    // Unregister a specific hotkey by ID
    bool UnregisterHotkey(int hotkeyId);

    // Unregister all hotkeys
    void UnregisterAll();

    // Unregister all hotkeys associated with a profile
    void UnregisterForProfile(const std::wstring& profileId);

    // Handle WM_HOTKEY message (call from window procedure)
    // Returns true if the message was handled
    bool HandleHotkeyMessage(WPARAM wParam, LPARAM lParam);

    // Get all currently registered hotkeys
    [[nodiscard]] std::vector<HotkeyRegistration> GetRegisteredHotkeys() const;

private:
    bool m_initialized = false;
    HWND m_messageWindow = nullptr;
    std::map<int, HotkeyRegistration> m_registeredHotkeys;
};

} // namespace prismshift

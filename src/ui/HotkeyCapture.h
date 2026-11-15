#pragma once

#include "ui/Renderer.h"
#include <windows.h>
#include <functional>
#include <string>
#include <set>

namespace prismshift {

// Interactive hotkey capture control.
// Click to activate listening mode, then press key combination.
// Holding modifiers (Ctrl, Shift, Alt, Win) keeps listening; pressing a
// non-modifier key confirms the combination immediately.
class HotkeyCapture {
public:
    using ChangeCallback = std::function<void(const std::wstring& hotkey)>;

    HotkeyCapture();
    ~HotkeyCapture() = default;

    void SetHotkey(const std::wstring& hotkey) { m_hotkey = hotkey; }
    [[nodiscard]] const std::wstring& GetHotkey() const { return m_hotkey; }

    void SetChangeCallback(ChangeCallback cb) { m_onChange = std::move(cb); }

    [[nodiscard]] bool IsListening() const { return m_isListening; }

    // Call this to start listening (e.g., on click)
    void BeginListen();

    // Call this to cancel listening (e.g., on Escape or click outside)
    void CancelListen();

    // Returns true if the key was consumed by this control
    bool OnKeyDown(WPARAM wParam);
    bool OnKeyUp(WPARAM wParam);

    // Paint the control within the given rect
    void Paint(Renderer& r, const RECT& rc, bool hover);

private:
    void UpdateListeningState();
    void ConfirmHotkey();
    std::wstring BuildHotkeyString() const;
    [[nodiscard]] bool IsModifierKey(WPARAM wParam) const;

    std::wstring m_hotkey;
    bool m_isListening = false;

    // Currently held keys during listening
    std::set<WPARAM> m_heldKeys;
    bool m_ctrlHeld = false;
    bool m_shiftHeld = false;
    bool m_altHeld = false;
    bool m_winHeld = false;
    WPARAM m_primaryKey = 0; // The non-modifier key

    ChangeCallback m_onChange;
};

} // namespace prismshift

#include "ui/HotkeyCapture.h"
#include "ui/UITheme.h"
#include "ui/Icons.h"
#include "ui/Controls.h"
#include <algorithm>

namespace prismshift {

namespace {
std::wstring GetKeyName(WPARAM vk) {
    switch (vk) {
        case VK_F1: return L"F1";
        case VK_F2: return L"F2";
        case VK_F3: return L"F3";
        case VK_F4: return L"F4";
        case VK_F5: return L"F5";
        case VK_F6: return L"F6";
        case VK_F7: return L"F7";
        case VK_F8: return L"F8";
        case VK_F9: return L"F9";
        case VK_F10: return L"F10";
        case VK_F11: return L"F11";
        case VK_F12: return L"F12";
        case VK_F13: return L"F13";
        case VK_F14: return L"F14";
        case VK_F15: return L"F15";
        case VK_F16: return L"F16";
        case VK_F17: return L"F17";
        case VK_F18: return L"F18";
        case VK_F19: return L"F19";
        case VK_F20: return L"F20";
        case VK_F21: return L"F21";
        case VK_F22: return L"F22";
        case VK_F23: return L"F23";
        case VK_F24: return L"F24";
        case VK_SNAPSHOT: return L"Print Screen";
        case VK_SCROLL: return L"Scroll Lock";
        case VK_PAUSE: return L"Pause";
        case VK_INSERT: return L"Insert";
        case VK_DELETE: return L"Delete";
        case VK_HOME: return L"Home";
        case VK_END: return L"End";
        case VK_PRIOR: return L"Page Up";
        case VK_NEXT: return L"Page Down";
        case VK_SPACE: return L"Space";
        case VK_TAB: return L"Tab";
        case VK_RETURN: return L"Enter";
        case VK_ESCAPE: return L"Escape";
        case VK_BACK: return L"Backspace";
        case VK_OEM_3: return L"`";
        case VK_OEM_MINUS: return L"-";
        case VK_OEM_PLUS: return L"=";
        case VK_OEM_4: return L"[";
        case VK_OEM_6: return L"]";
        case VK_OEM_5: return L"\\";
        case VK_OEM_1: return L";";
        case VK_OEM_7: return L"'";
        case VK_OEM_COMMA: return L",";
        case VK_OEM_PERIOD: return L".";
        case VK_OEM_2: return L"/";
        default:
            if (vk >= '0' && vk <= '9') return std::wstring(1, static_cast<wchar_t>(vk));
            if (vk >= 'A' && vk <= 'Z') return std::wstring(1, static_cast<wchar_t>(vk));
            return L"";
    }
}
} // namespace

HotkeyCapture::HotkeyCapture() = default;

void HotkeyCapture::BeginListen() {
    m_isListening = true;
    m_heldKeys.clear();
    m_ctrlHeld = false;
    m_shiftHeld = false;
    m_altHeld = false;
    m_winHeld = false;
    m_primaryKey = 0;
}

void HotkeyCapture::CancelListen() {
    m_isListening = false;
    m_heldKeys.clear();
    m_ctrlHeld = false;
    m_shiftHeld = false;
    m_altHeld = false;
    m_winHeld = false;
    m_primaryKey = 0;
}

bool HotkeyCapture::IsModifierKey(WPARAM wParam) const {
    return wParam == VK_CONTROL || wParam == VK_LCONTROL || wParam == VK_RCONTROL ||
           wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT ||
           wParam == VK_MENU || wParam == VK_LMENU || wParam == VK_RMENU ||
           wParam == VK_LWIN || wParam == VK_RWIN;
}

bool HotkeyCapture::OnKeyDown(WPARAM wParam) {
    if (!m_isListening) return false;

    // Escape cancels
    if (wParam == VK_ESCAPE) {
        CancelListen();
        return true;
    }

    m_heldKeys.insert(wParam);

    // Track modifiers
    if (wParam == VK_CONTROL || wParam == VK_LCONTROL || wParam == VK_RCONTROL) {
        m_ctrlHeld = true;
    } else if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) {
        m_shiftHeld = true;
    } else if (wParam == VK_MENU || wParam == VK_LMENU || wParam == VK_RMENU) {
        m_altHeld = true;
    } else if (wParam == VK_LWIN || wParam == VK_RWIN) {
        m_winHeld = true;
    } else if (!IsModifierKey(wParam)) {
        m_primaryKey = wParam;
        ConfirmHotkey();
    }

    return true;
}

bool HotkeyCapture::OnKeyUp(WPARAM wParam) {
    if (!m_isListening) return false;

    m_heldKeys.erase(wParam);

    const auto held = [this](WPARAM key) {
        return m_heldKeys.find(key) != m_heldKeys.end();
    };
    m_ctrlHeld = held(VK_CONTROL) || held(VK_LCONTROL) || held(VK_RCONTROL);
    m_shiftHeld = held(VK_SHIFT) || held(VK_LSHIFT) || held(VK_RSHIFT);
    m_altHeld = held(VK_MENU) || held(VK_LMENU) || held(VK_RMENU);
    m_winHeld = held(VK_LWIN) || held(VK_RWIN);

    return true;
}

std::wstring HotkeyCapture::BuildHotkeyString() const {
    std::wstring result;

    if (m_winHeld) result += L"Win+";
    if (m_ctrlHeld) result += L"Ctrl+";
    if (m_altHeld) result += L"Alt+";
    if (m_shiftHeld) result += L"Shift+";

    if (m_primaryKey != 0) {
        result += GetKeyName(m_primaryKey);
    }

    return result;
}

void HotkeyCapture::ConfirmHotkey() {
    std::wstring newHotkey = BuildHotkeyString();

    // Don't allow modifier-only hotkeys
    if (m_primaryKey == 0) {
        CancelListen();
        return;
    }

    m_hotkey = newHotkey;
    m_isListening = false;
    m_heldKeys.clear();
    m_ctrlHeld = false;
    m_shiftHeld = false;
    m_altHeld = false;
    m_winHeld = false;
    m_primaryKey = 0;

    if (m_onChange) {
        m_onChange(m_hotkey);
    }
}

void HotkeyCapture::Paint(Renderer& r, const RECT& rc, bool hover) {
    const bool isActive = m_isListening;

    // Background
    COLORREF bgColor = isActive ? UITheme::INVERSE
                     : hover ? UITheme::BG_HOVER
                     : UITheme::BG_SURFACE;

    // Border color
    COLORREF borderColor = isActive ? UITheme::INVERSE
                         : hover ? UITheme::BORDER_STRONG
                         : UITheme::BORDER;

    // Draw rounded card
    UITheme::DrawRoundedCard(r, rc, 6, bgColor, borderColor);

    // Icon
    RECT rcIcon = { rc.left + 12, rc.top + (rc.bottom - rc.top - 20) / 2,
                    rc.left + 32, rc.top + (rc.bottom - rc.top + 20) / 2 };
    COLORREF iconColor = isActive ? UITheme::INVERSE_TEXT : UITheme::TEXT_PRIMARY;
    Icons::Draw(r, Icons::Keyboard, rcIcon, 16, iconColor);

    // Text
    std::wstring displayText;
    if (isActive) {
        displayText = BuildHotkeyString();
        if (displayText.empty()) {
            displayText = L"Press keys...";
        }
    } else {
        displayText = m_hotkey.empty() ? L"Click to set" : m_hotkey;
    }

    TextStyle textStyle = Typography::Secondary();
    textStyle.weight = isActive ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL;

    COLORREF textColor = isActive ? UITheme::INVERSE_TEXT : UITheme::TEXT_PRIMARY;

    r.DrawText(displayText, textStyle,
               static_cast<float>(rc.left + 40),
               static_cast<float>(rc.top),
               static_cast<float>(rc.right - rc.left - 52),
               static_cast<float>(rc.bottom - rc.top),
               textColor,
               DWRITE_TEXT_ALIGNMENT_LEADING,
               DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

} // namespace prismshift

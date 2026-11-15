#include "screenshots/HotkeyManager.h"
#include "utilities/Logger.h"
#include <algorithm>
#include <unordered_map>

namespace prismshift {

namespace {

std::unordered_map<int, HotkeyManager::HotkeyCallback> g_hotkeyCallbacks;
int g_nextHotkeyId = 1;

} // namespace

HotkeyManager::HotkeyManager() = default;

HotkeyManager::~HotkeyManager() {
    UnregisterAll();
}

bool HotkeyManager::Initialize(HWND messageWindow) {
    if (m_initialized) {
        Logger::Warn(L"[HotkeyManager] Already initialized");
        return true;
    }

    if (!messageWindow || !IsWindow(messageWindow)) {
        Logger::Error(L"[HotkeyManager] Invalid message window");
        return false;
    }

    m_messageWindow = messageWindow;
    m_initialized = true;
    Logger::Info(L"[HotkeyManager] Initialized successfully");
    return true;
}

void HotkeyManager::Shutdown() {
    if (m_initialized) {
        UnregisterAll();
        m_messageWindow = nullptr;
        m_initialized = false;
        Logger::Info(L"[HotkeyManager] Shutdown complete");
    }
}

std::optional<int> HotkeyManager::RegisterHotkey(
    const HotkeyDefinition& definition,
    HotkeyCallback callback
) {
    if (!m_initialized) {
        Logger::Error(L"[HotkeyManager] Not initialized");
        return std::nullopt;
    }

    if (!definition.IsValid()) {
        Logger::Error(L"[HotkeyManager] Invalid hotkey definition");
        return std::nullopt;
    }

    UINT modifiers = definition.ToWin32Modifiers();
    UINT vk = definition.virtualKey;

    int hotkeyId = g_nextHotkeyId++;

    if (!::RegisterHotKey(m_messageWindow, hotkeyId, modifiers, vk)) {
        DWORD error = GetLastError();
        Logger::Error(L"[HotkeyManager] Failed to register hotkey: error " + std::to_wstring(error));
        return std::nullopt;
    }

    HotkeyRegistration reg;
    reg.id = hotkeyId;
    reg.definition = definition;
    reg.callback = std::move(callback);
    reg.profileId = definition.profileId;

    m_registeredHotkeys[hotkeyId] = std::move(reg);
    g_hotkeyCallbacks[hotkeyId] = m_registeredHotkeys[hotkeyId].callback;

    Logger::Info(L"[HotkeyManager] Registered hotkey for profile: " + definition.profileId);
    return hotkeyId;
}

bool HotkeyManager::UnregisterHotkey(int hotkeyId) {
    auto it = m_registeredHotkeys.find(hotkeyId);
    if (it == m_registeredHotkeys.end()) {
        return false;
    }

    if (::UnregisterHotKey(m_messageWindow, hotkeyId)) {
        g_hotkeyCallbacks.erase(hotkeyId);
        m_registeredHotkeys.erase(it);
        Logger::Info(L"[HotkeyManager] Unregistered hotkey");
        return true;
    }

    Logger::Error(L"[HotkeyManager] Failed to unregister hotkey");
    return false;
}

void HotkeyManager::UnregisterAll() {
    std::vector<int> ids;
    ids.reserve(m_registeredHotkeys.size());
    for (const auto& [id, _] : m_registeredHotkeys) {
        ids.push_back(id);
    }

    for (int id : ids) {
        UnregisterHotkey(id);
    }
}

void HotkeyManager::UnregisterForProfile(const std::wstring& profileId) {
    std::vector<int> toRemove;
    for (const auto& [id, reg] : m_registeredHotkeys) {
        if (reg.profileId == profileId) {
            toRemove.push_back(id);
        }
    }

    for (int id : toRemove) {
        UnregisterHotkey(id);
    }
}

bool HotkeyManager::HandleHotkeyMessage(WPARAM wParam, LPARAM /*lParam*/) {
    int hotkeyId = static_cast<int>(wParam);

    auto it = m_registeredHotkeys.find(hotkeyId);
    if (it == m_registeredHotkeys.end()) {
        return false;
    }

    const auto& reg = it->second;
    if (reg.callback) {
        Logger::Debug(L"[HotkeyManager] Triggering hotkey for profile: " + reg.profileId);
        reg.callback(reg.definition);
    }

    return true;
}

std::vector<HotkeyManager::HotkeyRegistration> HotkeyManager::GetRegisteredHotkeys() const {
    std::vector<HotkeyRegistration> result;
    result.reserve(m_registeredHotkeys.size());
    for (const auto& [_, reg] : m_registeredHotkeys) {
        result.push_back(reg);
    }
    return result;
}

bool HotkeyDefinition::IsValid() const {
    // Must have a virtual key
    if (virtualKey == 0) {
        return false;
    }

    // Function keys (F1-F24) can be used alone, but most other keys should have a modifier
    if (virtualKey >= VK_F1 && virtualKey <= VK_F24) {
        return true;
    }

    // Print Screen can be used alone
    if (virtualKey == VK_SNAPSHOT) {
        return true;
    }

    // Other keys should have at least one modifier to avoid conflicts
    return ctrl || alt || shift || win;
}

UINT HotkeyDefinition::ToWin32Modifiers() const {
    UINT modifiers = 0;
    if (ctrl) modifiers |= MOD_CONTROL;
    if (alt) modifiers |= MOD_ALT;
    if (shift) modifiers |= MOD_SHIFT;
    if (win) modifiers |= MOD_WIN;
    if (noRepeat) modifiers |= MOD_NOREPEAT;
    return modifiers;
}

std::wstring HotkeyDefinition::ToDisplayString() const {
    std::wstring result;

    if (win) result += L"Win+";
    if (ctrl) result += L"Ctrl+";
    if (alt) result += L"Alt+";
    if (shift) result += L"Shift+";

    // Common virtual key codes
    if (virtualKey >= 'A' && virtualKey <= 'Z') {
        result += static_cast<wchar_t>(virtualKey);
    } else if (virtualKey >= '0' && virtualKey <= '9') {
        result += static_cast<wchar_t>(virtualKey);
    } else if (virtualKey >= VK_F1 && virtualKey <= VK_F24) {
        result += L"F" + std::to_wstring(virtualKey - VK_F1 + 1);
    } else {
        switch (virtualKey) {
            case VK_SNAPSHOT: result += L"Print Screen"; break;
            case VK_INSERT: result += L"Insert"; break;
            case VK_DELETE: result += L"Delete"; break;
            case VK_HOME: result += L"Home"; break;
            case VK_END: result += L"End"; break;
            case VK_PRIOR: result += L"Page Up"; break;
            case VK_NEXT: result += L"Page Down"; break;
            case VK_SPACE: result += L"Space"; break;
            case VK_RETURN: result += L"Enter"; break;
            case VK_BACK: result += L"Backspace"; break;
            case VK_TAB: result += L"Tab"; break;
            case VK_ESCAPE: result += L"Escape"; break;
            default: result += L"Key " + std::to_wstring(virtualKey); break;
        }
    }

    return result;
}

bool HotkeyDefinition::FromString(const std::wstring& str) {
    *this = HotkeyDefinition();

    std::wstring work = str;
    std::transform(work.begin(), work.end(), work.begin(), ::towlower);

    // Parse modifiers
    if (work.find(L"win+") != std::wstring::npos || work.find(L"super+") != std::wstring::npos) {
        win = true;
    }
    if (work.find(L"ctrl+") != std::wstring::npos || work.find(L"control+") != std::wstring::npos) {
        ctrl = true;
    }
    if (work.find(L"alt+") != std::wstring::npos) {
        alt = true;
    }
    if (work.find(L"shift+") != std::wstring::npos) {
        shift = true;
    }

    // Extract the key part (after the last +)
    size_t lastPlus = work.rfind(L'+');
    std::wstring keyPart = (lastPlus != std::wstring::npos) ? work.substr(lastPlus + 1) : work;

    // Trim whitespace
    keyPart.erase(0, keyPart.find_first_not_of(L" \t"));
    keyPart.erase(keyPart.find_last_not_of(L" \t") + 1);

    // Parse the key
    if (keyPart.length() == 1) {
        wchar_t ch = towupper(keyPart[0]);
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            virtualKey = ch;
            return IsValid();
        }
    }

    // Function keys
    if (keyPart[0] == L'f' && keyPart.length() >= 2) {
        int fNum = _wtoi(keyPart.c_str() + 1);
        if (fNum >= 1 && fNum <= 24) {
            virtualKey = VK_F1 + fNum - 1;
            return IsValid();
        }
    }

    // Special keys
    if (keyPart == L"printscreen" || keyPart == L"print screen" || keyPart == L"prtscn") {
        virtualKey = VK_SNAPSHOT;
    } else if (keyPart == L"insert" || keyPart == L"ins") {
        virtualKey = VK_INSERT;
    } else if (keyPart == L"delete" || keyPart == L"del") {
        virtualKey = VK_DELETE;
    } else if (keyPart == L"home") {
        virtualKey = VK_HOME;
    } else if (keyPart == L"end") {
        virtualKey = VK_END;
    } else if (keyPart == L"pageup" || keyPart == L"page up" || keyPart == L"pgup") {
        virtualKey = VK_PRIOR;
    } else if (keyPart == L"pagedown" || keyPart == L"page down" || keyPart == L"pgdn") {
        virtualKey = VK_NEXT;
    } else if (keyPart == L"space" || keyPart == L"spacebar") {
        virtualKey = VK_SPACE;
    } else {
        return false;
    }

    return IsValid();
}

} // namespace prismshift

#include "utilities/StartupManager.h"
#include <windows.h>

namespace prismshift {

namespace {
constexpr const wchar_t* RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* VALUE_NAME = L"Lumetra";
}

bool StartupManager::IsEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD size = 0;
    LONG result = RegQueryValueExW(key, VALUE_NAME, nullptr, &type, nullptr, &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_SZ && size > sizeof(wchar_t);
}

bool StartupManager::SetEnabled(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    LONG result = ERROR_SUCCESS;
    if (enabled) {
        wchar_t executablePath[MAX_PATH]{};
        DWORD length = GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) {
            RegCloseKey(key);
            return false;
        }

        std::wstring command = L"\"" + std::wstring(executablePath, length) + L"\" --minimized";
        result = RegSetValueExW(key, VALUE_NAME, 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, VALUE_NAME);
        if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
    }

    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

} // namespace prismshift

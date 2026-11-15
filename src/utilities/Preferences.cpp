#include "utilities/Preferences.h"
#include "utilities/Logger.h"
#include <windows.h>
#include <shlobj.h>

namespace prismshift {

namespace {
constexpr const wchar_t* kKeyPath = L"Software\\Lumetra\\Preferences";

constexpr const wchar_t* kStartMinimized = L"StartMinimized";
constexpr const wchar_t* kCloseToTray = L"CloseToTray";
constexpr const wchar_t* kNotifications = L"Notifications";
constexpr const wchar_t* kReduceMotion = L"ReduceMotion";
constexpr const wchar_t* kThemeId = L"Theme";
constexpr const wchar_t* kResolutionSwitchMode = L"ResolutionSwitchMode";
constexpr const wchar_t* kScreenshotsEnabled = L"ScreenshotsEnabled";
constexpr const wchar_t* kAutoCopyScreenshot = L"AutoCopyScreenshot";
constexpr const wchar_t* kScreenshotsDirectory = L"ScreenshotsDirectory";
constexpr const wchar_t* kScreenshotGlobalHotkey = L"ScreenshotGlobalHotkey";
constexpr const wchar_t* kScreenshotClickAction = L"ScreenshotClickAction";
constexpr const wchar_t* kAutomaticUpdateChecks = L"AutomaticUpdateChecks";
constexpr const wchar_t* kLastAutomaticUpdateAttempt = L"LastAutomaticUpdateAttempt";
constexpr const wchar_t* kLastSuccessfulUpdateCheck = L"LastSuccessfulUpdateCheck";
constexpr const wchar_t* kLastShownReleaseNotesVersion = L"LastShownReleaseNotesVersion";
constexpr const wchar_t* kLastRunVersion = L"LastRunVersion";

bool ReadBool(HKEY key, const wchar_t* name, bool fallback) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    DWORD type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
        type == REG_DWORD) {
        return value != 0;
    }
    return fallback;
}

std::wstring ReadString(HKEY key, const wchar_t* name, const std::wstring& fallback) {
    wchar_t buffer[MAX_PATH * 2];
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(buffer), &size) == ERROR_SUCCESS &&
        type == REG_SZ && size > 0) {
        return std::wstring(buffer, (size / sizeof(wchar_t)) - 1);
    }
    return fallback;
}

void WriteBool(HKEY key, const wchar_t* name, bool value) {
    DWORD data = value ? 1 : 0;
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&data), sizeof(data));
}

void WriteString(HKEY key, const wchar_t* name, const std::wstring& value) {
    DWORD size = static_cast<DWORD>((value.length() + 1) * sizeof(wchar_t));
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), size);
}

std::uint64_t ReadUInt64(HKEY key, const wchar_t* name,
                         std::uint64_t fallback) {
    ULONGLONG value = 0;
    DWORD size = sizeof(value);
    DWORD type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type,
                         reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
        type == REG_QWORD && size == sizeof(value)) {
        return value;
    }
    return fallback;
}

void WriteUInt64(HKEY key, const wchar_t* name, std::uint64_t value) {
    const ULONGLONG data = value;
    RegSetValueExW(key, name, 0, REG_QWORD,
                   reinterpret_cast<const BYTE*>(&data), sizeof(data));
}
} // namespace

std::wstring PreferencesStore::GetDefaultScreenshotsDirectory() {
    wchar_t picturesPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_MYPICTURES, nullptr, 0, picturesPath))) {
        return std::wstring(picturesPath) + L"\\Lumetra";
    }
    return L"C:\\Users\\Public\\Pictures\\Lumetra";
}

Preferences PreferencesStore::Load() {
    Preferences prefs;
    prefs.screenshotsDirectory = GetDefaultScreenshotsDirectory();

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kKeyPath, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return prefs;
    }

    prefs.startMinimized = ReadBool(key, kStartMinimized, prefs.startMinimized);
    prefs.closeToTray = ReadBool(key, kCloseToTray, prefs.closeToTray);
    prefs.notifications = ReadBool(key, kNotifications, prefs.notifications);
    prefs.reduceMotion = ReadBool(key, kReduceMotion, prefs.reduceMotion);
    DWORD themeId = static_cast<DWORD>(prefs.themeId);
    DWORD themeIdSize = sizeof(themeId);
    DWORD themeIdType = 0;
    if (RegQueryValueExW(key, kThemeId, nullptr, &themeIdType,
                         reinterpret_cast<BYTE*>(&themeId), &themeIdSize) == ERROR_SUCCESS &&
        themeIdType == REG_DWORD && themeId <= 1) {
        prefs.themeId = static_cast<int>(themeId);
    }
    DWORD resolutionMode = static_cast<DWORD>(prefs.resolutionSwitchMode);
    DWORD resolutionModeSize = sizeof(resolutionMode);
    DWORD resolutionModeType = 0;
    if (RegQueryValueExW(key, kResolutionSwitchMode, nullptr, &resolutionModeType,
                         reinterpret_cast<BYTE*>(&resolutionMode),
                         &resolutionModeSize) == ERROR_SUCCESS &&
        resolutionModeType == REG_DWORD &&
        resolutionMode <= static_cast<DWORD>(ResolutionSwitchMode::Off)) {
        prefs.resolutionSwitchMode = static_cast<ResolutionSwitchMode>(resolutionMode);
    }
    prefs.screenshotsEnabled = ReadBool(key, kScreenshotsEnabled, prefs.screenshotsEnabled);
    prefs.autoCopyScreenshot = ReadBool(key, kAutoCopyScreenshot, prefs.autoCopyScreenshot);
    prefs.automaticUpdateChecks = ReadBool(
        key, kAutomaticUpdateChecks, prefs.automaticUpdateChecks);
    prefs.lastAutomaticUpdateAttempt =
        ReadUInt64(key, kLastAutomaticUpdateAttempt, 0);
    prefs.lastSuccessfulUpdateCheck =
        ReadUInt64(key, kLastSuccessfulUpdateCheck, 0);
    prefs.lastShownReleaseNotesVersion =
        ReadString(key, kLastShownReleaseNotesVersion, L"");
    prefs.lastRunVersion = ReadString(key, kLastRunVersion, L"");
    prefs.screenshotsDirectory = ReadString(key, kScreenshotsDirectory, prefs.screenshotsDirectory);
    prefs.screenshotGlobalHotkey = ReadString(
        key, kScreenshotGlobalHotkey, prefs.screenshotGlobalHotkey);
    if (prefs.screenshotGlobalHotkey.empty()) {
        prefs.screenshotGlobalHotkey = L"Alt+S";
    }
    DWORD clickAction = static_cast<DWORD>(prefs.screenshotClickAction);
    DWORD clickActionSize = sizeof(clickAction);
    DWORD clickActionType = 0;
    if (RegQueryValueExW(key, kScreenshotClickAction, nullptr, &clickActionType,
                         reinterpret_cast<BYTE*>(&clickAction),
                         &clickActionSize) == ERROR_SUCCESS &&
        clickActionType == REG_DWORD &&
        clickAction <= static_cast<DWORD>(ScreenshotClickAction::CopyImage)) {
        prefs.screenshotClickAction = static_cast<ScreenshotClickAction>(clickAction);
    }
    if (prefs.autoCopyScreenshot &&
        prefs.screenshotClickAction == ScreenshotClickAction::CopyImage) {
        prefs.screenshotClickAction = ScreenshotClickAction::OpenImage;
    }

    RegCloseKey(key);
    return prefs;
}

void PreferencesStore::Save(const Preferences& prefs) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKeyPath, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        Logger::Warn(L"Failed to open Lumetra preferences key for writing.");
        return;
    }

    WriteBool(key, kStartMinimized, prefs.startMinimized);
    WriteBool(key, kCloseToTray, prefs.closeToTray);
    WriteBool(key, kNotifications, prefs.notifications);
    WriteBool(key, kReduceMotion, prefs.reduceMotion);
    const DWORD themeId = static_cast<DWORD>(prefs.themeId);
    RegSetValueExW(key, kThemeId, 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&themeId), sizeof(themeId));
    const DWORD resolutionMode = static_cast<DWORD>(prefs.resolutionSwitchMode);
    RegSetValueExW(key, kResolutionSwitchMode, 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&resolutionMode), sizeof(resolutionMode));
    WriteBool(key, kScreenshotsEnabled, prefs.screenshotsEnabled);
    WriteBool(key, kAutoCopyScreenshot, prefs.autoCopyScreenshot);
    WriteBool(key, kAutomaticUpdateChecks, prefs.automaticUpdateChecks);
    WriteUInt64(key, kLastAutomaticUpdateAttempt,
                prefs.lastAutomaticUpdateAttempt);
    WriteUInt64(key, kLastSuccessfulUpdateCheck,
                prefs.lastSuccessfulUpdateCheck);
    WriteString(key, kLastShownReleaseNotesVersion,
                prefs.lastShownReleaseNotesVersion);
    WriteString(key, kLastRunVersion, prefs.lastRunVersion);
    WriteString(key, kScreenshotsDirectory, prefs.screenshotsDirectory);
    WriteString(key, kScreenshotGlobalHotkey, prefs.screenshotGlobalHotkey);
    const DWORD clickAction = static_cast<DWORD>(prefs.screenshotClickAction);
    RegSetValueExW(key, kScreenshotClickAction, 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&clickAction), sizeof(clickAction));

    RegCloseKey(key);
}

} // namespace prismshift

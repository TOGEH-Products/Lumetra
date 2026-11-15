#pragma once

#include <string>
#include <cstdint>

namespace prismshift {

enum class ScreenshotClickAction {
    OpenImage,
    OpenScreenshotsFolder,
    CopyImage
};

enum class ResolutionSwitchMode {
    AutoSwitch,
    AskBeforeApplying,
    Off
};

// Small, human-facing preference set persisted under
// HKCU\Software\Lumetra\Preferences. Display tuning stays in profiles.json;
// this is only shell behaviour.
struct Preferences {
    bool startMinimized = false;
    bool closeToTray = true;
    bool notifications = true;
    bool reduceMotion = false;
    ResolutionSwitchMode resolutionSwitchMode = ResolutionSwitchMode::Off;

    // Theme selection: 0 = Obsidian (black & white, default), 1 = Paper.
    // Values mirror prismshift::ThemeId in ui/UITheme.h.
    int themeId = 0;

    // Screenshot settings
    bool screenshotsEnabled = true;
    bool autoCopyScreenshot = true;
    bool automaticUpdateChecks = true;
    std::uint64_t lastAutomaticUpdateAttempt = 0;
    std::uint64_t lastSuccessfulUpdateCheck = 0;
    std::wstring lastShownReleaseNotesVersion;
    std::wstring lastRunVersion;
    std::wstring screenshotsDirectory;        // Empty = use default (%USERPROFILE%\Pictures\Lumetra)
    std::wstring screenshotGlobalHotkey = L"Alt+S";
    ScreenshotClickAction screenshotClickAction = ScreenshotClickAction::OpenImage;
};

class PreferencesStore {
public:
    static Preferences Load();
    static void Save(const Preferences& prefs);

    // Get the default screenshots directory
    static std::wstring GetDefaultScreenshotsDirectory();
};

} // namespace prismshift

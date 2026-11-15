#pragma once

#include "ui/Anim.h"
#include "ui/HotkeyCapture.h"
#include "ui/Renderer.h"
#include "utilities/Preferences.h"
#include <windows.h>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace prismshift {

// Settings section: grouped preference cards with animated rows, toggles and
// scroll. Persistence and side effects are owned by MainWindow/Application.
class SettingsView {
public:
    enum class ToggleId {
        StartWithWindows = 0,
        StartMinimized,
        CloseToTray,
        Notifications,
        ReduceMotion,
        Switching,
        Screenshots,
        AutoCopyScreenshot,
        AutomaticUpdateChecks
    };

    using ToggleCallback = std::function<void(ToggleId id, bool enabled)>;
    using ActionCallback = std::function<void()>;
    using TextInputCallback = std::function<void(const std::wstring& value)>;
    using ResolutionModeCallback = std::function<void(ResolutionSwitchMode mode)>;
    using ThemeCallback = std::function<void(int themeId)>;

    SettingsView();
    ~SettingsView() = default;

    void SetToggleCallback(ToggleCallback cb) { m_onToggle = std::move(cb); }
    void SetResetDisplayCallback(ActionCallback cb) { m_onResetDisplay = std::move(cb); }
    void SetOpenConfigCallback(ActionCallback cb) { m_onOpenConfig = std::move(cb); }
    void SetOpenScreenshotsCallback(ActionCallback cb) { m_onOpenScreenshots = std::move(cb); }
    void SetCheckUpdatesCallback(ActionCallback cb) { m_onCheckUpdates = std::move(cb); }
    void SetBrowseScreenshotsCallback(ActionCallback cb) { m_onBrowseScreenshots = std::move(cb); }
    void SetScreenshotHotkeyCallback(TextInputCallback cb) { m_onScreenshotHotkey = std::move(cb); }
    void SetScreenshotClickActionCallback(ActionCallback cb) {
        m_onScreenshotClickAction = std::move(cb);
    }
    void SetResolutionModeCallback(ResolutionModeCallback cb) {
        m_onResolutionMode = std::move(cb);
    }
    void SetThemeCallback(ThemeCallback cb) { m_onTheme = std::move(cb); }
    void SetTheme(int themeId) { m_theme = themeId; }
    [[nodiscard]] int GetTheme() const { return m_theme; }

    // The brand mark bitmap (Logo.jpg) shared from MainWindow. May be null if
    // the image is missing; the About card falls back to a lettermark.
    void SetLogoBitmap(ID2D1Bitmap* bitmap) { m_logoBitmap = bitmap; }

    void SetToggleState(ToggleId id, bool enabled);
    [[nodiscard]] bool GetToggleState(ToggleId id) const;

    void SetConfigPath(const std::wstring& path) { m_configPath = path; }
    void SetScreenshotsDirectory(const std::wstring& path) { m_screenshotsDir = path; }
    void SetScreenshotGlobalHotkey(const std::wstring& hotkey);
    void SetScreenshotClickAction(ScreenshotClickAction action) {
        m_screenshotClickAction = action;
    }
    void SetResolutionSwitchMode(ResolutionSwitchMode mode) {
        m_resolutionSwitchMode = mode;
    }
    void SetVersion(const std::wstring& version) { m_version = version; }
    void SetBackend(const std::wstring& backend) { m_backend = backend; }
    void SetUpdateStatus(const std::wstring& status) { m_updateStatus = status; }

    void Paint(Renderer& r, const RECT& rcBounds);
    bool OnMouseMove(POINT pt, const RECT& rcBounds);
    bool OnLButtonDown(POINT pt, const RECT& rcBounds);
    bool OnMouseWheel(int delta, const RECT& rcBounds);
    bool OnKeyDown(WPARAM wParam);
    bool OnKeyUp(WPARAM wParam);
    [[nodiscard]] bool IsListeningForScreenshotHotkey() const {
        return m_screenshotHotkeyCapture.IsListening();
    }

    bool TickAnimations(float dt);
    [[nodiscard]] bool HasActiveAnimations() const;
    void ResetHover();

private:
    enum class ControlId {
        None,
        StartWithWindows,
        StartMinimized,
        CloseToTray,
        Notifications,
        ReduceMotion,
        Theme,
        Switching,
        Screenshots,
        AutoCopyScreenshot,
        ScreenshotsFolder,
        ScreenshotsHotkey,
        ScreenshotClickAction,
        ResolutionMode,
        OpenScreenshotsFolder,
        AutomaticUpdateChecks,
        CheckForUpdates,
        UpdateStatus,
        ResetDisplay,
        ConfigFolder,
        Brand,
        Version,
        Backend
    };

    struct RowLayout {
        ControlId id = ControlId::None;
        RECT rcRow{};
        RECT rcControl{};
        int animIndex = 0;
        bool isToggle = false;
    };

    struct SectionLayout {
        std::wstring label;
        RECT rcLabel{};
        RECT rcCard{};
    };

    struct Layout {
        std::vector<SectionLayout> sections;
        std::vector<RowLayout> rows;
        int contentTop = 0;
        int contentHeight = 0;
    };

    [[nodiscard]] Layout BuildLayout(const RECT& rcBounds) const;
    [[nodiscard]] bool IsToggle(ControlId id) const;
    [[nodiscard]] ToggleId ToToggleId(ControlId id) const;
    void DrawRow(Renderer& r, const RowLayout& row, float hover);
    void DrawScreenshotHotkeyField(Renderer& r, const RECT& rcControl, bool hover);

    ToggleCallback m_onToggle;
    ActionCallback m_onResetDisplay;
    ActionCallback m_onOpenConfig;
    ActionCallback m_onOpenScreenshots;
    ActionCallback m_onCheckUpdates;
    ActionCallback m_onBrowseScreenshots;
    TextInputCallback m_onScreenshotHotkey;
    ActionCallback m_onScreenshotClickAction;
    ResolutionModeCallback m_onResolutionMode;
    ThemeCallback m_onTheme;

    bool m_startWithWindows = false;
    bool m_startMinimized = false;
    bool m_closeToTray = true;
    bool m_notifications = true;
    bool m_reduceMotion = false;
    bool m_switching = true;
    bool m_screenshots = true;
    bool m_autoCopyScreenshot = true;
    bool m_automaticUpdateChecks = true;
    ScreenshotClickAction m_screenshotClickAction = ScreenshotClickAction::OpenImage;
    ResolutionSwitchMode m_resolutionSwitchMode = ResolutionSwitchMode::Off;
    int m_theme = 0;
    ID2D1Bitmap* m_logoBitmap = nullptr;

    std::wstring m_configPath;
    std::wstring m_screenshotsDir;
    std::wstring m_screenshotGlobalHotkey;
    HotkeyCapture m_screenshotHotkeyCapture;
    std::wstring m_version = L"0.2.0";
    std::wstring m_backend = L"Win32 gamma ramp + DWM color matrix";
    std::wstring m_updateStatus = L"Stable releases only.";

    ControlId m_hoverControl = ControlId::None;
    int m_hoverRow = -1;
    int m_scrollY = 0;
    int m_scrollMax = 0;

    Anim m_scrollAnim;
    std::vector<Anim> m_hoverAnims;
    std::vector<Anim> m_toggleAnims;
};

} // namespace prismshift

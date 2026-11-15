#pragma once

#include "screenshots/ScreenshotCapture.h"
#include "screenshots/HotkeyManager.h"
#include "utilities/NonCopyable.h"
#include <windows.h>
#include <map>
#include <string>
#include <functional>

namespace prismshift {

// Orchestrates screenshot capture with profile-aware organization and hotkey management.
// Automatically saves screenshots to profile-specific subfolders based on the active game.
class ScreenshotManager : public NonCopyable {
public:
    using NotificationCallback = std::function<void(
        const std::wstring& message, bool isError, const std::wstring& imagePath,
        bool imageCopied)>;

    struct Settings {
        std::wstring screenshotsDirectory;
        bool enabled = true;
        bool autoCopy = true;
        HotkeyDefinition globalHotkey;
    };

    ScreenshotManager();
    ~ScreenshotManager();

    bool Initialize(HWND messageWindow);
    void Shutdown();
    void SetClipboardOwnerWindow(HWND owner) { m_clipboardOwner = owner; }

    // Settings management
    void SetSettings(const Settings& settings);
    void SetAutoCopyEnabled(bool enabled) { m_settings.autoCopy = enabled; }
    [[nodiscard]] const Settings& GetSettings() const { return m_settings; }

    void SetNotificationCallback(NotificationCallback cb) { m_onNotification = std::move(cb); }

    // Set the currently active profile (used for subdirectory selection)
    void SetActiveProfile(const std::wstring& profileId, const std::wstring& profileName);

    // Register or update a per-profile hotkey
    void RegisterProfileHotkey(const std::wstring& profileId, const HotkeyDefinition& hotkey);
    void UnregisterProfileHotkey(const std::wstring& profileId);

    // Capture screenshot (can be called manually or via hotkey)
    void CaptureScreenshot(ScreenshotCapture::CaptureMode mode = ScreenshotCapture::CaptureMode::ActiveWindow);
    bool CopyScreenshotToClipboard(const std::wstring& imagePath);

    // Handle WM_HOTKEY message (forward from application)
    bool HandleHotkeyMessage(WPARAM wParam, LPARAM lParam);

    // Open the screenshots folder in explorer
    void OpenScreenshotsFolder() const;
    void OpenProfileFolder(const std::wstring& profileName) const;

private:
    void OnHotkeyTriggered(const HotkeyDefinition& hotkey);
    void RefreshGlobalHotkey();
    void RefreshProfileHotkeys();
    void TryRegisterProfileHotkey(const std::wstring& profileId,
                                  const HotkeyDefinition& hotkey);
    std::wstring GetSubdirectoryForCurrentProfile() const;
    std::wstring SanitizeDirectoryName(const std::wstring& name) const;

    ScreenshotCapture m_capture;
    HotkeyManager m_hotkeyManager;
    Settings m_settings;
    std::map<std::wstring, HotkeyDefinition> m_profileHotkeys;

    std::wstring m_activeProfileId;
    std::wstring m_activeProfileName;

    NotificationCallback m_onNotification;
    HWND m_clipboardOwner = nullptr;
    int m_globalHotkeyId = -1;
};

} // namespace prismshift

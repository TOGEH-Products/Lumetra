#include "screenshots/ScreenshotManager.h"
#include "utilities/Logger.h"
#include <shellapi.h>
#include <mmsystem.h>
#include <atomic>
#include <filesystem>
#include <iterator>
#include <system_error>
#include <thread>
#include <vector>

namespace prismshift {

namespace {
std::atomic_uint g_soundAliasId = 0;

void PlayScreenshotSound(bool isError) {
    std::vector<wchar_t> modulePath(32768);
    const DWORD pathLength = GetModuleFileNameW(nullptr, modulePath.data(),
                                                 static_cast<DWORD>(modulePath.size()));
    if (pathLength == 0 || pathLength >= modulePath.size()) {
        Logger::Warn(L"[ScreenshotManager] Unable to locate screenshot sound files");
        MessageBeep(MB_ICONASTERISK);
        return;
    }

    const std::filesystem::path soundPath =
        std::filesystem::path(std::wstring(modulePath.data(), pathLength)).parent_path()
        / L"Sounds" / (isError ? L"ScreenshotFailed.mp3" : L"ScreenshotSuccess.mp3");
    const DWORD soundAttributes = GetFileAttributesW(soundPath.c_str());
    if (soundAttributes == INVALID_FILE_ATTRIBUTES ||
        (soundAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        Logger::Warn(L"[ScreenshotManager] Screenshot sound file is missing: " +
                     soundPath.wstring());
        MessageBeep(MB_ICONASTERISK);
        return;
    }

    const std::wstring alias = L"LumetraScreenshotSound" +
                               std::to_wstring(++g_soundAliasId);

    try {
        std::thread([path = soundPath.wstring(), alias, isError]() {
            const std::wstring openCommand =
                L"open \"" + path + L"\" type mpegvideo alias " + alias;
            MCIERROR result = mciSendStringW(openCommand.c_str(), nullptr, 0, nullptr);
            if (result != 0) {
                wchar_t errorText[256]{};
                mciGetErrorStringW(result, errorText, static_cast<UINT>(std::size(errorText)));
                Logger::Warn(L"[ScreenshotManager] Could not open screenshot sound file: " +
                             std::wstring(errorText));
                MessageBeep(MB_ICONASTERISK);
                return;
            }

            if (!isError) {
                const std::wstring volumeCommand =
                    L"setaudio " + alias + L" volume to 650";
                MCIERROR volumeResult =
                    mciSendStringW(volumeCommand.c_str(), nullptr, 0, nullptr);
                if (volumeResult != 0) {
                    wchar_t errorText[256]{};
                    mciGetErrorStringW(volumeResult, errorText,
                                       static_cast<UINT>(std::size(errorText)));
                    Logger::Warn(L"[ScreenshotManager] Could not lower screenshot success sound volume: " +
                                 std::wstring(errorText));
                }
            }

            const std::wstring playCommand = L"play " + alias + L" wait";
            result = mciSendStringW(playCommand.c_str(), nullptr, 0, nullptr);
            mciSendStringW((L"close " + alias).c_str(), nullptr, 0, nullptr);
            if (result != 0) {
                wchar_t errorText[256]{};
                mciGetErrorStringW(result, errorText, static_cast<UINT>(std::size(errorText)));
                Logger::Warn(L"[ScreenshotManager] Could not play screenshot sound: " +
                             std::wstring(errorText));
                MessageBeep(MB_ICONASTERISK);
            }
        }).detach();
    } catch (const std::system_error&) {
        Logger::Warn(L"[ScreenshotManager] Unable to start screenshot sound playback");
        MessageBeep(MB_ICONASTERISK);
    }
}
} // namespace

ScreenshotManager::ScreenshotManager() = default;

ScreenshotManager::~ScreenshotManager() {
    Shutdown();
}

bool ScreenshotManager::Initialize(HWND messageWindow) {
    m_clipboardOwner = messageWindow;
    if (!m_capture.Initialize()) {
        Logger::Error(L"[ScreenshotManager] Failed to initialize screenshot capture");
        return false;
    }

    if (!m_hotkeyManager.Initialize(messageWindow)) {
        Logger::Error(L"[ScreenshotManager] Failed to initialize hotkey manager");
        m_capture.Shutdown();
        return false;
    }

    Logger::Info(L"[ScreenshotManager] Initialized successfully");
    return true;
}

void ScreenshotManager::Shutdown() {
    m_hotkeyManager.Shutdown();
    m_capture.Shutdown();
    m_clipboardOwner = nullptr;
    Logger::Info(L"[ScreenshotManager] Shutdown complete");
}

void ScreenshotManager::SetSettings(const Settings& settings) {
    m_settings = settings;

    if (!m_settings.screenshotsDirectory.empty() &&
        !ScreenshotCapture::EnsureDirectoryExists(m_settings.screenshotsDirectory)) {
        Logger::Error(L"[ScreenshotManager] Failed to create screenshots directory: " +
                      m_settings.screenshotsDirectory);
    }

    for (const auto& registration : m_hotkeyManager.GetRegisteredHotkeys()) {
        if (!registration.profileId.empty()) {
            m_hotkeyManager.UnregisterHotkey(registration.id);
        }
    }

    RefreshGlobalHotkey();
    RefreshProfileHotkeys();

    Logger::Info(L"[ScreenshotManager] Settings updated");
}

void ScreenshotManager::SetActiveProfile(const std::wstring& profileId, const std::wstring& profileName) {
    if (m_activeProfileId != profileId) {
        m_activeProfileId = profileId;
        m_activeProfileName = profileName;
        Logger::Debug(L"[ScreenshotManager] Active profile: " + profileName);
    }
}

void ScreenshotManager::RegisterProfileHotkey(const std::wstring& profileId, const HotkeyDefinition& hotkey) {
    if (!hotkey.IsValid()) {
        Logger::Warn(L"[ScreenshotManager] Invalid hotkey definition for profile: " + profileId);
        return;
    }

    HotkeyDefinition def = hotkey;
    def.profileId = profileId;
    m_profileHotkeys[profileId] = def;
    m_hotkeyManager.UnregisterForProfile(profileId);
    TryRegisterProfileHotkey(profileId, def);
}

void ScreenshotManager::TryRegisterProfileHotkey(
    const std::wstring& profileId,
    const HotkeyDefinition& hotkey
) {
    if (!m_settings.enabled) {
        return;
    }
    if (m_settings.globalHotkey.IsValid() && hotkey == m_settings.globalHotkey) {
        Logger::Info(L"[ScreenshotManager] Profile uses the global screenshot hotkey: " +
                     profileId);
        return;
    }

    auto hotkeyId = m_hotkeyManager.RegisterHotkey(hotkey, [this](const HotkeyDefinition& hk) {
        OnHotkeyTriggered(hk);
    });

    if (hotkeyId.has_value()) {
        Logger::Info(L"[ScreenshotManager] Registered hotkey for profile: " + profileId);
    }
}

void ScreenshotManager::UnregisterProfileHotkey(const std::wstring& profileId) {
    m_profileHotkeys.erase(profileId);
    m_hotkeyManager.UnregisterForProfile(profileId);
}

void ScreenshotManager::RefreshProfileHotkeys() {
    for (const auto& [profileId, hotkey] : m_profileHotkeys) {
        TryRegisterProfileHotkey(profileId, hotkey);
    }
}

void ScreenshotManager::CaptureScreenshot(ScreenshotCapture::CaptureMode mode) {
    if (!m_settings.enabled) {
        Logger::Info(L"[ScreenshotManager] Screenshot capture is disabled");
        return;
    }

    if (m_settings.screenshotsDirectory.empty()) {
        Logger::Error(L"[ScreenshotManager] Screenshots directory not configured");
        PlayScreenshotSound(true);
        if (m_onNotification) {
            m_onNotification(L"Screenshots directory not set. Configure it in Settings.",
                             true, L"", false);
        }
        return;
    }

    std::wstring subdirectory = GetSubdirectoryForCurrentProfile();

    Logger::Info(L"[ScreenshotManager] Capturing screenshot to: " + subdirectory);

    auto result = m_capture.Capture(m_settings.screenshotsDirectory, subdirectory, mode);
    PlayScreenshotSound(!result.success);

    if (result.success) {
        Logger::Info(L"[ScreenshotManager] Screenshot saved: " + result.filePath);
        const bool imageCopied = m_settings.autoCopy &&
            ScreenshotCapture::CopyImageToClipboard(result.filePath, m_clipboardOwner);
        if (imageCopied) {
            Logger::Info(L"[ScreenshotManager] Screenshot copied to clipboard");
        }
        if (m_onNotification) {
            m_onNotification(L"Screenshot saved: " + result.filePath, false,
                             result.filePath, imageCopied);
        }
    } else {
        Logger::Error(L"[ScreenshotManager] Screenshot capture failed: " + result.errorMessage);
        if (m_onNotification) {
            m_onNotification(L"Screenshot failed: " + result.errorMessage, true, L"", false);
        }
    }
}

bool ScreenshotManager::CopyScreenshotToClipboard(const std::wstring& imagePath) {
    const bool copied =
        ScreenshotCapture::CopyImageToClipboard(imagePath, m_clipboardOwner);
    if (copied) {
        Logger::Info(L"[ScreenshotManager] Screenshot copied to clipboard");
    }
    return copied;
}

bool ScreenshotManager::HandleHotkeyMessage(WPARAM wParam, LPARAM lParam) {
    return m_hotkeyManager.HandleHotkeyMessage(wParam, lParam);
}

void ScreenshotManager::OnHotkeyTriggered(const HotkeyDefinition& /*hotkey*/) {
    Logger::Debug(L"[ScreenshotManager] Hotkey triggered");
    CaptureScreenshot(ScreenshotCapture::CaptureMode::ActiveWindow);
}

void ScreenshotManager::RefreshGlobalHotkey() {
    // Unregister old global hotkey
    if (m_globalHotkeyId >= 0) {
        m_hotkeyManager.UnregisterHotkey(m_globalHotkeyId);
        m_globalHotkeyId = -1;
    }

    // Register new global hotkey if enabled and valid
    if (m_settings.enabled && m_settings.globalHotkey.IsValid()) {
        HotkeyDefinition def = m_settings.globalHotkey;
        def.profileId = L""; // Empty = global

        auto hotkeyId = m_hotkeyManager.RegisterHotkey(def, [this](const HotkeyDefinition& hk) {
            OnHotkeyTriggered(hk);
        });

        if (hotkeyId.has_value()) {
            m_globalHotkeyId = hotkeyId.value();
            Logger::Info(L"[ScreenshotManager] Registered global hotkey");
        } else {
            Logger::Error(L"[ScreenshotManager] Failed to register global screenshot hotkey: " +
                          def.ToDisplayString());
        }
    }
}

std::wstring ScreenshotManager::GetSubdirectoryForCurrentProfile() const {
    if (m_activeProfileId.empty() || m_activeProfileId == L"monitor") {
        return L"Desktop";
    }

    // Use profile name if available, otherwise use ID
    std::wstring dirName = !m_activeProfileName.empty() ? m_activeProfileName : m_activeProfileId;
    return SanitizeDirectoryName(dirName);
}

std::wstring ScreenshotManager::SanitizeDirectoryName(const std::wstring& name) const {
    std::wstring sanitized = name;

    // Replace invalid filesystem characters
    const wchar_t invalidChars[] = L"<>:\"/\\|?*";
    for (wchar_t& ch : sanitized) {
        for (wchar_t invalid : invalidChars) {
            if (ch == invalid) {
                ch = L'_';
                break;
            }
        }
    }

    // Trim leading/trailing spaces and dots
    size_t start = sanitized.find_first_not_of(L" .");
    size_t end = sanitized.find_last_not_of(L" .");

    if (start == std::wstring::npos) {
        return L"Unknown";
    }

    sanitized = sanitized.substr(start, end - start + 1);

    // Ensure it's not empty
    if (sanitized.empty()) {
        return L"Unknown";
    }

    return sanitized;
}

void ScreenshotManager::OpenScreenshotsFolder() const {
    if (m_settings.screenshotsDirectory.empty()) {
        Logger::Warn(L"[ScreenshotManager] Cannot open folder: directory not configured");
        return;
    }

    // Ensure directory exists
    if (!ScreenshotCapture::EnsureDirectoryExists(m_settings.screenshotsDirectory)) {
        Logger::Error(L"[ScreenshotManager] Failed to create screenshots directory");
        return;
    }

    ShellExecuteW(nullptr, L"open", m_settings.screenshotsDirectory.c_str(), nullptr, nullptr, SW_SHOW);
    Logger::Info(L"[ScreenshotManager] Opened screenshots folder");
}

void ScreenshotManager::OpenProfileFolder(const std::wstring& profileName) const {
    if (m_settings.screenshotsDirectory.empty()) {
        Logger::Warn(L"[ScreenshotManager] Cannot open folder: directory not configured");
        return;
    }

    std::wstring fullPath = m_settings.screenshotsDirectory;
    if (fullPath.back() != L'\\' && fullPath.back() != L'/') {
        fullPath += L"\\";
    }
    fullPath += SanitizeDirectoryName(profileName);

    // Ensure directory exists
    if (!ScreenshotCapture::EnsureDirectoryExists(fullPath)) {
        Logger::Error(L"[ScreenshotManager] Failed to create profile folder");
        return;
    }

    ShellExecuteW(nullptr, L"open", fullPath.c_str(), nullptr, nullptr, SW_SHOW);
    Logger::Info(L"[ScreenshotManager] Opened profile folder");
}

} // namespace prismshift

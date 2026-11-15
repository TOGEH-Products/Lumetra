#include "ui/MainWindow.h"
#include "ui/UITheme.h"
#include "ui/Icons.h"
#include "ui/Controls.h"
#include "ui/TrayIcon.h"
#include "app/Application.h"
#include "utilities/Logger.h"
#include "utilities/AppIcon.h"
#include "utilities/StartupManager.h"
#include "utilities/Preferences.h"
#include "profiles/ProfileStorage.h"
#include "updates/UpdateConfig.h"
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <memory>
#include <new>
#include <system_error>
#include <thread>
#include <vector>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace prismshift {

namespace {
constexpr const wchar_t* MAIN_WINDOW_CLASS = L"Lumetra_MainWindowClass";
constexpr int WINDOW_WIDTH = 1180;
constexpr int WINDOW_HEIGHT = 760; // client-area design canvas (96-DPI units / DIPs)
constexpr int MIN_WINDOW_WIDTH = 1020;
constexpr int MIN_WINDOW_HEIGHT = 740;
constexpr int SIDEBAR_WIDTH = 180;  // Reduced from 180 to make room
constexpr int HEADER_HEIGHT = 96;
constexpr int LIST_PANE_WIDTH = 280; // Reduced from 280 to make room
constexpr int TERTIARY_RAILER_WIDTH = 140; // New tertiary railer for profile tabs
constexpr int MAX_FORM_WIDTH = 560;
constexpr int MAX_FORM_HEIGHT = 440;

constexpr UINT_PTR TIMER_ANIMATION = 2;
constexpr UINT_PTR TIMER_TOAST = 3;
constexpr UINT_PTR TIMER_SCREENSHOT_TOAST = 4;
constexpr UINT_PTR TIMER_SCREENSHOT_TOAST_ANIMATION = 5;
constexpr UINT_PTR TIMER_RESOLUTION_TOAST_ANIMATION = 6;
constexpr UINT ANIMATION_INTERVAL_MS = 16;
constexpr std::uint64_t UPDATE_CHECK_INTERVAL_SECONDS = 24ULL * 60 * 60;
constexpr UINT TOAST_DURATION_MS = 2600;
constexpr UINT IDM_SCREENSHOT_CLICK_OPEN_IMAGE = 3200;
constexpr UINT IDM_SCREENSHOT_CLICK_OPEN_FOLDER = 3201;
constexpr UINT IDM_SCREENSHOT_CLICK_COPY_IMAGE = 3202;
constexpr UINT IDM_RESOLUTION_AUTO = 3300;
constexpr UINT IDM_RESOLUTION_ASK = 3301;
constexpr UINT IDM_RESOLUTION_OFF = 3302;
constexpr UINT IDM_RESOLUTION_PRESET_BASE = 3400;
constexpr UINT IDM_RESOLUTION_SCALE_BASE = 3500;

constexpr DWORD DWMWA_WINDOW_CORNER_PREFERENCE_ATTR = 33;
constexpr DWORD DWMWA_BORDER_COLOR_ATTR = 34;
constexpr DWORD DWMWA_CAPTION_COLOR_ATTR = 35;
constexpr DWORD DWMWA_TEXT_COLOR_ATTR = 36;

constexpr UINT IDM_PROFILE_DUPLICATE = 3100;
constexpr UINT IDM_PROFILE_DELETE = 3101;
constexpr UINT IDM_PROFILE_EXPORT = 3102;
constexpr UINT IDM_PROFILE_IMPORT = 3103;

void PlayResolutionPopupSound() {
    std::vector<wchar_t> modulePath(32768);
    const DWORD pathLength = GetModuleFileNameW(
        nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (pathLength == 0 || pathLength >= modulePath.size()) {
        Logger::Debug(L"Resolution popup sound is unavailable; continuing silently.");
        return;
    }

    const std::filesystem::path soundsDirectory =
        std::filesystem::path(std::wstring(modulePath.data(), pathLength))
            .parent_path() / L"Sounds";
    const std::filesystem::path wavePath = soundsDirectory / L"PopupSfx.wav";
    const std::filesystem::path mp3Path = soundsDirectory / L"PopupSfx.mp3";
    const auto exists = [](const std::filesystem::path& path) {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES &&
               (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    };

    if (exists(wavePath)) {
        if (!PlaySoundW(wavePath.c_str(), nullptr,
                        SND_FILENAME | SND_ASYNC | SND_NODEFAULT)) {
            Logger::Warn(L"Could not play the resolution popup WAV sound.");
        }
        return;
    }
    if (!exists(mp3Path)) {
        Logger::Debug(L"Optional Sounds\\PopupSfx audio is not installed; continuing silently.");
        return;
    }

    static std::atomic_uint soundAliasId{ 0 };
    const std::wstring alias = L"LumetraResolutionPopup" +
                               std::to_wstring(++soundAliasId);
    try {
        std::thread([path = mp3Path.wstring(), alias]() {
            const std::wstring openCommand =
                L"open \"" + path + L"\" type mpegvideo alias " + alias;
            MCIERROR result =
                mciSendStringW(openCommand.c_str(), nullptr, 0, nullptr);
            if (result != 0) {
                Logger::Warn(L"Could not open the optional resolution popup sound.");
                return;
            }
            result = mciSendStringW((L"play " + alias + L" wait").c_str(),
                                    nullptr, 0, nullptr);
            mciSendStringW((L"close " + alias).c_str(), nullptr, 0, nullptr);
            if (result != 0) {
                Logger::Warn(L"Could not play the optional resolution popup sound.");
            }
        }).detach();
    } catch (const std::system_error&) {
        Logger::Warn(L"Could not start optional resolution popup sound playback.");
    }
}

std::wstring ProfileCountText(size_t count) {
    return std::to_wstring(count) + (count == 1 ? L" profile" : L" profiles");
}

const wchar_t* ResolutionScalingName(ResolutionScaling scaling) {
    switch (scaling) {
        case ResolutionScaling::FullScreenStretch: return L"Full screen stretch";
        case ResolutionScaling::NoScaling: return L"No scaling";
        default: return L"Maintain aspect ratio";
    }
}

std::uint64_t CurrentUnixTime() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::wstring FormatUpdateCheckTime(std::uint64_t timestamp) {
    if (timestamp == 0) return L"Never";
    const std::time_t time = static_cast<std::time_t>(timestamp);
    std::tm localTime{};
    if (localtime_s(&localTime, &time) != 0) return L"Previously";
    wchar_t buffer[64]{};
    if (std::wcsftime(buffer, std::size(buffer), L"%Y-%m-%d %H:%M",
                      &localTime) == 0) {
        return L"Previously";
    }
    return buffer;
}
} // namespace

MainWindow::MainWindow(ProfileManager& profileManager, DisplayManager& displayManager,
                       ResolutionManager& resolutionManager)
    : m_profileManager(profileManager), m_displayManager(displayManager),
      m_resolutionManager(resolutionManager) {
    s_instance = this;

    m_preferences = PreferencesStore::Load();
    Anim::enabled = !m_preferences.reduceMotion;
    UITheme::SetTheme(static_cast<ThemeId>(std::clamp(m_preferences.themeId, 0, 1)));

    m_navSlide.speed = 17.0f;
    m_navProfilesHover.speed = 18.0f;
    m_navMonitorHover.speed = 18.0f;
    m_navSettingsHover.speed = 18.0f;
    m_pauseHover.speed = 18.0f;
    m_pillHover.speed = 18.0f;
    m_resetHover.speed = 18.0f;
    m_emptyCtaHover.speed = 18.0f;
    m_toastAnim.speed = 16.0f;
    m_screenshotToastAnim.speed = 16.0f;
    for (Anim& a : m_tertiaryTabHover) a.speed = 18.0f;
    m_tertiaryTabIndicator.speed = 20.0f;

    // ---- Master list ----
    m_listView.SetCallbacks(
        [this](const std::wstring& profileId) {
            SelectProfile(profileId);
        },
        [this](const std::wstring& profileId, bool enabled) {
            m_profileManager.SetProfileEnabled(profileId, enabled);
            Redraw();
        },
        [this]() {
            BeginAddApp();
        }
    );

    // ---- Detail editor ----
    m_editorView.SetCallbacks(
        [this](const ColorProfile& updated) {
            if (m_profileManager.UpdateProfile(updated)) {
                if (auto* app = Application::GetInstance()) {
                    auto& screenshotManager = app->GetScreenshotManager();
                    screenshotManager.UnregisterProfileHotkey(updated.id);
                    if (!updated.screenshotHotkey.empty()) {
                        HotkeyDefinition hotkey;
                        if (hotkey.FromString(updated.screenshotHotkey)) {
                            screenshotManager.RegisterProfileHotkey(updated.id, hotkey);
                        }
                    }
                }
                const auto* active = m_displayManager.GetActiveProfile();
                if (active && active->id == updated.id) {
                    if (const auto* saved = m_profileManager.GetProfileById(updated.id)) {
                        m_displayManager.ApplyProfile(*saved);
                    }
                }
                if (updated.isDefault || updated.id == L"monitor") {
                    m_displayManager.SetMonitorProfile(updated);
                    if (m_onResolutionModeChanged) {
                        m_onResolutionModeChanged(m_preferences.resolutionSwitchMode);
                    }
                }
                m_displayManager.CommitPreview();
                ShowToast(L"Profile saved", updated.name + L" is up to date.");
            }
            Redraw();
        },
        [this](const ColorProfile& preview) {
            m_displayManager.PreviewProfile(preview);
        },
        [this](bool comparing) {
            if (comparing) {
                m_displayManager.PreviewProfile(m_displayManager.GetMonitorProfile());
            } else {
                m_displayManager.PreviewProfile(m_editorView.GetEditingProfile());
            }
            Redraw();
        },
        [this](const std::wstring& profileId) {
            OnSetProfileHotkey(profileId);
        }
    );
    m_editorView.SetResolutionPickerCallback([this]() {
        ShowResolutionPickerMenu();
    });
    m_editorView.SetScalingPickerCallback([this]() {
        ShowResolutionScalingMenu();
    });
    m_editorView.SetScalingSupportCallback([this](int width, int height) {
        return m_resolutionManager.SupportsScaling(width, height);
    });

    // ---- Add application ----
    m_addDialog.SetCallbacks(
        [this](const ColorProfile& newProfile) {
            std::wstring newId = newProfile.id;
            std::wstring newName = newProfile.name;
            if (m_profileManager.AddProfile(newProfile)) {
                SelectProfile(newId);
                ShowToast(L"Profile created", newName + L" is ready to tune.");
            } else {
                Redraw();
            }
        },
        [this]() {
            m_detailMode = DetailMode::Empty;
            Redraw();
        }
    );

    // ---- Settings ----
    m_settingsView.SetToggleCallback([this](SettingsView::ToggleId id, bool enabled) {
        OnSettingsToggle(id, enabled);
    });
    m_settingsView.SetResetDisplayCallback([this]() {
        ResetDisplay();
    });
    m_settingsView.SetOpenConfigCallback([]() {
        std::wstring dir = ProfileStorage::GetConfigDirectory();
        CreateDirectoryW(dir.c_str(), nullptr);
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    });
    m_settingsView.SetOpenScreenshotsCallback([this]() {
        OnOpenScreenshotsFolder();
    });
    m_settingsView.SetCheckUpdatesCallback([this]() {
        StartUpdateCheck(true);
    });
    m_settingsView.SetBrowseScreenshotsCallback([this]() {
        OnBrowseScreenshotsFolder();
    });
    m_settingsView.SetScreenshotHotkeyCallback([this](const std::wstring& currentHotkey) {
        OnSetScreenshotHotkey(currentHotkey);
        Redraw();
    });
    m_settingsView.SetScreenshotClickActionCallback([this]() {
        ShowScreenshotClickActionMenu();
    });
    m_settingsView.SetResolutionModeCallback([this](ResolutionSwitchMode) {
        ShowResolutionModeMenu();
    });
    m_settingsView.SetThemeCallback([this](int themeId) {
        OnThemeChanged(themeId);
    });
    m_settingsView.SetTheme(m_preferences.themeId);

    m_settingsView.SetConfigPath(ProfileStorage::GetConfigDirectory());
    m_settingsView.SetScreenshotsDirectory(m_preferences.screenshotsDirectory.empty()
        ? PreferencesStore::GetDefaultScreenshotsDirectory()
        : m_preferences.screenshotsDirectory);
    m_settingsView.SetScreenshotGlobalHotkey(m_preferences.screenshotGlobalHotkey);
    m_settingsView.SetScreenshotClickAction(m_preferences.screenshotClickAction);
    m_settingsView.SetResolutionSwitchMode(m_preferences.resolutionSwitchMode);
    m_settingsView.SetVersion(LUMETRA_VERSION);
    m_settingsView.SetBackend(L"Win32 gamma ramp + DWM color matrix");
    m_settingsView.SetUpdateStatus(
        UpdateService::IsConfigured(LUMETRA_GITHUB_OWNER, LUMETRA_GITHUB_REPO)
            ? L"Stable releases only. Last successful check: " +
                  FormatUpdateCheckTime(m_preferences.lastSuccessfulUpdateCheck)
            : L"Stable channel. Release repository is not configured for this build.");
    m_settingsView.SetToggleState(SettingsView::ToggleId::StartWithWindows, StartupManager::IsEnabled());
    m_settingsView.SetToggleState(SettingsView::ToggleId::StartMinimized, m_preferences.startMinimized);
    m_settingsView.SetToggleState(SettingsView::ToggleId::CloseToTray, m_preferences.closeToTray);
    m_settingsView.SetToggleState(SettingsView::ToggleId::Notifications, m_preferences.notifications);
    m_settingsView.SetToggleState(SettingsView::ToggleId::ReduceMotion, m_preferences.reduceMotion);
    m_settingsView.SetToggleState(SettingsView::ToggleId::Switching, !m_isPaused);
    m_settingsView.SetToggleState(SettingsView::ToggleId::Screenshots, m_preferences.screenshotsEnabled);
    m_settingsView.SetToggleState(
        SettingsView::ToggleId::AutoCopyScreenshot, m_preferences.autoCopyScreenshot);
    m_settingsView.SetToggleState(SettingsView::ToggleId::AutomaticUpdateChecks,
                                  m_preferences.automaticUpdateChecks);

    m_listView.SetSelectedId(L"");
}

MainWindow::~MainWindow() {
    m_updateCancelRequested.store(true);
    if (m_updateThread.joinable()) {
        m_updateThread.join();
    }
    if (m_hwnd) {
        MSG message{};
        while (PeekMessageW(&message, m_hwnd, UPDATE_JOB_COMPLETE,
                            UPDATE_JOB_COMPLETE, PM_REMOVE)) {
            delete reinterpret_cast<UpdateJobResult*>(message.lParam);
        }
    }
    if (m_resolutionToastHwnd) {
        m_resolutionToastRenderer.Shutdown();
        DestroyWindow(m_resolutionToastHwnd);
        m_resolutionToastHwnd = nullptr;
    }
    if (m_screenshotToastHwnd) {
        m_screenshotToastRenderer.Shutdown();
        DestroyWindow(m_screenshotToastHwnd);
        m_screenshotToastHwnd = nullptr;
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool MainWindow::Create(HINSTANCE hInstance, int nCmdShow) {
    // The color-matrix backend is only known after DisplayManager::Initialize(),
    // which runs before Create() but after this object is constructed.
    const ColorEngineCapabilities caps = m_displayManager.GetCapabilities();
    m_editorView.SetColorEffectsAvailable(
        caps.supportsSaturation && caps.supportsVibrance && caps.supportsHue);
    m_editorView.SetAvailableResolutions(m_resolutionManager.GetAvailableResolutions());

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // Rendered in WM_PAINT via Direct2D
    wc.lpszClassName = MAIN_WINDOW_CLASS;
    wc.hIcon = LoadAppIcon();
    wc.hIconSm = LoadAppIcon();

    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        Logger::Error(L"Failed to register Lumetra main window class.");
        return false;
    }

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenW - WINDOW_WIDTH) / 2;
    int posY = (screenH - WINDOW_HEIGHT) / 2;

    m_hwnd = CreateWindowExW(
        0,
        MAIN_WINDOW_CLASS,
        L"Lumetra",
        WS_OVERLAPPEDWINDOW,
        posX, posY, WINDOW_WIDTH, WINDOW_HEIGHT,
        nullptr,
        nullptr,
        hInstance,
        this
    );

    if (!m_hwnd) {
        Logger::Error(L"Failed to create Lumetra main window.");
        return false;
    }

    // Initialize the Direct2D renderer bound to this window. The render target
    // is created at the window's current monitor DPI (per-monitor aware).
    if (!m_renderer.Initialize(m_hwnd)) {
        Logger::Error(L"Failed to initialize the Direct2D renderer.");
        return false;
    }

    // Size the outer window so the client area lands on the design canvas
    // (1180x760 DIPs) at the current monitor DPI.
    ApplyInitialDpiAndSize();

    // Windows 10/11 chrome, themed to match the active palette.
    ApplyWindowChrome();

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
    MaybeStartUpdateWork();

    Logger::Info(L"Lumetra main window created and shown.");
    return true;
}

void MainWindow::ApplyWindowChrome() {
    if (!m_hwnd) return;

    // Windows 10/11 chrome, themed to match the active palette.
    BOOL darkMode = UITheme::IsDark() ? TRUE : FALSE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

    // Rounded corners + explicit border/caption colors (Windows 11).
    int cornerPreference = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE_ATTR,
                          &cornerPreference, sizeof(cornerPreference));
    COLORREF borderColor = UITheme::BORDER;
    DwmSetWindowAttribute(m_hwnd, DWMWA_BORDER_COLOR_ATTR, &borderColor, sizeof(borderColor));
    COLORREF captionColor = UITheme::BG_SIDEBAR;
    DwmSetWindowAttribute(m_hwnd, DWMWA_CAPTION_COLOR_ATTR, &captionColor, sizeof(captionColor));
    COLORREF textColor = UITheme::TEXT_PRIMARY;
    DwmSetWindowAttribute(m_hwnd, DWMWA_TEXT_COLOR_ATTR, &textColor, sizeof(textColor));
}

void MainWindow::OnThemeChanged(int themeId) {
    const int clamped = std::clamp(themeId, 0, 1);
    if (m_preferences.themeId == clamped) return;

    m_preferences.themeId = clamped;
    UITheme::SetTheme(static_cast<ThemeId>(clamped));
    SavePreferences();
    ApplyWindowChrome();
    Redraw();
}

void MainWindow::Show() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_RESTORE);
        SetForegroundWindow(m_hwnd);
    }
}

void MainWindow::Hide() {
    if (m_hwnd) {
        // An unsaved live preview must not survive the window being dismissed,
        // otherwise the previewed (often neutral) profile sticks to whatever
        // app is focused next.
        m_displayManager.CancelPreview();

        // When the window is hidden/closed, revert to the monitor baseline so
        // the currently-focused app gets the correct profile applied.
        m_displayManager.RestoreDefault();

        ShowWindow(m_hwnd, SW_HIDE);
        if (!m_shownTrayNotice && m_pTrayIcon && m_preferences.notifications) {
            m_pTrayIcon->ShowBalloonTip(
                L"Lumetra is still running",
                L"Lumetra continues running in the background. Access it from the system tray."
            );
            m_shownTrayNotice = true;
        }
    }
}

bool MainWindow::IsVisible() const {
    return m_hwnd && IsWindowVisible(m_hwnd);
}

void MainWindow::SetSwitchingPaused(bool paused) {
    const bool changed = (m_isPaused != paused);
    m_isPaused = paused;
    m_settingsView.SetToggleState(SettingsView::ToggleId::Switching, !paused);
    if (changed && IsVisible()) {
        ShowToast(paused ? L"Switching paused" : L"Switching resumed",
                  paused ? L"Profiles stay neutral until you resume."
                         : L"Profiles will apply as apps come forward.");
    }
    Redraw();
}

void MainWindow::SetActiveProfileName(const std::wstring& name) {
    m_activeProfileName = name;
    Redraw();
}

void MainWindow::Redraw() {
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
        StartAnimationTimer();
    }
}

void MainWindow::SelectSection(Section section) {
    if (m_section == section) return;
    if (section != Section::Monitor && m_displayManager.IsPreviewActive()) {
        m_displayManager.CancelPreview();
    }
    m_section = section;
    m_listView.BlurSearch();

    if (section == Section::Monitor) {
        SelectMonitor();
    } else {
        m_detailMode = DetailMode::Empty;
    }
    Redraw();
}

void MainWindow::SelectMonitor() {
    if (m_displayManager.IsPreviewActive()) {
        m_displayManager.CancelPreview();
    }
    m_selectedProfileId.clear();
    m_listView.SetSelectedId(L"");
    m_editorView.LoadProfile(m_profileManager.GetMonitorProfile());
    const auto& monitorProfile = m_profileManager.GetMonitorProfile();
    m_editorView.SetScalingModesSupported(
        monitorProfile.resolutionProfile &&
        m_resolutionManager.SupportsScaling(
            monitorProfile.resolutionProfile->width,
            monitorProfile.resolutionProfile->height));
    m_editorView.SetMonitorProfile(nullptr); // monitor tab edits the monitor profile itself
    m_detailMode = DetailMode::Editor;
}

void MainWindow::SelectProfile(const std::wstring& profileId) {
    const auto* profile = m_profileManager.GetProfileById(profileId);
    if (!profile) return;

    // The monitor/desktop profile lives in its own tab — route there instead
    // of opening it inside the game-profiles master-detail pane.
    if (profile->isDefault || profile->id == L"monitor") {
        SelectSection(Section::Monitor);
        return;
    }

    if (m_displayManager.IsPreviewActive()) {
        m_displayManager.CancelPreview();
    }

    m_selectedProfileId = profileId;
    m_listView.SetSelectedId(profileId);
    m_editorView.LoadProfile(*profile);
    m_editorView.SetScalingModesSupported(
        profile->resolutionProfile &&
        m_resolutionManager.SupportsScaling(profile->resolutionProfile->width,
                                            profile->resolutionProfile->height));
    m_editorView.SetMonitorProfile(&m_profileManager.GetMonitorProfile());

    m_section = Section::GameProfiles;
    m_detailMode = DetailMode::Editor;
    Redraw();
}

void MainWindow::BeginAddApp() {
    m_addDialog.Reset();
    m_detailMode = DetailMode::AddApp;
    m_section = Section::GameProfiles;
    Redraw();
}

void MainWindow::TogglePause() {
    bool next = !m_isPaused;
    if (m_onPauseToggle) {
        m_onPauseToggle(next);
    } else {
        SetSwitchingPaused(next);
    }
}

void MainWindow::ResetDisplay() {
    if (m_displayManager.IsPreviewActive()) {
        m_displayManager.CancelPreview();
    }
    if (m_onResetDisplay) {
        m_onResetDisplay();
    } else {
        m_displayManager.ResetDisplayToIdentity();
        SetActiveProfileName(L"Reset to Linear Identity");
    }
    ShowToast(L"Display reset", L"Gamma and color returned to neutral.");
}

void MainWindow::ShowToast(const std::wstring& title, const std::wstring& body) {
    m_toastTitle = title;
    m_toastBody = body;
    m_toastExpire = GetTickCount64() + TOAST_DURATION_MS;
    m_toastAnim.To(1.0f);
    if (m_hwnd) {
        KillTimer(m_hwnd, TIMER_TOAST);
        SetTimer(m_hwnd, TIMER_TOAST, TOAST_DURATION_MS, nullptr);
        StartAnimationTimer();
        Redraw();
    }
}

void MainWindow::ShowScreenshotToast(bool isError, const std::wstring& imagePath,
                                     bool imageCopied) {
    m_screenshotToastIsError = isError;
    m_screenshotToastImageCopied = imageCopied;
    m_screenshotToastImagePath = imagePath;
    if (!m_screenshotToastHwnd) {
        m_screenshotToastHwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            MAIN_WINDOW_CLASS,
            L"LumetraScreenshotToast",
            WS_POPUP,
            0, 0, 1, 1,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            &m_screenshotToastHwnd);
        if (!m_screenshotToastHwnd) {
            Logger::Error(L"Failed to create screenshot notification popup.");
            if (IsVisible()) {
                ShowToast(isError ? L"Screenshot failed" : L"Screenshot saved",
                          isError ? L"Please try again."
                                  : L"Saved to your screenshots folder.");
            } else if (m_pTrayIcon) {
                m_pTrayIcon->ShowBalloonTip(
                    isError ? L"Screenshot failed" : L"Screenshot saved",
                    isError ? L"Please try again." : L"Saved to your screenshots folder.");
            }
            return;
        }
        if (!m_screenshotToastRenderer.Initialize(m_screenshotToastHwnd)) {
            Logger::Error(L"Failed to initialize screenshot notification renderer.");
            m_screenshotToastRenderer.Shutdown();
            DestroyWindow(m_screenshotToastHwnd);
            m_screenshotToastHwnd = nullptr;
            if (m_pTrayIcon) {
                m_pTrayIcon->ShowBalloonTip(
                    isError ? L"Screenshot failed" : L"Screenshot saved",
                    isError ? L"Please try again." : L"Saved to your screenshots folder.");
            }
            return;
        }
    }

    const UINT dpi = m_screenshotToastRenderer.GetDpi();
    const int width = MulDiv(336, static_cast<int>(dpi), 96);
    m_screenshotToastPreview.Reset();
    if (!isError && !imagePath.empty()) {
        m_screenshotToastPreview =
            m_screenshotToastRenderer.LoadBitmapFromFile(imagePath);
        if (!m_screenshotToastPreview) {
            Logger::Warn(L"Could not load screenshot notification preview: " + imagePath);
        }
    }
    const int height = MulDiv(m_screenshotToastPreview ? 154 : 70,
                              static_cast<int>(dpi), 96);
    HMONITOR monitor = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    int x = 20;
    int y = 20;
    if (GetMonitorInfoW(monitor, &info)) {
        x = info.rcWork.left + MulDiv(20, static_cast<int>(dpi), 96);
        y = info.rcWork.top + MulDiv(20, static_cast<int>(dpi), 96);
    }

    m_screenshotToastTargetRect = RECT{ x, y, x + width, y + height };
    m_screenshotToastAnim.Set(0.0f);
    m_screenshotToastAnim.To(1.0f);
    m_lastScreenshotToastTick = GetTickCount64();
    KillTimer(m_screenshotToastHwnd, TIMER_SCREENSHOT_TOAST);
    SetTimer(m_screenshotToastHwnd, TIMER_SCREENSHOT_TOAST, TOAST_DURATION_MS, nullptr);
    SetTimer(m_screenshotToastHwnd, TIMER_SCREENSHOT_TOAST_ANIMATION,
             ANIMATION_INTERVAL_MS, nullptr);
    SetWindowPos(m_screenshotToastHwnd, HWND_TOPMOST, x - MulDiv(24, static_cast<int>(dpi), 96),
                 y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    m_screenshotToastRenderer.Resize(static_cast<UINT>(width), static_cast<UINT>(height));
    const int cornerDiameter = MulDiv(24, static_cast<int>(dpi), 96);
    HRGN roundedRegion = CreateRoundRectRgn(
        0, 0, width + 1, height + 1, cornerDiameter, cornerDiameter);
    if (!roundedRegion) {
        Logger::Error(L"Failed to create rounded screenshot notification region.");
    } else if (!SetWindowRgn(m_screenshotToastHwnd, roundedRegion, TRUE)) {
        DeleteObject(roundedRegion);
        Logger::Error(L"Failed to apply rounded screenshot notification region.");
    }
    InvalidateRect(m_screenshotToastHwnd, nullptr, FALSE);
    UpdateWindow(m_screenshotToastHwnd);
}

void MainWindow::SetResolutionPromptCallbacks(ResolutionApplyCallback onApply,
                                              ActionCallback onDismiss) {
    m_onResolutionApply = std::move(onApply);
    m_onResolutionDismiss = std::move(onDismiss);
}

void MainWindow::ShowResolutionPrompt(const std::wstring& gameName,
                                      const ResolutionProfile& current,
                                      const ResolutionProfile& proposed,
                                      bool supported,
                                      const std::wstring& status,
                                      HMONITOR monitor) {
    m_resolutionToastGameName = gameName;
    m_resolutionToastCurrent = current;
    m_resolutionToastProposed = proposed;
    m_resolutionToastSupported = supported;
    m_resolutionToastStatus = status;
    m_resolutionToastHoverApply = false;
    m_resolutionToastHoverDismiss = false;
    const auto showFallback = [this](const std::wstring& message) {
        if (m_onResolutionDismiss) m_onResolutionDismiss();
        if (IsVisible()) {
            ShowToast(L"Resolution prompt unavailable", message);
        } else if (m_pTrayIcon) {
            m_pTrayIcon->ShowBalloonTip(L"Resolution prompt unavailable", message);
        }
    };

    if (!m_resolutionToastHwnd) {
        m_resolutionToastHwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            MAIN_WINDOW_CLASS, L"LumetraResolutionPrompt", WS_POPUP,
            0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr),
            &m_resolutionToastHwnd);
        if (!m_resolutionToastHwnd) {
            Logger::Error(L"Failed to create resolution profile notification popup.");
            showFallback(supported
                ? L"Open Lumetra to review the selected display mode."
                : L"The selected mode is not supported by this display.");
            return;
        }
        if (!m_resolutionToastRenderer.Initialize(m_resolutionToastHwnd)) {
            Logger::Error(L"Failed to initialize resolution popup renderer.");
            m_resolutionToastRenderer.Shutdown();
            DestroyWindow(m_resolutionToastHwnd);
            m_resolutionToastHwnd = nullptr;
            showFallback(L"Open Lumetra to review the selected display mode.");
            return;
        }
    }

    UpdateResolutionToastLayout(monitor);
    InvalidateRect(m_resolutionToastHwnd, nullptr, FALSE);
    UpdateWindow(m_resolutionToastHwnd);
    PlayResolutionPopupSound();
}

void MainWindow::UpdateResolutionToastLayout(HMONITOR monitor) {
    if (!m_resolutionToastHwnd) {
        return;
    }

    // The popup can move between monitors with a different scaling, so its
    // size is derived from the DPI the renderer currently uses instead of a
    // cached one. A stale DPI would put the buttons somewhere other than
    // where they are drawn, and the clicks would land on nothing.
    const UINT dpi = m_resolutionToastRenderer.GetDpi();
    const int width = MulDiv(400, static_cast<int>(dpi), 96);
    const int height = MulDiv(224, static_cast<int>(dpi), 96);

    HMONITOR target = monitor;
    if (!target) {
        target = MonitorFromWindow(m_resolutionToastHwnd, MONITOR_DEFAULTTOPRIMARY);
    }
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    int x = 20;
    int y = 20;
    if (GetMonitorInfoW(target, &info)) {
        x = info.rcWork.right - width - MulDiv(20, static_cast<int>(dpi), 96);
        y = info.rcWork.bottom - height - MulDiv(20, static_cast<int>(dpi), 96);
    }
    SetWindowPos(m_resolutionToastHwnd, HWND_TOPMOST, x, y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    m_resolutionToastRenderer.Resize(static_cast<UINT>(width),
                                     static_cast<UINT>(height));
    ApplyResolutionToastRegion();
}

void MainWindow::ApplyResolutionToastRegion() {
    if (!m_resolutionToastHwnd) {
        return;
    }

    RECT windowRect{};
    if (!GetWindowRect(m_resolutionToastHwnd, &windowRect)) {
        return;
    }
    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;
    const UINT dpi = m_resolutionToastRenderer.GetDpi();

    HRGN roundedRegion = CreateRoundRectRgn(0, 0, width + 1, height + 1,
                                            MulDiv(24, static_cast<int>(dpi), 96),
                                            MulDiv(24, static_cast<int>(dpi), 96));
    if (!roundedRegion) {
        Logger::Error(L"Failed to create rounded resolution popup region.");
        return;
    }
    if (!SetWindowRgn(m_resolutionToastHwnd, roundedRegion, TRUE)) {
        DeleteObject(roundedRegion);
        Logger::Error(L"Failed to apply rounded resolution popup region.");
    }
}

void MainWindow::UpdateResolutionToastHover(POINT dipPoint) {
    const bool hoverDismiss = PtInRect(&RESOLUTION_TOAST_DISMISS_RECT, dipPoint);
    const bool hoverApply = m_resolutionToastSupported &&
                            PtInRect(&RESOLUTION_TOAST_APPLY_RECT, dipPoint);
    if (hoverDismiss == m_resolutionToastHoverDismiss &&
        hoverApply == m_resolutionToastHoverApply) {
        return;
    }
    m_resolutionToastHoverDismiss = hoverDismiss;
    m_resolutionToastHoverApply = hoverApply;
    if (m_resolutionToastHwnd) {
        InvalidateRect(m_resolutionToastHwnd, nullptr, FALSE);
    }
}

void MainWindow::DismissResolutionPrompt() {
    if (m_resolutionToastHwnd) {
        ShowWindow(m_resolutionToastHwnd, SW_HIDE);
    }
}

void MainWindow::ShowResolutionFailure(const std::wstring& message) {
    if (IsVisible()) {
        ShowToast(L"Resolution profile issue", message);
    } else if (m_pTrayIcon) {
        m_pTrayIcon->ShowBalloonTip(L"Resolution profile issue", message);
    }
}

void MainWindow::PaintResolutionToast(Renderer& r) {
    constexpr int width = 400;
    constexpr int height = 224;
    const RECT card{ 0, 0, width, height };
    // The prompt floats above whatever the player is looking at, so it gets
    // the strongest glass treatment in the app. The radius matches the
    // rounded window region exactly, so the glass body fills the clipped
    // shape edge to edge with no unpainted sliver at the corners.
    UITheme::DrawGlassCard(r, card, 12, UITheme::BG_RAISED, 1.15f, 0.60f, 22.0f, 8.0f);

    const RECT icon{ 18, 18, 48, 48 };
    UITheme::DrawRoundedCard(r, icon, 10, UITheme::BG_SURFACE, UITheme::BORDER);
    Icons::Draw(r, Icons::FullScreen, icon, 16, UITheme::TEXT_PRIMARY);
    TextStyle eyebrow = Typography::Eyebrow();
    eyebrow.sizeDip = 9.0f;
    r.DrawText(L"RESOLUTION PROFILE", eyebrow, 60.0f, 18.0f, 250.0f, 14.0f,
               UITheme::TEXT_TERTIARY);
    r.DrawText(m_resolutionToastGameName, Typography::PageTitle(),
               60.0f, 34.0f, 316.0f, 24.0f, UITheme::TEXT_PRIMARY,
               DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, true);

    UITheme::DrawHLine(r, 18, width - 18, 68, UITheme::DIVIDER);
    const std::wstring current = std::to_wstring(m_resolutionToastCurrent.width) +
        L" x " + std::to_wstring(m_resolutionToastCurrent.height);
    const std::wstring currentText = m_resolutionToastCurrent.width > 0 &&
                                             m_resolutionToastCurrent.height > 0
        ? current
        : L"Unavailable";
    const std::wstring proposed = std::to_wstring(m_resolutionToastProposed.width) +
        L" x " + std::to_wstring(m_resolutionToastProposed.height);
    r.DrawText(L"Current", Typography::Muted(), 20.0f, 80.0f, 150.0f, 18.0f,
               UITheme::TEXT_TERTIARY);
    r.DrawText(currentText, Typography::Numeric(), 20.0f, 101.0f, 150.0f, 24.0f,
               UITheme::TEXT_SECONDARY);
    Icons::Draw(r, Icons::ChevronRight, RECT{ 183, 96, 207, 120 }, 12,
                UITheme::TEXT_TERTIARY);
    r.DrawText(L"Proposed", Typography::Muted(), 224.0f, 80.0f, 150.0f, 18.0f,
               UITheme::TEXT_TERTIARY);
    r.DrawText(proposed, Typography::Numeric(), 224.0f, 101.0f, 150.0f, 24.0f,
               m_resolutionToastSupported ? UITheme::TEXT_PRIMARY
                                          : UITheme::TEXT_DISABLED);
    r.DrawText(ResolutionScalingName(m_resolutionToastProposed.scaling),
               Typography::Muted(), 224.0f, 126.0f, 154.0f, 16.0f,
               m_resolutionToastSupported ? UITheme::TEXT_TERTIARY
                                          : UITheme::TEXT_DISABLED);

    const std::wstring status = !m_resolutionToastStatus.empty()
        ? m_resolutionToastStatus
        : (m_resolutionToastSupported
               ? L"Apply this mode for the active game? Lumetra restores your previous mode when you leave it."
               : L"This resolution or scaling mode is not supported by the primary display.");
    r.DrawText(status, Typography::Muted(), 20.0f, 148.0f, 360.0f, 26.0f,
               m_resolutionToastSupported ? UITheme::TEXT_TERTIARY
                                          : UITheme::TEXT_DISABLED,
               DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, true);

    const RECT dismiss = RESOLUTION_TOAST_DISMISS_RECT;
    const RECT apply = RESOLUTION_TOAST_APPLY_RECT;
    Controls::DrawButton(r, dismiss, L"Dismiss", Controls::ButtonStyle::Secondary,
                         m_resolutionToastHoverDismiss ? 1.0f : 0.0f, false, true,
                         Typography::Button());
    Controls::DrawButton(r, apply, L"Apply", Controls::ButtonStyle::Primary,
                         m_resolutionToastHoverApply ? 1.0f : 0.0f, false,
                         m_resolutionToastSupported, Typography::Button());
}

void MainWindow::SavePreferences() {
    PreferencesStore::Save(m_preferences);
}

void MainWindow::SetUpdateStatus(const std::wstring& status) {
    m_settingsView.SetUpdateStatus(status);
    if (IsVisible()) Redraw();
}

void MainWindow::MaybeStartUpdateWork() {
    if (!UpdateService::IsConfigured(LUMETRA_GITHUB_OWNER,
                                     LUMETRA_GITHUB_REPO)) {
        if (m_preferences.lastRunVersion != LUMETRA_VERSION) {
            m_preferences.lastRunVersion = LUMETRA_VERSION;
            SavePreferences();
        }
        SetUpdateStatus(
            L"Stable channel. Release repository is not configured for this build.");
        return;
    }

    if (m_updateJobRunning.load()) return;
    if (!m_preferences.lastRunVersion.empty() &&
        m_preferences.lastRunVersion != LUMETRA_VERSION &&
        m_preferences.lastShownReleaseNotesVersion != LUMETRA_VERSION) {
        StartUpdateJob(UpdateJobKind::Notes, false, LUMETRA_VERSION);
        return;
    }

    if (m_preferences.lastRunVersion != LUMETRA_VERSION) {
        m_preferences.lastRunVersion = LUMETRA_VERSION;
        SavePreferences();
    }
    if (!m_preferences.automaticUpdateChecks) {
        SetUpdateStatus(L"Automatic checks are off. Stable releases only.");
        return;
    }

    const std::uint64_t now = CurrentUnixTime();
    const std::uint64_t lastAttempt =
        m_preferences.lastAutomaticUpdateAttempt;
    if (lastAttempt != 0 &&
        (now < lastAttempt || now - lastAttempt < UPDATE_CHECK_INTERVAL_SECONDS)) {
        SetUpdateStatus(
            L"Stable releases only. Last successful check: " +
            FormatUpdateCheckTime(m_preferences.lastSuccessfulUpdateCheck));
        return;
    }
    StartUpdateCheck(false);
}

void MainWindow::StartUpdateCheck(bool manual) {
    if (!UpdateService::IsConfigured(LUMETRA_GITHUB_OWNER,
                                     LUMETRA_GITHUB_REPO)) {
        const std::wstring message =
            L"Update checks are disabled until the official GitHub owner and repository are configured for this build.";
        SetUpdateStatus(message);
        if (manual) ShowToast(L"Updates unavailable", message);
        return;
    }
    StartUpdateJob(UpdateJobKind::Check, manual);
}

void MainWindow::StartUpdateDownload() {
    if (!m_availableRelease) {
        ShowToast(L"Download unavailable", L"Check for a stable release first.");
        return;
    }
    StartUpdateJob(UpdateJobKind::Download, false, L"", m_availableRelease);
}

void MainWindow::StartUpdateJob(UpdateJobKind kind, bool manual,
                                std::wstring version,
                                std::optional<ReleaseInfo> release) {
    if (m_updateJobRunning.exchange(true)) {
        if (manual) {
            ShowToast(L"Update check in progress",
                      L"Wait for the current update request to finish.");
        }
        return;
    }
    if (m_updateThread.joinable()) {
        m_updateThread.join();
    }
    if (kind == UpdateJobKind::Check && !manual) {
        m_preferences.lastAutomaticUpdateAttempt = CurrentUnixTime();
        SavePreferences();
    }
    m_updateCancelRequested.store(false);

    if (kind == UpdateJobKind::Check) {
        SetUpdateStatus(L"Checking the stable GitHub release...");
    } else if (kind == UpdateJobKind::Download) {
        SetUpdateStatus(L"Downloading the stable release for integrity verification...");
    } else {
        SetUpdateStatus(L"Loading release notes for the installed version...");
    }

    const HWND target = m_hwnd;
    try {
        m_updateThread = std::thread(
            [this, target, kind, manual, version = std::move(version),
             release = std::move(release)]() mutable {
                auto result = std::unique_ptr<UpdateJobResult>(
                    new (std::nothrow) UpdateJobResult());
                if (!result) {
                    Logger::Error(L"Could not allocate an update result.");
                    if (!PostMessageW(target, UPDATE_JOB_FAILED,
                                      manual ? 1 : 0, 0)) {
                        m_updateJobRunning.store(false);
                    }
                    return;
                }
                result->kind = kind;
                result->manual = manual;
                UpdateService service(LUMETRA_GITHUB_OWNER,
                                      LUMETRA_GITHUB_REPO, LUMETRA_VERSION);
                switch (kind) {
                    case UpdateJobKind::Check:
                        result->check =
                            service.CheckLatestRelease(&m_updateCancelRequested);
                        break;
                    case UpdateJobKind::Download:
                        if (release) {
                            result->download = service.DownloadRelease(
                                *release, &m_updateCancelRequested);
                        } else {
                            result->download.message =
                                L"No validated release package is available.";
                        }
                        break;
                    case UpdateJobKind::Notes:
                        result->notes = service.GetReleaseNotes(
                            version, &m_updateCancelRequested);
                        break;
                }
                if (!PostMessageW(target, UPDATE_JOB_COMPLETE, 0,
                                  reinterpret_cast<LPARAM>(result.get()))) {
                    m_updateJobRunning.store(false);
                    Logger::Error(L"Could not deliver the update result to the UI.");
                    return;
                }
                result.release();
            });
    } catch (const std::system_error&) {
        m_updateJobRunning.store(false);
        const std::wstring message = L"Windows could not start the update worker.";
        SetUpdateStatus(message);
        Logger::Error(message);
        if (manual) ShowToast(L"Update unavailable", message);
    }
}

void MainWindow::HandleUpdateJobResult(UpdateJobResult& result) {
    if (result.kind == UpdateJobKind::Check) {
        if (result.check.status == UpdateCheckStatus::Error) {
            SetUpdateStatus(result.check.message);
            Logger::Warn(L"Update check did not succeed: " + result.check.message);
            if (result.manual) {
                ShowToast(L"Update check failed", result.check.message);
            }
            return;
        }

        m_preferences.lastSuccessfulUpdateCheck = CurrentUnixTime();
        SavePreferences();
        const std::wstring lastCheck =
            FormatUpdateCheckTime(m_preferences.lastSuccessfulUpdateCheck);
        if (result.check.status == UpdateCheckStatus::UpToDate) {
            SetUpdateStatus(L"Stable releases only. Last successful check: " +
                            lastCheck + L".");
            if (result.manual) {
                ShowToast(L"Lumetra is up to date",
                          L"You already have the latest stable version.");
            }
            return;
        }

        m_availableRelease = std::move(result.check.release);
        m_updateNotesVersion = m_availableRelease->version.ToString();
        m_updateNotesTitle = m_availableRelease->title;
        m_updateNotesBody = m_availableRelease->notes;
        m_updateNotesScroll = 0;
        SetUpdateStatus(L"Version " + m_updateNotesVersion +
                        L" is available. Last successful check: " + lastCheck +
                        L". Download-only until trusted signing is configured.");
        if (!m_updateNoticeDismissed) {
            m_updateShowingNotes = false;
            m_updateNotificationVisible = true;
            if (IsVisible()) {
                Redraw();
            } else if (m_pTrayIcon && m_preferences.notifications) {
                m_pTrayIcon->ShowBalloonTip(
                    L"Lumetra update available",
                    L"Stable version " + m_updateNotesVersion +
                        L" is ready to review when you open Lumetra.");
            }
        }
        return;
    }

    if (result.kind == UpdateJobKind::Download) {
        if (!result.download.success) {
            SetUpdateStatus(result.download.message);
            Logger::Warn(L"Update package download failed: " +
                         result.download.message);
            ShowToast(L"Update download failed", result.download.message);
            return;
        }
        m_updateDownloadPath = result.download.filePath;
        SetUpdateStatus(L"SHA-256 integrity check passed. The package was not installed.");
        ShowToast(L"Download ready",
                  L"Verified package saved to Downloads. Lumetra did not run it.");
        return;
    }

    if (!result.notes.success) {
        SetUpdateStatus(result.notes.error);
        Logger::Warn(L"Could not load release notes: " + result.notes.error);
        return;
    }
    if (m_preferences.lastShownReleaseNotesVersion == result.notes.version) {
        m_preferences.lastRunVersion = LUMETRA_VERSION;
        SavePreferences();
        MaybeStartUpdateWork();
        return;
    }
    m_updateNotesVersion = std::move(result.notes.version);
    m_updateNotesTitle = std::move(result.notes.title);
    m_updateNotesBody = std::move(result.notes.notes);
    m_updateNotesScroll = 0;
    m_updateShowingNotes = true;
    m_updateNotificationVisible = true;
    if (IsVisible()) {
        Redraw();
    } else if (m_pTrayIcon && m_preferences.notifications) {
        m_pTrayIcon->ShowBalloonTip(
            L"What's new in Lumetra " + m_updateNotesVersion,
            L"Release notes are available when you open Lumetra.");
    }
}

MainWindow::UpdateDialogLayout MainWindow::GetUpdateDialogLayout() const {
    UpdateDialogLayout layout;
    if (!m_renderer.GetTarget()) return layout;
    const D2D1_SIZE_F size = m_renderer.GetTarget()->GetSize();
    const float width = std::min(660.0f, std::max(480.0f, size.width - 40.0f));
    const float height = std::min(m_updateShowingNotes ? 520.0f : 400.0f,
                                  std::max(320.0f, size.height - 40.0f));
    const float left = (size.width - width) / 2.0f;
    const float top = (size.height - height) / 2.0f;
    layout.card = RECT{ static_cast<LONG>(left), static_cast<LONG>(top),
                        static_cast<LONG>(left + width),
                        static_cast<LONG>(top + height) };
    layout.content = RECT{ layout.card.left + 28, layout.card.top + 118,
                           layout.card.right - 28, layout.card.bottom - 84 };
    const int buttonTop = layout.card.bottom - 62;
    if (m_updateShowingNotes) {
        layout.gotIt = RECT{ layout.card.right - 144, buttonTop,
                             layout.card.right - 28, buttonTop + 36 };
    } else {
        const int center = (layout.card.left + layout.card.right) / 2;
        layout.whatsNew = RECT{ center - 238, buttonTop, center - 94,
                                buttonTop + 36 };
        layout.download = RECT{ center - 82, buttonTop, center + 82,
                                buttonTop + 36 };
        layout.later = RECT{ center + 94, buttonTop, center + 238,
                             buttonTop + 36 };
    }
    return layout;
}

void MainWindow::PaintUpdateDialog(Renderer& r) {
    if (!m_updateNotificationVisible) return;
    const D2D1_SIZE_F size = r.GetTarget()->GetSize();
    r.FillRectGradient(D2D1::RectF(0.0f, 0.0f, size.width, size.height),
                       Color::Rgb(0, 0, 0, 0.62f),
                       Color::Rgb(0, 0, 0, 0.62f));
    const UpdateDialogLayout layout = GetUpdateDialogLayout();
    r.DrawShadowRoundedRect(
        D2D1::RectF(static_cast<float>(layout.card.left),
                    static_cast<float>(layout.card.top),
                    static_cast<float>(layout.card.right),
                    static_cast<float>(layout.card.bottom)),
        16.0f, 16.0f, 18.0f, 5.0f, UITheme::ShadowColor(0.35f));
    UITheme::DrawRoundedCard(r, layout.card, 16, UITheme::BG_SURFACE,
                             UITheme::BORDER_STRONG);

    TextStyle eyebrow = Typography::Muted();
    eyebrow.sizeDip = 10.0f;
    r.DrawText(m_updateShowingNotes ? L"RELEASE NOTES" : L"STABLE RELEASE",
               eyebrow, static_cast<float>(layout.card.left + 28),
               static_cast<float>(layout.card.top + 24), 260.0f, 16.0f,
               UITheme::TEXT_TERTIARY);
    TextStyle title = Typography::Body();
    title.sizeDip = 21.0f;
    title.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    const std::wstring titleText =
        m_updateShowingNotes
            ? L"What's New in " + m_updateNotesVersion
            : L"Lumetra update available";
    r.DrawText(titleText, title, static_cast<float>(layout.card.left + 28),
               static_cast<float>(layout.card.top + 47),
               static_cast<float>(layout.card.right - layout.card.left - 56),
               32.0f, UITheme::TEXT_PRIMARY, DWRITE_TEXT_ALIGNMENT_LEADING,
               DWRITE_PARAGRAPH_ALIGNMENT_NEAR, true);

    TextStyle body = Typography::Muted();
    body.wordWrap = true;
    body.sizeDip = 12.0f;
    if (m_updateShowingNotes) {
        r.GetTarget()->PushAxisAlignedClip(
            D2D1::RectF(static_cast<float>(layout.content.left),
                        static_cast<float>(layout.content.top),
                        static_cast<float>(layout.content.right),
                        static_cast<float>(layout.content.bottom)),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        r.DrawText(m_updateNotesBody, body,
                   static_cast<float>(layout.content.left),
                   static_cast<float>(layout.content.top - m_updateNotesScroll),
                   static_cast<float>(layout.content.right - layout.content.left),
                   12000.0f, UITheme::TEXT_SECONDARY,
                   DWRITE_TEXT_ALIGNMENT_LEADING,
                   DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        r.GetTarget()->PopAxisAlignedClip();
    } else {
        const std::wstring installed =
            L"Installed: " + std::wstring(LUMETRA_VERSION) + L"\nAvailable: " +
            m_updateNotesVersion;
        r.DrawText(installed, body,
                   static_cast<float>(layout.content.left),
                   static_cast<float>(layout.content.top),
                   static_cast<float>(layout.content.right - layout.content.left),
                   42.0f, UITheme::TEXT_SECONDARY);
        std::wstring summary = m_updateNotesBody;
        if (summary.empty()) summary = L"No release summary was provided.";
        if (summary.size() > 320) {
            summary.resize(320);
            summary += L"...";
        }
        body.sizeDip = 11.0f;
        r.DrawText(summary, body,
                   static_cast<float>(layout.content.left),
                   static_cast<float>(layout.content.top + 54),
                   static_cast<float>(layout.content.right - layout.content.left),
                   92.0f, UITheme::TEXT_TERTIARY,
                   DWRITE_TEXT_ALIGNMENT_LEADING,
                   DWRITE_PARAGRAPH_ALIGNMENT_NEAR, true);
        body.sizeDip = 10.0f;
        r.DrawText(
            L"The package will be downloaded and checked for SHA-256 integrity only. It will not be executed or installed because trusted publisher signing is not configured.",
            body, static_cast<float>(layout.content.left),
            static_cast<float>(layout.content.top + 154),
            static_cast<float>(layout.content.right - layout.content.left),
            58.0f, UITheme::TEXT_TERTIARY,
            DWRITE_TEXT_ALIGNMENT_LEADING,
            DWRITE_PARAGRAPH_ALIGNMENT_NEAR, true);
    }

    if (m_updateShowingNotes) {
        Controls::DrawButton(r, layout.gotIt, L"Got It",
                             Controls::ButtonStyle::Primary,
                             m_updateDialogHover == 3 ? 1.0f : 0.0f,
                             false, true);
    } else {
        Controls::DrawButton(r, layout.whatsNew, L"What's New",
                             Controls::ButtonStyle::Secondary,
                             m_updateDialogHover == 0 ? 1.0f : 0.0f,
                             false, true);
        Controls::DrawButton(r, layout.download, L"Update Now",
                             Controls::ButtonStyle::Primary,
                             m_updateDialogHover == 1 ? 1.0f : 0.0f,
                             false, true);
        Controls::DrawButton(r, layout.later, L"Later",
                             Controls::ButtonStyle::Secondary,
                             m_updateDialogHover == 2 ? 1.0f : 0.0f,
                             false, true);
    }
}

void MainWindow::ShowResolutionModeMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        Logger::Error(L"Failed to create resolution switching mode menu.");
        return;
    }
    AppendMenuW(menu, MF_STRING |
                    (m_preferences.resolutionSwitchMode == ResolutionSwitchMode::AutoSwitch
                         ? MF_CHECKED : 0),
                IDM_RESOLUTION_AUTO, L"Auto-Switch");
    AppendMenuW(menu, MF_STRING |
                    (m_preferences.resolutionSwitchMode ==
                             ResolutionSwitchMode::AskBeforeApplying
                         ? MF_CHECKED : 0),
                IDM_RESOLUTION_ASK, L"Ask Before Applying");
    AppendMenuW(menu, MF_STRING |
                    (m_preferences.resolutionSwitchMode == ResolutionSwitchMode::Off
                         ? MF_CHECKED : 0),
                IDM_RESOLUTION_OFF, L"Off");

    POINT screenPoint{};
    if (!GetCursorPos(&screenPoint)) {
        DestroyMenu(menu);
        Logger::Error(L"Failed to locate resolution mode menu position.");
        return;
    }
    SetForegroundWindow(m_hwnd);
    const UINT command = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y,
        0, m_hwnd, nullptr);
    DestroyMenu(menu);

    switch (command) {
        case IDM_RESOLUTION_AUTO:
            m_preferences.resolutionSwitchMode = ResolutionSwitchMode::AutoSwitch;
            break;
        case IDM_RESOLUTION_ASK:
            m_preferences.resolutionSwitchMode =
                ResolutionSwitchMode::AskBeforeApplying;
            break;
        case IDM_RESOLUTION_OFF:
            m_preferences.resolutionSwitchMode = ResolutionSwitchMode::Off;
            break;
        default:
            return;
    }
    m_settingsView.SetResolutionSwitchMode(m_preferences.resolutionSwitchMode);
    SavePreferences();
    if (m_onResolutionModeChanged) {
        m_onResolutionModeChanged(m_preferences.resolutionSwitchMode);
    }
    Redraw();
}

void MainWindow::ShowResolutionPickerMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        Logger::Error(L"Failed to create resolution preset menu.");
        return;
    }

    std::vector<ResolutionProfile> allModes =
        m_resolutionManager.GetAvailableResolutions();
    static constexpr std::pair<int, int> popular[] = {
        { 1280, 720 }, { 1366, 768 }, { 1600, 900 },
        { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 },
        { 1024, 768 }, { 1280, 960 }, { 1440, 1080 }
    };
    std::vector<ResolutionProfile> presets;
    for (const auto& preset : popular) {
        const auto found = std::find_if(
            allModes.begin(), allModes.end(),
            [&preset](const ResolutionProfile& mode) {
                return mode.width == preset.first && mode.height == preset.second;
            });
        if (found != allModes.end()) presets.push_back(*found);
    }
    for (const auto& mode : allModes) {
            const bool alreadyAdded = std::any_of(
                presets.begin(), presets.end(),
                [&mode](const ResolutionProfile& preset) {
                    return preset.width == mode.width && preset.height == mode.height;
                });
            if (!alreadyAdded) {
                presets.push_back(mode);
            }
    }
    if (presets.empty()) {
        DestroyMenu(menu);
        ShowToast(L"No supported resolutions", L"Windows reported no display modes.");
        return;
    }

    const auto& editing = m_editorView.GetEditingProfile();
    for (size_t i = 0; i < presets.size(); ++i) {
        const std::wstring label = std::to_wstring(presets[i].width) + L" x " +
                                   std::to_wstring(presets[i].height);
        const bool selected = editing.resolutionProfile &&
            editing.resolutionProfile->width == presets[i].width &&
            editing.resolutionProfile->height == presets[i].height;
        AppendMenuW(menu, MF_STRING | (selected ? MF_CHECKED : 0),
                    IDM_RESOLUTION_PRESET_BASE + static_cast<UINT>(i),
                    label.c_str());
    }

    POINT screenPoint{};
    if (!GetCursorPos(&screenPoint)) {
        DestroyMenu(menu);
        Logger::Error(L"Failed to locate resolution preset menu position.");
        return;
    }
    SetForegroundWindow(m_hwnd);
    const UINT command = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y,
        0, m_hwnd, nullptr);
    DestroyMenu(menu);
    if (command < IDM_RESOLUTION_PRESET_BASE) return;
    const size_t index = command - IDM_RESOLUTION_PRESET_BASE;
    if (index >= presets.size()) return;

    ResolutionProfile selected = presets[index];
    if (editing.resolutionProfile) {
        selected.scaling = editing.resolutionProfile->scaling;
        if (!m_resolutionManager.IsSupported(selected)) {
            selected.scaling = ResolutionScaling::MaintainAspectRatio;
        }
    }
    m_editorView.SetResolutionProfile(selected);
    m_editorView.SetScalingModesSupported(
        m_resolutionManager.SupportsScaling(selected.width, selected.height));
    Redraw();
}

void MainWindow::ShowResolutionScalingMenu() {
    const auto& editing = m_editorView.GetEditingProfile();
    if (!editing.resolutionProfile) return;

    HMENU menu = CreatePopupMenu();
    if (!menu) {
        Logger::Error(L"Failed to create resolution scaling menu.");
        return;
    }
    const ResolutionProfile current = *editing.resolutionProfile;
    const ResolutionScaling options[] = {
        ResolutionScaling::MaintainAspectRatio,
        ResolutionScaling::FullScreenStretch,
        ResolutionScaling::NoScaling
    };
    const wchar_t* labels[] = {
        L"Maintain aspect ratio",
        L"Full screen stretch",
        L"No scaling"
    };
    for (size_t i = 0; i < std::size(options); ++i) {
        ResolutionProfile candidate = current;
        candidate.scaling = options[i];
        UINT flags = MF_STRING;
        if (options[i] == current.scaling) flags |= MF_CHECKED;
        if (!m_resolutionManager.IsSupported(candidate)) flags |= MF_GRAYED;
        AppendMenuW(menu, flags, IDM_RESOLUTION_SCALE_BASE + static_cast<UINT>(i),
                    labels[i]);
    }

    POINT screenPoint{};
    if (!GetCursorPos(&screenPoint)) {
        DestroyMenu(menu);
        Logger::Error(L"Failed to locate resolution scaling menu position.");
        return;
    }
    SetForegroundWindow(m_hwnd);
    const UINT command = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y,
        0, m_hwnd, nullptr);
    DestroyMenu(menu);
    if (command < IDM_RESOLUTION_SCALE_BASE ||
        command >= IDM_RESOLUTION_SCALE_BASE + std::size(options)) {
        return;
    }
    m_editorView.SetResolutionScaling(
        options[command - IDM_RESOLUTION_SCALE_BASE]);
    Redraw();
}
void MainWindow::OnSettingsToggle(SettingsView::ToggleId id, bool enabled) {
    switch (id) {
        case SettingsView::ToggleId::Screenshots: {
            m_preferences.screenshotsEnabled = enabled;
            PreferencesStore::Save(m_preferences);
            // Update screenshot manager
            if (Application::GetInstance()) {
                auto& mgr = Application::GetInstance()->GetScreenshotManager();
                auto settings = mgr.GetSettings();
                settings.enabled = enabled;
                mgr.SetSettings(settings);
            }
            break;
        }
        case SettingsView::ToggleId::AutoCopyScreenshot: {
            m_preferences.autoCopyScreenshot = enabled;
            if (enabled &&
                m_preferences.screenshotClickAction == ScreenshotClickAction::CopyImage) {
                m_preferences.screenshotClickAction = ScreenshotClickAction::OpenImage;
                m_settingsView.SetScreenshotClickAction(m_preferences.screenshotClickAction);
            }
            SavePreferences();
            if (Application::GetInstance()) {
                Application::GetInstance()->GetScreenshotManager().SetAutoCopyEnabled(enabled);
            }
            break;
        }
        case SettingsView::ToggleId::StartWithWindows: {
            if (!StartupManager::SetEnabled(enabled)) {
                m_settingsView.SetToggleState(id, !enabled);
                ShowToast(L"Startup unchanged", L"Windows blocked the startup entry.");
            } else {
                ShowToast(L"Startup updated", enabled ? L"Lumetra will launch when you sign in."
                                                      : L"Lumetra will no longer launch at sign-in.");
            }
            break;
        }
        case SettingsView::ToggleId::StartMinimized:
            m_preferences.startMinimized = enabled;
            SavePreferences();
            break;
        case SettingsView::ToggleId::CloseToTray:
            m_preferences.closeToTray = enabled;
            SavePreferences();
            break;
        case SettingsView::ToggleId::Notifications:
            m_preferences.notifications = enabled;
            SavePreferences();
            break;
        case SettingsView::ToggleId::ReduceMotion:
            m_preferences.reduceMotion = enabled;
            Anim::enabled = !enabled;
            SavePreferences();
            break;
        case SettingsView::ToggleId::AutomaticUpdateChecks:
            m_preferences.automaticUpdateChecks = enabled;
            SavePreferences();
            if (enabled) MaybeStartUpdateWork();
            break;
        case SettingsView::ToggleId::Switching:
            if (enabled == m_isPaused) {
                TogglePause();
            } else {
                m_settingsView.SetToggleState(id, enabled);
            }
            break;
    }
    Redraw();
}

// ---- Layout ----

void MainWindow::ApplyInitialDpiAndSize() {
    const UINT dpi = m_renderer.GetDpi();

    const int width = MulDiv(WINDOW_WIDTH, static_cast<int>(dpi), 96);
    const int height = MulDiv(WINDOW_HEIGHT, static_cast<int>(dpi), 96);

    // Size the outer window so the client area lands on the design canvas
    // (1180x760 DIPs) rather than shrinking by the caption and borders.
    RECT frame = { 0, 0, width, height };
    AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    const int outerW = frame.right - frame.left;
    const int outerH = frame.bottom - frame.top;

    HMONITOR monitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
        const int workW = info.rcWork.right - info.rcWork.left;
        const int workH = info.rcWork.bottom - info.rcWork.top;
        const int x = info.rcWork.left + (workW - outerW) / 2;
        const int y = info.rcWork.top + (workH - outerH) / 2;
        SetWindowPos(m_hwnd, nullptr, x, y, outerW, outerH, SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        SetWindowPos(m_hwnd, nullptr, 0, 0, outerW, outerH,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
    }
}

RECT MainWindow::ClientRect() const {
    D2D1_SIZE_F size = m_renderer.GetTarget()->GetSize();
    return RECT{ 0, 0, static_cast<LONG>(std::floor(size.width)),
                 static_cast<LONG>(std::floor(size.height)) };
}

RECT MainWindow::SidebarRect() const {
    RECT client = ClientRect();
    return RECT{ client.left, client.top, SIDEBAR_WIDTH, client.bottom };
}

RECT MainWindow::HeaderRect() const {
    RECT client = ClientRect();
    return RECT{ SIDEBAR_WIDTH, 0, client.right, HEADER_HEIGHT };
}

RECT MainWindow::BodyRect() const {
    RECT client = ClientRect();
    return RECT{ SIDEBAR_WIDTH, HEADER_HEIGHT, client.right, client.bottom };
}

RECT MainWindow::ListPaneRect() const {
    RECT body = BodyRect();
    return RECT{ body.left, body.top, body.left + LIST_PANE_WIDTH, body.bottom };
}

RECT MainWindow::TertiaryRailRect() const {
    RECT body = BodyRect();
    return RECT{ body.left + LIST_PANE_WIDTH, body.top,
                 body.left + LIST_PANE_WIDTH + TERTIARY_RAILER_WIDTH, body.bottom };
}

RECT MainWindow::ProfileTabRailRect() const {
    if (m_section == Section::Monitor) {
        RECT body = BodyRect();
        return RECT{ body.left, body.top,
                     body.left + TERTIARY_RAILER_WIDTH, body.bottom };
    }
    return TertiaryRailRect();
}

RECT MainWindow::MonitorEditorRect() const {
    RECT body = BodyRect();
    return RECT{ body.left + TERTIARY_RAILER_WIDTH, body.top,
                 body.right, body.bottom };
}

RECT MainWindow::DetailPaneRect() const {
    RECT body = BodyRect();
    return RECT{ body.left + LIST_PANE_WIDTH + TERTIARY_RAILER_WIDTH, body.top, body.right, body.bottom };
}

RECT MainWindow::AddFormRect() const {
    RECT detail = DetailPaneRect();
    int width = std::min(MAX_FORM_WIDTH, static_cast<int>(detail.right - detail.left) - 48);
    int height = std::min(MAX_FORM_HEIGHT, static_cast<int>(detail.bottom - detail.top) - 40);
    const int cx = (detail.left + detail.right) / 2;
    const int cy = (detail.top + detail.bottom) / 2;
    return RECT{ cx - width / 2, cy - height / 2, cx + width / 2, cy + height / 2 };
}

RECT MainWindow::NavItemRect(Section section) const {
    const int top =
        126 + (section == Section::GameProfiles ? 48 : section == Section::Settings ? 96 : 0);
    return RECT{ 12, top, SIDEBAR_WIDTH - 12, top + 42 };
}

RECT MainWindow::TertiaryRailItemRect(int index) const {
    RECT rail = ProfileTabRailRect();
    const int itemHeight = 48;
    const int top = rail.top + 120 + index * (itemHeight + 8);
    return RECT{ rail.left + 12, top, rail.right - 12, top + itemHeight };
}

RECT MainWindow::PauseButtonRect() const {
    RECT sidebar = SidebarRect();
    const int cardBottom = sidebar.bottom - 44;
    // Center a fixed-width button inside the card rather than stretching
    // it edge-to-edge, so it doesn't look too wide on a 140-px sidebar.
    const int btnW = 110;
    const int cx = (sidebar.left + sidebar.right) / 2;
    return RECT{ cx - btnW / 2, cardBottom - 44, cx + btnW / 2, cardBottom - 14 };
}

RECT MainWindow::HeaderStatusRect() const {
    RECT header = HeaderRect();
    const int w = 116;
    const int h = 34;
    return RECT{ header.right - 32 - w, header.top + 31, header.right - 32,
                 header.top + 31 + h };
}

RECT MainWindow::ResetButtonRect() const {
    RECT pill = HeaderStatusRect();
    const int w = 136;
    return RECT{ pill.left - 10 - w, pill.top, pill.left - 10, pill.bottom };
}

RECT MainWindow::EmptyCardRect() const {
    RECT detail = DetailPaneRect();
    const int w = 420;
    const int h = 264;
    const int cx = (detail.left + detail.right) / 2;
    const int cy = (detail.top + detail.bottom) / 2;
    return RECT{ cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2 };
}

RECT MainWindow::EmptyCtaRect() const {
    RECT card = EmptyCardRect();
    const int w = 156;
    const int h = 40;
    const int cx = (card.left + card.right) / 2;
    return RECT{ cx - w / 2, card.top + 186, cx + w / 2, card.top + 186 + h };
}

POINT MainWindow::ToDip(POINT pt) const {
    D2D1_POINT_2F d = m_renderer.ToDip(pt);
    return POINT{ static_cast<LONG>(std::floor(d.x)), static_cast<LONG>(std::floor(d.y)) };
}

POINT MainWindow::ToPhysical(POINT dipPt) const {
    return m_renderer.ToPhysical(
        D2D1::Point2F(static_cast<float>(dipPt.x), static_cast<float>(dipPt.y)));
}

// ---- Painting ----

void MainWindow::PaintSidebar(Renderer& r, const RECT& rcSidebar) {
    // The rail is a graded surface rather than a flat fill, which gives the
    // window a sense of depth before a single control is drawn.
    UITheme::DrawPanel(r, rcSidebar, UITheme::BG_SIDEBAR,
                       UITheme::Darken(UITheme::BG_SIDEBAR, 4), false);
    // The rail catches the light along its top edge, like a pane standing
    // upright against a window.
    UITheme::DrawGlassEdge(r, rcSidebar, 0.8f);

    RECT rcEdge = { rcSidebar.right - 1, rcSidebar.top, rcSidebar.right, rcSidebar.bottom };
    UITheme::FillSolidRect(r, rcEdge, UITheme::BORDER);

    // Brand mark: logo tile with rounded corners.
    RECT rcLogo = { rcSidebar.left + 20, rcSidebar.top + 22, rcSidebar.left + 54, rcSidebar.top + 56 };

    // Lazily load the logo from Logo.jpg (your crisp vector-style image).
    if (!m_logoBitmap) {
        // Search for Logo.jpg in the same locations we search for Icon.ico.
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring exeDir(exePath);
        size_t slash = exeDir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) exeDir.erase(slash);

        std::vector<std::wstring> candidates = {
            exeDir + L"\\Logo.jpg",
            exeDir + L"\\..\\Logo.jpg",
            L"Logo.jpg"
        };
        for (const auto& path : candidates) {
            m_logoBitmap = r.LoadBitmapFromFile(path);
            if (m_logoBitmap) break;
        }
    }

    if (m_logoBitmap) {
        // Share the loaded mark with the About card in Settings.
        m_settingsView.SetLogoBitmap(m_logoBitmap.Get());
        // The plate under the mark lifts it off the rail.
        RECT rcPlate = { rcLogo.left - 3, rcLogo.top - 3, rcLogo.right + 3,
                         rcLogo.bottom + 3 };
        r.DrawShadowRoundedRect(
            D2D1::RectF(static_cast<float>(rcPlate.left), static_cast<float>(rcPlate.top),
                        static_cast<float>(rcPlate.right), static_cast<float>(rcPlate.bottom)),
            13.0f, 13.0f, 8.0f, 3.0f, UITheme::ShadowColor(0.18f));
        // Draw with rounded corners matching the tile's 10px radius.
        D2D1_RECT_F destF = D2D1::RectF(
            static_cast<float>(rcLogo.left),
            static_cast<float>(rcLogo.top),
            static_cast<float>(rcLogo.right),
            static_cast<float>(rcLogo.bottom));
        r.DrawBitmapRounded(m_logoBitmap.Get(), destF, 10.0f);
    } else {
        // Fallback: black rounded tile + "L" lettermark.
        UITheme::DrawRoundedCard(r, rcLogo, 10, UITheme::INVERSE, UITheme::INVERSE);
        TextStyle logoStyle = Typography::PageTitle();
        logoStyle.sizeDip = 18.0f;
        logoStyle.weight = DWRITE_FONT_WEIGHT_BOLD;
        r.DrawText(L"L", logoStyle,
                   static_cast<float>(rcLogo.left), static_cast<float>(rcLogo.top),
                   static_cast<float>(rcLogo.right - rcLogo.left),
                   static_cast<float>(rcLogo.bottom - rcLogo.top),
                   UITheme::INVERSE_TEXT,
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    {
        TextStyle wordmarkStyle = Typography::PageTitle();
        wordmarkStyle.sizeDip = 15.0f;
        wordmarkStyle.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        RECT rcWordmark = { rcLogo.right + 12, rcSidebar.top + 21, rcSidebar.right - 16,
                            rcSidebar.top + 41 };
        r.DrawText(L"Lumetra", wordmarkStyle,
                   static_cast<float>(rcWordmark.left), static_cast<float>(rcWordmark.top),
                   static_cast<float>(rcWordmark.right - rcWordmark.left),
                   static_cast<float>(rcWordmark.bottom - rcWordmark.top),
                   UITheme::TEXT_PRIMARY);
    }

    UITheme::DrawEyebrow(r, L"DISPLAY STUDIO",
                         RECT{ rcLogo.right + 12, rcSidebar.top + 42, rcSidebar.right - 16,
                               rcSidebar.top + 56 },
                         UITheme::TEXT_TERTIARY, 8, FW_SEMIBOLD, 1.0f);

    UITheme::DrawHLine(r, rcSidebar.left + 20, rcSidebar.right - 20,
                       rcSidebar.top + 82, UITheme::DIVIDER);
    UITheme::DrawEyebrow(r, L"WORKSPACE",
                         RECT{ rcSidebar.left + 22, rcSidebar.top + 100, rcSidebar.right - 20,
                               rcSidebar.top + 114 },
                         UITheme::TEXT_TERTIARY, 9, FW_SEMIBOLD, 1.0f);

    // Animated active pill slides between navigation items.
    m_navSlide.To(static_cast<float>(static_cast<int>(m_section)));
    const RECT rcMonitorNav = NavItemRect(Section::Monitor);
    const RECT rcGameProfilesNav = NavItemRect(Section::GameProfiles);
    const RECT rcSettingsNav = NavItemRect(Section::Settings);

    // rails[] is indexed to match Section: 0 = Monitor, 1 = GameProfiles, 2 = Settings
    const RECT rails[] = { rcMonitorNav, rcGameProfilesNav, rcSettingsNav };

    const float raw = m_navSlide.value; // 0 = Monitor, 1 = Game Profiles, 2 = Settings
    const int fromIdx = static_cast<int>(std::floor(raw));
    const int toIdx = std::min(fromIdx + 1, 2);
    const float local = raw - static_cast<float>(fromIdx);
    const int activeTop = AnimLerpInt(rails[fromIdx].top, rails[toIdx].top, local);
    RECT rcActive = { rails[1].left, activeTop, rails[1].right, activeTop + 42 };

    // Hover washes for inactive items.
    if (m_navProfilesHover.value > 0.01f && m_section != Section::GameProfiles) {
        UITheme::DrawRoundedCard(
            r, rcGameProfilesNav, 9,
            UITheme::Mix(UITheme::BG_SIDEBAR, UITheme::BG_HOVER,
                         m_navProfilesHover.value * 0.8f),
            UITheme::Mix(UITheme::BG_SIDEBAR, UITheme::BG_HOVER,
                         m_navProfilesHover.value * 0.8f));
    }
    if (m_navMonitorHover.value > 0.01f && m_section != Section::Monitor) {
        UITheme::DrawRoundedCard(
            r, rcMonitorNav, 9,
            UITheme::Mix(UITheme::BG_SIDEBAR, UITheme::BG_HOVER,
                         m_navMonitorHover.value * 0.8f),
            UITheme::Mix(UITheme::BG_SIDEBAR, UITheme::BG_HOVER,
                         m_navMonitorHover.value * 0.8f));
    }
    if (m_navSettingsHover.value > 0.01f && m_section != Section::Settings) {
        UITheme::DrawRoundedCard(
            r, rcSettingsNav, 9,
            UITheme::Mix(UITheme::BG_SIDEBAR, UITheme::BG_HOVER,
                         m_navSettingsHover.value * 0.8f),
            UITheme::Mix(UITheme::BG_SIDEBAR, UITheme::BG_HOVER,
                         m_navSettingsHover.value * 0.8f));
    }

    // The active item is a raised plate with an accent bar on its edge, so
    // the current section is obvious at a glance.
    UITheme::DrawGlassCard(r, rcActive, 9, UITheme::BG_RAISED, 0.9f, 0.45f, 13.0f, 4.0f);
    RECT rcNavAccent = { rcActive.left + 3, rcActive.top + 13, rcActive.left + 6,
                         rcActive.bottom - 13 };
    UITheme::DrawRoundedCard(r, rcNavAccent, 2, UITheme::INVERSE, UITheme::INVERSE);

    struct NavDef {
        Section section;
        const wchar_t* label;
        wchar_t glyph;
        float hover;
    };
    const NavDef navs[] = {
        { Section::Monitor, L"Monitor", Icons::Pc, m_navMonitorHover.value },
        { Section::GameProfiles, L"Game Profiles", Icons::Controller,
          m_navProfilesHover.value },
        { Section::Settings, L"Settings", Icons::Settings, m_navSettingsHover.value },
    };

    TextStyle navLabelStyle = Typography::Button();

    for (const NavDef& nav : navs) {
        RECT rcItem = NavItemRect(nav.section);
        const bool active = m_section == nav.section;
        RECT rcIcon = { rcItem.left + 14, rcItem.top + 13, rcItem.left + 30, rcItem.top + 29 };
        Icons::Draw(r, nav.glyph, rcIcon, 14,
                    active ? UITheme::TEXT_PRIMARY
                           : UITheme::Mix(UITheme::TEXT_SECONDARY, UITheme::TEXT_PRIMARY, nav.hover));

        const COLORREF textColor = active ? UITheme::TEXT_PRIMARY
                                          : UITheme::Mix(UITheme::TEXT_SECONDARY,
                                                         UITheme::TEXT_PRIMARY, nav.hover);
        RECT rcItemLabel = { rcItem.left + 40, rcItem.top, rcItem.right - 12, rcItem.bottom };
        r.DrawText(nav.label, navLabelStyle,
                   static_cast<float>(rcItemLabel.left), static_cast<float>(rcItemLabel.top),
                   static_cast<float>(rcItemLabel.right - rcItemLabel.left),
                   static_cast<float>(rcItemLabel.bottom - rcItemLabel.top),
                   textColor,
                   DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // ---- Footer status card ----
    const int cardBottom = rcSidebar.bottom - 44;
    RECT rcCard = { rcSidebar.left + 8, cardBottom - 144, rcSidebar.right - 8, cardBottom };
    UITheme::DrawGlassCard(r, rcCard, 12, UITheme::BG_RAISED, 0.9f, 0.45f, 15.0f, 5.0f);

    UITheme::DrawEyebrow(r, L"AUTOMATION",
                         RECT{ rcCard.left + 12, rcCard.top + 12, rcCard.right - 12,
                               rcCard.top + 24 },
                         UITheme::TEXT_TERTIARY, 8, FW_SEMIBOLD, 1.0f);

    const int dotCx = rcCard.left + 14;
    const int dotCy = rcCard.top + 38;
    UITheme::DrawStatusDot(r, dotCx, dotCy, 3, m_isPaused ? UITheme::TEXT_SECONDARY : UITheme::INVERSE,
                           m_isPaused);

    {
        TextStyle statusStyle = Typography::SmallLabel();
        statusStyle.sizeDip = 10.0f;
        r.DrawText(m_isPaused ? L"Paused" : L"Running", statusStyle,
                   static_cast<float>(dotCx + 10), static_cast<float>(rcCard.top + 28),
                   static_cast<float>(rcCard.right - 12 - (dotCx + 10)), 16.0f,
                   m_isPaused ? UITheme::TEXT_SECONDARY : UITheme::TEXT_PRIMARY,
                   DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    UITheme::DrawEyebrow(r, L"ACTIVE PROFILE",
                         RECT{ rcCard.left + 12, rcCard.top + 56, rcCard.right - 12,
                               rcCard.top + 68 },
                         UITheme::TEXT_TERTIARY, 7, FW_SEMIBOLD, 1.0f);

    r.DrawText(m_activeProfileName.c_str(), Typography::SmallMono(),
               static_cast<float>(rcCard.left + 12), static_cast<float>(rcCard.top + 68),
               static_cast<float>(rcCard.right - 12 - (rcCard.left + 12)), 14.0f,
               UITheme::TEXT_SECONDARY,
               DWRITE_TEXT_ALIGNMENT_LEADING,
               DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
               true);

    RECT rcPause = PauseButtonRect();
    TextStyle pauseStyle = Typography::SmallLabel();
    pauseStyle.sizeDip = 10.0f;
    pauseStyle.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    Controls::DrawButtonWithIcon(r, rcPause, m_isPaused ? Icons::Play : Icons::Pause,
                                 m_isPaused ? L"Resume" : L"Pause",
                                 Controls::ButtonStyle::Secondary, m_pauseHover.value,
                                 false, true, pauseStyle);

    TextStyle versionStyle = Typography::SmallMono();
    r.DrawText(L"v0.2.0", versionStyle,
               static_cast<float>(rcSidebar.left + 12),
               static_cast<float>(rcSidebar.bottom - 30),
               static_cast<float>(rcSidebar.right - rcSidebar.left - 24), 16.0f,
               UITheme::TEXT_TERTIARY,
               DWRITE_TEXT_ALIGNMENT_CENTER);
}

void MainWindow::PaintHeader(Renderer& r, const RECT& rcHeader) {
    // A graded header with a hairline under it separates the page title from
    // the content without needing a heavy rule.
    UITheme::DrawPanel(r, rcHeader, UITheme::BG_CANVAS,
                       UITheme::Darken(UITheme::BG_CANVAS, 3));
    UITheme::DrawGlassEdge(r, rcHeader, 1.0f);

    TextStyle titleStyle = Typography::PageTitle();
    const wchar_t* pageTitle = L"Settings";
    if (m_section == Section::GameProfiles) pageTitle = L"Game Profiles";
    else if (m_section == Section::Monitor) pageTitle = L"Monitor";
    r.DrawText(pageTitle, titleStyle,
               static_cast<float>(rcHeader.left + 32), static_cast<float>(rcHeader.top + 24),
               std::max(1.0f, static_cast<float>(ResetButtonRect().left - 20 - (rcHeader.left + 32))),
               28.0f,
               UITheme::TEXT_PRIMARY);

    TextStyle subStyle = Typography::Muted();
    std::wstring subtitle;
    if (m_section == Section::GameProfiles) {
        subtitle = ProfileCountText(m_profileManager.GetAllProfiles().size()) +
                   L"  \u00B7  Tune display output for each application";
    } else if (m_section == Section::Monitor) {
        subtitle = L"Tune the fallback profile applied to every application";
    } else {
        subtitle = L"Preferences, maintenance and build information";
    }
    r.DrawText(subtitle.c_str(), subStyle,
               static_cast<float>(rcHeader.left + 32), static_cast<float>(rcHeader.top + 56),
               std::max(1.0f, static_cast<float>(ResetButtonRect().left - 20 - (rcHeader.left + 32))),
               18.0f,
               UITheme::TEXT_TERTIARY,
               DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
               true);

    // Reset display (ghost, with icon)
    RECT rcReset = ResetButtonRect();
    TextStyle ghostStyle = Typography::SmallLabel();
    ghostStyle.sizeDip = 11.0f;
    ghostStyle.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    Controls::DrawButtonWithIcon(r, rcReset, Icons::Reset, L"Reset display",
                                 Controls::ButtonStyle::Ghost, m_resetHover.value,
                                 m_resetPressed, true, ghostStyle);

    // Switching pill
    RECT rcPill = HeaderStatusRect();
    const float pillHover = std::clamp(m_pillHover.value, 0.0f, 1.0f);
    UITheme::DrawGlassCard(r, rcPill, rcPill.bottom - rcPill.top,
                           UITheme::Mix(UITheme::BG_SURFACE, UITheme::BG_RAISED, pillHover),
                           0.85f + pillHover * 0.15f,
                           0.30f + pillHover * 0.25f, 10.0f, 3.0f);

    const bool running = !m_isPaused;
    const int dotCx = rcPill.left + 18;
    const int dotCy = (rcPill.top + rcPill.bottom) / 2;
    UITheme::DrawStatusDot(r, dotCx, dotCy, 4,
                           running ? UITheme::INVERSE : UITheme::TEXT_SECONDARY, !running);

    {
        TextStyle pillStyle = Typography::SmallLabel();
        pillStyle.sizeDip = 11.0f;
        pillStyle.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        r.DrawText(running ? L"Switching on" : L"Paused", pillStyle,
                   static_cast<float>(dotCx + 13), static_cast<float>(rcPill.top),
                   static_cast<float>(rcPill.right - 14 - (dotCx + 13)),
                   static_cast<float>(rcPill.bottom - rcPill.top),
                   running ? UITheme::TEXT_PRIMARY : UITheme::TEXT_SECONDARY,
                   DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    RECT rcDivider = { rcHeader.left, rcHeader.bottom - 1, rcHeader.right, rcHeader.bottom };
    UITheme::FillSolidRect(r, rcDivider, UITheme::DIVIDER);
}

void MainWindow::PaintTertiaryRail(Renderer& r, const RECT& rcRail) {
    UITheme::FillSolidRect(r, rcRail, UITheme::BG_SIDEBAR);

    RECT rcEdge = { rcRail.right - 1, rcRail.top, rcRail.right, rcRail.bottom };
    UITheme::FillSolidRect(r, rcEdge, UITheme::BORDER);

    UITheme::DrawEyebrow(r, L"PROFILE SETTINGS",
                         RECT{ rcRail.left + 12, rcRail.top + 24, rcRail.right - 12,
                               rcRail.top + 38 },
                         UITheme::TEXT_TERTIARY, 9, FW_SEMIBOLD, 1.0f);

    constexpr int TAB_COUNT = 5;
    m_tertiaryTabIndicator.To(static_cast<float>(static_cast<int>(m_editorView.GetActiveTab())));
    RECT items[TAB_COUNT];
    for (int i = 0; i < TAB_COUNT; ++i) {
        items[i] = TertiaryRailItemRect(i);
    }
    const float raw = m_tertiaryTabIndicator.value;
    const int fromIdx = std::clamp(static_cast<int>(std::floor(raw)), 0, TAB_COUNT - 1);
    const int toIdx = std::min(fromIdx + 1, TAB_COUNT - 1);
    const float local = raw - static_cast<float>(fromIdx);
    const int activeTop = AnimLerpInt(items[fromIdx].top, items[toIdx].top, local);
    RECT rcActive = { items[1].left, activeTop, items[1].right, activeTop + 48 };

    const int activeIndex = static_cast<int>(m_editorView.GetActiveTab());
    for (int i = 0; i < TAB_COUNT; ++i) {
        if (m_tertiaryTabHover[i].value > 0.01f && activeIndex != i) {
            const COLORREF hoverColor =
                UITheme::Mix(UITheme::BG_SIDEBAR, UITheme::BG_HOVER,
                             m_tertiaryTabHover[i].value * 0.8f);
            UITheme::DrawRoundedCard(r, items[i], 9, hoverColor, hoverColor);
        }
    }

    UITheme::DrawGlassCard(r, rcActive, 9, UITheme::BG_RAISED, 0.85f, 0.40f, 11.0f, 3.0f);
    RECT rcTabAccent = { rcActive.left + 1, rcActive.top + 13, rcActive.left + 4,
                         rcActive.bottom - 13 };
    UITheme::DrawRoundedCard(r, rcTabAccent, 2, UITheme::INVERSE, UITheme::INVERSE);

    struct TabDef {
        int index;
        const wchar_t* label;
        wchar_t glyph;
    };
    const TabDef tabs[] = {
        { 0, L"Display", Icons::Monitor },
        { 1, L"RGB Gamma", Icons::Settings },
        { 2, L"Color", Icons::Color },
        { 3, L"Shortcut", Icons::Keyboard },
        { 4, L"Resolution", Icons::FullScreen },
    };

    TextStyle navLabelStyle = Typography::Button();

    for (const TabDef& tab : tabs) {
        RECT rcItem = items[tab.index];
        const bool active = activeIndex == tab.index;
        const float hover = m_tertiaryTabHover[tab.index].value;
        RECT rcIcon = { rcItem.left + 14, rcItem.top + 14, rcItem.left + 30, rcItem.top + 30 };
        Icons::Draw(r, tab.glyph, rcIcon, 14,
                    active ? UITheme::TEXT_PRIMARY
                           : UITheme::Mix(UITheme::TEXT_SECONDARY, UITheme::TEXT_PRIMARY, hover));

        const COLORREF textColor = active ? UITheme::TEXT_PRIMARY
                                          : UITheme::Mix(UITheme::TEXT_SECONDARY,
                                                         UITheme::TEXT_PRIMARY, hover);
        RECT rcItemLabel = { rcItem.left + 40, rcItem.top, rcItem.right - 12, rcItem.bottom };
        r.DrawText(tab.label, navLabelStyle,
                   static_cast<float>(rcItemLabel.left), static_cast<float>(rcItemLabel.top),
                   static_cast<float>(rcItemLabel.right - rcItemLabel.left),
                   static_cast<float>(rcItemLabel.bottom - rcItemLabel.top),
                   textColor,
                   DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

void MainWindow::PaintEmptyDetail(Renderer& r, const RECT& rcDetail) {
    UITheme::FillSolidRect(r, rcDetail, UITheme::BG_CANVAS);

    // A soft spotlight behind the card keeps the detail pane from reading as
    // an empty void when nothing is selected. The gradient fades to fully
    // transparent so it leaves no visible edge.
    RECT card = EmptyCardRect();
    const float spotCx = (static_cast<float>(card.left) +
                          static_cast<float>(card.right)) * 0.5f;
    const float spotCy = (static_cast<float>(card.top) +
                          static_cast<float>(card.bottom)) * 0.5f - 40.0f;
    // A faint spotlight behind the empty card. Warm on paper, a whisper of
    // light on the dark theme.
    if (UITheme::IsDark()) {
        r.FillEllipseRadialGradient(spotCx, spotCy, 420.0f, 330.0f, spotCx, spotCy,
                                    Color(1.0f, 1.0f, 1.0f, 0.06f),
                                    Color(1.0f, 1.0f, 1.0f, 0.0f));
    } else {
        r.FillEllipseRadialGradient(spotCx, spotCy, 420.0f, 330.0f, spotCx, spotCy,
                                    Color(0.96f, 0.92f, 0.86f, 0.5f),
                                    Color(0.96f, 0.92f, 0.86f, 0.0f));
    }
    UITheme::DrawGlassCard(r, card, 16, UITheme::BG_SURFACE, 0.9f);

    const int cx = (card.left + card.right) / 2;

    RECT rcCircle = { cx - 28, card.top + 34, cx + 28, card.top + 90 };
    // The icon sits on a raised plate so the empty state has a subject.
    r.DrawShadowEllipse(static_cast<float>(cx), static_cast<float>(card.top + 62),
                         28.0f, 28.0f, 8.0f, 3.0f,
                         UITheme::ShadowColor(0.14f));
    r.FillEllipseRadialGradient(static_cast<float>(cx), static_cast<float>(card.top + 62),
                                28.0f, 28.0f, static_cast<float>(cx - 9.0f),
                                static_cast<float>(card.top + 53.0f),
                                Color(UITheme::Lighten(UITheme::BG_RAISED, 4)),
                                Color(UITheme::Darken(UITheme::BG_RAISED, 4)));
    r.DrawRoundedRectOutline(
        D2D1::RectF(static_cast<float>(rcCircle.left), static_cast<float>(rcCircle.top),
                    static_cast<float>(rcCircle.right), static_cast<float>(rcCircle.bottom)),
        28.0f, 28.0f, UITheme::BORDER, 1.0f);
    Icons::Draw(r, Icons::Profiles, rcCircle, 20, UITheme::ACCENT);

    {
        TextStyle titleStyle = Typography::CardHeading();
        titleStyle.sizeDip = 18.0f;
        titleStyle.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        r.DrawText(L"Nothing selected", titleStyle,
                   static_cast<float>(card.left + 32), static_cast<float>(card.top + 108),
                   static_cast<float>(card.right - card.left - 64), 26.0f,
                   UITheme::TEXT_PRIMARY,
                   DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    {
        r.DrawText(L"Choose a profile from the library, or create a new one.",
                   Typography::Muted(),
                   static_cast<float>(card.left + 40), static_cast<float>(card.top + 138),
                   static_cast<float>(card.right - card.left - 80), 20.0f,
                   UITheme::TEXT_TERTIARY,
                   DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    RECT rcCta = EmptyCtaRect();
    Controls::DrawButtonWithIcon(r, rcCta, Icons::Add, L"New profile",
                                 Controls::ButtonStyle::Primary, m_emptyCtaHover.value,
                                 m_emptyCtaPressed, true, Typography::Button());

    TextStyle hintStyle = Typography::SmallMono();
    r.DrawText(L"CTRL N", hintStyle,
               static_cast<float>(card.left + 32), static_cast<float>(rcCta.bottom + 10),
               static_cast<float>(card.right - card.left - 64), 16.0f,
               UITheme::TEXT_TERTIARY,
               DWRITE_TEXT_ALIGNMENT_CENTER);
}

void MainWindow::PaintToast(Renderer& r) {
    if (m_toastTitle.empty()) return;
    const float eased = m_toastAnim.Eased();
    if (eased <= 0.01f) return;

    D2D1_SIZE_F size = r.GetTarget()->GetSize();
    const int w = 320;
    const int h = 68;
    const int x = static_cast<int>(size.width) - 24 - w;
    const int y = static_cast<int>(size.height) - 24 - h +
                  static_cast<int>((1.0f - eased) * 14.0f);
    RECT rcToast = { x, y, x + w, y + h };

    UITheme::DrawGlassCard(r, rcToast, 12, UITheme::BG_RAISED, 1.0f, 0.50f, 18.0f, 6.0f);

    RECT rcIcon = { rcToast.left + 16, rcToast.top + 19, rcToast.left + 46,
                    rcToast.top + 49 };
    UITheme::DrawRoundedCard(r, rcIcon, 30, UITheme::INVERSE, UITheme::INVERSE);
    Icons::Draw(r, Icons::Check, rcIcon, 13, UITheme::INVERSE_TEXT);

    r.DrawText(m_toastTitle.c_str(), Typography::Button(),
               static_cast<float>(rcIcon.right + 12), static_cast<float>(rcToast.top + 14),
               static_cast<float>(rcToast.right - 16 - (rcIcon.right + 12)), 20.0f,
               UITheme::TEXT_PRIMARY);

    r.DrawText(m_toastBody.c_str(), Typography::Muted(),
               static_cast<float>(rcIcon.right + 12), static_cast<float>(rcToast.top + 35),
               static_cast<float>(rcToast.right - 16 - (rcIcon.right + 12)), 20.0f,
               UITheme::TEXT_TERTIARY);
}

void MainWindow::PaintScreenshotToast(Renderer& r) {
    constexpr int w = 336;
    constexpr int h = 154;
    const RECT rcToast = { 0, 0, w, h };
    const COLORREF accent = m_screenshotToastIsError ? UITheme::DANGER : UITheme::SUCCESS;
    const COLORREF iconBg = m_screenshotToastIsError ? UITheme::DANGER_SOFT : UITheme::SUCCESS_SOFT;

    RECT rcCard = rcToast;
    if (!m_screenshotToastPreview) {
        rcCard.bottom = 70;
    }
    UITheme::DrawGlassCard(r, rcCard, 12, UITheme::BG_RAISED, 1.05f, 0.55f, 20.0f, 7.0f);

    const RECT rcAccent = { rcToast.left, rcToast.top + 13, rcToast.left + 3, 57 };
    UITheme::FillSolidRect(r, rcAccent, accent);

    const RECT rcIcon = { rcToast.left + 17, rcToast.top + 19,
                          rcToast.left + 49, rcToast.top + 51 };
    UITheme::DrawRoundedCard(r, rcIcon, 16, iconBg, iconBg);
    Icons::Draw(r, m_screenshotToastIsError ? Icons::Close : Icons::Check,
                rcIcon, 14, accent);

    const std::wstring title = m_screenshotToastIsError ? L"Screenshot failed" : L"Screenshot saved";
    std::wstring body = L"Please try again.";
    if (!m_screenshotToastIsError) {
        if (m_preferences.screenshotClickAction == ScreenshotClickAction::OpenScreenshotsFolder) {
            body = m_screenshotToastImageCopied
                ? L"Copied - click to open folder."
                : L"Click to open screenshots folder.";
        } else if (m_preferences.screenshotClickAction == ScreenshotClickAction::CopyImage) {
            body = m_screenshotToastImageCopied
                ? L"Copied to clipboard."
                : L"Click to copy image.";
        } else {
            body = m_screenshotToastImageCopied
                ? L"Copied - click to open image."
                : L"Click to open image.";
        }
    }
    r.DrawText(title, Typography::Button(),
               static_cast<float>(rcIcon.right + 12), static_cast<float>(rcToast.top + 14),
               static_cast<float>(rcToast.right - rcIcon.right - 24), 20.0f,
               UITheme::TEXT_PRIMARY);
    r.DrawText(body, Typography::Muted(),
               static_cast<float>(rcIcon.right + 12), static_cast<float>(rcToast.top + 36),
               static_cast<float>(rcToast.right - rcIcon.right - 24), 18.0f,
               UITheme::TEXT_TERTIARY);

    if (m_screenshotToastPreview) {
        UITheme::DrawHLine(r, 18, w - 18, 75, UITheme::DIVIDER);
        const RECT rcPreview = { 18, 86, 142, 146 };
        UITheme::DrawRoundedCard(r, rcPreview, 8, UITheme::BG_INPUT, UITheme::BORDER);

        const D2D1_SIZE_F bitmapSize = m_screenshotToastPreview->GetSize();
        const float maxWidth = static_cast<float>(rcPreview.right - rcPreview.left - 10);
        const float maxHeight = static_cast<float>(rcPreview.bottom - rcPreview.top - 8);
        const float scale = std::min(maxWidth / bitmapSize.width,
                                     maxHeight / bitmapSize.height);
        const float previewWidth = bitmapSize.width * scale;
        const float previewHeight = bitmapSize.height * scale;
        const float previewX = (static_cast<float>(rcPreview.left + rcPreview.right) -
                                previewWidth) * 0.5f;
        const float previewY = (static_cast<float>(rcPreview.top + rcPreview.bottom) -
                                previewHeight) * 0.5f;
        r.DrawBitmapRounded(
            m_screenshotToastPreview.Get(),
            D2D1::RectF(previewX, previewY, previewX + previewWidth,
                        previewY + previewHeight),
            4.0f);

        TextStyle previewLabel = Typography::Eyebrow();
        previewLabel.sizeDip = 9.0f;
        r.DrawText(L"SCREENSHOT PREVIEW", previewLabel, 158.0f, 99.0f, 160.0f, 16.0f,
                   UITheme::TEXT_TERTIARY);
        const wchar_t* previewHint = m_screenshotToastImageCopied
            ? L"Ready to paste or open."
            : L"Click the notification to continue.";
        r.DrawText(previewHint, Typography::Muted(), 158.0f, 119.0f, 160.0f, 18.0f,
                   UITheme::TEXT_SECONDARY, DWRITE_TEXT_ALIGNMENT_LEADING,
                   DWRITE_PARAGRAPH_ALIGNMENT_NEAR, true);
    }
}

void MainWindow::PaintGameProfiles(Renderer& r, const RECT& /*rcBody*/) {
    RECT rcList = ListPaneRect();
    RECT rcDetail = DetailPaneRect();

    auto profiles = m_profileManager.SearchProfiles(m_listView.GetSearchQuery());
    m_listView.Paint(r, rcList, profiles);

    // Paint tertiary railer only when a profile is selected in editor mode
    if (m_detailMode == DetailMode::Editor) {
        PaintTertiaryRail(r, TertiaryRailRect());
    }

    switch (m_detailMode) {
        case DetailMode::Editor:
            m_editorView.Paint(r, rcDetail);
            break;
        case DetailMode::AddApp:
            UITheme::FillSolidRect(r, rcDetail, UITheme::BG_CANVAS);
            m_addDialog.Paint(r, AddFormRect());
            break;
        case DetailMode::Empty:
        default:
            PaintEmptyDetail(r, rcDetail);
            break;
    }
}

void MainWindow::PaintMonitor(Renderer& r, const RECT& /*rcBody*/) {
    PaintTertiaryRail(r, ProfileTabRailRect());
    m_editorView.Paint(r, MonitorEditorRect());
}

void MainWindow::PaintSettings(Renderer& r, const RECT& rcBody) {
    m_settingsView.Paint(r, rcBody);
}

void MainWindow::Paint(Renderer& r) {
    D2D1_SIZE_F size = r.GetTarget()->GetSize();
    RECT rcClient = { 0, 0, static_cast<LONG>(std::floor(size.width)),
                      static_cast<LONG>(std::floor(size.height)) };
    UITheme::FillSolidRect(r, rcClient, UITheme::BG_CANVAS);

    PaintSidebar(r, SidebarRect());
    PaintHeader(r, HeaderRect());

    switch (m_section) {
        case Section::GameProfiles:
            PaintGameProfiles(r, BodyRect());
            break;
        case Section::Monitor:
            PaintMonitor(r, BodyRect());
            break;
        case Section::Settings:
        default:
            PaintSettings(r, BodyRect());
            break;
    }

    PaintToast(r);
    PaintUpdateDialog(r);
}

// ---- Animation timer ----

void MainWindow::StartAnimationTimer() {
    if (!m_hwnd || m_animTimerActive) return;
    m_animTimerActive = true;
    m_lastAnimTick = GetTickCount64();
    SetTimer(m_hwnd, TIMER_ANIMATION, ANIMATION_INTERVAL_MS, nullptr);
}

void MainWindow::StopAnimationTimer() {
    if (m_hwnd && m_animTimerActive) {
        KillTimer(m_hwnd, TIMER_ANIMATION);
        m_animTimerActive = false;
    }
}

bool MainWindow::HasActiveAnimations() const {
    if (!m_navSlide.Settled() || !m_navMonitorHover.Settled() ||
        !m_navProfilesHover.Settled() || !m_navSettingsHover.Settled() ||
        !m_pauseHover.Settled() || !m_pillHover.Settled() || !m_resetHover.Settled() ||
        !m_emptyCtaHover.Settled() || !m_toastAnim.Settled()) {
        return true;
    }
    if (!m_tertiaryTabIndicator.Settled()) return true;
    for (int i = 0; i < 5; ++i) {
        if (!m_tertiaryTabHover[i].Settled()) return true;
    }
    if (m_section == Section::GameProfiles) {
        if (m_listView.HasActiveAnimations()) return true;
        if (m_detailMode == DetailMode::Editor && m_editorView.HasActiveAnimations()) return true;
        if (m_detailMode == DetailMode::AddApp && m_addDialog.HasActiveAnimations()) return true;
    } else if (m_section == Section::Monitor) {
        if (m_editorView.HasActiveAnimations()) return true;
    } else if (m_settingsView.HasActiveAnimations()) {
        return true;
    }
    return false;
}

void MainWindow::TickAnimations() {
    const ULONGLONG now = GetTickCount64();
    float dt = 0.016f;
    if (m_lastAnimTick != 0) {
        dt = std::clamp(static_cast<float>(now - m_lastAnimTick) / 1000.0f, 0.001f, 0.05f);
    }
    m_lastAnimTick = now;

    m_navSlide.Advance(dt);
    m_navMonitorHover.Advance(dt);
    m_navProfilesHover.Advance(dt);
    m_navSettingsHover.Advance(dt);
    m_pauseHover.Advance(dt);
    m_pillHover.Advance(dt);
    m_resetHover.Advance(dt);
    m_emptyCtaHover.Advance(dt);
    m_tertiaryTabIndicator.Advance(dt);
    for (int i = 0; i < 5; ++i) {
        m_tertiaryTabHover[i].Advance(dt);
    }
    m_toastAnim.Advance(dt);

    if (m_section == Section::GameProfiles) {
        m_listView.TickAnimations(dt);
        if (m_detailMode == DetailMode::Editor) m_editorView.TickAnimations(dt);
        if (m_detailMode == DetailMode::AddApp) m_addDialog.TickAnimations(dt);
    } else if (m_section == Section::Monitor) {
        m_editorView.TickAnimations(dt);
    } else {
        m_settingsView.TickAnimations(dt);
    }

    if (m_toastAnim.Settled() && m_toastAnim.target <= 0.0f && !m_toastTitle.empty()) {
        m_toastTitle.clear();
        m_toastBody.clear();
        m_toastExpire = 0;
    }
    if (m_screenshotToastAnim.Settled() && m_screenshotToastAnim.target <= 0.0f) {
        m_screenshotToastIsError = false;
    }

    InvalidateRect(m_hwnd, nullptr, FALSE);
    if (!HasActiveAnimations()) StopAnimationTimer();
}

// ---- Context menu ----

void MainWindow::ShowProfileContextMenu(POINT clientPoint) {
    auto profiles = m_profileManager.SearchProfiles(m_listView.GetSearchQuery());
    std::wstring profileId = m_listView.GetProfileIdAt(clientPoint, ListPaneRect(), profiles);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, IDM_PROFILE_IMPORT, L"Import Profile...");
    if (!profileId.empty()) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_PROFILE_DUPLICATE, L"Duplicate Profile");
        AppendMenuW(menu, MF_STRING, IDM_PROFILE_EXPORT, L"Export Profile...");
        const auto* profile = m_profileManager.GetProfileById(profileId);
        if (profile && !profile->isDefault) {
            AppendMenuW(menu, MF_STRING, IDM_PROFILE_DELETE, L"Delete Profile");
        }
    }

    POINT screenPoint = ToPhysical(clientPoint);
    ClientToScreen(m_hwnd, &screenPoint);
    SetForegroundWindow(m_hwnd);
    UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                  screenPoint.x, screenPoint.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);

    if (command == IDM_PROFILE_IMPORT) {
        wchar_t path[MAX_PATH]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = m_hwnd;
        dialog.lpstrFile = path;
        dialog.nMaxFile = MAX_PATH;
        dialog.lpstrFilter = L"Lumetra Profile (*.json)\0*.json\0All Files (*.*)\0*.*\0";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&dialog)) {
            if (auto imported = ProfileStorage::ImportProfile(path)) {
                std::wstring importedId = imported->id;
                if (m_profileManager.AddProfile(std::move(*imported))) {
                    SelectProfile(importedId);
                } else {
                    Redraw();
                }
            }
        }
    } else if (command == IDM_PROFILE_DUPLICATE && !profileId.empty()) {
        std::wstring newId;
        if (m_profileManager.DuplicateProfile(profileId, newId)) {
            SelectProfile(newId);
        }
    } else if (command == IDM_PROFILE_DELETE && !profileId.empty()) {
        if (m_profileManager.DeleteProfile(profileId)) {
            if (const auto* active = m_displayManager.GetActiveProfile(); active && active->id == profileId) {
                m_displayManager.RestoreDefault();
            }
            if (m_selectedProfileId == profileId) {
                m_selectedProfileId.clear();
                m_listView.SetSelectedId(L"");
                m_detailMode = DetailMode::Empty;
            }
            Redraw();
        }
    } else if (command == IDM_PROFILE_EXPORT && !profileId.empty()) {
        const auto* profile = m_profileManager.GetProfileById(profileId);
        if (profile) {
            wchar_t path[MAX_PATH] = L"profile.json";
            OPENFILENAMEW dialog{};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = m_hwnd;
            dialog.lpstrFile = path;
            dialog.nMaxFile = MAX_PATH;
            dialog.lpstrFilter = L"Lumetra Profile (*.json)\0*.json\0";
            dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetSaveFileNameW(&dialog)) ProfileStorage::ExportProfile(*profile, path);
        }
    }
}

// ---- Window procedure ----

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (s_instance) {
        if (uMsg == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            if (create->lpCreateParams == &s_instance->m_screenshotToastHwnd) {
                s_instance->m_screenshotToastHwnd = hwnd;
            } else if (create->lpCreateParams == &s_instance->m_resolutionToastHwnd) {
                s_instance->m_resolutionToastHwnd = hwnd;
            }
        }

        const bool isScreenshotToast = hwnd == s_instance->m_screenshotToastHwnd;
        const bool isResolutionToast = hwnd == s_instance->m_resolutionToastHwnd;
        const LRESULT result = s_instance->HandleMessage(hwnd, uMsg, wParam, lParam);
        if (isScreenshotToast && uMsg == WM_NCDESTROY) {
            s_instance->m_screenshotToastHwnd = nullptr;
        } else if (isResolutionToast && uMsg == WM_NCDESTROY) {
            s_instance->m_resolutionToastHwnd = nullptr;
        }
        return result;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (!m_hwnd) {
        m_hwnd = hwnd;
    }

    if (hwnd == m_hwnd && uMsg == UPDATE_JOB_COMPLETE) {
        std::unique_ptr<UpdateJobResult> result(
            reinterpret_cast<UpdateJobResult*>(lParam));
        if (m_updateThread.joinable()) m_updateThread.join();
        m_updateJobRunning.store(false);
        if (result) HandleUpdateJobResult(*result);
        return 0;
    }
    if (hwnd == m_hwnd && uMsg == UPDATE_JOB_FAILED) {
        m_updateJobRunning.store(false);
        const std::wstring message = L"Could not allocate the update result.";
        SetUpdateStatus(message);
        Logger::Error(message);
        if (wParam != 0) ShowToast(L"Update unavailable", message);
        return 0;
    }

    if (hwnd == m_resolutionToastHwnd) {
        if (uMsg == WM_PAINT) {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd, &ps);
            m_resolutionToastRenderer.BeginDraw();
            PaintResolutionToast(m_resolutionToastRenderer);
            const HRESULT hr = m_resolutionToastRenderer.EndDraw();
            if (hr == D2DERR_RECREATE_TARGET &&
                !m_resolutionToastRenderer.RecreateTarget()) {
                Logger::Error(L"Failed to recreate resolution popup render target.");
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        if (uMsg == WM_DPICHANGED) {
            // The popup followed its monitor to a different scaling. Without
            // this the buttons would be drawn at one scale and hit-tested at
            // another, and clicking them would do nothing.
            const UINT newDpi = HIWORD(wParam);
            RECT suggested{};
            GetWindowRect(hwnd, &suggested);
            m_resolutionToastRenderer.OnDpiChanged(newDpi, &suggested);
            ApplyResolutionToastRegion();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (uMsg == WM_DISPLAYCHANGE) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (uMsg == WM_MOUSEMOVE) {
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hwnd;
            TrackMouseEvent(&track);
            const UINT dpi = m_resolutionToastRenderer.GetDpi();
            const POINT pt{ MulDiv(GET_X_LPARAM(lParam), 96, static_cast<int>(dpi)),
                            MulDiv(GET_Y_LPARAM(lParam), 96, static_cast<int>(dpi)) };
            UpdateResolutionToastHover(pt);
            return 0;
        }
        if (uMsg == WM_MOUSELEAVE) {
            m_resolutionToastHoverApply = false;
            m_resolutionToastHoverDismiss = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (uMsg == WM_LBUTTONUP) {
            const UINT dpi = m_resolutionToastRenderer.GetDpi();
            const POINT pt{ MulDiv(GET_X_LPARAM(lParam), 96, static_cast<int>(dpi)),
                            MulDiv(GET_Y_LPARAM(lParam), 96, static_cast<int>(dpi)) };
            if (PtInRect(&RESOLUTION_TOAST_DISMISS_RECT, pt)) {
                if (m_onResolutionDismiss) m_onResolutionDismiss();
                DismissResolutionPrompt();
            } else if (PtInRect(&RESOLUTION_TOAST_APPLY_RECT, pt) &&
                       m_resolutionToastSupported) {
                if (m_onResolutionApply && m_onResolutionApply()) {
                    DismissResolutionPrompt();
                } else {
                    m_resolutionToastStatus =
                        L"The mode could not be applied. Lumetra attempted to restore the captured display mode.";
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }
        if (uMsg == WM_SETCURSOR) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        if (uMsg == WM_NCHITTEST) {
            return HTCLIENT;
        }
        if (uMsg == WM_CLOSE) {
            if (m_onResolutionDismiss) m_onResolutionDismiss();
            DismissResolutionPrompt();
            return 0;
        }
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    if (hwnd == m_screenshotToastHwnd) {
        if (uMsg == WM_PAINT) {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd, &ps);
            m_screenshotToastRenderer.BeginDraw();
            PaintScreenshotToast(m_screenshotToastRenderer);
            const HRESULT hr = m_screenshotToastRenderer.EndDraw();
            if (hr == D2DERR_RECREATE_TARGET) {
                if (!m_screenshotToastRenderer.RecreateTarget()) {
                    Logger::Error(L"Failed to recreate screenshot notification render target.");
                } else if (!m_screenshotToastImagePath.empty()) {
                    m_screenshotToastPreview = m_screenshotToastRenderer.LoadBitmapFromFile(
                        m_screenshotToastImagePath);
                }
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        if (uMsg == WM_TIMER && wParam == TIMER_SCREENSHOT_TOAST) {
            KillTimer(hwnd, TIMER_SCREENSHOT_TOAST);
            m_screenshotToastAnim.To(0.0f);
            m_lastScreenshotToastTick = GetTickCount64();
            SetTimer(hwnd, TIMER_SCREENSHOT_TOAST_ANIMATION,
                     ANIMATION_INTERVAL_MS, nullptr);
            return 0;
        }
        if (uMsg == WM_TIMER && wParam == TIMER_SCREENSHOT_TOAST_ANIMATION) {
            const ULONGLONG now = GetTickCount64();
            const float dt = std::clamp(
                static_cast<float>(now - m_lastScreenshotToastTick) / 1000.0f,
                0.001f, 0.05f);
            m_lastScreenshotToastTick = now;
            m_screenshotToastAnim.Advance(dt);

            const float eased = m_screenshotToastAnim.Eased();
            const int width = m_screenshotToastTargetRect.right -
                              m_screenshotToastTargetRect.left;
            const int height = m_screenshotToastTargetRect.bottom -
                               m_screenshotToastTargetRect.top;
            const int slideOffset = MulDiv(
                24, static_cast<int>(m_screenshotToastRenderer.GetDpi()), 96);
            const int x = m_screenshotToastTargetRect.left -
                          static_cast<int>((1.0f - eased) * slideOffset);
            SetWindowPos(hwnd, HWND_TOPMOST, x, m_screenshotToastTargetRect.top,
                         width, height, SWP_NOACTIVATE);

            if (m_screenshotToastAnim.Settled()) {
                KillTimer(hwnd, TIMER_SCREENSHOT_TOAST_ANIMATION);
                if (m_screenshotToastAnim.target <= 0.0f) {
                    ShowWindow(hwnd, SW_HIDE);
                } else {
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }
        if (uMsg == WM_LBUTTONUP && !m_screenshotToastImagePath.empty()) {
            switch (m_preferences.screenshotClickAction) {
                case ScreenshotClickAction::OpenImage: {
                    const HINSTANCE result = ShellExecuteW(
                        nullptr, L"open", m_screenshotToastImagePath.c_str(),
                        nullptr, nullptr, SW_SHOWNORMAL);
                    if (reinterpret_cast<INT_PTR>(result) <= 32) {
                        Logger::Error(L"Failed to open screenshot: " +
                                      m_screenshotToastImagePath);
                    }
                    break;
                }
                case ScreenshotClickAction::OpenScreenshotsFolder:
                    OnOpenScreenshotsFolder();
                    break;
                case ScreenshotClickAction::CopyImage:
                    if (Application::GetInstance() &&
                        Application::GetInstance()->GetScreenshotManager()
                            .CopyScreenshotToClipboard(m_screenshotToastImagePath)) {
                        m_screenshotToastImageCopied = true;
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    break;
            }
            return 0;
        }
        if (uMsg == WM_SETCURSOR && !m_screenshotToastImagePath.empty()) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        if (uMsg == WM_NCHITTEST) {
            return HTCLIENT;
        }
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    switch (uMsg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            m_renderer.BeginDraw();
            Paint(m_renderer);
            HRESULT hr = m_renderer.EndDraw();
            if (hr == D2DERR_RECREATE_TARGET) {
                // Device was lost and recreated; cached bitmaps are stale.
                m_logoBitmap.Reset();
                m_listView.InvalidateIconCache();
            }
            EndPaint(hwnd, &ps);
            if (HasActiveAnimations()) StartAnimationTimer();
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            const int dpi = static_cast<int>(m_renderer.GetDpi());
            mmi->ptMinTrackSize.x = MulDiv(MIN_WINDOW_WIDTH, dpi, 96);
            mmi->ptMinTrackSize.y = MulDiv(MIN_WINDOW_HEIGHT, dpi, 96);
            return 0;
        }

        case WM_DPICHANGED: {
            const UINT newDpi = LOWORD(wParam);
            m_renderer.OnDpiChanged(newDpi, reinterpret_cast<const RECT*>(lParam));
            Redraw();
            return 0;
        }

        case WM_SIZE: {
            if (wParam != SIZE_MINIMIZED) {
                m_renderer.Resize(LOWORD(lParam), HIWORD(lParam));
                Redraw();
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (!m_trackingMouse) {
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
                m_trackingMouse = true;
            }

            POINT pt = ToDip(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
            if (m_updateNotificationVisible) {
                const UpdateDialogLayout layout = GetUpdateDialogLayout();
                int hover = -1;
                if (m_updateShowingNotes) {
                    if (PtInRect(&layout.gotIt, pt)) hover = 3;
                } else if (PtInRect(&layout.whatsNew, pt)) {
                    hover = 0;
                } else if (PtInRect(&layout.download, pt)) {
                    hover = 1;
                } else if (PtInRect(&layout.later, pt)) {
                    hover = 2;
                }
                if (hover != m_updateDialogHover) {
                    m_updateDialogHover = hover;
                    Redraw();
                }
                return 0;
            }
            bool redraw = false;

            RECT rcGameProfilesNav = NavItemRect(Section::GameProfiles);
            RECT rcMonitorNav = NavItemRect(Section::Monitor);
            RECT rcSettingsNav = NavItemRect(Section::Settings);
            RECT rcPause = PauseButtonRect();
            RECT rcPill = HeaderStatusRect();
            RECT rcReset = ResetButtonRect();
            RECT rcEmptyCta = EmptyCtaRect();

            const bool navGameProfiles = PtInRect(&rcGameProfilesNav, pt) != FALSE;
            const bool navMonitor = PtInRect(&rcMonitorNav, pt) != FALSE;
            const bool navSettings = PtInRect(&rcSettingsNav, pt) != FALSE;
            const bool pauseHover = PtInRect(&rcPause, pt) != FALSE;
            const bool pillHover = PtInRect(&rcPill, pt) != FALSE;
            const bool resetHover = PtInRect(&rcReset, pt) != FALSE;
            const bool emptyHover = m_section == Section::GameProfiles &&
                                    m_detailMode == DetailMode::Empty &&
                                    PtInRect(&rcEmptyCta, pt) != FALSE;

            // Tertiary railer hover
            int prevTertiaryTab = m_hoverTertiaryTab;
            m_hoverTertiaryTab = -1;
            if (m_section == Section::Monitor ||
                (m_section == Section::GameProfiles &&
                 m_detailMode == DetailMode::Editor)) {
                for (int i = 0; i < 5; ++i) {
                    RECT rcTab = TertiaryRailItemRect(i);
                    if (PtInRect(&rcTab, pt)) {
                        m_hoverTertiaryTab = i;
                        break;
                    }
                }
            }

            if (navGameProfiles != m_hoverNavProfiles ||
                navMonitor != m_hoverNavMonitor || navSettings != m_hoverNavSettings ||
                pauseHover != m_hoverPauseButton || pillHover != m_hoverHeaderPill ||
                resetHover != m_hoverResetButton || emptyHover != m_hoverEmptyCta ||
                prevTertiaryTab != m_hoverTertiaryTab) {
                m_hoverNavProfiles = navGameProfiles;
                m_hoverNavMonitor = navMonitor;
                m_hoverNavSettings = navSettings;
                m_hoverPauseButton = pauseHover;
                m_hoverHeaderPill = pillHover;
                m_hoverResetButton = resetHover;
                m_hoverEmptyCta = emptyHover;
                redraw = true;
            }

            m_navProfilesHover.Towards(m_hoverNavProfiles);
            m_navMonitorHover.Towards(m_hoverNavMonitor);
            m_navSettingsHover.Towards(m_hoverNavSettings);
            m_pauseHover.Towards(m_hoverPauseButton);
            m_pillHover.Towards(m_hoverHeaderPill);
            m_resetHover.Towards(m_hoverResetButton);
            m_emptyCtaHover.Towards(m_hoverEmptyCta);
            for (int i = 0; i < 5; ++i) {
                m_tertiaryTabHover[i].Towards(m_hoverTertiaryTab == i);
            }

            if (m_section == Section::GameProfiles) {
                auto profiles = m_profileManager.SearchProfiles(m_listView.GetSearchQuery());
                if (m_listView.OnMouseMove(pt, ListPaneRect(), profiles)) redraw = true;
                if (m_detailMode == DetailMode::Editor && m_editorView.OnMouseMove(pt, DetailPaneRect())) {
                    redraw = true;
                }
                if (m_detailMode == DetailMode::AddApp && m_addDialog.OnMouseMove(pt, AddFormRect())) {
                    redraw = true;
                }
            } else if (m_section == Section::Monitor) {
                if (m_editorView.OnMouseMove(pt, MonitorEditorRect())) redraw = true;
            } else {
                if (m_settingsView.OnMouseMove(pt, BodyRect())) redraw = true;
            }

            if (redraw) Redraw();
            return 0;
        }

        case WM_MOUSELEAVE: {
            m_trackingMouse = false;
            m_hoverNavProfiles = false;
            m_hoverNavMonitor = false;
            m_hoverNavSettings = false;
            m_hoverPauseButton = false;
            m_hoverHeaderPill = false;
            m_hoverResetButton = false;
            m_hoverEmptyCta = false;
            m_hoverTertiaryTab = -1;
            m_navProfilesHover.To(0.0f);
            m_navMonitorHover.To(0.0f);
            m_navSettingsHover.To(0.0f);
            m_pauseHover.To(0.0f);
            m_pillHover.To(0.0f);
            m_resetHover.To(0.0f);
            m_emptyCtaHover.To(0.0f);
            for (int i = 0; i < 5; ++i) {
                m_tertiaryTabHover[i].To(0.0f);
            }
            m_listView.ResetHover();
            m_editorView.ResetHover();
            m_settingsView.ResetHover();
            Redraw();
            return 0;
        }

        case WM_LBUTTONDOWN: {
            POINT pt = ToDip(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
            SetCapture(hwnd);
            if (m_updateNotificationVisible) {
                const UpdateDialogLayout layout = GetUpdateDialogLayout();
                if (m_updateShowingNotes) {
                    if (PtInRect(&layout.gotIt, pt)) {
                        m_preferences.lastShownReleaseNotesVersion =
                            m_updateNotesVersion;
                        if (m_updateNotesVersion == LUMETRA_VERSION) {
                            m_preferences.lastRunVersion = LUMETRA_VERSION;
                        }
                        SavePreferences();
                        m_updateNotificationVisible = false;
                        m_updateShowingNotes = false;
                        m_updateDialogHover = -1;
                        MaybeStartUpdateWork();
                    }
                    return 0;
                }
                if (PtInRect(&layout.whatsNew, pt)) {
                    m_updateShowingNotes = true;
                    m_updateNotesScroll = 0;
                } else if (PtInRect(&layout.download, pt)) {
                    m_updateNotificationVisible = false;
                    m_updateNoticeDismissed = true;
                    StartUpdateDownload();
                } else if (PtInRect(&layout.later, pt)) {
                    m_updateNotificationVisible = false;
                    m_updateNoticeDismissed = true;
                }
                m_updateDialogHover = -1;
                Redraw();
                return 0;
            }

            RECT rcGameProfilesNav = NavItemRect(Section::GameProfiles);
            RECT rcMonitorNav = NavItemRect(Section::Monitor);
            RECT rcSettingsNav = NavItemRect(Section::Settings);
            if (PtInRect(&rcGameProfilesNav, pt)) {
                SelectSection(Section::GameProfiles);
                return 0;
            }
            if (PtInRect(&rcMonitorNav, pt)) {
                SelectSection(Section::Monitor);
                return 0;
            }
            if (PtInRect(&rcSettingsNav, pt)) {
                SelectSection(Section::Settings);
                return 0;
            }

            RECT rcPause = PauseButtonRect();
            RECT rcPill = HeaderStatusRect();
            if (PtInRect(&rcPause, pt) || PtInRect(&rcPill, pt)) {
                TogglePause();
                return 0;
            }

            RECT rcReset = ResetButtonRect();
            if (PtInRect(&rcReset, pt)) {
                m_resetPressed = true;
                ResetDisplay();
                return 0;
            }

            if (m_section == Section::GameProfiles && m_detailMode == DetailMode::Empty) {
                RECT rcEmptyCta = EmptyCtaRect();
                if (PtInRect(&rcEmptyCta, pt)) {
                    m_emptyCtaPressed = true;
                    BeginAddApp();
                    return 0;
                }
            }

            // Tertiary railer tab clicks
            if (m_section == Section::Monitor ||
                (m_section == Section::GameProfiles &&
                 m_detailMode == DetailMode::Editor)) {
                for (int i = 0; i < 5; ++i) {
                    RECT rcTab = TertiaryRailItemRect(i);
                    if (PtInRect(&rcTab, pt)) {
                        // Switch to the corresponding tab in ProfileEditorView
                        ProfileEditorView::EditorTab newTab = static_cast<ProfileEditorView::EditorTab>(i);
                        m_editorView.SetTab(newTab);
                        Redraw();
                        return 0;
                    }
                }
            }

            bool redraw = false;
            if (m_section == Section::GameProfiles) {
                RECT rcList = ListPaneRect();
                if (PtInRect(&rcList, pt)) {
                    auto profiles = m_profileManager.SearchProfiles(m_listView.GetSearchQuery());
                    redraw = m_listView.OnLButtonDown(pt, rcList, profiles);
                } else if (m_detailMode == DetailMode::Editor) {
                    redraw = m_editorView.OnLButtonDown(pt, DetailPaneRect());
                } else if (m_detailMode == DetailMode::AddApp) {
                    redraw = m_addDialog.OnLButtonDown(hwnd, pt, AddFormRect());
                }
            } else if (m_section == Section::Monitor) {
                redraw = m_editorView.OnLButtonDown(pt, BodyRect());
            } else {
                redraw = m_settingsView.OnLButtonDown(pt, BodyRect());
            }

            if (redraw) Redraw();
            return 0;
        }

        case WM_LBUTTONUP: {
            ReleaseCapture();
            bool redraw = m_resetPressed || m_emptyCtaPressed;
            m_resetPressed = false;
            m_emptyCtaPressed = false;

            if ((m_section == Section::GameProfiles && m_detailMode == DetailMode::Editor) ||
                m_section == Section::Monitor) {
                POINT pt = ToDip(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
                const RECT rcEditor =
                    m_section == Section::Monitor ? MonitorEditorRect() : DetailPaneRect();
                if (m_editorView.OnLButtonUp(pt, rcEditor)) redraw = true;
            }
            if (redraw) Redraw();
            return 0;
        }

        case WM_RBUTTONUP: {
            if (m_section == Section::GameProfiles) {
                POINT pt = ToDip(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
                RECT rcList = ListPaneRect();
                if (PtInRect(&rcList, pt)) {
                    ShowProfileContextMenu(pt);
                }
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            if (m_updateNotificationVisible && m_updateShowingNotes) {
                const UpdateDialogLayout layout = GetUpdateDialogLayout();
                const int charactersPerLine =
                    std::max(32, static_cast<int>(
                                     (layout.content.right - layout.content.left) / 7));
                const int lineCount = static_cast<int>(
                    (m_updateNotesBody.size() +
                     static_cast<std::size_t>(charactersPerLine - 1)) /
                    static_cast<std::size_t>(charactersPerLine));
                const int contentHeight = lineCount * 20;
                const int maxScroll = std::max(
                    0, contentHeight - static_cast<int>(
                                           layout.content.bottom - layout.content.top));
                m_updateNotesScroll = std::clamp(
                    m_updateNotesScroll -
                        (GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA) * 48,
                    0, maxScroll);
                Redraw();
                return 0;
            }
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            pt = ToDip(pt);
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            bool redraw = false;

            if (m_section == Section::GameProfiles) {
                RECT rcList = ListPaneRect();
                if (PtInRect(&rcList, pt)) redraw = m_listView.OnMouseWheel(delta);
            } else if (m_section == Section::Settings) {
                redraw = m_settingsView.OnMouseWheel(delta, BodyRect());
            }

            if (redraw) Redraw();
            return 0;
        }

        case WM_CHAR: {
            wchar_t ch = static_cast<wchar_t>(wParam);
            if (m_section == Section::GameProfiles) {
                bool redraw = false;
                if (m_detailMode == DetailMode::AddApp) {
                    redraw = m_addDialog.OnChar(ch);
                } else {
                    redraw = m_listView.OnChar(ch);
                    if (m_detailMode == DetailMode::Editor) {
                        redraw = m_editorView.OnChar(ch) || redraw;
                    }
                }
                if (redraw) Redraw();
            } else if (m_section == Section::Monitor && m_editorView.OnChar(ch)) {
                Redraw();
            }
            return 0;
        }

        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP: {
            if (m_section == Section::Settings &&
                m_settingsView.IsListeningForScreenshotHotkey()) {
                const bool handled = uMsg == WM_SYSKEYDOWN
                                         ? m_settingsView.OnKeyDown(wParam)
                                         : m_settingsView.OnKeyUp(wParam);
                if (handled) Redraw();
                return 0;
            }
            const bool editorActive =
                m_section == Section::Monitor ||
                (m_section == Section::GameProfiles && m_detailMode == DetailMode::Editor);
            if (editorActive && m_editorView.IsListeningForHotkey()) {
                const bool handled = uMsg == WM_SYSKEYDOWN
                                         ? m_editorView.OnKeyDown(wParam)
                                         : m_editorView.OnKeyUp(wParam);
                if (handled) Redraw();
                return 0;
            }
            return DefWindowProcW(hwnd, uMsg, wParam, lParam);
        }

        case WM_KEYDOWN: {
            if (m_updateNotificationVisible && wParam == VK_ESCAPE) {
                m_updateNotificationVisible = false;
                m_updateNoticeDismissed = true;
                m_updateShowingNotes = false;
                Redraw();
                return 0;
            }
            bool handled = false;
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (m_section == Section::Settings &&
                m_settingsView.IsListeningForScreenshotHotkey()) {
                handled = m_settingsView.OnKeyDown(wParam);
                if (handled) Redraw();
                return 0;
            }
            const bool editorActive =
                m_section == Section::Monitor ||
                (m_section == Section::GameProfiles && m_detailMode == DetailMode::Editor);

            if (editorActive && m_editorView.IsListeningForHotkey()) {
                handled = m_editorView.OnKeyDown(wParam);
                if (handled) Redraw();
                return 0;
            }

            if (m_section == Section::GameProfiles && ctrl &&
                (wParam == 'N' || wParam == 'n')) {
                BeginAddApp();
                return 0;
            }
            if (ctrl && (wParam == 'S' || wParam == 's') && editorActive) {
                if (m_editorView.Save()) Redraw();
                return 0;
            }

            if (m_section == Section::GameProfiles) {
                if (m_detailMode == DetailMode::AddApp) {
                    handled = m_addDialog.OnKeyDown(wParam);
                } else if (m_detailMode == DetailMode::Editor) {
                    handled = m_editorView.OnKeyDown(wParam);
                    if (!handled && wParam == VK_ESCAPE) {
                        m_displayManager.CancelPreview();
                        m_editorView.SetMonitorProfile(nullptr);
                        m_selectedProfileId.clear();
                        m_listView.SetSelectedId(L"");
                        m_detailMode = DetailMode::Empty;
                        handled = true;
                    }
                }

                if (!handled && wParam == VK_ESCAPE && m_listView.IsSearchFocused()) {
                    m_listView.BlurSearch();
                    handled = true;
                }
            } else if (m_section == Section::Monitor) {
                handled = m_editorView.OnKeyDown(wParam);
            }

            if (handled) Redraw();
            return 0;
        }

        case WM_KEYUP: {
            bool handled = false;
            if (m_section == Section::Settings &&
                m_settingsView.IsListeningForScreenshotHotkey()) {
                handled = m_settingsView.OnKeyUp(wParam);
                if (handled) Redraw();
                return 0;
            }

            if (m_section == Section::GameProfiles) {
                if (m_detailMode == DetailMode::Editor) {
                    handled = m_editorView.OnKeyUp(wParam);
                }
            } else if (m_section == Section::Monitor) {
                handled = m_editorView.OnKeyUp(wParam);
            }

            if (handled) Redraw();
            return 0;
        }

        case WM_TIMER: {
            if (wParam == TIMER_ANIMATION) {
                TickAnimations();
            } else if (wParam == TIMER_TOAST) {
                KillTimer(hwnd, TIMER_TOAST);
                m_toastAnim.To(0.0f);
                StartAnimationTimer();
            }
            return 0;
        }

        case WM_CLOSE: {
            if (m_preferences.closeToTray) {
                Hide();
            } else if (m_onExitRequested) {
                m_onExitRequested();
            } else {
                PostQuitMessage(0);
            }
            return 0;
        }

        case WM_DESTROY: {
            StopAnimationTimer();
            KillTimer(hwnd, TIMER_TOAST);
            KillTimer(hwnd, TIMER_SCREENSHOT_TOAST);
            return 0;
        }

        case WM_TRAYICON: {
            if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
                Show();
            } else if (lParam == WM_RBUTTONUP && m_pTrayIcon) {
                m_pTrayIcon->ShowContextMenu(hwnd, m_isPaused, StartupManager::IsEnabled());
            }
            return 0;
        }

        case WM_COMMAND: {
            UINT cmdId = LOWORD(wParam);
            switch (cmdId) {
                case IDM_TRAY_OPEN:
                    Show();
                    break;
                case IDM_TRAY_TOGGLE_PAUSE:
                    TogglePause();
                    break;
                case IDM_TRAY_RESET_DISPLAY:
                    ResetDisplay();
                    break;
                case IDM_TRAY_EXIT:
                    PostQuitMessage(0);
                    break;
                default:
                    break;
            }
            return 0;
        }

        case WM_DISPLAYCHANGE: {
            m_displayManager.OnDisplayConfigurationChanged();
            m_editorView.SetAvailableResolutions(
                m_resolutionManager.GetAvailableResolutions());
            const auto& profile = m_editorView.GetEditingProfile();
            m_editorView.SetScalingModesSupported(
                profile.resolutionProfile &&
                m_resolutionManager.SupportsScaling(
                    profile.resolutionProfile->width,
                    profile.resolutionProfile->height));
            return 0;
        }

        default:
            return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
}

void MainWindow::OnOpenScreenshotsFolder() {
    if (Application::GetInstance()) {
        Application::GetInstance()->GetScreenshotManager().OpenScreenshotsFolder();
    }
}

void MainWindow::ShowScreenshotClickActionMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        Logger::Error(L"Failed to create screenshot notification action menu.");
        return;
    }

    const UINT openImageFlags =
        MF_STRING | (m_preferences.screenshotClickAction == ScreenshotClickAction::OpenImage
                         ? MF_CHECKED : 0);
    const UINT openFolderFlags =
        MF_STRING | (m_preferences.screenshotClickAction ==
                             ScreenshotClickAction::OpenScreenshotsFolder
                         ? MF_CHECKED : 0);
    const UINT copyImageFlags =
        MF_STRING | (m_preferences.screenshotClickAction == ScreenshotClickAction::CopyImage
                         ? MF_CHECKED : 0);
    AppendMenuW(menu, openImageFlags, IDM_SCREENSHOT_CLICK_OPEN_IMAGE, L"Open the image");
    AppendMenuW(menu, openFolderFlags, IDM_SCREENSHOT_CLICK_OPEN_FOLDER,
                L"Open screenshots folder");
    if (!m_preferences.autoCopyScreenshot) {
        AppendMenuW(menu, copyImageFlags, IDM_SCREENSHOT_CLICK_COPY_IMAGE, L"Copy image");
    }

    POINT screenPoint{};
    if (!GetCursorPos(&screenPoint)) {
        DestroyMenu(menu);
        Logger::Error(L"Failed to locate screenshot action menu position.");
        return;
    }

    SetForegroundWindow(m_hwnd);
    const UINT command = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y,
        0, m_hwnd, nullptr);
    DestroyMenu(menu);

    switch (command) {
        case IDM_SCREENSHOT_CLICK_OPEN_IMAGE:
            m_preferences.screenshotClickAction = ScreenshotClickAction::OpenImage;
            break;
        case IDM_SCREENSHOT_CLICK_OPEN_FOLDER:
            m_preferences.screenshotClickAction =
                ScreenshotClickAction::OpenScreenshotsFolder;
            break;
        case IDM_SCREENSHOT_CLICK_COPY_IMAGE:
            if (m_preferences.autoCopyScreenshot) return;
            m_preferences.screenshotClickAction = ScreenshotClickAction::CopyImage;
            break;
        default:
            return;
    }

    m_settingsView.SetScreenshotClickAction(m_preferences.screenshotClickAction);
    SavePreferences();
    Redraw();
}

void MainWindow::OnBrowseScreenshotsFolder() {
    // Open folder browser dialog
    wchar_t folderPath[MAX_PATH] = {};
    std::wstring current = m_preferences.screenshotsDirectory.empty()
        ? PreferencesStore::GetDefaultScreenshotsDirectory()
        : m_preferences.screenshotsDirectory;
    wcscpy_s(folderPath, current.c_str());

    BROWSEINFOW bi = {};
    bi.hwndOwner = m_hwnd;
    bi.pszDisplayName = folderPath;
    bi.lpszTitle = L"Select Screenshots Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, path)) {
            m_preferences.screenshotsDirectory = path;
            m_settingsView.SetScreenshotsDirectory(path);
            SavePreferences();

            // Update screenshot manager
            if (Application::GetInstance()) {
                auto& mgr = Application::GetInstance()->GetScreenshotManager();
                auto settings = mgr.GetSettings();
                settings.screenshotsDirectory = path;
                mgr.SetSettings(settings);
            }

            ShowToast(L"Screenshots folder updated", std::wstring(L"Screenshots will be saved to ") + path);
            Redraw();
        }
        CoTaskMemFree(pidl);
    }
}

void MainWindow::OnSetScreenshotHotkey(const std::wstring& hotkeyString) {
    HotkeyDefinition hotkey;
    if (!hotkey.FromString(hotkeyString)) {
        Logger::Warn(L"Screenshot hotkey was not valid: " + hotkeyString);
        return;
    }

    m_preferences.screenshotGlobalHotkey = hotkey.ToDisplayString();
    SavePreferences();
    m_settingsView.SetScreenshotGlobalHotkey(m_preferences.screenshotGlobalHotkey);

    if (Application::GetInstance()) {
        auto& screenshotManager = Application::GetInstance()->GetScreenshotManager();
        auto settings = screenshotManager.GetSettings();
        settings.globalHotkey = hotkey;
        screenshotManager.SetSettings(settings);
    }
    ShowToast(L"Screenshot shortcut updated", m_preferences.screenshotGlobalHotkey);
}

void MainWindow::OnSetProfileHotkey(const std::wstring& profileId) {
    // Get the profile
    const auto* profile = m_profileManager.GetProfileById(profileId);
    if (!profile) return;

    std::wstring msg = L"Configure screenshot hotkey for profile: " + profile->name + L"\n\n";
    msg += L"Current hotkey: " + (profile->screenshotHotkey.empty()
        ? L"(none)" : profile->screenshotHotkey);
    msg += L"\n\nValid hotkey examples:\n";
    msg += L"  F12\n";
    msg += L"  Ctrl+Shift+S\n";
    msg += L"  Win+Print Screen\n\n";
    msg += L"Full hotkey capture dialog will be available soon.";

    MessageBoxW(m_hwnd, msg.c_str(), L"Set Profile Screenshot Hotkey",
               MB_OK | MB_ICONINFORMATION);
}

} // namespace prismshift

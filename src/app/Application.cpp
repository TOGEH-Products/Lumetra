#include "app/Application.h"
#include "utilities/Logger.h"
#include "utilities/Preferences.h"

namespace prismshift {

namespace {
constexpr const wchar_t* MESSAGE_WINDOW_CLASS = L"Lumetra_MessageClass";

// What is known about a process id.
enum class ProcessState {
    NotRunning,  // The id is not in use any more.
    Running,     // The process is alive.
    Unknown      // The process exists but refuses the query (protected games).
};

ProcessState QueryProcessState(DWORD pid) {
    if (pid == 0) {
        return ProcessState::NotRunning;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        // A process id that Windows does not know any more fails with
        // ERROR_INVALID_PARAMETER. Every other failure means the process
        // exists but does not allow the query.
        return GetLastError() == ERROR_INVALID_PARAMETER
            ? ProcessState::NotRunning
            : ProcessState::Unknown;
    }
    DWORD exitCode = 0;
    ProcessState state = ProcessState::Running;
    if (GetExitCodeProcess(process, &exitCode) && exitCode != STILL_ACTIVE) {
        state = ProcessState::NotRunning;
    }
    CloseHandle(process);
    return state;
}

// Reads the start time of a process. Returns false when the process does not
// allow the query, which happens for anti-cheat protected games.
bool QueryProcessCreationTime(DWORD pid, ULONGLONG& creationTime) {
    creationTime = 0;
    if (pid == 0) {
        return false;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (!process) {
            return false;
        }
    }
    FILETIME creation{}, exit{}, kernel{}, user{};
    const BOOL queried = GetProcessTimes(process, &creation, &exit, &kernel, &user);
    CloseHandle(process);
    if (!queried) {
        return false;
    }
    creationTime = (static_cast<ULONGLONG>(creation.dwHighDateTime) << 32) |
                   creation.dwLowDateTime;
    return creationTime != 0;
}
}

Application::Application()
    : m_mainWindow(m_profileManager, m_displayManager, m_resolutionManager) {
    s_instance = this;
}

Application::~Application() {
    RequestShutdown();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool Application::Initialize(bool /*debugMode*/, bool startMinimized) {
    Logger::Info(L"Starting Lumetra Stage 2 initialization...");

    // 1. Initialize Display Manager & query baseline gamma ramp
    if (!m_displayManager.Initialize()) {
        Logger::Error(L"Failed to initialize DisplayManager.");
        return false;
    }

    const GammaRamp& baselineRamp = m_displayManager.GetEngine()->GetDefaultRamp();

    // 2. Initialize Profile Manager with baseline hardware ramp & stored profiles
    m_profileManager.Initialize(baselineRamp);
    m_currentProfileId = m_profileManager.GetDefaultProfile().id;

    // 3. Create hidden message-only window for OS events (WM_DISPLAYCHANGE, WM_TRAYICON, WM_HOTKEY)
    if (!CreateMessageWindow()) {
        Logger::Warn(L"Failed to create message-only window.");
    }

    // 4. Initialize Screenshot Manager
    if (!m_screenshotManager.Initialize(m_messageHwnd)) {
        Logger::Warn(L"Failed to initialize screenshot manager (feature will be unavailable).");
    } else {
        // Load screenshot preferences and configure
        const Preferences prefs = PreferencesStore::Load();
        ScreenshotManager::Settings screenshotSettings;
        screenshotSettings.enabled = prefs.screenshotsEnabled;
        screenshotSettings.autoCopy = prefs.autoCopyScreenshot;
        screenshotSettings.screenshotsDirectory = prefs.screenshotsDirectory.empty()
            ? PreferencesStore::GetDefaultScreenshotsDirectory()
            : prefs.screenshotsDirectory;

        // Parse global hotkey
        if (!prefs.screenshotGlobalHotkey.empty()) {
            screenshotSettings.globalHotkey.FromString(prefs.screenshotGlobalHotkey);
        }

        m_screenshotManager.SetSettings(screenshotSettings);

        // Register per-profile hotkeys
        for (const auto& profile : m_profileManager.GetAllProfiles()) {
            if (!profile.screenshotHotkey.empty()) {
                HotkeyDefinition hotkey;
                if (hotkey.FromString(profile.screenshotHotkey)) {
                    m_screenshotManager.RegisterProfileHotkey(profile.id, hotkey);
                }
            }
        }

        const auto& monitorProfile = m_profileManager.GetMonitorProfile();
        if (!monitorProfile.screenshotHotkey.empty()) {
            HotkeyDefinition hotkey;
            if (hotkey.FromString(monitorProfile.screenshotHotkey)) {
                m_screenshotManager.RegisterProfileHotkey(monitorProfile.id, hotkey);
            }
        }

    }

    // 5. Initialize system tray icon (Application owns this)
    if (!m_trayIcon.Initialize(m_messageHwnd, GetModuleHandleW(nullptr))) {
        Logger::Warn(L"Failed to initialize system tray icon.");
    }

    // 6. Create main configuration window, then wire tray icon.
    // The command-line flag always wins; otherwise honour the saved preference.
    const Preferences prefs = PreferencesStore::Load();
    int showCmd = (startMinimized || prefs.startMinimized) ? SW_HIDE : SW_SHOW;
    if (!m_mainWindow.Create(GetModuleHandleW(nullptr), showCmd)) {
        Logger::Error(L"Failed to create main configuration window.");
        return false;
    }
    m_screenshotManager.SetClipboardOwnerWindow(m_mainWindow.GetHwnd());
    m_mainWindow.SetTrayIcon(&m_trayIcon);
    m_screenshotManager.SetNotificationCallback(
        [this](const std::wstring& /*message*/, bool isError,
               const std::wstring& imagePath, bool imageCopied) {
            m_mainWindow.ShowScreenshotToast(isError, imagePath, imageCopied);
    });

    // The shell drives the real automation state (not a local-only flag).
    m_mainWindow.SetPauseToggleCallback([this](bool paused) {
        SetSwitchingPaused(paused);
    });
    m_mainWindow.SetResetDisplayCallback([this]() {
        ResetDisplayColors();
    });
    m_mainWindow.SetExitCallback([this]() {
        RequestShutdown();
    });
    m_mainWindow.SetResolutionPromptCallbacks(
        [this]() { return ApplyPendingResolution(); },
        [this]() { ClearPendingResolution(); });
    m_mainWindow.SetResolutionModeChangedCallback(
        [this](ResolutionSwitchMode mode) {
            if (mode == ResolutionSwitchMode::Off) {
                RestoreResolution();
                m_mainWindow.DismissResolutionPrompt();
                ClearPendingResolution();
            }
            m_detector.ForceRefresh();
        });

    // 7. Install Console Ctrl Handler for clean Ctrl-C / shutdown handling
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    // 8. Start Foreground Detector with event callback
    bool detectorStarted = m_detector.Start([this](HWND hwnd, const ProcessInfo& process) {
        OnForegroundChanged(hwnd, process);
    });

    if (!detectorStarted) {
        Logger::Error(L"Failed to start ForegroundDetector.");
        return false;
    }

    // 9. Check initial foreground state immediately upon launch
    m_detector.CheckCurrentForeground();

    m_running = true;
    Logger::Info(L"Lumetra Stage 2 initialized successfully.");
    return true;
}

int Application::Run() {
    MSG msg;
    while (m_running && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Logger::Info(L"Message loop exited.");
    return 0;
}

void Application::RequestShutdown() {
    if (m_running) {
        m_running = false;
        Logger::Info(L"Lumetra shutting down...");

        m_detector.Stop();
        RestoreResolution();
        m_mainWindow.DismissResolutionPrompt();
        ClearPendingResolution();
        m_screenshotManager.Shutdown();
        m_trayIcon.Remove();
        m_mainWindow.Hide();
        DestroyMessageWindow();
        m_displayManager.Shutdown();

        PostQuitMessage(0);
    }
}

void Application::TogglePauseSwitching() {
    SetSwitchingPaused(!m_switchingPaused);
}

void Application::SetSwitchingPaused(bool paused) {
    m_switchingPaused = paused;
    m_mainWindow.SetSwitchingPaused(paused);

    if (paused) {
        RestoreResolution();
        m_mainWindow.DismissResolutionPrompt();
        ClearPendingResolution();
        Logger::Info(L"Profile switching paused. Restoring baseline display profile.");
        m_displayManager.RestoreDefault();
        m_currentProfileId.clear();
        m_mainWindow.SetActiveProfileName(L"Switching Paused");
    } else {
        Logger::Info(L"Profile switching resumed. Checking current foreground application...");
        m_detector.CheckCurrentForeground();
    }
}

void Application::ResetDisplayColors() {
    Logger::Info(L"User requested reset of display colors to linear identity.");
    m_displayManager.ResetDisplayToIdentity();
    m_currentProfileId = m_profileManager.GetDefaultProfile().id;
    m_mainWindow.SetActiveProfileName(L"Reset to Linear Identity");
}

void Application::OnForegroundChanged(HWND hwnd, const ProcessInfo& process) {
    if (m_switchingPaused) {
        return;
    }

    const ColorProfile* matchedProfile = nullptr;
    std::wstring displayName = process.exeName.empty() ? L"Desktop / Unmanaged" : process.exeName;

    if (process.IsValid()) {
        matchedProfile = m_profileManager.MatchProfile(process.exeName);
    }

    if (!matchedProfile || !matchedProfile->enabled) {
        matchedProfile = &m_profileManager.GetDefaultProfile();
    }

    UpdateResolutionForForeground(hwnd, matchedProfile, process);

    // Check if the required profile is already active
    if (m_currentProfileId == matchedProfile->id) {
        // The profile selection is correct, but the hardware may have been reset
        // by a popup, display-mode transition, or another fullscreen app while
        // focus was elsewhere. Repair it now instead of waiting for the timer.
        m_displayManager.EnsureCurrentProfileApplied();
        Logger::Debug(L"Focused: [" + displayName + L"] (PID " + std::to_wstring(process.pid) +
                      L") -> Profile [" + matchedProfile->name + L"] already active.");
        return;
    }

    Logger::Info(L"Focused: [" + displayName + L"] (PID " + std::to_wstring(process.pid) +
                 L") -> Switching profile to: [" + matchedProfile->name + L"]");

    bool applied = matchedProfile->isDefault
        ? m_displayManager.RestoreDefault()
        : m_displayManager.ApplyProfile(*matchedProfile);
    if (!applied) {
        Logger::Error(L"Failed to apply profile: [" + matchedProfile->name + L"]");
        return;
    }

    m_currentProfileId = matchedProfile->id;
    m_mainWindow.SetActiveProfileName(matchedProfile->name);

    // Update screenshot manager with current profile
    UpdateScreenshotManagerProfile(matchedProfile);
}

void Application::ClearPendingResolution() {
    m_pendingResolutionProfileId.clear();
    m_pendingResolutionPid = 0;
    m_pendingResolutionWindow = nullptr;
    m_pendingResolutionDevice.clear();
}

void Application::PrunePromptRecords(DWORD activePid) {
    for (auto it = m_promptedProcesses.begin(); it != m_promptedProcesses.end();) {
        if (it->first != activePid &&
            QueryProcessState(it->first) == ProcessState::NotRunning) {
            it = m_promptedProcesses.erase(it);
        } else {
            ++it;
        }
    }
}

void Application::RefreshPromptRecord(DWORD pid, HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        return;
    }
    const auto it = m_promptedProcesses.find(pid);
    if (it == m_promptedProcesses.end()) {
        return;
    }
    DWORD owner = 0;
    GetWindowThreadProcessId(hwnd, &owner);
    if (owner != pid) {
        return;
    }
    // A game that recreates its window - which happens when a display mode
    // changes - would otherwise look like a new launch.
    it->second.window = hwnd;
}

bool Application::WasPromptedFor(DWORD pid, ULONGLONG creationTime,
                                  bool hasCreationTime) const {
    const auto it = m_promptedProcesses.find(pid);
    if (it == m_promptedProcesses.end()) {
        return false;
    }

    const ResolutionPromptRecord& record = it->second;
    if (hasCreationTime && record.hasCreationTime) {
        return record.creationTime == creationTime;
    }

    // The start time is unavailable for anti-cheat protected games, so the
    // window the game was showing when it was prompted is used instead. A new
    // launch comes up with a new window, so the record only matches while
    // that window still belongs to the same process.
    if (record.window && IsWindow(record.window)) {
        DWORD owner = 0;
        GetWindowThreadProcessId(record.window, &owner);
        return owner == pid;
    }
    return false;
}

void Application::UpdateResolutionForForeground(HWND hwnd,
                                                const ColorProfile* profile,
                                                const ProcessInfo& process) {
    PrunePromptRecords(process.pid);
    RefreshPromptRecord(process.pid, hwnd);

    const Preferences prefs = PreferencesStore::Load();
    if (!profile || prefs.resolutionSwitchMode == ResolutionSwitchMode::Off) {
        RestoreResolution();
        m_mainWindow.DismissResolutionPrompt();
        ClearPendingResolution();
        return;
    }

    if (!profile->isDefault &&
        (!profile->enabled || !profile->resolutionProfile)) {
        profile = &m_profileManager.GetMonitorProfile();
    }
    if (!profile->resolutionProfile) {
        RestoreResolution();
        m_mainWindow.DismissResolutionPrompt();
        ClearPendingResolution();
        return;
    }

    if (profile->isDefault) {
        // The monitor profile belongs to Windows' primary display.
        m_resolutionManager.SetTargetToPrimaryDisplay();
        m_mainWindow.DismissResolutionPrompt();
        ClearPendingResolution();
        if (m_activeResolutionProfileId == profile->id &&
            m_resolutionManager.IsCurrentResolution(*profile->resolutionProfile)) {
            return;
        }
        if (!m_resolutionManager.Apply(*profile->resolutionProfile)) {
            Logger::Warn(L"Could not apply the monitor resolution profile.");
            RestoreResolution();
            m_mainWindow.ShowResolutionFailure(
                L"Windows could not apply the monitor's " +
                std::to_wstring(profile->resolutionProfile->width) + L" x " +
                std::to_wstring(profile->resolutionProfile->height) + L" mode.");
            return;
        }
        m_activeResolutionProfileId = profile->id;
        return;
    }

    // A game profile belongs to the display the game is running on, so a
    // second monitor is never resized behind the user's back.
    m_resolutionManager.SetTargetToDisplayOfWindow(hwnd);

    if (prefs.resolutionSwitchMode == ResolutionSwitchMode::AutoSwitch) {
        m_mainWindow.DismissResolutionPrompt();
        ClearPendingResolution();
        if (m_activeResolutionProfileId == profile->id &&
            m_resolutionManager.IsCurrentResolution(*profile->resolutionProfile)) {
            return;
        }
        if (!m_resolutionManager.Apply(*profile->resolutionProfile)) {
            Logger::Warn(L"Could not apply resolution profile for game: " + profile->name);
            RestoreResolution();
            m_mainWindow.ShowResolutionFailure(
                L"Windows could not apply " +
                std::to_wstring(profile->resolutionProfile->width) + L" x " +
                std::to_wstring(profile->resolutionProfile->height) + L".");
            return;
        }
        m_activeResolutionProfileId = profile->id;
        return;
    }

    ULONGLONG creationTime = 0;
    const bool hasCreationTime = QueryProcessCreationTime(process.pid, creationTime);
    if (WasPromptedFor(process.pid, creationTime, hasCreationTime)) {
        if (m_pendingResolutionPid != process.pid ||
            m_pendingResolutionProfileId != profile->id) {
            m_mainWindow.DismissResolutionPrompt();
            ClearPendingResolution();
        }
        return;
    }

    RestoreResolution();
    m_pendingResolutionProfileId = profile->id;
    m_pendingResolutionPid = process.pid;
    m_pendingResolutionWindow = hwnd;
    // Remember the display the game is on. The window may be gone by the time
    // Apply is clicked, and the mode has to land on the same display.
    const auto gameDisplay = GetDisplayForWindow(hwnd);
    m_pendingResolutionDevice = gameDisplay ? gameDisplay->deviceName : L"";
    m_promptedProcesses[process.pid] =
        ResolutionPromptRecord{ creationTime, hasCreationTime, hwnd };

    const auto current = m_resolutionManager.GetCurrentResolution();
    ResolutionProfile currentMode = current.value_or(ResolutionProfile{});
    if (!current) {
        currentMode.width = 0;
        currentMode.height = 0;
    }
    const bool supported = m_resolutionManager.IsSupported(*profile->resolutionProfile);
    m_mainWindow.ShowResolutionPrompt(
        profile->name, currentMode, *profile->resolutionProfile, supported,
        current ? L"" : L"Windows could not read the current display mode.",
        MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY));
}

bool Application::ApplyPendingResolution() {
    const ColorProfile* profile =
        m_profileManager.GetProfileById(m_pendingResolutionProfileId);
    if (!profile || !profile->enabled || !profile->resolutionProfile) {
        Logger::Warn(L"Resolution prompt no longer has a valid game profile.");
        ClearPendingResolution();
        return true;
    }

    // The user clicked Apply, so the game does not have to be the foreground
    // window any more: clicking the non-activating popup, or the display mode
    // change that follows, can hand the focus to another window. Only a game
    // that really exited invalidates the prompt.
    if (m_pendingResolutionPid != 0 &&
        QueryProcessState(m_pendingResolutionPid) == ProcessState::NotRunning) {
        Logger::Warn(L"Discarding a resolution prompt for a game that has exited.");
        ClearPendingResolution();
        return true;
    }

    if (!m_pendingResolutionDevice.empty()) {
        m_resolutionManager.SetTargetDisplay(m_pendingResolutionDevice);
    } else {
        m_resolutionManager.SetTargetToDisplayOfWindow(m_pendingResolutionWindow);
    }

    if (!m_resolutionManager.Apply(*profile->resolutionProfile)) {
        Logger::Error(L"Failed to apply the selected game's resolution profile.");
        RestoreResolution();
        return false;
    }
    m_activeResolutionProfileId = profile->id;
    ClearPendingResolution();
    return true;
}

void Application::RestoreResolution() {
    if (!m_resolutionManager.HasOriginalMode()) {
        m_activeResolutionProfileId.clear();
        m_restoreFailureNotified = false;
        return;
    }
    if (m_resolutionManager.Restore()) {
        m_activeResolutionProfileId.clear();
        m_restoreFailureNotified = false;
    } else if (!m_restoreFailureNotified) {
        m_mainWindow.ShowResolutionFailure(
            L"Lumetra could not restore the display's original mode. It will retry on the next foreground change.");
        m_restoreFailureNotified = true;
    }
}

void Application::UpdateScreenshotManagerProfile(const ColorProfile* profile) {
    if (profile) {
        m_screenshotManager.SetActiveProfile(profile->id, profile->name);
    } else {
        m_screenshotManager.SetActiveProfile(L"", L"Desktop");
    }
}

void Application::ToggleStartup() {
    bool enabled = !StartupManager::IsEnabled();
    if (!StartupManager::SetEnabled(enabled)) {
        Logger::Warn(L"Failed to update Windows startup setting.");
    }
}

bool Application::CreateMessageWindow() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = MESSAGE_WINDOW_CLASS;

    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    m_messageHwnd = CreateWindowExW(
        0,
        MESSAGE_WINDOW_CLASS,
        L"LumetraMessageReceiver",
        0, 0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        GetModuleHandleW(nullptr),
        this
    );

    if (m_messageHwnd) {
        SetTimer(m_messageHwnd, FOREGROUND_WATCHDOG_TIMER_ID, FOREGROUND_WATCHDOG_INTERVAL_MS, nullptr);
    }

    return m_messageHwnd != nullptr;
}

void Application::DestroyMessageWindow() {
    if (m_messageHwnd) {
        KillTimer(m_messageHwnd, FOREGROUND_WATCHDOG_TIMER_ID);
        DestroyWindow(m_messageHwnd);
        m_messageHwnd = nullptr;
        UnregisterClassW(MESSAGE_WINDOW_CLASS, GetModuleHandleW(nullptr));
    }
}

LRESULT CALLBACK Application::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (s_instance) {
        return s_instance->HandleMessage(hwnd, uMsg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

LRESULT Application::HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (!m_messageHwnd) {
        m_messageHwnd = hwnd;
    }

    switch (uMsg) {
        case WM_HOTKEY: {
            // Forward hotkey messages to screenshot manager
            if (m_screenshotManager.HandleHotkeyMessage(wParam, lParam)) {
                return 0;
            }
            break;
        }

        case WM_TRAYICON: {
            if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
                m_mainWindow.Show();
            } else if (lParam == WM_RBUTTONUP) {
                m_trayIcon.ShowContextMenu(m_messageHwnd, m_switchingPaused, StartupManager::IsEnabled());
            }
            return 0;
        }

        case WM_COMMAND: {
            UINT cmdId = LOWORD(wParam);
            switch (cmdId) {
                case IDM_TRAY_OPEN:
                    m_mainWindow.Show();
                    break;
                case IDM_TRAY_TOGGLE_PAUSE:
                    TogglePauseSwitching();
                    break;
                case IDM_TRAY_RESET_DISPLAY:
                    ResetDisplayColors();
                    break;
                case IDM_TRAY_TOGGLE_STARTUP:
                    ToggleStartup();
                    break;
                case IDM_TRAY_EXIT:
                    RequestShutdown();
                    break;
            }
            return 0;
        }

        case WM_DISPLAYCHANGE: {
            m_displayManager.OnDisplayConfigurationChanged();
            return 0;
        }

        case WM_TIMER: {
            if (wParam == FOREGROUND_WATCHDOG_TIMER_ID) {
                // Safety net for missed/coalesced WinEvent notifications. This is
                // O(1) while nothing changes: one GetForegroundWindow call and a
                // handle comparison. The process is resolved only on a change.
                m_detector.Refresh();

                if (++m_watchdogTicks >= GAMMA_REASSERT_TICKS) {
                    m_watchdogTicks = 0;
                    m_displayManager.EnsureCurrentProfileApplied();
                }
            }
            return 0;
        }

        case WM_CLOSE: {
            RequestShutdown();
            return 0;
        }

        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }

        default:
            return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

BOOL WINAPI Application::ConsoleCtrlHandler(DWORD ctrlType) {
    switch (ctrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            Logger::Info(L"System shutdown or termination signal received. Restoring display...");
            if (s_instance) {
                s_instance->RequestShutdown();
            }
            return TRUE;
        default:
            return FALSE;
    }
}

} // namespace prismshift

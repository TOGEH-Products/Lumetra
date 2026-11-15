#pragma once

#include "utilities/NonCopyable.h"
#include "utilities/StartupManager.h"
#include "detection/ForegroundDetector.h"
#include "display/DisplayManager.h"
#include "display/ResolutionManager.h"
#include "profiles/ProfileManager.h"
#include "screenshots/ScreenshotManager.h"
#include "ui/MainWindow.h"
#include "ui/TrayIcon.h"
#include <windows.h>
#include <optional>
#include <unordered_map>

namespace prismshift {

// Bookkeeping for the "ask before applying" prompt.
//
// A prompt is offered once per game launch, so the record has to identify a
// process *instance* and not just a process id: Windows recycles process ids,
// and an anti-cheat protected game may not allow its start time to be read at
// all. The window that was foreground when the prompt was shown is kept as a
// fallback identity for those games.
struct ResolutionPromptRecord {
    ULONGLONG creationTime = 0;  // 0 when the process does not allow querying it.
    bool hasCreationTime = false;
    HWND window = nullptr;       // Foreground window when the prompt was shown.
};

class Application : public NonCopyable {
public:
    Application();
    ~Application();

    bool Initialize(bool debugMode = false, bool startMinimized = false);
    int Run();
    void RequestShutdown();

    void TogglePauseSwitching();
    void SetSwitchingPaused(bool paused);
    [[nodiscard]] bool IsSwitchingPaused() const { return m_switchingPaused; }

    void ResetDisplayColors();
    void ToggleStartup();

    [[nodiscard]] static Application* GetInstance() { return s_instance; }
    [[nodiscard]] MainWindow& GetMainWindow() { return m_mainWindow; }
    [[nodiscard]] TrayIcon& GetTrayIcon() { return m_trayIcon; }
    [[nodiscard]] ScreenshotManager& GetScreenshotManager() { return m_screenshotManager; }

private:
    bool CreateMessageWindow();
    void DestroyMessageWindow();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void OnForegroundChanged(HWND hwnd, const ProcessInfo& process);
    void UpdateResolutionForForeground(HWND hwnd, const ColorProfile* profile,
                                       const ProcessInfo& process);
    bool ApplyPendingResolution();
    void RestoreResolution();
    void UpdateScreenshotManagerProfile(const ColorProfile* profile);
    static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType);

    // Drops the record of a prompt that was already handled.
    void ClearPendingResolution();
    // Removes records of processes that are gone.
    void PrunePromptRecords(DWORD activePid);
    // Keeps the recorded window of a game in sync with the window it shows.
    void RefreshPromptRecord(DWORD pid, HWND hwnd);
    // True when this exact game launch has already been offered a prompt.
    [[nodiscard]] bool WasPromptedFor(DWORD pid, ULONGLONG creationTime,
                                      bool hasCreationTime) const;

    static constexpr UINT FOREGROUND_WATCHDOG_TIMER_ID = 1;
    static constexpr UINT FOREGROUND_WATCHDOG_INTERVAL_MS = 1000;
    // At 1 Hz this repairs gamma/color drift roughly every five seconds without
    // reading the hardware ramp on every watchdog tick.
    static constexpr UINT GAMMA_REASSERT_TICKS = 5;

    DisplayManager m_displayManager;
    ResolutionManager m_resolutionManager;
    ProfileManager m_profileManager;
    ForegroundDetector m_detector;
    ScreenshotManager m_screenshotManager;
    TrayIcon m_trayIcon;

    MainWindow m_mainWindow;

    HWND m_messageHwnd = nullptr;
    bool m_running = false;
    bool m_switchingPaused = false;
    UINT m_watchdogTicks = 0;
    std::wstring m_currentProfileId;
    std::wstring m_activeResolutionProfileId;
    std::wstring m_pendingResolutionProfileId;
    DWORD m_pendingResolutionPid = 0;
    HWND m_pendingResolutionWindow = nullptr;
    std::wstring m_pendingResolutionDevice;
    bool m_restoreFailureNotified = false;
    std::unordered_map<DWORD, ResolutionPromptRecord> m_promptedProcesses;

    static inline Application* s_instance = nullptr;
};

} // namespace prismshift

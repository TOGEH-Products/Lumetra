#pragma once

#include "ui/ProfileListView.h"
#include "ui/ProfileEditorView.h"
#include "ui/AddAppDialog.h"
#include "ui/SettingsView.h"
#include "ui/TrayIcon.h"
#include "ui/Anim.h"
#include "ui/Renderer.h"
#include "profiles/ProfileManager.h"
#include "display/DisplayManager.h"
#include "display/ResolutionManager.h"
#include "updates/UpdateService.h"
#include "utilities/NonCopyable.h"
#include "utilities/Preferences.h"
#include <windows.h>
#include <d2d1.h>
#include <wrl/client.h>
#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace prismshift {

class Application; // Forward declaration

// Lumetra application shell: fixed left navigation rail plus a routed content
// area. Profiles is a master-detail section; Settings is a grouped form.
//
// Rendering is owned by m_renderer (Direct2D + DirectWrite). All layout
// literals are device-independent pixels (DIPs, 1/96 inch); the renderer maps
// them onto the window's current monitor DPI automatically, so nothing is
// scaled twice.
class MainWindow : public NonCopyable {
public:
    enum class Section {
        Monitor,      // top of the rail
        GameProfiles, // middle
        Settings      // bottom
    };

    using PauseToggleCallback = std::function<void(bool paused)>;
    using ActionCallback = std::function<void()>;
    using ResolutionApplyCallback = std::function<bool()>;
    using ResolutionModeChangedCallback =
        std::function<void(ResolutionSwitchMode mode)>;

    MainWindow(ProfileManager& profileManager, DisplayManager& displayManager,
               ResolutionManager& resolutionManager);
    ~MainWindow();

    bool Create(HINSTANCE hInstance, int nCmdShow);
    void Show();
    void Hide();
    [[nodiscard]] bool IsVisible() const;
    [[nodiscard]] HWND GetHwnd() const { return m_hwnd; }
    [[nodiscard]] Renderer& GetRenderer() { return m_renderer; }

    void SetSwitchingPaused(bool paused);
    [[nodiscard]] bool IsSwitchingPaused() const { return m_isPaused; }

    void SetActiveProfileName(const std::wstring& name);
    void SetTrayIcon(TrayIcon* trayIcon) { m_pTrayIcon = trayIcon; }
    [[nodiscard]] TrayIcon* GetTrayIcon() const { return m_pTrayIcon; }

    void SetPauseToggleCallback(PauseToggleCallback cb) { m_onPauseToggle = std::move(cb); }
    void SetResetDisplayCallback(ActionCallback cb) { m_onResetDisplay = std::move(cb); }
    void SetExitCallback(ActionCallback cb) { m_onExitRequested = std::move(cb); }

    void ShowScreenshotToast(bool isError, const std::wstring& imagePath = L"",
                             bool imageCopied = false);
    void SetResolutionPromptCallbacks(ResolutionApplyCallback onApply,
                                      ActionCallback onDismiss);
    void SetResolutionModeChangedCallback(ResolutionModeChangedCallback callback) {
        m_onResolutionModeChanged = std::move(callback);
    }
    void ShowResolutionPrompt(const std::wstring& gameName,
                              const ResolutionProfile& current,
                              const ResolutionProfile& proposed,
                              bool supported,
                              const std::wstring& status = L"",
                              HMONITOR monitor = nullptr);
    void DismissResolutionPrompt();
    void ShowResolutionFailure(const std::wstring& message);
    void Redraw();

private:
    enum class UpdateJobKind {
        Check,
        Download,
        Notes
    };

    struct UpdateJobResult {
        UpdateJobKind kind = UpdateJobKind::Check;
        bool manual = false;
        UpdateCheckResult check;
        UpdateDownloadResult download;
        ReleaseNotesResult notes;
    };

    struct UpdateDialogLayout {
        RECT card{};
        RECT content{};
        RECT whatsNew{};
        RECT download{};
        RECT later{};
        RECT gotIt{};
    };

    static constexpr UINT UPDATE_JOB_COMPLETE = WM_APP + 0x31;
    static constexpr UINT UPDATE_JOB_FAILED = WM_APP + 0x32;

    enum class DetailMode {
        Empty,
        Editor,
        AddApp
    };

    // Buttons of the resolution prompt, in device-independent pixels.
    static constexpr RECT RESOLUTION_TOAST_DISMISS_RECT{ 178, 178, 274, 210 };
    static constexpr RECT RESOLUTION_TOAST_APPLY_RECT{ 284, 178, 380, 210 };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void Paint(Renderer& r);
    void PaintSidebar(Renderer& r, const RECT& rcSidebar);
    void PaintHeader(Renderer& r, const RECT& rcHeader);
    void PaintTertiaryRail(Renderer& r, const RECT& rcRail);
    void PaintGameProfiles(Renderer& r, const RECT& rcBody);
    void PaintMonitor(Renderer& r, const RECT& rcBody);
    void PaintSettings(Renderer& r, const RECT& rcBody);
    void PaintEmptyDetail(Renderer& r, const RECT& rcDetail);
    void PaintToast(Renderer& r);
    void PaintScreenshotToast(Renderer& r);
    void PaintResolutionToast(Renderer& r);
    void ShowProfileContextMenu(POINT clientPoint);

    // Places the resolution popup on `monitor`, scales it to the popup's
    // current DPI and rebuilds its rounded window region.
    void UpdateResolutionToastLayout(HMONITOR monitor);
    void ApplyResolutionToastRegion();
    void UpdateResolutionToastHover(POINT dipPoint);

    [[nodiscard]] RECT ClientRect() const;
    [[nodiscard]] RECT SidebarRect() const;
    [[nodiscard]] RECT HeaderRect() const;
    [[nodiscard]] RECT BodyRect() const;
    [[nodiscard]] RECT ListPaneRect() const;
    [[nodiscard]] RECT TertiaryRailRect() const;
    [[nodiscard]] RECT ProfileTabRailRect() const;
    [[nodiscard]] RECT MonitorEditorRect() const;
    [[nodiscard]] RECT DetailPaneRect() const;
    [[nodiscard]] RECT AddFormRect() const;
    [[nodiscard]] RECT NavItemRect(Section section) const;
    [[nodiscard]] RECT TertiaryRailItemRect(int index) const;
    [[nodiscard]] RECT PauseButtonRect() const;
    [[nodiscard]] RECT HeaderStatusRect() const;
    [[nodiscard]] RECT ResetButtonRect() const;
    [[nodiscard]] RECT EmptyCtaRect() const;
    [[nodiscard]] RECT EmptyCardRect() const;

    // Converts physical mouse coordinates to integer DIPs for hit-testing
    // against the DIP-based layout rects.
    [[nodiscard]] POINT ToDip(POINT pt) const;
    [[nodiscard]] POINT ToPhysical(POINT dipPt) const;

    void ApplyInitialDpiAndSize();

    void SelectSection(Section section);
    void SelectProfile(const std::wstring& profileId);
    void SelectMonitor();
    void BeginAddApp();
    void TogglePause();
    void ResetDisplay();

    void ShowToast(const std::wstring& title, const std::wstring& body);
    void OnSettingsToggle(SettingsView::ToggleId id, bool enabled);    void OnOpenScreenshotsFolder();
    void OnBrowseScreenshotsFolder();
    void OnSetScreenshotHotkey(const std::wstring& currentHotkey);
    void ShowScreenshotClickActionMenu();
    void ShowResolutionModeMenu();
    void ShowResolutionPickerMenu();
    void ShowResolutionScalingMenu();
    void OnSetProfileHotkey(const std::wstring& profileId);
    void SavePreferences();
    void MaybeStartUpdateWork();
    void StartUpdateJob(UpdateJobKind kind, bool manual = false,
                        std::wstring version = L"",
                        std::optional<ReleaseInfo> release = std::nullopt);
    void StartUpdateCheck(bool manual);
    void StartUpdateDownload();
    void HandleUpdateJobResult(UpdateJobResult& result);
    [[nodiscard]] UpdateDialogLayout GetUpdateDialogLayout() const;
    void PaintUpdateDialog(Renderer& r);
    void SetUpdateStatus(const std::wstring& status);

    // Applies the active theme to the DWM title bar (light vs dark chrome and
    // caption/border/text colours). Called on creation and on theme change.
    void ApplyWindowChrome();
    void OnThemeChanged(int themeId);

    void StartAnimationTimer();
    void StopAnimationTimer();
    void TickAnimations();
    [[nodiscard]] bool HasActiveAnimations() const;

    HWND m_hwnd = nullptr;
    HWND m_screenshotToastHwnd = nullptr;
    HWND m_resolutionToastHwnd = nullptr;
    Renderer m_renderer;
    Renderer m_screenshotToastRenderer;
    Renderer m_resolutionToastRenderer;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_screenshotToastPreview;
    std::wstring m_resolutionToastGameName;
    ResolutionProfile m_resolutionToastCurrent;
    ResolutionProfile m_resolutionToastProposed;
    std::wstring m_resolutionToastStatus;
    bool m_resolutionToastSupported = false;
    bool m_resolutionToastHoverApply = false;
    bool m_resolutionToastHoverDismiss = false;
    ProfileManager& m_profileManager;
    DisplayManager& m_displayManager;
    ResolutionManager& m_resolutionManager;

    ProfileListView m_listView;
    ProfileEditorView m_editorView;
    AddAppDialog m_addDialog;
    SettingsView m_settingsView;
    TrayIcon* m_pTrayIcon = nullptr;

    Section m_section = Section::GameProfiles;
    DetailMode m_detailMode = DetailMode::Empty;
    std::wstring m_selectedProfileId;

    bool m_isPaused = false;
    bool m_shownTrayNotice = false;
    std::wstring m_activeProfileName = L"Windows Default Desktop";

    Preferences m_preferences;
    std::thread m_updateThread;
    std::atomic_bool m_updateJobRunning{ false };
    std::atomic_bool m_updateCancelRequested{ false };
    std::optional<ReleaseInfo> m_availableRelease;
    std::wstring m_updateNotesVersion;
    std::wstring m_updateNotesTitle;
    std::wstring m_updateNotesBody;
    std::wstring m_updateDownloadPath;
    bool m_updateNotificationVisible = false;
    bool m_updateShowingNotes = false;
    bool m_updateNoticeDismissed = false;
    int m_updateDialogHover = -1;
    int m_updateNotesScroll = 0;

    bool m_hoverNavProfiles = false;
    bool m_hoverNavMonitor = false;
    bool m_hoverNavSettings = false;
    bool m_hoverPauseButton = false;
    bool m_hoverHeaderPill = false;
    bool m_hoverResetButton = false;
    bool m_hoverEmptyCta = false;
    bool m_resetPressed = false;
    bool m_emptyCtaPressed = false;
    bool m_trackingMouse = false;
    int m_hoverTertiaryTab = -1;

    Anim m_navSlide;
    Anim m_navProfilesHover;
    Anim m_navMonitorHover;
    Anim m_navSettingsHover;
    Anim m_pauseHover;
    Anim m_pillHover;
    Anim m_resetHover;
    Anim m_emptyCtaHover;
    std::array<Anim, 5> m_tertiaryTabHover;
    Anim m_tertiaryTabIndicator;

    Anim m_toastAnim;
    std::wstring m_toastTitle;
    std::wstring m_toastBody;
    ULONGLONG m_toastExpire = 0;
    Anim m_screenshotToastAnim;
    bool m_screenshotToastIsError = false;
    bool m_screenshotToastImageCopied = false;
    std::wstring m_screenshotToastImagePath;
    RECT m_screenshotToastTargetRect{};
    ULONGLONG m_lastScreenshotToastTick = 0;

    bool m_animTimerActive = false;
    ULONGLONG m_lastAnimTick = 0;

    // Cached bitmap of the app icon (Icon.ico) for the sidebar brand mark.
    // Created lazily on first paint, invalidated on device loss.
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_logoBitmap;

    PauseToggleCallback m_onPauseToggle;
    ActionCallback m_onResetDisplay;
    ActionCallback m_onExitRequested;
    ResolutionApplyCallback m_onResolutionApply;
    ActionCallback m_onResolutionDismiss;
    ResolutionModeChangedCallback m_onResolutionModeChanged;

    static inline MainWindow* s_instance = nullptr;
};

} // namespace prismshift

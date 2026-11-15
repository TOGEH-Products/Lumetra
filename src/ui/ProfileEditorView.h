#pragma once

#include "profiles/ColorProfile.h"
#include "ui/Anim.h"
#include "ui/Renderer.h"
#include "ui/HotkeyCapture.h"
#include <windows.h>
#include <array>
#include <functional>
#include <string>
#include <vector>

namespace prismshift {

// Detail pane for a single profile.
class ProfileEditorView {
public:
    using SaveCallback = std::function<void(const ColorProfile& updated)>;
    using PreviewCallback = std::function<void(const ColorProfile& preview)>;
    using CompareCallback = std::function<void(bool comparing)>; // true = baseline
    using HotkeyCallback = std::function<void(const std::wstring& profileId)>;
    using ActionCallback = std::function<void()>;
    using ScalingSupportCallback = std::function<bool(int width, int height)>;

    enum class EditorTab {
        Display,
        Channels,
        Color,
        ShortcutManager,
        Resolution
    };

    ProfileEditorView();
    ~ProfileEditorView() = default;

    void SetCallbacks(SaveCallback onSave, PreviewCallback onPreview,
                      CompareCallback onCompare = nullptr, HotkeyCallback onHotkey = nullptr) {
        m_onSave = std::move(onSave);
        m_onPreview = std::move(onPreview);
        m_onCompare = std::move(onCompare);
        m_onHotkey = std::move(onHotkey);
    }

    // Non-owning monitor baseline used for the hold-to-compare button.
    void SetMonitorProfile(const ColorProfile* monitorProfile) { m_monitorProfile = monitorProfile; }

    // Color values stay editable even when the engine cannot preview their
    // fullscreen matrix effect.
    void SetColorEffectsAvailable(bool available) { m_colorEffectsAvailable = available; }
    [[nodiscard]] bool AreColorEffectsAvailable() const { return m_colorEffectsAvailable; }

    [[nodiscard]] bool IsComparing() const { return m_isComparing; }
    [[nodiscard]] bool IsEditingName() const { return m_editingName; }
    [[nodiscard]] bool IsDirty() const;
    [[nodiscard]] bool IsListeningForHotkey() const;
    [[nodiscard]] EditorTab GetActiveTab() const { return m_activeTab; }
    void SetTab(EditorTab tab);

    void LoadProfile(const ColorProfile& profile);
    void SetAvailableResolutions(std::vector<ResolutionProfile> resolutions);
    void SetResolutionPickerCallback(ActionCallback callback) {
        m_onResolutionPicker = std::move(callback);
    }
    void SetScalingPickerCallback(ActionCallback callback) {
        m_onScalingPicker = std::move(callback);
    }
    void SetScalingSupportCallback(ScalingSupportCallback callback) {
        m_onScalingSupport = std::move(callback);
    }
    void SetResolutionProfile(const ResolutionProfile& profile) {
        m_editingProfile.resolutionProfile = profile;
    }
    void SetResolutionScaling(ResolutionScaling scaling) {
        if (m_editingProfile.resolutionProfile) {
            m_editingProfile.resolutionProfile->scaling = scaling;
        }
    }
    void SetScalingModesSupported(bool supported) {
        m_scalingModesSupported = supported;
    }
    [[nodiscard]] const ColorProfile& GetEditingProfile() const { return m_editingProfile; }

    // Commits the current edits through the save callback. Returns false when
    // there is nothing to save or no callback is wired.
    bool Save();

    void Paint(Renderer& r, const RECT& rcBounds);

    bool OnMouseMove(POINT pt, const RECT& rcBounds);
    bool OnLButtonDown(POINT pt, const RECT& rcBounds);
    bool OnLButtonUp(POINT pt, const RECT& rcBounds);
    bool OnChar(wchar_t ch);
    bool OnKeyDown(WPARAM wParam);
    bool OnKeyUp(WPARAM wParam);

    bool TickAnimations(float dt);
    [[nodiscard]] bool HasActiveAnimations() const;
    void ResetHover();

private:
    enum class SliderId {
        None,
        Brightness,
        Contrast,
        Gamma,
        Red,
        Green,
        Blue,
        Saturation,
        Vibrance,
        Hue
    };

    struct SliderLayout {
        SliderId id = SliderId::None;
        RECT rcRow{};
        RECT rcTrack{};
        RECT rcThumb{};
        RECT rcValue{};
        std::wstring label;
        std::wstring valueStr;
        std::wstring minStr;
        std::wstring maxStr;
        double minVal = 0.0;
        double maxVal = 1.0;
        double currentVal = 0.0;
        double neutralVal = 0.0;
    };

    [[nodiscard]] std::array<SliderId, 3> GetActiveSliders(size_t& count) const;
    [[nodiscard]] SliderLayout GetSliderLayout(SliderId id, int index,
                                               const RECT& rcBounds) const;
    [[nodiscard]] RECT GetContentRect(const RECT& rcBounds) const;
    [[nodiscard]] RECT GetCardRect(const RECT& rcBounds) const;
    [[nodiscard]] RECT GetToggleRect(const RECT& rcBounds) const;
    [[nodiscard]] RECT GetNameRect(const RECT& rcBounds) const;
    [[nodiscard]] RECT GetAvatarRect(const RECT& rcBounds) const;
    [[nodiscard]] RECT GetResetBtnRect(const RECT& rcBounds) const;
    [[nodiscard]] RECT GetSaveBtnRect(const RECT& rcBounds) const;
    [[nodiscard]] RECT GetCompareBtnRect(const RECT& rcBounds) const;
    [[nodiscard]] RECT GetHotkeyBtnRect(const RECT& rcBounds) const;
    [[nodiscard]] RECT GetScreenshotHotkeyRect(const RECT& rcBounds) const;
    [[nodiscard]] bool CanCompare() const;
    [[nodiscard]] bool ShowCompare(const RECT& rcBounds) const;

    void DrawHero(Renderer& r, const RECT& rcBounds);
    void DrawSlider(Renderer& r, const SliderLayout& layout, int sliderIndex);
    void DrawFooter(Renderer& r, const RECT& rcBounds);
    bool ApplySliderValue(SliderId id, double newVal);
    void FlushPendingSliderPreview();

    ColorProfile m_originalProfile;
    ColorProfile m_editingProfile;
    const ColorProfile* m_monitorProfile = nullptr;

    SaveCallback m_onSave;
    PreviewCallback m_onPreview;
    CompareCallback m_onCompare;
    HotkeyCallback m_onHotkey;
    ActionCallback m_onResolutionPicker;
    ActionCallback m_onScalingPicker;
    ScalingSupportCallback m_onScalingSupport;

    EditorTab m_activeTab = EditorTab::Display;
    SliderId m_draggingSlider = SliderId::None;

    int m_hoverSlider = -1;
    bool m_hoverToggle = false;
    bool m_hoverName = false;
    bool m_hoverAvatar = false;
    bool m_hoverReset = false;
    bool m_hoverSave = false;
    bool m_hoverCompare = false;
    bool m_hoverHotkey = false;
    bool m_editingName = false;
    bool m_isComparing = false;
    bool m_colorEffectsAvailable = false;
    bool m_sliderPreviewPending = false;
    std::vector<ResolutionProfile> m_availableResolutions;
    bool m_scalingModesSupported = false;

    std::array<Anim, 3> m_sliderHover;
    Anim m_toggleAnim;
    Anim m_toggleHover;
    Anim m_nameHover;
    Anim m_avatarHover;
    Anim m_resetHover;
    Anim m_saveHover;
    Anim m_compareHover;
    Anim m_hotkeyHover;
    Anim m_dirtyAnim;

    // Hotkey capture component
    HotkeyCapture m_screenshotHotkeyCapture;
};

} // namespace prismshift

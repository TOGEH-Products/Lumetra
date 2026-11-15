#include "ui/ProfileEditorView.h"
#include "ui/UITheme.h"
#include "ui/Controls.h"
#include "ui/Icons.h"
#include <shlobj.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace prismshift {

namespace {
constexpr int PAD = 32;
constexpr int HERO_TOP = 24;
constexpr int AVATAR = 52;
constexpr int CARD_TOP = 128;
constexpr int CARD_HEADER = 52;
constexpr int SLIDER_ROW = 96;
constexpr int CARD_PAD = 14;
constexpr int FOOTER_H = 84;
constexpr int RESOLUTION_CARD_H = 444;
constexpr int RESOLUTION_PRESET_LIMIT = 6;
constexpr int RESOLUTION_GRID_COLUMNS = 3;
constexpr int STRETCHED_SUGGESTION_TOP = 274;
constexpr int SCALING_SECTION_TOP = 340;

struct StretchedSuggestion {
    int width;
    int height;
    const wchar_t* label;
    const wchar_t* detail;
};

constexpr StretchedSuggestion STRETCHED_SUGGESTIONS[] = {
    { 1440, 1080, L"Sharper", L"More detail" },
    { 1280, 960, L"Balanced", L"Classic 4:3" },
    { 1024, 768, L"More FPS", L"Lightest load" }
};

RECT ResolutionPresetRect(const RECT& card, int index) {
    const int left = card.left + 20;
    const int right = card.right - 20;
    const int gap = 8;
    const int width = (right - left - gap * (RESOLUTION_GRID_COLUMNS - 1)) /
                      RESOLUTION_GRID_COLUMNS;
    const int column = index % RESOLUTION_GRID_COLUMNS;
    const int row = index / RESOLUTION_GRID_COLUMNS;
    const int top = card.top + 188 + row * 40;
    const int x = left + column * (width + gap);
    return RECT{ x, top, x + width, top + 32 };
}

RECT ResolutionScalingRect(const RECT& card, int index) {
    const int left = card.left + 20;
    const int right = card.right - 20;
    const int gap = 8;
    const int width = (right - left - gap * 2) / 3;
    const int x = left + index * (width + gap);
    const int top = card.top + SCALING_SECTION_TOP + 26;
    return RECT{ x, top, x + width, top + 34 };
}

std::wstring MonogramFor(const ColorProfile& profile) {
    if (profile.isDefault || profile.id == L"monitor") return L"M";
    for (wchar_t ch : profile.name) {
        if (!std::iswspace(ch)) return std::wstring(1, static_cast<wchar_t>(std::towupper(ch)));
    }
    return L"?";
}

std::wstring SignedPercent(double value) {
    int v = static_cast<int>(std::round(value));
    return (v > 0 ? L"+" : L"") + std::to_wstring(v) + L"%";
}

std::wstring Fixed2(double value) {
    std::wostringstream ss;
    ss << std::fixed << std::setprecision(2) << value;
    return ss.str();
}

std::wstring SignedInt(int value, const wchar_t* suffix = L"") {
    return (value > 0 ? L"+" : L"") + std::to_wstring(value) + suffix;
}
} // namespace

ProfileEditorView::ProfileEditorView() {
    for (Anim& a : m_sliderHover) a.speed = 20.0f;
    m_toggleAnim.speed = 17.0f;
    m_toggleHover.speed = 18.0f;
    m_nameHover.speed = 18.0f;
    m_avatarHover.speed = 18.0f;
    m_resetHover.speed = 18.0f;
    m_saveHover.speed = 18.0f;
    m_compareHover.speed = 18.0f;
    m_hotkeyHover.speed = 18.0f;
    m_dirtyAnim.speed = 18.0f;
}

void ProfileEditorView::LoadProfile(const ColorProfile& profile) {
    m_screenshotHotkeyCapture.CancelListen();
    m_originalProfile = profile;
    m_editingProfile = profile;
    m_draggingSlider = SliderId::None;
    m_editingName = false;
    m_activeTab = EditorTab::Display;
    m_isComparing = false;

    // Initialize hotkey capture with current profile hotkey
    m_screenshotHotkeyCapture.SetHotkey(profile.screenshotHotkey);
    m_screenshotHotkeyCapture.SetChangeCallback([this](const std::wstring& newHotkey) {
        m_editingProfile.screenshotHotkey = newHotkey;
        if (m_onPreview) m_onPreview(m_editingProfile);
    });

    m_toggleAnim.Set(profile.enabled ? 1.0f : 0.0f);
    m_dirtyAnim.Set(0.0f);
    for (Anim& a : m_sliderHover) a.Set(0.0f);
    m_sliderPreviewPending = false;
    ResetHover();
}

void ProfileEditorView::SetAvailableResolutions(
    std::vector<ResolutionProfile> resolutions) {
    static constexpr std::pair<int, int> popular[] = {
        { 1280, 720 }, { 1366, 768 }, { 1600, 900 },
        { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 },
        { 1024, 768 }, { 1280, 960 }, { 1440, 1080 }
    };
    m_availableResolutions.clear();
    for (const auto& preset : popular) {
        const auto found = std::find_if(
            resolutions.begin(), resolutions.end(),
            [&preset](const ResolutionProfile& item) {
                return item.width == preset.first && item.height == preset.second;
            });
        if (found != resolutions.end()) {
            m_availableResolutions.push_back(*found);
        }
    }
    for (const auto& mode : resolutions) {
        const bool alreadyAdded = std::any_of(
            m_availableResolutions.begin(), m_availableResolutions.end(),
            [&mode](const ResolutionProfile& item) {
                return item.width == mode.width && item.height == mode.height;
            });
        if (!alreadyAdded) {
            m_availableResolutions.push_back(mode);
        }
    }
    if (m_editingProfile.resolutionProfile &&
        std::none_of(m_availableResolutions.begin(), m_availableResolutions.end(),
                     [this](const ResolutionProfile& mode) {
                         return mode.width == m_editingProfile.resolutionProfile->width &&
                                mode.height == m_editingProfile.resolutionProfile->height;
                     })) {
        const auto current = std::find_if(
            resolutions.begin(), resolutions.end(),
            [this](const ResolutionProfile& mode) {
                return mode.width == m_editingProfile.resolutionProfile->width &&
                       mode.height == m_editingProfile.resolutionProfile->height;
            });
        if (current != resolutions.end()) {
            m_availableResolutions.push_back(*current);
        }
    }
}

bool ProfileEditorView::IsDirty() const {
    const auto nearly = [](double a, double b) { return std::abs(a - b) < 1e-6; };
    return m_editingProfile.name != m_originalProfile.name ||
           m_editingProfile.enabled != m_originalProfile.enabled ||
           m_editingProfile.screenshotHotkey != m_originalProfile.screenshotHotkey ||
           m_editingProfile.resolutionProfile != m_originalProfile.resolutionProfile ||
           !nearly(m_editingProfile.brightness, m_originalProfile.brightness) ||
           !nearly(m_editingProfile.contrast, m_originalProfile.contrast) ||
           !nearly(m_editingProfile.gammaR, m_originalProfile.gammaR) ||
           !nearly(m_editingProfile.gammaG, m_originalProfile.gammaG) ||
           !nearly(m_editingProfile.gammaB, m_originalProfile.gammaB) ||
           !nearly(m_editingProfile.saturation, m_originalProfile.saturation) ||
           m_editingProfile.vibrance != m_originalProfile.vibrance ||
           m_editingProfile.hue != m_originalProfile.hue;
}

bool ProfileEditorView::Save() {
    if (!m_onSave || !IsDirty()) return false;
    FlushPendingSliderPreview();
    m_editingName = false;
    m_isComparing = false;
    m_originalProfile = m_editingProfile;
    m_onSave(m_editingProfile);
    m_dirtyAnim.To(0.0f);
    return true;
}

void ProfileEditorView::ResetHover() {
    m_hoverSlider = -1;
    m_hoverToggle = false;
    m_hoverName = false;
    m_hoverAvatar = false;
    m_hoverReset = false;
    m_hoverSave = false;
    m_hoverCompare = false;
    m_hoverHotkey = false;

    for (Anim& a : m_sliderHover) a.To(0.0f);
    m_toggleHover.To(0.0f);
    m_nameHover.To(0.0f);
    m_avatarHover.To(0.0f);
    m_resetHover.To(0.0f);
    m_saveHover.To(0.0f);
    m_compareHover.To(0.0f);
    m_hotkeyHover.To(0.0f);
}

void ProfileEditorView::SetTab(EditorTab tab) {
    if (m_screenshotHotkeyCapture.IsListening()) {
        m_screenshotHotkeyCapture.CancelListen();
    }
    if (m_activeTab == tab) return;
    FlushPendingSliderPreview();
    m_activeTab = tab;
    m_draggingSlider = SliderId::None;
    m_hoverSlider = -1;
}

bool ProfileEditorView::CanCompare() const {
    return m_monitorProfile != nullptr && !m_editingProfile.isDefault &&
           m_editingProfile.id != L"monitor";
}

RECT ProfileEditorView::GetContentRect(const RECT& rcBounds) const {
    return RECT{ rcBounds.left + PAD, rcBounds.top, rcBounds.right - PAD, rcBounds.bottom };
}

RECT ProfileEditorView::GetCardRect(const RECT& rcBounds) const {
    RECT content = GetContentRect(rcBounds);
    if (m_activeTab == EditorTab::Resolution) {
        return RECT{ content.left, rcBounds.top + CARD_TOP,
                     content.right, rcBounds.top + CARD_TOP + RESOLUTION_CARD_H };
    }
    return RECT{ content.left, rcBounds.top + CARD_TOP,
                 content.right, rcBounds.top + CARD_TOP + CARD_HEADER +
                     3 * SLIDER_ROW + CARD_PAD };
}

RECT ProfileEditorView::GetToggleRect(const RECT& rcBounds) const {
    RECT content = GetContentRect(rcBounds);
    const int w = 44;
    const int h = 24;
    return RECT{ content.right - w, rcBounds.top + 36, content.right,
                 rcBounds.top + 36 + h };
}

RECT ProfileEditorView::GetAvatarRect(const RECT& rcBounds) const {
    RECT content = GetContentRect(rcBounds);
    return RECT{ content.left, rcBounds.top + HERO_TOP,
                 content.left + AVATAR, rcBounds.top + HERO_TOP + AVATAR };
}

RECT ProfileEditorView::GetNameRect(const RECT& rcBounds) const {
    RECT content = GetContentRect(rcBounds);
    const int left = content.left + AVATAR + 16;
    const int right = content.right - 190;
    return RECT{ left, rcBounds.top + HERO_TOP + 2, right,
                 rcBounds.top + HERO_TOP + 32 };
}

RECT ProfileEditorView::GetResetBtnRect(const RECT& rcBounds) const {
    RECT content = GetContentRect(rcBounds);
    return RECT{ content.left, rcBounds.bottom - 66, content.left + 112,
                 rcBounds.bottom - 28 };
}

RECT ProfileEditorView::GetSaveBtnRect(const RECT& rcBounds) const {
    RECT content = GetContentRect(rcBounds);
    return RECT{ content.right - 156, rcBounds.bottom - 66, content.right,
                 rcBounds.bottom - 28 };
}

RECT ProfileEditorView::GetCompareBtnRect(const RECT& rcBounds) const {
    RECT reset = GetResetBtnRect(rcBounds);
    RECT save = GetSaveBtnRect(rcBounds);
    const int w = 156;
    const int left = reset.right + (save.left - reset.right - w) / 2;
    return RECT{ left, rcBounds.bottom - 66, left + w, rcBounds.bottom - 28 };
}

RECT ProfileEditorView::GetHotkeyBtnRect(const RECT& rcBounds) const {
    RECT reset = GetResetBtnRect(rcBounds);
    const int gap = 12;
    const int w = 140;
    return RECT{ reset.right + gap, rcBounds.bottom - 66,
                 reset.right + gap + w, rcBounds.bottom - 28 };
}

RECT ProfileEditorView::GetScreenshotHotkeyRect(const RECT& rcBounds) const {
    RECT reset = GetResetBtnRect(rcBounds);
    const int gap = 12;
    const int w = 180; // Wider for the hotkey capture component
    return RECT{ reset.right + gap, rcBounds.bottom - 66,
                 reset.right + gap + w, rcBounds.bottom - 28 };
}

bool ProfileEditorView::ShowCompare(const RECT& rcBounds) const {
    if (!CanCompare()) return false;
    RECT reset = GetResetBtnRect(rcBounds);
    RECT save = GetSaveBtnRect(rcBounds);
    return (save.left - reset.right) >= 180;
}

std::array<ProfileEditorView::SliderId, 3>
ProfileEditorView::GetActiveSliders(size_t& count) const {
    std::array<SliderId, 3> sliders{};
    count = 0;
    switch (m_activeTab) {
        case EditorTab::Display:
            sliders = { SliderId::Brightness, SliderId::Contrast, SliderId::Gamma };
            count = sliders.size();
            break;
        case EditorTab::Channels:
            sliders = { SliderId::Red, SliderId::Green, SliderId::Blue };
            count = sliders.size();
            break;
        case EditorTab::Color:
            sliders = { SliderId::Saturation, SliderId::Vibrance, SliderId::Hue };
            count = sliders.size();
            break;
        case EditorTab::ShortcutManager:
        case EditorTab::Resolution:
            break;
        default:
            break;
    }
    return sliders;
}

ProfileEditorView::SliderLayout ProfileEditorView::GetSliderLayout(
    SliderId id, int index, const RECT& rcBounds) const {
    SliderLayout sl;
    sl.id = id;

    RECT card = GetCardRect(rcBounds);
    const int rowTop = card.top + CARD_HEADER + index * SLIDER_ROW;
    sl.rcRow = RECT{ card.left + 20, rowTop, card.right - 20, rowTop + SLIDER_ROW };
    sl.rcValue = RECT{ sl.rcRow.right - 72, rowTop + 12, sl.rcRow.right, rowTop + 38 };
    sl.rcTrack = RECT{ sl.rcRow.left, rowTop + 52, sl.rcRow.right, rowTop + 56 };

    switch (id) {
        case SliderId::Brightness: {
            sl.label = L"Brightness";
            sl.minVal = -50.0;
            sl.maxVal = 50.0;
            sl.neutralVal = 0.0;
            sl.currentVal = (m_editingProfile.brightness - 1.0) * 100.0;
            sl.valueStr = SignedPercent(sl.currentVal);
            sl.minStr = L"-50%";
            sl.maxStr = L"+50%";
            break;
        }
        case SliderId::Contrast: {
            sl.label = L"Contrast";
            sl.minVal = -50.0;
            sl.maxVal = 50.0;
            sl.neutralVal = 0.0;
            sl.currentVal = (m_editingProfile.contrast - 1.0) * 100.0;
            sl.valueStr = SignedPercent(sl.currentVal);
            sl.minStr = L"-50%";
            sl.maxStr = L"+50%";
            break;
        }
        case SliderId::Gamma: {
            sl.label = L"Midtone gamma";
            sl.minVal = 0.50;
            sl.maxVal = 2.00;
            sl.neutralVal = 1.00;
            sl.currentVal = m_editingProfile.gammaR;
            sl.valueStr = Fixed2(sl.currentVal);
            sl.minStr = L"0.50";
            sl.maxStr = L"2.00";
            break;
        }
        case SliderId::Red: {
            sl.label = L"Red gamma";
            sl.minVal = 0.50;
            sl.maxVal = 2.00;
            sl.neutralVal = 1.00;
            sl.currentVal = m_editingProfile.gammaR;
            sl.valueStr = Fixed2(sl.currentVal);
            sl.minStr = L"0.50";
            sl.maxStr = L"2.00";
            break;
        }
        case SliderId::Green: {
            sl.label = L"Green gamma";
            sl.minVal = 0.50;
            sl.maxVal = 2.00;
            sl.neutralVal = 1.00;
            sl.currentVal = m_editingProfile.gammaG;
            sl.valueStr = Fixed2(sl.currentVal);
            sl.minStr = L"0.50";
            sl.maxStr = L"2.00";
            break;
        }
        case SliderId::Blue: {
            sl.label = L"Blue gamma";
            sl.minVal = 0.50;
            sl.maxVal = 2.00;
            sl.neutralVal = 1.00;
            sl.currentVal = m_editingProfile.gammaB;
            sl.valueStr = Fixed2(sl.currentVal);
            sl.minStr = L"0.50";
            sl.maxStr = L"2.00";
            break;
        }
        case SliderId::Saturation: {
            sl.label = L"Saturation";
            sl.minVal = 0.0;
            sl.maxVal = 200.0;
            sl.neutralVal = 100.0;
            sl.currentVal = m_editingProfile.saturation * 100.0;
            sl.valueStr = std::to_wstring(static_cast<int>(std::round(sl.currentVal))) + L"%";
            sl.minStr = L"0%";
            sl.maxStr = L"200%";
            break;
        }
        case SliderId::Vibrance: {
            sl.label = L"Vibrance";
            sl.minVal = -100.0;
            sl.maxVal = 100.0;
            sl.neutralVal = 0.0;
            sl.currentVal = static_cast<double>(m_editingProfile.vibrance);
            sl.valueStr = SignedInt(m_editingProfile.vibrance);
            sl.minStr = L"-100";
            sl.maxStr = L"+100";
            break;
        }
        case SliderId::Hue: {
            sl.label = L"Hue rotation";
            sl.minVal = -180.0;
            sl.maxVal = 180.0;
            sl.neutralVal = 0.0;
            sl.currentVal = static_cast<double>(m_editingProfile.hue);
            sl.valueStr = SignedInt(m_editingProfile.hue, L"\u00B0");
            sl.minStr = L"-180\u00B0";
            sl.maxStr = L"+180\u00B0";
            break;
        }
        default:
            break;
    }

    const double range = sl.maxVal - sl.minVal;
    double ratio = range > 0.0 ? (sl.currentVal - sl.minVal) / range : 0.0;
    ratio = std::clamp(ratio, 0.0, 1.0);
    const int cx = sl.rcTrack.left +
                   static_cast<int>(ratio * (sl.rcTrack.right - sl.rcTrack.left) + 0.5);
    sl.rcThumb = RECT{ cx - 9, sl.rcTrack.top - 5, cx + 9, sl.rcTrack.bottom + 5 };
    return sl;
}

bool ProfileEditorView::ApplySliderValue(SliderId id, double newVal) {
    bool changed = false;
    switch (id) {
        case SliderId::Brightness: {
            const double value = 1.0 + newVal / 100.0;
            changed = m_editingProfile.brightness != value;
            m_editingProfile.brightness = value;
            break;
        }
        case SliderId::Contrast: {
            const double value = 1.0 + newVal / 100.0;
            changed = m_editingProfile.contrast != value;
            m_editingProfile.contrast = value;
            break;
        }
        case SliderId::Gamma:
            changed = m_editingProfile.gammaR != newVal ||
                      m_editingProfile.gammaG != newVal ||
                      m_editingProfile.gammaB != newVal;
            m_editingProfile.gammaR = newVal;
            m_editingProfile.gammaG = newVal;
            m_editingProfile.gammaB = newVal;
            break;
        case SliderId::Red:
            changed = m_editingProfile.gammaR != newVal;
            m_editingProfile.gammaR = newVal;
            break;
        case SliderId::Green:
            changed = m_editingProfile.gammaG != newVal;
            m_editingProfile.gammaG = newVal;
            break;
        case SliderId::Blue:
            changed = m_editingProfile.gammaB != newVal;
            m_editingProfile.gammaB = newVal;
            break;
        case SliderId::Saturation: {
            const double value = newVal / 100.0;
            changed = m_editingProfile.saturation != value;
            m_editingProfile.saturation = value;
            break;
        }
        case SliderId::Vibrance: {
            const int value = static_cast<int>(std::round(newVal));
            changed = m_editingProfile.vibrance != value;
            m_editingProfile.vibrance = value;
            break;
        }
        case SliderId::Hue: {
            const int value = static_cast<int>(std::round(newVal));
            changed = m_editingProfile.hue != value;
            m_editingProfile.hue = value;
            break;
        }
        default: return false;
    }
    if (!changed) return false;
    m_editingProfile.RecomputeRamp();
    m_sliderPreviewPending = true;
    return true;
}

void ProfileEditorView::FlushPendingSliderPreview() {
    if (!m_sliderPreviewPending) return;
    m_sliderPreviewPending = false;
    if (m_onPreview) m_onPreview(m_editingProfile);
}

void ProfileEditorView::DrawHero(Renderer& r, const RECT& rcBounds) {
    RECT content = GetContentRect(rcBounds);
    const bool isMonitor = m_editingProfile.isDefault || m_editingProfile.id == L"monitor";

    RECT rcAvatar = GetAvatarRect(rcBounds);

    // Avatar background card
    if (isMonitor) {
        UITheme::DrawRoundedCard(r, rcAvatar, 14, UITheme::INVERSE, UITheme::INVERSE);
    } else {
        UITheme::DrawElevatedCard(r, rcAvatar, 14, UITheme::BG_RAISED,
                                  UITheme::BORDER_STRONG, 0.40f, 12.0f, 4.0f);
    }

    // Draw the actual icon content (icon glyph or custom image or monogram)
    // Try to load custom icon from ProfileIcons/{id}/icon.* folder
    Microsoft::WRL::ComPtr<ID2D1Bitmap> customIcon;
    if (!isMonitor) {
        wchar_t appData[MAX_PATH];
        if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData) == S_OK) {
            std::wstring iconDir = std::wstring(appData) + L"\\Lumetra\\ProfileIcons\\" + m_editingProfile.id + L"\\";
            std::vector<std::wstring> iconPaths = {
                iconDir + L"icon.png",
                iconDir + L"icon.jpg",
                iconDir + L"icon.jpeg"
            };
            for (const auto& path : iconPaths) {
                customIcon = r.LoadBitmapFromFile(path);
                if (customIcon) break;
            }
        }
    }

    if (isMonitor) {
        Icons::Draw(r, Icons::Monitor, rcAvatar, 22, UITheme::INVERSE_TEXT);
    } else if (customIcon) {
        const float pad = 6.0f;
        D2D1_RECT_F destF = D2D1::RectF(
            static_cast<float>(rcAvatar.left) + pad,
            static_cast<float>(rcAvatar.top) + pad,
            static_cast<float>(rcAvatar.right) - pad,
            static_cast<float>(rcAvatar.bottom) - pad);
        r.DrawBitmapRounded(customIcon.Get(), destF, 10.0f);
    } else {
        // Fallback: monogram
        TextStyle monoStyle = Typography::PageTitle();
        monoStyle.sizeDip = 20.0f;
        r.DrawText(MonogramFor(m_editingProfile), monoStyle,
                   static_cast<float>(rcAvatar.left), static_cast<float>(rcAvatar.top),
                   static_cast<float>(rcAvatar.right - rcAvatar.left),
                   static_cast<float>(rcAvatar.bottom - rcAvatar.top),
                   UITheme::TEXT_PRIMARY,
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // Hover overlay for non-monitor profiles: show edit icon indicator
    if (!isMonitor && m_hoverAvatar) {
        const float hoverAlpha = std::clamp(m_avatarHover.value, 0.0f, 1.0f);
        if (hoverAlpha > 0.01f) {
            // Dark overlay with rounded corners
            const COLORREF overlayColor = RGB(0, 0, 0);

            D2D1_RECT_F avatarF = D2D1::RectF(
                static_cast<float>(rcAvatar.left),
                static_cast<float>(rcAvatar.top),
                static_cast<float>(rcAvatar.right),
                static_cast<float>(rcAvatar.bottom));

            // Draw semi-transparent rounded overlay
            r.DrawRoundedRect(avatarF, 14.0f, 14.0f, overlayColor, overlayColor, 0.0f);

            // Adjust opacity by drawing multiple times (simple but effective)
            if (hoverAlpha > 0.3f) {
                r.DrawRoundedRect(avatarF, 14.0f, 14.0f, overlayColor, overlayColor, 0.0f);
            }

            // Edit icon in center
            RECT rcEditIcon = {
                rcAvatar.left + (AVATAR - 20) / 2,
                rcAvatar.top + (AVATAR - 20) / 2,
                rcAvatar.left + (AVATAR + 20) / 2,
                rcAvatar.top + (AVATAR + 20) / 2
            };
            Icons::Draw(r, Icons::Edit, rcEditIcon, 15,
                        RGB(static_cast<BYTE>(255.0f * hoverAlpha),
                            static_cast<BYTE>(255.0f * hoverAlpha),
                            static_cast<BYTE>(255.0f * hoverAlpha)));
        }
    }

    RECT rcName = GetNameRect(rcBounds);
    const int nameRight = rcName.right - (m_hoverName && !m_editingName ? 20 : 0);

    if (m_editingName) {
        RECT rcInput = { rcName.left - 8, rcName.top - 4, rcName.right,
                         rcName.bottom + 4 };
        UITheme::DrawRoundedCard(r, rcInput, 8, UITheme::BG_INPUT,
                                 UITheme::BORDER_STRONG);
        RECT rcRing = { rcInput.left - 2, rcInput.top - 2, rcInput.right + 2,
                        rcInput.bottom + 2 };
        UITheme::DrawFocusRing(r, rcRing, 10,
                               UITheme::Mix(UITheme::BG_SURFACE, UITheme::BORDER_STRONG,
                                            m_nameHover.value));

        TextStyle nameStyle = Typography::PageTitle();
        nameStyle.sizeDip = 18.0f;
        RECT rcText = { rcInput.left + 9, rcName.top - 2, rcInput.right - 9,
                        rcName.bottom + 2 };
        r.DrawText(m_editingProfile.name.c_str(), nameStyle,
                   static_cast<float>(rcText.left), static_cast<float>(rcText.top),
                   static_cast<float>(rcText.right - rcText.left),
                   static_cast<float>(rcText.bottom - rcText.top),
                   UITheme::TEXT_PRIMARY,
                   DWRITE_TEXT_ALIGNMENT_LEADING,
                   DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        const float textWidth =
            r.MeasureText(m_editingProfile.name.c_str(), nameStyle);
        int caretX =
            static_cast<int>(static_cast<float>(rcText.left) + textWidth + 2.0f);
        caretX = std::min(caretX, static_cast<int>(rcText.right) - 2);
        RECT rcCaret = { caretX, rcText.top + 4, caretX + 1, rcText.bottom - 4 };
        UITheme::FillSolidRect(r, rcCaret, UITheme::TEXT_PRIMARY);
    } else {
        TextStyle nameStyle = Typography::PageTitle();
        nameStyle.sizeDip = 18.0f;
        nameStyle.trim = true;
        RECT rcText = { rcName.left, rcName.top, nameRight, rcName.bottom };
        r.DrawText(m_editingProfile.name.c_str(), nameStyle,
                   static_cast<float>(rcText.left), static_cast<float>(rcText.top),
                   static_cast<float>(rcText.right - rcText.left),
                   static_cast<float>(rcText.bottom - rcText.top),
                   UITheme::TEXT_PRIMARY);

        const float textWidth =
            r.MeasureText(m_editingProfile.name.c_str(), nameStyle);
        const int textRight =
            std::min(nameRight,
                     static_cast<int>(rcName.left) +
                         static_cast<int>(textWidth));

        if (!isMonitor && m_hoverName) {
            RECT rcEdit = { std::min(textRight + 8, static_cast<int>(rcName.right) - 14),
                            rcName.top + 7,
                            std::min(textRight + 22, static_cast<int>(rcName.right)),
                            rcName.top + 21 };
            Icons::Draw(r, Icons::Edit, rcEdit, 11, UITheme::TEXT_TERTIARY);
        }

        if (IsDirty()) {
            const float t = std::clamp(m_dirtyAnim.value, 0.0f, 1.0f);
            int dotX = textRight + (m_hoverName ? 28 : 10);
            dotX = std::min(dotX, static_cast<int>(rcName.right) - 6);
            UITheme::DrawDot(r, dotX, (rcName.top + rcName.bottom) / 2, 3,
                             UITheme::Mix(UITheme::BG_CANVAS, UITheme::INVERSE, t));
        }
    }

    // Subtitle: target executable, or fallback explanation for monitor.
    std::wstring subtitle;
    if (isMonitor) {
        subtitle = L"Fallback for every application";
    } else if (!m_editingProfile.targetProcesses.empty()) {
        subtitle = m_editingProfile.targetProcesses.front();
        if (m_editingProfile.targetProcesses.size() > 1) {
            subtitle += L"  +" + std::to_wstring(m_editingProfile.targetProcesses.size() - 1) + L" more";
        }
    } else if (!m_editingProfile.executablePath.empty()) {
        subtitle = m_editingProfile.executablePath;
    } else {
        subtitle = L"No target executable";
    }

    r.DrawText(subtitle.c_str(), Typography::Muted(),
               static_cast<float>(rcName.left), static_cast<float>(rcName.bottom + 4),
               static_cast<float>((content.right - 150) - rcName.left), 20.0f,
               UITheme::TEXT_TERTIARY);

    // Enabled control / global badge.
    if (isMonitor) {
        RECT rcBadge = { content.right - 78, rcBounds.top + 38, content.right,
                         rcBounds.top + 64 };
        UITheme::DrawPill(r, rcBadge, UITheme::BG_RAISED, UITheme::BORDER);
        r.DrawText(L"GLOBAL", Typography::SmallMono(),
                   static_cast<float>(rcBadge.left), static_cast<float>(rcBadge.top),
                   static_cast<float>(rcBadge.right - rcBadge.left),
                   static_cast<float>(rcBadge.bottom - rcBadge.top),
                   UITheme::TEXT_TERTIARY,
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    } else {
        RECT rcToggle = GetToggleRect(rcBounds);
        RECT rcLabel = { rcToggle.left - 60, rcToggle.top - 18, rcToggle.right,
                         rcToggle.top - 4 };
        r.DrawText(L"ENABLED", Typography::SmallLabel(),
                   static_cast<float>(rcLabel.left), static_cast<float>(rcLabel.top),
                   static_cast<float>(rcLabel.right - rcLabel.left),
                   static_cast<float>(rcLabel.bottom - rcLabel.top),
                   UITheme::TEXT_TERTIARY,
                   DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        Controls::DrawToggle(r, rcToggle, m_toggleAnim.value,
                             m_toggleHover.value);
    }

    UITheme::DrawHLine(r, content.left, content.right, rcBounds.top + 100,
                       UITheme::DIVIDER);
}

void ProfileEditorView::DrawSlider(Renderer& r, const SliderLayout& layout,
                                   int sliderIndex) {
    r.DrawText(layout.label.c_str(), Typography::Button(),
               static_cast<float>(layout.rcRow.left),
               static_cast<float>(layout.rcRow.top + 12),
               static_cast<float>(layout.rcValue.left - 12 - layout.rcRow.left),
               20.0f,
               UITheme::TEXT_PRIMARY);

    UITheme::DrawPill(r, layout.rcValue, UITheme::BG_INPUT, UITheme::BORDER);
    {
        TextStyle valueStyle = Typography::Numeric();
        valueStyle.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        r.DrawText(layout.valueStr.c_str(), valueStyle,
                   static_cast<float>(layout.rcValue.left),
                   static_cast<float>(layout.rcValue.top),
                   static_cast<float>(layout.rcValue.right - layout.rcValue.left),
                   static_cast<float>(layout.rcValue.bottom - layout.rcValue.top),
                   UITheme::TEXT_PRIMARY,
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // Track - a recessed groove rather than a flat bar.
    {
        const D2D1_RECT_F trackRect(static_cast<float>(layout.rcTrack.left),
                                     static_cast<float>(layout.rcTrack.top),
                                     static_cast<float>(layout.rcTrack.right),
                                     static_cast<float>(layout.rcTrack.bottom));
        r.FillRoundedRectGradient(trackRect, 2.0f, 2.0f,
                                  Color(UITheme::Darken(UITheme::BG_INPUT, 8)),
                                  Color(UITheme::Lighten(UITheme::BG_INPUT, 3)));
    }

    const double range = layout.maxVal - layout.minVal;
    double ratio = range > 0.0 ? (layout.currentVal - layout.minVal) / range : 0.0;
    ratio = std::clamp(ratio, 0.0, 1.0);
    const int trackW = layout.rcTrack.right - layout.rcTrack.left;
    const int thumbX = layout.rcTrack.left +
                       static_cast<int>(ratio * trackW + 0.5);

    if (thumbX > layout.rcTrack.left + 1) {
        // The filled part of the track is a lit cylinder, not a flat white
        // rectangle.
        const D2D1_RECT_F fillRect(static_cast<float>(layout.rcTrack.left),
                                    static_cast<float>(layout.rcTrack.top),
                                    static_cast<float>(thumbX),
                                    static_cast<float>(layout.rcTrack.bottom));
        r.FillRoundedRectGradient(fillRect, 2.0f, 2.0f,
                                  Color(UITheme::Lighten(UITheme::ACCENT, 4)),
                                  Color(UITheme::Darken(UITheme::ACCENT, 4)));
    }

    // Ticks + neutral marker.
    for (int i = 1; i <= 4; ++i) {
        if (i == 4) continue;
        const int x = layout.rcTrack.left + (trackW * i) / 4;
        RECT rcTick = { x, layout.rcTrack.bottom + 6, x + 1,
                        layout.rcTrack.bottom + 10 };
        UITheme::FillSolidRect(r, rcTick, UITheme::DIVIDER);
    }
    if (range > 0.0) {
        double neutralRatio = (layout.neutralVal - layout.minVal) / range;
        neutralRatio = std::clamp(neutralRatio, 0.0, 1.0);
        const int x =
            layout.rcTrack.left + static_cast<int>(neutralRatio * trackW + 0.5);
        RECT rcNeutral = { x, layout.rcTrack.bottom + 4, x + 1,
                           layout.rcTrack.bottom + 12 };
        UITheme::FillSolidRect(r, rcNeutral, UITheme::BORDER_STRONG);
    }

    // Thumb - a raised knob that catches the light.
    const bool active = m_hoverSlider == sliderIndex ||
                        m_draggingSlider == layout.id;
    const int radius = active ? 7 : 6;
    const int cy = (layout.rcTrack.top + layout.rcTrack.bottom) / 2;
    r.DrawShadowEllipse(static_cast<float>(thumbX), static_cast<float>(cy),
                         static_cast<float>(radius), static_cast<float>(radius),
                         5.0f, 2.0f, UITheme::ShadowColor(0.20f));
    UITheme::DrawDot(r, thumbX, cy, radius + 2, UITheme::BORDER_STRONG);
    r.FillEllipseRadialGradient(static_cast<float>(thumbX),
                                static_cast<float>(cy) - 1.5f,
                                static_cast<float>(radius), static_cast<float>(radius),
                                static_cast<float>(thumbX),
                                static_cast<float>(cy) - 2.5f,
                                Color(UITheme::Lighten(UITheme::BG_SURFACE, 5)),
                                Color(UITheme::BG_SURFACE),
                                Color(UITheme::Darken(UITheme::BG_SURFACE, 5)));

    r.DrawText(layout.minStr.c_str(), Typography::SmallMono(),
               static_cast<float>(layout.rcTrack.left),
               static_cast<float>(layout.rcTrack.bottom + 10),
               80.0f, 14.0f, UITheme::TEXT_TERTIARY);
    r.DrawText(layout.maxStr.c_str(), Typography::SmallMono(),
               static_cast<float>(layout.rcTrack.right - 80),
               static_cast<float>(layout.rcTrack.bottom + 10),
               80.0f, 14.0f, UITheme::TEXT_TERTIARY,
               DWRITE_TEXT_ALIGNMENT_TRAILING);
}

void ProfileEditorView::DrawFooter(Renderer& r, const RECT& rcBounds) {
    RECT content = GetContentRect(rcBounds);
    const int dividerY = rcBounds.bottom - FOOTER_H;
    UITheme::DrawHLine(r, content.left, content.right, dividerY, UITheme::DIVIDER);

    RECT rcReset = GetResetBtnRect(rcBounds);
    Controls::DrawButtonWithIcon(r, rcReset, Icons::Reset, L"Reset",
                                 Controls::ButtonStyle::Ghost,
                                 m_resetHover.value, false, true,
                                 Typography::Button());

    RECT rcSave = GetSaveBtnRect(rcBounds);
    const bool dirty = IsDirty();
    Controls::DrawButtonWithIcon(
        r, rcSave, dirty ? Icons::Save : Icons::Check,
        dirty ? L"Save changes" : L"Saved", Controls::ButtonStyle::Primary,
        m_saveHover.value, false, dirty, Typography::Button());

    if (ShowCompare(rcBounds)) {
        RECT rcCompare = GetCompareBtnRect(rcBounds);
        Controls::DrawButtonWithIcon(
            r, rcCompare, Icons::Compare,
            m_isComparing ? L"Baseline" : L"Hold to compare",
            m_isComparing ? Controls::ButtonStyle::Primary
                          : Controls::ButtonStyle::Secondary,
            m_compareHover.value, false, true, Typography::Button());
    } else if (!CanCompare()) {
        TextStyle hintStyle = Typography::Muted();
        hintStyle.sizeDip = 10.0f;
        r.DrawText(L"Monitor profile is the fallback baseline", hintStyle,
                   static_cast<float>(content.left),
                   static_cast<float>(rcBounds.bottom - 60),
                   static_cast<float>(content.right - content.left), 26.0f,
                   UITheme::TEXT_TERTIARY,
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

void ProfileEditorView::Paint(Renderer& r, const RECT& rcBounds) {
    UITheme::FillSolidRect(r, rcBounds, UITheme::BG_CANVAS);

    m_toggleAnim.Towards(m_editingProfile.enabled);
    m_toggleHover.Towards(m_hoverToggle);
    m_nameHover.Towards(m_hoverName);
    m_avatarHover.Towards(m_hoverAvatar);
    m_resetHover.Towards(m_hoverReset);
    m_saveHover.Towards(m_hoverSave);
    m_compareHover.Towards(m_hoverCompare);
    m_hotkeyHover.Towards(m_hoverHotkey);
    m_dirtyAnim.Towards(IsDirty());
    for (int i = 0; i < 3; ++i) {
        m_sliderHover[static_cast<size_t>(i)].Towards(m_hoverSlider == i);
    }

    DrawHero(r, rcBounds);
    if (m_activeTab == EditorTab::ShortcutManager) {
        RECT card = GetCardRect(rcBounds);
        UITheme::DrawElevatedCard(r, card, 14, UITheme::BG_SURFACE, UITheme::BORDER,
                                   0.40f, 16.0f, 6.0f);

        UITheme::DrawEyebrow(r, L"SCREENSHOT HOTKEY",
                             RECT{ card.left + 20, card.top + 16,
                                   card.right - 20, card.top + 30 },
                             UITheme::TEXT_TERTIARY, 9, FW_SEMIBOLD, 1.0f);

        TextStyle descStyle = Typography::Muted();
        r.DrawText(L"Set a keyboard shortcut to take screenshots of the active window",
                   descStyle,
                   static_cast<float>(card.left + 20), static_cast<float>(card.top + 40),
                   static_cast<float>(card.right - card.left - 40), 20.0f,
                   UITheme::TEXT_SECONDARY);

        const int hotkeyW = 240;
        const int hotkeyH = 44;
        const int hotkeyCx = (card.left + card.right) / 2;
        const int hotkeyCy = card.top + 100;
        RECT rcHotkey = { hotkeyCx - hotkeyW / 2, hotkeyCy - hotkeyH / 2,
                          hotkeyCx + hotkeyW / 2, hotkeyCy + hotkeyH / 2 };
        m_screenshotHotkeyCapture.Paint(r, rcHotkey, m_hoverHotkey);

        TextStyle hintStyle = Typography::SmallMono();
        hintStyle.sizeDip = 10.0f;
        r.DrawText(L"Click to set \u00B7 Supports Ctrl, Alt, Shift, Win combinations",
                   hintStyle,
                   static_cast<float>(card.left + 20), static_cast<float>(hotkeyCy + 36),
                   static_cast<float>(card.right - card.left - 40), 16.0f,
                   UITheme::TEXT_TERTIARY,
                   DWRITE_TEXT_ALIGNMENT_CENTER);
    } else if (m_activeTab == EditorTab::Resolution) {
        RECT card = GetCardRect(rcBounds);
        UITheme::DrawElevatedCard(r, card, 14, UITheme::BG_SURFACE, UITheme::BORDER,
                                   0.40f, 16.0f, 6.0f);
        const bool isMonitorProfile = m_editingProfile.isDefault ||
                                      m_editingProfile.id == L"monitor";
        const bool hasResolution = m_editingProfile.resolutionProfile.has_value();
        const int left = card.left + 20;
        const int right = card.right - 20;
        const RECT actionButton{ right - 148, card.top + 14, right, card.top + 44 };
        Controls::DrawButton(
            r, actionButton,
            hasResolution ? L"Remove profile" : L"Add profile",
            Controls::ButtonStyle::Secondary, 0.0f, false,
            hasResolution || !m_availableResolutions.empty(),
            Typography::Button());
        UITheme::DrawEyebrow(
            r, isMonitorProfile ? L"MONITOR RESOLUTION" : L"GAME RESOLUTION",
            RECT{ left, card.top + 16, actionButton.left - 12, card.top + 30 },
            UITheme::TEXT_TERTIARY, 9, FW_SEMIBOLD, 1.0f);
        r.DrawText(isMonitorProfile
                       ? L"Choose a temporary mode for your desktop and unmanaged apps"
                       : L"Choose a temporary display mode for this game",
                   Typography::Muted(), static_cast<float>(left),
                   static_cast<float>(card.top + 37),
                   static_cast<float>(actionButton.left - left - 12), 18.0f,
                   UITheme::TEXT_SECONDARY);

        UITheme::DrawRoundedCard(
            r, RECT{ left, card.top + 60, right, card.top + 151 }, 10,
            UITheme::BG_INPUT, UITheme::BORDER);
        const std::wstring resolutionText = hasResolution
            ? std::to_wstring(m_editingProfile.resolutionProfile->width) + L" \u00D7 " +
                  std::to_wstring(m_editingProfile.resolutionProfile->height)
            : L"Not configured";
        TextStyle summaryLabel = Typography::Eyebrow();
        summaryLabel.sizeDip = 9.0f;
        r.DrawText(L"SELECTED PROFILE MODE", summaryLabel,
                   static_cast<float>(left + 14), static_cast<float>(card.top + 69),
                   280.0f, 14.0f, UITheme::TEXT_TERTIARY);
        TextStyle resolutionValue = Typography::Numeric();
        resolutionValue.sizeDip = hasResolution ? 25.0f : 20.0f;
        r.DrawText(resolutionText, resolutionValue,
                   static_cast<float>(left + 14), static_cast<float>(card.top + 85),
                   static_cast<float>(right - left - 190), 34.0f,
                   hasResolution ? UITheme::TEXT_PRIMARY : UITheme::TEXT_SECONDARY);
        std::wstring aspectText;
        if (hasResolution) {
            const int divisor = std::gcd(m_editingProfile.resolutionProfile->width,
                                         m_editingProfile.resolutionProfile->height);
            const double megapixels =
                static_cast<double>(m_editingProfile.resolutionProfile->width) *
                m_editingProfile.resolutionProfile->height / 1000000.0;
            std::wostringstream detail;
            detail << m_editingProfile.resolutionProfile->width / divisor << L":"
                   << m_editingProfile.resolutionProfile->height / divisor
                   << L" aspect \u00B7 " << std::fixed << std::setprecision(2)
                   << megapixels << L" MP";
            aspectText = detail.str();
        } else {
            aspectText = L"Select a supported mode below";
        }
        r.DrawText(aspectText, Typography::Muted(),
                   static_cast<float>(left + 14), static_cast<float>(card.top + 121),
                   static_cast<float>(right - left - 190), 16.0f,
                   UITheme::TEXT_TERTIARY);

        const RECT previewFrame{ right - 154, card.top + 68,
                                 right - 14, card.top + 137 };
        UITheme::DrawRoundedCard(r, previewFrame, 6, UITheme::BG_SURFACE,
                                 UITheme::BORDER_STRONG);
        const float previewWidth = static_cast<float>(previewFrame.right -
                                                       previewFrame.left - 20);
        const float previewHeight = static_cast<float>(previewFrame.bottom -
                                                        previewFrame.top - 20);
        const float aspect = hasResolution
            ? static_cast<float>(m_editingProfile.resolutionProfile->width) /
                  m_editingProfile.resolutionProfile->height
            : 16.0f / 9.0f;
        float screenWidth = previewWidth;
        float screenHeight = screenWidth / aspect;
        if (screenHeight > previewHeight) {
            screenHeight = previewHeight;
            screenWidth = screenHeight * aspect;
        }
        const float screenLeft =
            (previewFrame.left + previewFrame.right - screenWidth) * 0.5f;
        const float screenTop =
            (previewFrame.top + previewFrame.bottom - screenHeight) * 0.5f;
        r.DrawRoundedRect(
            D2D1::RectF(screenLeft, screenTop, screenLeft + screenWidth,
                        screenTop + screenHeight),
            3.0f, 3.0f,
            hasResolution ? UITheme::BG_RAISED : UITheme::BG_INPUT,
            hasResolution ? UITheme::TEXT_SECONDARY : UITheme::BORDER_STRONG,
            1.0f);
        TextStyle previewLabel = Typography::SmallMono();
        previewLabel.sizeDip = 9.0f;
        r.DrawText(hasResolution ? L"ASPECT PREVIEW" : L"PREVIEW",
                   previewLabel, static_cast<float>(previewFrame.left),
                   static_cast<float>(previewFrame.bottom + 2),
                   static_cast<float>(previewFrame.right - previewFrame.left),
                   12.0f, UITheme::TEXT_TERTIARY,
                   DWRITE_TEXT_ALIGNMENT_CENTER);

        UITheme::DrawHLine(r, left, right, card.top + 162, UITheme::DIVIDER);
        r.DrawText(L"Available resolutions", Typography::Button(),
                   static_cast<float>(left), static_cast<float>(card.top + 170),
                   230.0f, 18.0f, UITheme::TEXT_PRIMARY);
        const size_t presetCount = std::min(
            m_availableResolutions.size(),
            static_cast<size_t>(RESOLUTION_PRESET_LIMIT));
        const std::wstring modeCountText =
            std::to_wstring(m_availableResolutions.size()) + L" supported";
        TextStyle modeCountStyle = Typography::Muted();
        modeCountStyle.sizeDip = 10.0f;
        const int modeCountLeft = left + 230;
        const int modeCountRight = right -
                                      (m_availableResolutions.size() > presetCount
                                           ? 94
                                           : 0);
        r.DrawText(modeCountText, modeCountStyle,
                   static_cast<float>(modeCountLeft), static_cast<float>(card.top + 171),
                   static_cast<float>(std::max(0, modeCountRight - modeCountLeft)),
                   16.0f, UITheme::TEXT_TERTIARY,
                   DWRITE_TEXT_ALIGNMENT_TRAILING);
        const RECT allModesButton{ right - 92, card.top + 168, right, card.top + 188 };
        if (m_availableResolutions.size() > presetCount) {
            TextStyle allModesStyle = Typography::Button();
            allModesStyle.sizeDip = 10.0f;
            r.DrawText(L"All modes", allModesStyle,
                       static_cast<float>(allModesButton.left),
                       static_cast<float>(allModesButton.top),
                       static_cast<float>(allModesButton.right -
                                          allModesButton.left),
                       static_cast<float>(allModesButton.bottom -
                                          allModesButton.top),
                       UITheme::TEXT_SECONDARY, DWRITE_TEXT_ALIGNMENT_TRAILING,
                       DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        for (size_t i = 0; i < presetCount; ++i) {
            const RECT presetRect = ResolutionPresetRect(card, static_cast<int>(i));
            const ResolutionProfile& preset = m_availableResolutions[i];
            const bool selected = hasResolution &&
                preset.width == m_editingProfile.resolutionProfile->width &&
                preset.height == m_editingProfile.resolutionProfile->height;
            UITheme::DrawRoundedCard(
                r, presetRect, 7,
                selected ? UITheme::INVERSE : UITheme::BG_INPUT,
                selected ? UITheme::INVERSE : UITheme::BORDER_STRONG);
            const std::wstring presetLabel =
                std::to_wstring(preset.width) + L" \u00D7 " +
                std::to_wstring(preset.height);
            r.DrawText(presetLabel, Typography::Numeric(),
                       static_cast<float>(presetRect.left + 4),
                       static_cast<float>(presetRect.top),
                       static_cast<float>(presetRect.right - presetRect.left - 8),
                       static_cast<float>(presetRect.bottom - presetRect.top),
                       selected ? UITheme::INVERSE_TEXT : UITheme::TEXT_SECONDARY,
                       DWRITE_TEXT_ALIGNMENT_CENTER,
                       DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
        }

        std::vector<const StretchedSuggestion*> supportedSuggestions;
        for (const auto& suggestion : STRETCHED_SUGGESTIONS) {
            const auto found = std::find_if(
                m_availableResolutions.begin(), m_availableResolutions.end(),
                [&suggestion](const ResolutionProfile& mode) {
                    return mode.width == suggestion.width &&
                           mode.height == suggestion.height;
                });
            if (found != m_availableResolutions.end()) {
                supportedSuggestions.push_back(&suggestion);
            }
        }
        if (!supportedSuggestions.empty()) {
            r.DrawText(L"Stretched suggestions", Typography::Button(),
                       static_cast<float>(left),
                       static_cast<float>(card.top + STRETCHED_SUGGESTION_TOP),
                       220.0f, 18.0f, UITheme::TEXT_PRIMARY);
            TextStyle suggestionHint = Typography::Muted();
            suggestionHint.sizeDip = 10.0f;
            r.DrawText(L"Common 4:3 choices for a wider, stretched image",
                       suggestionHint, static_cast<float>(left + 220),
                       static_cast<float>(card.top + STRETCHED_SUGGESTION_TOP + 1),
                       static_cast<float>(right - left - 220), 16.0f,
                       UITheme::TEXT_TERTIARY);
            const int gap = 8;
            const int tileWidth = (right - left - gap * 2) / 3;
            for (size_t i = 0; i < supportedSuggestions.size(); ++i) {
                const StretchedSuggestion& suggestion = *supportedSuggestions[i];
                const int tileLeft = left + static_cast<int>(i) * (tileWidth + gap);
                const RECT suggestionRect{
                    tileLeft, card.top + STRETCHED_SUGGESTION_TOP + 22,
                    tileLeft + tileWidth, card.top + STRETCHED_SUGGESTION_TOP + 58
                };
                const bool selected = hasResolution &&
                    m_editingProfile.resolutionProfile->width == suggestion.width &&
                    m_editingProfile.resolutionProfile->height == suggestion.height;
                UITheme::DrawRoundedCard(
                    r, suggestionRect, 7,
                    selected ? UITheme::INVERSE : UITheme::BG_INPUT,
                    selected ? UITheme::INVERSE : UITheme::BORDER_STRONG);
                const std::wstring suggestionResolution =
                    std::to_wstring(suggestion.width) + L" \u00D7 " +
                    std::to_wstring(suggestion.height);
                r.DrawText(suggestionResolution, Typography::Numeric(),
                           static_cast<float>(suggestionRect.left + 5),
                           static_cast<float>(suggestionRect.top + 3),
                           static_cast<float>(suggestionRect.right -
                                              suggestionRect.left - 10),
                           16.0f,
                           selected ? UITheme::INVERSE_TEXT : UITheme::TEXT_PRIMARY,
                           DWRITE_TEXT_ALIGNMENT_CENTER,
                           DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
                TextStyle suggestionDetail = Typography::Muted();
                suggestionDetail.sizeDip = 9.0f;
                const std::wstring suggestionCaption =
                    std::wstring(suggestion.label) + L" \u00B7 " + suggestion.detail;
                r.DrawText(suggestionCaption, suggestionDetail,
                           static_cast<float>(suggestionRect.left + 4),
                           static_cast<float>(suggestionRect.top + 19),
                           static_cast<float>(suggestionRect.right -
                                              suggestionRect.left - 8),
                           13.0f,
                           selected ? UITheme::INVERSE_TEXT : UITheme::TEXT_TERTIARY,
                           DWRITE_TEXT_ALIGNMENT_CENTER,
                           DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
            }
        } else {
            TextStyle suggestionHint = Typography::Muted();
            suggestionHint.sizeDip = 10.0f;
            r.DrawText(L"No suggested 4:3 modes are reported by this display.",
                       suggestionHint, static_cast<float>(left),
                       static_cast<float>(card.top + STRETCHED_SUGGESTION_TOP + 4),
                       static_cast<float>(right - left), 16.0f,
                       UITheme::TEXT_TERTIARY);
        }

        r.DrawText(L"Scaling", Typography::Button(), static_cast<float>(left),
                   static_cast<float>(card.top + SCALING_SECTION_TOP), 180.0f, 18.0f,
                   UITheme::TEXT_PRIMARY);
        static constexpr const wchar_t* scalingLabels[] = {
            L"Aspect ratio", L"Stretch", L"No scaling"
        };
        static constexpr ResolutionScaling scalingValues[] = {
            ResolutionScaling::MaintainAspectRatio,
            ResolutionScaling::FullScreenStretch,
            ResolutionScaling::NoScaling
        };
        for (int i = 0; i < 3; ++i) {
            const RECT scalingRect = ResolutionScalingRect(card, i);
            const bool selected = hasResolution &&
                m_editingProfile.resolutionProfile->scaling == scalingValues[i];
            const bool enabled = hasResolution &&
                (i == 0 || m_scalingModesSupported);
            UITheme::DrawRoundedCard(
                r, scalingRect, 7,
                selected ? UITheme::INVERSE : UITheme::BG_INPUT,
                selected ? UITheme::INVERSE :
                    (enabled ? UITheme::BORDER_STRONG : UITheme::BORDER));
            r.DrawText(scalingLabels[i], Typography::Button(),
                       static_cast<float>(scalingRect.left + 4),
                       static_cast<float>(scalingRect.top),
                       static_cast<float>(scalingRect.right -
                                          scalingRect.left - 8),
                       static_cast<float>(scalingRect.bottom -
                                          scalingRect.top),
                       selected ? UITheme::INVERSE_TEXT :
                           (enabled ? UITheme::TEXT_SECONDARY
                                    : UITheme::TEXT_DISABLED),
                       DWRITE_TEXT_ALIGNMENT_CENTER,
                       DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
        }

        std::wstring hint;
        if (isMonitorProfile) {
            hint = L"Resolution profiles are configured individually on game profiles.";
        } else if (!hasResolution) {
            hint = m_availableResolutions.empty()
                       ? L"Windows did not report any selectable display modes."
                       : L"Select a mode above to add a resolution profile for this game.";
        } else if (hasResolution &&
                   std::none_of(m_availableResolutions.begin(),
                                m_availableResolutions.end(),
                                [this](const ResolutionProfile& mode) {
                                    return mode.width ==
                                               m_editingProfile.resolutionProfile->width &&
                                           mode.height ==
                                               m_editingProfile.resolutionProfile->height;
                                })) {
            hint = L"The saved resolution is not supported by this display.";
        } else if (hasResolution && !m_scalingModesSupported &&
                   m_editingProfile.resolutionProfile->scaling !=
                       ResolutionScaling::MaintainAspectRatio) {
            hint = L"This scaling mode is not supported at the selected resolution.";
        } else if (m_availableResolutions.empty()) {
            hint = L"Windows did not report any selectable display modes.";
        } else {
            hint = L"Only resolutions reported by the primary display can be selected.";
        }
        TextStyle hintStyle = Typography::Muted();
        hintStyle.sizeDip = 10.0f;
        r.DrawText(hint, hintStyle,
                   static_cast<float>(left), static_cast<float>(card.top + 414),
                   static_cast<float>(right - left), 24.0f,
                   UITheme::TEXT_TERTIARY);
    } else {
        // Color and gamma controls remain editable even when live color-matrix
        // effects are unsupported; unsupported effects are not previewed.
        RECT card = GetCardRect(rcBounds);
        UITheme::DrawElevatedCard(r, card, 14, UITheme::BG_SURFACE, UITheme::BORDER,
                                   0.40f, 16.0f, 6.0f);

        const wchar_t* section = m_activeTab == EditorTab::Color
                                     ? L"COLOR"
                                     : m_activeTab == EditorTab::Channels
                                           ? L"RGB GAMMA"
                                           : L"DISPLAY";
        UITheme::DrawEyebrow(r, section,
                             RECT{ card.left + 20, card.top + 16,
                                   card.right - 20, card.top + 30 },
                             UITheme::TEXT_TERTIARY, 9, FW_SEMIBOLD, 1.0f);

        const wchar_t* hint = L"Brightness \u00B7 Contrast \u00B7 Gamma";
        if (m_activeTab == EditorTab::Channels) {
            hint = L"Red \u00B7 Green \u00B7 Blue";
        } else if (m_activeTab == EditorTab::Color) {
            hint = m_colorEffectsAvailable
                       ? L"Saturation \u00B7 Vibrance \u00B7 Hue"
                       : L"Live color effects unavailable; values can still be saved";
        }

        r.DrawText(hint, Typography::SmallMono(),
                   static_cast<float>(card.left + 20), static_cast<float>(card.top + 16),
                   static_cast<float>(card.right - card.left - 40), 14.0f,
                   UITheme::TEXT_TERTIARY,
                   DWRITE_TEXT_ALIGNMENT_TRAILING);

        size_t sliderCount = 0;
        const auto sliders = GetActiveSliders(sliderCount);
        for (size_t i = 0; i < sliderCount; ++i) {
            SliderLayout layout = GetSliderLayout(sliders[i], static_cast<int>(i),
                                                  rcBounds);
            DrawSlider(r, layout, static_cast<int>(i));
        }

    }

    DrawFooter(r, rcBounds);
}

bool ProfileEditorView::OnMouseMove(POINT pt, const RECT& rcBounds) {
    if (m_draggingSlider != SliderId::None) {
        size_t sliderCount = 0;
        const auto sliders = GetActiveSliders(sliderCount);
        for (size_t i = 0; i < sliderCount; ++i) {
            if (sliders[i] != m_draggingSlider) continue;
            SliderLayout sl = GetSliderLayout(sliders[i], static_cast<int>(i), rcBounds);
            const int width = sl.rcTrack.right - sl.rcTrack.left;
            if (width <= 0) return false;
            double ratio = static_cast<double>(pt.x - sl.rcTrack.left) / width;
            ratio = std::clamp(ratio, 0.0, 1.0);
            return ApplySliderValue(
                sl.id, sl.minVal + ratio * (sl.maxVal - sl.minVal));
        }
    }

    const int prevSlider = m_hoverSlider;
    const bool prevToggle = m_hoverToggle;
    const bool prevName = m_hoverName;
    const bool prevAvatar = m_hoverAvatar;
    const bool prevReset = m_hoverReset;
    const bool prevSave = m_hoverSave;
    const bool prevCompare = m_hoverCompare;
    const bool prevHotkey = m_hoverHotkey;

    m_hoverSlider = -1;
    m_hoverToggle = false;
    m_hoverName = false;
    m_hoverAvatar = false;
    m_hoverReset = false;
    m_hoverSave = false;
    m_hoverCompare = false;
    m_hoverHotkey = false;

    RECT rcName = GetNameRect(rcBounds);
    const bool isMonitor = m_editingProfile.isDefault || m_editingProfile.id == L"monitor";

    // Avatar hover (only for non-monitor profiles)
    if (!isMonitor) {
        RECT rcAvatar = GetAvatarRect(rcBounds);
        m_hoverAvatar = PtInRect(&rcAvatar, pt) != FALSE;
    }

    m_hoverName = !isMonitor && PtInRect(&rcName, pt) != FALSE;

    if (!isMonitor) {
        RECT rcToggle = GetToggleRect(rcBounds);
        m_hoverToggle = PtInRect(&rcToggle, pt) != FALSE;
    }

    RECT rcReset = GetResetBtnRect(rcBounds);
    m_hoverReset = PtInRect(&rcReset, pt) != FALSE;
    RECT rcSave = GetSaveBtnRect(rcBounds);
    m_hoverSave = PtInRect(&rcSave, pt) != FALSE;

    // Hotkey hover - only in Shortcut Manager tab
    if (m_activeTab == EditorTab::ShortcutManager && !isMonitor) {
        RECT card = GetCardRect(rcBounds);
        const int hotkeyW = 240;
        const int hotkeyH = 44;
        const int hotkeyCx = (card.left + card.right) / 2;
        const int hotkeyCy = card.top + 100;
        RECT rcHotkey = { hotkeyCx - hotkeyW / 2, hotkeyCy - hotkeyH / 2,
                          hotkeyCx + hotkeyW / 2, hotkeyCy + hotkeyH / 2 };
        m_hoverHotkey = PtInRect(&rcHotkey, pt) != FALSE;
    }

    if (ShowCompare(rcBounds)) {
        RECT rcCompare = GetCompareBtnRect(rcBounds);
        m_hoverCompare = PtInRect(&rcCompare, pt) != FALSE;
    }

    size_t sliderCount = 0;
    const auto sliders = GetActiveSliders(sliderCount);
    for (size_t i = 0; i < sliderCount; ++i) {
        SliderLayout sl = GetSliderLayout(sliders[i], static_cast<int>(i), rcBounds);
        RECT rcHit = sl.rcTrack;
        rcHit.top -= 16;
        rcHit.bottom += 16;
        if (PtInRect(&rcHit, pt)) { m_hoverSlider = static_cast<int>(i); break; }
    }

    return m_hoverSlider != prevSlider ||
           m_hoverToggle != prevToggle || m_hoverName != prevName ||
           m_hoverAvatar != prevAvatar ||
           m_hoverReset != prevReset || m_hoverSave != prevSave ||
           m_hoverCompare != prevCompare || m_hoverHotkey != prevHotkey;
}

bool ProfileEditorView::OnLButtonDown(POINT pt, const RECT& rcBounds) {
    const bool wasEditingName = m_editingName;
    RECT rcName = GetNameRect(rcBounds);
    const bool isMonitor = m_editingProfile.isDefault || m_editingProfile.id == L"monitor";

    // If listening for hotkey and clicked outside, cancel listening
    if (m_screenshotHotkeyCapture.IsListening()) {
        if (m_activeTab == EditorTab::ShortcutManager) {
            RECT card = GetCardRect(rcBounds);
            const int hotkeyW = 240;
            const int hotkeyH = 44;
            const int hotkeyCx = (card.left + card.right) / 2;
            const int hotkeyCy = card.top + 100;
            RECT rcHotkey = { hotkeyCx - hotkeyW / 2, hotkeyCy - hotkeyH / 2,
                              hotkeyCx + hotkeyW / 2, hotkeyCy + hotkeyH / 2 };
            if (!PtInRect(&rcHotkey, pt)) {
                m_screenshotHotkeyCapture.CancelListen();
                return true;
            }
        } else {
            m_screenshotHotkeyCapture.CancelListen();
            return true;
        }
    }

    if (m_editingName && !PtInRect(&rcName, pt)) {
        m_editingName = false;
    }

    // Avatar click: open file picker
    if (!isMonitor) {
        RECT rcAvatar = GetAvatarRect(rcBounds);
        if (PtInRect(&rcAvatar, pt)) {
            // Open file picker
            wchar_t filePath[MAX_PATH] = L"";
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = GetActiveWindow();
            ofn.lpstrFile = filePath;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"Image Files\0*.png;*.jpg;*.jpeg\0All Files\0*.*\0";
            ofn.lpstrTitle = L"Choose Profile Icon";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

            if (GetOpenFileNameW(&ofn)) {
                // Create profile icon directory
                wchar_t appData[MAX_PATH];
                if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData) == S_OK) {
                    std::wstring iconDir = std::wstring(appData) + L"\\Lumetra\\ProfileIcons\\" + m_editingProfile.id + L"\\";
                    SHCreateDirectoryExW(nullptr, iconDir.c_str(), nullptr);

                    // Copy file to profile folder as "icon.{ext}"
                    std::wstring srcPath(filePath);
                    std::wstring ext = srcPath.substr(srcPath.find_last_of(L"."));
                    std::wstring destPath = iconDir + L"icon" + ext;
                    CopyFileW(filePath, destPath.c_str(), FALSE);
                }
            }
            return true;
        }
    }

    if (!isMonitor && PtInRect(&rcName, pt)) {
        m_editingName = true;
        return true;
    }

    if (m_activeTab == EditorTab::Resolution) {
        RECT card = GetCardRect(rcBounds);
        const int right = card.right - 20;
        const RECT enableButton{ right - 148, card.top + 14, right, card.top + 44 };
        if (PtInRect(&enableButton, pt)) {
            if (m_editingProfile.resolutionProfile) {
                m_editingProfile.resolutionProfile.reset();
            } else if (!m_availableResolutions.empty()) {
                ResolutionProfile selected = m_availableResolutions.front();
                for (const auto& mode : m_availableResolutions) {
                    if (mode.width == 1920 && mode.height == 1080) {
                        selected = mode;
                        break;
                    }
                }
                m_editingProfile.resolutionProfile = selected;
                m_scalingModesSupported = m_onScalingSupport &&
                    m_onScalingSupport(selected.width, selected.height);
            }
            return true;
        }

        const size_t presetCount = std::min(
            m_availableResolutions.size(),
            static_cast<size_t>(RESOLUTION_PRESET_LIMIT));
        const RECT allModesButton{ right - 92, card.top + 168, right, card.top + 188 };
        if (m_availableResolutions.size() > presetCount &&
            PtInRect(&allModesButton, pt)) {
            if (m_onResolutionPicker) m_onResolutionPicker();
            return true;
        }

        for (size_t i = 0; i < presetCount; ++i) {
            const RECT presetRect = ResolutionPresetRect(card, static_cast<int>(i));
            if (!PtInRect(&presetRect, pt)) continue;
            ResolutionProfile selected = m_availableResolutions[i];
            if (m_editingProfile.resolutionProfile) {
                selected.scaling = m_editingProfile.resolutionProfile->scaling;
            }
            m_scalingModesSupported = m_onScalingSupport &&
                m_onScalingSupport(selected.width, selected.height);
            if (!m_scalingModesSupported &&
                selected.scaling != ResolutionScaling::MaintainAspectRatio) {
                selected.scaling = ResolutionScaling::MaintainAspectRatio;
            }
            m_editingProfile.resolutionProfile = selected;
            return true;
        }

        std::vector<const StretchedSuggestion*> supportedSuggestions;
        for (const auto& suggestion : STRETCHED_SUGGESTIONS) {
            const auto found = std::find_if(
                m_availableResolutions.begin(), m_availableResolutions.end(),
                [&suggestion](const ResolutionProfile& mode) {
                    return mode.width == suggestion.width &&
                           mode.height == suggestion.height;
                });
            if (found != m_availableResolutions.end()) {
                supportedSuggestions.push_back(&suggestion);
            }
        }
        const int left = card.left + 20;
        const int gap = 8;
        const int tileWidth = (right - left - gap * 2) / 3;
        for (size_t i = 0; i < supportedSuggestions.size(); ++i) {
            const StretchedSuggestion& suggestion = *supportedSuggestions[i];
            const int tileLeft = left + static_cast<int>(i) * (tileWidth + gap);
            const RECT suggestionRect{
                tileLeft, card.top + STRETCHED_SUGGESTION_TOP + 22,
                tileLeft + tileWidth, card.top + STRETCHED_SUGGESTION_TOP + 58
            };
            if (!PtInRect(&suggestionRect, pt)) continue;
            ResolutionProfile selected{ suggestion.width, suggestion.height };
            m_scalingModesSupported = m_onScalingSupport &&
                m_onScalingSupport(selected.width, selected.height);
            if (m_scalingModesSupported) {
                selected.scaling = ResolutionScaling::FullScreenStretch;
            }
            m_editingProfile.resolutionProfile = selected;
            return true;
        }

        if (m_editingProfile.resolutionProfile) {
            for (int i = 0; i < 3; ++i) {
                const RECT scalingRect = ResolutionScalingRect(card, i);
                if (!PtInRect(&scalingRect, pt)) continue;
                if (i == 0 || m_scalingModesSupported) {
                    m_editingProfile.resolutionProfile->scaling =
                        static_cast<ResolutionScaling>(i);
                }
                return true;
            }
        }
    }

    if (!isMonitor) {
        RECT rcToggle = GetToggleRect(rcBounds);
        if (PtInRect(&rcToggle, pt)) {
            m_editingProfile.enabled = !m_editingProfile.enabled;
            if (m_onPreview) m_onPreview(m_editingProfile);
            return true;
        }
    }

    RECT rcReset = GetResetBtnRect(rcBounds);
    if (PtInRect(&rcReset, pt)) {
        m_editingProfile.brightness = 1.0;
        m_editingProfile.contrast = 1.0;
        m_editingProfile.gammaR = 1.0;
        m_editingProfile.gammaG = 1.0;
        m_editingProfile.gammaB = 1.0;
        m_editingProfile.saturation = 1.0;
        m_editingProfile.vibrance = 0;
        m_editingProfile.hue = 0;
        m_editingProfile.RecomputeRamp();
        m_sliderPreviewPending = false;
        if (m_onPreview) m_onPreview(m_editingProfile);
        return true;
    }

    RECT rcSave = GetSaveBtnRect(rcBounds);
    if (PtInRect(&rcSave, pt)) {
        return Save();
    }

    // Hotkey button click in the Shortcut Manager tab.
    if (m_activeTab == EditorTab::ShortcutManager) {
        RECT card = GetCardRect(rcBounds);
        const int hotkeyW = 240;
        const int hotkeyH = 44;
        const int hotkeyCx = (card.left + card.right) / 2;
        const int hotkeyCy = card.top + 100;
        RECT rcHotkey = { hotkeyCx - hotkeyW / 2, hotkeyCy - hotkeyH / 2,
                          hotkeyCx + hotkeyW / 2, hotkeyCy + hotkeyH / 2 };
        if (PtInRect(&rcHotkey, pt)) {
            // Start listening for hotkey input
            m_screenshotHotkeyCapture.BeginListen();
            return true;
        }
    }

    if (ShowCompare(rcBounds)) {
        RECT rcCompare = GetCompareBtnRect(rcBounds);
        if (PtInRect(&rcCompare, pt)) {
            m_isComparing = true;
            if (m_onCompare) m_onCompare(true);
            return true;
        }
    }

    size_t sliderCount = 0;
    const auto sliders = GetActiveSliders(sliderCount);
    for (size_t i = 0; i < sliderCount; ++i) {
        SliderLayout sl = GetSliderLayout(sliders[i], static_cast<int>(i), rcBounds);
        RECT rcHit = sl.rcTrack;
        rcHit.top -= 16;
        rcHit.bottom += 16;
        if (PtInRect(&rcHit, pt)) {
            m_draggingSlider = sl.id;
            m_hoverSlider = static_cast<int>(i);
            OnMouseMove(pt, rcBounds);
            return true;
        }
    }

    return m_editingName || wasEditingName;
}

bool ProfileEditorView::OnLButtonUp(POINT /*pt*/, const RECT& /*rcBounds*/) {
    bool changed = false;
    if (m_isComparing) {
        m_isComparing = false;
        if (m_onCompare) m_onCompare(false);
        changed = true;
    }
    if (m_draggingSlider != SliderId::None) {
        m_draggingSlider = SliderId::None;
        changed = true;
    }
    if (m_sliderPreviewPending) {
        FlushPendingSliderPreview();
        changed = true;
    }
    return changed;
}

bool ProfileEditorView::OnChar(wchar_t ch) {
    if (!m_editingName) return false;
    if (ch == VK_BACK) {
        if (!m_editingProfile.name.empty()) m_editingProfile.name.pop_back();
    } else if (ch >= 32 && ch != 127 && m_editingProfile.name.size() < 48) {
        m_editingProfile.name.push_back(ch);
    } else {
        return false;
    }
    return true;
}

bool ProfileEditorView::OnKeyDown(WPARAM wParam) {
    // Hotkey capture takes priority
    if (m_screenshotHotkeyCapture.IsListening()) {
        return m_screenshotHotkeyCapture.OnKeyDown(wParam);
    }

    if (m_editingName && (wParam == VK_RETURN || wParam == VK_ESCAPE)) {
        m_editingName = false;
        return true;
    }
    return false;
}

bool ProfileEditorView::OnKeyUp(WPARAM wParam) {
    // Hotkey capture takes priority
    if (m_screenshotHotkeyCapture.IsListening()) {
        return m_screenshotHotkeyCapture.OnKeyUp(wParam);
    }
    return false;
}

bool ProfileEditorView::IsListeningForHotkey() const {
    return m_screenshotHotkeyCapture.IsListening();
}

bool ProfileEditorView::TickAnimations(float dt) {
    bool active = false;
    for (Anim& a : m_sliderHover) active |= a.Advance(dt);
    active |= m_toggleAnim.Advance(dt);
    active |= m_toggleHover.Advance(dt);
    active |= m_nameHover.Advance(dt);
    active |= m_avatarHover.Advance(dt);
    active |= m_resetHover.Advance(dt);
    active |= m_saveHover.Advance(dt);
    active |= m_compareHover.Advance(dt);
    FlushPendingSliderPreview();
    active |= m_dirtyAnim.Advance(dt);
    return active;
}

bool ProfileEditorView::HasActiveAnimations() const {
    if (m_draggingSlider != SliderId::None || m_sliderPreviewPending ||
        !m_toggleAnim.Settled() ||
        !m_toggleHover.Settled() || !m_nameHover.Settled() ||
        !m_avatarHover.Settled() ||
        !m_resetHover.Settled() || !m_saveHover.Settled() ||
        !m_compareHover.Settled() || !m_dirtyAnim.Settled()) {
        return true;
    }
    for (const Anim& a : m_sliderHover)
        if (!a.Settled()) return true;
    return false;
}

} // namespace prismshift

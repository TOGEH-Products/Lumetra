#include "ui/SettingsView.h"
#include "ui/UITheme.h"
#include "ui/Controls.h"
#include "ui/Icons.h"
#include <algorithm>

namespace prismshift {

namespace {
constexpr int PAD_X = 36;
constexpr int TOP_PAD = 12;
constexpr int BOTTOM_PAD = 16;
constexpr int MAX_WIDTH = 760;
constexpr int SECTION_GAP = 20;
constexpr int LABEL_H = 16;
constexpr int LABEL_GAP = 10;
constexpr int CARD_PAD = 4;
constexpr int ROW_H = 58;
constexpr int INFO_ROW_H = 48;
constexpr int CARD_RADIUS = 12;

constexpr int TOGGLE_W = 44;
constexpr int TOGGLE_H = 24;
constexpr int BUTTON_W = 118;
constexpr int BUTTON_H = 32;
constexpr int DROPDOWN_W = 200;
constexpr int THEME_CONTROL_W = 176;

std::pair<RECT, RECT> ThemeSegmentRects(const RECT& rc) {
    constexpr int gap = 4;
    const int w = (rc.right - rc.left - gap) / 2;
    RECT a{ rc.left, rc.top, rc.left + w, rc.bottom };
    RECT b{ rc.left + w + gap, rc.top, rc.right, rc.bottom };
    return { a, b };
}

void DrawThemeSegment(Renderer& r, const RECT& rc, const wchar_t* label, bool active) {
    const COLORREF fill = active ? UITheme::ACCENT : UITheme::BG_INPUT;
    const COLORREF border = active ? UITheme::ACCENT : UITheme::BORDER;
    const COLORREF text = active ? UITheme::ACCENT_TEXT : UITheme::TEXT_SECONDARY;
    UITheme::DrawRoundedCard(r, rc, 8, fill, border);
    TextStyle seg = Typography::Button();
    seg.sizeDip = 11.0f;
    r.DrawText(label, seg, static_cast<float>(rc.left), static_cast<float>(rc.top),
               static_cast<float>(rc.right - rc.left),
               static_cast<float>(rc.bottom - rc.top), text,
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}
} // namespace

SettingsView::SettingsView() {
    m_hoverAnims.assign(32, Anim(0.0f, 18.0f));
    m_toggleAnims.assign(9, Anim(0.0f, 17.0f));
    m_scrollAnim.speed = 20.0f;
    m_screenshotHotkeyCapture.SetChangeCallback([this](const std::wstring& hotkey) {
        m_screenshotGlobalHotkey = hotkey;
        if (m_onScreenshotHotkey) m_onScreenshotHotkey(hotkey);
    });
}

void SettingsView::SetScreenshotGlobalHotkey(const std::wstring& hotkey) {
    m_screenshotGlobalHotkey = hotkey;
    m_screenshotHotkeyCapture.SetHotkey(hotkey);
}

bool SettingsView::IsToggle(ControlId id) const {
    switch (id) {
        case ControlId::StartWithWindows:
        case ControlId::StartMinimized:
        case ControlId::CloseToTray:
        case ControlId::Notifications:
        case ControlId::ReduceMotion:
        case ControlId::Switching:
        case ControlId::Screenshots:
        case ControlId::AutoCopyScreenshot:
        case ControlId::AutomaticUpdateChecks:
            return true;
        default:
            return false;
    }
}

SettingsView::ToggleId SettingsView::ToToggleId(ControlId id) const {
    switch (id) {
        case ControlId::StartWithWindows: return ToggleId::StartWithWindows;
        case ControlId::StartMinimized:   return ToggleId::StartMinimized;
        case ControlId::CloseToTray:      return ToggleId::CloseToTray;
        case ControlId::Notifications:    return ToggleId::Notifications;
        case ControlId::ReduceMotion:     return ToggleId::ReduceMotion;
        case ControlId::Switching:        return ToggleId::Switching;
        case ControlId::Screenshots:      return ToggleId::Screenshots;
        case ControlId::AutoCopyScreenshot: return ToggleId::AutoCopyScreenshot;
        case ControlId::AutomaticUpdateChecks: return ToggleId::AutomaticUpdateChecks;
        default:                          return ToggleId::Switching;
    }
}

void SettingsView::SetToggleState(ToggleId id, bool enabled) {
    switch (id) {
        case ToggleId::StartWithWindows: m_startWithWindows = enabled; break;
        case ToggleId::StartMinimized:   m_startMinimized = enabled; break;
        case ToggleId::CloseToTray:      m_closeToTray = enabled; break;
        case ToggleId::Notifications:    m_notifications = enabled; break;
        case ToggleId::ReduceMotion:     m_reduceMotion = enabled; break;
        case ToggleId::Switching:        m_switching = enabled; break;
        case ToggleId::Screenshots:      m_screenshots = enabled; break;
        case ToggleId::AutoCopyScreenshot: m_autoCopyScreenshot = enabled; break;
        case ToggleId::AutomaticUpdateChecks: m_automaticUpdateChecks = enabled; break;
    }
    m_toggleAnims[static_cast<int>(id)].To(enabled);
}

bool SettingsView::GetToggleState(ToggleId id) const {
    switch (id) {
        case ToggleId::StartWithWindows: return m_startWithWindows;
        case ToggleId::StartMinimized:   return m_startMinimized;
        case ToggleId::CloseToTray:      return m_closeToTray;
        case ToggleId::Notifications:    return m_notifications;
        case ToggleId::ReduceMotion:     return m_reduceMotion;
        case ToggleId::Switching:        return m_switching;
        case ToggleId::Screenshots:      return m_screenshots;
        case ToggleId::AutoCopyScreenshot: return m_autoCopyScreenshot;
        case ToggleId::AutomaticUpdateChecks: return m_automaticUpdateChecks;
    }
    return false;
}

SettingsView::Layout SettingsView::BuildLayout(const RECT& rcBounds) const {
    Layout layout;
    int left = rcBounds.left + PAD_X;
    int right = rcBounds.right - PAD_X;
    if (right - left > MAX_WIDTH) {
        const int cx = (left + right) / 2;
        left = cx - MAX_WIDTH / 2;
        right = left + MAX_WIDTH;
    }

    int y = rcBounds.top + TOP_PAD - static_cast<int>(m_scrollAnim.value + 0.5f);
    layout.contentTop = y;
    int animCounter = 6;

    auto addSection = [&](const wchar_t* label,
                          std::initializer_list<std::pair<ControlId, int>> rows) {
        SectionLayout section;
        section.label = label;
        section.rcLabel = RECT{ left + 4, y, right, y + LABEL_H };
        y += LABEL_H + LABEL_GAP;

        auto descriptionFor = [&](ControlId id) -> std::wstring {
            switch (id) {
                case ControlId::StartWithWindows: return L"Launch Lumetra quietly when you sign in.";
                case ControlId::StartMinimized: return L"Open to the system tray instead of the window.";
                case ControlId::CloseToTray: return L"Keep automation running when the window closes.";
                case ControlId::Notifications: return L"Show a tray notice the first time Lumetra hides.";
                case ControlId::ReduceMotion: return L"Disable hover, toggle and scroll animations.";
                case ControlId::Theme: return L"Obsidian is black and white; Paper is warm light.";
                case ControlId::Switching: return L"Apply a profile when its application comes to the front.";
                case ControlId::ResetDisplay: return L"Return gamma and color to neutral immediately.";
                case ControlId::ConfigFolder:
                    return m_configPath.empty() ? L"Profiles and settings are stored here." : m_configPath;
                case ControlId::Screenshots: return L"Capture and organize gaming screenshots with hotkeys.";
                case ControlId::AutoCopyScreenshot: return L"Copy each captured image to the clipboard automatically.";
                case ControlId::ScreenshotClickAction: return L"Choose what the screenshot notification opens or copies.";
                case ControlId::ResolutionMode:
                    if (m_resolutionSwitchMode == ResolutionSwitchMode::AskBeforeApplying) {
                        return L"A notification will appear when a supported game launches, allowing you to decide whether to apply its resolution profile.";
                    }
                    if (m_resolutionSwitchMode == ResolutionSwitchMode::AutoSwitch) {
                        return L"Automatically apply the selected game's resolution profile when the game launches.";
                    }
                    return L"Resolution profile switching is disabled.";
                case ControlId::ScreenshotsFolder:
                    return m_screenshotsDir.empty() ? L"Screenshots are saved here, organized by game." : m_screenshotsDir;
                case ControlId::ScreenshotsHotkey:
                    return L"Press to capture a screenshot (works in any application).";
                case ControlId::OpenScreenshotsFolder:
                    return L"Open the screenshots folder to view your captures.";
                case ControlId::AutomaticUpdateChecks:
                    return L"Check for stable releases no more than once every 24 hours.";
                case ControlId::CheckForUpdates:
                    return L"Manually check the configured official GitHub release source.";
                case ControlId::UpdateStatus:
                    return m_updateStatus;
                case ControlId::Brand: return L"Automatic display color profiles for games.";
                case ControlId::Version: return L"Lumetra desktop";
                case ControlId::Backend: return L"Gamma ramp with DWM color matrix";
                default: return L"";
            }
        };
        auto rowHeight = [&](ControlId id, int minimum) {
            if (id == ControlId::ResolutionMode) {
                if (m_resolutionSwitchMode == ResolutionSwitchMode::AutoSwitch) return 120;
                if (m_resolutionSwitchMode == ResolutionSwitchMode::AskBeforeApplying) return 82;
                return 68;
            }
            const int availableWidth = std::max(120, right - left -
                (id == ControlId::Version || id == ControlId::Backend ? 300 :
                 (IsToggle(id) || id == ControlId::Theme || id == ControlId::ResetDisplay ||
                  id == ControlId::ConfigFolder || id == ControlId::ScreenshotsFolder ||
                  id == ControlId::OpenScreenshotsFolder ||
                  id == ControlId::CheckForUpdates ||
                  id == ControlId::ScreenshotsHotkey ||
                  id == ControlId::ScreenshotClickAction ? 250 : 40)));
            const int charsPerLine = std::max(24, availableWidth / 6);
            const int lines = std::max(1, static_cast<int>(
                (descriptionFor(id).size() + static_cast<size_t>(charsPerLine) - 1) /
                static_cast<size_t>(charsPerLine)));
            return std::max(minimum, 58 + (lines - 1) * 16);
        };

        int height = CARD_PAD * 2;
        for (const auto& row : rows) height += rowHeight(row.first, row.second);

        section.rcCard = RECT{ left, y, right, y + height };
        int rowTop = y + CARD_PAD;
        for (const auto& def : rows) {
            const int currentRowHeight = rowHeight(def.first, def.second);
            RowLayout row;
            row.id = def.first;
            row.isToggle = IsToggle(def.first);
            row.rcRow = RECT{ left, rowTop, right, rowTop + currentRowHeight };

            if (row.isToggle) {
                const int top = rowTop + (currentRowHeight - TOGGLE_H) / 2;
                row.rcControl = RECT{ right - 22 - TOGGLE_W, top, right - 22,
                                      top + TOGGLE_H };
                row.animIndex = static_cast<int>(ToToggleId(def.first));
            } else if (def.first == ControlId::Theme) {
                const int top = rowTop + (currentRowHeight - 26) / 2;
                row.rcControl = RECT{ right - 22 - THEME_CONTROL_W, top, right - 22, top + 26 };
                row.animIndex = animCounter++;
            } else if (def.first == ControlId::ResetDisplay ||
                       def.first == ControlId::ConfigFolder ||
                       def.first == ControlId::ScreenshotsFolder ||
                       def.first == ControlId::OpenScreenshotsFolder ||
                       def.first == ControlId::CheckForUpdates) {
                const int top = rowTop + (currentRowHeight - BUTTON_H) / 2;
                row.rcControl = RECT{ right - 20 - BUTTON_W, top, right - 20,
                                      top + BUTTON_H };
                row.animIndex = animCounter++;
            } else if (def.first == ControlId::ScreenshotsHotkey) {
                const int width = 180;
                const int top = rowTop + (currentRowHeight - BUTTON_H) / 2;
                row.rcControl = RECT{ right - 20 - width, top, right - 20,
                                      top + BUTTON_H };
                row.animIndex = animCounter++;
            } else if (def.first == ControlId::ScreenshotClickAction) {
                const int top = rowTop + (currentRowHeight - BUTTON_H) / 2;
                row.rcControl = RECT{ right - 20 - DROPDOWN_W, top, right - 20,
                                      top + BUTTON_H };
                row.animIndex = animCounter++;
            } else if (def.first == ControlId::ResolutionMode) {
                const int top = rowTop + 12;
                row.rcControl = RECT{ right - 22 - DROPDOWN_W, top, right - 22,
                                      top + 32 };
                row.animIndex = animCounter++;
            } else if (def.first == ControlId::Version ||
                       def.first == ControlId::Backend) {
                row.rcControl = RECT{ right - 260, rowTop, right - 22,
                                      rowTop + currentRowHeight };
                row.animIndex = animCounter++;
            } else {
                row.animIndex = animCounter++;
            }

            layout.rows.push_back(row);
            rowTop += currentRowHeight;
        }

        layout.sections.push_back(section);
        y = section.rcCard.bottom + SECTION_GAP;
    };

    addSection(L"PERSONALIZE", {
        { ControlId::ReduceMotion, ROW_H },
        { ControlId::Theme, ROW_H },
    });

    addSection(L"BEHAVIOR", {
        { ControlId::StartWithWindows, ROW_H },
        { ControlId::StartMinimized, ROW_H },
        { ControlId::CloseToTray, ROW_H },
        { ControlId::Notifications, ROW_H },
    });

    addSection(L"DISPLAY", {
        { ControlId::Switching, ROW_H },
        { ControlId::ResetDisplay, ROW_H },
        { ControlId::ResolutionMode, ROW_H },
    });

    addSection(L"SCREENSHOTS", {
        { ControlId::Screenshots, ROW_H },
        { ControlId::AutoCopyScreenshot, ROW_H },
        { ControlId::ScreenshotClickAction, ROW_H },
        { ControlId::ScreenshotsFolder, ROW_H },
        { ControlId::ScreenshotsHotkey, ROW_H },
        { ControlId::OpenScreenshotsFolder, ROW_H },
    });

    addSection(L"DATA", {
        { ControlId::ConfigFolder, ROW_H },
    });

    addSection(L"UPDATES", {
        { ControlId::AutomaticUpdateChecks, ROW_H },
        { ControlId::CheckForUpdates, ROW_H },
        { ControlId::UpdateStatus, INFO_ROW_H },
    });

    addSection(L"ABOUT", {
        { ControlId::Brand, 72 },
        { ControlId::Version, INFO_ROW_H },
        { ControlId::Backend, INFO_ROW_H },
    });

    layout.contentHeight = (y - SECTION_GAP) - layout.contentTop;
    return layout;
}

void SettingsView::DrawRow(Renderer& r, const RowLayout& row, float hover) {
    RECT rcControl = row.rcControl;

    if (hover > 0.01f) {
        RECT rcHover = { row.rcRow.left + 6, row.rcRow.top + 2,
                         row.rcRow.right - 6, row.rcRow.bottom - 2 };
        UITheme::DrawRoundedCard(r, rcHover, 9,
                                 UITheme::Mix(UITheme::BG_SURFACE, UITheme::BG_RAISED, hover),
                                 UITheme::Mix(UITheme::BG_SURFACE, UITheme::BG_RAISED, hover));
    }

    const int textLeft = row.rcRow.left + 18;
    const int textRight = (row.rcControl.left > 0 ? row.rcControl.left - 16 : row.rcRow.right - 18);

    // The About card opens with the brand mark, so the settings page reads as
    // Lumetra's own space rather than a generic form.
    if (row.id == ControlId::Brand) {
        const int tile = 44;
        const int tileTop = row.rcRow.top + (row.rcRow.bottom - row.rcRow.top - tile) / 2;
        RECT rcLogo = { textLeft, tileTop, textLeft + tile, tileTop + tile };
        if (m_logoBitmap) {
            D2D1_RECT_F dest = D2D1::RectF(static_cast<float>(rcLogo.left),
                                           static_cast<float>(rcLogo.top),
                                           static_cast<float>(rcLogo.right),
                                           static_cast<float>(rcLogo.bottom));
            r.DrawBitmapRounded(m_logoBitmap, dest, 10.0f);
        } else {
            UITheme::DrawRoundedCard(r, rcLogo, 10, UITheme::ACCENT, UITheme::ACCENT);
            TextStyle mark = Typography::PageTitle();
            mark.sizeDip = 18.0f;
            mark.weight = DWRITE_FONT_WEIGHT_BOLD;
            r.DrawText(L"L", mark, static_cast<float>(rcLogo.left),
                       static_cast<float>(rcLogo.top), static_cast<float>(tile),
                       static_cast<float>(tile), UITheme::ACCENT_TEXT,
                       DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        const int textX = rcLogo.right + 14;
        TextStyle nameStyle = Typography::CardHeading();
        nameStyle.sizeDip = 15.0f;
        nameStyle.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        r.DrawText(L"Lumetra", nameStyle, static_cast<float>(textX),
                   static_cast<float>(row.rcRow.top + 12),
                   static_cast<float>(row.rcRow.right - textX - 18), 20.0f,
                   UITheme::TEXT_PRIMARY);
        TextStyle tagline = Typography::Muted();
        tagline.wordWrap = true;
        r.DrawText(L"Automatic display color profiles for games", tagline,
                   static_cast<float>(textX), static_cast<float>(row.rcRow.top + 34),
                   static_cast<float>(row.rcRow.right - textX - 18), 18.0f,
                   UITheme::TEXT_TERTIARY);
        return;
    }

    const wchar_t* title = L"";
    std::wstring desc;

    switch (row.id) {
        case ControlId::StartWithWindows:
            title = L"Start with Windows";
            desc = L"Launch Lumetra quietly when you sign in.";
            break;
        case ControlId::StartMinimized:
            title = L"Start minimized";
            desc = L"Open to the system tray instead of the window.";
            break;
        case ControlId::CloseToTray:
            title = L"Close to tray";
            desc = L"Keep automation running when the window closes.";
            break;
        case ControlId::Notifications:
            title = L"Notifications";
            desc = L"Show a tray notice the first time Lumetra hides.";
            break;
        case ControlId::ReduceMotion:
            title = L"Reduce motion";
            desc = L"Disable hover, toggle and scroll animations.";
            break;
        case ControlId::Theme:
            title = L"Theme";
            desc = L"Obsidian is black and white; Paper is warm light.";
            break;
        case ControlId::Switching:
            title = L"Profile switching";
            desc = L"Apply a profile when its application comes to the front.";
            break;
        case ControlId::ResetDisplay:
            title = L"Reset display";
            desc = L"Return gamma and color to neutral immediately.";
            break;
        case ControlId::ConfigFolder:
            title = L"Configuration folder";
            desc = m_configPath.empty() ? L"Profiles and settings are stored here." : m_configPath;
            break;
        case ControlId::Screenshots:
            title = L"Enable screenshots";
            desc = L"Capture and organize gaming screenshots with hotkeys.";
            break;
        case ControlId::AutoCopyScreenshot:
            title = L"Auto-copy screenshot";
            desc = L"Copy each captured image to the clipboard automatically.";
            break;
        case ControlId::ScreenshotClickAction:
            title = L"When clicking notification";
            desc = L"Choose what the screenshot notification opens or copies.";
            break;
        case ControlId::ResolutionMode:
            title = L"Auto-Switch Resolution Profile";
            if (m_resolutionSwitchMode == ResolutionSwitchMode::AskBeforeApplying) {
                desc = L"A notification will appear when a supported game launches, allowing you to decide whether to apply its resolution profile.";
            } else if (m_resolutionSwitchMode == ResolutionSwitchMode::AutoSwitch) {
                desc = L"Automatically apply the selected game's resolution profile when the game launches.";
            } else {
                desc = L"Resolution profile switching is disabled.";
            }
            break;
        case ControlId::ScreenshotsFolder:
            title = L"Screenshots folder";
            desc = m_screenshotsDir.empty() ? L"Screenshots are saved here, organized by game." : m_screenshotsDir;
            break;
        case ControlId::ScreenshotsHotkey:
            title = L"Global hotkey";
            desc = L"Press to capture a screenshot (works in any application).";
            break;
        case ControlId::OpenScreenshotsFolder:
            title = L"Screenshots library";
            desc = L"Open the screenshots folder to view your captures.";
            break;
        case ControlId::AutomaticUpdateChecks:
            title = L"Automatic update checks";
            desc = L"Check for stable releases no more than once every 24 hours.";
            break;
        case ControlId::CheckForUpdates:
            title = L"Check for updates";
            desc = L"Manually check the configured official GitHub release source.";
            break;
        case ControlId::UpdateStatus:
            title = L"Update status · Stable";
            desc = m_updateStatus;
            break;
        case ControlId::Version:
            title = L"Version";
            desc = L"Lumetra desktop";
            break;
        case ControlId::Backend:
            title = L"Display backend";
            desc = L"Gamma ramp with DWM color matrix";
            break;
        default:
            break;
    }

    TextStyle titleStyle = Typography::Body();
    titleStyle.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    titleStyle.trim = true;
    r.DrawText(title, titleStyle,
               static_cast<float>(textLeft), static_cast<float>(row.rcRow.top + 14),
               static_cast<float>(textRight - textLeft), 19.0f,
               row.id == ControlId::ResolutionMode &&
                       m_resolutionSwitchMode == ResolutionSwitchMode::Off
                   ? UITheme::TEXT_DISABLED
                   : UITheme::TEXT_PRIMARY);

    if (row.id == ControlId::ResolutionMode) {
        TextStyle explanationStyle = Typography::Muted();
        explanationStyle.wordWrap = true;
        const int explanationHeight =
            m_resolutionSwitchMode == ResolutionSwitchMode::AutoSwitch ? 34 :
            m_resolutionSwitchMode == ResolutionSwitchMode::AskBeforeApplying ? 34 : 18;
        r.DrawText(desc.c_str(), explanationStyle,
                   static_cast<float>(textLeft),
                   static_cast<float>(row.rcRow.top + 39),
                   static_cast<float>(row.rcRow.right - textLeft - 18),
                   static_cast<float>(explanationHeight),
                   m_resolutionSwitchMode == ResolutionSwitchMode::Off
                       ? UITheme::TEXT_DISABLED
                       : UITheme::TEXT_TERTIARY,
                   DWRITE_TEXT_ALIGNMENT_LEADING,
                   DWRITE_PARAGRAPH_ALIGNMENT_NEAR, true);
        if (m_resolutionSwitchMode == ResolutionSwitchMode::AutoSwitch) {
            const int warningTop = row.rcRow.top + 78;
            const COLORREF warningColor = UITheme::WARNING;
            const RECT rcWarning{ textLeft, warningTop + 1,
                                  textLeft + 15, warningTop + 16 };
            Icons::Draw(r, Icons::Warning, rcWarning, 13, warningColor);
            TextStyle warningStyle = Typography::Muted();
            warningStyle.sizeDip = 10.0f;
            warningStyle.wordWrap = true;
            r.DrawText(
                L"Frequent display resolution changes may temporarily interrupt your display or affect window positioning. Use automatic switching with care.",
                warningStyle, static_cast<float>(textLeft + 21),
                static_cast<float>(warningTop),
                static_cast<float>(row.rcRow.right - textLeft - 39),
                static_cast<float>(row.rcRow.bottom - warningTop - 4),
                warningColor, DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_NEAR, true);
        }
    } else {
        TextStyle descriptionStyle = Typography::Muted();
        descriptionStyle.wordWrap = true;
        const float descriptionHeight = static_cast<float>(
            std::max<LONG>(16, row.rcRow.bottom - row.rcRow.top - 40));
        r.DrawText(desc.c_str(), descriptionStyle,
                   static_cast<float>(textLeft), static_cast<float>(row.rcRow.top + 34),
                   static_cast<float>(textRight - textLeft), descriptionHeight,
                   row.rcRow.bottom - row.rcRow.top <= ROW_H
                       ? UITheme::TEXT_TERTIARY
                       : UITheme::TEXT_SECONDARY,
                   DWRITE_TEXT_ALIGNMENT_LEADING,
                   DWRITE_PARAGRAPH_ALIGNMENT_NEAR, true);
    }

    const bool controlHover = m_hoverControl == row.id;
    switch (row.id) {
        case ControlId::StartWithWindows:
        case ControlId::StartMinimized:
        case ControlId::CloseToTray:
        case ControlId::Notifications:
        case ControlId::ReduceMotion:
        case ControlId::Switching:
        case ControlId::Screenshots:
        case ControlId::AutoCopyScreenshot:
        case ControlId::AutomaticUpdateChecks: {
            const int index = static_cast<int>(ToToggleId(row.id));
            Controls::DrawToggle(r, row.rcControl, m_toggleAnims[index].value,
                                 controlHover ? 1.0f : 0.0f);
            break;
        }
        case ControlId::ScreenshotsHotkey: {
            DrawScreenshotHotkeyField(r, row.rcControl, controlHover);
            break;
        }
        case ControlId::ScreenshotClickAction: {
            UITheme::DrawPill(r, row.rcControl, UITheme::BG_INPUT, UITheme::BORDER);
            const wchar_t* actionLabel = L"Open the image";
            if (m_screenshotClickAction == ScreenshotClickAction::OpenScreenshotsFolder) {
                actionLabel = L"Open screenshots folder";
            } else if (m_screenshotClickAction == ScreenshotClickAction::CopyImage) {
                actionLabel = L"Copy image";
            }
            r.DrawText(actionLabel, Typography::Button(),
                       static_cast<float>(row.rcControl.left + 12),
                       static_cast<float>(row.rcControl.top),
                       static_cast<float>(row.rcControl.right - row.rcControl.left - 34),
                       static_cast<float>(row.rcControl.bottom - row.rcControl.top),
                       UITheme::TEXT_SECONDARY,
                       DWRITE_TEXT_ALIGNMENT_LEADING,
                       DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                       true);
            const RECT rcChevron = { row.rcControl.right - 28, row.rcControl.top + 6,
                                     row.rcControl.right - 8, row.rcControl.bottom - 6 };
            Icons::Draw(r, Icons::ChevronDown, rcChevron, 10, UITheme::TEXT_TERTIARY);
            break;
        }
        case ControlId::ResolutionMode: {
            const bool enabled = m_resolutionSwitchMode != ResolutionSwitchMode::Off;
            const COLORREF controlFill = enabled ? UITheme::BG_INPUT : UITheme::BG_SURFACE;
            const COLORREF controlBorder = enabled ? UITheme::BORDER : UITheme::DIVIDER;
            const COLORREF controlText = enabled ? UITheme::TEXT_SECONDARY : UITheme::TEXT_DISABLED;
            UITheme::DrawPill(r, row.rcControl, controlFill, controlBorder);
            const wchar_t* modeLabel = L"Off";
            if (m_resolutionSwitchMode == ResolutionSwitchMode::AutoSwitch) {
                modeLabel = L"Auto-Switch";
            } else if (m_resolutionSwitchMode == ResolutionSwitchMode::AskBeforeApplying) {
                modeLabel = L"Ask Before Applying";
            }
            r.DrawText(modeLabel, Typography::Button(),
                       static_cast<float>(row.rcControl.left + 10),
                       static_cast<float>(row.rcControl.top),
                       static_cast<float>(row.rcControl.right - row.rcControl.left - 34),
                       static_cast<float>(row.rcControl.bottom - row.rcControl.top),
                       controlText,
                       DWRITE_TEXT_ALIGNMENT_LEADING,
                       DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
            const RECT rcChevron = { row.rcControl.right - 28, row.rcControl.top + 7,
                                     row.rcControl.right - 8, row.rcControl.bottom - 7 };
            Icons::Draw(r, Icons::ChevronDown, rcChevron, 10,
                        enabled ? UITheme::TEXT_TERTIARY : UITheme::TEXT_DISABLED);
            break;
        }
        case ControlId::Theme: {
            const auto [rcObsidian, rcPaper] = ThemeSegmentRects(row.rcControl);
            DrawThemeSegment(r, rcObsidian, L"Obsidian", m_theme == 0);
            DrawThemeSegment(r, rcPaper, L"Paper", m_theme == 1);
            break;
        }
        case ControlId::ResetDisplay: {
            TextStyle btnStyle = Typography::Button();
            btnStyle.sizeDip = 11.0f;
            Controls::DrawButton(r, row.rcControl, L"Reset display",
                                 Controls::ButtonStyle::Secondary,
                                 controlHover ? 1.0f : 0.0f, false, true,
                                 btnStyle);
            break;
        }
        case ControlId::ConfigFolder: {
            TextStyle btnStyle = Typography::Button();
            btnStyle.sizeDip = 11.0f;
            Controls::DrawButton(r, row.rcControl, L"Open folder",
                                 Controls::ButtonStyle::Secondary,
                                 controlHover ? 1.0f : 0.0f, false, true,
                                 btnStyle);
            break;
        }
        case ControlId::ScreenshotsFolder: {
            TextStyle btnStyle = Typography::Button();
            btnStyle.sizeDip = 11.0f;
            Controls::DrawButton(r, row.rcControl, L"Browse...",
                                 Controls::ButtonStyle::Secondary,
                                 controlHover ? 1.0f : 0.0f, false, true,
                                 btnStyle);
            break;
        }
        case ControlId::OpenScreenshotsFolder: {
            TextStyle btnStyle = Typography::Button();
            btnStyle.sizeDip = 11.0f;
            Controls::DrawButton(r, row.rcControl, L"Open folder",
                                 Controls::ButtonStyle::Secondary,
                                 controlHover ? 1.0f : 0.0f, false, true,
                                 btnStyle);
            break;
        }
        case ControlId::CheckForUpdates: {
            TextStyle btnStyle = Typography::Button();
            btnStyle.sizeDip = 11.0f;
            Controls::DrawButton(r, row.rcControl, L"Check now",
                                 Controls::ButtonStyle::Secondary,
                                 controlHover ? 1.0f : 0.0f, false, true,
                                 btnStyle);
            break;
        }
        case ControlId::Version:
            r.DrawText(m_version.c_str(), Typography::Numeric(),
                       static_cast<float>(row.rcControl.left),
                       static_cast<float>(row.rcControl.top),
                       static_cast<float>(row.rcControl.right - row.rcControl.left),
                       static_cast<float>(row.rcControl.bottom - row.rcControl.top),
                       UITheme::TEXT_SECONDARY,
                       DWRITE_TEXT_ALIGNMENT_TRAILING,
                       DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            break;
        case ControlId::Backend:
            r.DrawText(m_backend.c_str(), Typography::Numeric(),
                       static_cast<float>(row.rcControl.left),
                       static_cast<float>(row.rcControl.top),
                       static_cast<float>(row.rcControl.right - row.rcControl.left),
                       static_cast<float>(row.rcControl.bottom - row.rcControl.top),
                       UITheme::TEXT_SECONDARY,
                       DWRITE_TEXT_ALIGNMENT_TRAILING,
                       DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            break;
        default:
            break;
    }
}

void SettingsView::Paint(Renderer& r, const RECT& rcBounds) {
    UITheme::FillSolidRect(r, rcBounds, UITheme::BG_CANVAS);

    // Keep toggle channels aligned with the authoritative view state.
    for (int i = 0; i < 7; ++i) {
        m_toggleAnims[i].Towards(GetToggleState(static_cast<ToggleId>(i)));
    }
    m_scrollAnim.To(static_cast<float>(m_scrollY));

    Layout layout = BuildLayout(rcBounds);
    const int visibleHeight = (rcBounds.bottom - BOTTOM_PAD) - (rcBounds.top + TOP_PAD);
    m_scrollMax = std::max(0, layout.contentHeight - visibleHeight);
    m_scrollY = std::clamp(m_scrollY, 0, m_scrollMax);

    const D2D1_RECT_F clip = D2D1::RectF(
        static_cast<float>(rcBounds.left), static_cast<float>(rcBounds.top + TOP_PAD),
        static_cast<float>(rcBounds.right), static_cast<float>(rcBounds.bottom - BOTTOM_PAD));
    r.GetTarget()->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    for (const SectionLayout& section : layout.sections) {
        UITheme::DrawEyebrow(r, section.label, section.rcLabel,
                             UITheme::TEXT_TERTIARY, 9, FW_SEMIBOLD, 1.0f);
        UITheme::DrawGlassCard(r, section.rcCard, CARD_RADIUS, UITheme::BG_SURFACE, 0.85f);
    }

    for (size_t i = 0; i < layout.rows.size(); ++i) {
        const RowLayout& row = layout.rows[i];
        const float hover = (static_cast<int>(i) == m_hoverRow)
                                ? m_hoverAnims[std::min<int>(
                                      row.animIndex,
                                      static_cast<int>(m_hoverAnims.size()) - 1)]
                                      .value
                                : 0.0f;
        DrawRow(r, row, hover);

        // Hairline between rows, inset to respect the card's rounded corners.
        const bool lastInCard = (i + 1 >= layout.rows.size()) ||
                                (layout.rows[i + 1].rcRow.top != row.rcRow.bottom);
        if (!lastInCard) {
            UITheme::DrawHLine(r, row.rcRow.left + 18, row.rcRow.right - 18,
                               row.rcRow.bottom - 1, UITheme::DIVIDER);
        }
    }

    r.GetTarget()->PopAxisAlignedClip();

    if (m_scrollMax > 0) {
        RECT rcTrack = { rcBounds.right - 15, rcBounds.top + TOP_PAD,
                         rcBounds.right - 11, rcBounds.bottom - BOTTOM_PAD };
        UITheme::DrawScrollbar(r, rcTrack,
                               static_cast<int>(m_scrollAnim.value + 0.5f),
                               m_scrollMax, visibleHeight);
    }
}

bool SettingsView::OnMouseMove(POINT pt, const RECT& rcBounds) {
    Layout layout = BuildLayout(rcBounds);
    const int prevRow = m_hoverRow;
    const ControlId prevControl = m_hoverControl;

    m_hoverRow = -1;
    m_hoverControl = ControlId::None;

    if (pt.y >= rcBounds.top + TOP_PAD && pt.y < rcBounds.bottom - BOTTOM_PAD) {
        for (size_t i = 0; i < layout.rows.size(); ++i) {
            const RowLayout& row = layout.rows[i];
            if (PtInRect(&row.rcRow, pt)) {
                m_hoverRow = static_cast<int>(i);
                if (row.rcControl.left > 0 && PtInRect(&row.rcControl, pt)) {
                    m_hoverControl = row.id;
                }
                break;
            }
        }
    }

    for (size_t i = 0; i < m_hoverAnims.size(); ++i) {
        m_hoverAnims[i].Towards(static_cast<int>(i) ==
                                            (m_hoverRow >= 0 &&
                                             m_hoverRow <
                                                 static_cast<int>(layout.rows.size())
                                                ? layout.rows[m_hoverRow].animIndex
                                                : -1));
    }

    return m_hoverRow != prevRow || m_hoverControl != prevControl;
}

bool SettingsView::OnLButtonDown(POINT pt, const RECT& rcBounds) {
    Layout layout = BuildLayout(rcBounds);
    if (pt.y < rcBounds.top + TOP_PAD || pt.y >= rcBounds.bottom - BOTTOM_PAD)
        return false;

    for (const RowLayout& row : layout.rows) {
        if (!PtInRect(&row.rcRow, pt)) continue;
        if (row.rcControl.left == 0 || !PtInRect(&row.rcControl, pt)) return false;

        if (row.isToggle) {
            const ToggleId id = ToToggleId(row.id);
            const bool next = !GetToggleState(id);
            SetToggleState(id, next);
            if (m_onToggle) m_onToggle(id, next);
            return true;
        }
        if (row.id == ControlId::ResetDisplay) {
            if (m_onResetDisplay) m_onResetDisplay();
            return true;
        }
        if (row.id == ControlId::ConfigFolder) {
            if (m_onOpenConfig) m_onOpenConfig();
            return true;
        }
        if (row.id == ControlId::ScreenshotsFolder) {
            if (m_onBrowseScreenshots) m_onBrowseScreenshots();
            return true;
        }
        if (row.id == ControlId::OpenScreenshotsFolder) {
            if (m_onOpenScreenshots) m_onOpenScreenshots();
            return true;
        }
        if (row.id == ControlId::CheckForUpdates) {
            if (m_onCheckUpdates) m_onCheckUpdates();
            return true;
        }
        if (row.id == ControlId::ScreenshotsHotkey) {
            m_screenshotHotkeyCapture.BeginListen();
            return true;
        }
        if (row.id == ControlId::ScreenshotClickAction) {
            if (m_onScreenshotClickAction) m_onScreenshotClickAction();
            return true;
        }
        if (row.id == ControlId::ResolutionMode) {
            if (m_onResolutionMode) m_onResolutionMode(m_resolutionSwitchMode);
            return true;
        }
        if (row.id == ControlId::Theme) {
            const auto [rcObsidian, rcPaper] = ThemeSegmentRects(row.rcControl);
            int next = m_theme;
            if (PtInRect(&rcObsidian, pt)) next = 0;
            else if (PtInRect(&rcPaper, pt)) next = 1;
            if (next != m_theme) {
                m_theme = next;
                if (m_onTheme) m_onTheme(next);
            }
            return true;
        }
        return false;
    }
    return false;
}

bool SettingsView::OnKeyDown(WPARAM wParam) {
    return m_screenshotHotkeyCapture.OnKeyDown(wParam);
}

bool SettingsView::OnKeyUp(WPARAM wParam) {
    return m_screenshotHotkeyCapture.OnKeyUp(wParam);
}

bool SettingsView::OnMouseWheel(int delta, const RECT& /*rcBounds*/) {
    if (m_scrollMax <= 0) return false;
    const int prev = m_scrollY;
    m_scrollY = std::clamp(m_scrollY - (delta / 120) * 64, 0, m_scrollMax);
    if (m_scrollY != prev) {
        m_scrollAnim.To(static_cast<float>(m_scrollY));
        return true;
    }
    return false;
}

bool SettingsView::TickAnimations(float dt) {
    bool active = false;
    active |= m_scrollAnim.Advance(dt);
    for (Anim& a : m_hoverAnims) active |= a.Advance(dt);
    for (Anim& a : m_toggleAnims) active |= a.Advance(dt);
    return active;
}

bool SettingsView::HasActiveAnimations() const {
    if (!m_scrollAnim.Settled()) return true;
    for (const Anim& a : m_hoverAnims)
        if (!a.Settled()) return true;
    for (const Anim& a : m_toggleAnims)
        if (!a.Settled()) return true;
    return false;
}

void SettingsView::ResetHover() {
    m_hoverRow = -1;
    m_hoverControl = ControlId::None;
    for (Anim& a : m_hoverAnims) a.To(0.0f);
}

void SettingsView::DrawScreenshotHotkeyField(Renderer& r, const RECT& rcControl, bool hover) {
    m_screenshotHotkeyCapture.Paint(r, rcControl, hover);
}

} // namespace prismshift

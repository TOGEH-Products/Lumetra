#pragma once

#include <windows.h>
#include <algorithm>
#include <string>

#include "ui/Renderer.h"

namespace prismshift {

// Lumetra theme identity.
//
// Two palettes share one drawing language: flat cards with hairline edges,
// warm/neutral depth and a single accent. "Obsidian" is strict black and
// white (dark surfaces, ink text, white accent); "Paper" is warm light with a
// clay accent. Both stay opaque, because Lumetra previews live gamma ramps on
// the real display and no panel may tint what it covers.
enum class ThemeId {
    Obsidian = 0, // black & white, default
    Paper = 1,    // warm light
};

// Every colour token as a flat value, so a theme is a single lookup table.
struct ThemePalette {
    COLORREF bgCanvas;
    COLORREF bgSidebar;
    COLORREF bgSurface;
    COLORREF bgRaised;
    COLORREF bgHover;
    COLORREF bgInput;
    COLORREF bgInputHover;
    COLORREF border;
    COLORREF borderStrong;
    COLORREF divider;
    COLORREF textPrimary;
    COLORREF textSecondary;
    COLORREF textTertiary;
    COLORREF textDisabled;
    COLORREF accent;
    COLORREF accentHover;
    COLORREF accentText;
    COLORREF accentSoft;
    COLORREF success;
    COLORREF successSoft;
    COLORREF danger;
    COLORREF dangerSoft;
    COLORREF warning;
    COLORREF toggleOffTrack;
    COLORREF toggleOffKnob;
};

inline constexpr ThemePalette kObsidianPalette = {
    RGB(12, 12, 12),   // bgCanvas
    RGB(16, 16, 16),   // bgSidebar
    RGB(20, 20, 20),   // bgSurface
    RGB(28, 28, 28),   // bgRaised
    RGB(36, 36, 36),   // bgHover
    RGB(16, 16, 16),   // bgInput
    RGB(22, 22, 22),   // bgInputHover
    RGB(40, 40, 40),   // border
    RGB(60, 60, 60),   // borderStrong
    RGB(28, 28, 28),   // divider
    RGB(245, 245, 245),// textPrimary
    RGB(168, 168, 168),// textSecondary
    RGB(120, 120, 120),// textTertiary
    RGB(80, 80, 80),   // textDisabled
    RGB(255, 255, 255),// accent (pure white)
    RGB(228, 228, 228),// accentHover
    RGB(12, 12, 12),   // accentText
    RGB(40, 40, 40),   // accentSoft
    RGB(210, 210, 210),// success
    RGB(34, 34, 34),   // successSoft
    RGB(255, 255, 255),// danger
    RGB(34, 34, 34),   // dangerSoft
    RGB(190, 190, 190),// warning
    RGB(16, 16, 16),   // toggleOffTrack
    RGB(255, 255, 255),// toggleOffKnob
};

inline constexpr ThemePalette kPaperPalette = {
    RGB(249, 246, 241),  // bgCanvas
    RGB(244, 240, 233),  // bgSidebar
    RGB(255, 253, 249),  // bgSurface
    RGB(251, 248, 243),  // bgRaised
    RGB(241, 235, 227),  // bgHover
    RGB(243, 239, 232),  // bgInput
    RGB(238, 233, 225),  // bgInputHover
    RGB(227, 220, 210),  // border
    RGB(205, 196, 184),  // borderStrong
    RGB(235, 230, 222),  // divider
    RGB(46, 40, 33),     // textPrimary
    RGB(108, 98, 88),    // textSecondary
    RGB(157, 147, 136),  // textTertiary
    RGB(196, 188, 178),  // textDisabled
    RGB(196, 104, 55),   // accent (clay)
    RGB(178, 91, 47),    // accentHover
    RGB(255, 254, 252),  // accentText
    RGB(246, 228, 216),  // accentSoft
    RGB(84, 144, 96),    // success
    RGB(229, 240, 231),  // successSoft
    RGB(197, 86, 74),    // danger
    RGB(249, 230, 226),  // dangerSoft
    RGB(187, 137, 52),   // warning
    RGB(243, 239, 232),  // toggleOffTrack
    RGB(255, 254, 252),  // toggleOffKnob
};

// All drawing helpers take a Renderer& and draw in device-independent pixels
// (DIPs). Fonts and text layout are owned by DirectWriteText. The colour
// tokens below are the *active* palette; call UITheme::SetTheme to swap them.
struct UITheme {
    inline static ThemeId CurrentId = ThemeId::Obsidian;

    // ---- Active palette (mutable) ----
    inline static COLORREF BG_CANVAS       = kObsidianPalette.bgCanvas;
    inline static COLORREF BG_SIDEBAR      = kObsidianPalette.bgSidebar;
    inline static COLORREF BG_SURFACE      = kObsidianPalette.bgSurface;
    inline static COLORREF BG_RAISED       = kObsidianPalette.bgRaised;
    inline static COLORREF BG_HOVER        = kObsidianPalette.bgHover;
    inline static COLORREF BG_INPUT        = kObsidianPalette.bgInput;
    inline static COLORREF BG_INPUT_HOVER  = kObsidianPalette.bgInputHover;
    inline static COLORREF BORDER          = kObsidianPalette.border;
    inline static COLORREF BORDER_STRONG   = kObsidianPalette.borderStrong;
    inline static COLORREF DIVIDER         = kObsidianPalette.divider;
    inline static COLORREF TEXT_PRIMARY    = kObsidianPalette.textPrimary;
    inline static COLORREF TEXT_SECONDARY  = kObsidianPalette.textSecondary;
    inline static COLORREF TEXT_TERTIARY   = kObsidianPalette.textTertiary;
    inline static COLORREF TEXT_DISABLED   = kObsidianPalette.textDisabled;
    inline static COLORREF ACCENT          = kObsidianPalette.accent;
    inline static COLORREF ACCENT_HOVER    = kObsidianPalette.accentHover;
    inline static COLORREF ACCENT_TEXT     = kObsidianPalette.accentText;
    inline static COLORREF ACCENT_SOFT     = kObsidianPalette.accentSoft;
    inline static COLORREF SUCCESS         = kObsidianPalette.success;
    inline static COLORREF SUCCESS_SOFT    = kObsidianPalette.successSoft;
    inline static COLORREF DANGER          = kObsidianPalette.danger;
    inline static COLORREF DANGER_SOFT     = kObsidianPalette.dangerSoft;
    inline static COLORREF WARNING         = kObsidianPalette.warning;
    inline static COLORREF TOGGLE_OFF_TRACK = kObsidianPalette.toggleOffTrack;
    inline static COLORREF TOGGLE_OFF_KNOB  = kObsidianPalette.toggleOffKnob;

    // ---- Legacy aliases (references so they always track the active theme) ----
    inline static COLORREF& INVERSE         = ACCENT;
    inline static COLORREF& INVERSE_HOVER   = ACCENT_HOVER;
    inline static COLORREF& INVERSE_TEXT    = ACCENT_TEXT;
    inline static COLORREF& BG_DARK         = BG_CANVAS;
    inline static COLORREF& BG_HEADER       = BG_CANVAS;
    inline static COLORREF& BG_CARD         = BG_RAISED;
    inline static COLORREF& BG_CARD_HOVER   = BG_HOVER;
    inline static COLORREF& BG_FOOTER       = BG_SIDEBAR;
    inline static COLORREF& BORDER_HOVER    = BORDER_STRONG;
    inline static COLORREF& TEXT_DIM        = TEXT_TERTIARY;
    inline static COLORREF& GREEN_ACTIVE    = SUCCESS;
    inline static COLORREF& RED_DANGER      = DANGER;
    inline static COLORREF& DISABLED_SLIDER = BORDER;
    inline static COLORREF& FOCUS_RING      = ACCENT;

    static bool IsDark() { return CurrentId == ThemeId::Obsidian; }

    static void SetTheme(ThemeId id) {
        CurrentId = id;
        const ThemePalette& p = (id == ThemeId::Paper) ? kPaperPalette : kObsidianPalette;
        BG_CANVAS        = p.bgCanvas;
        BG_SIDEBAR       = p.bgSidebar;
        BG_SURFACE       = p.bgSurface;
        BG_RAISED        = p.bgRaised;
        BG_HOVER         = p.bgHover;
        BG_INPUT         = p.bgInput;
        BG_INPUT_HOVER   = p.bgInputHover;
        BORDER           = p.border;
        BORDER_STRONG    = p.borderStrong;
        DIVIDER          = p.divider;
        TEXT_PRIMARY     = p.textPrimary;
        TEXT_SECONDARY   = p.textSecondary;
        TEXT_TERTIARY    = p.textTertiary;
        TEXT_DISABLED    = p.textDisabled;
        ACCENT           = p.accent;
        ACCENT_HOVER     = p.accentHover;
        ACCENT_TEXT      = p.accentText;
        ACCENT_SOFT      = p.accentSoft;
        SUCCESS          = p.success;
        SUCCESS_SOFT     = p.successSoft;
        DANGER           = p.danger;
        DANGER_SOFT      = p.dangerSoft;
        WARNING          = p.warning;
        TOGGLE_OFF_TRACK = p.toggleOffTrack;
        TOGGLE_OFF_KNOB  = p.toggleOffKnob;
    }

    // Warm soft shadows on paper; on the near-black Obsidian theme a black
    // shadow is invisible, which is exactly what a flat dark theme wants.
    static Color ShadowColor(float alpha) {
        if (CurrentId == ThemeId::Paper) return Color(0.24f, 0.20f, 0.16f, alpha);
        return Color(0.0f, 0.0f, 0.0f, alpha);
    }

    // Monochrome profile badges: vary only lightness.
    static COLORREF GetBadgeColorForProfile(const std::wstring& id) {
        if (id == L"valorant")     return RGB(235, 235, 235);
        if (id == L"roblox")       return RGB(200, 200, 200);
        if (id == L"cs2")          return RGB(215, 215, 215);
        if (id == L"fortnite")     return RGB(190, 190, 190);
        if (id == L"apex_legends") return RGB(225, 225, 225);
        if (id == L"overwatch2")   return RGB(205, 205, 205);
        if (id == L"minecraft")    return RGB(180, 180, 180);
        if (id == L"gta5")         return RGB(210, 210, 210);
        return RGB(170, 170, 170);
    }

    static COLORREF Mix(COLORREF a, COLORREF b, float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        const int r = static_cast<int>(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t + 0.5f);
        const int g = static_cast<int>(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t + 0.5f);
        const int bl = static_cast<int>(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t + 0.5f);
        return RGB(r, g, bl);
    }

    // Per-channel lighten/darken so coloured tokens (the clay accent) keep
    // their hue instead of collapsing to grey.
    static COLORREF Lighten(COLORREF c, int delta) {
        const int r = std::min(255, static_cast<int>(GetRValue(c)) + delta);
        const int g = std::min(255, static_cast<int>(GetGValue(c)) + delta);
        const int bl = std::min(255, static_cast<int>(GetBValue(c)) + delta);
        return RGB(r, g, bl);
    }

    static COLORREF Darken(COLORREF c, int delta) {
        const int r = std::max(0, static_cast<int>(GetRValue(c)) - delta);
        const int g = std::max(0, static_cast<int>(GetGValue(c)) - delta);
        const int bl = std::max(0, static_cast<int>(GetBValue(c)) - delta);
        return RGB(r, g, bl);
    }

    // ---- Drawing helpers (Direct2D, coordinates in DIPs) ----

    static void FillSolidRect(Renderer& r, const RECT& rc, COLORREF color) {
        r.FillRect(D2D1::RectF(static_cast<float>(rc.left), static_cast<float>(rc.top),
                               static_cast<float>(rc.right), static_cast<float>(rc.bottom)),
                   color);
    }

    static void FillSolidRect(Renderer& r, float x, float y, float w, float h,
                              COLORREF color) {
        r.FillRect(x, y, w, h, color);
    }

    static void DrawRoundedCard(Renderer& r, const RECT& rc, int radius,
                                COLORREF bgColor, COLORREF borderColor) {
        r.DrawRoundedRect(D2D1::RectF(static_cast<float>(rc.left), static_cast<float>(rc.top),
                                      static_cast<float>(rc.right), static_cast<float>(rc.bottom)),
                          static_cast<float>(radius), static_cast<float>(radius),
                          bgColor, borderColor, 1.0f);
    }

    // A flat paper card: solid fill plus a hairline edge. `sheen` is kept for
    // signature compatibility; paper has no sheen.
    static void DrawCard(Renderer& r, const RECT& rc, int radius, COLORREF bgColor,
                         COLORREF borderColor, bool sheen = true) {
        (void)sheen;
        DrawRoundedCard(r, rc, radius, bgColor, borderColor);
    }

    static void DrawPill(Renderer& r, const RECT& rc, COLORREF bg, COLORREF border) {
        DrawRoundedCard(r, rc, (rc.bottom - rc.top), bg, border);
    }

    // A card that sits a little above the page. Shadows are soft and warm on
    // paper, and simply absent on the flat dark theme.
    static void DrawElevatedCard(Renderer& r, const RECT& rc, int radius,
                                 COLORREF bgColor, COLORREF borderColor,
                                 float shadowAlpha = 0.55f, float blur = 14.0f,
                                 float offsetY = 5.0f) {
        const D2D1_RECT_F rect(static_cast<float>(rc.left), static_cast<float>(rc.top),
                               static_cast<float>(rc.right), static_cast<float>(rc.bottom));
        if (shadowAlpha > 0.0f) {
            r.DrawShadowRoundedRect(rect, static_cast<float>(radius), static_cast<float>(radius),
                                    blur, offsetY, ShadowColor(shadowAlpha * 0.30f));
        }
        DrawRoundedCard(r, rc, radius, bgColor, borderColor);
    }

    // "Glass" now simply means "a card". The name survives because the old
    // call sites read as glass; every theme renders it as a flat, opaque card.
    static void DrawGlassCard(Renderer& r, const RECT& rc, int radius,
                              COLORREF baseColor, float strength = 1.0f,
                              float shadowAlpha = 0.45f, float blur = 16.0f,
                              float offsetY = 5.0f) {
        (void)strength;
        if (rc.right <= rc.left || rc.bottom <= rc.top) return;
        const D2D1_RECT_F rect(static_cast<float>(rc.left), static_cast<float>(rc.top),
                               static_cast<float>(rc.right), static_cast<float>(rc.bottom));
        if (shadowAlpha > 0.0f) {
            r.DrawShadowRoundedRect(rect, static_cast<float>(radius), static_cast<float>(radius),
                                    blur, offsetY, ShadowColor(shadowAlpha * 0.30f));
        }
        DrawRoundedCard(r, rc, radius, baseColor, BORDER);
    }

    // The specular edge of the old glass language. Kept as a no-op so existing
    // call sites compile unchanged.
    static void DrawGlassEdge(Renderer& r, const RECT& rc, float strength = 1.0f) {
        (void)r;
        (void)rc;
        (void)strength;
    }

    // A large surface such as the navigation rail or the page header. Flat
    // where possible; the hairline below separates it from the content.
    static void DrawPanel(Renderer& r, const RECT& rc, COLORREF top, COLORREF bottom,
                          bool hairlineAtBottom = true) {
        if (top == bottom) {
            FillSolidRect(r, rc, top);
        } else {
            r.FillRectGradient(rc, Color(top), Color(bottom));
        }
        if (hairlineAtBottom) {
            DrawHLine(r, rc.left, rc.right, rc.bottom - 1, DIVIDER);
        }
    }

    // The brand mark: a flat accent plate.
    static void DrawBrandPlate(Renderer& r, const RECT& rc, int radius) {
        DrawRoundedCard(r, rc, radius, ACCENT, ACCENT);
    }

    static void DrawHLine(Renderer& r, int x1, int x2, int y, COLORREF color) {
        r.DrawHairline(static_cast<float>(x1), static_cast<float>(y),
                       static_cast<float>(x2), static_cast<float>(y), color);
    }

    static void DrawVLine(Renderer& r, int x, int y1, int y2, COLORREF color) {
        r.DrawHairline(static_cast<float>(x), static_cast<float>(y1),
                       static_cast<float>(x), static_cast<float>(y2), color);
    }

    static void DrawDot(Renderer& r, int cx, int cy, int radius, COLORREF fill,
                        bool hollow = false) {
        if (hollow) {
            r.DrawEllipse(static_cast<float>(cx), static_cast<float>(cy),
                          static_cast<float>(radius), static_cast<float>(radius),
                          std::nullopt, fill, 1.0f);
        } else {
            r.DrawEllipse(static_cast<float>(cx), static_cast<float>(cy),
                          static_cast<float>(radius), static_cast<float>(radius),
                          fill, std::nullopt, 0.0f);
        }
    }

    // A status dot with a soft outer ring; reads as "live" without colour.
    static void DrawStatusDot(Renderer& r, int cx, int cy, int radius, COLORREF fill,
                              bool hollow = false) {
        if (hollow) {
            DrawDot(r, cx, cy, radius + 1, BORDER_STRONG, true);
        } else {
            DrawDot(r, cx, cy, radius + 2, Mix(fill, BG_CANVAS, 0.72f));
            DrawDot(r, cx, cy, radius, fill);
        }
    }

    // Thin, unobtrusive scroll indicator (3px) pinned inside a pane.
    static void DrawScrollbar(Renderer& r, const RECT& rcTrack, int scrollY,
                              int scrollMax, int viewportHeight) {
        if (scrollMax <= 0 || viewportHeight <= 0) return;
        const int trackH = rcTrack.bottom - rcTrack.top;
        if (trackH <= 8) return;

        int thumbH = static_cast<int>((static_cast<long long>(viewportHeight) * trackH) /
                                      (viewportHeight + scrollMax));
        thumbH = std::clamp(thumbH, 30, trackH);
        const int travel = trackH - thumbH;
        const int thumbY = rcTrack.top + (scrollMax > 0 ? (scrollY * travel) / scrollMax : 0);

        RECT rcThumb = { rcTrack.left, thumbY, rcTrack.right, thumbY + thumbH };
        DrawRoundedCard(r, rcThumb, 3, BORDER_STRONG, BORDER_STRONG);
    }

    // Keyboard/attention ring used by focused inputs and active controls.
    static void DrawFocusRing(Renderer& r, const RECT& rc, int radius, COLORREF color) {
        r.DrawRoundedRectOutline(D2D1::RectF(static_cast<float>(rc.left),
                                             static_cast<float>(rc.top),
                                             static_cast<float>(rc.right),
                                             static_cast<float>(rc.bottom)),
                                 static_cast<float>(radius), static_cast<float>(radius),
                                 color, 1.0f);
    }

    // Eyebrow labels ("WORKSPACE", "STARTUP"): small, semibold, wide tracking.
    static void DrawEyebrow(Renderer& r, const std::wstring& text, const RECT& rc,
                            COLORREF color, int size = 10, int weight = FW_SEMIBOLD,
                            float tracking = 1.2f) {
        TextStyle style = Typography::Eyebrow();
        style.sizeDip = static_cast<float>(size);
        style.weight = (weight >= FW_SEMIBOLD) ? DWRITE_FONT_WEIGHT_SEMI_BOLD
                                               : DWRITE_FONT_WEIGHT_NORMAL;
        style.trackingDip = tracking;
        r.DrawText(text, style, static_cast<float>(rc.left), static_cast<float>(rc.top),
                   static_cast<float>(rc.right - rc.left),
                   static_cast<float>(rc.bottom - rc.top), color,
                   DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
};

} // namespace prismshift

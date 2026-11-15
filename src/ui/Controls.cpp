#include "ui/Controls.h"
#include "ui/UITheme.h"
#include "ui/Icons.h"
#include <algorithm>
#include <cwchar>

namespace prismshift {
namespace Controls {

namespace {
struct Palette {
    COLORREF bg;
    COLORREF border;
    COLORREF text;
};

Palette Resolve(ButtonStyle style, float hover, bool pressed, bool enabled) {
    hover = std::clamp(hover, 0.0f, 1.0f);
    if (!enabled) {
        if (style == ButtonStyle::Primary) {
            return { UITheme::BG_RAISED, UITheme::BORDER, UITheme::TEXT_DISABLED };
        }
        if (style == ButtonStyle::Ghost) {
            return { UITheme::BG_CANVAS, UITheme::BG_CANVAS, UITheme::TEXT_DISABLED };
        }
        return { UITheme::BG_SURFACE, UITheme::BORDER, UITheme::TEXT_DISABLED };
    }

    switch (style) {
        case ButtonStyle::Primary: {
            COLORREF bg = UITheme::Mix(UITheme::ACCENT, UITheme::ACCENT_HOVER, hover);
            if (pressed) bg = UITheme::Darken(bg, 12);
            return { bg, bg, UITheme::ACCENT_TEXT };
        }
        case ButtonStyle::Secondary: {
            COLORREF bg = UITheme::Mix(UITheme::BG_RAISED, UITheme::BG_HOVER, hover);
            COLORREF border = UITheme::Mix(UITheme::BORDER, UITheme::BORDER_STRONG, hover);
            if (pressed) bg = UITheme::Darken(bg, 4);
            return { bg, border, UITheme::TEXT_PRIMARY };
        }
        case ButtonStyle::Ghost: {
            COLORREF bg = hover > 0.01f
                              ? UITheme::Mix(UITheme::BG_CANVAS, UITheme::BG_HOVER, hover)
                              : UITheme::BG_CANVAS;
            if (pressed) bg = UITheme::Darken(bg, 4);
            const COLORREF text =
                UITheme::Mix(UITheme::TEXT_SECONDARY, UITheme::TEXT_PRIMARY, hover);
            return { bg, bg, text };
        }
        case ButtonStyle::Danger:
        default: {
            COLORREF bg = UITheme::Mix(UITheme::BG_SURFACE, UITheme::DANGER_SOFT, hover);
            COLORREF border = UITheme::Mix(UITheme::BORDER, UITheme::DANGER, hover);
            const COLORREF text = UITheme::DANGER;
            return { bg, border, text };
        }
    }
}
} // namespace

void DrawButton(Renderer& r, const RECT& rc, const wchar_t* label, ButtonStyle style,
                float hover, bool pressed, bool enabled,
                const TextStyle& textStyle) {
    const Palette p = Resolve(style, hover, pressed, enabled);
    const int radius = 9;
    const int dy = pressed ? 1 : 0;
    RECT fillRc = { rc.left, rc.top + dy, rc.right, rc.bottom + dy };
    const D2D1_RECT_F fillRect(static_cast<float>(fillRc.left),
                                static_cast<float>(fillRc.top),
                                static_cast<float>(fillRc.right),
                                static_cast<float>(fillRc.bottom));

    const bool ghost = style == ButtonStyle::Ghost && p.bg == UITheme::BG_CANVAS;
    if (!ghost) {
        if (style == ButtonStyle::Primary && enabled) {
            // The primary action is a solid clay plate that lifts a little.
            r.DrawShadowRoundedRect(fillRect, static_cast<float>(radius),
                                     static_cast<float>(radius), 8.0f, 2.0f,
                                     UITheme::ShadowColor(0.16f));
            r.FillRoundedRectGradient(fillRect, static_cast<float>(radius),
                                      static_cast<float>(radius),
                                      Color(UITheme::Lighten(p.bg, 5)),
                                      Color(UITheme::Darken(p.bg, 3)));
        } else {
            // Secondary surfaces are flat, with a hairline to define their edge.
            r.FillRoundedRectGradient(fillRect, static_cast<float>(radius),
                                      static_cast<float>(radius),
                                      Color(UITheme::Lighten(p.bg, 2)),
                                      Color(p.bg));
            if (p.border != p.bg) {
                r.DrawRoundedRectOutline(fillRect, static_cast<float>(radius),
                                         static_cast<float>(radius), p.border, 1.0f);
            }
        }
    }

    RECT textRc = fillRc;
    r.DrawText(label, textStyle,
               static_cast<float>(textRc.left), static_cast<float>(textRc.top),
               static_cast<float>(textRc.right - textRc.left),
               static_cast<float>(textRc.bottom - textRc.top), p.text,
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void DrawButtonWithIcon(Renderer& r, const RECT& rc, wchar_t glyph,
                        const wchar_t* label, ButtonStyle style, float hover,
                        bool pressed, bool enabled,
                        const TextStyle& textStyle, int iconSize) {
    const Palette p = Resolve(style, hover, pressed, enabled);
    const int radius = 9;
    const int dy = pressed ? 1 : 0;

    const bool ghost = style == ButtonStyle::Ghost && p.bg == UITheme::BG_CANVAS;
    if (!ghost) {
        RECT fillRc = { rc.left, rc.top + dy, rc.right, rc.bottom + dy };
        const D2D1_RECT_F fillRect(static_cast<float>(fillRc.left),
                                    static_cast<float>(fillRc.top),
                                    static_cast<float>(fillRc.right),
                                    static_cast<float>(fillRc.bottom));
        if (style == ButtonStyle::Primary && enabled) {
            r.DrawShadowRoundedRect(fillRect, static_cast<float>(radius),
                                     static_cast<float>(radius), 8.0f, 2.0f,
                                     UITheme::ShadowColor(0.16f));
            r.FillRoundedRectGradient(fillRect, static_cast<float>(radius),
                                      static_cast<float>(radius),
                                      Color(UITheme::Lighten(p.bg, 5)),
                                      Color(UITheme::Darken(p.bg, 3)));
        } else {
            r.FillRoundedRectGradient(fillRect, static_cast<float>(radius),
                                      static_cast<float>(radius),
                                      Color(UITheme::Lighten(p.bg, 2)),
                                      Color(p.bg));
            if (p.border != p.bg) {
                r.DrawRoundedRectOutline(fillRect, static_cast<float>(radius),
                                         static_cast<float>(radius), p.border, 1.0f);
            }
        }
    }

    const int gap = 7;
    const float textWidth = r.MeasureText(label, textStyle);
    const float total = static_cast<float>(iconSize) + static_cast<float>(gap) + textWidth;
    const float startX =
        static_cast<float>(rc.left) +
        (static_cast<float>(rc.right - rc.left) - total) / 2.0f;
    const float cy = (static_cast<float>(rc.top + rc.bottom)) / 2.0f + dy;

    RECT rcIcon = { static_cast<LONG>(startX),
                    static_cast<LONG>(cy - iconSize / 2.0f),
                    static_cast<LONG>(startX + iconSize),
                    static_cast<LONG>(cy + iconSize / 2.0f + 1) };
    Icons::Draw(r, glyph, rcIcon, iconSize, p.text);

    const float textX = startX + iconSize + gap;
    const float textW = static_cast<float>(rc.right) - textX;
    r.DrawText(label, textStyle, textX, static_cast<float>(rc.top), textW,
               static_cast<float>(rc.bottom - rc.top), p.text,
               DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void DrawToggle(Renderer& r, const RECT& rc, float progress, float hover) {
    const int h = rc.bottom - rc.top;
    if (h <= 0) return;

    const float t = std::clamp(progress, 0.0f, 1.0f);
    const float eased = t * t * (3.0f - 2.0f * t);
    const float hoverClamped = std::clamp(hover, 0.0f, 1.0f);

    COLORREF track = UITheme::Mix(UITheme::TOGGLE_OFF_TRACK, UITheme::ACCENT, eased);
    if (eased < 0.5f) {
        track = UITheme::Mix(track, UITheme::BG_INPUT_HOVER, hoverClamped);
    }
    const COLORREF border =
        UITheme::Mix(UITheme::BORDER_STRONG, UITheme::ACCENT, eased);
    UITheme::DrawRoundedCard(r, rc, h, track, border);

    const int knob = h - 6;
    const int xOff =
        static_cast<int>(eased * (rc.right - rc.left - knob - 6) + 0.5f);
    const int kx = rc.left + 3 + xOff;
    const int kcy = rc.top + 3 + knob / 2;
    const float kcx = static_cast<float>(kx) + knob / 2.0f;

    const COLORREF knobFill =
        UITheme::Mix(UITheme::TOGGLE_OFF_KNOB, UITheme::ACCENT_TEXT, eased);
    const COLORREF knobBorder = UITheme::BORDER_STRONG;

    r.DrawShadowEllipse(kcx, static_cast<float>(kcy), knob / 2.0f, knob / 2.0f,
                        3.0f, 1.0f, UITheme::ShadowColor(0.18f));
    r.DrawEllipse(kcx, static_cast<float>(kcy), knob / 2.0f, knob / 2.0f,
                  knobFill, knobBorder, 1.0f);
}

void DrawGlowingOrb(Renderer& r, const RECT& rc, float progress, float hover,
                     float shimmer) {
    // The orb is no longer a lit sphere: it is a plain state indicator. Off is
    // a warm recessed disc, on is a solid clay disc, and the ring hardens as it
    // turns on. This is the "lightweight" half of the design.
    (void)shimmer;

    const int size = rc.right - rc.left;
    if (size <= 0) return;

    const float t = std::clamp(progress, 0.0f, 1.0f);
    const float eased = t * t * (3.0f - 2.0f * t);
    const float hoverT = std::clamp(hover, 0.0f, 1.0f);

    const float cx = (static_cast<float>(rc.left) + static_cast<float>(rc.right)) * 0.5f;
    const float cy = (static_cast<float>(rc.top) + static_cast<float>(rc.bottom)) * 0.5f;
    const float rOuter = size * 0.5f - 1.0f;

    const COLORREF fill = UITheme::Mix(UITheme::BG_INPUT, UITheme::ACCENT, eased);
    const COLORREF ring = UITheme::Mix(UITheme::BORDER_STRONG, UITheme::ACCENT, eased);
    r.DrawEllipse(cx, cy, rOuter, rOuter, fill, ring, 1.0f);

    // A quiet accent ring on hover is the only affordance we add.
    if (hoverT > 0.01f) {
        r.DrawEllipse(cx, cy, rOuter + 1.5f, rOuter + 1.5f,
                      std::nullopt, UITheme::Mix(UITheme::BORDER, UITheme::ACCENT, hoverT),
                      1.0f);
    }
}

void DrawSectionLabel(Renderer& r, int x, int y, const wchar_t* label,
                      const TextStyle& textStyle) {
    r.DrawText(label, textStyle, static_cast<float>(x), static_cast<float>(y), 400.0f, 18.0f,
               UITheme::TEXT_TERTIARY);
}

void DrawDivider(Renderer& r, int x1, int x2, int y) {
    UITheme::DrawHLine(r, x1, x2, y, UITheme::DIVIDER);
}

} // namespace Controls
} // namespace prismshift

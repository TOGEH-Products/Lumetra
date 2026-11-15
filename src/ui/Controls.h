#pragma once

#include <windows.h>

#include "ui/DirectWriteText.h"
#include "ui/Renderer.h"

namespace prismshift {
namespace Controls {

enum class ButtonStyle {
    Primary,    // inverted white block
    Secondary,  // outlined surface
    Ghost,      // text only, subtle hover wash
    Danger      // outlined, inverts on hover
};

// hover: 0..1 animated hover amount. pressed: mouse-down feedback.
// textStyle: the typography token used for the label (defaults to Button).
void DrawButton(Renderer& r, const RECT& rc, const wchar_t* label, ButtonStyle style,
                float hover, bool pressed, bool enabled,
                const TextStyle& textStyle = Typography::Button());

// Same, with a leading MDL2 glyph (used by Reset / Save / New profile).
void DrawButtonWithIcon(Renderer& r, const RECT& rc, wchar_t glyph,
                        const wchar_t* label, ButtonStyle style, float hover,
                        bool pressed, bool enabled,
                        const TextStyle& textStyle = Typography::Button(),
                        int iconSize = 13);

// progress: 0 = off, 1 = on, smoothly interpolated by the caller. The track
// and palette-specific knob colors transition together.
void DrawToggle(Renderer& r, const RECT& rc, float progress, float hover);

// The profile state indicator: a flat disc that slides from a warm recessed
// grey (off) to the clay accent (on) with a matching hairline ring. `progress`
// is the animated toggle value and `hover` deepens the ring.
void DrawGlowingOrb(Renderer& r, const RECT& rc, float progress, float hover,
                     float shimmer = 0.0f);

void DrawSectionLabel(Renderer& r, int x, int y, const wchar_t* label,
                      const TextStyle& textStyle = Typography::Muted());

void DrawDivider(Renderer& r, int x1, int x2, int y);

} // namespace Controls
} // namespace prismshift

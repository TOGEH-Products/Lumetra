#include "ui/Icons.h"

namespace prismshift {
namespace Icons {

void Draw(Renderer& r, wchar_t glyph, const RECT& rc, int size, COLORREF color) {
    TextStyle style;
    style.family = FontFamilies::Icons[0];
    style.sizeDip = static_cast<float>(size);
    style.weight = DWRITE_FONT_WEIGHT_NORMAL;

    std::wstring text(1, glyph);
    r.DrawText(text, style, static_cast<float>(rc.left), static_cast<float>(rc.top),
               static_cast<float>(rc.right - rc.left),
               static_cast<float>(rc.bottom - rc.top), color,
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

} // namespace Icons
} // namespace prismshift

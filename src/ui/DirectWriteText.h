#pragma once

#include <dwrite.h>
#include <d2d1.h>
#include <wrl/client.h>
#include <windows.h>
#include <string>
#include <map>

namespace prismshift {

using Microsoft::WRL::ComPtr;

// A named, reusable text style. Sizes are expressed in device-independent
// pixels (DIPs, 1/96 inch). DirectWrite performs layout and rasterization in
// DIPs, so these values map 1:1 onto the renderer's coordinate system and are
// never double-scaled.
struct TextStyle {
    std::wstring family;            // resolved font family name
    float sizeDip = 13.0f;          // em size in DIPs
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
    float trackingDip = 0.0f;       // extra inter-character spacing in DIPs
    float lineHeightMult = 0.0f;    // 0 = default; otherwise proportional line height
    bool trim = false;              // ellipsis trimming at the layout boundary
    bool wordWrap = false;

    bool operator<(const TextStyle& other) const noexcept;
};

// Predefined typography roles for the Lumetra "Monochrome Studio" design
// language. These are starting guidelines; individual elements may override.
// Sizes are in DIPs at 96 DPI and scale automatically with the monitor DPI.
struct Typography {
    static TextStyle PageTitle();      // 20 DIP Semibold
    static TextStyle SectionHeading(); // 16 DIP Semibold
    static TextStyle CardHeading();    // 14 DIP Semibold
    static TextStyle Body();           // 13 DIP Regular
    static TextStyle Secondary();      // 12 DIP Regular
    static TextStyle Muted();          // 11 DIP Regular
    static TextStyle SmallLabel();     // 11 DIP Medium
    static TextStyle Eyebrow();        // 10 DIP Semibold + tracking
    static TextStyle Button();         // 12 DIP Semibold
    static TextStyle Numeric();        // 11 DIP Mono Regular
    static TextStyle SmallMono();      // 9 DIP Mono
};

// Font families in preference order. The first family found on the system
// wins; this provides a reliable fallback chain instead of assuming any one
// font is installed (e.g. Segoe UI Variable is Windows 11 only).
struct FontFamilies {
    static const wchar_t* Ui[];       // Segoe UI Variable Text -> Segoe UI
    static const wchar_t* Display[];  // Segoe UI Variable Display -> Segoe UI
    static const wchar_t* Mono[];     // Cascadia Mono -> Consolas
    static const wchar_t* Icons[];    // Segoe MDL2 Assets
};

// DirectWrite text engine. Owns the IDWriteFactory, caches text formats, and
// creates/lays out text layouts. Device-independent: it survives render-target
// loss and DPI changes. All coordinates and sizes are in DIPs.
class DirectWriteText {
public:
    DirectWriteText();
    ~DirectWriteText();

    bool Initialize();

    [[nodiscard]] IDWriteFactory* Factory() const { return m_factory.Get(); }

    // Returns a cached text format for the given style.
    [[nodiscard]] IDWriteTextFormat* GetFormat(const TextStyle& style) const;

    // Measures the advance width of `text` in DIPs using `style` (used for
    // caret placement and right-aligned values).
    [[nodiscard]] float MeasureWidth(const std::wstring& text,
                                     const TextStyle& style) const;

    // Creates a text layout for `text` constrained to maxWidth/maxHeight DIPs.
    [[nodiscard]] HRESULT CreateLayout(const std::wstring& text,
                                       const TextStyle& style,
                                       float maxWidth, float maxHeight,
                                       IDWriteTextLayout** layout) const;

    // Draws `text` with an externally owned brush.
    void Draw(ID2D1RenderTarget* target, ID2D1Brush* brush,
              const std::wstring& text, const TextStyle& style,
              float x, float y, float maxWidth, float maxHeight,
              DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING,
              DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment =
                  DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    // Draws a pre-built layout with an externally owned brush.
    void DrawLayout(ID2D1RenderTarget* target, ID2D1Brush* brush,
                    IDWriteTextLayout* layout, float x, float y) const;

    // Invalidates all cached formats (theme / font change).
    void ClearCache();

private:
    // Draws text with deliberate inter-character tracking (eyebrow labels)
    // by positioning each glyph individually.
    void DrawTracked(ID2D1RenderTarget* target, ID2D1Brush* brush,
                     const std::wstring& text, const TextStyle& style,
                     float x, float y, float maxWidth, float maxHeight) const;

private:
    [[nodiscard]] static HRESULT ResolveFamily(
        IDWriteFontCollection* collection, const wchar_t* const* candidates,
        size_t candidateCount, std::wstring& outFamily);

    ComPtr<IDWriteFactory> m_factory;
    mutable std::map<TextStyle, ComPtr<IDWriteTextFormat>> m_formatCache;
};

} // namespace prismshift

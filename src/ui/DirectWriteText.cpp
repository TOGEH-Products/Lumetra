#include "ui/DirectWriteText.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace prismshift {

namespace {

bool FontFamilyExists(IDWriteFontCollection* collection, const wchar_t* family) {
    if (!collection || !family) return false;
    UINT32 count = collection->GetFontFamilyCount();
    for (UINT32 i = 0; i < count; ++i) {
        ComPtr<IDWriteFontFamily> fontFamily;
        if (FAILED(collection->GetFontFamily(i, &fontFamily))) continue;
        ComPtr<IDWriteLocalizedStrings> names;
        if (FAILED(fontFamily->GetFamilyNames(&names))) continue;
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (SUCCEEDED(names->FindLocaleName(L"en-us", &index, &exists)) && exists) {
            wchar_t name[256] = {};
            UINT32 nameSize = 0;
            names->GetStringLength(index, &nameSize);
            if (nameSize > 0 && nameSize < std::size(name)) {
                names->GetString(index, name, std::size(name));
                if (lstrcmpiW(name, family) == 0) return true;
            }
        }
    }
    return false;
}

} // namespace

bool TextStyle::operator<(const TextStyle& other) const noexcept {
    if (family != other.family) return family < other.family;
    if (sizeDip != other.sizeDip) return sizeDip < other.sizeDip;
    if (weight != other.weight) return weight < other.weight;
    if (style != other.style) return style < other.style;
    if (trackingDip != other.trackingDip) return trackingDip < other.trackingDip;
    if (trim != other.trim) return trim < other.trim;
    return wordWrap < other.wordWrap;
}

// ---------------------------------------------------------------------------
// Typography roles
// ---------------------------------------------------------------------------

TextStyle Typography::PageTitle() {
    TextStyle s;
    s.family = FontFamilies::Display[0];
    s.sizeDip = 20.0f;
    s.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    return s;
}

TextStyle Typography::SectionHeading() {
    TextStyle s;
    s.family = FontFamilies::Ui[0];
    s.sizeDip = 16.0f;
    s.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    return s;
}

TextStyle Typography::CardHeading() {
    TextStyle s;
    s.family = FontFamilies::Ui[0];
    s.sizeDip = 14.0f;
    s.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    return s;
}

TextStyle Typography::Body() {
    TextStyle s;
    s.family = FontFamilies::Ui[0];
    s.sizeDip = 13.0f;
    s.weight = DWRITE_FONT_WEIGHT_NORMAL;
    return s;
}

TextStyle Typography::Secondary() {
    TextStyle s;
    s.family = FontFamilies::Ui[0];
    s.sizeDip = 12.0f;
    s.weight = DWRITE_FONT_WEIGHT_NORMAL;
    return s;
}

TextStyle Typography::Muted() {
    TextStyle s;
    s.family = FontFamilies::Ui[0];
    s.sizeDip = 11.0f;
    s.weight = DWRITE_FONT_WEIGHT_NORMAL;
    return s;
}

TextStyle Typography::SmallLabel() {
    TextStyle s;
    s.family = FontFamilies::Ui[0];
    s.sizeDip = 11.0f;
    s.weight = DWRITE_FONT_WEIGHT_MEDIUM;
    return s;
}

TextStyle Typography::Eyebrow() {
    TextStyle s;
    s.family = FontFamilies::Ui[0];
    s.sizeDip = 10.0f;
    s.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    s.trackingDip = 1.2f; // deliberate uppercase tracking
    return s;
}

TextStyle Typography::Button() {
    TextStyle s;
    s.family = FontFamilies::Ui[0];
    s.sizeDip = 12.0f;
    s.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    return s;
}

TextStyle Typography::Numeric() {
    TextStyle s;
    s.family = FontFamilies::Mono[0];
    s.sizeDip = 11.0f;
    s.weight = DWRITE_FONT_WEIGHT_NORMAL;
    return s;
}

TextStyle Typography::SmallMono() {
    TextStyle s;
    s.family = FontFamilies::Mono[0];
    s.sizeDip = 9.0f;
    s.weight = DWRITE_FONT_WEIGHT_NORMAL;
    return s;
}

// ---------------------------------------------------------------------------
// FontFamilies definitions
// ---------------------------------------------------------------------------

const wchar_t* FontFamilies::Ui[] = {
    L"Segoe UI Variable Text",
    L"Segoe UI Variable",
    L"Segoe UI",
    nullptr,
};

const wchar_t* FontFamilies::Display[] = {
    L"Segoe UI Variable Display",
    L"Segoe UI Variable Text",
    L"Segoe UI",
    nullptr,
};

const wchar_t* FontFamilies::Mono[] = {
    L"Cascadia Mono",
    L"Cascadia Code",
    L"Consolas",
    nullptr,
};

const wchar_t* FontFamilies::Icons[] = {
    L"Segoe MDL2 Assets",
    nullptr,
};

HRESULT DirectWriteText::ResolveFamily(IDWriteFontCollection* collection,
                                       const wchar_t* const* candidates,
                                       size_t candidateCount,
                                       std::wstring& outFamily) {
    for (size_t i = 0; i < candidateCount; ++i) {
        if (!candidates[i]) continue;
        if (FontFamilyExists(collection, candidates[i])) {
            outFamily = candidates[i];
            return S_OK;
        }
    }
    // Last resort: use the first candidate even if not enumerated; DirectWrite
    // will perform its own substitution rather than failing.
    if (candidateCount > 0 && candidates[0]) {
        outFamily = candidates[0];
        return S_OK;
    }
    return E_FAIL;
}

// ---------------------------------------------------------------------------
// DirectWriteText
// ---------------------------------------------------------------------------

DirectWriteText::DirectWriteText() = default;

DirectWriteText::~DirectWriteText() = default;

bool DirectWriteText::Initialize() {
    if (m_factory) return true;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                     __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown**>(m_factory.GetAddressOf()));
    return SUCCEEDED(hr);
}

IDWriteTextFormat* DirectWriteText::GetFormat(const TextStyle& style) const {
    auto it = m_formatCache.find(style);
    if (it != m_formatCache.end()) return it->second.Get();

    ComPtr<IDWriteFontCollection> collection;
    static_cast<void>(m_factory->GetSystemFontCollection(&collection));
    std::wstring family;

    // Pick the candidate list based on the requested family.
    const wchar_t* const* candidates = FontFamilies::Ui;
    size_t candidateCount = 3;
    if (style.family == FontFamilies::Display[0] ||
        style.family == FontFamilies::Display[1] ||
        style.family == FontFamilies::Display[2]) {
        candidates = FontFamilies::Display;
        candidateCount = 3;
    } else if (style.family == FontFamilies::Mono[0] ||
               style.family == FontFamilies::Mono[1] ||
               style.family == FontFamilies::Mono[2]) {
        candidates = FontFamilies::Mono;
        candidateCount = 3;
    } else if (style.family == FontFamilies::Icons[0]) {
        candidates = FontFamilies::Icons;
        candidateCount = 1;
    } else {
        candidates = FontFamilies::Ui;
        candidateCount = 3;
    }
    static_cast<void>(ResolveFamily(collection.Get(), candidates, candidateCount, family));

    ComPtr<IDWriteTextFormat> format;
    HRESULT hr = m_factory->CreateTextFormat(
        family.c_str(), nullptr, style.weight, style.style,
        DWRITE_FONT_STRETCH_NORMAL, style.sizeDip, L"en-us", &format);
    if (FAILED(hr)) return nullptr;

    if (style.trackingDip > 0.0f) {
        // Note: this SDK's IDWriteTextFormat does not expose SetCharacterSpacing.
        // Deliberate tracking (eyebrow labels) is applied per-glyph in DrawTracked.
    }

    if (style.lineHeightMult > 0.0f) {
        format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_PROPORTIONAL,
                              style.sizeDip * style.lineHeightMult,
                              style.sizeDip * 0.85f);
    }

    if (style.trim) {
        DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        format->SetTrimming(&trimming, nullptr);
    }

    format->SetWordWrapping(style.wordWrap ? DWRITE_WORD_WRAPPING_WRAP
                                           : DWRITE_WORD_WRAPPING_NO_WRAP);

    m_formatCache.emplace(style, format);
    return format ? format.Get() : nullptr;
}

float DirectWriteText::MeasureWidth(const std::wstring& text,
                                    const TextStyle& style) const {
    if (text.empty()) return 0.0f;
    IDWriteTextFormat* format = GetFormat(style);
    if (!format) return 0.0f;

    ComPtr<IDWriteTextLayout> layout;
    HRESULT hr = m_factory->CreateTextLayout(
        text.c_str(), static_cast<UINT32>(text.size()), format,
        100000.0f, 100000.0f, &layout);
    if (FAILED(hr)) return 0.0f;

    DWRITE_TEXT_METRICS metrics = {};
    layout->GetMetrics(&metrics);
    return metrics.widthIncludingTrailingWhitespace;
}

HRESULT DirectWriteText::CreateLayout(const std::wstring& text,
                                      const TextStyle& style,
                                      float maxWidth, float maxHeight,
                                      IDWriteTextLayout** layout) const {
    if (!layout) return E_INVALIDARG;
    *layout = nullptr;
    IDWriteTextFormat* format = GetFormat(style);
    if (!format) return E_FAIL;
    return m_factory->CreateTextLayout(text.c_str(),
                                       static_cast<UINT32>(text.size()),
                                       format, maxWidth, maxHeight, layout);
}

void DirectWriteText::DrawTracked(ID2D1RenderTarget* target, ID2D1Brush* brush,
                                  const std::wstring& text, const TextStyle& style,
                                  float x, float y, float maxWidth,
                                  float maxHeight) const {
    if (text.empty() || !target || !brush) return;

    const float tracking = style.trackingDip;

    // Measure the full string once (without tracking) to know its natural width.
    ComPtr<IDWriteTextLayout> full;
    if (FAILED(CreateLayout(text, style, maxWidth, maxHeight, &full)) || !full) return;
    DWRITE_TEXT_METRICS fullMetrics = {};
    full->GetMetrics(&fullMetrics);

    const float naturalWidth = fullMetrics.widthIncludingTrailingWhitespace;
    const float trackedWidth =
        naturalWidth + tracking * static_cast<float>(std::max(static_cast<int>(text.size()) - 1, 0));

    float cx = x;
    for (size_t i = 0; i < text.size(); ++i) {
        std::wstring one(1, text[i]);
        ComPtr<IDWriteTextLayout> glyph;
        const float remainingWidth = std::max(0.0f, maxWidth - (cx - x));
        if (FAILED(CreateLayout(one, style, remainingWidth, maxHeight, &glyph)) ||
            !glyph) {
            break;
        }
        target->DrawTextLayout(D2D1::Point2F(cx, y), glyph.Get(), brush,
                               D2D1_DRAW_TEXT_OPTIONS_NONE);
        DWRITE_TEXT_METRICS gm = {};
        glyph->GetMetrics(&gm);
        cx += gm.widthIncludingTrailingWhitespace + tracking;
    }
}

void DirectWriteText::Draw(ID2D1RenderTarget* target, ID2D1Brush* brush,
                           const std::wstring& text, const TextStyle& style,
                           float x, float y, float maxWidth, float maxHeight,
                           DWRITE_TEXT_ALIGNMENT alignment,
                           DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment) {
    if (text.empty() || !target || !brush) return;

    // Deliberate character tracking (e.g. uppercase eyebrow labels) is applied
    // per-glyph because this SDK's IDWriteTextFormat does not expose
    // SetCharacterSpacing.
    if (style.trackingDip > 0.0f && text.size() > 1 &&
        alignment == DWRITE_TEXT_ALIGNMENT_LEADING) {
        DrawTracked(target, brush, text, style, x, y, maxWidth, maxHeight);
        return;
    }

    ComPtr<IDWriteTextLayout> layout;
    HRESULT hr = CreateLayout(text, style, maxWidth, maxHeight, &layout);
    if (FAILED(hr) || !layout) return;

    layout->SetTextAlignment(alignment);
    layout->SetParagraphAlignment(paragraphAlignment);
    target->DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), brush,
                           D2D1_DRAW_TEXT_OPTIONS_NONE);
}

void DirectWriteText::DrawLayout(ID2D1RenderTarget* target, ID2D1Brush* brush,
                                 IDWriteTextLayout* layout, float x,
                                 float y) const {
    if (!target || !brush || !layout) return;
    target->DrawTextLayout(D2D1::Point2F(x, y), layout, brush,
                           D2D1_DRAW_TEXT_OPTIONS_NONE);
}

void DirectWriteText::ClearCache() { m_formatCache.clear(); }

} // namespace prismshift

#include "ui/Renderer.h"

#include <algorithm>
#include <cmath>
#include <d2d1helper.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace prismshift {

Color::Color(COLORREF rgb, float alpha)
    : r(static_cast<float>(GetRValue(rgb)) / 255.0f),
      g(static_cast<float>(GetGValue(rgb)) / 255.0f),
      b(static_cast<float>(GetBValue(rgb)) / 255.0f),
      a(alpha) {}

D2D1_COLOR_F Color::ToD2D() const {
    return D2D1::ColorF(r, g, b, a);
}

Color Color::WithAlpha(float alpha) const {
    return Color(r, g, b, alpha);
}

Color Color::Lerp(const Color& a, const Color& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return Color(a.r + (b.r - a.r) * t,
                 a.g + (b.g - a.g) * t,
                 a.b + (b.b - a.b) * t,
                 a.a + (b.a - a.a) * t);
}

bool Color::operator<(const Color& other) const {
    if (r != other.r) return r < other.r;
    if (g != other.g) return g < other.g;
    if (b != other.b) return b < other.b;
    return a < other.a;
}

namespace {
D2D1::ColorF ColorFFromColorref(COLORREF color) {
    return D2D1::ColorF(static_cast<float>(GetRValue(color)) / 255.0f,
                        static_cast<float>(GetGValue(color)) / 255.0f,
                        static_cast<float>(GetBValue(color)) / 255.0f);
}
} // namespace

D2D1::ColorF Renderer::ColorF(COLORREF color) {
    return ColorFFromColorref(color);
}

Renderer::Renderer() = default;

Renderer::~Renderer() { Shutdown(); }

void Renderer::Shutdown() {
    DiscardTargetResources();
    m_factory.Reset();
    m_hwnd = nullptr;
    m_dpi = 96;
    m_text.ClearCache();
}

bool Renderer::Initialize(HWND hwnd) {
    m_hwnd = hwnd;
    m_dpi = ::GetDpiForWindow(hwnd);
    if (m_dpi == 0) m_dpi = 96;

    D2D1_FACTORY_OPTIONS opts = {};
    opts.debugLevel = D2D1_DEBUG_LEVEL_NONE;
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, opts,
                                   m_factory.GetAddressOf());
    if (FAILED(hr)) return false;

    if (!m_text.Initialize()) return false;

    RECT client{};
    ::GetClientRect(hwnd, &client);
    return CreateTarget(m_dpi, static_cast<UINT>(client.right - client.left),
                        static_cast<UINT>(client.bottom - client.top));
}

bool Renderer::CreateTarget(UINT dpi, UINT physW, UINT physH) {
    if (physW == 0 || physH == 0) physW = physH = 1;

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_HARDWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        static_cast<float>(dpi), static_cast<float>(dpi),
        D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);

    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
        m_hwnd, D2D1::SizeU(physW, physH), D2D1_PRESENT_OPTIONS_NONE);

    ComPtr<ID2D1HwndRenderTarget> target;
    HRESULT hr = m_factory->CreateHwndRenderTarget(&rtProps, &hwndProps, &target);
    if (FAILED(hr)) {
        // Fall back to software rendering if hardware is unavailable.
        rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            static_cast<float>(dpi), static_cast<float>(dpi),
            D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
        hr = m_factory->CreateHwndRenderTarget(&rtProps, &hwndProps, &target);
        if (FAILED(hr)) return false;
    }

    m_target = target;
    m_dpi = dpi;

    // Opaque window rendering: ClearType gives the sharpest color text.
    m_target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    m_target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    m_brushes.clear();
    return true;
}

void Renderer::DiscardTargetResources() {
    m_brushes.clear();
    m_target.Reset();
}

bool Renderer::RecreateTarget() {
    if (!m_hwnd) return false;
    DiscardTargetResources();
    RECT client{};
    ::GetClientRect(m_hwnd, &client);
    UINT dpi = ::GetDpiForWindow(m_hwnd);
    if (dpi == 0) dpi = m_dpi;
    return CreateTarget(dpi,
                        static_cast<UINT>(client.right - client.left),
                        static_cast<UINT>(client.bottom - client.top));
}

void Renderer::OnDpiChanged(UINT newDpi, const RECT* suggestedWindowRect) {
    if (newDpi == 0) newDpi = 96;

    // Apply the suggested window rectangle so the client area scales with DPI.
    if (suggestedWindowRect && m_hwnd) {
        ::SetWindowPos(m_hwnd, nullptr, suggestedWindowRect->left, suggestedWindowRect->top,
                       suggestedWindowRect->right - suggestedWindowRect->left,
                       suggestedWindowRect->bottom - suggestedWindowRect->top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // Recreate the render target at the new DPI; device-independent resources
    // (factories, text formats) are retained.
    static_cast<void>(RecreateTarget());
}

void Renderer::Resize(UINT physW, UINT physH) {
    if (m_target) {
        m_target->Resize(D2D1::SizeU(physW ? physW : 1, physH ? physH : 1));
    }
}

void Renderer::BeginDraw() { m_target->BeginDraw(); }

HRESULT Renderer::EndDraw() {
    HRESULT hr = m_target->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        static_cast<void>(RecreateTarget());
    }
    return hr;
}

void Renderer::Clear(D2D1::ColorF color) { m_target->Clear(color); }

ComPtr<ID2D1SolidColorBrush> Renderer::GetBrush(COLORREF color) {
    return GetBrush(Color(color));
}

ComPtr<ID2D1SolidColorBrush> Renderer::GetBrush(const Color& color) {
    auto it = m_brushes.find(color);
    if (it != m_brushes.end()) return it->second;

    ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(m_target->CreateSolidColorBrush(color.ToD2D(), &brush))) {
        m_brushes.emplace(color, brush);
    }
    return brush;
}

void Renderer::FillRect(const D2D1_RECT_F& rect, COLORREF color) {
    auto brush = GetBrush(color);
    if (brush) m_target->FillRectangle(rect, brush.Get());
}

void Renderer::FillRect(float x, float y, float w, float h, COLORREF color) {
    FillRect(D2D1::RectF(x, y, x + w, y + h), color);
}

void Renderer::DrawRect(const D2D1_RECT_F& rect, COLORREF color,
                        float strokeWidth) {
    auto brush = GetBrush(color);
    if (brush) m_target->DrawRectangle(rect, brush.Get(), strokeWidth);
}

void Renderer::DrawRoundedRect(const D2D1_RECT_F& rect, float radiusX,
                               float radiusY, COLORREF fill, COLORREF stroke,
                               float strokeWidth) {
    D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(rect, radiusX, radiusY);
    auto fillBrush = GetBrush(fill);
    if (fillBrush) m_target->FillRoundedRectangle(rounded, fillBrush.Get());
    if (stroke != fill) {
        auto strokeBrush = GetBrush(stroke);
        if (strokeBrush) m_target->DrawRoundedRectangle(rounded, strokeBrush.Get(),
                                                        strokeWidth);
    }
}

void Renderer::DrawRoundedRectOutline(const D2D1_RECT_F& rect, float radiusX,
                                      float radiusY, COLORREF color,
                                      float strokeWidth) {
    D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(rect, radiusX, radiusY);
    auto brush = GetBrush(color);
    if (brush) m_target->DrawRoundedRectangle(rounded, brush.Get(), strokeWidth);
}

void Renderer::DrawRoundedRectOutline(const D2D1_RECT_F& rect, float radiusX,
                                      float radiusY, const Color& color,
                                      float strokeWidth) {
    if (!m_target || color.a <= 0.0f) return;
    D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(rect, radiusX, radiusY);
    auto brush = GetBrush(color);
    if (brush) m_target->DrawRoundedRectangle(rounded, brush.Get(), strokeWidth);
}

void Renderer::DrawLine(float x1, float y1, float x2, float y2,
                        COLORREF color, float strokeWidth) {
    auto brush = GetBrush(color);
    if (brush) m_target->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2),
                                  brush.Get(), strokeWidth);
}

void Renderer::DrawLine(float x1, float y1, float x2, float y2,
                        const Color& color, float strokeWidth) {
    if (!m_target || color.a <= 0.0f) return;
    auto brush = GetBrush(color);
    if (brush) m_target->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2),
                                  brush.Get(), strokeWidth);
}

void Renderer::DrawEllipse(float cx, float cy, float rx, float ry,
                           std::optional<COLORREF> fill,
                           std::optional<COLORREF> stroke, float strokeWidth) {
    D2D1_ELLIPSE ellipse = D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry);
    if (fill) {
        auto brush = GetBrush(*fill);
        if (brush) m_target->FillEllipse(ellipse, brush.Get());
    }
    if (stroke) {
        auto brush = GetBrush(*stroke);
        if (brush) m_target->DrawEllipse(ellipse, brush.Get(), strokeWidth);
    }
}

void Renderer::FillEllipse(float cx, float cy, float rx, float ry, const Color& color) {
    if (!m_target || color.a <= 0.0f) return;
    auto brush = GetBrush(color);
    if (!brush) return;
    m_target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry),
                          brush.Get());
}

void Renderer::DrawHairline(float x1, float y1, float x2, float y2,
                            COLORREF color) {
    // Snap to physical pixels so a 1-px line does not blur between pixels.
    float sy1 = SnapToPixel(y1);
    float sy2 = SnapToPixel(y2);
    float sx1 = SnapToPixel(x1);
    float sx2 = SnapToPixel(x2);
    DrawLine(sx1, sy1, sx2, sy2, color, 1.0f);
}

// ---- Depth and light ----------------------------------------------------

ComPtr<ID2D1RadialGradientBrush> Renderer::CreateRadialBrush(
    float centerX, float centerY, float radiusX, float radiusY,
    float originOffsetX, float originOffsetY,
    const D2D1_GRADIENT_STOP* stops, UINT stopCount) {
    if (!m_target || radiusX <= 0.0f || radiusY <= 0.0f || stopCount == 0) {
        return nullptr;
    }

    ComPtr<ID2D1GradientStopCollection> collection;
    if (FAILED(m_target->CreateGradientStopCollection(stops, stopCount,
                                                      collection.GetAddressOf()))) {
        return nullptr;
    }

    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES props{};
    props.center = D2D1::Point2F(centerX, centerY);
    props.gradientOriginOffset = D2D1::Point2F(originOffsetX, originOffsetY);
    props.radiusX = radiusX;
    props.radiusY = radiusY;

    ComPtr<ID2D1RadialGradientBrush> brush;
    if (FAILED(m_target->CreateRadialGradientBrush(props, collection.Get(),
                                                   brush.GetAddressOf()))) {
        return nullptr;
    }
    return brush;
}

void Renderer::FillRoundedRectGradient(const D2D1_RECT_F& rect, float radiusX,
                                       float radiusY, const Color& top,
                                       const Color& bottom) {
    FillRoundedRectGradient(rect, radiusX, radiusY,
                            D2D1_POINT_2F{ rect.left, rect.top },
                            D2D1_POINT_2F{ rect.left, rect.bottom },
                            top, bottom);
}

void Renderer::FillRoundedRectGradient(const D2D1_RECT_F& rect, float radiusX,
                                       float radiusY, const D2D1_POINT_2F& start,
                                       const D2D1_POINT_2F& end, const Color& top,
                                       const Color& bottom) {
    if (!m_target) return;
    if (start.x == end.x && start.y == end.y) return;

    D2D1_GRADIENT_STOP stops[2];
    stops[0] = D2D1_GRADIENT_STOP{ 0.0f, top.ToD2D() };
    stops[1] = D2D1_GRADIENT_STOP{ 1.0f, bottom.ToD2D() };

    ComPtr<ID2D1GradientStopCollection> collection;
    if (FAILED(m_target->CreateGradientStopCollection(stops, 2,
                                                      collection.GetAddressOf()))) {
        return;
    }

    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props{};
    props.startPoint = start;
    props.endPoint = end;

    ComPtr<ID2D1LinearGradientBrush> brush;
    if (FAILED(m_target->CreateLinearGradientBrush(props, collection.Get(),
                                                   brush.GetAddressOf()))) {
        return;
    }
    m_target->FillRoundedRectangle(D2D1::RoundedRect(rect, radiusX, radiusY),
                                   brush.Get());
}

void Renderer::FillRoundedRectGradient(const RECT& rc, int radius,
                                       const Color& top, const Color& bottom) {
    FillRoundedRectGradient(
        D2D1::RectF(static_cast<float>(rc.left), static_cast<float>(rc.top),
                    static_cast<float>(rc.right), static_cast<float>(rc.bottom)),
        static_cast<float>(radius), static_cast<float>(radius), top, bottom);
}

void Renderer::FillRectGradient(const D2D1_RECT_F& rect, const Color& top,
                                const Color& bottom) {
    if (!m_target) return;

    D2D1_GRADIENT_STOP stops[2];
    stops[0] = D2D1_GRADIENT_STOP{ 0.0f, top.ToD2D() };
    stops[1] = D2D1_GRADIENT_STOP{ 1.0f, bottom.ToD2D() };

    ComPtr<ID2D1GradientStopCollection> collection;
    if (FAILED(m_target->CreateGradientStopCollection(stops, 2,
                                                      collection.GetAddressOf()))) {
        return;
    }

    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props{};
    props.startPoint = D2D1::Point2F(rect.left, rect.top);
    props.endPoint = D2D1::Point2F(rect.left, rect.bottom);

    ComPtr<ID2D1LinearGradientBrush> brush;
    if (FAILED(m_target->CreateLinearGradientBrush(props, collection.Get(),
                                                   brush.GetAddressOf()))) {
        return;
    }
    m_target->FillRectangle(rect, brush.Get());
}

void Renderer::FillRectGradient(const RECT& rc, const Color& top, const Color& bottom) {
    FillRectGradient(D2D1::RectF(static_cast<float>(rc.left), static_cast<float>(rc.top),
                                 static_cast<float>(rc.right), static_cast<float>(rc.bottom)),
                     top, bottom);
}

void Renderer::FillEllipseRadialGradient(float cx, float cy, float rx, float ry,
                                         float focusX, float focusY,
                                         const Color& inner, const Color& outer) {
    FillEllipseRadialGradient(cx, cy, rx, ry, focusX, focusY, inner,
                              Color::Lerp(inner, outer, 0.45f), outer);
}

void Renderer::FillEllipseRadialGradient(float cx, float cy, float rx, float ry,
                                         float focusX, float focusY,
                                         const Color& highlight, const Color& body,
                                         const Color& shade) {
    if (!m_target) return;

    // Three stops give a sphere its structure: a lit core, a body tone and a
    // rim that falls away into shadow.
    D2D1_GRADIENT_STOP stops[3];
    stops[0] = D2D1_GRADIENT_STOP{ 0.0f, highlight.ToD2D() };
    stops[1] = D2D1_GRADIENT_STOP{ 0.55f, body.ToD2D() };
    stops[2] = D2D1_GRADIENT_STOP{ 1.0f, shade.ToD2D() };

    const float radius = std::max(rx, ry);
    auto brush = CreateRadialBrush(cx, cy, radius, radius,
                                   focusX - cx, focusY - cy, stops, 3);
    if (!brush) return;
    m_target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry),
                          brush.Get());
}

void Renderer::DrawGlowEllipse(float cx, float cy, float rx, float ry,
                               float spread, const Color& color,
                               float intensity) {
    if (!m_target || intensity <= 0.0f || color.a <= 0.0f) return;
    if (spread <= 1.0f) return;

    // The halo is strongest just outside the shape and fades away from it,
    // so it reads as light bleeding into the background rather than a disc.
    const float peak = 1.0f / spread;
    const float glowX = rx * spread;
    const float glowY = ry * spread;

    D2D1_GRADIENT_STOP stops[3];
    stops[0] = D2D1_GRADIENT_STOP{ 0.0f, color.WithAlpha(0.0f).ToD2D() };
    stops[1] = D2D1_GRADIENT_STOP{ peak, color.WithAlpha(color.a * intensity).ToD2D() };
    stops[2] = D2D1_GRADIENT_STOP{ 1.0f, color.WithAlpha(0.0f).ToD2D() };

    auto brush = CreateRadialBrush(cx, cy, glowX, glowY, 0.0f, 0.0f, stops, 3);
    if (!brush) return;
    m_target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), glowX, glowY),
                          brush.Get());
}

void Renderer::DrawShadowRoundedRect(const D2D1_RECT_F& rect, float radiusX,
                                      float radiusY, float blur, float offsetY,
                                      const Color& color) {
    if (!m_target || blur <= 0.0f || color.a <= 0.0f) return;

    constexpr int kSteps = 8;
    constexpr float kWeightSum = 36.0f; // 8 + 7 + ... + 1
    for (int j = 1; j <= kSteps; ++j) {
        const float grow = blur * (static_cast<float>(j) / static_cast<float>(kSteps));
        D2D1_RECT_F layer = rect;
        layer.left -= grow;
        layer.right += grow;
        layer.top -= grow - offsetY;
        layer.bottom += grow + offsetY;

        // The tight layers carry most of the density, the wide ones only the
        // soft falloff.
        const float weight = static_cast<float>(kSteps - j + 1) / kWeightSum;
        auto brush = GetBrush(Color(color.r, color.g, color.b, color.a * weight));
        if (!brush) continue;

        m_target->FillRoundedRectangle(
            D2D1::RoundedRect(layer, radiusX + grow, radiusY + grow), brush.Get());
    }
}

void Renderer::DrawShadowEllipse(float cx, float cy, float rx, float ry,
                                 float blur, float offsetY, const Color& color) {
    if (!m_target || blur <= 0.0f || color.a <= 0.0f) return;

    constexpr int kSteps = 8;
    constexpr float kWeightSum = 36.0f;
    for (int j = 1; j <= kSteps; ++j) {
        const float grow = blur * (static_cast<float>(j) / static_cast<float>(kSteps));
        const float weight = static_cast<float>(kSteps - j + 1) / kWeightSum;
        auto brush = GetBrush(Color(color.r, color.g, color.b, color.a * weight));
        if (!brush) continue;

        m_target->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(cx, cy + offsetY), rx + grow, ry + grow),
            brush.Get());
    }
}

float Renderer::SnapToPixel(float dip) const {
    float phys = dip * PixelsPerDip();
    int snapped = static_cast<int>(std::floor(phys + 0.5f));
    return snapped / PixelsPerDip();
}

D2D1_POINT_2F Renderer::ToDip(POINT pt) const {
    float scale = PixelsPerDip();
    return D2D1::Point2F(static_cast<float>(pt.x) / scale,
                         static_cast<float>(pt.y) / scale);
}

D2D1_RECT_F Renderer::ToDip(const RECT& rc) const {
    float scale = PixelsPerDip();
    return D2D1::RectF(static_cast<float>(rc.left) / scale,
                       static_cast<float>(rc.top) / scale,
                       static_cast<float>(rc.right) / scale,
                       static_cast<float>(rc.bottom) / scale);
}

POINT Renderer::ToPhysical(D2D1_POINT_2F pt) const {
    float scale = PixelsPerDip();
    return POINT{static_cast<LONG>(std::floor(pt.x * scale + 0.5f)),
                 static_cast<LONG>(std::floor(pt.y * scale + 0.5f))};
}

void Renderer::DrawText(const std::wstring& text, const TextStyle& style,
                        float x, float y, float maxWidth, float maxHeight,
                        COLORREF color,
                        DWRITE_TEXT_ALIGNMENT alignment,
                        DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment,
                        bool trim) {
    auto brush = GetBrush(color);
    if (!brush) return;
    TextStyle s = style;
    s.trim = trim;
    m_text.Draw(m_target.Get(), brush.Get(), text, s, x, y, maxWidth,
                maxHeight, alignment, paragraphAlignment);
}

float Renderer::MeasureText(const std::wstring& text,
                            const TextStyle& style) const {
    return m_text.MeasureWidth(text, style);
}

ComPtr<ID2D1Bitmap> Renderer::CreateBitmapFromIcon(HICON hIcon,
                                                   int iconSizePx) const {
    if (!hIcon || !m_target) return nullptr;

    // Use GDI to render the icon into a DIB so we can read the pixels.
    const int sz = iconSizePx;

    // Create a 32-bpp DIB to receive the icon pixels.
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = sz;
    bmi.bmiHeader.biHeight      = -sz; // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem    = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp  = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, hdcScreen);

    if (!hBmp || !bits) {
        DeleteDC(hdcMem);
        return nullptr;
    }

    HGDIOBJ old = SelectObject(hdcMem, hBmp);
    // Fill with transparent black first.
    RECT fill = { 0, 0, sz, sz };
    ::FillRect(hdcMem, &fill, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    DrawIconEx(hdcMem, 0, 0, hIcon, sz, sz, 0, nullptr, DI_NORMAL);
    SelectObject(hdcMem, old);
    DeleteDC(hdcMem);

    // Copy pixel data into a D2D1Bitmap.
    // D2D uses BGRA pre-multiplied; Windows DIBs are BGRA non-premultiplied.
    // For icons we accept the slight inaccuracy on semi-transparent edges.
    D2D1_BITMAP_PROPERTIES props;
    props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                          D2D1_ALPHA_MODE_IGNORE);
    props.dpiX = static_cast<float>(m_dpi);
    props.dpiY = static_cast<float>(m_dpi);

    ComPtr<ID2D1Bitmap> bitmap;
    HRESULT hr = m_target->CreateBitmap(
        D2D1::SizeU(sz, sz), bits,
        static_cast<UINT32>(sz * 4),
        props, &bitmap);

    DeleteObject(hBmp);
    return SUCCEEDED(hr) ? bitmap : nullptr;
}

ComPtr<ID2D1Bitmap> Renderer::LoadBitmapFromFile(const std::wstring& filePath) const {
    if (!m_target || filePath.empty()) return nullptr;

    ComPtr<IWICImagingFactory> wicFactory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&wicFactory)))) {
        return nullptr;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wicFactory->CreateDecoderFromFilename(filePath.c_str(), nullptr,
                                                     GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                                     &decoder))) {
        return nullptr;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return nullptr;

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wicFactory->CreateFormatConverter(&converter))) return nullptr;
    if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                    WICBitmapDitherTypeNone, nullptr, 0.0,
                                    WICBitmapPaletteTypeMedianCut))) {
        return nullptr;
    }

    ComPtr<ID2D1Bitmap> bitmap;
    HRESULT hr = m_target->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &bitmap);
    return SUCCEEDED(hr) ? bitmap : nullptr;
}

void Renderer::DrawBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F& destRect,
                          float opacity) {
    if (m_target && bitmap)
        m_target->DrawBitmap(bitmap, destRect, opacity);
}

void Renderer::DrawBitmapRounded(ID2D1Bitmap* bitmap, const D2D1_RECT_F& destRect,
                                float cornerRadius, float opacity) {
    if (!m_target || !bitmap) return;

    ComPtr<ID2D1RoundedRectangleGeometry> roundedGeom;
    D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(destRect, cornerRadius, cornerRadius);
    if (FAILED(m_factory->CreateRoundedRectangleGeometry(roundedRect, &roundedGeom)))
        return;

    m_target->PushLayer(
        D2D1::LayerParameters(destRect, roundedGeom.Get()),
        nullptr);
    m_target->DrawBitmap(bitmap, destRect, opacity);
    m_target->PopLayer();
}

} // namespace prismshift

#pragma once

#include <wrl/client.h>
#include <d2d1.h>
#include <wincodec.h>
#include <windows.h>
#include <map>
#include <optional>

#include "ui/DirectWriteText.h"

namespace prismshift {

using Microsoft::WRL::ComPtr;

// A colour with an alpha channel.
//
// Direct2D composites in premultiplied alpha, so translucent work needs a
// real alpha channel - COLORREF cannot express one. This is the currency for
// gradients, glows and shadows.
struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float red, float green, float blue, float alpha = 1.0f)
        : r(red), g(green), b(blue), a(alpha) {}
    explicit Color(COLORREF rgb, float alpha = 1.0f);

    // Builds a colour from 8 bit components, the way designers think about
    // them. The float constructor works in 0..1, which is easy to get wrong.
    [[nodiscard]] static constexpr Color Rgb(int red, int green, int blue,
                                             float alpha = 1.0f) {
        return Color(red / 255.0f, green / 255.0f, blue / 255.0f, alpha);
    }

    [[nodiscard]] D2D1_COLOR_F ToD2D() const;
    [[nodiscard]] Color WithAlpha(float alpha) const;
    [[nodiscard]] static Color Lerp(const Color& a, const Color& b, float t);
    [[nodiscard]] bool operator<(const Color& other) const;
};

// Direct2D rendering backend for Lumetra.
//
// All drawing happens in device-independent pixels (DIPs, 1/96 inch). The
// render target is created at the window's current monitor DPI (via
// GetDpiForWindow), so Direct2D automatically maps DIPs onto physical pixels.
// Layout literals therefore stay constant across DPI settings and are never
// scaled twice.
//
// Device-independent resources (D2D factory, DWrite factory, text formats)
// live for the renderer's lifetime. Device-dependent resources (render target,
// brushes) are recreated when the target is lost or the DPI changes.
class Renderer {
public:
    Renderer();
    ~Renderer();

    bool Initialize(HWND hwnd);
    void Shutdown();

    // Starts a frame. Call drawing functions between BeginDraw/EndDraw.
    void BeginDraw();

    // Finishes a frame. Returns the raw HRESULT; callers should treat
    // D2DERR_RECREATE_TARGET as a signal to call RecreateTarget().
    [[nodiscard]] HRESULT EndDraw();

    [[nodiscard]] HWND GetHwnd() const { return m_hwnd; }
    [[nodiscard]] UINT GetDpi() const { return m_dpi; }
    [[nodiscard]] float DipsPerPixel() const { return 96.0f / static_cast<float>(m_dpi); }
    [[nodiscard]] float PixelsPerDip() const { return static_cast<float>(m_dpi) / 96.0f; }

    [[nodiscard]] ID2D1RenderTarget* GetTarget() const { return m_target.Get(); }
    [[nodiscard]] IDWriteFactory* GetDWriteFactory() const { return m_text.Factory(); }
    [[nodiscard]] DirectWriteText& Text() { return m_text; }

    // Converts physical window coordinates (mouse, WM_DPICHANGED rect) to DIPs.
    [[nodiscard]] D2D1_POINT_2F ToDip(POINT pt) const;
    [[nodiscard]] D2D1_RECT_F ToDip(const RECT& rc) const;
    [[nodiscard]] POINT ToPhysical(D2D1_POINT_2F pt) const;

    // Snaps a DIP coordinate to the nearest physical pixel boundary so 1-pixel
    // lines stay crisp at every scaling level.
    [[nodiscard]] float SnapToPixel(float dip) const;

    // Handles WM_DPICHANGED: updates the tracked DPI, resizes the render
    // target to the suggested window size, and recreates device-dependent
    // resources so everything renders at the new DPI.
    void OnDpiChanged(UINT newDpi, const RECT* suggestedWindowRect = nullptr);

    // Resizes the render target to a new client size (physical pixels).
    void Resize(UINT physW, UINT physH);

    // Releases and recreates the render target and its device-dependent
    // resources. Used after device/target loss or DPI change.
    [[nodiscard]] bool RecreateTarget();

    // ---- Frame ----
    void Clear(D2D1::ColorF color);

    // ---- Primitives (coordinates in DIPs, colors as COLORREF) ----
    void FillRect(const D2D1_RECT_F& rect, COLORREF color);
    void FillRect(float x, float y, float w, float h, COLORREF color);

    void DrawRect(const D2D1_RECT_F& rect, COLORREF color, float strokeWidth = 1.0f);
    void DrawRoundedRect(const D2D1_RECT_F& rect, float radiusX, float radiusY,
                         COLORREF fill, COLORREF stroke, float strokeWidth = 1.0f);

    void DrawRoundedRectOutline(const D2D1_RECT_F& rect, float radiusX,
                                float radiusY, COLORREF color,
                                float strokeWidth = 1.0f);

    // Same, with a colour that carries an alpha channel. Translucent outlines
    // are what separate a lit rim from a drawn border.
    void DrawRoundedRectOutline(const D2D1_RECT_F& rect, float radiusX,
                                float radiusY, const Color& color,
                                float strokeWidth = 1.0f);

    void DrawLine(float x1, float y1, float x2, float y2,
                  COLORREF color, float strokeWidth = 1.0f);

    void DrawLine(float x1, float y1, float x2, float y2,
                  const Color& color, float strokeWidth = 1.0f);

    void DrawEllipse(float cx, float cy, float rx, float ry,
                     std::optional<COLORREF> fill = std::nullopt,
                     std::optional<COLORREF> stroke = std::nullopt, float strokeWidth = 1.0f);

    // Fills an ellipse with a translucent colour.
    void FillEllipse(float cx, float cy, float rx, float ry, const Color& color);

    // 1-pixel hairline, snapped to a physical pixel boundary.
    void DrawHairline(float x1, float y1, float x2, float y2, COLORREF color);

    // ---- Depth and light -------------------------------------------------
    // These are what separate a flat mock-up from a surface that reads as a
    // physical material: gradients give a face a direction, shadows give it
    // a distance from the plane behind it, and glows give it energy.

    // Vertical linear gradient inside a rounded rectangle.
    void FillRoundedRectGradient(const D2D1_RECT_F& rect, float radiusX, float radiusY,
                                  const Color& top, const Color& bottom);
    void FillRoundedRectGradient(const RECT& rect, int radius,
                                  const Color& top, const Color& bottom);

    // Vertical linear gradient inside a rounded rectangle with explicit
    // gradient endpoints. The endpoints may sit anywhere inside the rect,
    // which is what lets a short specular band hug the top edge of a surface
    // instead of fading across its whole height.
    void FillRoundedRectGradient(const D2D1_RECT_F& rect, float radiusX, float radiusY,
                                  const D2D1_POINT_2F& start, const D2D1_POINT_2F& end,
                                  const Color& top, const Color& bottom);

    // Vertical linear gradient inside a plain rectangle.
    void FillRectGradient(const D2D1_RECT_F& rect, const Color& top, const Color& bottom);
    void FillRectGradient(const RECT& rect, const Color& top, const Color& bottom);

    // Radial gradient inside an ellipse. The focus point may sit off-centre,
    // which is what makes a sphere look lit from one side.
    void FillEllipseRadialGradient(float cx, float cy, float rx, float ry,
                                   float focusX, float focusY,
                                   const Color& inner, const Color& outer);

    // Three stop variant: a lit core, a body tone and a shaded rim. This is
    // the gradient a sphere is made of.
    void FillEllipseRadialGradient(float cx, float cy, float rx, float ry,
                                   float focusX, float focusY,
                                   const Color& highlight, const Color& body,
                                   const Color& shade);

    // A soft halo around a shape. The shape is described by its radii, the
    // glow reaches `spread` times as far and is strongest right at the edge
    // of the shape, fading out into the background.
    void DrawGlowEllipse(float cx, float cy, float rx, float ry, float spread,
                         const Color& color, float intensity);

    // Soft drop shadows, built from a few stacked translucent shapes.
    void DrawShadowRoundedRect(const D2D1_RECT_F& rect, float radiusX, float radiusY,
                               float blur, float offsetY, const Color& color);
    void DrawShadowEllipse(float cx, float cy, float rx, float ry,
                           float blur, float offsetY, const Color& color);

    // ---- Text (convenience wrappers over DirectWriteText) ----
    void DrawText(const std::wstring& text, const TextStyle& style,
                  float x, float y, float maxWidth, float maxHeight,
                  COLORREF color,
                  DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING,
                  DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment =
                      DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                  bool trim = false);

    [[nodiscard]] float MeasureText(const std::wstring& text,
                                    const TextStyle& style) const;

    // ---- Bitmap ----
    // Creates a D2D bitmap from an HICON (e.g. extracted from an executable).
    // Returns nullptr on failure. The bitmap is NOT cached — callers should
    // cache it themselves if it will be redrawn every frame.
    [[nodiscard]] Microsoft::WRL::ComPtr<ID2D1Bitmap>
        CreateBitmapFromIcon(HICON hIcon, int iconSizePx = 32) const;

    // Loads a JPG/PNG image file into a D2D1Bitmap via WIC.
    [[nodiscard]] Microsoft::WRL::ComPtr<ID2D1Bitmap>
        LoadBitmapFromFile(const std::wstring& filePath) const;

    // Draws an ID2D1Bitmap scaled to fill destRect (DIP coordinates).
    void DrawBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F& destRect,
                    float opacity = 1.0f);

    // Draws an ID2D1Bitmap with rounded corners by clipping to a rounded rect.
    void DrawBitmapRounded(ID2D1Bitmap* bitmap, const D2D1_RECT_F& destRect,
                          float cornerRadius, float opacity = 1.0f);

private:
    static D2D1::ColorF ColorF(COLORREF color);
    ComPtr<ID2D1SolidColorBrush> GetBrush(COLORREF color);
    ComPtr<ID2D1SolidColorBrush> GetBrush(const Color& color);

    // Creates a radial gradient brush. The centre is given in device space
    // (DIPs) and the stops run from the origin outwards. The origin offset
    // moves the bright end of the gradient, which is what lights a sphere
    // from one side.
    ComPtr<ID2D1RadialGradientBrush> CreateRadialBrush(float centerX, float centerY,
                                                        float radiusX, float radiusY,
                                                        float originOffsetX,
                                                        float originOffsetY,
                                                        const D2D1_GRADIENT_STOP* stops,
                                                        UINT stopCount);

    bool CreateTarget(UINT dpi, UINT physW, UINT physH);
    void DiscardTargetResources();

    ComPtr<ID2D1Factory> m_factory;
    ComPtr<ID2D1HwndRenderTarget> m_target;
    HWND m_hwnd = nullptr;
    UINT m_dpi = 96;
    DirectWriteText m_text;

    std::map<Color, ComPtr<ID2D1SolidColorBrush>> m_brushes;
};

} // namespace prismshift

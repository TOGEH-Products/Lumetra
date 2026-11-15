#pragma once

#include <windows.h>

#include "ui/DirectWriteText.h"
#include "ui/Renderer.h"

namespace prismshift {
namespace Icons {

// Segoe MDL2 Assets ships on Windows 10 (1607+) and Windows 11, so it is the
// most reliable monochrome, scalable icon source with zero dependencies.
constexpr const wchar_t* FONT = L"Segoe MDL2 Assets";

// MDL2 / Fluent-compatible glyph codepoints.
constexpr wchar_t Profiles     = L'\uE8FD';
constexpr wchar_t Pc           = L'\uE977'; // PC1 (Segoe MDL2 Assets)
constexpr wchar_t Monitor      = L'\uE7F4'; // Display / monitor
constexpr wchar_t Controller   = L'\uE7FC'; // Game controller
constexpr wchar_t Settings     = L'\uE713';
constexpr wchar_t Add          = L'\uE710';
constexpr wchar_t Search       = L'\uE721';
constexpr wchar_t Back         = L'\uE72B';
constexpr wchar_t ChevronRight = L'\uE76C';
constexpr wchar_t Pause        = L'\uE769';
constexpr wchar_t Play         = L'\uE768';
constexpr wchar_t Save         = L'\uE74E';
constexpr wchar_t Reset        = L'\uE7A7';
constexpr wchar_t Compare      = L'\uE8AB';
constexpr wchar_t Delete       = L'\uE74D';
constexpr wchar_t Copy         = L'\uE8C8';
constexpr wchar_t Close        = L'\uE711';
constexpr wchar_t Folder       = L'\uE8B7';
constexpr wchar_t Info         = L'\uE946';
constexpr wchar_t Check        = L'\uE73E';
constexpr wchar_t Warning      = L'\uE7BA';
constexpr wchar_t Edit         = L'\uE70F';
constexpr wchar_t Refresh      = L'\uE72C';
constexpr wchar_t ChevronDown  = L'\uE70D';
constexpr wchar_t Bell         = L'\uE7ED';
constexpr wchar_t Keyboard     = L'\uE92E';
constexpr wchar_t Maximize     = L'\uE922';
constexpr wchar_t FullScreen   = L'\uE1D9'; // Resolution icon
constexpr wchar_t Color        = L'\uE790'; // Color palette

// Draws a single glyph centered inside `rc`. Coordinates are in DIPs.
void Draw(Renderer& r, wchar_t glyph, const RECT& rc, int size, COLORREF color);

} // namespace Icons
} // namespace prismshift

#include "ui/ProfileListView.h"
#include "ui/UITheme.h"
#include "ui/Icons.h"
#include "ui/Controls.h"
#include "utilities/AppIcon.h"
#include <shlobj.h>
#include <algorithm>
#include <cwctype>

namespace prismshift {

namespace {
constexpr int PANE_PAD = 20;
constexpr int HEADER_HEIGHT = 100;
constexpr int ROW_HEIGHT = 84;
constexpr int ROW_GAP = 6;
constexpr int FOOTER_HEIGHT = 78;
constexpr int SEARCH_HEIGHT = 38;
constexpr int AVATAR_LEFT = 42;
constexpr int AVATAR_SIZE = 32;
constexpr int TEXT_GAP = 12;
constexpr int TEXT_RIGHT_PAD = 12;
constexpr int NAME_LINE_HEIGHT = 16;
constexpr int EXECUTABLE_LINE_HEIGHT = 13;
constexpr int TEXT_BLOCK_GAP = 3;
constexpr float MONO_CHARACTER_WIDTH = 6.0f;

std::wstring MonogramFor(const ColorProfile& profile) {
    if (profile.isDefault || profile.id == L"monitor") return L"M";
    for (wchar_t ch : profile.name) {
        if (!std::iswspace(ch)) {
            return std::wstring(1, static_cast<wchar_t>(std::towupper(ch)));
        }
    }
    return L"?";
}

std::wstring SubtitleFor(const ColorProfile& profile) {
    if (profile.isDefault || profile.id == L"monitor") {
        return L"Fallback for every application";
    }
    if (!profile.targetProcesses.empty()) return profile.targetProcesses[0];
    if (!profile.executablePath.empty()) return profile.executablePath;
    return L"No target executable";
}

std::wstring WrapExecutableText(const std::wstring& text, size_t maxCharacters) {
    std::wstring wrapped;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = std::min(start + maxCharacters, text.size());
        if (end < text.size()) {
            const size_t breakAt = text.find_last_of(L" -_/\\.", end - 1);
            if (breakAt != std::wstring::npos &&
                breakAt >= start + maxCharacters / 2) {
                end = breakAt + 1;
            }
        }
        if (!wrapped.empty()) wrapped.push_back(L'\n');
        wrapped.append(text, start, end - start);
        start = end;
    }
    return wrapped;
}

size_t WrappedLineCount(const std::wstring& text) {
    if (text.empty()) return 1;
    return static_cast<size_t>(std::count(text.begin(), text.end(), L'\n')) + 1;
}
} // namespace

ProfileListView::ProfileListView() = default;

ProfileListView::~ProfileListView() {
    // Release any cached bitmaps before the D2D target can be destroyed.
    m_iconCache.clear();
}

Microsoft::WRL::ComPtr<ID2D1Bitmap>
ProfileListView::GetOrCreateIconBitmap(Renderer& r, const ColorProfile& profile) {
    // Use profile ID as the cache key so user-provided icons override everything.
    const std::wstring& key = profile.id;
    auto it = m_iconCache.find(key);
    if (it != m_iconCache.end()) {
        return it->second.attempted ? it->second.bitmap : nullptr;
    }

    // Mark as attempted immediately so we don't retry on every frame if it fails.
    IconEntry& entry = m_iconCache[key];
    entry.attempted = true;

    // First, try to load from ProfileIcons/{id}/icon.*
    wchar_t appData[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData) == S_OK) {
        std::wstring iconDir = std::wstring(appData) + L"\\Lumetra\\ProfileIcons\\" + profile.id + L"\\";
        std::vector<std::wstring> iconPaths = {
            iconDir + L"icon.png",
            iconDir + L"icon.jpg",
            iconDir + L"icon.jpeg"
        };

        for (const auto& path : iconPaths) {
            entry.bitmap = r.LoadBitmapFromFile(path);
            if (entry.bitmap) return entry.bitmap;
        }
    }

    // Fallback: try to extract icon from the executable if we have a full path.
    if (!profile.executablePath.empty()) {
        HICON hIcon = ExtractExeIcon(profile.executablePath, 32);
        if (hIcon) {
            entry.bitmap = r.CreateBitmapFromIcon(hIcon, 32);
            DestroyIcon(hIcon);
            if (entry.bitmap) return entry.bitmap;
        }
    }

    // No icon available; caller will fall back to monogram.
    return nullptr;
}

RECT ProfileListView::GetSearchRect(const RECT& rcBounds) const {
    return RECT{ rcBounds.left + PANE_PAD, rcBounds.top + 54,
                 rcBounds.right - PANE_PAD, rcBounds.top + 54 + SEARCH_HEIGHT };
}

RECT ProfileListView::GetSearchClearRect(const RECT& rcBounds) const {
    RECT rcSearch = GetSearchRect(rcBounds);
    const int size = 20;
    const int right = rcSearch.right - 9;
    return RECT{ right - size, rcSearch.top + (SEARCH_HEIGHT - size) / 2,
                 right, rcSearch.top + (SEARCH_HEIGHT - size) / 2 + size };
}

int ProfileListView::GetRowHeight(const ColorProfile& profile,
                                  const RECT& rcBounds) const {
    const int rowWidth = rcBounds.right - rcBounds.left - 2 * PANE_PAD;
    const int textWidth = rowWidth - AVATAR_LEFT - AVATAR_SIZE - TEXT_GAP - TEXT_RIGHT_PAD;
    const size_t maxCharacters = static_cast<size_t>(
        std::max(1, static_cast<int>(static_cast<float>(textWidth) /
                                     MONO_CHARACTER_WIDTH)));
    const size_t lines = WrappedLineCount(
        WrapExecutableText(SubtitleFor(profile), maxCharacters));
    const int textHeight = NAME_LINE_HEIGHT + TEXT_BLOCK_GAP +
                           static_cast<int>(lines) * EXECUTABLE_LINE_HEIGHT;
    return std::max(ROW_HEIGHT, textHeight + 24);
}

int ProfileListView::GetRowsContentHeight(
    const RECT& rcBounds, const std::vector<ColorProfile>& profiles) const {
    int total = 0;
    for (const ColorProfile& profile : profiles) {
        total += GetRowHeight(profile, rcBounds) + ROW_GAP;
    }
    return profiles.empty() ? 0 : total - ROW_GAP;
}

std::vector<RECT> ProfileListView::BuildRowRects(
    const RECT& rcBounds, const std::vector<ColorProfile>& profiles) const {
    std::vector<RECT> rows;
    rows.reserve(profiles.size());
    int top = rcBounds.top + HEADER_HEIGHT - ScrollOffset();
    for (const ColorProfile& profile : profiles) {
        const int height = GetRowHeight(profile, rcBounds);
        rows.push_back(RECT{ rcBounds.left + PANE_PAD, top,
                             rcBounds.right - PANE_PAD, top + height });
        top += height + ROW_GAP;
    }
    return rows;
}

RECT ProfileListView::GetToggleRect(const RECT& rcRow) const {
    // The orb sits at the far left of the row, clear of the avatar.
    const int orbSize = 18;
    const int left = rcRow.left + 14;
    const int top = rcRow.top + ((rcRow.bottom - rcRow.top) - orbSize) / 2;
    return RECT{ left, top, left + orbSize, top + orbSize };
}

RECT ProfileListView::GetAddBtnRect(const RECT& rcBounds) const {
    return RECT{ rcBounds.left + PANE_PAD, rcBounds.bottom - 62,
                 rcBounds.right - PANE_PAD, rcBounds.bottom - 16 };
}

int ProfileListView::ScrollOffset() const {
    return static_cast<int>(m_scrollAnim.value + 0.5f);
}

bool ProfileListView::SyncRowAnimations(
    const std::vector<ColorProfile>& profiles) {
    bool rowsChanged = m_rowToggleIds.size() != profiles.size();
    m_rowHover.resize(profiles.size());
    m_rowToggle.resize(profiles.size());
    m_rowOrbHover.resize(profiles.size());
    m_rowToggleIds.resize(profiles.size());

    for (size_t i = 0; i < profiles.size(); ++i) {
        if (m_rowToggleIds[i] != profiles[i].id) {
            m_rowToggleIds[i] = profiles[i].id;
            m_rowToggle[i].Set(profiles[i].enabled ? 1.0f : 0.0f);
            rowsChanged = true;
        }
    }

    if (rowsChanged) {
        m_hoverIndex = -1;
        m_hoverToggleIndex = -1;
        for (Anim& animation : m_rowHover) animation.Set(0.0f);
        for (Anim& animation : m_rowOrbHover) animation.Set(0.0f);
    }
    return rowsChanged;
}

void ProfileListView::Paint(Renderer& r, const RECT& rcBounds,
                            const std::vector<ColorProfile>& profiles) {
    UITheme::FillSolidRect(r, rcBounds, UITheme::BG_SURFACE);

    // Right edge separator between the master list and the detail pane.
    RECT rcEdge = { rcBounds.right - 1, rcBounds.top, rcBounds.right, rcBounds.bottom };
    UITheme::FillSolidRect(r, rcEdge, UITheme::BORDER);

    // Keep per-row channels in sync with the (possibly filtered) profile list.
    SyncRowAnimations(profiles);
    for (size_t i = 0; i < profiles.size(); ++i) {
        m_rowHover[i].Towards(static_cast<int>(i) == m_hoverIndex);
        m_rowToggle[i].Towards(profiles[i].enabled);
        m_rowOrbHover[i].Towards(static_cast<int>(i) == m_hoverToggleIndex);
    }
    m_addHover.Towards(m_hoverAddBtn);
    const TextStyle sectionStyle = Typography::SectionHeading();
    const TextStyle bodyStyle = Typography::Body();
    const TextStyle mutedStyle = Typography::Muted();

    // ---- Header ----
    UITheme::DrawEyebrow(r, L"LIBRARY",
                         RECT{ rcBounds.left + PANE_PAD, rcBounds.top + 18,
                               rcBounds.right - PANE_PAD, rcBounds.top + 32 },
                         UITheme::TEXT_TERTIARY, 9, FW_SEMIBOLD, 1.0f);

    r.DrawText(L"Profiles", sectionStyle,
               static_cast<float>(rcBounds.left + PANE_PAD),
               static_cast<float>(rcBounds.top + 30),
               static_cast<float>((rcBounds.right - 68) - (rcBounds.left + PANE_PAD)), 22.0f,
               UITheme::TEXT_PRIMARY);

    // Count pill
    RECT rcCount = { rcBounds.right - PANE_PAD - 30, rcBounds.top + 31,
                     rcBounds.right - PANE_PAD, rcBounds.top + 51 };
    UITheme::DrawPill(r, rcCount, UITheme::BG_RAISED, UITheme::BORDER);
    {
        TextStyle countStyle = Typography::Eyebrow();
        countStyle.sizeDip = 10.0f;
        countStyle.trackingDip = 0.0f;
        std::wstring countText = std::to_wstring(profiles.size());
        r.DrawText(countText, countStyle,
                   static_cast<float>(rcCount.left), static_cast<float>(rcCount.top),
                   static_cast<float>(rcCount.right - rcCount.left),
                   static_cast<float>(rcCount.bottom - rcCount.top),
                   UITheme::TEXT_SECONDARY,
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // ---- Search field ----
    RECT rcSearch = GetSearchRect(rcBounds);
    const float focus = m_searchFocus.value;
    const COLORREF searchBg = UITheme::Mix(UITheme::BG_INPUT, UITheme::BG_INPUT_HOVER, focus);
    const COLORREF searchBorder = UITheme::Mix(UITheme::BORDER, UITheme::BORDER_STRONG, focus);
    // The field is a recessed surface: darker at the top, slightly lighter
    // at the bottom, with a hairline that brightens when it takes focus.
    {
        const D2D1_RECT_F searchRect(static_cast<float>(rcSearch.left),
                                      static_cast<float>(rcSearch.top),
                                      static_cast<float>(rcSearch.right),
                                      static_cast<float>(rcSearch.bottom));
        r.FillRoundedRectGradient(searchRect, 10.0f, 10.0f,
                                  Color(UITheme::Darken(UITheme::BG_INPUT, 6)),
                                  Color(searchBg));
        r.DrawRoundedRectOutline(searchRect, 10.0f, 10.0f, searchBorder, 1.0f);
    }

    RECT rcSearchIcon = { rcSearch.left + 12, rcSearch.top + 12,
                          rcSearch.left + 26, rcSearch.top + 26 };
    Icons::Draw(r, Icons::Search, rcSearchIcon, 13, UITheme::TEXT_TERTIARY);

    TextStyle searchStyle = Typography::Secondary();
    searchStyle.trim = true;
    std::wstring searchText = m_searchQuery.empty() ? L"Search profiles" : m_searchQuery;
    const int searchTextRight = m_searchQuery.empty()
                                    ? rcSearch.right - 12
                                    : GetSearchClearRect(rcBounds).left - 4;
    r.DrawText(searchText, searchStyle,
               static_cast<float>(rcSearch.left + 34), static_cast<float>(rcSearch.top),
               static_cast<float>(searchTextRight - (rcSearch.left + 34)),
               static_cast<float>(rcSearch.bottom - rcSearch.top),
               m_searchQuery.empty() ? UITheme::TEXT_TERTIARY : UITheme::TEXT_PRIMARY,
                   DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (m_searchFocused) {
        const float textWidth =
            r.MeasureText(searchText, Typography::Secondary());
        int caretX = static_cast<int>(static_cast<float>(rcSearch.left + 34) +
                                      textWidth + 2.0f);
        caretX = std::min(caretX, searchTextRight - 2);
        RECT rcCaret = { caretX, rcSearch.top + 11, caretX + 1,
                         rcSearch.bottom - 11 };
        UITheme::FillSolidRect(r, rcCaret, UITheme::TEXT_PRIMARY);
    }

    if (!m_searchQuery.empty()) {
        RECT rcClear = GetSearchClearRect(rcBounds);
        UITheme::DrawRoundedCard(r, rcClear, 10, UITheme::BG_RAISED,
                                 UITheme::BG_RAISED);
        Icons::Draw(r, Icons::Close, rcClear, 9, UITheme::TEXT_SECONDARY);
    }

    // ---- Rows ----
    RECT rcAdd = GetAddBtnRect(rcBounds);
    int rowsTop = rcBounds.top + HEADER_HEIGHT;
    int rowsBottom = rcAdd.top - 12;
    int rowsHeight = GetRowsContentHeight(rcBounds, profiles);
    m_scrollMax = std::max(0, rowsHeight - (rowsBottom - rowsTop));
    m_scrollY = std::clamp(m_scrollY, 0, m_scrollMax);
    m_scrollAnim.To(static_cast<float>(m_scrollY));
    const std::vector<RECT> rowRects = BuildRowRects(rcBounds, profiles);

    // Keep scrolled rows inside the list viewport so they can never paint over
    // the fixed header, search field or the pinned add button.
    r.GetTarget()->PushAxisAlignedClip(
        D2D1::RectF(static_cast<float>(rcBounds.left), static_cast<float>(rowsTop),
                    static_cast<float>(rcBounds.right), static_cast<float>(rowsBottom)),
        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    for (size_t i = 0; i < profiles.size(); ++i) {
        RECT rcRow = rowRects[i];
        if (rcRow.bottom < rowsTop || rcRow.top > rowsBottom) continue;

        const ColorProfile& profile = profiles[i];
        const bool isMonitor = profile.isDefault || profile.id == L"monitor";
        const bool isSelected = !m_selectedId.empty() && profile.id == m_selectedId;
        const float hover = std::clamp(m_rowHover[i].value, 0.0f, 1.0f);

        COLORREF bg = UITheme::Mix(UITheme::BG_SURFACE, UITheme::BG_HOVER, hover);
        COLORREF border = UITheme::Mix(UITheme::BG_SURFACE,
                                       UITheme::BORDER_STRONG, hover);
        if (isSelected) {
            bg = UITheme::Mix(UITheme::BG_RAISED, UITheme::BG_HOVER, hover * 0.7f);
            border = UITheme::Mix(UITheme::BORDER, UITheme::BORDER_STRONG, hover);
        }
        if (hover > 0.001f) {
            const D2D1_RECT_F rowRect(
                static_cast<float>(rcRow.left), static_cast<float>(rcRow.top),
                static_cast<float>(rcRow.right), static_cast<float>(rcRow.bottom));
            r.DrawShadowRoundedRect(rowRect, 10.0f, 10.0f, 13.0f, 4.0f,
                                    UITheme::ShadowColor(hover * 0.18f));
        }
        UITheme::DrawRoundedCard(r, rcRow, 10, bg, border);

        // The selected row is identified by the raised card itself; a marker
        // at the edge of the pane read as a stray artefact next to the orb.

        // Glowing orb toggle on the left (only for non-monitor profiles)
        if (!isMonitor) {
            RECT rcToggle = GetToggleRect(rcRow);
            const float toggleHover =
                static_cast<int>(i) < static_cast<int>(m_rowOrbHover.size())
                    ? m_rowOrbHover[i].value
                    : 0.0f;
            Controls::DrawGlowingOrb(r, rcToggle, m_rowToggle[i].value, toggleHover,
                                     toggleHover * m_orbShimmer);
        }

        // Monogram avatar
        const int avatarSize = AVATAR_SIZE;
        const int avatarLeft = rcRow.left + AVATAR_LEFT;
        const int avatarTop = rcRow.top + ((rcRow.bottom - rcRow.top) - avatarSize) / 2;
        RECT rcAvatar = { avatarLeft, avatarTop, avatarLeft + avatarSize,
                          avatarTop + avatarSize };
        if (isMonitor) {
            UITheme::DrawRoundedCard(r, rcAvatar, 8, UITheme::INVERSE,
                                     UITheme::INVERSE);
            // Draw the monitor icon glyph instead of the "M" text.
            Icons::Draw(r, Icons::Monitor, rcAvatar, 14, UITheme::INVERSE_TEXT);
        } else {
            // The monogram plate is graded and outlined so it reads as a
            // small recessed tile rather than a flat swatch.
            const D2D1_RECT_F avatarRect(static_cast<float>(rcAvatar.left),
                                         static_cast<float>(rcAvatar.top),
                                         static_cast<float>(rcAvatar.right),
                                         static_cast<float>(rcAvatar.bottom));
            r.FillRoundedRectGradient(avatarRect, 8.0f, 8.0f,
                                      Color(UITheme::Lighten(UITheme::BG_RAISED, 3)),
                                      Color(UITheme::Darken(UITheme::BG_RAISED, 3)));
            r.DrawRoundedRectOutline(avatarRect, 8.0f, 8.0f, UITheme::BORDER_STRONG,
                                     1.0f);
            // Try to show the game's own executable icon; fall back to monogram.
            auto iconBmp = GetOrCreateIconBitmap(r, profile);
            if (iconBmp) {
                // Inset slightly so the icon doesn't hug the corners.
                const float pad = 3.0f;
                D2D1_RECT_F destF = D2D1::RectF(
                    static_cast<float>(rcAvatar.left) + pad,
                    static_cast<float>(rcAvatar.top) + pad,
                    static_cast<float>(rcAvatar.right) - pad,
                    static_cast<float>(rcAvatar.bottom) - pad);
                r.DrawBitmap(iconBmp.Get(), destF);
            } else {
                TextStyle monoStyle = Typography::CardHeading();
                monoStyle.sizeDip = 12.0f;
                r.DrawText(MonogramFor(profile), monoStyle,
                           static_cast<float>(rcAvatar.left),
                           static_cast<float>(rcAvatar.top),
                           static_cast<float>(rcAvatar.right - rcAvatar.left),
                           static_cast<float>(rcAvatar.bottom - rcAvatar.top),
                           UITheme::TEXT_PRIMARY,
                           DWRITE_TEXT_ALIGNMENT_CENTER,
                           DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
        }

        // Text block - two lines
        const int textLeft = rcAvatar.right + TEXT_GAP;
        const int textRight = rcRow.right - TEXT_RIGHT_PAD;
        const std::wstring executableText = SubtitleFor(profile);
        const int textWidth = textRight - textLeft;
        const size_t maxCharacters = static_cast<size_t>(
            std::max(1, static_cast<int>(static_cast<float>(textWidth) /
                                         MONO_CHARACTER_WIDTH)));
        const std::wstring wrappedExecutable =
            WrapExecutableText(executableText, maxCharacters);
        const int executableLines = static_cast<int>(WrappedLineCount(wrappedExecutable));
        const int textBlockHeight = NAME_LINE_HEIGHT + TEXT_BLOCK_GAP +
                                    executableLines * EXECUTABLE_LINE_HEIGHT;
        const int textTop = rcRow.top + ((rcRow.bottom - rcRow.top) - textBlockHeight) / 2;

        // Game name (first line, scaled to fit one line)
        {
            TextStyle nameStyle = Typography::Body();
            nameStyle.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            nameStyle.trim = true;
            nameStyle.sizeDip = 12.0f;
            r.DrawText(profile.name.c_str(), nameStyle,
                       static_cast<float>(textLeft), static_cast<float>(textTop),
                       static_cast<float>(textWidth), static_cast<float>(NAME_LINE_HEIGHT),
                       profile.enabled ? UITheme::TEXT_PRIMARY : UITheme::TEXT_TERTIARY);
        }

        // Keep every executable character visible, including long unbroken names.
        {
            TextStyle execStyle = Typography::SmallMono();
            execStyle.sizeDip = 10.0f;
            execStyle.trim = false;
            execStyle.wordWrap = true;
            r.DrawText(wrappedExecutable, execStyle,
                       static_cast<float>(textLeft),
                       static_cast<float>(textTop + NAME_LINE_HEIGHT + TEXT_BLOCK_GAP),
                       static_cast<float>(textWidth),
                       static_cast<float>(executableLines * EXECUTABLE_LINE_HEIGHT),
                       UITheme::TEXT_TERTIARY,
                       DWRITE_TEXT_ALIGNMENT_LEADING,
                       DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                       false);
        }

        // Control on the right (only for monitor)
        if (isMonitor) {
            RECT rcBadge = { rcRow.right - 64,
                             rcRow.top + ((rcRow.bottom - rcRow.top) - 18) / 2,
                             rcRow.right - 12,
                             rcRow.top + ((rcRow.bottom - rcRow.top) - 18) / 2 + 18 };
            UITheme::DrawPill(r, rcBadge, UITheme::BG_INPUT, UITheme::BORDER);
            r.DrawText(L"GLOBAL", Typography::SmallMono(),
                       static_cast<float>(rcBadge.left),
                       static_cast<float>(rcBadge.top),
                       static_cast<float>(rcBadge.right - rcBadge.left),
                       static_cast<float>(rcBadge.bottom - rcBadge.top),
                       UITheme::TEXT_TERTIARY,
                       DWRITE_TEXT_ALIGNMENT_CENTER,
                       DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    r.GetTarget()->PopAxisAlignedClip();

    if (profiles.empty()) {
        int cx = (rcBounds.left + rcBounds.right) / 2;
        int cy = rowsTop + 60;
        RECT rcCircle = { cx - 22, cy - 22, cx + 22, cy + 22 };
        UITheme::DrawRoundedCard(r, rcCircle, 44, UITheme::BG_RAISED,
                                 UITheme::BORDER);
        Icons::Draw(r, m_searchQuery.empty() ? Icons::Profiles : Icons::Search,
                    rcCircle, 18, UITheme::TEXT_SECONDARY);

        {
            TextStyle titleStyle = Typography::CardHeading();
            titleStyle.sizeDip = 18.0f;
            titleStyle.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            r.DrawText(m_searchQuery.empty() ? L"No profiles yet" : L"No matches",
                       titleStyle,
                       static_cast<float>(rcBounds.left + PANE_PAD),
                       static_cast<float>(cy + 34),
                       static_cast<float>(rcBounds.right - rcBounds.left - 2 * PANE_PAD),
                       20.0f,
                       UITheme::TEXT_SECONDARY,
                       DWRITE_TEXT_ALIGNMENT_CENTER);
        }

        {
            r.DrawText(m_searchQuery.empty() ? L"Create one below to get started"
                                             : L"Try a different name",
                       Typography::Muted(),
                       static_cast<float>(rcBounds.left + PANE_PAD),
                       static_cast<float>(cy + 56),
                       static_cast<float>(rcBounds.right - rcBounds.left - 2 * PANE_PAD),
                       18.0f,
                       UITheme::TEXT_TERTIARY,
                       DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }

    // ---- Scrollbar ----
    if (m_scrollMax > 0) {
        RECT rcTrack = { rcBounds.right - 8, rowsTop + 4, rcBounds.right - 4,
                         rowsBottom - 4 };
        UITheme::DrawScrollbar(r, rcTrack, ScrollOffset(), m_scrollMax,
                               rowsBottom - rowsTop);
    }

    // ---- Pinned add button ----
    const float addHover = std::clamp(m_addHover.value, 0.0f, 1.0f);
    const COLORREF addBg =
        UITheme::Mix(UITheme::BG_RAISED, UITheme::BG_HOVER, addHover);
    const COLORREF addBorder =
        UITheme::Mix(UITheme::BORDER, UITheme::BORDER_STRONG, addHover);
    UITheme::DrawCard(r, rcAdd, 10, addBg, addBorder);

    RECT rcPlus = { rcAdd.left + 14, rcAdd.top + 13, rcAdd.left + 34,
                    rcAdd.top + 33 };
    UITheme::DrawRoundedCard(r, rcPlus, 20, UITheme::INVERSE, UITheme::INVERSE);
    Icons::Draw(r, Icons::Add, rcPlus, 10, UITheme::INVERSE_TEXT);

    r.DrawText(L"New profile", Typography::Button(),
               static_cast<float>(rcPlus.right + 10), static_cast<float>(rcAdd.top),
               static_cast<float>(rcAdd.right - 70 - (rcPlus.right + 10)),
               static_cast<float>(rcAdd.bottom - rcAdd.top),
               UITheme::TEXT_PRIMARY,
               DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    r.DrawText(L"CTRL N", Typography::SmallMono(),
               static_cast<float>(rcAdd.right - 64), static_cast<float>(rcAdd.top),
               50.0f, static_cast<float>(rcAdd.bottom - rcAdd.top),
               UITheme::TEXT_TERTIARY,
               DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

bool ProfileListView::OnMouseMove(POINT pt, const RECT& rcBounds,
                                  const std::vector<ColorProfile>& profiles) {
    const int prevHover = m_hoverIndex;
    const int prevToggle = m_hoverToggleIndex;
    const bool prevAdd = m_hoverAddBtn;
    const bool rowsChanged = SyncRowAnimations(profiles);

    m_hoverIndex = -1;
    m_hoverToggleIndex = -1;
    m_hoverAddBtn = false;

    RECT rcAdd = GetAddBtnRect(rcBounds);
    const bool insideRows = pt.y >= rcBounds.top + HEADER_HEIGHT &&
                            pt.y < rcAdd.top - 12;

    if (insideRows) {
        const std::vector<RECT> rowRects = BuildRowRects(rcBounds, profiles);
        for (size_t i = 0; i < profiles.size(); ++i) {
            RECT rcRow = rowRects[i];
            if (PtInRect(&rcRow, pt)) {
                m_hoverIndex = static_cast<int>(i);
                const bool isMonitor = profiles[i].isDefault ||
                                       profiles[i].id == L"monitor";
                RECT rcToggle = GetToggleRect(rcRow);
                // Check if hovering over the orb (on the left)
                if (!isMonitor && PtInRect(&rcToggle, pt)) {
                    m_hoverToggleIndex = static_cast<int>(i);
                }
                break;
            }
        }
    }

    if (PtInRect(&rcAdd, pt)) m_hoverAddBtn = true;

    for (size_t i = 0; i < m_rowHover.size(); ++i) {
        m_rowHover[i].Towards(static_cast<int>(i) == m_hoverIndex);
    }
    for (size_t i = 0; i < m_rowOrbHover.size(); ++i) {
        m_rowOrbHover[i].Towards(static_cast<int>(i) == m_hoverToggleIndex);
    }
    m_addHover.Towards(m_hoverAddBtn);

    return rowsChanged || m_hoverIndex != prevHover ||
           m_hoverToggleIndex != prevToggle || m_hoverAddBtn != prevAdd;
}

bool ProfileListView::OnLButtonDown(POINT pt, const RECT& rcBounds,
                                    const std::vector<ColorProfile>& profiles) {
    RECT rcSearch = GetSearchRect(rcBounds);
    if (PtInRect(&rcSearch, pt)) {
        RECT rcClear = GetSearchClearRect(rcBounds);
        if (!m_searchQuery.empty() && PtInRect(&rcClear, pt)) {
            m_searchQuery.clear();
            ResetHover();
            FocusSearch();
            return true;
        }
        FocusSearch();
        return true;
    }

    const bool wasFocused = m_searchFocused;
    BlurSearch();

    RECT rcAdd = GetAddBtnRect(rcBounds);
    const bool insideRows = pt.y >= rcBounds.top + HEADER_HEIGHT &&
                            pt.y < rcAdd.top - 12;

    if (insideRows) {
        const std::vector<RECT> rowRects = BuildRowRects(rcBounds, profiles);
        for (size_t i = 0; i < profiles.size(); ++i) {
            RECT rcRow = rowRects[i];
            if (!PtInRect(&rcRow, pt)) continue;

            const bool isMonitor = profiles[i].isDefault ||
                                   profiles[i].id == L"monitor";
            RECT rcToggle = GetToggleRect(rcRow);
            if (!isMonitor && PtInRect(&rcToggle, pt)) {
                m_selectedId = profiles[i].id;
                if (m_onToggle) m_onToggle(profiles[i].id, !profiles[i].enabled);
                return true;
            }

            m_selectedId = profiles[i].id;
            if (m_onSelect) m_onSelect(profiles[i].id);
            return true;
        }
    }

    if (PtInRect(&rcAdd, pt)) {
        if (m_onAdd) m_onAdd();
        return true;
    }

    return wasFocused;
}

bool ProfileListView::OnChar(wchar_t ch) {
    if (!m_searchFocused) return false;
    if (ch == VK_BACK) {
        if (!m_searchQuery.empty()) m_searchQuery.pop_back();
    } else if (ch >= 32 && ch != 127 && m_searchQuery.size() < 64) {
        m_searchQuery.push_back(ch);
    } else {
        return false;
    }
    m_scrollY = 0;
    m_scrollAnim.To(0.0f);
    ResetHover();
    return true;
}

std::wstring ProfileListView::GetProfileIdAt(POINT pt, const RECT& rcBounds,
                                             const std::vector<ColorProfile>& profiles) const {
    RECT rcAdd = GetAddBtnRect(rcBounds);
    if (pt.y < rcBounds.top + HEADER_HEIGHT || pt.y >= rcAdd.top - 12) return {};
    const std::vector<RECT> rowRects = BuildRowRects(rcBounds, profiles);
    for (size_t i = 0; i < profiles.size(); ++i) {
        RECT rcRow = rowRects[i];
        if (PtInRect(&rcRow, pt)) return profiles[i].id;
    }
    return {};
}

bool ProfileListView::OnMouseWheel(int delta) {
    const int prev = m_scrollY;
    m_scrollY = std::clamp(m_scrollY - (delta / 120) * 56, 0, m_scrollMax);
    if (m_scrollY != prev) {
        m_scrollAnim.To(static_cast<float>(m_scrollY));
        ResetHover();
        return true;
    }
    return false;
}

bool ProfileListView::TickAnimations(float dt) {
    bool active = false;
    active |= m_searchFocus.Advance(dt);
    active |= m_addHover.Advance(dt);
    active |= m_scrollAnim.Advance(dt);
    for (Anim& a : m_rowHover) active |= a.Advance(dt);
    for (Anim& a : m_rowToggle) active |= a.Advance(dt);
    for (Anim& a : m_rowOrbHover) active |= a.Advance(dt);

    // The shimmer only breathes while an orb is under the pointer, and it
    // honours the "reduce motion" preference.
    m_orbShimmerTime += dt;
    m_orbShimmer = Anim::enabled ? BreathingPhase(m_orbShimmerTime, 2.6f) : 0.0f;
    if (m_hoverToggleIndex >= 0) active = true;

    return active;
}

bool ProfileListView::HasActiveAnimations() const {
    if (m_hoverToggleIndex >= 0) return true;
    if (!m_searchFocus.Settled() || !m_addHover.Settled() || !m_scrollAnim.Settled())
        return true;
    for (const Anim& a : m_rowHover)
        if (!a.Settled()) return true;
    for (const Anim& a : m_rowToggle)
        if (!a.Settled()) return true;
    for (const Anim& a : m_rowOrbHover)
        if (!a.Settled()) return true;
    return false;
}

void ProfileListView::ResetHover() {
    m_hoverIndex = -1;
    m_hoverToggleIndex = -1;
    m_hoverAddBtn = false;
    for (Anim& a : m_rowHover) a.To(0.0f);
    for (Anim& a : m_rowOrbHover) a.To(0.0f);
    m_addHover.To(0.0f);
}

} // namespace prismshift

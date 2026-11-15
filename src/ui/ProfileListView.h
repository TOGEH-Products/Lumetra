#pragma once

#include "profiles/ColorProfile.h"
#include "ui/Anim.h"
#include "ui/Renderer.h"
#include <windows.h>
#include <d2d1.h>
#include <wrl/client.h>
#include <map>
#include <vector>
#include <functional>
#include <string>

namespace prismshift {

// Master list for the Profiles section. Fixed-width column: header, search,
// scrollable rows, pinned "New profile" action.
class ProfileListView {
public:
    using SelectCallback = std::function<void(const std::wstring& profileId)>;
    using ToggleCallback = std::function<void(const std::wstring& profileId, bool enabled)>;
    using AddAppCallback = std::function<void()>;

    ProfileListView();
    ~ProfileListView();

    void SetCallbacks(SelectCallback onSelect, ToggleCallback onToggle,
                      AddAppCallback onAdd) {
        m_onSelect = std::move(onSelect);
        m_onToggle = std::move(onToggle);
        m_onAdd = std::move(onAdd);
    }

    void SetSelectedId(const std::wstring& id) { m_selectedId = id; }
    [[nodiscard]] const std::wstring& GetSelectedId() const { return m_selectedId; }

    void Paint(Renderer& r, const RECT& rcBounds,
               const std::vector<ColorProfile>& profiles);

    bool OnMouseMove(POINT pt, const RECT& rcBounds,
                     const std::vector<ColorProfile>& profiles);
    bool OnLButtonDown(POINT pt, const RECT& rcBounds,
                       const std::vector<ColorProfile>& profiles);
    bool OnMouseWheel(int delta);
    bool OnChar(wchar_t ch);
    [[nodiscard]] const std::wstring& GetSearchQuery() const { return m_searchQuery; }
    [[nodiscard]] bool IsSearchFocused() const { return m_searchFocused; }
    void BlurSearch() { m_searchFocused = false; m_searchFocus.To(0.0f); }
    void FocusSearch() { m_searchFocused = true; m_searchFocus.To(1.0f); }
    void ClearSearch() {
        m_searchQuery.clear();
        ResetHover();
    }
    [[nodiscard]] std::wstring GetProfileIdAt(POINT pt, const RECT& rcBounds,
                                              const std::vector<ColorProfile>& profiles) const;

    // Advances hover/toggle/scroll channels. Returns true while anything moves.
    bool TickAnimations(float dt);
    [[nodiscard]] bool HasActiveAnimations() const;

    void ResetHover();

    // Call when the profile list changes so cached bitmaps are rebuilt on next paint.
    void InvalidateIconCache() { m_iconCache.clear(); }

private:
    RECT GetSearchRect(const RECT& rcBounds) const;
    RECT GetSearchClearRect(const RECT& rcBounds) const;
    std::vector<RECT> BuildRowRects(const RECT& rcBounds,
                                    const std::vector<ColorProfile>& profiles) const;
    int GetRowHeight(const ColorProfile& profile, const RECT& rcBounds) const;
    int GetRowsContentHeight(const RECT& rcBounds,
                             const std::vector<ColorProfile>& profiles) const;
    RECT GetToggleRect(const RECT& rcRow) const;
    RECT GetAddBtnRect(const RECT& rcBounds) const;
    [[nodiscard]] int ScrollOffset() const;
    bool SyncRowAnimations(const std::vector<ColorProfile>& profiles);

    // Returns cached D2D bitmap for a profile's executable icon, or nullptr.
    // Bitmaps are lazily created on first access and keyed by executable path.
    Microsoft::WRL::ComPtr<ID2D1Bitmap>
        GetOrCreateIconBitmap(Renderer& r, const ColorProfile& profile);

    SelectCallback m_onSelect;
    ToggleCallback m_onToggle;
    AddAppCallback m_onAdd;

    int m_hoverIndex = -1;
    int m_hoverToggleIndex = -1;
    bool m_hoverAddBtn = false;
    int m_scrollY = 0;
    int m_scrollMax = 0;
    bool m_searchFocused = false;
    std::wstring m_searchQuery;
    std::wstring m_selectedId;

    Anim m_searchFocus;
    Anim m_addHover;
    Anim m_scrollAnim;
    std::vector<Anim> m_rowHover;
    std::vector<Anim> m_rowToggle;
    std::vector<Anim> m_rowOrbHover;
    std::vector<std::wstring> m_rowToggleIds;

    // Elapsed time for the orb shimmer, and the current phase of that
    // shimmer. It only runs while the pointer rests on an orb, so an idle
    // window still costs nothing.
    float m_orbShimmerTime = 0.0f;
    float m_orbShimmer = 0.0f;

    // Keyed by executablePath; nullptr sentinel = "no icon available".
    struct IconEntry {
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        bool attempted = false; // true once we've tried (avoids retrying missing icons)
    };
    std::map<std::wstring, IconEntry> m_iconCache;
};

} // namespace prismshift

#pragma once

#include "profiles/ColorProfile.h"
#include "ui/Anim.h"
#include "ui/Renderer.h"
#include <windows.h>
#include <functional>
#include <string>

namespace prismshift {

class AddAppDialog {
public:
    using AddCallback = std::function<void(const ColorProfile& newProfile)>;
    using CancelCallback = std::function<void()>;

    AddAppDialog();
    ~AddAppDialog() = default;

    void SetCallbacks(AddCallback onAdd, CancelCallback onCancel) {
        m_onAdd = std::move(onAdd);
        m_onCancel = std::move(onCancel);
    }

    void Reset();

    void Paint(Renderer& r, const RECT& rcBounds);

    bool OnMouseMove(POINT pt, const RECT& rcBounds);
    bool OnLButtonDown(HWND hwndParent, POINT pt, const RECT& rcBounds);
    bool OnChar(wchar_t ch);
    bool OnKeyDown(WPARAM wParam);

    bool TickAnimations(float dt);
    [[nodiscard]] bool HasActiveAnimations() const;

private:
    RECT GetCancelBtnRect(const RECT& rcBounds) const;
    RECT GetBrowseBtnRect(const RECT& rcBounds) const;
    RECT GetSubmitBtnRect(const RECT& rcBounds) const;
    RECT GetNameInputRect(const RECT& rcBounds) const;
    RECT GetPathInputRect(const RECT& rcBounds) const;

    void OpenFilePicker(HWND hwndParent);
    bool Submit();

    AddCallback m_onAdd;
    CancelCallback m_onCancel;

    std::wstring m_name;
    std::wstring m_path;
    std::wstring m_errorMessage;

    bool m_focusName = true;
    bool m_hoverCancel = false;
    bool m_hoverBrowse = false;
    bool m_hoverSubmit = false;

    Anim m_focusAnim;
    Anim m_cancelHover;
    Anim m_browseHover;
    Anim m_submitHover;
};

} // namespace prismshift

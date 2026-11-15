#include "ui/AddAppDialog.h"
#include "ui/UITheme.h"
#include "ui/Controls.h"
#include "ui/Icons.h"
#include "detection/ProcessHelper.h"
#include <commdlg.h>
#include <shlwapi.h>
#include <algorithm>

namespace prismshift {

namespace {
constexpr int PAD = 28;
constexpr int INPUT_H = 42;
constexpr int BROWSE_W = 104;
} // namespace

AddAppDialog::AddAppDialog() {
    m_focusAnim.speed = 20.0f;
    m_cancelHover.speed = 18.0f;
    m_browseHover.speed = 18.0f;
    m_submitHover.speed = 18.0f;
    Reset();
}

void AddAppDialog::Reset() {
    m_name.clear();
    m_path.clear();
    m_errorMessage.clear();
    m_focusName = true;
    m_hoverCancel = false;
    m_hoverBrowse = false;
    m_hoverSubmit = false;
    m_focusAnim.Set(1.0f);
    m_cancelHover.Set(0.0f);
    m_browseHover.Set(0.0f);
    m_submitHover.Set(0.0f);
}

RECT AddAppDialog::GetCancelBtnRect(const RECT& rcBounds) const {
    return RECT{ rcBounds.left + PAD, rcBounds.bottom - 64,
                 rcBounds.left + PAD + 108, rcBounds.bottom - 24 };
}

RECT AddAppDialog::GetSubmitBtnRect(const RECT& rcBounds) const {
    return RECT{ rcBounds.right - PAD - 152, rcBounds.bottom - 64,
                 rcBounds.right - PAD, rcBounds.bottom - 24 };
}

RECT AddAppDialog::GetNameInputRect(const RECT& rcBounds) const {
    return RECT{ rcBounds.left + PAD, rcBounds.top + 156,
                 rcBounds.right - PAD, rcBounds.top + 156 + INPUT_H };
}

RECT AddAppDialog::GetPathInputRect(const RECT& rcBounds) const {
    return RECT{ rcBounds.left + PAD, rcBounds.top + 238,
                 rcBounds.right - PAD - BROWSE_W - 8, rcBounds.top + 238 + INPUT_H };
}

RECT AddAppDialog::GetBrowseBtnRect(const RECT& rcBounds) const {
    return RECT{ rcBounds.right - PAD - BROWSE_W, rcBounds.top + 238,
                 rcBounds.right - PAD, rcBounds.top + 238 + INPUT_H };
}

void AddAppDialog::OpenFilePicker(HWND hwndParent) {
    wchar_t szFile[MAX_PATH] = { 0 };

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwndParent;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"Executable Files (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) {
        m_path = szFile;
        m_errorMessage.clear();

        // If name is empty, auto-generate from file stem.
        if (m_name.empty()) {
            std::wstring file = ProcessHelper::ExtractFileName(m_path);
            size_t dotPos = file.find_last_of(L'.');
            m_name = (dotPos != std::wstring::npos) ? file.substr(0, dotPos) : file;
        }
    }
}

void AddAppDialog::Paint(Renderer& r, const RECT& rcBounds) {
    UITheme::DrawGlassCard(r, rcBounds, 14, UITheme::BG_SURFACE, 1.0f, 0.5f, 18.0f, 6.0f);

    const TextStyle titleStyle = Typography::PageTitle();
    const TextStyle labelStyle = Typography::SmallLabel();
    const TextStyle bodyStyle = Typography::Secondary();
    const TextStyle monoStyle = Typography::SmallMono();

    // ---- Header ----
    UITheme::DrawEyebrow(r, L"NEW PROFILE",
                         RECT{ rcBounds.left + PAD, rcBounds.top + 24,
                               rcBounds.right - PAD, rcBounds.top + 38 },
                         UITheme::TEXT_TERTIARY, 9, FW_SEMIBOLD, 1.0f);

    r.DrawText(L"Add application", titleStyle,
               static_cast<float>(rcBounds.left + PAD),
               static_cast<float>(rcBounds.top + 42),
               static_cast<float>(rcBounds.right - rcBounds.left - 2 * PAD), 28.0f,
               UITheme::TEXT_PRIMARY);

    r.DrawText(L"Create an automated color profile for any Windows executable.",
               bodyStyle,
               static_cast<float>(rcBounds.left + PAD),
               static_cast<float>(rcBounds.top + 76),
               static_cast<float>(rcBounds.right - rcBounds.left - 2 * PAD), 20.0f,
               UITheme::TEXT_TERTIARY);

    // ---- Fields ----
    r.DrawText(L"Profile name", labelStyle,
               static_cast<float>(rcBounds.left + PAD),
               static_cast<float>(rcBounds.top + 128),
               static_cast<float>(rcBounds.right - rcBounds.left - 2 * PAD), 18.0f,
               UITheme::TEXT_SECONDARY);

    RECT rcNameBox = GetNameInputRect(rcBounds);
    const float nameFocus = m_focusAnim.value;
    UITheme::DrawRoundedCard(r, rcNameBox, 9, UITheme::BG_INPUT,
                             UITheme::Mix(UITheme::BORDER, UITheme::BORDER_STRONG,
                                          nameFocus));
    if (nameFocus > 0.05f) {
        RECT rcRing = { rcNameBox.left - 2, rcNameBox.top - 2,
                        rcNameBox.right + 2, rcNameBox.bottom + 2 };
        UITheme::DrawFocusRing(r, rcRing, 10,
                               UITheme::Mix(UITheme::BG_SURFACE, UITheme::BORDER_STRONG,
                                            nameFocus));
    }

    {
        TextStyle inputStyle = Typography::Body();
        RECT rcNameText = { rcNameBox.left + 14, rcNameBox.top,
                            rcNameBox.right - 14, rcNameBox.bottom };
        if (m_name.empty()) {
            r.DrawText(L"e.g. My Game", inputStyle,
                       static_cast<float>(rcNameText.left),
                       static_cast<float>(rcNameText.top),
                       static_cast<float>(rcNameText.right - rcNameText.left),
                       static_cast<float>(rcNameText.bottom - rcNameText.top),
                       UITheme::TEXT_TERTIARY,
                       DWRITE_TEXT_ALIGNMENT_LEADING,
                       DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        } else {
            r.DrawText(m_name.c_str(), inputStyle,
                       static_cast<float>(rcNameText.left),
                       static_cast<float>(rcNameText.top),
                       static_cast<float>(rcNameText.right - rcNameText.left),
                       static_cast<float>(rcNameText.bottom - rcNameText.top),
                       UITheme::TEXT_PRIMARY,
                       DWRITE_TEXT_ALIGNMENT_LEADING,
                       DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        if (nameFocus > 0.5f) {
            const float textWidth = r.MeasureText(m_name.c_str(), inputStyle);
            int caretX =
                static_cast<int>(static_cast<float>(rcNameText.left) + textWidth +
                                 2.0f);
            caretX = std::min(caretX, static_cast<int>(rcNameText.right) - 2);
            RECT rcCaret = { caretX, rcNameBox.top + 11, caretX + 1,
                             rcNameBox.bottom - 11 };
            UITheme::FillSolidRect(r, rcCaret, UITheme::TEXT_PRIMARY);
        }
    }

    r.DrawText(L"Executable", labelStyle,
               static_cast<float>(rcBounds.left + PAD),
               static_cast<float>(rcBounds.top + 210),
               static_cast<float>(rcBounds.right - rcBounds.left - 2 * PAD), 18.0f,
               UITheme::TEXT_SECONDARY);

    RECT rcPathBox = GetPathInputRect(rcBounds);
    const float pathFocus = 1.0f - nameFocus;
    UITheme::DrawRoundedCard(r, rcPathBox, 9, UITheme::BG_INPUT,
                             UITheme::Mix(UITheme::BORDER, UITheme::BORDER_STRONG,
                                          pathFocus));
    if (pathFocus > 0.05f) {
        RECT rcRing = { rcPathBox.left - 2, rcPathBox.top - 2,
                        rcPathBox.right + 2, rcPathBox.bottom + 2 };
        UITheme::DrawFocusRing(r, rcRing, 10,
                               UITheme::Mix(UITheme::BG_SURFACE, UITheme::BORDER_STRONG,
                                            pathFocus));
    }

    {
        TextStyle inputStyle = Typography::Secondary();
        RECT rcPathText = { rcPathBox.left + 14, rcPathBox.top,
                            rcPathBox.right - 12, rcPathBox.bottom };
        if (m_path.empty()) {
            r.DrawText(L"Select an executable file\u2026", inputStyle,
                       static_cast<float>(rcPathText.left),
                       static_cast<float>(rcPathText.top),
                       static_cast<float>(rcPathText.right - rcPathText.left),
                       static_cast<float>(rcPathText.bottom - rcPathText.top),
                       UITheme::TEXT_TERTIARY,
                       DWRITE_TEXT_ALIGNMENT_LEADING,
                       DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        } else {
            r.DrawText(m_path.c_str(), inputStyle,
                       static_cast<float>(rcPathText.left),
                       static_cast<float>(rcPathText.top),
                       static_cast<float>(rcPathText.right - rcPathText.left),
                       static_cast<float>(rcPathText.bottom - rcPathText.top),
                       UITheme::TEXT_PRIMARY,
                       DWRITE_TEXT_ALIGNMENT_LEADING,
                       DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    RECT rcBrowse = GetBrowseBtnRect(rcBounds);
    TextStyle browseStyle = Typography::Button();
    browseStyle.sizeDip = 11.0f;
    Controls::DrawButtonWithIcon(r, rcBrowse, Icons::Folder, L"Browse",
                                 Controls::ButtonStyle::Secondary,
                                 m_browseHover.value, false, true,
                                 browseStyle);

    // ---- Inline error ----
    if (!m_errorMessage.empty()) {
        RECT rcErrIcon = { rcBounds.left + PAD, rcBounds.top + 292,
                           rcBounds.left + PAD + 14, rcBounds.top + 306 };
        Icons::Draw(r, Icons::Warning, rcErrIcon, 11, UITheme::TEXT_PRIMARY);

        r.DrawText(m_errorMessage.c_str(), Typography::Muted(),
                   static_cast<float>(rcErrIcon.right + 7),
                   static_cast<float>(rcBounds.top + 288),
                   static_cast<float>(rcBounds.right - PAD - (rcErrIcon.right + 7)),
                   20.0f,
                   UITheme::TEXT_PRIMARY,
                   DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // ---- Footer ----
    UITheme::DrawHLine(r, rcBounds.left + PAD, rcBounds.right - PAD,
                       rcBounds.bottom - 84, UITheme::DIVIDER);

    RECT rcCancel = GetCancelBtnRect(rcBounds);
    Controls::DrawButton(r, rcCancel, L"Cancel", Controls::ButtonStyle::Ghost,
                         m_cancelHover.value, false, true, Typography::Button());

    RECT rcSubmit = GetSubmitBtnRect(rcBounds);
    Controls::DrawButtonWithIcon(r, rcSubmit, Icons::Add, L"Add application",
                                 Controls::ButtonStyle::Primary,
                                 m_submitHover.value, false, true,
                                 Typography::Button());
}

bool AddAppDialog::OnMouseMove(POINT pt, const RECT& rcBounds) {
    const bool prevCancel = m_hoverCancel;
    const bool prevBrowse = m_hoverBrowse;
    const bool prevSubmit = m_hoverSubmit;

    RECT rcCancel = GetCancelBtnRect(rcBounds);
    RECT rcBrowse = GetBrowseBtnRect(rcBounds);
    RECT rcSubmit = GetSubmitBtnRect(rcBounds);

    m_hoverCancel = PtInRect(&rcCancel, pt) != FALSE;
    m_hoverBrowse = PtInRect(&rcBrowse, pt) != FALSE;
    m_hoverSubmit = PtInRect(&rcSubmit, pt) != FALSE;

    m_cancelHover.Towards(m_hoverCancel);
    m_browseHover.Towards(m_hoverBrowse);
    m_submitHover.Towards(m_hoverSubmit);

    return m_hoverCancel != prevCancel || m_hoverBrowse != prevBrowse ||
           m_hoverSubmit != prevSubmit;
}

bool AddAppDialog::OnLButtonDown(HWND hwndParent, POINT pt, const RECT& rcBounds) {
    RECT rcCancel = GetCancelBtnRect(rcBounds);
    if (PtInRect(&rcCancel, pt)) {
        if (m_onCancel) m_onCancel();
        return true;
    }

    RECT rcBrowse = GetBrowseBtnRect(rcBounds);
    if (PtInRect(&rcBrowse, pt)) {
        m_focusName = false;
        m_focusAnim.To(0.0f);
        OpenFilePicker(hwndParent);
        return true;
    }

    RECT rcNameBox = GetNameInputRect(rcBounds);
    if (PtInRect(&rcNameBox, pt)) {
        m_focusName = true;
        m_focusAnim.To(1.0f);
        return true;
    }

    RECT rcPathBox = GetPathInputRect(rcBounds);
    if (PtInRect(&rcPathBox, pt)) {
        m_focusName = false;
        m_focusAnim.To(0.0f);
        OpenFilePicker(hwndParent);
        return true;
    }

    RECT rcSubmit = GetSubmitBtnRect(rcBounds);
    if (PtInRect(&rcSubmit, pt)) {
        return Submit();
    }

    return false;
}

bool AddAppDialog::Submit() {
    if (m_path.empty()) {
        m_errorMessage = L"Select an executable (.exe) first.";
        return true;
    }
    if (m_name.empty()) {
        m_errorMessage = L"Enter a profile display name.";
        return true;
    }

    std::wstring exeFileName = ProcessHelper::ToLower(ProcessHelper::ExtractFileName(m_path));
    std::wstring profileId = L"custom_" + std::to_wstring(GetTickCount64());

    ColorProfile newProfile = ColorProfile::CreateGame(
        profileId, m_name, { exeFileName }, 1.18, 1.18, 1.18, // Default gaming boost
        1.00, 1.04, true, m_path);

    if (m_onAdd) m_onAdd(newProfile);
    return true;
}

bool AddAppDialog::OnChar(wchar_t ch) {
    if (!m_focusName) return false;

    if (ch == VK_BACK) {
        if (!m_name.empty()) {
            m_name.pop_back();
            return true;
        }
    } else if (ch >= 32 && ch != 127) {
        if (m_name.size() < 40) {
            m_name.push_back(ch);
            m_errorMessage.clear();
            return true;
        }
    }
    return false;
}

bool AddAppDialog::OnKeyDown(WPARAM wParam) {
    if (wParam == VK_ESCAPE) {
        if (m_onCancel) m_onCancel();
        return true;
    }
    if (wParam == VK_TAB) {
        m_focusName = !m_focusName;
        m_focusAnim.To(m_focusName ? 1.0f : 0.0f);
        return true;
    }
    if (wParam == VK_RETURN) {
        return Submit();
    }
    return false;
}

bool AddAppDialog::TickAnimations(float dt) {
    bool active = false;
    active |= m_focusAnim.Advance(dt);
    active |= m_cancelHover.Advance(dt);
    active |= m_browseHover.Advance(dt);
    active |= m_submitHover.Advance(dt);
    return active;
}

bool AddAppDialog::HasActiveAnimations() const {
    return !m_focusAnim.Settled() || !m_cancelHover.Settled() ||
           !m_browseHover.Settled() || !m_submitHover.Settled();
}

} // namespace prismshift

#include "display/DisplayManager.h"
#include "display/Win32GammaEngine.h"
#include "utilities/Logger.h"

namespace prismshift {

DisplayManager::DisplayManager()
    : m_engine(std::make_unique<Win32GammaEngine>()) {
}

DisplayManager::DisplayManager(std::unique_ptr<IColorEngine> engine)
    : m_engine(std::move(engine)) {
}

DisplayManager::~DisplayManager() {
    Shutdown();
}

bool DisplayManager::Initialize() {
    if (m_initialized) {
        return true;
    }

    if (!m_engine || !m_engine->Initialize()) {
        Logger::Error(L"DisplayManager failed to initialize color engine.");
        return false;
    }

    m_monitorProfile = ColorProfile::CreateMonitorCustomization(m_engine->GetDefaultRamp());
    m_initialized = true;
    Logger::Info(L"DisplayManager initialized successfully with Monitor Customization support.");
    return true;
}

void DisplayManager::Shutdown() {
    if (m_initialized) {
        if (m_engine) {
            m_engine->Shutdown();
        }
        m_activeProfile.reset();
        m_previewPreviousProfile.reset();
        m_previewActive = false;
        m_previewComparing = false;
        m_identityResetActive = false;
        m_initialized = false;
        Logger::Info(L"DisplayManager shut down.");
    }
}

void DisplayManager::SetMonitorProfile(const ColorProfile& profile) {
    m_monitorProfile = profile;
    m_monitorProfile.id = L"monitor";
    m_monitorProfile.name = L"Monitor Customization";
    m_monitorProfile.isDefault = true;
    m_monitorProfile.RecomputeRamp();
}

bool DisplayManager::ApplyProfileInternal(const ColorProfile& profile) {
    if (!m_initialized || !m_engine) {
        return false;
    }

    bool okRamp = false;
    if (profile.isDefault && profile.IsRampNeutral()) {
        // Always reassert: another process may have changed the hardware ramp
        // without updating the engine's cached value.
        okRamp = m_engine->ReassertRamp(m_engine->GetDefaultRamp());
    } else {
        okRamp = m_engine->ReassertRamp(profile.ramp);
    }

    // Force the matrix as well, for the same reason. A neutral profile produces
    // an identity matrix.
    bool okColor = m_engine->ReassertColorEffect(profile.saturation, profile.vibrance, profile.hue);

    if (okRamp || okColor) {
        m_activeProfile = profile;
        m_identityResetActive = false;
        return true;
    }
    return false;
}

bool DisplayManager::ApplyProfile(const ColorProfile& profile) {
    if (!m_initialized || !m_engine) {
        Logger::Error(L"Cannot apply profile: DisplayManager not initialized.");
        return false;
    }

    Logger::Info(L"DisplayManager applying profile: [" + profile.name + L"]");
    if (profile.id == L"monitor" || profile.id == L"default") {
        SetMonitorProfile(profile);
    }

    return ApplyProfileInternal(profile);
}

bool DisplayManager::ApplyMonitorCustomization() {
    Logger::Info(L"DisplayManager applying Monitor Customization: [" + m_monitorProfile.name + L"]");
    return ApplyProfileInternal(m_monitorProfile);
}

bool DisplayManager::PreviewProfile(const ColorProfile& profile) {
    if (!m_initialized || !m_engine) {
        return false;
    }

    if (!m_previewActive) {
        m_previewPreviousProfile = m_activeProfile ? *m_activeProfile : m_monitorProfile;
        m_previewActive = true;
        m_previewComparing = false;
    }

    m_previewTargetProfile = profile;
    return ApplyProfileInternal(profile);
}

bool DisplayManager::TogglePreviewCompare() {
    if (!m_previewActive || !m_initialized) {
        return false;
    }

    m_previewComparing = !m_previewComparing;
    if (m_previewComparing) {
        Logger::Info(L"Preview Compare: Viewing Monitor Customization baseline (State B).");
        return ApplyProfileInternal(m_monitorProfile);
    } else {
        Logger::Info(L"Preview Compare: Viewing Game Configuration (State A).");
        return ApplyProfileInternal(m_previewTargetProfile);
    }
}

bool DisplayManager::CancelPreview() {
    if (!m_previewActive) {
        return true;
    }

    ColorProfile previous = m_previewPreviousProfile.value_or(m_monitorProfile);
    m_previewPreviousProfile.reset();
    m_previewActive = false;
    m_previewComparing = false;
    Logger::Info(L"DisplayManager canceled preview. Restored profile: [" + previous.name + L"]");
    return ApplyProfile(previous);
}

void DisplayManager::CommitPreview() {
    m_previewPreviousProfile.reset();
    m_previewActive = false;
    m_previewComparing = false;
    Logger::Info(L"DisplayManager committed preview configuration.");
}

bool DisplayManager::RestoreDefault() {
    return ApplyMonitorCustomization();
}

bool DisplayManager::ResetDisplayToIdentity() {
    if (!m_initialized || !m_engine) {
        return false;
    }

    Logger::Info(L"DisplayManager resetting display hardware ramp and color matrix to linear identity.");
    bool okRamp = m_engine->ResetToIdentity();
    bool okColor = m_engine->ResetColorEffect();
    m_activeProfile.reset();
    m_identityResetActive = true;
    return okRamp && okColor;
}

void DisplayManager::EnsureCurrentProfileApplied() {
    if (!m_initialized || !m_engine) {
        return;
    }

    GammaRamp expectedRamp;
    if (m_activeProfile) {
        // A neutral default profile maps to the captured hardware baseline, not
        // to the identity ramp stored in ColorProfile::ramp. Using the latter
        // here would overwrite a calibrated baseline with a linear identity.
        expectedRamp = (m_activeProfile->isDefault && m_activeProfile->IsRampNeutral())
            ? m_engine->GetDefaultRamp()
            : m_activeProfile->ramp;
    } else if (m_identityResetActive) {
        expectedRamp = GammaRamp::CreateIdentity();
    } else {
        expectedRamp = m_engine->GetDefaultRamp();
    }

    GammaRamp currentRamp;
    if (m_engine->GetCurrentRamp(currentRamp) && currentRamp == expectedRamp) {
        return;
    }

    Logger::Info(L"Display gamma ramp drift detected; reasserting the active display profile.");
    m_engine->ReassertRamp(expectedRamp);

    // The fullscreen color matrix can also be replaced by another app or the
    // OS. Reassert it on the same repair path so the profile is fully restored.
    if (m_activeProfile) {
        m_engine->ReassertColorEffect(m_activeProfile->saturation,
                                      m_activeProfile->vibrance,
                                      m_activeProfile->hue);
    }
}

void DisplayManager::OnDisplayConfigurationChanged() {
    Logger::Info(L"Display configuration change detected (WM_DISPLAYCHANGE).");
    if (!m_engine) {
        return;
    }

    // Re-apply the full active profile to the newly configured display. Going
    // through ApplyProfileInternal keeps neutral default profiles on the
    // captured hardware baseline.
    if (m_activeProfile) {
        ApplyProfileInternal(*m_activeProfile);
    } else if (m_identityResetActive) {
        m_engine->ResetToIdentity();
    } else {
        m_engine->RestoreDefaultRamp();
        m_engine->ResetColorEffect();
    }
}

std::vector<DisplayDescriptor> DisplayManager::GetAvailableDisplays() const {
    std::vector<DisplayDescriptor> displays;
    EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            auto* result = reinterpret_cast<std::vector<DisplayDescriptor>*>(data);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(monitor, &info)) return TRUE;

            DisplayDescriptor descriptor;
            descriptor.deviceName = info.szDevice;
            descriptor.bounds = info.rcMonitor;
            descriptor.isPrimary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
            result->push_back(std::move(descriptor));
            return TRUE;
        }, reinterpret_cast<LPARAM>(&displays));
    return displays;
}

} // namespace prismshift

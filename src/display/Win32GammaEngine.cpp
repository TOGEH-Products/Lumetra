#include "display/Win32GammaEngine.h"
#include "utilities/Logger.h"

namespace prismshift {

Win32GammaEngine::Win32GammaEngine() = default;

Win32GammaEngine::~Win32GammaEngine() {
    Shutdown();
}

HDC Win32GammaEngine::GetDisplayDC() {
    return GetDC(nullptr);
}

void Win32GammaEngine::ReleaseDisplayDC(HDC hdc) {
    if (hdc) {
        ReleaseDC(nullptr, hdc);
    }
}

bool Win32GammaEngine::Initialize() {
    if (m_initialized) {
        return true;
    }

    HDC hdc = GetDisplayDC();
    if (!hdc) {
        Logger::Error(L"Failed to obtain display device context (DC).");
        return false;
    }

    // Attempt to read the existing hardware gamma ramp
    BOOL success = GetDeviceGammaRamp(hdc, &m_defaultRamp);
    if (!success) {
        Logger::Error(L"GetDeviceGammaRamp failed; refusing to guess the baseline display state.");
        ReleaseDisplayDC(hdc);
        return false;
    } else {
        Logger::Info(L"Captured baseline system hardware gamma ramp successfully.");
    }

    m_currentRamp = m_defaultRamp;
    m_initialized = true;

    ReleaseDisplayDC(hdc);

    // Initialize magnification color engine for saturation, vibrance, and hue transforms
    m_magEngine.Initialize();

    return true;
}

void Win32GammaEngine::Shutdown() {
    if (m_initialized) {
        m_magEngine.Shutdown();
        RestoreDefaultRamp();
        m_initialized = false;
        Logger::Info(L"Win32GammaEngine shut down and restored default baseline display state.");
    }
}

bool Win32GammaEngine::SetRamp(const GammaRamp& ramp) {
    return WriteRamp(ramp, false);
}

bool Win32GammaEngine::ReassertRamp(const GammaRamp& ramp) {
    // Bypass the cached-value short-circuit: another process may have changed
    // the hardware ramp without updating m_currentRamp.
    return WriteRamp(ramp, true);
}

bool Win32GammaEngine::WriteRamp(const GammaRamp& ramp, bool force) {
    if (!m_initialized) {
        Logger::Error(L"Cannot set gamma ramp: Win32GammaEngine is not initialized.");
        return false;
    }

    // Skip redundant hardware update if identical to current state, unless the
    // caller explicitly wants the hardware reasserted.
    if (!force && ramp == m_currentRamp) {
        return true;
    }

    HDC hdc = GetDisplayDC();
    if (!hdc) {
        Logger::Error(L"Failed to obtain display DC for SetDeviceGammaRamp.");
        return false;
    }

    // Win32 API expects LPVOID pointing to 3x256 WORD array
    BOOL success = SetDeviceGammaRamp(hdc, const_cast<GammaRamp*>(&ramp));
    ReleaseDisplayDC(hdc);

    if (!success) {
        DWORD err = GetLastError();
        Logger::Error(L"SetDeviceGammaRamp failed with error: " + std::to_wstring(err));
        return false;
    }

    m_currentRamp = ramp;
    return true;
}

bool Win32GammaEngine::GetCurrentRamp(GammaRamp& outRamp) {
    if (!m_initialized) {
        return false;
    }

    HDC hdc = GetDisplayDC();
    if (!hdc) {
        return false;
    }

    BOOL success = GetDeviceGammaRamp(hdc, &outRamp);
    ReleaseDisplayDC(hdc);
    if (success) {
        // Keep the cache aligned with the real hardware state so a later
        // SetRamp() does not skip a needed repair.
        m_currentRamp = outRamp;
    }
    return success != FALSE;
}

bool Win32GammaEngine::RestoreDefaultRamp() {
    if (!m_initialized) {
        return false;
    }

    Logger::Info(L"Restoring default baseline gamma ramp...");
    return SetRamp(m_defaultRamp);
}

bool Win32GammaEngine::ResetToIdentity() {
    if (!m_initialized) {
        return false;
    }

    Logger::Info(L"Resetting display to neutral linear identity...");
    ResetColorEffect();
    GammaRamp identity = GammaRamp::CreateIdentity();
    bool ok = ReassertRamp(identity);
    return ok;
}

bool Win32GammaEngine::SetColorEffect(double saturation, int vibrance, int hue) {
    if (!m_initialized) {
        return false;
    }
    return m_magEngine.SetColorEffect(saturation, vibrance, hue);
}

bool Win32GammaEngine::ReassertColorEffect(double saturation, int vibrance, int hue) {
    if (!m_initialized) {
        return false;
    }
    return m_magEngine.ReassertColorEffect(saturation, vibrance, hue);
}

bool Win32GammaEngine::ResetColorEffect() {
    if (!m_initialized) {
        return false;
    }
    return m_magEngine.ResetColorEffect();
}

} // namespace prismshift

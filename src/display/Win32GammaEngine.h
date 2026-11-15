#pragma once

#include "display/IColorEngine.h"
#include "display/MagnificationEngine.h"
#include <windows.h>

namespace prismshift {

class Win32GammaEngine : public IColorEngine {
public:
    Win32GammaEngine();
    ~Win32GammaEngine() override;

    bool Initialize() override;
    void Shutdown() override;
    [[nodiscard]] bool IsInitialized() const override { return m_initialized; }

    bool SetRamp(const GammaRamp& ramp) override;
    bool ReassertRamp(const GammaRamp& ramp) override;
    bool GetCurrentRamp(GammaRamp& outRamp) override;
    bool RestoreDefaultRamp() override;
    bool ResetToIdentity() override;
    [[nodiscard]] const GammaRamp& GetDefaultRamp() const override { return m_defaultRamp; }

    bool SetColorEffect(double saturation, int vibrance, int hue) override;
    bool ReassertColorEffect(double saturation, int vibrance, int hue) override;
    bool ResetColorEffect() override;

    [[nodiscard]] ColorEngineCapabilities GetCapabilities() const override {
        bool magAvailable = m_magEngine.IsAvailable();
        return {
            .supportsBrightness = true,
            .supportsContrast = true,
            .supportsGamma = true,
            .supportsSaturation = magAvailable,
            .supportsVibrance = magAvailable,
            .supportsHue = magAvailable,
            .supportsLUT = false,
            .supportsPerMonitor = false,
        };
    }

private:
    HDC GetDisplayDC();
    void ReleaseDisplayDC(HDC hdc);
    bool WriteRamp(const GammaRamp& ramp, bool force);

    bool m_initialized = false;
    GammaRamp m_defaultRamp{};
    GammaRamp m_currentRamp{};
    MagnificationEngine m_magEngine;
};

} // namespace prismshift

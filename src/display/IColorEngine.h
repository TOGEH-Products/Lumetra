#pragma once

#include "display/GammaRamp.h"
#include "utilities/NonCopyable.h"

namespace prismshift {

struct ColorEngineCapabilities {
    bool supportsBrightness = false;
    bool supportsContrast = false;
    bool supportsGamma = false;
    bool supportsSaturation = false;
    bool supportsVibrance = false;
    bool supportsHue = false;
    bool supportsLUT = false;
    bool supportsPerMonitor = false;
};

class IColorEngine : public NonCopyable {
public:
    virtual ~IColorEngine() = default;

    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    [[nodiscard]] virtual bool IsInitialized() const = 0;

    virtual bool SetRamp(const GammaRamp& ramp) = 0;
    // Re-applies a ramp even when it matches the engine's cached value. Used by
    // the drift repair path, where another process may have changed the
    // hardware state without updating our cache.
    virtual bool ReassertRamp(const GammaRamp& ramp) { return SetRamp(ramp); }
    virtual bool GetCurrentRamp(GammaRamp& outRamp) = 0;
    virtual bool RestoreDefaultRamp() = 0;
    virtual bool ResetToIdentity() = 0;
    [[nodiscard]] virtual const GammaRamp& GetDefaultRamp() const = 0;

    virtual bool SetColorEffect(double saturation, int vibrance, int hue) {
        (void)saturation; (void)vibrance; (void)hue;
        return false;
    }
    virtual bool ReassertColorEffect(double saturation, int vibrance, int hue) {
        return SetColorEffect(saturation, vibrance, hue);
    }
    virtual bool ResetColorEffect() { return false; }

    [[nodiscard]] virtual ColorEngineCapabilities GetCapabilities() const { return {}; }
};

} // namespace prismshift

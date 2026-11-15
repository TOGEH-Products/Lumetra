#include "display/GammaRamp.h"
#include <cmath>
#include <algorithm>

namespace prismshift {

GammaRamp GammaRamp::CreateIdentity() {
    GammaRamp ramp;
    for (size_t i = 0; i < 256; ++i) {
        auto val = static_cast<uint16_t>((i * 65535) / 255);
        ramp.red[i] = val;
        ramp.green[i] = val;
        ramp.blue[i] = val;
    }
    return ramp;
}

namespace {

uint16_t CalculateRampEntry(size_t index, double gamma, double brightness, double contrast) {
    // Normalized input in [0, 1]
    double input = static_cast<double>(index) / 255.0;

    // Apply gamma curve (gamma correction: x^(1/gamma))
    double safeGamma = (gamma <= 0.01) ? 0.01 : gamma;
    double curve = std::pow(input, 1.0 / safeGamma);

    // Apply contrast around midpoint (0.5)
    double contrasted = (curve - 0.5) * contrast + 0.5;

    // Brightness is an additive offset around the neutral value of 1.0.
    double brightened = contrasted + (brightness - 1.0);

    // Clamp to [0, 1]
    double clamped = std::clamp(brightened, 0.0, 1.0);

    return static_cast<uint16_t>(std::round(clamped * 65535.0));
}

} // namespace

GammaRamp GammaRamp::CreateFromParameters(
    double redGamma,
    double greenGamma,
    double blueGamma,
    double brightness,
    double contrast
) {
    GammaRamp ramp;
    for (size_t i = 0; i < 256; ++i) {
        ramp.red[i]   = CalculateRampEntry(i, redGamma, brightness, contrast);
        ramp.green[i] = CalculateRampEntry(i, greenGamma, brightness, contrast);
        ramp.blue[i]  = CalculateRampEntry(i, blueGamma, brightness, contrast);
    }
    return ramp;
}

} // namespace prismshift

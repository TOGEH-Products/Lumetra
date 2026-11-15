#pragma once

#include <cstdint>
#include <array>
#include <windows.h>

namespace prismshift {

#pragma pack(push, 1)
struct GammaRamp {
    std::array<uint16_t, 256> red{};
    std::array<uint16_t, 256> green{};
    std::array<uint16_t, 256> blue{};

    bool operator==(const GammaRamp& other) const = default;

    static GammaRamp CreateIdentity();

    // Generates a ramp adjusted by gamma (per channel), contrast, and brightness
    // gamma: 1.0 is neutral, >1.0 brightens midtones (reduces dark shadows), <1.0 darkens
    // contrast: 1.0 is neutral, >1.0 increases contrast around mid-gray
    // brightness: 1.0 is neutral, >1.0 scales highlights, <1.0 dims
    static GammaRamp CreateFromParameters(
        double redGamma,
        double greenGamma,
        double blueGamma,
        double brightness = 1.0,
        double contrast = 1.0
    );
};
#pragma pack(pop)

static_assert(sizeof(GammaRamp) == 1536, "GammaRamp size must match Win32 3x256 WORD gamma ramp");

} // namespace prismshift

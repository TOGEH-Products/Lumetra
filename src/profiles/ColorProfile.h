#pragma once

#include "display/GammaRamp.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace prismshift {

enum class ResolutionScaling {
    MaintainAspectRatio,
    FullScreenStretch,
    NoScaling
};

struct ResolutionProfile {
    int width = 1920;
    int height = 1080;
    ResolutionScaling scaling = ResolutionScaling::MaintainAspectRatio;

    [[nodiscard]] bool IsValid() const {
        return width >= 320 && width <= 32768 &&
               height >= 200 && height <= 32768 &&
               scaling >= ResolutionScaling::MaintainAspectRatio &&
               scaling <= ResolutionScaling::NoScaling;
    }

    [[nodiscard]] bool operator==(const ResolutionProfile&) const = default;
};

struct ColorProfile {
    std::wstring id;
    std::wstring name;
    std::vector<std::wstring> targetProcesses; // Lowercase executable names
    std::wstring executablePath;              // Optional full path for custom applications
    bool enabled = true;
    bool isDefault = false;
    bool isCustom = false;

    // Active color transformation parameters
    double gammaR = 1.0;
    double gammaG = 1.0;
    double gammaB = 1.0;
    double brightness = 1.0; // 1.0 is neutral (0% offset), 0.5 to 1.5
    double contrast = 1.0;   // 1.0 is neutral (0% offset), 0.5 to 1.5
    // Color transforms (saturation / vibrance / hue) go through the DWM
    // fullscreen color matrix; gamma, brightness and contrast use the ramp.
    double saturation = 1.0;
    int vibrance = 0; // -100 to +100
    int hue = 0;      // -180 to +180

    // Screenshot hotkey (empty/invalid = no hotkey for this profile)
    std::wstring screenshotHotkey; // e.g., "F12", "Ctrl+Shift+S"

    // Optional temporary display mode for a game or the monitor fallback profile.
    std::optional<ResolutionProfile> resolutionProfile;

    // Computed hardware ramp
    GammaRamp ramp{};

    [[nodiscard]] bool IsValid() const {
        const auto finiteInRange = [](double value, double minimum, double maximum) {
            return std::isfinite(value) && value >= minimum && value <= maximum;
        };
        return !id.empty() && !name.empty() &&
               finiteInRange(gammaR, 0.5, 2.5) &&
               finiteInRange(gammaG, 0.5, 2.5) &&
               finiteInRange(gammaB, 0.5, 2.5) &&
               finiteInRange(brightness, 0.5, 1.5) &&
               finiteInRange(contrast, 0.5, 1.5) &&
               finiteInRange(saturation, 0.0, 2.0) &&
               vibrance >= -100 && vibrance <= 100 &&
               hue >= -180 && hue <= 180 &&
               (!resolutionProfile || resolutionProfile->IsValid());
    }

    void ClampToSupportedRanges() {
        gammaR = std::clamp(gammaR, 0.5, 2.5);
        gammaG = std::clamp(gammaG, 0.5, 2.5);
        gammaB = std::clamp(gammaB, 0.5, 2.5);
        brightness = std::clamp(brightness, 0.5, 1.5);
        contrast = std::clamp(contrast, 0.5, 1.5);
        saturation = std::clamp(saturation, 0.0, 2.0);
        vibrance = std::clamp(vibrance, -100, 100);
        hue = std::clamp(hue, -180, 180);
    }

    [[nodiscard]] bool IsNeutral() const {
        return std::abs(gammaR - 1.0) < 0.001 &&
               std::abs(gammaG - 1.0) < 0.001 &&
               std::abs(gammaB - 1.0) < 0.001 &&
               std::abs(brightness - 1.0) < 0.001 &&
               std::abs(contrast - 1.0) < 0.001 &&
               std::abs(saturation - 1.0) < 0.001 &&
               vibrance == 0 &&
               hue == 0;
    }

    // True when the ramp-affecting parameters (gamma, brightness, contrast) are
    // all neutral. Unlike IsNeutral(), this ignores DWM color transforms, so the
    // default profile can still map to the captured hardware baseline ramp.
    [[nodiscard]] bool IsRampNeutral() const {
        return std::abs(gammaR - 1.0) < 0.001 &&
               std::abs(gammaG - 1.0) < 0.001 &&
               std::abs(gammaB - 1.0) < 0.001 &&
               std::abs(brightness - 1.0) < 0.001 &&
               std::abs(contrast - 1.0) < 0.001;
    }

    void RecomputeRamp() {
        ClampToSupportedRanges();
        ramp = GammaRamp::CreateFromParameters(gammaR, gammaG, gammaB, brightness, contrast);
    }

    static ColorProfile CreateMonitorCustomization(const GammaRamp& baselineRamp) {
        ColorProfile profile;
        profile.id = L"monitor";
        profile.name = L"Monitor Customization";
        profile.ramp = baselineRamp;
        profile.isDefault = true;
        profile.enabled = true;
        profile.gammaR = 1.0;
        profile.gammaG = 1.0;
        profile.gammaB = 1.0;
        profile.brightness = 1.0;
        profile.contrast = 1.0;
        profile.saturation = 1.0;
        profile.vibrance = 0;
        profile.hue = 0;
        return profile;
    }

    static ColorProfile CreateDefault(const GammaRamp& baselineRamp) {
        return CreateMonitorCustomization(baselineRamp);
    }

    static ColorProfile CreateGame(
        std::wstring id,
        std::wstring name,
        std::vector<std::wstring> targets,
        double rGamma,
        double gGamma,
        double bGamma,
        double brightness = 1.0,
        double contrast = 1.0,
        bool isCustom = false,
        std::wstring exePath = L"",
        double saturation = 1.0,
        int vibrance = 0,
        int hue = 0
    ) {
        ColorProfile profile;
        profile.id = std::move(id);
        profile.name = std::move(name);
        profile.targetProcesses = std::move(targets);
        profile.executablePath = std::move(exePath);
        profile.isCustom = isCustom;
        profile.isDefault = false;
        profile.enabled = true;
        profile.gammaR = rGamma;
        profile.gammaG = gGamma;
        profile.gammaB = bGamma;
        profile.brightness = brightness;
        profile.contrast = contrast;
        profile.saturation = saturation;
        profile.vibrance = vibrance;
        profile.hue = hue;
        profile.RecomputeRamp();
        return profile;
    }

    static ColorProfile CreateGameWithVibrance(
        std::wstring id,
        std::wstring name,
        std::vector<std::wstring> targets,
        double rGamma,
        double gGamma,
        double bGamma,
        double brightness,
        double contrast,
        double saturation,
        int vibrance = 0,
        int hue = 0,
        bool isCustom = false,
        std::wstring exePath = L""
    ) {
        return CreateGame(std::move(id), std::move(name), std::move(targets),
                          rGamma, gGamma, bGamma, brightness, contrast,
                          isCustom, std::move(exePath), saturation, vibrance, hue);
    }
};

} // namespace prismshift

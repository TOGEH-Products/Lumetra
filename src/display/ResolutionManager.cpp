#include "display/ResolutionManager.h"

#include "utilities/Logger.h"
#include <algorithm>
#include <vector>

namespace prismshift {

namespace {

// Placement fields describe where a display sits and how it is oriented. They
// are carried over from the live mode so a resolution change keeps the
// orientation and flags of the display.
constexpr DWORD PLACEMENT_FIELDS = DM_DISPLAYORIENTATION | DM_DISPLAYFLAGS;

// DM_POSITION is a reposition request, not a resolution hint. A resolution
// change never writes it.
constexpr DWORD REPOSITION_FIELD = DM_POSITION;

ResolutionScaling ScalingFromMode(const DEVMODEW& mode) {
    if ((mode.dmFields & DM_DISPLAYFIXEDOUTPUT) == 0) {
        return ResolutionScaling::MaintainAspectRatio;
    }
    switch (mode.dmDisplayFixedOutput) {
        case DMDFO_STRETCH:
            return ResolutionScaling::FullScreenStretch;
        case DMDFO_CENTER:
            return ResolutionScaling::NoScaling;
        case DMDFO_DEFAULT:
        default:
            return ResolutionScaling::MaintainAspectRatio;
    }
}

DWORD FixedOutputForScaling(ResolutionScaling scaling) {
    switch (scaling) {
        case ResolutionScaling::FullScreenStretch:
            return DMDFO_STRETCH;
        case ResolutionScaling::NoScaling:
            return DMDFO_CENTER;
        case ResolutionScaling::MaintainAspectRatio:
        default:
            return DMDFO_DEFAULT;
    }
}

bool SamePoint(POINT a, POINT b) {
    return a.x == b.x && a.y == b.y;
}

bool SamePosition(POINTL a, POINTL b) {
    return a.x == b.x && a.y == b.y;
}

bool SameGeometry(const DEVMODEW& a, const DEVMODEW& b) {
    return a.dmPelsWidth == b.dmPelsWidth &&
           a.dmPelsHeight == b.dmPelsHeight &&
           a.dmBitsPerPel == b.dmBitsPerPel &&
           a.dmDisplayFrequency == b.dmDisplayFrequency &&
           a.dmDisplayOrientation == b.dmDisplayOrientation &&
           a.dmDisplayFlags == b.dmDisplayFlags;
}

bool SameScaling(const DEVMODEW& a, const DEVMODEW& b) {
    return ScalingFromMode(a) == ScalingFromMode(b);
}

bool HasSamePlacement(const DEVMODEW& a, const DEVMODEW& b) {
    if ((a.dmFields & REPOSITION_FIELD) == 0 ||
        (b.dmFields & REPOSITION_FIELD) == 0) {
        return true;
    }
    return SamePosition(a.dmPosition, b.dmPosition);
}

std::wstring DescribeMode(const DEVMODEW& mode) {
    return std::to_wstring(mode.dmPelsWidth) + L"x" + std::to_wstring(mode.dmPelsHeight);
}

// The modes to try for a requested scaling, most specific first. A driver can
// reject a scaling that is invalid for a mode - stretching to the panel's own
// native resolution is one example - so the plain mode is always the last
// resort.
std::vector<DEVMODEW> BuildFallbackModes(const DEVMODEW& mode) {
    std::vector<DEVMODEW> candidates;
    candidates.push_back(mode);

    DEVMODEW defaultScaling = mode;
    defaultScaling.dmDisplayFixedOutput = DMDFO_DEFAULT;
    candidates.push_back(defaultScaling);

    DEVMODEW withoutScaling = mode;
    withoutScaling.dmFields &= ~DM_DISPLAYFIXEDOUTPUT;
    candidates.push_back(withoutScaling);

    return candidates;
}

} // namespace

void ResolutionManager::SetTargetDisplay(std::wstring deviceName) {
    m_targetDeviceName = std::move(deviceName);
}

void ResolutionManager::SetTargetToPrimaryDisplay() {
    const auto primary = GetPrimaryDisplay();
    m_targetDeviceName = primary ? primary->deviceName : std::wstring{};
}

void ResolutionManager::SetTargetToDisplayOfWindow(HWND hwnd) {
    const auto display = GetDisplayForWindow(hwnd);
    m_targetDeviceName = display ? display->deviceName : std::wstring{};
}

std::vector<DisplayDevice> ResolutionManager::GetTargetCandidates() const {
    const auto displays = EnumerateAttachedDisplays();

    std::vector<DisplayDevice> candidates;
    const auto add = [&candidates, &displays](const std::wstring& deviceName) {
        if (deviceName.empty()) {
            return;
        }
        const bool alreadyAdded = std::any_of(
            candidates.begin(), candidates.end(),
            [&deviceName](const DisplayDevice& candidate) {
                return candidate.deviceName == deviceName;
            });
        if (alreadyAdded) {
            return;
        }
        const auto match = std::find_if(
            displays.begin(), displays.end(),
            [&deviceName](const DisplayDevice& candidate) {
                return candidate.deviceName == deviceName;
            });
        if (match != displays.end()) {
            candidates.push_back(*match);
        }
    };

    // The explicitly targeted display first, then the primary display, then
    // any other attached display that could host the mode instead.
    add(m_targetDeviceName);
    for (const auto& device : displays) {
        if (device.isPrimary) {
            add(device.deviceName);
        }
    }
    for (const auto& device : displays) {
        add(device.deviceName);
    }

    return candidates;
}

std::vector<ResolutionProfile> ResolutionManager::GetAvailableResolutions() const {
    std::vector<ResolutionProfile> resolutions;

    for (const auto& device : EnumerateAttachedDisplays()) {
        for (DWORD index = 0;; ++index) {
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            if (!EnumDisplaySettingsExW(device.deviceName.c_str(), index, &mode, 0)) {
                break;
            }
            if (mode.dmPelsWidth < 320 || mode.dmPelsWidth > 32768 ||
                mode.dmPelsHeight < 200 || mode.dmPelsHeight > 32768) {
                continue;
            }

            ResolutionProfile candidate;
            candidate.width = static_cast<int>(mode.dmPelsWidth);
            candidate.height = static_cast<int>(mode.dmPelsHeight);
            const bool known = std::any_of(
                resolutions.begin(), resolutions.end(),
                [&candidate](const ResolutionProfile& item) {
                    return item.width == candidate.width &&
                           item.height == candidate.height;
                });
            if (!known) {
                resolutions.push_back(candidate);
            }
        }
    }

    std::sort(resolutions.begin(), resolutions.end(),
              [](const ResolutionProfile& a, const ResolutionProfile& b) {
                  if (a.width != b.width) {
                      return a.width < b.width;
                  }
                  return a.height < b.height;
              });
    return resolutions;
}

std::optional<ResolutionProfile> ResolutionManager::GetCurrentResolution() const {
    const auto candidates = GetTargetCandidates();
    if (candidates.empty()) {
        return std::nullopt;
    }

    DEVMODEW mode{};
    if (!ReadDisplayMode(candidates.front().deviceName, mode, ENUM_CURRENT_SETTINGS)) {
        return std::nullopt;
    }

    ResolutionProfile current;
    current.width = static_cast<int>(mode.dmPelsWidth);
    current.height = static_cast<int>(mode.dmPelsHeight);
    current.scaling = ScalingFromMode(mode);
    return current;
}

bool ResolutionManager::IsCurrentResolution(const ResolutionProfile& profile) const {
    if (!profile.IsValid()) {
        return false;
    }

    const auto candidates = GetTargetCandidates();
    if (candidates.empty()) {
        return false;
    }

    DEVMODEW mode{};
    if (!ReadDisplayMode(candidates.front().deviceName, mode, ENUM_CURRENT_SETTINGS)) {
        return false;
    }
    return mode.dmPelsWidth == static_cast<DWORD>(profile.width) &&
           mode.dmPelsHeight == static_cast<DWORD>(profile.height);
}

bool ResolutionManager::FindMode(const std::wstring& deviceName,
                                 const ResolutionProfile& profile,
                                 const DEVMODEW& currentMode,
                                 DEVMODEW& mode) {
    if (!profile.IsValid()) {
        return false;
    }

    DEVMODEW best{};
    best.dmSize = sizeof(best);
    bool found = false;
    long bestScore = -1;

    for (DWORD index = 0;; ++index) {
        DEVMODEW enumerated{};
        enumerated.dmSize = sizeof(enumerated);
        const BOOL ok = deviceName.empty()
            ? EnumDisplaySettingsExW(nullptr, index, &enumerated, 0)
            : EnumDisplaySettingsExW(deviceName.c_str(), index, &enumerated, 0);
        if (!ok) {
            break;
        }
        if (enumerated.dmPelsWidth != static_cast<DWORD>(profile.width) ||
            enumerated.dmPelsHeight != static_cast<DWORD>(profile.height)) {
            continue;
        }

        // Prefer the refresh rate the display runs now, then the current
        // colour depth, then the highest refresh rate on offer.
        long score = 0;
        if (currentMode.dmDisplayFrequency > 1 &&
            enumerated.dmDisplayFrequency == currentMode.dmDisplayFrequency) {
            score += 100000;
        }
        if (currentMode.dmBitsPerPel > 0 &&
            enumerated.dmBitsPerPel == currentMode.dmBitsPerPel) {
            score += 10000;
        }
        if (enumerated.dmDisplayFrequency > 1) {
            score += static_cast<long>(enumerated.dmDisplayFrequency);
        }

        if (!found || score > bestScore) {
            best = enumerated;
            bestScore = score;
            found = true;
        }
    }

    if (!found) {
        return false;
    }

    // Carry the placement and capability fields of the live mode over so the
    // new mode keeps the orientation and flags of the display.
    if ((currentMode.dmFields & DM_DISPLAYORIENTATION) != 0) {
        best.dmFields |= DM_DISPLAYORIENTATION;
        best.dmDisplayOrientation = currentMode.dmDisplayOrientation;
    }
    if ((currentMode.dmFields & DM_DISPLAYFLAGS) != 0) {
        best.dmFields |= DM_DISPLAYFLAGS;
        best.dmDisplayFlags = currentMode.dmDisplayFlags;
    }

    // A resolution change is not a move. Leaving DM_POSITION out keeps the
    // placement Windows already has for this display.
    best.dmFields &= ~REPOSITION_FIELD;
    best.dmPosition = POINTL{ 0, 0 };

    // The scaling request is applied on top of the mode Windows offered.
    best.dmFields |= DM_DISPLAYFIXEDOUTPUT;
    best.dmDisplayFixedOutput = FixedOutputForScaling(profile.scaling);

    if (best.dmBitsPerPel == 0 && currentMode.dmBitsPerPel > 0) {
        best.dmBitsPerPel = currentMode.dmBitsPerPel;
    }
    if (best.dmDisplayFrequency <= 1 && currentMode.dmDisplayFrequency > 1) {
        best.dmDisplayFrequency = currentMode.dmDisplayFrequency;
    }

    mode = best;
    return true;
}

bool ResolutionManager::IsSupported(const ResolutionProfile& profile) const {
    if (!profile.IsValid()) {
        return false;
    }

    for (const auto& candidate : GetTargetCandidates()) {
        DEVMODEW current{};
        DEVMODEW mode{};
        if (!ReadDisplayMode(candidate.deviceName, current, ENUM_CURRENT_SETTINGS)) {
            continue;
        }
        if (FindMode(candidate.deviceName, profile, current, mode)) {
            return true;
        }
    }
    return false;
}

bool ResolutionManager::SupportsScaling(int width, int height) const {
    const ResolutionProfile profile{ width, height, ResolutionScaling::FullScreenStretch };
    if (!profile.IsValid()) {
        return false;
    }

    for (const auto& candidate : GetTargetCandidates()) {
        DEVMODEW current{};
        DEVMODEW mode{};
        if (!ReadDisplayMode(candidate.deviceName, current, ENUM_CURRENT_SETTINGS)) {
            continue;
        }
        if (!FindMode(candidate.deviceName, profile, current, mode)) {
            continue;
        }

        // Ask Windows whether the driver accepts a scaling override here
        // instead of guessing from the enumerated modes.
        for (const ResolutionScaling scaling :
             { ResolutionScaling::FullScreenStretch, ResolutionScaling::NoScaling }) {
            DEVMODEW test = mode;
            test.dmDisplayFixedOutput = FixedOutputForScaling(scaling);
            const LONG result = ChangeDisplaySettingsExW(
                candidate.deviceName.c_str(), &test, nullptr, CDS_TEST, nullptr);
            if (result == DISP_CHANGE_SUCCESSFUL) {
                return true;
            }
        }
    }
    return false;
}

void ResolutionManager::RepairLayout(const std::vector<DisplayDevice>& layoutBefore) {
    for (const auto& display : EnumerateAttachedDisplays()) {
        const auto previous = std::find_if(
            layoutBefore.begin(), layoutBefore.end(),
            [&display](const DisplayDevice& candidate) {
                return candidate.deviceName == display.deviceName;
            });
        if (previous == layoutBefore.end() || !previous->hasPosition ||
            !display.hasPosition) {
            continue;
        }
        if (SamePoint(previous->position, display.position)) {
            continue;
        }

        // A driver re-flowed the desktop layout. Windows' saved layout still
        // knows where this display belongs, so put it back.
        DEVMODEW saved{};
        if (!ReadDisplayMode(display.deviceName, saved, ENUM_REGISTRY_SETTINGS) ||
            (saved.dmFields & REPOSITION_FIELD) == 0) {
            continue;
        }

        DEVMODEW mode{};
        if (!ReadDisplayMode(display.deviceName, mode, ENUM_CURRENT_SETTINGS)) {
            continue;
        }
        mode.dmFields |= REPOSITION_FIELD;
        mode.dmPosition = saved.dmPosition;

        const LONG result = ChangeDisplaySettingsExW(
            display.deviceName.c_str(), &mode, nullptr, 0, nullptr);
        if (result != DISP_CHANGE_SUCCESSFUL) {
            Logger::Warn(L"Windows moved " + display.description +
                         L" while switching modes and Lumetra could not move it back.");
        } else {
            Logger::Info(L"Moved " + display.description +
                         L" back to its saved position.");
        }
    }
}

bool ResolutionManager::Apply(const ResolutionProfile& profile) {
    if (!profile.IsValid()) {
        Logger::Warn(L"The requested resolution profile is out of range.");
        return false;
    }

    const auto candidates = GetTargetCandidates();
    if (candidates.empty()) {
        Logger::Error(L"No display device is attached to the desktop.");
        return false;
    }

    // Pick the first candidate display that actually offers the requested mode.
    std::wstring deviceName;
    DEVMODEW current{};
    DEVMODEW target{};
    for (const auto& candidate : candidates) {
        DEVMODEW candidateCurrent{};
        if (!ReadDisplayMode(candidate.deviceName, candidateCurrent,
                             ENUM_CURRENT_SETTINGS)) {
            continue;
        }
        DEVMODEW candidateTarget{};
        if (!FindMode(candidate.deviceName, profile, candidateCurrent,
                      candidateTarget)) {
            continue;
        }
        deviceName = candidate.deviceName;
        current = candidateCurrent;
        target = candidateTarget;
        break;
    }

    if (deviceName.empty()) {
        Logger::Warn(L"Windows offers no " + std::to_wstring(profile.width) +
                     L" x " + std::to_wstring(profile.height) +
                     L" mode on any attached display.");
        return false;
    }

    const bool geometryMatches = current.dmPelsWidth == target.dmPelsWidth &&
                                 current.dmPelsHeight == target.dmPelsHeight;
    if (geometryMatches && SameScaling(current, target)) {
        Logger::Info(L"Display " + deviceName + L" already runs the requested mode.");
        return true;
    }

    // Fallback chain: the requested scaling, the driver's default scaling and
    // finally no scaling flag at all, for drivers that reject fixed output.
    const auto attempts = BuildFallbackModes(target);

    const auto layoutBefore = EnumerateAttachedDisplays();

    for (size_t attemptIndex = 0; attemptIndex < attempts.size(); ++attemptIndex) {
        DEVMODEW attempt = attempts[attemptIndex];
        const LONG testResult = ChangeDisplaySettingsExW(
            deviceName.c_str(), &attempt, nullptr, CDS_TEST, nullptr);
        if (testResult != DISP_CHANGE_SUCCESSFUL) {
            Logger::Warn(L"Windows rejected the " + DescribeMode(attempt) +
                         L" mode on " + deviceName + L" during validation (result " +
                         std::to_wstring(testResult) + L").");
            continue;
        }

        DEVMODEW apply = attempts[attemptIndex];
        const LONG applyResult = ChangeDisplaySettingsExW(
            deviceName.c_str(), &apply, nullptr, 0, nullptr);
        if (applyResult != DISP_CHANGE_SUCCESSFUL) {
            Logger::Warn(L"Windows refused to switch " + deviceName + L" to " +
                         DescribeMode(apply) + L" (result " +
                         std::to_wstring(applyResult) + L").");
            continue;
        }

        DEVMODEW applied{};
        if (!ReadDisplayMode(deviceName, applied, ENUM_CURRENT_SETTINGS) ||
            applied.dmPelsWidth != static_cast<DWORD>(profile.width) ||
            applied.dmPelsHeight != static_cast<DWORD>(profile.height)) {
            Logger::Error(L"Windows did not switch " + deviceName +
                          L" to the requested mode; restoring the previous one.");
            DEVMODEW rollback = current;
            ChangeDisplaySettingsExW(deviceName.c_str(), &rollback, nullptr, 0, nullptr);
            continue;
        }

        // Remember Windows' saved desktop configuration so the mode can be
        // restored when the game ends. A fullscreen game only replaces the
        // live mode, so the registry still holds the user's desktop layout.
        SavedMode saved{};
        saved.live = current;
        if (!ReadDisplayMode(deviceName, saved.registry, ENUM_REGISTRY_SETTINGS)) {
            saved.registry = current;
        }
        m_originalModes[deviceName] = saved;

        RepairLayout(layoutBefore);

        if (!SameScaling(applied, target)) {
            Logger::Warn(L"Windows applied " + DescribeMode(applied) +
                         L" but kept its own scaling mode.");
        } else if (attemptIndex != 0) {
            Logger::Warn(L"The requested scaling mode was rejected; applied the "
                         L"driver's default scaling instead.");
        }

        Logger::Info(L"Applied " + DescribeMode(applied) + L" on " + deviceName +
                     L".");
        return true;
    }

    return false;
}

bool ResolutionManager::ApplySavedMode(const std::wstring& deviceName,
                                        const DEVMODEW& saved) {
    for (const auto& candidate : BuildFallbackModes(saved)) {
        DEVMODEW testMode = candidate;
        const LONG testResult = ChangeDisplaySettingsExW(
            deviceName.c_str(), &testMode, nullptr, CDS_TEST, nullptr);
        if (testResult != DISP_CHANGE_SUCCESSFUL) {
            continue;
        }

        DEVMODEW applyMode = candidate;
        const LONG result = ChangeDisplaySettingsExW(
            deviceName.c_str(), &applyMode, nullptr, 0, nullptr);
        if (result != DISP_CHANGE_SUCCESSFUL) {
            continue;
        }

        DEVMODEW applied{};
        if (!ReadDisplayMode(deviceName, applied, ENUM_CURRENT_SETTINGS)) {
            continue;
        }
        if (applied.dmPelsWidth != saved.dmPelsWidth ||
            applied.dmPelsHeight != saved.dmPelsHeight) {
            continue;
        }
        return true;
    }
    return false;
}

bool ResolutionManager::Restore() {
    if (m_originalModes.empty()) {
        return true;
    }

    bool restoredAll = true;
    for (const auto& entry : m_originalModes) {
        const std::wstring& deviceName = entry.first;
        const SavedMode& saved = entry.second;

        DEVMODEW current{};
        if (!ReadDisplayMode(deviceName, current, ENUM_CURRENT_SETTINGS)) {
            restoredAll = false;
            continue;
        }
        // The display is already back when it runs the saved resolution and
        // the saved placement. The refresh rate is not compared because a
        // driver may report a slightly different value for the same mode.
        if (current.dmPelsWidth == saved.registry.dmPelsWidth &&
            current.dmPelsHeight == saved.registry.dmPelsHeight &&
            HasSamePlacement(current, saved.registry)) {
            continue;
        }

        // Windows' saved configuration first, then the mode the display was
        // running before Lumetra touched it.
        if (!ApplySavedMode(deviceName, saved.registry) &&
            !ApplySavedMode(deviceName, saved.live)) {
            Logger::Error(L"Windows failed to restore the saved desktop mode of " +
                          deviceName + L".");
            restoredAll = false;
            continue;
        }

        Logger::Info(L"Restored the saved desktop mode on " + deviceName + L".");
    }

    if (restoredAll) {
        m_originalModes.clear();
    }
    return restoredAll;
}

} // namespace prismshift

#pragma once

#include "display/DisplayDevices.h"
#include "profiles/ColorProfile.h"
#include "utilities/NonCopyable.h"
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <windows.h>

namespace prismshift {

// Applies and restores the temporary display-mode changes Lumetra makes.
//
// Every change targets one concrete display device ("\\.\DISPLAYx") instead
// of "the default display", so a per-game resolution lands on the monitor the
// game is actually running on and a monitor profile lands on Windows' primary
// display. Changing the mode of the wrong display is what makes the desktop
// re-flow and leaves a screen parked in the top-left corner.
//
// Two rules keep the layout stable:
//
//   * DM_POSITION is a reposition request, not a resolution hint. A resolution
//     change therefore never writes DM_POSITION, which lets Windows keep the
//     display exactly where the user put it.
//
//   * After every change the placement of all attached displays is verified
//     against Windows' saved layout and repaired when a driver moved
//     something.
//
// The mode that is restored is the one Windows keeps in its registry
// (ENUM_REGISTRY_SETTINGS): that is the user's saved desktop configuration,
// and a fullscreen game only ever replaces the live mode.
class ResolutionManager : public NonCopyable {
public:
    // --- Target selection --------------------------------------------------

    // Selects the display the next Apply() works on. An empty name means
    // "the primary display".
    void SetTargetDisplay(std::wstring deviceName);
    void SetTargetToPrimaryDisplay();
    void SetTargetToDisplayOfWindow(HWND hwnd);
    [[nodiscard]] std::wstring GetTargetDisplayName() const { return m_targetDeviceName; }

    // --- Queries -----------------------------------------------------------

    // Every resolution offered by any attached display.
    [[nodiscard]] std::vector<ResolutionProfile> GetAvailableResolutions() const;
    // The mode the target display is running right now.
    [[nodiscard]] std::optional<ResolutionProfile> GetCurrentResolution() const;
    // True when the target display already runs the requested resolution.
    [[nodiscard]] bool IsCurrentResolution(const ResolutionProfile& profile) const;
    [[nodiscard]] bool IsSupported(const ResolutionProfile& profile) const;
    [[nodiscard]] bool SupportsScaling(int width, int height) const;
    [[nodiscard]] bool HasOriginalMode() const { return !m_originalModes.empty(); }

    // --- Changes -----------------------------------------------------------

    bool Apply(const ResolutionProfile& profile);
    bool Restore();

private:
    // The modes of a display before Lumetra changed it: the configuration
    // Windows keeps in its registry and the mode the display was running.
    struct SavedMode {
        DEVMODEW registry{};
        DEVMODEW live{};
    };

    // Displays that may host the requested mode, most preferred first.
    [[nodiscard]] std::vector<DisplayDevice> GetTargetCandidates() const;

    // Finds the mode of `deviceName` that matches `profile`.
    [[nodiscard]] static bool FindMode(const std::wstring& deviceName,
                                       const ResolutionProfile& profile,
                                       const DEVMODEW& currentMode,
                                       DEVMODEW& mode);

    // Applies `saved`, falling back to the driver's default scaling when the
    // saved scaling is not accepted for that mode.
    [[nodiscard]] static bool ApplySavedMode(const std::wstring& deviceName,
                                             const DEVMODEW& saved);

    // Puts every display back where Windows' saved layout says it belongs.
    static void RepairLayout(const std::vector<DisplayDevice>& layoutBefore);

    std::wstring m_targetDeviceName;

    // The saved modes of every display Lumetra changed, keyed by device name.
    std::map<std::wstring, SavedMode> m_originalModes;
};

} // namespace prismshift

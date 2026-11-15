#pragma once

#include <optional>
#include <string>
#include <vector>
#include <windows.h>

namespace prismshift {

// One display device that Windows exposes on the desktop.
//
// "Device" here means a single monitor attached to a display adapter,
// identified by the name that EnumDisplaySettingsEx and ChangeDisplaySettingsEx
// expect - for example "\\.\DISPLAY1". The primary display is just the device
// that carries DISPLAY_DEVICE_PRIMARY_DEVICE.
struct DisplayDevice {
    std::wstring deviceName;   // "\\.\DISPLAY1"
    std::wstring deviceKey;    // Registry key of the display device.
    std::wstring deviceId;     // Interface path of the attached monitor.
    std::wstring description;  // Human readable monitor description.
    bool isPrimary = false;    // Windows' "main display".
    bool isAttached = false;   // Currently part of the desktop.
    bool hasPosition = false;  // The live mode reports a virtual-desktop position.
    POINT position{ 0, 0 };    // Top-left corner in virtual-desktop coordinates.
    UINT width = 0;            // Live mode width in pixels.
    UINT height = 0;           // Live mode height in pixels.
};

// Reads a display device's mode.
//
// `flags` is ENUM_CURRENT_SETTINGS for the mode the display is running right
// now (a fullscreen game may have replaced it) or ENUM_REGISTRY_SETTINGS for
// the mode Windows persists as the user's saved desktop configuration.
// An empty device name means "the primary display".
[[nodiscard]] bool ReadDisplayMode(const std::wstring& deviceName, DEVMODEW& mode,
                                   DWORD flags);

// Every display device Windows currently exposes, attached or not.
[[nodiscard]] std::vector<DisplayDevice> EnumerateDisplayDevices();

// Every display device that is currently part of the desktop.
[[nodiscard]] std::vector<DisplayDevice> EnumerateAttachedDisplays();

// Windows' primary ("main") display.
[[nodiscard]] std::optional<DisplayDevice> GetPrimaryDisplay();

// The display that owns a window. Falls back to the primary display when the
// window cannot be located.
[[nodiscard]] std::optional<DisplayDevice> GetDisplayForWindow(HWND hwnd);

// The display that owns a monitor handle. Falls back to the primary display.
[[nodiscard]] std::optional<DisplayDevice> GetDisplayForMonitor(HMONITOR monitor);

} // namespace prismshift

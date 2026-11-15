#include "display/DisplayDevices.h"

#include <algorithm>

namespace prismshift {

namespace {

bool ReadDeviceMode(const wchar_t* deviceName, DEVMODEW& mode, DWORD flags) {
    mode = {};
    mode.dmSize = sizeof(mode);
    return EnumDisplaySettingsExW(deviceName, flags, &mode, 0) != FALSE;
}

} // namespace

bool ReadDisplayMode(const std::wstring& deviceName, DEVMODEW& mode, DWORD flags) {
    if (deviceName.empty()) {
        return ReadDeviceMode(nullptr, mode, flags);
    }
    return ReadDeviceMode(deviceName.c_str(), mode, flags);
}

std::vector<DisplayDevice> EnumerateDisplayDevices() {
    std::vector<DisplayDevice> devices;

    // The first level of EnumDisplayDevices lists the display devices of the
    // desktop ("\\.\DISPLAY1", "\\.\DISPLAY2", ...). Those names are the ones
    // EnumDisplaySettingsEx and ChangeDisplaySettingsEx expect, and every
    // attached display device corresponds to one monitor on the desktop.
    //
    // The second level lists the monitors behind a display device. Their
    // names ("\\.\DISPLAY1\Monitor0") are *not* accepted by the display-mode
    // APIs, so they are only used to describe the display more precisely.
    for (DWORD index = 0;; ++index) {
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(nullptr, index, &device, 0)) {
            break;
        }

        DisplayDevice entry;
        entry.deviceName = device.DeviceName;
        entry.deviceKey = device.DeviceKey;
        entry.description = device.DeviceString;
        entry.isAttached =
            (device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0;
        entry.isPrimary =
            (device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;

        for (DWORD monitorIndex = 0;; ++monitorIndex) {
            DISPLAY_DEVICEW monitor{};
            monitor.cb = sizeof(monitor);
            if (!EnumDisplayDevicesW(device.DeviceName, monitorIndex, &monitor,
                                     EDD_GET_DEVICE_INTERFACE_NAME)) {
                break;
            }
            if ((monitor.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0) {
                entry.deviceId = monitor.DeviceID;
                if (monitor.DeviceString[0] != L'\0') {
                    entry.description = monitor.DeviceString;
                }
                break;
            }
        }

        if (entry.isAttached) {
            DEVMODEW mode{};
            if (ReadDisplayMode(entry.deviceName, mode, ENUM_CURRENT_SETTINGS)) {
                entry.width = mode.dmPelsWidth;
                entry.height = mode.dmPelsHeight;
                if ((mode.dmFields & DM_POSITION) != 0) {
                    entry.hasPosition = true;
                    entry.position.x = mode.dmPosition.x;
                    entry.position.y = mode.dmPosition.y;
                }
            }
        }

        devices.push_back(std::move(entry));
    }

    // Some drivers only report the primary flag on the adapter. When no
    // device claims it, the first attached device is the primary one.
    const bool anyPrimary = std::any_of(
        devices.begin(), devices.end(),
        [](const DisplayDevice& device) { return device.isPrimary; });
    if (!anyPrimary) {
        for (auto& device : devices) {
            if (device.isAttached) {
                device.isPrimary = true;
                break;
            }
        }
    }

    return devices;
}

std::vector<DisplayDevice> EnumerateAttachedDisplays() {
    std::vector<DisplayDevice> attached;
    for (auto& device : EnumerateDisplayDevices()) {
        if (device.isAttached) {
            attached.push_back(std::move(device));
        }
    }
    return attached;
}

std::optional<DisplayDevice> GetPrimaryDisplay() {
    const auto devices = EnumerateAttachedDisplays();
    for (const auto& device : devices) {
        if (device.isPrimary) {
            return device;
        }
    }
    if (!devices.empty()) {
        return devices.front();
    }
    return std::nullopt;
}

std::optional<DisplayDevice> GetDisplayForMonitor(HMONITOR monitor) {
    if (!monitor) {
        return GetPrimaryDisplay();
    }

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        return GetPrimaryDisplay();
    }

    // DEVMODE.dmPosition and MONITORINFO.rcMonitor share the virtual-desktop
    // coordinate space, so the top-left corner identifies the display. The
    // rectangle itself is not used because a rotated display reports its
    // unrotated panel size in the mode.
    for (const auto& device : EnumerateAttachedDisplays()) {
        if (!device.hasPosition) {
            continue;
        }
        if (device.position.x == info.rcMonitor.left &&
            device.position.y == info.rcMonitor.top) {
            return device;
        }
    }

    return GetPrimaryDisplay();
}

std::optional<DisplayDevice> GetDisplayForWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        return GetPrimaryDisplay();
    }
    return GetDisplayForMonitor(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY));
}

} // namespace prismshift

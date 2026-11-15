#pragma once

#include "utilities/NonCopyable.h"
#include <windows.h>
#include <string>
#include <functional>

namespace prismshift {

// High-performance screenshot capture using GDI+ with minimal game interruption.
// Captures can be scoped to a specific window, the primary monitor, or all monitors.
class ScreenshotCapture : public NonCopyable {
public:
    enum class CaptureMode {
        ActiveWindow,     // Capture the foreground window
        PrimaryMonitor,   // Capture the primary monitor
        AllMonitors       // Capture all monitors as a single image
    };

    struct CaptureResult {
        bool success = false;
        std::wstring filePath;
        std::wstring errorMessage;
    };

    ScreenshotCapture();
    ~ScreenshotCapture();

    // Initialize GDI+ (call once at startup)
    bool Initialize();
    void Shutdown();

    // Capture a screenshot and save it to the specified directory
    // Filename is auto-generated with timestamp
    // Returns the full path to the saved file
    CaptureResult Capture(
        const std::wstring& outputDirectory,
        const std::wstring& subdirectory,
        CaptureMode mode = CaptureMode::ActiveWindow,
        HWND targetWindow = nullptr
    );

    // Helper to generate timestamped filename
    [[nodiscard]] static std::wstring GenerateFilename();

    // Ensure a directory exists, creating it if necessary
    static bool EnsureDirectoryExists(const std::wstring& path);

    // Copies a saved PNG image to the Windows clipboard as a DIB.
    static bool CopyImageToClipboard(const std::wstring& filePath, HWND owner);

private:
    struct CaptureContext {
        HDC hdcScreen = nullptr;
        HDC hdcMemory = nullptr;
        HBITMAP hBitmap = nullptr;
        HBITMAP hOldBitmap = nullptr;
        int width = 0;
        int height = 0;
    };

    bool CaptureWindow(HWND hwnd, CaptureContext& ctx);
    bool CaptureMonitor(HMONITOR hMonitor, CaptureContext& ctx);
    bool CaptureAllMonitors(CaptureContext& ctx);
    bool SaveBitmapToPng(const CaptureContext& ctx, const std::wstring& filePath);
    void CleanupContext(CaptureContext& ctx);

    bool m_initialized = false;
    ULONG_PTR m_gdiplusToken = 0;
};

} // namespace prismshift

#include "screenshots/ScreenshotCapture.h"
#include "utilities/Logger.h"
#include <objbase.h>
#include <gdiplus.h>
#include <chrono>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>

#pragma comment(lib, "gdiplus.lib")

namespace prismshift {

namespace {

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0;
    UINT size = 0;

    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;

    auto* pImageCodecInfo = reinterpret_cast<Gdiplus::ImageCodecInfo*>(malloc(size));
    if (pImageCodecInfo == nullptr) return -1;

    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }

    free(pImageCodecInfo);
    return -1;
}

} // namespace

ScreenshotCapture::ScreenshotCapture() = default;

ScreenshotCapture::~ScreenshotCapture() {
    Shutdown();
}

bool ScreenshotCapture::Initialize() {
    if (m_initialized) return true;

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::Status status = Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr);

    if (status != Gdiplus::Ok) {
        Logger::Error(L"[ScreenshotCapture] Failed to initialize GDI+");
        return false;
    }

    m_initialized = true;
    Logger::Info(L"[ScreenshotCapture] Initialized successfully");
    return true;
}

void ScreenshotCapture::Shutdown() {
    if (m_initialized) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_initialized = false;
        Logger::Info(L"[ScreenshotCapture] Shutdown complete");
    }
}

std::wstring ScreenshotCapture::GenerateFilename() {
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm;
    localtime_s(&tm, &timeT);

    std::wostringstream oss;
    oss << L"Screenshot_"
        << std::setfill(L'0')
        << std::setw(4) << (tm.tm_year + 1900)
        << std::setw(2) << (tm.tm_mon + 1)
        << std::setw(2) << tm.tm_mday
        << L"_"
        << std::setw(2) << tm.tm_hour
        << std::setw(2) << tm.tm_min
        << std::setw(2) << tm.tm_sec
        << L"_"
        << std::setw(3) << ms.count()
        << L".png";

    return oss.str();
}

bool ScreenshotCapture::EnsureDirectoryExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return true;
    }

    // Try to create the directory (including parent directories)
    if (CreateDirectoryW(path.c_str(), nullptr)) {
        return true;
    }

    DWORD error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS) {
        return true;
    }

    if (error == ERROR_PATH_NOT_FOUND) {
        // Try to create parent directory first
        size_t lastSlash = path.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) {
            std::wstring parent = path.substr(0, lastSlash);
            if (EnsureDirectoryExists(parent)) {
                return CreateDirectoryW(path.c_str(), nullptr) != FALSE;
            }
        }
    }

    return false;
}

bool ScreenshotCapture::CopyImageToClipboard(const std::wstring& filePath, HWND owner) {
    if (!owner || !IsWindow(owner)) {
        Logger::Warn(L"[ScreenshotCapture] Cannot copy screenshot: clipboard owner window is unavailable");
        return false;
    }

    Gdiplus::Bitmap image(filePath.c_str());
    if (image.GetLastStatus() != Gdiplus::Ok) {
        Logger::Warn(L"[ScreenshotCapture] Cannot load screenshot for clipboard copy: " + filePath);
        return false;
    }

    const UINT width = image.GetWidth();
    const UINT height = image.GetHeight();
    if (width == 0 || height == 0 ||
        width > static_cast<UINT>(std::numeric_limits<LONG>::max()) ||
        height > static_cast<UINT>(std::numeric_limits<LONG>::max())) {
        Logger::Warn(L"[ScreenshotCapture] Screenshot dimensions are invalid for clipboard copy");
        return false;
    }

    const ULONGLONG rowBytes = (static_cast<ULONGLONG>(width) * 3 + 3) & ~3ULL;
    const ULONGLONG pixelBytes = rowBytes * height;
    if (pixelBytes > std::numeric_limits<DWORD>::max() ||
        pixelBytes > std::numeric_limits<SIZE_T>::max() - sizeof(BITMAPINFOHEADER)) {
        Logger::Warn(L"[ScreenshotCapture] Screenshot is too large for clipboard copy");
        return false;
    }

    HBITMAP bitmap = nullptr;
    if (image.GetHBITMAP(Gdiplus::Color(255, 255, 255), &bitmap) != Gdiplus::Ok ||
        !bitmap) {
        Logger::Warn(L"[ScreenshotCapture] Failed to create bitmap for clipboard copy");
        return false;
    }

    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) {
        DeleteObject(bitmap);
        Logger::Warn(L"[ScreenshotCapture] Failed to create device context for clipboard copy");
        return false;
    }

    const SIZE_T dibSize = sizeof(BITMAPINFOHEADER) + static_cast<SIZE_T>(pixelBytes);
    HGLOBAL dib = GlobalAlloc(GMEM_MOVEABLE, dibSize);
    if (!dib) {
        DeleteDC(dc);
        DeleteObject(bitmap);
        Logger::Warn(L"[ScreenshotCapture] Failed to allocate clipboard image data");
        return false;
    }

    auto* dibInfo = static_cast<BITMAPINFO*>(GlobalLock(dib));
    if (!dibInfo) {
        GlobalFree(dib);
        DeleteDC(dc);
        DeleteObject(bitmap);
        Logger::Warn(L"[ScreenshotCapture] Failed to lock clipboard image data");
        return false;
    }

    dibInfo->bmiHeader = {};
    dibInfo->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dibInfo->bmiHeader.biWidth = static_cast<LONG>(width);
    dibInfo->bmiHeader.biHeight = static_cast<LONG>(height);
    dibInfo->bmiHeader.biPlanes = 1;
    dibInfo->bmiHeader.biBitCount = 24;
    dibInfo->bmiHeader.biCompression = BI_RGB;
    dibInfo->bmiHeader.biSizeImage = static_cast<DWORD>(pixelBytes);

    BYTE* pixelData = reinterpret_cast<BYTE*>(dibInfo) + sizeof(BITMAPINFOHEADER);
    const int copiedRows = GetDIBits(dc, bitmap, 0, height, pixelData, dibInfo,
                                     DIB_RGB_COLORS);
    GlobalUnlock(dib);
    DeleteDC(dc);
    DeleteObject(bitmap);

    if (copiedRows != static_cast<int>(height)) {
        GlobalFree(dib);
        Logger::Warn(L"[ScreenshotCapture] Failed to convert screenshot for clipboard copy");
        return false;
    }

    bool opened = false;
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (OpenClipboard(owner)) {
            opened = true;
            break;
        }
        Sleep(10);
    }
    if (!opened) {
        GlobalFree(dib);
        Logger::Warn(L"[ScreenshotCapture] Windows clipboard is unavailable");
        return false;
    }

    bool copied = false;
    if (EmptyClipboard()) {
        copied = SetClipboardData(CF_DIB, dib) != nullptr;
    }
    CloseClipboard();

    if (!copied) {
        GlobalFree(dib);
        Logger::Warn(L"[ScreenshotCapture] Failed to place screenshot on clipboard");
        return false;
    }

    return true;
}

ScreenshotCapture::CaptureResult ScreenshotCapture::Capture(
    const std::wstring& outputDirectory,
    const std::wstring& subdirectory,
    CaptureMode mode,
    HWND targetWindow
) {
    CaptureResult result;

    if (!m_initialized) {
        result.errorMessage = L"Screenshot capture not initialized";
        Logger::Error(L"[ScreenshotCapture] " + result.errorMessage);
        return result;
    }

    // Build full output path
    std::wstring fullPath = outputDirectory;
    if (!subdirectory.empty()) {
        if (fullPath.back() != L'\\' && fullPath.back() != L'/') {
            fullPath += L"\\";
        }
        fullPath += subdirectory;
    }

    // Ensure directory exists
    if (!EnsureDirectoryExists(fullPath)) {
        result.errorMessage = L"Failed to create output directory: " + fullPath;
        Logger::Error(L"[ScreenshotCapture] " + result.errorMessage);
        return result;
    }

    // Generate filename
    std::wstring filename = GenerateFilename();
    if (fullPath.back() != L'\\' && fullPath.back() != L'/') {
        fullPath += L"\\";
    }
    fullPath += filename;

    // Capture based on mode
    CaptureContext ctx;
    bool captureSuccess = false;

    switch (mode) {
        case CaptureMode::ActiveWindow: {
            HWND hwnd = targetWindow ? targetWindow : GetForegroundWindow();
            if (hwnd) {
                captureSuccess = CaptureWindow(hwnd, ctx);
            } else {
                result.errorMessage = L"No active window to capture";
            }
            break;
        }
        case CaptureMode::PrimaryMonitor: {
            HMONITOR hMonitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
            captureSuccess = CaptureMonitor(hMonitor, ctx);
            break;
        }
        case CaptureMode::AllMonitors: {
            captureSuccess = CaptureAllMonitors(ctx);
            break;
        }
    }

    if (!captureSuccess) {
        CleanupContext(ctx);
        if (result.errorMessage.empty()) {
            result.errorMessage = L"Failed to capture screenshot";
        }
        Logger::Error(L"[ScreenshotCapture] " + result.errorMessage);
        return result;
    }

    // Save to file
    if (SaveBitmapToPng(ctx, fullPath)) {
        result.success = true;
        result.filePath = fullPath;
        Logger::Info(L"[ScreenshotCapture] Saved screenshot");
    } else {
        result.errorMessage = L"Failed to save screenshot to: " + fullPath;
        Logger::Error(L"[ScreenshotCapture] " + result.errorMessage);
    }

    CleanupContext(ctx);
    return result;
}

bool ScreenshotCapture::CaptureWindow(HWND hwnd, CaptureContext& ctx) {
    RECT rcWindow{};
    if (!GetWindowRect(hwnd, &rcWindow)) {
        return false;
    }

    ctx.width = rcWindow.right - rcWindow.left;
    ctx.height = rcWindow.bottom - rcWindow.top;

    if (ctx.width <= 0 || ctx.height <= 0) {
        return false;
    }

    // Copy from the composed desktop so GPU-rendered clients (for example
    // Chromium/Electron windows) are captured as they appear on screen.
    ctx.hdcScreen = GetDC(nullptr);
    if (!ctx.hdcScreen) {
        return false;
    }

    ctx.hdcMemory = CreateCompatibleDC(ctx.hdcScreen);
    if (!ctx.hdcMemory) {
        ReleaseDC(hwnd, ctx.hdcScreen);
        ctx.hdcScreen = nullptr;
        return false;
    }

    ctx.hBitmap = CreateCompatibleBitmap(ctx.hdcScreen, ctx.width, ctx.height);
    if (!ctx.hBitmap) {
        DeleteDC(ctx.hdcMemory);
        ReleaseDC(hwnd, ctx.hdcScreen);
        ctx.hdcMemory = nullptr;
        ctx.hdcScreen = nullptr;
        return false;
    }

    ctx.hOldBitmap = static_cast<HBITMAP>(SelectObject(ctx.hdcMemory, ctx.hBitmap));

    if (!BitBlt(ctx.hdcMemory, 0, 0, ctx.width, ctx.height, ctx.hdcScreen,
                rcWindow.left, rcWindow.top, SRCCOPY | CAPTUREBLT)) {
        return false;
    }

    return true;
}

bool ScreenshotCapture::CaptureMonitor(HMONITOR hMonitor, CaptureContext& ctx) {
    MONITORINFO mi = { sizeof(MONITORINFO) };
    if (!GetMonitorInfo(hMonitor, &mi)) {
        return false;
    }

    ctx.width = mi.rcMonitor.right - mi.rcMonitor.left;
    ctx.height = mi.rcMonitor.bottom - mi.rcMonitor.top;

    ctx.hdcScreen = GetDC(nullptr);
    if (!ctx.hdcScreen) {
        return false;
    }

    ctx.hdcMemory = CreateCompatibleDC(ctx.hdcScreen);
    if (!ctx.hdcMemory) {
        ReleaseDC(nullptr, ctx.hdcScreen);
        ctx.hdcScreen = nullptr;
        return false;
    }

    ctx.hBitmap = CreateCompatibleBitmap(ctx.hdcScreen, ctx.width, ctx.height);
    if (!ctx.hBitmap) {
        DeleteDC(ctx.hdcMemory);
        ReleaseDC(nullptr, ctx.hdcScreen);
        ctx.hdcMemory = nullptr;
        ctx.hdcScreen = nullptr;
        return false;
    }

    ctx.hOldBitmap = static_cast<HBITMAP>(SelectObject(ctx.hdcMemory, ctx.hBitmap));

    if (!BitBlt(ctx.hdcMemory, 0, 0, ctx.width, ctx.height, ctx.hdcScreen,
                mi.rcMonitor.left, mi.rcMonitor.top, SRCCOPY)) {
        return false;
    }

    return true;
}

bool ScreenshotCapture::CaptureAllMonitors(CaptureContext& ctx) {
    ctx.width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    ctx.height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);

    ctx.hdcScreen = GetDC(nullptr);
    if (!ctx.hdcScreen) {
        return false;
    }

    ctx.hdcMemory = CreateCompatibleDC(ctx.hdcScreen);
    if (!ctx.hdcMemory) {
        ReleaseDC(nullptr, ctx.hdcScreen);
        ctx.hdcScreen = nullptr;
        return false;
    }

    ctx.hBitmap = CreateCompatibleBitmap(ctx.hdcScreen, ctx.width, ctx.height);
    if (!ctx.hBitmap) {
        DeleteDC(ctx.hdcMemory);
        ReleaseDC(nullptr, ctx.hdcScreen);
        ctx.hdcMemory = nullptr;
        ctx.hdcScreen = nullptr;
        return false;
    }

    ctx.hOldBitmap = static_cast<HBITMAP>(SelectObject(ctx.hdcMemory, ctx.hBitmap));

    if (!BitBlt(ctx.hdcMemory, 0, 0, ctx.width, ctx.height, ctx.hdcScreen, x, y, SRCCOPY)) {
        return false;
    }

    return true;
}

bool ScreenshotCapture::SaveBitmapToPng(const CaptureContext& ctx, const std::wstring& filePath) {
    Gdiplus::Bitmap bitmap(ctx.hBitmap, nullptr);

    CLSID pngClsid;
    if (GetEncoderClsid(L"image/png", &pngClsid) == -1) {
        Logger::Error(L"[ScreenshotCapture] PNG encoder not found");
        return false;
    }

    Gdiplus::Status status = bitmap.Save(filePath.c_str(), &pngClsid, nullptr);
    return status == Gdiplus::Ok;
}

void ScreenshotCapture::CleanupContext(CaptureContext& ctx) {
    if (ctx.hOldBitmap && ctx.hdcMemory) {
        SelectObject(ctx.hdcMemory, ctx.hOldBitmap);
    }
    if (ctx.hBitmap) {
        DeleteObject(ctx.hBitmap);
    }
    if (ctx.hdcMemory) {
        DeleteDC(ctx.hdcMemory);
    }
    if (ctx.hdcScreen) {
        ReleaseDC(nullptr, ctx.hdcScreen);
    }
}

} // namespace prismshift

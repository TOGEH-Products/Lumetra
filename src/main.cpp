#include "app/Application.h"
#include "display/GammaRamp.h"
#include "utilities/Logger.h"
#include <windows.h>
#include <string_view>

namespace {

constexpr const wchar_t* SINGLE_INSTANCE_MUTEX = L"Local\\Lumetra_SingleInstance_Mutex";
constexpr const wchar_t* MAIN_WINDOW_CLASS = L"Lumetra_MainWindowClass";

bool HasCommandLineFlag(std::wstring_view cmdLine, std::wstring_view flag) {
    return cmdLine.find(flag) != std::wstring_view::npos;
}

bool ResetDisplayToNeutral() {
    HDC hdc = GetDC(nullptr);
    if (!hdc) return false;

    prismshift::GammaRamp identity = prismshift::GammaRamp::CreateIdentity();
    BOOL success = SetDeviceGammaRamp(hdc, &identity);
    ReleaseDC(nullptr, hdc);
    return success != FALSE;
}

} // namespace

int WINAPI wWinMain(
    HINSTANCE /*hInstance*/,
    HINSTANCE /*hPrevInstance*/,
    PWSTR pCmdLine,
    int /*nCmdShow*/
) {
    // Keep the native UI crisp instead of letting Windows bitmap-scale it.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    std::wstring_view cmdLine = (pCmdLine != nullptr) ? pCmdLine : L"";

    bool debugMode = HasCommandLineFlag(cmdLine, L"--debug") ||
                     HasCommandLineFlag(cmdLine, L"/debug") ||
                     HasCommandLineFlag(cmdLine, L"--console") ||
                     HasCommandLineFlag(cmdLine, L"/console");

    bool fileLog = debugMode || HasCommandLineFlag(cmdLine, L"--log") || HasCommandLineFlag(cmdLine, L"/log");
    std::wstring logPath = fileLog ? L"lumetra.log" : L"";

    // Initialize logging (allocates/attaches console if debug flag requested, writes to lumetra.log if requested, otherwise silent with OutputDebugString)
    prismshift::Logger::Initialize(debugMode, true, logPath);

    // Immediate rescue flag: --reset
    if (HasCommandLineFlag(cmdLine, L"--reset") || HasCommandLineFlag(cmdLine, L"/reset")) {
        prismshift::Logger::Info(L"=== Executing Display Rescue Reset (--reset) ===");
        if (ResetDisplayToNeutral()) {
            prismshift::Logger::Info(L"Display hardware gamma ramp successfully reset to neutral linear identity.");
        } else {
            prismshift::Logger::Error(L"Failed to reset display hardware gamma ramp.");
        }
        prismshift::Logger::Shutdown();
        return 0;
    }

    prismshift::Logger::Info(L"=== Lumetra Starting (Stage 2) ===");

    // Single-instance check: if another instance is running, activate its window and exit cleanly
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, SINGLE_INSTANCE_MUTEX);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        prismshift::Logger::Info(L"Another instance of Lumetra is already running. Restoring existing window...");

        HWND hwndExisting = FindWindowW(MAIN_WINDOW_CLASS, nullptr);
        if (hwndExisting) {
            ShowWindow(hwndExisting, SW_RESTORE);
            SetForegroundWindow(hwndExisting);
        }

        if (hMutex) {
            CloseHandle(hMutex);
        }
        prismshift::Logger::Shutdown();
        return 0;
    }

    bool startMinimized = HasCommandLineFlag(cmdLine, L"--minimized") ||
                         HasCommandLineFlag(cmdLine, L"/minimized");

    int exitCode = 0;
    {
        prismshift::Application app;
        if (app.Initialize(debugMode, startMinimized)) {
            exitCode = app.Run();
        } else {
            prismshift::Logger::Error(L"Lumetra application failed to initialize.");
            exitCode = -1;
        }
    }

    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    prismshift::Logger::Info(L"=== Lumetra Terminated ===");
    prismshift::Logger::Shutdown();

    return exitCode;
}

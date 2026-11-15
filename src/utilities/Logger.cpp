#include "utilities/Logger.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace prismshift {

namespace {

std::wstring Utf8ToWide(std::string_view utf8Str) {
    if (utf8Str.empty()) return {};
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), static_cast<int>(utf8Str.size()), nullptr, 0);
    if (sizeNeeded <= 0) return {};
    std::wstring wide(sizeNeeded, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), static_cast<int>(utf8Str.size()), wide.data(), sizeNeeded);
    return wide;
}

const wchar_t* LogLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return L"DEBUG";
        case LogLevel::Info:    return L"INFO ";
        case LogLevel::Warning: return L"WARN ";
        case LogLevel::Error:   return L"ERROR";
        default:                return L"LOG  ";
    }
}

WORD LogLevelToConsoleColor(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return FOREGROUND_INTENSITY; // Dark gray / dim white
        case LogLevel::Info:    return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY; // Bright white
        case LogLevel::Warning: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY; // Yellow
        case LogLevel::Error:   return FOREGROUND_RED | FOREGROUND_INTENSITY; // Bright red
        default:                return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    }
}

} // namespace

void Logger::Initialize(bool enableConsole, bool enableDebugOutput, std::wstring_view logFilePath) {
    std::lock_guard<std::mutex> lock(s_logMutex);
    s_debugOutputEnabled = enableDebugOutput;
    s_logFilePath = logFilePath;

    if (enableConsole) {
        // First attempt to attach to the caller's console (e.g. if launched from cmd.exe or powershell)
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            s_consoleEnabled = true;
            s_allocatedConsole = false;
        } else {
            // Otherwise allocate a dedicated console window
            if (AllocConsole()) {
                s_consoleEnabled = true;
                s_allocatedConsole = true;
                SetConsoleTitleW(L"Lumetra - Debug Console");
            }
        }

        if (s_consoleEnabled) {
            FILE* fp = nullptr;
            freopen_s(&fp, "CONOUT$", "w", stdout);
            freopen_s(&fp, "CONOUT$", "w", stderr);
            freopen_s(&fp, "CONIN$", "r", stdin);
            std::ios::sync_with_stdio(true);
        }
    }
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(s_logMutex);
    if (s_allocatedConsole) {
        FreeConsole();
        s_allocatedConsole = false;
    }
    s_consoleEnabled = false;
    s_logFilePath.clear();
}

bool Logger::IsConsoleEnabled() {
    return s_consoleEnabled;
}

std::wstring Logger::FormatLogMessage(LogLevel level, std::wstring_view message) {
    SYSTEMTIME st;
    GetLocalTime(&st);

    std::wostringstream oss;
    oss << L"["
        << std::setfill(L'0') << std::setw(2) << st.wHour << L":"
        << std::setfill(L'0') << std::setw(2) << st.wMinute << L":"
        << std::setfill(L'0') << std::setw(2) << st.wSecond << L"."
        << std::setfill(L'0') << std::setw(3) << st.wMilliseconds << L"] "
        << L"[" << LogLevelToString(level) << L"] "
        << message << L"\n";

    return oss.str();
}

void Logger::Log(LogLevel level, std::wstring_view message) {
    std::lock_guard<std::mutex> lock(s_logMutex);
    std::wstring formatted = FormatLogMessage(level, message);

    if (s_debugOutputEnabled) {
        OutputDebugStringW(formatted.c_str());
    }

    if (!s_logFilePath.empty()) {
        HANDLE hFile = CreateFileW(
            s_logFilePath.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (hFile != INVALID_HANDLE_VALUE) {
            // Write UTF-8 to file
            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, formatted.c_str(), static_cast<int>(formatted.size()), nullptr, 0, nullptr, nullptr);
            if (utf8Len > 0) {
                std::string utf8Str(utf8Len, '\0');
                WideCharToMultiByte(CP_UTF8, 0, formatted.c_str(), static_cast<int>(formatted.size()), utf8Str.data(), utf8Len, nullptr, nullptr);
                DWORD written = 0;
                WriteFile(hFile, utf8Str.data(), static_cast<DWORD>(utf8Str.size()), &written, nullptr);
            }
            CloseHandle(hFile);
        }
    }

    if (s_consoleEnabled) {
        HANDLE hConsole = GetStdHandle(level == LogLevel::Error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
        if (hConsole != INVALID_HANDLE_VALUE && hConsole != nullptr) {
            CONSOLE_SCREEN_BUFFER_INFO csbi;
            bool hasCsbi = GetConsoleScreenBufferInfo(hConsole, &csbi) != FALSE;
            WORD oldAttr = hasCsbi ? csbi.wAttributes : 0;

            SetConsoleTextAttribute(hConsole, LogLevelToConsoleColor(level));
            DWORD written = 0;
            WriteConsoleW(hConsole, formatted.data(), static_cast<DWORD>(formatted.size()), &written, nullptr);

            if (hasCsbi) {
                SetConsoleTextAttribute(hConsole, oldAttr);
            }
        }
    }
}

void Logger::Log(LogLevel level, std::string_view message) {
    std::wstring wideMsg = Utf8ToWide(message);
    Log(level, wideMsg);
}

} // namespace prismshift

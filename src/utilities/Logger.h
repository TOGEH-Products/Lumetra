#pragma once

#include <string>
#include <string_view>
#include <mutex>
#include <windows.h>

namespace prismshift {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class Logger {
public:
    static void Initialize(bool enableConsole, bool enableDebugOutput = true, std::wstring_view logFilePath = L"");
    static void Shutdown();

    static void Log(LogLevel level, std::wstring_view message);
    static void Log(LogLevel level, std::string_view message);

    static void Debug(std::wstring_view message) { Log(LogLevel::Debug, message); }
    static void Info(std::wstring_view message) { Log(LogLevel::Info, message); }
    static void Warn(std::wstring_view message) { Log(LogLevel::Warning, message); }
    static void Error(std::wstring_view message) { Log(LogLevel::Error, message); }

    static void Debug(std::string_view message) { Log(LogLevel::Debug, message); }
    static void Info(std::string_view message) { Log(LogLevel::Info, message); }
    static void Warn(std::string_view message) { Log(LogLevel::Warning, message); }
    static void Error(std::string_view message) { Log(LogLevel::Error, message); }

    static bool IsConsoleEnabled();

private:
    static std::wstring FormatLogMessage(LogLevel level, std::wstring_view message);

    static inline bool s_consoleEnabled = false;
    static inline bool s_debugOutputEnabled = true;
    static inline bool s_allocatedConsole = false;
    static inline std::wstring s_logFilePath;
    static inline std::mutex s_logMutex;
};

} // namespace prismshift

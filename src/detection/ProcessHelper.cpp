#include "detection/ProcessHelper.h"
#include <vector>
#include <algorithm>
#include <cwctype>

namespace prismshift {

ProcessInfo ProcessHelper::GetProcessInfoFromWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        return {};
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) {
        return {};
    }

    return GetProcessInfoFromPid(pid);
}

ProcessInfo ProcessHelper::GetProcessInfoFromPid(DWORD pid) {
    if (pid == 0) {
        return {};
    }

    ProcessInfo info;
    info.pid = pid;

    // Use PROCESS_QUERY_LIMITED_INFORMATION for minimal privilege requirement and anti-cheat safety
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) {
        return info;
    }

    DWORD bufferSize = MAX_PATH;
    std::vector<wchar_t> buffer(bufferSize);

    while (true) {
        DWORD currentSize = bufferSize;
        if (QueryFullProcessImageNameW(hProcess, 0, buffer.data(), &currentSize)) {
            info.fullPath.assign(buffer.data(), currentSize);
            info.exeName = ExtractFileName(info.fullPath);
            break;
        }

        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && bufferSize < 32768) {
            bufferSize *= 2;
            buffer.resize(bufferSize);
        } else {
            break;
        }
    }

    CloseHandle(hProcess);
    return info;
}

std::wstring ProcessHelper::ExtractFileName(std::wstring_view fullPath) {
    if (fullPath.empty()) {
        return {};
    }

    size_t lastSlash = fullPath.find_last_of(L"\\/");
    if (lastSlash != std::wstring_view::npos) {
        return std::wstring(fullPath.substr(lastSlash + 1));
    }

    return std::wstring(fullPath);
}

std::wstring ProcessHelper::ToLower(std::wstring_view str) {
    std::wstring result(str);
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return result;
}

} // namespace prismshift

#pragma once

#include <string>
#include <string_view>
#include <windows.h>

namespace prismshift {

struct ProcessInfo {
    DWORD pid = 0;
    std::wstring exeName;
    std::wstring fullPath;

    [[nodiscard]] bool IsValid() const {
        return pid != 0 && !exeName.empty();
    }
};

class ProcessHelper {
public:
    static ProcessInfo GetProcessInfoFromWindow(HWND hwnd);
    static ProcessInfo GetProcessInfoFromPid(DWORD pid);
    static std::wstring ExtractFileName(std::wstring_view fullPath);
    static std::wstring ToLower(std::wstring_view str);
};

} // namespace prismshift

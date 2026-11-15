#pragma once

#include <windows.h>
#include <string>

namespace prismshift {

// Loads the application icon from Icon.ico. The icon is searched for next to
// the executable, then one directory up (e.g. when the executable lives in a
// build/ subdirectory), then in the working directory. Falls back to the
// standard application icon if Icon.ico cannot be found. The returned handle
// is shared and must not be destroyed with DestroyIcon.
HICON LoadAppIcon();

// Extracts the first icon from an executable or shortcut at |exePath|.
// Returns nullptr if the file has no icons or cannot be found.
// The returned handle IS owned by the caller and must be destroyed with
// DestroyIcon when no longer needed.
HICON ExtractExeIcon(const std::wstring& exePath, int desiredSizePx = 32);

} // namespace prismshift

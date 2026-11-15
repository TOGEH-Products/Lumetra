#include "AppIcon.h"

#include <shellapi.h>
#include <commctrl.h>
#include <string>
#include <vector>

namespace prismshift {

namespace {

std::wstring ExeDirectory() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p(path);
    size_t slash = p.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        p.erase(slash);
    }
    return p;
}

constexpr int kEmbeddedIconId = 101;

HICON TryLoadIcon(const std::wstring& filePath) {
    if (filePath.empty()) {
        return nullptr;
    }
    return static_cast<HICON>(LoadImageW(nullptr, filePath.c_str(), IMAGE_ICON,
                                         0, 0,
                                         LR_LOADFROMFILE | LR_DEFAULTSIZE | LR_SHARED));
}

} // namespace

HICON LoadAppIcon() {
    static constexpr const wchar_t* kFileName = L"Icon.ico";

    // Prefer the icon embedded in the executable (see Lumetra.rc) so a copied
    // or packaged Lumetra.exe carries its identity without extra files.
    if (HICON embedded = static_cast<HICON>(LoadImageW(
            GetModuleHandleW(nullptr), MAKEINTRESOURCEW(kEmbeddedIconId), IMAGE_ICON,
            0, 0, LR_DEFAULTSIZE | LR_SHARED))) {
        return embedded;
    }

    std::vector<std::wstring> candidates;
    std::wstring exeDir = ExeDirectory();
    candidates.push_back(exeDir + L"\\" + kFileName);

    // If the executable lives in a subdirectory (e.g. build/), also look one
    // level up where Icon.ico typically sits next to the project source.
    if (!exeDir.empty()) {
        size_t slash = exeDir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            candidates.push_back(exeDir.substr(0, slash) + L"\\" + kFileName);
        }
    }

    candidates.push_back(kFileName); // working directory

    for (const auto& candidate : candidates) {
        if (HICON h = TryLoadIcon(candidate)) {
            return h;
        }
    }

    return LoadIconW(nullptr, IDI_APPLICATION);
}

HICON ExtractExeIcon(const std::wstring& exePath, int desiredSizePx) {
    if (exePath.empty()) return nullptr;

    // SHGetFileInfoW with SHGFI_ICON gives us the shell icon at the requested
    // size (small ≤ 16px, large otherwise). For anything in between we use
    // ExtractIconExW and let the caller scale via DrawIconEx.
    const UINT flags = SHGFI_ICON | (desiredSizePx <= 16 ? SHGFI_SMALLICON : SHGFI_LARGEICON);
    SHFILEINFOW sfi{};
    if (SHGetFileInfoW(exePath.c_str(), 0, &sfi, sizeof(sfi), flags) && sfi.hIcon) {
        return sfi.hIcon; // caller owns this handle
    }

    // Fallback: direct extraction from the file
    HICON large = nullptr, small = nullptr;
    if (ExtractIconExW(exePath.c_str(), 0, &large, &small, 1) > 0) {
        HICON chosen = desiredSizePx <= 16 ? small : large;
        HICON discard = desiredSizePx <= 16 ? large : small;
        if (discard) DestroyIcon(discard);
        return chosen;
    }
    if (large) DestroyIcon(large);
    if (small) DestroyIcon(small);
    return nullptr;
}

} // namespace prismshift

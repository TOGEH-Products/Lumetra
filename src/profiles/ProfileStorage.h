#pragma once

#include "profiles/ColorProfile.h"
#include <cstdint>
#include <vector>
#include <string>
#include <optional>

namespace prismshift {

inline constexpr std::uint64_t kMaxProfileStorageFileSize = 4ull * 1024 * 1024;

constexpr bool IsProfileStorageFileSizeAllowed(std::uint64_t size) noexcept {
    return size > 0 && size <= kMaxProfileStorageFileSize;
}

struct StorageData {
    ColorProfile monitorProfile;
    std::vector<ColorProfile> gameProfiles;
};

class ProfileStorage {
public:
    static std::wstring GetConfigDirectory();
    static std::wstring GetConfigFilePath();

    static StorageData LoadStorage(const GammaRamp& baselineRamp);
    static bool SaveStorage(const StorageData& data);

    static std::vector<ColorProfile> LoadProfiles();
    static bool SaveProfiles(const std::vector<ColorProfile>& profiles);
    static bool ExportProfile(const ColorProfile& profile, const std::wstring& filePath);
    static std::optional<ColorProfile> ImportProfile(const std::wstring& filePath);
};

} // namespace prismshift

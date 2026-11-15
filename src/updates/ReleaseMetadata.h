#pragma once

#include "updates/SemanticVersion.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace prismshift {

struct ReleaseInfo {
    SemanticVersion version;
    std::wstring tag;
    std::wstring title;
    std::wstring notes;
    std::wstring assetName;
    std::wstring assetUrl;
    std::wstring sha256;
    std::uint64_t assetSize = 0;
};

struct MetadataResult {
    std::optional<ReleaseInfo> release;
    std::wstring error;
};

struct ReleaseNotesResult {
    bool success = false;
    std::wstring version;
    std::wstring title;
    std::wstring notes;
    std::wstring error;
};

[[nodiscard]] MetadataResult ParseStableRelease(std::string_view json,
                                                std::wstring_view owner,
                                                std::wstring_view repository);
[[nodiscard]] ReleaseNotesResult ParseReleaseNotes(std::string_view json,
                                                   std::wstring_view expectedVersion);

} // namespace prismshift

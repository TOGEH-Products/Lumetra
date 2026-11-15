#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prismshift {

struct SemanticVersion {
    std::uint64_t major = 0;
    std::uint64_t minor = 0;
    std::uint64_t patch = 0;
    std::vector<std::wstring> prerelease;
    std::wstring buildMetadata;

    [[nodiscard]] static std::optional<SemanticVersion> Parse(std::wstring_view value);
    [[nodiscard]] int Compare(const SemanticVersion& other) const;
    [[nodiscard]] bool IsPrerelease() const { return !prerelease.empty(); }
    [[nodiscard]] std::wstring ToString() const;
};

} // namespace prismshift

#include "updates/SemanticVersion.h"

#include <algorithm>
#include <limits>

namespace prismshift {
namespace {

bool IsAsciiDigit(wchar_t ch) {
    return ch >= L'0' && ch <= L'9';
}

bool IsIdentifierCharacter(wchar_t ch) {
    return IsAsciiDigit(ch) || (ch >= L'a' && ch <= L'z') ||
           (ch >= L'A' && ch <= L'Z') || ch == L'-';
}

bool IsNumeric(std::wstring_view value) {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), IsAsciiDigit);
}

bool ParseNumber(std::wstring_view value, std::uint64_t& result) {
    if (value.empty() || (value.size() > 1 && value.front() == L'0')) {
        return false;
    }

    result = 0;
    for (wchar_t ch : value) {
        if (!IsAsciiDigit(ch)) return false;
        const auto digit = static_cast<std::uint64_t>(ch - L'0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
            return false;
        }
        result = result * 10 + digit;
    }
    return true;
}

bool SplitIdentifiers(std::wstring_view value,
                      std::vector<std::wstring>& identifiers,
                      bool rejectLeadingZeroNumbers) {
    if (value.empty()) return false;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find(L'.', start);
        const std::size_t length =
            (end == std::wstring_view::npos ? value.size() : end) - start;
        if (length == 0) return false;
        const std::wstring_view part = value.substr(start, length);
        if (!std::all_of(part.begin(), part.end(), IsIdentifierCharacter)) {
            return false;
        }
        if (rejectLeadingZeroNumbers && IsNumeric(part) && part.size() > 1 &&
            part.front() == L'0') {
            return false;
        }
        identifiers.emplace_back(part);
        if (end == std::wstring_view::npos) break;
        start = end + 1;
    }
    return true;
}

int CompareIdentifiers(const std::wstring& left, const std::wstring& right) {
    const bool leftNumeric = IsNumeric(left);
    const bool rightNumeric = IsNumeric(right);
    if (leftNumeric && rightNumeric) {
        if (left.size() != right.size()) return left.size() < right.size() ? -1 : 1;
        if (left == right) return 0;
        return left < right ? -1 : 1;
    }
    if (leftNumeric != rightNumeric) return leftNumeric ? -1 : 1;
    if (left == right) return 0;
    return left < right ? -1 : 1;
}

} // namespace

std::optional<SemanticVersion> SemanticVersion::Parse(std::wstring_view value) {
    if (value.size() > 128) return std::nullopt;
    if (!value.empty() && value.front() == L'v') value.remove_prefix(1);
    if (value.empty()) return std::nullopt;

    SemanticVersion parsed;
    const std::size_t plus = value.find(L'+');
    if (plus != std::wstring_view::npos) {
        if (value.find(L'+', plus + 1) != std::wstring_view::npos) {
            return std::nullopt;
        }
        const std::wstring_view build = value.substr(plus + 1);
        std::vector<std::wstring> buildIdentifiers;
        if (!SplitIdentifiers(build, buildIdentifiers, false)) return std::nullopt;
        parsed.buildMetadata.assign(build);
        value = value.substr(0, plus);
    }

    const std::size_t dash = value.find(L'-');
    if (dash != std::wstring_view::npos) {
        const std::wstring_view prerelease = value.substr(dash + 1);
        if (!SplitIdentifiers(prerelease, parsed.prerelease, true)) {
            return std::nullopt;
        }
        value = value.substr(0, dash);
    }

    const std::size_t firstDot = value.find(L'.');
    if (firstDot == std::wstring_view::npos) return std::nullopt;
    const std::size_t secondDot = value.find(L'.', firstDot + 1);
    if (secondDot == std::wstring_view::npos ||
        value.find(L'.', secondDot + 1) != std::wstring_view::npos) {
        return std::nullopt;
    }
    if (!ParseNumber(value.substr(0, firstDot), parsed.major) ||
        !ParseNumber(value.substr(firstDot + 1, secondDot - firstDot - 1),
                     parsed.minor) ||
        !ParseNumber(value.substr(secondDot + 1), parsed.patch)) {
        return std::nullopt;
    }
    return parsed;
}

int SemanticVersion::Compare(const SemanticVersion& other) const {
    if (major != other.major) return major < other.major ? -1 : 1;
    if (minor != other.minor) return minor < other.minor ? -1 : 1;
    if (patch != other.patch) return patch < other.patch ? -1 : 1;
    if (prerelease.empty() != other.prerelease.empty()) {
        return prerelease.empty() ? 1 : -1;
    }

    const std::size_t common =
        std::min(prerelease.size(), other.prerelease.size());
    for (std::size_t index = 0; index < common; ++index) {
        const int result =
            CompareIdentifiers(prerelease[index], other.prerelease[index]);
        if (result != 0) return result;
    }
    if (prerelease.size() == other.prerelease.size()) return 0;
    return prerelease.size() < other.prerelease.size() ? -1 : 1;
}

std::wstring SemanticVersion::ToString() const {
    std::wstring value = std::to_wstring(major) + L"." +
                         std::to_wstring(minor) + L"." +
                         std::to_wstring(patch);
    if (!prerelease.empty()) {
        value += L"-";
        for (std::size_t index = 0; index < prerelease.size(); ++index) {
            if (index != 0) value += L".";
            value += prerelease[index];
        }
    }
    if (!buildMetadata.empty()) value += L"+" + buildMetadata;
    return value;
}

} // namespace prismshift

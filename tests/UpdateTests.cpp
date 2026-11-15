#include "updates/ReleaseMetadata.h"
#include "updates/SemanticVersion.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void TestSemanticVersionParsingAndOrdering() {
    using prismshift::SemanticVersion;
    const auto release = SemanticVersion::Parse(L"v1.2.3");
    const auto prerelease = SemanticVersion::Parse(L"1.2.3-rc.2");
    const auto laterPrerelease = SemanticVersion::Parse(L"1.2.3-rc.10");
    const auto prior = SemanticVersion::Parse(L"1.2.2");
    const auto same = SemanticVersion::Parse(L"1.2.3+build.5");
    Check(release.has_value(), "stable version parses");
    Check(prerelease.has_value(), "prerelease version parses");
    Check(prerelease && prerelease->IsPrerelease(), "prerelease is identified");
    Check(release && prerelease && release->Compare(*prerelease) > 0,
          "stable version sorts after prerelease");
    Check(prerelease && laterPrerelease &&
              prerelease->Compare(*laterPrerelease) < 0,
          "numeric prerelease identifiers compare numerically");
    Check(prior && release && prior->Compare(*release) < 0,
          "patch versions compare numerically");
    Check(release && same && release->Compare(*same) == 0,
          "build metadata does not change semantic precedence");
    Check(!SemanticVersion::Parse(L"1.02.3"), "leading zero is rejected");
    Check(!SemanticVersion::Parse(L"1.2"), "incomplete version is rejected");
    Check(!SemanticVersion::Parse(L"1.2.3-01"), "numeric prerelease leading zero is rejected");
}

std::string StableMetadata(std::string_view digest =
                               "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                           std::string_view url =
                               "https://github.com/owner/project/releases/download/v1.2.3/Lumetra-1.2.3-win-x64.zip",
                           std::string_view version = "v1.2.3",
                           bool prerelease = false) {
    return std::string("{\"draft\":false,\"prerelease\":") +
           (prerelease ? "true" : "false") +
           ",\"tag_name\":\"" + std::string(version) +
           "\",\"name\":\"Lumetra 1.2.3\",\"body\":\"Release notes\","
           "\"assets\":[{\"name\":\"Lumetra-1.2.3-win-x64.zip\","
           "\"size\":12,\"browser_download_url\":\"" + std::string(url) +
           "\",\"digest\":\"" + std::string(digest) + "\"}]}";
}

void TestReleaseMetadataValidation() {
    using prismshift::ParseStableRelease;
    const auto valid =
        ParseStableRelease(StableMetadata(), L"owner", L"project");
    Check(valid.release.has_value(), "valid stable release metadata parses");
    Check(valid.release && valid.release->assetSize == 12,
          "expected package size is retained");
    Check(valid.release && valid.release->sha256.size() == 64,
          "SHA-256 digest is retained");
    Check(!ParseStableRelease(StableMetadata({}, {}, "v1.2.3-rc.1", true),
                              L"owner", L"project")
               .release,
          "prerelease metadata is rejected");
    Check(!ParseStableRelease(StableMetadata("sha1:aaaaaaaa"), L"owner",
                              L"project")
               .release,
          "missing SHA-256 metadata is rejected");
    Check(!ParseStableRelease(
               StableMetadata({}, "https://example.invalid/package.zip"),
               L"owner", L"project")
               .release,
          "unexpected download URL is rejected");
    Check(!ParseStableRelease("{", L"owner", L"project").release,
          "invalid JSON is rejected");
    Check(!ParseStableRelease(StableMetadata(), L"", L"project").release,
          "missing official repository identity is rejected");

    const std::string notes =
        "{\"draft\":false,\"prerelease\":false,\"tag_name\":\"v1.2.3\","
        "\"name\":\"Lumetra 1.2.3\",\"body\":\"New stable release notes\"}";
    const auto validNotes = prismshift::ParseReleaseNotes(notes, L"1.2.3");
    Check(validNotes.success && validNotes.notes == L"New stable release notes",
          "release notes for the installed stable version parse");
    Check(!prismshift::ParseReleaseNotes(notes, L"1.2.4").success,
          "release notes for a different version are rejected");
}

} // namespace

int main() {
    TestSemanticVersionParsingAndOrdering();
    TestReleaseMetadataValidation();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All update tests passed\n";
    return 0;
}

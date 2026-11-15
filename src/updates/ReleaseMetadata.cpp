#include "updates/ReleaseMetadata.h"

#include "utilities/JsonHelper.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace prismshift {
namespace {

const JsonValue* GetField(const JsonValue& object, const wchar_t* name) {
    return object.Get(name);
}

bool ReadString(const JsonValue& object, const wchar_t* name,
                std::wstring& value) {
    const JsonValue* field = GetField(object, name);
    if (!field || !field->IsString()) return false;
    value = field->AsString();
    return true;
}

bool IsSafeRepositoryPart(std::wstring_view value) {
    return !value.empty() && value != L"." && value != L".." &&
           std::all_of(value.begin(), value.end(), [](wchar_t ch) {
        return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
               (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'_' ||
               ch == L'.';
    });
}

bool IsSha256(std::wstring_view value) {
    return value.size() == 64 &&
           std::all_of(value.begin(), value.end(), [](wchar_t ch) {
               return (ch >= L'0' && ch <= L'9') ||
                      (ch >= L'a' && ch <= L'f') ||
                      (ch >= L'A' && ch <= L'F');
           });
}

} // namespace

MetadataResult ParseStableRelease(std::string_view json,
                                  std::wstring_view owner,
                                  std::wstring_view repository) {
    MetadataResult result;
    if (!IsSafeRepositoryPart(owner) || !IsSafeRepositoryPart(repository)) {
        result.error = L"An official GitHub owner and repository must be configured.";
        return result;
    }

    const auto parsed = JsonValue::Parse(json);
    if (!parsed || !parsed->IsObject()) {
        result.error = L"GitHub returned invalid release metadata.";
        return result;
    }

    const JsonValue* draft = GetField(*parsed, L"draft");
    const JsonValue* prerelease = GetField(*parsed, L"prerelease");
    std::wstring tag;
    if (!draft || !draft->IsBool() || draft->AsBool() ||
        !prerelease || !prerelease->IsBool() || prerelease->AsBool() ||
        !ReadString(*parsed, L"tag_name", tag)) {
        result.error = L"GitHub did not return a published stable release.";
        return result;
    }

    const auto version = SemanticVersion::Parse(tag);
    if (!version || version->IsPrerelease()) {
        result.error = L"The release tag is not a valid stable semantic version.";
        return result;
    }

    const JsonValue* assets = GetField(*parsed, L"assets");
    if (!assets || !assets->IsArray()) {
        result.error = L"The release has no valid download assets.";
        return result;
    }

    ReleaseInfo release;
    release.version = *version;
    release.tag = tag;
    ReadString(*parsed, L"name", release.title);
    ReadString(*parsed, L"body", release.notes);
    if (release.notes.size() > 16000) release.notes.resize(16000);

    release.assetName = L"Lumetra-" + version->ToString() + L"-win-x64.zip";
    const std::wstring expectedUrl =
        L"https://github.com/" + std::wstring(owner) + L"/" +
        std::wstring(repository) + L"/releases/download/" + tag + L"/" +
        release.assetName;
    constexpr std::uint64_t kMaxAssetSize = 500ULL * 1024 * 1024;
    bool foundAsset = false;
    for (const JsonValue& asset : assets->AsArray()) {
        if (!asset.IsObject()) continue;
        std::wstring name;
        std::wstring url;
        std::wstring digest;
        const JsonValue* size = GetField(asset, L"size");
        if (!ReadString(asset, L"name", name) || name != release.assetName) {
            continue;
        }
        if (!ReadString(asset, L"browser_download_url", url) ||
            url != expectedUrl || !size || !size->IsNumber()) {
            result.error = L"The release asset URL or size did not match the expected package.";
            return result;
        }
        const double sizeValue = size->AsDouble(-1.0);
        if (!std::isfinite(sizeValue) || sizeValue < 1.0 ||
            sizeValue > static_cast<double>(kMaxAssetSize) ||
            std::floor(sizeValue) != sizeValue) {
            result.error = L"The release asset size is invalid or exceeds the download limit.";
            return result;
        }
        if (!ReadString(asset, L"digest", digest) ||
            digest.rfind(L"sha256:", 0) != 0 ||
            !IsSha256(std::wstring_view(digest).substr(7))) {
            result.error = L"The release does not provide a valid SHA-256 asset digest.";
            return result;
        }

        release.assetUrl = std::move(url);
        release.sha256 = digest.substr(7);
        std::transform(release.sha256.begin(), release.sha256.end(),
                       release.sha256.begin(), [](wchar_t ch) {
                           return static_cast<wchar_t>(towlower(ch));
                       });
        release.assetSize = static_cast<std::uint64_t>(sizeValue);
        foundAsset = true;
        break;
    }

    if (!foundAsset) {
        result.error = L"The expected x64 release package was not found.";
        return result;
    }

    result.release = std::move(release);
    return result;
}

ReleaseNotesResult ParseReleaseNotes(std::string_view json,
                                     std::wstring_view expectedVersion) {
    ReleaseNotesResult result;
    const auto expected = SemanticVersion::Parse(expectedVersion);
    const auto parsed = JsonValue::Parse(json);
    if (!expected || expected->IsPrerelease() || !parsed ||
        !parsed->IsObject()) {
        result.error = L"Release notes metadata is invalid.";
        return result;
    }

    const JsonValue* draft = GetField(*parsed, L"draft");
    const JsonValue* prerelease = GetField(*parsed, L"prerelease");
    std::wstring tag;
    if (!draft || !draft->IsBool() || draft->AsBool() ||
        !prerelease || !prerelease->IsBool() || prerelease->AsBool() ||
        !ReadString(*parsed, L"tag_name", tag)) {
        result.error = L"The requested release is not a published stable release.";
        return result;
    }
    const auto actual = SemanticVersion::Parse(tag);
    if (!actual || actual->IsPrerelease() || actual->Compare(*expected) != 0) {
        result.error = L"GitHub returned notes for a different version.";
        return result;
    }
    if (!ReadString(*parsed, L"body", result.notes)) {
        result.error = L"The release does not contain valid notes.";
        return result;
    }
    if (result.notes.size() > 16000) result.notes.resize(16000);
    ReadString(*parsed, L"name", result.title);
    result.version = expected->ToString();
    result.success = true;
    return result;
}

} // namespace prismshift

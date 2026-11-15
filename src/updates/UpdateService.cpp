#include "updates/UpdateService.h"

#include "utilities/JsonHelper.h"
#include "utilities/Logger.h"

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <memory>
#include <vector>

namespace prismshift {
namespace {

constexpr std::size_t kMaxApiResponseBytes = 2 * 1024 * 1024;
constexpr std::uint64_t kMaxDownloadBytes = 500ULL * 1024 * 1024;

bool IsSha256(std::wstring_view digest) {
    return digest.size() == 64 &&
           std::all_of(digest.begin(), digest.end(), [](wchar_t ch) {
               return (ch >= L'0' && ch <= L'9') ||
                      (ch >= L'a' && ch <= L'f') ||
                      (ch >= L'A' && ch <= L'F');
           });
}

struct WinHandle {
    HINTERNET value = nullptr;
    ~WinHandle() { if (value) WinHttpCloseHandle(value); }
    WinHandle(const WinHandle&) = delete;
    WinHandle& operator=(const WinHandle&) = delete;
    WinHandle() = default;
};

struct FileHandle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~FileHandle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    FileHandle() = default;
};

struct HttpUrl {
    std::wstring host;
    std::wstring path;
};

bool ParseHttpsUrl(const std::wstring& url, HttpUrl& parsed) {
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0,
                         &components) ||
        components.nScheme != INTERNET_SCHEME_HTTPS) {
        return false;
    }
    parsed.host.assign(components.lpszHostName, components.dwHostNameLength);
    parsed.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength != 0) {
        parsed.path.append(components.lpszExtraInfo,
                           components.dwExtraInfoLength);
    }
    return !parsed.host.empty() && !parsed.path.empty();
}

std::wstring WinHttpError(const wchar_t* context) {
    return std::wstring(context) + L" (Windows error " +
           std::to_wstring(GetLastError()) + L").";
}

bool OpenRequest(const std::wstring& url, const wchar_t* userAgent,
                 const wchar_t* headers, WinHandle& session,
                 WinHandle& connection, WinHandle& request,
                 std::wstring& error, const std::atomic_bool* cancel) {
    if (cancel && cancel->load()) {
        error = L"The update request was cancelled.";
        return false;
    }
    HttpUrl parts;
    if (!ParseHttpsUrl(url, parts)) {
        error = L"Only HTTPS update URLs are allowed.";
        return false;
    }
    session.value = WinHttpOpen(userAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                                0);
    if (!session.value) {
        error = WinHttpError(L"Could not initialize the secure update connection");
        return false;
    }
    if (!WinHttpSetTimeouts(session.value, 5000, 5000, 3000, 3000)) {
        error = WinHttpError(L"Could not configure update connection timeouts");
        return false;
    }
    connection.value = WinHttpConnect(session.value, parts.host.c_str(),
                                      INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection.value) {
        error = WinHttpError(L"Could not connect to the update host");
        return false;
    }
    request.value = WinHttpOpenRequest(connection.value, L"GET",
                                       parts.path.c_str(), nullptr,
                                       WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       WINHTTP_FLAG_SECURE);
    if (!request.value) {
        error = WinHttpError(L"Could not create the update request");
        return false;
    }
    if (headers && !WinHttpAddRequestHeaders(
                       request.value, headers, static_cast<DWORD>(-1),
                       WINHTTP_ADDREQ_FLAG_ADD)) {
        error = WinHttpError(L"Could not configure the update request");
        return false;
    }
    if (!WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) {
        error = WinHttpError(L"The update server could not be reached");
        return false;
    }
    if (cancel && cancel->load()) {
        error = L"The update request was cancelled.";
        return false;
    }
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.value,
                             WINHTTP_QUERY_STATUS_CODE |
                                 WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status,
                             &statusSize, WINHTTP_NO_HEADER_INDEX) ||
        status != HTTP_STATUS_OK) {
        error = status == HTTP_STATUS_NOT_FOUND
                    ? L"The configured GitHub repository was not found."
                    : L"The update server returned an unsuccessful response.";
        return false;
    }
    return true;
}

bool ReadResponse(HINTERNET request, std::string& response,
                  std::wstring& error, const std::atomic_bool* cancel) {
    for (;;) {
        if (cancel && cancel->load()) {
            error = L"The update request was cancelled.";
            return false;
        }
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            error = WinHttpError(L"Could not read the update response");
            return false;
        }
        if (available == 0) return true;
        if (available > kMaxApiResponseBytes ||
            response.size() > kMaxApiResponseBytes - available) {
            error = L"The GitHub response exceeded the size limit.";
            return false;
        }
        const std::size_t start = response.size();
        response.resize(start + available);
        DWORD read = 0;
        if (!WinHttpReadData(request, response.data() + start, available, &read)) {
            error = WinHttpError(L"Could not read the update response");
            return false;
        }
        response.resize(start + read);
        if (read == 0) return true;
    }
}

bool ReadSha256(const std::wstring& path, std::wstring& digest,
                std::wstring& error, const std::atomic_bool* cancel) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD hashLength = 0;
    DWORD bytes = 0;
    std::vector<UCHAR> hashObject;
    std::array<UCHAR, 64 * 1024> buffer{};

    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status < 0) {
        error = L"Windows could not initialize SHA-256 verification.";
        return false;
    }
    auto closeAlgorithm = [&]() {
        if (hash) BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
    };
    status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                               reinterpret_cast<PUCHAR>(&objectLength),
                               sizeof(objectLength), &bytes, 0);
    if (status >= 0) {
        status = BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                                   reinterpret_cast<PUCHAR>(&hashLength),
                                   sizeof(hashLength), &bytes, 0);
    }
    if (status < 0 || hashLength != 32 || objectLength == 0) {
        closeAlgorithm();
        error = L"Windows could not initialize SHA-256 verification.";
        return false;
    }
    hashObject.resize(objectLength);
    std::array<UCHAR, 32> hashValue{};
    status = BCryptCreateHash(algorithm, &hash, hashObject.data(),
                              objectLength, nullptr, 0, 0);
    if (status < 0) {
        closeAlgorithm();
        error = L"Windows could not initialize SHA-256 verification.";
        return false;
    }

    FileHandle file;
    file.value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                             nullptr);
    if (file.value == INVALID_HANDLE_VALUE) {
        closeAlgorithm();
        error = L"Could not reopen the downloaded file for integrity verification.";
        return false;
    }
    for (;;) {
        if (cancel && cancel->load()) {
            closeAlgorithm();
            error = L"The update download was cancelled.";
            return false;
        }
        DWORD read = 0;
        if (!ReadFile(file.value, buffer.data(),
                      static_cast<DWORD>(buffer.size()), &read, nullptr)) {
            closeAlgorithm();
            error = L"Could not read the downloaded file for integrity verification.";
            return false;
        }
        if (read == 0) break;
        status = BCryptHashData(hash, buffer.data(), read, 0);
        if (status < 0) {
            closeAlgorithm();
            error = L"SHA-256 integrity verification failed.";
            return false;
        }
    }
    status = BCryptFinishHash(hash, hashValue.data(),
                              static_cast<ULONG>(hashValue.size()), 0);
    closeAlgorithm();
    if (status < 0) {
        error = L"SHA-256 integrity verification failed.";
        return false;
    }

    static constexpr wchar_t hex[] = L"0123456789abcdef";
    digest.clear();
    digest.reserve(hashValue.size() * 2);
    for (UCHAR byte : hashValue) {
        digest.push_back(hex[byte >> 4]);
        digest.push_back(hex[byte & 0x0f]);
    }
    return true;
}

bool IsZipFile(const std::wstring& path) {
    FileHandle file;
    file.value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                             nullptr);
    if (file.value == INVALID_HANDLE_VALUE) return false;
    std::array<BYTE, 4> signature{};
    DWORD read = 0;
    if (!ReadFile(file.value, signature.data(),
                  static_cast<DWORD>(signature.size()), &read, nullptr) ||
        read != signature.size()) {
        return false;
    }
    return signature[0] == 'P' && signature[1] == 'K' &&
           ((signature[2] == 3 && signature[3] == 4) ||
            (signature[2] == 5 && signature[3] == 6) ||
            (signature[2] == 7 && signature[3] == 8));
}

bool WriteDownload(HINTERNET request, const std::wstring& partialPath,
                   std::uint64_t expectedSize, std::wstring& error,
                   const std::atomic_bool* cancel) {
    DWORD contentLength = 0;
    DWORD contentLengthSize = sizeof(contentLength);
    if (!WinHttpQueryHeaders(request,
                             WINHTTP_QUERY_CONTENT_LENGTH |
                                 WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &contentLength,
                             &contentLengthSize, WINHTTP_NO_HEADER_INDEX) ||
        contentLength != expectedSize) {
        error = L"The download size did not match the published release metadata.";
        return false;
    }

    FileHandle file;
    file.value = CreateFileW(partialPath.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file.value == INVALID_HANDLE_VALUE) {
        error = L"Could not create the update download file.";
        return false;
    }

    std::array<BYTE, 64 * 1024> buffer{};
    std::uint64_t total = 0;
    for (;;) {
        if (cancel && cancel->load()) {
            error = L"The update download was cancelled.";
            return false;
        }
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            error = WinHttpError(L"The update download was interrupted");
            return false;
        }
        if (available == 0) break;
        if (total + available > expectedSize ||
            total + available > kMaxDownloadBytes) {
            error = L"The downloaded file exceeded its expected size.";
            return false;
        }
        DWORD remaining = available;
        while (remaining > 0) {
            if (cancel && cancel->load()) {
                error = L"The update download was cancelled.";
                return false;
            }
            const DWORD requested =
                std::min<DWORD>(remaining, static_cast<DWORD>(buffer.size()));
            DWORD read = 0;
            if (!WinHttpReadData(request, buffer.data(), requested, &read) ||
                read == 0) {
                error = WinHttpError(L"The update download was interrupted");
                return false;
            }
            DWORD written = 0;
            if (!WriteFile(file.value, buffer.data(), read, &written, nullptr) ||
                written != read) {
                error = L"Could not save the update download.";
                return false;
            }
            total += read;
            remaining -= read;
        }
    }
    if (total != expectedSize) {
        error = L"The update download was incomplete.";
        return false;
    }
    if (!FlushFileBuffers(file.value)) {
        error = L"Could not flush the completed update download to disk.";
        return false;
    }
    return true;
}

} // namespace

UpdateService::UpdateService(std::wstring owner, std::wstring repository,
                             std::wstring currentVersion)
    : m_owner(std::move(owner)), m_repository(std::move(repository)),
      m_currentVersion(std::move(currentVersion)) {}

bool UpdateService::IsConfigured(std::wstring_view owner,
                                 std::wstring_view repository) {
    return !owner.empty() && !repository.empty();
}

UpdateCheckResult UpdateService::CheckLatestRelease(
    const std::atomic_bool* cancel) const {
    UpdateCheckResult result;
    result.currentVersion = m_currentVersion;
    if (!IsConfigured(m_owner, m_repository)) {
        result.message = L"Update checks are not configured for this build.";
        return result;
    }
    const auto current = SemanticVersion::Parse(m_currentVersion);
    if (!current || current->IsPrerelease()) {
        result.message = L"The installed application version is invalid.";
        return result;
    }

    const std::wstring url = L"https://api.github.com/repos/" + m_owner + L"/" +
                             m_repository + L"/releases/latest";
    const std::wstring userAgent = L"Lumetra/" + m_currentVersion;
    constexpr wchar_t headers[] =
        L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2022-11-28\r\n";
    WinHandle session;
    WinHandle connection;
    WinHandle request;
    std::wstring error;
    if (!OpenRequest(url, userAgent.c_str(), headers, session, connection,
                     request, error, cancel)) {
        result.message = std::move(error);
        return result;
    }

    std::string response;
    if (!ReadResponse(request.value, response, error, cancel)) {
        result.message = std::move(error);
        return result;
    }
    MetadataResult metadata =
        ParseStableRelease(response, m_owner, m_repository);
    if (!metadata.release) {
        result.message = std::move(metadata.error);
        return result;
    }

    if (metadata.release->version.Compare(*current) <= 0) {
        result.status = UpdateCheckStatus::UpToDate;
        result.message = L"Lumetra is up to date.";
        return result;
    }
    result.status = UpdateCheckStatus::UpdateAvailable;
    result.message = L"A new stable release is available.";
    result.release = std::move(*metadata.release);
    return result;
}

ReleaseNotesResult UpdateService::GetReleaseNotes(
    std::wstring_view version, const std::atomic_bool* cancel) const {
    ReleaseNotesResult result;
    const auto parsedVersion = SemanticVersion::Parse(version);
    if (!IsConfigured(m_owner, m_repository) || !parsedVersion ||
        parsedVersion->IsPrerelease()) {
        result.error = L"Release notes are not configured for this stable version.";
        return result;
    }

    const std::wstring encodedVersion = parsedVersion->ToString();
    const std::wstring url =
        L"https://api.github.com/repos/" + m_owner + L"/" + m_repository +
        L"/releases/tags/v" + encodedVersion;
    const std::wstring userAgent = L"Lumetra/" + m_currentVersion;
    constexpr wchar_t headers[] =
        L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2022-11-28\r\n";
    WinHandle session;
    WinHandle connection;
    WinHandle request;
    std::wstring error;
    if (!OpenRequest(url, userAgent.c_str(), headers, session, connection,
                     request, error, cancel)) {
        result.error = std::move(error);
        return result;
    }

    std::string response;
    if (!ReadResponse(request.value, response, error, cancel)) {
        result.error = std::move(error);
        return result;
    }
    return ParseReleaseNotes(response, encodedVersion);
}

UpdateDownloadResult UpdateService::DownloadRelease(
    const ReleaseInfo& release, const std::atomic_bool* cancel) const {
    UpdateDownloadResult result;
    if (!IsConfigured(m_owner, m_repository)) {
        result.message = L"Update downloads are not configured for this build.";
        return result;
    }
    const auto version = SemanticVersion::Parse(release.tag);
    if (!version || version->IsPrerelease() ||
        version->ToString() != release.version.ToString() ||
        release.assetName !=
            L"Lumetra-" + version->ToString() + L"-win-x64.zip" ||
        release.assetUrl !=
            L"https://github.com/" + m_owner + L"/" + m_repository +
                L"/releases/download/" + release.tag + L"/" +
                release.assetName ||
        release.assetSize == 0 || release.assetSize > kMaxDownloadBytes ||
        !IsSha256(release.sha256)) {
        result.message = L"The release download metadata failed validation.";
        return result;
    }

    HttpUrl urlParts;
    if (!ParseHttpsUrl(release.assetUrl, urlParts) ||
        urlParts.host != L"github.com") {
        result.message = L"Only the configured GitHub release host is allowed.";
        return result;
    }

    PWSTR downloadsPath = nullptr;
    const HRESULT knownFolderResult =
        SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DEFAULT, nullptr,
                             &downloadsPath);
    if (FAILED(knownFolderResult) || !downloadsPath) {
        result.message = L"Could not find the Windows Downloads folder.";
        return result;
    }
    std::wstring directory(downloadsPath);
    CoTaskMemFree(downloadsPath);
    directory += L"\\Lumetra";
    if (!CreateDirectoryW(directory.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        result.message = L"Could not create a Lumetra folder in Downloads.";
        return result;
    }
    const DWORD directoryAttributes = GetFileAttributesW(directory.c_str());
    if (directoryAttributes == INVALID_FILE_ATTRIBUTES ||
        (directoryAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        result.message = L"The Lumetra download path is not a directory.";
        return result;
    }

    const std::wstring filePath = directory + L"\\" + release.assetName;
    const std::wstring partialPath =
        filePath + L"." + std::to_wstring(GetCurrentProcessId()) + L".part";
    if (GetFileAttributesW(partialPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        result.message = L"Another Lumetra download is already using this temporary file.";
        return result;
    }

    const std::wstring userAgent = L"Lumetra/" + m_currentVersion;
    WinHandle session;
    WinHandle connection;
    WinHandle request;
    std::wstring error;
    if (!OpenRequest(release.assetUrl, userAgent.c_str(), nullptr, session,
                     connection, request, error, cancel)) {
        result.message = std::move(error);
        return result;
    }
    if (!WriteDownload(request.value, partialPath, release.assetSize, error,
                       cancel)) {
        DeleteFileW(partialPath.c_str());
        result.message = std::move(error);
        return result;
    }

    std::wstring actualDigest;
    if (!IsZipFile(partialPath)) {
        DeleteFileW(partialPath.c_str());
        result.message = L"The downloaded file is not the expected ZIP package.";
        return result;
    }
    if (!ReadSha256(partialPath, actualDigest, error, cancel)) {
        DeleteFileW(partialPath.c_str());
        result.message = std::move(error);
        return result;
    }
    if (_wcsicmp(actualDigest.c_str(), release.sha256.c_str()) != 0) {
        DeleteFileW(partialPath.c_str());
        result.message = L"The downloaded file did not match its SHA-256 digest.";
        return result;
    }
    if (!MoveFileExW(partialPath.c_str(), filePath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(partialPath.c_str());
        result.message = L"Could not finalize the verified update download.";
        return result;
    }

    result.success = true;
    result.filePath = filePath;
    result.message =
        L"The package was downloaded and its SHA-256 integrity check passed. "
        L"It was not executed or installed.";
    Logger::Info(L"Verified a release package for manual inspection; no execution was attempted.");
    return result;
}

} // namespace prismshift

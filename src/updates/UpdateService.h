#pragma once

#include "updates/ReleaseMetadata.h"

#include <atomic>
#include <string>
#include <string_view>

namespace prismshift {

enum class UpdateCheckStatus {
    UpdateAvailable,
    UpToDate,
    Error
};

struct UpdateCheckResult {
    UpdateCheckStatus status = UpdateCheckStatus::Error;
    std::wstring currentVersion;
    std::wstring message;
    ReleaseInfo release;
};

struct UpdateDownloadResult {
    bool success = false;
    std::wstring filePath;
    std::wstring message;
};

class UpdateService {
public:
    UpdateService(std::wstring owner, std::wstring repository,
                  std::wstring currentVersion);

    [[nodiscard]] UpdateCheckResult CheckLatestRelease(
        const std::atomic_bool* cancel = nullptr) const;
    [[nodiscard]] ReleaseNotesResult GetReleaseNotes(
        std::wstring_view version,
        const std::atomic_bool* cancel = nullptr) const;
    [[nodiscard]] UpdateDownloadResult DownloadRelease(
        const ReleaseInfo& release,
        const std::atomic_bool* cancel = nullptr) const;

    [[nodiscard]] static bool IsConfigured(std::wstring_view owner,
                                           std::wstring_view repository);

private:
    std::wstring m_owner;
    std::wstring m_repository;
    std::wstring m_currentVersion;
};

} // namespace prismshift

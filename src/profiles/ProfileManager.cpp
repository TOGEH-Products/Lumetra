#include "profiles/ProfileManager.h"
#include "profiles/ProfileStorage.h"
#include "detection/ProcessHelper.h"
#include "utilities/Logger.h"

namespace prismshift {

ProfileManager::ProfileManager() = default;

void ProfileManager::Initialize(const GammaRamp& baselineRamp) {
    StorageData data = ProfileStorage::LoadStorage(baselineRamp);
    m_monitorProfile = std::move(data.monitorProfile);
    m_profiles = std::move(data.gameProfiles);

    // Ensure all target processes are lowercased for fast matching
    for (auto& profile : m_profiles) {
        for (auto& target : profile.targetProcesses) {
            target = ProcessHelper::ToLower(target);
        }
        profile.RecomputeRamp();
    }
    m_monitorProfile.RecomputeRamp();

    m_initialized = true;
    Logger::Info(L"ProfileManager initialized with Monitor Customization and " +
                 std::to_wstring(m_profiles.size()) + L" game profiles.");
}

void ProfileManager::SetMonitorProfile(const ColorProfile& profile) {
    m_monitorProfile = profile;
    m_monitorProfile.id = L"monitor";
    m_monitorProfile.isDefault = true;
    m_monitorProfile.RecomputeRamp();
    Save();
}

const ColorProfile* ProfileManager::MatchProfile(std::wstring_view exeName) const {
    if (exeName.empty()) {
        return &m_monitorProfile;
    }

    std::wstring lowerExe = ProcessHelper::ToLower(exeName);

    for (const auto& profile : m_profiles) {
        if (!profile.enabled) {
            continue; // Skip disabled profiles
        }

        for (const auto& target : profile.targetProcesses) {
            if (target == lowerExe) {
                return &profile;
            }
        }
    }

    // Unmapped application or profile disabled -> Monitor Customization
    return &m_monitorProfile;
}

const ColorProfile* ProfileManager::GetProfileById(const std::wstring& id) const {
    if (id == L"monitor" || id == L"default") {
        return &m_monitorProfile;
    }
    for (const auto& p : m_profiles) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

ColorProfile* ProfileManager::GetProfileByIdMut(const std::wstring& id) {
    if (id == L"monitor" || id == L"default") {
        return &m_monitorProfile;
    }
    for (auto& p : m_profiles) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

bool ProfileManager::UpdateProfile(const ColorProfile& updated) {
    if (!updated.IsValid()) {
        Logger::Warn(L"Rejected invalid profile update: [" + updated.id + L"]");
        return false;
    }

    if (updated.id == L"monitor" || updated.id == L"default") {
        m_monitorProfile = updated;
        m_monitorProfile.id = L"monitor";
        m_monitorProfile.isDefault = true;
        m_monitorProfile.RecomputeRamp();
        Save();
        return true;
    }

    for (auto& p : m_profiles) {
        if (p.id == updated.id) {
            p = updated;
            for (auto& target : p.targetProcesses) {
                target = ProcessHelper::ToLower(target);
            }
            p.RecomputeRamp();
            Save();
            return true;
        }
    }
    return false;
}

bool ProfileManager::AddProfile(ColorProfile profile) {
    if (profile.id.empty() || profile.name.empty()) {
        return false;
    }
    if (GetProfileById(profile.id) != nullptr) {
        return false;
    }

    for (auto& target : profile.targetProcesses) {
        target = ProcessHelper::ToLower(target);
    }
    profile.RecomputeRamp();
    if (!profile.IsValid()) {
        return false;
    }
    m_profiles.push_back(std::move(profile));
    Save();
    return true;
}

bool ProfileManager::DuplicateProfile(const std::wstring& id, std::wstring& newId) {
    const auto* source = GetProfileById(id);
    if (!source || source->isDefault) {
        return false;
    }

    ColorProfile copy = *source;
    newId = source->id + L"_copy";
    int suffix = 2;
    while (GetProfileById(newId)) {
        newId = source->id + L"_copy_" + std::to_wstring(suffix++);
    }
    copy.id = newId;
    copy.name += L" Copy";
    return AddProfile(std::move(copy));
}

bool ProfileManager::RenameProfile(const std::wstring& id, const std::wstring& newName) {
    if (newName.empty()) {
        return false;
    }
    auto* profile = GetProfileByIdMut(id);
    if (!profile || profile->isDefault) {
        return false;
    }
    profile->name = newName;
    return Save();
}

bool ProfileManager::DeleteProfile(const std::wstring& id) {
    auto it = std::remove_if(m_profiles.begin(), m_profiles.end(), [&](const ColorProfile& p) {
        return p.id == id;
    });

    if (it != m_profiles.end()) {
        m_profiles.erase(it, m_profiles.end());
        Save();
        return true;
    }
    return false;
}

bool ProfileManager::SetProfileEnabled(const std::wstring& id, bool enabled) {
    for (auto& p : m_profiles) {
        if (p.id == id) {
            p.enabled = enabled;
            Save();
            return true;
        }
    }
    return false;
}

std::vector<ColorProfile> ProfileManager::SearchProfiles(std::wstring_view query) const {
    std::vector<ColorProfile> matches;
    // Monitor Customization now lives in its own nav tab — exclude it from
    // the game-profiles list so it doesn't appear as a duplicate entry.

    if (query.empty()) {
        matches.insert(matches.end(), m_profiles.begin(), m_profiles.end());
        return matches;
    }

    std::wstring lowerQuery = ProcessHelper::ToLower(query);
    for (const auto& profile : m_profiles) {
        if (ProcessHelper::ToLower(profile.name).find(lowerQuery) != std::wstring::npos ||
            ProcessHelper::ToLower(profile.id).find(lowerQuery) != std::wstring::npos) {
            matches.push_back(profile);
            continue;
        }
        for (const auto& target : profile.targetProcesses) {
            if (ProcessHelper::ToLower(target).find(lowerQuery) != std::wstring::npos) {
                matches.push_back(profile);
                break;
            }
        }
    }
    return matches;
}

ProfileManager::GameMatchResult ProfileManager::TestGameLogicMatch(std::wstring_view exeName) const {
    GameMatchResult res;
    if (exeName.empty()) {
        res.matched = false;
        res.isOverride = false;
        res.profile = &m_monitorProfile;
        res.matchedExe = L"";
        res.reason = L"Desktop / Unmanaged window. Monitor Customization applies.";
        return res;
    }

    std::wstring lowerExe = ProcessHelper::ToLower(exeName);
    for (const auto& profile : m_profiles) {
        for (const auto& target : profile.targetProcesses) {
            if (target == lowerExe) {
                if (profile.enabled) {
                    res.matched = true;
                    res.isOverride = true;
                    res.profile = &profile;
                    res.matchedExe = target;
                    res.reason = L"Game profile [" + profile.name + L"] matches " + target + L" and OVERRIDES Monitor Customization.";
                    return res;
                } else {
                    res.matched = true;
                    res.isOverride = false;
                    res.profile = &m_monitorProfile;
                    res.matchedExe = target;
                    res.reason = L"Game profile [" + profile.name + L"] matches " + target + L" but is DISABLED. Monitor Customization applies.";
                    return res;
                }
            }
        }
    }

    res.matched = false;
    res.isOverride = false;
    res.profile = &m_monitorProfile;
    res.matchedExe = lowerExe;
    res.reason = L"No game rule matched for " + lowerExe + L". Monitor Customization applies.";
    return res;
}

bool ProfileManager::Save() {
    StorageData data;
    data.monitorProfile = m_monitorProfile;
    data.gameProfiles = m_profiles;
    return ProfileStorage::SaveStorage(data);
}

} // namespace prismshift

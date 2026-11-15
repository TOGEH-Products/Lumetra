#pragma once

#include "profiles/ColorProfile.h"
#include "utilities/NonCopyable.h"
#include <vector>
#include <string>
#include <string_view>
#include <optional>

namespace prismshift {

class ProfileManager : public NonCopyable {
public:
    ProfileManager();
    ~ProfileManager() = default;

    void Initialize(const GammaRamp& baselineRamp);

    [[nodiscard]] const ColorProfile* MatchProfile(std::wstring_view exeName) const;

    [[nodiscard]] const ColorProfile& GetMonitorProfile() const { return m_monitorProfile; }
    [[nodiscard]] ColorProfile& GetMonitorProfileMut() { return m_monitorProfile; }
    void SetMonitorProfile(const ColorProfile& profile);

    [[nodiscard]] const ColorProfile& GetDefaultProfile() const { return m_monitorProfile; }
    [[nodiscard]] const std::vector<ColorProfile>& GetAllProfiles() const { return m_profiles; }
    [[nodiscard]] std::vector<ColorProfile>& GetAllProfilesMut() { return m_profiles; }

    [[nodiscard]] const ColorProfile* GetProfileById(const std::wstring& id) const;
    [[nodiscard]] ColorProfile* GetProfileByIdMut(const std::wstring& id);

    bool UpdateProfile(const ColorProfile& updated);
    bool AddProfile(ColorProfile profile);
    bool DuplicateProfile(const std::wstring& id, std::wstring& newId);
    bool RenameProfile(const std::wstring& id, const std::wstring& newName);
    bool DeleteProfile(const std::wstring& id);
    bool SetProfileEnabled(const std::wstring& id, bool enabled);
    [[nodiscard]] std::vector<ColorProfile> SearchProfiles(std::wstring_view query) const;

    struct GameMatchResult {
        bool matched = false;
        bool isOverride = false;
        const ColorProfile* profile = nullptr;
        std::wstring matchedExe;
        std::wstring reason;
    };
    [[nodiscard]] GameMatchResult TestGameLogicMatch(std::wstring_view exeName) const;

    bool Save();

private:
    ColorProfile m_monitorProfile;
    std::vector<ColorProfile> m_profiles;
    bool m_initialized = false;
};

} // namespace prismshift

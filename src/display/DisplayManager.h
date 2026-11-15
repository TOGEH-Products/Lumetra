#pragma once

#include "display/IColorEngine.h"
#include "profiles/ColorProfile.h"
#include "utilities/NonCopyable.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace prismshift {

struct DisplayDescriptor {
    std::wstring deviceName;
    RECT bounds{};
    bool isPrimary = false;
};

class DisplayManager : public NonCopyable {
public:
    DisplayManager();
    explicit DisplayManager(std::unique_ptr<IColorEngine> engine);
    ~DisplayManager();

    bool Initialize();
    void Shutdown();

    bool ApplyProfile(const ColorProfile& profile);
    bool ApplyMonitorCustomization();

    bool PreviewProfile(const ColorProfile& profile);
    bool TogglePreviewCompare();
    [[nodiscard]] bool IsPreviewActive() const { return m_previewActive; }
    [[nodiscard]] bool IsPreviewComparing() const { return m_previewComparing; }
    [[nodiscard]] const ColorProfile& GetPreviewTargetProfile() const { return m_previewTargetProfile; }
    bool CancelPreview();
    void CommitPreview();

    bool RestoreDefault();
    bool ResetDisplayToIdentity();
    void EnsureCurrentProfileApplied();

    void SetMonitorProfile(const ColorProfile& profile);
    [[nodiscard]] const ColorProfile& GetMonitorProfile() const { return m_monitorProfile; }

    [[nodiscard]] const ColorProfile* GetActiveProfile() const {
        return m_activeProfile ? &*m_activeProfile : nullptr;
    }
    [[nodiscard]] IColorEngine* GetEngine() { return m_engine.get(); }
    [[nodiscard]] ColorEngineCapabilities GetCapabilities() const {
        return m_engine ? m_engine->GetCapabilities() : ColorEngineCapabilities{};
    }

    void OnDisplayConfigurationChanged();
    [[nodiscard]] std::vector<DisplayDescriptor> GetAvailableDisplays() const;

private:
    bool ApplyProfileInternal(const ColorProfile& profile);

    std::unique_ptr<IColorEngine> m_engine;
    ColorProfile m_monitorProfile;
    std::optional<ColorProfile> m_activeProfile;
    std::optional<ColorProfile> m_previewPreviousProfile;
    ColorProfile m_previewTargetProfile;
    bool m_previewActive = false;
    bool m_previewComparing = false;
    bool m_identityResetActive = false;
    bool m_initialized = false;
};

} // namespace prismshift

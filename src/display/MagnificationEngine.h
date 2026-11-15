#pragma once

#include "utilities/NonCopyable.h"
#include <windows.h>
#include <magnification.h>

namespace prismshift {

class MagnificationEngine : public NonCopyable {
public:
    MagnificationEngine();
    ~MagnificationEngine();

    bool Initialize();
    void Shutdown();
    [[nodiscard]] bool IsInitialized() const { return m_initialized; }
    [[nodiscard]] bool IsAvailable() const { return m_available; }

    bool SetColorEffect(double saturation, int vibrance, int hue);
    bool ReassertColorEffect(double saturation, int vibrance, int hue);
    bool ResetColorEffect();

    static MAGCOLOREFFECT BuildMatrix(double saturation, int vibrance, int hue);

private:
    using PFN_MagInitialize = BOOL(WINAPI*)(void);
    using PFN_MagUninitialize = BOOL(WINAPI*)(void);
    using PFN_MagSetFullscreenColorEffect = BOOL(WINAPI*)(PMAGCOLOREFFECT pEffect);

    bool ApplyColorEffect(double saturation, int vibrance, int hue, bool force);

    HMODULE m_hMagLib = nullptr;
    PFN_MagInitialize m_pMagInitialize = nullptr;
    PFN_MagUninitialize m_pMagUninitialize = nullptr;
    PFN_MagSetFullscreenColorEffect m_pMagSetFullscreenColorEffect = nullptr;

    bool m_initialized = false;
    bool m_available = false;
    bool m_isIdentity = true;

    double m_lastSaturation = 1.0;
    int m_lastVibrance = 0;
    int m_lastHue = 0;
};

} // namespace prismshift

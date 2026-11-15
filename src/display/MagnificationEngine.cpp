#include "display/MagnificationEngine.h"
#include "utilities/Logger.h"
#include <cmath>
#include <numbers>

namespace prismshift {

namespace {
constexpr float REC709_LR = 0.2126f;
constexpr float REC709_LG = 0.7152f;
constexpr float REC709_LB = 0.0722f;
}

MagnificationEngine::MagnificationEngine() = default;

MagnificationEngine::~MagnificationEngine() {
    Shutdown();
}

bool MagnificationEngine::Initialize() {
    if (m_initialized) {
        return true;
    }

    m_hMagLib = LoadLibraryW(L"magnification.dll");
    if (!m_hMagLib) {
        Logger::Warn(L"MagnificationEngine: magnification.dll is not available on this system.");
        return false;
    }

    m_pMagInitialize = reinterpret_cast<PFN_MagInitialize>(GetProcAddress(m_hMagLib, "MagInitialize"));
    m_pMagUninitialize = reinterpret_cast<PFN_MagUninitialize>(GetProcAddress(m_hMagLib, "MagUninitialize"));
    m_pMagSetFullscreenColorEffect = reinterpret_cast<PFN_MagSetFullscreenColorEffect>(
        GetProcAddress(m_hMagLib, "MagSetFullscreenColorEffect"));

    if (!m_pMagInitialize || !m_pMagUninitialize || !m_pMagSetFullscreenColorEffect) {
        Logger::Warn(L"MagnificationEngine: Required magnification functions could not be resolved.");
        FreeLibrary(m_hMagLib);
        m_hMagLib = nullptr;
        return false;
    }

    if (!m_pMagInitialize()) {
        Logger::Warn(L"MagnificationEngine: MagInitialize() failed.");
        FreeLibrary(m_hMagLib);
        m_hMagLib = nullptr;
        return false;
    }

    m_initialized = true;
    m_available = true;
    m_isIdentity = true;
    Logger::Info(L"MagnificationEngine initialized successfully (DWM Color Matrix supported).");
    return true;
}

void MagnificationEngine::Shutdown() {
    if (m_initialized) {
        ResetColorEffect();
        if (m_pMagUninitialize) {
            m_pMagUninitialize();
        }
        if (m_hMagLib) {
            FreeLibrary(m_hMagLib);
            m_hMagLib = nullptr;
        }
        m_pMagInitialize = nullptr;
        m_pMagUninitialize = nullptr;
        m_pMagSetFullscreenColorEffect = nullptr;
        m_initialized = false;
        m_available = false;
        Logger::Info(L"MagnificationEngine shut down cleanly.");
    }
}

MAGCOLOREFFECT MagnificationEngine::BuildMatrix(double saturation, int vibrance, int hue) {
    MAGCOLOREFFECT effect{};
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 5; ++j) {
            effect.transform[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }

    // Check if neutral
    bool isSatNeutral = std::abs(saturation - 1.0) < 0.001;
    bool isVibNeutral = (vibrance == 0);
    bool isHueNeutral = (hue == 0);

    if (isSatNeutral && isVibNeutral && isHueNeutral) {
        return effect;
    }

    // Vibrance adjusts chroma and holds red slightly to preserve natural skin tones
    float vFactor = static_cast<float>(vibrance) / 100.0f;
    float vR = 1.0f + (vFactor * 0.65f); // Red hold
    float vG = 1.0f + vFactor;
    float vB = 1.0f + vFactor;

    float sR = static_cast<float>(saturation) * vR;
    float sG = static_cast<float>(saturation) * vG;
    float sB = static_cast<float>(saturation) * vB;

    sR = std::max(0.0f, sR);
    sG = std::max(0.0f, sG);
    sB = std::max(0.0f, sB);

    // Saturation matrix (Rec. 709 luminance weights)
    float numR = 1.0f - sR;
    float numG = 1.0f - sG;
    float numB = 1.0f - sB;

    float S[3][3] = {
        { REC709_LR * numR + sR, REC709_LR * numG,      REC709_LR * numB },
        { REC709_LG * numR,      REC709_LG * numG + sG, REC709_LG * numB },
        { REC709_LB * numR,      REC709_LB * numG,      REC709_LB * numB + sB }
    };

    if (isHueNeutral) {
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                effect.transform[r][c] = S[r][c];
            }
        }
        return effect;
    }

    // Hue rotation around luminance axis
    float rad = static_cast<float>(hue) * (std::numbers::pi_v<float> / 180.0f);
    float cosA = std::cos(rad);
    float sinA = std::sin(rad);

    float H[3][3] = {
        { REC709_LR + cosA * (1.0f - REC709_LR) - sinA * REC709_LR,
          REC709_LR - cosA * REC709_LR + sinA * 0.143f,
          REC709_LR - cosA * REC709_LR - sinA * (1.0f - REC709_LR) },

        { REC709_LG - cosA * REC709_LG - sinA * REC709_LG,
          REC709_LG + cosA * (1.0f - REC709_LG) + sinA * 0.140f,
          REC709_LG - cosA * REC709_LG + sinA * REC709_LG },

        { REC709_LB - cosA * REC709_LB + sinA * (1.0f - REC709_LB),
          REC709_LB - cosA * REC709_LB - sinA * 0.283f,
          REC709_LB + cosA * (1.0f - REC709_LB) + sinA * REC709_LB }
    };

    // Multiply S * H
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            float sum = 0.0f;
            for (int k = 0; k < 3; ++k) {
                sum += S[i][k] * H[k][j];
            }
            effect.transform[i][j] = sum;
        }
    }

    return effect;
}

bool MagnificationEngine::SetColorEffect(double saturation, int vibrance, int hue) {
    return ApplyColorEffect(saturation, vibrance, hue, false);
}

bool MagnificationEngine::ReassertColorEffect(double saturation, int vibrance, int hue) {
    // Bypass the cached-value short-circuit. Another fullscreen app or the OS
    // may have replaced the matrix without updating our cache.
    return ApplyColorEffect(saturation, vibrance, hue, true);
}

bool MagnificationEngine::ApplyColorEffect(double saturation, int vibrance, int hue, bool force) {
    if (!m_initialized || !m_pMagSetFullscreenColorEffect) {
        return false;
    }

    bool isNeutral = (std::abs(saturation - 1.0) < 0.001) && (vibrance == 0) && (hue == 0);
    if (!force) {
        if (isNeutral && m_isIdentity) {
            return true;
        }

        if (!m_isIdentity &&
            std::abs(saturation - m_lastSaturation) < 0.001 &&
            vibrance == m_lastVibrance &&
            hue == m_lastHue) {
            return true;
        }
    }

    MAGCOLOREFFECT effect = BuildMatrix(saturation, vibrance, hue);
    BOOL ok = m_pMagSetFullscreenColorEffect(&effect);
    if (!ok) {
        Logger::Warn(L"MagSetFullscreenColorEffect failed with error: " + std::to_wstring(GetLastError()));
        return false;
    }

    m_isIdentity = isNeutral;
    m_lastSaturation = saturation;
    m_lastVibrance = vibrance;
    m_lastHue = hue;
    Logger::Debug(L"Color matrix applied: saturation=" + std::to_wstring(saturation) +
                  L" vibrance=" + std::to_wstring(vibrance) + L" hue=" + std::to_wstring(hue));
    return true;
}

bool MagnificationEngine::ResetColorEffect() {
    if (!m_initialized || !m_pMagSetFullscreenColorEffect) {
        return false;
    }

    if (m_isIdentity) {
        return true;
    }

    MAGCOLOREFFECT identity{};
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 5; ++j) {
            identity.transform[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }

    BOOL ok = m_pMagSetFullscreenColorEffect(&identity);
    if (ok) {
        m_isIdentity = true;
        m_lastSaturation = 1.0;
        m_lastVibrance = 0;
        m_lastHue = 0;
    }
    return ok != FALSE;
}

} // namespace prismshift

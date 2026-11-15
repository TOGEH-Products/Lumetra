#pragma once

#include <algorithm>
#include <cmath>

namespace prismshift {

// A small animation channel.
//
// The value chases its target with exponential smoothing. The smoothing is
// expressed per second and the frame time is integrated, so a movement looks
// identical on a 60 Hz and a 144 Hz display - the previous linear
// approximation drifted with the refresh rate.
//
// The window drives these from a timer that only runs while something is
// moving, so an idle window costs nothing. Set Anim::enabled = false to
// honour "reduce motion".
struct Anim {
    inline static bool enabled = true;

    float value = 0.0f;
    float target = 0.0f;
    float speed = 18.0f; // higher = snappier

    Anim() = default;
    explicit Anim(float initial, float response = 18.0f)
        : value(initial), target(initial), speed(response) {}

    void Set(float v) { value = target = v; }
    void To(float t) { target = t; }
    void Snap() { value = target; }

    // Smoothstep: the classic gentle in-out curve.
    [[nodiscard]] float Eased() const {
        const float t = std::clamp(value, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    // Smootherstep: zero velocity at both ends, which reads as more
    // deliberate on large movements such as a panel slide.
    [[nodiscard]] float EasedSmooth() const {
        const float t = std::clamp(value, 0.0f, 1.0f);
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    [[nodiscard]] bool Settled() const {
        return std::abs(target - value) < 0.0005f;
    }

    // Returns true while the value is still moving.
    bool Advance(float dt) {
        if (!enabled) {
            if (value == target) return false;
            value = target;
            return true;
        }
        if (value == target) return false;
        const float delta = target - value;
        if (std::abs(delta) < 0.0005f) {
            value = target;
            return true;
        }
        // Frame-rate independent exponential smoothing.
        const float factor = 1.0f - std::exp(-speed * dt);
        value += delta * factor;
        if (std::abs(target - value) < 0.0005f) {
            value = target;
        }
        return true;
    }

    // 0..1 progress helper: maps a bool to its animated target.
    void Towards(bool on) { target = on ? 1.0f : 0.0f; }
};

inline float AnimLerp(float a, float b, float t) {
    return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}

inline int AnimLerpInt(int a, int b, float t) {
    return static_cast<int>(a + (b - a) * std::clamp(t, 0.0f, 1.0f) + 0.5f);
}

// A continuous 0..1..0 pulse driven by elapsed seconds. Used for the slow
// breathing of the profile orb.
inline float BreathingPhase(float seconds, float periodSeconds) {
    if (periodSeconds <= 0.0f) return 0.0f;
    const float t = std::fmod(seconds, periodSeconds) / periodSeconds;
    return 0.5f - 0.5f * std::cos(t * 6.283185307179586f);
}

} // namespace prismshift

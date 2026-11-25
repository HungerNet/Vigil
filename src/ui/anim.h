#pragma once
// Vigil UI — motion primitives. Each interaction class uses a distinct curve or spring so the
// interface communicates through motion instead of repeating one generic fade.

#include <algorithm>
#include <cmath>

namespace vigil::ui {

// Global motion level: 1 = full, ~0.35 = reduced (shorter, no travel), 0 = off.
float motionLevel();
void setMotionLevel(float m);

namespace ease {
inline float clamp01(float t) { return std::clamp(t, 0.f, 1.f); }
inline float outCubic(float t) { t = clamp01(t); const float u = 1.f - t; return 1.f - u * u * u; }
inline float inOutCubic(float t) { t = clamp01(t); return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) * 0.5f; }
inline float outExpo(float t) { t = clamp01(t); return t >= 1.f ? 1.f : 1.f - std::pow(2.f, -10.f * t); }
inline float outQuint(float t) { t = clamp01(t); const float u = 1.f - t; return 1.f - u * u * u * u * u; }
inline float outBack(float t, float s = 1.4f) { t = clamp01(t) - 1.f; return 1.f + (s + 1.f) * t * t * t + s * t * t; }
// "Emphasized" decelerate, close to cubic-bezier(0.05, 0.7, 0.1, 1).
inline float emphasized(float t) { return outQuint(t) * 0.6f + outExpo(t) * 0.4f; }
}  // namespace ease

// Frame-rate independent exponential approach.
inline float approach(float current, float target, float rate, float dt) {
    const float m = motionLevel();
    if (m <= 0.01f) return target;
    const float k = 1.f - std::exp(-rate * dt / std::max(0.35f, m));
    return current + (target - current) * k;
}

// Damped spring integrated with sub-steps for stability at low frame rates.
struct Spring {
    float x = 0.f;
    float v = 0.f;
    bool initialized = false;

    void snap(float value) { x = value; v = 0.f; initialized = true; }

    float step(float target, float dt, float stiffness = 220.f, float damping = 24.f) {
        if (!initialized) snap(target);
        const float m = motionLevel();
        if (m <= 0.01f) { snap(target); return x; }
        // Reduced motion: stiffer and critically damped (no overshoot).
        if (m < 0.99f) { stiffness *= 2.2f; damping = 2.f * std::sqrt(stiffness); }
        dt = std::min(dt, 0.1f);
        const int steps = std::max(1, static_cast<int>(std::ceil(dt / (1.f / 240.f))));
        const float h = dt / steps;
        for (int i = 0; i < steps; ++i) {
            const float a = stiffness * (target - x) - damping * v;
            v += a * h;
            x += v * h;
        }
        if (std::fabs(target - x) < 1e-4f && std::fabs(v) < 1e-3f) { x = target; v = 0.f; }
        return x;
    }
};

}  // namespace vigil::ui

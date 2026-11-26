#pragma once
// Vigil UI — motion language.
//
// Every duration, rate and spring used by the interface is defined here and nowhere else, so the
// product moves with one voice. Interactions are grouped by intent; each group has its own
// behavior on purpose (see PLAN.md §5):
//
//   hover      fast exponential approach; color and elevation only, never position
//   press      stiff, critically damped; tactile without wobble
//   toggle     under-damped spring with a small overshoot (a physical switch)
//   indicator  springs that glide selection markers between targets (rail, tabs, segmented)
//   page/tab   short emphasized-decelerate tweens that travel in the direction of navigation
//   overlay    modals and popovers rise from their anchor; leave faster than they arrive
//   toast      spring in from the edge, accelerate out
//   data       numbers count up, charts smooth toward new values
//   attention  one pulse per event, never a perpetual loop

#include "anim.h"

namespace vigil::ui::motion {

// ── Exponential approach rates (1/s). ~3/rate seconds to settle. ──────────────────────────────
inline constexpr float kHover = 26.f;      // ≈ 110 ms
inline constexpr float kSelect = 20.f;     // selected-row tint, focus rings
inline constexpr float kReveal = 14.f;     // secondary affordances (reset buttons, markers)
inline constexpr float kFade = 10.f;       // scrollbars, ambient chrome
inline constexpr float kValue = 9.f;       // live values (rings, meters)
inline constexpr float kChart = 7.f;       // chart smoothing

// ── Springs. ─────────────────────────────────────────────────────────────────────────────────
struct SpringSpec {
    float stiffness;
    float damping;
};
inline constexpr SpringSpec kPress{900.f, 60.f};       // critically damped
inline constexpr SpringSpec kToggle{560.f, 25.f};      // ~6 % overshoot
inline constexpr SpringSpec kIndicator{430.f, 36.f};   // glide, no overshoot to speak of
inline constexpr SpringSpec kGrow{380.f, 24.f};        // slider knob, swatches
inline constexpr SpringSpec kScroll{320.f, 34.f};
inline constexpr SpringSpec kToast{320.f, 27.f};
inline constexpr SpringSpec kReflow{380.f, 32.f};

// ── Tweens (seconds). ────────────────────────────────────────────────────────────────────────
inline constexpr float kMenuOpen = 0.26f;
inline constexpr float kMenuClose = 0.15f;
inline constexpr float kPage = 0.20f;
inline constexpr float kTab = 0.16f;
inline constexpr float kModalIn = 0.20f;
inline constexpr float kModalOut = 0.12f;
inline constexpr float kPopover = 0.14f;
inline constexpr float kTooltipDelay = 0.40f;
inline constexpr float kTooltipIn = 0.12f;
inline constexpr float kTooltipOut = 0.07f;
inline constexpr float kListItem = 0.18f;
inline constexpr float kStagger = 0.03f;
inline constexpr int kStaggerMax = 8;
inline constexpr float kToastOut = 0.18f;
inline constexpr float kCount = 0.60f;
inline constexpr float kPulse = 0.90f;      // one attention pulse

// ── Travel distances (dp). ───────────────────────────────────────────────────────────────────
inline constexpr float kPageTravel = 12.f;
inline constexpr float kTabTravel = 6.f;
inline constexpr float kModalTravel = 8.f;
inline constexpr float kPopoverTravel = 4.f;
inline constexpr float kListTravel = 8.f;

// Normalized progress of a tween that started at `start` (seconds) and lasts `duration`.
// Reduced motion shortens tweens; Off completes them immediately.
inline float progress(double now, double start, float duration) {
    const float m = motionLevel();
    if (m <= 0.01f) return 1.f;
    const float d = duration * (m < 0.99f ? 0.6f : 1.f);
    if (d <= 0.f) return 1.f;
    return ease::clamp01(static_cast<float>((now - start) / d));
}

// Travel scaled by the motion level (no travel when reduced/off).
inline float travel(float distance) {
    const float m = motionLevel();
    return m < 0.99f ? 0.f : distance;
}

// Entrance curve for the i-th item of a list that appeared at `start`.
inline float stagger(double now, double start, int index) {
    const int i = index < 0 ? 0 : (index > kStaggerMax ? kStaggerMax : index);
    return ease::outCubic(progress(now, start + i * kStagger, kListItem));
}

// Single attention pulse: 0 → 1 → 0 over kPulse seconds after `start`; 0 afterwards.
inline float pulse(double now, double start) {
    if (motionLevel() <= 0.01f) return 0.f;
    const float t = static_cast<float>((now - start) / kPulse);
    if (t < 0.f || t > 1.f) return 0.f;
    return std::sin(t * 3.14159265f) * (1.f - t * 0.35f);
}

}  // namespace vigil::ui::motion

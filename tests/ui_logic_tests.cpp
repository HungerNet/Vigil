// Vigil UI logic tests — the pure decisions behind the interface (no window, no OpenGL):
// typed slider values, keyboard focus order, the words used for uncertain findings, interface
// sounds and the higher-contrast option.

#include "../src/core/settings.h"
#include "../src/detection/engine.h"
#include "../src/ui/sound.h"
#include "../src/ui/suspicion.h"
#include "../src/ui/theme.h"
#include "../src/ui/widgets.h"
#include "test_framework.h"

#include <cmath>
#include <cstring>

using namespace vigil;
using namespace vigil::ui;
using vigil::test::runSuite;
using vigil::test::Test;

static void testNumberInput() {
    double v = 0.0;
    EXPECT(parseNumberInput("0.42", 0.0, 1.0, 0.0, v) && v == 0.42, "plain");
    EXPECT(parseNumberInput("0,42", 0.0, 1.0, 0.0, v) && v == 0.42, "comma decimal");
    EXPECT(parseNumberInput("  0.405 b/tick", 0.36, 0.7, 0.005, v) && std::fabs(v - 0.405) < 1e-9, "unit suffix and step");
    EXPECT(parseNumberInput("0.4031", 0.36, 0.7, 0.005, v) && std::fabs(v - 0.405) < 1e-9, "snapped to the step (%.4f)", v);
    EXPECT(parseNumberInput("75%", 10.0, 100.0, 1.0, v) && v == 75.0, "percent");
    EXPECT(parseNumberInput("1.2x", 0.8, 1.4, 0.05, v) && std::fabs(v - 1.2) < 1e-9, "multiplier");
    EXPECT(parseNumberInput("9000", 10.0, 100.0, 1.0, v) && v == 100.0, "clamped to the maximum");
    EXPECT(parseNumberInput("-5", 10.0, 100.0, 1.0, v) && v == 10.0, "clamped to the minimum");
    EXPECT(!parseNumberInput("", 0.0, 1.0, 0.0, v), "empty");
    EXPECT(!parseNumberInput("abc", 0.0, 1.0, 0.0, v), "no number");
    EXPECT(!parseNumberInput("1.2.3", 0.0, 9.0, 0.0, v), "digits after the number are not a unit");
}

static void testFocusOrder() {
    EXPECT(nextFocusIndex(-1, 5, 1) == 0, "Tab with nothing focused starts at the first control");
    EXPECT(nextFocusIndex(-1, 5, -1) == 4, "Shift+Tab with nothing focused starts at the last");
    EXPECT(nextFocusIndex(2, 5, 1) == 3 && nextFocusIndex(2, 5, -1) == 1, "steps");
    EXPECT(nextFocusIndex(4, 5, 1) == 0 && nextFocusIndex(0, 5, -1) == 4, "wraps both ways");
    EXPECT(nextFocusIndex(0, 0, 1) == -1, "nothing focusable");
    EXPECT(nextFocusIndex(9, 5, 1) == 0, "a stale index restarts at the edge");
}

static void testConcernLanguage() {
    detection::Engine e;
    e.initialize();
    e.resetSession(1000.0);
    EXPECT(categoryConcern(detection::CheckCategory::Movement) == "Unusual movement", "category phrase");
    detection::PlayerRecord& r = e.session().record("k", "k", "Someone", 1000.0);
    EXPECT(concernLabel(e, r).empty(), "nothing observed, nothing said");
    const detection::Check* speed = e.checkById("speed");
    if (!speed) { EXPECT(false, "speed check exists"); return; }
    auto& cr = r.checks[static_cast<std::size_t>(speed->index())];
    cr.evidence = 2;
    cr.model.restore(25.f);
    r.suspicion = 25.f;
    EXPECT(concernLabel(e, r) == "Unusual movement", "Low: the kind of behavior (%s)", concernLabel(e, r).c_str());
    cr.model.restore(55.f);
    r.suspicion = 55.f;
    EXPECT(concernLabel(e, r) == "Speed", "Elevated: the check (%s)", concernLabel(e, r).c_str());
}

static void testSounds() {
    for (Sound s : {Sound::Notify, Sound::Briefing, Sound::Confirm, Sound::Saved, Sound::Error}) {
        const auto wav = synthesizeSound(s, 1.f);
        EXPECT(wav.size() > 44 && std::memcmp(wav.data(), "RIFF", 4) == 0 && std::memcmp(wav.data() + 8, "WAVE", 4) == 0, "a WAV file");
        const std::size_t samples = (wav.size() - 44) / 2;
        EXPECT(samples < 44100 * 4 / 10, "under 400 ms (%zu samples)", samples);
        int peak = 0;
        for (std::size_t i = 0; i < samples; ++i) {
            const int v = static_cast<std::int16_t>(wav[44 + i * 2] | (wav[45 + i * 2] << 8));
            peak = std::max(peak, std::abs(v));
        }
        EXPECT(peak > 3000 && peak <= static_cast<int>(32767 * 0.36f), "quiet but audible at full volume (peak %d)", peak);
        const int first = static_cast<std::int16_t>(wav[44] | (wav[45] << 8));
        const int last = static_cast<std::int16_t>(wav[wav.size() - 2] | (wav[wav.size() - 1] << 8));
        EXPECT(std::abs(first) < 200 && std::abs(last) < 200, "starts and ends in silence (no click)");
    }
    const auto quiet = synthesizeSound(Sound::Notify, 0.1f);
    int peak = 0;
    for (std::size_t i = 44; i + 1 < quiet.size(); i += 2) peak = std::max(peak, std::abs(static_cast<int>(static_cast<std::int16_t>(quiet[i] | (quiet[i + 1] << 8)))));
    EXPECT(peak < 32767 * 0.05f, "volume scales the output (%d)", peak);
}

static float luminance(Color c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

static void testHigherContrast() {
    for (const Theme& base : themes()) {
        const Theme hc = withHigherContrast(base);
        const float before = std::fabs(luminance(base.textFaint) - luminance(base.surface));
        const float after = std::fabs(luminance(hc.textFaint) - luminance(hc.surface));
        EXPECT(after > before, "%s: faint text gains contrast (%.3f → %.3f)", base.name, before, after);
        EXPECT(hc.accent.r == base.accent.r && hc.verdict.g == base.verdict.g, "%s: identity colors unchanged", base.name);
    }
}

int main(int argc, char** argv) {
    setSoundOutputEnabled(false);
    const Test tests[] = {
        {"typed slider values", testNumberInput},
        {"keyboard focus order", testFocusOrder},
        {"uncertain finding language", testConcernLanguage},
        {"interface sounds", testSounds},
        {"higher contrast", testHigherContrast},
    };
    return runSuite(argc, argv, tests, "ui_logic");
}

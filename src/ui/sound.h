#pragma once
// Vigil UI — interface sounds (Settings → Accessibility → Interface sounds; off by default).
//
// Sound is used only where it confirms something that happened out of the user's line of sight
// or that changed durable state, never for hover, navigation or toggles (PLAN.md, UI audio review):
//
//   Notify     a detection notification appeared while you were playing (soft two-note chime)
//   Briefing   the match briefing appeared (one low, short note)
//   Confirm    you confirmed or exempted a player (warm, low two-note)
//   Saved      a profile was saved, exported or imported (short bright tick)
//   Error      something you asked for failed (short low dip)
//
// Tones are synthesized once (sine partials, gentle attack, exponential decay, no clipping) and
// played with PlaySound from memory, so there are no files and nothing to download. The in-game
// detection chime (Alerts → Alert sound) is separate: it plays through Minecraft's own sound engine.

#include <cstdint>
#include <vector>

namespace vigil::ui {

enum class Sound { Notify, Briefing, Confirm, Saved, Error };

// Registers the settings (call before Settings::load()).
void registerSoundSettings();
// Plays a sound if interface sounds are on. Safe to call from the render thread; never blocks.
void playSound(Sound s);
// Disables playback entirely (the preview harness and tests).
void setSoundOutputEnabled(bool on);

// A complete in-memory WAV (16-bit mono PCM, 44.1 kHz) for `s` at `volume` 0..1. Pure function,
// unit-tested: bounded peak, silence at both ends, duration under 400 ms.
std::vector<std::uint8_t> synthesizeSound(Sound s, float volume);

}  // namespace vigil::ui

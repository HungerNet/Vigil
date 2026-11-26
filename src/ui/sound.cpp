#include "sound.h"

#include "../core/settings.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>

namespace vigil::ui {
namespace {

constexpr int kRate = 44100;
bool g_output = true;
Setting* g_mode = nullptr;
Setting* g_volume = nullptr;

struct Note {
    float freq;      // Hz
    float start;     // s
    float length;    // s
    float gain;      // 0..1
};

struct Voice {
    std::vector<Note> notes;
    float decay;     // per-second exponential decay rate
};

Voice voiceOf(Sound s) {
    switch (s) {
        // E5 → B5, soft and short: something worth a glance happened.
        case Sound::Notify: return {{{659.25f, 0.f, 0.22f, 0.8f}, {987.77f, 0.085f, 0.26f, 0.65f}}, 11.f};
        // A single low G4: a card is on screen, nothing urgent.
        case Sound::Briefing: return {{{392.0f, 0.f, 0.28f, 0.7f}}, 9.f};
        // C5 → G4 descending, warm: a durable decision was recorded.
        case Sound::Confirm: return {{{523.25f, 0.f, 0.2f, 0.7f}, {392.0f, 0.09f, 0.28f, 0.7f}}, 9.5f};
        // A short bright A5 tick.
        case Sound::Saved: return {{{880.0f, 0.f, 0.12f, 0.6f}}, 22.f};
        // A short low dip D4 → A3.
        case Sound::Error: return {{{293.66f, 0.f, 0.14f, 0.7f}, {220.0f, 0.07f, 0.2f, 0.7f}}, 14.f};
    }
    return {{}, 10.f};
}

void put16(std::vector<std::uint8_t>& b, std::size_t at, std::uint16_t v) { b[at] = v & 0xFF; b[at + 1] = v >> 8; }
void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
}

}  // namespace

void registerSoundSettings() {
    auto& S = Settings::instance();
    g_mode = &S.addChoice("ui.sounds", "Interface sounds", 0, {"Off", "On"},
                          "Short, quiet sounds when something happens out of view: a detection notification, the match "
                          "briefing, a confirmation or exemption you made, a saved or imported profile, or an error. "
                          "Never for clicks or navigation.");
    g_mode->group = "Accessibility";
    g_volume = &S.addFloat("ui.soundVolume", "Sound volume", 0.5, {0.05, 1.0, 0.05, 2, ""},
                           "Loudness of interface sounds, relative to your system volume.");
    g_volume->group = "Accessibility";
    g_volume->warning = "Interface sounds are off; this volume applies when they are on.";
    g_volume->warnWhen = [](const Setting&) { return g_mode && g_mode->i() == 0; };
}

void setSoundOutputEnabled(bool on) { g_output = on; }

std::vector<std::uint8_t> synthesizeSound(Sound s, float volume) {
    const Voice v = voiceOf(s);
    float duration = 0.f;
    for (const Note& n : v.notes) duration = std::max(duration, n.start + n.length);
    duration += 0.01f;   // trailing silence
    const int frames = static_cast<int>(duration * kRate);
    std::vector<float> mix(static_cast<std::size_t>(frames), 0.f);
    for (const Note& n : v.notes) {
        const int a = static_cast<int>(n.start * kRate);
        const int len = static_cast<int>(n.length * kRate);
        for (int i = 0; i < len && a + i < frames; ++i) {
            const float t = static_cast<float>(i) / kRate;
            // 6 ms attack (no click), exponential decay, 10 ms release at the end of the note.
            const float attack = std::min(1.f, t / 0.006f);
            const float release = std::min(1.f, (n.length - t) / 0.010f);
            const float env = attack * std::exp(-v.decay * t) * std::max(0.f, release);
            const float w = 2.f * 3.14159265f * n.freq * t;
            // Fundamental with a quiet octave and twelfth: rounder than a pure sine, never harsh.
            const float tone = std::sin(w) + 0.18f * std::sin(2.f * w) + 0.06f * std::sin(3.f * w);
            mix[static_cast<std::size_t>(a + i)] += n.gain * env * tone;
        }
    }
    float peak = 1e-6f;
    for (float x : mix) peak = std::max(peak, std::fabs(x));
    // Normalized to a modest ceiling: the loudest interface sound peaks at 35 % of full scale.
    const float scale = 0.35f * std::clamp(volume, 0.f, 1.f) / peak;
    const std::size_t dataBytes = static_cast<std::size_t>(frames) * 2;
    std::vector<std::uint8_t> wav(44 + dataBytes);
    std::memcpy(wav.data(), "RIFF", 4);
    put32(wav, 4, static_cast<std::uint32_t>(36 + dataBytes));
    std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    put32(wav, 16, 16);
    put16(wav, 20, 1);          // PCM
    put16(wav, 22, 1);          // mono
    put32(wav, 24, kRate);
    put32(wav, 28, kRate * 2);
    put16(wav, 32, 2);
    put16(wav, 34, 16);
    std::memcpy(wav.data() + 36, "data", 4);
    put32(wav, 40, static_cast<std::uint32_t>(dataBytes));
    for (int i = 0; i < frames; ++i) {
        const int sample = static_cast<int>(std::lround(std::clamp(mix[static_cast<std::size_t>(i)] * scale, -1.f, 1.f) * 32767.f));
        put16(wav, 44 + static_cast<std::size_t>(i) * 2, static_cast<std::uint16_t>(static_cast<std::int16_t>(sample)));
    }
    return wav;
}

void playSound(Sound s) {
    if (!g_output || !g_mode || g_mode->i() == 0) return;
    // PlaySound with SND_MEMORY needs the buffer alive while it plays: one buffer per sound,
    // rebuilt only when the volume changes.
    static std::mutex mu;
    static std::array<std::vector<std::uint8_t>, 5> cache;
    static std::array<float, 5> cachedVolume{-1.f, -1.f, -1.f, -1.f, -1.f};
    std::lock_guard<std::mutex> lk(mu);
    const std::size_t i = static_cast<std::size_t>(s);
    const float vol = g_volume ? g_volume->f() : 0.5f;
    if (cachedVolume[i] != vol) {
        PlaySoundW(nullptr, nullptr, 0);   // stop anything that may still read the old buffer
        cache[i] = synthesizeSound(s, vol);
        cachedVolume[i] = vol;
    }
    PlaySoundW(reinterpret_cast<LPCWSTR>(cache[i].data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

}  // namespace vigil::ui

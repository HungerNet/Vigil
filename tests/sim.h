#pragma once
// Vigil test simulator — a minimal vanilla 1.8.9 movement model and a World that feeds the real
// Engine with WorldFrames exactly as the game adapter does. Shared by every engine-level suite.

#include "../src/detection/engine.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace vigil::test {
using namespace vigil::detection;

// ── Minimal vanilla movement simulator ───────────────────────────────────────────────────────

struct Sim {
    PlayerFrame f;
    Vec3 motion;
    double groundY = 64.0;
    double ceilingY = 0.0;     // bottom of a ceiling block (0 = open sky)
    double moveYaw = 0.0;      // movement direction (degrees, MC convention)
    int swingTicks = -1;

    Sim(int id, const std::string& name) {
        f.entityId = id;
        f.name = name;
        f.uuid = "00000000-0000-4000-8000-" + std::to_string(100000000000LL + id);
        f.pos = {0.0, groundY, 0.0};
        f.onGround = true;
        f.serverPosition = true;
        f.potionsKnown = true;
        f.blockBelow = 2;
        f.blockAboveHead = 0;
        f.heldItem = 276;
        f.health = 20.f;
    }

    // One tick of vanilla physics. `input` = forward input (0..1); `jump` jumps if on ground.
    void step(double input, bool jump, bool sprint = true, double airControl = mc::kAirControlSprint) {
        const double rad = moveYaw * 3.14159265358979323846 / 180.0;
        const Vec3 fwd{-std::sin(rad), 0.0, std::cos(rad)};
        f.sprinting = sprint && input > 0.0;
        if (f.onGround) {
            if (jump) {
                motion.y = mc::kJumpVelocity;
                if (f.sprinting) motion = motion + fwd * 0.2;
            }
            const double accel = (f.sprinting ? 0.13 : 0.1) * input;
            motion = motion + fwd * accel;
            f.pos = f.pos + Vec3{motion.x, 0.0, motion.z};
            motion.x *= 0.546;
            motion.z *= 0.546;
        } else {
            motion = motion + fwd * (airControl * input);
            f.pos = f.pos + Vec3{motion.x, 0.0, motion.z};
            motion.x *= mc::kAirFriction;
            motion.z *= mc::kAirFriction;
        }
        f.pos.y += motion.y;
        if (ceilingY > 0.0 && f.pos.y + mc::kPlayerHeight > ceilingY) {
            f.pos.y = ceilingY - mc::kPlayerHeight;   // head hits the ceiling
            motion.y = 0.0;
        }
        if (f.pos.y <= groundY) {
            f.pos.y = groundY;
            motion.y = 0.0;
            f.onGround = true;
        } else {
            f.onGround = false;
            motion.y = (motion.y - 0.08) * 0.98;
        }
        tickAnimations();
    }

    void tickAnimations() {
        if (swingTicks >= 0) {
            ++swingTicks;
            f.swingProgress = swingTicks;
            if (swingTicks >= mc::kSwingAnimationTicks) {
                swingTicks = -1;
                f.swingInProgress = false;
                f.swingProgress = 0;
            }
        }
        if (f.hurtTime > 0) --f.hurtTime;
    }

    void swing() {
        swingTicks = 0;
        f.swingInProgress = true;
        f.swingProgress = 0;
    }
    void hurt() { f.hurtTime = mc::kHurtTimeMax; }
    void lookAt(const Sim& o) {
        const Vec3 eye{f.pos.x, f.pos.y + mc::kEyeHeight, f.pos.z};
        const Vec3 c{o.f.pos.x, o.f.pos.y + 0.9, o.f.pos.z};
        f.yaw = static_cast<float>(yawTowards(eye, c));
        f.pitch = static_cast<float>(pitchTowards(eye, c));
    }

    PlayerFrame frame() const {
        PlayerFrame out = f;
        // Server positions are relayed at 1/32 block precision.
        out.pos.x = std::round(f.pos.x * 32.0) / 32.0;
        out.pos.y = std::round(f.pos.y * 32.0) / 32.0;
        out.pos.z = std::round(f.pos.z * 32.0) / 32.0;
        // Rotations are relayed at 360/256 precision.
        out.yaw = static_cast<float>(std::round(f.yaw / mc::kRotationQuantum) * mc::kRotationQuantum);
        out.pitch = static_cast<float>(std::round(f.pitch / mc::kRotationQuantum) * mc::kRotationQuantum);
        return out;
    }

    // Server relay: positions (and the ground flag that travels with them) reach the observer
    // `delay` ticks late, every `cadence` ticks, and not at all while `freeze` is set (lag); the
    // next update after a freeze is a catch-up.
    int delay = 0;
    int cadence = 1;
    bool freeze = false;
    std::deque<std::pair<Vec3, bool>> past;
    std::pair<Vec3, bool> shown{{}, true};
    long relayCount = 0;
    PlayerFrame relayFrame() {
        PlayerFrame out = frame();
        if (delay == 0 && cadence == 1 && !freeze && past.empty()) return out;
        past.push_back({f.pos, f.onGround});
        while (static_cast<int>(past.size()) > delay + 1) past.pop_front();
        if (relayCount++ == 0 || (!freeze && relayCount % cadence == 0)) shown = past.front();
        out.pos = {std::round(shown.first.x * 32.0) / 32.0, std::round(shown.first.y * 32.0) / 32.0,
                   std::round(shown.first.z * 32.0) / 32.0};
        out.onGround = shown.second;
        return out;
    }
};

struct World {
    Engine engine;
    std::vector<Sim*> sims;
    Tick tick = 1000;
    GameContext context = GameContext::InGame;
    std::vector<std::string> chat;      // delivered with the next frame
    bool worldChanged = false;          // delivered with the next frame
    std::vector<Sim*> hidden;           // out of view this frame
    std::vector<WorldFrame>* recorded = nullptr;   // when set, every frame is also appended here

    World() {
        engine.initialize();
        engine.resetSession(tick * 50.0);
    }
    void add(Sim& s) { sims.push_back(&s); }
    void frame() {
        WorldFrame w;
        w.tick = ++tick;
        w.timeMs = tick * 50.0;
        w.context = context;
        w.chat.swap(chat);
        w.worldChanged = worldChanged;
        worldChanged = false;
        for (Sim* s : sims) {
            if (std::find(hidden.begin(), hidden.end(), s) != hidden.end()) continue;
            w.players.push_back(s->relayFrame());
        }
        if (recorded) recorded->push_back(w);
        engine.onFrame(w);
    }
    const PlayerRecord* rec(const Sim& s) const { return engine.session().find(s.f.uuid); }
    int detections(const Sim& s, const char* checkId) const {
        const PlayerRecord* r = rec(s);
        const Check* c = engine.checkById(checkId);
        if (!r || !c) return 0;
        return r->checks[static_cast<std::size_t>(c->index())].detections;
    }
    int evidence(const Sim& s, const char* checkId) const {
        const PlayerRecord* r = rec(s);
        const Check* c = engine.checkById(checkId);
        if (!r || !c) return 0;
        return r->checks[static_cast<std::size_t>(c->index())].evidence;
    }
    int indicatorCount(const Sim& s, const char* checkId, const char* indicator) const {
        const PlayerRecord* r = rec(s);
        const Check* c = engine.checkById(checkId);
        if (!r || !c) return 0;
        const auto& cr = r->checks[static_cast<std::size_t>(c->index())];
        for (std::size_t i = 0; i < c->indicators().size(); ++i) {
            if (c->indicators()[i].id == indicator) return i < cr.indicatorCounts.size() ? cr.indicatorCounts[i] : 0;
        }
        return 0;
    }
    int totalDetections(const Sim& s) const {
        const PlayerRecord* r = rec(s);
        return r ? static_cast<int>(r->detections.size()) : 0;
    }
};

// Places `s` on a far-away observer position so the local player exists.
inline void setupLocal(Sim& local) {
    local.f.isLocal = true;
    local.f.pos = {200.0, 64.0, 200.0};
}

}  // namespace vigil::test

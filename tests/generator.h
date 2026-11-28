#pragma once
// Vigil generated scenarios (RUST_PLAN P3, P3A_PLAN.md §3) — random sessions for differential
// testing of two engine implementations. Each scenario is a pure function of (seed, index): a frame
// sequence built with the simulator (tests/sim.h), the configuration snapshots the engine receives
// and an optional marks file. Nothing here depends on the engine that later replays the frames.
//
// A scenario mixes, per player and re-drawn every few seconds: legitimate movement, low hops,
// ground glides and bursts, air strafes and acceleration, item use (legit and NoSlow), block-hitting,
// machine / human / jittered clicking, flat / Keep-Y / eagle / legitimate bridging, duels (reach,
// aim, knockback), multi-targeting and robotic tracking; plus potions, movement attributes,
// surfaces, liquids, ladders, cobwebs, riding, ceilings, walls, burning, death, invisibility,
// knockback launches, teleports, relay delay and cadence, lag freezes, frame drops, tick gaps;
// identities (invalid names, NPC UUIDs, missing UUIDs, duplicate entities, name collisions,
// unlisted entities, entity id reuse, players leaving and returning); phases (lobby, queue, match,
// replay, scoreboard flicker, world changes, game-end chat); marks; and random configurations
// changed mid-session.

#include "scenarios.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace vigil::test {

struct GeneratedScenario {
    std::string name;
    std::vector<WorldFrame> frames;
    // Configuration snapshots and the frame before which each reaches the engine (-1 = before the
    // session starts).
    std::vector<std::pair<long long, EngineConfig>> configs;
    std::string marks;   // history/marks.json contents ("" = no marks)
};

namespace gen {

enum Behavior : int {
    kIdle, kWalk, kSprint, kLowHop, kGlide, kStrafe, kBoost, kEatLegit, kEatCheat, kBlockHit, kMachineClick, kHumanClick,
    kJitterClick, kFlatBridge, kKeepY, kEagle, kSlowBridge, kDuelAttacker, kDuelVictim, kTracker, kCircled, kBowUser,
    kMultiAura, kSpam, kBehaviorCount
};

inline bool isClicker(int b) { return b == kMachineClick || b == kHumanClick || b == kJitterClick || b == kSpam; }

struct Actor {
    Sim sim{0, ""};
    int behavior = kIdle;
    int until = 0;          // frame at which the behavior is re-drawn
    int partner = -1;       // duel / tracking partner (index)
    int phase = 0;          // per-behavior counter
    int clickEvery = 4;
    double speed = 0.5;
    double duelDistance = 2.6;
    bool aimAway = false, antiKb = false, legitKb = true;
    int hurtDelay = 1;
    bool hidden = false;
    int hiddenUntil = -1;
    bool gone = false;
    int unlistedUntil = -1;
    bool neverListed = false;
    double angle = 0.0;
    int second = -1;        // multi-aura: the second victim
    // Rotations as a client sees them between packets (interpolated, not on the 360/256 grid).
    // Differences of grid values are exact in float; these are not, so they exercise every
    // float/double promotion of rotation arithmetic (P3A falsification F18).
    bool rawRotation = false;
};

inline std::string uuidFor(int n, int version) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "00000000-0000-%d000-8000-%012d", version, n);
    return buf;
}

class Generator {
public:
    Generator(std::uint64_t seed, int index, std::shared_ptr<const ConfigSchema> schema)
        : rng_(seed * 0x9E3779B97F4A7C15ull + static_cast<std::uint64_t>(index) * 0xD1B54A32D192ED03ull + 1), schema_(std::move(schema)) {
        char name[32];
        std::snprintf(name, sizeof name, "gen-%05d", index);
        out_.name = name;
        index_ = index;
    }

    GeneratedScenario run() {
        const int kind = uni(0, 199);
        if (kind == 0) churn();
        else if (kind <= 4) session(uni(7000, 8200), uni(2, 4));   // marathon: history compaction, caps
        else session(uni(150, 650), uni(2, 9));
        configs();
        return std::move(out_);
    }

private:
    std::mt19937_64 rng_;
    std::shared_ptr<const ConfigSchema> schema_;
    GeneratedScenario out_;
    int index_ = 0;
    std::vector<Actor> actors_;
    Tick tick_ = 1000;
    double jitter_ = 0.0;
    GameContext context_ = GameContext::InGame;
    std::vector<std::string> chat_;
    bool worldChanged_ = false;
    int frame_ = 0;
    int stallLeft_ = 0;

    int uni(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng_); }
    double real(double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng_); }
    bool chance(double p) { return std::uniform_real_distribution<double>(0.0, 1.0)(rng_) < p; }

    const char* pick(std::initializer_list<const char*> v) { return *(v.begin() + uni(0, static_cast<int>(v.size()) - 1)); }

    std::string playerName(int i) {
        static const char* pool[] = {"Kestrel", "Vale", "Orrin", "Lumen", "Brisk", "Tamsin", "Quill", "Harrow", "Sable", "Ember",
                                     "Corvid", "Juniper", "Wren", "Talon", "Mire", "Falk", "Odessa", "Pike", "Rook", "Sorrel"};
        std::string n = pool[(index_ * 7 + i * 3) % 20];
        if (chance(0.5)) n += std::to_string(uni(1, 99));
        return n;
    }

    // ── Identity ──────────────────────────────────────────────────────────────────────────────
    void makeActors(int n, bool local) {
        actors_.clear();
        actors_.reserve(static_cast<std::size_t>(n) + 1);
        const int ping = chance(0.3) ? -1 : uni(20, 300);
        for (int i = 0; i <= n; ++i) {
            if (i == 0 && !local) continue;
            Actor a;
            const int id = chance(0.1) ? uni(1000, 2000000) : i + 1;
            a.sim = Sim(chance(0.02) ? -id : id, i == 0 ? "Observer" : playerName(i));
            Sim& s = a.sim;
            s.f.uuid = uuidFor(index_ * 100 + i, 4);
            if (i == 0) {
                s.f.isLocal = true;
                s.f.pos = {real(-50, 50), 64, real(-50, 50)};
            } else {
                s.f.pos = {i * 40.0 + real(-5, 5), 64, real(-5, 5)};
                const double r = real(0, 1);
                if (r < 0.04) s.f.uuid = uuidFor(index_ * 100 + i, 2);        // server NPC
                else if (r < 0.08) s.f.uuid.clear();                            // unknown UUID
                else if (r < 0.12) s.f.uuid = uuidFor(index_ * 100 + i, 1);   // nick
                if (chance(0.04)) s.f.name = std::string("[NPC] ") + s.f.name;
                else if (chance(0.03)) s.f.name = "#" + std::to_string(id);
                if (chance(0.15)) s.f.isTeammate = true;
                if (chance(0.03)) s.f.invisible = true;
            }
            a.rawRotation = chance(0.35);
            s.f.tabKnown = chance(0.85);
            s.f.tabListed = true;
            if (i > 0 && chance(0.06)) a.neverListed = true;
            else if (i > 0 && chance(0.08)) a.unlistedUntil = uni(10, 90);
            s.f.pingMs = chance(0.2) ? uni(20, 400) : ping;
            s.delay = chance(0.5) ? 0 : uni(1, 3);
            s.cadence = chance(0.6) ? 1 : uni(2, 3);
            s.f.potionsKnown = chance(0.85);
            s.f.potions.speed = chance(0.12) ? uni(0, 1) : -1;
            s.f.potions.jumpBoost = chance(0.05) ? 0 : -1;
            s.f.potions.count = (s.f.potions.speed >= 0) + (s.f.potions.jumpBoost >= 0);
            s.f.potionParticles = s.f.potions.count > 0 || chance(0.05);
            static const double kAttributes[] = {0.1, 0.13, 0.12, 0.15, 0.2};
            if (chance(0.5)) s.f.moveSpeedAttribute = kAttributes[uni(0, 4)];
            a.until = 0;
            actors_.push_back(a);
        }
        // Shared identities: a duplicate entity for one player, a name collision, the local name.
        const int m = static_cast<int>(actors_.size());
        if (m >= 3 && chance(0.06)) {
            Actor copy = actors_[static_cast<std::size_t>(uni(1, m - 1))];
            copy.sim.f.entityId += 500;
            copy.sim.f.pos.x += 0.5;
            actors_.push_back(copy);
        }
        if (m >= 3 && chance(0.06)) {
            Actor& a = actors_[static_cast<std::size_t>(uni(1, m - 1))];
            Actor& b = actors_[static_cast<std::size_t>(uni(1, m - 1))];
            if (&a != &b) {
                b.sim.f.name = a.sim.f.name;
                if (chance(0.5)) b.neverListed = true;
            }
        }
        if (local && m >= 2 && chance(0.03)) {
            std::string upper = actors_[0].sim.f.name;
            for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            actors_[1].sim.f.name = upper;
        }
    }

    // ── Behaviors ─────────────────────────────────────────────────────────────────────────────
    int partnerFor(int self) {
        const int m = static_cast<int>(actors_.size());
        if (m < 2) return -1;
        for (int tries = 0; tries < 6; ++tries) {
            const int p = uni(0, m - 1);
            if (p != self && actors_[static_cast<std::size_t>(p)].behavior != kDuelAttacker) return p;
        }
        return -1;
    }

    void draw(int i) {
        Actor& a = actors_[static_cast<std::size_t>(i)];
        a.until = frame_ + uni(60, 300);
        a.phase = 0;
        a.behavior = uni(0, kBehaviorCount - 1);
        if (a.behavior == kDuelVictim || a.behavior == kCircled) a.behavior = kSprint;
        a.speed = real(0.3, 0.7);
        a.clickEvery = uni(2, 7);
        a.sim.moveYaw = real(-180, 180);
        a.sim.f.heldItem = 276;
        a.sim.f.usingItem = a.sim.f.blocking = false;
        a.sim.f.sneaking = false;
        a.sim.f.pitch = static_cast<float>(real(-20, 30));
        a.sim.groundY = std::floor(a.sim.f.pos.y);
        if (isClicker(a.behavior) || a.behavior == kMultiAura) {
            // Clicking is judged in combat only: stay next to someone.
            a.partner = partnerFor(i);
            if (a.behavior == kMultiAura) a.second = partnerFor(i);
            if (a.behavior == kMultiAura && (a.second < 0 || a.second == a.partner)) a.behavior = kMachineClick;
            if (a.partner < 0) a.behavior = kWalk;   // nobody to stand next to
            a.clickEvery = a.behavior == kMultiAura ? uni(1, 4) : uni(2, 7);
        }
        if (a.behavior == kDuelAttacker || a.behavior == kTracker) {
            a.partner = partnerFor(i);
            if (a.partner < 0) {
                a.behavior = kWalk;
            } else {
                Actor& p = actors_[static_cast<std::size_t>(a.partner)];
                p.behavior = a.behavior == kDuelAttacker ? kDuelVictim : kCircled;
                p.until = a.until;
                p.partner = i;
                a.duelDistance = chance(0.3) ? real(3.2, 5.6) : real(1.8, 3.0);
                a.aimAway = chance(0.25);
                a.antiKb = chance(0.25);
                a.legitKb = chance(0.7);
                a.hurtDelay = uni(0, 3);
                a.clickEvery = uni(9, 14);
            }
        }
        if (a.behavior == kFlatBridge || a.behavior == kKeepY || a.behavior == kEagle || a.behavior == kSlowBridge) {
            a.sim.f.heldItem = 1;
            a.sim.f.pitch = static_cast<float>(real(60, 85));
        }
        if (a.behavior == kEatLegit || a.behavior == kEatCheat) a.sim.f.heldItem = chance(0.7) ? 322 : 373;
        if (a.behavior == kBowUser) a.sim.f.heldItem = 261;
    }

    void act(int i) {
        Actor& a = actors_[static_cast<std::size_t>(i)];
        Sim& s = a.sim;
        const int k = a.phase++;
        switch (a.behavior) {
            case kIdle:
                s.f.yaw = static_cast<float>(s.f.yaw + real(-3, 3));
                s.tickAnimations();
                break;
            case kWalk:
                s.moveYaw += real(-8, 8);
                s.f.yaw = static_cast<float>(s.moveYaw);
                s.step(a.speed, false, false);
                break;
            case kSprint:
                s.moveYaw += real(-6, 6);
                s.f.yaw = static_cast<float>(s.moveYaw);
                s.step(1.0, chance(0.9));
                break;
            case kLowHop: corpus::lowHop(s, a.speed + 0.1); break;
            case kGlide: {
                const double rad = s.moveYaw * 3.14159265358979323846 / 180.0;
                const double v = (k % 150) >= 140 ? 1.6 : a.speed;
                s.f.pos = s.f.pos + Vec3{-std::sin(rad) * v, 0.0, std::cos(rad) * v};
                s.f.onGround = true;
                s.f.sprinting = true;
                s.tickAnimations();
                break;
            }
            case kStrafe:
            case kBoost:
                if (s.f.onGround) {
                    s.step(1.0, k % 10 >= 8);
                } else {
                    if (a.behavior == kStrafe) {
                        const double sp = std::max(0.30, s.motion.horizontalLength());
                        s.moveYaw += 9.0;
                        const double rad = s.moveYaw * 3.14159265358979323846 / 180.0;
                        s.motion.x = -std::sin(rad) * sp;
                        s.motion.z = std::cos(rad) * sp;
                    } else {
                        s.motion.z += 0.06;
                    }
                    s.step(0.0, false);
                }
                break;
            case kEatLegit:
            case kEatCheat: {
                const bool eating = (k % 60) > 10;
                s.f.usingItem = eating;
                if (a.behavior == kEatCheat) s.step(1.0, false, true);
                else s.step(eating ? 0.2 : 1.0, false, !eating);
                break;
            }
            case kBlockHit:
                s.f.heldItem = 276;
                s.f.blocking = s.f.usingItem = (k % 100) < 90;
                if (k % a.clickEvery == 0) s.swing();
                s.tickAnimations();
                break;
            case kMachineClick:
            case kHumanClick:
            case kJitterClick: {
                bool sw = false;
                if (a.behavior == kMachineClick) {
                    sw = k % a.clickEvery == 0;
                } else if (a.behavior == kJitterClick) {
                    // Nearly constant timing that never runs 20 identical intervals: 18 equal
                    // intervals, then one a tick longer (timing consistency without a streak).
                    if (k == 0 || k >= a.angle) {
                        sw = true;
                        a.second = a.second + 1;
                        a.angle = k + a.clickEvery + (a.second % 19 == 0 ? 1 : 0);
                    }
                } else {
                    sw = chance(1.0 / a.clickEvery);
                }
                if (sw) s.swing();
                follow(a);
                s.tickAnimations();
                break;
            }
            case kSpam:
                // A new swing on 20 of every 21 ticks (the animation restarts): beyond the rate ceiling.
                s.f.heldItem = 276;
                s.f.swingInProgress = true;
                s.f.swingProgress = 20 - (k % 21);
                follow(a);
                break;
            case kMultiAura: multiAura(a, k); break;
            case kFlatBridge:
                if (k % uni(5, 8) == 0) s.swing();
                s.step(1.0, false, true);
                break;
            case kKeepY:
                if (!s.f.onGround && (k % 4 == 1)) s.swing();
                s.step(1.0, true, true);
                break;
            case kEagle:
                s.moveYaw = 180.0;
                s.f.sneaking = (k % 8) == 0;
                if (k % 8 == 0) s.swing();
                s.step(0.4, false, false);
                break;
            case kSlowBridge:
                s.moveYaw = 180.0;
                s.f.sneaking = (k % 13) < 6;
                if (k % 13 == 0) s.swing();
                s.step(0.3, false, false);
                break;
            case kDuelAttacker: duel(a, k); break;
            case kTracker: track(a, k); break;
            case kBowUser:
                if (k % 20 == 0) s.swing();
                s.step(0.5, false, false);
                break;
            case kDuelVictim:
            case kCircled:
                break;   // moved by the partner
            default: s.tickAnimations(); break;
        }
    }

    void duel(Actor& a, int k) {
        Actor& v = actors_[static_cast<std::size_t>(a.partner)];
        Sim& att = a.sim;
        Sim& vic = v.sim;
        const int t = k % a.clickEvery;
        if (t == 0) {
            vic.f.pos = {att.f.pos.x, vic.groundY, att.f.pos.z + a.duelDistance};
            vic.f.onGround = true;
            vic.motion = {};
        }
        att.lookAt(vic);
        if (a.aimAway) att.f.yaw += 180.f;
        if (t == 0) att.swing();
        if (t == a.hurtDelay) {
            vic.hurt();
            if (!a.antiKb && a.legitKb) {
                vic.motion.y = 0.3608;
                vic.motion.z += 0.4;
                vic.f.onGround = false;
                vic.f.pos.y += 0.0001;
            }
        }
        att.tickAnimations();
        if (!vic.f.onGround || vic.motion.horizontalLength() > 0) {
            vic.f.pos = vic.f.pos + vic.motion;
            vic.motion.x *= vic.f.onGround ? 0.546 : 0.91;
            vic.motion.z *= vic.f.onGround ? 0.546 : 0.91;
            if (vic.f.pos.y <= vic.groundY) {
                vic.f.pos.y = vic.groundY;
                vic.motion.y = 0;
                vic.f.onGround = true;
            } else {
                vic.f.onGround = false;
                vic.motion.y = (vic.motion.y - 0.08) * 0.98;
            }
        }
        if (vic.f.hurtTime > 0) --vic.f.hurtTime;
        vic.f.pos.x += (k % 2 == 0 ? 0.04 : -0.04);
    }

    // Stays within reach of the partner.
    void follow(Actor& a) {
        if (a.partner < 0 || a.partner >= static_cast<int>(actors_.size())) return;
        const Sim& p = actors_[static_cast<std::size_t>(a.partner)].sim;
        a.sim.f.pos = {p.f.pos.x + 1.8, p.f.pos.y, p.f.pos.z + 1.2};
        a.sim.f.onGround = true;
        a.sim.lookAt(p);
    }

    // Two victims on opposite sides, hit a few ticks apart (KillAura multi-target and snaps).
    void multiAura(Actor& a, int k) {
        Sim& v1 = actors_[static_cast<std::size_t>(a.partner)].sim;
        Sim& v2 = actors_[static_cast<std::size_t>(a.second)].sim;
        const int t = k % 12;
        if (t == 0) {
            v1.f.pos = {a.sim.f.pos.x - 2.2, v1.groundY, a.sim.f.pos.z + 0.5};
            v2.f.pos = {a.sim.f.pos.x + 2.2, v2.groundY, a.sim.f.pos.z + 0.5};
            v1.f.onGround = v2.f.onGround = true;
        }
        a.sim.lookAt(t < a.clickEvery ? v1 : v2);
        if (t == 0 || t == a.clickEvery) a.sim.swing();
        if (t == 1) v1.hurt();
        if (t == a.clickEvery + 1) v2.hurt();
        a.sim.tickAnimations();
        v1.f.pos.z += (t % 2 ? 0.03 : -0.03);
        v2.f.pos.z += (t % 2 ? -0.03 : 0.03);
    }

    void track(Actor& a, int k) {
        Actor& c = actors_[static_cast<std::size_t>(a.partner)];
        c.angle += a.duelDistance > 3.0 ? 7.0 : 5.0;
        const double rad = c.angle * 3.14159265358979323846 / 180.0;
        c.sim.f.pos = {a.sim.f.pos.x + 3.0 * std::sin(rad), c.sim.groundY, a.sim.f.pos.z + 3.0 * std::cos(rad)};
        a.sim.lookAt(c.sim);
        if (k % 12 == 0) a.sim.swing();
        if (k % 12 == 1) c.sim.hurt();
        a.sim.tickAnimations();
        if (c.sim.f.hurtTime > 0) --c.sim.f.hurtTime;
    }

    // Random situations layered over the behavior.
    void events(Actor& a) {
        Sim& s = a.sim;
        s.f.blockBelow = 2;
        if (chance(0.004)) { s.hurt(); s.f.pos = s.f.pos + Vec3{real(-2, 2), 0.2, real(-2, 2)}; s.f.wallMask = static_cast<std::uint8_t>(uni(0, 255)); s.f.wallNear = s.f.wallMask != 0; }
        if (chance(0.002)) s.f.pos = s.f.pos + Vec3{real(-15, 15), 0, real(-15, 15)};   // teleport
        if (chance(0.01)) s.freeze = !s.freeze;
        if (s.freeze && chance(0.15)) s.freeze = false;
        auto window = [&](bool& flag, double start, double stop) {
            if (!flag && chance(start)) flag = true;
            else if (flag && chance(stop)) flag = false;
        };
        window(s.f.inWater, 0.002, 0.08);
        window(s.f.onLadder, 0.002, 0.15);
        window(s.f.inWeb, 0.001, 0.2);
        window(s.f.riding, 0.001, 0.05);
        window(s.f.burning, 0.002, 0.1);
        window(s.f.sneaking, a.behavior == kEagle || a.behavior == kSlowBridge ? 0.0 : 0.003, 0.2);
        static const int kSurfaces[] = {79, 174, 165, 88, 9};   // ice, packed ice, slime, soul sand, water
        if (chance(0.003)) s.f.blockBelow = kSurfaces[uni(0, 4)];
        if (chance(0.002)) { s.ceilingY = s.groundY + 2.0; s.f.blockAboveHead = 1; }
        else if (s.ceilingY > 0.0 && chance(0.05)) { s.ceilingY = 0.0; s.f.blockAboveHead = 0; }
        if (chance(0.0015)) { s.f.dead = true; s.f.health = 0.f; }
        else if (s.f.dead && chance(0.2)) { s.f.dead = false; s.f.health = 20.f; }
        if (s.f.potionsKnown && chance(0.002)) s.f.potions.speed = s.f.potions.speed >= 0 ? -1 : uni(0, 2);
        s.f.sprintModifier = s.f.sprinting && s.f.moveSpeedAttribute > 0.0;
    }

    void visibility(Actor& a) {
        if (a.gone) return;
        if (a.hidden && frame_ >= a.hiddenUntil) a.hidden = false;
        if (!a.hidden && !a.sim.f.isLocal && chance(0.002)) {
            a.hidden = true;
            a.hiddenUntil = frame_ + (chance(0.6) ? uni(5, 35) : uni(41, 90));
        }
        if (!a.sim.f.isLocal && chance(0.0004)) a.gone = true;
        // Entity id reuse: the same entity id now carries another player.
        if (!a.sim.f.isLocal && chance(0.0008)) {
            a.sim.f.uuid = uuidFor(index_ * 100 + 50 + uni(0, 49), 4);
            a.sim.f.name = playerName(uni(20, 40));
        }
        a.sim.f.tabListed = !a.neverListed && frame_ >= a.unlistedUntil;
    }

    void emitFrame() {
        WorldFrame w;
        w.tick = ++tick_;
        if (chance(0.004)) tick_ += uni(1, 4);   // tick gap
        w.timeMs = tick_ * 50.0 + (jitter_ != 0.0 ? real(-jitter_, jitter_) : 0.0);
        w.skippedTicks = chance(0.01) ? uni(1, 8) : 0;
        w.context = context_;
        w.contextDetail = context_ == GameContext::InGame ? "Bed Wars" : "";
        w.chat.swap(chat_);
        w.worldChanged = worldChanged_;
        worldChanged_ = false;
        for (Actor& a : actors_) {
            if (a.hidden || a.gone) continue;
            PlayerFrame pf = a.sim.relayFrame();
            if (a.rawRotation) {
                pf.yaw = static_cast<float>(a.sim.f.yaw + real(-0.7, 0.7));
                pf.pitch = static_cast<float>(std::clamp(a.sim.f.pitch + real(-0.7, 0.7), -90.0, 90.0));
            }
            w.players.push_back(pf);
        }
        out_.frames.push_back(std::move(w));
    }

    void session(int frames, int players) {
        makeActors(players, chance(0.85));
        jitter_ = chance(0.3) ? real(0.5, 4.0) : 0.0;
        tick_ = chance(0.1) ? uni(0, 50) : 1000 + uni(0, 100000);
        int segmentEnd = 0;
        for (frame_ = 0; frame_ < frames; ++frame_) {
            if (frame_ >= segmentEnd) {
                const int r = uni(0, 9);
                const GameContext next = r < 5 ? GameContext::InGame : r < 7 ? GameContext::Pregame : r < 8 ? GameContext::Lobby : r < 9 ? GameContext::Replay : GameContext::Unknown;
                segmentEnd = frame_ + (next == GameContext::Unknown ? uni(1, 3) : uni(40, 320));
                if (frame_ > 0 && chance(0.3)) worldChanged_ = true;
                context_ = next;
            }
            if (context_ == GameContext::InGame && chance(0.003)) chat_.push_back(pick({"                  1st Killer - Someone - 7", "Reward Summary", "VICTORY!", "Winner: Team Red", "GAME OVER!"}));
            if (chance(0.004)) chat_.push_back("[MVP+] Someone: winner: me lol");
            for (int i = 0; i < static_cast<int>(actors_.size()); ++i) {
                if (frame_ >= actors_[static_cast<std::size_t>(i)].until) draw(i);
            }
            for (int i = 0; i < static_cast<int>(actors_.size()); ++i) {
                act(i);
                events(actors_[static_cast<std::size_t>(i)]);
                visibility(actors_[static_cast<std::size_t>(i)]);
            }
            // A server stall: nobody's position arrives for a moment, then everyone catches up.
            if (stallLeft_ == 0 && chance(0.003)) stallLeft_ = uni(4, 12);
            if (stallLeft_ > 0) {
                --stallLeft_;
                for (Actor& a : actors_) a.sim.freeze = stallLeft_ > 0;
            }
            emitFrame();
        }
        marks();
    }

    // Hundreds of players passing through many short games: the session grows past 400 records
    // and earlier scenes are pruned.
    void churn() {
        actors_.clear();
        context_ = GameContext::InGame;
        for (frame_ = 0; frame_ < 1400; ++frame_) {
            if (frame_ % 60 == 0) {
                actors_.clear();
                worldChanged_ = frame_ > 0;
                context_ = frame_ % 120 == 0 ? GameContext::InGame : GameContext::Pregame;
                for (int i = 0; i < 40; ++i) {
                    Actor a;
                    const int n = frame_ / 60 * 40 + i;
                    a.sim = Sim(i + 1, "P" + std::to_string(n));
                    a.sim.f.uuid = uuidFor(900000 + n, 4);
                    a.sim.f.pos = {i * 5.0, 64, 0};
                    a.sim.f.tabKnown = a.sim.f.tabListed = true;
                    a.behavior = i % 7 == 0 ? kLowHop : kWalk;
                    a.until = 1 << 30;
                    actors_.push_back(a);
                }
            }
            for (int i = 0; i < static_cast<int>(actors_.size()); ++i) act(i);
            emitFrame();
        }
    }

    void marks() {
        if (!chance(0.3) || actors_.size() < 2) return;
        std::string players;
        auto add = [&](const std::string& key, const std::string& name, bool watch, bool confirmed, bool exempt) {
            if (!players.empty()) players += ",";
            players += "\"" + key + "\":{\"name\":\"" + name + "\",\"watch\":" + (watch ? "true" : "false") + ",\"confirmed\":" +
                       (confirmed ? "true" : "false") + ",\"exempt\":" + (exempt ? "true" : "false") +
                       ",\"added\":1764201600000,\"updated\":1764201600000,\"reasons\":[]}";
        };
        const int n = uni(1, 3);
        for (int k = 0; k < n; ++k) {
            const Actor& a = actors_[static_cast<std::size_t>(uni(0, static_cast<int>(actors_.size()) - 1))];
            const std::string& name = a.sim.f.name;
            bool plain = !name.empty() && name.find('"') == std::string::npos && name.find('\\') == std::string::npos;
            if (!plain) continue;
            const double r = real(0, 1);
            std::string lower = name;
            for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (r < 0.4) add(a.sim.f.uuid.empty() ? "name:" + name : a.sim.f.uuid, name, false, false, true);
            else if (r < 0.7) add("user:" + lower, name, false, false, true);   // added by name, exempt: binds when seen
            else if (r < 0.85) add("user:" + lower, name, true, false, false);
            else add(a.sim.f.uuid.empty() ? "name:" + name : a.sim.f.uuid, name, true, true, false);
        }
        if (!players.empty()) out_.marks = "{\"format\":2,\"players\":{" + players + "}}";
    }

    // ── Configurations ────────────────────────────────────────────────────────────────────────
    void perturb(EngineConfig& c, int count) {
        const int n = static_cast<int>(schema_->size());
        for (int k = 0; k < count; ++k) {
            const ParamId id = uni(0, n - 1);
            const ParamSpec& p = schema_->spec(id);
            switch (p.type) {
                case ParamType::Bool: c.set(id, c.b(id) ? 0.0 : 1.0); break;
                case ParamType::Choice: c.set(id, uni(0, static_cast<int>(p.choices.size()) - 1)); break;
                case ParamType::Int: c.set(id, uni(static_cast<int>(p.range.min), static_cast<int>(p.range.max))); break;
                case ParamType::Float: {
                    if (p.range.step > 0.0) {
                        const int steps = static_cast<int>(std::floor((p.range.max - p.range.min) / p.range.step + 1e-9));
                        c.set(id, p.range.min + uni(0, steps) * p.range.step);
                    } else {
                        c.set(id, real(p.range.min, p.range.max));
                    }
                    break;
                }
            }
        }
    }

    void setKey(EngineConfig& c, const char* key, double v) { c.set(key, v); }

    void configs() {
        EngineConfig c(schema_);
        bool any = false;
        if (chance(0.7)) {
            perturb(c, uni(0, 12));
            any = true;
        }
        if (chance(0.35)) {
            // Debug output exercises every debug line's formatting.
            for (const char* check : {"killaura", "reach", "autoclicker", "velocity", "bhop", "keepy", "legitscaff"}) {
                if (chance(0.6)) setKey(c, (std::string("checks.") + check + ".debug").c_str(), 1.0);
            }
            any = true;
        }
        if (chance(0.15)) { setKey(c, "general.runIn.lobby", 1.0); setKey(c, "general.runIn.pregame", 1.0); any = true; }
        if (chance(0.1)) { setKey(c, "general.analyzeSelf", 1.0); setKey(c, "exempt.teammates", 0.0); any = true; }
        if (chance(0.03)) { setKey(c, "general.monitoring", 0.0); any = true; }
        if (any || chance(0.5)) out_.configs.push_back({-1, c});
        const int changes = chance(0.35) ? uni(1, 3) : 0;
        const int total = static_cast<int>(out_.frames.size());
        for (int k = 0; k < changes && total > 2; ++k) {
            const int at = uni(1, total - 1);
            if (chance(0.25)) setKey(c, "general.monitoring", c.b(schema_->find("general.monitoring")) ? 0.0 : 1.0);
            else perturb(c, uni(1, 6));
            out_.configs.push_back({at, c});
        }
        std::stable_sort(out_.configs.begin(), out_.configs.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    }
};

}  // namespace gen

// Scenario `index` of the generated set `seed`.
inline GeneratedScenario generatedScenario(std::uint64_t seed, int index, const std::shared_ptr<const ConfigSchema>& schema) {
    return gen::Generator(seed, index, schema).run();
}

}  // namespace vigil::test

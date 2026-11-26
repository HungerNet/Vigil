// Vigil UI preview — runs the real UI and detection engine outside Minecraft.
//
// A small simulated arena (legitimate players and players running specific cheats) feeds the
// engine with WorldFrames, exactly like the game adapter, so every screen shows real evidence.
//
//   VigilPreview.exe                       interactive window
//   VigilPreview.exe --shot out.png [--page evidence] [--theme 0..4] [--size 1600x1000]
//                    [--mouse x,y] [--hud] [--check bhop] [--accent N] [--frames N]

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>

#include "../../src/core/log.h"
#include "../../src/core/settings.h"
#include "../../src/detection/engine.h"
#include "../../src/identity/identity.h"
#include "../../src/ui/app.h"
#include "../../src/ui/gfx.h"
#include "../../src/utilities/utilities.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace vigil::ui {
void setPreviewMode(bool preview);
}

using namespace vigil;
using namespace vigil::detection;

namespace {

// ── Simulated arena ──────────────────────────────────────────────────────────────────────────

struct Sim {
    PlayerFrame f;
    Vec3 motion;
    double moveYaw = 0.0;
    int swingTicks = -1;
    Sim(int id, const char* name, double x, double z) {
        f.entityId = id;
        f.name = name;
        char uuid[64];
        std::snprintf(uuid, sizeof(uuid), "5f1c%04x-7a2b-4c3d-9e8f-%012d", id, id * 7919);
        f.uuid = uuid;
        f.pos = {x, 64.0, z};
        f.onGround = true;
        f.serverPosition = true;
        f.potionsKnown = true;
        f.blockBelow = 2;
        f.blockAboveHead = 0;
        f.heldItem = 276;
        f.health = 20.f;
    }
    void anim() {
        if (swingTicks >= 0 && ++swingTicks >= 6) { swingTicks = -1; f.swingInProgress = false; f.swingProgress = 0; }
        else if (swingTicks >= 0) f.swingProgress = swingTicks;
        if (f.hurtTime > 0) --f.hurtTime;
    }
    void swing() { swingTicks = 0; f.swingInProgress = true; f.swingProgress = 0; }
    void hurt() { f.hurtTime = 10; }
    void lookAt(const Sim& o) {
        const Vec3 eye{f.pos.x, f.pos.y + 1.62, f.pos.z};
        const Vec3 c{o.f.pos.x, o.f.pos.y + 0.9, o.f.pos.z};
        f.yaw = static_cast<float>(yawTowards(eye, c));
        f.pitch = static_cast<float>(pitchTowards(eye, c));
    }
    void physics(double input, bool jump, double speedOverride = 0.0) {
        const double rad = moveYaw * 3.14159265358979323846 / 180.0;
        const Vec3 fwd{-std::sin(rad), 0.0, std::cos(rad)};
        f.sprinting = input > 0;
        if (speedOverride > 0.0) {
            f.pos = f.pos + fwd * speedOverride;
            if (f.onGround && jump) { motion.y = 0.25; f.onGround = false; f.pos.y += 0.01; }
        } else if (f.onGround) {
            if (jump) { motion.y = 0.42; if (f.sprinting) motion = motion + fwd * 0.2; }
            motion = motion + fwd * (0.13 * input);
            f.pos = f.pos + Vec3{motion.x, 0.0, motion.z};
            motion.x *= 0.546;
            motion.z *= 0.546;
        } else {
            motion = motion + fwd * (0.026 * input);
            f.pos = f.pos + Vec3{motion.x, 0.0, motion.z};
            motion.x *= 0.91;
            motion.z *= 0.91;
        }
        f.pos.y += motion.y;
        if (f.pos.y <= 64.0) { f.pos.y = 64.0; motion.y = 0.0; f.onGround = true; }
        else { f.onGround = false; motion.y = (motion.y - 0.08) * 0.98; }
    }
    PlayerFrame frame() const {
        PlayerFrame o = f;
        o.pos.x = std::round(f.pos.x * 32.0) / 32.0;
        o.pos.y = std::round(f.pos.y * 32.0) / 32.0;
        o.pos.z = std::round(f.pos.z * 32.0) / 32.0;
        o.yaw = static_cast<float>(std::round(f.yaw / 1.40625) * 1.40625);
        o.pitch = static_cast<float>(std::round(f.pitch / 1.40625) * 1.40625);
        return o;
    }
};

class Arena {
public:
    Arena() {
        add(1, "You", 5, -10).f.isLocal = true;
        add(2, "Brightwater", 0, 0);
        add(3, "tidewalker", 0, 2.6);
        add(4, "Kestrel", 20, 0);
        add(5, "Nimbus", 17.8, 0.6);
        add(6, "Vale", 22.2, 0.6);
        add(7, "Morrow", 0, 20);
        add(8, "Fennick", 0, 24.3);
        add(9, "Lowhop_", 40, 40);
        add(10, "Quill", -20, 0);
        add(11, "Sorrel", -20, 3);
        add(12, "Harrow", -20, 17.4);
        add(13, "Ashen", -20, 20);
        add(14, "Wren", 60, -30);
        // A nicked player: Hypixel nicks carry version-1 UUIDs.
        add(15, "Disguised", 64, -26).f.uuid = "0b4f2ba3-6c5d-1e8f-9a0b-1c2d3e4f5a6b";
        // A shopkeeper NPC (version-2 UUID): filtered, never listed or analyzed.
        add(16, "Item_Shop", 70, -20).f.uuid = "3c4d5e6f-1a2b-2c3d-8e9f-0a1b2c3d4e5f";
        // Players from the previous game (present only before the world change).
        earlier_.emplace_back(31, "Ember", 30, 60);
        earlier_.emplace_back(32, "Thistle", 34, 60);
        earlier_.emplace_back(33, "Corvid", 38, 60);
    }

    static std::string uuidOf(int id) {
        char uuid[64];
        std::snprintf(uuid, sizeof(uuid), "5f1c%04x-7a2b-4c3d-9e8f-%012d", id, id * 7919);
        return uuid;
    }
    bool previousGame = false;

    void step(Tick tick) {
        Sim& you = get("You");
        (void)you;
        const int t = static_cast<int>(tick % 100000);
        // Legit duel: alternate hits, real knockback.
        duel(get("Brightwater"), get("tidewalker"), t, 2.6, true, false);
        // Multi-target aura.
        {
            Sim& a = get("Kestrel");
            Sim& v1 = get("Nimbus");
            Sim& v2 = get("Vale");
            a.lookAt((t / 6) % 2 ? v1 : v2);
            if (t % 12 == 0) {
                v1.f.pos = {17.8, 64.0, 0.6};
                v2.f.pos = {22.2, 64.0, 0.6};
                v1.motion = v2.motion = {};
                v1.f.onGround = v2.f.onGround = true;
            }
            if (t % 12 == 0 && phase(t, 3)) a.swing();
            if (t % 12 == 1 && phase(t, 3)) {
                for (Sim* v : {&v1, &v2}) {
                    v->hurt();
                    const Vec3 away = (v->f.pos - a.f.pos).horizontal();
                    const double len = std::max(1e-6, away.horizontalLength());
                    v->motion = away * (0.4 / len);
                    v->motion.y = 0.3608;
                    v->f.onGround = false;
                    v->f.pos.y += 0.0001;
                }
            }
            for (Sim* v : {&v1, &v2}) {
                if (!v->f.onGround) {
                    v->f.pos = v->f.pos + v->motion;
                    v->motion.x *= 0.91;
                    v->motion.z *= 0.91;
                    if (v->f.pos.y <= 64.0) { v->f.pos.y = 64.0; v->motion = {}; v->f.onGround = true; }
                    else v->motion.y = (v->motion.y - 0.08) * 0.98;
                }
                v->f.pos.x += (t % 2 == 0 ? 0.03 : -0.03);
            }
        }
        // Reach.
        duel(get("Morrow"), get("Fennick"), t, 4.3, phase(t, 2), false);
        // Low-hop speed.
        {
            Sim& b = get("Lowhop_");
            b.moveYaw += 0.6;
            if (phase(t, 4)) b.physics(1.0, true, 0.55);
            else b.physics(1.0, true);
        }
        // Autoclicker.
        {
            Sim& q = get("Quill");
            q.lookAt(get("Sorrel"));
            if (phase(t, 5) ? t % 4 == 0 : rng_() % 5 == 0) q.swing();
        }
        // Anti-knockback victim.
        duel(get("Harrow"), get("Ashen"), t, 2.6, true, phase(t, 6));
        // Wanderer.
        {
            Sim& w = get("Wren");
            w.moveYaw += 2.0;
            w.physics(1.0, (t % 30) < 15);
        }
        for (Sim& s : sims_) {
            if (s.f.name != "Lowhop_" && s.f.name != "Wren") s.f.sprinting = false;
            s.anim();
        }
        // Relay floor sample (some player swinging at the server's cadence).
        if (t < 60 && t % 3 == 0) get("Wren").swing();
    }

    WorldFrame frame(Tick tick, double timeMs) {
        WorldFrame w;
        w.tick = tick;
        w.timeMs = timeMs;
        w.context = GameContext::InGame;
        w.contextDetail = "BED WARS";
        for (const Sim& s : sims_) w.players.push_back(s.frame());
        if (previousGame) {
            for (Sim& s : earlier_) {
                s.moveYaw += 1.0;
                s.physics(0.6, false);
                w.players.push_back(s.frame());
            }
        }
        w.worldChanged = worldChangePending_;
        worldChangePending_ = false;
        return w;
    }
    void endPreviousGame() {
        previousGame = false;
        worldChangePending_ = true;
    }

private:
    // Cheaters act in bursts so confidence rises and decays visibly.
    static bool phase(int t, int seed) { return ((t / 400) + seed) % 3 != 0; }

    Sim& add(int id, const char* name, double x, double z) {
        sims_.emplace_back(id, name, x, z);
        return sims_.back();
    }
    Sim& get(const char* name) {
        for (Sim& s : sims_) if (s.f.name == name) return s;
        return sims_.front();
    }
    void duel(Sim& a, Sim& v, int t, double dist, bool active, bool antiKb) {
        const int k = t % 12;
        if (k == 0) {
            v.f.pos = {a.f.pos.x + 0.03 * std::sin(t * 0.1), 64.0, a.f.pos.z + dist};
            v.f.onGround = true;
            v.motion = {};
        }
        a.lookAt(v);
        if (!active) return;
        if (k == 0) a.swing();
        if (k == 1) {
            v.hurt();
            if (!antiKb) { v.motion.y = 0.3608; v.motion.z += 0.4; v.f.onGround = false; v.f.pos.y += 0.0001; }
        }
        if (!v.f.onGround || v.motion.horizontalLength() > 0) {
            v.f.pos = v.f.pos + v.motion;
            v.motion.x *= v.f.onGround ? 0.546 : 0.91;
            v.motion.z *= v.f.onGround ? 0.546 : 0.91;
            if (v.f.pos.y <= 64.0) { v.f.pos.y = 64.0; v.motion.y = 0; v.f.onGround = true; }
            else { v.f.onGround = false; v.motion.y = (v.motion.y - 0.08) * 0.98; }
        }
        v.f.pos.x += (k % 2 == 0 ? 0.04 : -0.04);
    }

    std::vector<Sim> sims_;
    std::vector<Sim> earlier_;
    bool worldChangePending_ = false;
    std::mt19937 rng_{42};
};

// ── Host ─────────────────────────────────────────────────────────────────────────────────────

// Deterministic pixel-art faces (8×8, Minecraft skin layout) so the preview shows real avatars.
unsigned int makeFaceTexture(const std::string& name) {
    std::uint32_t h = 2166136261u;
    for (char c : name) { h ^= static_cast<unsigned char>(c); h *= 16777619u; }
    auto pick = [&](int shift, int n) { return static_cast<int>((h >> shift) % static_cast<std::uint32_t>(n)); };
    static const unsigned char skins[5][3] = {{236, 188, 156}, {214, 160, 120}, {170, 120, 84}, {120, 82, 58}, {246, 210, 182}};
    static const unsigned char hairs[6][3] = {{40, 28, 20}, {92, 60, 36}, {196, 152, 82}, {20, 20, 24}, {150, 62, 40}, {210, 210, 200}};
    static const unsigned char eyes[4][3] = {{60, 90, 160}, {60, 120, 70}, {90, 60, 40}, {40, 40, 40}};
    const auto* sk = skins[pick(0, 5)];
    const auto* hr = hairs[pick(3, 6)];
    const auto* ey = eyes[pick(7, 4)];
    const int fringe = 1 + pick(10, 2);
    std::vector<unsigned char> px(64 * 64 * 4, 0);
    auto put = [&](int x, int y, const unsigned char* c, unsigned char a = 255) {
        unsigned char* p = &px[static_cast<std::size_t>((y * 64 + x) * 4)];
        p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; p[3] = a;
    };
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const bool hair = y < fringe || ((x == 0 || x == 7) && y < 4);
            unsigned char c[3] = {sk[0], sk[1], sk[2]};
            if (hair) { c[0] = hr[0]; c[1] = hr[1]; c[2] = hr[2]; }
            const int shade = ((x * 7 + y * 3 + static_cast<int>(h)) % 5) - 2;
            for (unsigned char& v : c) v = static_cast<unsigned char>(std::clamp(v + shade * 4, 0, 255));
            put(8 + x, 8 + y, c);
        }
    }
    const unsigned char white[3] = {240, 240, 240};
    put(9, 12, white); put(10, 12, ey); put(13, 12, ey); put(14, 12, white);
    const unsigned char mouth[3] = {static_cast<unsigned char>(sk[0] * 0.6), static_cast<unsigned char>(sk[1] * 0.45), static_cast<unsigned char>(sk[2] * 0.45)};
    put(11, 14, mouth); put(12, 14, mouth);
    return ui::glx::createTextureRGBA(64, 64, px.data());
}

class PreviewHost final : public ui::Host {
public:
    detection::Engine* engine = nullptr;
    void reportPlayer(const std::string& name) override { std::printf("[preview] report %s\n", name.c_str()); }
    void openFolder(const std::string& path) override { std::printf("[preview] open %s\n", path.c_str()); }
    std::string dataDirectory() const override { return "C:\\Users\\you\\AppData\\Local\\Vigil"; }
    bool connectedToHypixel() const override { return true; }
    std::string signalSummary() const override { return "All signals available (simulated arena)"; }
    void newSession() override {}
    ui::RuntimeStatus runtimeStatus() const override {
        ui::RuntimeStatus s;
        s.hosted = false;
        s.hypixel = true;
        s.server = "mc.hypixel.net";
        s.samplerResolved = true;
        for (const char* n : {"Server positions", "Head rotation", "Movement attribute", "Potion effects", "Block probes", "Held items",
                              "Arm swings", "Hurt animations", "Player list", "Player latency"}) {
            s.signals.push_back({n, true, ""});
        }
        s.dataDir = dataDirectory();
        return s;
    }
    bool playerFace(const std::string& key, ui::FaceTexture& out) override {
        if (!engine) return false;
        const auto* r = engine->session().find(key);
        if (!r) return false;
        auto it = faces_.find(key);
        if (it == faces_.end()) it = faces_.emplace(key, makeFaceTexture(r->name)).first;
        out.texture = it->second;
        return true;
    }

private:
    std::unordered_map<std::string, unsigned int> faces_;
};

double qpcMs() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart);
}

// Procedural "game world" behind the UI so the backdrop blur has something to work with.
void drawWorld(int w, int h, double tSec) {
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glBegin(GL_QUADS);
    glColor3f(0.42f, 0.62f, 0.92f);
    glVertex2f(0, 0);
    glVertex2f(static_cast<float>(w), 0);
    glColor3f(0.75f, 0.86f, 0.98f);
    glVertex2f(static_cast<float>(w), h * 0.62f);
    glVertex2f(0, h * 0.62f);
    glEnd();
    // Sun.
    glColor3f(1.f, 0.96f, 0.82f);
    glBegin(GL_QUADS);
    const float sx = w * 0.72f, sy = h * 0.18f, ss = h * 0.06f;
    glVertex2f(sx - ss, sy - ss); glVertex2f(sx + ss, sy - ss); glVertex2f(sx + ss, sy + ss); glVertex2f(sx - ss, sy + ss);
    glEnd();
    // Blocky terrain.
    const int cols = 48;
    const float bw = static_cast<float>(w) / cols;
    for (int i = 0; i < cols; ++i) {
        const float hh = h * (0.22f + 0.08f * std::sin(i * 0.45f) + 0.05f * std::sin(i * 1.7f + 1.3f));
        const float top = std::floor((h - hh) / bw) * bw;
        glColor3f(0.36f, 0.62f, 0.25f);
        glBegin(GL_QUADS);
        glVertex2f(i * bw, top); glVertex2f((i + 1) * bw, top); glVertex2f((i + 1) * bw, top + bw); glVertex2f(i * bw, top + bw);
        glEnd();
        glColor3f(0.48f, 0.34f, 0.22f);
        glBegin(GL_QUADS);
        glVertex2f(i * bw, top + bw); glVertex2f((i + 1) * bw, top + bw); glVertex2f((i + 1) * bw, static_cast<float>(h));
        glVertex2f(i * bw, static_cast<float>(h));
        glEnd();
    }
    // A few "players".
    for (int i = 0; i < 4; ++i) {
        const float px = w * (0.2f + 0.18f * i) + 20.f * static_cast<float>(std::sin(tSec + i));
        const float py = h * 0.58f;
        glColor3f(0.2f + 0.15f * i, 0.25f, 0.55f - 0.1f * i);
        glBegin(GL_QUADS);
        glVertex2f(px, py); glVertex2f(px + 18, py); glVertex2f(px + 18, py + 54); glVertex2f(px, py + 54);
        glEnd();
    }
}

// Minimal PNG writer (stored deflate blocks).
std::uint32_t crc32(const unsigned char* d, std::size_t n, std::uint32_t c = 0xFFFFFFFFu) {
    static std::uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t k = i;
            for (int j = 0; j < 8; ++j) k = (k & 1) ? 0xEDB88320u ^ (k >> 1) : k >> 1;
            table[i] = k;
        }
        init = true;
    }
    for (std::size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c;
}

bool writePng(const std::string& path, int w, int h, const std::vector<unsigned char>& rgba) {
    std::vector<unsigned char> raw;
    raw.reserve(static_cast<std::size_t>((w * 3 + 1) * h));
    for (int y = h - 1; y >= 0; --y) {  // GL rows are bottom-up
        raw.push_back(0);
        for (int x = 0; x < w; ++x) {
            const unsigned char* p = &rgba[static_cast<std::size_t>((y * w + x) * 4)];
            raw.push_back(p[0]); raw.push_back(p[1]); raw.push_back(p[2]);
        }
    }
    std::vector<unsigned char> z = {0x78, 0x01};
    std::uint32_t a = 1, b = 0;
    for (unsigned char c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    for (std::size_t off = 0; off < raw.size();) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - off);
        z.push_back(off + n >= raw.size() ? 1 : 0);
        z.push_back(n & 0xFF); z.push_back((n >> 8) & 0xFF);
        z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(off), raw.begin() + static_cast<std::ptrdiff_t>(off + n));
        off += n;
    }
    const std::uint32_t adler = (b << 16) | a;
    for (int i = 3; i >= 0; --i) z.push_back((adler >> (i * 8)) & 0xFF);
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    auto u32 = [&](std::uint32_t v) { unsigned char c[4] = {static_cast<unsigned char>(v >> 24), static_cast<unsigned char>(v >> 16),
                                                            static_cast<unsigned char>(v >> 8), static_cast<unsigned char>(v)}; out.write(reinterpret_cast<char*>(c), 4); };
    auto chunk = [&](const char* type, const std::vector<unsigned char>& data) {
        u32(static_cast<std::uint32_t>(data.size()));
        std::vector<unsigned char> td(type, type + 4);
        td.insert(td.end(), data.begin(), data.end());
        out.write(reinterpret_cast<const char*>(td.data()), static_cast<std::streamsize>(td.size()));
        u32(crc32(td.data(), td.size()) ^ 0xFFFFFFFFu);
    };
    const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    out.write(reinterpret_cast<const char*>(sig), 8);
    std::vector<unsigned char> ihdr = {static_cast<unsigned char>(w >> 24), static_cast<unsigned char>(w >> 16), static_cast<unsigned char>(w >> 8),
                                       static_cast<unsigned char>(w), static_cast<unsigned char>(h >> 24), static_cast<unsigned char>(h >> 16),
                                       static_cast<unsigned char>(h >> 8), static_cast<unsigned char>(h), 8, 2, 0, 0, 0};
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    return true;
}

struct Options {
    std::string shot;
    std::string page = "overview";
    std::string tab;            // profile tab: summary, evidence, behavior, identity
    std::string player;         // player to open on the Players page
    std::string search;         // open the search palette with this query
    std::string section;        // settings section name
    int theme = 0, accent = 0;
    int w = 1600, h = 1000;
    int mx = -1, my = -1;
    bool hud = false;
    bool activation = false;
    bool onboarding = false;
    int onboardingStep = 0;
    int frames = 150;
    bool clickAt = false;
    double scale = 1.0;
    float wheel = 0.f;                  // wheel notches at the --mouse position on frame 3
    int dragX0 = -1, dragY0 = -1, dragX1 = -1, dragY1 = -1;   // press, move, keep holding
    bool release = false;               // release the drag 20 frames before the capture
    std::vector<std::string> options;   // App::previewOption
    int transitionTo = -1;              // capture a theme transition in progress
    int transitionFrames = 8;
};

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ui::App& app = ui::App::instance();
    switch (msg) {
        case WM_CLOSE: PostQuitMessage(0); return 0;
        case WM_MOUSEMOVE: app.onMouseMove(static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))); return 0;
        case WM_LBUTTONDOWN: app.onMouseButton(0, true, static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))); return 0;
        case WM_LBUTTONUP: app.onMouseButton(0, false, static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))); return 0;
        case WM_MOUSEWHEEL: app.onMouseWheel(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA); return 0;
        case WM_CHAR: app.onChar(static_cast<unsigned int>(wp)); return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE && !app.textInputActive()) { if (app.isOpen()) app.requestClose(); else app.open(); return 0; }
            app.onKey(static_cast<int>(wp), true, (GetKeyState(VK_CONTROL) & 0x8000) != 0, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                LPCSTR id = IDC_ARROW;
                if (app.cursor() == ui::Cursor::Hand) id = IDC_HAND;
                else if (app.cursor() == ui::Cursor::IBeam) id = IDC_IBEAM;
                SetCursor(LoadCursorA(nullptr, id));
                return TRUE;
            }
            break;
        default: break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// Demo data for the identity tools and utilities so their screens show real states.
void seedDemoServices(const Arena&) {
    using namespace identity;
    Service& s = Service::instance();
    s.setFetcher([](const std::string& url, int& status, std::string& body, std::string&) {
        status = 200;
        if (url.find("sessionserver") != std::string::npos) body = R"({"id":"7d3c2e1f0a9b48c7b6d5e4f3a2b1c0d9","name":"Halcyon"})";
        else body = R"({"success":true,"data":[{"name":"Halcyon","distance":0}]})";
        return true;
    });
    // {"profileId":"7d3c2e1f0a9b48c7b6d5e4f3a2b1c0d9","profileName":"Halcyon","textures":{"SKIN":{"url":"http://textures.minecraft.net/texture/9f2e"}}}
    const std::string textures =
        "eyJwcm9maWxlSWQiOiI3ZDNjMmUxZjBhOWI0OGM3YjZkNWU0ZjNhMmIxYzBkOSIsInByb2ZpbGVOYW1lIjoiSGFsY3lvbiIsInRleHR1cmVzIjp7"
        "IlNLSU4iOnsidXJsIjoiaHR0cDovL3RleHR1cmVzLm1pbmVjcmFmdC5uZXQvdGV4dHVyZS85ZjJlIn19fQ==";
    s.observe("0b4f2ba3-6c5d-1e8f-9a0b-1c2d3e4f5a6b", "Disguised", "0b4f2ba3-6c5d-1e8f-9a0b-1c2d3e4f5a6b", textures);

    using utilities::Activity;
    using utilities::Kind;
    Activity::instance().add(Kind::AutoWho, "Sent /who as the game started");
    Activity::instance().add(Kind::PartyChat, "Party of 3 joined: Kestrel, Nimbus, Vale", true);
    Activity::instance().add(Kind::PartyQueue, "Queue party of 2 (ids 4601\xE2\x80\x93" "4602)", true);
    Activity::instance().add(Kind::FastBuy, "Bought from Quick Buy slot 19");
    utilities::LiveState live;
    live.hypixel = true;
    live.chatReader = true;
    live.bedwarsPregame = true;
    live.mode = "Doubles";
    live.queueMaxParty = 2;
    utilities::setLiveState(live);
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--shot") o.shot = next();
        else if (a == "--page") o.page = next();
        else if (a == "--tab") o.tab = next();
        else if (a == "--player") o.player = next();
        else if (a == "--search") o.search = next();
        else if (a == "--section") o.section = next();
        else if (a == "--theme") o.theme = std::atoi(next().c_str());
        else if (a == "--accent") o.accent = std::atoi(next().c_str());
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &o.w, &o.h);
        else if (a == "--mouse") std::sscanf(next().c_str(), "%d,%d", &o.mx, &o.my);
        else if (a == "--click") o.clickAt = true;
        else if (a == "--hud") o.hud = true;
        else if (a == "--activation") o.activation = true;
        else if (a == "--onboarding") { o.onboarding = true; o.onboardingStep = std::atoi(next().c_str()); }
        else if (a == "--frames") o.frames = std::atoi(next().c_str());
        else if (a == "--scale") o.scale = std::atof(next().c_str());
        else if (a == "--wheel") o.wheel = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--drag") std::sscanf(next().c_str(), "%d,%d,%d,%d", &o.dragX0, &o.dragY0, &o.dragX1, &o.dragY1);
        else if (a == "--release") o.release = true;
        else if (a == "--option") o.options.push_back(next());
        else if (a == "--transition-to") o.transitionTo = std::atoi(next().c_str());
        else if (a == "--transition-frames") o.transitionFrames = std::atoi(next().c_str());
    }

    // Engine + settings (in a temporary directory so the preview never touches real data).
    const std::string tmp = (std::filesystem::temp_directory_path() / "vigil-preview").string();
    std::filesystem::create_directories(tmp);
    log::init(tmp);
    log::write("vigil", "Vigil preview starting");
    log::write("runtime", "sampler: resolved=1 serverPos=1 head=1 potions=1 attribute=1 blocks=1 item=1 swing=1 hurt=1");
    log::write("runtime", "present hook: 1 import slots patched");
    log::write("runtime", "window procedure hooked");
    Engine engine;
    engine.initialize();
    PreviewHost host;
    host.engine = &engine;
    ui::App& app = ui::App::instance();
    app.initialize(&host, &engine);
    Settings::instance().find("ui.theme")->set(o.theme);
    Settings::instance().find("ui.accent")->set(o.accent);
    Settings::instance().find("ui.onboarded")->set(o.onboarding ? 0.0 : 1.0);
    Settings::instance().find("ui.scale")->set(o.scale);
    // Marks: one confirmed player and one watched player, in a scratch file.
    {
        const std::string marks = (std::filesystem::path(tmp) / "marks.json").string();
        std::remove(marks.c_str());
        engine.setMarksPath(marks);
        engine.marks().confirm(Arena::uuidOf(4), "Kestrel", {"KillAura"});
        engine.marks().setWatch(Arena::uuidOf(7), "Morrow", true);
        engine.marks().setWatch("5f1c0063-7a2b-4c3d-9e8f-000000999999", "Halloway", true);   // not seen this session
    }

    // Window + GL context.
    WNDCLASSA wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "VigilPreview";
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    RegisterClassA(&wc);
    RECT r{0, 0, o.w, o.h};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowA("VigilPreview", "Vigil Preview", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                              r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
    HGLRC rc = wglCreateContext(dc);
    wglMakeCurrent(dc, rc);
    ui::glx::load();

    // Fast-forward the arena so the session has history.
    Arena arena;
    seedDemoServices(arena);
    const int warmTicks = 20 * 240;
    const double start = qpcMs() - warmTicks * 50.0;
    engine.resetSession(start);
    Tick tick = 0;
    arena.previousGame = true;   // the first minute is an earlier game in another world
    for (; tick < warmTicks; ++tick) {
        if (tick == 20 * 60) arena.endPreviousGame();
        arena.step(tick);
        engine.onFrame(arena.frame(tick, start + tick * 50.0));
    }

    if (!o.hud && !o.activation) app.open();
    auto key = [&](int vk, bool ctrl) { app.onKey(vk, true, ctrl, false); };
    auto selectPage = [&](const std::string& p) { app.openTo(p); };

    if (!o.shot.empty()) {
        ui::setPreviewMode(true);
        unsigned int colorTex = 0;
        const unsigned int fbo = ui::glx::createFramebuffer(o.w, o.h, colorTex);
        double now = qpcMs();
        if (o.activation) app.showActivation();
        for (int f = 0; f < o.frames; ++f) {
            // Keep the simulation running in real time during the capture.
            arena.step(tick);
            engine.onFrame(arena.frame(tick, start + tick * 50.0));
            ++tick;
            now += 1000.0 / 60.0;
            if (f == 1 && !o.hud && !o.activation) {
                std::string playerKey;
                if (!o.player.empty()) {
                    for (const auto& pr : engine.session().players()) if (pr.name == o.player) playerKey = pr.key;
                }
                app.openTo(o.page, playerKey, o.tab, o.section);
                if (!o.search.empty()) app.openSearch(o.search == "-" ? "" : o.search);
                for (const auto& opt : o.options) app.previewOption(opt);
            }
            if (o.transitionTo >= 0 && f == o.frames - 1 - o.transitionFrames) Settings::instance().find("ui.theme")->set(o.transitionTo);
            if (o.mx >= 0 && (o.dragX0 < 0 || f < o.frames / 3)) app.onMouseMove(o.mx, o.my);
            if (o.wheel != 0.f && f == 3) app.onMouseWheel(o.wheel);
            if (o.clickAt && f == o.frames / 2) { app.onMouseButton(0, true, o.mx, o.my); }
            if (o.clickAt && f == o.frames / 2 + 1) { app.onMouseButton(0, false, o.mx, o.my); }
            if (o.dragX0 >= 0) {
                // Press at the start point, glide to the end point over 40 frames, keep holding.
                const int f0 = o.frames / 3, f1 = f0 + 40, fr = o.frames - 20;
                if (f == f0) app.onMouseButton(0, true, o.dragX0, o.dragY0);
                if (f > f0 && f <= f1) {
                    const float k = static_cast<float>(f - f0) / (f1 - f0);
                    app.onMouseMove(static_cast<int>(o.dragX0 + (o.dragX1 - o.dragX0) * k), static_cast<int>(o.dragY0 + (o.dragY1 - o.dragY0) * k));
                }
                if (o.release && f == fr) app.onMouseButton(0, false, o.dragX1, o.dragY1);
            }
            if (o.onboarding && o.onboardingStep > 0 && f > 2 && f <= 2 + o.onboardingStep) key(VK_RIGHT, false);
            drawWorld(o.w, o.h, now / 1000.0);
            app.render(o.w, o.h, now, true);
        }
        std::vector<unsigned char> px(static_cast<std::size_t>(o.w) * o.h * 4);
        glReadPixels(0, 0, o.w, o.h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        const bool ok = writePng(o.shot, o.w, o.h, px);
        std::printf("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED", o.shot.c_str(), o.w, o.h);
        ui::glx::deleteFramebuffer(fbo, colorTex);
        identity::Service::instance().stop();
        return ok ? 0 : 1;
    }

    selectPage(o.page);
    ShowWindow(hwnd, SW_SHOW);
    MSG msg;
    double nextTick = qpcMs();
    for (;;) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { identity::Service::instance().stop(); return 0; }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        const double now = qpcMs();
        while (now >= nextTick) {
            arena.step(tick);
            engine.onFrame(arena.frame(tick, start + tick * 50.0));
            ++tick;
            nextTick += 50.0;
        }
        RECT cr;
        GetClientRect(hwnd, &cr);
        drawWorld(cr.right, cr.bottom, now / 1000.0);
        app.render(cr.right, cr.bottom, now, true);
        SwapBuffers(dc);
        Settings::instance().tick(now);
    }
}

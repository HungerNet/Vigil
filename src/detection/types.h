#pragma once
// Vigil detection framework — fundamental types.
//
// This layer is pure C++: no JNI, no OpenGL, no Windows. The game adapter (src/game) converts
// live Minecraft state into these structures once per client tick; tests and the UI preview
// construct them synthetically.

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace vigil::detection {

using Tick = std::int64_t;

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
    Vec3() = default;
    Vec3(double ax, double ay, double az) : x(ax), y(ay), z(az) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    double length() const { return std::sqrt(x * x + y * y + z * z); }
    double horizontalLength() const { return std::sqrt(x * x + z * z); }
    Vec3 horizontal() const { return {x, 0.0, z}; }
};

// Vanilla 1.8.9 physics and protocol facts used across checks. Each constant is documented
// because detection thresholds are derived from them and the UI quotes them in explanations.
namespace mc {
constexpr double kTickSeconds = 0.05;
constexpr double kEyeHeight = 1.62;
constexpr double kEyeHeightSneaking = 1.54;
constexpr double kPlayerHalfWidth = 0.3;
constexpr double kPlayerHeight = 1.8;
constexpr double kHitboxBorder = 0.1;            // Entity#getCollisionBorderSize for melee ray tests
constexpr double kVanillaReach = 3.0;             // survival client reach (eye → hitbox)
constexpr double kPositionQuantum = 1.0 / 32.0;   // relative-move packet precision
constexpr double kRotationQuantum = 360.0 / 256.0; // rotation packet precision (degrees)
constexpr double kAirFriction = 0.91;
constexpr double kAirControlSprint = 0.026;       // jumpMovementFactor while sprinting
constexpr double kSprintGroundSpeed = 0.2806;     // ≈5.61 m/s steady sprint on flat ground
constexpr double kSprintJumpAverage = 0.3563;     // ≈7.13 m/s average while sprint-jumping
constexpr double kJumpVelocity = 0.42;
constexpr int kFlatJumpAirTicks = 12;             // ground-to-ground on flat terrain
constexpr int kHurtTimeMax = 10;
constexpr int kSwingAnimationTicks = 6;

// Item ids (1.8.9 numeric ids).
inline bool isSword(int id) { return id == 267 || id == 268 || id == 272 || id == 276 || id == 283; }
inline bool isBlockItem(int id) { return id >= 1 && id <= 255; }
inline bool isRanged(int id) { return id == 261 /*bow*/ || id == 346 /*rod*/ || id == 332 /*snowball*/ ||
                                      id == 344 /*egg*/ || id == 368 /*pearl*/; }
inline bool isConsumableOrUsable(int id) {
    return id == 373 /*potion*/ || id == 261 /*bow*/ || id == 322 /*gapple*/ || id == 260 /*apple*/ ||
           id == 297 /*bread*/ || id == 364 /*steak*/ || id == 320 /*porkchop*/ || id == 335 /*milk*/ ||
           id == 282 /*stew*/ || id == 391 /*carrot*/ || id == 393 /*potato*/ || id == 366 /*chicken*/;
}
// Block ids relevant to movement exemptions.
inline bool isIce(int id) { return id == 79 || id == 174; }
inline bool isSlime(int id) { return id == 165; }
inline bool isSoulSand(int id) { return id == 88; }
inline bool isWeb(int id) { return id == 30; }
inline bool isClimbable(int id) { return id == 65 /*ladder*/ || id == 106 /*vine*/; }
inline bool isLiquid(int id) { return id >= 8 && id <= 11; }
inline bool isStairOrSlab(int id) {
    return id == 44 || id == 126 || id == 182 || id == 53 || id == 67 || id == 108 || id == 109 ||
           id == 114 || id == 128 || id == 134 || id == 135 || id == 136 || id == 156 || id == 163 ||
           id == 164 || id == 180;
}
inline bool isSolidish(int id) { return id > 0 && !isLiquid(id) && id != 30 && id != 31 && id != 32 &&
                                        id != 37 && id != 38 && id != 39 && id != 40 && id != 50 &&
                                        id != 51 && id != 55 && id != 59 && id != 63 && id != 65 &&
                                        id != 66 && id != 68 && id != 69 && id != 70 && id != 72 &&
                                        id != 75 && id != 76 && id != 77 && id != 78 && id != 83 &&
                                        id != 90 && id != 106 && id != 111 && id != 115 && id != 132 &&
                                        id != 141 && id != 142 && id != 143 && id != 171 && id != 175; }
}  // namespace mc

enum class GameContext : std::uint8_t { Unknown, Lobby, Pregame, InGame, Replay };

inline const char* gameContextName(GameContext c) {
    switch (c) {
        case GameContext::Lobby: return "Lobby";
        case GameContext::Pregame: return "Pre-game";
        case GameContext::InGame: return "In game";
        case GameContext::Replay: return "Replay";
        default: return "Idle";
    }
}

struct Potions {
    int speed = -1;      // amplifier, -1 = none
    int slowness = -1;
    int jumpBoost = -1;
};

// One client tick of one player as observed by the local client.
struct PlayerFrame {
    int entityId = -1;
    std::string uuid;
    std::string name;
    bool isLocal = false;
    bool isTeammate = false;

    Vec3 pos;                 // best available position (server-relayed when available)
    bool serverPosition = false;
    float yaw = 0.f;          // head yaw, degrees (MC convention: 0 = +Z, 90 = -X)
    float pitch = 0.f;        // degrees, positive looks down

    bool onGround = false;
    bool sprinting = false;
    bool sneaking = false;
    bool usingItem = false;
    bool blocking = false;
    bool invisible = false;
    bool riding = false;
    bool inWater = false;
    bool inLava = false;
    bool onLadder = false;
    bool inWeb = false;
    bool dead = false;

    int heldItem = -1;
    int swingProgress = 0;
    bool swingInProgress = false;
    int hurtTime = 0;
    float health = -1.f;
    Potions potions;
    bool potionsKnown = false; // false when the adapter could not read potion effects

    int blockBelow = -1;      // block id under the feet (-1 = unknown)
    int blockAboveHead = -1;  // block id at feet + 2 (ceiling check)
    bool wallNear = false;    // solid block within 0.6 horizontally at body height (sampled when hurt)
};

// Speed potion multiplier for movement limits. Unknown potion state uses a conservative value
// that tolerates Speed II so missing data never produces false positives.
inline double speedMultiplier(const PlayerFrame& f) {
    if (!f.potionsKnown) return 1.45;
    return f.potions.speed >= 0 ? 1.0 + 0.2 * (f.potions.speed + 1) : 1.0;
}

struct WorldFrame {
    Tick tick = 0;
    double timeMs = 0.0;
    int skippedTicks = 0;     // client ticks elapsed but not sampled (frame drops)
    GameContext context = GameContext::Unknown;
    std::string contextDetail; // e.g. "Bed Wars"
    std::vector<PlayerFrame> players;
};

// ── Geometry helpers ────────────────────────────────────────────────────────────────────────

inline double wrapDegrees(double d) {
    d = std::fmod(d, 360.0);
    if (d >= 180.0) d -= 360.0;
    if (d < -180.0) d += 360.0;
    return d;
}

// Look vector for MC yaw/pitch (degrees).
inline Vec3 lookVector(double yawDeg, double pitchDeg) {
    const double yaw = yawDeg * 3.14159265358979323846 / 180.0;
    const double pitch = pitchDeg * 3.14159265358979323846 / 180.0;
    return {-std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
}

// MC yaw (degrees) that faces from `from` toward `to`.
inline double yawTowards(const Vec3& from, const Vec3& to) {
    const double dx = to.x - from.x, dz = to.z - from.z;
    return -std::atan2(dx, dz) * 180.0 / 3.14159265358979323846;
}

inline double pitchTowards(const Vec3& from, const Vec3& to) {
    const double dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
    return -std::atan2(dy, std::sqrt(dx * dx + dz * dz)) * 180.0 / 3.14159265358979323846;
}

struct Box {
    Vec3 min, max;
    static Box player(const Vec3& feet, double grow = 0.0) {
        const double h = mc::kPlayerHalfWidth + grow;
        return {{feet.x - h, feet.y - grow, feet.z - h}, {feet.x + h, feet.y + mc::kPlayerHeight + grow, feet.z + h}};
    }
    Vec3 center() const { return {(min.x + max.x) * 0.5, (min.y + max.y) * 0.5, (min.z + max.z) * 0.5}; }
    double distanceTo(const Vec3& p) const {
        const double dx = std::fmax(std::fmax(min.x - p.x, 0.0), p.x - max.x);
        const double dy = std::fmax(std::fmax(min.y - p.y, 0.0), p.y - max.y);
        const double dz = std::fmax(std::fmax(min.z - p.z, 0.0), p.z - max.z);
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
};

// Angular distance (degrees) from a view direction to the nearest part of a box. Zero when the
// view ray passes through the box's angular extent.
inline double angleOutsideBox(const Vec3& eye, double yawDeg, double pitchDeg, const Box& box) {
    const Vec3 c = box.center();
    const double horiz = std::sqrt((c.x - eye.x) * (c.x - eye.x) + (c.z - eye.z) * (c.z - eye.z));
    const double halfW = (box.max.x - box.min.x) * 0.5 * 1.41421356;
    const double halfH = (box.max.y - box.min.y) * 0.5;
    const double yawSpan = std::atan2(halfW, std::fmax(horiz, 0.05)) * 180.0 / 3.14159265358979323846;
    const double pitchSpan = std::atan2(halfH, std::fmax(horiz, 0.05)) * 180.0 / 3.14159265358979323846;
    const double dyaw = std::fabs(wrapDegrees(yawTowards(eye, c) - yawDeg));
    const double dpitch = std::fabs(pitchTowards(eye, c) - pitchDeg);
    const double yo = std::fmax(0.0, dyaw - yawSpan);
    const double po = std::fmax(0.0, dpitch - pitchSpan);
    return std::sqrt(yo * yo + po * po);
}

}  // namespace vigil::detection

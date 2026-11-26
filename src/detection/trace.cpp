#include "trace.h"

#include "../core/json.h"

#include <filesystem>

namespace vigil::detection {
namespace {

// Flag bits for the boolean PlayerFrame fields ("f").
enum : std::uint32_t {
    kOnGround = 1u << 0, kSprinting = 1u << 1, kSneaking = 1u << 2, kUsingItem = 1u << 3, kBlocking = 1u << 4,
    kInvisible = 1u << 5, kRiding = 1u << 6, kInWater = 1u << 7, kInLava = 1u << 8, kOnLadder = 1u << 9,
    kInWeb = 1u << 10, kDead = 1u << 11, kSwinging = 1u << 12, kPotionsKnown = 1u << 13, kParticles = 1u << 14,
    kSprintModifier = 1u << 15, kBurning = 1u << 16, kTabKnown = 1u << 17, kTabListed = 1u << 18, kWallNear = 1u << 19,
    kServerPos = 1u << 20, kLocal = 1u << 21, kTeammate = 1u << 22,
};

std::uint32_t flagsOf(const PlayerFrame& p) {
    std::uint32_t f = 0;
    auto set = [&](bool b, std::uint32_t bit) { if (b) f |= bit; };
    set(p.onGround, kOnGround); set(p.sprinting, kSprinting); set(p.sneaking, kSneaking); set(p.usingItem, kUsingItem);
    set(p.blocking, kBlocking); set(p.invisible, kInvisible); set(p.riding, kRiding); set(p.inWater, kInWater);
    set(p.inLava, kInLava); set(p.onLadder, kOnLadder); set(p.inWeb, kInWeb); set(p.dead, kDead);
    set(p.swingInProgress, kSwinging); set(p.potionsKnown, kPotionsKnown); set(p.potionParticles, kParticles);
    set(p.sprintModifier, kSprintModifier); set(p.burning, kBurning); set(p.tabKnown, kTabKnown);
    set(p.tabListed, kTabListed); set(p.wallNear, kWallNear); set(p.serverPosition, kServerPos);
    set(p.isLocal, kLocal); set(p.isTeammate, kTeammate);
    return f;
}

void applyFlags(PlayerFrame& p, std::uint32_t f) {
    p.onGround = f & kOnGround; p.sprinting = f & kSprinting; p.sneaking = f & kSneaking; p.usingItem = f & kUsingItem;
    p.blocking = f & kBlocking; p.invisible = f & kInvisible; p.riding = f & kRiding; p.inWater = f & kInWater;
    p.inLava = f & kInLava; p.onLadder = f & kOnLadder; p.inWeb = f & kInWeb; p.dead = f & kDead;
    p.swingInProgress = f & kSwinging; p.potionsKnown = f & kPotionsKnown; p.potionParticles = f & kParticles;
    p.sprintModifier = f & kSprintModifier; p.burning = f & kBurning; p.tabKnown = f & kTabKnown;
    p.tabListed = f & kTabListed; p.wallNear = f & kWallNear; p.serverPosition = f & kServerPos;
    p.isLocal = f & kLocal; p.isTeammate = f & kTeammate;
}

json::Value playerJson(const PlayerFrame& p) {
    const PlayerFrame d;   // defaults are omitted
    json::Value j = json::Value::object();
    j["e"] = json::Value(p.entityId);
    if (!p.uuid.empty()) j["u"] = json::Value(p.uuid);
    if (!p.name.empty()) j["n"] = json::Value(p.name);
    json::Value pos = json::Value::array();
    pos.push(json::Value(p.pos.x));
    pos.push(json::Value(p.pos.y));
    pos.push(json::Value(p.pos.z));
    j["p"] = pos;
    j["yw"] = json::Value(static_cast<double>(p.yaw));
    j["pt"] = json::Value(static_cast<double>(p.pitch));
    j["f"] = json::Value(static_cast<double>(flagsOf(p)));
    if (p.heldItem != d.heldItem) j["h"] = json::Value(p.heldItem);
    if (p.swingProgress != d.swingProgress) j["sw"] = json::Value(p.swingProgress);
    if (p.hurtTime != d.hurtTime) j["hu"] = json::Value(p.hurtTime);
    if (p.health != d.health) j["hp"] = json::Value(static_cast<double>(p.health));
    if (p.potions.speed != -1 || p.potions.slowness != -1 || p.potions.jumpBoost != -1 || p.potions.count != 0) {
        json::Value po = json::Value::array();
        po.push(json::Value(p.potions.speed));
        po.push(json::Value(p.potions.slowness));
        po.push(json::Value(p.potions.jumpBoost));
        po.push(json::Value(p.potions.count));
        j["po"] = po;
    }
    if (p.moveSpeedAttribute != d.moveSpeedAttribute) j["ms"] = json::Value(p.moveSpeedAttribute);
    if (p.blockBelow != d.blockBelow) j["bb"] = json::Value(p.blockBelow);
    if (p.blockAboveHead != d.blockAboveHead) j["ba"] = json::Value(p.blockAboveHead);
    if (p.wallMask) j["wm"] = json::Value(static_cast<int>(p.wallMask));
    if (p.pingMs != d.pingMs) j["pg"] = json::Value(p.pingMs);
    return j;
}

bool playerFrom(const json::Value& j, PlayerFrame& p) {
    if (!j.isObject()) return false;
    p = PlayerFrame{};
    p.entityId = static_cast<int>(j.get("e").asNumber(-1));
    p.uuid = j.get("u").asString();
    p.name = j.get("n").asString();
    const json::Value& pos = j.get("p");
    if (!pos.isArray() || pos.size() != 3) return false;
    p.pos = {pos.at(0).asNumber(), pos.at(1).asNumber(), pos.at(2).asNumber()};
    p.yaw = static_cast<float>(j.get("yw").asNumber());
    p.pitch = static_cast<float>(j.get("pt").asNumber());
    applyFlags(p, static_cast<std::uint32_t>(j.get("f").asNumber()));
    p.heldItem = static_cast<int>(j.get("h").asNumber(-1));
    p.swingProgress = static_cast<int>(j.get("sw").asNumber(0));
    p.hurtTime = static_cast<int>(j.get("hu").asNumber(0));
    p.health = static_cast<float>(j.get("hp").asNumber(-1));
    if (const json::Value* po = j.find("po"); po && po->isArray() && po->size() == 4) {
        p.potions.speed = static_cast<int>(po->at(0).asNumber(-1));
        p.potions.slowness = static_cast<int>(po->at(1).asNumber(-1));
        p.potions.jumpBoost = static_cast<int>(po->at(2).asNumber(-1));
        p.potions.count = static_cast<int>(po->at(3).asNumber(0));
    }
    p.moveSpeedAttribute = j.get("ms").asNumber(-1.0);
    p.blockBelow = static_cast<int>(j.get("bb").asNumber(-1));
    p.blockAboveHead = static_cast<int>(j.get("ba").asNumber(-1));
    p.wallMask = static_cast<std::uint8_t>(j.get("wm").asNumber(0));
    p.pingMs = static_cast<int>(j.get("pg").asNumber(-1));
    return true;
}

}  // namespace

std::string traceHeaderLine(const TraceHeader& h) {
    json::Value j = json::Value::object();
    j["kind"] = json::Value("vigil-trace");
    j["format"] = json::Value(h.format);
    j["vigil"] = json::Value(h.vigil);
    j["build"] = json::Value(h.build);
    j["started"] = json::Value(h.startedEpochMs);
    if (!h.note.empty()) j["note"] = json::Value(h.note);
    return json::write(j, -1);
}

bool parseTraceHeader(const std::string& line, TraceHeader& out, std::string& error) {
    json::Value j;
    if (!json::parse(line, j, &error) || j.get("kind").asString() != "vigil-trace") {
        if (error.empty()) error = "not a Vigil trace";
        return false;
    }
    out.format = static_cast<int>(j.get("format").asNumber(0));
    if (out.format < 1 || out.format > TraceHeader::kFormat) {
        error = "unsupported trace format " + std::to_string(out.format);
        return false;
    }
    out.vigil = j.get("vigil").asString();
    out.build = j.get("build").asString();
    out.startedEpochMs = j.get("started").asNumber();
    out.note = j.get("note").asString();
    return true;
}

std::string traceFrameLine(const WorldFrame& f) {
    json::Value j = json::Value::object();
    j["t"] = json::Value(static_cast<double>(f.tick));
    j["ms"] = json::Value(f.timeMs);
    if (f.skippedTicks) j["sk"] = json::Value(f.skippedTicks);
    j["c"] = json::Value(static_cast<int>(f.context));
    if (!f.contextDetail.empty()) j["cd"] = json::Value(f.contextDetail);
    if (f.worldChanged) j["wc"] = json::Value(true);
    if (!f.chat.empty()) {
        json::Value chat = json::Value::array();
        for (const auto& c : f.chat) chat.push(json::Value(c));
        j["chat"] = chat;
    }
    json::Value players = json::Value::array();
    for (const auto& p : f.players) players.push(playerJson(p));
    j["pl"] = players;
    return json::write(j, -1);
}

bool parseTraceFrame(const std::string& line, WorldFrame& out, std::string& error) {
    json::Value j;
    if (!json::parse(line, j, &error) || !j.isObject()) {
        if (error.empty()) error = "malformed frame";
        return false;
    }
    out = WorldFrame{};
    out.tick = static_cast<Tick>(j.get("t").asNumber());
    out.timeMs = j.get("ms").asNumber();
    out.skippedTicks = static_cast<int>(j.get("sk").asNumber(0));
    const int ctx = static_cast<int>(j.get("c").asNumber(0));
    out.context = ctx >= 0 && ctx <= static_cast<int>(GameContext::Replay) ? static_cast<GameContext>(ctx) : GameContext::Unknown;
    out.contextDetail = j.get("cd").asString();
    out.worldChanged = j.get("wc").asBool();
    for (const auto& c : j.get("chat").items()) out.chat.push_back(c.asString());
    for (const auto& pj : j.get("pl").items()) {
        PlayerFrame p;
        if (!playerFrom(pj, p)) { error = "malformed player"; return false; }
        out.players.push_back(std::move(p));
    }
    return true;
}

bool readTrace(const std::string& path, TraceHeader& header, std::vector<WorldFrame>& frames, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "cannot open " + path; return false; }
    std::string line;
    if (!std::getline(in, line)) { error = "empty trace"; return false; }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!parseTraceHeader(line, header, error)) return false;
    int n = 1;
    while (std::getline(in, line)) {
        ++n;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        WorldFrame f;
        if (!parseTraceFrame(line, f, error)) {
            error = "line " + std::to_string(n) + ": " + error;
            return false;
        }
        frames.push_back(std::move(f));
    }
    return true;
}

bool TraceWriter::start(const std::string& path, const TraceHeader& header, std::uint64_t maxBytes, int maxFrames) {
    stop();
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    out_.open(path, std::ios::binary | std::ios::trunc);
    if (!out_) return false;
    path_ = path;
    maxBytes_ = maxBytes;
    maxFrames_ = maxFrames;
    bytes_ = 0;
    frames_ = 0;
    stopReason_.clear();
    const std::string h = traceHeaderLine(header) + "\n";
    out_ << h;
    bytes_ += h.size();
    active_ = true;
    return true;
}

void TraceWriter::write(const WorldFrame& f) {
    if (!active_) return;
    const std::string line = traceFrameLine(f) + "\n";
    out_ << line;
    bytes_ += line.size();
    ++frames_;
    if (bytes_ >= maxBytes_) stop("size limit reached");
    else if (frames_ >= maxFrames_) stop("time limit reached");
}

void TraceWriter::stop(const std::string& reason) {
    if (!active_) return;
    out_.flush();
    out_.close();
    active_ = false;
    stopReason_ = reason;
}

}  // namespace vigil::detection

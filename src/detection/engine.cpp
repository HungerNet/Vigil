#include "engine.h"

#include "../core/json.h"
#include "../core/log.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace vigil::detection {

Engine::Engine() = default;
Engine::~Engine() = default;

void Engine::registerGlobalSettings() {
    auto& S = Settings::instance();
    auto group = [](Setting& s, const char* g, bool adv = false) -> Setting& { s.group = g; s.advanced = adv; return s; };

    monitoring_ = &group(S.addBool("general.monitoring", "Monitoring", true,
                                   "Master switch. When off, Vigil observes nothing and all checks are idle."), "Monitoring");
    runInGame_ = &group(S.addBool("general.runIn.game", "In games", true,
                                  "Analyze players during matches (Bed Wars, Duels, SkyWars and similar)."), "Where checks run");
    runReplay_ = &group(S.addBool("general.runIn.replay", "Replays & Atlas", true,
                                  "Analyze players while watching Hypixel replays or reviewing Atlas reports."), "Where checks run");
    runPregame_ = &group(S.addBool("general.runIn.pregame", "Pre-game queues", false,
                                   "Analyze players while waiting for a match to start. They are always listed under "
                                   "This game; this decides whether checks judge them before the match."),
                         "Where checks run");
    runLobby_ = &group(S.addBool("general.runIn.lobby", "Main lobbies", false,
                                 "Analyze players in main lobbies. Lobbies contain parkour, launch pads and other "
                                 "mechanics that resemble cheats."), "Where checks run");
    runLobby_->warning = "Lobby mechanics (launch pads, parkour, double jump) produce movement that resembles cheats.";
    runLobby_->warnWhen = [](const Setting& s) { return s.b(); };
    analyzeSelf_ = &group(S.addBool("general.analyzeSelf", "Analyze yourself", false,
                                    "Runs every check on your own player. Use it to calibrate thresholds: anything Vigil "
                                    "reports about you while playing legitimately is a false positive."), "Calibration");

    ignoreTeammates_ = &group(S.addBool("exempt.teammates", "Ignore teammates", true,
                                        "Skip players on your team (resolved from scoreboard team colors)."), "Exemptions");
    kbTicks_ = &group(S.addInt("exempt.knockbackTicks", "Knockback grace", 30, {0, 100, 1, 0, "ticks"},
                               "After a player takes damage, movement checks ignore them for this long. Fireballs and TNT "
                               "can launch players far beyond normal speed."), "Exemptions");
    kbTicks_->warning = "A short grace lets fireball/TNT knockback count as speed.";
    kbTicks_->warnWhen = [](const Setting& s) { return s.i() < 15; };
    teleportTicks_ = &group(S.addInt("exempt.teleportTicks", "Teleport grace", 20, {0, 100, 1, 0, "ticks"},
                                     "Movement checks ignore a player for this long after a teleport (respawn, pearl, server correction)."),
                            "Exemptions", true);
    iceTicks_ = &group(S.addInt("exempt.iceTicks", "Ice grace", 40, {0, 200, 1, 0, "ticks"},
                                "Speed-related checks ignore a player for this long after touching ice or packed ice."),
                       "Exemptions", true);
    freshTicks_ = &group(S.addInt("exempt.freshTicks", "Warm-up", 20, {0, 100, 1, 0, "ticks"},
                                  "Players are observed for this long before any check judges them."), "Exemptions", true);

    swingWindow_ = &group(S.addInt("combat.swingWindow", "Swing → hurt window", 2, {0, 6, 1, 0, "ticks"},
                                   "How many ticks may separate an attacker's swing from the victim's hurt animation for "
                                   "the hit to be attributed."), "Hit attribution", true);
    settleTicks_ = &group(S.addInt("combat.settleTicks", "Settle delay", 3, {0, 10, 1, 0, "ticks"},
                                   "Hits are evaluated after this delay so late position and rotation packets are included."),
                          "Hit attribution", true);
    compTicks_ = &group(S.addInt("combat.compensationTicks", "Latency compensation", 5, {0, 12, 1, 0, "ticks"},
                                 "How far back the victim's positions are considered when measuring reach and aim. "
                                 "Covers the attacker's ping and entity interpolation."), "Hit attribution", true);
    compTicks_->warning = "Low compensation measures against positions the attacker may never have seen; laggy players will look like they reach.";
    compTicks_->warnWhen = [](const Setting& s) { return s.i() < 3; };
    combatRange_ = &group(S.addFloat("combat.range", "Candidate range", 6.0, {3.5, 10.0, 0.5, 1, "blocks"},
                                     "Swinging players farther than this from the victim are not considered as the attacker."),
                          "Hit attribution", true);

    journal_ = &group(S.addBool("data.journal", "Session journal", true,
                                "Write every detection and its evidence to sessions/ as JSON lines."), "Data");
    history_ = &group(S.addBool("data.history", "Player history", true,
                                "Remember players with evidence across sessions (name, sessions, detections, peak "
                                "suspicion) in history/players.json."), "Data");
}

void Engine::initialize() {
    registerGlobalSettings();
    checks_ = createAllChecks();
    for (std::size_t i = 0; i < checks_.size(); ++i) {
        checks_[i]->setIndex(static_cast<int>(i));
        checks_[i]->registerAll();
    }
    session_.reset(0.0, checks_.size());
}

Check* Engine::checkById(const std::string& id) const {
    for (const auto& c : checks_) if (c->id() == id) return c.get();
    return nullptr;
}

bool Engine::monitoringEnabled() const { return monitoring_ && monitoring_->b(); }

// Where checks judge players.
bool Engine::phaseAnalyzed(GamePhase p) const {
    switch (p) {
        case GamePhase::Match: return runInGame_->b();
        case GamePhase::Replay: return runReplay_->b();
        case GamePhase::Queue: return runPregame_->b();
        case GamePhase::Lobby: return runLobby_->b();
        // Post-game: winners celebrate, spectators fly, fireworks launch players. Never judged.
        default: return false;
    }
}

// Where players are recorded (listed, remembered, briefed) even when nobody is judged.
bool Engine::phaseRecorded(GamePhase p) const {
    switch (p) {
        case GamePhase::Queue:
        case GamePhase::Match:
        case GamePhase::PostGame:
        case GamePhase::Replay: return true;
        case GamePhase::Lobby: return runLobby_->b();   // main lobbies are crowds, not opponents
        default: return false;
    }
}

void Engine::onWorldChanged() {
    tracked_.clear();
    combat_.reset();
    recentCombat_.clear();
    filtered_.clear();
    localId_ = -1;
    noUpdateRun_ = 0;
    // A stall window belongs to the world it was measured in (PLAN.md §7.5).
    stallUntil_ = -1;
    match_.onWorldChanged();
    for (PlayerRecord& r : session_.players()) {
        if (r.present) {
            r.present = false;
            r.timeline.push_back({TimelineKind::Left, timeMs_, tick_, -1, 0, 0.f, "Left view (world changed)"});
        }
    }
    session_.bump();
}

void Engine::resetSession(double nowMs) {
    flushHistory();
    session_.reset(nowMs, checks_.size());
    tracked_.clear();
    combat_.reset();
    alerts_.clear();
    debugLog_.clear();
    recentCombat_.clear();
    stats_ = EngineStats{};
    relayFloor_ = 0;
}

void Engine::flushHistory() {
    if (history_ && history_->b() && !historyPath_.empty()) {
        std::vector<std::string> ids;
        for (const auto& c : checks_) ids.push_back(c->id());
        session_.saveHistory(historyPath_, ids);
    }
}

PlayerTrack* Engine::track(int entityId) const {
    auto it = tracked_.find(entityId);
    return it == tracked_.end() ? nullptr : const_cast<PlayerTrack*>(&it->second.track);
}

const PlayerTrack* Engine::localTrack() const { return localId_ >= 0 ? track(localId_) : nullptr; }

const Exemptions& Engine::exemptions(const PlayerTrack& t) const {
    static const Exemptions none;
    auto it = tracked_.find(t.entityId());
    return it == tracked_.end() ? none : it->second.ex;
}

void Engine::computeExemptions(Tracked& t) {
    const PlayerTrack& tr = t.track;
    const PlayerFrame& f = tr.cur();
    Exemptions& e = t.ex;
    e.knockback = tick_ - tr.lastHurtTick() <= kbTicks_->i();
    e.teleport = tick_ - tr.lastTeleportTick() <= teleportTicks_->i();
    e.riding = tick_ - tr.lastRideTick() <= 10;
    e.liquid = tick_ - tr.lastLiquidTick() <= 10;
    e.climbing = tick_ - tr.lastClimbTick() <= 6;
    e.web = tick_ - tr.lastWebTick() <= 6;
    e.ice = tick_ - tr.lastIceTick() <= iceTicks_->i();
    e.slime = tick_ - tr.lastSlimeTick() <= 20;
    e.fresh = tick_ - tr.firstTick() < freshTicks_->i();
    e.dead = f.dead || f.health == 0.f;
    e.stall = tick_ < stallUntil_;
    // Sustained airborne without descending → flight (spectator/creative/server flight). Invisible
    // players are usually spectators waiting to respawn, so they qualify after half a second; a
    // visible player must hover for two seconds (a jump lasts 12 ticks).
    e.flying = false;
    const int flightTicks = f.invisible ? 10 : 40;
    const int lookBack = f.invisible ? 10 : 20;
    if (tr.airborne() && tr.airTicks() > flightTicks && tr.size() > lookBack) {
        e.flying = tr.back(0).pos.y >= tr.back(lookBack).pos.y - (f.invisible ? 0.3 : 0.5);
    }
}

// World-level stall: when nearly every moving player stops receiving positions at once and then
// jumps, the server (or our connection) stalled. Everyone looks fast for a moment; nobody is.
void Engine::detectStall() {
    int movers = 0, updated = 0;
    for (auto& kv : tracked_) {
        const Tracked& t = kv.second;
        if (t.lastSeen != tick_ || !t.isPlayer() || t.track.cur().isLocal || t.track.size() < 14) continue;
        if (t.track.horizontalSpeed(12) < 0.06) continue;
        ++movers;
        if (t.track.positionUpdated()) ++updated;
    }
    if (movers < 2) {
        noUpdateRun_ = 0;
        return;
    }
    if (updated == 0) {
        ++noUpdateRun_;
    } else {
        if (noUpdateRun_ >= 4) {
            stallUntil_ = std::max(stallUntil_, tick_ + 25);
            ++stats_.stalls;
        }
        noUpdateRun_ = 0;
    }
}

void Engine::freshStates(Tracked& t) {
    t.states.clear();
    t.states.resize(checks_.size());
    for (std::size_t i = 0; i < checks_.size(); ++i) t.states[i] = checks_[i]->createState();
}

// Who is a player: NPC rules per entity, then duplicates (one person, two entities) and name
// collisions (an NPC wearing a listed player's name).
void Engine::classifyAll(const std::string& localName) {
    const bool replay = context_ == GameContext::Replay;
    for (auto& kv : tracked_) {
        Tracked& t = kv.second;
        if (t.lastSeen != tick_) continue;
        const PlayerFrame& f = t.track.cur();
        t.verdict = classifyEntity(f, t.obs, localName, replay);
        t.pending = !replay && !f.isLocal && f.tabKnown && !f.tabListed && t.obs.ticksUnlisted < kUnlistedGraceTicks &&
                    t.verdict.kind == EntityKind::Player;
    }
    // Same UUID on several entities: the one updated most recently is the player.
    std::unordered_map<std::string, Tracked*> byUuid;
    for (auto& kv : tracked_) {
        Tracked& t = kv.second;
        if (t.lastSeen != tick_ || !t.isPlayer() || t.track.uuid().empty()) continue;
        auto [it, inserted] = byUuid.emplace(t.track.uuid(), &t);
        if (inserted) continue;
        Tracked* other = it->second;
        const bool newer = t.track.updateTickAt(0) > other->track.updateTickAt(0) ||
                           (t.track.updateTickAt(0) == other->track.updateTickAt(0) && t.obs.ticksTracked > other->obs.ticksTracked);
        Tracked* loser = newer ? other : &t;
        loser->verdict = {EntityKind::Duplicate, "Second entity for the same player"};
        it->second = newer ? &t : other;
    }
    // Same name, different UUIDs: a listed player beats an unlisted entity wearing their name.
    std::unordered_map<std::string, std::vector<Tracked*>> byName;
    for (auto& kv : tracked_) {
        Tracked& t = kv.second;
        if (t.lastSeen != tick_ || !t.isPlayer()) continue;
        std::string n = t.track.name();
        for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        byName[n].push_back(&t);
    }
    for (auto& kv : byName) {
        auto& group = kv.second;
        if (group.size() < 2) {
            if (!group.empty()) {
                if (PlayerRecord* r = session_.find(group.front()->recordKey)) r->displaySuffix.clear();
            }
            continue;
        }
        bool anyListed = false;
        for (Tracked* t : group) if (t->track.cur().tabKnown && t->track.cur().tabListed) anyListed = true;
        for (Tracked* t : group) {
            const PlayerFrame& f = t->track.cur();
            if (anyListed && f.tabKnown && !f.tabListed) {
                t->verdict = {EntityKind::Npc, "Name collision with a listed player"};
            } else if (PlayerRecord* r = session_.find(t->recordKey)) {
                // Two genuine players with one name (rare): keep both, tell them apart.
                const std::string id = f.uuid.size() >= 4 ? f.uuid.substr(0, 4) : std::to_string(f.entityId);
                r->displaySuffix = " \xC2\xB7 " + id;
            }
        }
    }
    filtered_.clear();
    for (auto& kv : tracked_) {
        const Tracked& t = kv.second;
        if (t.lastSeen != tick_ || t.isPlayer() || t.pending) continue;
        filtered_.push_back({t.track.entityId(), t.track.name(), t.verdict.kind, t.verdict.reason});
    }
    stats_.filteredEntities = static_cast<int>(filtered_.size());
}

// Records of earlier scenes with nothing to show are dropped once the session grows large.
void Engine::pruneRecords() {
    if (session_.players().size() <= 400) return;
    const int scene = match_.scene();
    stats_.prunedRecords += session_.prune([&](const PlayerRecord& r) {
        return !r.present && !r.local && r.scene < scene - 1 && r.evidence.empty() && r.detections.empty() &&
               !marks_.find(r.key);
    });
}

bool Engine::shouldAnalyze(const Tracked& t) const {
    const PlayerFrame& f = t.track.cur();
    if (f.isLocal && !analyzeSelf_->b()) return false;
    if (f.isTeammate && ignoreTeammates_->b() && context_ != GameContext::Replay) return false;
    // Exempt by the user: listed, never judged (marks.h).
    if (!t.recordKey.empty() && marks_.exempt(t.recordKey)) return false;
    return true;
}

PlayerState Engine::playerState(const PlayerRecord& r) const {
    PlayerState s;
    s.listed = true;
    s.local = r.local;
    s.teammate = r.teammate;
    s.exempt = marks_.exempt(r.key);
    s.inThisGame = inThisGame(r);
    s.judged = !s.exempt && (!r.local || analyzeSelf_->b()) && (!r.teammate || !ignoreTeammates_->b() || context_ == GameContext::Replay);
    return s;
}

void Engine::refreshSuspicion(PlayerRecord& r) {
    float combined = 1.f;
    for (const CheckRecord& cr : r.checks) combined *= (1.f - cr.model.value() / 100.f);
    r.suspicion = 100.f * (1.f - combined);
}

PlayerRecord& Engine::recordFor(Tracked& t) {
    const PlayerFrame& f = t.track.cur();
    std::string key = !f.uuid.empty() ? f.uuid : ("name:" + f.name);
    t.recordKey = key;
    PlayerRecord& r = session_.record(key, f.uuid, f.name, timeMs_);
    const int scene = match_.scene();
    if (r.scene != scene) {
        // First sight in this scene. Confidence from an earlier game is relevant but must not let
        // this game's first observations complete a detection: it is halved and the evidence gate
        // starts empty.
        // A queue and the match it became are one game.
        const bool sameGame = r.scene >= 0 && r.scene == match_.queueScene() && scene == match_.matchScene();
        if (r.scene >= 0) {
            bool carried = false;
            for (CheckRecord& cr : r.checks) {
                if (cr.model.value() > 0.f) carried = true;
                cr.model.resume(timeMs_, sameGame ? 1.f : 0.5f);
            }
            refreshSuspicion(r);
            if (carried && !sameGame) {
                r.timeline.push_back({TimelineKind::Carried, timeMs_, tick_, -1, 0, r.suspicion,
                                      "Carried over from an earlier game (halved)"});
            }
        }
        r.scene = scene;
        r.participant = false;
        ++r.scenesSeen;
        // A player the user added by name before Vigil saw them: the mark now follows this account.
        marks_.bindName(r.key, r.name, r.uuid);
        marks_.rememberName(r.key, r.name);
    }
    if (!r.present) {
        r.present = true;
        r.timeline.push_back({TimelineKind::Seen, timeMs_, tick_, -1, 0, 0.f, "Entered view"});
        // Nothing was observed while away: the decay clock resumes now (no charge for the absence).
        for (CheckRecord& cr : r.checks) cr.model.resume(timeMs_);
    }
    const GamePhase ph = match_.phase();
    if (ph == GamePhase::Queue || ph == GamePhase::Match || ph == GamePhase::PostGame) {
        r.participant = true;
        if (ph != GamePhase::Queue) r.matchScene = scene;
    }
    r.entityId = f.entityId;
    r.local = f.isLocal;
    r.teammate = f.isTeammate;
    r.lastSeenMs = timeMs_;
    return r;
}

void Engine::updateRelayFloor(const PlayerTrack& t) {
    const auto& sw = t.swingTicks();
    if (sw.size() < 2 || !t.swingStarted()) return;
    const int interval = static_cast<int>(sw.back() - sw[sw.size() - 2]);
    if (interval <= 0) return;
    if (relayFloor_ == 0 || interval < relayFloor_) relayFloor_ = interval;
}

void Engine::updateStats(Tracked& t, PlayerRecord& r) {
    const PlayerTrack& tr = t.track;
    BehaviorStats& s = r.stats;
    ++s.ticksObserved;
    if (tick_ - tr.lastSwingTick() < 40 || tick_ - tr.lastHurtTick() < 40) ++s.ticksInCombat;
    if (tr.swingStarted()) {
        ++s.swings;
        const auto& sw = tr.swingTicks();
        int count = 0;
        for (auto it = sw.rbegin(); it != sw.rend() && tick_ - *it < 20; ++it) ++count;
        s.cpsRecent = count;  // swings in the last second
        s.cpsPeak = std::max(s.cpsPeak, static_cast<double>(count));
    }
    if (tr.landed()) {
        const AirSegment& seg = tr.airSegments().back();
        if (seg.jumped) {
            ++s.jumps;
            s.airTicksSum += seg.airTicks;
        }
    }
    if (tick_ % 10 == 0 && tr.size() > 10 && !t.ex.movement()) {
        const double sp = tr.horizontalSpeed(10);
        s.speedPeak = std::max(s.speedPeak, sp);
        s.speedSum += sp;
        ++s.speedSamples;
        s.speedRecent.push_back(static_cast<float>(sp));
        while (s.speedRecent.size() > 120) s.speedRecent.pop_front();
    }
}

// `observing`: checks are judging this player, so time without evidence is evidence of normal
// play and confidence decays. While not observing (queue, post-game, paused checks) it holds.
void Engine::sampleHistory(PlayerRecord& r, bool observing) {
    auto push = [](std::vector<ConfidencePoint>& h, double t, float v) {
        h.push_back({t, v});
        if (h.size() > 720) {
            // Keep the whole session visible: halve resolution of the older half.
            std::vector<ConfidencePoint> compact;
            compact.reserve(h.size());
            for (std::size_t i = 0; i < h.size(); ++i) {
                if (i < h.size() / 2 && (i % 2) == 1) continue;
                compact.push_back(h[i]);
            }
            h.swap(compact);
        }
    };
    float combined = 1.f;
    for (std::size_t i = 0; i < r.checks.size(); ++i) {
        CheckRecord& cr = r.checks[i];
        if (observing) cr.model.decayTo(timeMs_, checks_[i]->confidenceParams().halfLifeSec);
        else cr.model.resume(timeMs_);
        const float v = cr.model.value();
        combined *= (1.f - v / 100.f);
        if (v > 0.f || cr.lastSampled > 0.f) {
            if (std::fabs(v - cr.lastSampled) > 0.2f || tick_ % 40 == 0) {
                push(cr.history, timeMs_, v);
                cr.lastSampled = v;
            }
        }
    }
    r.suspicion = 100.f * (1.f - combined);
    r.peakSuspicion = std::max(r.peakSuspicion, r.suspicion);
    if (r.suspicion > 0.f || !r.suspicionHistory.empty()) push(r.suspicionHistory, timeMs_, r.suspicion);
}

void Engine::onFrame(const WorldFrame& frame) {
    const auto t0 = std::chrono::steady_clock::now();
    tick_ = frame.tick;
    timeMs_ = frame.timeMs;
    if (session_.startMs() == 0.0) session_.reset(frame.timeMs, checks_.size());
    if (journalDir_.size() && journal_->b()) {
        char name[64];
        std::snprintf(name, sizeof(name), "/session-%llu.jsonl", static_cast<unsigned long long>(session_.startEpochMs()));
        session_.setJournal(journalDir_ + name);
    } else {
        session_.setJournal({});
    }

    if (frame.worldChanged) onWorldChanged();   // tests and the preview signal it in the frame
    if (frame.context != context_) {
        context_ = frame.context;
        session_.bump();
    }
    contextDetail_ = frame.contextDetail;
    const MatchTracker::Update phaseUpdate = match_.onFrame(frame.context, frame.chat, timeMs_);
    if (phaseUpdate.sceneChanged) {
        session_.bump();
        pruneRecords();
    }
    const GamePhase phase = match_.phase();
    recording_ = monitoringEnabled() && phaseRecorded(phase);
    active_ = monitoringEnabled() && phaseAnalyzed(phase);
    stats_.skippedTicks += frame.skippedTicks;
    // A long hitch on our side delivers several ticks of everyone's movement at once.
    if (frame.skippedTicks >= 5) stallUntil_ = std::max(stallUntil_, tick_ + 20);

    // Update tracks (every entity in the player list; classification decides who is a player).
    localId_ = -1;
    std::string localName;
    for (const PlayerFrame& pf : frame.players) {
        if (pf.entityId < 0) continue;
        if (pf.isLocal) localName = pf.name;
        Tracked& t = tracked_[pf.entityId];
        if (t.states.empty()) freshStates(t);
        // Entity id reuse with a different identity → start fresh.
        if (!t.track.empty() && !pf.uuid.empty() && t.track.cur().uuid != pf.uuid) {
            t = Tracked{};
            freshStates(t);
        }
        if (t.track.empty()) t.firstPos = pf.pos;
        t.track.push(pf, tick_, timeMs_);
        t.lastSeen = tick_;
        ++t.obs.ticksTracked;
        if (pf.tabKnown) t.obs.ticksUnlisted = pf.tabListed ? 0 : t.obs.ticksUnlisted + 1;
        t.obs.maxDisplacement = std::max(t.obs.maxDisplacement, (pf.pos - t.firstPos).length());
        if (pf.invisible) t.obs.everInvisible = true;
        if (pf.isLocal) localId_ = pf.entityId;
    }
    // Drop entities no longer in the world.
    for (auto it = tracked_.begin(); it != tracked_.end();) {
        if (tick_ - it->second.lastSeen > 40) {
            if (PlayerRecord* r = session_.find(it->second.recordKey)) {
                if (r->present) {
                    r->present = false;
                    r->timeline.push_back({TimelineKind::Left, timeMs_, tick_, -1, 0, 0.f, "Left view"});
                    session_.bump();
                }
            }
            it = tracked_.erase(it);
        } else {
            ++it;
        }
    }
    classifyAll(localName);
    // An entity reclassified as a non-player (e.g. a name collision) leaves the roster.
    for (auto& kv : tracked_) {
        Tracked& t = kv.second;
        if (t.lastSeen != tick_ || t.isPlayer() || t.recordKey.empty()) continue;
        if (PlayerRecord* r = session_.find(t.recordKey)) {
            if (r->present && r->entityId == t.track.entityId()) {
                r->present = false;
                session_.bump();
            }
        }
    }
    int players = 0;
    for (const auto& kv : tracked_) if (kv.second.lastSeen == tick_ && kv.second.isPlayer()) ++players;
    stats_.trackedPlayers = players;

    if (!recording_) {
        // Tracks keep updating (cheap, keeps history warm) but nothing is judged or recorded.
        recentCombat_.clear();
        return;
    }

    detectStall();
    std::vector<PlayerTrack*> all;
    all.reserve(tracked_.size());
    for (auto& kv : tracked_) {
        if (kv.second.lastSeen != tick_ || !kv.second.isPlayer()) continue;
        computeExemptions(kv.second);
        updateRelayFloor(kv.second.track);
        all.push_back(&kv.second.track);
    }
    for (auto& kv : tracked_) {
        if (kv.second.lastSeen != tick_ || !kv.second.isPlayer()) continue;
        double best = 1e9;
        const Vec3 p = kv.second.track.cur().pos;
        for (const PlayerTrack* o : all) {
            if (o == &kv.second.track) continue;
            best = std::min(best, (o->cur().pos - p).length());
        }
        kv.second.ex.nearestOpponent = best;
    }

    // Records, then checks (only where the phase allows judging).
    const PlayerTrack* local = localTrack();
    for (auto& kv : tracked_) {
        Tracked& t = kv.second;
        if (t.lastSeen != tick_ || !t.isPlayer()) continue;
        PlayerRecord& rec = recordFor(t);
        updateStats(t, rec);
        const bool judged = active_ && shouldAnalyze(t);
        if (judged) {
            for (std::size_t i = 0; i < checks_.size(); ++i) {
                Check* c = checks_[i].get();
                if (!c->enabled()) continue;
                if (c->samplingOn() && c->supportsTickSampling()) {
                    if (((tick_ + t.track.entityId()) % c->samplingInterval()) != 0) continue;
                    if (local && (t.track.cur().pos - local->cur().pos).length() > c->samplingRadius()) continue;
                }
                current_ = c;
                c->onTick(*this, t.track, t.states[i].get());
            }
            current_ = nullptr;
        }
        if (tick_ % 10 == 0) sampleHistory(rec, judged);
    }

    // Combat events (players only: an NPC never attacks, and hits on NPCs are not evidence).
    CombatSettings cs;
    cs.swingWindowTicks = swingWindow_->i();
    cs.settleTicks = settleTicks_->i();
    cs.compensationTicks = compTicks_->i();
    cs.candidateRange = combatRange_->value;
    if (active_) combat_.update(tick_, timeMs_, all, cs);
    else recentCombat_.clear();
    for (const CombatEvent& ev : active_ ? combat_.ready() : std::vector<CombatEvent>{}) {
        auto ia = tracked_.find(ev.attacker);
        auto iv = tracked_.find(ev.victim);
        if (ia == tracked_.end() || iv == tracked_.end()) continue;
        ++stats_.combatEvents;
        recentCombat_.push_back(ev);
        if (recentCombat_.size() > 64) recentCombat_.erase(recentCombat_.begin());
        PlayerRecord& ra = recordFor(ia->second);
        PlayerRecord& rv = recordFor(iv->second);
        ++ra.stats.hitsLanded;
        ++rv.stats.hitsTaken;
        if (ev.certainty >= 0.99f) {
            ra.stats.reachSum += ev.reach;
            ++ra.stats.reachSamples;
            ra.stats.reachMax = std::max(ra.stats.reachMax, ev.reach);
            ra.stats.aimOutsideSum += ev.aimOutside;
            ra.stats.reachRecent.push_back(static_cast<float>(ev.reach));
            while (ra.stats.reachRecent.size() > 120) ra.stats.reachRecent.pop_front();
        }
        ra.timeline.push_back({TimelineKind::Hit, ev.timeMs, ev.tick, -1, ev.id, static_cast<float>(ev.reach),
                               "Hit " + iv->second.track.name()});
        rv.timeline.push_back({TimelineKind::Hurt, ev.timeMs, ev.tick, -1, ev.id, static_cast<float>(ev.reach),
                               "Hit by " + ia->second.track.name()});
        const bool judgeAttacker = shouldAnalyze(ia->second);
        const bool judgeVictim = shouldAnalyze(iv->second);
        for (std::size_t i = 0; i < checks_.size(); ++i) {
            Check* c = checks_[i].get();
            if (!c->enabled()) continue;
            current_ = c;
            if (judgeAttacker) c->onCombat(*this, ev, ia->second.track, iv->second.track, ia->second.states[i].get());
            // Some checks (Velocity) judge the victim's response to the hit.
            if (judgeVictim) c->onHitTaken(*this, ev, ia->second.track, iv->second.track, iv->second.states[i].get());
        }
        current_ = nullptr;
    }

    // Bound timelines.
    for (PlayerRecord& r : session_.players()) {
        while (r.timeline.size() > 1500) r.timeline.pop_front();
    }

    if (timeMs_ - lastHistoryFlushMs_ > 60000.0) {
        lastHistoryFlushMs_ = timeMs_;
        flushHistory();
    }

    const double micros = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    ++stats_.ticksProcessed;
    stats_.avgTickMicros = stats_.avgTickMicros == 0.0 ? micros : stats_.avgTickMicros * 0.98 + micros * 0.02;
    stats_.peakTickMicros = std::max(stats_.peakTickMicros * 0.999, micros);
}

void Engine::emit(const PlayerTrack& subject, int indicator, float severity, std::string summary, std::string detail,
                  std::vector<EvidenceValue> values, std::string target) {
    Check* c = current_;
    if (!c || !c->indicatorOn(indicator)) return;
    auto it = tracked_.find(subject.entityId());
    if (it == tracked_.end()) return;
    PlayerRecord& r = recordFor(it->second);
    CheckRecord& cr = r.checks[static_cast<std::size_t>(c->index())];
    if (cr.indicatorCounts.size() < c->indicators().size()) cr.indicatorCounts.resize(c->indicators().size());

    const ConfidenceParams params = c->confidenceParams();
    Evidence ev;
    ev.id = session_.nextEvidenceId();
    ev.tick = tick_;
    ev.timeMs = timeMs_;
    ev.check = c->index();
    ev.indicator = indicator;
    ev.weight = c->indicatorWeight(indicator);
    ev.severity = std::clamp(severity, 0.f, 1.f);
    ev.contribution = cr.model.add(timeMs_, ev.weight, ev.severity, indicator, params);
    ev.confidenceAfter = cr.model.value();
    ev.summary = std::move(summary);
    ev.detail = std::move(detail);
    ev.target = std::move(target);
    ev.values = std::move(values);
    ev.envelope = deriveEnvelope(ev.values);

    cr.evidence++;
    cr.indicatorCounts[static_cast<std::size_t>(indicator)]++;
    cr.lastEvidenceMs = timeMs_;
    cr.peak = std::max(cr.peak, cr.model.value());
    session_.countEvidence();
    r.timeline.push_back({TimelineKind::Evidence, timeMs_, tick_, c->index(), ev.id, ev.confidenceAfter, ev.summary});

    if (c->debug()) {
        debug(subject, fmt("evidence %s sev=%.2f +%.1f → %.1f%% | %s", c->indicators()[indicator].id.c_str(),
                           ev.severity, ev.contribution, ev.confidenceAfter, ev.summary.c_str()));
    }

    {
        json::Value j = json::Value::object();
        j["type"] = json::Value("evidence");
        j["time"] = json::Value(timeMs_);
        j["player"] = json::Value(r.name);
        j["uuid"] = json::Value(r.uuid);
        j["check"] = json::Value(c->id());
        j["indicator"] = json::Value(c->indicators()[indicator].id);
        j["severity"] = json::Value(static_cast<double>(ev.severity));
        j["confidence"] = json::Value(static_cast<double>(ev.confidenceAfter));
        j["summary"] = json::Value(ev.summary);
        json::Value& vals = j["values"];
        vals = json::Value::object();
        for (const auto& v : ev.values) vals[v.name] = json::Value(v.value);
        session_.journal(json::write(j, -1));
    }

    const std::uint64_t evId = ev.id;
    r.evidence.push_back(std::move(ev));
    while (r.evidence.size() > 600) r.evidence.pop_front();

    if (cr.model.canDetect(timeMs_, params)) {
        cr.model.markDetected(timeMs_);
        Detection d;
        d.id = session_.nextDetectionId();
        d.tick = tick_;
        d.timeMs = timeMs_;
        d.check = c->index();
        d.confidence = cr.model.value();
        cr.model.gate(timeMs_, params, d.evidenceCount, d.distinctIndicators);
        const double windowMs = params.windowSec * 1000.0;
        std::vector<std::string> reasons;
        for (auto e = r.evidence.rbegin(); e != r.evidence.rend(); ++e) {
            if (timeMs_ - e->timeMs > windowMs) break;
            if (e->check != c->index()) continue;
            d.evidence.push_back(e->id);
            if (reasons.size() < 4) reasons.push_back(e->summary);
        }
        d.summary = c->name() + " · " + fmt("%.0f%%", d.confidence) + " · " +
                    fmt("%d observations, %d indicator%s", d.evidenceCount, d.distinctIndicators,
                        d.distinctIndicators == 1 ? "" : "s");
        cr.detections++;
        session_.countDetection();
        r.detections.push_back(d);
        r.timeline.push_back({TimelineKind::Detection, timeMs_, tick_, c->index(), d.id, d.confidence, d.summary});

        Alert a;
        a.seq = ++alertSeq_;
        a.timeMs = timeMs_;
        a.playerKey = r.key;
        a.name = r.name;
        a.check = c->index();
        a.confidence = d.confidence;
        a.summary = reasons.empty() ? c->summary() : reasons.front();
        a.reasons = reasons;
        alerts_.push_back(a);
        while (alerts_.size() > 64) alerts_.pop_front();

        log::writef("detections", "DETECTION %s %s %.0f%% (%d evidence, %d indicators) — %s", r.name.c_str(),
                    c->id().c_str(), d.confidence, d.evidenceCount, d.distinctIndicators, a.summary.c_str());
        json::Value j = json::Value::object();
        j["type"] = json::Value("detection");
        j["time"] = json::Value(timeMs_);
        j["player"] = json::Value(r.name);
        j["uuid"] = json::Value(r.uuid);
        j["check"] = json::Value(c->id());
        j["confidence"] = json::Value(static_cast<double>(d.confidence));
        j["evidence"] = json::Value(d.evidenceCount);
        session_.journal(json::write(j, -1));
    }
    (void)evId;
    session_.bump();
}

bool Engine::debugEnabled() const { return current_ && current_->debug(); }

void Engine::debug(const PlayerTrack& subject, const std::string& line) {
    if (!current_ || !current_->debug()) return;
    DebugLine d;
    d.timeMs = timeMs_;
    d.tick = tick_;
    d.check = current_->index();
    d.player = subject.name();
    d.text = line;
    debugLog_.push_back(d);
    while (debugLog_.size() > 2000) debugLog_.pop_front();
    log::writef("detections", "[%s] %s: %s", current_->id().c_str(), subject.name().c_str(), line.c_str());
}

}  // namespace vigil::detection

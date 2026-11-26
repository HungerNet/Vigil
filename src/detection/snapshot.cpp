#include "snapshot.h"

#include "../core/json.h"
#include "core/version.h"
#include "engine.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace vigil::detection {
namespace {

// Bounds for files from other people (PLAN.md §13).
constexpr std::size_t kMaxEvidence = 600;
constexpr std::size_t kMaxDetections = 200;
constexpr std::size_t kMaxTimeline = 1500;
constexpr std::size_t kMaxHistory = 1440;
constexpr std::size_t kMaxValues = 16;
constexpr std::size_t kMaxString = 2000;

std::string clip(const std::string& s) { return s.size() > kMaxString ? s.substr(0, kMaxString) : s; }

int indexOf(const std::vector<std::string>& ids, const std::string& id) {
    for (std::size_t i = 0; i < ids.size(); ++i) if (ids[i] == id) return static_cast<int>(i);
    return -1;
}

json::Value points(const std::vector<ConfidencePoint>& pts) {
    json::Value a = json::Value::array();
    for (const auto& p : pts) {
        json::Value pair = json::Value::array();
        pair.push(json::Value(p.timeMs));
        pair.push(json::Value(static_cast<double>(p.value)));
        a.push(pair);
    }
    return a;
}

std::vector<ConfidencePoint> readPoints(const json::Value& a) {
    std::vector<ConfidencePoint> out;
    for (const auto& p : a.items()) {
        if (out.size() >= kMaxHistory) break;
        if (!p.isArray() || p.size() < 2) continue;
        out.push_back({p.at(0).asNumber(), static_cast<float>(std::clamp(p.at(1).asNumber(), 0.0, 100.0))});
    }
    return out;
}

const char* kindName(TimelineKind k) {
    switch (k) {
        case TimelineKind::Seen: return "seen";
        case TimelineKind::Left: return "left";
        case TimelineKind::Evidence: return "evidence";
        case TimelineKind::Detection: return "detection";
        case TimelineKind::Hit: return "hit";
        case TimelineKind::Hurt: return "hurt";
        case TimelineKind::Carried: return "carried";
    }
    return "seen";
}

bool kindFrom(const std::string& s, TimelineKind& k) {
    static const std::pair<const char*, TimelineKind> kinds[] = {
        {"seen", TimelineKind::Seen}, {"left", TimelineKind::Left}, {"evidence", TimelineKind::Evidence},
        {"detection", TimelineKind::Detection}, {"hit", TimelineKind::Hit}, {"hurt", TimelineKind::Hurt},
        {"carried", TimelineKind::Carried}};
    for (const auto& kv : kinds) if (s == kv.first) { k = kv.second; return true; }
    return false;
}

std::string safeStem(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') out.push_back(c);
    }
    if (out.empty()) out = "player";
    return out.substr(0, 24);
}

std::string stampOf(double epochMs) {
    const std::time_t t = static_cast<std::time_t>(epochMs / 1000.0);
    std::tm tmv{};
    localtime_s(&tmv, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tmv);
    return buf;
}

bool readFile(const std::string& path, std::string& out, std::string& error) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec) { error = "The file could not be read."; return false; }
    if (size > ProfileSnapshot::kMaxFileBytes) { error = "The file is too large to be a Vigil profile."; return false; }
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "The file could not be opened."; return false; }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool writeFile(const std::string& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out || !(out << text)) return false;
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(path, ec);
        fs::rename(tmp, path, ec);
    }
    return !ec;
}

}  // namespace

CheckCatalog CheckCatalog::of(const Engine& e) {
    CheckCatalog c;
    for (const auto& chk : e.checks()) {
        c.checks.push_back(chk->id());
        std::vector<std::string> inds;
        for (const auto& i : chk->indicators()) inds.push_back(i.id);
        c.indicators.push_back(std::move(inds));
    }
    return c;
}

ProfileSnapshot captureSnapshot(const Engine& e, const PlayerRecord& r) {
    ProfileSnapshot s;
    s.vigilVersion = VIGIL_VERSION_STRING;
    s.savedAtEpochMs = epochNowMs();
    s.sessionStartMs = e.session().startMs();
    s.capturedAtMs = e.lastTimeMs();
    s.record = r;
    s.record.present = false;
    if (const Mark* m = e.marks().find(r.key)) {
        s.watched = m->watch || m->confirmed;
        s.confirmed = m->confirmed;
        s.exempt = m->exempt;
        s.reasons = m->reasons;
    }
    return s;
}

std::string snapshotToJson(const ProfileSnapshot& s, const CheckCatalog& cat) {
    const PlayerRecord& r = s.record;
    auto checkId = [&](int i) { return i >= 0 && i < static_cast<int>(cat.checks.size()) ? cat.checks[static_cast<std::size_t>(i)] : std::string(); };
    auto indicatorId = [&](int c, int i) {
        if (c < 0 || c >= static_cast<int>(cat.indicators.size())) return std::string();
        const auto& v = cat.indicators[static_cast<std::size_t>(c)];
        return i >= 0 && i < static_cast<int>(v.size()) ? v[static_cast<std::size_t>(i)] : std::string();
    };
    json::Value root = json::Value::object();
    root["format"] = json::Value(ProfileSnapshot::kFormat);
    root["kind"] = json::Value("vigil-profile");
    root["vigil"] = json::Value(s.vigilVersion);
    root["savedAt"] = json::Value(s.savedAtEpochMs);
    root["sessionStart"] = json::Value(s.sessionStartMs);
    root["capturedAt"] = json::Value(s.capturedAtMs);
    root["origin"] = json::Value(s.origin);
    if (!s.note.empty()) root["note"] = json::Value(s.note);
    json::Value& marks = root["marks"];
    marks = json::Value::object();
    marks["watched"] = json::Value(s.watched);
    marks["confirmed"] = json::Value(s.confirmed);
    marks["exempt"] = json::Value(s.exempt);
    json::Value reasons = json::Value::array();
    for (const auto& x : s.reasons) reasons.push(json::Value(x));
    marks["reasons"] = reasons;
    json::Value& idn = root["identity"];
    idn = json::Value::object();
    idn["nicked"] = json::Value(s.nicked);
    if (!s.likelyAccount.empty()) idn["likelyAccount"] = json::Value(s.likelyAccount);
    json::Value cands = json::Value::array();
    for (const auto& x : s.candidates) cands.push(json::Value(x));
    idn["candidates"] = cands;

    json::Value& p = root["player"];
    p = json::Value::object();
    p["key"] = json::Value(r.key);
    p["uuid"] = json::Value(r.uuid);
    p["name"] = json::Value(r.name);
    p["local"] = json::Value(r.local);
    p["teammate"] = json::Value(r.teammate);
    p["firstSeen"] = json::Value(r.firstSeenMs);
    p["lastSeen"] = json::Value(r.lastSeenMs);
    p["suspicion"] = json::Value(static_cast<double>(r.suspicion));
    p["peakSuspicion"] = json::Value(static_cast<double>(r.peakSuspicion));
    p["priorSessions"] = json::Value(r.priorSessions);
    p["priorDetections"] = json::Value(r.priorDetections);
    p["priorPeak"] = json::Value(static_cast<double>(r.priorPeak));
    p["suspicionHistory"] = points(r.suspicionHistory);

    json::Value checks = json::Value::array();
    for (std::size_t i = 0; i < r.checks.size(); ++i) {
        const CheckRecord& cr = r.checks[i];
        if (cr.evidence == 0 && cr.detections == 0 && cr.peak <= 0.f) continue;
        json::Value c = json::Value::object();
        c["id"] = json::Value(checkId(static_cast<int>(i)));
        c["confidence"] = json::Value(static_cast<double>(cr.model.value()));
        c["peak"] = json::Value(static_cast<double>(cr.peak));
        c["evidence"] = json::Value(cr.evidence);
        c["detections"] = json::Value(cr.detections);
        json::Value counts = json::Value::object();
        for (std::size_t k = 0; k < cr.indicatorCounts.size(); ++k) {
            const std::string iid = indicatorId(static_cast<int>(i), static_cast<int>(k));
            if (!iid.empty() && cr.indicatorCounts[k] > 0) counts[iid] = json::Value(cr.indicatorCounts[k]);
        }
        c["indicators"] = counts;
        c["history"] = points(cr.history);
        checks.push(c);
    }
    root["checks"] = checks;

    json::Value ev = json::Value::array();
    for (const Evidence& e : r.evidence) {
        json::Value j = json::Value::object();
        j["id"] = json::Value(static_cast<double>(e.id));
        j["tick"] = json::Value(static_cast<double>(e.tick));
        j["time"] = json::Value(e.timeMs);
        j["check"] = json::Value(checkId(e.check));
        j["indicator"] = json::Value(indicatorId(e.check, e.indicator));
        j["weight"] = json::Value(static_cast<double>(e.weight));
        j["severity"] = json::Value(static_cast<double>(e.severity));
        j["contribution"] = json::Value(static_cast<double>(e.contribution));
        j["after"] = json::Value(static_cast<double>(e.confidenceAfter));
        j["summary"] = json::Value(e.summary);
        j["detail"] = json::Value(e.detail);
        if (!e.target.empty()) j["target"] = json::Value(e.target);
        json::Value vals = json::Value::array();
        for (const auto& v : e.values) {
            json::Value x = json::Value::object();
            x["name"] = json::Value(v.name);
            x["value"] = json::Value(v.value);
            if (!v.unit.empty()) x["unit"] = json::Value(v.unit);
            if (v.hasLimit) x["limit"] = json::Value(v.limit);
            if (v.role != EvidenceValue::Role::Measure) x["role"] = json::Value(v.role == EvidenceValue::Role::Expected ? "expected" : "context");
            vals.push(x);
        }
        j["values"] = vals;
        ev.push(j);
    }
    root["evidence"] = ev;

    json::Value dets = json::Value::array();
    for (const Detection& d : r.detections) {
        json::Value j = json::Value::object();
        j["id"] = json::Value(static_cast<double>(d.id));
        j["tick"] = json::Value(static_cast<double>(d.tick));
        j["time"] = json::Value(d.timeMs);
        j["check"] = json::Value(checkId(d.check));
        j["confidence"] = json::Value(static_cast<double>(d.confidence));
        j["observations"] = json::Value(d.evidenceCount);
        j["indicators"] = json::Value(d.distinctIndicators);
        json::Value ids = json::Value::array();
        for (auto id : d.evidence) ids.push(json::Value(static_cast<double>(id)));
        j["evidence"] = ids;
        j["summary"] = json::Value(d.summary);
        dets.push(j);
    }
    root["detections"] = dets;

    json::Value tl = json::Value::array();
    for (const TimelineEvent& t : r.timeline) {
        json::Value j = json::Value::object();
        j["kind"] = json::Value(kindName(t.kind));
        j["time"] = json::Value(t.timeMs);
        j["tick"] = json::Value(static_cast<double>(t.tick));
        if (t.check >= 0) j["check"] = json::Value(checkId(t.check));
        if (t.ref) j["ref"] = json::Value(static_cast<double>(t.ref));
        j["value"] = json::Value(static_cast<double>(t.value));
        j["text"] = json::Value(t.text);
        tl.push(j);
    }
    root["timeline"] = tl;

    const BehaviorStats& b = r.stats;
    json::Value st = json::Value::object();
    st["ticksObserved"] = json::Value(b.ticksObserved);
    st["ticksInCombat"] = json::Value(b.ticksInCombat);
    st["hitsLanded"] = json::Value(b.hitsLanded);
    st["hitsTaken"] = json::Value(b.hitsTaken);
    st["swings"] = json::Value(b.swings);
    st["reachSum"] = json::Value(b.reachSum);
    st["reachSamples"] = json::Value(b.reachSamples);
    st["reachMax"] = json::Value(b.reachMax);
    st["aimOutsideSum"] = json::Value(b.aimOutsideSum);
    st["cpsPeak"] = json::Value(b.cpsPeak);
    st["speedPeak"] = json::Value(b.speedPeak);
    st["speedSum"] = json::Value(b.speedSum);
    st["speedSamples"] = json::Value(b.speedSamples);
    st["jumps"] = json::Value(b.jumps);
    st["airTicksSum"] = json::Value(b.airTicksSum);
    json::Value rr = json::Value::array();
    for (float x : b.reachRecent) rr.push(json::Value(static_cast<double>(x)));
    st["reachRecent"] = rr;
    json::Value sr = json::Value::array();
    for (float x : b.speedRecent) sr.push(json::Value(static_cast<double>(x)));
    st["speedRecent"] = sr;
    root["stats"] = st;
    return json::write(root, 1);
}

bool snapshotFromJson(const std::string& text, const CheckCatalog& cat, ProfileSnapshot& out, std::string& error) {
    if (text.size() > ProfileSnapshot::kMaxFileBytes) { error = "The file is too large to be a Vigil profile."; return false; }
    json::Value root;
    std::string perr;
    if (!json::parse(text, root, &perr) || !root.isObject()) { error = "This is not a Vigil profile (" + perr + ")."; return false; }
    if (root.get("kind").asString() != "vigil-profile") { error = "This is not a Vigil profile."; return false; }
    const int format = static_cast<int>(root.get("format").asNumber(0));
    if (format < 1) { error = "This profile has no format number."; return false; }
    if (format > ProfileSnapshot::kFormat) { error = "This profile was saved by a newer Vigil. Update Vigil to open it."; return false; }

    ProfileSnapshot s;
    s.vigilVersion = clip(root.get("vigil").asString());
    s.savedAtEpochMs = root.get("savedAt").asNumber();
    s.sessionStartMs = root.get("sessionStart").asNumber();
    s.capturedAtMs = root.get("capturedAt").asNumber();
    s.origin = root.get("origin").asString() == "imported" ? "imported" : "saved";
    s.note = clip(root.get("note").asString());
    const json::Value& marks = root.get("marks");
    s.watched = marks.get("watched").asBool();
    s.confirmed = marks.get("confirmed").asBool();
    s.exempt = marks.get("exempt").asBool();
    for (const auto& x : marks.get("reasons").items()) if (s.reasons.size() < 16) s.reasons.push_back(clip(x.asString()));
    const json::Value& idn = root.get("identity");
    s.nicked = idn.get("nicked").asBool();
    s.likelyAccount = clip(idn.get("likelyAccount").asString());
    for (const auto& x : idn.get("candidates").items()) if (s.candidates.size() < 16) s.candidates.push_back(clip(x.asString()));

    const json::Value& p = root.get("player");
    PlayerRecord& r = s.record;
    r.key = clip(p.get("key").asString());
    r.uuid = clip(p.get("uuid").asString());
    r.name = clip(p.get("name").asString());
    if (r.name.empty()) { error = "The profile names no player."; return false; }
    if (r.key.empty()) r.key = r.uuid.empty() ? "name:" + r.name : r.uuid;
    r.local = p.get("local").asBool();
    r.teammate = p.get("teammate").asBool();
    r.present = false;
    r.firstSeenMs = p.get("firstSeen").asNumber();
    r.lastSeenMs = p.get("lastSeen").asNumber();
    r.suspicion = static_cast<float>(std::clamp(p.get("suspicion").asNumber(), 0.0, 100.0));
    r.peakSuspicion = static_cast<float>(std::clamp(p.get("peakSuspicion").asNumber(), 0.0, 100.0));
    r.priorSessions = static_cast<int>(p.get("priorSessions").asNumber());
    r.priorDetections = static_cast<int>(p.get("priorDetections").asNumber());
    r.priorPeak = static_cast<float>(p.get("priorPeak").asNumber());
    r.suspicionHistory = readPoints(p.get("suspicionHistory"));

    r.checks.assign(cat.checks.size(), CheckRecord{});
    for (std::size_t i = 0; i < cat.checks.size(); ++i) r.checks[i].indicatorCounts.assign(cat.indicators[i].size(), 0);
    for (const auto& c : root.get("checks").items()) {
        const std::string id = c.get("id").asString();
        const int ci = indexOf(cat.checks, id);
        if (ci < 0) {
            if (!id.empty() && std::find(s.droppedChecks.begin(), s.droppedChecks.end(), id) == s.droppedChecks.end() &&
                s.droppedChecks.size() < 32) {
                s.droppedChecks.push_back(clip(id));
            }
            continue;
        }
        CheckRecord& cr = r.checks[static_cast<std::size_t>(ci)];
        cr.model.restore(static_cast<float>(c.get("confidence").asNumber()));
        cr.peak = static_cast<float>(std::clamp(c.get("peak").asNumber(), 0.0, 100.0));
        cr.evidence = static_cast<int>(c.get("evidence").asNumber());
        cr.detections = static_cast<int>(c.get("detections").asNumber());
        for (const auto& kv : c.get("indicators").members()) {
            const int ii = indexOf(cat.indicators[static_cast<std::size_t>(ci)], kv.first);
            if (ii >= 0) cr.indicatorCounts[static_cast<std::size_t>(ii)] = static_cast<int>(kv.second.asNumber());
        }
        cr.history = readPoints(c.get("history"));
    }

    for (const auto& j : root.get("evidence").items()) {
        if (r.evidence.size() >= kMaxEvidence) break;
        const int ci = indexOf(cat.checks, j.get("check").asString());
        if (ci < 0) continue;
        const int ii = indexOf(cat.indicators[static_cast<std::size_t>(ci)], j.get("indicator").asString());
        if (ii < 0) continue;
        Evidence e;
        e.id = static_cast<std::uint64_t>(j.get("id").asNumber());
        e.tick = static_cast<Tick>(j.get("tick").asNumber());
        e.timeMs = j.get("time").asNumber();
        e.check = ci;
        e.indicator = ii;
        e.weight = static_cast<float>(j.get("weight").asNumber(1.0));
        e.severity = static_cast<float>(std::clamp(j.get("severity").asNumber(), 0.0, 1.0));
        e.contribution = static_cast<float>(j.get("contribution").asNumber());
        e.confidenceAfter = static_cast<float>(std::clamp(j.get("after").asNumber(), 0.0, 100.0));
        e.summary = clip(j.get("summary").asString());
        e.detail = clip(j.get("detail").asString());
        e.target = clip(j.get("target").asString());
        for (const auto& x : j.get("values").items()) {
            if (e.values.size() >= kMaxValues) break;
            EvidenceValue v;
            v.name = clip(x.get("name").asString());
            v.value = x.get("value").asNumber();
            v.unit = clip(x.get("unit").asString());
            if (const json::Value* l = x.find("limit")) { v.limit = l->asNumber(); v.hasLimit = true; }
            const std::string role = x.get("role").asString();
            v.role = role == "expected" ? EvidenceValue::Role::Expected : role == "context" ? EvidenceValue::Role::Context
                                                                                             : EvidenceValue::Role::Measure;
            e.values.push_back(v);
        }
        e.envelope = deriveEnvelope(e.values);
        r.evidence.push_back(std::move(e));
    }
    for (const auto& j : root.get("detections").items()) {
        if (r.detections.size() >= kMaxDetections) break;
        const int ci = indexOf(cat.checks, j.get("check").asString());
        if (ci < 0) continue;
        Detection d;
        d.id = static_cast<std::uint64_t>(j.get("id").asNumber());
        d.tick = static_cast<Tick>(j.get("tick").asNumber());
        d.timeMs = j.get("time").asNumber();
        d.check = ci;
        d.confidence = static_cast<float>(std::clamp(j.get("confidence").asNumber(), 0.0, 100.0));
        d.evidenceCount = static_cast<int>(j.get("observations").asNumber());
        d.distinctIndicators = static_cast<int>(j.get("indicators").asNumber());
        for (const auto& id : j.get("evidence").items()) {
            if (d.evidence.size() >= kMaxEvidence) break;
            d.evidence.push_back(static_cast<std::uint64_t>(id.asNumber()));
        }
        d.summary = clip(j.get("summary").asString());
        r.detections.push_back(std::move(d));
    }
    for (const auto& j : root.get("timeline").items()) {
        if (r.timeline.size() >= kMaxTimeline) break;
        TimelineEvent t;
        if (!kindFrom(j.get("kind").asString(), t.kind)) continue;
        t.timeMs = j.get("time").asNumber();
        t.tick = static_cast<Tick>(j.get("tick").asNumber());
        t.check = j.find("check") ? indexOf(cat.checks, j.get("check").asString()) : -1;
        if (j.find("check") && t.check < 0) continue;   // an unknown check's entry
        t.ref = static_cast<std::uint64_t>(j.get("ref").asNumber());
        t.value = static_cast<float>(j.get("value").asNumber());
        t.text = clip(j.get("text").asString());
        r.timeline.push_back(std::move(t));
    }
    const json::Value& st = root.get("stats");
    BehaviorStats& b = r.stats;
    b.ticksObserved = static_cast<int>(st.get("ticksObserved").asNumber());
    b.ticksInCombat = static_cast<int>(st.get("ticksInCombat").asNumber());
    b.hitsLanded = static_cast<int>(st.get("hitsLanded").asNumber());
    b.hitsTaken = static_cast<int>(st.get("hitsTaken").asNumber());
    b.swings = static_cast<int>(st.get("swings").asNumber());
    b.reachSum = st.get("reachSum").asNumber();
    b.reachSamples = static_cast<int>(st.get("reachSamples").asNumber());
    b.reachMax = st.get("reachMax").asNumber();
    b.aimOutsideSum = st.get("aimOutsideSum").asNumber();
    b.cpsPeak = st.get("cpsPeak").asNumber();
    b.speedPeak = st.get("speedPeak").asNumber();
    b.speedSum = st.get("speedSum").asNumber();
    b.speedSamples = static_cast<int>(st.get("speedSamples").asNumber());
    b.jumps = static_cast<int>(st.get("jumps").asNumber());
    b.airTicksSum = st.get("airTicksSum").asNumber();
    for (const auto& x : st.get("reachRecent").items()) if (b.reachRecent.size() < 120) b.reachRecent.push_back(static_cast<float>(x.asNumber()));
    for (const auto& x : st.get("speedRecent").items()) if (b.speedRecent.size() < 120) b.speedRecent.push_back(static_cast<float>(x.asNumber()));
    out = std::move(s);
    return true;
}

// ── Store ────────────────────────────────────────────────────────────────────────────────────

void SnapshotStore::setDirectories(const std::string& profilesDir, const std::string& exportsDir, const std::string& importsDir) {
    profiles_ = profilesDir;
    exports_ = exportsDir;
    imports_ = importsDir;
}

std::string SnapshotStore::uniqueId(const std::string& name, double epochMs) const {
    const std::string base = safeStem(name) + "-" + stampOf(epochMs);
    std::string id = base;
    std::error_code ec;
    for (int n = 2; fs::exists(fs::path(profiles_) / (id + ".json"), ec); ++n) id = base + "-" + std::to_string(n);
    return id;
}

void SnapshotStore::refresh(const CheckCatalog& catalog) {
    list_.clear();
    std::error_code ec;
    if (profiles_.empty() || !fs::exists(profiles_, ec)) { ++revision_; return; }
    for (const auto& entry : fs::directory_iterator(profiles_, ec)) {
        if (entry.path().extension() != ".json") continue;
        std::string text, err;
        if (!readFile(entry.path().string(), text, err)) continue;
        ProfileSnapshot s;
        if (!snapshotFromJson(text, catalog, s, err)) continue;
        SnapshotSummary sum;
        sum.id = entry.path().stem().string();
        sum.name = s.record.name;
        sum.uuid = s.record.uuid;
        sum.savedAtEpochMs = s.savedAtEpochMs;
        sum.peakSuspicion = s.record.peakSuspicion;
        sum.suspicion = s.record.suspicion;
        sum.detections = static_cast<int>(s.record.detections.size());
        sum.evidence = 0;
        for (const auto& c : s.record.checks) sum.evidence += c.evidence;
        sum.confirmed = s.confirmed;
        sum.origin = s.origin;
        sum.note = s.note;
        list_.push_back(std::move(sum));
        if (list_.size() >= 500) break;
    }
    std::sort(list_.begin(), list_.end(), [](const SnapshotSummary& a, const SnapshotSummary& b) { return a.savedAtEpochMs > b.savedAtEpochMs; });
    ++revision_;
}

bool SnapshotStore::save(const ProfileSnapshot& s, const CheckCatalog& catalog, std::string& id, std::string& error) {
    if (profiles_.empty()) { error = "No profiles folder."; return false; }
    id = uniqueId(s.record.name, s.savedAtEpochMs > 0.0 ? s.savedAtEpochMs : epochNowMs());
    if (!writeFile((fs::path(profiles_) / (id + ".json")).string(), snapshotToJson(s, catalog))) {
        error = "Vigil could not write to " + profiles_ + ".";
        return false;
    }
    refresh(catalog);
    return true;
}

bool SnapshotStore::load(const std::string& id, const CheckCatalog& catalog, ProfileSnapshot& out, std::string& error) const {
    std::string text;
    if (!readFile((fs::path(profiles_) / (id + ".json")).string(), text, error)) return false;
    if (!snapshotFromJson(text, catalog, out, error)) return false;
    out.id = id;
    return true;
}

bool SnapshotStore::remove(const std::string& id) {
    std::error_code ec;
    const bool ok = fs::remove(fs::path(profiles_) / (id + ".json"), ec);
    if (ok) {
        list_.erase(std::remove_if(list_.begin(), list_.end(), [&](const SnapshotSummary& s) { return s.id == id; }), list_.end());
        ++revision_;
    }
    return ok;
}

bool SnapshotStore::setNote(const std::string& id, const std::string& note, const CheckCatalog& catalog) {
    ProfileSnapshot s;
    std::string err;
    if (!load(id, catalog, s, err)) return false;
    s.note = note.size() > 500 ? note.substr(0, 500) : note;
    if (!writeFile((fs::path(profiles_) / (id + ".json")).string(), snapshotToJson(s, catalog))) return false;
    for (auto& sum : list_) if (sum.id == id) sum.note = s.note;
    ++revision_;
    return true;
}

bool SnapshotStore::exportTo(const std::string& id, std::string& path, std::string& error) const {
    std::string text;
    if (!readFile((fs::path(profiles_) / (id + ".json")).string(), text, error)) return false;
    std::error_code ec;
    fs::create_directories(exports_, ec);
    path = (fs::path(exports_) / (id + ".vigil-profile.json")).string();
    if (!writeFile(path, text)) {
        error = "Vigil could not write to " + exports_ + ".";
        return false;
    }
    return true;
}

bool SnapshotStore::importFile(const std::string& path, const CheckCatalog& catalog, std::string& id, std::string& error) {
    std::string text;
    if (!readFile(path, text, error)) return false;
    ProfileSnapshot s;
    if (!snapshotFromJson(text, catalog, s, error)) return false;
    s.origin = "imported";
    // Re-serialized from what this version understood: nothing from the file is kept verbatim.
    id = uniqueId(s.record.name, s.savedAtEpochMs > 0.0 ? s.savedAtEpochMs : epochNowMs());
    if (!writeFile((fs::path(profiles_) / (id + ".json")).string(), snapshotToJson(s, catalog))) {
        error = "Vigil could not write to " + profiles_ + ".";
        return false;
    }
    refresh(catalog);
    return true;
}

int SnapshotStore::importPending(const CheckCatalog& catalog, std::vector<std::string>& errors) {
    std::error_code ec;
    if (imports_.empty() || !fs::exists(imports_, ec)) return 0;
    int added = 0;
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(imports_, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".json") files.push_back(entry.path());
    }
    for (const auto& f : files) {
        std::string id, err;
        if (importFile(f.string(), catalog, id, err)) {
            ++added;
            fs::remove(f, ec);   // imported: the library has its own copy
        } else {
            errors.push_back(f.filename().string() + ": " + err);
        }
    }
    return added;
}

}  // namespace vigil::detection

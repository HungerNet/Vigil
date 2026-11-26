// Vigil records tests — the user's durable records: marks (watch, confirmed by you, exempt),
// players added by name, saved/exported/imported profiles, and the data-format guards that keep
// one Vigil version from destroying another version's files (PLAN.md §3.4, §10.1).

#include "../src/core/json.h"
#include "../src/core/settings.h"
#include "../src/detection/snapshot.h"
#include "sim.h"
#include "test_framework.h"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace vigil;
using namespace vigil::test;
namespace fs = std::filesystem;

namespace {

std::string readAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void writeAll(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string tempDir(const char* name) {
    const char* t = std::getenv("TEMP");
    const fs::path p = fs::path(t ? t : ".") / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p.string();
}

constexpr const char* kRealUuid = "069a79f4-44e9-4726-a5be-fca90e38aaf5";   // version 4
constexpr const char* kNickUuid = "0b4f2ba3-6c5d-1e8f-9a0b-1c2d3e4f5a6b";   // version 1

}  // namespace

// Confirm, watch and exempt are separate decisions with defined interactions.
static void testMarkSemantics() {
    TempFile f("vigil_test_marks2.json");
    Marks m;
    m.setPath(f.path);
    m.confirm("k1", "Kestrel", {"KillAura"}, 82.f);
    const Mark* k1 = m.find("k1");
    EXPECT(k1 && k1->confirmed && k1->watch && !k1->exempt, "confirming also watches");
    EXPECT(k1 && k1->confidenceAtConfirm == 82.f, "Vigil's confidence at the moment of confirming is kept");
    m.setExempt("k1", "Kestrel", true);
    k1 = m.find("k1");
    EXPECT(k1 && k1->exempt && !k1->confirmed && !k1->watch, "exempting clears the confirmation and the watch");
    EXPECT(m.exempt("k1") && !m.confirmed("k1") && !m.watched("k1"), "queries agree");
    m.confirm("k1", "Kestrel", {});
    EXPECT(m.confirmed("k1") && !m.exempt("k1"), "confirming clears an exemption");
    m.setExempt("k2", "Friend", true);
    m.setExempt("k2", "Friend", false);
    EXPECT(!m.find("k2"), "removing the only mark removes the record");
    m.setNote("k1", "Aim snaps every fight");
    m.setReasons("k1", {"KillAura", "Reach"});
    Marks back;
    back.setPath(f.path);
    const Mark* b = back.find("k1");
    EXPECT(b && b->note == "Aim snaps every fight" && b->reasons.size() == 2, "notes and edited reasons persist");
    EXPECT(back.exemptCount() == 0 && back.confirmedCount() == 1, "counts");
}

static void testSearchMarks() {
    TempFile f("vigil_test_marks_search.json");
    Marks m;
    m.setPath(f.path);
    m.confirm("a", "Kestrel", {"KillAura"});
    m.setWatch("b", "Kessler", true);
    m.setExempt("c", "Friendly", true);
    m.setNote("c", "my duo partner");
    EXPECT(m.search("kes").size() == 2, "name search (%zu)", m.search("kes").size());
    EXPECT(m.search("duo").size() == 1, "note search");
    EXPECT(m.search("killaura").size() == 1, "reason search");
    EXPECT(m.search("", static_cast<int>(MarkKind::Exempt)).size() == 1, "kind filter: exempt");
    EXPECT(m.search("", static_cast<int>(MarkKind::Watch)).size() == 1, "kind filter: watching excludes confirmed");
    EXPECT(m.search("", static_cast<int>(MarkKind::Confirm)).size() == 1, "kind filter: confirmed");
}

// A player added by name binds to the first real account with that name — never to a nick.
static void testAddByNameBinding() {
    TempFile f("vigil_test_marks_bind.json");
    Marks m;
    m.setPath(f.path);
    EXPECT(m.addByName("not a name!", MarkKind::Confirm).empty(), "invalid names are refused");
    const std::string key = m.addByName("Notch", MarkKind::Confirm);
    EXPECT(key == "user:notch", "added by name (%s)", key.c_str());
    EXPECT(m.find(key) && m.find(key)->origin == "name" && m.find(key)->confirmed, "origin recorded");
    EXPECT(!m.bindName(kNickUuid, "Notch", kNickUuid), "a nick (v1) never takes the mark");
    EXPECT(m.find(key) != nullptr, "the mark waits for the real account");
    EXPECT(m.bindName(kRealUuid, "notch", kRealUuid), "the real account binds (case-insensitive)");
    EXPECT(!m.find(key) && m.confirmed(kRealUuid), "the mark now follows the account");
    // Adding the same person by name later updates their account mark, not a second one.
    EXPECT(m.addByName("NOTCH", MarkKind::Watch) == kRealUuid, "a known name reuses the account key");
}

// The engine never judges an exempt player, and binds name-added marks on first sight.
static void testEngineExemptAndBinding() {
    TempFile f("vigil_test_marks_engine.json");
    World w;
    w.engine.setMarksPath(f.path);
    Sim local(1, "Observer");
    setupLocal(local);
    Sim friendSim(2, "Buddy");
    friendSim.f.uuid = kRealUuid;
    w.add(local);
    w.add(friendSim);
    w.engine.marks().addByName("Buddy", MarkKind::Exempt);
    // Low-hopping at 0.55 b/tick: plenty of Speed and BHop evidence for anyone judged.
    for (int i = 0; i < 20 * 30; ++i) {
        const double rad = friendSim.moveYaw * 3.14159265358979323846 / 180.0;
        const Vec3 fwd{-std::sin(rad), 0.0, std::cos(rad)};
        if (friendSim.f.onGround) {
            friendSim.motion.y = 0.25;
            friendSim.f.pos = friendSim.f.pos + Vec3{0, 0.01, 0};
            friendSim.f.onGround = false;
        }
        friendSim.f.pos = friendSim.f.pos + fwd * 0.55;
        friendSim.f.pos.y += friendSim.motion.y;
        if (friendSim.f.pos.y <= friendSim.groundY) {
            friendSim.f.pos.y = friendSim.groundY;
            friendSim.motion.y = 0.0;
            friendSim.f.onGround = true;
        } else {
            friendSim.motion.y = (friendSim.motion.y - 0.08) * 0.98;
        }
        friendSim.f.sprinting = true;
        friendSim.tickAnimations();
        w.frame();
    }
    const PlayerRecord* r = w.rec(friendSim);
    EXPECT(r != nullptr, "an exempt player is still listed");
    EXPECT(w.engine.marks().exempt(kRealUuid), "the name-added exemption bound to the account");
    EXPECT(r && r->evidence.empty() && r->detections.empty(), "an exempt player is never judged (evidence=%zu)",
           r ? r->evidence.size() : 0);
    if (r) {
        const PlayerState s = w.engine.playerState(*r);
        EXPECT(s.listed && s.exempt && !s.judged, "player state: listed, exempt, not judged");
    }
    EXPECT(w.engine.alerts().empty(), "no alerts for an exempt player");
    // Lifting the exemption: judged again from now on.
    w.engine.marks().setExempt(kRealUuid, "Buddy", false);
    for (int i = 0; i < 20 * 20; ++i) {
        friendSim.f.pos = friendSim.f.pos + Vec3{0.0, 0.0, 0.55};
        w.frame();
    }
    r = w.rec(friendSim);
    EXPECT(r && !r->evidence.empty(), "evidence resumes once the exemption is removed");
}

// Format 1 marks files (dev.8) load unchanged; a newer format is never overwritten.
static void testMarksFormatGuards() {
    TempFile f("vigil_test_marks_format.json");
    writeAll(f.path, R"({"format":1,"players":{"abc":{"name":"Old","watch":true,"confirmed":true,"confirmedAt":1700000000000,"updated":1700000000000,"reasons":["Reach"]}}})");
    Marks m;
    m.setPath(f.path);
    EXPECT(m.confirmed("abc") && !m.readOnly(), "format 1 loads");
    EXPECT(m.find("abc") && m.find("abc")->addedEpochMs > 0.0, "added time derived for old marks");
    writeAll(f.path, R"({"format":99,"players":{"abc":{"name":"Future","watch":true}}})");
    const std::string before = readAll(f.path);
    Marks n;
    n.setPath(f.path);
    EXPECT(n.readOnly(), "a newer format opens read-only");
    n.setWatch("xyz", "Someone", true);
    EXPECT(readAll(f.path) == before, "and is never overwritten");
    writeAll(f.path, "{ not json");
    Marks bad;
    bad.setPath(f.path);
    EXPECT(bad.readOnly() && !bad.loadNote().empty(), "an unreadable file is preserved, not replaced");
}

// Settings written by another version keep their unknown keys.
static void testSettingsKeepUnknownKeys() {
    TempFile f("vigil_test_settings_unknown.json");
    writeAll(f.path, R"({"format":1,"settings":{"general.monitoring":true,"future.feature":{"a":1},"future.flag":true}})");
    Settings& S = Settings::instance();
    S.addBool("general.monitoring", "Monitoring", true, "");
    S.setPath(f.path);
    EXPECT(S.load(), "loads");
    EXPECT(S.save(), "saves");
    json::Value root;
    EXPECT(json::parse(readAll(f.path), root), "valid JSON");
    EXPECT(root.get("settings").find("future.feature") && root.get("settings").find("future.flag"),
           "keys this version doesn't know are written back");
    EXPECT(!root.get("writtenBy").asString().empty(), "the writing version is recorded");
    S.setPath("");
}

// Numbers survive a write/parse round trip exactly (traces and profiles depend on it).
static void testJsonRoundTrip() {
    const double values[] = {0.1, 0.356123456789, 1.0 / 3.0, 64.03125, -1e-9, 123456.789012345, 0.4};
    for (double v : values) {
        json::Value parsed;
        EXPECT(json::parse(json::write(json::Value(v), -1), parsed) && parsed.asNumber() == v, "round trip %.17g", v);
    }
    EXPECT(json::write(json::Value(0.4), -1) == "0.4", "short form kept for readability (%s)", json::write(json::Value(0.4), -1).c_str());
}

// Building a session with evidence and detections for snapshot tests.
static void buildSession(World& w, Sim& local, Sim& cheat) {
    setupLocal(local);
    w.add(local);
    w.add(cheat);
    for (int i = 0; i < 20 * 30; ++i) {
        cheat.f.pos = cheat.f.pos + Vec3{0.0, 0.0, 0.6};
        cheat.f.onGround = true;
        cheat.f.sprinting = true;
        w.frame();
    }
}

static void testSnapshotRoundTrip() {
    World w;
    Sim local(1, "Observer"), cheat(2, "Glider");
    buildSession(w, local, cheat);
    const PlayerRecord* r = w.rec(cheat);
    EXPECT(r && !r->evidence.empty(), "sanity: evidence recorded (%zu)", r ? r->evidence.size() : 0);
    if (!r) return;
    const CheckCatalog cat = CheckCatalog::of(w.engine);
    ProfileSnapshot s = captureSnapshot(w.engine, *r);
    s.note = "Saved during testing";
    s.nicked = true;
    s.likelyAccount = "RealGlider";
    const std::string text = snapshotToJson(s, cat);
    ProfileSnapshot back;
    std::string err;
    EXPECT(snapshotFromJson(text, cat, back, err), "parses (%s)", err.c_str());
    EXPECT(back.record.name == "Glider" && back.record.uuid == r->uuid, "identity");
    EXPECT(back.record.evidence.size() == r->evidence.size(), "every observation (%zu vs %zu)", back.record.evidence.size(), r->evidence.size());
    EXPECT(back.record.detections.size() == r->detections.size(), "every detection");
    EXPECT(std::fabs(back.record.suspicion - r->suspicion) < 1e-4f, "confidence");
    EXPECT(back.note == s.note && back.nicked && back.likelyAccount == "RealGlider", "note and identity summary");
    if (!back.record.evidence.empty()) {
        const Evidence& a = r->evidence.back();
        const Evidence& b = back.record.evidence.back();
        EXPECT(a.check == b.check && a.indicator == b.indicator && a.summary == b.summary && a.values.size() == b.values.size(),
               "observation fields");
        EXPECT(b.envelope.valid() == a.envelope.valid() && b.envelope.limit == a.envelope.limit, "envelope re-derived identically");
    }
    const Check* speed = w.engine.checkById("speed");
    if (speed) {
        const auto& ca = r->checks[static_cast<std::size_t>(speed->index())];
        const auto& cb = back.record.checks[static_cast<std::size_t>(speed->index())];
        EXPECT(ca.evidence == cb.evidence && ca.detections == cb.detections && ca.indicatorCounts == cb.indicatorCounts,
               "per-check counts");
    }
}

// A file from another version: unknown checks are dropped and listed, known ones remapped by id.
static void testSnapshotRemapping() {
    World w;
    Sim local(1, "Observer"), cheat(2, "Glider");
    buildSession(w, local, cheat);
    const PlayerRecord* r = w.rec(cheat);
    if (!r) return;
    CheckCatalog cat = CheckCatalog::of(w.engine);
    const std::string text = snapshotToJson(captureSnapshot(w.engine, *r), cat);
    // An older version with "speed" missing and the remaining checks in reverse order.
    CheckCatalog other = cat;
    const auto si = std::find(other.checks.begin(), other.checks.end(), "speed") - other.checks.begin();
    other.checks.erase(other.checks.begin() + si);
    other.indicators.erase(other.indicators.begin() + si);
    std::reverse(other.checks.begin(), other.checks.end());
    std::reverse(other.indicators.begin(), other.indicators.end());
    ProfileSnapshot back;
    std::string err;
    EXPECT(snapshotFromJson(text, other, back, err), "parses");
    EXPECT(std::find(back.droppedChecks.begin(), back.droppedChecks.end(), "speed") != back.droppedChecks.end(), "unknown check listed");
    bool mapped = true;
    for (const auto& e : back.record.evidence) mapped = mapped && other.checks[static_cast<std::size_t>(e.check)] != "speed";
    EXPECT(mapped, "evidence references the reader's own check indices");
    EXPECT(back.record.checks.size() == other.checks.size(), "check records sized for the reader");
}

// Hostile or broken files are refused cleanly.
static void testSnapshotRejects() {
    World w;
    const CheckCatalog cat = CheckCatalog::of(w.engine);
    ProfileSnapshot s;
    std::string err;
    EXPECT(!snapshotFromJson("{", cat, s, err) && !err.empty(), "malformed JSON");
    EXPECT(!snapshotFromJson(R"({"kind":"something-else","format":1})", cat, s, err), "wrong kind");
    EXPECT(!snapshotFromJson(R"({"kind":"vigil-profile","format":99,"player":{"name":"X"}})", cat, s, err) &&
               err.find("newer") != std::string::npos, "newer format explains itself");
    EXPECT(!snapshotFromJson(R"({"kind":"vigil-profile","format":1,"player":{}})", cat, s, err), "no player");
    std::string big(ProfileSnapshot::kMaxFileBytes + 1, ' ');
    EXPECT(!snapshotFromJson(big, cat, s, err), "oversized input");
    // Counts are capped no matter what the file claims.
    std::string many = R"({"kind":"vigil-profile","format":1,"player":{"name":"Spam"},"evidence":[)";
    for (int i = 0; i < 2000; ++i) {
        many += std::string(i ? "," : "") + R"({"check":"speed","indicator":"sustained","summary":"x","values":[]})";
    }
    many += "]}";
    EXPECT(snapshotFromJson(many, cat, s, err) && s.record.evidence.size() <= 600, "evidence capped (%zu)", s.record.evidence.size());
}

// Save, list, note, export, import (file and pending folder), delete.
static void testSnapshotStore() {
    World w;
    Sim local(1, "Observer"), cheat(2, "Glider");
    buildSession(w, local, cheat);
    const PlayerRecord* r = w.rec(cheat);
    if (!r) return;
    const CheckCatalog cat = CheckCatalog::of(w.engine);
    const std::string root = tempDir("vigil_test_profiles");
    SnapshotStore store;
    store.setDirectories(root + "\\profiles", root + "\\exports", root + "\\imports");
    std::string id, err;
    EXPECT(store.save(captureSnapshot(w.engine, *r), cat, id, err), "save (%s)", err.c_str());
    EXPECT(store.list().size() == 1 && store.list()[0].name == "Glider", "listed");
    EXPECT(store.setNote(id, "keep for the report", cat) && store.list()[0].note == "keep for the report", "note");
    std::string exported;
    EXPECT(store.exportTo(id, exported, err) && fs::exists(exported), "export (%s)", exported.c_str());
    const std::string suffix = ".vigil-profile.json";
    EXPECT(exported.size() > suffix.size() && exported.compare(exported.size() - suffix.size(), suffix.size(), suffix) == 0,
           "export file name (%s)", exported.c_str());
    std::string imported;
    EXPECT(store.importFile(exported, cat, imported, err) && imported != id, "import a file (%s)", err.c_str());
    EXPECT(store.list().size() == 2, "both listed");
    ProfileSnapshot s;
    EXPECT(store.load(imported, cat, s, err) && s.origin == "imported", "imported origin");
    fs::create_directories(root + "\\imports");
    fs::copy_file(exported, root + "\\imports\\from-a-friend.json");
    writeAll(root + "\\imports\\broken.json", "{");
    std::vector<std::string> errors;
    EXPECT(store.importPending(cat, errors) == 1 && errors.size() == 1, "pending imports: one added, one refused");
    EXPECT(!fs::exists(root + "\\imports\\from-a-friend.json"), "imported files leave the imports folder");
    EXPECT(fs::exists(root + "\\imports\\broken.json"), "refused files stay for the user to inspect");
    EXPECT(store.remove(id) && store.list().size() == 2, "delete");
    std::error_code ec;
    fs::remove_all(root, ec);
}

int main(int argc, char** argv) {
    const Test tests[] = {
        {"mark semantics", testMarkSemantics},
        {"search marks", testSearchMarks},
        {"add by name and binding", testAddByNameBinding},
        {"engine: exempt and binding", testEngineExemptAndBinding},
        {"marks format guards", testMarksFormatGuards},
        {"settings keep unknown keys", testSettingsKeepUnknownKeys},
        {"json number round trip", testJsonRoundTrip},
        {"snapshot round trip", testSnapshotRoundTrip},
        {"snapshot check remapping", testSnapshotRemapping},
        {"snapshot rejects bad files", testSnapshotRejects},
        {"snapshot store", testSnapshotStore},
    };
    return runSuite(argc, argv, tests, "records");
}

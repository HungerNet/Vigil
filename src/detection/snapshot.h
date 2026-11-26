#pragma once
// Vigil detection framework — saved profiles (PLAN.md §3, X4).
//
// A snapshot is a player's investigation frozen at the moment the user saved it: confidence over
// the session, every recorded observation with its measured values, detections, the timeline,
// behavior statistics, the user's marks and an identity summary. It is a record of what Vigil saw,
// never live: viewing it changes nothing and nothing changes it.
//
//   Save     keeps the snapshot in %LOCALAPPDATA%\Vigil\profiles (Records → Saved)
//   Export   writes a copy to %LOCALAPPDATA%\Vigil\exports for sharing (file name ends in
//            .vigil-profile.json)
//   Import   validates a file and adds it to Records → Saved
//
// Files are JSON, format 1. Checks and indicators are referenced by id, not by index, so a
// snapshot opens in any later version; observations of checks this version doesn't know are
// dropped and listed. Parsing is bounded (size, counts, string lengths) because imported files
// come from other people.

#include "session.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vigil::detection {

class Engine;

struct ProfileSnapshot {
    static constexpr int kFormat = 1;
    static constexpr std::size_t kMaxFileBytes = 8u * 1024u * 1024u;

    std::string id;                 // file stem in the profiles folder
    std::string vigilVersion;       // version that captured it
    double savedAtEpochMs = 0.0;
    double sessionStartMs = 0.0;    // engine clock of the session it came from
    double capturedAtMs = 0.0;      // engine clock at capture ("now" when viewing)
    std::string origin = "saved";   // "saved" or "imported"
    std::string note;               // the user's words

    // The user's marks when it was saved.
    bool watched = false, confirmed = false, exempt = false;
    std::vector<std::string> reasons;
    // Identity summary (filled by the caller; the detection layer has no identity service).
    bool nicked = false;
    std::string likelyAccount;
    std::vector<std::string> candidates;

    std::vector<std::string> droppedChecks;   // check ids in the file that this version doesn't know
    PlayerRecord record;                      // check indices mapped to the current engine
};

// Check and indicator ids of an engine, in index order (what files reference).
struct CheckCatalog {
    std::vector<std::string> checks;
    std::vector<std::vector<std::string>> indicators;
    static CheckCatalog of(const Engine& e);
};

ProfileSnapshot captureSnapshot(const Engine& e, const PlayerRecord& r);
std::string snapshotToJson(const ProfileSnapshot& s, const CheckCatalog& catalog);
bool snapshotFromJson(const std::string& text, const CheckCatalog& catalog, ProfileSnapshot& out, std::string& error);

struct SnapshotSummary {
    std::string id;
    std::string name;
    std::string uuid;
    double savedAtEpochMs = 0.0;
    float peakSuspicion = 0.f;
    float suspicion = 0.f;
    int detections = 0;
    int evidence = 0;
    bool confirmed = false;
    std::string origin;
    std::string note;
};

class SnapshotStore {
public:
    void setDirectories(const std::string& profilesDir, const std::string& exportsDir, const std::string& importsDir);
    const std::string& profilesDir() const { return profiles_; }
    const std::string& exportsDir() const { return exports_; }
    const std::string& importsDir() const { return imports_; }

    // Rescans the profiles folder (cheap; call when the list is shown or after a change).
    void refresh(const CheckCatalog& catalog);
    const std::vector<SnapshotSummary>& list() const { return list_; }
    std::uint64_t revision() const { return revision_; }

    bool save(const ProfileSnapshot& s, const CheckCatalog& catalog, std::string& id, std::string& error);
    bool load(const std::string& id, const CheckCatalog& catalog, ProfileSnapshot& out, std::string& error) const;
    bool remove(const std::string& id);
    bool setNote(const std::string& id, const std::string& note, const CheckCatalog& catalog);
    // Writes a shareable copy; returns its full path.
    bool exportTo(const std::string& id, std::string& path, std::string& error) const;
    // Validates `path` and adds it to the library (origin "imported"). Returns the new id.
    bool importFile(const std::string& path, const CheckCatalog& catalog, std::string& id, std::string& error);
    // Imports every profile file waiting in the imports folder; returns how many were added.
    int importPending(const CheckCatalog& catalog, std::vector<std::string>& errors);

private:
    std::string uniqueId(const std::string& name, double epochMs) const;
    std::string profiles_, exports_, imports_;
    std::vector<SnapshotSummary> list_;
    std::uint64_t revision_ = 1;
};

}  // namespace vigil::detection

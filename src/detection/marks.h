#pragma once
// Vigil detection framework — the user's own records about players ("marks").
//
//   Watching            pinned to the watchlist; announced when they appear in a match
//   Confirmed by you    the user's conclusion after investigating. Vigil never sets it.
//   Exempt              the user's decision that Vigil should not judge this player (a friend,
//                       a party member). Listed, never analyzed: no evidence, no detections, no
//                       alerts, no highlight. Never automatic.
//
// Marks are the user's decisions, kept apart from Vigil's confidence: no check reads them, and
// they never change evidence or confidence (exempt only decides whether checks run at all).
// Confirmed and Exempt are mutually exclusive: confirming clears an exemption and exempting clears
// a confirmation, so a player is never both "a cheater" and "not to be judged".
//
// Keys. A mark is keyed like the PlayerRecord it belongs to (the UUID, or "name:<Name>" when the
// UUID is unknown). A player added by name before Vigil has seen them is keyed "user:<lowercase
// name>"; the first time Vigil sees a real (version-4) account with that name, the mark moves to
// that account's key and follows the account from then on (bindName). Nicks are never bound:
// Hypixel does not let a nick use an existing account's name, so a name match on a nick is not the
// person the user meant.
//
// Persistence: history/marks.json, format 2 (format 1 files load unchanged). A file written by a
// newer format is loaded read-only and never overwritten (PLAN.md §10.1).

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace vigil::detection {

struct Mark {
    std::string key;                    // record key, or "user:<lowercase name>" (added by name, not seen yet)
    std::string name;                   // last known name
    std::string uuid;                   // when known
    bool watch = false;
    bool confirmed = false;
    bool exempt = false;
    double addedEpochMs = 0.0;
    double confirmedAtEpochMs = 0.0;
    double exemptAtEpochMs = 0.0;
    double updatedEpochMs = 0.0;
    std::vector<std::string> reasons;   // check names the user ticked when confirming
    std::string note;                   // free text, the user's own words
    float confidenceAtConfirm = -1.f;   // Vigil's confidence when confirmed (-1: not observed then)
    std::string origin;                 // "profile" (from a live profile) or "name" (added by name)

    bool addedByName() const { return key.rfind("user:", 0) == 0; }
    bool any() const { return watch || confirmed || exempt; }
};

enum class MarkKind { Watch, Confirm, Exempt };

class Marks {
public:
    static constexpr int kFormat = 2;

    void setPath(const std::string& path);   // loads immediately
    bool save() const;
    // The file on disk was written by a newer Vigil: changes are kept in memory only.
    bool readOnly() const { return readOnly_; }
    const std::string& loadNote() const { return loadNote_; }

    const Mark* find(const std::string& key) const;
    // A mark for this name: an exact key first, then the added-by-name key.
    const Mark* findByName(const std::string& name) const;
    bool watched(const std::string& key) const;     // watch or confirmed
    bool confirmed(const std::string& key) const;
    bool exempt(const std::string& key) const;

    void setWatch(const std::string& key, const std::string& name, bool on);
    // Explicit user action only. Confirming also watches the player and clears an exemption.
    // `confidence` is Vigil's confidence at that moment (-1 when the player is not observed).
    void confirm(const std::string& key, const std::string& name, std::vector<std::string> reasons, float confidence = -1.f);
    void unconfirm(const std::string& key);
    // Explicit user action only. Exempting clears a confirmation and the watch.
    void setExempt(const std::string& key, const std::string& name, bool on);
    void setNote(const std::string& key, const std::string& note);
    void setReasons(const std::string& key, std::vector<std::string> reasons);
    void remove(const std::string& key);
    void rememberName(const std::string& key, const std::string& name);   // keeps the stored name current

    // Adds (or updates) a player by name — someone Vigil may never have seen. Returns the key used
    // (an existing record key when `knownKey` is given, else "user:<lowercase name>"); empty when
    // the name is not a valid Minecraft name.
    std::string addByName(const std::string& name, MarkKind kind, const std::string& knownKey = {});
    // A player was seen: moves an added-by-name mark onto the account's key (real accounts only).
    // Returns true when a mark was bound.
    bool bindName(const std::string& recordKey, const std::string& name, const std::string& uuid);

    std::vector<Mark> all() const;           // confirmed first, then watched, then exempt; newest first
    // Case-insensitive search over names, notes and reasons. `kind` filters (-1 = any mark).
    std::vector<Mark> search(const std::string& query, int kind = -1) const;
    int confirmedCount() const;
    int watchedCount() const;
    int exemptCount() const;
    std::uint64_t revision() const { return revision_; }

private:
    void changed();
    Mark& touch(const std::string& key, const std::string& name);
    std::map<std::string, Mark> marks_;
    std::string path_;
    bool readOnly_ = false;
    std::string loadNote_;
    std::uint64_t revision_ = 1;
};

double epochNowMs();
std::string lowerName(const std::string& s);

}  // namespace vigil::detection

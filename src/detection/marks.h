#pragma once
// Vigil detection framework — the user's own marks on players.
//
//   Watching            pinned to the watchlist; announced when they appear in a match
//   Confirmed by you    the user's conclusion after investigating. Vigil never sets it.
//
// Marks are the user's conclusions, kept apart from Vigil's confidence: no check reads them, and
// they never change evidence or confidence. They persist in history/marks.json, keyed by UUID
// (the record key), with the last known name.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace vigil::detection {

struct Mark {
    std::string key;                 // PlayerRecord key (uuid, or "name:<name>")
    std::string name;                // last known name
    bool watch = false;
    bool confirmed = false;
    double confirmedAtEpochMs = 0.0;
    std::vector<std::string> reasons;   // check names the user ticked when confirming
    double updatedEpochMs = 0.0;
};

class Marks {
public:
    void setPath(const std::string& path);   // loads immediately
    bool save() const;

    const Mark* find(const std::string& key) const;
    bool watched(const std::string& key) const;
    bool confirmed(const std::string& key) const;

    void setWatch(const std::string& key, const std::string& name, bool on);
    // Explicit user action only. Confirming also watches the player.
    void confirm(const std::string& key, const std::string& name, std::vector<std::string> reasons);
    void unconfirm(const std::string& key);
    void remove(const std::string& key);
    void rememberName(const std::string& key, const std::string& name);   // keeps the stored name current

    std::vector<Mark> all() const;           // confirmed first, then watched; newest first
    int confirmedCount() const;
    int watchedCount() const;
    std::uint64_t revision() const { return revision_; }

private:
    void changed();
    std::map<std::string, Mark> marks_;
    std::string path_;
    std::uint64_t revision_ = 1;
};

double epochNowMs();

}  // namespace vigil::detection

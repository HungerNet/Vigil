#pragma once
// Vigil identity tools — denickers, restored from the legacy client and integrated into
// investigations.
//
//   Nick detection     Hypixel gives nicked players version-1 UUIDs (real accounts use version 4).
//   Skin denicker      The signed `textures` property names the account that owns the skin. When a
//                      nicked player keeps their own skin, that account is the player. Hypixel's
//                      pool of nick skins is embedded, so pool skins are recognised and never
//                      looked up. The owner's current name comes from Mojang's public session
//                      server (one request per nicked player).
//   Number denicker    Bed Wars announces lifetime counters ("final #23,835", "bed #21,362"). With
//                      the user's own Bordic API key, each counter maps to candidate accounts;
//                      intersecting finals and beds narrows the candidates.
//
// Privacy: no analytics, telemetry or reporting. Requests are made only for nicked players, only
// to the endpoints above, from a worker thread, with a generic user agent. Each lookup is shown in
// the player's Identity tab. Everything can be switched off in Utilities → Identity tools.

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace vigil {
struct Setting;
}

namespace vigil::identity {

// ── Pure helpers (unit-tested) ────────────────────────────────────────────────────────────────
int uuidVersion(const std::string& uuid);                 // 0 when malformed
std::string normalizeUuid(const std::string& any);        // dashed lower-case, empty when invalid
bool base64Decode(const std::string& in, std::string& out);
struct Textures {
    std::string skinUrl;
    std::string skinHash;      // last path segment of the skin URL
    std::string profileId;     // owner of the skin (dashed uuid)
    std::string profileName;   // owner's name when the texture was signed
};
bool decodeTextures(const std::string& base64Value, Textures& out);
bool isNickPoolSkin(const std::string& skinHash);
int nickPoolSize();
// Bed Wars lifetime counters.
bool parseFinalKill(const std::string& cleanLine, std::string& killer, std::string& number);
bool parseBedBreak(const std::string& cleanLine, std::string& breaker, std::string& number);

// ── State ─────────────────────────────────────────────────────────────────────────────────────
enum class SkinKind { Unknown, Own, NickPool, Foreign };
enum class Lookup { Idle, Pending, Done, Failed, Disabled };
const char* lookupName(Lookup l);

struct Profile {
    std::string key;            // engine PlayerRecord key (uuid)
    std::string name;           // name shown in game (the nick, when nicked)
    std::string uuid;
    int uuidVersion = 0;
    bool nicked = false;

    Textures textures;
    SkinKind skin = SkinKind::Unknown;

    // Skin denicker.
    Lookup skinLookup = Lookup::Idle;
    std::string realName;       // current name of the skin's owner
    std::string realUuid;
    std::string skinNote;       // explanation shown in the UI

    // Number denicker.
    Lookup numberLookup = Lookup::Idle;
    std::string finalsNumber, bedsNumber;
    bool finalsChecked = false, bedsChecked = false;   // lookups completed
    std::vector<std::string> candidates;
    std::string numberNote;

    double updatedMs = 0.0;     // epoch ms
};

struct ServiceStats {
    int requests = 0;
    int failures = 0;
    int pending = 0;
    std::string lastError;
    double lastRequestMs = 0.0;
};

struct IdentitySettings {
    Setting* skinDenick = nullptr;
    Setting* autoLookup = nullptr;
    Setting* numberDenick = nullptr;
    Setting* bordicKey = nullptr;
    Setting* announce = nullptr;
};
void registerSettings();
const IdentitySettings& settings();

class Service {
public:
    // Performs one HTTPS GET. Returns false on transport failure.
    using Fetch = std::function<bool(const std::string& url, int& status, std::string& body, std::string& error)>;

    static Service& instance();
    ~Service();

    void setFetcher(Fetch f);   // default: WinHTTP (src/net)
    void stop();

    // Game thread: a player was seen (call whenever the profile is refreshed).
    void observe(const std::string& key, const std::string& name, const std::string& uuid, const std::string& texturesBase64);
    // Explicit request from the UI (Identity tab, search, profile header).
    void requestSkinLookup(const std::string& key);
    // Game thread: every clean chat line (number denicker).
    void onChatLine(const std::string& cleanLine);
    void resetGame();   // new world

    bool find(const std::string& key, Profile& out) const;
    bool findByName(const std::string& name, Profile& out) const;
    std::vector<Profile> nicked() const;
    ServiceStats stats() const;

    // Announcements for chat ("nick → real"), consumed by the runtime.
    std::vector<std::string> takeAnnouncements();

private:
    struct Job {
        enum Type { SkinOwner, Number } type = SkinOwner;
        std::string key;
        std::string arg;     // uuid (no dashes) or counter number
        std::string kind;    // "finals" / "beds"
    };
    Service() = default;
    void ensureWorker();
    void worker();
    bool fetch(const std::string& url, int& status, std::string& body, std::string& error);
    void runSkinJob(const Job& j);
    void runNumberJob(const Job& j);
    void queueSkinLocked(Profile& p);

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::thread thread_;
    bool stop_ = false;
    bool running_ = false;
    std::deque<Job> jobs_;
    std::map<std::string, Profile> profiles_;
    std::map<std::string, std::string> byName_;  // lower-case name → key
    std::vector<std::string> announcements_;
    ServiceStats stats_;
    Fetch fetch_;
};

}  // namespace vigil::identity

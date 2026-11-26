#include "identity.h"

#include "../core/json.h"
#include "../core/log.h"
#include "../core/settings.h"
#include "../net/http.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <regex>
#include <unordered_set>

namespace vigil::identity {

namespace {

double epochMs() {
    using namespace std::chrono;
    return static_cast<double>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string stripDashes(const std::string& s) {
    std::string out;
    for (char c : s) if (c != '-') out.push_back(c);
    return out;
}

const std::unordered_set<std::string>& nickPool() {
    static const std::unordered_set<std::string> pool = {
#include "nick_skins.inc"
    };
    return pool;
}

IdentitySettings g_settings;

}  // namespace

// ── Settings ─────────────────────────────────────────────────────────────────────────────────

void registerSettings() {
    if (g_settings.skinDenick) return;
    auto& S = Settings::instance();
    auto grp = [](Setting& s, bool adv = false) -> Setting& { s.group = "Identity"; s.advanced = adv; return s; };
    g_settings.skinDenick = &grp(S.addBool("identity.skinDenick", "Skin denicker", true,
                                           "Recognises nicked players and identifies the account that owns their skin. "
                                           "Looking up an owner contacts Mojang's public profile service."));
    g_settings.autoLookup = &grp(S.addBool("identity.autoLookup", "Look up automatically", true,
                                           "Look up skin owners as soon as a nicked player is seen. When off, lookups "
                                           "happen only when you ask for one in a player's Identity tab."));
    g_settings.numberDenick = &grp(S.addBool("identity.numberDenick", "Number denicker", false,
                                             "Uses the lifetime counters Bed Wars announces (\"final #23,835\", \"bed "
                                             "#21,362\") to find candidate accounts for nicked players. Requires your own "
                                             "Bordic API key."));
    g_settings.bordicKey = &grp(S.addText("identity.bordicKey", "Bordic API key", "",
                                          "Your personal key for the number denicker. Stored only in Vigil's "
                                          "configuration file and sent only to Bordic."));
    g_settings.announce = &grp(S.addBool("identity.announce", "Announce in chat", true,
                                         "Print a Vigil chat line when a nicked player is identified."), true);
}

const IdentitySettings& settings() { return g_settings; }

// ── Pure helpers ─────────────────────────────────────────────────────────────────────────────

std::string normalizeUuid(const std::string& any) {
    const std::string c = lower(stripDashes(any));
    if (c.size() != 32) return {};
    for (char ch : c) if (!std::isxdigit(static_cast<unsigned char>(ch))) return {};
    return c.substr(0, 8) + "-" + c.substr(8, 4) + "-" + c.substr(12, 4) + "-" + c.substr(16, 4) + "-" + c.substr(20);
}

int uuidVersion(const std::string& uuid) {
    const std::string n = normalizeUuid(uuid);
    if (n.empty()) return 0;
    const char v = n[14];
    return std::isdigit(static_cast<unsigned char>(v)) ? v - '0' : 0;
}

bool base64Decode(const std::string& in, std::string& out) {
    out.clear();
    auto val = [](unsigned char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };
    int buf = 0, bits = 0;
    for (unsigned char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        const int v = val(c);
        if (v < 0) return false;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buf >> bits) & 0xFF));
        }
    }
    return !out.empty();
}

bool decodeTextures(const std::string& base64Value, Textures& out) {
    out = Textures{};
    std::string decoded;
    if (!base64Decode(base64Value, decoded)) return false;
    json::Value root;
    if (!json::parse(decoded, root) || !root.isObject()) return false;
    out.profileId = normalizeUuid(root.get("profileId").asString());
    out.profileName = root.get("profileName").asString();
    const json::Value& skin = root.get("textures").get("SKIN");
    out.skinUrl = skin.get("url").asString();
    if (!out.skinUrl.empty()) {
        std::string tail = out.skinUrl.substr(out.skinUrl.find_last_of('/') + 1);
        const auto q = tail.find('?');
        if (q != std::string::npos) tail = tail.substr(0, q);
        out.skinHash = lower(tail);
    }
    return !out.profileId.empty() || !out.skinHash.empty();
}

bool isNickPoolSkin(const std::string& skinHash) { return !skinHash.empty() && nickPool().count(lower(skinHash)) > 0; }
int nickPoolSize() { return static_cast<int>(nickPool().size()); }

bool parseFinalKill(const std::string& cleanLine, std::string& killer, std::string& number) {
    // "Octa117 was itzalex's final #23,835. FINAL KILL!"
    static const std::regex rgx("^[\\w-]+\\s+was\\s+([\\w-]+)'s\\s+final\\s*#([\\d,]+)\\.\\s*FINAL\\s+KILL!$", std::regex::icase);
    std::string line = cleanLine;
    // Normalize typographic apostrophes.
    for (const char* q : {"\xE2\x80\x99", "\xC2\xB4"}) {
        for (std::size_t p; (p = line.find(q)) != std::string::npos;) line.replace(p, std::strlen(q), "'");
    }
    std::smatch m;
    if (!std::regex_match(line, m, rgx)) return false;
    killer = m[1].str();
    number = m[2].str();
    number.erase(std::remove(number.begin(), number.end(), ','), number.end());
    return !killer.empty() && !number.empty();
}

bool parseBedBreak(const std::string& cleanLine, std::string& breaker, std::string& number) {
    // "BED DESTRUCTION > Your Bed was bed #21,362 destroyed by AidenHerrera1997!"
    static const std::regex rgx("BED\\s+DESTRUCTION\\s*>.*?bed\\s*#([\\d,]+).*?destroyed\\s+by\\s+([\\w-]+)\\s*!", std::regex::icase);
    std::smatch m;
    if (!std::regex_search(cleanLine, m, rgx)) return false;
    number = m[1].str();
    number.erase(std::remove(number.begin(), number.end(), ','), number.end());
    breaker = m[2].str();
    return !breaker.empty() && !number.empty();
}

const char* lookupName(Lookup l) {
    switch (l) {
        case Lookup::Idle: return "Not looked up";
        case Lookup::Pending: return "Looking up";
        case Lookup::Done: return "Done";
        case Lookup::Failed: return "Failed";
        case Lookup::Disabled: return "Off";
    }
    return "";
}

// ── Service ──────────────────────────────────────────────────────────────────────────────────

Service& Service::instance() {
    static Service s;
    return s;
}

Service::~Service() {
    // Runs during process teardown (possibly under the loader lock inside the game): never wait
    // for the worker here. Explicit shutdown paths call stop(), which joins.
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_ = true;
        jobs_.clear();
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.detach();
}

void Service::setFetcher(Fetch f) {
    std::lock_guard<std::mutex> lk(mu_);
    fetch_ = std::move(f);
}

void Service::stop() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_ = true;
        jobs_.clear();
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lk(mu_);
    running_ = false;
    stop_ = false;
}

void Service::ensureWorker() {
    if (running_) return;
    running_ = true;
    stop_ = false;
    thread_ = std::thread([this] { worker(); });
}

bool Service::fetch(const std::string& url, int& status, std::string& body, std::string& error) {
    Fetch f;
    {
        std::lock_guard<std::mutex> lk(mu_);
        f = fetch_;
        ++stats_.requests;
        stats_.lastRequestMs = epochMs();
    }
    bool ok = false;
    if (f) {
        ok = f(url, status, body, error);
    } else {
        net::Response r;
        ok = net::get(url, r);
        status = r.status;
        body = std::move(r.body);
        error = r.error;
    }
    if (!ok || status != 200) {
        std::lock_guard<std::mutex> lk(mu_);
        ++stats_.failures;
        stats_.lastError = !error.empty() ? error : ("HTTP " + std::to_string(status));
    }
    return ok;
}

void Service::queueSkinLocked(Profile& p) {
    if (!g_settings.skinDenick || !g_settings.skinDenick->b()) { p.skinLookup = Lookup::Disabled; return; }
    if (p.skin != SkinKind::Foreign || p.textures.profileId.empty()) return;
    if (p.skinLookup == Lookup::Pending || p.skinLookup == Lookup::Done) return;
    p.skinLookup = Lookup::Pending;
    p.skinNote = "Looking up the skin's owner\xE2\x80\xA6";
    jobs_.push_back({Job::SkinOwner, p.key, stripDashes(p.textures.profileId), {}});
    ensureWorker();
    cv_.notify_one();
}

void Service::observe(const std::string& key, const std::string& name, const std::string& uuid, const std::string& texturesBase64) {
    if (key.empty()) return;
    std::lock_guard<std::mutex> lk(mu_);
    Profile& p = profiles_[key];
    const bool fresh = p.key.empty();
    p.key = key;
    if (!name.empty()) {
        p.name = name;
        byName_[lower(name)] = key;
    }
    p.uuid = normalizeUuid(uuid);
    p.uuidVersion = uuidVersion(uuid);
    p.nicked = p.uuidVersion == 1;
    if (!texturesBase64.empty() && (fresh || p.textures.skinHash.empty())) {
        decodeTextures(texturesBase64, p.textures);
    }
    if (!p.nicked) {
        p.skin = SkinKind::Own;
        return;
    }
    if (p.skin == SkinKind::Unknown && (!p.textures.skinHash.empty() || !p.textures.profileId.empty())) {
        if (isNickPoolSkin(p.textures.skinHash)) {
            p.skin = SkinKind::NickPool;
            p.skinNote = "Wears one of Hypixel's nick skins, so the skin does not reveal the account.";
        } else if (!p.textures.profileId.empty() && p.textures.profileId != p.uuid) {
            p.skin = SkinKind::Foreign;
            p.skinNote = "Wears a skin owned by another account.";
        } else {
            p.skin = SkinKind::Unknown;
            p.skinNote = "The skin's owner could not be read.";
        }
        p.updatedMs = epochMs();
    }
    if (p.skin == SkinKind::Foreign && p.skinLookup == Lookup::Idle && g_settings.autoLookup && g_settings.autoLookup->b()) {
        queueSkinLocked(p);
    }
}

void Service::requestSkinLookup(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = profiles_.find(key);
    if (it == profiles_.end()) return;
    if (it->second.skinLookup == Lookup::Failed) it->second.skinLookup = Lookup::Idle;
    queueSkinLocked(it->second);
}

void Service::onChatLine(const std::string& cleanLine) {
    std::string who, number, kind;
    if (parseFinalKill(cleanLine, who, number)) kind = "finals";
    else if (parseBedBreak(cleanLine, who, number)) kind = "beds";
    else return;
    std::lock_guard<std::mutex> lk(mu_);
    if (!g_settings.numberDenick || !g_settings.numberDenick->b()) return;
    if (!g_settings.bordicKey || g_settings.bordicKey->text.empty()) return;
    auto n = byName_.find(lower(who));
    if (n == byName_.end()) return;
    Profile& p = profiles_[n->second];
    if (!p.nicked) return;
    std::string& slot = kind == "finals" ? p.finalsNumber : p.bedsNumber;
    if (!slot.empty()) return;   // one lookup per counter
    slot = number;
    p.numberLookup = Lookup::Pending;
    p.numberNote = "Looking up " + std::string(kind == "finals" ? "final kill" : "bed") + " #" + number + "\xE2\x80\xA6";
    jobs_.push_back({Job::Number, p.key, number, kind});
    ensureWorker();
    cv_.notify_one();
}

void Service::resetGame() {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [k, p] : profiles_) {
        (void)k;
        if (p.numberLookup != Lookup::Done) {
            p.finalsNumber.clear();
            p.bedsNumber.clear();
            p.finalsChecked = p.bedsChecked = false;
            p.candidates.clear();
            p.numberLookup = Lookup::Idle;
        }
    }
}

bool Service::find(const std::string& key, Profile& out) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = profiles_.find(key);
    if (it == profiles_.end()) return false;
    out = it->second;
    return true;
}

bool Service::findByName(const std::string& name, Profile& out) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto n = byName_.find(lower(name));
    if (n == byName_.end()) return false;
    auto it = profiles_.find(n->second);
    if (it == profiles_.end()) return false;
    out = it->second;
    return true;
}

std::vector<Profile> Service::nicked() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<Profile> out;
    for (const auto& [k, p] : profiles_) {
        (void)k;
        if (p.nicked) out.push_back(p);
    }
    return out;
}

ServiceStats Service::stats() const {
    std::lock_guard<std::mutex> lk(mu_);
    ServiceStats s = stats_;
    s.pending = static_cast<int>(jobs_.size());
    return s;
}

std::vector<std::string> Service::takeAnnouncements() {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> out;
    out.swap(announcements_);
    return out;
}

void Service::worker() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] { return stop_ || !jobs_.empty(); });
            if (stop_) return;
            job = jobs_.front();
            jobs_.pop_front();
        }
        if (job.type == Job::SkinOwner) runSkinJob(job);
        else runNumberJob(job);
        // Be gentle with public services.
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
}

void Service::runSkinJob(const Job& j) {
    int status = 0;
    std::string body, error;
    const bool ok = fetch("https://sessionserver.mojang.com/session/minecraft/profile/" + j.arg, status, body, error);
    std::string name, id;
    if (ok && status == 200) {
        json::Value v;
        if (json::parse(body, v)) {
            name = v.get("name").asString();
            id = normalizeUuid(v.get("id").asString());
        }
    }
    std::lock_guard<std::mutex> lk(mu_);
    auto it = profiles_.find(j.key);
    if (it == profiles_.end()) return;
    Profile& p = it->second;
    p.updatedMs = epochMs();
    if (!name.empty()) {
        p.skinLookup = Lookup::Done;
        p.realName = name;
        p.realUuid = id;
        if (lower(name) == lower(p.name)) {
            p.skinNote = "The skin belongs to an account with the same name.";
        } else {
            p.skinNote = "Wears " + name + "'s skin. Nicking with your own skin keeps it, so this is most likely "
                         "who they are; a borrowed skin would point elsewhere.";
            if (g_settings.announce && g_settings.announce->b()) announcements_.push_back(p.name + "|" + name);
        }
        log::writef("identity", "skin owner resolved for a nicked player (%s)", p.name.c_str());
    } else {
        p.skinLookup = Lookup::Failed;
        p.skinNote = status == 204 || status == 404 ? "The skin's owner no longer exists."
                                                    : "The lookup failed (" + (error.empty() ? "HTTP " + std::to_string(status) : error) +
                                                          "). Try again from the Identity tab.";
    }
}

void Service::runNumberJob(const Job& j) {
    std::string key;
    {
        std::lock_guard<std::mutex> lk(mu_);
        key = g_settings.bordicKey ? g_settings.bordicKey->text : std::string();
    }
    auto enc = [](const std::string& s) {
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        for (unsigned char c : s) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out.push_back(static_cast<char>(c));
            else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
        }
        return out;
    };
    const std::string url = "https://bordic.xyz/api/v2/resources/lookup/" + j.kind + "?key=" + enc(key) + "&value=" + enc(j.arg) +
                            "&range=200&max=5";
    int status = 0;
    std::string body, error;
    const bool ok = fetch(url, status, body, error);
    std::vector<std::string> names;
    if (ok && status == 200) {
        json::Value v;
        if (json::parse(body, v)) {
            std::function<void(const json::Value&)> walk = [&](const json::Value& x) {
                if (x.isObject()) {
                    const json::Value* n = x.find("name");
                    const json::Value* d = x.find("distance");
                    if (n && n->isString() && d && d->isNumber() && d->asNumber() <= 0.0) names.push_back(n->asString());
                    for (const auto& m : x.members()) walk(m.second);
                } else if (x.isArray()) {
                    for (const auto& i : x.items()) walk(i);
                }
            };
            walk(v);
        }
    }
    std::lock_guard<std::mutex> lk(mu_);
    auto it = profiles_.find(j.key);
    if (it == profiles_.end()) return;
    Profile& p = it->second;
    p.updatedMs = epochMs();
    if (!ok || status != 200) {
        p.numberLookup = Lookup::Failed;
        p.numberNote = status == 401 || status == 403 ? "Bordic rejected the API key." : "The lookup failed.";
        return;
    }
    const bool first = !p.finalsChecked && !p.bedsChecked;
    (j.kind == "finals" ? p.finalsChecked : p.bedsChecked) = true;
    if (first) {
        p.candidates = names;
    } else {
        std::vector<std::string> keep;
        for (const auto& c : p.candidates) {
            for (const auto& n : names) if (lower(n) == lower(c)) { keep.push_back(c); break; }
        }
        p.candidates = keep;
    }
    const bool both = p.finalsChecked && p.bedsChecked;
    if (p.candidates.empty()) {
        p.numberLookup = Lookup::Done;
        p.numberNote = "No account matches the announced counters.";
    } else if (p.candidates.size() == 1 && both) {
        p.numberLookup = Lookup::Done;
        p.numberNote = "Final-kill and bed counters agree on one account.";
        if (g_settings.announce && g_settings.announce->b()) announcements_.push_back(p.name + "|" + p.candidates.front());
    } else {
        p.numberLookup = both ? Lookup::Done : Lookup::Pending;
        p.numberNote = std::to_string(p.candidates.size()) + " candidate account" + (p.candidates.size() == 1 ? "" : "s") +
                       (both ? "." : "; the next announced counter will narrow this down.");
    }
}

}  // namespace vigil::identity

// Vigil feature tests — Utilities (Auto Who, party detectors, Fast Buy) and identity tools
// (nick detection, textures decoding, counter parsing, number denicker narrowing).

#include "../src/core/settings.h"
#include "../src/identity/identity.h"
#include "../src/utilities/utilities.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace vigil;

static int g_failures = 0;
static int g_checks = 0;

#define EXPECT(cond, ...)                                                                  \
    do {                                                                                   \
        ++g_checks;                                                                        \
        if (!(cond)) {                                                                     \
            ++g_failures;                                                                  \
            std::printf("    FAIL %s:%d  %s  — ", __FILE__, __LINE__, #cond);              \
            std::printf(__VA_ARGS__);                                                      \
            std::printf("\n");                                                             \
        }                                                                                  \
    } while (0)

static void testWhoTriggers() {
    using namespace utilities;
    EXPECT(matchesWhoTrigger("Protect your bed and destroy the enemy beds."), "bed wars start");
    EXPECT(matchesWhoTrigger("                Gather resources and equipment on your island"), "skywars start");
    EXPECT(matchesWhoTrigger("Opponent: [MVP+] Someone"), "duels opponent");
    EXPECT(!matchesWhoTrigger("[MVP+] Chatter: protect your bed guys"), "player chat must not trigger");
    EXPECT(!matchesWhoTrigger("Welcome to Hypixel!"), "unrelated line");
    AutoWho w;
    w.setEnabled(true);
    EXPECT(w.onChat("Protect your bed and destroy the enemy beds.", 1000.0, 400.0), "first trigger schedules");
    EXPECT(!w.due(1300.0) && w.due(1400.0), "respects delay");
    w.markSent();
    EXPECT(!w.onChat("Protect your bed and destroy the enemy beds.", 2000.0, 400.0), "cooldown blocks duplicate");
    EXPECT(w.onChat("Protect your bed and destroy the enemy beds.", 9000.0, 0.0), "next game schedules again");
}

// Disabled must mean fully disabled: no scheduling, no queued or delayed sends, no retries.
static void testAutoWhoDisabled() {
    using namespace utilities;
    const char* start = "Protect your bed and destroy the enemy beds.";
    AutoWho off;
    EXPECT(!off.onChat(start, 1000.0, 0.0), "a disabled Auto Who schedules nothing");
    EXPECT(!off.due(5000.0) && !off.pending(), "nothing is due while disabled");

    // Scheduled, then turned off before the delay elapsed: never sent, not even after re-enabling.
    AutoWho w;
    w.setEnabled(true);
    EXPECT(w.onChat(start, 1000.0, 400.0), "sanity: schedules while enabled");
    w.setEnabled(false);
    EXPECT(!w.due(1400.0), "not due after being turned off");
    w.setEnabled(true);
    EXPECT(!w.due(60000.0) && !w.pending(), "re-enabling later does not send the stale /who");

    // Retrying, then turned off: retries stop for good.
    AutoWho r;
    r.setEnabled(true);
    r.onChat(start, 0.0, 0.0);
    EXPECT(r.due(0.0), "sanity: due immediately");
    r.markFailed(0.0);
    r.setEnabled(false);
    r.setEnabled(true);
    EXPECT(!r.due(1000.0) && !r.pending(), "retries do not resume after a disable");

    // The cooldown is not carried across a disable: the next real game start schedules normally.
    EXPECT(r.onChat(start, 1500.0, 0.0), "a fresh game start after re-enabling schedules");
}

static void testPartyChat() {
    using namespace utilities;
    std::string n;
    EXPECT(PartyChatDetector::parseJoin("Kestrel has joined (5/16)!", n) && n == "Kestrel", "join parse");
    EXPECT(PartyChatDetector::parseQuit("Kestrel has quit!", n) && n == "Kestrel", "quit parse");
    EXPECT(!PartyChatDetector::parseJoin("[VIP] Kestrel: has joined (lol/)", n), "player chat ignored");
    PartyChatDetector d;
    d.onChat("Alpha has joined (5/16)!", 0.0);
    d.onChat("Bravo has joined (6/16)!", 40.0);
    d.onChat("Charlie has joined (7/16)!", 80.0);
    EXPECT(d.poll(300.0).empty(), "batch still settling");
    auto out = d.poll(800.0);
    EXPECT(out.size() == 1 && out[0].joined && out[0].size == 3, "party of 3 joined (got %zu)", out.size());
    d.onChat("Solo has joined (8/16)!", 2000.0);
    EXPECT(d.poll(3000.0).empty(), "single join is not a party");
    d.onChat("Alpha has quit!", 4000.0);
    d.onChat("Bravo has quit!", 4010.0);
    out = d.poll(5000.0);
    EXPECT(out.size() == 1 && !out[0].joined && out[0].size == 2, "party of 2 quit");
}

static void testQueueParty() {
    using namespace utilities;
    EXPECT(maxPartySizeFromMode("Mode: Doubles") == 2, "doubles");
    EXPECT(maxPartySizeFromMode("4v4v4v4") == 4, "fours");
    EXPECT(maxPartySizeFromMode("3v3v3v3") == 3, "threes");
    EXPECT(maxPartySizeFromMode("BEDWARS_EIGHT_ONE") == 1, "solo");
    QueuePartyDetector q;
    q.setActive(true, 0.0);
    q.setMaxPartySize(2, 0.0);
    for (int id : {4601, 4602, 4603, 4604, 4610}) q.onSpawn(id, 10.0);
    auto r = q.poll(700.0);
    EXPECT(r.parties.size() == 2, "run of 4 in doubles = 2 parties (got %zu)", r.parties.size());
    if (r.parties.size() == 2) EXPECT(r.parties[0].minId == 4601 && r.parties[1].maxId == 4604, "ranges");
    q.onSpawn(4605, 1000.0);
    r = q.poll(1700.0);
    EXPECT(r.parties.empty(), "already-announced ids are not re-announced");
    QueuePartyDetector fresh;
    fresh.setActive(true, 0.0);
    fresh.setMaxPartySize(4, 0.0);
    fresh.onSpawn(12, 10.0);
    fresh.onSpawn(13, 10.0);
    r = fresh.poll(700.0);
    EXPECT(r.requeueSuggested && r.parties.empty(), "low ids suggest a requeue");
}

static void testFastBuy() {
    EXPECT(utilities::isQuickBuyMenu("\xC2\xA7" "aQuick Buy"), "formatted title");
    EXPECT(!utilities::isQuickBuyMenu("Blocks"), "other shop tab");
}

static void testIdentityHelpers() {
    using namespace identity;
    EXPECT(uuidVersion("0b4f2ba3-6c5d-1e8f-9a0b-1c2d3e4f5a6b") == 1, "v1 nick uuid");
    EXPECT(uuidVersion("069a79f444e94726a5befca90e38aaf5") == 4, "v4 real uuid without dashes");
    EXPECT(uuidVersion("nope") == 0, "malformed");
    EXPECT(normalizeUuid("069A79F444E94726A5BEFCA90E38AAF5") == "069a79f4-44e9-4726-a5be-fca90e38aaf5", "normalize");
    // {"profileId":"069a79f444e94726a5befca90e38aaf5","profileName":"Notch","textures":{"SKIN":{"url":"http://textures.minecraft.net/texture/abc123"}}}
    const std::string b64 =
        "eyJwcm9maWxlSWQiOiIwNjlhNzlmNDQ0ZTk0NzI2YTViZWZjYTkwZTM4YWFmNSIsInByb2ZpbGVOYW1lIjoiTm90Y2giLCJ0ZXh0dXJlcyI6"
        "eyJTS0lOIjp7InVybCI6Imh0dHA6Ly90ZXh0dXJlcy5taW5lY3JhZnQubmV0L3RleHR1cmUvYWJjMTIzIn19fQ==";
    Textures t;
    EXPECT(decodeTextures(b64, t), "textures decode");
    EXPECT(t.profileName == "Notch" && t.profileId == "069a79f4-44e9-4726-a5be-fca90e38aaf5", "owner");
    EXPECT(t.skinHash == "abc123", "skin hash = %s", t.skinHash.c_str());
    EXPECT(nickPoolSize() > 200, "nick pool embedded (%d)", nickPoolSize());
    EXPECT(isNickPoolSkin("4c7b0468044bfecacc43d00a3a69335a834b73937688292c20d3988cae58248d"), "pool hash recognised");
    std::string who, num;
    EXPECT(parseFinalKill("Octa117 was itzalex's final #23,835. FINAL KILL!", who, num) && who == "itzalex" && num == "23835",
           "final kill parse");
    EXPECT(parseFinalKill("Octa117 was itzalex\xE2\x80\x99s final #12. FINAL KILL!", who, num) && who == "itzalex",
           "typographic apostrophe");
    EXPECT(parseBedBreak("BED DESTRUCTION > Your Bed was bed #21,362 destroyed by AidenH!", who, num) && who == "AidenH" &&
               num == "21362",
           "bed parse");
}

static void testIdentityService() {
    using namespace identity;
    registerSettings();
    Service& s = Service::instance();
    std::vector<std::string> urls;
    s.setFetcher([&urls](const std::string& url, int& status, std::string& body, std::string&) {
        urls.push_back(url);
        status = 200;
        if (url.find("sessionserver") != std::string::npos) body = R"({"id":"069a79f444e94726a5befca90e38aaf5","name":"Notch"})";
        else body = url.find("finals") != std::string::npos
                        ? R"({"success":true,"data":[{"name":"Alpha","distance":0},{"name":"Bravo","distance":0}]})"
                        : R"({"success":true,"data":[{"name":"Bravo","distance":0},{"name":"Charlie","distance":0}]})";
        return true;
    });
    const std::string b64 =
        "eyJwcm9maWxlSWQiOiIwNjlhNzlmNDQ0ZTk0NzI2YTViZWZjYTkwZTM4YWFmNSIsInByb2ZpbGVOYW1lIjoiTm90Y2giLCJ0ZXh0dXJlcyI6"
        "eyJTS0lOIjp7InVybCI6Imh0dHA6Ly90ZXh0dXJlcy5taW5lY3JhZnQubmV0L3RleHR1cmUvYWJjMTIzIn19fQ==";
    // A real (v4) player is never looked up.
    s.observe("real", "RealPlayer", "11111111-2222-4333-8444-555555555555", b64);
    // A nicked (v1) player wearing someone else's skin is.
    s.observe("nick", "Disguised", "0b4f2ba3-6c5d-1e8f-9a0b-1c2d3e4f5a6b", b64);
    Settings::instance().find("identity.numberDenick")->set(1.0);
    Settings::instance().find("identity.bordicKey")->setText("test-key");
    s.onChatLine("Victim was Disguised's final #100. FINAL KILL!");
    s.onChatLine("BED DESTRUCTION > Red Bed was bed #200 destroyed by Disguised!");
    for (int i = 0; i < 100; ++i) {
        Profile p;
        if (s.find("nick", p) && p.skinLookup == Lookup::Done && p.numberLookup == Lookup::Done) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    Profile p;
    EXPECT(s.find("nick", p), "nicked profile tracked");
    EXPECT(p.nicked && p.skin == SkinKind::Foreign, "foreign skin");
    EXPECT(p.skinLookup == Lookup::Done && p.realName == "Notch", "skin owner resolved (%s)", p.realName.c_str());
    EXPECT(p.candidates.size() == 1 && p.candidates[0] == "Bravo", "counters intersect to one account (%zu)", p.candidates.size());
    Profile r;
    EXPECT(s.find("real", r) && !r.nicked, "real player not nicked");
    int sessionLookups = 0;
    for (const auto& u : urls) if (u.find("sessionserver") != std::string::npos) ++sessionLookups;
    EXPECT(sessionLookups == 1, "exactly one owner lookup (%d)", sessionLookups);
    s.stop();
}

int main() {
    struct Test { const char* name; void (*fn)(); };
    const Test tests[] = {
        {"who triggers", testWhoTriggers},
        {"auto who disabled means disabled", testAutoWhoDisabled},
        {"party chat detector", testPartyChat},
        {"queue party detector", testQueueParty},
        {"fast buy menu", testFastBuy},
        {"identity helpers", testIdentityHelpers},
        {"identity service", testIdentityService},
    };
    for (const Test& t : tests) {
        const int before = g_failures;
        std::printf("[ RUN  ] %s\n", t.name);
        t.fn();
        std::printf("[ %s ] %s\n", g_failures == before ? " OK " : "FAIL", t.name);
    }
    std::printf("\n%d expectations, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

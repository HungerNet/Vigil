#include "denick.h"
#include "utils.h"
#include "globals.h"
#include "class_finder.h"
#include "jni_helpers.h"
#include <wincrypt.h>
#include <fstream>
#include <deque>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>
#include <regex>
#include <cstdio>
#include <cstdarg>
#include <atomic>

namespace taboverlay {
namespace {

struct DenickEventCounters {
    std::atomic<int> texturesProfileOk{0};
    std::atomic<int> texturesProfileEmpty{0};
    std::atomic<int> base64DecodeOk{0};
    std::atomic<int> base64DecodeFail{0};
    std::atomic<int> skinHashOk{0};
    std::atomic<int> skinHashNoUrl{0};
    std::atomic<int> profileIdOk{0};
    std::atomic<int> profileIdFail{0};
    std::atomic<int> nickKnownHit{0};
    std::atomic<int> nickQueued{0};
    std::atomic<int> workerKnownSkin{0};
    std::atomic<int> workerDenicked{0};
    std::atomic<int> workerFailed{0};
};

DenickEventCounters g_denickCnt;
std::unordered_map<std::string, ULONGLONG> g_denickLogLast;
std::string g_knownNicksLoadPath;

bool DenickShouldLog(const std::string& key, ULONGLONG intervalMs = 4000) {
    const ULONGLONG now = GetTickCount64();
    auto it = g_denickLogLast.find(key);
    if (it != g_denickLogLast.end() && (now - it->second) < intervalMs) return false;
    g_denickLogLast[key] = now;
    return true;
}

std::string HashPreview(const std::string& h) {
    if (h.empty()) return "(empty)";
    if (h.size() <= 20) return h;
    return h.substr(0, 20) + "...";
}

const char* DenickStateLabel(DenickOverlayData::State s) {
    switch (s) {
    case DenickOverlayData::State::Unknown: return "Unknown";
    case DenickOverlayData::State::Pending: return "Pending";
    case DenickOverlayData::State::KnownNickSkin: return "KnownNickSkin";
    case DenickOverlayData::State::Denicked: return "Denicked";
    case DenickOverlayData::State::Failed: return "Failed";
    }
    return "?";
}

void DenickLogf(const char*, ...) {}

void DenickKnownCheckLogf(const char*, ...) {}

constexpr ULONGLONG kDenickRetryMs = 120ULL * 1000ULL;
constexpr int kMaxDenickQueue = 24;

struct SkinRecord {
    std::string displayName;
    std::string texturesBase64;
};

struct DenickEntry {
    DenickOverlayData::State state = DenickOverlayData::State::Unknown;
    ULONGLONG updatedAt = 0;
    std::string skinHash;
    std::string realName;
    std::string realUuid;
    CachedHypixelStats hyp{};
    CachedSeraphTag seraph{};
    bool hypOk = false;
    bool seraphOk = false;
    bool chatPending = false;
    bool knownNickChatPending = false;
};

struct DenickJob {
    std::string nickKey;
    std::string texturesBase64;
    std::string skinHash;
};

std::mutex g_denickMu;
std::condition_variable g_denickCv;
std::deque<DenickJob> g_denickJobs;
std::unordered_set<std::string> g_denickQueued;
std::unordered_map<std::string, SkinRecord> g_skinByHash;
std::unordered_map<std::string, DenickEntry> g_denickByNick;
std::unordered_set<std::string> g_knownNickSkins;
std::unordered_set<std::string> g_knownCheckLoggedThisGame;
bool g_knownNicksLoaded = false;
std::string g_denickHypKey;
std::string g_denickSerKey;
bool g_denickEnabled = false;
bool g_denickWorkerStop = false;
bool g_denickWorkerOn = false;
std::thread g_denickWorker;

std::string CanonKey(const std::string& ign) {
    return LowerAscii(Trim(StripMinecraftFormatting(ign)));
}

void LogSkinHashKnownCheck(const std::string& nickIgn, const std::string& skinHash,
                           const std::string& uuidHint) {
    const std::string nickKey = CanonKey(nickIgn);
    if (nickKey.empty()) return;
    if (g_knownCheckLoggedThisGame.count(nickKey)) return;
    g_knownCheckLoggedThisGame.insert(nickKey);
    const bool inList = !skinHash.empty() && g_knownNickSkins.count(skinHash) > 0;
    DenickKnownCheckLogf("[Denick][KnownCheck] nick=%s uuid=%s hash=%s in_known_nicks=%s\n",
                       nickIgn.c_str(),
                       uuidHint.empty() ? "(none)" : uuidHint.c_str(),
                       skinHash.empty() ? "(empty)" : skinHash.c_str(),
                       inList ? "yes" : "no");
}

static bool DenickSkinMatchesRegisteredReal(const std::string& realNameKey, const std::string& skinHash) {
    if (skinHash.empty()) return false;
    bool hasRegistered = false;
    for (const auto& pr : g_skinByHash) {
        if (CanonKey(pr.second.displayName) == realNameKey) {
            hasRegistered = true;
            if (pr.first == skinHash) return true;
        }
    }
    return !hasRegistered;
}

bool Base64Decode(const std::string& in, std::string& out, const char* logCtx = nullptr) {
    out.clear();
    if (in.empty()) {
        g_denickCnt.base64DecodeFail.fetch_add(1, std::memory_order_relaxed);
        if (logCtx && DenickShouldLog(std::string("b64:empty:") + logCtx)) {
            DenickLogf("[Denick][B64] empty input ctx=%s\n", logCtx);
        }
        return false;
    }
    DWORD outLen = 0;
    if (!CryptStringToBinaryA(in.c_str(), (DWORD)in.size(), CRYPT_STRING_BASE64,
                              nullptr, &outLen, nullptr, nullptr) || outLen == 0) {
        g_denickCnt.base64DecodeFail.fetch_add(1, std::memory_order_relaxed);
        if (logCtx && DenickShouldLog(std::string("b64:size:") + logCtx)) {
            DenickLogf("[Denick][B64] size probe failed ctx=%s inLen=%zu\n", logCtx, in.size());
        }
        return false;
    }
    out.resize(outLen);
    if (!CryptStringToBinaryA(in.c_str(), (DWORD)in.size(), CRYPT_STRING_BASE64,
                              (BYTE*)&out[0], &outLen, nullptr, nullptr)) {
        out.clear();
        g_denickCnt.base64DecodeFail.fetch_add(1, std::memory_order_relaxed);
        if (logCtx && DenickShouldLog(std::string("b64:decode:") + logCtx)) {
            DenickLogf("[Denick][B64] decode failed ctx=%s inLen=%zu\n", logCtx, in.size());
        }
        return false;
    }
    out.resize(outLen);
    g_denickCnt.base64DecodeOk.fetch_add(1, std::memory_order_relaxed);
    if (logCtx && DenickShouldLog(std::string("b64:ok:") + logCtx, 8000)) {
        DenickLogf("[Denick][B64] ok ctx=%s inLen=%zu outLen=%zu\n", logCtx, in.size(), out.size());
    }
    return true;
}

// Same hash key as Vigil Lite proxy: last path segment of textures.SKIN.url.
static bool IsHexSkinHash(const std::string& s) {
    if (s.size() < 32) return false;
    for (char c : s) {
        const char lc = (char)std::tolower((unsigned char)c);
        if (!((lc >= '0' && lc <= '9') || (lc >= 'a' && lc <= 'f'))) return false;
    }
    return true;
}

static std::string ParseSkinHashFromResourcePath(const std::string& path) {
    if (path.empty()) return {};
    std::string tail = path;
    const size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && slash + 1 < path.size()) {
        tail = path.substr(slash + 1);
    }
    const size_t dot = tail.find('.');
    if (dot != std::string::npos) tail = tail.substr(0, dot);
    tail = LowerAscii(tail);
    return IsHexSkinHash(tail) ? tail : std::string{};
}

static std::string JniGetStringMethod(JNIEnv* env, jobject obj, const char* method, const char* sig) {
    if (!env || !obj) return {};
    jclass cls = env->GetObjectClass(obj);
    if (!cls || env->ExceptionCheck()) {
        env->ExceptionClear();
        return {};
    }
    jmethodID mid = env->GetMethodID(cls, method, sig);
    env->DeleteLocalRef(cls);
    if (!mid || env->ExceptionCheck()) {
        env->ExceptionClear();
        return {};
    }
    jstring jStr = (jstring)env->CallObjectMethod(obj, mid);
    if (!jStr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return {};
    }
    const char* c = env->GetStringUTFChars(jStr, nullptr);
    std::string out = c ? c : "";
    if (c) env->ReleaseStringUTFChars(jStr, c);
    env->DeleteLocalRef(jStr);
    return out;
}

static std::string ReadTexturesPropertyFromMap(JNIEnv* env, jobject propMap) {
    if (!env || !propMap) return {};

    auto tryGetCollection = [&](jobject mapObj) -> std::string {
        jclass mapClass = env->GetObjectClass(mapObj);
        jmethodID midGet = mapClass ? env->GetMethodID(mapClass, "get", "(Ljava/lang/String;)Ljava/util/Collection;") : nullptr;
        if (mapClass) env->DeleteLocalRef(mapClass);
        if (!midGet || env->ExceptionCheck()) {
            env->ExceptionClear();
            return {};
        }
        jstring jKey = env->NewStringUTF("textures");
        jobject collection = env->CallObjectMethod(mapObj, midGet, jKey);
        env->DeleteLocalRef(jKey);
        if (!collection || env->ExceptionCheck()) {
            env->ExceptionClear();
            return {};
        }

        jclass collClass = env->GetObjectClass(collection);
        jmethodID midIter = collClass ? env->GetMethodID(collClass, "iterator", "()Ljava/util/Iterator;") : nullptr;
        if (collClass) env->DeleteLocalRef(collClass);
        if (!midIter || env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(collection);
            return {};
        }

        jobject iter = env->CallObjectMethod(collection, midIter);
        env->DeleteLocalRef(collection);
        if (!iter || env->ExceptionCheck()) {
            env->ExceptionClear();
            return {};
        }

        jclass iterClass = env->GetObjectClass(iter);
        jmethodID midHasNext = iterClass ? env->GetMethodID(iterClass, "hasNext", "()Z") : nullptr;
        jmethodID midNext = iterClass ? env->GetMethodID(iterClass, "next", "()Ljava/lang/Object;") : nullptr;
        if (iterClass) env->DeleteLocalRef(iterClass);

        std::string value;
        if (midHasNext && midNext && env->CallBooleanMethod(iter, midHasNext)) {
            jobject propObj = env->CallObjectMethod(iter, midNext);
            if (propObj && !env->ExceptionCheck()) {
                jclass propClass = env->GetObjectClass(propObj);
                jmethodID midGetValue = propClass ? env->GetMethodID(propClass, "getValue", "()Ljava/lang/String;") : nullptr;
                if (midGetValue && !env->ExceptionCheck()) {
                    jstring jVal = (jstring)env->CallObjectMethod(propObj, midGetValue);
                    if (jVal && !env->ExceptionCheck()) {
                        const char* c = env->GetStringUTFChars(jVal, nullptr);
                        if (c) value = c;
                        if (c) env->ReleaseStringUTFChars(jVal, c);
                        env->DeleteLocalRef(jVal);
                    } else {
                        env->ExceptionClear();
                    }
                } else {
                    env->ExceptionClear();
                }
                if (propClass) env->DeleteLocalRef(propClass);
                env->DeleteLocalRef(propObj);
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(iter);
        return value;
    };

    std::string value = tryGetCollection(propMap);
    if (!value.empty()) return value;

    jclass mapClass = env->GetObjectClass(propMap);
    jmethodID midValues = mapClass ? env->GetMethodID(mapClass, "values", "()Ljava/util/Collection;") : nullptr;
    if (midValues && !env->ExceptionCheck()) {
        jobject values = env->CallObjectMethod(propMap, midValues);
        if (values && !env->ExceptionCheck()) {
            jclass collClass = env->GetObjectClass(values);
            jmethodID midIter = collClass ? env->GetMethodID(collClass, "iterator", "()Ljava/util/Iterator;") : nullptr;
            if (collClass) env->DeleteLocalRef(collClass);
            if (midIter) {
                jobject iter = env->CallObjectMethod(values, midIter);
                if (iter && !env->ExceptionCheck()) {
                    jclass iterClass = env->GetObjectClass(iter);
                    jmethodID midHasNext = iterClass ? env->GetMethodID(iterClass, "hasNext", "()Z") : nullptr;
                    jmethodID midNext = iterClass ? env->GetMethodID(iterClass, "next", "()Ljava/lang/Object;") : nullptr;
                    if (iterClass) env->DeleteLocalRef(iterClass);
                    if (midHasNext && midNext) {
                        while (env->CallBooleanMethod(iter, midHasNext)) {
                            jobject propObj = env->CallObjectMethod(iter, midNext);
                            if (!propObj || env->ExceptionCheck()) {
                                env->ExceptionClear();
                                break;
                            }
                            jclass propClass = env->GetObjectClass(propObj);
                            jmethodID midGetName = propClass ? env->GetMethodID(propClass, "getName", "()Ljava/lang/String;") : nullptr;
                            jmethodID midGetValue = propClass ? env->GetMethodID(propClass, "getValue", "()Ljava/lang/String;") : nullptr;
                            if (midGetName && midGetValue) {
                                jstring jName = (jstring)env->CallObjectMethod(propObj, midGetName);
                                if (jName && !env->ExceptionCheck()) {
                                    const char* n = env->GetStringUTFChars(jName, nullptr);
                                    const bool isTex = n && LowerAscii(n) == "textures";
                                    if (n) env->ReleaseStringUTFChars(jName, n);
                                    env->DeleteLocalRef(jName);
                                    if (isTex) {
                                        jstring jVal = (jstring)env->CallObjectMethod(propObj, midGetValue);
                                        if (jVal && !env->ExceptionCheck()) {
                                            const char* c = env->GetStringUTFChars(jVal, nullptr);
                                            if (c) value = c;
                                            if (c) env->ReleaseStringUTFChars(jVal, c);
                                            env->DeleteLocalRef(jVal);
                                        } else {
                                            env->ExceptionClear();
                                        }
                                    }
                                } else {
                                    env->ExceptionClear();
                                }
                            }
                            if (propClass) env->DeleteLocalRef(propClass);
                            env->DeleteLocalRef(propObj);
                            if (!value.empty()) break;
                        }
                    }
                    env->DeleteLocalRef(iter);
                } else {
                    env->ExceptionClear();
                }
            }
            env->DeleteLocalRef(values);
        } else {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }
    if (!value.empty()) return value;

    if (mapClass) env->DeleteLocalRef(mapClass);
    mapClass = env->GetObjectClass(propMap);
    jmethodID midEntries = mapClass ? env->GetMethodID(mapClass, "entries", "()Ljava/util/Set;") : nullptr;
    if (mapClass) env->DeleteLocalRef(mapClass);
    if (!midEntries || env->ExceptionCheck()) {
        env->ExceptionClear();
        return {};
    }

    jobject entries = env->CallObjectMethod(propMap, midEntries);
    if (!entries || env->ExceptionCheck()) {
        env->ExceptionClear();
        return {};
    }

    jclass setClass = env->GetObjectClass(entries);
    jmethodID midIter = setClass ? env->GetMethodID(setClass, "iterator", "()Ljava/util/Iterator;") : nullptr;
    if (setClass) env->DeleteLocalRef(setClass);
    if (!midIter || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(entries);
        return {};
    }

    jobject iter = env->CallObjectMethod(entries, midIter);
    env->DeleteLocalRef(entries);
    if (!iter || env->ExceptionCheck()) {
        env->ExceptionClear();
        return {};
    }

    jclass iterClass = env->GetObjectClass(iter);
    jmethodID midHasNext = iterClass ? env->GetMethodID(iterClass, "hasNext", "()Z") : nullptr;
    jmethodID midNext = iterClass ? env->GetMethodID(iterClass, "next", "()Ljava/lang/Object;") : nullptr;
    if (iterClass) env->DeleteLocalRef(iterClass);

    if (midHasNext && midNext) {
        while (env->CallBooleanMethod(iter, midHasNext)) {
            jobject entry = env->CallObjectMethod(iter, midNext);
            if (!entry || env->ExceptionCheck()) {
                env->ExceptionClear();
                break;
            }
            jclass entryClass = env->GetObjectClass(entry);
            jmethodID midGetKey = entryClass ? env->GetMethodID(entryClass, "getKey", "()Ljava/lang/Object;") : nullptr;
            jmethodID midGetVal = entryClass ? env->GetMethodID(entryClass, "getValue", "()Ljava/lang/Object;") : nullptr;
            if (entryClass) env->DeleteLocalRef(entryClass);

            bool isTextures = false;
            if (midGetKey) {
                jstring jKey = (jstring)env->CallObjectMethod(entry, midGetKey);
                if (jKey && !env->ExceptionCheck()) {
                    const char* k = env->GetStringUTFChars(jKey, nullptr);
                    if (k && std::string(k) == "textures") isTextures = true;
                    if (k) env->ReleaseStringUTFChars(jKey, k);
                    env->DeleteLocalRef(jKey);
                } else {
                    env->ExceptionClear();
                }
            }

            if (isTextures && midGetVal) {
                jobject propObj = env->CallObjectMethod(entry, midGetVal);
                if (propObj && !env->ExceptionCheck()) {
                    jclass propClass = env->GetObjectClass(propObj);
                    jmethodID midGetValue = propClass ? env->GetMethodID(propClass, "getValue", "()Ljava/lang/String;") : nullptr;
                    if (midGetValue && !env->ExceptionCheck()) {
                        jstring jVal = (jstring)env->CallObjectMethod(propObj, midGetValue);
                        if (jVal && !env->ExceptionCheck()) {
                            const char* c = env->GetStringUTFChars(jVal, nullptr);
                            if (c) value = c;
                            if (c) env->ReleaseStringUTFChars(jVal, c);
                            env->DeleteLocalRef(jVal);
                        } else {
                            env->ExceptionClear();
                        }
                    } else {
                        env->ExceptionClear();
                    }
                    if (propClass) env->DeleteLocalRef(propClass);
                    env->DeleteLocalRef(propObj);
                } else {
                    env->ExceptionClear();
                }
            }
            env->DeleteLocalRef(entry);
            if (!value.empty()) break;
        }
    }
    env->DeleteLocalRef(iter);
    return value;
}

std::string ExtractSkinHashFromTextures(const std::string& texturesBase64, const char* logCtx = nullptr) {
    std::string decoded;
    if (!Base64Decode(texturesBase64, decoded, logCtx)) return {};

    std::regex skinUrlRgx("\"SKIN\"\\s*:\\s*\\{[^}]*\"url\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch m;
    if (!std::regex_search(decoded, m, skinUrlRgx)) {
        g_denickCnt.skinHashNoUrl.fetch_add(1, std::memory_order_relaxed);
        if (logCtx && DenickShouldLog(std::string("skin:no_url:") + logCtx)) {
            DenickLogf("[Denick][Skin] no SKIN.url ctx=%s decodedLen=%zu\n", logCtx, decoded.size());
        }
        return {};
    }
    const std::string& url = m[1].str();
    const size_t slash = url.find_last_of('/');
    if (slash == std::string::npos || slash + 1 >= url.size()) {
        g_denickCnt.skinHashNoUrl.fetch_add(1, std::memory_order_relaxed);
        if (logCtx && DenickShouldLog(std::string("skin:bad_url:") + logCtx)) {
            DenickLogf("[Denick][Skin] bad url ctx=%s url=%s\n", logCtx, url.c_str());
        }
        return {};
    }
    std::string hash = url.substr(slash + 1);
    const size_t q = hash.find('?');
    if (q != std::string::npos) hash = hash.substr(0, q);
    hash = LowerAscii(hash);
    g_denickCnt.skinHashOk.fetch_add(1, std::memory_order_relaxed);
    return hash;
}

void LoadKnownNickSkinsLocked() {
    if (g_knownNicksLoaded) return;
    g_knownNicksLoaded = true;

    std::vector<std::string> paths;
    if (!g_dllDir.empty()) {
        paths.push_back(g_dllDir + "\\config\\known-nicks.json");
        paths.push_back(g_dllDir + "\\known-nicks.json");
    }

    const std::regex hexRgx("\"([0-9a-fA-F]+)\"");
    bool loaded = false;
    for (const std::string& path : paths) {
        std::ifstream fin(path, std::ios::binary);
        if (!fin) {
            if (DenickShouldLog("known:miss:" + path, 15000)) {
                DenickLogf("[Denick][Known] file not found path=%s\n", path.c_str());
            }
            continue;
        }
        const size_t before = g_knownNickSkins.size();
        std::string content((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
        for (std::sregex_iterator it(content.begin(), content.end(), hexRgx), end; it != end; ++it) {
            const std::string h = LowerAscii(it->str(1));
            if (!h.empty()) g_knownNickSkins.insert(h);
        }
        g_knownNicksLoadPath = path;
        loaded = true;
        DenickLogf("[Denick][Known] loaded path=%s entries=%zu (+%zu)\n",
                   path.c_str(), g_knownNickSkins.size(), g_knownNickSkins.size() - before);
        break;
    }
    if (!loaded && DenickShouldLog("known:none", 20000)) {
        DenickLogf("[Denick][Known] no known-nicks.json found (dllDir=%s)\n", g_dllDir.c_str());
    }
}

bool ExtractJsonStringLoose(const std::string& json, const std::string& key, std::string& out) {
    std::regex rgx("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch m;
    if (!std::regex_search(json, m, rgx)) return false;
    out = m[1].str();
    return true;
}

std::string UrlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

std::string ExtractUuidFromTexturesValue(const std::string& texturesBase64, const char* logCtx = nullptr) {
    std::string decoded;
    if (!Base64Decode(texturesBase64, decoded, logCtx)) {
        g_denickCnt.profileIdFail.fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    std::string rawId;
    if (!ExtractJsonStringLoose(decoded, "profileId", rawId)) {
        g_denickCnt.profileIdFail.fetch_add(1, std::memory_order_relaxed);
        if (logCtx && DenickShouldLog(std::string("uuid:no_id:") + logCtx)) {
            DenickLogf("[Denick][UUID] no profileId ctx=%s decodedLen=%zu\n", logCtx, decoded.size());
        }
        return {};
    }
    std::string out;
    std::string compact;
    for (char c : rawId) {
        if (c != '-') compact.push_back(c);
    }
    if (compact.size() != 32) {
        g_denickCnt.profileIdFail.fetch_add(1, std::memory_order_relaxed);
        if (logCtx && DenickShouldLog(std::string("uuid:bad_len:") + logCtx)) {
            DenickLogf("[Denick][UUID] bad profileId len=%zu ctx=%s raw=%s\n",
                       compact.size(), logCtx, rawId.c_str());
        }
        return {};
    }
    for (size_t i = 0; i < compact.size(); ++i) {
        if (i == 8 || i == 12 || i == 16 || i == 20) out.push_back('-');
        out.push_back((char)std::tolower((unsigned char)compact[i]));
    }
    g_denickCnt.profileIdOk.fetch_add(1, std::memory_order_relaxed);
    if (logCtx && DenickShouldLog(std::string("uuid:ok:") + logCtx, 6000)) {
        DenickLogf("[Denick][UUID] ok ctx=%s uuid=%s\n", logCtx, out.c_str());
    }
    return out;
}

std::string RemoveDashes(const std::string& uuid) {
    std::string out;
    out.reserve(uuid.size());
    for (char c : uuid) if (c != '-') out.push_back(c);
    return out;
}

bool NormalizeUuid32(const std::string& compact, std::string& out) {
    out.clear();
    std::string c = RemoveDashes(compact);
    if (c.size() != 32) return false;
    for (char ch : c) {
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    }
    for (size_t i = 0; i < c.size(); ++i) {
        if (i == 8 || i == 12 || i == 16 || i == 20) out.push_back('-');
        out.push_back((char)std::tolower((unsigned char)c[i]));
    }
    return true;
}

bool FetchMojangName(const std::string& uuidNoDash, std::string& outName, std::string& outCanonUuid) {
    outName.clear();
    outCanonUuid.clear();
    if (uuidNoDash.empty()) return false;
    std::string body;
    DWORD status = 0;
    const std::string url = "https://sessionserver.mojang.com/session/minecraft/profile/" + uuidNoDash;
    if (!HttpGetHttps(url, body, status) || status != 200) return false;
    std::string rawId, name;
    if (!ExtractJsonStringLoose(body, "id", rawId) || !ExtractJsonStringLoose(body, "name", name)) {
        return false;
    }
    std::string canon;
    if (!NormalizeUuid32(rawId, canon)) return false;
    outName = name;
    outCanonUuid = canon;
    return !outName.empty();
}

bool FetchHypixelByName(const std::string& key, const std::string& name, CachedHypixelStats& out,
                        std::string& outUuid, bool& noPlayer) {
    noPlayer = false;
    outUuid.clear();
    if (key.empty() || name.empty()) return false;
    std::string body;
    DWORD status = 0;
    const std::string url = "https://api.hypixel.net/v2/player?key=" + UrlEncode(key) + "&name=" + UrlEncode(name);
    if (!HttpGetHttps(url, body, status) || status != 200) return false;

    bool apiOk = (body.find("\"success\":true") != std::string::npos);
    int sn = 0;
    std::regex snRgx("\"success\"\\s*:\\s*(true|false)");
    std::smatch sm;
    if (std::regex_search(body, sm, snRgx)) apiOk = (sm[1].str() == "true");
    if (!apiOk) {
        noPlayer = true;
        return false;
    }

    std::regex playerRgx("\"player\"\\s*:\\s*\\{");
    if (!std::regex_search(body, playerRgx)) {
        noPlayer = true;
        return false;
    }

    std::regex uuidRgx("\"uuid\"\\s*:\\s*\"([^\"]*)\"");
    if (std::regex_search(body, sm, uuidRgx)) {
        std::string canon;
        if (NormalizeUuid32(sm[1].str(), canon)) outUuid = canon;
    }

    out = CachedHypixelStats{};
    auto extractInt = [&](const char* key, int& v) {
        std::regex rgx(std::string("\"") + key + "\"\\s*:\\s*(-?[0-9]+)");
        if (std::regex_search(body, sm, rgx)) v = atoi(sm[1].str().c_str());
    };

    int stars = 0, gp = 0;
    extractInt("bedwars_level", stars);
    extractInt("games_played_bedwars", gp);
    if (gp <= 0 && stars <= 0) {
        noPlayer = true;
        return false;
    }
    out.stars = stars;
    extractInt("wins_bedwars", out.wins);
    extractInt("losses_bedwars", out.losses);
    extractInt("final_kills_bedwars", out.finalKills);
    extractInt("final_deaths_bedwars", out.finalDeaths);
    extractInt("beds_broken_bedwars", out.bedsBroken);
    extractInt("beds_lost_bedwars", out.bedsLost);
    extractInt("kills_bedwars", out.kills);
    extractInt("deaths_bedwars", out.deaths);
    extractInt("entity_attack_final_kills_bedwars", out.entityAttackFinalKills);
    extractInt("entity_attack_kills_bedwars", out.entityAttackKills);
    extractInt("entity_attack_final_deaths_bedwars", out.entityAttackFinalDeaths);
    extractInt("entity_attack_deaths_bedwars", out.entityAttackDeaths);
    extractInt("magic_final_deaths_bedwars", out.magicFinalDeaths);
    extractInt("games_played_bedwars", out.gamesPlayed);
    extractInt("winstreak", out.winstreak);
    if (out.losses <= 0) out.losses = 1;
    if (out.finalDeaths <= 0) out.finalDeaths = 1;
    if (out.bedsLost <= 0) out.bedsLost = 1;
    if (out.deaths <= 0) out.deaths = 1;
    out.ok = true;
    out.fetchedAt = GetTickCount64();
    return true;
}

bool FetchSeraph(const std::string& key, const std::string& uuid, CachedSeraphTag& out) {
    if (key.empty() || uuid.empty()) return false;
    std::string canon;
    if (!NormalizeUuid32(uuid, canon)) canon = LowerAscii(uuid);
    std::string body;
    DWORD status = 0;
    // Seraph v2 path: /{uuid}/blacklist?key=...
    const std::string url = "https://api.seraph.si/" + canon + "/blacklist?key=" + UrlEncode(key);
    const std::string hdr = "seraph-api-key: " + key;
    if (!HttpGetHttps(url, body, status, hdr) || status != 200) return false;

    std::regex successRgx("\"success\"\\s*:\\s*(true|false)");
    std::smatch sm;
    if (!std::regex_search(body, sm, successRgx) || sm[1].str() != "true") return false;

    // Prefer blacklist.tagged (response also has member/bot/safelist tagged fields).
    std::regex blTaggedRgx("\"blacklist\"\\s*:\\s*\\{[\\s\\S]*?\"tagged\"\\s*:\\s*(true|false)");
    if (!std::regex_search(body, sm, blTaggedRgx)) return false;
    if (sm[1].str() != "true") {
        out = CachedSeraphTag{};
        out.resolved = true;
        out.blacklisted = false;
        out.fetchedAt = GetTickCount64();
        return true;
    }

    std::string reportType, reason, tooltip;
    ExtractJsonStringLoose(body, "report_type", reportType);
    ExtractJsonStringLoose(body, "reason", reason);
    ExtractJsonStringLoose(body, "tooltip", tooltip);
    out.resolved = true;
    out.blacklisted = true;
    out.reportType = reportType;
    out.tag = reportType.empty() ? (reason.empty() ? tooltip : reason) : reportType;
    out.reason = reason.empty() ? tooltip : reason;
    out.tooltip = tooltip;
    out.fetchedAt = GetTickCount64();
    return true;
}

static void CancelDenickJobsForNickLocked(const std::string& nickKey) {
    for (auto it = g_denickJobs.begin(); it != g_denickJobs.end(); ) {
        if (it->nickKey == nickKey) {
            g_denickQueued.erase(it->nickKey + "|" + it->skinHash);
            it = g_denickJobs.erase(it);
        } else {
            ++it;
        }
    }
}

void PushDenickJobLocked(DenickJob job) {
    if (job.skinHash.empty()) return;
    if ((int)g_denickJobs.size() >= kMaxDenickQueue) return;
    const std::string qk = job.nickKey + "|" + job.skinHash;
    if (!g_denickQueued.insert(qk).second) return;
    g_denickJobs.push_back(std::move(job));
    g_denickCv.notify_one();
}

void DenickWorkerLoop() {
    for (;;) {
        DenickJob job;
        std::string hypKey, serKey;
        {
            std::unique_lock<std::mutex> lk(g_denickMu);
            g_denickCv.wait(lk, [] { return g_denickWorkerStop || !g_denickJobs.empty(); });
            if (g_denickWorkerStop) return;
            job = g_denickJobs.front();
            g_denickJobs.pop_front();
            hypKey = g_denickHypKey;
            serKey = g_denickSerKey;
        }

        DenickEntry result{};
        result.state = DenickOverlayData::State::Failed;
        result.updatedAt = GetTickCount64();

        {
            std::lock_guard<std::mutex> lk(g_denickMu);
            LoadKnownNickSkinsLocked();
        }
        if (job.skinHash.empty() && !job.texturesBase64.empty()) {
            job.skinHash = ExtractSkinHashFromTextures(job.texturesBase64, job.nickKey.c_str());
        }
        result.skinHash = job.skinHash;

        if (!job.skinHash.empty() && g_knownNickSkins.count(job.skinHash)) {
            result.state = DenickOverlayData::State::KnownNickSkin;
            result.knownNickChatPending = true;
            g_denickCnt.workerKnownSkin.fetch_add(1, std::memory_order_relaxed);
            if (DenickShouldLog("worker:known:" + job.nickKey)) {
                DenickLogf("[Denick][Worker] known skin nick=%s hash=%s\n",
                           job.nickKey.c_str(), HashPreview(job.skinHash).c_str());
            }
        } else if (job.skinHash.empty()) {
            if (DenickShouldLog("worker:no_hash:" + job.nickKey, 8000)) {
                DenickLogf("[Denick][Worker] skip nick=%s (skin hash not ready)\n", job.nickKey.c_str());
            }
        } else {
            const std::string profileUuid = ExtractUuidFromTexturesValue(job.texturesBase64, job.nickKey.c_str());
            if (!profileUuid.empty()) {
                std::string mojangName, canonUuid;
                if (FetchMojangName(RemoveDashes(profileUuid), mojangName, canonUuid)) {
                    const std::string nickLower = job.nickKey;
                    const std::string realLower = LowerAscii(mojangName);
                    if (!realLower.empty() && realLower != nickLower &&
                        DenickSkinMatchesRegisteredReal(realLower, job.skinHash)) {
                        result.realName = mojangName;
                        result.realUuid = canonUuid;
                        bool noPlayer = false;
                        CachedHypixelStats hyp{};
                        std::string hypUuid;
                        CachedSeraphTag ser{};
                        bool hypFetchOk = false;
                        bool serFetchOk = false;

                        const std::string serUuid = canonUuid;
                        std::thread hypThread;
                        std::thread serThread;
                        if (!hypKey.empty()) {
                            hypThread = std::thread([&]() {
                                hypFetchOk = FetchHypixelByName(hypKey, mojangName, hyp, hypUuid, noPlayer) && !noPlayer;
                            });
                        }
                        if (!serKey.empty() && !serUuid.empty()) {
                            serThread = std::thread([&]() {
                                serFetchOk = FetchSeraph(serKey, serUuid, ser);
                            });
                        }
                        if (hypThread.joinable()) hypThread.join();
                        if (serThread.joinable()) serThread.join();

                        if (hypFetchOk) {
                            result.hyp = hyp;
                            result.hypOk = true;
                            if (!hypUuid.empty()) result.realUuid = hypUuid;
                        }
                        if (serFetchOk) {
                            result.seraph = ser;
                            result.seraphOk = true;
                        }
                        if (result.hypOk) {
                            result.state = DenickOverlayData::State::Denicked;
                            result.chatPending = true;
                            g_denickCnt.workerDenicked.fetch_add(1, std::memory_order_relaxed);
                            if (DenickShouldLog("worker:dn:" + job.nickKey)) {
                                DenickLogf("[Denick][Worker] denicked nick=%s -> %s hypStars=%d\n",
                                           job.nickKey.c_str(), result.realName.c_str(), result.hyp.stars);
                            }
                        } else if (DenickShouldLog("worker:hyp_fail:" + job.nickKey, 8000)) {
                            DenickLogf("[Denick][Worker] mojang ok but hypixel fail nick=%s real=%s\n",
                                       job.nickKey.c_str(), mojangName.c_str());
                        }
                    } else if (!realLower.empty() && realLower != nickLower &&
                               DenickShouldLog("worker:skin_mismatch:" + job.nickKey, 8000)) {
                        DenickLogf("[Denick][Worker] skin mismatch nick=%s -> %s hash=%s (skip)\n",
                                   job.nickKey.c_str(), mojangName.c_str(),
                                   HashPreview(job.skinHash).c_str());
                    }
                }
            }
        }

        if (result.state == DenickOverlayData::State::Failed) {
            g_denickCnt.workerFailed.fetch_add(1, std::memory_order_relaxed);
            if (DenickShouldLog("worker:fail:" + job.nickKey, 8000)) {
                DenickLogf("[Denick][Worker] failed nick=%s hash=%s\n",
                           job.nickKey.c_str(), HashPreview(job.skinHash).c_str());
            }
        }

        {
            std::lock_guard<std::mutex> lk(g_denickMu);
            g_denickQueued.erase(job.nickKey + "|" + job.skinHash);
            DenickEntry& e = g_denickByNick[job.nickKey];
            if (!job.skinHash.empty() && g_knownNickSkins.count(job.skinHash)) {
                e.state = DenickOverlayData::State::KnownNickSkin;
                e.updatedAt = result.updatedAt;
                e.skinHash = job.skinHash;
                e.realName.clear();
                e.realUuid.clear();
                e.hypOk = false;
                e.seraphOk = false;
                e.chatPending = false;
                e.knownNickChatPending = true;
            } else if (e.state == DenickOverlayData::State::KnownNickSkin) {
                // Known nick skins must not be overwritten by a late worker result.
            } else if (e.state == DenickOverlayData::State::Pending || e.updatedAt == 0 ||
                       result.state == DenickOverlayData::State::Denicked) {
                e = result;
                e.skinHash = job.skinHash;
            }
        }

        Sleep(120);
    }
}

void StartDenickWorkerLocked() {
    if (g_denickWorkerOn) return;
    g_denickWorkerStop = false;
    if (g_denickWorker.joinable()) g_denickWorker.join();
    g_denickWorker = std::thread(DenickWorkerLoop);
    g_denickWorkerOn = true;
}

void StopDenickWorker() {
    {
        std::lock_guard<std::mutex> lk(g_denickMu);
        if (!g_denickWorkerOn) return;
        g_denickWorkerStop = true;
        g_denickJobs.clear();
        g_denickQueued.clear();
    }
    g_denickCv.notify_all();
    if (g_denickWorker.joinable()) g_denickWorker.join();
    std::lock_guard<std::mutex> lk(g_denickMu);
    g_denickWorkerOn = false;
}

} // namespace

void DenickOnGameStarted() {
    std::lock_guard<std::mutex> lk(g_denickMu);
    g_knownCheckLoggedThisGame.clear();
}

void DenickSetEnabled(bool enabled) {
    std::lock_guard<std::mutex> lk(g_denickMu);
    LoadKnownNickSkinsLocked();
    if (g_denickEnabled != enabled) {
        DenickKnownCheckLogf("[Denick] enabled=%d hypKey=%d knownSkins=%zu\n",
                           enabled ? 1 : 0, g_denickHypKey.empty() ? 0 : 1, g_knownNickSkins.size(),
                           g_dllDir.empty() ? "(no dllDir) " : (g_dllDir + "\\").c_str());
    }
    g_denickEnabled = enabled;
    if (enabled && !g_denickHypKey.empty()) StartDenickWorkerLocked();
}

bool DenickIsEnabled() {
    std::lock_guard<std::mutex> lk(g_denickMu);
    return g_denickEnabled;
}

void DenickSetApiKeys(const std::string& hypixelKey, const std::string& seraphKey) {
    std::lock_guard<std::mutex> lk(g_denickMu);
    LoadKnownNickSkinsLocked();
    g_denickHypKey = hypixelKey;
    g_denickSerKey = seraphKey;
    if (g_denickEnabled && !g_denickHypKey.empty()) StartDenickWorkerLocked();
    if (!seraphKey.empty()) { /* seraph fetched inline in worker */ }
}

void DenickShutdown() { StopDenickWorker(); }

std::string DenickGetSkinHashFromSkinManager(JNIEnv* env, jobject mcInstance, jobject gameProfile) {
    if (!env || !mcInstance || !gameProfile) return {};

    jclass mcClass = env->GetObjectClass(mcInstance);
    jmethodID midGetSkinMgr = mcClass ? env->GetMethodID(mcClass, "getSkinManager", "()Lnet/minecraft/client/resources/SkinManager;") : nullptr;
    if (mcClass) env->DeleteLocalRef(mcClass);
    if (!midGetSkinMgr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return {};
    }

    jobject skinMgr = env->CallObjectMethod(mcInstance, midGetSkinMgr);
    if (!skinMgr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return {};
    }

    jclass smClass = McFindClass(env, "net/minecraft/client/resources/SkinManager");
    jmethodID midLoadCache = smClass ? JniResolveInstanceMethod(env, smClass,
        "(Lcom/mojang/authlib/GameProfile;)Ljava/util/Map;", "loadSkinFromCache", "a") : nullptr;
    if (smClass) env->DeleteLocalRef(smClass);
    if (!midLoadCache || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(skinMgr);
        return {};
    }

    jobject texMap = env->CallObjectMethod(skinMgr, midLoadCache, gameProfile);
    env->DeleteLocalRef(skinMgr);
    if (!texMap || env->ExceptionCheck()) {
        env->ExceptionClear();
        return {};
    }

    jclass typeClass = McFindClass(env, "com/mojang/authlib/minecraft/MinecraftProfileTexture$Type");
    jfieldID fidSkin = typeClass ? env->GetStaticFieldID(typeClass, "SKIN",
        "Lcom/mojang/authlib/minecraft/MinecraftProfileTexture$Type;") : nullptr;
    jobject skinType = (fidSkin && typeClass) ? env->GetStaticObjectField(typeClass, fidSkin) : nullptr;
    if (typeClass) env->DeleteLocalRef(typeClass);
    if (!skinType || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(texMap);
        return {};
    }

    jclass mapClass = env->FindClass("java/util/Map");
    jmethodID midMapGet = mapClass ? env->GetMethodID(mapClass, "get", "(Ljava/lang/Object;)Ljava/lang/Object;") : nullptr;
    if (mapClass) env->DeleteLocalRef(mapClass);
    jobject profileTex = (midMapGet && !env->ExceptionCheck())
        ? env->CallObjectMethod(texMap, midMapGet, skinType) : nullptr;
    env->DeleteLocalRef(texMap);
    env->DeleteLocalRef(skinType);
    if (!profileTex || env->ExceptionCheck()) {
        env->ExceptionClear();
        return {};
    }

    std::string hash = JniGetStringMethod(env, profileTex, "getHash", "()Ljava/lang/String;");
    if (hash.empty()) {
        const std::string url = JniGetStringMethod(env, profileTex, "getUrl", "()Ljava/lang/String;");
        hash = ParseSkinHashFromResourcePath(url);
    }
    env->DeleteLocalRef(profileTex);
    hash = LowerAscii(hash);
    if (!hash.empty() && DenickShouldLog("skinmgr:hash", 6000)) {
        DenickLogf("[Denick][SkinMgr] hash=%s\n", HashPreview(hash).c_str());
    }
    return hash;
}

std::string DenickGetTexturesFromProfile(JNIEnv* env, jobject gameProfile) {
    if (!env || !gameProfile) return "";

    jclass gpClass = env->GetObjectClass(gameProfile);
    if (!gpClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }

    jmethodID midGetProps = env->GetMethodID(gpClass, "getProperties",
        "()Lcom/mojang/authlib/properties/PropertyMap;");
    jfieldID fidProps = env->GetFieldID(gpClass, "properties",
        "Lcom/mojang/authlib/properties/PropertyMap;");
    env->DeleteLocalRef(gpClass);
    if (env->ExceptionCheck()) env->ExceptionClear();

    jobject propMap = nullptr;
    if (midGetProps && !env->ExceptionCheck()) {
        propMap = env->CallObjectMethod(gameProfile, midGetProps);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            propMap = nullptr;
        }
    }
    if (!propMap && fidProps && !env->ExceptionCheck()) {
        propMap = env->GetObjectField(gameProfile, fidProps);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            propMap = nullptr;
        }
    }
    if (!propMap) {
        if (DenickShouldLog("profile:no_propmap", 5000)) {
            DenickLogf("[Denick][Profile] PropertyMap unavailable\n");
        }
        return "";
    }

    std::string value = ReadTexturesPropertyFromMap(env, propMap);
    env->DeleteLocalRef(propMap);
    if (!value.empty()) {
        g_denickCnt.texturesProfileOk.fetch_add(1, std::memory_order_relaxed);
        if (DenickShouldLog("profile:textures", 10000)) {
            DenickLogf("[Denick][Profile] textures ok len=%zu\n", value.size());
        }
    } else {
        g_denickCnt.texturesProfileEmpty.fetch_add(1, std::memory_order_relaxed);
        if (DenickShouldLog("profile:empty", 10000)) {
            DenickLogf("[Denick][Profile] textures empty (no property)\n");
        }
    }
    return value;
}

std::string DenickExtractSkinHashFromResourceLocation(JNIEnv* env, jobject skinResourceLocation) {
    if (!env || !skinResourceLocation) return {};
    // jy.a() = resource path (e.g. skins/<hash>), jy.b() = domain (usually "minecraft").
    const std::string resPath = JniGetStringMethod(env, skinResourceLocation, "a", "()Ljava/lang/String;");
    const std::string resDomain = JniGetStringMethod(env, skinResourceLocation, "b", "()Ljava/lang/String;");
    std::string hash = ParseSkinHashFromResourcePath(resPath);
    if (hash.empty() && !resDomain.empty() && !resPath.empty()) {
        hash = ParseSkinHashFromResourcePath(resDomain + ":" + resPath);
    }
    if (hash.empty()) {
        const std::string resStr = JniGetStringMethod(env, skinResourceLocation, "toString", "()Ljava/lang/String;");
        hash = ParseSkinHashFromResourcePath(resStr);
    }
    if (!hash.empty()) {
        if (DenickShouldLog("skinrl:hash", 6000)) {
            DenickLogf("[Denick][SkinRL] domain=%s path=%s hash=%s\n",
                       resDomain.c_str(), resPath.c_str(), HashPreview(hash).c_str());
        }
    } else if (resPath.empty() && resDomain.empty()) {
        if (DenickShouldLog("skinrl:empty", 8000)) {
            DenickLogf("[Denick][SkinRL] empty ResourceLocation\n");
        }
    } else if (DenickShouldLog("skinrl:nohash:" + resPath, 8000)) {
        DenickLogf("[Denick][SkinRL] no hex hash domain=%s path=%s\n",
                   resDomain.c_str(), resPath.c_str());
    }
    return hash;
}

bool DenickResolveSkinFromEntityPlayer(JNIEnv* env, jobject mcInstance, const std::string& nickIgn,
                                       DenickEntitySkinProbe& out) {
    out = DenickEntitySkinProbe{};
    if (!env || !mcInstance || nickIgn.empty()) return false;

    const std::string nickKey = CanonKey(nickIgn);
    if (nickKey.empty()) return false;

    jclass mcClass = env->GetObjectClass(mcInstance);
    jfieldID fidWorld = mcClass ? env->GetFieldID(mcClass, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;") : nullptr;
    if (mcClass) env->DeleteLocalRef(mcClass);
    if (!fidWorld || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jobject world = env->GetObjectField(mcInstance, fidWorld);
    if (!world || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jclass worldClass = env->GetObjectClass(world);
    jfieldID fidPlayers = worldClass ? env->GetFieldID(worldClass, "playerEntities", "Ljava/util/List;") : nullptr;
    if (worldClass) env->DeleteLocalRef(worldClass);
    if (!fidPlayers || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(world);
        return false;
    }

    jobject playerList = env->GetObjectField(world, fidPlayers);
    env->DeleteLocalRef(world);
    if (!playerList || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    jclass listClass = env->GetObjectClass(playerList);
    jmethodID midSize = listClass ? env->GetMethodID(listClass, "size", "()I") : nullptr;
    jmethodID midGet = listClass ? env->GetMethodID(listClass, "get", "(I)Ljava/lang/Object;") : nullptr;
    if (listClass) env->DeleteLocalRef(listClass);
    if (!midSize || !midGet || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(playerList);
        return false;
    }

    jclass clsEntityPlayer = McFindClass(env, "net/minecraft/entity/player/EntityPlayer");
    jclass clsAbstractClient = McFindClass(env, "net/minecraft/client/entity/AbstractClientPlayer");
    jmethodID midGetGameProfile = clsEntityPlayer
        ? env->GetMethodID(clsEntityPlayer, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;") : nullptr;
    jmethodID midGetLocationSkin = clsAbstractClient
        ? env->GetMethodID(clsAbstractClient, "getLocationSkin", "()Lnet/minecraft/util/ResourceLocation;") : nullptr;
    if (env->ExceptionCheck()) env->ExceptionClear();

    const int count = env->CallIntMethod(playerList, midSize);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        if (clsEntityPlayer) env->DeleteLocalRef(clsEntityPlayer);
        if (clsAbstractClient) env->DeleteLocalRef(clsAbstractClient);
        env->DeleteLocalRef(playerList);
        return false;
    }

    for (int i = 0; i < count; ++i) {
        jobject entity = env->CallObjectMethod(playerList, midGet, i);
        if (!entity || env->ExceptionCheck()) {
            env->ExceptionClear();
            continue;
        }

        if (!clsEntityPlayer || !midGetGameProfile) {
            env->DeleteLocalRef(entity);
            continue;
        }

        jclass entityClass = env->GetObjectClass(entity);
        if (!env->IsAssignableFrom(clsEntityPlayer, entityClass)) {
            env->DeleteLocalRef(entityClass);
            env->DeleteLocalRef(entity);
            continue;
        }

        jobject gameProfile = env->CallObjectMethod(entity, midGetGameProfile);
        if (!gameProfile || env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(entityClass);
            env->DeleteLocalRef(entity);
            continue;
        }

        std::string entityName;
        jclass gpClass = env->GetObjectClass(gameProfile);
        jmethodID midGetName = gpClass ? env->GetMethodID(gpClass, "getName", "()Ljava/lang/String;") : nullptr;
        if (gpClass) env->DeleteLocalRef(gpClass);
        if (midGetName && !env->ExceptionCheck()) {
            jstring jName = (jstring)env->CallObjectMethod(gameProfile, midGetName);
            if (jName && !env->ExceptionCheck()) {
                const char* c = env->GetStringUTFChars(jName, nullptr);
                if (c) entityName = c;
                if (c) env->ReleaseStringUTFChars(jName, c);
                env->DeleteLocalRef(jName);
            } else {
                env->ExceptionClear();
            }
        } else {
            env->ExceptionClear();
        }

        if (CanonKey(entityName) != nickKey) {
            env->DeleteLocalRef(gameProfile);
            env->DeleteLocalRef(entityClass);
            env->DeleteLocalRef(entity);
            continue;
        }

        out.found = true;
        out.texturesBase64 = DenickGetTexturesFromProfile(env, gameProfile);
        out.skinHash = DenickGetSkinHashFromSkinManager(env, mcInstance, gameProfile);
        if (out.skinHash.empty()) {
            out.skinHash = ExtractSkinHashFromTextures(out.texturesBase64, "entity:profile");
        }

        if (out.skinHash.empty() && clsAbstractClient && midGetLocationSkin &&
            env->IsAssignableFrom(clsAbstractClient, entityClass)) {
            jobject skinLoc = env->CallObjectMethod(entity, midGetLocationSkin);
            if (skinLoc && !env->ExceptionCheck()) {
                out.skinHash = DenickExtractSkinHashFromResourceLocation(env, skinLoc);
                env->DeleteLocalRef(skinLoc);
            } else {
                env->ExceptionClear();
            }
        }

        env->DeleteLocalRef(gameProfile);
        env->DeleteLocalRef(entityClass);
        env->DeleteLocalRef(entity);

        if (DenickShouldLog("entity:resolved:" + nickKey)) {
            DenickLogf("[Denick][Entity] nick=%s texLen=%zu hashLen=%zu hash=%s\n",
                       nickIgn.c_str(), out.texturesBase64.size(), out.skinHash.size(),
                       HashPreview(out.skinHash).c_str());
        }
        break;
    }

    if (clsEntityPlayer) env->DeleteLocalRef(clsEntityPlayer);
    if (clsAbstractClient) env->DeleteLocalRef(clsAbstractClient);
    env->DeleteLocalRef(playerList);
    return out.found;
}

void DenickRegisterRealSkin(const std::string& ign, const std::string& texturesBase64) {
    if (texturesBase64.empty()) return;
    const std::string key = CanonKey(ign);
    if (key.empty()) return;
    const std::string ctx = std::string("real:") + key;
    const std::string hash = ExtractSkinHashFromTextures(texturesBase64, ctx.c_str());
    if (hash.empty()) return;
    std::lock_guard<std::mutex> lk(g_denickMu);
    g_skinByHash[hash] = SkinRecord{ign, texturesBase64};
    if (DenickShouldLog("real:map:" + key, 8000)) {
        DenickLogf("[Denick][Real] mapped ign=%s hash=%s mapSize=%zu\n",
                   ign.c_str(), HashPreview(hash).c_str(), g_skinByHash.size());
    }
}

void DenickOnNickPlayer(const std::string& nickIgn, const std::string& texturesBase64,
                        const std::string& skinHashFallback, const std::string& uuidHint) {
    if (!g_denickEnabled) return;
    const std::string nickKey = CanonKey(nickIgn);
    if (nickKey.empty()) return;

    const std::string ctx = std::string("nick:") + nickKey;
    std::string skinHash = ExtractSkinHashFromTextures(texturesBase64, ctx.c_str());
    if (skinHash.empty() && !skinHashFallback.empty()) {
        skinHash = LowerAscii(skinHashFallback);
        if (DenickShouldLog("nick:hash_fallback:" + nickKey)) {
            DenickLogf("[Denick][Nick] using ResourceLocation hash=%s nick=%s\n",
                       HashPreview(skinHash).c_str(), nickIgn.c_str());
        }
    }
    if (skinHash.empty() && texturesBase64.empty()) return;
    const ULONGLONG now = GetTickCount64();

    {
        std::lock_guard<std::mutex> lk(g_denickMu);
        LoadKnownNickSkinsLocked();
        LogSkinHashKnownCheck(nickIgn, skinHash, uuidHint);

        if (!skinHash.empty() && g_knownNickSkins.count(skinHash)) {
            CancelDenickJobsForNickLocked(nickKey);
            DenickEntry e;
            e.state = DenickOverlayData::State::KnownNickSkin;
            e.updatedAt = now;
            e.skinHash = skinHash;
            e.knownNickChatPending = true;
            g_denickByNick[nickKey] = e;
            g_denickCnt.nickKnownHit.fetch_add(1, std::memory_order_relaxed);
            DenickLogf("[Denick][Nick] known-list hit nick=%s hash=%s\n",
                       nickIgn.c_str(), HashPreview(skinHash).c_str());
            return;
        }

        auto it = g_denickByNick.find(nickKey);
        if (it != g_denickByNick.end()) {
            if (it->second.state == DenickOverlayData::State::KnownNickSkin) return;
            if (it->second.state == DenickOverlayData::State::Denicked) return;
            if (it->second.state == DenickOverlayData::State::Pending &&
                (now - it->second.updatedAt) < kDenickRetryMs) {
                return;
            }
        }

        DenickEntry e;
        e.state = DenickOverlayData::State::Pending;
        e.updatedAt = now;
        e.skinHash = skinHash;
        g_denickByNick[nickKey] = e;

        for (const auto& pr : g_skinByHash) {
            if (CanonKey(pr.second.displayName) == nickKey) {
                PushDenickJobLocked(DenickJob{nickKey, pr.second.texturesBase64, pr.first});
                g_denickCnt.nickQueued.fetch_add(1, std::memory_order_relaxed);
                if (DenickShouldLog("nick:queue:skinmap:" + nickKey)) {
                    DenickLogf("[Denick][Nick] queued via skin map nick=%s hash=%s b64Len=%zu\n",
                               nickIgn.c_str(), HashPreview(pr.first).c_str(), pr.second.texturesBase64.size());
                }
                if (!g_denickHypKey.empty()) StartDenickWorkerLocked();
                return;
            }
        }
    }

    if (!texturesBase64.empty() && !skinHash.empty()) {
        PushDenickJobLocked(DenickJob{nickKey, texturesBase64, skinHash});
        g_denickCnt.nickQueued.fetch_add(1, std::memory_order_relaxed);
        if (DenickShouldLog("nick:queue:textures:" + nickKey)) {
            DenickLogf("[Denick][Nick] queued nick=%s hash=%s b64Len=%zu\n",
                       nickIgn.c_str(), HashPreview(skinHash).c_str(), texturesBase64.size());
        }
        {
            std::lock_guard<std::mutex> lk(g_denickMu);
            if (!g_denickHypKey.empty()) StartDenickWorkerLocked();
        }
    }
}

bool DenickGetOverlayData(const std::string& nickIgn, DenickOverlayData& out) {
    out = DenickOverlayData{};
    const std::string nickKey = CanonKey(nickIgn);
    if (nickKey.empty()) return false;

    std::lock_guard<std::mutex> lk(g_denickMu);
    auto it = g_denickByNick.find(nickKey);
    if (it == g_denickByNick.end()) return false;

    const DenickEntry& e = it->second;
    out.state = e.state;
    out.realName = e.realName;
    out.realUuid = e.realUuid;
    out.hypOk = e.hypOk;
    out.hyp = e.hyp;
    out.seraphOk = e.seraphOk;
    out.seraph = e.seraph;
    return true;
}

bool DenickTryConsumeDenickChat(const std::string& nickIgn, std::string& outNickIgn, std::string& outRealName) {
    const std::string nickKey = CanonKey(nickIgn);
    if (nickKey.empty()) return false;

    std::lock_guard<std::mutex> lk(g_denickMu);
    auto it = g_denickByNick.find(nickKey);
    if (it == g_denickByNick.end() || !it->second.chatPending) return false;
    if (it->second.state != DenickOverlayData::State::Denicked || it->second.realName.empty()) return false;

    it->second.chatPending = false;
    outNickIgn = nickIgn;
    outRealName = it->second.realName;
    return true;
}

bool DenickTryConsumeKnownNickChat(const std::string& nickIgn, std::string& outNickIgn) {
    const std::string nickKey = CanonKey(nickIgn);
    if (nickKey.empty()) return false;

    std::lock_guard<std::mutex> lk(g_denickMu);
    auto it = g_denickByNick.find(nickKey);
    if (it == g_denickByNick.end() || !it->second.knownNickChatPending) return false;
    if (it->second.state != DenickOverlayData::State::KnownNickSkin) return false;

    it->second.knownNickChatPending = false;
    outNickIgn = nickIgn;
    return true;
}

void DenickLogTabBatchSummary(int nickCount, int noTextures) {
    DenickDebugSnapshot dn{};
    DenickGetDebugSnapshot(dn);
    DenickLogf("[Denick][Tab] batch nicks=%d noTex=%d nicksTracked=%d knownHitsTotal=%d\n",
               nickCount, noTextures, dn.nicksTracked, dn.nickKnownHit);
}

void DenickLogTabNickSeen(const std::string& nickIgn, int uuidVer, size_t texturesLen, size_t skinHashLen) {
    const std::string key = CanonKey(nickIgn);
    if (key.empty()) return;
    if (DenickShouldLog("tab:nick:" + key, 5000)) {
        DenickLogf("[Denick][Tab] nick=%s uuidVer=%d texturesLen=%zu skinHashLen=%zu enabled=%d\n",
                   nickIgn.c_str(), uuidVer, texturesLen, skinHashLen, g_denickEnabled ? 1 : 0);
    }
}

void DenickGetDebugSnapshot(DenickDebugSnapshot& out) {
    out = DenickDebugSnapshot{};
    out.texturesProfileOk = g_denickCnt.texturesProfileOk.load(std::memory_order_relaxed);
    out.texturesProfileEmpty = g_denickCnt.texturesProfileEmpty.load(std::memory_order_relaxed);
    out.base64DecodeOk = g_denickCnt.base64DecodeOk.load(std::memory_order_relaxed);
    out.base64DecodeFail = g_denickCnt.base64DecodeFail.load(std::memory_order_relaxed);
    out.skinHashOk = g_denickCnt.skinHashOk.load(std::memory_order_relaxed);
    out.skinHashNoUrl = g_denickCnt.skinHashNoUrl.load(std::memory_order_relaxed);
    out.profileIdOk = g_denickCnt.profileIdOk.load(std::memory_order_relaxed);
    out.profileIdFail = g_denickCnt.profileIdFail.load(std::memory_order_relaxed);
    out.nickKnownHit = g_denickCnt.nickKnownHit.load(std::memory_order_relaxed);
    out.nickQueued = g_denickCnt.nickQueued.load(std::memory_order_relaxed);

    std::lock_guard<std::mutex> lk(g_denickMu);
    out.knownSkinsLoaded = (int)g_knownNickSkins.size();
    out.realSkinsMapped = (int)g_skinByHash.size();
    out.nicksTracked = (int)g_denickByNick.size();
    for (const auto& pr : g_denickByNick) {
        switch (pr.second.state) {
        case DenickOverlayData::State::Pending:
            out.statePending++;
            break;
        case DenickOverlayData::State::KnownNickSkin:
            out.stateKnownSkin++;
            break;
        case DenickOverlayData::State::Denicked:
            out.stateDenicked++;
            break;
        case DenickOverlayData::State::Failed:
            out.stateFailed++;
            break;
        default:
            break;
        }
    }
}

} // namespace taboverlay

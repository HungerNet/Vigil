#pragma once

#include "tab_overlay.h"
#include <jni.h>
#include <string>

namespace taboverlay {

struct DenickOverlayData {
    enum class State {
        Unknown,
        Pending,
        KnownNickSkin,
        Denicked,
        Failed
    };

    State state = State::Unknown;
    std::string realName;
    std::string realUuid;
    bool hypOk = false;
    CachedHypixelStats hyp{};
    bool seraphOk = false;
    CachedSeraphTag seraph{};
};

void DenickSetEnabled(bool enabled);
bool DenickIsEnabled();

// Clears per-game known-nicks check logs (call on Bedwars game start).
void DenickOnGameStarted();

void DenickSetApiKeys(const std::string& hypixelKey, const std::string& seraphKey);
void DenickShutdown();

std::string DenickGetTexturesFromProfile(JNIEnv* env, jobject gameProfile);
std::string DenickGetSkinHashFromSkinManager(JNIEnv* env, jobject mcInstance, jobject gameProfile);
std::string DenickExtractSkinHashFromResourceLocation(JNIEnv* env, jobject skinResourceLocation);

struct DenickEntitySkinProbe {
    bool found = false;
    std::string texturesBase64;
    std::string skinHash;
};

// Fallback: EntityPlayer.cd() -> GameProfile (+ AbstractClientPlayer skin RL).
bool DenickResolveSkinFromEntityPlayer(JNIEnv* env, jobject mcInstance, const std::string& nickIgn,
                                       DenickEntitySkinProbe& out);
void DenickRegisterRealSkin(const std::string& ign, const std::string& texturesBase64);
void DenickOnNickPlayer(const std::string& nickIgn, const std::string& texturesBase64,
                        const std::string& skinHashFallback = "",
                        const std::string& uuidHint = "");
bool DenickGetOverlayData(const std::string& nickIgn, DenickOverlayData& out);

// Returns true once when a nick was successfully denicked (for optional chat line).
bool DenickTryConsumeDenickChat(const std::string& nickIgn, std::string& outNickIgn, std::string& outRealName);

// Returns true once for a known proxy nick skin (no Mojang denick).
bool DenickTryConsumeKnownNickChat(const std::string& nickIgn, std::string& outNickIgn);

struct DenickDebugSnapshot {
    int texturesProfileOk = 0;
    int texturesProfileEmpty = 0;
    int base64DecodeOk = 0;
    int base64DecodeFail = 0;
    int skinHashOk = 0;
    int skinHashNoUrl = 0;
    int profileIdOk = 0;
    int profileIdFail = 0;
    int knownSkinsLoaded = 0;
    int realSkinsMapped = 0;
    int nicksTracked = 0;
    int nickKnownHit = 0;
    int nickQueued = 0;
    int statePending = 0;
    int stateKnownSkin = 0;
    int stateDenicked = 0;
    int stateFailed = 0;
};

void DenickGetDebugSnapshot(DenickDebugSnapshot& out);

void DenickLogTabNickSeen(const std::string& nickIgn, int uuidVer, size_t texturesLen, size_t skinHashLen);
void DenickLogTabBatchSummary(int nickCount, int noTextures);

} // namespace taboverlay

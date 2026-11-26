#pragma once
// Vigil game layer — samples live Minecraft state into detection::WorldFrame once per client tick.

#include "../detection/types.h"

#include <jni.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace vigil::game {

struct SignalStatus {
    bool serverPositions = false;
    bool headRotation = false;
    bool moveAttribute = false;   // generic.movementSpeed (+ sprint modifier)
    bool potions = false;
    bool potionParticles = false;
    bool blocks = false;
    bool heldItem = false;
    bool swing = false;
    bool hurt = false;
    bool profiles = false;        // GameProfile textures (identity tools)
    bool skins = false;           // skin textures (player faces)
    bool tabList = false;         // NetHandlerPlayClient#getPlayerInfo (NPC filtering)
    bool latency = false;         // NetworkPlayerInfo#getResponseTime (Velocity response window)
    bool burning = false;         // Entity#isBurning (Velocity: damage without an attacker)
};

// What the sampler could bind in this client (Status → Diagnostics report).
struct BindingReport {
    int total = 0;
    int missing = 0;
    int obfuscated = 0;               // resolved through a confirmed obfuscated fallback name
    std::vector<std::string> notes;   // "missing: isBurning", "posX → s"
};

class Sampler {
public:
    const BindingReport& bindings() const;
    // Fills `out` and returns true when a new client tick elapsed since the last call.
    bool sample(JNIEnv* env, jobject mc, detection::WorldFrame& out, bool& worldChanged);
    void reset(JNIEnv* env);
    const SignalStatus& signals() const { return signals_; }
    bool resolved() const { return resolved_; }
    // GL texture holding the player's skin (Minecraft's own texture), keyed like PlayerRecord.
    bool faceTexture(const std::string& playerKey, unsigned int& texture) const;
    // Entity ids of players that appeared since the last call (Party Queue Detector).
    std::vector<int> takeSpawns() { std::vector<int> s; s.swap(spawns_); return s; }

private:
    struct Identity {
        std::string uuid;
        std::string name;
        bool teammate = false;
        bool texturesRead = false;
        unsigned int skinTexture = 0;
        jobject uuidRef = nullptr;      // global ref to the profile's java.util.UUID (tab-list queries)
        bool listed = false;
        int pingMs = -1;
        detection::Tick refreshedAt = -100000;
        detection::Tick teamAt = -100000;
        detection::Tick skinAt = -100000;
        detection::Tick tabAt = -100000;
    };
    void readProfileExtras(JNIEnv* env, jobject mc, jobject entity, jobject profile, Identity& id, bool isLocal);

    bool resolve(JNIEnv* env, jobject mc);
    void readPlayer(JNIEnv* env, jobject mc, jobject world, jobject netHandler, jobject entity, bool isLocal,
                    detection::PlayerFrame& f);
    // Block id at a position. Results are cached for a short time per block, so standing players,
    // hitbox-corner probes and repeated wall probes cost no extra calls into the game.
    int blockAt(JNIEnv* env, jobject world, double x, double y, double z);
    int probeBlock(JNIEnv* env, jobject world, int bx, int by, int bz);
    void refreshIdentity(JNIEnv* env, jobject mc, jobject entity, Identity& id, bool isLocal);
    void releaseIdentity(JNIEnv* env, Identity& id);

    bool resolved_ = false;
    SignalStatus signals_;
    detection::Tick tick_ = 0;
    int lastTicksExisted_ = -1;
    jweak lastWorld_ = nullptr;
    jweak lastSelf_ = nullptr;
    std::string localTeam_;
    detection::Tick localTeamAt_ = -100000;
    std::unordered_map<int, Identity> identities_;
    std::unordered_map<std::string, unsigned int> faces_;
    std::unordered_map<int, bool> seenIds_;
    std::vector<int> spawns_;
    struct CachedBlock { int id; detection::Tick at; };
    std::unordered_map<long long, CachedBlock> blockCache_;

    // Classes (global refs)
    jclass clsEntity_ = nullptr, clsLiving_ = nullptr, clsPlayer_ = nullptr, clsOther_ = nullptr;
    jclass clsItem_ = nullptr, clsBlockPos_ = nullptr, clsBlock_ = nullptr, clsList_ = nullptr;
    // Minecraft
    jfieldID fWorld_ = nullptr, fSelf_ = nullptr;
    jfieldID fPlayerEntities_ = nullptr;
    // Entity
    jfieldID fEntityId_ = nullptr, fTicksExisted_ = nullptr, fPosX_ = nullptr, fPosY_ = nullptr, fPosZ_ = nullptr;
    jfieldID fServerX_ = nullptr, fServerY_ = nullptr, fServerZ_ = nullptr;
    jfieldID fYaw_ = nullptr, fPitch_ = nullptr, fOnGround_ = nullptr, fIsDead_ = nullptr, fInWeb_ = nullptr;
    jmethodID mSprinting_ = nullptr, mSneaking_ = nullptr, mInvisible_ = nullptr, mRiding_ = nullptr;
    jmethodID mInWater_ = nullptr, mInLava_ = nullptr;
    // Living
    jfieldID fHeadYaw_ = nullptr, fHurtTime_ = nullptr, fSwinging_ = nullptr, fSwingProgress_ = nullptr;
    jmethodID mHealth_ = nullptr, mOnLadder_ = nullptr, mPotions_ = nullptr;
    // Player
    jmethodID mUsing_ = nullptr, mBlocking_ = nullptr, mHeld_ = nullptr, mProfile_ = nullptr;
    // Other player (exact relayed rotation targets)
    jfieldID fOtherPitch_ = nullptr;
    // Items / potions / blocks
    jmethodID mStackItem_ = nullptr, mItemId_ = nullptr, mPotionId_ = nullptr, mPotionAmp_ = nullptr;
    jmethodID mToArray_ = nullptr, mListSize_ = nullptr, mListGet_ = nullptr;
    jmethodID mBlockPosCtor_ = nullptr, mGetBlockState_ = nullptr, mStateBlock_ = nullptr, mBlockId_ = nullptr;
    // Profile
    jmethodID mProfileId_ = nullptr, mProfileName_ = nullptr, mUuidToString_ = nullptr;
    jmethodID mProfileProps_ = nullptr, mPropMapGet_ = nullptr, mPropValue_ = nullptr;
    jobject texturesKey_ = nullptr;       // global "textures" string
    // Movement attribute.
    jobject moveSpeedAttr_ = nullptr;     // SharedMonsterAttributes.movementSpeed (global ref)
    jobject sprintUuid_ = nullptr;        // vanilla sprint modifier UUID (global ref)
    jmethodID mGetAttribute_ = nullptr, mAttrValue_ = nullptr, mAttrModifier_ = nullptr;
    // Potion particles (DataWatcher index 7).
    jmethodID mDataWatcher_ = nullptr, mWatchInt_ = nullptr;
    // Skins.
    jclass clsAbstractClient_ = nullptr;
    jmethodID mLocationSkin_ = nullptr, mTextureManager_ = nullptr, mGetTexture_ = nullptr, mGlTextureId_ = nullptr;
    // Tab list and latency.
    jmethodID mNetHandler_ = nullptr, mGetPlayerInfo_ = nullptr, mResponseTime_ = nullptr;
    jmethodID mBurning_ = nullptr;
};

}  // namespace vigil::game

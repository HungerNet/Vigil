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
    bool potions = false;
    bool blocks = false;
    bool heldItem = false;
    bool swing = false;
    bool hurt = false;
};

class Sampler {
public:
    // Fills `out` and returns true when a new client tick elapsed since the last call.
    bool sample(JNIEnv* env, jobject mc, detection::WorldFrame& out, bool& worldChanged);
    void reset(JNIEnv* env);
    const SignalStatus& signals() const { return signals_; }
    bool resolved() const { return resolved_; }

private:
    struct Identity {
        std::string uuid;
        std::string name;
        bool teammate = false;
        detection::Tick refreshedAt = -100000;
        detection::Tick teamAt = -100000;
    };

    bool resolve(JNIEnv* env, jobject mc);
    void readPlayer(JNIEnv* env, jobject mc, jobject world, jobject entity, bool isLocal, detection::PlayerFrame& f);
    int blockAt(JNIEnv* env, jobject world, double x, double y, double z);
    void refreshIdentity(JNIEnv* env, jobject mc, jobject entity, Identity& id, bool isLocal);

    bool resolved_ = false;
    SignalStatus signals_;
    detection::Tick tick_ = 0;
    int lastTicksExisted_ = -1;
    jweak lastWorld_ = nullptr;
    jweak lastSelf_ = nullptr;
    std::string localTeam_;
    detection::Tick localTeamAt_ = -100000;
    std::unordered_map<int, Identity> identities_;

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
};

}  // namespace vigil::game

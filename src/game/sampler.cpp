#include "sampler.h"

#include "../core/log.h"
#include "../identity/identity.h"
#include "class_finder.h"
#include "jvm.h"
#include "mc.h"
#include "minecraft_util.h"

#include <cmath>

namespace vigil::game {
namespace {

jclass globalClass(JNIEnv* env, const char* name) {
    jclass local = McFindClass(env, name);
    if (!local) return nullptr;
    jclass g = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    return g;
}

// Binding report for live validation (docs/LIVE_VALIDATION.md): which members could not be
// resolved, and which resolved only through their obfuscated (confirmed) fallback name.
BindingReport g_report;

void note(bool resolved, bool byName, const char* name, const char* obf) {
    ++g_report.total;
    if (!resolved) {
        ++g_report.missing;
        g_report.notes.push_back(std::string("missing: ") + name);
    } else if (!byName) {
        ++g_report.obfuscated;
        g_report.notes.push_back(std::string(name) + " \xE2\x86\x92 " + (obf ? obf : "?"));
    }
}

jfieldID field(JNIEnv* env, jclass cls, const char* name, const char* sig, const char* obf = nullptr) {
    if (!cls) { note(false, false, name, obf); return nullptr; }
    jfieldID byName = env->GetFieldID(cls, name, sig);
    if (env->ExceptionCheck()) { env->ExceptionClear(); byName = nullptr; }
    jfieldID id = byName ? byName : JniResolveFieldID(env, cls, sig, name, obf);
    note(id != nullptr, byName != nullptr, name, obf);
    return id;
}

jmethodID method(JNIEnv* env, jclass cls, const char* name, const char* sig, const char* obf = nullptr) {
    if (!cls) { note(false, false, name, obf); return nullptr; }
    jmethodID byName = env->GetMethodID(cls, name, sig);
    if (env->ExceptionCheck()) { env->ExceptionClear(); byName = nullptr; }
    jmethodID id = byName ? byName : JniResolveInstanceMethod(env, cls, sig, name, obf);
    note(id != nullptr, byName != nullptr, name, obf);
    return id;
}

bool callBool(JNIEnv* env, jobject o, jmethodID m) {
    if (!m) return false;
    const bool v = env->CallBooleanMethod(o, m) == JNI_TRUE;
    return jniFailed(env) ? false : v;
}

}  // namespace

const BindingReport& Sampler::bindings() const { return g_report; }

bool Sampler::resolve(JNIEnv* env, jobject mc) {
    if (resolved_) return true;
    g_report = BindingReport{};
    clsEntity_ = globalClass(env, "net/minecraft/entity/Entity");
    clsLiving_ = globalClass(env, "net/minecraft/entity/EntityLivingBase");
    clsPlayer_ = globalClass(env, "net/minecraft/entity/player/EntityPlayer");
    clsOther_ = globalClass(env, "net/minecraft/client/entity/EntityOtherPlayerMP");
    clsItem_ = globalClass(env, "net/minecraft/item/Item");
    clsBlockPos_ = globalClass(env, "net/minecraft/util/BlockPos");
    clsBlock_ = globalClass(env, "net/minecraft/block/Block");
    clsList_ = globalClass(env, "java/util/List");
    if (!clsEntity_ || !clsLiving_ || !clsPlayer_ || !clsList_) return false;

    jclass mcClass = env->GetObjectClass(mc);
    static const char* kWorldSigs[] = {"Lnet/minecraft/client/multiplayer/WorldClient;"};
    static const char* kSelfSigs[] = {"Lnet/minecraft/client/entity/EntityPlayerSP;"};
    fWorld_ = JniResolveFieldIDFlex(env, mcClass, "theWorld", "f", kWorldSigs, 1);
    fSelf_ = JniResolveFieldIDFlex(env, mcClass, "thePlayer", "h", kSelfSigs, 1);
    env->DeleteLocalRef(mcClass);
    jclass worldBase = McFindClass(env, "net/minecraft/world/World");
    if (worldBase) {
        fPlayerEntities_ = field(env, worldBase, "playerEntities", "Ljava/util/List;", "j");
        mGetBlockState_ = method(env, worldBase, "getBlockState",
                                 "(Lnet/minecraft/util/BlockPos;)Lnet/minecraft/block/state/IBlockState;");
        env->DeleteLocalRef(worldBase);
    }

    fEntityId_ = field(env, clsEntity_, "entityId", "I", "c");
    fTicksExisted_ = field(env, clsEntity_, "ticksExisted", "I");
    fPosX_ = field(env, clsEntity_, "posX", "D", "s");
    fPosY_ = field(env, clsEntity_, "posY", "D", "t");
    fPosZ_ = field(env, clsEntity_, "posZ", "D", "u");
    fServerX_ = field(env, clsEntity_, "serverPosX", "I");
    fServerY_ = field(env, clsEntity_, "serverPosY", "I");
    fServerZ_ = field(env, clsEntity_, "serverPosZ", "I");
    fYaw_ = field(env, clsEntity_, "rotationYaw", "F", "y");
    fPitch_ = field(env, clsEntity_, "rotationPitch", "F", "z");
    fOnGround_ = field(env, clsEntity_, "onGround", "Z");
    fIsDead_ = field(env, clsEntity_, "isDead", "Z");
    fInWeb_ = field(env, clsEntity_, "isInWeb", "Z");
    mSprinting_ = method(env, clsEntity_, "isSprinting", "()Z", "aw");
    mSneaking_ = method(env, clsEntity_, "isSneaking", "()Z", "av");
    mInvisible_ = method(env, clsEntity_, "isInvisible", "()Z", "ax");
    mRiding_ = method(env, clsEntity_, "isRiding", "()Z");
    mInWater_ = method(env, clsEntity_, "isInWater", "()Z");
    mInLava_ = method(env, clsEntity_, "isInLava", "()Z");

    fHeadYaw_ = field(env, clsLiving_, "rotationYawHead", "F");
    fHurtTime_ = field(env, clsLiving_, "hurtTime", "I", "au");
    fSwinging_ = field(env, clsLiving_, "isSwingInProgress", "Z", "ar");
    fSwingProgress_ = field(env, clsLiving_, "swingProgressInt", "I", "as");
    mHealth_ = method(env, clsLiving_, "getHealth", "()F", "bn");
    mOnLadder_ = method(env, clsLiving_, "isOnLadder", "()Z");
    mPotions_ = method(env, clsLiving_, "getActivePotionEffects", "()Ljava/util/Collection;");

    mUsing_ = method(env, clsPlayer_, "isUsingItem", "()Z", "bS");
    mBlocking_ = method(env, clsPlayer_, "isBlocking", "()Z", "bW");
    mHeld_ = method(env, clsPlayer_, "getHeldItem", "()Lnet/minecraft/item/ItemStack;", "bA");
    mProfile_ = method(env, clsPlayer_, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;", "cd");
    if (clsOther_) fOtherPitch_ = field(env, clsOther_, "otherPlayerMPPitch", "D");

    if (jclass stack = McFindClass(env, "net/minecraft/item/ItemStack")) {
        mStackItem_ = method(env, stack, "getItem", "()Lnet/minecraft/item/Item;", "b");
        env->DeleteLocalRef(stack);
    }
    if (clsItem_) mItemId_ = JniResolveStaticMethod(env, clsItem_, "(Lnet/minecraft/item/Item;)I", "getIdFromItem", "b");
    if (jclass pe = McFindClass(env, "net/minecraft/potion/PotionEffect")) {
        mPotionId_ = method(env, pe, "getPotionID", "()I");
        mPotionAmp_ = method(env, pe, "getAmplifier", "()I");
        env->DeleteLocalRef(pe);
    }
    if (jclass coll = env->FindClass("java/util/Collection")) {
        mToArray_ = env->GetMethodID(coll, "toArray", "()[Ljava/lang/Object;");
        jniFailed(env);
        env->DeleteLocalRef(coll);
    }
    mListSize_ = env->GetMethodID(clsList_, "size", "()I");
    mListGet_ = env->GetMethodID(clsList_, "get", "(I)Ljava/lang/Object;");
    jniFailed(env);
    if (clsBlockPos_) mBlockPosCtor_ = env->GetMethodID(clsBlockPos_, "<init>", "(III)V");
    jniFailed(env);
    if (jclass st = McFindClass(env, "net/minecraft/block/state/IBlockState")) {
        mStateBlock_ = method(env, st, "getBlock", "()Lnet/minecraft/block/Block;");
        env->DeleteLocalRef(st);
    }
    if (clsBlock_) mBlockId_ = JniResolveStaticMethod(env, clsBlock_, "(Lnet/minecraft/block/Block;)I", "getIdFromBlock", nullptr);
    if (jclass gp = env->FindClass("com/mojang/authlib/GameProfile")) {
        mProfileId_ = env->GetMethodID(gp, "getId", "()Ljava/util/UUID;");
        mProfileName_ = env->GetMethodID(gp, "getName", "()Ljava/lang/String;");
        jniFailed(env);
        env->DeleteLocalRef(gp);
    } else {
        jniFailed(env);
    }
    if (jclass uuid = env->FindClass("java/util/UUID")) {
        mUuidToString_ = env->GetMethodID(uuid, "toString", "()Ljava/lang/String;");
        jniFailed(env);
        env->DeleteLocalRef(uuid);
    }

    // Movement-speed attribute: synced by the server whenever it changes (potions, abilities,
    // sprinting), so it is the authoritative movement modifier for other players.
    if (jclass attrs = McFindClass(env, "net/minecraft/entity/SharedMonsterAttributes")) {
        for (const char* n : {"movementSpeed"}) {
            jfieldID sf = env->GetStaticFieldID(attrs, n, "Lnet/minecraft/entity/ai/attributes/IAttribute;");
            if (!sf || jniFailed(env)) continue;
            jobject a = env->GetStaticObjectField(attrs, sf);
            if (a && !jniFailed(env)) moveSpeedAttr_ = env->NewGlobalRef(a);
            if (a) env->DeleteLocalRef(a);
            break;
        }
        env->DeleteLocalRef(attrs);
    }
    mGetAttribute_ = method(env, clsLiving_, "getEntityAttribute",
                            "(Lnet/minecraft/entity/ai/attributes/IAttribute;)Lnet/minecraft/entity/ai/attributes/IAttributeInstance;");
    if (jclass inst = McFindClass(env, "net/minecraft/entity/ai/attributes/IAttributeInstance")) {
        mAttrValue_ = method(env, inst, "getAttributeValue", "()D");
        mAttrModifier_ = method(env, inst, "getModifier", "(Ljava/util/UUID;)Lnet/minecraft/entity/ai/attributes/AttributeModifier;");
        env->DeleteLocalRef(inst);
    }
    if (jclass uuid = env->FindClass("java/util/UUID")) {
        jmethodID from = env->GetStaticMethodID(uuid, "fromString", "(Ljava/lang/String;)Ljava/util/UUID;");
        jstring s = env->NewStringUTF("662A6B8D-DA3E-4C1C-8813-96EA6097278D");
        jobject u = (from && s) ? env->CallStaticObjectMethod(uuid, from, s) : nullptr;
        if (u && !jniFailed(env)) sprintUuid_ = env->NewGlobalRef(u);
        if (u) env->DeleteLocalRef(u);
        if (s) env->DeleteLocalRef(s);
        env->DeleteLocalRef(uuid);
    }
    jniFailed(env);
    mDataWatcher_ = method(env, clsEntity_, "getDataWatcher", "()Lnet/minecraft/entity/DataWatcher;", "H");
    if (jclass dw = McFindClass(env, "net/minecraft/entity/DataWatcher")) {
        mWatchInt_ = method(env, dw, "getWatchableObjectInt", "(I)I");
        env->DeleteLocalRef(dw);
    }
    // Profile textures (identity tools) and skins (faces).
    if (jclass gp = env->FindClass("com/mojang/authlib/GameProfile")) {
        mProfileProps_ = env->GetMethodID(gp, "getProperties", "()Lcom/mojang/authlib/properties/PropertyMap;");
        jniFailed(env);
        env->DeleteLocalRef(gp);
    } else {
        jniFailed(env);
    }
    if (jclass pm = env->FindClass("com/mojang/authlib/properties/PropertyMap")) {
        mPropMapGet_ = env->GetMethodID(pm, "get", "(Ljava/lang/Object;)Ljava/util/Collection;");
        jniFailed(env);
        env->DeleteLocalRef(pm);
    } else {
        jniFailed(env);
    }
    if (jclass prop = env->FindClass("com/mojang/authlib/properties/Property")) {
        mPropValue_ = env->GetMethodID(prop, "getValue", "()Ljava/lang/String;");
        jniFailed(env);
        env->DeleteLocalRef(prop);
    } else {
        jniFailed(env);
    }
    if (jstring k = env->NewStringUTF("textures")) {
        texturesKey_ = env->NewGlobalRef(k);
        env->DeleteLocalRef(k);
    }
    clsAbstractClient_ = globalClass(env, "net/minecraft/client/entity/AbstractClientPlayer");
    if (clsAbstractClient_) mLocationSkin_ = method(env, clsAbstractClient_, "getLocationSkin", "()Lnet/minecraft/util/ResourceLocation;", "i");
    {
        jclass mcc = env->GetObjectClass(mc);
        mTextureManager_ = method(env, mcc, "getTextureManager", "()Lnet/minecraft/client/renderer/texture/TextureManager;");
        env->DeleteLocalRef(mcc);
    }
    if (jclass tm = McFindClass(env, "net/minecraft/client/renderer/texture/TextureManager")) {
        mGetTexture_ = method(env, tm, "getTexture",
                              "(Lnet/minecraft/util/ResourceLocation;)Lnet/minecraft/client/renderer/texture/ITextureObject;");
        env->DeleteLocalRef(tm);
    }
    if (jclass to = McFindClass(env, "net/minecraft/client/renderer/texture/ITextureObject")) {
        mGlTextureId_ = method(env, to, "getGlTextureId", "()I");
        env->DeleteLocalRef(to);
    }
    jniFailed(env);
    // Tab list (who is a real player) and each player's latency. Names confirmed in
    // docs/reference/mcp-obfuscation-map.json: Minecraft#getNetHandler (u),
    // NetHandlerPlayClient#getPlayerInfo(UUID) (a), NetworkPlayerInfo#getResponseTime (c).
    {
        jclass mcc = env->GetObjectClass(mc);
        mNetHandler_ = method(env, mcc, "getNetHandler", "()Lnet/minecraft/client/network/NetHandlerPlayClient;", "u");
        env->DeleteLocalRef(mcc);
    }
    if (jclass nh = McFindClass(env, "net/minecraft/client/network/NetHandlerPlayClient")) {
        mGetPlayerInfo_ = method(env, nh, "getPlayerInfo", "(Ljava/util/UUID;)Lnet/minecraft/client/network/NetworkPlayerInfo;", "a");
        env->DeleteLocalRef(nh);
    }
    if (jclass npi = McFindClass(env, "net/minecraft/client/network/NetworkPlayerInfo")) {
        mResponseTime_ = method(env, npi, "getResponseTime", "()I", "c");
        env->DeleteLocalRef(npi);
    }
    mBurning_ = method(env, clsEntity_, "isBurning", "()Z");
    jniFailed(env);
    signals_.tabList = mNetHandler_ && mGetPlayerInfo_ && mProfileId_;
    signals_.latency = signals_.tabList && mResponseTime_;
    signals_.burning = mBurning_ != nullptr;

    signals_.serverPositions = fServerX_ && fServerY_ && fServerZ_;
    signals_.headRotation = fHeadYaw_ != nullptr;
    signals_.moveAttribute = moveSpeedAttr_ && mGetAttribute_ && mAttrValue_;
    signals_.potionParticles = mDataWatcher_ && mWatchInt_;
    signals_.profiles = mProfileProps_ && mPropMapGet_ && mPropValue_ && texturesKey_;
    signals_.skins = clsAbstractClient_ && mLocationSkin_ && mTextureManager_ && mGetTexture_ && mGlTextureId_;
    signals_.potions = mPotions_ && mPotionId_ && mPotionAmp_ && mToArray_;
    signals_.blocks = mBlockPosCtor_ && mGetBlockState_ && mStateBlock_ && mBlockId_;
    signals_.heldItem = mHeld_ && mStackItem_ && mItemId_;
    signals_.swing = fSwinging_ && fSwingProgress_;
    signals_.hurt = fHurtTime_ != nullptr;

    resolved_ = fWorld_ && fSelf_ && fPlayerEntities_ && fEntityId_ && fPosX_ && fTicksExisted_ && mListGet_;
    log::writef("runtime",
                "sampler: resolved=%d serverPos=%d head=%d attribute=%d potions=%d particles=%d blocks=%d item=%d swing=%d "
                "hurt=%d profiles=%d skins=%d tab=%d latency=%d burning=%d",
                resolved_, signals_.serverPositions, signals_.headRotation, signals_.moveAttribute, signals_.potions,
                signals_.potionParticles, signals_.blocks, signals_.heldItem, signals_.swing, signals_.hurt, signals_.profiles,
                signals_.skins, signals_.tabList, signals_.latency, signals_.burning);
    return resolved_;
}

void Sampler::releaseIdentity(JNIEnv* env, Identity& id) {
    if (env && id.uuidRef) env->DeleteGlobalRef(id.uuidRef);
    id.uuidRef = nullptr;
}

void Sampler::reset(JNIEnv* env) {
    if (env) {
        if (lastWorld_) env->DeleteWeakGlobalRef(lastWorld_);
        if (lastSelf_) env->DeleteWeakGlobalRef(lastSelf_);
    }
    lastWorld_ = lastSelf_ = nullptr;
    lastTicksExisted_ = -1;
    for (auto& kv : identities_) releaseIdentity(env, kv.second);
    identities_.clear();
    faces_.clear();      // texture ids may be reused after a world change
    seenIds_.clear();
    spawns_.clear();
    blockCache_.clear();
}

bool Sampler::faceTexture(const std::string& playerKey, unsigned int& texture) const {
    auto it = faces_.find(playerKey);
    if (it == faces_.end() || it->second == 0) return false;
    texture = it->second;
    return true;
}

void Sampler::readProfileExtras(JNIEnv* env, jobject mc, jobject entity, jobject profile, Identity& id, bool isLocal) {
    const std::string key = !id.uuid.empty() ? id.uuid : "name:" + id.name;
    // Signed textures (identity tools). Read once per entity; nicked players are recognised by
    // their UUID version inside the identity service.
    if (!id.texturesRead && signals_.profiles && profile && !isLocal) {
        id.texturesRead = true;
        std::string textures;
        jobject props = env->CallObjectMethod(profile, mProfileProps_);
        if (props && !jniFailed(env)) {
            jobject coll = env->CallObjectMethod(props, mPropMapGet_, texturesKey_);
            if (coll && !jniFailed(env) && mToArray_) {
                jobjectArray arr = static_cast<jobjectArray>(env->CallObjectMethod(coll, mToArray_));
                if (arr && !jniFailed(env)) {
                    if (env->GetArrayLength(arr) > 0) {
                        jobject prop = env->GetObjectArrayElement(arr, 0);
                        if (prop) {
                            jstring v = static_cast<jstring>(env->CallObjectMethod(prop, mPropValue_));
                            if (v && !jniFailed(env)) textures = jstringToUtf8(env, v);
                            if (v) env->DeleteLocalRef(v);
                            env->DeleteLocalRef(prop);
                        }
                    }
                    env->DeleteLocalRef(arr);
                }
            }
            if (coll) env->DeleteLocalRef(coll);
            env->DeleteLocalRef(props);
        }
        jniFailed(env);
        identity::Service::instance().observe(key, id.name, id.uuid, textures);
    }
    // Skin texture for faces: retried until Minecraft has loaded the skin, then refreshed slowly.
    const detection::Tick interval = id.skinTexture ? 400 : 40;
    if (signals_.skins && tick_ - id.skinAt >= interval && env->IsInstanceOf(entity, clsAbstractClient_)) {
        id.skinAt = tick_;
        jobject rl = env->CallObjectMethod(entity, mLocationSkin_);
        jobject tm = (rl && !jniFailed(env)) ? env->CallObjectMethod(mc, mTextureManager_) : nullptr;
        jobject tex = (tm && !jniFailed(env)) ? env->CallObjectMethod(tm, mGetTexture_, rl) : nullptr;
        if (tex && !jniFailed(env)) {
            const int glId = env->CallIntMethod(tex, mGlTextureId_);
            if (!jniFailed(env) && glId > 0) {
                id.skinTexture = static_cast<unsigned int>(glId);
                faces_[key] = id.skinTexture;
            }
        }
        jniFailed(env);
        if (tex) env->DeleteLocalRef(tex);
        if (tm) env->DeleteLocalRef(tm);
        if (rl) env->DeleteLocalRef(rl);
    }
}

int Sampler::blockAt(JNIEnv* env, jobject world, double x, double y, double z) {
    if (!signals_.blocks || !world) return -1;
    const int bx = static_cast<int>(std::floor(x)), by = static_cast<int>(std::floor(y)), bz = static_cast<int>(std::floor(z));
    // Blocks rarely change; a block probed in the last second is reused.
    const long long key = (static_cast<long long>(bx + (1 << 25)) << 34) ^ (static_cast<long long>(bz + (1 << 25)) << 8) ^
                          static_cast<long long>(by & 0xFF);
    auto it = blockCache_.find(key);
    if (it != blockCache_.end() && tick_ - it->second.at <= 20) return it->second.id;
    if (blockCache_.size() > 8192) blockCache_.clear();
    const int id = probeBlock(env, world, bx, by, bz);
    blockCache_[key] = {id, tick_};
    return id;
}

int Sampler::probeBlock(JNIEnv* env, jobject world, int bx, int by, int bz) {
    jobject pos = env->NewObject(clsBlockPos_, mBlockPosCtor_, static_cast<jint>(bx), static_cast<jint>(by), static_cast<jint>(bz));
    if (!pos || jniFailed(env)) return -1;
    int id = -1;
    jobject state = env->CallObjectMethod(world, mGetBlockState_, pos);
    if (state && !jniFailed(env)) {
        jobject block = env->CallObjectMethod(state, mStateBlock_);
        if (block && !jniFailed(env)) {
            id = env->CallStaticIntMethod(clsBlock_, mBlockId_, block);
            if (jniFailed(env)) id = -1;
            env->DeleteLocalRef(block);
        }
        env->DeleteLocalRef(state);
    } else {
        jniFailed(env);
    }
    env->DeleteLocalRef(pos);
    return id;
}

void Sampler::refreshIdentity(JNIEnv* env, jobject mc, jobject entity, Identity& id, bool isLocal) {
    if (tick_ - id.refreshedAt >= 100 && mProfile_ && mProfileId_ && mProfileName_) {
        id.refreshedAt = tick_;
        jobject profile = env->CallObjectMethod(entity, mProfile_);
        if (profile && !jniFailed(env)) {
            jstring jn = static_cast<jstring>(env->CallObjectMethod(profile, mProfileName_));
            if (jn && !jniFailed(env)) id.name = StripMinecraftFormatting(jstringToUtf8(env, jn));
            if (jn) env->DeleteLocalRef(jn);
            jobject uuid = env->CallObjectMethod(profile, mProfileId_);
            if (uuid && !jniFailed(env) && mUuidToString_) {
                jstring ju = static_cast<jstring>(env->CallObjectMethod(uuid, mUuidToString_));
                if (ju && !jniFailed(env)) id.uuid = jstringToUtf8(env, ju);
                if (ju) env->DeleteLocalRef(ju);
            }
            // Keep the UUID object for tab-list queries (it identifies the profile, never changes).
            if (uuid && !id.uuidRef) id.uuidRef = env->NewGlobalRef(uuid);
            if (uuid) env->DeleteLocalRef(uuid);
            readProfileExtras(env, mc, entity, profile, id, isLocal);
            env->DeleteLocalRef(profile);
        }
    } else {
        readProfileExtras(env, mc, entity, nullptr, id, isLocal);
    }
    if (!isLocal && tick_ - id.teamAt >= 40) {
        id.teamAt = tick_;
        jclass mcClass = env->GetObjectClass(mc);
        jclass entityClass = env->GetObjectClass(entity);
        const std::string team = GetEntityTeamColorName(env, mc, mcClass, entity, entityClass);
        jniFailed(env);
        env->DeleteLocalRef(entityClass);
        env->DeleteLocalRef(mcClass);
        id.teammate = !team.empty() && team != "white" && !localTeam_.empty() && localTeam_ != "white" && team == localTeam_;
    }
}

void Sampler::readPlayer(JNIEnv* env, jobject mc, jobject world, jobject netHandler, jobject e, bool isLocal,
                         detection::PlayerFrame& f) {
    using namespace detection;
    f.entityId = env->GetIntField(e, fEntityId_);
    f.isLocal = isLocal;
    Identity& id = identities_[f.entityId];
    refreshIdentity(env, mc, e, id, isLocal);
    f.uuid = id.uuid;
    f.name = id.name.empty() ? ("#" + std::to_string(f.entityId)) : id.name;
    f.isTeammate = id.teammate;

    // Tab list: real players (nicked or not) are listed; NPCs are removed shortly after spawning.
    if (!isLocal && signals_.tabList && netHandler && id.uuidRef && tick_ - id.tabAt >= 10) {
        id.tabAt = tick_;
        jobject info = env->CallObjectMethod(netHandler, mGetPlayerInfo_, id.uuidRef);
        if (!jniFailed(env)) {
            id.listed = info != nullptr;
            if (info && mResponseTime_) {
                const int ms = env->CallIntMethod(info, mResponseTime_);
                id.pingMs = (!jniFailed(env) && ms > 0 && ms < 5000) ? ms : -1;
            }
        }
        if (info) env->DeleteLocalRef(info);
    }
    f.tabKnown = !isLocal && id.tabAt > -100000;
    f.tabListed = isLocal || id.listed;
    f.pingMs = id.listed ? id.pingMs : -1;
    f.burning = callBool(env, e, mBurning_);

    const double px = env->GetDoubleField(e, fPosX_);
    const double py = env->GetDoubleField(e, fPosY_);
    const double pz = env->GetDoubleField(e, fPosZ_);
    if (!isLocal && signals_.serverPositions) {
        f.pos = {env->GetIntField(e, fServerX_) / 32.0, env->GetIntField(e, fServerY_) / 32.0, env->GetIntField(e, fServerZ_) / 32.0};
        f.serverPosition = true;
        // A fresh entity may not have server coordinates yet.
        if (std::fabs(f.pos.x - px) > 16.0 || std::fabs(f.pos.z - pz) > 16.0) {
            f.pos = {px, py, pz};
            f.serverPosition = false;
        }
    } else {
        f.pos = {px, py, pz};
    }
    f.yaw = (fHeadYaw_ && !isLocal) ? env->GetFloatField(e, fHeadYaw_) : (fYaw_ ? env->GetFloatField(e, fYaw_) : 0.f);
    f.pitch = fPitch_ ? env->GetFloatField(e, fPitch_) : 0.f;
    if (!isLocal && fOtherPitch_ && clsOther_ && env->IsInstanceOf(e, clsOther_)) {
        f.pitch = static_cast<float>(env->GetDoubleField(e, fOtherPitch_));
    }
    f.onGround = fOnGround_ && env->GetBooleanField(e, fOnGround_) == JNI_TRUE;
    f.dead = fIsDead_ && env->GetBooleanField(e, fIsDead_) == JNI_TRUE;
    f.inWeb = fInWeb_ && env->GetBooleanField(e, fInWeb_) == JNI_TRUE;
    jniFailed(env);
    f.sprinting = callBool(env, e, mSprinting_);
    f.sneaking = callBool(env, e, mSneaking_);
    f.invisible = callBool(env, e, mInvisible_);
    f.riding = callBool(env, e, mRiding_);
    f.inWater = callBool(env, e, mInWater_);
    f.inLava = callBool(env, e, mInLava_);
    f.onLadder = callBool(env, e, mOnLadder_);
    f.usingItem = callBool(env, e, mUsing_);
    f.blocking = callBool(env, e, mBlocking_);
    if (fHurtTime_) f.hurtTime = env->GetIntField(e, fHurtTime_);
    if (signals_.swing) {
        f.swingInProgress = env->GetBooleanField(e, fSwinging_) == JNI_TRUE;
        f.swingProgress = env->GetIntField(e, fSwingProgress_);
    }
    if (mHealth_) {
        f.health = env->CallFloatMethod(e, mHealth_);
        if (jniFailed(env)) f.health = -1.f;
    }

    if (signals_.heldItem) {
        jobject stack = env->CallObjectMethod(e, mHeld_);
        if (stack && !jniFailed(env)) {
            jobject item = env->CallObjectMethod(stack, mStackItem_);
            if (item && !jniFailed(env)) {
                f.heldItem = env->CallStaticIntMethod(clsItem_, mItemId_, item);
                if (jniFailed(env)) f.heldItem = -1;
                env->DeleteLocalRef(item);
            }
            env->DeleteLocalRef(stack);
        } else {
            jniFailed(env);
        }
    }

    if (signals_.potions) {
        f.potionsKnown = true;
        jobject coll = env->CallObjectMethod(e, mPotions_);
        if (coll && !jniFailed(env)) {
            jobjectArray arr = static_cast<jobjectArray>(env->CallObjectMethod(coll, mToArray_));
            if (arr && !jniFailed(env)) {
                const jsize n = env->GetArrayLength(arr);
                for (jsize i = 0; i < n; ++i) {
                    jobject pe = env->GetObjectArrayElement(arr, i);
                    if (!pe) continue;
                    const int pid = env->CallIntMethod(pe, mPotionId_);
                    const int amp = env->CallIntMethod(pe, mPotionAmp_);
                    if (!jniFailed(env)) {
                        ++f.potions.count;
                        if (pid == 1) f.potions.speed = amp;
                        else if (pid == 2) f.potions.slowness = amp;
                        else if (pid == 8) f.potions.jumpBoost = amp;
                    }
                    env->DeleteLocalRef(pe);
                }
                env->DeleteLocalRef(arr);
            }
            env->DeleteLocalRef(coll);
        } else {
            f.potionsKnown = !jniFailed(env);
        }
    }

    // Potion particle color (DataWatcher 7) is synced continuously, unlike the effect list.
    if (signals_.potionParticles) {
        jobject dw = env->CallObjectMethod(e, mDataWatcher_);
        if (dw && !jniFailed(env)) {
            const int color = env->CallIntMethod(dw, mWatchInt_, 7);
            if (!jniFailed(env)) f.potionParticles = color != 0;
            env->DeleteLocalRef(dw);
        }
    }
    // Movement-speed attribute and whether it currently carries the sprint boost.
    if (signals_.moveAttribute) {
        jobject inst = env->CallObjectMethod(e, mGetAttribute_, moveSpeedAttr_);
        if (inst && !jniFailed(env)) {
            const double v = env->CallDoubleMethod(inst, mAttrValue_);
            if (!jniFailed(env) && v > 0.0 && v < 10.0) f.moveSpeedAttribute = v;
            if (mAttrModifier_ && sprintUuid_) {
                jobject mod = env->CallObjectMethod(inst, mAttrModifier_, sprintUuid_);
                if (!jniFailed(env)) f.sprintModifier = mod != nullptr;
                if (mod) env->DeleteLocalRef(mod);
            } else {
                f.sprintModifier = f.sprinting;
            }
            env->DeleteLocalRef(inst);
        }
    }

    if (signals_.blocks) {
        f.blockBelow = blockAt(env, world, f.pos.x, f.pos.y - 0.2, f.pos.z);
        // Ceiling over the whole hitbox top: a block over any corner stops a jump (head-hitters at
        // the edge of an overhang). Corners in the same block as the center cost nothing (cache).
        f.blockAboveHead = blockAt(env, world, f.pos.x, f.pos.y + 2.05, f.pos.z);
        if (!detection::mc::isSolidish(f.blockAboveHead)) {
            const double h = detection::mc::kPlayerHalfWidth - 0.001;
            const double corners[4][2] = {{h, h}, {h, -h}, {-h, h}, {-h, -h}};
            for (const auto& c : corners) {
                const int corner = blockAt(env, world, f.pos.x + c[0], f.pos.y + 2.05, f.pos.z + c[1]);
                if (detection::mc::isSolidish(corner)) { f.blockAboveHead = corner; break; }
            }
        }
        if (f.hurtTime > 0) {
            // Walls in eight directions at feet and head height (Velocity: a wall behind the victim
            // in the push direction stops the push). Direction i is MC yaw i × 45°.
            for (int i = 0; i < 8; ++i) {
                const double yaw = i * 45.0 * 3.14159265358979323846 / 180.0;
                const double reach = (i % 2) ? 0.5 : 0.62;
                const double dx = -std::sin(yaw) * reach, dz = std::cos(yaw) * reach;
                for (double hy : {0.5, 1.5}) {
                    if (detection::mc::isSolidish(blockAt(env, world, f.pos.x + dx, f.pos.y + hy, f.pos.z + dz))) {
                        f.wallMask = static_cast<std::uint8_t>(f.wallMask | (1u << i));
                        break;
                    }
                }
            }
            f.wallNear = f.wallMask != 0;
        }
    }
}

bool Sampler::sample(JNIEnv* env, jobject mc, detection::WorldFrame& out, bool& worldChanged) {
    worldChanged = false;
    if (!env || !mc) return false;
    if (!resolve(env, mc)) return false;

    jobject world = env->GetObjectField(mc, fWorld_);
    jobject self = env->GetObjectField(mc, fSelf_);
    if (jniFailed(env) || !world || !self) {
        if (world) env->DeleteLocalRef(world);
        if (self) env->DeleteLocalRef(self);
        if (lastWorld_) { worldChanged = true; reset(env); }
        return false;
    }

    if (!lastWorld_ || !env->IsSameObject(world, lastWorld_)) {
        if (lastWorld_) worldChanged = true;
        reset(env);
        lastWorld_ = env->NewWeakGlobalRef(world);
    }
    if (!lastSelf_ || !env->IsSameObject(self, lastSelf_)) {
        if (lastSelf_) env->DeleteWeakGlobalRef(lastSelf_);
        lastSelf_ = env->NewWeakGlobalRef(self);
        lastTicksExisted_ = -1;  // respawn: tick counter restarts
    }

    const int te = env->GetIntField(self, fTicksExisted_);
    if (te == lastTicksExisted_) {
        env->DeleteLocalRef(world);
        env->DeleteLocalRef(self);
        return false;
    }
    int delta = lastTicksExisted_ < 0 ? 1 : te - lastTicksExisted_;
    if (delta <= 0 || delta > 100) delta = 1;
    lastTicksExisted_ = te;
    tick_ += delta;

    out.tick = tick_;
    out.skippedTicks = delta - 1;
    out.players.clear();

    if (tick_ - localTeamAt_ >= 40) {
        localTeamAt_ = tick_;
        jclass mcClass = env->GetObjectClass(mc);
        jclass selfClass = env->GetObjectClass(self);
        localTeam_ = GetEntityTeamColorName(env, mc, mcClass, self, selfClass);
        jniFailed(env);
        env->DeleteLocalRef(selfClass);
        env->DeleteLocalRef(mcClass);
    }

    jobject netHandler = signals_.tabList ? env->CallObjectMethod(mc, mNetHandler_) : nullptr;
    if (jniFailed(env)) netHandler = nullptr;
    jobject list = env->GetObjectField(world, fPlayerEntities_);
    if (list && !jniFailed(env)) {
        const int n = env->CallIntMethod(list, mListSize_);
        if (!jniFailed(env)) {
            out.players.reserve(static_cast<std::size_t>(n));
            for (int i = 0; i < n && i < 256; ++i) {
                if (env->PushLocalFrame(64) != 0) { jniFailed(env); break; }
                jobject e = env->CallObjectMethod(list, mListGet_, i);
                if (e && !jniFailed(env)) {
                    detection::PlayerFrame f;
                    readPlayer(env, mc, world, netHandler, e, env->IsSameObject(e, self) == JNI_TRUE, f);
                    jniFailed(env);
                    if (!f.isLocal && seenIds_.emplace(f.entityId, true).second) spawns_.push_back(f.entityId);
                    out.players.push_back(std::move(f));
                }
                env->PopLocalFrame(nullptr);
            }
        }
        env->DeleteLocalRef(list);
    }
    if (netHandler) env->DeleteLocalRef(netHandler);

    // Forget identities of entities that left.
    if (tick_ % 200 == 0) {
        for (auto it = identities_.begin(); it != identities_.end();) {
            bool present = false;
            for (const auto& p : out.players) if (p.entityId == it->first) { present = true; break; }
            if (present) {
                ++it;
            } else {
                releaseIdentity(env, it->second);
                it = identities_.erase(it);
            }
        }
    }

    env->DeleteLocalRef(world);
    env->DeleteLocalRef(self);
    return true;
}

}  // namespace vigil::game

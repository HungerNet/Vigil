#include "anticheat.h"
#include "class_finder.h"
#include "utils.h"
#include "jni_helpers.h"
#include <unordered_set>
#include <cstdarg>

static void AcLogLine(const std::string& msg) {
    AppendVigilNamedLog("vigil_anticheat.log", msg);
}

static void AcLogf(const char* fmt, ...) {
    if (!fmt) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    AcLogLine(buf);
}

static void AcJsonLog(const std::string& json) {
    AcLogLine(json);
}

std::map<int, ACEntityData>         g_acData;
std::map<int, std::string>          g_entityIdToUUID;
std::map<int, std::string>          g_entityIdToName;
std::map<std::string, std::string>  g_uuidToName;
static std::mutex g_acDataMutex;

static const int SWORD_IDS[] = {
    267, 268, 272, 276, 283, // iron, wood, stone, diamond, gold
    -1
};
static const int CONSUMABLE_IDS[] = {
    373, 374, 261, 346, // potion, glass_bottle, bow, fishing_rod
    -1
};

static bool IsSwordId(int id) {
    for (int i = 0; SWORD_IDS[i] != -1; ++i) if (SWORD_IDS[i] == id) return true;
    return false;
}
static bool IsConsumableId(int id) {
    for (int i = 0; CONSUMABLE_IDS[i] != -1; ++i) if (CONSUMABLE_IDS[i] == id) return true;
    return false;
}
static bool IsBlockId(int id) { return id >= 1 && id <= 255; }

static constexpr ULONGLONG kAutoBlockMinBlockMs = 200;
static constexpr float kLegitScaffPitchThreshold = 60.0f;
static constexpr float kLegitScaffMinSpeed = 0.380f;
static constexpr float kScaffoldBMinSpeed = 0.400f;
static constexpr int kScaffoldBFlagVL = 9 ;
static constexpr ULONGLONG kLegitScaffBRecentSneakMs = 1200;
static constexpr float kLegitScaffBMinSneakDist = 1.2f;
static constexpr float kLegitScaffBMaxSneakDist = 7.0f;
static constexpr int kLegitScaffBFlagVL = 5;
static constexpr ULONGLONG kLegitScaffBDecayMs = 1000;
static constexpr ULONGLONG kLegitScaffHitGraceMs = 1500;
static constexpr float kLegitScaffKnockbackSpeed = 0.65f;
static constexpr float kScaffoldAPitchThreshold = 55.0f;
static constexpr float kScaffoldAJumpDY = 0.08f;
static constexpr float kScaffoldAMaxHeightGain = 1.05f;
static constexpr float kScaffoldAPatternMaxHeightGain = 0.65f;
static constexpr float kScaffoldAPatternMaxHeightGainWide = 0.75f;
static constexpr float kScaffoldAPatternWideHoriz = 2.5f;
static constexpr float kScaffoldAPatternMaxPeakGain = 0.50f;
static constexpr int kScaffoldAFlatBlocks = 4;
static constexpr int kScaffoldAMinFlatSwings = 1;
static constexpr float kScaffoldAMinFlatHoriz = 2.0f;
static constexpr float kScaffoldAMaxFlatHoriz = 12.0f;
static constexpr float kScaffoldATowerClimbDY = 0.65f;
static constexpr float kScaffoldATowerMaxHoriz = 1.8f;
static constexpr int kScaffoldATowerStreakMax = 2;
static constexpr ULONGLONG kScaffoldATowerWindowMs = 5000;
static constexpr ULONGLONG kScaffoldASessionMs = 2500;
static constexpr int kScaffoldAFlagVL = 4;
static constexpr ULONGLONG kScaffoldAInactivityResetMs = 1500;

static std::string GetEntityUUIDString(JNIEnv* env, jobject entity, jclass entityClass);

static int GetHeldItemId(JNIEnv* env, jobject entity, jclass entityClass) {
    jmethodID midHeld = env->GetMethodID(entityClass, "getCurrentEquippedItem", "()Lnet/minecraft/item/ItemStack;");
    if (!midHeld) { env->ExceptionClear(); midHeld = env->GetMethodID(entityClass, "getHeldItem", "()Lnet/minecraft/item/ItemStack;"); }
    jobject stack = nullptr;
    if (midHeld) {
        stack = env->CallObjectMethod(entity, midHeld);
        if (env->ExceptionCheck()) { env->ExceptionClear(); stack = nullptr; }
    } else {
        env->ExceptionClear();
    }

    // Fallback: inventory.currentItem + getStackInSlot (EntityOtherPlayerMP on some Lunar builds).
    if (!stack) {
        jfieldID fidInv = env->GetFieldID(entityClass, "inventory", "Lnet/minecraft/entity/player/InventoryPlayer;");
        if (!fidInv || env->ExceptionCheck()) {
            env->ExceptionClear();
            return -1;
        }
        jobject inv = env->GetObjectField(entity, fidInv);
        if (!inv || env->ExceptionCheck()) {
            env->ExceptionClear();
            return -1;
        }
        jclass invClass = env->GetObjectClass(inv);
        jfieldID fidSlot = env->GetFieldID(invClass, "currentItem", "I");
        jmethodID midSlot = env->GetMethodID(invClass, "getStackInSlot", "(I)Lnet/minecraft/item/ItemStack;");
        if (fidSlot && midSlot && !env->ExceptionCheck()) {
            const int slot = env->GetIntField(inv, fidSlot);
            stack = env->CallObjectMethod(inv, midSlot, slot);
            if (env->ExceptionCheck()) { env->ExceptionClear(); stack = nullptr; }
        } else {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(invClass);
        env->DeleteLocalRef(inv);
    }
    if (!stack) return -1;

    jclass stackClass = env->GetObjectClass(stack);
    jmethodID midGetItem = env->GetMethodID(stackClass, "getItem", "()Lnet/minecraft/item/Item;");
    if (!midGetItem) {
        env->ExceptionClear();
        env->DeleteLocalRef(stackClass);
        env->DeleteLocalRef(stack);
        return -1;
    }
    jobject item = env->CallObjectMethod(stack, midGetItem);
    if (!item || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(stackClass);
        env->DeleteLocalRef(stack);
        return -1;
    }

    int itemId = -1;
    jclass itemClass = env->GetObjectClass(item);
    jmethodID midGetId = JniResolveStaticMethod(env, itemClass, "(Lnet/minecraft/item/Item;)I",
                                                "getIdFromItem", "b");
    if (!midGetId) {
        jclass itemBase = McFindClass(env, "net/minecraft/item/Item");
        if (itemBase) {
            midGetId = JniResolveStaticMethod(env, itemBase, "(Lnet/minecraft/item/Item;)I",
                                              "getIdFromItem", "b");
            if (midGetId) {
                itemId = env->CallStaticIntMethod(itemBase, midGetId, item);
                if (env->ExceptionCheck()) { env->ExceptionClear(); itemId = -1; }
            }
            env->DeleteLocalRef(itemBase);
        }
    } else {
        itemId = env->CallStaticIntMethod(itemClass, midGetId, item);
        if (env->ExceptionCheck()) { env->ExceptionClear(); itemId = -1; }
    }
    env->DeleteLocalRef(itemClass);
    env->DeleteLocalRef(item);
    env->DeleteLocalRef(stackClass);
    env->DeleteLocalRef(stack);
    return itemId;
}

static std::string GetEntityUUIDString(JNIEnv* env, jobject entity, jclass entityClass) {
    jmethodID midProfile = env->GetMethodID(entityClass, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;");
    if (!midProfile) { env->ExceptionClear(); return ""; }
    jobject profile = env->CallObjectMethod(entity, midProfile);
    if (!profile || env->ExceptionCheck()) { env->ExceptionClear(); return ""; }

    jclass profileClass = env->GetObjectClass(profile);
    jmethodID midGetId = env->GetMethodID(profileClass, "getId", "()Ljava/util/UUID;");
    if (!midGetId || env->ExceptionCheck()) {
        env->ExceptionClear(); env->DeleteLocalRef(profileClass); env->DeleteLocalRef(profile); return "";
    }
    jobject uuidObj = env->CallObjectMethod(profile, midGetId);
    if (!uuidObj || env->ExceptionCheck()) {
        env->ExceptionClear(); env->DeleteLocalRef(profileClass); env->DeleteLocalRef(profile); return "";
    }

    jclass uuidClass = env->GetObjectClass(uuidObj);
    jmethodID midToString = env->GetMethodID(uuidClass, "toString", "()Ljava/lang/String;");
    std::string uuid;
    if (midToString && !env->ExceptionCheck()) {
        jstring jUuid = (jstring)env->CallObjectMethod(uuidObj, midToString);
        if (jUuid && !env->ExceptionCheck()) {
            const char* u = env->GetStringUTFChars(jUuid, nullptr);
            uuid = u ? u : "";
            if (u) env->ReleaseStringUTFChars(jUuid, u);
            env->DeleteLocalRef(jUuid);
        } else { env->ExceptionClear(); }
    } else { env->ExceptionClear(); }

    env->DeleteLocalRef(uuidClass);
    env->DeleteLocalRef(uuidObj);
    env->DeleteLocalRef(profileClass);
    env->DeleteLocalRef(profile);
    return uuid;
}

static void RefreshUUIDNameMapFromNetHandler(JNIEnv* env, jobject mcObj, jclass mcClass) {
    jmethodID midNetHandler = env->GetMethodID(mcClass, "getNetHandler", "()Lnet/minecraft/client/network/NetHandlerPlayClient;");
    if (!midNetHandler || env->ExceptionCheck()) { env->ExceptionClear(); return; }
    jobject netHandler = env->CallObjectMethod(mcObj, midNetHandler);
    if (!netHandler || env->ExceptionCheck()) { env->ExceptionClear(); return; }

    jclass nhClass = env->GetObjectClass(netHandler);
    jmethodID midInfoMap = env->GetMethodID(nhClass, "getPlayerInfoMap", "()Ljava/util/Collection;");
    if (!midInfoMap || env->ExceptionCheck()) {
        env->ExceptionClear(); env->DeleteLocalRef(nhClass); env->DeleteLocalRef(netHandler); return;
    }

    jobject infos = env->CallObjectMethod(netHandler, midInfoMap);
    env->DeleteLocalRef(nhClass); env->DeleteLocalRef(netHandler);
    if (!infos || env->ExceptionCheck()) { env->ExceptionClear(); return; }

    jclass infosClass = env->GetObjectClass(infos);
    jmethodID midIterator = env->GetMethodID(infosClass, "iterator", "()Ljava/util/Iterator;");
    if (!midIterator || env->ExceptionCheck()) {
        env->ExceptionClear(); env->DeleteLocalRef(infosClass); env->DeleteLocalRef(infos); return;
    }

    jobject it = env->CallObjectMethod(infos, midIterator);
    env->DeleteLocalRef(infosClass); env->DeleteLocalRef(infos);
    if (!it || env->ExceptionCheck()) { env->ExceptionClear(); return; }

    jclass itClass = env->GetObjectClass(it);
    jmethodID midHasNext = env->GetMethodID(itClass, "hasNext", "()Z");
    jmethodID midNext    = env->GetMethodID(itClass, "next",    "()Ljava/lang/Object;");
    if (!midHasNext || !midNext || env->ExceptionCheck()) {
        env->ExceptionClear(); env->DeleteLocalRef(itClass); env->DeleteLocalRef(it); return;
    }

    while (env->CallBooleanMethod(it, midHasNext)) {
        jobject info = env->CallObjectMethod(it, midNext);
        if (!info || env->ExceptionCheck()) { env->ExceptionClear(); continue; }

        jclass infoClass = env->GetObjectClass(info);
        jmethodID midGetProfile = env->GetMethodID(infoClass, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;");
        if (!midGetProfile || env->ExceptionCheck()) {
            env->ExceptionClear(); env->DeleteLocalRef(infoClass); env->DeleteLocalRef(info); continue;
        }

        jobject profile = env->CallObjectMethod(info, midGetProfile);
        env->DeleteLocalRef(infoClass); env->DeleteLocalRef(info);
        if (!profile || env->ExceptionCheck()) { env->ExceptionClear(); continue; }

        jclass profileClass = env->GetObjectClass(profile);
        jmethodID midGetId   = env->GetMethodID(profileClass, "getId",  "()Ljava/util/UUID;");
        jmethodID midGetName = env->GetMethodID(profileClass, "getName","()Ljava/lang/String;");
        if (!midGetId || !midGetName || env->ExceptionCheck()) {
            env->ExceptionClear(); env->DeleteLocalRef(profileClass); env->DeleteLocalRef(profile); continue;
        }

        jobject uuidObj = env->CallObjectMethod(profile, midGetId);
        jstring jName   = (jstring)env->CallObjectMethod(profile, midGetName);
        if (!uuidObj || !jName || env->ExceptionCheck()) {
            env->ExceptionClear();
            if (uuidObj) env->DeleteLocalRef(uuidObj);
            if (jName)   env->DeleteLocalRef(jName);
            env->DeleteLocalRef(profileClass); env->DeleteLocalRef(profile); continue;
        }

        jclass uuidClass = env->GetObjectClass(uuidObj);
        jmethodID midToString = env->GetMethodID(uuidClass, "toString", "()Ljava/lang/String;");
        if (!midToString || env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(uuidClass); env->DeleteLocalRef(uuidObj);
            env->DeleteLocalRef(jName); env->DeleteLocalRef(profileClass); env->DeleteLocalRef(profile); continue;
        }

        jstring jUuid = (jstring)env->CallObjectMethod(uuidObj, midToString);
        if (!jUuid || env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(uuidClass); env->DeleteLocalRef(uuidObj);
            env->DeleteLocalRef(jName); env->DeleteLocalRef(profileClass); env->DeleteLocalRef(profile); continue;
        }

        const char* uuidChars = env->GetStringUTFChars(jUuid, nullptr);
        const char* nameChars = env->GetStringUTFChars(jName, nullptr);
        if (uuidChars && nameChars) {
            std::string uuid = uuidChars;
            std::string name = StripMinecraftFormatting(nameChars);
            if (!uuid.empty() && !name.empty()) g_uuidToName[uuid] = name;
        }
        if (uuidChars) env->ReleaseStringUTFChars(jUuid, uuidChars);
        if (nameChars) env->ReleaseStringUTFChars(jName, nameChars);

        env->DeleteLocalRef(jUuid);
        env->DeleteLocalRef(uuidClass);
        env->DeleteLocalRef(uuidObj);
        env->DeleteLocalRef(jName);
        env->DeleteLocalRef(profileClass);
        env->DeleteLocalRef(profile);
    }

    env->DeleteLocalRef(itClass);
    env->DeleteLocalRef(it);
}

static std::string GetEntityPlayerName(JNIEnv* env, jobject mcObj, jclass mcClass, jobject entity, jclass entityClass) {
    jmethodID midProfile = env->GetMethodID(entityClass, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;");
    if (!midProfile) { env->ExceptionClear(); return "Unknown"; }
    jobject profile = env->CallObjectMethod(entity, midProfile);
    if (!profile || env->ExceptionCheck()) { env->ExceptionClear(); return "Unknown"; }

    jclass profileClass = env->GetObjectClass(profile);
    jmethodID midGetName = env->GetMethodID(profileClass, "getName", "()Ljava/lang/String;");
    jmethodID midGetId   = env->GetMethodID(profileClass, "getId",   "()Ljava/util/UUID;");
    std::string name = "Unknown";
    std::string entityProfileName = "Unknown";
    std::string tablistName;
    std::string displayNameFlat;
    std::string teamMemberNameCandidate;
    std::string tabFailReason;

    if (midGetName) {
        jstring jname = (jstring)env->CallObjectMethod(profile, midGetName);
        if (jname && !env->ExceptionCheck()) {
            const char* n = env->GetStringUTFChars(jname, nullptr);
            entityProfileName = n ? n : "Unknown";
            if (n) env->ReleaseStringUTFChars(jname, n);
            env->DeleteLocalRef(jname);
        } else { env->ExceptionClear(); tabFailReason = "entity_getName_failed"; }
    } else { env->ExceptionClear(); tabFailReason = "entity_getName_method_missing"; }

    if (midGetId) {
        jobject uuidObj = env->CallObjectMethod(profile, midGetId);
        if (uuidObj && !env->ExceptionCheck()) {
            jmethodID midNetHandler = env->GetMethodID(mcClass, "getNetHandler", "()Lnet/minecraft/client/network/NetHandlerPlayClient;");
            if (midNetHandler && !env->ExceptionCheck()) {
                jobject netHandler = env->CallObjectMethod(mcObj, midNetHandler);
                if (netHandler && !env->ExceptionCheck()) {
                    jclass nhClass = env->GetObjectClass(netHandler);
                    jmethodID midGetInfoByUUID = env->GetMethodID(nhClass, "getPlayerInfo", "(Ljava/util/UUID;)Lnet/minecraft/client/network/NetworkPlayerInfo;");
                    if (midGetInfoByUUID && !env->ExceptionCheck()) {
                        jobject info = env->CallObjectMethod(netHandler, midGetInfoByUUID, uuidObj);
                        if (info && !env->ExceptionCheck()) {
                            jclass infoClass = env->GetObjectClass(info);
                            jmethodID midInfoGetProfile = env->GetMethodID(infoClass, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;");
                            if (midInfoGetProfile && !env->ExceptionCheck()) {
                                jobject tabProfile = env->CallObjectMethod(info, midInfoGetProfile);
                                if (tabProfile && !env->ExceptionCheck()) {
                                    jclass tabProfileClass = env->GetObjectClass(tabProfile);
                                    jmethodID midTabGetName = env->GetMethodID(tabProfileClass, "getName", "()Ljava/lang/String;");
                                    if (midTabGetName && !env->ExceptionCheck()) {
                                        jstring jTabName = (jstring)env->CallObjectMethod(tabProfile, midTabGetName);
                                        if (jTabName && !env->ExceptionCheck()) {
                                            const char* n = env->GetStringUTFChars(jTabName, nullptr);
                                            tablistName = n ? n : "";
                                            if (n) env->ReleaseStringUTFChars(jTabName, n);
                                            env->DeleteLocalRef(jTabName);
                                        } else { env->ExceptionClear(); tabFailReason = "tab_getName_call_failed"; }
                                    } else { env->ExceptionClear(); tabFailReason = "tab_getName_method_missing"; }
                                    env->DeleteLocalRef(tabProfileClass);
                                    env->DeleteLocalRef(tabProfile);
                                } else { env->ExceptionClear(); tabFailReason = "tab_profile_null"; }
                            } else { env->ExceptionClear(); tabFailReason = "npi_getProfile_method_missing"; }

                            // Try displayName
                            jmethodID midGetDisplayName = env->GetMethodID(infoClass, "getDisplayName", "()Lnet/minecraft/util/IChatComponent;");
                            if (midGetDisplayName && !env->ExceptionCheck()) {
                                jobject displayComp = env->CallObjectMethod(info, midGetDisplayName);
                                if (displayComp && !env->ExceptionCheck()) {
                                    jclass compClass = env->GetObjectClass(displayComp);
                                    jmethodID midUnformatted = env->GetMethodID(compClass, "getUnformattedText", "()Ljava/lang/String;");
                                    if (midUnformatted && !env->ExceptionCheck()) {
                                        jstring jDisp = (jstring)env->CallObjectMethod(displayComp, midUnformatted);
                                        if (jDisp && !env->ExceptionCheck()) {
                                            const char* d = env->GetStringUTFChars(jDisp, nullptr);
                                            displayNameFlat = d ? d : "";
                                            if (d) env->ReleaseStringUTFChars(jDisp, d);
                                            env->DeleteLocalRef(jDisp);
                                        } else { env->ExceptionClear(); }
                                    } else { env->ExceptionClear(); }
                                    env->DeleteLocalRef(compClass);
                                    env->DeleteLocalRef(displayComp);
                                } else { env->ExceptionClear(); }
                            } else { env->ExceptionClear(); }

                            // Try team membership
                            jmethodID midGetTeam = env->GetMethodID(infoClass, "getPlayerTeam", "()Lnet/minecraft/scoreboard/ScorePlayerTeam;");
                            if (midGetTeam && !env->ExceptionCheck()) {
                                jobject teamObj = env->CallObjectMethod(info, midGetTeam);
                                if (teamObj && !env->ExceptionCheck()) {
                                    jclass teamClass = env->GetObjectClass(teamObj);
                                    jmethodID midMembers = env->GetMethodID(teamClass, "getMembershipCollection", "()Ljava/util/Collection;");
                                    if (midMembers && !env->ExceptionCheck()) {
                                        jobject members = env->CallObjectMethod(teamObj, midMembers);
                                        if (members && !env->ExceptionCheck()) {
                                            jclass membersClass = env->GetObjectClass(members);
                                            jmethodID midIterator = env->GetMethodID(membersClass, "iterator", "()Ljava/util/Iterator;");
                                            if (midIterator && !env->ExceptionCheck()) {
                                                jobject iter = env->CallObjectMethod(members, midIterator);
                                                if (iter && !env->ExceptionCheck()) {
                                                    jclass iterClass = env->GetObjectClass(iter);
                                                    jmethodID midHasNext = env->GetMethodID(iterClass, "hasNext", "()Z");
                                                    jmethodID midNext    = env->GetMethodID(iterClass, "next",    "()Ljava/lang/Object;");
                                                    if (midHasNext && midNext && !env->ExceptionCheck()) {
                                                        while (env->CallBooleanMethod(iter, midHasNext)) {
                                                            jobject obj = env->CallObjectMethod(iter, midNext);
                                                            if (obj && !env->ExceptionCheck()) {
                                                                jstring jMember = (jstring)obj;
                                                                const char* m = env->GetStringUTFChars(jMember, nullptr);
                                                                std::string member = m ? m : "";
                                                                if (m) env->ReleaseStringUTFChars(jMember, m);
                                                                env->DeleteLocalRef(jMember);
                                                                if (member.size() >= 3 && member.size() <= 16 && member.size() > teamMemberNameCandidate.size()) {
                                                                    teamMemberNameCandidate = member;
                                                                }
                                                            } else { env->ExceptionClear(); }
                                                        }
                                                    } else { env->ExceptionClear(); }
                                                    env->DeleteLocalRef(iterClass);
                                                    env->DeleteLocalRef(iter);
                                                } else { env->ExceptionClear(); }
                                            } else { env->ExceptionClear(); }
                                            env->DeleteLocalRef(membersClass);
                                            env->DeleteLocalRef(members);
                                        } else { env->ExceptionClear(); }
                                    } else { env->ExceptionClear(); }
                                    env->DeleteLocalRef(teamClass);
                                    env->DeleteLocalRef(teamObj);
                                } else { env->ExceptionClear(); }
                            } else { env->ExceptionClear(); }

                            env->DeleteLocalRef(infoClass);
                            env->DeleteLocalRef(info);
                        } else { env->ExceptionClear(); tabFailReason = "playerInfo_by_uuid_null"; }
                    } else { env->ExceptionClear(); tabFailReason = "nh_getPlayerInfo_method_missing"; }
                    env->DeleteLocalRef(nhClass);
                    env->DeleteLocalRef(netHandler);
                } else { env->ExceptionClear(); tabFailReason = "netHandler_null"; }
            } else { env->ExceptionClear(); tabFailReason = "mc_getNetHandler_method_missing"; }
            env->DeleteLocalRef(uuidObj);
        } else { env->ExceptionClear(); tabFailReason = "profile_getId_call_failed"; }
    } else { env->ExceptionClear(); tabFailReason = "profile_getId_method_missing"; }

    if (!tablistName.empty()) {
        name = tablistName;

        auto stripFormat = [](const std::string& s) {
            std::string out;
            for (std::string::size_type i = 0; i < s.size(); ) {
                unsigned char b = (unsigned char)s[i];
                if (b == 0xC2 && i + 1 < s.size() && (unsigned char)s[i+1] == 0xA7) {
                    i += (i + 2 < s.size()) ? 3 : 2;
                } else if (b == 0xA7) {
                    i += (i + 1 < s.size()) ? 2 : 1;
                } else { out += s[i++]; }
            }
            return out;
        };

        std::string baseName = stripFormat(tablistName);

        if (!teamMemberNameCandidate.empty() && !baseName.empty() &&
            teamMemberNameCandidate.rfind(baseName, 0) == 0 &&
            teamMemberNameCandidate.size() > baseName.size()) {
            name = teamMemberNameCandidate;
        }

        if (tablistName.size() >= 3 && tablistName.size() == 16 &&
            (unsigned char)tablistName[tablistName.size()-3] == 0xC2 &&
            (unsigned char)tablistName[tablistName.size()-2] == 0xA7 &&
            !displayNameFlat.empty()) {
            auto isNameChar = [](char c) -> bool {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
            };
            std::string best;
            std::string cur;
            for (char c : displayNameFlat) {
                if (isNameChar(c)) { cur.push_back(c); }
                else {
                    if (cur.size() >= 3 && cur.size() <= 16 && cur.size() > best.size()) best = cur;
                    cur.clear();
                }
            }
            if (cur.size() >= 3 && cur.size() <= 16 && cur.size() > best.size()) best = cur;
            if (!best.empty()) name = best;
        }
    } else {
        name = entityProfileName;
    }

    env->DeleteLocalRef(profileClass);
    env->DeleteLocalRef(profile);
    return name;
}


static char AcFlagCheckColorCode(const std::string& checkName) {
    if (checkName == "LegitScaff") return 'e';
    if (checkName == "Keep-y" || checkName == "ScaffoldA") return 'd';
    if (checkName == "NoSlow") return '5';
    if (checkName == "AutoBlock") return '4';
    return 'c';
}

static std::string ResolveAcFlagPlayerColorName(JNIEnv* env, jobject mcObj, jclass mcClass,
                                                jobject entity, jclass entityClass,
                                                ACEntityData& d) {
    std::string colorName = d.cachedTeamColor;
    if (colorName.empty() || colorName == "white") {
        colorName = GetEntityTeamColorName(env, mcObj, mcClass, entity, entityClass);
        if (!colorName.empty() && colorName != "white") {
            d.cachedTeamColor = colorName;
            d.teamColorResolved = true;
        }
    }
    if (colorName.empty() || colorName == "white") {
        const int rgb = GetEntityTeamRgb(env, mcObj, mcClass, entity, entityClass);
        if (rgb > 0 && rgb != 0xFFFFFF) {
            const std::string fromRgb = TeamRgbToColorName(rgb);
            if (!fromRgb.empty() && fromRgb != "white") {
                colorName = fromRgb;
                d.cachedTeamColor = colorName;
                d.teamColorResolved = true;
            }
        }
    }
    return colorName.empty() ? "white" : colorName;
}

static std::string BuildFlagLocalChatMessage(const std::string& checkName,
                                             const std::string& cleanName,
                                             char nameMcCode) {
    const std::string sec = "\xC2\xA7";
    const char checkCode = AcFlagCheckColorCode(checkName);
    return sec + "7[" + sec + "1Fre" + sec + "fnch" + sec + "cify" + sec + "7] " +
           sec + std::string(1, nameMcCode) + cleanName + sec + "7 flagged " +
           sec + std::string(1, checkCode) + checkName;
}

void RunAnticheatChecks(JNIEnv* env, jobject mcObj, jclass mcClass, jclass customGuiClass) {
    static bool s_wasEnabled = false;
    static bool s_loggedConfig = false;
    std::lock_guard<std::mutex> guard(g_acDataMutex);
    if (!customGuiClass) return;
    auto readBool = [&](const char* nm) -> bool {
        jfieldID fid = env->GetStaticFieldID(customGuiClass, nm, "Z");
        if (!fid) { env->ExceptionClear(); return false; }
        return (bool)env->GetStaticBooleanField(customGuiClass, fid);
    };
    if (!readBool("anticheat")) {
        if (s_wasEnabled) {
            AcLogf("anticheat OFF (cleared state)");
            s_wasEnabled = false;
        }
        s_loggedConfig = false;
        g_acData.clear();
        g_entityIdToUUID.clear();
        g_entityIdToName.clear();
        {
            std::lock_guard<std::mutex> lock(g_flaggedPlayersMutex);
            g_flaggedPlayersLastCheckByUuid.clear();
            g_flaggedPlayerCountsByUuid.clear();
            g_flaggedPlayersLastCheckByNameLower.clear();
            g_flaggedPlayerCountsByNameLower.clear();
        }
        return;
    }
    bool ac_flagsound  = readBool("ac_flagsound");
    bool ac_autoblock  = readBool("ac_autoblock");
    bool ac_noslow     = readBool("ac_noslow");
    bool ac_scaffold   = readBool("ac_scaffold");
    bool ac_scaffoldb     = readBool("ac_scaffoldb");
    bool ac_legitscaff    = readBool("ac_legitscaff");
    bool ac_bhop       = readBool("ac_bhop");
    bool ac_killaura   = readBool("ac_killaura");
    bool ac_ignoreTeammates = readBool("ignoreTeammates");

    {
        if (!s_loggedConfig) {
            s_loggedConfig = true;
            s_wasEnabled = true;
            AcLogf("=== anticheat log (rework / false-flag debug) ===");
            AcLogf("enabled checks: AutoBlock=%d NoSlow=%d Keep-y=%d LegitScaff=%d FlatScaff=%d BHop=%d KillAura=%d ignoreTeammates=%d flagSound=%d",
                   ac_autoblock ? 1 : 0, ac_noslow ? 1 : 0, ac_scaffold ? 1 : 0,
                   ac_legitscaff ? 1 : 0, ac_scaffoldb ? 1 : 0,
                   ac_bhop ? 1 : 0, ac_killaura ? 1 : 0,
                   ac_ignoreTeammates ? 1 : 0, ac_flagsound ? 1 : 0);
            AcLogf("thresholds: AutoBlock vl>=5 minBlockMs=%llu | NoSlow vl>=3 stopMs>700 | Keep-y vl>=%d pitch>=%.0f placed>=%d | LegitScaff vl>=%d pitch>=%.0f speed>%.3f sneakDist=[%.1f,%.1f] | FlatScaff vl>=%d pitch>=60 speed>%.3f",
                   (unsigned long long)kAutoBlockMinBlockMs, kScaffoldAFlagVL, (double)kScaffoldAPitchThreshold,
                   kScaffoldAFlatBlocks, kLegitScaffBFlagVL, (double)kLegitScaffPitchThreshold,
                   (double)kLegitScaffMinSpeed, (double)kLegitScaffBMinSneakDist, (double)kLegitScaffBMaxSneakDist,
                   kScaffoldBFlagVL, (double)kScaffoldBMinSpeed);
        }
    }

    static jfieldID s_fidWorld = nullptr;
    static jfieldID s_fidPlayers = nullptr;
    static jfieldID s_fidSelf = nullptr;
    static jfieldID s_fidEntityId = nullptr;
    if (!s_fidWorld) {
        static const char* kMcWorldSigs[] = {
            "Lnet/minecraft/client/multiplayer/WorldClient;",
            "Lnet/minecraft/world/World;",
        };
        s_fidWorld = JniResolveFieldIDFlex(env, mcClass, "theWorld", "f", kMcWorldSigs, 2);
    }
    if (!s_fidWorld) return;
    jobject world = env->GetObjectField(mcObj, s_fidWorld);
    if (!world) return;
    if (!s_fidPlayers) {
        jclass worldClass = env->GetObjectClass(world);
        s_fidPlayers = JniResolveFieldID(env, worldClass, "Ljava/util/List;", "playerEntities", "j");
        if (!s_fidPlayers) {
            jclass worldBase = McFindClass(env, "net/minecraft/world/World");
            if (worldBase) {
                s_fidPlayers = JniResolveFieldID(env, worldBase, "Ljava/util/List;", "playerEntities", "j");
                env->DeleteLocalRef(worldBase);
            }
        }
        env->DeleteLocalRef(worldClass);
    }
    if (!s_fidPlayers) {
        static bool s_loggedPlayersMiss = false;
        if (!s_loggedPlayersMiss) {
            s_loggedPlayersMiss = true;
            AcJsonLog("{\"event\":\"ac_players_miss\"}");
        }
        env->DeleteLocalRef(world);
        return;
    }
    jobject playerList = env->GetObjectField(world, s_fidPlayers);
    env->DeleteLocalRef(world);
    if (!playerList) return;

    if (!s_fidEntityId) {
        jclass entityBaseClass = McFindClass(env, "net/minecraft/entity/Entity");
        if (entityBaseClass) {
            s_fidEntityId = JniResolveFieldID(env, entityBaseClass, "I", "entityId", "c");
            env->DeleteLocalRef(entityBaseClass);
        }
    }

    int selfId = -1;
    std::string selfTeamColor;
    double selfPosX = 0.0, selfPosY = 0.0, selfPosZ = 0.0;
    float selfYaw = 0.0f;
    {
        if (!s_fidSelf) {
            static const char* kSelfSigs[] = {
                "Lnet/minecraft/client/entity/EntityPlayerSP;",
                "Lnet/minecraft/entity/player/EntityPlayer;",
            };
            s_fidSelf = JniResolveFieldIDFlex(env, mcClass, "thePlayer", "h", kSelfSigs, 2);
        }
        if (s_fidSelf) {
            jobject selfPlayer = env->GetObjectField(mcObj, s_fidSelf);
            if (selfPlayer) {
                jclass selfClass = env->GetObjectClass(selfPlayer);
                if (s_fidEntityId) selfId = env->GetIntField(selfPlayer, s_fidEntityId);
                else env->ExceptionClear();
                selfTeamColor = GetEntityTeamColorName(env, mcObj, mcClass, selfPlayer, selfClass);
                jfieldID selfYawFid = env->GetFieldID(selfClass, "rotationYaw", "F");
                if (selfYawFid) selfYaw = env->GetFloatField(selfPlayer, selfYawFid);
                else env->ExceptionClear();
                jfieldID selfPosXFid = env->GetFieldID(selfClass, "posX", "D");
                jfieldID selfPosYFid = env->GetFieldID(selfClass, "posY", "D");
                jfieldID selfPosZFid = env->GetFieldID(selfClass, "posZ", "D");
                if (selfPosXFid) selfPosX = env->GetDoubleField(selfPlayer, selfPosXFid); else env->ExceptionClear();
                if (selfPosYFid) selfPosY = env->GetDoubleField(selfPlayer, selfPosYFid); else env->ExceptionClear();
                if (selfPosZFid) selfPosZ = env->GetDoubleField(selfPlayer, selfPosZFid); else env->ExceptionClear();
                if (selfId != -1) {
                    std::string selfUuid = GetEntityUUIDString(env, selfPlayer, selfClass);
                    std::string selfName = GetEntityPlayerName(env, mcObj, mcClass, selfPlayer, selfClass);
                    if (!selfUuid.empty()) {
                        g_entityIdToUUID[selfId] = selfUuid;
                        g_entityIdToName[selfId] = selfName;
                        g_uuidToName[selfUuid]   = selfName;
                        g_localPlayerIdLower = LowerAscii(selfUuid);
                    }
                    if (!selfName.empty()) {
                        g_localPlayerNameLower = LowerAscii(Trim(StripMinecraftFormatting(selfName)));
                    }
                }
                env->DeleteLocalRef(selfClass); env->DeleteLocalRef(selfPlayer);
            }
        } else { env->ExceptionClear(); }
    }

    jclass listClass = env->GetObjectClass(playerList);
    jmethodID midSize = env->GetMethodID(listClass, "size", "()I");
    jmethodID midGet  = env->GetMethodID(listClass, "get",  "(I)Ljava/lang/Object;");
    env->DeleteLocalRef(listClass);
    if (!midSize || !midGet) { env->ExceptionClear(); env->DeleteLocalRef(playerList); return; }

    int count = env->CallIntMethod(playerList, midSize);
    ULONGLONG now = GetTickCount64();
    static const ULONGLONG FLAG_COOLDOWN = 5000;

    RefreshUUIDNameMapFromNetHandler(env, mcObj, mcClass);

    for (int i = 0; i < count; i++) {
        jobject entity = env->CallObjectMethod(playerList, midGet, i);
        if (!entity) continue;
        jclass entityClass = env->GetObjectClass(entity);

        int eid = -1;
        if (s_fidEntityId) eid = env->GetIntField(entity, s_fidEntityId);
        else {
            jfieldID fidEid = env->GetFieldID(entityClass, "entityId", "I");
            if (fidEid) eid = env->GetIntField(entity, fidEid); else env->ExceptionClear();
        }
        if (eid == selfId || eid == -1) { env->DeleteLocalRef(entityClass); env->DeleteLocalRef(entity); continue; }

        ACEntityData& d = g_acData[eid];

        std::string liveUUID = GetEntityUUIDString(env, entity, entityClass);
        if (!liveUUID.empty() && d.cachedUUID != liveUUID) {
            d.cachedUUID = liveUUID;
            g_entityIdToUUID[eid] = liveUUID;
            d.teamColorResolved   = false;
            d.teamColorMissLogged = false;
            d.teamColorHitLogged  = false;
        }

        if (d.cachedName.empty()) {
            std::string rawName = GetEntityPlayerName(env, mcObj, mcClass, entity, entityClass);
            d.cachedName = StripMinecraftFormatting(rawName);
        }

        if (!d.cachedUUID.empty()) {
            auto itNm = g_uuidToName.find(d.cachedUUID);
            if (itNm != g_uuidToName.end() && !itNm->second.empty())
                d.cachedName = StripMinecraftFormatting(itNm->second);
            g_entityIdToUUID[eid]          = d.cachedUUID;
            g_uuidToName[d.cachedUUID]     = d.cachedName;
        }
        g_entityIdToName[eid] = d.cachedName;
        d.lastLoggedName = d.cachedName;
        d.lastLoggedUUID = d.cachedUUID;

        const std::string& eName = d.cachedName;

        if (!d.teamColorResolved) {
            d.cachedTeamColor = GetEntityTeamColorName(env, mcObj, mcClass, entity, entityClass);
            if (!d.cachedTeamColor.empty()) {
                d.teamColorResolved  = true;
                d.teamColorHitLogged = true;
            } else if (!d.teamColorMissLogged) {
                d.teamColorMissLogged = true;
            }
        }

        auto flagPlayer = [&](const std::string& checkName, const std::string& detail = "") {
            if (ac_ignoreTeammates && !g_allowNickV2Display) {
                if (!d.cachedUUID.empty() && !g_localPlayerIdLower.empty() &&
                    LowerAscii(d.cachedUUID) == g_localPlayerIdLower) {
                    AcLogf("skip self check=%s player=%s", checkName.c_str(), eName.c_str());
                    return;
                }
                if (!selfTeamColor.empty() && !d.cachedTeamColor.empty() &&
                    d.cachedTeamColor == selfTeamColor) {
                    AcLogf("skip teammate check=%s player=%s team=%s",
                           checkName.c_str(), eName.c_str(), selfTeamColor.c_str());
                    return;
                }
                if (!g_localPlayerNameLower.empty()) {
                    const std::string nameKey = LowerAscii(Trim(StripMinecraftFormatting(eName)));
                    if (!nameKey.empty() && nameKey == g_localPlayerNameLower) {
                        AcLogf("skip self-name check=%s player=%s", checkName.c_str(), eName.c_str());
                        return;
                    }
                }
            }
            const char* checkColor = "red";
            if      (checkName == "LegitScaff") checkColor = "yellow";
            else if (checkName == "Keep-y" || checkName == "ScaffoldA")  checkColor = "light_purple";
            else if (checkName == "NoSlow")    checkColor = "dark_purple";
            else if (checkName == "AutoBlock") checkColor = "dark_red";

            std::string cleanName;
            for (std::string::size_type ci = 0; ci < eName.size(); ) {
                if ((unsigned char)eName[ci] == 0xC2 && ci+1 < eName.size() && (unsigned char)eName[ci+1] == 0xA7) {
                    ci += (ci+2 < eName.size()) ? 3 : 2;
                } else { cleanName += eName[ci++]; }
            }
            std::string nameJson = cleanName;
            for (std::string::size_type np = 0; (np = nameJson.find('"', np)) != std::string::npos; np += 2)
                nameJson.replace(np, 1, "\\\"");
            const std::string nameColor = ResolveAcFlagPlayerColorName(env, mcObj, mcClass, entity, entityClass, d);
            char nameMcCode = TeamColorNameToMcCode(nameColor);
            if (!nameMcCode || nameMcCode == 'f') {
                const int rgb = GetEntityTeamRgb(env, mcObj, mcClass, entity, entityClass);
                if (rgb > 0 && rgb != 0xFFFFFF) nameMcCode = McColorCodeFromRgb(rgb);
                else nameMcCode = 'f';
            }

            std::string json =
                "{\"text\":\"\"," 
                "\"clickEvent\":{\"action\":\"run_command\",\"value\":\"/wdr " + nameJson + "\"},"
                "\"hoverEvent\":{\"action\":\"show_text\",\"value\":{\"text\":\"/wdr " + nameJson + "\"}},"
                "\"extra\":["
                "{\"text\":\"[\",\"color\":\"gray\"},"
                "{\"text\":\"Fre\",\"color\":\"dark_blue\"},"
                "{\"text\":\"nch\",\"color\":\"white\"},"
                "{\"text\":\"ify\",\"color\":\"red\"},"
                "{\"text\":\"] \",\"color\":\"gray\"},"
                "{\"text\":\"" + nameJson + "\",\"color\":\"" + nameColor + "\"},"
                "{\"text\":\" flagged\",\"color\":\"gray\"},"
                "{\"text\":\" " + checkName + "\",\"color\":\"" + checkColor + "\"}"
                "]}";
            if (ac_flagsound) PlayAlertSound(env, mcObj, mcClass);
            auto applyFlagCount = [&](PlayerAcFlagCounts& counts) {
                if      (checkName == "Keep-y" || checkName == "ScaffoldA") counts.scaffoldA++;
                else if (checkName == "NoSlow")    counts.noSlow++;
                else if (checkName == "AutoBlock") counts.autoBlock++;
                else if (checkName == "LegitScaff") counts.legitScaff++;
                else if (checkName == "FlatScaff" || checkName == "ScaffoldB")  counts.scaffoldB++;
                else if (checkName == "BHop") counts.bhop++;
                else if (checkName == "KillAura") counts.killAura++;
            };
            {
                std::lock_guard<std::mutex> lock(g_flaggedPlayersMutex);
                if (!d.cachedUUID.empty()) {
                    PlayerAcFlagCounts& counts = g_flaggedPlayerCountsByUuid[d.cachedUUID];
                    applyFlagCount(counts);
                    if (AcWarningThresholdMet(counts)) {
                        g_flaggedPlayersLastCheckByUuid[d.cachedUUID] = checkName;
                    }
                }
                if (g_allowNickV2Display && !cleanName.empty()) {
                    const std::string nameKey = LowerAscii(Trim(cleanName));
                    if (!nameKey.empty()) {
                        PlayerAcFlagCounts& counts = g_flaggedPlayerCountsByNameLower[nameKey];
                        applyFlagCount(counts);
                        if (AcWarningThresholdMet(counts)) {
                            g_flaggedPlayersLastCheckByNameLower[nameKey] = checkName;
                        }
                    }
                }
            }
            ULONGLONG& lastChatAt = d.lastFlagTime[checkName];
            const bool chatAllowed = (now - lastChatAt) >= FLAG_COOLDOWN;
            PlayerAcFlagCounts postCounts{};
            bool tabWarn = false;
            {
                std::lock_guard<std::mutex> lock(g_flaggedPlayersMutex);
                if (!d.cachedUUID.empty()) {
                    const auto it = g_flaggedPlayerCountsByUuid.find(d.cachedUUID);
                    if (it != g_flaggedPlayerCountsByUuid.end()) {
                        postCounts = it->second;
                        tabWarn = AcWarningThresholdMet(it->second);
                    }
                }
            }
            AcLogf("FLAG check=%s player=%s uuid=%s eid=%d chat=%s tabWarn=%d counts=[KeepY:%d NoSlow:%d AutoBlock:%d LegitScaff:%d FlatScaff:%d] red=%s mode=%s lastPitch=%.1f%s%s",
                   checkName.c_str(), cleanName.c_str(),
                   d.cachedUUID.empty() ? "-" : d.cachedUUID.c_str(), eid,
                   chatAllowed ? "yes" : "cooldown", tabWarn ? 1 : 0,
                   postCounts.scaffoldA, postCounts.noSlow, postCounts.autoBlock,
                   postCounts.legitScaff, postCounts.scaffoldB,
                   g_redState.c_str(), g_modeState.c_str(), (double)d.lastPitch,
                   detail.empty() ? "" : " | ",
                   detail.empty() ? "" : detail.c_str());
            if (chatAllowed) {
                if (g_allowNickV2Display) {
                    PrintLocalChat(env, mcObj, mcClass, BuildFlagLocalChatMessage(checkName, cleanName, nameMcCode));
                } else if (!PrintJsonChat(env, mcObj, mcClass, json)) {
                    PrintLocalChat(env, mcObj, mcClass, BuildFlagLocalChatMessage(checkName, cleanName, nameMcCode));
                }
                lastChatAt = now;
            }
        };

        static jfieldID s_fidPitch = nullptr;
        static jfieldID s_fidPosX  = nullptr;
        static jfieldID s_fidPosY  = nullptr;
        static jfieldID s_fidPosZ  = nullptr;
        if (!s_fidPosX) {
            jclass baseEntityClass = McFindClass(env, "net/minecraft/entity/Entity");
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (baseEntityClass) {
                s_fidPitch = JniResolveFieldID(env, baseEntityClass, "F", "rotationPitch", "z");
                s_fidPosX  = JniResolveFieldID(env, baseEntityClass, "D", "posX", "s");
                s_fidPosY  = JniResolveFieldID(env, baseEntityClass, "D", "posY", "t");
                s_fidPosZ  = JniResolveFieldID(env, baseEntityClass, "D", "posZ", "u");
                env->DeleteLocalRef(baseEntityClass);
            }
        }

        float pitch = 0.0f;
        if (s_fidPitch) pitch = env->GetFloatField(entity, s_fidPitch);
        double posX = 0.0, posY = 0.0, posZ = 0.0;
        if (s_fidPosX) posX = env->GetDoubleField(entity, s_fidPosX);
        if (s_fidPosY) posY = env->GetDoubleField(entity, s_fidPosY);
        if (s_fidPosZ) posZ = env->GetDoubleField(entity, s_fidPosZ);

        jmethodID midSneak  = env->GetMethodID(entityClass, "isSneaking", "()Z");
        jmethodID midSprint = env->GetMethodID(entityClass, "isSprinting", "()Z");
        bool isSneaking  = midSneak  ? (bool)env->CallBooleanMethod(entity, midSneak)  : false;
        bool isSprinting = midSprint ? (bool)env->CallBooleanMethod(entity, midSprint) : false;
        if (!midSneak)  env->ExceptionClear();
        if (!midSprint) env->ExceptionClear();
        if (env->ExceptionCheck()) env->ExceptionClear();

        jmethodID midBlock = JniResolveInstanceMethod(env, entityClass, "()Z", "isBlocking", "bW");
        jmethodID midUsing = JniResolveInstanceMethod(env, entityClass, "()Z", "isUsingItem", "bS");
        bool isBlocking  = midBlock ? (bool)env->CallBooleanMethod(entity, midBlock) : false;
        bool isUsingItem = midUsing ? (bool)env->CallBooleanMethod(entity, midUsing) : false;
        if (!midBlock) env->ExceptionClear();
        if (!midUsing) env->ExceptionClear();
        if (env->ExceptionCheck()) env->ExceptionClear();

        bool isSwinging    = false;
        int  swingProgress = 0;
        {
            static jfieldID s_fidSwinging = nullptr;
            static jfieldID s_fidSwingProgress = nullptr;
            if (!s_fidSwinging || !s_fidSwingProgress) {
                jclass livingClass = McFindClass(env, "net/minecraft/entity/EntityLivingBase");
                if (livingClass) {
                    s_fidSwinging = JniResolveFieldID(env, livingClass, "Z", "isSwingInProgress", "ar");
                    s_fidSwingProgress = JniResolveFieldID(env, livingClass, "I", "swingProgressInt", "as");
                    env->DeleteLocalRef(livingClass);
                }
            }
            if (s_fidSwinging) isSwinging = (bool)env->GetBooleanField(entity, s_fidSwinging);
            else env->ExceptionClear();
            if (s_fidSwingProgress) swingProgress = env->GetIntField(entity, s_fidSwingProgress);
            else env->ExceptionClear();
        }

        int heldItemId = GetHeldItemId(env, entity, entityClass);

        bool onGround = false;
        {
            jfieldID fidOnGround = env->GetFieldID(entityClass, "onGround", "Z");
            if (!fidOnGround) fidOnGround = env->GetFieldID(entityClass, "isOnGround", "Z");
            if (fidOnGround) onGround = env->GetBooleanField(entity, fidOnGround);
            else env->ExceptionClear();
        }

        if (ac_bhop && d.posInitialized) {
            const double moveDX = posX - d.lastPosX;
            const double moveDZ = posZ - d.lastPosZ;
            const float moveDist = std::sqrt((float)(moveDX * moveDX + moveDZ * moveDZ));
            const bool jumpStarted = d.lastOnGround && !onGround && (posY > d.lastPosY + 0.15f);
            const bool movingFastEnough = moveDist > 0.18f;
            const bool fastCycle = d.lastJumpAt != 0 && (now - d.lastJumpAt) < 350ULL;
            if (jumpStarted && movingFastEnough && fastCycle) {
                d.bhopVL++;
                char detail[256];
                std::snprintf(detail, sizeof(detail),
                    "dist=%.2f lastJumpGapMs=%llu yDelta=%.2f lastGround=%d onGround=%d",
                    (double)moveDist, (unsigned long long)(now - d.lastJumpAt), (double)(posY - d.lastPosY),
                    d.lastOnGround ? 1 : 0, onGround ? 1 : 0);
                AcLogf("bhop vl++ player=%s %s", eName.c_str(), detail);
                if (d.bhopVL >= 3) {
                    flagPlayer("BHop", detail);
                    d.bhopVL = 0;
                }
            } else if (d.bhopVL > 0 && (!movingFastEnough || (now - d.lastJumpAt) > 600ULL)) {
                d.bhopVL = std::max(0, d.bhopVL - 1);
            }
            if (jumpStarted) d.lastJumpAt = now;
            d.lastOnGround = onGround;
        } else {
            d.lastOnGround = onGround;
        }

        if (ac_killaura && selfId != -1 && isSwinging && swingProgress > 0 && swingProgress != d.lastSwingProgress) {
            const double dx = posX - selfPosX;
            const double dy = posY - selfPosY;
            const double dz = posZ - selfPosZ;
            const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist > 0.0 && dist <= 3.2) {
                const float targetYaw = (float)(std::atan2(dx, dz) * 57.2957795f);
                const float yawDelta = std::fabs(std::fmod(std::fabs(selfYaw - targetYaw) + 180.0f, 360.0f) - 180.0f);
                const bool rapidSwing = (now - d.lastAttackAt) < 260ULL;
                if (rapidSwing && yawDelta <= 24.0f) {
                    d.killauraVL++;
                    char detail[256];
                    std::snprintf(detail, sizeof(detail),
                        "dist=%.2f yawDelta=%.1f attackGapMs=%llu swing=%d selfYaw=%.1f targetYaw=%.1f",
                        dist, (double)yawDelta, (unsigned long long)(now - d.lastAttackAt),
                        swingProgress, (double)selfYaw, (double)targetYaw);
                    AcLogf("killaura vl++ player=%s %s", eName.c_str(), detail);
                    if (d.killauraVL >= 3) {
                        flagPlayer("KillAura", detail);
                        d.killauraVL = 0;
                    }
                } else {
                    d.killauraVL = std::max(0, d.killauraVL - 1);
                }
                d.lastAttackAt = now;
            } else {
                d.killauraVL = std::max(0, d.killauraVL - 1);
            }
        }

        // AutoBlock
        if (ac_autoblock) {
            const bool swordHeld = IsSwordId(heldItemId);
            if (isBlocking && swordHeld) {
                if (!d.wasBlocking) d.blockStartTime = now;
                d.wasBlocking = true;
                const ULONGLONG blockMs = (d.blockStartTime > 0) ? (now - d.blockStartTime) : 0ULL;
                if (isSwinging && blockMs > kAutoBlockMinBlockMs) {
                    d.autoblockVL++;
                    char detail[384];
                    std::snprintf(detail, sizeof(detail),
                        "blocking=1 sword=1 item=%d blockMs=%llu minBlockMs=%llu swing=1 swingProg=%d vl=%d thresh=5 sprint=%d sneak=%d pitch=%.1f",
                        heldItemId, (unsigned long long)blockMs, (unsigned long long)kAutoBlockMinBlockMs,
                        swingProgress, d.autoblockVL,
                        isSprinting ? 1 : 0, isSneaking ? 1 : 0, (double)pitch);
                    AcLogf("autoblock vl++ player=%s %s", eName.c_str(), detail);
                    if (d.autoblockVL >= 5) {
                        flagPlayer("AutoBlock", detail);
                        d.autoblockVL = 0;
                    }
                }
            } else {
                if (d.wasBlocking || d.autoblockVL > 0) {
                    AcLogf("autoblock reset player=%s blocking=%d sword=%d item=%d prevVl=%d",
                           eName.c_str(), isBlocking ? 1 : 0, swordHeld ? 1 : 0, heldItemId, d.autoblockVL);
                }
                d.wasBlocking = false;
                if (d.autoblockVL > 0) d.autoblockVL--;
            }
        }

        // NoSlow
        if (ac_noslow) {
            static jfieldID s_fidRiding = nullptr;
            if (!s_fidRiding) {
                jclass baseEntityClass = McFindClass(env, "net/minecraft/entity/Entity");
                if (baseEntityClass) {
                    s_fidRiding = JniResolveFieldID(env, baseEntityClass,
                        "Lnet/minecraft/entity/Entity;", "ridingEntity", "m");
                    if (!s_fidRiding) {
                        s_fidRiding = JniResolveFieldID(env, baseEntityClass, "Lnet/minecraft/entity/Entity;", nullptr, "m");
                    }
                    if (!s_fidRiding) {
                        s_fidRiding = JniResolveFieldID(env, baseEntityClass, "Lnet/minecraft/entity/Entity;", nullptr, "ae");
                    }
                    if (env->ExceptionCheck()) { env->ExceptionClear(); s_fidRiding = nullptr; }
                    env->DeleteLocalRef(baseEntityClass);
                }
            }
            bool isRiding = false;
            if (s_fidRiding) {
                jobject ridingEntity = env->GetObjectField(entity, s_fidRiding);
                isRiding = (ridingEntity != nullptr);
                if (ridingEntity) env->DeleteLocalRef(ridingEntity);
            }

            bool isSameItem = (d.lastStopUsingItem == -1) || (heldItemId == d.lastStopUsingItem);
            const ULONGLONG msSinceStop = (d.lastStopUsingTime > 0) ? (now - d.lastStopUsingTime) : 0ULL;
            const bool isSword = IsSwordId(heldItemId);
            const bool isConsumable = IsConsumableId(heldItemId);

            bool shouldFlagNS =
                isSprinting && isUsingItem && !isRiding &&
                (isConsumable || isSword) &&
                (!isSameItem || msSinceStop > 700);

            if (shouldFlagNS) {
                d.lastNoSlowTrigger = now;
                d.noSlowVL++;
                char detail[384];
                std::snprintf(detail, sizeof(detail),
                    "sprint=1 using=1 riding=0 item=%d sword=%d consumable=%d sameItem=%d lastStopItem=%d msSinceStop=%llu minMs=700 vl=%d thresh=3 sneak=%d pitch=%.1f block=%d",
                    heldItemId, isSword ? 1 : 0, isConsumable ? 1 : 0, isSameItem ? 1 : 0,
                    d.lastStopUsingItem, (unsigned long long)msSinceStop, d.noSlowVL,
                    isSneaking ? 1 : 0, (double)pitch, isBlocking ? 1 : 0);
                AcLogf("noslow vl++ player=%s %s", eName.c_str(), detail);
                if (d.noSlowVL >= 3) {
                    flagPlayer("NoSlow", detail);
                    d.noSlowVL = 0;
                }
            } else {
                if (!isUsingItem) { d.lastStopUsingItem = heldItemId; d.lastStopUsingTime = now; }
                if ((now - d.lastNoSlowTrigger) >= 1000 && d.noSlowVL > 0) {
                    d.noSlowVL = max(0, d.noSlowVL - 1);
                    AcLogf("noslow vl-- player=%s vl=%d sprint=%d using=%d riding=%d item=%d",
                           eName.c_str(), d.noSlowVL, isSprinting ? 1 : 0, isUsingItem ? 1 : 0,
                           isRiding ? 1 : 0, heldItemId);
                    d.lastNoSlowTrigger = now;
                }
            }
        }

        bool scaffoldHitDetected = false;
        if ((ac_scaffold || ac_scaffoldb) && d.posInitialized) {
            static jfieldID s_hitHurtTime = nullptr;
            static jfieldID s_hitHurtResistant = nullptr;
            static jfieldID s_hitRecentlyHit = nullptr;
            static jmethodID s_hitGetHealth = nullptr;
            if (!s_hitHurtTime) {
                jclass livingClass = McFindClass(env, "net/minecraft/entity/EntityLivingBase");
                if (livingClass) {
                    s_hitHurtTime = env->GetFieldID(livingClass, "hurtTime", "I");
                    if (env->ExceptionCheck()) { env->ExceptionClear(); s_hitHurtTime = nullptr; }
                    s_hitRecentlyHit = env->GetFieldID(livingClass, "recentlyHit", "I");
                    if (env->ExceptionCheck()) { env->ExceptionClear(); s_hitRecentlyHit = nullptr; }
                    s_hitGetHealth = env->GetMethodID(livingClass, "getHealth", "()F");
                    if (env->ExceptionCheck()) { env->ExceptionClear(); s_hitGetHealth = nullptr; }
                    env->DeleteLocalRef(livingClass);
                } else {
                    env->ExceptionClear();
                }
                jclass entityBaseClass = McFindClass(env, "net/minecraft/entity/Entity");
                if (entityBaseClass) {
                    s_hitHurtResistant = env->GetFieldID(entityBaseClass, "hurtResistantTime", "I");
                    if (env->ExceptionCheck()) { env->ExceptionClear(); s_hitHurtResistant = nullptr; }
                    env->DeleteLocalRef(entityBaseClass);
                } else {
                    env->ExceptionClear();
                }
            }

            int hurtTime = 0;
            int hurtResistant = 0;
            int recentlyHit = 0;
            float health = d.lastLegitScaffHealth;
            if (s_hitHurtTime) hurtTime = env->GetIntField(entity, s_hitHurtTime);
            else env->ExceptionClear();
            if (s_hitHurtResistant) hurtResistant = env->GetIntField(entity, s_hitHurtResistant);
            else env->ExceptionClear();
            if (s_hitRecentlyHit) recentlyHit = env->GetIntField(entity, s_hitRecentlyHit);
            else env->ExceptionClear();
            if (s_hitGetHealth) {
                health = env->CallFloatMethod(entity, s_hitGetHealth);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    health = d.lastLegitScaffHealth;
                }
            }

            const bool hurtTimeUp = hurtTime > d.lastLegitScaffHurtTime;
            const bool hurtResUp = hurtResistant > d.lastLegitScaffHurtResistant;
            const bool recentlyHitUp = recentlyHit > d.lastLegitScaffRecentlyHit;
            const bool healthDrop =
                d.lastLegitScaffHealth >= 0.0f && health >= 0.0f &&
                health < (d.lastLegitScaffHealth - 0.01f);
            const bool hitFlash = hurtTime > 0 || hurtResistant > 0 || recentlyHit > 0;
            scaffoldHitDetected = hitFlash || hurtTimeUp || hurtResUp || recentlyHitUp || healthDrop;

            d.lastLegitScaffHurtTime = hurtTime;
            d.lastLegitScaffHurtResistant = hurtResistant;
            d.lastLegitScaffRecentlyHit = recentlyHit;
            if (health >= 0.0f) d.lastLegitScaffHealth = health;
            if (scaffoldHitDetected) d.lastLegitScaffHitAt = now;
        }

        // Keep-y (ScaffoldA) — jump, place 4 flat blocks under while looking down, max +1 block height.
        if (ac_scaffold && d.posInitialized) {
            const float dY = (float)(posY - d.lastPosY);
            const float dXabs = std::abs((float)(posX - d.lastPosX));
            const float dZabs = std::abs((float)(posZ - d.lastPosZ));
            const float horiz = dXabs + dZabs;
            const bool swingEdge = isSwinging && swingProgress > 0 && swingProgress != d.lastSwingProgress;
            const bool lookingDown = pitch >= kScaffoldAPitchThreshold;
            const bool blockHeld = IsBlockId(heldItemId);
            const bool isJumping = dY > kScaffoldAJumpDY;

            if (d.scaffoldVL > 0 && d.lastScaffoldVLInc > 0 &&
                (now - d.lastScaffoldVLInc) > kScaffoldAInactivityResetMs) {
                d.scaffoldVL = 0;
            }

            auto resetScaffoldASession = [&]() {
                d.scaffoldASessionActive = false;
                d.scaffoldAPlacedCount = 0;
                d.scaffoldASessionHoriz = 0.0f;
                d.scaffoldASessionMaxY = 0.0f;
                d.scaffoldASessionPlaceStartY = 0.0f;
                d.scaffoldAFlatSwingCount = 0;
                d.scaffoldASessionStartAt = 0;
            };

            if (scaffoldHitDetected) {
                resetScaffoldASession();
                d.scaffoldVL = 0;
                d.lastScaffoldVLInc = 0;
            }

            if (isJumping && blockHeld && lookingDown && !d.scaffoldASessionActive) {
                if (d.scaffoldALastSessionStartY > 0.0f &&
                    d.scaffoldALastSessionAt > 0 &&
                    (now - d.scaffoldALastSessionAt) <= kScaffoldATowerWindowMs &&
                    (float)posY > d.scaffoldALastSessionStartY + kScaffoldATowerClimbDY) {
                    d.scaffoldATowerStreak++;
                } else {
                    d.scaffoldATowerStreak = 0;
                }

                d.scaffoldASessionActive = true;
                d.scaffoldASessionStartY = (float)posY;
                d.scaffoldASessionMaxY = (float)posY;
                d.scaffoldASessionStartAt = now;
                d.scaffoldALastSessionStartY = (float)posY;
                d.scaffoldALastSessionAt = now;
                d.scaffoldAPlacedCount = 0;
                d.scaffoldASessionHoriz = 0.0f;
                d.scaffoldAFlatSwingCount = 0;
            }

            if (d.scaffoldASessionActive) {
                if ((now - d.scaffoldASessionStartAt) > kScaffoldASessionMs) {
                    resetScaffoldASession();
                } else if ((float)posY > d.scaffoldASessionStartY + kScaffoldAMaxHeightGain) {
                    resetScaffoldASession();
                } else {
                    d.scaffoldASessionHoriz += horiz;

                    if (swingEdge && blockHeld && lookingDown) {
                        if (d.scaffoldAPlacedCount == 0) {
                            d.scaffoldASessionPlaceStartY = (float)posY;
                            d.scaffoldASessionMaxY = (float)posY;
                        } else if ((float)posY > d.scaffoldASessionMaxY) {
                            d.scaffoldASessionMaxY = (float)posY;
                        }

                        if (dY <= 0.05f) d.scaffoldAFlatSwingCount++;
                        d.scaffoldAPlacedCount++;

                        if (d.scaffoldAPlacedCount >= kScaffoldAFlatBlocks) {
                            const float heightGain = (float)posY - d.scaffoldASessionStartY;
                            const float peakGain = d.scaffoldASessionMaxY - d.scaffoldASessionPlaceStartY;
                            const float maxGainAllowed =
                                d.scaffoldASessionHoriz >= kScaffoldAPatternWideHoriz
                                    ? kScaffoldAPatternMaxHeightGainWide
                                    : kScaffoldAPatternMaxHeightGain;
                            const bool flatHeight =
                                heightGain <= maxGainAllowed &&
                                peakGain <= kScaffoldAPatternMaxPeakGain;
                            const bool flatHoriz =
                                d.scaffoldASessionHoriz >= kScaffoldAMinFlatHoriz &&
                                d.scaffoldASessionHoriz <= kScaffoldAMaxFlatHoriz;
                            const bool flatSwings = d.scaffoldAFlatSwingCount >= kScaffoldAMinFlatSwings;
                            const bool notTower =
                                d.scaffoldATowerStreak < kScaffoldATowerStreakMax ||
                                d.scaffoldASessionHoriz >= kScaffoldATowerMaxHoriz;

                            if (flatHeight && flatHoriz && flatSwings && notTower) {
                                d.scaffoldVL++;
                                d.lastScaffoldVLInc = now;
                                char detail[512];
                                std::snprintf(detail, sizeof(detail),
                                    "placed=%d needPlaced=%d flatSwings=%d needSwings=%d horiz=%.2f horizRange=[%.1f,%.1f] height=%.2f peak=%.2f maxGain=%.2f maxPeak=%.2f towerStreak=%d towerMax=%d sessionMs=%llu pitch=%.1f pitchMin=%.1f item=%d block=1 sneak=%d sprint=%d dY=%.3f",
                                    d.scaffoldAPlacedCount, kScaffoldAFlatBlocks,
                                    d.scaffoldAFlatSwingCount, kScaffoldAMinFlatSwings,
                                    (double)d.scaffoldASessionHoriz,
                                    (double)kScaffoldAMinFlatHoriz, (double)kScaffoldAMaxFlatHoriz,
                                    (double)heightGain, (double)peakGain,
                                    (double)maxGainAllowed, (double)kScaffoldAPatternMaxPeakGain,
                                    d.scaffoldATowerStreak, kScaffoldATowerStreakMax,
                                    (unsigned long long)(now - d.scaffoldASessionStartAt),
                                    (double)pitch, (double)kScaffoldAPitchThreshold,
                                    heldItemId, isSneaking ? 1 : 0, isSprinting ? 1 : 0, (double)dY);
                                AcLogf("keepy vl++ player=%s vl=%d thresh=%d %s",
                                       eName.c_str(), d.scaffoldVL, kScaffoldAFlagVL, detail);
                                if (d.scaffoldVL >= kScaffoldAFlagVL) {
                                    flagPlayer("Keep-y", detail);
                                    d.scaffoldVL = 0;
                                }
                            }
                            resetScaffoldASession();
                        }
                    }
                }
            }
        }

        // LegitScaff — intermittent sneak scaffold (every ~2-3 blocks); flag yellow.
        if (ac_legitscaff && d.posInitialized) {
            static jfieldID s_fidHurtTime = nullptr;
            static jfieldID s_fidMaxHurtTime = nullptr;
            static jfieldID s_fidHurtResistant = nullptr;
            static jfieldID s_fidRecentlyHit = nullptr;
            static jfieldID s_fidAttackedYaw = nullptr;
            static jmethodID s_midGetHealth = nullptr;
            static jmethodID s_midGetDataWatcher = nullptr;
            static jmethodID s_midDwGetFloat = nullptr;
            auto resolveIntField = [&](jclass cls, const char* mcp, const char* obf, jfieldID& out) {
                if (out || !cls) return;
                out = JniResolveFieldID(env, cls, "I", mcp, obf);
            };
            auto resolveFloatField = [&](jclass cls, const char* mcp, const char* obf, jfieldID& out) {
                if (out || !cls) return;
                out = JniResolveFieldID(env, cls, "F", mcp, obf);
            };

            jclass livingClass = McFindClass(env, "net/minecraft/entity/EntityLivingBase");
            jclass entityBaseClass = McFindClass(env, "net/minecraft/entity/Entity");
            jclass dwClass = McFindClass(env, "net/minecraft/entity/DataWatcher");
            if (livingClass) {
                resolveIntField(livingClass, "hurtTime", "au", s_fidHurtTime);
                resolveIntField(livingClass, "maxHurtTime", "av", s_fidMaxHurtTime);
                resolveIntField(livingClass, "recentlyHit", "aO", s_fidRecentlyHit);
                resolveFloatField(livingClass, "attackedAtYaw", "aw", s_fidAttackedYaw);
                if (!s_midGetHealth) {
                    s_midGetHealth = env->GetMethodID(livingClass, "getHealth", "()F");
                    if (env->ExceptionCheck()) { env->ExceptionClear(); s_midGetHealth = nullptr; }
                }
                env->DeleteLocalRef(livingClass);
            } else {
                env->ExceptionClear();
            }
            if (entityBaseClass) {
                resolveIntField(entityBaseClass, "hurtResistantTime", "Z", s_fidHurtResistant);
                if (!s_midGetDataWatcher) {
                    s_midGetDataWatcher = env->GetMethodID(entityBaseClass, "getDataWatcher", "()Lnet/minecraft/entity/DataWatcher;");
                    if (env->ExceptionCheck()) { env->ExceptionClear(); s_midGetDataWatcher = nullptr; }
                }
                env->DeleteLocalRef(entityBaseClass);
            } else {
                env->ExceptionClear();
            }
            if (dwClass) {
                if (!s_midDwGetFloat) {
                    s_midDwGetFloat = env->GetMethodID(dwClass, "getWatchableObjectFloat", "(I)F");
                    if (env->ExceptionCheck()) { env->ExceptionClear(); s_midDwGetFloat = nullptr; }
                }
                env->DeleteLocalRef(dwClass);
            } else {
                env->ExceptionClear();
            }
            if (!s_fidHurtTime) resolveIntField(entityClass, "hurtTime", "au", s_fidHurtTime);
            if (!s_fidMaxHurtTime) resolveIntField(entityClass, "maxHurtTime", "av", s_fidMaxHurtTime);
            if (!s_fidRecentlyHit) resolveIntField(entityClass, "recentlyHit", "aO", s_fidRecentlyHit);
            if (!s_fidAttackedYaw) resolveFloatField(entityClass, "attackedAtYaw", "aw", s_fidAttackedYaw);
            if (!s_fidHurtResistant) resolveIntField(entityClass, "hurtResistantTime", "Z", s_fidHurtResistant);
            if (!s_midGetHealth) {
                s_midGetHealth = env->GetMethodID(entityClass, "getHealth", "()F");
                if (env->ExceptionCheck()) { env->ExceptionClear(); s_midGetHealth = nullptr; }
            }
            if (!s_midGetDataWatcher) {
                s_midGetDataWatcher = env->GetMethodID(entityClass, "getDataWatcher", "()Lnet/minecraft/entity/DataWatcher;");
                if (env->ExceptionCheck()) { env->ExceptionClear(); s_midGetDataWatcher = nullptr; }
            }

            int hurtTime = 0;
            int maxHurtTime = 0;
            int hurtResistant = 0;
            int recentlyHit = 0;
            float attackedYaw = d.lastLegitScaffAttackedYaw;
            float health = d.lastLegitScaffHealth;
            if (s_fidHurtTime) hurtTime = env->GetIntField(entity, s_fidHurtTime);
            else env->ExceptionClear();
            if (s_fidMaxHurtTime) maxHurtTime = env->GetIntField(entity, s_fidMaxHurtTime);
            else env->ExceptionClear();
            if (s_fidHurtResistant) hurtResistant = env->GetIntField(entity, s_fidHurtResistant);
            else env->ExceptionClear();
            if (s_fidRecentlyHit) recentlyHit = env->GetIntField(entity, s_fidRecentlyHit);
            else env->ExceptionClear();
            if (s_fidAttackedYaw) attackedYaw = env->GetFloatField(entity, s_fidAttackedYaw);
            else env->ExceptionClear();
            if (s_midGetHealth) {
                health = env->CallFloatMethod(entity, s_midGetHealth);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    health = d.lastLegitScaffHealth;
                }
            }
            if (health < 0.0f && s_midGetDataWatcher && s_midDwGetFloat) {
                jobject dw = env->CallObjectMethod(entity, s_midGetDataWatcher);
                if (!env->ExceptionCheck() && dw) {
                    const float dwHealth = env->CallFloatMethod(dw, s_midDwGetFloat, 6);
                    if (!env->ExceptionCheck() && dwHealth >= 0.0f) health = dwHealth;
                    else env->ExceptionClear();
                    env->DeleteLocalRef(dw);
                } else {
                    env->ExceptionClear();
                }
            }

            const bool hurtTimeUp = hurtTime > d.lastLegitScaffHurtTime;
            const bool hurtResUp = hurtResistant > d.lastLegitScaffHurtResistant;
            const bool recentlyHitUp = recentlyHit > d.lastLegitScaffRecentlyHit;
            const bool healthDrop =
                d.lastLegitScaffHealth >= 0.0f && health >= 0.0f &&
                health < (d.lastLegitScaffHealth - 0.01f);
            const bool attackedYawChanged =
                d.lastLegitScaffAttackedYaw > -9000.0f &&
                std::fabs(attackedYaw - d.lastLegitScaffAttackedYaw) > 0.25f;
            const bool hitFlash = hurtTime > 0 || hurtResistant > 0 || recentlyHit > 0;
            const bool hitDetected =
                hitFlash || hurtTimeUp || hurtResUp || recentlyHitUp || healthDrop || attackedYawChanged;
            if (hitDetected) d.lastLegitScaffHitAt = now;

            const ULONGLONG graceLeftMs =
                (d.lastLegitScaffHitAt > 0 && now >= d.lastLegitScaffHitAt)
                    ? ((now - d.lastLegitScaffHitAt) <= kLegitScaffHitGraceMs
                           ? (kLegitScaffHitGraceMs - (now - d.lastLegitScaffHitAt))
                           : 0ULL)
                    : 0ULL;
            const bool inHitGrace = graceLeftMs > 0;

            const float dX = (float)(posX - d.lastPosX);
            const float dZ = (float)(posZ - d.lastPosZ);
            const float speed = std::sqrt(dX * dX + dZ * dZ);
            const bool isLookingDown = d.lastPitch >= kLegitScaffPitchThreshold;
            const bool isBlockItem = IsBlockId(heldItemId);
            const bool isScaffolding = isLookingDown && isBlockItem && speed > kLegitScaffMinSpeed;

            d.lastLegitScaffHurtTime = hurtTime;
            d.lastLegitScaffMaxHurtTime = maxHurtTime;
            d.lastLegitScaffHurtResistant = hurtResistant;
            d.lastLegitScaffRecentlyHit = recentlyHit;
            d.lastLegitScaffAttackedYaw = attackedYaw;
            if (health >= 0.0f) d.lastLegitScaffHealth = health;

            if (inHitGrace) {
                if (hitDetected && d.legitScaffBVL > 0) {
                    AcLogf("legitscaff hit_grace player=%s vl=%d graceLeftMs=%llu speed=%.3f pitch=%.1f sneak=%d item=%d block=%d hurt=%d health=%.1f",
                           eName.c_str(), d.legitScaffBVL, (unsigned long long)graceLeftMs,
                           (double)speed, (double)d.lastPitch, isSneaking ? 1 : 0, heldItemId,
                           isBlockItem ? 1 : 0, hitDetected ? 1 : 0, (double)health);
                }
                d.legitScaffBVL = 0;
                d.legitScaffBSinceSneakDist = 0.0f;
                if (hitDetected) d.lastLegitScaffBSneakAt = 0;
            } else {
                const bool sneakStart = isSneaking && !d.lastSneakLegitScaffB;

                if (isSneaking || sneakStart) {
                    d.lastLegitScaffBSneakAt = now;
                }

                const bool recentlySneaked =
                    d.lastLegitScaffBSneakAt > 0 && (now - d.lastLegitScaffBSneakAt) <= kLegitScaffBRecentSneakMs;
                const ULONGLONG msSinceSneak =
                    (d.lastLegitScaffBSneakAt > 0) ? (now - d.lastLegitScaffBSneakAt) : 0ULL;
                const bool knockbackBridge =
                    isScaffolding && !isSneaking && speed > kLegitScaffKnockbackSpeed;

                if (knockbackBridge || hitDetected) {
                    if (d.legitScaffBVL > 0 || knockbackBridge) {
                        AcLogf("legitscaff hit_reset player=%s vl=%d kbBridge=%d hit=%d speed=%.3f kbSpeedMin=%.3f pitch=%.1f sneak=%d item=%d block=%d sinceSneakDist=%.3f",
                               eName.c_str(), d.legitScaffBVL, knockbackBridge ? 1 : 0, hitDetected ? 1 : 0,
                               (double)speed, (double)kLegitScaffKnockbackSpeed, (double)d.lastPitch,
                               isSneaking ? 1 : 0, heldItemId, isBlockItem ? 1 : 0,
                               (double)d.legitScaffBSinceSneakDist);
                    }
                    if (!hitDetected) d.lastLegitScaffHitAt = now;
                    d.legitScaffBVL = 0;
                    d.legitScaffBSinceSneakDist = 0.0f;
                    d.lastLegitScaffBSneakAt = 0;
                } else if (isScaffolding) {
                    if (sneakStart) {
                        if (d.legitScaffBSinceSneakDist >= kLegitScaffBMinSneakDist &&
                            d.legitScaffBSinceSneakDist <= kLegitScaffBMaxSneakDist) {
                            d.legitScaffBVL += 2;
                            d.lastLegitScaffBInc = now;
                            AcLogf("legitscaff sneak_pattern player=%s vl=%d +2 sinceSneakDist=%.3f distRange=[%.1f,%.1f] pitch=%.1f pitchMin=%.1f speed=%.3f speedMin=%.3f sneak=1 item=%d block=1",
                                   eName.c_str(), d.legitScaffBVL,
                                   (double)d.legitScaffBSinceSneakDist,
                                   (double)kLegitScaffBMinSneakDist, (double)kLegitScaffBMaxSneakDist,
                                   (double)d.lastPitch, (double)kLegitScaffPitchThreshold,
                                   (double)speed, (double)kLegitScaffMinSpeed, heldItemId);
                        }
                        d.legitScaffBSinceSneakDist = 0.0f;
                    } else {
                        d.legitScaffBSinceSneakDist += speed;
                    }

                    if (isSneaking || recentlySneaked) {
                        d.legitScaffBVL++;
                        d.lastLegitScaffBInc = now;
                        char detail[512];
                        std::snprintf(detail, sizeof(detail),
                            "vl=%d thresh=%d sneak=%d recentSneak=%d msSinceSneak=%llu recentSneakMs=%llu pitch=%.1f pitchMin=%.1f speed=%.3f speedMin=%.3f sinceSneakDist=%.3f item=%d block=1 lookingDown=1 scaffolding=1 sprint=%d swing=%d health=%.1f",
                            d.legitScaffBVL, kLegitScaffBFlagVL,
                            isSneaking ? 1 : 0, recentlySneaked ? 1 : 0,
                            (unsigned long long)msSinceSneak, (unsigned long long)kLegitScaffBRecentSneakMs,
                            (double)d.lastPitch, (double)kLegitScaffPitchThreshold,
                            (double)speed, (double)kLegitScaffMinSpeed,
                            (double)d.legitScaffBSinceSneakDist, heldItemId,
                            isSprinting ? 1 : 0, isSwinging ? 1 : 0, (double)health);
                        AcLogf("legitscaff vl++ player=%s %s", eName.c_str(), detail);
                        if (d.legitScaffBVL >= kLegitScaffBFlagVL) {
                            AcLogf("legitscaff FLAG player=%s %s", eName.c_str(), detail);
                            flagPlayer("LegitScaff", detail);
                            d.legitScaffBVL = 0;
                            d.legitScaffBSinceSneakDist = 0.0f;
                        }
                    }
                } else {
                    if (d.legitScaffBVL > 0 && (now - d.lastLegitScaffBInc) > kLegitScaffBDecayMs) {
                        d.legitScaffBVL = max(0, d.legitScaffBVL - 1);
                        AcLogf("legitscaff vl-- player=%s vl=%d pitch=%.1f speed=%.3f sneak=%d item=%d block=%d lookingDown=%d scaffolding=0",
                               eName.c_str(), d.legitScaffBVL, (double)d.lastPitch, (double)speed,
                               isSneaking ? 1 : 0, heldItemId, isBlockItem ? 1 : 0, isLookingDown ? 1 : 0);
                    }
                    if (!recentlySneaked) {
                        d.legitScaffBSinceSneakDist = 0.0f;
                    }
                }
            }

            d.lastSneakLegitScaffB = isSneaking;
        }

        // FlatScaff (ScaffoldB) — no sneak, no jump; flag color red (c).
        if (ac_scaffoldb && d.posInitialized) {
            const float dX = (float)(posX - d.lastPosX);
            const float dY = (float)(posY - d.lastPosY);
            const float dZ = (float)(posZ - d.lastPosZ);
            const float instantHoriz = std::sqrt(dX * dX + dZ * dZ);
            const bool isJumping = dY > 0.08f;
            const bool cPitch = pitch >= 60.0f;
            const bool cBlock = IsBlockId(heldItemId);
            const bool cSpeed = instantHoriz > kScaffoldBMinSpeed;

            if (scaffoldHitDetected) {
                if (d.scaffoldBVL > 0) {
                    AcLogf("flatscaff hit_reset player=%s prevVl=%d pitch=%.1f speed=%.3f",
                           eName.c_str(), d.scaffoldBVL, (double)pitch, (double)instantHoriz);
                }
                d.scaffoldBVL = 0;
                d.lastScaffoldBInc = 0;
            } else if (isSneaking || isJumping) {
                if (d.scaffoldBVL > 0) {
                    AcLogf("flatscaff reset player=%s prevVl=%d sneak=%d jump=%d dY=%.3f",
                           eName.c_str(), d.scaffoldBVL, isSneaking ? 1 : 0, isJumping ? 1 : 0, (double)dY);
                }
                d.scaffoldBVL = 0;
                d.lastScaffoldBInc = 0;
            } else {
                const bool triggers = cPitch && cBlock && cSpeed;
                if (triggers) {
                    d.scaffoldBVL++;
                    d.lastScaffoldBInc = now;
                    char detail[384];
                    std::snprintf(detail, sizeof(detail),
                        "vl=%d thresh=%d pitch=%.1f pitchMin=60 speed=%.3f speedMin=%.3f sneak=0 jump=0 item=%d block=1 hit=0 sprint=%d swing=%d dY=%.3f",
                        d.scaffoldBVL, kScaffoldBFlagVL, (double)pitch,
                        (double)instantHoriz, (double)kScaffoldBMinSpeed, heldItemId,
                        isSprinting ? 1 : 0, isSwinging ? 1 : 0, (double)dY);
                    AcLogf("flatscaff vl++ player=%s %s", eName.c_str(), detail);
                    if (d.scaffoldBVL >= kScaffoldBFlagVL) {
                        flagPlayer("FlatScaff", detail);
                        d.scaffoldBVL = 0;
                    }
                } else if (d.scaffoldBVL > 0 && (now - d.lastScaffoldBInc) > 3000) {
                    d.scaffoldBVL = max(0, d.scaffoldBVL - 1);
                    AcLogf("flatscaff vl-- player=%s vl=%d pitch=%.1f speed=%.3f pitchOk=%d block=%d speedOk=%d",
                           eName.c_str(), d.scaffoldBVL, (double)pitch, (double)instantHoriz,
                           cPitch ? 1 : 0, cBlock ? 1 : 0, cSpeed ? 1 : 0);
                    d.lastScaffoldBInc = now;
                }
            }
        }

        d.lastSwingProgress = swingProgress;
        d.lastPitch = pitch;
        d.posInitialized = true;
        d.lastPosX = (float)posX;
        d.lastPosY = (float)posY;
        d.lastPosZ = (float)posZ;

        env->DeleteLocalRef(entityClass);
        env->DeleteLocalRef(entity);
    }
    env->DeleteLocalRef(playerList);
}

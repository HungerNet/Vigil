#pragma once
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <fstream>
#include <set>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <atomic>
#include <regex>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <jni.h>

#include "../java_src/lunar_headers/FrenchifyGuiLunar.h"

namespace taboverlay {
class OverlayController;
}

#ifndef FRENCHIFY_DEBUG_LOG
#define FRENCHIFY_DEBUG_LOG 0
#endif

// ====== ANTICHEAT STATE ======
struct ACEntityData {
    bool wasBlocking = false;
    ULONGLONG blockStartTime = 0;
    int autoblockVL = 0;
    int lastSwingProgress = 0;
    int noSlowVL = 0;
    int lastStopUsingItem = -1;
    ULONGLONG lastStopUsingTime = 0;
    ULONGLONG lastNoSlowTrigger = 0;
    int scaffoldVL = 0;
    float lastPosY = 0;
    ULONGLONG lastLowMoveTime = 0;
    ULONGLONG lastScaffoldVLInc = 0;
    bool scaffoldASessionActive = false;
    float scaffoldASessionStartY = 0.0f;
    ULONGLONG scaffoldASessionStartAt = 0;
    int scaffoldAPlacedCount = 0;
    float scaffoldASessionHoriz = 0.0f;
    float scaffoldASessionMaxY = 0.0f;
    float scaffoldASessionPlaceStartY = 0.0f;
    int scaffoldAFlatSwingCount = 0;
    float scaffoldALastSessionStartY = 0.0f;
    ULONGLONG scaffoldALastSessionAt = 0;
    int scaffoldATowerStreak = 0;
    ULONGLONG lastScaffoldTraceLog = 0;
    int eagleVL = 0;
    ULONGLONG lastEagleInc = 0;
    int scaffoldBVL = 0;
    ULONGLONG lastScaffoldBInc = 0;
    int legitScaffBVL = 0;
    ULONGLONG lastLegitScaffBInc = 0;
    ULONGLONG lastLegitScaffBSneakAt = 0;
    ULONGLONG lastLegitScaffHitAt = 0;
    int lastLegitScaffHurtTime = 0;
    int lastLegitScaffMaxHurtTime = 0;
    int lastLegitScaffHurtResistant = 0;
    int lastLegitScaffRecentlyHit = 0;
    float lastLegitScaffHealth = -1.0f;
    float lastLegitScaffAttackedYaw = -9999.0f;
    float legitScaffBSinceSneakDist = 0.0f;
    bool lastSneakLegitScaffB = false;
    bool lastSneak = false;
    float lastPitch = 0.0f;
    float eagleAccumDX = 0, eagleAccumDZ = 0;
    ULONGLONG eagleAccumStart = 0;
    float eagleSpeed = 0.0f;
    bool posInitialized = false;
    float lastPosX = 0, lastPosZ = 0;
    std::map<std::string, ULONGLONG> lastFlagTime;
    std::string cachedName;
    std::string cachedUUID;
    std::string lastLoggedName;
    std::string lastLoggedUUID;
    bool teamColorResolved = false;
    bool teamColorMissLogged = false;
    bool teamColorHitLogged = false;
    std::string cachedTeamColor;
    bool nicked = false;
    ULONGLONG fetchedAt = 0;
    int stars = 0;
    int kills = 0;
    int deaths = 1;
    int finalKills = 0;
    int finalDeaths = 1;
    int wins = 0;
    int losses = 1;
    int bedsBroken = 0;
    int bedsLost = 1;
    int swordKills = 0;
    double bblr = 0.0;
    double fkdr = 0.0;
    double kdr = 0.0;
    double swordkr = 0.0;
    std::string renderText;
};

// Cross-module globals (defined in dllmain.cpp)
extern JavaVM*      g_jvmGlobal;
extern jclass       g_mcClassGlobal;
extern jmethodID    g_midGetMinecraftStatic;
extern jclass       g_customGuiClassGlobal;
extern jobject      g_injectClassLoaderGlobal;
extern std::string  g_dllDir;
extern bool         g_allowNickV2Display;
extern std::string  g_atlasReplayViewMode;
extern std::string  g_redState;
extern std::string  g_modeState;
extern std::string  g_tokensState;
extern std::string  g_localPlayerNameLower;
extern std::string  g_localPlayerIdLower;
extern std::string  g_localTeamLetter;
extern bool         g_localTeamColorResolved;
extern std::unordered_set<std::string> g_whoTeammateNamesLower;
extern bool         g_whoTeamResolved;
struct PlayerAcFlagCounts {
    int scaffoldA = 0;
    int noSlow = 0;
    int autoBlock = 0;
    int legitScaff = 0;
    int scaffoldB = 0;
};

inline bool AcWarningThresholdMet(const PlayerAcFlagCounts& c) {
    return c.scaffoldA >= 2 || c.noSlow >= 2 || c.autoBlock >= 3 || c.legitScaff >= 3 || c.scaffoldB >= 3;
}

extern std::mutex   g_flaggedPlayersMutex;
extern std::map<std::string, std::string> g_flaggedPlayersLastCheckByUuid;
extern std::map<std::string, PlayerAcFlagCounts> g_flaggedPlayerCountsByUuid;
extern std::map<std::string, std::string> g_flaggedPlayersLastCheckByNameLower;
extern std::map<std::string, PlayerAcFlagCounts> g_flaggedPlayerCountsByNameLower;

extern taboverlay::OverlayController g_tabOverlay;
extern bool         g_tabHeld;
extern bool         g_useNativeTabPipeline;
extern std::string  g_tokensState;
extern bool         g_tabOverlayGuiEnabled;
extern bool         g_tabOverlayStateAllowed;
extern std::atomic<bool> g_injectReady;

void FrenchifyBootLog(const char* msg);

void PumpMinecraftClientLogic(JNIEnv* env, jobject mcObj, jclass mcClass);
extern std::atomic<ULONGLONG> g_clientPumpHeartbeat;
extern std::atomic<ULONGLONG> g_mainLoopHeartbeat;

#pragma once

#include <jni.h>
#include <string>

struct SessionStatsSettings {
    bool enabled = false;
    bool gameStats = true;
    bool finalKills = true;
    bool beds = true;
    bool kills = true;
    bool wins = true;
    bool winstreak = true;
    bool sessionGames = true;
    bool gameTime = true;
    bool avgGameTime = true;
    bool sessionTime = true;
    bool queueEta = true;
    bool slumberTickets = true;
    bool textShadow = false;
    int bgOpacity = 100;
    int scalePercent = 100;
    int posX = 1000;
    int posY = 1000;
    bool resetPending = false;
};

void SessionStatsSetBedwarsContext(bool active);
void SessionStatsOnChat(JNIEnv* env, jobject mcObj, jclass mcClass, const std::string& msg);
void SessionStatsTick(JNIEnv* env, jobject mcObj, jclass mcClass, jclass guiClass);
bool SessionStatsRender(JNIEnv* env);
bool TryInstallSessionHudHook(JNIEnv* env, jobject mcObj, jobject classLoader);
bool IsSessionHudHookInstalled();
bool SessionStatsGetHudRect(float& startX, float& startY, float& endX, float& endY);
void SessionStatsApplyPosNormalized(int posX, int posY);
bool SessionStatsGetGuiScaledSize(JNIEnv* env, jobject mcObj, int& outW, int& outH);

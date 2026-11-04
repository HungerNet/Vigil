#pragma once

#include <jni.h>
#include <string>

void PartyQueueDetectorReset();
void PartyQueueDetectorSetActive(bool active);
// Sets the max party size for the current queue based on the Hypixel locraw "mode" field
// (e.g. BEDWARS_EIGHT_TWO -> 2, BEDWARS_FOUR_THREE -> 3, BEDWARS_FOUR_FOUR -> 4).
void PartyQueueDetectorSetModeFromLocraw(const std::string& locrawModeRaw);
void PartyQueueDetectorOnSpawnPlayer(int entityId);
void PartyQueueDetectorOnWorldLoad();
void PartyQueueDetectorTick(JNIEnv* env, jobject mcObj, jclass mcClass);
// Debounce before announcing parties / requeue (shared by spawn-id analysis and chat join batching).
unsigned long long PartyQueueDetectorAnnounceDelayMs();
bool TryInstallPartyQueuePacketHook(JNIEnv* env, jobject mcObj, jclass mcClass, jobject classLoader);
bool IsPartyQueuePacketHookInstalled();

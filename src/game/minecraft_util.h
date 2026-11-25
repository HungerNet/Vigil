#pragma once
// Vigil game layer — Minecraft text, team-color and server helpers.

#include <jni.h>

#include <string>

std::string Trim(const std::string& s);
std::string DecodeJsonStringEscapes(const std::string& in);
bool IsValidIgn(const std::string& s);
std::string LowerAscii(std::string s);
std::string FormatRatio(double v);
std::string PadRight(const std::string& s, size_t width);
size_t VisibleTextWidth(const std::string& s);
std::string PadRightVisible(const std::string& s, size_t width);
std::string TruncateVisible(const std::string& s, size_t width);
std::string StripMinecraftFormatting(const std::string& text);
char TeamLetterFromColorCode(char c);
bool IsMinecraftColorCode(char c);
bool IsMinecraftFormatCode(char c);

// Team color resolved from scoreboard teams / tab display names (Hypixel conventions).
std::string GetEntityTeamColorName(JNIEnv* env, jobject mcObj, jclass mcClass, jobject entity, jclass entityClass);
int GetEntityTeamRgb(JNIEnv* env, jobject mcObj, jclass mcClass, jobject entity, jclass entityClass);
struct HitboxTeamColorInfo {
    int rgb = 0xFFFFFF;
    const char* source = "fallback";
    std::string prefix;
    std::string playerName;
};
HitboxTeamColorInfo ResolveEntityTeamColor(JNIEnv* env, jobject mcObj, jclass mcClass, jobject entity, jclass entityClass);
bool ShouldDrawHitboxTeamColor(const HitboxTeamColorInfo& info);
HitboxTeamColorInfo ResolveDragonHitboxTeamColor(JNIEnv* env, jobject mcObj, jclass mcClass, jobject entity, jclass entityClass);
bool ShouldDrawDragonHitboxTeamColor(const HitboxTeamColorInfo& info);
int GetScoreboardTeamRgb(JNIEnv* env, jobject team);
std::string GetScoreboardTeamSuffix(JNIEnv* env, jobject team);
bool SetScoreboardTeamSuffix(JNIEnv* env, jobject team, const std::string& suffix);
std::string GetBedwarsTeamNameFromTabDisplay(const std::string& displayFormatted);
std::string BedwarsTeamNameFromTeamLetter(const std::string& teamLetter);
std::string GetEntityDisplayNameFormatted(JNIEnv* env, jobject entity, jclass entityClass);
int TeamColorNameToRgb(const std::string& colorName);
char TeamColorNameToMcCode(const std::string& colorName);
char McColorCodeFromRgb(int rgb);
std::string TeamRgbToColorName(int rgb);

// Server whitelist: Vigil only observes when connected to *.hypixel.net.
bool HostEndsWithHypixelNet(const std::string& hostOrIp);
bool ReadMinecraftServerHost(JNIEnv* env, jobject mcObj, jclass mcClass, std::string& outHost);
bool VigilIsHypixelServerActive();
void VigilUpdateHypixelServerWhitelist(JNIEnv* env, jobject mcObj, jclass mcClass);

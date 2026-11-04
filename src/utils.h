#pragma once
#include "globals.h"
#include <jni.h>

std::string  Trim(const std::string& s);
std::string  DecodeJsonStringEscapes(const std::string& in);
bool         IsValidIgn(const std::string& s);
std::string  LowerAscii(std::string s);
std::string  FormatRatio(double v);

std::string  PadRight(const std::string& s, size_t width);
size_t       VisibleTextWidth(const std::string& s);
std::string  PadRightVisible(const std::string& s, size_t width);
std::string  TruncateVisible(const std::string& s, size_t width);

std::string  StripMinecraftFormatting(const std::string& text);
char         TeamLetterFromColorCode(char c);
bool         IsMinecraftColorCode(char c);
bool         IsMinecraftFormatCode(char c);

std::string  SectionColor(char code);
char         FkdrColorCode(double v);
char         KdrColorCode(double v);
char         WlrColorCode(double v);
char         DuelKdrColorCode(double v);
char         DuelWinsColorCode(int wins);
char         PingColorCode(int ping);
char         BowDistanceColorCode(double blocks);
std::string  GetPrestigeColorText(int stars);
int          BedwarsExpToStars(int experience);
std::string  GetSkywarsStarText(int stars);
int          SkywarsExpToLevel(int experience);
std::string  FormatSkywarsLevelWithBrackets(int level);
std::string  FormatSkywarsStarsDisplay(int swLevel, int rawAchievementStars);
bool         IsHpChatMessage(const std::string& cleanMsg);
bool         IsBowDistanceChatMessage(const std::string& cleanMsg);
bool         IsIgnoredChatForWhoParty(const std::string& cleanMsg);
bool         IsHypixelLocrawChatLine(const std::string& cleanMsg);
int          GetScoreboardTeamRgb(JNIEnv* env, jobject team);
std::string  GetScoreboardTeamSuffix(JNIEnv* env, jobject team);
bool         SetScoreboardTeamSuffix(JNIEnv* env, jobject team, const std::string& suffix);
void         SyncChatPollAfterRewrite(JNIEnv* env, jobject mcObj, jclass mcClass);
std::string  FormatDuelRatio(double v);
char         BwdScoreColorCode(double score);
char         HypixelRankColorCode(const std::string& newPackageRank,
                                  const std::string& monthlyPackageRank);
std::string  FormatSeraphReportTitleCase(const std::string& reportType);
std::string  JsonEscapeString(const std::string& s);
void         JsonLineLog(const std::string& line);
void         FrenchifyFileLog(const char* tag, const char* msg);
void         AppendFrenchifyNamedLog(const char* filename, const std::string& msg);

std::vector<std::string> ParseOnlineNamesFromMessage(const std::string& cleanMsg);
bool IsWhoTeamsHeaderLine(const std::string& cleanMsg);
bool IsWhoModeHeaderLine(const std::string& cleanMsg);
bool IsWhoFullOnlineLine(const std::string& cleanMsg);

// HTTPS GET via libcurl. extraHeaderLine e.g. "seraph-api-key: xxx"
bool HttpGetHttps(const std::string& url, std::string& body, DWORD& status,
                  const std::string& extraHeaderLine = "");
DWORD HttpGetLastErrorCode();

void RunBowDistanceShotTracker(JNIEnv* env, jobject mcObj, jclass mcClass, bool enabled);
bool TryHandleBowDistanceHpChat(JNIEnv* env, jobject mcObj, jclass mcClass, jobject chatComponent,
                                const std::string& rawMsg, const std::string& cleanMsg);

// Team color from scoreboard (entity team / scoreboard name / tab — Bedwars/Hypixel).
std::string GetEntityTeamColorName(JNIEnv* env, jobject mcObj, jclass mcClass,
                                   jobject entity, jclass entityClass);
int GetEntityTeamRgb(JNIEnv* env, jobject mcObj, jclass mcClass,
                     jobject entity, jclass entityClass);
struct HitboxTeamColorInfo {
    int rgb = 0xFFFFFF;
    const char* source = "fallback";
    std::string prefix;
    std::string playerName;
};
HitboxTeamColorInfo ResolveEntityTeamColor(JNIEnv* env, jobject mcObj, jclass mcClass,
                                           jobject entity, jclass entityClass);
bool ShouldDrawHitboxTeamColor(const HitboxTeamColorInfo& info);
HitboxTeamColorInfo ResolveDragonHitboxTeamColor(JNIEnv* env, jobject mcObj, jclass mcClass,
                                                 jobject entity, jclass entityClass);
bool ShouldDrawDragonHitboxTeamColor(const HitboxTeamColorInfo& info);
std::string GetBedwarsTeamNameFromTabDisplay(const std::string& displayFormatted);
std::string BedwarsTeamNameFromTeamLetter(const std::string& teamLetter);
std::string GetEntityDisplayNameFormatted(JNIEnv* env, jobject entity, jclass entityClass);
int TeamColorNameToRgb(const std::string& colorName);
char TeamColorNameToMcCode(const std::string& colorName);
char McColorCodeFromRgb(int rgb);
std::string TeamRgbToColorName(int rgb);

bool ReadGuiStaticBool(JNIEnv* env, jclass customGuiClass, const char* fieldName, bool fallback);
int ReadGuiStaticInt(JNIEnv* env, jclass customGuiClass, const char* fieldName, int fallback);
std::string ReadGuiStaticString(JNIEnv* env, jclass customGuiClass, const char* fieldName, const std::string& fallback = "");

// Server whitelist: mod features only run when connected host ends with "hypixel.net".
bool HostEndsWithHypixelNet(const std::string& hostOrIp);
bool ReadMinecraftServerHost(JNIEnv* env, jobject mcObj, jclass mcClass, std::string& outHost);
bool FrenchifyIsHypixelServerActive();
void FrenchifyUpdateHypixelServerWhitelist(JNIEnv* env, jobject mcObj, jclass mcClass);

#pragma once
#include "globals.h"

bool        ReadGuiBool(JNIEnv* env, jclass guiClass, const char* name, bool fallback);
std::string ReadGuiString(JNIEnv* env, jclass guiClass, const char* name, const std::string& fallback);

void WriteTogglesToFile(JNIEnv* env, jclass guiClass);

void SetSuppressChatHook(bool suppress);
bool IsChatHookSuppressed();

jmethodID JniResolveInstanceMethod(JNIEnv* env, jclass cls, const char* signature,
                                   const char* mcpName, const char* obfName);
jmethodID JniResolveStaticMethod(JNIEnv* env, jclass cls, const char* signature,
                                 const char* mcpName, const char* obfName);
jfieldID JniResolveFieldID(JNIEnv* env, jclass cls, const char* signature,
                           const char* mcpName, const char* obfName);
jfieldID JniResolveFieldIDFlex(JNIEnv* env, jclass cls,
                               const char* mcpName, const char* obfName,
                               const char* const* signatures, int signatureCount);
void JniLogMiss(const char* kind, const char* owner, const char* name, const char* signature);
jmethodID JniResolveInstanceMethodAny(JNIEnv* env, jclass cls,
                                      const char* const* names, const char* const* signatures,
                                      int candidateCount);

jobject ResolveChatGuiDelegate(JNIEnv* env, jobject chatGui);

bool PrintLocalChat(JNIEnv* env, jobject mcObj, jclass mcClass, const std::string& message);
bool RemoveLatestChatLine(JNIEnv* env, jobject mcObj, jclass mcClass);
void PrintChatComponent(JNIEnv* env, jobject mcObj, jclass mcClass, jobject chatComponent);
bool PrintJsonChat(JNIEnv* env, jobject mcObj, jclass mcClass, const std::string& json);
void PlayAlertSound(JNIEnv* env, jobject mcObj, jclass mcClass);
void PlayBlacklistPlingSound(JNIEnv* env, jobject mcObj, jclass mcClass);

// On-screen toast shown once after injection (works on main menu and in-game).
// Returns true when the toast has fully finished and can stop being rendered.
bool RenderInjectToast(JNIEnv* env);
void ShowFrenchifyToast(const std::string& headline, const std::string& detail);
bool FrenchifyToastIsShowing();

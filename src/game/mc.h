#pragma once
// Vigil game layer — Minecraft JNI helpers: member resolution, chat output, sounds, screens.

#include <jni.h>

#include <string>

jmethodID JniResolveInstanceMethod(JNIEnv* env, jclass cls, const char* signature, const char* mcpName, const char* obfName);
jmethodID JniResolveStaticMethod(JNIEnv* env, jclass cls, const char* signature, const char* mcpName, const char* obfName);
jfieldID JniResolveFieldID(JNIEnv* env, jclass cls, const char* signature, const char* mcpName, const char* obfName);
jfieldID JniResolveFieldIDFlex(JNIEnv* env, jclass cls, const char* mcpName, const char* obfName,
                               const char* const* signatures, int signatureCount);
jmethodID JniResolveInstanceMethodAny(JNIEnv* env, jclass cls, const char* const* names, const char* const* signatures,
                                      int candidateCount);

namespace vigil::game {

// Chat (client-side only; nothing is sent to the server).
bool printChat(JNIEnv* env, jobject mc, const std::string& legacyFormatted);
bool printJsonChat(JNIEnv* env, jobject mc, const std::string& json);
// Sends a chat line/command to the server as the player (used for /wdr on explicit user action).
bool sendChatMessage(JNIEnv* env, jobject mc, const std::string& text);
void addToSentHistory(JNIEnv* env, jobject mc, const std::string& text);

void playSound(JNIEnv* env, jobject mc, const char* soundName, float pitch);

// Screens.
jobject currentScreen(JNIEnv* env, jobject mc);  // local ref or null
bool displayScreen(JNIEnv* env, jobject mc, jobject screen);
jobject newBlankScreen(JNIEnv* env);              // vanilla GuiScreen instance (local ref)
bool isChatScreen(JNIEnv* env, jobject screen);
std::string chatInputText(JNIEnv* env, jobject chatScreen);

}  // namespace vigil::game

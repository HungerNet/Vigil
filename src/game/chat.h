#pragma once
// Vigil game layer — reads new chat lines from the client's chat history (GuiNewChat.chatLines).
// Restored from the legacy client for Auto Who, the Party Detector and the number denicker.

#include <jni.h>

#include <string>
#include <vector>

namespace vigil::game {

class ChatReader {
public:
    // New lines since the last call, oldest first, without formatting codes. Lines Vigil printed
    // itself are skipped. The first call only remembers where chat currently ends.
    std::vector<std::string> poll(JNIEnv* env, jobject mc);
    void reset(JNIEnv* env);
    bool available() const { return available_; }
    int linesRead() const { return linesRead_; }

private:
    bool resolve(JNIEnv* env, jobject chatGui);

    bool resolved_ = false;
    bool resolveFailed_ = false;
    bool available_ = false;
    bool primed_ = false;
    int linesRead_ = 0;
    jweak newest_ = nullptr;
    jfieldID fLines_ = nullptr;
    jmethodID mSize_ = nullptr, mGet_ = nullptr, mComponent_ = nullptr, mText_ = nullptr;
};

}  // namespace vigil::game

#pragma once
// Vigil game layer — runs the Utilities (src/utilities) against the live client: chat lines,
// player spawns, the Bed Wars shop and keyboard shortcuts.

#include "../utilities/utilities.h"
#include "context.h"

#include <windows.h>
#include <jni.h>

#include <atomic>
#include <string>
#include <vector>

namespace vigil {
struct Setting;
}

namespace vigil::game {

class UtilitiesRuntime {
public:
    void onChat(const std::string& cleanLine, bool bedwarsPregame, bool hypixel, double nowMs);
    void onSpawns(const std::vector<int>& entityIds, double nowMs);
    void onWorldChanged();
    // Minecraft thread, once per pump.
    void update(JNIEnv* env, jobject mc, const ContextDetector& ctx, bool hypixel, bool chatAvailable, double nowMs);
    // Window procedure (Minecraft thread): Fast Buy swallows left clicks in the Quick Buy menu.
    bool onMouse(UINT msg);
    // Keyboard shortcut; returns the utility it toggled, or nullptr.
    Setting* onHotkey(int vk);

private:
    void announce(JNIEnv* env, jobject mc, const std::string& text);
    bool resolveShop(JNIEnv* env, jobject mc);
    bool quickBuyVisible(JNIEnv* env, jobject screen);
    bool replayClick(JNIEnv* env, jobject mc, jobject screen, int& slotOut);

    utilities::AutoWho autoWho_;
    utilities::PartyChatDetector party_;
    utilities::QueuePartyDetector queue_;
    std::atomic<bool> quickBuyOpen_{false};
    std::atomic<int> pendingClicks_{0};
    std::atomic<bool> buttonHeld_{false};
    std::atomic<long long> lastQueuedMs_{0};
    int failStreak_ = 0;
    bool fastBuyAnnounced_ = false;

    bool shopResolved_ = false, shopFailed_ = false;
    jclass clsChest_ = nullptr, clsContainer_ = nullptr;
    jfieldID fLowerInv_ = nullptr, fTheSlot_ = nullptr, fInvSlots_ = nullptr, fSlotNumber_ = nullptr, fWindowId_ = nullptr;
    jfieldID fController_ = nullptr, fPlayer_ = nullptr;
    jmethodID mStackInSlot_ = nullptr, mDisplayName_ = nullptr, mWindowClick_ = nullptr;
};

}  // namespace vigil::game

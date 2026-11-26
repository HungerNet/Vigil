#include "utilities_runtime.h"

#include "../core/log.h"
#include "../core/settings.h"
#include "alerts.h"
#include "class_finder.h"
#include "jvm.h"
#include "mc.h"
#include "minecraft_util.h"

#include <cctype>

namespace vigil::game {
namespace {

long long tickMs() { return static_cast<long long>(GetTickCount64()); }

}  // namespace

void UtilitiesRuntime::announce(JNIEnv* env, jobject mc, const std::string& text) {
    const auto& s = utilities::settings();
    if (s.announceInChat && s.announceInChat->b()) printChat(env, mc, chatPrefix() + "\xC2\xA7" "7" + text);
}

void UtilitiesRuntime::onChat(const std::string& line, bool bedwarsPregame, bool hypixel, double nowMs) {
    const auto& s = utilities::settings();
    autoWho_.setEnabled(s.autoWho && s.autoWho->b());
    if (!hypixel) return;
    autoWho_.onChat(line, nowMs, s.autoWhoDelay ? s.autoWhoDelay->value : 400.0);
    if (s.partyChat && s.partyChat->b() && bedwarsPregame) party_.onChat(line, nowMs);
}

void UtilitiesRuntime::onSpawns(const std::vector<int>& ids, double nowMs) {
    for (int id : ids) queue_.onSpawn(id, nowMs);
}

void UtilitiesRuntime::onWorldChanged() {
    party_.reset();
    queue_.reset();
    autoWho_.reset();
}

Setting* UtilitiesRuntime::onHotkey(int vk) {
    if (vk == 0) return nullptr;
    const auto& s = utilities::settings();
    const std::pair<Setting*, Setting*> map[] = {
        {s.autoWhoKey, s.autoWho}, {s.fastBuyKey, s.fastBuy}, {s.partyChatKey, s.partyChat}, {s.partyQueueKey, s.partyQueue}};
    for (const auto& [key, enabled] : map) {
        if (key && enabled && key->i() == vk) {
            enabled->set(enabled->b() ? 0.0 : 1.0);
            return enabled;
        }
    }
    return nullptr;
}

bool UtilitiesRuntime::onMouse(UINT msg) {
    const auto& s = utilities::settings();
    if (!s.fastBuy || !s.fastBuy->b() || !quickBuyOpen_.load()) {
        buttonHeld_ = false;
        return false;
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
        buttonHeld_ = true;
        lastQueuedMs_ = tickMs();
        if (pendingClicks_.fetch_add(1) > 64) pendingClicks_ = 64;  // keep a short queue when spam clicking
        return true;   // replaced by an instant purchase on the Minecraft thread
    }
    if (msg == WM_LBUTTONUP) {
        buttonHeld_ = false;
        return true;   // swallow the matching release too
    }
    return false;
}

bool UtilitiesRuntime::resolveShop(JNIEnv* env, jobject mc) {
    if (shopResolved_) return true;
    if (shopFailed_) return false;
    auto global = [&](const char* name) -> jclass {
        jclass l = McFindClass(env, name);
        if (!l) return nullptr;
        jclass g = static_cast<jclass>(env->NewGlobalRef(l));
        env->DeleteLocalRef(l);
        return g;
    };
    clsChest_ = global("net/minecraft/client/gui/inventory/GuiChest");
    clsContainer_ = global("net/minecraft/client/gui/inventory/GuiContainer");
    if (clsChest_) fLowerInv_ = JniResolveFieldID(env, clsChest_, "Lnet/minecraft/inventory/IInventory;", "lowerChestInventory", "w");
    if (clsContainer_) {
        fTheSlot_ = JniResolveFieldID(env, clsContainer_, "Lnet/minecraft/inventory/Slot;", "theSlot", "u");
        fInvSlots_ = JniResolveFieldID(env, clsContainer_, "Lnet/minecraft/inventory/Container;", "inventorySlots", "h");
    }
    if (jclass inv = McFindClass(env, "net/minecraft/inventory/IInventory")) {
        mStackInSlot_ = JniResolveInstanceMethod(env, inv, "(I)Lnet/minecraft/item/ItemStack;", "getStackInSlot", "a");
        env->DeleteLocalRef(inv);
    }
    if (jclass stack = McFindClass(env, "net/minecraft/item/ItemStack")) {
        mDisplayName_ = JniResolveInstanceMethod(env, stack, "()Ljava/lang/String;", "getDisplayName", "q");
        env->DeleteLocalRef(stack);
    }
    if (jclass slot = McFindClass(env, "net/minecraft/inventory/Slot")) {
        fSlotNumber_ = JniResolveFieldID(env, slot, "I", "slotNumber", "e");
        env->DeleteLocalRef(slot);
    }
    if (jclass cont = McFindClass(env, "net/minecraft/inventory/Container")) {
        fWindowId_ = JniResolveFieldID(env, cont, "I", "windowId", "d");
        env->DeleteLocalRef(cont);
    }
    jclass mcClass = env->GetObjectClass(mc);
    fController_ = JniResolveFieldID(env, mcClass, "Lnet/minecraft/client/multiplayer/PlayerControllerMP;", "playerController", "c");
    static const char* kSelfSigs[] = {"Lnet/minecraft/client/entity/EntityPlayerSP;"};
    fPlayer_ = JniResolveFieldIDFlex(env, mcClass, "thePlayer", "h", kSelfSigs, 1);
    env->DeleteLocalRef(mcClass);
    if (jclass pc = McFindClass(env, "net/minecraft/client/multiplayer/PlayerControllerMP")) {
        mWindowClick_ = JniResolveInstanceMethod(env, pc, "(IIIILnet/minecraft/entity/player/EntityPlayer;)Lnet/minecraft/item/ItemStack;",
                                                 "windowClick", "a");
        env->DeleteLocalRef(pc);
    }
    jniFailed(env);
    shopResolved_ = clsChest_ && clsContainer_ && fLowerInv_ && fTheSlot_ && fInvSlots_ && mStackInSlot_ && mDisplayName_ &&
                    fSlotNumber_ && fWindowId_ && fController_ && fPlayer_ && mWindowClick_;
    if (!shopResolved_) shopFailed_ = true;
    log::writef("runtime", "fast buy: shop members resolved=%d", shopResolved_);
    return shopResolved_;
}

bool UtilitiesRuntime::quickBuyVisible(JNIEnv* env, jobject screen) {
    if (!screen || !env->IsInstanceOf(screen, clsChest_)) return false;
    bool found = false;
    jobject inv = env->GetObjectField(screen, fLowerInv_);
    if (inv && !jniFailed(env)) {
        jobject stack = env->CallObjectMethod(inv, mStackInSlot_, 0);
        if (stack && !jniFailed(env)) {
            jstring name = static_cast<jstring>(env->CallObjectMethod(stack, mDisplayName_));
            if (name && !jniFailed(env)) found = utilities::isQuickBuyMenu(jstringToUtf8(env, name));
            if (name) env->DeleteLocalRef(name);
            env->DeleteLocalRef(stack);
        }
        env->DeleteLocalRef(inv);
    }
    jniFailed(env);
    return found;
}

bool UtilitiesRuntime::replayClick(JNIEnv* env, jobject mc, jobject screen, int& slotOut) {
    bool ok = false;
    jobject slot = env->GetObjectField(screen, fTheSlot_);
    jobject container = env->GetObjectField(screen, fInvSlots_);
    jobject controller = env->GetObjectField(mc, fController_);
    jobject player = env->GetObjectField(mc, fPlayer_);
    if (!jniFailed(env) && container && controller && player) {
        if (!slot) {
            ok = true;   // clicked outside a slot: nothing to buy, drop the click
            slotOut = -1;
        } else {
            const jint slotNumber = env->GetIntField(slot, fSlotNumber_);
            const jint windowId = env->GetIntField(container, fWindowId_);
            if (!jniFailed(env)) {
                // The shop treats a middle click (button 2, mode 3) as an instant purchase.
                jobject result = env->CallObjectMethod(controller, mWindowClick_, windowId, slotNumber, 2, 3, player);
                ok = !jniFailed(env);
                if (result) env->DeleteLocalRef(result);
                slotOut = slotNumber;
            }
        }
    }
    for (jobject o : {slot, container, controller, player}) if (o) env->DeleteLocalRef(o);
    return ok;
}

void UtilitiesRuntime::update(JNIEnv* env, jobject mc, const ContextDetector& ctx, bool hypixel, bool chatAvailable, double nowMs) {
    const auto& s = utilities::settings();
    const bool pregame = hypixel && ctx.bedwarsPregame();

    // Auto Who. Turning it off discards anything scheduled; `due` is false while off.
    autoWho_.setEnabled(s.autoWho && s.autoWho->b());
    if (autoWho_.due(nowMs)) {
        if (sendChatMessage(env, mc, "/who")) {
            autoWho_.markSent();
            utilities::Activity::instance().add(utilities::Kind::AutoWho, "Sent /who as the game started");
        } else {
            autoWho_.markFailed(nowMs);
        }
    }

    // Party Detector (lobby chat).
    if (s.partyChat && s.partyChat->b() && pregame) {
        for (const auto& a : party_.poll(nowMs)) {
            std::string names;
            for (std::size_t i = 0; i < a.names.size(); ++i) names += (i ? ", " : "") + a.names[i];
            const std::string text = "Party of " + std::to_string(a.size) + (a.joined ? " joined: " : " quit: ") + names;
            utilities::Activity::instance().add(utilities::Kind::PartyChat, text, true);
            announce(env, mc, "Party of \xC2\xA7" "c" + std::to_string(a.size) + "\xC2\xA7" "7" + (a.joined ? " joined." : " quit."));
        }
    } else {
        party_.reset();
    }

    // Party Queue Detector (entity ids).
    const int maxParty = utilities::maxPartySizeFromMode(ctx.mode());
    queue_.setActive(s.partyQueue && s.partyQueue->b() && pregame, nowMs);
    if (queue_.active()) {
        if (maxParty != queue_.maxPartySize() && !ctx.mode().empty()) queue_.setMaxPartySize(maxParty, nowMs);
        const auto r = queue_.poll(nowMs);
        if (r.requeueSuggested) {
            utilities::Activity::instance().add(utilities::Kind::PartyQueue, "Fresh server: ids are too low to read parties");
            announce(env, mc, "New server \xC2\xB7 \xC2\xA7" "erequeue to read parties");
        }
        for (const auto& p : r.parties) {
            const std::string text = "Queue party of " + std::to_string(p.size) + " (ids " + std::to_string(p.minId) + "\xE2\x80\x93" +
                                     std::to_string(p.maxId) + ")";
            utilities::Activity::instance().add(utilities::Kind::PartyQueue, text, true);
            announce(env, mc, "Queue party of \xC2\xA7" "c" + std::to_string(p.size) + " \xC2\xA7" "7(ids \xC2\xA7" "e" +
                                  std::to_string(p.minId) + "\xC2\xA7" "7-\xC2\xA7" "e" + std::to_string(p.maxId) + "\xC2\xA7" "7)");
        }
    }

    // Fast Buy.
    bool shopOpen = false;
    if (s.fastBuy && s.fastBuy->b() && hypixel && resolveShop(env, mc)) {
        if (jobject screen = currentScreen(env, mc)) {
            shopOpen = quickBuyVisible(env, screen);
            if (shopOpen) {
                while (pendingClicks_.load() > 0) {
                    int slot = -1;
                    if (replayClick(env, mc, screen, slot)) {
                        --pendingClicks_;
                        failStreak_ = 0;
                        if (slot >= 0) utilities::Activity::instance().add(utilities::Kind::FastBuy, "Bought from Quick Buy slot " + std::to_string(slot));
                    } else if (++failStreak_ >= 8) {
                        --pendingClicks_;   // never leave the queue stuck
                        failStreak_ = 0;
                    } else {
                        break;
                    }
                }
            }
            env->DeleteLocalRef(screen);
        }
    }
    quickBuyOpen_ = shopOpen;
    if (!shopOpen && pendingClicks_.load() > 0 && !buttonHeld_.load() && tickMs() - lastQueuedMs_.load() > 350) pendingClicks_ = 0;

    utilities::LiveState live;
    live.hosted = true;
    live.hypixel = hypixel;
    live.chatReader = chatAvailable;
    live.bedwarsPregame = pregame;
    live.mode = ctx.mode();
    if (!live.mode.empty()) live.mode[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(live.mode[0])));
    live.queueMaxParty = maxParty;
    live.quickBuyOpen = shopOpen;
    utilities::setLiveState(live);
}

}  // namespace vigil::game

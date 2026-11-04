#pragma once

#include "tab_overlay.h"
#include <string>
#include <vector>

namespace taboverlay {

struct NumberDenickerOverlayData {
    bool resolved = false;
    bool suspected = false;
    bool hasPotentials = false;
    int potentialCount = 0;
    std::vector<std::string> potentials;
    std::string realName;
    CachedHypixelStats hyp{};
    bool hypOk = false;
};

void NumberDenickerSetEnabled(bool enabled);
void NumberDenickerSetKeys(const std::string& auroraKey, const std::string& hypixelKey);
void NumberDenickerSetInGame(bool inGame);
void NumberDenickerShutdown();

void NumberDenickerOnChatLine(const std::string& msg);
void NumberDenickerRegisterNick(const std::string& nickIgn);

bool NumberDenickerGetOverlayData(const std::string& nickIgn, NumberDenickerOverlayData& out);
// Returns true once when a nick was resolved (for nick -> realName chat line).
bool NumberDenickerTryConsumeDenickChat(const std::string& nickIgn, std::string& outNickIgn, std::string& outRealName);
// outIsJson: message is JSON for PrintJsonChat (hover). Otherwise plain § text.
bool NumberDenickerTryConsumeChat(std::string& outMsg, bool& outIsJson);

struct NumberDenickerDebugSnapshot {
    int chatFinals = 0;
    int chatBeds = 0;
    int nickRegistered = 0;
    int auroraQueued = 0;
    int resolved = 0;
    int tracked = 0;
    int jobsPending = 0;
    int inGame = 0;
    int gameStarted = 0;
};

void NumberDenickerGetDebugSnapshot(NumberDenickerDebugSnapshot& out);

} // namespace taboverlay

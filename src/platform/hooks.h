#pragma once
// Vigil platform — window and presentation hooks.

#include <windows.h>

namespace vigil::platform {

HWND findGameWindow();
bool installWindowHook(HWND hwnd);
void removeWindowHook();
void ensureWindowHook();
bool installPresentHook();
void ensurePresentHook();
void removePresentHook();

// Health (Status page).
int presentHookSlots();
bool windowHookActive();

}  // namespace vigil::platform

#pragma once

#include <stdint.h>
#include <string>

// FMOD gets one listener per view while the split is on. Call once from DllMain.
void AudioInstallHooks();
// Testing ("p2audio on|off"): off leaves the game's one listener.
std::string AudioTwoListeners(bool on);
std::string AudioDump();
// The game's listener object goes on this vtable (found by player2's heap scan).
uintptr_t AudioListenerVtable();
uintptr_t AudioPrivateListenerVtable();
void AudioListenerMode(int mode);

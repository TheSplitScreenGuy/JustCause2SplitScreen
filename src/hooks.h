#pragma once

// Hooks IDXGISwapChain::Present on the game's own swap chain:
//   game's CreateDXGIFactory import -> IDXGIFactory::CreateSwapChain -> Present.
// Everything after this call happens on the game's thread as it creates its
// renderer. No device of our own is created, because that raced the Steam
// overlay's hooks and crashed the game.
// Call from DllMain, before the game creates its renderer.
bool InstallHooks();

// Caps the frame rate (frames per second, 0 = off); applied in the Present hook.
void SetFpsCap(int fps);

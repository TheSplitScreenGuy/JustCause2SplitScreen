#pragma once

struct IDXGISwapChain;

// Called from the Present hook on the game's render thread, just before the frame is shown.
// F8 toggles the status overlay. The split divider is drawn while the split is active.
void OverlayOnPresent(IDXGISwapChain* swapChain);

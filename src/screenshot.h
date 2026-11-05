#pragma once

struct IDXGISwapChain;

// Saves the current back buffer as a half-size RGB PNG. On failure returns false and
// sets *error. Must be called on the render thread (from the Present hook).
bool SaveScreenshot(IDXGISwapChain* swapChain, const char* path, const char** error);

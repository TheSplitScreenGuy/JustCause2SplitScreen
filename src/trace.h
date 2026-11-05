#pragma once

#include <stdint.h>
#include <string>

// Records the game's main thread (call from DllMain, which runs on it).
void TraceInitMainThread();
unsigned long MainThreadId();

// Logs each distinct call chain (return addresses via the EBP chain) that reaches addr on the
// main thread, for ms milliseconds, using a hardware execute breakpoint. ECX and the first two
// stack arguments are logged with each chain.
std::string TraceStart(uintptr_t addr, unsigned ms, unsigned long threadId = 0); // 0 = main thread

// Call regularly (from Present); stops the trace when its time is up.
void TraceTick();

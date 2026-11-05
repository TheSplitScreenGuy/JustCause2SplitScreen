#pragma once

#include <stddef.h>
#include <stdint.h>

// Replaces the first len bytes of the function at target (which must equal expected and
// contain no relative jumps/calls) with a jump to hook. Returns a trampoline that runs the
// replaced bytes and continues in the original function, or nullptr on failure.
void* InlineHook(uintptr_t target, const unsigned char* expected, size_t len, void* hook);

// Hardware execute breakpoints (debug registers 2 and 3) on one thread: fn runs with the
// thread's registers each time it reaches addr, then the instruction executes normally.
// Changes no code bytes, so it also works on code the game checks for modification.
// Setting replaces any previous breakpoints on that thread.
typedef struct _CONTEXT CONTEXT;
typedef void (*BreakpointFn)(CONTEXT* ctx);
struct Breakpoint {
    uintptr_t addr;
    BreakpointFn fn;
};
// threadId 0 = every thread in the process; threads created later don't get them. Keep that
// short-lived: Steam's anti-tamper check quits the game (status 0x8000DEAD) when it runs on a
// thread with a hardware breakpoint set.
bool SetThreadBreakpoints(unsigned long threadId, const Breakpoint* bps, int count); // count 1 or 2
// Clears them and takes the exception handler out again (nothing left for the anti-tamper check).
void ClearThreadBreakpoints(unsigned long threadId);

// Whether the game runs under Wine/Proton (e.g. on the Steam Deck).
bool RunningUnderWine();

// From inside a breakpoint callback: points execute breakpoint slot 0 or 1 (Dr2/Dr3) of the
// current thread at another address with another callback (addr 0 disarms it).
void RearmBreakpoint(CONTEXT* ctx, int slot, uintptr_t addr, BreakpointFn fn);

// From inside a breakpoint callback: arms (or with addr 0 disarms) a read/write data watch on the
// 4 bytes at addr using debug register 3 of the current thread; each access is logged once per
// instruction. Replaces any execute breakpoint in slot 3.
void ArmDataWatch(CONTEXT* ctx, uintptr_t addr);

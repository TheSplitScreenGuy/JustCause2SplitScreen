#include <windows.h>
#include <stdio.h>
#include "trace.h"
#include "patch.h"
#include "log.h"

namespace {

DWORD g_mainThread;
DWORD g_traceThread;
DWORD g_traceEnd;
bool g_tracing;
const int kMaxChains = 32;
DWORD g_seen[kMaxChains];
volatile LONG g_seenCount;

void OnTraceHit(CONTEXT* ctx)
{
    __try {
        DWORD* esp = (DWORD*)ctx->Esp;
        DWORD chain[10];
        int n = 0;
        chain[n++] = esp[0];
        DWORD* ebp = (DWORD*)ctx->Ebp;
        while (n < 10 && ebp && !IsBadReadPtr(ebp, 8)) {
            chain[n++] = ebp[1];
            ebp = (DWORD*)ebp[0];
        }
        DWORD hash = ctx->Ecx; // distinct objects on the same path are reported separately
        for (int i = 0; i < n; i++) hash = hash * 31 + chain[i];
        for (LONG i = 0; i < g_seenCount; i++)
            if (g_seen[i] == hash) return;
        if (g_seenCount >= kMaxChains) return;
        g_seen[g_seenCount++] = hash;

        char text[200];
        int len = 0;
        for (int i = 0; i < n; i++) len += sprintf_s(text + len, sizeof(text) - len, " %08lX", chain[i]);
        Log("trace %08lX: ecx %08lX args %08lX %08lX from%s", ctx->Eip, ctx->Ecx, esp[1], esp[2], text);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

} // namespace

void TraceInitMainThread()
{
    g_mainThread = GetCurrentThreadId();
}

unsigned long MainThreadId()
{
    return g_mainThread;
}

std::string TraceStart(uintptr_t addr, unsigned ms, unsigned long threadId)
{
    if (g_tracing) return "error trace already running";
    g_seenCount = 0;
    g_traceThread = threadId ? threadId : g_mainThread;
    Breakpoint bp = { addr, OnTraceHit };
    if (!SetThreadBreakpoints(g_traceThread, &bp, 1)) return "error could not set breakpoint";
    g_tracing = true;
    g_traceEnd = GetTickCount() + ms;
    Log("trace on %08X for %u ms (thread %lu)", (unsigned)addr, ms, g_traceThread);
    return "ok tracing (results go to jc2ss.log)";
}

void TraceTick()
{
    if (g_tracing && (LONG)(GetTickCount() - g_traceEnd) >= 0) {
        ClearThreadBreakpoints(g_traceThread);
        g_tracing = false;
        Log("trace done (%ld distinct chains)", g_seenCount);
    }
}

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include "watch.h"

namespace {

struct Hit {
    volatile LONG eip;
    volatile LONG count;
    DWORD thread;
    int slot;
    DWORD ecx; // last value seen ("this" for __thiscall functions)
    DWORD calls[4]; // last seen: the first words on the stack that look like code addresses
};

const DWORD kCodeBegin = 0x00401000, kCodeEnd = 0x00C00000;

void StackCalls(const CONTEXT* ctx, DWORD* calls)
{
    const DWORD* esp = (const DWORD*)ctx->Esp;
    int n = 0;
    for (int i = 0; i < 64 && n < 4; i++)
        if (esp[i] >= kCodeBegin && esp[i] < kCodeEnd) calls[n++] = esp[i];
    while (n < 4) calls[n++] = 0;
}

const int kMaxHits = 512;
Hit g_hits[kMaxHits];
volatile LONG g_overflow;
PVOID g_handler;
uintptr_t g_addrs[4];
int g_count;
bool g_execute;

bool g_byEcx; // "watch ... ecx": a row per (address, ecx) instead of per address

void Record(DWORD eip, int slot, DWORD ecx, const CONTEXT* ctx)
{
    // Open addressing on (eip, slot[, ecx]); safe from many threads at once.
    LONG key = (LONG)eip ^ slot ^ (g_byEcx ? (LONG)(ecx * 31u) : 0);
    for (int i = 0; i < kMaxHits; i++) {
        Hit& h = g_hits[(DWORD(key) * 2654435761u + i) % kMaxHits];
        LONG prev = InterlockedCompareExchange(&h.eip, (LONG)eip, 0);
        if (prev == 0) {
            h.slot = slot;
            h.thread = GetCurrentThreadId();
            h.ecx = ecx;
            MemoryBarrier();
            StackCalls(ctx, h.calls);
            InterlockedIncrement(&h.count);
            return;
        }
        if (prev == (LONG)eip && h.slot == slot && (!g_byEcx || h.ecx == ecx)) {
            h.ecx = ecx;
            StackCalls(ctx, h.calls);
            InterlockedIncrement(&h.count);
            return;
        }
    }
    InterlockedIncrement(&g_overflow);
}

LONG CALLBACK OnException(EXCEPTION_POINTERS* info)
{
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* ctx = info->ContextRecord;
    DWORD triggered = ctx->Dr6 & 0xF;
    if (!triggered) return EXCEPTION_CONTINUE_SEARCH;
    for (int i = 0; i < 4; i++)
        if (triggered & (1u << i)) Record(ctx->Eip, i, ctx->Ecx, ctx);
    ctx->Dr6 = 0;
    // Execute breakpoints fire before the instruction runs; the resume flag lets it run once.
    if (g_execute) ctx->EFlags |= 0x10000;
    return EXCEPTION_CONTINUE_EXECUTION;
}

struct ApplyArgs {
    DWORD dr[4];
    DWORD dr7;
};

// Runs on its own thread so it can also update the thread that asked for it.
DWORD WINAPI ApplyToAllThreads(LPVOID param)
{
    auto args = (ApplyArgs*)param;

    // Collect thread IDs before suspending anything, so no allocation happens while
    // another thread (possibly holding the heap lock) is suspended.
    DWORD ids[1024];
    int n = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = { sizeof(te) };
    for (BOOL ok = Thread32First(snap, &te); ok && n < 1024; ok = Thread32Next(snap, &te))
        if (te.th32OwnerProcessID == GetCurrentProcessId() && te.th32ThreadID != GetCurrentThreadId())
            ids[n++] = te.th32ThreadID;
    CloseHandle(snap);

    for (int i = 0; i < n; i++) {
        HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, ids[i]);
        if (!t) continue;
        if (SuspendThread(t) != (DWORD)-1) {
            CONTEXT ctx = {};
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(t, &ctx)) {
                ctx.Dr0 = args->dr[0];
                ctx.Dr1 = args->dr[1];
                ctx.Dr2 = args->dr[2];
                ctx.Dr3 = args->dr[3];
                ctx.Dr6 = 0;
                ctx.Dr7 = args->dr7;
                SetThreadContext(t, &ctx);
            }
            ResumeThread(t);
        }
        CloseHandle(t);
    }
    return 0;
}

void Apply(const ApplyArgs& args)
{
    HANDLE t = CreateThread(nullptr, 0, ApplyToAllThreads, (LPVOID)&args, 0, nullptr);
    if (t) {
        WaitForSingleObject(t, 5000);
        CloseHandle(t);
    }
}

} // namespace

bool WatchStart(const uintptr_t* addrs, int count, WatchMode mode, std::string* error, bool byEcx)
{
    if (g_count) { *error = "a watch is already running"; return false; }
    if (count < 1 || count > 4) { *error = "watch 1 to 4 addresses"; return false; }

    ApplyArgs args = {};
    for (int i = 0; i < count; i++) {
        if (mode != WatchMode::Execute && (addrs[i] & 3)) { *error = "addresses must be 4-byte aligned"; return false; }
        args.dr[i] = (DWORD)addrs[i];
        g_addrs[i] = addrs[i];
        // Local enable; condition 00 execute / 01 write / 11 read-write;
        // length 4 bytes (11) for data, 1 byte (00) for execute.
        DWORD rw = mode == WatchMode::Execute ? 0u : mode == WatchMode::Write ? 1u : 3u;
        DWORD len = mode == WatchMode::Execute ? 0u : 3u;
        args.dr7 |= (1u << (i * 2)) | (rw << (16 + i * 4)) | (len << (18 + i * 4));
    }
    g_execute = mode == WatchMode::Execute;
    g_byEcx = byEcx;

    memset(g_hits, 0, sizeof(g_hits));
    g_overflow = 0;
    g_count = count;
    g_handler = AddVectoredExceptionHandler(1, OnException);
    Apply(args);
    return true;
}

std::string WatchStop()
{
    if (!g_count) return "no watch running";
    ApplyArgs clear = {};
    Apply(clear);
    if (g_handler) RemoveVectoredExceptionHandler(g_handler);
    g_handler = nullptr;

    std::string report;
    char line[200];
    for (int slot = 0; slot < g_count; slot++) {
        sprintf_s(line, "watch %d: %08X\n", slot, (unsigned)g_addrs[slot]);
        report += line;
        for (const Hit& h : g_hits) {
            if (!h.eip || h.slot != slot) continue;
            sprintf_s(line, "  %s %08X  hits %-8ld thread %-6lu ecx %08X  stack %08X %08X %08X %08X\n",
                      g_execute ? "at   " : "after", (unsigned)h.eip, h.count, h.thread, (unsigned)h.ecx,
                      (unsigned)h.calls[0], (unsigned)h.calls[1], (unsigned)h.calls[2], (unsigned)h.calls[3]);
            report += line;
        }
    }
    if (g_overflow) {
        sprintf_s(line, "(%ld hits not recorded: table full)\n", g_overflow);
        report += line;
    }
    g_count = 0;
    return report;
}

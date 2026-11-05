#include <windows.h>
#include <tlhelp32.h>
#include <string.h>
#include "patch.h"
#include "log.h"

void* InlineHook(uintptr_t target, const unsigned char* expected, size_t len, void* hook)
{
    if (len < 5 || memcmp((void*)target, expected, len) != 0) return nullptr;
    auto tramp = (unsigned char*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return nullptr;
    memcpy(tramp, (void*)target, len);
    tramp[len] = 0xE9;
    *(int32_t*)(tramp + len + 1) = (int32_t)(target + len - (uintptr_t)(tramp + len + 5));

    DWORD oldProtect;
    if (!VirtualProtect((void*)target, len, PAGE_EXECUTE_READWRITE, &oldProtect)) return nullptr;
    auto p = (unsigned char*)target;
    p[0] = 0xE9;
    *(int32_t*)(p + 1) = (int32_t)((uintptr_t)hook - (target + 5));
    for (size_t i = 5; i < len; i++) p[i] = 0x90;
    VirtualProtect((void*)target, len, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), (void*)target, len);
    return tramp;
}

namespace {

// Slots 2 and 3 (Dr2/Dr3); 0 and 1 are left for the watch tool.
Breakpoint volatile g_bps[2];
PVOID g_bpHandler;

uintptr_t volatile g_dataWatch; // address watched by ArmDataWatch, 0 if none
DWORD g_dataSeen[64];
int g_dataSeenCount;

// Slot to switch back on after the single step past its breakpoint (-1 none), per thread.
__declspec(thread) int t_rearmSlot = -1;

LONG CALLBACK OnBreakpoint(EXCEPTION_POINTERS* info)
{
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* ctx = info->ContextRecord;
    // The trap after the breakpoint's instruction: the breakpoint goes back on.
    if (t_rearmSlot >= 0 && ctx->Eip != g_bps[t_rearmSlot].addr) {
        int slot = t_rearmSlot;
        t_rearmSlot = -1;
        ctx->ContextFlags |= CONTEXT_DEBUG_REGISTERS; // changes below only apply with it (Wine)
        if (g_bps[slot].addr) ctx->Dr7 |= 1u << (4 + 2 * slot);
        ctx->EFlags &= ~0x100u;
        ctx->Dr6 = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (g_dataWatch && (ctx->Dr6 & 8) && ctx->Dr3 == g_dataWatch) {
        bool seen = false;
        for (int i = 0; i < g_dataSeenCount; i++) seen |= g_dataSeen[i] == ctx->Eip;
        if (!seen && g_dataSeenCount < 64) {
            g_dataSeen[g_dataSeenCount++] = ctx->Eip;
            DWORD* ebp = (DWORD*)ctx->Ebp;
            DWORD r1 = 0, r2 = 0;
            __try { r1 = ebp ? ebp[1] : 0; r2 = (ebp && ebp[0]) ? ((DWORD*)ebp[0])[1] : 0; } __except (EXCEPTION_EXECUTE_HANDLER) {}
            Log("  data watch %08X: accessed by %08lX (from %08lX %08lX) ecx %08lX", (unsigned)g_dataWatch, ctx->Eip, r1, r2, ctx->Ecx);
        }
        ctx->Dr6 = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    // Matched by address alone: under Wine/Proton (Steam Deck) Dr6 doesn't say which slot fired,
    // and every hit went unhandled (CreatePlayer died with 0x80000004 at the constructor).
    for (int i = 0; i < 2; i++) {
        if (!g_bps[i].addr || ctx->Eip != g_bps[i].addr) continue;
        ctx->ContextFlags |= CONTEXT_DEBUG_REGISTERS; // changes below only apply with it (Wine)
        if (BreakpointFn fn = g_bps[i].fn) fn(ctx);
        ctx->Dr6 = 0;
        ctx->EFlags |= 0x10000; // resume flag: run the instruction without re-triggering
        // Wine may ignore the resume flag: the slot also goes off for one single step (trap flag)
        // and comes back on at the trap, unless the callback moved it elsewhere.
        if (ctx->Eip == g_bps[i].addr && (i == 0 ? ctx->Dr2 : ctx->Dr3) == g_bps[i].addr) {
            ctx->Dr7 &= ~(1u << (4 + 2 * i));
            ctx->EFlags |= 0x100;
            t_rearmSlot = i;
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

struct DrArgs {
    DWORD thread;
    DWORD addr[2]; // 0 = disabled
};

// A thread can't reliably change its own debug registers, so a helper does it.
DWORD WINAPI SetDr23(LPVOID param)
{
    auto a = (DrArgs*)param;
    HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, a->thread);
    if (!t) return 0;
    if (SuspendThread(t) != (DWORD)-1) {
        CONTEXT ctx = {};
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(t, &ctx)) {
            ctx.Dr2 = a->addr[0];
            ctx.Dr3 = a->addr[1];
            // Slots 2/3: enable bits 4-7, condition+length bits 24-31. Execute = condition 00, length 00.
            ctx.Dr7 &= ~((0xFu << 4) | (0xFFu << 24));
            if (a->addr[0]) ctx.Dr7 |= 1u << 4;
            if (a->addr[1]) ctx.Dr7 |= 1u << 6;
            // Clearing (no slot of ours left) leaves no debug register state at all: Steam's
            // anti-tamper quits (0x8000DEAD) when it finds any on the thread, and under Wine/Proton
            // (Steam Deck) what was left after Player 2's spawn ended the game a while later.
            if (!a->addr[0] && !a->addr[1] && !g_dataWatch) {
                ctx.Dr0 = ctx.Dr1 = ctx.Dr2 = ctx.Dr3 = 0;
                ctx.Dr6 = 0;
                ctx.Dr7 = 0;
            }
            SetThreadContext(t, &ctx);
            if (!a->addr[0] && !a->addr[1]) {
                CONTEXT check = {};
                check.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (GetThreadContext(t, &check) &&
                    (check.Dr0 || check.Dr1 || check.Dr2 || check.Dr3 || check.Dr7))
                    Log("Breakpoints: thread %lu still has debug registers after clearing (dr0-3 %08lX %08lX "
                        "%08lX %08lX, dr6 %08lX, dr7 %08lX)", a->thread, check.Dr0, check.Dr1, check.Dr2, check.Dr3,
                        check.Dr6, check.Dr7);
            }
        }
        ResumeThread(t);
    }
    CloseHandle(t);
    return 1;
}

// thread 0 = every thread in the process (except the helper itself).
DWORD WINAPI SetDr23AllThreads(LPVOID param)
{
    auto a = (DrArgs*)param;
    // Collect the ids first: nothing is suspended while the snapshot allocates.
    DWORD ids[1024];
    int n = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = { sizeof(te) };
    for (BOOL ok = Thread32First(snap, &te); ok && n < 1024; ok = Thread32Next(snap, &te))
        if (te.th32OwnerProcessID == GetCurrentProcessId() && te.th32ThreadID != GetCurrentThreadId())
            ids[n++] = te.th32ThreadID;
    CloseHandle(snap);
    for (int i = 0; i < n; i++) {
        DrArgs one = { ids[i], { a->addr[0], a->addr[1] } };
        SetDr23(&one);
    }
    return 1;
}

// Under Wine/Proton the debug registers a helper sets from outside (SetThreadContext) and the ones a
// breakpoint handler changes through its context can disagree: the spawn's breakpoints were changed
// in the handler, the helper's clear then left the real registers set while reading back zeros, and
// the anti-tamper check quit the game a while later (Steam Deck). So on the calling thread itself
// they are set the handler's way: a private exception whose handler writes them into the context
// the thread continues with.
const DWORD kSetOwnDrException = 0xE0D2D2D2;
__declspec(thread) DWORD t_ownDr[3]; // Dr2, Dr3, Dr7

LONG CALLBACK OnSetOwnDr(EXCEPTION_POINTERS* info)
{
    if (info->ExceptionRecord->ExceptionCode != kSetOwnDrException) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* ctx = info->ContextRecord;
    ctx->ContextFlags |= CONTEXT_DEBUG_REGISTERS;
    ctx->Dr0 = ctx->Dr1 = 0;
    ctx->Dr2 = t_ownDr[0];
    ctx->Dr3 = t_ownDr[1];
    ctx->Dr6 = 0;
    ctx->Dr7 = t_ownDr[2];
    return EXCEPTION_CONTINUE_EXECUTION;
}

bool SetOwnDebugRegisters(DWORD a0, DWORD a1)
{
    t_ownDr[0] = a0;
    t_ownDr[1] = a1;
    t_ownDr[2] = (a0 ? 1u << 4 : 0) | (a1 ? 1u << 6 : 0);
    PVOID handler = AddVectoredExceptionHandler(1, OnSetOwnDr);
    if (!handler) return false;
    RaiseException(kSetOwnDrException, 0, 0, nullptr);
    RemoveVectoredExceptionHandler(handler);
    return true;
}

bool Apply(DWORD thread, DWORD a0, DWORD a1)
{
    if (thread && thread == GetCurrentThreadId() && RunningUnderWine()) return SetOwnDebugRegisters(a0, a1);
    DrArgs a = { thread, { a0, a1 } };
    HANDLE h = CreateThread(nullptr, 0, thread ? SetDr23 : SetDr23AllThreads, &a, 0, nullptr);
    if (!h) return false;
    WaitForSingleObject(h, 5000);
    DWORD code = 0;
    GetExitCodeThread(h, &code);
    CloseHandle(h);
    return code == 1;
}

} // namespace

bool SetThreadBreakpoints(unsigned long threadId, const Breakpoint* bps, int count)
{
    if (!g_bpHandler) g_bpHandler = AddVectoredExceptionHandler(1, OnBreakpoint);
    for (int i = 0; i < 2; i++) {
        g_bps[i].addr = i < count ? bps[i].addr : 0;
        g_bps[i].fn = i < count ? bps[i].fn : nullptr;
    }
    return Apply(threadId, (DWORD)g_bps[0].addr, (DWORD)g_bps[1].addr);
}

void RearmBreakpoint(CONTEXT* ctx, int slot, uintptr_t addr, BreakpointFn fn)
{
    if (slot != 0 && slot != 1) return;
    g_bps[slot].addr = addr;
    g_bps[slot].fn = fn;
    DWORD enable = 1u << (4 + 2 * slot);
    DWORD conditions = 0xFu << (24 + 4 * slot);
    ctx->Dr7 &= ~(enable | conditions); // execute: condition and length 00
    if (slot == 0) ctx->Dr2 = (DWORD)addr;
    else ctx->Dr3 = (DWORD)addr;
    if (addr) ctx->Dr7 |= enable;
}

void ArmDataWatch(CONTEXT* ctx, uintptr_t addr)
{
    g_bps[1].addr = 0; // slot 3 no longer an execute breakpoint
    g_bps[1].fn = nullptr;
    ctx->Dr7 &= ~((3u << 6) | (0xFu << 28));
    ctx->Dr3 = (DWORD)addr;
    if (addr) ctx->Dr7 |= (1u << 6) | (3u << 28) | (3u << 30); // local enable, read/write, 4 bytes
    g_dataSeenCount = 0;
    g_dataWatch = addr;
}

void ClearThreadBreakpoints(unsigned long threadId)
{
    g_dataWatch = 0;
    Apply(threadId, 0, 0);
    for (int i = 0; i < 2; i++) {
        g_bps[i].fn = nullptr;
        g_bps[i].addr = 0;
    }
    // No handler left behind either: under Wine/Proton (Steam Deck) the anti-tamper check still
    // quit the game after Player 2's spawn with every debug register verified clear.
    if (g_bpHandler) {
        RemoveVectoredExceptionHandler(g_bpHandler);
        g_bpHandler = nullptr;
    }
    t_rearmSlot = -1;
}

bool RunningUnderWine()
{
    static int wine = -1;
    if (wine < 0) {
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        wine = ntdll && GetProcAddress(ntdll, "wine_get_version") ? 1 : 0;
        // Testing on Windows: jc2ss.ini [Debug] ForceWine=1 takes the Wine paths.
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        if (char* slash = strrchr(path, '\\')) *(slash + 1) = '\0';
        strcat_s(path, "jc2ss.ini");
        if (GetPrivateProfileIntA("Debug", "ForceWine", 0, path)) wine = 1;
    }
    return wine == 1;
}

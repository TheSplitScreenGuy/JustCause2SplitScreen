# Prints each JustCause2.exe thread's EIP and the game-code addresses found on its stack (likely
# return addresses), for diagnosing hangs. Threads are suspended briefly one at a time.
# Usage: python -I threadstacks.py [maxThreads]
import ctypes, ctypes.wintypes as wt, sys, struct
sys.path.insert(0, __file__.rsplit('\\', 1)[0])
from procmem import Game, find_pid

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
TH32CS_SNAPTHREAD = 0x4
THREAD_ALL = 0x0002 | 0x0008 | 0x0010 | 0x0040  # suspend/resume, get/set context, query


class THREADENTRY32(ctypes.Structure):
    _fields_ = [('dwSize', wt.DWORD), ('cntUsage', wt.DWORD), ('th32ThreadID', wt.DWORD),
                ('th32OwnerProcessID', wt.DWORD), ('tpBasePri', ctypes.c_long), ('tpDeltaPri', ctypes.c_long),
                ('dwFlags', wt.DWORD)]


class WOW64_CONTEXT(ctypes.Structure):
    _fields_ = [('ContextFlags', wt.DWORD), ('Dr', wt.DWORD * 6), ('FloatSave', ctypes.c_byte * 112),
                ('SegGs', wt.DWORD), ('SegFs', wt.DWORD), ('SegEs', wt.DWORD), ('SegDs', wt.DWORD),
                ('Edi', wt.DWORD), ('Esi', wt.DWORD), ('Ebx', wt.DWORD), ('Edx', wt.DWORD), ('Ecx', wt.DWORD),
                ('Eax', wt.DWORD), ('Ebp', wt.DWORD), ('Eip', wt.DWORD), ('SegCs', wt.DWORD),
                ('EFlags', wt.DWORD), ('Esp', wt.DWORD), ('SegSs', wt.DWORD), ('ExtendedRegisters', ctypes.c_byte * 512)]


k32.CreateToolhelp32Snapshot.restype = wt.HANDLE
k32.OpenThread.restype = wt.HANDLE
pid = find_pid()
g = Game()
snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
te = THREADENTRY32()
te.dwSize = ctypes.sizeof(te)
ids = []
ok = k32.Thread32First(snap, ctypes.byref(te))
while ok:
    if te.th32OwnerProcessID == pid:
        ids.append(te.th32ThreadID)
    ok = k32.Thread32Next(snap, ctypes.byref(te))
k32.CloseHandle(snap)
limit = int(sys.argv[1]) if len(sys.argv) > 1 else 200
for tid in ids[:limit]:
    h = k32.OpenThread(THREAD_ALL, False, tid)
    if not h:
        continue
    k32.Wow64SuspendThread(h)
    ctx = WOW64_CONTEXT()
    ctx.ContextFlags = 0x00010001 | 0x00010002  # control | integer
    got = k32.Wow64GetThreadContext(h, ctypes.byref(ctx))
    k32.ResumeThread(h)
    k32.CloseHandle(h)
    if not got:
        continue
    raw = g.read(ctx.Esp, 0x1000) or b''
    rets = [v for v in struct.unpack('<%dI' % (len(raw) // 4), raw) if 0x00401000 <= v < 0x00D92000][:14]
    print('%6d eip %08X esp %08X  %s' % (tid, ctx.Eip, ctx.Esp, ' '.join('%08X' % v for v in rets)))

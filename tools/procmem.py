# Minimal read-only access to JustCause2.exe memory for analysis scripts.
import ctypes, ctypes.wintypes as wt, struct

PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
TH32CS_SNAPPROCESS = 0x2

k32 = ctypes.WinDLL('kernel32', use_last_error=True)


class PROCESSENTRY32(ctypes.Structure):
    _fields_ = [('dwSize', wt.DWORD), ('cntUsage', wt.DWORD), ('th32ProcessID', wt.DWORD),
                ('th32DefaultHeapID', ctypes.c_size_t), ('th32ModuleID', wt.DWORD), ('cntThreads', wt.DWORD),
                ('th32ParentProcessID', wt.DWORD), ('pcPriClassBase', ctypes.c_long), ('dwFlags', wt.DWORD),
                ('szExeFile', ctypes.c_char * 260)]


class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [('BaseAddress', ctypes.c_void_p), ('AllocationBase', ctypes.c_void_p), ('AllocationProtect', wt.DWORD),
                ('PartitionId', wt.WORD), ('RegionSize', ctypes.c_size_t), ('State', wt.DWORD), ('Protect', wt.DWORD),
                ('Type', wt.DWORD)]


k32.VirtualQueryEx.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.POINTER(MEMORY_BASIC_INFORMATION), ctypes.c_size_t]
k32.VirtualQueryEx.restype = ctypes.c_size_t
k32.CreateToolhelp32Snapshot.restype = wt.HANDLE
k32.OpenProcess.restype = wt.HANDLE
k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]


def find_pid(name=b'JustCause2.exe'):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    pe = PROCESSENTRY32()
    pe.dwSize = ctypes.sizeof(pe)
    pid = None
    ok = k32.Process32First(snap, ctypes.byref(pe))
    while ok:
        if pe.szExeFile.lower() == name.lower():
            pid = pe.th32ProcessID
        ok = k32.Process32Next(snap, ctypes.byref(pe))
    k32.CloseHandle(snap)
    if pid is None:
        raise SystemExit('JustCause2.exe is not running')
    return pid


k32.WriteProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]


class Game:
    def __init__(self, write=False):
        access = PROCESS_VM_READ | PROCESS_QUERY_INFORMATION | (0x0020 | 0x0008 if write else 0)
        self.handle = k32.OpenProcess(access, False, find_pid())
        if not self.handle:
            raise SystemExit('could not open JustCause2.exe')

    def write(self, addr, data):
        n = ctypes.c_size_t()
        if not k32.WriteProcessMemory(self.handle, ctypes.c_void_p(addr), data, len(data), ctypes.byref(n)) or n.value != len(data):
            # Read-only data (.rdata): make the page writable first.
            old = wt.DWORD()
            if not k32.VirtualProtectEx(self.handle, ctypes.c_void_p(addr), len(data), 0x04, ctypes.byref(old)) or \
               not k32.WriteProcessMemory(self.handle, ctypes.c_void_p(addr), data, len(data), ctypes.byref(n)):
                raise SystemExit('write failed at %08X' % addr)

    def write_u32(self, addr, value):
        self.write(addr, struct.pack('<I', value))

    def read(self, addr, size):
        buf = ctypes.create_string_buffer(size)
        got = ctypes.c_size_t()
        if not k32.ReadProcessMemory(self.handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(got)) or got.value != size:
            return None
        return buf.raw

    def floats(self, addr, n):
        b = self.read(addr, n * 4)
        return struct.unpack('<%df' % n, b) if b else None

    def u32(self, addr):
        b = self.read(addr, 4)
        return struct.unpack('<I', b)[0] if b else None

    def regions(self):
        """Yields (base, size) of committed, writable, non-mapped memory below 4 GB."""
        mbi = MEMORY_BASIC_INFORMATION()
        p = 0
        writable = 0x04 | 0x08 | 0x40 | 0x80
        while p < 0x100000000 and k32.VirtualQueryEx(self.handle, ctypes.c_void_p(p), ctypes.byref(mbi), ctypes.sizeof(mbi)):
            base = mbi.BaseAddress or 0
            if mbi.State == 0x1000 and (mbi.Protect & writable) and not (mbi.Protect & 0x100) and mbi.Type != 0x40000:
                yield base, mbi.RegionSize
            p = base + mbi.RegionSize

    def find_dwords(self, values, limit=1000000):
        """Returns {value: [addresses]} for aligned occurrences of each dword in writable memory."""
        pats = {v: struct.pack('<I', v) for v in values}
        found = {v: [] for v in values}
        for base, size in self.regions():
            for off in range(0, size, 1 << 24):
                n = min(1 << 24, size - off)
                b = self.read(base + off, n)
                if not b:
                    continue
                for v, pat in pats.items():
                    i = b.find(pat)
                    while i >= 0 and len(found[v]) < limit:
                        if i % 4 == 0:
                            found[v].append(base + off + i)
                        i = b.find(pat, i + 1)
        return found

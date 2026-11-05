// memscan: a small command-line memory scanner for JustCause2.exe, used instead of
// Cheat Engine to find addresses (camera, player, cutscene state) by narrowing
// down values across scans. Scan state is kept on disk between runs in tools\scans.
//
//   memscan new <type> eq <v> | range <a> <b> | unknown
//   memscan next eq <v> | range <a> <b> | changed | unchanged | inc | dec | incby <d> | decby <d>
//   memscan list [n]
//   memscan read <addr> [bytes]
//   memscan watch <addr> <type> [seconds]
//   memscan write <addr> <type> <value>
//   memscan modules
//
// Types: u8, i32, f32, f64. Addresses: hex (0x1234ABCD) or module+offset (JustCause2.exe+0x10).
// Float "eq" and "incby"/"decby" match within a tolerance: add "tol <x>" (default 0.01).

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <string>
#include <vector>

namespace {

enum ValueType { T_U8, T_I32, T_F32, T_F64, T_COUNT };
struct TypeInfo { const char* name; int size; int align; };
const TypeInfo kTypes[T_COUNT] = { { "u8", 1, 1 }, { "i32", 4, 4 }, { "f32", 4, 4 }, { "f64", 8, 4 } };

enum Op { OP_EQ, OP_RANGE, OP_CHANGED, OP_UNCHANGED, OP_INC, OP_DEC, OP_INCBY, OP_DECBY, OP_UNKNOWN };
struct Cond { Op op = OP_UNKNOWN; double a = 0, b = 0, tol = 0; };

enum { KIND_SNAPSHOT = 0, KIND_CANDIDATES = 1 };
struct Header { char magic[4]; uint32_t kind; uint32_t type; uint32_t reserved; uint64_t count; };
struct Rec { uint64_t addr; uint64_t raw; };
struct ChunkHeader { uint64_t base; uint64_t size; };

const size_t kChunk = 1 << 20;

HANDLE g_proc;
std::string g_scanDir;

struct Module { std::string name; uint64_t base, size; };
std::vector<Module> g_modules;

__declspec(noreturn) void Fail(const char* msg)
{
    fprintf(stderr, "error: %s\n", msg);
    exit(1);
}

bool IsFloat(ValueType t) { return t == T_F32 || t == T_F64; }

double AsDouble(ValueType t, uint64_t raw)
{
    switch (t) {
    case T_U8: return (double)(uint8_t)raw;
    case T_I32: return (double)(int32_t)(uint32_t)raw;
    case T_F32: { float f; uint32_t u = (uint32_t)raw; memcpy(&f, &u, 4); return f; }
    default: { double d; memcpy(&d, &raw, 8); return d; }
    }
}

uint64_t FromDouble(ValueType t, double v)
{
    uint64_t raw = 0;
    switch (t) {
    case T_U8: raw = (uint8_t)(int)v; break;
    case T_I32: raw = (uint32_t)(int32_t)v; break;
    case T_F32: { float f = (float)v; memcpy(&raw, &f, 4); break; }
    default: memcpy(&raw, &v, 8); break;
    }
    return raw;
}

uint64_t LoadRaw(const uint8_t* p, ValueType t)
{
    uint64_t raw = 0;
    memcpy(&raw, p, kTypes[t].size);
    return raw;
}

std::string FormatValue(ValueType t, uint64_t raw)
{
    char buf[64];
    if (IsFloat(t)) sprintf_s(buf, "%.6g", AsDouble(t, raw)); // %f overflows the buffer on huge doubles
    else sprintf_s(buf, "%lld", (long long)AsDouble(t, raw));
    return buf;
}

bool Match(const Cond& c, ValueType t, uint64_t prevRaw, uint64_t curRaw)
{
    double cur = AsDouble(t, curRaw);
    if (IsFloat(t) && !isfinite(cur)) return false;
    double prev = AsDouble(t, prevRaw);
    switch (c.op) {
    case OP_EQ: return fabs(cur - c.a) <= c.tol;
    case OP_RANGE: return cur >= c.a && cur <= c.b;
    case OP_CHANGED: return curRaw != prevRaw;
    case OP_UNCHANGED: return curRaw == prevRaw;
    case OP_INC: return cur > prev;
    case OP_DEC: return cur < prev;
    case OP_INCBY: return fabs((cur - prev) - c.a) <= c.tol;
    case OP_DECBY: return fabs((prev - cur) - c.a) <= c.tol;
    default: return true;
    }
}

ValueType ParseType(const char* s)
{
    for (int i = 0; i < T_COUNT; i++)
        if (_stricmp(s, kTypes[i].name) == 0) return (ValueType)i;
    Fail("unknown type (use u8, i32, f32, f64)");
}

double ParseNumber(const char* s)
{
    char* end;
    double v = (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? (double)strtoull(s, &end, 16) : strtod(s, &end);
    if (*end) Fail("bad number");
    return v;
}

// Parses "<op> [args] [tol <x>]" starting at argv[i].
Cond ParseCond(int argc, char** argv, int i, ValueType t, bool firstScan)
{
    if (i >= argc) Fail("missing condition");
    Cond c;
    const char* op = argv[i++];
    auto need = [&](int n) { if (i + n > argc) Fail("missing value for condition"); };
    if (!_stricmp(op, "eq")) { need(1); c.op = OP_EQ; c.a = ParseNumber(argv[i++]); }
    else if (!_stricmp(op, "range")) { need(2); c.op = OP_RANGE; c.a = ParseNumber(argv[i++]); c.b = ParseNumber(argv[i++]); }
    else if (!_stricmp(op, "unknown") && firstScan) c.op = OP_UNKNOWN;
    else if (!firstScan && !_stricmp(op, "changed")) c.op = OP_CHANGED;
    else if (!firstScan && !_stricmp(op, "unchanged")) c.op = OP_UNCHANGED;
    else if (!firstScan && !_stricmp(op, "inc")) c.op = OP_INC;
    else if (!firstScan && !_stricmp(op, "dec")) c.op = OP_DEC;
    else if (!firstScan && !_stricmp(op, "incby")) { need(1); c.op = OP_INCBY; c.a = ParseNumber(argv[i++]); }
    else if (!firstScan && !_stricmp(op, "decby")) { need(1); c.op = OP_DECBY; c.a = ParseNumber(argv[i++]); }
    else Fail("unknown condition");

    c.tol = IsFloat(t) ? 0.01 : 0.0;
    if (i + 1 < argc && !_stricmp(argv[i], "tol")) c.tol = ParseNumber(argv[i + 1]);
    return c;
}

void OpenGame(bool write)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    DWORD pid = 0;
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!_stricmp(pe.szExeFile, "JustCause2.exe")) pid = pe.th32ProcessID;
    CloseHandle(snap);
    if (!pid) Fail("JustCause2.exe is not running");

    DWORD access = PROCESS_VM_READ | PROCESS_QUERY_INFORMATION;
    if (write) access |= PROCESS_VM_WRITE | PROCESS_VM_OPERATION;
    g_proc = OpenProcess(access, FALSE, pid);
    if (!g_proc) Fail("could not open JustCause2.exe");

    HMODULE mods[1024];
    DWORD needed = 0;
    if (EnumProcessModulesEx(g_proc, mods, sizeof(mods), &needed, LIST_MODULES_ALL)) {
        for (DWORD i = 0; i < needed / sizeof(HMODULE) && i < 1024; i++) {
            char name[MAX_PATH];
            MODULEINFO info;
            if (GetModuleBaseNameA(g_proc, mods[i], name, MAX_PATH) && GetModuleInformation(g_proc, mods[i], &info, sizeof(info)))
                g_modules.push_back({ name, (uint64_t)info.lpBaseOfDll, info.SizeOfImage });
        }
    }
}

std::string FormatAddr(uint64_t addr)
{
    char buf[128];
    for (const Module& m : g_modules) {
        if (addr >= m.base && addr < m.base + m.size) {
            sprintf_s(buf, "%08llX (%s+0x%llX)", addr, m.name.c_str(), addr - m.base);
            return buf;
        }
    }
    sprintf_s(buf, "%08llX", addr);
    return buf;
}

uint64_t ParseAddr(const char* s)
{
    const char* plus = strchr(s, '+');
    if (plus) {
        std::string name(s, plus - s);
        for (const Module& m : g_modules)
            if (!_stricmp(m.name.c_str(), name.c_str())) return m.base + strtoull(plus + 1, nullptr, 16);
        Fail("module not found");
    }
    return strtoull(s, nullptr, 16);
}

bool ReadMem(uint64_t addr, void* out, size_t size)
{
    SIZE_T got = 0;
    return ReadProcessMemory(g_proc, (LPCVOID)addr, out, size, &got) && got == size;
}

// Committed, writable, non-guard memory below 4 GB (the game is 32-bit).
std::vector<ChunkHeader> WritableRegions()
{
    std::vector<ChunkHeader> out;
    MEMORY_BASIC_INFORMATION mbi;
    uint64_t p = 0;
    const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    while (p < 0x100000000ull && VirtualQueryEx(g_proc, (LPCVOID)p, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        if (mbi.State == MEM_COMMIT && (mbi.Protect & writable) && !(mbi.Protect & PAGE_GUARD) && mbi.Type != MEM_MAPPED)
            out.push_back({ (uint64_t)mbi.BaseAddress, mbi.RegionSize });
        p = (uint64_t)mbi.BaseAddress + mbi.RegionSize;
    }
    return out;
}

std::string StatePath(const char* name) { return g_scanDir + "\\" + name; }

FILE* OpenWrite(const std::string& path, Header& h, uint32_t kind, ValueType t)
{
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") || !f) Fail("cannot write scan state");
    setvbuf(f, nullptr, _IOFBF, 1 << 20);
    memcpy(h.magic, "MSCN", 4);
    h.kind = kind;
    h.type = t;
    h.reserved = 0;
    h.count = 0;
    fwrite(&h, sizeof(h), 1, f);
    return f;
}

void CloseWrite(FILE* f, Header& h)
{
    fseek(f, 0, SEEK_SET);
    fwrite(&h, sizeof(h), 1, f);
    fclose(f);
}

FILE* OpenState(Header& h)
{
    FILE* f = nullptr;
    if (fopen_s(&f, StatePath("state.bin").c_str(), "rb") || !f) Fail("no scan yet: run 'memscan new' first");
    setvbuf(f, nullptr, _IOFBF, 1 << 20);
    if (fread(&h, sizeof(h), 1, f) != 1 || memcmp(h.magic, "MSCN", 4)) Fail("scan state is corrupt");
    return f;
}

void Commit()
{
    std::string tmp = StatePath("state.tmp"), dst = StatePath("state.bin");
    if (!MoveFileExA(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) Fail("cannot save scan state");
}

// Scans every aligned value in buf; prev == nullptr means compare against nothing (first scan).
void ScanBuffer(const uint8_t* cur, const uint8_t* prev, size_t size, uint64_t base, ValueType t,
                const Cond& c, FILE* out, Header& h)
{
    int sz = kTypes[t].size, align = kTypes[t].align;
    for (size_t off = 0; off + sz <= size; off += align) {
        uint64_t curRaw = LoadRaw(cur + off, t);
        uint64_t prevRaw = prev ? LoadRaw(prev + off, t) : 0;
        if (Match(c, t, prevRaw, curRaw)) {
            Rec r = { base + off, curRaw };
            fwrite(&r, sizeof(r), 1, out);
            h.count++;
        }
    }
}

void CmdNew(ValueType t, const Cond& c)
{
    Header h;
    FILE* out = OpenWrite(StatePath("state.tmp"), h, c.op == OP_UNKNOWN ? KIND_SNAPSHOT : KIND_CANDIDATES, t);
    std::vector<uint8_t> buf(kChunk);
    uint64_t bytes = 0;

    for (const ChunkHeader& region : WritableRegions()) {
        for (uint64_t off = 0; off < region.size; off += kChunk) {
            ChunkHeader chunk = { region.base + off, std::min((uint64_t)kChunk, region.size - off) };
            if (!ReadMem(chunk.base, buf.data(), (size_t)chunk.size)) continue;
            bytes += chunk.size;
            if (c.op == OP_UNKNOWN) {
                fwrite(&chunk, sizeof(chunk), 1, out);
                fwrite(buf.data(), 1, (size_t)chunk.size, out);
                h.count++;
            } else {
                ScanBuffer(buf.data(), nullptr, (size_t)chunk.size, chunk.base, t, c, out, h);
            }
        }
    }
    CloseWrite(out, h);
    Commit();

    if (c.op == OP_UNKNOWN)
        printf("Snapshot of %.1f MB taken (%s). Now change the value in game and run 'memscan next ...'.\n",
               bytes / 1048576.0, kTypes[t].name);
    else
        printf("%llu matches (%s) in %.1f MB\n", h.count, kTypes[t].name, bytes / 1048576.0);
}

// Reads one value at a time, caching the 4 KB page it sits in.
struct PageReader {
    uint64_t page = ~0ull;
    bool ok = false;
    uint8_t buf[4096];

    bool Read(uint64_t addr, void* out, int size)
    {
        uint64_t base = addr & ~4095ull;
        if (addr + size > base + 4096) return ReadMem(addr, out, size);
        if (base != page) {
            page = base;
            ok = ReadMem(base, buf, 4096);
        }
        if (ok) memcpy(out, buf + (addr - base), size);
        return ok;
    }
};

void CmdNext(const Cond& c)
{
    Header in;
    FILE* f = OpenState(in);
    ValueType t = (ValueType)in.type;
    Header h;
    FILE* out = OpenWrite(StatePath("state.tmp"), h, KIND_CANDIDATES, t);

    if (in.kind == KIND_SNAPSHOT) {
        std::vector<uint8_t> prev(kChunk), cur(kChunk);
        ChunkHeader chunk;
        for (uint64_t i = 0; i < in.count && fread(&chunk, sizeof(chunk), 1, f) == 1; i++) {
            fread(prev.data(), 1, (size_t)chunk.size, f);
            if (!ReadMem(chunk.base, cur.data(), (size_t)chunk.size)) continue;
            ScanBuffer(cur.data(), prev.data(), (size_t)chunk.size, chunk.base, t, c, out, h);
        }
    } else {
        PageReader reader;
        Rec r;
        int sz = kTypes[t].size;
        for (uint64_t i = 0; i < in.count && fread(&r, sizeof(r), 1, f) == 1; i++) {
            uint64_t curRaw = 0;
            if (!reader.Read(r.addr, &curRaw, sz)) continue;
            if (Match(c, t, r.raw, curRaw)) {
                Rec n = { r.addr, curRaw };
                fwrite(&n, sizeof(n), 1, out);
                h.count++;
            }
        }
    }
    fclose(f);
    CloseWrite(out, h);
    Commit();
    printf("%llu matches left (%s)\n", h.count, kTypes[t].name);
}

void CmdList(uint64_t limit)
{
    Header h;
    FILE* f = OpenState(h);
    if (h.kind == KIND_SNAPSHOT) Fail("only a snapshot so far: run 'memscan next ...' first");
    ValueType t = (ValueType)h.type;
    printf("%llu matches (%s)\n", h.count, kTypes[t].name);

    PageReader reader;
    Rec r;
    for (uint64_t i = 0; i < h.count && i < limit && fread(&r, sizeof(r), 1, f) == 1; i++) {
        uint64_t curRaw = 0;
        std::string now = reader.Read(r.addr, &curRaw, kTypes[t].size) ? FormatValue(t, curRaw) : "?";
        printf("  %-40s last %-14s now %s\n", FormatAddr(r.addr).c_str(), FormatValue(t, r.raw).c_str(), now.c_str());
    }
    fclose(f);
}

void CmdRead(uint64_t addr, int bytes)
{
    std::vector<uint8_t> buf(bytes);
    if (!ReadMem(addr, buf.data(), bytes)) Fail("address not readable");
    for (int row = 0; row < bytes; row += 16) {
        printf("%s ", FormatAddr(addr + row).c_str());
        for (int i = 0; i < 16; i++) {
            if (row + i < bytes) printf(" %02X", buf[row + i]);
            else printf("   ");
        }
        printf("  |");
        for (int i = 0; i + 4 <= 16 && row + i + 4 <= bytes; i += 4) {
            float f;
            memcpy(&f, &buf[row + i], 4);
            printf(" %12.4g", f);
        }
        printf("\n");
    }
}

void CmdWatch(uint64_t addr, ValueType t, double seconds)
{
    uint64_t last = ~0ull;
    ULONGLONG end = GetTickCount64() + (ULONGLONG)(seconds * 1000);
    while (GetTickCount64() < end) {
        uint64_t raw = 0;
        if (!ReadMem(addr, &raw, kTypes[t].size)) Fail("address not readable");
        if (raw != last) {
            printf("%s = %s\n", FormatAddr(addr).c_str(), FormatValue(t, raw).c_str());
            fflush(stdout);
            last = raw;
        }
        Sleep(100);
    }
}

void CmdWrite(uint64_t addr, ValueType t, double value)
{
    uint64_t raw = FromDouble(t, value);
    SIZE_T written = 0;
    if (!WriteProcessMemory(g_proc, (LPVOID)addr, &raw, kTypes[t].size, &written)) Fail("write failed");
    printf("Wrote %s to %s\n", FormatValue(t, raw).c_str(), FormatAddr(addr).c_str());
}

void Usage()
{
    printf(
        "memscan new <type> eq <v> | range <a> <b> | unknown\n"
        "memscan next eq <v> | range <a> <b> | changed | unchanged | inc | dec | incby <d> | decby <d>\n"
        "memscan list [n]\n"
        "memscan read <addr> [bytes]\n"
        "memscan watch <addr> <type> [seconds]\n"
        "memscan write <addr> <type> <value>\n"
        "memscan modules\n"
        "types: u8 i32 f32 f64   float matches accept 'tol <x>' (default 0.01)\n");
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { Usage(); return 1; }
    const char* cmd = argv[1];

    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string dir(exe);
    dir = dir.substr(0, dir.find_last_of('\\'));
    g_scanDir = dir + "\\..\\scans";
    CreateDirectoryA(g_scanDir.c_str(), nullptr);

    bool write = !_stricmp(cmd, "write");
    OpenGame(write);

    if (!_stricmp(cmd, "new") && argc >= 3) {
        ValueType t = ParseType(argv[2]);
        CmdNew(t, ParseCond(argc, argv, 3, t, true));
    } else if (!_stricmp(cmd, "next")) {
        Header h;
        FILE* f = OpenState(h);
        fclose(f);
        CmdNext(ParseCond(argc, argv, 2, (ValueType)h.type, false));
    } else if (!_stricmp(cmd, "list")) {
        CmdList(argc >= 3 ? (uint64_t)ParseNumber(argv[2]) : 30);
    } else if (!_stricmp(cmd, "read") && argc >= 3) {
        CmdRead(ParseAddr(argv[2]), argc >= 4 ? (int)ParseNumber(argv[3]) : 64);
    } else if (!_stricmp(cmd, "watch") && argc >= 4) {
        CmdWatch(ParseAddr(argv[2]), ParseType(argv[3]), argc >= 5 ? ParseNumber(argv[4]) : 10);
    } else if (write && argc >= 5) {
        CmdWrite(ParseAddr(argv[2]), ParseType(argv[3]), ParseNumber(argv[4]));
    } else if (!_stricmp(cmd, "modules")) {
        for (const Module& m : g_modules) printf("%08llX %08llX %s\n", m.base, m.size, m.name.c_str());
    } else {
        Usage();
        return 1;
    }
    return 0;
}

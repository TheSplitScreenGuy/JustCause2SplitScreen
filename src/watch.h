#pragma once

#include <stdint.h>
#include <string>

// Hardware watchpoints (CPU debug registers) on up to 4 addresses, applied to every thread
// in the game. Data watches (aligned dwords) record the instruction after each access, the
// thread and which address was touched, so we can find the code that reads or writes a value
// (e.g. the camera matrix). Execute watches record entries into code, with ECX ("this").

enum class WatchMode { Write, ReadWrite, Execute };

// byEcx: a report row per (address, ECX) instead of per address.
bool WatchStart(const uintptr_t* addrs, int count, WatchMode mode, std::string* error, bool byEcx = false);

// Clears the watchpoints and returns a report of the hits.
std::string WatchStop();
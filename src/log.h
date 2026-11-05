#pragma once

// Writes jc2ss.log next to JustCause2.exe. Safe to call from any thread.
void LogInit();
void Log(const char* fmt, ...);

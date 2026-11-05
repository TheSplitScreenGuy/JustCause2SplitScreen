#include <windows.h>
#include <stdio.h>
#include <share.h>
#include <stdarg.h>
#include "log.h"

static FILE* g_file;
static CRITICAL_SECTION g_lock;

void LogInit()
{
    InitializeCriticalSection(&g_lock);

    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) *(slash + 1) = '\0';
    strcat_s(path, "jc2ss.log");

    g_file = _fsopen(path, "w", _SH_DENYWR);
}

void Log(const char* fmt, ...)
{
    if (!g_file) return;

    SYSTEMTIME t;
    GetLocalTime(&t);

    EnterCriticalSection(&g_lock);
    fprintf(g_file, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    vfprintf(g_file, fmt, args);
    va_end(args);
    fputc('\n', g_file);
    fflush(g_file);
    LeaveCriticalSection(&g_lock);
}

// diagnostics.cpp — Crash logging, debug output, exception handling.
// Separate from main.cpp so it can be ripped out for release builds.
//
// This file owns:
//   - dbglog / dbglog_loc (debug file logging)
//   - VEH crash handler (diagnostic-only, never suppresses exceptions)
//   - Log file initialization and path management

#include "skygfx.h"
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

// ============================================================
// Log file path
// ============================================================
static char s_logPath[MAX_PATH];
static int  s_logInit = 0;

const char* diag_getLogPath(void) { return s_logPath; }

// ============================================================
// dbglog — file-based debug logging
// ============================================================
static const char *s_logLevelStr[] = { "INFO ", "WARN ", "ERROR", "FATAL" };

static void
dbglog_internal(LogLevel level, const char *file, int line, const char *func, const char *fmt, va_list ap)
{
	char msg[4096];
	int len = vsnprintf(msg, sizeof(msg), fmt, ap);
	if(len < 0) return;
	if(len >= sizeof(msg)) len = sizeof(msg) - 1;

	const char *shortFile = file;
	for(const char *p = file; *p; p++)
		if(*p == '\\' || *p == '/') shortFile = p + 1;

	char buf[4096];
	SYSTEMTIME st;
	GetLocalTime(&st);
	int hlen;
	if(level >= 0)
		hlen = snprintf(buf, sizeof(buf), "[%02d:%02d:%02d.%03d] [%s] %s:%d %s() - %s\n",
			st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
			s_logLevelStr[level], shortFile, line, func, msg);
	else
		hlen = snprintf(buf, sizeof(buf), "[%02d:%02d:%02d.%03d] [TRACE] %s:%d %s() - %s\n",
			st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
			shortFile, line, func, msg);
	if(hlen < 0 || hlen >= sizeof(buf)) return;

	HANDLE h = CreateFileA(s_logPath, FILE_APPEND_DATA, FILE_SHARE_READ,
		NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if(h == INVALID_HANDLE_VALUE) return;
	DWORD written;
	WriteFile(h, buf, hlen, &written, NULL);
	FlushFileBuffers(h);
	CloseHandle(h);
}

void
dbglog(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	dbglog_internal(LOG_INFO, "", 0, "", fmt, ap);
	va_end(ap);
}

void
dbglog_loc(LogLevel level, const char *file, int line, const char *func, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	dbglog_internal(level, file, line, func, fmt, ap);
	va_end(ap);
}

// ============================================================
// VEH crash handler — diagnostic only, never suppresses exceptions
// ============================================================
static LONG WINAPI
diag_crashHandler(EXCEPTION_POINTERS *ep)
{
	DWORD code = ep->ExceptionRecord->ExceptionCode;

	// Let C++ exceptions pass through to try/catch blocks
	if(code == 0xE06D7363)
		return EXCEPTION_CONTINUE_SEARCH;

	CONTEXT *ctx = ep->ContextRecord;
	DWORD numParams = ep->ExceptionRecord->NumberParameters;
	dbglog("CRASH: code=0x%08X at=0x%p EAX=%08X EBX=%08X ECX=%08X EDX=%08X ESI=%08X EDI=%08X EBP=%08X ESP=%08X",
		code, ep->ExceptionRecord->ExceptionAddress,
		ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx, ctx->Esi, ctx->Edi, ctx->Ebp, ctx->Esp);
	if(numParams >= 2)
		dbglog("  ExceptionInfo: %s addr=0x%p",
			ep->ExceptionRecord->ExceptionInformation[0] == 0 ? "READ" :
			ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "WRITE" : "EXECUTE",
			(void*)ep->ExceptionRecord->ExceptionInformation[1]);

	// Module info
	HMODULE exeMod = GetModuleHandle(NULL);
	MEMORY_BASIC_INFORMATION mbi = {};
	VirtualQuery((void*)ctx->Eip, &mbi, sizeof(mbi));
	HMODULE crashMod = NULL;
	if(mbi.AllocationBase)
		crashMod = (HMODULE)mbi.AllocationBase;
	dbglog("  EIP module: %p (base=%p protect=0x%X)", crashMod, exeMod, mbi.Protect);

	// Walk EBP chain (up to 8 frames)
	DWORD *frame = (DWORD*)ctx->Ebp;
	dbglog("  Stack trace:");
	for(int i = 0; i < 8 && frame && frame != (DWORD*)0xFFFFFFFF; i++){
		DWORD retAddr = frame[1];
		MEMORY_BASIC_INFORMATION fmbi = {};
		VirtualQuery((void*)retAddr, &fmbi, sizeof(fmbi));
		HMODULE fmod = fmbi.AllocationBase ? (HMODULE)fmbi.AllocationBase : NULL;
		dbglog("    [%d] EBP=%08X RET=%08X (module=%p)", i, (DWORD)frame, retAddr, fmod);
		DWORD *next = (DWORD*)frame[0];
		if(next <= frame) break;
		frame = next;
	}

	// Dump first 16 DWORDs from ESP
	DWORD *sp = (DWORD*)ctx->Esp;
	dbglog("  Stack dump (ESP):");
	for(int i = 0; i < 16; i++)
		dbglog("    [ESP+0x%02X] = %08X", i*4, sp[i]);

	// Diagnostic-only: always let exceptions propagate naturally.
	return EXCEPTION_CONTINUE_SEARCH;
}

// ============================================================
// Public API
// ============================================================
static LONG_PTR s_oldVEH = 0;

void
diag_init(const char *logPath)
{
	if(logPath)
		strncpy(s_logPath, logPath, MAX_PATH - 1);
	s_logInit = 1;

	// Truncate log on fresh boot
	HANDLE hLog = CreateFileA(s_logPath, GENERIC_WRITE, FILE_SHARE_READ,
		NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);
}

void
diag_installVEH(void)
{
	s_oldVEH = (LONG_PTR)AddVectoredExceptionHandler(1, diag_crashHandler);
}

void
diag_removeVEH(void)
{
	if(s_oldVEH){
		RemoveVectoredExceptionHandler((HANDLE)s_oldVEH);
		s_oldVEH = 0;
	}
}

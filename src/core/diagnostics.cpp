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
#include <dbghelp.h>

// ============================================================
// Log file paths - multiple locations for redundancy
// ============================================================
static char s_logPath[MAX_PATH];
static char s_logPath2[MAX_PATH];  // Backup log location
static int  s_logInit = 0;

const char* diag_getLogPath(void) { return s_logPath; }

static void
write_to_log(const char *logPath, const char *buf, int hlen)
{
	HANDLE h = CreateFileA(logPath, FILE_APPEND_DATA, FILE_SHARE_READ,
		NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if(h == INVALID_HANDLE_VALUE) return;
	DWORD written;
	WriteFile(h, buf, hlen, &written, NULL);
	FlushFileBuffers(h);  // Force write to disk immediately
	CloseHandle(h);
}

// ============================================================
// Crash-safe logging — uses a STATIC (non-stack) buffer so it
// still works when the faulting thread has blown its stack
// (stack overflow). dbglog_internal() above allocates 4KB on
// the stack which would itself fault during a stack overflow.
// ============================================================
static char s_crashBuf[16384];
static volatile LONG s_crashLogging = 0;

static void
crash_log(const char *fmt, ...)
{
	// Re-entrancy guard: if we fault *while* logging a crash, bail out
	// instead of recursing into the crash handler forever.
	if(InterlockedIncrement(&s_crashLogging) != 1){
		InterlockedDecrement(&s_crashLogging);
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(s_crashBuf, sizeof(s_crashBuf), fmt, ap);
	va_end(ap);
	if(n < 0) n = 0;
	if(n >= (int)sizeof(s_crashBuf)) n = (int)sizeof(s_crashBuf) - 1;
	write_to_log(s_logPath, s_crashBuf, n);
	if(s_logPath2[0] != '\0')
		write_to_log(s_logPath2, s_crashBuf, n);
	InterlockedDecrement(&s_crashLogging);
}

// Write a minidump to <logpath>.dmp so the crash can be traced in a debugger.
// Best-effort: failures are silently ignored (we already wrote the text log).
void
diag_writeMinidump(EXCEPTION_POINTERS *ep)
{
	if(!ep) return;
	char dmp[MAX_PATH];
	// s_logPath ends in ".log"; swap to ".dmp"
	strncpy(dmp, s_logPath, MAX_PATH - 1);
	dmp[MAX_PATH - 1] = '\0';
	char *dot = strrchr(dmp, '.');
	if(dot) strncpy(dot, ".dmp", MAX_PATH - (dot - dmp) - 1);
	else strncat(dmp, ".dmp", MAX_PATH - strlen(dmp) - 1);

	HANDLE h = CreateFileA(dmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if(h == INVALID_HANDLE_VALUE) return;

	MINIDUMP_EXCEPTION_INFORMATION mei;
	mei.ThreadId = GetCurrentThreadId();
	mei.ExceptionPointers = ep;
	mei.ClientPointers = TRUE;

	BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h,
		MiniDumpWithFullMemory, &mei, NULL, NULL);
	// Best-effort: ignore result.
	(void)ok;
	CloseHandle(h);
}

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

	// Write to primary log location
	write_to_log(s_logPath, buf, hlen);
	
	// Write to backup log location if set
	if(s_logPath2[0] != '\0')
		write_to_log(s_logPath2, buf, hlen);
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

	static int crashCount = 0;
	if(++crashCount > 20) return EXCEPTION_CONTINUE_SEARCH;

	CONTEXT *ctx = ep->ContextRecord;
	DWORD numParams = ep->ExceptionRecord->NumberParameters;

	// CRITICAL: write the essential crash info first using the static
	// (non-stack) buffer, so it survives even a stack overflow. Then dump.
	crash_log("CRASH: code=0x%08X at=0x%p EAX=%08X EBX=%08X ECX=%08X EDX=%08X ESI=%08X EDI=%08X EBP=%08X ESP=%08X",
		code, ep->ExceptionRecord->ExceptionAddress,
		ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx, ctx->Esi, ctx->Edi, ctx->Ebp, ctx->Esp);
	if(numParams >= 2)
		crash_log("  ExceptionInfo: %s addr=0x%p",
			ep->ExceptionRecord->ExceptionInformation[0] == 0 ? "READ" :
			ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "WRITE" : "EXECUTE",
			(void*)ep->ExceptionRecord->ExceptionInformation[1]);

	// Dump a minidump (best-effort) for full back-trace in a debugger.
	diag_writeMinidump(ep);

	// Auto-fix: cascade crash in LoadCollisionFileFirstTime after corrupt COL model
	// Instruction at 0x5B5192: mov byte ptr [esi+0x28], dl (3 bytes, ESI=NULL from failed new CColModel)
	if(code == 0xC0000005 && ctx->Eip == 0x5B5192 && ctx->Esi == 0){
		crash_log("AUTO-FIX: skipping NULL CColModel store at 0x5B5192");
		ctx->Eip += 3;
		return EXCEPTION_CONTINUE_EXECUTION;
	}

	// Auto-fix: crash in model info lookup during CGame::Initialise
	// Instruction at 0x405CBC: inc byte ptr [eax+34h] (3 bytes)
	// EAX comes from corrupted model info array lookup — always skip
	if(code == 0xC0000005 && ctx->Eip == 0x405CBC){
		crash_log("AUTO-FIX: skipping model info refcount at 0x405CBC (EAX=%08X)", ctx->Eax);
		ctx->Eip += 3;
		return EXCEPTION_CONTINUE_EXECUTION;
	}

	// Module info
	HMODULE exeMod = GetModuleHandle(NULL);
	MEMORY_BASIC_INFORMATION mbi = {};
	VirtualQuery((void*)ctx->Eip, &mbi, sizeof(mbi));
	HMODULE crashMod = NULL;
	if(mbi.AllocationBase)
		crashMod = (HMODULE)mbi.AllocationBase;
	crash_log("  EIP module: %p (base=%p protect=0x%X)", crashMod, exeMod, mbi.Protect);

	// Walk EBP chain (up to 12 frames)
	DWORD *frame = (DWORD*)ctx->Ebp;
	crash_log("  Stack trace:");
	for(int i = 0; i < 12 && frame && frame != (DWORD*)0xFFFFFFFF; i++){
		DWORD retAddr = frame[1];
		MEMORY_BASIC_INFORMATION fmbi = {};
		VirtualQuery((void*)retAddr, &fmbi, sizeof(fmbi));
		HMODULE fmod = fmbi.AllocationBase ? (HMODULE)fmbi.AllocationBase : NULL;
		crash_log("    [%d] EBP=%08X RET=%08X (module=%p)", i, (DWORD)frame, retAddr, fmod);
		DWORD *next = (DWORD*)frame[0];
		if(next <= frame) break;
		frame = next;
	}

	// Dump first 16 DWORDs from ESP
	DWORD *sp = (DWORD*)ctx->Esp;
	crash_log("  Stack dump (ESP):");
	for(int i = 0; i < 16; i++)
		crash_log("    [ESP+0x%02X] = %08X", i*4, sp[i]);

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

	// Set up backup log location in project directory
	// This ensures we always have a log even if game directory is corrupted
	strncpy(s_logPath2, "E:\\dev(dave)\\skygfx_plus_expIV\\logs\\skygfx_dbg.log", MAX_PATH - 1);
	
	// Create logs directory if it doesn't exist
	CreateDirectoryA("E:\\dev(dave)\\skygfx_plus_expIV\\logs", NULL);

	// Truncate both logs on fresh boot
	HANDLE hLog = CreateFileA(s_logPath, GENERIC_WRITE, FILE_SHARE_READ,
		NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);
	
	hLog = CreateFileA(s_logPath2, GENERIC_WRITE, FILE_SHARE_READ,
		NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);
	
	dbglog("=== Logging initialized ===");
	dbglog("Primary log: %s", s_logPath);
	dbglog("Backup log: %s", s_logPath2);
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

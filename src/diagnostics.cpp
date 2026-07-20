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

	// Write minidump
	diag_writeMinidump(ep);

	// Build a human-readable crash summary for MessageBox
	char mbTitle[128];
	snprintf(mbTitle, sizeof(mbTitle), "skygfx CRASH 0x%08X", code);
	char mbBody[2048];
	snprintf(mbBody, sizeof(mbBody),
		"Exception: 0x%08X\n"
		"Address:   %p\n"
		"Registers: EAX=%08X EBX=%08X ECX=%08X EDX=%08X\n"
		"           ESI=%08X EDI=%08X EBP=%08X ESP=%08X\n"
		"EIP mod:   %p  (exe=%p)\n\n"
		"Full crash log written to:\n%s\n\n"
		"Click OK to let the game continue (may hang).\n"
		"Click Cancel to terminate.",
		code, ep->ExceptionRecord->ExceptionAddress,
		ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx,
		ctx->Esi, ctx->Edi, ctx->Ebp, ctx->Esp,
		crashMod, exeMod, s_logPath);
	if(code == EXCEPTION_ACCESS_VIOLATION && numParams >= 2){
		int rw = (int)ep->ExceptionRecord->ExceptionInformation[0];
		snprintf(mbBody + strlen(mbBody), sizeof(mbBody) - strlen(mbBody),
			"\nAccess: %s at %p",
			rw == 0 ? "READ" : rw == 1 ? "WRITE" : "EXECUTE",
			(void*)ep->ExceptionRecord->ExceptionInformation[1]);
	}

	// Show MessageBox — user can decide to continue or terminate
	int mbRet = MessageBoxA(NULL, mbBody, mbTitle, MB_ICONERROR | MB_OKCANCEL | MB_TOPMOST);
	if(mbRet == IDCANCEL)
		TerminateProcess(GetCurrentProcess(), code);

	// Diagnostic-only: always let exceptions propagate naturally.
	return EXCEPTION_CONTINUE_SEARCH;
}

// ============================================================
// Watchdog — detects freezes (no crash, just hangs)
// ============================================================
static volatile LONG s_heartbeat = 0;
static volatile LONG s_watchdogRunning = 0;
static HWND s_gameHwnd = NULL;

void diag_heartbeat(void)
{
	InterlockedExchange(&s_heartbeat, GetTickCount());
}

static void diag_findGameWindow(void)
{
	if(s_gameHwnd) return;
	// Find the game's main window by class name (GTA SA uses "LaunchUnrealUWindow" or gets a raw HWND)
	struct EnumCtx { HWND found; } ctx = { NULL };
	EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
		DWORD pid = 0;
		GetWindowThreadProcessId(hwnd, &pid);
		if(pid == GetCurrentProcessId() && IsWindowVisible(hwnd)){
			((EnumCtx*)lp)->found = hwnd;
			return FALSE;
		}
		return TRUE;
	}, (LPARAM)&ctx);
	s_gameHwnd = ctx.found;
}

static DWORD WINAPI watchdogThread(LPVOID)
{
	InterlockedExchange(&s_watchdogRunning, 1);
	dbglog("Watchdog: started (thread id=%08X)", GetCurrentThreadId());

	// Wait for game to start producing heartbeats
	Sleep(5000);

	while(s_watchdogRunning){
		DWORD last = (DWORD)s_heartbeat;
		DWORD now = GetTickCount();

		if(last != 0 && (now - last) > 30000){
			// 30 seconds with no heartbeat — game is frozen
			dbglog("Watchdog: FREEZE DETECTED — no heartbeat for %u ms (last=%u now=%u)", now - last, last, now);

			diag_findGameWindow();

			char title[128];
			snprintf(title, sizeof(title), "skygfx FREEZE DETECTED");
			char body[1024];
			snprintf(body, sizeof(body),
				"The game has not responded for over 30 seconds.\n\n"
				"It may be frozen on a loading screen or in a loop.\n\n"
				"Full log written to:\n%s\n\n"
				"Click OK to try to continue.\n"
				"Click Cancel to terminate.",
				s_logPath);

			// Show on top of whatever is visible
			int mbRet = MessageBoxA(s_gameHwnd, body, title, MB_ICONWARNING | MB_OKCANCEL | MB_TOPMOST);
			if(mbRet == IDCANCEL)
				TerminateProcess(GetCurrentProcess(), 0xDEAD);

			// Reset heartbeat so we don't re-trigger immediately
			InterlockedExchange(&s_heartbeat, GetTickCount());
			Sleep(5000);
		}

		Sleep(2000);
	}
	dbglog("Watchdog: exiting");
	return 0;
}

void diag_startWatchdog(void)
{
	if(s_watchdogRunning) return;
	InterlockedExchange(&s_heartbeat, GetTickCount());
	CreateThread(NULL, 0, watchdogThread, NULL, 0, NULL);
}

void diag_stopWatchdog(void)
{
	InterlockedExchange(&s_watchdogRunning, 0);
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

// ============================================================
// Minidump writer
// ============================================================
void
diag_writeMinidump(EXCEPTION_POINTERS *ep)
{
	if(!ep) return;

	char dumpPath[MAX_PATH];
	strncpy(dumpPath, s_logPath, MAX_PATH - 1);
	char *ext = strrchr(dumpPath, '.');
	if(ext) strcpy(ext, "_crash.dmp");
	else strcat(dumpPath, "_crash.dmp");

	HANDLE hFile = CreateFileA(dumpPath, GENERIC_WRITE, 0, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hFile == INVALID_HANDLE_VALUE){
		dbglog("diag_writeMinidump: failed to create %s (err=%d)", dumpPath, GetLastError());
		return;
	}

	// Use Windows MiniDumpWriteDump API
	typedef BOOL (WINAPI *MiniDumpWriteDump_t)(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
	HMODULE dbgDll = GetModuleHandleA("dbghelp.dll");
	if(!dbgDll) dbgDll = LoadLibraryA("dbghelp.dll");
	if(dbgDll){
		MiniDumpWriteDump_t pDump = (MiniDumpWriteDump_t)GetProcAddress(dbgDll, "MiniDumpWriteDump");
		if(pDump){
			MINIDUMP_EXCEPTION_INFORMATION mei;
			mei.ThreadId = GetCurrentThreadId();
			mei.ExceptionPointers = ep;
			mei.ClientPointers = FALSE;
			BOOL ok = pDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
				MINIDUMPWithDataSegs, &mei, NULL, NULL);
			dbglog("diag_writeMinidump: %s to %s", ok ? "OK" : "FAILED", dumpPath);
		} else {
			dbglog("diag_writeMinidump: MiniDumpWriteDump not found");
		}
	} else {
		dbglog("diag_writeMinidump: dbghelp.dll not available");
	}
	CloseHandle(hFile);
}

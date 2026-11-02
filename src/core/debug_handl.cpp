// debug_handl.cpp - consolidated debug/diagnostics/menu translation unit.
//
// Merged 2026-10-26 from THREE former TUs, concatenated in this order:
//   1. src/core/diagnostics.cpp  - logging, ring buffer, scope tags, dbglog,
//                                  crash VEH, watchdog, minidump
//   2. src/core/crashhandler.cpp  - self-installing unhandled-exception filter
//   3. src/core/menu_inject.cpp   - native CMenuManager settings injection
//
// Bodies are verbatim; only the includes were deduped, section banners added,
// and the single g_allowCrashPassThrough extern moved to debug_handl.h.
// Order matters: the crashhandler static installer must run after the
// diag_* statics above it.

// ---- Includes (deduped union of the three TUs) ----
#include "skygfx.h"
#include "debug_handl.h"
#include <windows.h>
#include <DbgHelp.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#pragma comment(lib, "dbghelp.lib")

// ===========================================================================
// LOGGING, DEBUG OUTPUT, RING BUFFER, EXCEPTION VEH, WATCHDOG  (was diagnostics.cpp)
// ===========================================================================
// diagnostics.cpp — Crash logging, debug output, exception handling.
// Separate from main.cpp so it can be ripped out for release builds.
//
// This file owns:
//   - dbglog / dbglog_loc (debug file logging)
//   - VEH crash handler (diagnostic-only, never suppresses exceptions)
//   - Log file initialization and path management
//   - Log ring buffer (last N messages) for crash dump context
//   - Scope tag registry (address→function name for crash backtrace)
// ============================================================
// Log file path
// ============================================================
static char s_logPath[MAX_PATH];
static int  s_logInit = 0;

const char* diag_getLogPath(void) { return s_logPath; }

// Diagnostic verbosity latch (decl + docs: debug_handl.h).  Default 0 =
// current behavior; nothing changes until a lane or config raises it.
int g_diagLevel = 0;

// ============================================================
// Log ring buffer — lock-free, single writer (main thread)
// ============================================================
#define RING_SIZE 32
#define RING_MSG_LEN 160
static char s_ring[RING_SIZE][RING_MSG_LEN];
static volatile int s_ringPos = 0;

void diag_ringPut(const char *msg) {
	int pos = s_ringPos;
	s_ring[pos % RING_SIZE][0] = 0;
	strncpy(s_ring[pos % RING_SIZE], msg, RING_MSG_LEN - 1);
	s_ring[pos % RING_SIZE][RING_MSG_LEN - 1] = 0;
	// Masked increment: a raw ++ would overflow this int negative (UB) after
	// 2^31 puts, making pos % RING_SIZE negative -> out-of-bounds slot write.
	// The mask keeps every index non-negative for the life of the process.
	s_ringPos = (pos + 1) & 0x7FFFFFFF; // volatile store, single thread
}

void diag_ringDump(void) {
	int count = s_ringPos;
	if(count > RING_SIZE) count = RING_SIZE;
	int start = (s_ringPos - count) % RING_SIZE;
	// Always log to a file handle directly (recursive log would be bad during crash)
	HANDLE h = CreateFileA(s_logPath, FILE_APPEND_DATA, FILE_SHARE_READ,
		NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if(h == INVALID_HANDLE_VALUE) return;
	char tmp[32];
	_snprintf(tmp, sizeof(tmp), "--- LAST LOG ENTRIES ---\n");
	DWORD w; WriteFile(h, tmp, (DWORD)strlen(tmp), &w, NULL);
	for(int i = 0; i < count; i++) {
		int idx = (start + i) % RING_SIZE;
		WriteFile(h, s_ring[idx], (DWORD)strnlen(s_ring[idx], RING_MSG_LEN), &w, NULL);
		WriteFile(h, "\n", 1, &w, NULL);
	}
	_snprintf(tmp, sizeof(tmp), "--- END LOG ENTRIES ---\n");
	WriteFile(h, tmp, (DWORD)strlen(tmp), &w, NULL);
	CloseHandle(h);
}

// ============================================================
// Scope tag registry
// ============================================================
ScopeTag g_scopeTags[SCOPE_TAG_MAX];
int g_scopeTagCount = 0;
int g_scopeTagLock = 0;

void diag_registerScope(const char *name, void *addr) {
	if(g_scopeTagCount >= SCOPE_TAG_MAX) return;
	g_scopeTags[g_scopeTagCount].name = name;
	g_scopeTags[g_scopeTagCount].addr = addr;
	g_scopeTagCount++;
}

void diag_lookupScope(void *eip, char *outName, int outSize, int *outOffset) {
	if(outSize <= 0) return;	// no room: outName[outSize-1] would be outName[-1]
	outName[0] = 0;
	*outOffset = 0;
	uintptr_t addr = (uintptr_t)eip;
	int best = -1;
	int bestDist = 0x7FFFFFFF;
	for(int i = 0; i < g_scopeTagCount; i++) {
		int dist = (int)(addr - (uintptr_t)g_scopeTags[i].addr);
		if(dist >= -0x400 && dist < 0x400 && abs(dist) < bestDist) {
			bestDist = abs(dist);
			best = i;
			*outOffset = dist;
		}
	}
	if(best >= 0) {
		strncpy(outName, g_scopeTags[best].name, outSize - 1);
		outName[outSize - 1] = 0;
	}
}

// ============================================================
// FRAME CORRELATION — frame id, packed code, pass marks
// ============================================================
// API + exact bit layout: debug_handl.h "FRAME CORRELATION" section.
// The log writer below stamps every skygfx_dbg.log line with
// DBGLOG_FRAME_PREFIX_FMT (skygfx.h) via debugFrameStamp() — ONE tick
// read, ONE SEH frame and ONE config deref per FRAME, not per line.

// Stamp state (file scope so DebugInvalidatePipelineCache can reach the
// nibble latch key).  All POD — nothing here needs unwinding.
static unsigned s_stampEpoch = 0;           // accumulated 0x10000 wrap count
static unsigned s_stampLastTick = 0;        // previous raw tick (widened compare)
static unsigned s_stampCachedId = 0;        // last good id (pre-init / fault fallback)
static int s_stampEngineSeen = 0;           // latch: RW engine observed alive once
static unsigned s_stampLastFrame = 0xFFFFFFFFu; // nibble-latch key (force first derive)
static unsigned s_stampNibble = 0xF;        // latched pipeline nibble

// SEH island: the only faulting load (RwGlobals.renderFrame at +8 off a
// possibly-torn engine pointer).  noinline keeps the __try frame setup out
// of the callers' prologues; callers pre-check the pointer chain, so this
// is invoked only while the engine is live.  POD-only inside (C2712-safe).
static void __declspec(noinline)
debugReadTick(unsigned *outTick, int *outOk)
{
	__try {
		*outTick = (unsigned)RWSRCGLOBAL(renderFrame);
		*outOk = 1;
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		*outOk = 0;
	}
}

// Combined per-line stamp (hot path — one call per log line):
//   *outId   widened monotonic frame id
//   *outCode (id & 0x0FFFFFFF) | (pipeline nibble << 28)
//
// Per-line cost after the guards: one chain NULL-check (2 loads), one
// island call (only while the engine is live), wrap math, one frame-id
// compare for the nibble latch.  No SEH setup on the pre-init path, no
// per-line config deref, no per-line string work.
static void
debugFrameStamp(unsigned *outId, unsigned *outCode)
{
	unsigned fid;
	unsigned tick = 0;
	int gotTick = 0;
	// Pre-init guard: the chain is rwengine -> *rwengine
	// (RwEngineInstance) -> RwGlobals.renderFrame (+8).  Before the engine
	// opens, *rwengine is NULL, so the NULL-check short-circuits BEFORE
	// the deref — no fault, no SEH dispatch, no VEH trip.  The island only
	// backs the unexpected (torn/garbage engine pointer from a crash
	// path; dbglog runs inside the VEH too).
	//   pre-init (never seen live)   -> id 0
	//   engine gone after the latch  -> last good id
	if(rwengine && *rwengine){
		debugReadTick(&tick, &gotTick);
	}
	if(!gotTick){
		fid = s_stampEngineSeen ? s_stampCachedId : 0;
	}else{
		s_stampEngineSeen = 1;   // first successful read latches engine liveness
		// Widened RW render-frame tick: RWSRCGLOBAL(renderFrame) is a
		// RwUInt16 "camera display count" (+1 per rendered frame — the
		// same tick softbody.cpp / ragdoll_death.cpp gate on) and wraps
		// every 65536 frames; accumulate a 64K epoch so the id stays
		// monotonic and unique for the whole session.
		if(tick < s_stampLastTick) s_stampEpoch += 0x10000u;
		s_stampLastTick = tick;
		s_stampCachedId = s_stampEpoch | tick;
		fid = s_stampCachedId;
	}

	// Pipeline nibble latch: config fields change at most once per frame
	// (INI reload / pipeline switch), so derive once per frame-id advance
	// instead of dereferencing config twice per log line.  Staleness
	// window: the remainder of the frame in which a switch happened;
	// DebugInvalidatePipelineCache() forces an immediate re-derive.
	if(fid != s_stampLastFrame){
		s_stampLastFrame = fid;
		unsigned pipe = 0xF;   // 0xF = unknown pipeline (config not live yet)
		if(config)
			pipe = (unsigned)((config->pipelineOverride >= 0
				? config->pipelineOverride : config->pipeline) & 0xF);
		s_stampNibble = pipe;
	}

	*outId = fid;
	*outCode = (fid & 0x0FFFFFFFu) | (s_stampNibble << 28);
}

unsigned
DebugFrameId(void)
{
	unsigned fid, fcode;
	debugFrameStamp(&fid, &fcode);
	return fid;
}

unsigned
DebugFrameCode(void)
{
	unsigned fid, fcode;
	debugFrameStamp(&fid, &fcode);
	return fcode;
}

void
DebugInvalidatePipelineCache(void)
{
	// Next debugFrameStamp() re-derives the pipeline nibble.  Optional
	// exactness hook for the pipeline re-derive receipt (main.cpp —
	// reported hunk, not wired); without it the latch self-heals on the
	// next frame-id advance.  Cheap, callable from any lane.
	s_stampLastFrame = 0xFFFFFFFFu;
}

unsigned
DebugOverlayCode(void)
{
	return DebugFrameCode();
}

void
DebugLogFrameMark(const char *tag)
{
	// The writer adds the "[#<frameId>:<code>] " prefix -> exactly one line:
	//   [#<frameId>:<code>] -- <tag> --
	dbglog("-- %s --", tag ? tag : "(null)");
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

	// Frame correlation prefix (debug_handl.h): stamped immediately before
	// the message so every skygfx_dbg.log line carries the same
	// [#<frameId>:<code>] the overlay lane shows on screen.  ONE combined
	// stamp call per line (single tick read, SEH island, per-frame
	// pipeline latch), fixed buffers only.
	unsigned fid, fcode;
	debugFrameStamp(&fid, &fcode);

	char buf[4096];
	SYSTEMTIME st;
	GetLocalTime(&st);
	int hlen;
	// Clamp before the table index: dbglog_loc takes a plain int level, and
	// s_logLevelStr has exactly 4 entries (LOG_INFO..LOG_FATAL). Anything
	// above 3 is a caller bug — degrade to FATAL instead of reading past
	// the table. (level < 0 already takes the TRACE branch below.)
	if(level > LOG_FATAL) level = LOG_FATAL;
	if(level >= 0)
		hlen = snprintf(buf, sizeof(buf), "[%02d:%02d:%02d.%03d] [%s] %s:%d %s() - " DBGLOG_FRAME_PREFIX_FMT "%s\n",
			st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
			s_logLevelStr[level], shortFile, line, func, fid, fcode, msg);
	else
		hlen = snprintf(buf, sizeof(buf), "[%02d:%02d:%02d.%03d] [TRACE] %s:%d %s() - " DBGLOG_FRAME_PREFIX_FMT "%s\n",
			st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
			shortFile, line, func, fid, fcode, msg);
	if(hlen < 0) return;
	// UCRT snprintf NUL-terminates on truncation and returns the would-be
	// length: clamp and emit the truncated line instead of dropping it.
	// No overflow is possible — snprintf is bounded by sizeof(buf).
	if(hlen >= (int)sizeof(buf)) hlen = (int)sizeof(buf) - 1;

	// Also push to ring buffer (truncated to fit) — carries the same frame
	// prefix so the crash-time ring dump stays correlatable.
	char ringMsg[RING_MSG_LEN];
	_snprintf(ringMsg, RING_MSG_LEN, DBGLOG_FRAME_PREFIX_FMT "%s", fid, fcode, msg);
	// MSVC _snprintf does NOT NUL-terminate when the output reaches the
	// count; without this, diag_ringPut's strncpy(slot, ringMsg, 159) reads
	// past this 160-byte stack buffer until a stray NUL (stack overread +
	// garbage ring entries on any line longer than ~159 chars).
	ringMsg[RING_MSG_LEN - 1] = 0;
	diag_ringPut(ringMsg);

	HANDLE h = CreateFileA(s_logPath, FILE_APPEND_DATA, FILE_SHARE_READ,
		NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if(h == INVALID_HANDLE_VALUE) return;
	DWORD written;
	WriteFile(h, buf, hlen, &written, NULL);
	// Flush throttle: WriteFile data survives process death (it lands in
	// the kernel file cache, not our address space), so the per-line
	// FlushFileBuffers was pure power-loss insurance at one forced
	// write-through per line — a measurable stall at ~100 lines/s on a
	// spinning drive.  Flush at most once per second; crash evidence is
	// covered by the fatal path's ring dump + receipt regardless.
	static DWORD s_lastFlushMs = 0;
	DWORD nowMs = GetTickCount();
	if(nowMs - s_lastFlushMs >= 1000){
		s_lastFlushMs = nowMs;
		FlushFileBuffers(h);
	}
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
	va_start(ap, level);
	dbglog_internal(level, file, line, func, fmt, ap);
	va_end(ap);
}

// ============================================================
// VEH crash handler — matches original skygfx crashHandler
// Logs crashes, auto-fixes known issues, suppresses rendering crashes
// ============================================================

static LONG WINAPI
diag_crashHandler(EXCEPTION_POINTERS *ep)
{
	DWORD code = ep->ExceptionRecord->ExceptionCode;

	// Let C++ exceptions (0xE06D7363) pass through to try/catch blocks in readint/readfloat
	// Swallowing them with EXCEPTION_EXECUTE_HANDLER corrupts C++ exception handling
	if(code == 0xE06D7363)
		return EXCEPTION_CONTINUE_SEARCH;

	// DBG_PRINTEXCEPTION_C (0x40010006) — the benign OutputDebugString
	// first-chance. Never a real crash: logging it burned the 20-trip
	// crashCount and dumped ~680 junk ring-buffer lines per trip. Ignore
	// silently; disposition is unchanged (EXCEPTION_CONTINUE_SEARCH — the
	// exception still reaches whoever owns it, other codes unaffected).
	if(code == 0x40010006)
		return EXCEPTION_CONTINUE_SEARCH;

	// Inside SEH-guarded Im2D dispatches the VEH must be a pure pass-through.
	// Any logging here does file I/O on the faulting thread and derails
	// dispatch before frame-based __try/__except handlers can run.
	// Forensics first: push a bounded ring entry (no file I/O, no malloc) so a
	// later hard-crash ring dump carries the guarded-phase context. Behavior
	// past this point (EXCEPTION_CONTINUE_SEARCH) is unchanged.
	if(g_inGuardedIm2DPass){
		char gmsg[RING_MSG_LEN];
		_snprintf(gmsg, sizeof(gmsg), "GUARD-FAULT eip=%08X phase=%s depth=%ld",
			ep->ContextRecord->Eip,
			g_renderPhase ? g_renderPhase : "",
			(long)g_guardDepth);
		gmsg[sizeof(gmsg) - 1] = 0;
		diag_ringPut(gmsg);
		return EXCEPTION_CONTINUE_SEARCH;
	}

	static int crashCount = 0;
	if(++crashCount > 20) return EXCEPTION_CONTINUE_SEARCH;

	CONTEXT *ctx = ep->ContextRecord;
	DWORD eip = ctx->Eip;
	DWORD esp = ctx->Esp;

	// Determine crash context from EIP range
	const char *crashZone = "UNKNOWN";
	if(eip >= 0x400000 && eip < 0x800000) crashZone = "GAME_TEXT";
	else if(eip >= 0x10000000) crashZone = "HEAP/DLL";
	else if(eip < 0x10000) crashZone = "NULL_DEREF";

	// Scope tag lookup: nearest skygfx function (cheap, no I/O)
	char scopeName[128];
	int scopeOff;
	diag_lookupScope((void*)(uintptr_t)eip, scopeName, sizeof(scopeName), &scopeOff);

	// ONE line per VEH catch.  This handler also fires for RECOVERED
	// first-chance faults (SEH __except guards elsewhere — SMAA passes,
	// guardedSetRT, the frame-id reads), so the old 5-line receipt plus a
	// per-catch diag_ringDump() re-emitted every recent line bare on each
	// catch (~215 boot-log lines for 8 recovered NULL+8 reads), and each
	// receipt logged through dbglog re-faulted recursively while the
	// frame-id read was unguarded.  Regs/scope are folded inline; the
	// greppable CRASH[N] token and the zone/type/access fields survive.
	// The ring dump belongs to the fatal path only (crashUnhandledFilter).
	if(code == 0xC0000005){
		DWORD accessType = ep->ExceptionRecord->ExceptionInformation[0];
		DWORD accessAddr = ep->ExceptionRecord->ExceptionInformation[1];
		const char *crashType =
			accessType == 0 ? "READ" :
			accessType == 1 ? "WRITE" :
			accessType == 8 ? "DEP" : "ACCESS_VIOLATION";
		dbglog("CRASH[%d]: %s at 0x%08X (zone=%s) accessing 0x%08X%s EAX=%08X EBX=%08X ECX=%08X EDX=%08X ESI=%08X EDI=%08X EBP=%08X ESP=%08X scope=%s(+0x%X)",
			crashCount, crashType, eip, crashZone, accessAddr,
			accessAddr < 0x1000 ? " NULLDEREF" : "",
			ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx, ctx->Esi, ctx->Edi,
			ctx->Ebp, esp,
			scopeName[0] ? scopeName : "(none)", scopeOff);
	} else {
		dbglog("CRASH[%d]: code=0x%08X at 0x%08X (zone=%s) EAX=%08X EBX=%08X ECX=%08X EDX=%08X ESI=%08X EDI=%08X EBP=%08X ESP=%08X scope=%s(+0x%X)",
			crashCount, code, eip, crashZone,
			ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx, ctx->Esi, ctx->Edi,
			ctx->Ebp, esp,
			scopeName[0] ? scopeName : "(none)", scopeOff);
	}

	// NO per-catch diag_ringDump() here — recovered catches must not
	// re-emit the ring (that was the "bare duplicate" noise).  The fatal
	// path owns the ring dump now.

	// CLASS 5: Removed EIP-skip auto-fixes — they silently mask corruption.
	// Our diagnostics now pinpoint the crash with ring buffer + scope tags.
	// Let real crashes surface so root causes are visible.

	// During InitialiseGame call, let SEH handlers fire to get exact crash info
	if(g_allowCrashPassThrough)
		return EXCEPTION_CONTINUE_SEARCH;

	// CLASS 5: Removed blanket EXCEPTION_EXECUTE_HANDLER for 0xC0000005.
	// Previously swallowed all access violations, hiding real bugs.
	// Now we let the crash propagate to the OS for full diagnostics.
	return EXCEPTION_CONTINUE_SEARCH;
}

// ============================================================
// Watchdog — detects freezes (no crash, just hangs)
// ============================================================
static volatile LONG s_heartbeat = 0;

void diag_heartbeat(void)
{
	InterlockedExchange(&s_heartbeat, GetTickCount());
}

// ============================================================
// Public API
// ============================================================
static LONG_PTR s_oldVEH = 0;
volatile LONG g_inGuardedIm2DPass = 0;
const char *g_renderPhase = "";

void
diag_init(const char *logPath)
{
	if(logPath)
		strncpy(s_logPath, logPath, MAX_PATH - 1);
	s_logPath[MAX_PATH - 1] = '\0';
	s_logInit = 1;

	// FIX-5 (log-evidence destruction): this used to open the log with
	// TRUNCATE_EXISTING straight away, wiping every prior test boot's
	// evidence (the PBR black-interior session's log was lost this way).
	// Rotate instead: move the existing log to <name>.prev.log (replacing
	// the previous rotation, so it stays one boot behind and never
	// accumulates), then create the fresh one below. Failure is harmless —
	// an absent/locked source just means nothing to rotate.
	if(s_logPath[0]){
		char prevPath[MAX_PATH];
		strncpy(prevPath, s_logPath, MAX_PATH - 1);
		prevPath[MAX_PATH - 1] = '\0';
		// "<dir>/skygfx_dbg.log" -> "<dir>/skygfx_dbg.prev.log"; strip only
		// the FILENAME's extension (dots in the directory must survive).
		char *slash = strrchr(prevPath, '\\');
		if(!slash) slash = strrchr(prevPath, '/');
		char *dot = strrchr(prevPath, '.');
		if(dot && (!slash || dot > slash))
			*dot = '\0';
		strncat(prevPath, ".prev.log", sizeof(prevPath) - strlen(prevPath) - 1);
		if(!MoveFileExA(s_logPath, prevPath, MOVEFILE_REPLACE_EXISTING)){
			// Rename failed (locked by another reader?) — fall back to a
			// copy so the old evidence survives the truncate below.
			CopyFileA(s_logPath, prevPath, FALSE);
		}
	}

	// Create (now empty) log for this boot
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

// ===========================================================================
// SELF-INSTALLING UNHANDLED-EXCEPTION FILTER + MINIDUMP  (was crashhandler.cpp)
// ===========================================================================
// crashhandler.cpp — self-installing unhandled-exception filter: minidump on every hard crash.
//
// Scope: NEW standalone file. Zero shared-file edits, zero build-system lib edits
// (dbghelp is linked via #pragma comment below; registration is one ClCompile
// line in build/skygfx.vcxproj — the effective source list, since fast_build.py
// runs MSBuild directly and never regenerates the project from premake5.lua's globs).
//
// Install: a namespace-scope static object whose constructor runs at CRT static
// init (DLL load, before user DllMain) calls SetUnhandledExceptionFilter and
// saves the previous filter.
//
// Chaining convention (stated per spec 2c): we do our work FIRST (log line +
// minidump), THEN call the previous filter if non-null and RETURN ITS RESULT —
// its behavior is preferred so nothing it does is silently dropped. If there is
// no previous filter, we return EXCEPTION_EXECUTE_HANDLER.
//
// Why SetUnhandledExceptionFilter and NOT AddVectoredExceptionHandler: the
// codebase has expected/caught first-chance AVs inside SEH guards
// (guardedSetRT in pipelinecommon.cpp, SMAA passes in postfx.cpp). A VEH would
// see those and produce bogus dumps. The unhandled filter only runs when no
// other handler claimed the exception — exactly the "hard crash" set. (The
// separate diagnostic VEH in diagnostics.cpp stays untouched and is
// forensics-only.) terminate/new handlers: N/A per spec — ignored.
//
// Re-entrancy: static LONG claimed via InterlockedCompareExchange as the VERY
// FIRST act of the handler — no allocation, no file I/O before the claim.
// Recursive faults during handling skip the work and fall straight through.
// Each best-effort step is additionally wrapped in its own __try/__except so
// a fault in the log append still lets the dump happen, and nothing ever
// raises out of the filter.
namespace {

// ------------------------------------------------------------
// Shared state
// ------------------------------------------------------------

// Re-entrancy guard: 0 = free, 1 = handler running (or ran).
volatile LONG s_crashHandling = 0;

// Previous unhandled filter, saved at install time; chained after our work.
LPTOP_LEVEL_EXCEPTION_FILTER s_prevFilter = NULL;

// ------------------------------------------------------------
// Path helpers (best-effort; return 0 on failure)
// ------------------------------------------------------------

// Directory of the module containing this code (skygfx.asi) WITH trailing
// backslash — matches how main.cpp derives skygfx_dbg.log (dll dir + name).
// Falls back to the exe dir if the module can't be resolved.
static DWORD
selfDir(char *buf, DWORD cap)
{
	HMODULE mod = NULL;
	DWORD n = 0;
	if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)(uintptr_t)&selfDir, &mod) && mod)
		n = GetModuleFileNameA(mod, buf, cap);
	if(n == 0 || n >= cap)
		n = GetModuleFileNameA(NULL, buf, cap);
	if(n == 0 || n >= cap)
		return 0;
	char *slash = strrchr(buf, '\\');
	if(!slash)
		return 0;
	slash[1] = '\0';
	return 1;
}

// Directory of gta_sa.exe WITH trailing backslash (spec: resolve dump dir
// and WER DumpFolder from GetModuleFileNameA(NULL)).
static DWORD
exeDir(char *buf, DWORD cap)
{
	DWORD n = GetModuleFileNameA(NULL, buf, cap);
	if(n == 0 || n >= cap)
		return 0;
	char *slash = strrchr(buf, '\\');
	if(!slash)
		return 0;
	slash[1] = '\0';
	return 1;
}

// ------------------------------------------------------------
// (a) Append one crash line to skygfx_dbg.log
// ------------------------------------------------------------

// Line format (single line, append semantics — the main app truncates this
// log every launch via diag_init, so we open it fresh with "a" behavior):
//   [YYYY-MM-DD HH:MM:SS.mmm] [#<frameId>:<code>] [CRASH] code=0x%08X addr=0x%08X
//   [YYYY-MM-DD HH:MM:SS.mmm] [#<frameId>:<code>] [CRASH] code=0x%08X addr=0x%08X op=N(READ|WRITE|DEP|OTHER) fault=0x%08X
// where addr = ContextRecord->Eip (ExceptionAddress), and for AV the two
// ExceptionInformation entries: [0] 0=read/1=write/8=DEP, [1] = fault address
// (the recon ask: the old dialog crash faulted at 0x39D1B71B — now logged).
// The frame prefix matches dbglog_internal's DBGLOG_FRAME_PREFIX_FMT so
// crash lines correlate with the overlay-lane code like every other line.
static void
crashAppendLog(EXCEPTION_POINTERS *ep)
{
	char path[MAX_PATH];
	if(!selfDir(path, sizeof(path)))
		return;
	strncat(path, "skygfx_dbg.log", sizeof(path) - strlen(path) - 1);

	SYSTEMTIME st;
	GetLocalTime(&st);

	DWORD code = 0;
	DWORD addr = 0;
	if(ep && ep->ExceptionRecord)
		code = ep->ExceptionRecord->ExceptionCode;
	if(ep && ep->ContextRecord)
		addr = ep->ContextRecord->Eip;   // x86 ExceptionAddress
	else if(ep && ep->ExceptionRecord)
		addr = (DWORD)(uintptr_t)ep->ExceptionRecord->ExceptionAddress;

	unsigned fid = DebugFrameId();
	unsigned fcode = DebugFrameCode();
	char line[320];
	int n;
	if(code == EXCEPTION_ACCESS_VIOLATION && ep && ep->ExceptionRecord &&
	   ep->ExceptionRecord->NumberParameters >= 2)
	{
		DWORD op = (DWORD)ep->ExceptionRecord->ExceptionInformation[0];
		DWORD fault = (DWORD)ep->ExceptionRecord->ExceptionInformation[1];
		const char *opStr =
			op == 0 ? "READ" :
			op == 1 ? "WRITE" :
			op == 8 ? "DEP" : "OTHER";
		n = snprintf(line, sizeof(line),
			"[%04u-%02u-%02u %02u:%02u:%02u.%03u] " DBGLOG_FRAME_PREFIX_FMT "[CRASH] code=0x%08X addr=0x%08X op=%u(%s) fault=0x%08X\n",
			(unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
			(unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
			(unsigned)st.wMilliseconds,
			fid, fcode,
			code, addr, op, opStr, fault);
	}else{
		n = snprintf(line, sizeof(line),
			"[%04u-%02u-%02u %02u:%02u:%02u.%03u] " DBGLOG_FRAME_PREFIX_FMT "[CRASH] code=0x%08X addr=0x%08X\n",
			(unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
			(unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
			(unsigned)st.wMilliseconds,
			fid, fcode,
			code, addr);
	}
	if(n <= 0)
		return;
	if(n >= (int)sizeof(line))
		n = (int)sizeof(line) - 1;   // never read past the buffer

	// "a" semantics: append regardless of current EOF; independent handle.
	// The app's own writer opens/closes per message, so no long-lived lock.
	HANDLE h = CreateFileA(path, FILE_APPEND_DATA,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if(h == INVALID_HANDLE_VALUE)
		return;
	DWORD written = 0;
	WriteFile(h, line, (DWORD)n, &written, NULL);
	CloseHandle(h);
}

// ------------------------------------------------------------
// (b) Minidump: <exe dir>\skygfx_crash_YYYY-MM-DD_HHMMSS.dmp
// ------------------------------------------------------------

static void
crashWriteDump(EXCEPTION_POINTERS *ep)
{
	char dir[MAX_PATH];
	if(!exeDir(dir, sizeof(dir)))
		return;

	SYSTEMTIME st;
	GetLocalTime(&st);
	char path[MAX_PATH];
	snprintf(path, sizeof(path),
		"%sskygfx_crash_%04u-%02u-%02u_%02u%02u%02u.dmp",
		dir,
		(unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
		(unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);

	HANDLE hFile = CreateFileA(path, GENERIC_WRITE, 0, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hFile == INVALID_HANDLE_VALUE)
		return;   // best-effort: fall through, never raise

	MINIDUMP_EXCEPTION_INFORMATION mei;
	MINIDUMP_EXCEPTION_INFORMATION *pMei = NULL;
	if(ep){
		mei.ThreadId = GetCurrentThreadId();
		mei.ExceptionPointers = ep;
		mei.ClientPointers = FALSE;
		pMei = &mei;
	}

	MINIDUMP_TYPE type = (MINIDUMP_TYPE)(
		MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo);

	BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
		hFile, type, pMei, NULL, NULL);
	DWORD size = GetFileSize(hFile, NULL);

	if(!ok || size == 0 || size == INVALID_FILE_SIZE){
		// Retry once with MiniDumpNormal.
		CloseHandle(hFile);
		hFile = CreateFileA(path, GENERIC_WRITE, 0, NULL,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if(hFile == INVALID_HANDLE_VALUE)
			return;
		MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
			hFile, MiniDumpNormal, pMei, NULL, NULL);
	}
	CloseHandle(hFile);
}

// ------------------------------------------------------------
// The unhandled filter
// ------------------------------------------------------------

static LONG WINAPI
crashUnhandledFilter(EXCEPTION_POINTERS *ep)
{
	// 1. Claim the re-entrancy flag BEFORE anything else (no alloc, no I/O).
	if(InterlockedCompareExchange(&s_crashHandling, 1, 0) != 0)
		return EXCEPTION_EXECUTE_HANDLER;   // recursive fault: don't redo work

	// 2. (a) log line — own SEH scope so a fault here still lets the dump run.
	__try {
		crashAppendLog(ep);
	} __except(EXCEPTION_EXECUTE_HANDLER) {
	}

	// 2.5 Ring dump — FATAL path only.  Moved out of the per-catch VEH
	// receipt, where it re-emitted every recent ring line bare on each
	// recovered first-chance catch.  Own SEH scope like the steps above.
	__try {
		diag_ringDump();
	} __except(EXCEPTION_EXECUTE_HANDLER) {
	}

	// 3. (b) minidump — own SEH scope; failure/0-byte retry handled
	// internally; nothing raises out.
	__try {
		crashWriteDump(ep);
	} __except(EXCEPTION_EXECUTE_HANDLER) {
	}

	// 4. Chain: previous filter runs AFTER our work; prefer its behavior.
	if(s_prevFilter)
		return s_prevFilter(ep);
	return EXCEPTION_EXECUTE_HANDLER;
}

// ------------------------------------------------------------
// (4) Best-effort WER LocalDumps backup — silent failure (likely no admin)
// ------------------------------------------------------------

static void
crashInstallWerBackup(void)
{
	char dir[MAX_PATH];
	if(!exeDir(dir, sizeof(dir)))
		return;
	size_t len = strlen(dir);
	if(len > 0 && dir[len - 1] == '\\'){
		dir[len - 1] = '\0';   // DumpFolder without trailing slash
		len--;
	}

	HKEY hKey = NULL;
	LONG rc = RegCreateKeyExA(HKEY_LOCAL_MACHINE,
		"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\gta_sa.exe",
		0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
	if(rc != ERROR_SUCCESS)
		return;   // silent: no admin rights, etc. — never abort

	RegSetValueExA(hKey, "DumpFolder", 0, REG_EXPAND_SZ,
		(const BYTE *)dir, (DWORD)(len + 1));   // +1 = NUL
	DWORD one = 1;
	RegSetValueExA(hKey, "DumpType", 0, REG_DWORD, (const BYTE *)&one, sizeof(one));
	RegSetValueExA(hKey, "DumpMonitored", 0, REG_DWORD, (const BYTE *)&one, sizeof(one));
	RegCloseKey(hKey);
}

// ------------------------------------------------------------
// Self-installer: CRT static init runs this at DLL load
// ------------------------------------------------------------

struct CrashHandlerInstaller {
	CrashHandlerInstaller();
};

CrashHandlerInstaller::CrashHandlerInstaller()
{
	// Save + install; chaining order documented at top of file (we run first,
	// previous filter runs after us and its return value wins).
	s_prevFilter = SetUnhandledExceptionFilter(crashUnhandledFilter);

	// WER backup for the case our own dump write fails (disk full, etc.).
	crashInstallWerBackup();
}

// Namespace-scope static: constructor executes during CRT initialization of
// this DLL — no hook in main.cpp required.
CrashHandlerInstaller s_crashInstaller;

} // anonymous namespace

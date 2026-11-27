// log.cpp — L0 FOUNDATION LAYER: logging, ring buffer, frame correlation,
// log-file lifecycle.
//
// m0272 layer split: this file is the successor of the logging half of
// src/core/debug_handl.cpp, which is now DELETED — every block it held has
// an explicit layer home:
//   * log path / g_diagLevel / ring buffer / frame correlation / dbglog /
//     watchdog heartbeat / diag_init  ->  HERE (L0)
//   * scope tag registry              ->  src/core/diagnostics.cpp (L2)
//   * crash handling (VEH, forensics, dumps, WER, spec-pool guard,
//     guarded sandbox chokepoints)    ->  src/core/crashhandler.cpp (L1)
// The public API re-exports from src/core/debug_handl.h (the facade), so no
// consumer include or call site changed.
//
// L0 CONTRACT: nothing below this file's layer — log.cpp must not reference
// a crashhandler or diagnostics symbol (it does not: grep before adding
// one), and the ring buffer's internals (s_ring) stay static here; L1 packs
// bytes into a slot of width RING_MSG_LEN and never sees the array.
//
// Provenance: the diagnostics.cpp/h -> debug_handl merge of 2026-10-26, and
// the crash de-merge (m0271) that first pulled the VEH back out.  Bodies
// below are byte-identical to the blocks they came from; only this header
// and three stale comment pointers into debug_handl.{cpp,h} were touched.
// ---- Includes (logging half of the old deduped union) ----
#include "skygfx.h"
#include "log.h"
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

// ===========================================================================
// LOGGING, DEBUG OUTPUT, RING BUFFER, WATCHDOG  (was diagnostics.cpp)
// ===========================================================================
// diagnostics.cpp — Crash logging, debug output, exception handling.
// Separate from main.cpp so it can be ripped out for release builds.
//
// This file owns:
//   - dbglog / dbglog_loc (debug file logging)
//   - Log file initialization and path management
//   - Log ring buffer (last N messages) for crash dump context
//   - frame correlation stamps (DebugFrameId/Code, debugFrameStamp)
// (The scope tag registry moved to diagnostics.cpp at m0272; the VEH
// crash handler, forensics and minidump sections moved to
// crashhandler.cpp at the m0271 de-merge.  Both sets of decls stay
// reachable through the facade, debug_handl.h.)
// ============================================================
// Log file path
// ============================================================
static char s_logPath[MAX_PATH];
static int  s_logInit = 0;

const char* diag_getLogPath(void) { return s_logPath; }

// Diagnostic verbosity latch (decl + docs: log.h).  Default 0 =
// current behavior; nothing changes until a lane or config raises it.
int g_diagLevel = 0;

// ============================================================
// Log ring buffer — lock-free, single writer (main thread)
// ============================================================
// RING_MSG_LEN promoted to debug_handl.h (m0271 de-merge: the VEH's
// GUARD-FAULT receipt lives in crashhandler.cpp now); RING_SIZE stays local.
#define RING_SIZE 32
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
// FRAME CORRELATION — frame id, packed code, pass marks
// ============================================================
// API + exact bit layout: log.h "FRAME CORRELATION" section.
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
// SUPPLEMENTARY PER-MODULE LOG FILES  (m2397)
// ============================================================
// Additive split: when a line's MESSAGE opens with a routed "[Tag]" token,
// the SAME composed line (timestamp + level + file:line + frame stamp) is
// also written to skygfx_<module>.log.  This path only ever ADDS a second
// write — skygfx_dbg.log keeps byte-for-byte the content it had before this
// feature (main log unchanged, module file gets a copy).
//
// Table-driven, default ON.  L0 reads no config, so the key the config lane
// may wire later is [SkyGfx] debugSplitLogs (documented in log.h).  Route an
// extra tag by adding ONE row to s_modTags; a new file needs one row there
// plus one row in s_modLogs.
struct ModLogTag   { const char *tag; unsigned char mod; }; // tag = inside "[ ]"; trailing '*' = prefix
struct ModLogFile  { const char *suffix; HANDLE h; };       // h lazy-opened, closed at rotate

static ModLogFile s_modLogs[] = {        // index == ModLogTag::mod
	{ "clouds", NULL },                   // 0 -> skygfx_clouds.log
	{ "corona", NULL },                   // 1 -> skygfx_corona.log
	{ "water",  NULL },                   // 2 -> skygfx_water.log
	{ "tex",    NULL },                   // 3 -> skygfx_tex.log
	{ "postfx", NULL },                   // 4 -> skygfx_postfx.log
	{ "pipe",   NULL },                   // 5 -> skygfx_pipe.log
};
#define MODLOG_COUNT (sizeof(s_modLogs) / sizeof(s_modLogs[0]))

static const ModLogTag s_modTags[] = {   // leading "[Tag]" of the message -> file
	{ "cloud*",     0 }, { "CloudShield", 0 },                   // clouds
	{ "PtLight*",   1 }, { "Corona*",     1 }, { "sprite_*", 1 },// corona
	{ "water*",     2 },                                         // water
	{ "texdefs",    3 }, { "texalpha",    3 },                   // tex
	{ "SSAO",       4 }, { "PF",          4 }, { "PostFX", 4 },  // postfx
	{ "BPIPE",      5 }, { "Bld*",        5 },                   // pipe
	{ "VPIPE",      5 }, { "Veh*",        5 },
};
#define MODLOG_TAG_COUNT (sizeof(s_modTags) / sizeof(s_modTags[0]))

// "<dir><base>_dbg.log" -> "<dir><base>_<suffix>.log": derived from the main
// log so module files always land in the SAME directory and follow a renamed
// module.  Empty out[] on overflow (caller skips).
static void
modLogPath(char *out, const char *suffix)
{
	if(!s_logPath[0]){ out[0] = '\0'; return; }   // pre-diag_init: nothing to derive from
	strncpy(out, s_logPath, MAX_PATH - 1);
	out[MAX_PATH - 1] = '\0';
	size_t n = strlen(out);
	if(n > 4 && _stricmp(out + n - 4, ".log") == 0){ out[n - 4] = '\0'; n -= 4; } // skygfx_dbg
	if(n > 4 && _stricmp(out + n - 4, "_dbg") == 0){ out[n - 4] = '\0'; n -= 4; } // skygfx
	if(n + strlen(suffix) + 6 > MAX_PATH){ out[0] = '\0'; return; }               // '_'+sfx+".log"+NUL
	strcat(out, "_"); strcat(out, suffix); strcat(out, ".log");
}

// Leading "[Tag]" token of msg -> s_modLogs index, or -1 (main log only).
// Case-insensitive; a '*' pattern matches by prefix, an exact pattern must
// match the whole tag.  Bounded: no scan past 40 tag chars, no scan past the
// first ']'.
static int
modLogRoute(const char *msg)
{
	if(!msg || msg[0] != '[') return -1;
	// Bounded scan for the closing bracket: at most 40 chars, so a message
	// that opens '[' and never closes cannot turn into a full-string walk.
	size_t k;
	for(k = 1; k <= 40 && msg[k] != '\0'; k++)
		if(msg[k] == ']') break;
	if(k > 40 || msg[k] != ']') return -1;
	size_t tagLen = k - 1;
	if(tagLen == 0) return -1;
	for(size_t i = 0; i < MODLOG_TAG_COUNT; i++){
		const char *pat = s_modTags[i].tag;
		size_t patLen = strlen(pat);
		if(patLen == 0) continue;
		if(pat[patLen - 1] == '*'){
			if(tagLen >= patLen - 1 && _strnicmp(msg + 1, pat, (int)(patLen - 1)) == 0)
				return s_modTags[i].mod;
		}else if(tagLen == patLen && _strnicmp(msg + 1, pat, (int)patLen) == 0){
			return s_modTags[i].mod;
		}
	}
	return -1;
}

// Append `line` to the module file for msg's tag.  Lazy open on first match
// (same CreateFileA idiom as the main writer); the handle stays open until
// diag_init rotates the file, so a routed line costs ONE WriteFile — no extra
// open/close beyond what the main logger already does per line.
static void
modLogWrite(const char *msg, const char *line, int lineLen)
{
	if(!s_logPath[0]) return;                 // pre-diag_init: no path to derive from
	int mod = modLogRoute(msg);
	if(mod < 0 || (size_t)mod >= MODLOG_COUNT) return;   // table row out of range = no file
	ModLogFile *f = &s_modLogs[mod];
	if(!f->h){
		char path[MAX_PATH];
		modLogPath(path, f->suffix);
		if(!path[0]) return;
		f->h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ,
			NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if(f->h == INVALID_HANDLE_VALUE){ f->h = NULL; return; }
	}
	DWORD written;
	WriteFile(f->h, line, (DWORD)lineLen, &written, NULL);
	// Same <=1 Hz flush throttle as the main writer: WriteFile data survives
	// process death (kernel file cache), so per-line flush is pure stall.
	static DWORD s_lastFlushMs = 0;
	DWORD nowMs = GetTickCount();
	if(nowMs - s_lastFlushMs >= 1000){
		s_lastFlushMs = nowMs;
		FlushFileBuffers(f->h);
	}
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

	// Frame correlation prefix (log.h): stamped immediately before
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

	// Supplementary per-module copy (m2397): routed ONLY by the message's
	// leading "[Tag]", written AFTER the main line above — the main log is
	// already committed either way, so this path can never cost it a line.
	modLogWrite(msg, buf, hlen);
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

	// m2397: rotate the supplementary module logs exactly like the main log
	// (skygfx_<mod>.log -> skygfx_<mod>.prev.log, one boot behind), and drop
	// any handle first so MoveFileEx never sees one of ours open.  Normally
	// none IS open — this runs once from DllMain, before the first dbglog —
	// so the close is belt-and-braces.  The fresh file is NOT created here:
	// a module log opens lazily on its first routed line (modLogWrite).
	for(size_t i = 0; i < MODLOG_COUNT; i++){
		ModLogFile *f = &s_modLogs[i];
		if(f->h){ CloseHandle(f->h); f->h = NULL; }
		char cur[MAX_PATH];
		modLogPath(cur, f->suffix);
		if(!cur[0]) continue;
		char modPrev[MAX_PATH];
		strncpy(modPrev, cur, MAX_PATH - 1);
		modPrev[MAX_PATH - 1] = '\0';
		size_t mlen = strlen(modPrev);
		if(mlen > 4 && _stricmp(modPrev + mlen - 4, ".log") == 0)
			modPrev[mlen - 4] = '\0';
		strncat(modPrev, ".prev.log", sizeof(modPrev) - strlen(modPrev) - 1);
		if(!MoveFileExA(cur, modPrev, MOVEFILE_REPLACE_EXISTING))
			CopyFileA(cur, modPrev, FALSE);   // absent/locked source: harmless, same as main
	}

	// Create (now empty) log for this boot
	HANDLE hLog = CreateFileA(s_logPath, GENERIC_WRITE, FILE_SHARE_READ,
		NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);

	// Session-start banner (log dissection).  The 64-bit FILETIME id is
	// monotonic across boots (100 ns ticks since 1601) and makes multiple
	// captured logs trivially orderable/distinguishable; pid + compiler
	// date/time pin the exact build even if a later [BUILD] fingerprint line
	// is missing (early failure).  One line, logged once per process.
	{
		FILETIME ft = {};
		GetSystemTimeAsFileTime(&ft);
		dbglog("=== SESSION-START === id=%08X%08X pid=%u built=%s %s",
			(unsigned)ft.dwHighDateTime, (unsigned)ft.dwLowDateTime,
			(unsigned)GetCurrentProcessId(), __DATE__, __TIME__);
	}
}

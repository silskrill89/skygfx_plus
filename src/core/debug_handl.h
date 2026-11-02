// debug_handl.h - consolidated public API for src/core/debug_handl.cpp.
//
// Merged 2026-10-26 from src/core/diagnostics.cpp + src/core/menu_inject.cpp
// + src/core/crashhandler.cpp.  Provenance of the three original headers:
//   - src/core/diagnostics.h  (logging / VEH / watchdog API)
//   - src/core/menu_inject.h  (CMenuManager injection)
//   - src/core/crashhandler.cpp (had no header: self-installing filter)
//
// Section order (log -> ring -> scope -> dbglog -> crash VEH -> watchdog ->
// crashhandler -> menu) is preserved and matters: the crashhandler static
// installer must run after the diag_* statics are initialised.

#pragma once

#include <windows.h>

// ---- LOGGING (was diagnostics.h) ----

// diagnostics.h — Crash logging, debug output, exception handling.
// Separate from main.cpp so it can be ripped out for release builds.
//
// To disable all diagnostics: #define DIAGNOSTICS_DISABLED before including this header,
// or add DIAGNOSTICS_DISABLED to the project preprocessor definitions.

#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H
#ifdef DIAGNOSTICS_DISABLED

// Stubs — all diagnostic calls become no-ops
#define diag_init(logPath)       ((void)0)
#define diag_shutdown()          ((void)0)
#define diag_getLogPath()        ((const char*)"")
#define diag_installVEH()       ((void)0)
#define diag_removeVEH()        ((void)0)
#define diag_heartbeat()        ((void)0)
#define diag_checkCrashMarker() ((void)0)

#else

// Initialize diagnostics: sets up log path, truncates old log.
// logPath should be the full path to the log file.
void diag_init(const char *logPath);

// Install the VEH crash handler (diagnostic-only, never suppresses).
void diag_installVEH(void);

// Remove the VEH crash handler.
void diag_removeVEH(void);

// Guard flag: set while inside SEH-guarded Im2D dispatches (SMAA passes).
// Lets the VEH pass through untouched so frame SEH containment can fire.
extern volatile LONG g_inGuardedIm2DPass;

// Current render-phase tag for the guarded choke-points (forensics only).
// Set at each SMAA pass boundary, cleared whenever the hoisted guard exits.
// The VEH reads it to tag GUARD-FAULT ring entries. Never NULL.
extern const char *g_renderPhase;

// Get the current log file path (for external code that needs it).
const char* diag_getLogPath(void);

// Call from heartbeat points to tell the watchdog the game is alive.
void diag_heartbeat(void);

// Check for crash marker from previous session — show dialog if found.
void diag_checkCrashMarker(void);

#endif // DIAGNOSTICS_DISABLED

// Guard flag: set by InitialiseGame_hook to let SEH handlers fire during
// CGame::Initialise (defined in main.cpp).
extern volatile int g_allowCrashPassThrough;

// Scope tag registry count (defined in debug_handl.cpp).
extern int g_scopeTagCount;

#endif // DIAGNOSTICS_H

// ---- FRAME CORRELATION (frame id + packed overlay code) ----
//
// Correlates a value printed on-screen (overlay lane) with the log lines of
// the same rendered frame.  Every skygfx_dbg.log line is prefixed by the log
// writer (dbglog_internal, debug_handl.cpp) with "#<frameId>:<code>" in
// square brackets immediately before the message text, e.g.
//
//     [HH:MM:SS.mmm] [INFO ] :0 () - [#12345:3000ABCD] message
//
// and the on-screen overlay lane prints the same packed code via
// DebugOverlayCode(), so one displayed number reconstructs frame -> log and
// lets systems be branched out per frame.
//
// DebugFrameId() — monotonically increasing per-frame id.  Source: the RW
//   render-frame tick RWSRCGLOBAL(renderFrame) (RwUInt16 "camera display
//   count", +1 per rendered frame — the same tick softbody.cpp /
//   ragdoll_death.cpp gate on), widened to 32 bits by wrap detection: each
//   time the 16-bit tick wraps, the accumulated epoch gains 0x10000, so the
//   id is unique per frame for the whole session.  Cheap: statics only, no
//   allocation; the rwengine -> RwEngineInstance pointer chain is
//   NULL-checked before every read — pre-init returns 0 with no fault and
//   no VEH trip (a latched "engine seen" flag keeps returning the last good
//   id if the engine goes away mid-session) — and the read itself stays
//   SEH-guarded as belt-and-braces, falling back to the last good id from
//   crash paths (dbglog runs inside the VEH too).  DebugFrameCode() is
//   pre-init-safe the same way: config==NULL -> pipeline nibble 0xF.
//
// DebugFrameCode() — packed 32-bit correlation code (the on-screen number):
//
//     bit 31..28  pipeline mode   Pipeline enum 0=PBR 1=PS2 2=Xbox 3=Mobile
//                                 4=GTAIV (pipelineOverride >= 0 wins over
//                                 config->pipeline); 0xF = unknown (config
//                                 not live yet)
//     bit 27..16  frame epoch     upper 12 bits of the widened frame id
//                                 (65536-frame wrap accumulator)
//     bit 15..0   short tick      raw RwUInt16 RWSRCGLOBAL(renderFrame)
//
//   i.e. code = (DebugFrameId() & 0x0FFFFFFF) | ((pipeline & 0xF) << 28).
//   The raw 16-bit short tick stays recoverable as (code & 0xFFFF); the
//   decimal <frameId> in the [#...] prefix is DebugFrameId().
//
// DebugOverlayCode() — alias of DebugFrameCode() for overlay lanes; returns
//   the exact value the log prefix carries for the current frame.
//
// DebugLogFrameMark(tag) — one-line pass-boundary marker:
//     [#<frameId>:<code>] -- <tag> --
// Call from other subsystems at pass boundaries.  Never called by
// debug_handl itself.
unsigned DebugFrameId(void);
unsigned DebugFrameCode(void);
unsigned DebugOverlayCode(void);
void DebugLogFrameMark(const char *tag);

// Forces the next log stamp to re-derive the pipeline nibble (the per-
// frame latch in debugFrameStamp, debug_handl.cpp).  Optional exactness
// hook for the pipeline re-derive receipt in main.cpp (reported hunk,
// not wired); without it the latch self-heals on the next frame-id
// advance.  Cheap, callable from any lane.
void DebugInvalidatePipelineCache(void);

// ---- DIAGNOSTIC VERBOSITY LATCH ----
//
// Single global knob for cheap silencing of per-frame receipts.  Default
// 0 = current behavior; nothing changes until it is raised.
//
//     0    full diagnostics (default)
//     >=1  dbglog_throttle's 2s interval stretches 5x (10s)
//     >=2  every throttle-gated receipt is silenced outright
//
// Lanes can also read it directly around their own per-frame dbglog()
// calls (one global int load).  NOT yet wired to config parsing —
// reported hunk for the orchestrator (do not apply from this lane):
//   1. skygfx.h Config struct (not this lane's region): add
//        int diagLevel;   // 0=full (default), 1=throttle x5, 2=quiet
//   2. main.cpp readIniFile, alongside the other readint lines:
//        c->diagLevel = readint(cfg.get("SkyGfx", "diagLevel", ""), 0);
//        g_diagLevel = config->diagLevel;
//   3. optional: debug menu / runtime setter writes g_diagLevel directly.
extern int g_diagLevel;

// ---- CRASH HANDLING (was crashhandler.cpp) ----
//
// No public API.  The unhandled-exception filter self-installs from a
// namespace-scope static object (s_crashInstaller) during CRT init, so
// there is nothing to declare or call from outside the TU.

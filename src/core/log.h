// log.h — L0 FOUNDATION LAYER of the diagnostics stack (m0272 layer split).
//
// Layer map (per the "fortify / contain / sandbox" directive):
//   L0  src/core/log.{h,cpp}           logging, ring buffer, frame
//                                       correlation, diag_init, g_diagLevel
//                                       MAY DEPEND ON: nothing
//   L1  src/core/crashhandler.{h,cpp}  VEH + forensics + dumps + the guarded
//                                       sandbox chokepoints (guardedSetRT /
//                                       guardedIm2DRender / g_guardDepth)
//                                       MAY DEPEND ON: L0
//   L2  src/core/diagnostics.{h,cpp}   scope registry (addr -> name)
//                                       MAY DEPEND ON: L0
//   facade  src/core/debug_handl.h      STABLE PUBLIC API — the one file every
//                                       existing consumer includes; it
//                                       re-exports all three layers.
//
// L0 NEVER CALLS UP: log.cpp must not reference a crashhandler or a
// diagnostics symbol (grep before adding one).  This is what makes a new
// logging feature safe: it cannot drag SEH/VEH code into an early-boot path,
// and nothing below L1 can reach it from a crash context.
//
// This header is re-exported by debug_handl.h (the facade); consumers may
// keep including only that file.

#pragma once

#include <windows.h>

// ============================================================
// Ring-buffer slot width
// ============================================================
// Was a file-local #define in debug_handl.cpp; promoted at the 2026-11-26
// crash de-merge (m0271) because the VEH's GUARD-FAULT receipt in
// crashhandler.cpp packs into the exact slot width the ring writer
// (diag_ringPut, L0) allocates.  L1 writes bytes into an L0 buffer — it never
// sees the ring internals (s_ring stays static in log.cpp).
#define RING_MSG_LEN 160

// ============================================================
// LOGGING CORE (definitions: log.cpp)
// ============================================================
// NOTE: diag_shutdown has NO declaration and NO definition outside this
// gate — it exists only as a DIAGNOSTICS_DISABLED stub in the facade
// (debug_handl.h).  Do not define it here.
#ifndef DIAGNOSTICS_DISABLED

// Initialize diagnostics: sets up log path, rotates the previous boot's log
// to <name>.prev.log, then creates the fresh file.
// logPath should be the full path to the log file.
// Called exactly once from DllMain (main.cpp) BEFORE diag_installVEH() —
// the spec-pool guard and the fatal filter both gate on
// diag_getLogPath()[0], so install must follow init.
void diag_init(const char *logPath);

// Get the current log file path (for external code that needs it).
const char* diag_getLogPath(void);

// Call from heartbeat points to tell the watchdog the game is alive.
// DEAD (m0271): grep 2026-11-26 found zero callers anywhere in src/ — the
// watchdog was removed at merge time (see main.cpp's "diag_startWatchdog()
// removed" note).  Declaration kept on purpose (other TUs may compile
// against it); the definition in log.cpp stays too.  Do NOT delete.
void diag_heartbeat(void);

#endif // DIAGNOSTICS_DISABLED

// ============================================================
// FRAME CORRELATION (frame id + packed overlay code)  [definitions: log.cpp]
// ============================================================
//
// Correlates a value printed on-screen (overlay lane) with the log lines of
// the same rendered frame.  Every skygfx_dbg.log line is prefixed by the log
// writer (dbglog_internal, log.cpp) with "#<frameId>:<code>" in
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
// log.cpp itself.
unsigned DebugFrameId(void);
unsigned DebugFrameCode(void);
unsigned DebugOverlayCode(void);
void DebugLogFrameMark(const char *tag);

// ---- FRAME COUNTER SEMANTICS (log dissection) ----
//
// The [#<frameId>:<code>] prefix is NOT wall-clock time and NOT a per-line
// counter.  Read it as a frame ordinal:
//
//   * <frameId> advances exactly once per RW-rendered frame (the widened
//     16-bit RWSRCGLOBAL(renderFrame) tick described above).  Two lines with
//     the same <frameId> belong to the same rendered frame.
//   * It is session-relative and starts at 0 until the engine first reports a
//     tick; it survives the 16-bit tick wrap (epoch-folded) and is never
//     reset by a map load or a pipeline switch.
//   * Gap analysis: a jump of N in <frameId> between adjacent log lines means
//     N frames elapsed with no logging — a stall, a loading screen, or an
//     interval where nothing in that family logged.  Per-line ordering inside
//     one frame is the line order itself.
//   * There is NO per-frame heartbeat line from log.cpp.  The only
//     periodic liveness line is the 2s diagnostic heartbeat emitted by
//     main.cpp; do not expect a tick per frame.  A frozen <frameId> with a
//     still-moving home-heartbeat means the render loop stalled before the
//     next frame began, not that logging broke.
//   * <code> packs the pipeline mode in bits 31..28 and repeats the low
//     frame bits; see the block above for the exact layout.

// Forces the next log stamp to re-derive the pipeline nibble (the per-
// frame latch in debugFrameStamp, log.cpp).  Called from
// derivePipelineFromPipes (main.cpp) whenever the pipeline is re-derived
// mid-frame, so the [#frame:code] prefix never shows a stale mode after a
// pipeline switch.  Cheap, callable from any lane.
void DebugInvalidatePipelineCache(void);

// ============================================================
// DIAGNOSTIC VERBOSITY LATCH  (definition: log.cpp)
// ============================================================
//
// Single global knob for cheap silencing of per-frame receipts.  Default
// 0 = current behavior; nothing changes until it is raised.
//
//     0    full diagnostics (default)
//     >=1  dbglog_throttle's 2s interval stretches 5x (10s)
//     >=2  every throttle-gated receipt is silenced outright
//
// Lanes can also read it directly around their own per-frame dbglog()
// calls (one global int load).  Wired: readIniFile (main.cpp) parses
// [SkyGfx] diagLevel into Config::diagLevel and syncs
// g_diagLevel = c->diagLevel.
extern int g_diagLevel;

// ============================================================
// SUPPLEMENTARY PER-MODULE LOG FILES  (m2397)   [definition: log.cpp]
// ============================================================
//
// Additive split: when a line's MESSAGE opens with a routed "[Tag]" token,
// dbglog_internal writes the SAME composed line (timestamp, level,
// file:line, func, [#frameId:code]) to a per-module file IN ADDITION to the
// main one — skygfx_dbg.log is byte-for-byte unchanged, the module file is
// only ever a second copy:
//
//     [cloud*|CloudShield]        -> skygfx_clouds.log
//     [PtLight*|Corona*|sprite_*] -> skygfx_corona.log
//     [water*]                    -> skygfx_water.log
//     [texdefs|texalpha]          -> skygfx_tex.log
//     [SSAO|PF|PostFX]            -> skygfx_postfx.log
//     [BPIPE|Bld*|VPIPE|Veh*]     -> skygfx_pipe.log
//
// Match: leading "[Tag]" only (up to the first ']', case-insensitive; a
// trailing '*' in the pattern means prefix), so a mid-message tag never
// routes.  Files live beside the main log (name derived from it), open
// LAZILY on their first routed line (static handles — one WriteFile per
// line after that) and are rotated by diag_init exactly like skygfx_dbg.log
// -> skygfx_dbg.prev.log (skygfx_<mod>.log -> skygfx_<mod>.prev.log).
//
// ROUTING TABLE: s_modTags / s_modLogs in log.cpp — one row to route an
// extra tag, one row per file.  Default ON; L0 reads no config, so the key
// the CONFIG LANE should wire later is:
//
//     [SkyGfx] debugSplitLogs=1   ; 1 (default) = module files on, 0 = off
//
// (suggested: Config::debugSplitLogs parsed in readIniFile -> a
// g_diagSplitLogs global read by modLogWrite, same pattern as g_diagLevel).
//
// No new API: the split is writer-side (dbglog_internal) + lifecycle-side
// (diag_init), so log.h declares nothing and needs no DIAGNOSTICS_DISABLED
// stub.

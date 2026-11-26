// crashhandler.h — L1 CONTAINMENT LAYER of the diagnostics stack
// (m0272 layer split; crash handling itself was de-merged out of
// debug_handl.cpp at m0271).
//
// This layer owns EVERYTHING that stands between a fault and the rest of
// the module:
//   * diag_crashHandler (the diagnostic VEH) + CRASH[n] receipts +
//     crash-relevant counters + diag_dumpStats
//   * diag_installVEH / diag_removeVEH (s_oldVEH) and the
//     g_inGuardedIm2DPass / g_renderPhase guard state
//   * crash* forensics primitives (module snapshot, address classify,
//     SEH-guarded safe read, instruction decode)
//   * crashAppendLog / crashAppendForensics / crashFlushLog /
//     crashWriteDump, crashUnhandledFilter + step fences,
//     crashInstallWerBackup, CrashHandlerInstaller (CRT static self-install)
//   * CrashHandler_InstallSpecPoolGuard (0x5D97E6 shutdown-ordering UAF)
//   * the guarded sandbox CHOKEPOINTS guardedSetRT / guardedIm2DRender /
//     g_guardDepth (bodies moved verbatim from render/pipelinecommon.cpp at
//     m0272; their declarations stay in skygfx.h:1638-1651)
//
// L1 MAY DEPEND ON L0 only (this header includes log.h for RING_MSG_LEN,
// diag_getLogPath and the frame stamps the receipts carry).  L1 must never
// reach into L2 or into a feature translation unit.

#pragma once

#include <windows.h>

#include "log.h"   // L0: RING_MSG_LEN (VEH receipt slot), diag_getLogPath,
                   //     DebugFrameId/DebugFrameCode (receipt stamps)

// ===========================================================================
// CONTAINMENT / SANDBOX CONTRACT — READ BEFORE WRITING A NEW FEATURE
// ===========================================================================
// (a) RENDER-STATE MUTATION INSIDE A PASS -> RAII fence.  Use the existing
//     rw_fixes / envmap __finally idiom: snapshot on entry, restore in
//     __finally so a fault cannot leak device state into the next pass.
// (b) FAULT-PRONE RAW CALL -> L1 chokepoint.  Route through
//     guardedSetRT() / guardedIm2DRender() (decls: skygfx.h): they
//     pre-check, SEH-contain, tag g_renderPhase, bump g_guardDepth and
//     return false so the caller fails open.  New primitives follow the
//     same shape and land HERE, not in the caller's TU.
// (c) MEMORY READS OF GAME OBJECTS -> liveness guards FIRST — the
//     RagdollPedIsLive / SoftbodyPedOwnsClump / IsFxRenderPrim pattern.
//     Never deref a pointer whose owner can free it mid-frame; guard, don't
//     catch.
// (d) CRASH DIAGNOSIS -> crashhandler primitives ONLY.  Exactly one VEH
//     (diag_installVEH) and one unhandled filter exist for this module.
//     NO new VEHs, NO new __except that swallows 0xC0000005 (CLASS 5:
//     swallowing access violations hides the real bug).
// (e) LAYER RULE -> L0 (log) never calls up.  Features call L1/L2 through
//     the facade (debug_handl.h) only; layer headers are re-exported there,
//     so a feature never has to include a layer directly.
// ===========================================================================

// Everything below is compiled only when diagnostics are live; under
// DIAGNOSTICS_DISABLED the facade (debug_handl.h) defines the stub macros
// and these declarations would be macro-mangled — see the gate there.
#ifndef DIAGNOSTICS_DISABLED

// Install the VEH crash handler (diagnostic-only, never suppresses).
// Call once from DllMain right after diag_init() (main.cpp) — order and
// names are load-bearing: the spec-pool guard's receipt gates on the log
// path that diag_init opens.
void diag_installVEH(void);

// Remove the VEH crash handler.  Detach order at shutdown: diag_dumpStats()
// first, then diag_removeVEH() (main.cpp).
void diag_removeVEH(void);

// Check for crash marker from previous session — show dialog if found.
// DEAD (m0271): grep 2026-11-26 found zero callers AND zero definitions in
// src/ (callers would fail to link).  Declaration kept on purpose — do NOT
// delete; re-wire before use (needs a definition).
void diag_checkCrashMarker(void);

// One-line end-of-session summary of crash-relevant counters (VEH catches,
// access-violation classes, guarded-pass faults).  Cheap and idempotent;
// call once from shutdown/exit so a log states its own crash families.
// Hook point (main.cpp, out of this lane's scope): the final log line before
// the module unloads, alongside wherever diag_removeVEH() is called.
void diag_dumpStats(void);

// Guard flag: set while inside SEH-guarded Im2D dispatches (SMAA passes).
// Lets the VEH pass through untouched so frame SEH containment can fire.
extern volatile LONG g_inGuardedIm2DPass;

// Current render-phase tag for the guarded choke-points (forensics only).
// Set at each SMAA pass boundary, cleared whenever the hoisted guard exits.
// The VEH reads it to tag GUARD-FAULT ring entries. Never NULL.
extern const char *g_renderPhase;

#endif // DIAGNOSTICS_DISABLED

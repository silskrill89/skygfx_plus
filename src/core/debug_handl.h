// debug_handl.h — STABLE PUBLIC API (facade) for the diagnostics stack.
//
// m0272 layer split ("fortify / contain / sandbox"): the old monolithic
// debug_handl.{cpp,h} is now three layers plus this facade, so a new
// feature can only attach to the layer that owns its concern instead of
// growing the shared debug translation unit:
//
//   L0  src/core/log.h           logging, ring buffer, frame correlation,
//                                 diag_init, g_diagLevel        (nothing)
//   L1  src/core/crashhandler.h  VEH, forensics, dumps, WER, spec-pool guard,
//                                 guardedSetRT / guardedIm2DRender /
//                                 g_guardDepth sandbox chokepoints   (L0)
//   L2  src/core/diagnostics.h   scope tag registry (addr -> name)   (L0)
//   facade  THIS FILE            re-exports all three; every existing
//                                 consumer keeps compiling unchanged.
//
// Provenance: merged 2026-10-26 from src/core/diagnostics.cpp +
// src/core/menu_inject.cpp + src/core/crashhandler.cpp (that de-merge is
// m0271, which moved crash handling back to src/core/crashhandler.cpp —
// and m0272 moved the rest out, deleting debug_handl.cpp entirely).  The
// CrashHandlerInstaller CRT static lives in crashhandler.cpp; its work
// (SetUnhandledExceptionFilter, the WER registry backup, the spec-pool guard
// patch) depends on no L0/L2 static, so cross-TU init order is moot.
//
// Consumers: keep including ONLY this file.  Include the layer headers
// directly only from inside a layer.

#pragma once

#include <windows.h>

// ---- LAYER RE-EXPORTS (the whole point of this header) ----
#include "log.h"          // L0: RING_MSG_LEN, diag_init/getLogPath/heartbeat,
                          //     DebugFrameId/Code/OverlayCode/LogFrameMark,
                          //     DebugInvalidatePipelineCache, g_diagLevel
#include "diagnostics.h"  // L2: g_scopeTagCount (registry decls: skygfx.h)
#include "crashhandler.h" // L1: diag_installVEH/removeVEH/dumpStats,
                          //     diag_checkCrashMarker, g_inGuardedIm2DPass,
                          //     g_renderPhase, containment contract

// ---- LOGGING (was diagnostics.h) ----
//
// diagnostics.h — Crash logging, debug output, exception handling.
// Separate from main.cpp so it can be ripped out for release builds.
//
// To disable all diagnostics: #define DIAGNOSTICS_DISABLED before including this header,
// or add DIAGNOSTICS_DISABLED to the project preprocessor definitions.
//
// The gate still owns the SAME eight stubs it always did — they are
// complete for every symbol that has a real definition plus the two
// stub-only names (diag_shutdown, diag_checkCrashMarker).  The real
// declarations live in the layer headers above, each guarded by
// #ifndef DIAGNOSTICS_DISABLED so this block never macro-mangles them.

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
#define diag_dumpStats()        ((void)0)

#else

// The live declarations are re-exported from the layer headers (log.h for
// diag_init / diag_getLogPath / diag_heartbeat, crashhandler.h for
// diag_installVEH / diag_removeVEH / diag_dumpStats / diag_checkCrashMarker
// + the guard state).  Nothing to repeat here.

#endif // DIAGNOSTICS_DISABLED

// Guard flag: set by InitialiseGame_hook to let SEH handlers fire during
// CGame::Initialise (defined in main.cpp).
// DEAD READ (m0271): the VEH's `if(g_allowCrashPassThrough)` was removed in
// the crashhandler.cpp de-merge — both branches were the identical
// EXCEPTION_CONTINUE_SEARCH disposition, so the flag had no semantics.
// The writes in main.cpp (single-writer-sensitive) are kept with a TODO at
// the site; remove decl + def + writes as one unit when that file is safe
// to touch for this purpose.
// (Kept here, in the facade, because it is the last surviving legacy decl
// with no layer that owns it.)
extern volatile int g_allowCrashPassThrough;

#endif // DIAGNOSTICS_H

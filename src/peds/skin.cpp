// skin.cpp — skin/skin-material sub-module, Phase 1 skeleton (INERT).
//
// Design authority: docs/plans/2026-10-31-peds-pipe-design.md §3.
// Phase 1 = module shell only. The four ped fixes (stage-0 resync, opaque
// contract, dual pass, pAmbient override) are absorbed VERBATIM in Phase 2
// from main.cpp — see the absorb table in skin.h. Nothing here touches D3D9
// state, RW render state, or the device.
//
// Receipts: [peds_skin] init stub / [peds_skin] classify stub — one-shot
// (dbglog_once), so an inert Phase 1 run proves the module is linked and
// reachable without polluting the per-frame log.

#include "skin.h"

static bool	s_skinInitDone;		// POD static — idempotence latch

void
Skin_Init(void)
{
	if(s_skinInitDone)
		return;
	s_skinInitDone = true;

	// Phase 1: loads nothing (no shaders, no rasters, no device objects).
	// Phase 2 wires SkinSSS.cso here (reserved IDR at pipelinecommon.cpp:
	// 810-811), lazy-bound and invalidated via PedsPipe_OnDeviceReset.
	dbglog("[peds_skin] init stub (loads nothing — Phase 2 absorbs per design doc §3.1)");
}

bool
Skin_ClassifyAtomic(RpAtomic *atomic, PedPipeState *st)
{
	// Cheap early-out contract: single bool read when the pipe is disabled.
	if(!PedsPipe_GetEnabled())
		return false;
	if(!atomic || !st)
		return false;

	// Phase 1 stub: no classification produced. The one-shot receipt proves
	// the call path is live (fires once per process, never per frame).
	if(dbglog_once("peds_skin_classify"))
		dbglog("[peds_skin] classify stub (atomic=%p state=%p — real absorb is Phase 2)", atomic, st);

	// Phase 2 fills st->skinClassMask / st->dualPassMask here and returns
	// true. Until then the caller keeps the legacy inline classification.
	return false;
}

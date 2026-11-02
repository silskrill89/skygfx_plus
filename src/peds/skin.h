#pragma once
#include "peds_pipe.h"

// skin.h — skin/skin-material sub-module (Phase 1 skeleton, INERT).
//
// Design authority: docs/plans/2026-10-31-peds-pipe-design.md §3.
// Phase 1 provides the module shell only: init stub + classification stub.
// The real absorbs (below) land in Phase 2 VERBATIM from main.cpp — no
// behaviour change, same receipt tags (design doc §3.1 acceptance probes).
//
// ---------------------------------------------------------------------------
// PHASE 2 ABSORB TABLE (design doc §3.1 — which main.cpp blocks move here)
// ---------------------------------------------------------------------------
// | Fix                                                      | Today (main.cpp)          | New home                    |
// |----------------------------------------------------------|---------------------------|-----------------------------|
// | stage-0 NULL-cache recovery + park/set + L3 pin           | pedStage0Resync 1781-1872 | SkinStage0Resync            |
// |   (NULL path 1798-1846: matList recovery, gpWhiteTexture  |                           |                             |
// |   fallback; L2 RwD3D9SetTexture 1824; L3 guarded 0xB4E9E0 |                           |                             |
// |   extraction 1859-1866 + TexDefsGetD3DTexture)            |                           |                             |
// | ped opaque contract (capture/divergence/enforce/restore)  | 1040-1084 / 1086-1099 /   | SkinEntryContract capture + |
// |                                                           | 1101-1161 / 1163-1180     | pedOpaqueEnforce /          |
// |                                                           |                           | SkinExitContract restore    |
// | ped dual pass (leg1 GREATEREQUAL@zt ZWRITE-on -> leg2     | 2260-2388 (blend-on F6    | SkinDrawDual                |
// |   LESS@zt ZWRITE-off + VERTEXALPHA blend-on; zt clamp     |   fix 2299-2308; zt clamp |                             |
// |   2268-2273; zwriteThresholdPed lever skygfx.h:364)       |   2268-2273)              |                             |
// | pAmbient override + black-guard + Sep-3 W3 revert         | 2185-2225 (guard 2207-    | SkinAmbientOverride         |
// |                                                           |   2219, throttled         |                             |
// |                                                           |   ped_ambient receipt)    |                             |
// | cull-mode A/B lever + alpha-test A/B lever + sticky       | 2035-2064 cull, 2009-2017 | SkinEntryContract           |
// |   ZWRITE repair                                           |   pedAlphaTest, 2075-2084 |                             |
// | exit [PedRS] monitor (21x GetRenderState triplet)         | 2391-2419+                | SkinExitContract            |
// ---------------------------------------------------------------------------
// NOT absorbed into skin (stays in peds_pipe): head-part receipt
// (chars_logHeadPartReceipt, main.cpp:2253-2258 — diagnostic router,
// design doc §3.1 last row).
//
// Phase 2 also binds the SM3.0 skin PS (SkinSSS.cso, reserved at
// pipelinecommon.cpp:810-811) gated on skinShaderEnable, register uploads
// via pipeUploadPBR() for c22/c23 (HARD project rule — manual upload
// produced flat/dark output), reusing g_partSSSParams verbatim
// (chars.cpp:9-17, no retuning). Design doc §3.2.
//
// Phase 3 = hair (hair.h), Phase 4 = cloth (cloth.h) — separate sub-modules,
// never routed through skin material state (design doc §1.1).

// One-shot module init. Phase 1: loads NOTHING (no shaders, no rasters, no
// device objects) — stub with receipt. Phase 2 wires SkinSSS.cso loading
// here (lazy, device-reset-safe via PedsPipe_OnDeviceReset invalidation).
// Call from InjectDelayedPatches (same slot as SoftbodyInit, main.cpp:8367).
// Idempotent.
void	Skin_Init(void);

// Classify one ped atomic into the per-clump state. Phase 1: STUB — always
// returns false and fires the one-shot [peds_skin] classify stub receipt.
// Phase 2 fills st->skinClassMask / st->dualPassMask (PartType encoding,
// chars.h:14-25) and absorbs the dual-pass atomicHasAlpha probes.
// Guards: NULL atomic/state and the PedsPipe_GetEnabled() gate are checked
// before any work (cheap early-out contract).
// Return: true iff classification was produced (Phase 2); false => caller
// keeps the legacy inline classification.
bool	Skin_ClassifyAtomic(RpAtomic *atomic, PedPipeState *st);

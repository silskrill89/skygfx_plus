#pragma once
#include "peds_pipe.h"

// skin.h — skin/skin-material sub-module. Phase 3 (SKIN absorb) LIVE.
//
// Design authority: docs/plans/2026-10-31-peds-pipe-design.md §3.
// Phase 3 absorbed the four ped fixes into callable functions below so the
// marked blocks in main.cpp's myDefaultCallback route through this module
// (pedsPipeEnable=0 keeps the legacy inline path byte-for-byte — the
// rollback lever). No behaviour change vs the inline code: same receipt
// tags (design doc §3.1 acceptance probes), same write order, same 3-layer
// idiom (pedOpaqueEnforce, main.cpp:1101).
//
// ---------------------------------------------------------------------------
// ABSORB TABLE (design doc §3.1 — which main.cpp blocks moved here)
// ---------------------------------------------------------------------------
// | Fix                                                      | Today (main.cpp)          | New home                    |
// |----------------------------------------------------------|---------------------------|-----------------------------|
// | ped opaque contract (capture/divergence/enforce)          | 1040-1084 / 1086-1099 /   | SkinEntryContract (+        |
// |                                                           | 1101-1161                 | skinOpaqueCapture/Enforce)  |
// | cull-mode A/B lever + alpha-test A/B lever + ZWRITE       | entry block 2161-2345     | SkinEntryContract           |
// |   3-layer guard + depth-trio force + saved-pair restore   |                           |                             |
// | pAmbient override + black-guard + Sep-3 W3 revert         | 2419-2465 (guard 2437-    | SkinAmbientOverrideEnter/   |
// |                                                           |   2459, throttled         | Exit                        |
// |                                                           |   ped_ambient receipt)    |                             |
// | ped opaque restore / GE@1 sticky reset + sticky-depth +   | exit block 2617-2698      | SkinExitContract (+         |
// |   ZWRITE sticky repair                                    |   (2625-2643 ZWRITE,      | skinOpaqueRestore)          |
// |                                                           |   2650-2676 restore/GE@1, |                             |
// |                                                           |   2678-2698 sticky-depth) |                             |
// | classification (opaque/cutout/translucent per atomic)     | atomicHasAlpha 959-992    | Skin_ClassifyAtomic         |
// ---------------------------------------------------------------------------
// Deferred-item absorb (the former "NOT absorbed" list below — now LIVE,
// same T3 dual-path flip, legacy inline kept byte-for-byte as the
// pedsPipeEnable=0 rollback lever): stage-0 resync (SkinStage0Resync), the
// dual-pass DRAW sequence (SkinDrawDual), the head-part receipt router
// (SkinHeadPartReceipt), and the [PedRS] exit monitor (SkinExitMonitor).
// Still NOT absorbed (stay in main.cpp): draw-boundary cull re-assert
// (reads PedPipeState::contractCullDraw). The SM3.0 skin PS (SkinSSS.cso,
// design doc §3.2) is a later phase — skinShaderEnable is parsed but unused.
//
// Phase 4+ = hair (hair.h), cloth (cloth.h) — separate sub-modules, never
// routed through skin material state (design doc §1.1).

// ---------------------------------------------------------------------------
// Classification bits — PedPipeState::skinClassMask (one atomic per call;
// the mask reflects the atomic being drawn).
// ---------------------------------------------------------------------------
#define SKINCLASS_OPAQUE      0x01u	// every part opaque (TAC_OPAQUE texture or
									// no texture, material/prelit alpha all 255)
#define SKINCLASS_CUTOUT      0x02u	// any hard 0/255 mask part (TAC_CUTOUT texture)
#define SKINCLASS_TRANSLUCENT 0x04u	// any faded/alpha<255 part (TAC_FADED texture,
									// material colour alpha<255, prelit alpha<255)
#define SKINCLASS_UNKNOWN     0x08u	// any unresolvable part (TAC_UNKNOWN) —
									// conservative: absorb decisions treat it as
									// "needs the second leg", never as opaque

// PedPipeState::dualPassMask bits.
#define SKINDUAL_SECOND_LEG   0x01u	// current atomic needs the dual-pass second
									// leg. Set for EVERY textured atomic and for
									// any cutout / translucent / unknown part;
									// cleared ONLY for a fully untextured, fully
									// opaque atomic (pure paint colour). Rationale
									// (skin.cpp, "Dual-pass decision — HEAD-PART
									// FIX"): TAC_OPAQUE is not "alpha-free" on an
									// FFP pipe — texalpha.h's class includes stray
									// alpha / AA-fringe atlases (the head, hair,
									// bandana, glasses family), and the ped pipe
									// has no shader alpha=1 to fall back on, so a
									// vetoed second leg paints that stray alpha
									// solid (green band / dark head) while opaque
									// body parts look identical either way. The
									// myDefaultCallback dual-pass gate reads
									// exactly this bit per atomic and VETOES the
									// second leg when it is clear.

// One-shot module init. Loads NOTHING (no shaders, no rasters, no device
// objects) — the SkinSSS.cso bind is a later phase (design doc §3.2).
// Called from InjectDelayedPatches (main.cpp:9360). Idempotent.
void	Skin_Init(void);

// Classify one ped atomic into the per-clump state (Phase 3: LIVE — the
// atomicHasAlpha/pedStage0Resync matList walk, main.cpp:959-992/1866-1877).
// Walks atomic->geometry->matList (material colour alpha + per-texture
// TexAlpha_Classify) and the prelit-lum alpha channel, writes the SKINCLASS_*
// mask to st->skinClassMask and the dual-pass decision to st->dualPassMask
// (both compare-then-write). Guards: NULL atomic/state and the
// PedsPipe_GetEnabled() gate are checked before any work (cheap early-out
// contract). Re-entrant: no span-scoped statics, all output in st.
// Return: true iff classification was produced; false => caller keeps the
// legacy inline classification.
//
// Dual-pass rule (HEAD-PART FIX — see the block comment in skin.cpp): the
// second leg is kept for every TEXTURED atomic and for any cutout /
// translucent / unknown part; it is vetoed only for a fully untextured,
// fully opaque atomic. TAC_OPAQUE alone is not a veto: on an FFP pipe there
// is no shader alpha=1 to neutralise the stray/AA alpha its class explicitly
// admits, and dropping leg 2 then paints the head-part fringe solid.
// That keeps the module arm byte-equivalent to the legacy inline arm, which
// has no veto at all (pedsPipeEnable=0 rollback lever).
bool	Skin_ClassifyAtomic(RpAtomic *atomic, PedPipeState *st);

// Entry state contract (design doc §3.1 — the SkinEntryContract lane:
// cull-mode A/B lever, pedAlphaTest A/B lever, ZWRITE 3-layer guard,
// depth-trio force, opaque contract capture/enforce). ONE call at the
// myDefaultCallback entry marker; it MUST own the whole chain because the
// enforce is the LAST writer before the skin pipeline (after
// pipeEnterAlphaMode + pipeForceAlphaBlock + the saved alpha-pair restore —
// main.cpp:1022-1026 invariant) while the capture must precede every force.
//
// Forces the canonical opaque signature — alpha test ON, GREATEREQUAL@1,
// VERTEXALPHA OFF, blend OFF — through all three layers (RwRenderStateSet /
// RwD3D9SetRenderState / raw device, the pedOpaqueEnforce idiom,
// main.cpp:1101) for opaque spans; alphaTestEnable==0 keeps the S5
// test-off lane (blend by the texel's own alpha). The enforce is
// compare-then-write (design doc §8 net-cost rule): when the capture
// already equals the target the whole write block is skipped. All contract
// state (capture, cull/alpha snapshots, zwrite/depth sticky flags, latches)
// is written to PedPipeState for SkinExitContract. Inert-safe
// (early-out when PedsPipe_GetEnabled() is false) and re-entrant.
void	SkinEntryContract(PedPipeState *st);

// pAmbient override pair (design doc §3.1 — the SkinAmbientOverride lane):
// Enter captures pAmbient->color, publishes the timecycle ambient with the
// black-guard (a pure-black timecycle sample with a non-black saved ambient
// falls back to the saved capture — log-forensics 2026-10-30 #4); Exit
// restores the capture after the ped draw. State in PedPipeState
// (ambSaved/ambOverridden), writes compare-then-write. Inert-safe,
// re-entrant (Exit is a no-op unless Enter ran).
void	SkinAmbientOverrideEnter(PedPipeState *st);
void	SkinAmbientOverrideExit(PedPipeState *st);

// Exit state contract (design doc §3.1 — the SkinExitContract lane): the
// per-layer restore (pedOpaqueRestore, ZWRITE excluded) when the contract
// latch is on, the fix-34 GREATEREQUAL@1 canonical reset through all three
// layers when it is off, then the sticky-depth repair — a divergent capture
// is NEVER re-emitted (canonical ZTEST=1 ZFUNC=LESSEQUAL left behind), a
// clean capture restores verbatim (main.cpp invariant 1029-1032). Also
// closes the span: pipeExitAlphaMode / ZWRITE sticky-or-restore /
// pipeExitCullMode from PedPipeState. The [PedRS] exit monitor is a
// separate entry point (SkinExitMonitor, own dual-path flip — it runs
// after either exit contract). Inert-safe, re-entrant.
void	SkinExitContract(PedPipeState *st);

// ---------------------------------------------------------------------------
// Deferred-item absorb (design doc §3.1) — the four items the Phase-3 table
// deferred. Each follows the EXACT T3 dual-path pattern: myDefaultCallback
// calls it ONLY on the module path (pedsPipeState non-NULL); the legacy
// inline code at each call site stays byte-for-byte as the pedsPipeEnable=0
// rollback lever (skin.h above, peds_pipe.h:34 — the stable-config runs rely
// on it). One definition per symbol, in skin.cpp.
//
// NOTE (deliberate deviation from the "early-out on PedsPipe_GetEnabled()"
// convention): these four are draw-path or draw-path-adjacent. Their cheap
// bool gate IS the call-site flip (pedsPipeState is non-NULL only when the
// module is enabled), and an internal early-out would DROP the resync/draw/
// receipt that the legacy path would have run instead of degrading to it.
// They run unconditionally once called; a NULL st skips only the state-lane
// writes (the legacy behaviour). No span state outside PedPipeState —
// file-statics inside are LOG-ONLY one-shots/throttles (receipt corpus).
// ---------------------------------------------------------------------------

// Stage-0 texture-raster resync (design doc §3.1 SkinStage0Resync lane, the
// main.cpp pedStage0Resync helper transcribed verbatim): makes the skin
// pipeline's per-material bind a real transition — NULL-cache recovery
// (matList walk -> gpWhiteTexture fallback, vehBindDiffuse3L 3-layer bind)
// + the park/set resync with the guarded 0xB4E9E0 L3 extraction. State
// lane (PedPipeState): stage0Latch 1 = armed / 2 = recovered this clump,
// stage0FailCount++, lastResyncFrame. Call site: the pedTexResync lever
// (main.cpp) gates both paths. Returns nothing; early-outs only on a NULL
// device (verbatim legacy contract).
void	SkinStage0Resync(RpAtomic *atomic, PedPipeState *st);

// Dual-pass DRAW sequence (design doc §3.1 SkinDrawDual lane, the
// myDefaultCallback isPedPipe dual branch transcribed verbatim): the
// restored original SkyGfx two-leg pass — leg 1 GREATEREQUAL@zt with
// ZWRITE ON (seeds depth at the cutout edge), leg 2 LESS@zt with ZWRITE
// OFF + VERTEXALPHA blend-on (F6 fix — the fringe blends by its own alpha),
// then ZWRITE ON + the caller's sanitized func/ref restored. Runs the two
// RxPipelineExecute legs and RETURNS the pipe (leg 2 reassigns it).
// alphatest/alpharef = the caller's sanitized saved pair (restored at the
// end); zt = the clamped zwriteThresholdPed. State lane (PedPipeState):
// dualPassActive 1 -> 2 -> 0, dualZTest, lastDualFrame. The [PedDual]
// one-shot receipts (same tags) live inside.
RxPipeline	*SkinDrawDual(RxPipeline *pipe, RpAtomic *atomic, PedPipeState *st,
			      int alphatest, int alpharef, int zt);

// Head-part (bandana/hair) one-shot texture receipt router (design doc
// §3.1 — chars_logHeadPartReceipt, impl chars.cpp:57; S5 diagnostic:
// texname + TexAlpha class + attach frame). Pure router, one-shot +
// NULL-safe inside the impl; the receipt corpus is identical on either
// dual-path leg.
void	SkinHeadPartReceipt(RpAtomic *atomic);

// [PedRS] exit state monitor (design doc §3.1, the myDefaultCallback
// isPedPipe exit block transcribed verbatim): throttled 21-read triplet
// readback (cull/blend/src/dst/zw/afunc/aref across rw cache / RW driver
// cache / raw device) after the exit contract — a divergent triplet
// fingerprints a foreign raw write that survived the restore. Log-only
// (ped_rs receipt; the reads are gated on the same throttle, first ped
// exit always samples once). Runs after EITHER exit contract (module or
// legacy) on its own dual-path flip.
void	SkinExitMonitor(void);

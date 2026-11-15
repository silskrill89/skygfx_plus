#pragma once
#include "skygfx.h"
#include "ini_parser.hpp"	// linb::ini — [Ped] config parse (Phase 2 call site: main.cpp readIniFile)

// peds_pipe.h — ped pipe control point (Phase 1 skeleton, INERT until wired).
//
// Design authority: docs/plans/2026-10-31-peds-pipe-design.md
// Plugin-slot registry discipline mirrors texdefs.cpp:285-317
// (TexDefsPluginAttach: guarded, one-shot, always-degrade-never-break).
//
// PHASE MAP (design doc §7):
//   Phase 1 — skeleton: clump plugin slot 0x50647031 ("Pdp1"),
//       per-ped state pool (fixed 64-entry free-list, POD, no heap churn),
//       [Ped] config parse into a file-static mirror, probe receipts.
//       Inert: every exported call early-outs on a single bool read while
//       pedsPipeEnable=0 (or before attach/config).
//   Phase 2 — main.cpp wiring: wrap myDefaultCallback (the 0x7491C0 hook
//       STAYS in main.cpp; design doc §2.1 wrap-first), absorb the ped-span
//       hook points P1-P15 (design doc §3.1 table) into skin.cpp;
//       readIniFile calls PedsPipe_ReadConfig; InjectDelayedPatches calls
//       Skin_Init.
//   Phase 3 (SKIN absorb — DONE) — skin.cpp owns the real skin logic:
//       Skin_ClassifyAtomic (material walk -> class mask),
//       SkinEntryContract (capture/enforce/cull/alpha/zwrite/depth),
//       SkinAmbientOverrideEnter/Exit (pAmbient black-guard pair),
//       SkinExitContract (restore / GE@1 reset / sticky-depth).
//       All contract state lives in PedPipeState (below) so the main.cpp
//       marked blocks route through the module with a pedsPipeEnable=0
//       legacy fallback. Hair thread (HairOnClumpAttach / HairPreRender /
//       HairDraw; enlivens the dead chars_classifyClump path, design doc
//       §4.1) and cloth sim (ClothUpdate / ClothPreRender, softbody A/B
//       gate, design doc §5) remain future sub-modules.
//
// ROLLBACK LEVER: [Ped] pedsPipeEnable=0 in any lane's INI restores the
// legacy inline ped path byte-for-byte (design doc §7 Phase 1).

// ---------------------------------------------------------------------------
// Pre-entry render-state triplet capture — the PedOpaqueState idiom
// (main.cpp:1074-1081), field-for-field. The module owns its own copy so
// main.cpp's legacy struct (and the pedsPipeEnable=0 inline path that uses
// it) stays byte-identical; when the pedContract.cpp extraction finally
// lands (design doc §3.1 "FUTURE render/pedContract.cpp"), both copies merge
// into that header verbatim. POD only — scalar members (C2712-safe).
// ---------------------------------------------------------------------------
struct SkinOpaqueState
{
	// L1 rw render-state cache
	RwUInt32 rwVtxAlpha, rwZWrite, rwZTest, rwAlphaFunc, rwAlphaRef;
	// L2 RW D3D9 driver cache
	DWORD drvBlend, drvZWrite, drvZEnable, drvZFunc, drvAlphaTest, drvAlphaFunc, drvAlphaRef;
	// L3 raw device
	DWORD devBlend, devZWrite, devZEnable, devZFunc, devAlphaTest, devAlphaFunc, devAlphaRef;
};

// ---------------------------------------------------------------------------
// Per-ped state (POD only — no ctors, no virtuals, no heap members).
// Stored in a fixed static pool; the clump plugin slot holds a PedPipeState*.
// The plugin ctor zeroes the SLOT at clump creation, so a recycled RpClump*
// address can never surface stale ped state (texdefs.cpp:16-19 rationale).
// ---------------------------------------------------------------------------
struct PedPipeState
{
	// --- identity ---
	RpAtomic	*backAtomic;		// atomic that first acquired this state (diagnostic back-pointer; may be NULL)
	RpClump		*ownerClump;		// clump that owns this pool entry (double-release / staleness guard)

	// --- skin classification (Skin_ClassifyAtomic fills, one atomic per call) ---
	uint32		skinClassMask;		// SKINCLASS_* bits (skin.h) of the MOST RECENTLY classified
									// atomic — opaque/cutout/translucent/unknown parts. The
									// PartType encoding (chars.h:14-25) stays with the
									// chars_classifyClump lane (design doc §4.1).
	uint32		dualPassMask;		// bit0 (SKINDUAL_SECOND_LEG, skin.h) = the most recently
									// classified atomic needs the dual-pass second leg. The
									// dual-pass gate in myDefaultCallback reads bit0 per
									// atomic (veto when clear). Cleared ONLY for a fully
									// untextured + fully opaque atomic — every textured
									// atomic (head/hair/bandana/glasses included) keeps
									// the leg, because the FFP ped pipe cannot force the
									// shader alpha=1 that TAC_OPAQUE assumes. See the
									// "HEAD-PART FIX" block in Skin_ClassifyAtomic.

	// --- stage-0 recovery latch (SkinStage0Resync state lane, design doc §3.1) ---
	uint32		stage0Latch;		// 0 = clean, 1 = resync armed, 2 = recovered this clump
	uint32		stage0FailCount;	// consecutive recovery failures (ped_tex_recover_fail counter)

	// --- dual-pass state (SkinDrawDual state lane, main.cpp:2541) ---
	uint32		dualPassActive;		// leg currently drawn: 0 = none, 1 = GREATEREQUAL@zt ZWRITE-on, 2 = LESS@zt ZWRITE-off + VERTEXALPHA blend-on (F6 fix)
	float		dualZTest;			// cached zt clamp value (zwriteThresholdPed lever, skygfx.h:364)

	// --- entry/exit contract (Phase 3: SkinEntryContract / SkinExitContract) ---
	// Written once per ped span by SkinEntryContract (pre-entry), consumed at
	// exit by SkinExitContract. A divergent capture is NEVER re-emitted at
	// exit (sticky rule) — the canonical signature is left behind instead.
	SkinOpaqueState	contractCap;		// pre-entry triplet capture (compare basis for the
										// §8 compare-then-write enforce + exit restore source)
	PipeAlphaState	contractAlpha;		// pipeEnterAlphaMode snapshot (exit: pipeExitAlphaMode)
	RwUInt32		contractCull;		// pipeEnterCullMode saved value (exit: pipeExitCullMode)
	RwUInt32		contractCullDraw;	// effective span cull forced at entry (main.cpp's
										// draw-boundary re-assert at :2516-2517 reads it)
	RwBool			zwriteRw;			// entry ZWRITE 3-layer guard captures (pre-seeded TRUE)
	DWORD			zwriteDrv;			// L2 RW driver cache
	DWORD			zwriteDev;			// L3 raw device
	uint32			contractOn;			// pedOpaqueContract latch (exit: restore vs GE@1 reset)
	uint32			alphaTestOn;		// pedAlphaTestEnable latch (1 = canonical, 0 = S5 lane)
	uint32			zwriteWasDesync;	// sticky ZWRITE rule flag (entry guard tripped)
	uint32			depthWasDesync;		// sticky-depth rule flag (ZTEST/ZENABLE/ZFUNC divergent)
	uint32			contractEntered;	// SkinEntryContract ran for this span — SkinExitContract
										// early-outs unless set (the ambOverridden idiom). The
										// pool zero-fills contractAlpha/contractCull/zwrite*;
										// an exit on that zeroed state would push colorWrite=0
										// through pipeExitAlphaMode (blank channels) and restore
										// ZWRITE=OFF — main.cpp pre-seeds savedAlpha/savedZWrite
										// (2132-2142) against the same hazard, so the module
										// needs its own pairing latch. Cleared at exit start, so
										// a second exit in the same span is a no-op (re-entrant).

	// --- pAmbient override (Phase 3: SkinAmbientOverride enter/exit pair) ---
	RwRGBAReal		ambSaved;			// pre-override pAmbient->color (exit restore source)
	uint32			ambOverridden;		// enter ran — exit must restore

	// --- frame stamps ---
	uint32		lastSeenFrame;		// last render frame this state was touched (staleness probe)
	uint32		lastResyncFrame;	// frame of the last stage-0 resync
	uint32		lastDualFrame;		// frame of the last dual-pass draw
};

// --- lifecycle -------------------------------------------------------------

// Registers the clump plugin slot 0x50647031 via RW plugin machinery.
// Called from myPluginAttach timing (main.cpp:5732 neighbourhood) — the ONLY
// RW touch in Phase 1, safe before any clump exists. Idempotent (guarded
// one-shot, texdefs.cpp:288 pattern). On registration failure: logs, marks
// the module unattached (every accessor degrades to NULL/false), NEVER
// breaks the game's plugin chain (always-return-1 rule, texdefs.cpp:316).
void	PedsPipe_PluginAttach(void);

// Config gate — SINGLE bool read (cheap early-out contract). False while
// pedsPipeEnable=0 OR before attach/config-read. Phase 2 render wiring
// branches on exactly this.
bool	PedsPipe_GetEnabled(void);

// Device-reset latch invalidation stub (chars_OnDeviceReset precedent,
// chars.h:49). Phase 1 owns no device objects — bumps a generation counter
// and logs once. Phase 2+ sub-modules invalidate cached device pointers here.
void	PedsPipe_OnDeviceReset(void);

// --- per-ped state ----------------------------------------------------------

// Guarded plugin-slot READ. Returns the live state or NULL when: disabled
// (cheap early-out), not attached, clump NULL, or the slot was never
// acquired. Pure read — no allocation, no slot write.
PedPipeState	*PedsPipe_GetState(RpClump *clump);

// Guarded plugin-slot ACQUIRE. Lazily pulls a zeroed entry from the fixed
// pool (64 entries, free-list) and writes the slot. Returns NULL when
// disabled/unattached/clump NULL/pool exhausted (throttled receipt). The
// plugin destructor callback returns entries to the free-list automatically
// at clump destruction — callers never free manually.
PedPipeState	*PedsPipe_AcquireState(RpClump *clump);

// --- config (Phase 2 call site: main.cpp readIniFile, one line) -------------

// Parses the [Ped] section into the file-static config mirror. Lane-scoped
// by construction (design doc §6): reads ONLY its own keys, never writes
// Config fields owned by other lanes. Missing keys default to 0 (all four
// features off = vanilla behaviour). Safe to call repeatedly (INI reload).
// Keys: pedsPipeEnable, skinShaderEnable, hairEnable, clothEnable.
void	PedsPipe_ReadConfig(linb::ini &cfg);

// --- diagnostics ------------------------------------------------------------

// Plugin-slot offset as registered (<= 0 when unattached/failed) — Phase 2
// probe receipts and the debug menu use this to prove attach health.
int		PedsPipe_GetSlotOffset(void);

// One-shot probe receipt helper (dbglog_once wrapper). Returns true the
// first time a given tag is probed — Phase 2 acceptance probes
// ([PedsPipe] wrap active, design doc §7 Phase 1) fire through this.
bool	PedsPipe_ProbeOnce(const char *tag);

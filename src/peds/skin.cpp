// skin.cpp — skin/skin-material sub-module. Phase 3 (SKIN absorb) LIVE.
//
// Design authority: docs/plans/2026-10-31-peds-pipe-design.md §3.
// Phase 3 absorbed the four ped fixes VERBATIM from main.cpp's
// myDefaultCallback marked blocks (same receipt tags, same write order,
// same 3-layer idiom) so those blocks route through this module:
//   Skin_ClassifyAtomic        — matList/prelit walk -> SKINCLASS_* mask
//   SkinEntryContract          — capture/enforce/cull/alpha/zwrite/depth
//   SkinAmbientOverrideEnter/  — pAmbient black-guard pair
//   SkinAmbientOverrideExit
//   SkinExitContract           — restore / GE@1 reset / sticky-depth
// The deferred-item wave then absorbed the four "NOT absorbed" items the
// same way (dual-path flips in myDefaultCallback, legacy inline kept
// byte-for-byte as the pedsPipeEnable=0 rollback lever):
//   SkinStage0Resync           — stage-0 bind chain + latch state lane
//   SkinDrawDual               — two-leg dual-pass draw + state lane
//   SkinHeadPartReceipt        — head-part receipt router (chars.cpp impl)
//   SkinExitMonitor            — [PedRS] 21-read exit triplet monitor
//
// State: everything span-scoped lives in PedPipeState (per-clump plugin
// slot, peds_pipe.h) — compare-then-write where a value can be skipped
// (design doc §8 net-cost rule). File-statics are LOG-ONLY one-shots/
// throttles (the receipt tags are part of the acceptance corpus,
// design doc §3.1) — no span state, so every function is re-entrant.
//
// Inert-safe: every entry point early-outs on one bool read
// (PedsPipe_GetEnabled()) before any work; a disabled module costs a bool
// test and main.cpp runs its legacy inline path instead. EXCEPTION: the
// four deferred-item entry points (SkinStage0Resync / SkinDrawDual /
// SkinHeadPartReceipt / SkinExitMonitor) are draw-path or draw-path-
// adjacent — their cheap bool gate IS the myDefaultCallback dual-path flip
// (they are called only when pedsPipeState is non-NULL, which implies the
// module is enabled), and an internal early-out there would DROP the
// resync/draw/receipt the legacy path would have run instead of degrading
// to it. They run unconditionally once called; a NULL st simply skips the
// state-lane writes (the legacy behaviour).
//
// RW touchpoints reuse the proven wrappers only: RwRenderStateGet/Set,
// RwD3D9Get/SetRenderState, pipeEnter/Exit/ForceCullMode,
// pipeEnter/ExitAlphaMode, pipeForceAlphaBlock (buildingPipe.cpp),
// pipeAlphaFuncToD3D (pipelinecommon.cpp), TexAlpha_Classify
// (texalpha.cpp) — no raw device pointers beyond the existing
// d3d9device null-guarded raw writes (pedOpaqueEnforce idiom).

#include "skin.h"
#include "texalpha.h"	// TexAlpha_Classify / TAC_* (per-raster cached, cheap)
#include "texdefs.h"	// TexDefsGetD3DTexture — L3 pin (pedStage0Resync idiom)

// buildingPipe.cpp — layer-3 alpha/blend repair (main.cpp:939 idiom).
extern void pipeForceAlphaBlock(void);
// pipelinecommon.cpp — rw alpha-func -> D3DCMP (main.cpp:227 idiom).
extern RwUInt32 pipeAlphaFuncToD3D(RwUInt32 rwFunc);
// chars.cpp — head-part material walk (the S5 keyword set, chars.cpp:57
// neighbourhood). Local extern, same pattern as SkinHeadPartReceipt below:
// chars.h is not part of this module's include surface.
extern bool chars_IsHeadPartAtomic(const RpAtomic *atomic);

static bool	s_skinInitDone;		// POD static — idempotence latch

void
Skin_Init(void)
{
	if(s_skinInitDone)
		return;
	s_skinInitDone = true;

	// Loads nothing (no shaders, no rasters, no device objects). The
	// SkinSSS.cso bind is a later phase (design doc §3.2), lazy-bound and
	// invalidated via PedsPipe_OnDeviceReset.
	dbglog("[peds_skin] init (SKIN absorb live — classify + entry/ambient/exit contracts; no device objects)");
}

// ===========================================================================
// PedOpaqueState idiom — pedOpaqueCapture / pedOpaqueDiverged /
// pedOpaqueEnforce / pedOpaqueRestore (main.cpp:1083-1249), transcribed
// field-for-field into the module-owned SkinOpaqueState (peds_pipe.h).
// ===========================================================================

static void
skinOpaqueCapture(SkinOpaqueState *s)
{
	// Pre-seed canonical values: a failed Get leaves the out-param untouched,
	// and the restore below must never push garbage to the device.
	s->rwVtxAlpha = FALSE;
	s->rwZWrite = TRUE;
	s->rwZTest = TRUE;
	s->rwAlphaFunc = rwALPHATESTFUNCTIONGREATEREQUAL;
	s->rwAlphaRef = 1;
	s->drvBlend = s->devBlend = FALSE;
	s->drvZWrite = s->devZWrite = TRUE;
	s->drvZEnable = s->devZEnable = TRUE;
	s->drvZFunc = s->devZFunc = D3DCMP_LESSEQUAL;
	s->drvAlphaTest = s->devAlphaTest = TRUE;
	s->drvAlphaFunc = s->devAlphaFunc = D3DCMP_GREATEREQUAL;
	s->drvAlphaRef = s->devAlphaRef = 1;

	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)&s->rwVtxAlpha);
	RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, (void*)&s->rwZWrite);
	RwRenderStateGet(rwRENDERSTATEZTESTENABLE, (void*)&s->rwZTest);
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, (void*)&s->rwAlphaFunc);
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)&s->rwAlphaRef);

	RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE, &s->drvBlend);
	RwD3D9GetRenderState(D3DRS_ZWRITEENABLE, &s->drvZWrite);
	RwD3D9GetRenderState(D3DRS_ZENABLE, &s->drvZEnable);
	RwD3D9GetRenderState(D3DRS_ZFUNC, &s->drvZFunc);
	RwD3D9GetRenderState(D3DRS_ALPHATESTENABLE, &s->drvAlphaTest);
	RwD3D9GetRenderState(D3DRS_ALPHAFUNC, &s->drvAlphaFunc);
	RwD3D9GetRenderState(D3DRS_ALPHAREF, &s->drvAlphaRef);

	if(d3d9device){
		d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &s->devBlend);
		d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &s->devZWrite);
		d3d9device->GetRenderState(D3DRS_ZENABLE, &s->devZEnable);
		d3d9device->GetRenderState(D3DRS_ZFUNC, &s->devZFunc);
		d3d9device->GetRenderState(D3DRS_ALPHATESTENABLE, &s->devAlphaTest);
		d3d9device->GetRenderState(D3DRS_ALPHAFUNC, &s->devAlphaFunc);
		d3d9device->GetRenderState(D3DRS_ALPHAREF, &s->devAlphaRef);
	}

	// Range-sanitize whatever the Gets actually returned (the pre-seed already
	// covers total failure). 0 is a legal rw ALPHAREF and stays faithful.
	if(s->rwVtxAlpha > 1) s->rwVtxAlpha = FALSE;
	if(s->rwZWrite > 1) s->rwZWrite = TRUE;
	if(s->rwZTest > 1) s->rwZTest = TRUE;
	if(s->rwAlphaFunc > rwALPHATESTFUNCTIONALWAYS) s->rwAlphaFunc = rwALPHATESTFUNCTIONGREATEREQUAL;
	if(s->rwAlphaRef > 255) s->rwAlphaRef = 1;
	if(s->drvZEnable > D3DZB_USEW) s->drvZEnable = TRUE;
	if(s->devZEnable > D3DZB_USEW) s->devZEnable = TRUE;
	if(s->drvZFunc < D3DCMP_NEVER || s->drvZFunc > D3DCMP_ALWAYS) s->drvZFunc = D3DCMP_LESSEQUAL;
	if(s->devZFunc < D3DCMP_NEVER || s->devZFunc > D3DCMP_ALWAYS) s->devZFunc = D3DCMP_LESSEQUAL;
	if(s->drvAlphaFunc < D3DCMP_NEVER || s->drvAlphaFunc > D3DCMP_ALWAYS) s->drvAlphaFunc = D3DCMP_GREATEREQUAL;
	if(s->devAlphaFunc < D3DCMP_NEVER || s->devAlphaFunc > D3DCMP_ALWAYS) s->devAlphaFunc = D3DCMP_GREATEREQUAL;
	if(s->drvAlphaRef > 255) s->drvAlphaRef = 1;
	if(s->devAlphaRef > 255) s->devAlphaRef = 1;
}

static bool
skinOpaqueDiverged(const SkinOpaqueState *s)
{
	return s->rwVtxAlpha != FALSE ||
	       s->drvBlend != FALSE || s->devBlend != FALSE ||
	       !s->rwZWrite || !s->drvZWrite || !s->devZWrite ||
	       !s->rwZTest || !s->drvZEnable || !s->devZEnable ||
	       s->drvZFunc != D3DCMP_LESSEQUAL || s->devZFunc != D3DCMP_LESSEQUAL ||
	       !s->drvAlphaTest || !s->devAlphaTest ||
	       s->rwAlphaFunc != rwALPHATESTFUNCTIONGREATEREQUAL ||
	       s->drvAlphaFunc != D3DCMP_GREATEREQUAL ||
	       s->devAlphaFunc != D3DCMP_GREATEREQUAL ||
	       s->rwAlphaRef != 1 || s->drvAlphaRef != 1 || s->devAlphaRef != 1;
}

static void
skinOpaqueEnforce(const SkinOpaqueState *cap, int alphaTestEnable)
{
	// ===== §8 net-cost rule — compare-then-write =====
	// The canonical lane (alphaTestEnable==1) forces EXACTLY the signature
	// skinOpaqueDiverged tests for. When the capture (fresh — taken at span
	// entry, before every force) already matches it, the entry chain left
	// the same canonical values live at this point (pipeEnterAlphaMode /
	// the depth-trio force / pipeForceAlphaBlock / the saved-pair restore
	// all write values that coincide with the canonical target when the
	// capture is canonical), so every write below would be a value-identical
	// no-op — skip the whole block. pedOpaqueDiverged (main.cpp:1086-1099)
	// already computes the info; reuse it before writing.
	// The S5 test-off lane (alphaTestEnable==0) can NOT use this shortcut:
	// its target (VERTEXALPHA/blend ON, ALPHATESTENABLE OFF) is never what
	// the entry chain leaves behind (pipeEnterAlphaMode/pipeForceAlphaBlock
	// force the opposite trio), so its writes stay unconditional — parity
	// with pedOpaqueEnforce(main.cpp:1157) over net-cost for the A/B lane.
	if(alphaTestEnable && !skinOpaqueDiverged(cap))
		return;

	// CULLMODE is deliberately absent here (main.cpp:1160-1165): the cull
	// writers are pipeEnterCullMode()/pipeForceCullMode() at entry and
	// pipeExitCullMode() at exit.
	// L1 rw cache (ALPHATESTENABLE and ZFUNC have no rw twin).
	// S5: the test-off lane (alphaTestEnable==0) must NOT paint sub-threshold
	// texels as SOLID RGB — head-part textures carry chroma-green in alpha=0
	// regions. Blend by the texel's own alpha instead; opaque texels (a=255)
	// are visually unchanged under SRCALPHA/INVSRCALPHA.
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)(alphaTestEnable ? FALSE : TRUE));
	if(!alphaTestEnable){
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	}
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	if(alphaTestEnable){
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONGREATEREQUAL);
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)1);
	}

	// L2 RW driver cache.
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, alphaTestEnable ? FALSE : TRUE);
	if(!alphaTestEnable){
		RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	}
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	RwD3D9SetRenderState(D3DRS_ZENABLE, TRUE);
	RwD3D9SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	// pedAlphaTestEnable lever: 1 = canonical cutout test (current
	// behaviour); 0 = test off, ALPHAFUNC/ALPHAREF writes skipped — the
	// in-game A/B for the view-angle ped-visibility report.
	if(alphaTestEnable){
		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
		RwD3D9SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
		RwD3D9SetRenderState(D3DRS_ALPHAREF, 1);
	}else
		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);

	// L3 raw device (repairs drift the caches cannot see).
	if(d3d9device){
		d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, alphaTestEnable ? FALSE : TRUE);
		if(!alphaTestEnable){
			d3d9device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			d3d9device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		}
		d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		d3d9device->SetRenderState(D3DRS_ZENABLE, TRUE);
		d3d9device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
		if(alphaTestEnable){
			d3d9device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
			d3d9device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
			d3d9device->SetRenderState(D3DRS_ALPHAREF, 1);
		}else
			d3d9device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	}
}

static void
skinOpaqueRestore(const SkinOpaqueState *s)
{
	// L1 rw cache. ZWRITE is deliberately absent: the sticky-repair block in
	// SkinExitContract owns D3DRS_ZWRITEENABLE (it restores the canonical
	// TRUE after a detected entry desync instead of recreating it).
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)s->rwVtxAlpha);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)s->rwZTest);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)s->rwAlphaFunc);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)s->rwAlphaRef);

	// L2 RW driver cache: each layer gets its own captured value.
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, s->drvBlend ? TRUE : FALSE);
	RwD3D9SetRenderState(D3DRS_ZENABLE, s->drvZEnable);
	RwD3D9SetRenderState(D3DRS_ZFUNC, s->drvZFunc);
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, s->drvAlphaTest ? TRUE : FALSE);
	RwD3D9SetRenderState(D3DRS_ALPHAFUNC, s->drvAlphaFunc);
	RwD3D9SetRenderState(D3DRS_ALPHAREF, s->drvAlphaRef);

	// L3 raw device.
	if(d3d9device){
		d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, s->devBlend ? TRUE : FALSE);
		d3d9device->SetRenderState(D3DRS_ZENABLE, s->devZEnable);
		d3d9device->SetRenderState(D3DRS_ZFUNC, s->devZFunc);
		d3d9device->SetRenderState(D3DRS_ALPHATESTENABLE, s->devAlphaTest ? TRUE : FALSE);
		d3d9device->SetRenderState(D3DRS_ALPHAFUNC, s->devAlphaFunc);
		d3d9device->SetRenderState(D3DRS_ALPHAREF, s->devAlphaRef);
	}
}

// ===========================================================================
// Item 1 — classification (the atomicHasAlpha matList walk, main.cpp:959-992,
// + the pedStage0Resync material walk idiom, main.cpp:1866-1877).
// ===========================================================================

bool
Skin_ClassifyAtomic(RpAtomic *atomic, PedPipeState *st)
{
	// Cheap early-out contract: single bool read when the pipe is disabled.
	if(!PedsPipe_GetEnabled())
		return false;
	if(!atomic || !st)
		return false;

	uint32 classes = 0;
	bool anyTexture = false;
	RpGeometry *geom = atomic->geometry;
	if(geom && geom->matList.materials){
		for(RwInt32 i = 0; i < geom->matList.numMaterials; i++){
			RpMaterial *m = geom->matList.materials[i];
			if(!m)
				continue;
			// Material colour alpha (atomicHasAlpha probe, main.cpp:968) —
			// a sub-255 colour alpha is true translucency, never a cutout.
			if(m->color.alpha != 255)
				classes |= SKINCLASS_TRANSLUCENT;
			// Texture alpha class (TexAlpha_Classify caches per raster, so
			// the walk is cheap — atomicHasAlpha note, main.cpp:957).
			// A NULL texture is TAC_OPAQUE, not UNKNOWN (texalpha.h rule).
			if(m->texture){
				anyTexture = true;
				switch(TexAlpha_Classify(m->texture)){
				case TAC_OPAQUE:
					classes |= SKINCLASS_OPAQUE;
					break;
				case TAC_CUTOUT:
					classes |= SKINCLASS_CUTOUT;
					break;
				case TAC_FADED:
					classes |= SKINCLASS_TRANSLUCENT;
					break;
				default:
					// TAC_UNKNOWN: unresolvable raster/extraction — the
					// conservative class. Absorb decisions must treat it as
					// "needs the second leg" (legacy parity: the dual pass
					// used to run unconditionally).
					classes |= SKINCLASS_UNKNOWN;
					break;
				}
			}else if(m->color.alpha == 255){
				// Untextured opaque material: solid paint-colour shading.
				classes |= SKINCLASS_OPAQUE;
			}
			// (an untextured material with alpha<255 already set
			// SKINCLASS_TRANSLUCENT above)
		}
	}
	// Prelit-lum alpha probe (atomicHasAlpha, main.cpp:985-990): vertex
	// alpha is another true-translucency source.
	if(geom && geom->preLitLum){
		const RwRGBA *c = geom->preLitLum;
		for(RwInt32 i = 0; i < geom->numVertices; i++){
			if(c[i].alpha != 255){
				classes |= SKINCLASS_TRANSLUCENT;
				break;
			}
		}
	}
	// Degenerate atomic (no materials at all): conservative — never claim
	// opaque, so the dual-pass veto can never starve an unclassifiable draw.
	if(classes == 0)
		classes = SKINCLASS_UNKNOWN;

	// ===== Dual-pass decision — HEAD-PART FIX (F6/S5 "green band + dark
	// head" family; body unaffected) ==========================================
	//
	// The second leg (LESS@zt fringe, ZWRITE OFF + VERTEXALPHA blend ON) is
	// what keeps a cutout's below-threshold texels from being painted as
	// SOLID RGB. It may only be vetoed for an atomic that is PROVABLY
	// alpha-free *for this pipe* — and the ped pipe is FFP, not a shader
	// pipe. Two facts decide it:
	//
	//  1. TAC_OPAQUE does NOT mean "every texel alpha == 255". texalpha.h
	//     defines the class to include "a solid texture with a stray alpha
	//     region" and "a thin-feature atlas whose AA fringe dominates" —
	//     which is precisely the head / hair / bandana / glasses class (the
	//     chroma-green transparent texels and the black atlas gutters).
	//     The building and vehicle pipes can shrug that off because they are
	//     SHADER pipes that "force shader alpha 1" (texalpha.h, verbatim).
	//     The ped pipe CANNOT: skinOpaqueEnforce leaves a hardware alpha test
	//     at GREATEREQUAL@1 with VERTEXALPHA/blend OFF, so EVERY texel with
	//     alpha >= 1 paints its raw RGB.
	//  2. Drop leg 2 on such an atomic and the single pass paints that stray
	//     alpha solid — chroma-green band across the face, black facets in
	//     the gutters, alpha-0 holes punched through to the head's interior
	//     (the dark blob). Body parts are visually unaffected because their
	//     textures are alpha-255 everywhere: GE@1 draws them identically
	//     either way. That is exactly the observed "body fixed, head wrong"
	//     split, and it is why only the player shows it (his per-part
	//     clothing textures carry the masks; generic ped TXDs do not).
	//
	// The legacy inline arm has NO veto at all (main.cpp's dual gate only
	// tests dualPassPed && zwriteThresholdPed > 0), so a module-only veto is
	// a divergence from the pedsPipeEnable=0 rollback lever — forbidden by
	// the dual-path rule. Restoring legacy parity AND correctness at once:
	// veto ONLY a fully untextured, fully opaque atomic (pure paint colour —
	// nothing can inject stray alpha). Every textured atomic keeps
	// SKINDUAL_SECOND_LEG, head parts included.
	uint32 dual = (!anyTexture && classes == SKINCLASS_OPAQUE)
		? 0u : SKINDUAL_SECOND_LEG;

	// Compare-then-write (design doc §8): skip identical state writes.
	if(st->skinClassMask != classes)
		st->skinClassMask = classes;
	if(st->dualPassMask != dual)
		st->dualPassMask = dual;

	// Evidence receipt for the retest (one-shot, BOUNDED scan): name the
	// first HEAD-PART atomic whose all-opaque classification the old rule
	// would have used to veto the second leg. The attempt budget caps the
	// cost for a run that never draws such an atomic — after it is spent the
	// block is two compares, so the steady state pays nothing.
	static bool pedHeadLeg2Logged = false;
	static int  pedHeadLeg2Scan = 16384;
	if(!pedHeadLeg2Logged && pedHeadLeg2Scan > 0 &&
	   (classes & (SKINCLASS_CUTOUT | SKINCLASS_TRANSLUCENT | SKINCLASS_UNKNOWN)) == 0){
		pedHeadLeg2Scan--;
		if(chars_IsHeadPartAtomic(atomic)){
			pedHeadLeg2Logged = true;
			dbglog("[PedHead] head-part atomic=%p classes=0x%X anyTexture=%d: "
			       "TAC_OPAQUE is NOT alpha-free on the FFP ped pipe (no shader "
			       "alpha=1 to fall back on) — dual-pass second leg KEPT; the "
			       "old rule vetoed it and painted the chroma-green/atlas-gutter "
			       "fringe solid (green band / dark head)",
			       (void*)atomic, (unsigned)classes, (int)anyTexture);
		}
	}

	if(dbglog_once("peds_skin_classify"))
		dbglog("[peds_skin] classify live (first: atomic=%p classes=0x%X dual=0x%X)",
		       (void*)atomic, (unsigned)classes, (unsigned)dual);

	return true;
}

// ===========================================================================
// Item 2 — entry state contract (design doc §3.1 SkinEntryContract lane:
// cull-mode A/B lever, pedAlphaTest A/B lever, ZWRITE 3-layer guard,
// depth-trio force, opaque contract capture/enforce). Transcribed from
// main.cpp myDefaultCallback entry block (2169-2345).
// ===========================================================================

void
SkinEntryContract(PedPipeState *st)
{
	if(!PedsPipe_GetEnabled() || !st)	// inert-safe: single bool read + NULL
		return;
	// Re-entrant (the ambOverridden idiom): the FIRST entry for this span
	// owns the pre-entry captures. A second entry without an intervening
	// exit would re-capture the state the first entry just FORCED — then
	// contractCull would hold the forced cull and pipeExitCullMode at exit
	// would leave the forced mode behind instead of the original span mode.
	if(st->contractEntered)
		return;

	// --- lever latches (the exit restore must match the entry force even if
	//     the INI value changes mid-frame — main.cpp:2154-2157) ---
	st->contractOn  = (config && config->pedOpaqueContract) ? 1u : 0u;
	st->alphaTestOn = (!config || config->pedAlphaTestEnable) ? 1u : 0u;

	// pedAlphaTestEnable A/B receipt (tag ped_atest), re-armed on change
	// so an INI reload is visible in the log (dbglog_once cannot re-fire).
	static int pedAlphaTestLogged = -2;
	if(pedAlphaTestLogged != (st->alphaTestOn ? 1 : 0)){
		pedAlphaTestLogged = st->alphaTestOn ? 1 : 0;
		dbglog("[PedAlphaTest] enabled=%d", pedAlphaTestLogged);
	}

	// --- pre-entry capture (pedOpaqueCapture, main.cpp:1083-1140) ---
	skinOpaqueCapture(&st->contractCap);
	// One-shot per run: what the DEVICE actually had before any repair.
	// [PipeAlpha] only reports cache-layer drift; this is the ped-pipeline
	// truth (ref=140 here confirms the envmap outdoor leak reached peds).
	if(dbglog_once("ped_opaque_pre"))
		dbglog("[PedOpaque] pre-entry dev ALPHAREF=%u ALPHAFUNC=%u ALPHABLENDENABLE=%u ZWRITE=%u ZENABLE=%u ZFUNC=%u ALPHATEST=%u | rw ref=%u fn=%u vtx=%u zw=%u ztest=%u | drv ref=%u fn=%u blend=%u zw=%u",
			(unsigned)st->contractCap.devAlphaRef, (unsigned)st->contractCap.devAlphaFunc,
			(unsigned)st->contractCap.devBlend, (unsigned)st->contractCap.devZWrite,
			(unsigned)st->contractCap.devZEnable, (unsigned)st->contractCap.devZFunc,
			(unsigned)st->contractCap.devAlphaTest,
			(unsigned)st->contractCap.rwAlphaRef, (unsigned)st->contractCap.rwAlphaFunc,
			(unsigned)st->contractCap.rwVtxAlpha, (unsigned)st->contractCap.rwZWrite,
			(unsigned)st->contractCap.rwZTest,
			(unsigned)st->contractCap.drvAlphaRef, (unsigned)st->contractCap.drvAlphaFunc,
			(unsigned)st->contractCap.drvBlend, (unsigned)st->contractCap.drvZWrite);

	// ===== pedCullMode A/B lever + explicit CULL set (3-layer paired) =====
	// -1 (default) = the skin expectation CULLBACK in the main view: the
	// span mode pipeEnterCullMode enforced (BACK outside a foreign cull
	// scope; inside one — mirror/shadow spans — the scope's own live mode,
	// per the vehiclePipe "never a hardcoded BACK" rule,
	// vehiclePipe.cpp:2933-2940). 1/2/3 force CULLNONE/CULLBACK/CULLFRONT
	// through all three layers for this ped span; the exit's
	// pipeExitCullMode(contractCull) restores the pre-entry span mode.
	// Out-of-range behaves as -1.
	// The write is UNCONDITIONAL (build-11 transparent-ped fix): the ped
	// entry must SET cull explicitly and restore it — inheriting "whatever
	// the caches hold" is the CULLNONE/FRONT leak family ("backfaces showing
	// through" skin = angle-dependent see-through, CULLFRONT = inside-out
	// ped). pipeForceCullMode is THE 3-layer paired write (L1 park+set / L2
	// park+set / L3 raw pin), so no cache-hit skip can hold the force back.
	st->contractCull = pipeEnterCullMode();
	int pedCullMode = config ? config->pedCullMode : -1;
	if(pedCullMode < -1 || pedCullMode == 0 || pedCullMode > 3)
		pedCullMode = -1;
	RwUInt32 pedCullForced = st->contractCull;
	if(pedCullForced != rwCULLMODECULLNONE &&
	   pedCullForced != rwCULLMODECULLBACK &&
	   pedCullForced != rwCULLMODECULLFRONT)
		pedCullForced = (RwUInt32)rwCULLMODECULLBACK;
	if(pedCullMode != -1){
		pedCullForced = (RwUInt32)rwCULLMODECULLNONE;
		if(pedCullMode == 2)
			pedCullForced = (RwUInt32)rwCULLMODECULLBACK;
		else if(pedCullMode == 3)
			pedCullForced = (RwUInt32)rwCULLMODECULLFRONT;
	}
	pipeForceCullMode(pedCullForced);
	st->contractCullDraw = pedCullForced;	// draw-boundary re-assert value
	// One-shot receipt (tag ped_cull), re-armed whenever the effective
	// mode changes (dbglog_once alone cannot re-fire after an INI reload).
	static int pedCullLoggedMode = -2;
	if(pedCullLoggedMode != pedCullMode){
		pedCullLoggedMode = pedCullMode;
		dbglog("[PedCull] mode=%d saved=%u forced=%u",
			pedCullMode, (unsigned)st->contractCull, (unsigned)pedCullForced);
	}

	st->contractAlpha = pipeEnterAlphaMode();

	// ZWRITE three-layer guard — mirror of buildingPipe.cpp:1185-1195.
	// Pre-seeded TRUE: a failed Get leaves the out-param untouched, and
	// TRUE is the canonical scene value (same pre-seed as the others).
	st->zwriteRw = TRUE;
	st->zwriteDrv = TRUE;
	st->zwriteDev = TRUE;
	RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, (void*)&st->zwriteRw);
	RwD3D9GetRenderState(D3DRS_ZWRITEENABLE, &st->zwriteDrv);
	if(d3d9device)
		d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &st->zwriteDev);
	// 3-layer parity with the building Switch (buildingPipe.cpp:3462):
	// ALL THREE captured layers count — a drv-only FALSE used to pass
	// this gate and was then faithfully re-emitted at exit.
	st->zwriteWasDesync = (!st->zwriteRw || !st->zwriteDrv || !st->zwriteDev) ? 1u : 0u;

	// ===== PED DEPTH-TEST CONTRACT (unconditional — build-11 fix) =====
	// "buildings/props draw over CJ" root family "Z-test state leaked into
	// the ped draw": the ped draw must ALWAYS run with ZTEST ON + ZFUNC
	// LESSEQUAL + ZWRITE ON — not only when pedOpaqueContract is on (that
	// gate covers only the alpha/blend signature). CJ's pixels paint even
	// with ZWRITE off, but his depth is never carved, so a building/prop
	// face drawn after him at its own face depth passes LESSEQUAL and paints
	// over the player. contractCap was captured above (skinOpaqueCapture),
	// so the receipt reports the true pre-entry state. ZTEST TRUE has an rw
	// twin (L1+L2+L3 paired); ZFUNC has none (L2+L3 paired —
	// pedOpaqueEnforce idiom).
	st->depthWasDesync = (st->zwriteWasDesync ||
		!st->contractCap.rwZTest || !st->contractCap.drvZEnable || !st->contractCap.devZEnable ||
		st->contractCap.drvZFunc != D3DCMP_LESSEQUAL ||
		st->contractCap.devZFunc != D3DCMP_LESSEQUAL) ? 1u : 0u;
	if(st->depthWasDesync && dbglog_throttle("PedZDepth"))
		dbglog("[PedZW] depth drift on entry (rw zw=%d ztest=%d | drv zw=%d zen=%u zfunc=%u | dev zw=%d zen=%u zfunc=%u) — forced ZTEST=1 ZFUNC=LESSEQUAL ZWRITE=1 (3L); victims if leaked: CJ overdrawn by props/buildings (draw-over-CJ)",
			(int)st->zwriteRw, (int)st->contractCap.rwZTest,
			(int)st->zwriteDrv, (unsigned)st->contractCap.drvZEnable,
			(unsigned)st->contractCap.drvZFunc,
			(int)st->zwriteDev, (unsigned)st->contractCap.devZEnable,
			(unsigned)st->contractCap.devZFunc);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwD3D9SetRenderState(D3DRS_ZENABLE, TRUE);
	RwD3D9SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	if(d3d9device){
		d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		d3d9device->SetRenderState(D3DRS_ZENABLE, TRUE);
		d3d9device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	}

	// Layer-3 alpha/blend repair: pipeEnterAlphaMode's RwD3D9 writes are
	// applied[]-gated and cannot reach a device that drifted behind an
	// unchanged cache (same rationale as buildingPipe.cpp:1197-1204).
	pipeForceAlphaBlock();
	// ALPHAFUNC/ALPHAREF: pipeEnterAlphaMode repaired the ref (degenerate
	// 0 -> 1) and pipeForceAlphaBlock re-pushes the CACHE's func/ref through
	// L2/L3 — not the caller's pre-entry pair. Put the captured pair
	// (contractAlpha, snapshotted before the entry contract ran) back
	// through L1 (rw setters) + L2 + raw L3 so the ped pass inherits exactly
	// what it found. ALPHATESTENABLE is deliberately NOT touched:
	// pipeForceAlphaBlock owns its force and the pre-entry value was never
	// captured.
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)st->contractAlpha.alphaFunc);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)st->contractAlpha.alphaRef);
	RwD3D9SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D(st->contractAlpha.alphaFunc));
	RwD3D9SetRenderState(D3DRS_ALPHAREF, (RwUInt32)st->contractAlpha.alphaRef);
	if(d3d9device){
		d3d9device->SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D(st->contractAlpha.alphaFunc));
		d3d9device->SetRenderState(D3DRS_ALPHAREF, (DWORD)st->contractAlpha.alphaRef);
	}

	// ===== PED OPAQUE CONTRACT =====
	// Last writer before the skin pipeline executes: pipeForceAlphaBlock
	// and the saved-pair restore above deliberately put the PRE-ENTRY alpha
	// func/ref back, which is exactly how envmap's leaked ref=140 reached
	// the device. Replace it with GREATEREQUAL@1 (rejects only
	// fully-transparent texels) and force blending off / depth writes on.
	// UNCONDITIONAL (build-11 transparent-ped fix): the canonical opaque
	// signature — alpha test ON, GREATEREQUAL@1, VERTEXALPHA OFF, blend OFF
	// — must hold for every opaque skin span; pedAlphaTestEnable=0 keeps
	// its S5 test-off lane (blend by the texel's own alpha). contractOn
	// gates ONLY the exit restore (SkinExitContract: pedOpaqueRestore vs
	// the fix-34 sticky reset).
	//
	// Receipt: dbglog_once for the run's FIRST repair (cannot be starved by
	// bucket collisions — the old "ped_contract" tag hashed to the same
	// bucket as pipeline_cull_enter, which fired ~9 ms earlier, so the
	// contract line never appeared even though the repair ran), then the
	// throttled "ped_contract2" tag.
	if(skinOpaqueDiverged(&st->contractCap) &&
	   (dbglog_once("ped_contract_first") || dbglog_throttle("ped_contract2")))
		dbglog("[PedOpaque] contract repaired divergent entry: dev(ref=%u fn=%u blend=%u zw=%u zen=%u zfunc=%u atest=%u) rw(ref=%u fn=%u vtx=%u zw=%u ztest=%u) drv(ref=%u fn=%u blend=%u zw=%u)",
			(unsigned)st->contractCap.devAlphaRef, (unsigned)st->contractCap.devAlphaFunc,
			(unsigned)st->contractCap.devBlend, (unsigned)st->contractCap.devZWrite,
			(unsigned)st->contractCap.devZEnable, (unsigned)st->contractCap.devZFunc,
			(unsigned)st->contractCap.devAlphaTest,
			(unsigned)st->contractCap.rwAlphaRef, (unsigned)st->contractCap.rwAlphaFunc,
			(unsigned)st->contractCap.rwVtxAlpha, (unsigned)st->contractCap.rwZWrite,
			(unsigned)st->contractCap.rwZTest,
			(unsigned)st->contractCap.drvAlphaRef, (unsigned)st->contractCap.drvAlphaFunc,
			(unsigned)st->contractCap.drvBlend, (unsigned)st->contractCap.drvZWrite);
	if(!st->contractOn && dbglog_once("ped_enforce_uncond"))
		dbglog("[PedOpaque] entry enforce UNCONDITIONAL (pedOpaqueContract=0): canonical GE@1/blend-off/vertexalpha-off pinned for the span");
	skinOpaqueEnforce(&st->contractCap, st->alphaTestOn ? 1 : 0);

	// Arm the exit pair LAST: every capture/force above completed, so
	// SkinExitContract may now consume contractAlpha/contractCull/zwrite*.
	// Set-after-work (not before) means an early bail can never leave a
	// half-initialized contract armed — the exit then no-ops on the
	// zeroed state instead of pushing colorWrite=0 through
	// pipeExitAlphaMode.
	st->contractEntered = 1;
}

// ===========================================================================
// Item 3 — pAmbient override pair (design doc §3.1 SkinAmbientOverride lane,
// main.cpp:2419-2465 enter / 2612-2615 restore). Sep-3 semantics (W3 revert
// — docs/plans/2026-10-30-sep3-correlation/git-chars-lighting.md §4.2):
// plain GetTimecycleAmbient() — no max() with the game's own ambient, no
// ambient-floor hue-preserving lift (that lever was removed 2026-11-10),
// no cutscene 0.15 special.
// ===========================================================================

void
SkinAmbientOverrideEnter(PedPipeState *st)
{
	if(!PedsPipe_GetEnabled() || !st)	// inert-safe
		return;
	if(!pAmbient)	// main.cpp gates `pipe == skinPipe && pAmbient` at the call site
		return;

	st->ambSaved = pAmbient->color;
	RwRGBAReal tcAmbient = GetTimecycleAmbient();
	// Guard (log-forensics 2026-10-30 #4): never publish PURE BLACK ambient
	// to the skin pipe unless the black is genuine — i.e. the game's own
	// saved ambient is black too (true night / blackout). The 04:38 run
	// showed 11x `[Ped] pAmbient override (0,0,0)` in the device-reset
	// window (04:38:35-55) while ambSaved held real light: a zeroed
	// GetTimecycleAmbient() sample is transient (reset / interior area
	// switch), and publishing it greys every ped to a featureless mannequin.
	// Fall back to the ambSaved capture in that case only; normal values
	// pass through verbatim (W3 revert semantics preserved).
	const float kAmbBlackEps = 1e-4f;
	bool tcBlack = tcAmbient.red   < kAmbBlackEps &&
	               tcAmbient.green < kAmbBlackEps &&
	               tcAmbient.blue  < kAmbBlackEps;
	bool savedBlack = st->ambSaved.red   < kAmbBlackEps &&
	                  st->ambSaved.green < kAmbBlackEps &&
	                  st->ambSaved.blue  < kAmbBlackEps;
	if(tcBlack && !savedBlack){
		tcAmbient = st->ambSaved;
		if(dbglog_throttle("ped_ambient"))
			dbglog("[Ped] pAmbient BLOCKED black override: timecycle=(0,0,0) with non-black saved ambient (%.3f,%.3f,%.3f) -> fallback savedAmb",
			       st->ambSaved.red, st->ambSaved.green, st->ambSaved.blue);
	}
	// Compare-then-write (§8): skip the struct store when the published
	// value is already live.
	if(pAmbient->color.red   != tcAmbient.red ||
	   pAmbient->color.green != tcAmbient.green ||
	   pAmbient->color.blue  != tcAmbient.blue ||
	   pAmbient->color.alpha != tcAmbient.alpha)
		pAmbient->color = tcAmbient;
	st->ambOverridden = 1;
	if(dbglog_throttle("ped_ambient"))
		dbglog("[Ped] pAmbient override: (%.3f,%.3f,%.3f)",
			tcAmbient.red, tcAmbient.green, tcAmbient.blue);
}

void
SkinAmbientOverrideExit(PedPipeState *st)
{
	if(!PedsPipe_GetEnabled() || !st)	// inert-safe
		return;
	if(!st->ambOverridden)	// re-entrant: restore only after a matching enter
		return;
	st->ambOverridden = 0;
	if(pAmbient){
		if(pAmbient->color.red   != st->ambSaved.red ||
		   pAmbient->color.green != st->ambSaved.green ||
		   pAmbient->color.blue  != st->ambSaved.blue ||
		   pAmbient->color.alpha != st->ambSaved.alpha)
			pAmbient->color = st->ambSaved;
	}
}

// ===========================================================================
// Item 4 — exit state contract (design doc §3.1 SkinExitContract lane,
// main.cpp:2617-2698): pipeExitAlphaMode + sticky ZWRITE repair +
// pipeExitCullMode + (pedOpaqueRestore | GE@1 canonical reset) +
// sticky-depth repair. Sticky rule (main.cpp invariant 1029-1032): a
// divergent capture is NEVER re-emitted — the canonical signature is left
// behind instead; a clean capture restores verbatim.
// ===========================================================================

void
SkinExitContract(PedPipeState *st)
{
	if(!PedsPipe_GetEnabled() || !st)	// inert-safe
		return;
	// Exit without a matching enter (or a SECOND exit in the same span):
	// contractAlpha/contractCull/zwrite* are pool-zeroed, and replaying them
	// would push colorWrite=0 through pipeExitAlphaMode (every channel
	// masked off) plus restore ZWRITE=OFF. Nothing was forced for this span,
	// so there is nothing to restore — no-op instead. Cleared BEFORE the
	// restore work so a nested/second exit in the same span is inert too
	// (re-entrant; mirrors SkinAmbientOverrideExit's ambOverridden clear).
	if(!st->contractEntered)
		return;
	st->contractEntered = 0;

	pipeExitAlphaMode(st->contractAlpha);

	// Sticky ZWRITE repair: when entry found the state off/desynced, the
	// repaired canonical TRUE is written to ALL THREE layers here instead of
	// restoring each layer to its own (desynced) pre-entry value — cache now
	// matches device, so the entry drift detector stops re-triggering every
	// frame. A clean pre-entry state (all layers agreed) still restores
	// faithfully per layer.
	if(st->zwriteWasDesync){
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		if(d3d9device)
			d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		if(dbglog_throttle("PedZWrite"))
			dbglog("[PED] ZWRITE sticky repair: canonical TRUE written to rw/drv/dev at exit");
	}else{
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)st->zwriteRw);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, st->zwriteDrv ? TRUE : FALSE);
		if(d3d9device)
			d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, st->zwriteDev);
	}

	pipeExitCullMode(st->contractCull);

	// Ped opaque contract exit: restore the true pre-entry values AFTER
	// pipeExitAlphaMode (which rewrites blend/alpha from the rw-cache
	// snapshot). Unconditional when the contract ran — no early return
	// above can skip it. D3DRS_ZWRITEENABLE is excluded; the sticky block
	// above owns it.
	if(st->contractOn){
		skinOpaqueRestore(&st->contractCap);
	}else{
		// fix-34: skinOpaqueRestore gated off (INI pedOpaqueContract=0).
		// The ENTRY enforce is unconditional (build-11 transparent-ped
		// fix), so the draw itself always carries the canonical opaque
		// signature — but pipeExitAlphaMode above still replays the
		// PRE-ENTRY func/ref through the rw-cache side (envmap's leaked
		// ref=140, envmap.cpp:448 outdoor park), which is how it would
		// reach and stay on the device. Sticky-repair rule (same as the
		// ZWRITE block above: never faithfully re-create a known leak):
		// reset the canonical GREATEREQUAL@1 through ALL THREE layers so
		// the alpharef/alphafunc contract no longer depends on the INI
		// lever. D3DRS_ZWRITEENABLE stays owned by the sticky block.
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONGREATEREQUAL);
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)1);
		RwD3D9SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D(rwALPHATESTFUNCTIONGREATEREQUAL));
		RwD3D9SetRenderState(D3DRS_ALPHAREF, 1);
		if(d3d9device){
			d3d9device->SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D(rwALPHATESTFUNCTIONGREATEREQUAL));
			d3d9device->SetRenderState(D3DRS_ALPHAREF, 1);
		}
		if(dbglog_throttle("ped_contract_skip"))
			dbglog("[PedOpaque] contract skipped (INI off) — exit "
			       "reset GREATEREQUAL@1 through rw/drv/dev");
	}

	// ===== sticky-depth repair (never re-create a known leak) =====
	// The restore above faithfully replays the per-layer capture — correct
	// for a CLEAN entry, but a divergent capture IS the leak (ZTEST/ZENABLE
	// off or ZFUNC drifted), and re-emitting it at exit hands it to every
	// later draw: buildings/props re-drawn after CJ then pass their depth
	// test over him (build-11 draw-over-CJ chain). Same sticky rule as the
	// ZWRITE block above — a divergent depth entry ends canonical
	// (ZTEST=1 ZENABLE=1 ZFUNC=LESSEQUAL), a clean entry keeps its verbatim
	// restore. ZWRITE stays owned by the block above. 3L-paired: ZTEST
	// rw+drv+dev, ZFUNC drv+dev (no rw twin).
	if(st->depthWasDesync){
		RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
		RwD3D9SetRenderState(D3DRS_ZENABLE, TRUE);
		RwD3D9SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
		if(d3d9device){
			d3d9device->SetRenderState(D3DRS_ZENABLE, TRUE);
			d3d9device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
		}
		if(dbglog_throttle("PedZDepth"))
			dbglog("[PedZW] sticky depth repair at exit: canonical ZTEST=1 ZFUNC=LESSEQUAL left behind (entry divergent — leak NOT re-emitted); victims if re-emitted: CJ overdrawn by props/buildings (draw-over-CJ)");
	}
}

// ===========================================================================
// Dual-pass leg-state writers + atomic alpha probe (main.cpp:229-249
// setAlphaTestTwoLayer / setZWriteTwoLayer and main.cpp:959-992
// atomicHasAlpha, transcribed for the module-owned SkinDrawDual — same
// pattern as skinOpaqueCapture above: the main.cpp statics stay where they
// are (legacy path), the module owns its copy).
// ===========================================================================

static void
skinSetAlphaTestTwoLayer(RwUInt32 rwFunc, int ref)
{
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwFunc);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)ref);
	RwD3D9SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D(rwFunc));
	RwD3D9SetRenderState(D3DRS_ALPHAREF, (RwUInt32)ref);
	if(d3d9device){
		d3d9device->SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D(rwFunc));
		d3d9device->SetRenderState(D3DRS_ALPHAREF, (DWORD)ref);
	}
}

static void
skinSetZWriteTwoLayer(RwBool on)
{
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)on);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, on ? TRUE : FALSE);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, on ? TRUE : FALSE);
}

// Atomic-level alpha probe (main.cpp atomicHasAlpha): material colour alpha
// + (optionally) the TexAlpha cutout/faded class + the prelit colour alpha
// channel. Used ONLY by the SkinDrawDual one-shot probe receipt.
static bool
skinAtomicHasAlpha(RpAtomic *atomic, bool includeTextureAlpha)
{
	RpGeometry *geom = atomic ? atomic->geometry : NULL;
	if(!geom)
		return false;
	if(geom->matList.materials){
		for(RwInt32 i = 0; i < geom->matList.numMaterials; i++){
			RpMaterial *m = geom->matList.materials[i];
			if(m && m->color.alpha != 255)
				return true;
		}
		if(includeTextureAlpha){
			for(RwInt32 i = 0; i < geom->matList.numMaterials; i++){
				RpMaterial *tm = geom->matList.materials[i];
				if(tm && tm->texture){
					int tac = TexAlpha_Classify(tm->texture);
					if(tac == TAC_CUTOUT || tac == TAC_FADED)
						return true;
				}
			}
		}
	}
	if(geom->preLitLum){
		const RwRGBA *c = geom->preLitLum;
		for(RwInt32 i = 0; i < geom->numVertices; i++)
			if(c[i].alpha != 255)
				return true;
	}
	return false;
}

// ===========================================================================
// Item 5 — stage-0 texture resync (design doc §3.1 SkinStage0Resync lane,
// main.cpp pedStage0Resync helper, transcribed VERBATIM — same receipt
// tags, same write order; the legacy helper stays in main.cpp as the
// pedsPipeEnable=0 rollback lever).
//
// Resync the stage-0 texture from the RW cache truth so the skin pipeline's
// per-material bind is a real transition. A raw NULL bind earlier can leave
// the device on a foreign raster while the cache still reports the old one;
// setting the cached value alone would then be a cache-hit no-op.
//
// NULL-cache recovery (test-4 grey-mannequin fix): when the rw texture cache
// itself holds NULL (postfx/water passes leave stage-0 NULL/leaked on exterior
// peds), the old park/set re-bound NULL and the L3 extraction had nothing to
// resolve — the ped drew untextured (grey skin, black legs). Walk the atomic
// being drawn (RpAtomic -> RpGeometry -> RpMaterialList -> RpMaterial->texture)
// for the real diffuse RwTexture and bind it through ALL THREE layers, the
// vehBindDiffuse3L idiom (vehiclePipe.cpp:767-821):
//   L1  RwRenderStateSet(rwRENDERSTATETEXTURERASTER) park-and-set — park on a
//       guaranteed-different raster first so the real set is always a
//       transition, never a cache-hit no-op;
//   L2  RwD3D9SetTexture(resolved, 0) — RW driver/applied[] cache;
//   L3  d3d9device->SetTexture(0, TexDefsGetD3DTexture(resolved)) — raw device,
//       the only layer a cache-hit skip can never hold back.
// No material texture found (or none with a raster) resolves to gpWhiteTexture
// — the pipe's neutral opaque sample — so the ped draws paint colour via
// MODULATE(white, DIFFUSE) instead of inheriting the previous pass's raster.
//
// State lane (st, write-order preserved): stage0Latch = 1 (armed) at entry,
// 2 (recovered this clump) on a successful recovery, stage0FailCount++ on a
// walk failure, lastResyncFrame at entry. No PedsPipe_GetEnabled() early-out
// (draw-path critical — the call-site dual-path flip is the gate; an
// early-out here would drop the resync the legacy path would have run).
// ===========================================================================

void
SkinStage0Resync(RpAtomic *atomic, PedPipeState *st)
{
	if(!d3d9device)
		return;
	// PedsPipe absorb (design doc §3.1 — SkinStage0Resync state lane):
	// per-clump stage-0 latch bookkeeping. st is non-NULL only when
	// the module is enabled AND owns this atomic (classification produced);
	// the receipts below are the legacy fallback and stay byte-identical.
	if(st){
		st->stage0Latch = 1;	// resync armed
		st->lastResyncFrame = *(uint32_t*)0xB7CB4C;	// CTimer::m_FrameCounter
	}
	void *rasRaw = NULL;
	RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &rasRaw);
	RwRaster *cur = (RwRaster*)rasRaw;
	if(cur == (RwRaster*)0xFFFFFFFF)
		cur = NULL;	// failed Get sentinel

	// ===== NULL-cache recovery path =====
	// The cache holds NULL: there is no cached truth to re-bind, so recover
	// the texture from the atomic's own materials and push it through all
	// three layers (see block comment above). Fully replaces the park/set
	// below for this case — parking white then setting NULL is exactly the
	// defect being repaired.
	if(cur == NULL){
		RwTexture *recovered = NULL;
		if(atomic && atomic->geometry && atomic->geometry->matList.materials){
			RpGeometry *geom = atomic->geometry;
			for(RwInt32 i = 0; i < geom->matList.numMaterials; i++){
				RpMaterial *m = geom->matList.materials[i];
				// First material with a real diffuse texture wins (skin
				// materials share one diffuse; later mats are heads/details
				// that re-bind per-material anyway).
				if(m && m->texture){
					recovered = m->texture;
					break;
				}
			}
		}
		// N9 policy honor (twin vehPolicyTexture, vehiclePipe.cpp:870-878):
		// `recovered` can be (1) NULL — legitimate "no texture" on the walk
		// path, still resolved to gpWhiteTexture below, unchanged; (2) a
		// POLICY MISS SENTINEL — TexDefsFindCB bound it in place of a failed
		// lookup, so the NULL test above can never see it; (3) a real
		// texture. This is a WORLD consumer: a sprite-class sentinel
		// (skygfx_missclear, 1x1 transparent magenta) must never be bound on
		// ped skin (magenta skin, or alpha-ref failure instead of visible
		// grey) — re-map it to the grey world sentinel. The discrimination
		// is pointer-identity (TexDefsMissTextureKind, texdefs.h:136-147),
		// which never allocates — the TexDefsMissSentinel* accessors would
		// create on demand and must not be called with a real texture in
		// hand. WORLD-kind (grey already) binds AS-IS.
		RwTexture *resolved = TexDefsResolveAtDraw(recovered);	// 2026-11-16: sentinel -> real if TXD loaded since
		if(resolved && TexDefsMissTextureKind(resolved) == TEXDEFS_MISS_TEX_SPRITE){
			if(dbglog_throttle("n9_ped_remap"))
				dbglog("[PedTex] N9: sprite-class miss sentinel re-mapped to grey world sentinel (was '%s')",
				       recovered->name[0] ? recovered->name : "?");
			resolved = TexDefsRemapToWorld(resolved);	// name-preserving world remap
		}
		if(!resolved)
			resolved = TexDefsMissSentinelWorld();	// A1: NULL from find-CB (miss) -> grey sentinel at draw (game keeps NULL)
		// L1 rw cache — park-and-set (vehBindDiffuse3L stage-0 discipline).
		if(resolved && resolved->raster){
			RwRaster *whiteRas = (gpWhiteTexture && gpWhiteTexture->raster)
				? gpWhiteTexture->raster : NULL;
			RwRaster *targetRas = resolved->raster;
			RwRaster *parkRas = (targetRas == whiteRas) ? NULL : whiteRas;
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)parkRas);
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)targetRas);
		}
		// L2 RW driver cache / texture apply (maps raster -> d3d internally).
		RwD3D9SetTexture(resolved, 0);
		// L3 raw device: bypass every cache. Only pin on a guarded extraction
		// success — never clear a valid bind on extraction failure (L2 already
		// bound the resolved texture correctly).
		if(resolved){
			IDirect3DTexture9 *d3d = TexDefsGetD3DTexture(resolved);
			if(d3d)
				d3d9device->SetTexture(0, d3d);
		}
		// Receipts: the first successful recovery names the texture
		// (dbglog_once — cannot be starved by bucket collisions); a repeated
		// walk failure is throttled so a broken atomic cannot spam the log.
		if(recovered){
			if(st)
				st->stage0Latch = 2;	// recovered this clump
			if(dbglog_once("ped_tex_recovered"))
				dbglog("[PedTex] recovered %s (atomic=%p geom=%p nmats=%d)",
				       recovered->name[0] ? recovered->name : "?",
				       (void*)atomic, (void*)atomic ? (void*)atomic->geometry : NULL,
				       atomic && atomic->geometry ? (int)atomic->geometry->matList.numMaterials : -1);
		}else{
			if(st)
				st->stage0FailCount++;	// consecutive recovery failures
			if(dbglog_throttle("ped_tex_recover_fail"))
				dbglog("[PedTex] recover FAILED: no material texture on atomic %p — gpWhiteTexture fallback",
				       (void*)atomic);
		}
		return;
	}

	// Park with a value guaranteed != target so both caches must transition.
	RwRaster *whiteRas = gpWhiteTexture ? gpWhiteTexture->raster : NULL;
	RwRaster *park = (cur == whiteRas) ? (RwRaster*)NULL : whiteRas;
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)park);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)cur);
	// L3: resolve the cached raster to its D3D texture and pin it raw.
	// Proven extraction (texalpha.cpp:571-584 / texdefs.cpp TexDefsGetNormalMap):
	// ext = raster + *(DWORD*)0xB4E9E0; ext+0 = IDirect3DTexture9*. The ext+8
	// "valid" byte can be stale, so accept a plausible pointer on its own.
	IDirect3DTexture9 *tex = NULL;
	if(cur){
		DWORD extOff = *(DWORD*)0xB4E9E0;
		if(extOff > 0 && extOff < 0x100000){
			IDirect3DTexture9 *cand = *(IDirect3DTexture9**)((uint8*)cur + extOff);
			if(cand &&
			   (uintptr_t)cand >= 0x10000u &&
			   (((uintptr_t)cand & 3u) == 0))
				tex = cand;
		}
	}
	if(tex)
		d3d9device->SetTexture(0, tex);
	if(dbglog_once("ped_tex_resync"))
		dbglog("[PedTex] stage0 resync cached=%p dev=%p", (void*)cur, (void*)tex);
}

// ===========================================================================
// Item 6 — dual-pass draw sequence (design doc §3.1 SkinDrawDual lane,
// main.cpp myDefaultCallback isPedPipe dual branch, transcribed VERBATIM —
// same receipt tags, same write order; the legacy inline sequence stays in
// main.cpp as the pedsPipeEnable=0 rollback lever).
//
// PED (skin pipe): the ORIGINAL SkyGfx behaviour (backup_original/
// src/main.cpp:422-436). The two legs run UNCONDITIONALLY whenever
// dualPassPed && zwriteThresholdPed>0: leg 1 = GREATEREQUAL@threshold with
// ZWRITE ON (seeds depth at the cutout edge), leg 2 = LESS@threshold with
// ZWRITE OFF (paints the below-threshold fringe without disturbing that
// depth) + VERTEXALPHA blend-on (F6 fix: the fringe blends by its own alpha
// instead of painting sub-threshold texels as SOLID RGB fill). Returns the
// pipe (leg 2's RxPipelineExecute reassigns it).
//
// State lane (st, write-order preserved): dualPassActive 1 (leg 1) -> 2
// (leg 2) -> 0 (restored), dualZTest = zt, lastDualFrame. alphatest/
// alpharef are the caller's sanitized saved pair restored at the end; zt
// is the clamped zwriteThresholdPed. No PedsPipe_GetEnabled() early-out
// (draw-path critical — an early-out here would drop the ped draw
// entirely; the call-site dual-path flip is the gate).
// ===========================================================================

RxPipeline *
SkinDrawDual(RxPipeline *pipe, RpAtomic *atomic, PedPipeState *st,
             int alphatest, int alpharef, int zt)
{
	// PedsPipe absorb (design doc §3.1 — SkinDrawDual state lane):
	// record the leg progression in the per-clump state when the
	// module owns this atomic (diagnostic writes only — the legacy
	// sequence below is untouched and stays byte-identical).
	if(st){
		st->dualPassActive = 1;	// leg 1: GREATEREQUAL@zt ZWRITE-on
		st->dualZTest = (float)zt;
		st->lastDualFrame = *(uint32_t*)0xB7CB4C;	// CTimer::m_FrameCounter
	}
	// Legacy skin-pipe sequence, transcribed from the original:
	//   ZWRITE TRUE -> leg1 GREATEREQUAL@zt (depth seeds) ->
	//   ZWRITE FALSE -> leg2 LESS@zt (fringe) ->
	//   ZWRITE TRUE -> restore caller func/ref.
	// The entry contract above already forced ZWRITE TRUE, but the
	// legacy callback re-forced it for the skin pipe (its
	// "parachute strings" fix) — keep that explicit write so the
	// sequence does not depend on the entry path.
	skinSetZWriteTwoLayer(TRUE);
	skinSetAlphaTestTwoLayer(rwALPHATESTFUNCTIONGREATEREQUAL, zt);
	// F6 fix: capture vertex-alpha before leg 1 so leg 2's restore
	// returns the caller's true value (pre-seeded per POD convention).
	BOOL blendSaved = TRUE;
	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)&blendSaved);
	RxPipelineExecute(pipe, atomic, 1);
	skinSetZWriteTwoLayer(FALSE);
	skinSetAlphaTestTwoLayer(rwALPHATESTFUNCTIONLESS, zt);
	// F6 fix: leg 2 (LESS@zt fringe) previously ran with blending OFF,
	// painting sub-threshold texels as SOLID RGB fill — head-part
	// textures carry chroma-green RGB in their transparent regions
	// (green facet / black head in interiors). Blend the fringe by
	// its own alpha instead (L2 mirror per buildingPipe:1946 discipline).
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	pipe = RxPipelineExecute(pipe, atomic, 1);
	if(st)
		st->dualPassActive = 2;	// leg 2: LESS@zt ZWRITE-off + blend-on
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)blendSaved);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, blendSaved ? TRUE : FALSE);
	skinSetZWriteTwoLayer(TRUE);
	skinSetAlphaTestTwoLayer((RwUInt32)alphatest, alpharef);
	if(st)
		st->dualPassActive = 0;	// both legs done, restored
	// One-shot receipt: proves the restored legacy path executed
	// (the removed gate used to log only its skip).
	static bool pedDualLegacyLogged = false;
	if(!pedDualLegacyLogged){
		pedDualLegacyLogged = true;
		dbglog("[PedDual] legacy dual pass restored (zwriteThresholdPed=%d)", zt);
	}
	// One-shot ped-only texture-alpha probe: the diagnostic that
	// explains why the old colour/prelit gate could never see CJ's
	// cutout masks. Purely informational; it does NOT gate the pass.
	static bool pedDualTexAlphaLogged = false;
	if(!pedDualTexAlphaLogged){
		pedDualTexAlphaLogged = true;
		dbglog("[PedDual] first ped atomic alpha probe: material/prelit=%d texture=%d",
		       (int)skinAtomicHasAlpha(atomic, false),
		       (int)skinAtomicHasAlpha(atomic, true));
	}
	return pipe;
}

// ===========================================================================
// Item 7 — head-part receipt router (design doc §3.1: chars_logHeadPartReceipt,
// impl chars.cpp:57 — S5 diagnostic, one-shot + NULL-safe there). Pure
// router; the receipt corpus (texname + TexAlpha class + attach frame) is
// identical on either dual-path leg. No PedsPipe_GetEnabled() early-out
// (call-site flip is the gate — see the header exception note).
// ===========================================================================

void
SkinHeadPartReceipt(RpAtomic *atomic)
{
	extern void chars_logHeadPartReceipt(const RpAtomic *atomic);
	chars_logHeadPartReceipt(atomic);
}

// ===========================================================================
// Item 8 — [PedRS] exit state monitor (design doc §3.1, main.cpp
// myDefaultCallback isPedPipe exit block, transcribed VERBATIM — the ped_rs
// receipt corpus is unchanged; the legacy inline monitor stays in main.cpp
// as the pedsPipeEnable=0 rollback lever).
//
// Throttled receipt (tag "ped_rs", at most once per 2 s): the post-restore
// value of every state this callback repairs, read from all three layers
// (rw cache / RW driver cache / raw device). A divergent triplet here is
// the fingerprint of a foreign raw write that survived the exit restore.
// The throttle gates the reads too, so the 21 GetRenderState calls only run
// when the line can actually be emitted; the first ped exit always samples
// once so the log proves the monitor is live even if the bucket was just
// taken. POD scalars only. Log-only (no PedPipeState writes); runs after
// either exit contract on its own dual-path flip.
// ===========================================================================

void
SkinExitMonitor(void)
{
	static bool pedRsFirst = false;
	bool pedRsDue = dbglog_throttle("ped_rs");
	if(pedRsDue || !pedRsFirst){
		pedRsFirst = true;
		RwUInt32 rwCull = 0, rwVtx = 0, rwSrc = 0, rwDst = 0;
		RwUInt32 rwZWrite = 0, rwAFunc = 0, rwARef = 0;
		DWORD drvCull = 0, drvBlend = 0, drvSrc = 0, drvDst = 0;
		DWORD drvZWrite = 0, drvAFunc = 0, drvARef = 0;
		DWORD devCull = 0, devBlend = 0, devSrc = 0, devDst = 0;
		DWORD devZWrite = 0, devAFunc = 0, devARef = 0;
		RwRenderStateGet(rwRENDERSTATECULLMODE, &rwCull);
		RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &rwVtx);
		RwRenderStateGet(rwRENDERSTATESRCBLEND, &rwSrc);
		RwRenderStateGet(rwRENDERSTATEDESTBLEND, &rwDst);
		RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &rwZWrite);
		RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &rwAFunc);
		RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, &rwARef);
		RwD3D9GetRenderState(D3DRS_CULLMODE, &drvCull);
		RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE, &drvBlend);
		RwD3D9GetRenderState(D3DRS_SRCBLEND, &drvSrc);
		RwD3D9GetRenderState(D3DRS_DESTBLEND, &drvDst);
		RwD3D9GetRenderState(D3DRS_ZWRITEENABLE, &drvZWrite);
		RwD3D9GetRenderState(D3DRS_ALPHAFUNC, &drvAFunc);
		RwD3D9GetRenderState(D3DRS_ALPHAREF, &drvARef);
		// Pre-seed the device mirrors from the driver cache: a null
		// device (reset/teardown) must not read as a false divergence.
		devCull = drvCull; devBlend = drvBlend; devSrc = drvSrc;
		devDst = drvDst; devZWrite = drvZWrite; devAFunc = drvAFunc;
		devARef = drvARef;
		if(d3d9device){
			d3d9device->GetRenderState(D3DRS_CULLMODE, &devCull);
			d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &devBlend);
			d3d9device->GetRenderState(D3DRS_SRCBLEND, &devSrc);
			d3d9device->GetRenderState(D3DRS_DESTBLEND, &devDst);
			d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &devZWrite);
			d3d9device->GetRenderState(D3DRS_ALPHAFUNC, &devAFunc);
			d3d9device->GetRenderState(D3DRS_ALPHAREF, &devARef);
		}
		// Domain normalisation for the comparison: rw vertex-alpha is the
		// rw-side face of D3DRS_ALPHABLENDENABLE; rw alpha func 1..8 maps
		// 1:1 to D3DCMP_* (pipeAlphaFuncToD3D); rw blend factors are 1:1
		// with D3DBLEND for valid values (pipeBlendToD3D's rule: rw 0/NA
		// and >SRCALPHASAT take the restore fallback). Cull is identity
		// for NONE/BACK (1/2) in both enums.
		RwUInt32 wantBlend = rwVtx ? TRUE : FALSE;
		RwUInt32 wantAFunc = pipeAlphaFuncToD3D(rwAFunc);
		RwUInt32 wantSrc = pipeBlendToD3D(rwSrc, D3DBLEND_SRCALPHA);
		RwUInt32 wantDst = pipeBlendToD3D(rwDst, D3DBLEND_INVSRCALPHA);
		bool rsDiverged =
			rwCull != (RwUInt32)drvCull || drvCull != devCull ||
			wantBlend != drvBlend || drvBlend != devBlend ||
			wantSrc != (RwUInt32)drvSrc || drvSrc != devSrc ||
			wantDst != (RwUInt32)drvDst || drvDst != devDst ||
			rwZWrite != (RwUInt32)drvZWrite || drvZWrite != devZWrite ||
			wantAFunc != drvAFunc || drvAFunc != devAFunc ||
			rwARef != (RwUInt32)drvARef || drvARef != devARef;
		dbglog("[PedRS] %s cull rw=%u drv=%u dev=%u | blend rw=%u drv=%u dev=%u | src rw=%u drv=%u dev=%u | dst rw=%u drv=%u dev=%u | zw rw=%u drv=%u dev=%u | afunc rw=%u drv=%u dev=%u | aref rw=%u drv=%u dev=%u",
			rsDiverged ? "DIVERGED" : "ok",
			(unsigned)rwCull, (unsigned)drvCull, (unsigned)devCull,
			(unsigned)wantBlend, (unsigned)drvBlend, (unsigned)devBlend,
			(unsigned)rwSrc, (unsigned)drvSrc, (unsigned)devSrc,
			(unsigned)rwDst, (unsigned)drvDst, (unsigned)devDst,
			(unsigned)rwZWrite, (unsigned)drvZWrite, (unsigned)devZWrite,
			(unsigned)wantAFunc, (unsigned)drvAFunc, (unsigned)devAFunc,
			(unsigned)rwARef, (unsigned)drvARef, (unsigned)devARef);
	}
}

// ============================================================
// mirrorPass.cpp — W7-C: CMirrors composite distortion fix
//
// Port of GTAIV.EFLC.FusionFix shaders/.../mirror/mirrorPS1.asm
// ("fixed distorted mirror reflections thanks to nukie") into SA's
// CMirrors pipeline. SA's composite is CMirrors::RenderMirrorBuffer
// (0x726090, reversed: gta-reversed/source/game_sa/Mirrors.cpp), whose
// single call site is inside CMain::Render at 0x53EABA (plugin-sdk
// Events.h renderMirrorEvent; gta-reversed app/app_game.cpp:360 — the
// call immediately before RenderScene(), so the mirror lands under the
// world draw exactly like the vanilla blit).
//
// ARCHITECTURE — additive overdraw, never a reimplementation:
//   1. the hook calls the GAME's RenderMirrorBuffer FIRST, so every
//      behaviour of that function is preserved bit-for-bit: the
//      STAIRS/bFudgeNow 8-track Im3D screens, the trailing world-space
//      stadium quad (camera < 50 m from (1003,-42,216)), and its
//      render-state restore baseline;
//   2. when SkyGfx/mirrorDistortionFix is on AND the game just took its
//      plain fullscreen else-branch (TypeOfMirror != MIRROR_TYPE_NONE —
//      the FIRST consumer of CMirrors::TypeOfMirror (0xC7C724), which
//      was declared in envmap.cpp:554 but never read — AND no
//      STAIRS/bFudgeNow AND outside the stadium-proximity block), the
//      pass OVERDRAWS the same fullscreen quad through
//      Mirror_Composite.ps: a per-pixel projective fetch of
//      CMirrors::pBuffer (s0) from the fragment's own NDC — the exact
//      IV fix. Same geometry, same blend (ONE/ZERO opaque replace), so
//      the overdraw substitutes the vanilla blit pixel-for-pixel;
//   3. every other case (fix off, no mirror, 8-track screens, stadium
//      quad, shader missing, fault) falls through to the game's own
//      function output untouched — the overdraw simply does not run.
//
// WHY PROJECTIVE: the vanilla Im2D blit hands the rasterizer fixed UVs
// {1,0}{1,1}{0,1}{0,0} over the screen quad; with RW's constant
// recipCameraZ that interpolation is linear and lands on the same
// texels the per-pixel path computes for a screen-aligned quad — the
// composite is therefore no-regress on the vanilla path, while the UV
// now DERIVES from the fragment position exactly like mirrorPS1.asm
// (rcp |w|, mad). That makes the mapping correct by construction for
// any future 3D/Im3D mirror quad (varying clip w) with zero shader
// change, and gives the IV-style colour-scale/alpha operands (c0.y/z)
// a live home for follow-up corrections.
//
// STATE DOCTINE (three layers, AGENTS.md): the pass writes only
// rw-cache render states — the SAME state set CMirrors::RenderMirrorBuffer
// itself writes (Mirrors.cpp:129-136 / restore :228-234) — and restores
// that exact post-function baseline in __finally. Raw D3D9 geom states
// are snapshotted (SaveRawGeomStates/RestoreRawGeomStates) and the PS
// constant registers c0/c1 are snapshotted (GetPixelShaderConstantF)
// around the draw. overrideIm2dPixelShader is reset in __finally AND in
// __except (postfx idiom — a stuck override leaks into every later Im2D
// dispatch). C2712: every local in the SEH spans is POD.
//
// SM3.0 / division safety: Mirror_Composite.hlsl guards its rcp with
// 1e-7; the CPU side floors the near-clip recip the same way.
// ============================================================

#include "skygfx.h"
#include "postfx.h"		// overrideIm2dPixelShader (postfx.h:116)
#include "mirrorPass.h"
#include "profiler.h"	// PF_SCOPE/PfBindPS/PfNote (renderlane profiler API)

// ---- game globals (gta-reversed Mirrors.cpp addresses) ----------------
// CMirrors::pBuffer 0xC7C71C — the mirror render target (1024x512 or
// 512x256 per fx quality, Mirrors.cpp CreateBuffer). MirrorFlags
// 0xC7C618 (eZoneAttributes bitfield; STAIRS = 0x2, CullZones.h).
static RwRaster *&CMirrors__pBuffer    = *(RwRaster**)0xC7C71C;
static uint8    &CMirrors__MirrorFlags = *(uint8*)0xC7C618;

// TypeOfMirror / bFudgeNow already have globals in envmap.cpp:554-555 —
// consume those, do NOT redefine (duplicate symbol).
extern int  &CMirrors__TypeOfMirror;	// envmap.cpp — 0xC7C724
extern bool &bFudgeNow;			// envmap.cpp — 0xC7C72A

#define MIRROR_TYPE_NONE	0
#define MIRROR_FLAG_STAIRS	0x2	// eZoneAttributes::STAIRS

// Trailing world-space quad guard (Mirrors.cpp:196): within 50 m of
// this point RenderMirrorBuffer draws an Im3D quad with the mirror
// texture AFTER the blit — our fullscreen overdraw (Z-test off) would
// bury it, so the fix stands down there.
static const CVector kMirrorStadiumQuadPoint = { 1003.0f, -42.0f, 216.0f };
static const float   kMirrorStadiumQuadRadiusSq = 50.0f * 50.0f;

// pipelinecommon.cpp:573 — same local-declaration idiom postfx_underwater.cpp:70
void makePS(int res, void **sh);

// skygfx.h:20 pulls resources/resource.h, so the canonical define is live
// (this lane owns resource.h and landed IDR_MIRROR_COMPOSITE = 277; 276 is
// W7-D's IDR_WATER_UNDERWATER reservation). Keep the numeric fallback so the
// TU still compiles if the header ever lags (postfx_underwater.cpp idiom).
#ifndef IDR_MIRROR_COMPOSITE
#define IDR_MIRROR_COMPOSITE     277
#endif

static void (*CMirrors__RenderMirrorBuffer_orig)(void);
static void *mirrorCompositePS = NULL;
static bool  mirrorPSLoadTried = false;

// Naked thunk in main.cpp (EAXJMP 0x7FC200) — plain-C++ declaration, the
// same idiom as pipelinecommon.cpp:19 / envmap.cpp / buildingPipe.cpp:58.
// Required by the park-and-set 3-layer restore below: the flush is
// applied[]-gated, so the raw L3 pin must follow a committed pending[] block.
void _rwD3D9RenderStateFlushCache(void);

// ============================================================
// Park-and-set 3-layer restore for one rw state with a D3D9 twin —
// thin wrapper over the canonical pipeRwParkSet/pipeL2ParkSet/
// pipeL3Set primitives (pipelinecommon.cpp:1134-1153), the same
// discipline as postfx_utils.cpp RestoreOneRawGeomState. ONE park
// order for the whole tree: PARK-THEN-TARGET PER LAYER —
//   (1) L1 park -> target, (2) L2 park -> target, flush, (3) raw pin.
// The pre-conversion interleaving (L1park/L2park/L1target/L2target)
// is the deprecated order (pipelinecommon.cpp:1118-1121): same end
// state, not canonical — no draw can occur between the writes, so
// the transient park never reaches the device. Per-layer values pass
// through VERBATIM (rwPark/rwTarget feed L1 only; d3dPark/d3dTarget
// feed L2/L3 only); rwState == rwRENDERSTATENARENDERSTATE skips the
// L1 leg (no rw-cache twin). Writing the OPPOSITE value first
// guarantees a real transition on both caches even when they already
// hold `target`; the target write then really reaches pending[], the
// flush commits it, and the raw write pins layer (3).
// ============================================================
static void
MirrorRestoreState3L(RwRenderState rwState, D3DRENDERSTATETYPE d3dState,
                     RwUInt32 rwPark, RwUInt32 rwTarget,
                     RwUInt32 d3dPark, RwUInt32 d3dTarget)
{
	if(rwState != rwRENDERSTATENARENDERSTATE)
		pipeRwParkSet(rwState, rwPark, rwTarget);		// (1) L1 park -> target
	pipeL2ParkSet(d3dState, d3dPark, d3dTarget);			// (2) L2 park -> target
	_rwD3D9RenderStateFlushCache();					// commit pending[]
	pipeL3Set(d3dState, d3dTarget);					// (3) raw pin
}

// ============================================================
// Band fix (test-4, docs/plans/2026-10-31-test4/band-forensics.md).
//
// ROOT CAUSE: the vanilla fullscreen blit of CMirrors::pBuffer — the
// else-branch quad inside CMirrors::RenderMirrorBuffer (exe-verified:
// RwIm2DRenderIndexedPrimitive 0x734EA0 called at ~0x72627F, 5 cdecl
// args; Mirrors.cpp:167-179) — is a VANILLA FFP Im2D draw, but it
// executes with whatever object-pipe shaders the mirror pre-pass left
// device-bound: CMirrors::BeforeMainRender (Main.cpp:4230) renders
// RenderScene() into pBuffer through our Xbox building pipe
// (interior-pipe-divergence-audit §4.1; the [BUILDING]/[BLIT]
// camRas=1C6D2848 receipts), and nothing between that span and this
// blit unbinds shaders (start-of-frame + DefinedState are FFP-only;
// the water flatpath receipts prove the building VS survives across
// vanilla FFP consumers). An RHW screen-space quad processed by the
// building VS (object-space verts + WVP constant contract, read
// through the building decl, not the Im2D layout) projects to an
// arbitrary screen region: the horizontally-seamed mirrored-world
// band drawn at frame end.
//
// SPAN SAFETY (exe-verified 2026-10-31): RenderMirrorBuffer (0x726090)
// does NOT call RenderScene (0x53DF40) — the reflected-world render is
// OUTSIDE this hook's span. The span's only draws are vanilla FFP
// consumers: the blit (0x72627F) and the trailing stadium-quad Im3D
// triplet (0x7EF450/0x7EF550/0x7EF520, jmp-skipped on the else-branch);
// the only code between hook entry and the blit writes render states
// (0x734650 helper + inline writes at 0x726260-0x72627E) and binds no
// shader. Forcing FFP at entry therefore cannot starve a shader-owning
// draw, and it simultaneously repairs the 8-track screens / stadium
// quad (forensics §4 secondary variant) and the composite overdraw
// (its own Im2D quad previously ran through the same stale VS).
//
// FIX (W1/W5 leave-FFP idiom, main.cpp:5923-5930 / 5960-5976): force
// FFP at hook entry — BEFORE the vanilla blit executes — via raw
// SetVertexShader(NULL)/SetPixelShader(NULL) + the RW value-compare
// cache seeds 0x8E2448/0x8E244C = NULL, and LEAVE FFP on every exit:
// the captured handles are NOT re-bound (they ARE the defect), and
// every shader-owning consumer re-binds at its own callback entry
// (buildingPipe.cpp:1971/1975/2650 per-atomic RwD3D9SetVertexShader).
// The AddRef/Release pairing of the captured handles is the state
// contract; re-binding them would re-create the band for every
// vanilla FFP draw that follows (RenderScene's clouds draw first).
// ============================================================
static void
MirrorPass_ForceFFP(void)
{
	if(d3d9device){
		d3d9device->SetVertexShader(NULL);
		d3d9device->SetPixelShader(NULL);
	}
	*(void**)0x8E2448 = NULL;	// _rwD3D9LastVertexShaderUsed
	*(void**)0x8E244C = NULL;	// _rwD3D9LastPixelShaderUsed
}

// Leave-FFP exit: re-assert the force (idempotent — nothing in the span
// binds a VS, and the composite's PS is already cleared through the rw
// cache by MirrorPass_CompositeRestore) and release the AddRef'd
// captures taken at entry (NULLs on frames without a capture — no-op).
static void
MirrorPass_LeaveFFP(IDirect3DVertexShader9 *staleVS,
                    IDirect3DPixelShader9 *stalePS,
                    IDirect3DBaseTexture9 *staleS0)
{
	MirrorPass_ForceFFP();
	if(staleVS) staleVS->Release();
	if(stalePS) stalePS->Release();
	if(staleS0) staleS0->Release();
}

// ============================================================
// The composite — overdraw of the vanilla fullscreen quad through
// Mirror_Composite.ps (s0 = CMirrors::pBuffer).
//
// Vertex contract (Mirror_Composite.hlsl header): v0.xy = NDC of the
// final screen position ([-1..1], y down); c0 = (rcpW, colourScale,
// alpha, 0); c1 = (0.5, 0.5, 0, 0). Corner order matches the game's
// else-branch quad (Mirrors.cpp:167-179): TL, BL, BR, TR with UVs
// {1,0}{1,1}{0,1}{0,0} — the shader's 0.5∓0.5·ndc reproduces those
// exactly for the screen-aligned quad (u = 1-x/W, v = y/H).
// ============================================================
// POD payload for MirrorPass_CompositeRestore. C2702: MSVC forbids __except
// inside a __finally termination block, so the guarded restore body lives in
// its own function (which carries the SEH) and the __finally only calls it.
struct MirrorCompositeSaved {
	bool pushedStates;
	bool rawGeomSaved;
	bool savedPSConstsValid;
	DWORD rawGeom[9];
	float savedPSConsts[8];
	RwUInt32 savedFilter, savedFog, savedZTest, savedZWrite;
	RwUInt32 savedVtxAlpha, savedSrcBlend, savedDstBlend;
	void *savedTexRaster;
};

static void
MirrorPass_CompositeRestore(const MirrorCompositeSaved *s)
{
	__try {
		if(s->pushedStates){
			// ---- restore every state this pass forced ----
			// Texture stage 0 (raster + filter) has NO D3DRS twin: RW maps
			// it onto SetTexture(0) / the sampler M*FILTER triad, so the L1
			// leg IS the restore. Park first so the cache always transitions.
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, s->savedTexRaster);
			RwRenderStateSet(rwRENDERSTATETEXTUREFILTER,
				(void*)(s->savedFilter == (RwUInt32)rwFILTERLINEAR ?
					(RwUInt32)rwFILTERNEAREST : (RwUInt32)rwFILTERLINEAR));
			RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)s->savedFilter);

			// Blend/Z/fog family: L1+L2 park-and-set, one flush, raw L3 pin.
			// rw bools are 0/1; park = the opposite of the captured value.
			MirrorRestoreState3L(rwRENDERSTATEVERTEXALPHAENABLE, D3DRS_ALPHABLENDENABLE,
				s->savedVtxAlpha ? 0u : 1u, s->savedVtxAlpha,
				s->savedVtxAlpha ? 0u : 1u, s->savedVtxAlpha);
			MirrorRestoreState3L(rwRENDERSTATEFOGENABLE, D3DRS_FOGENABLE,
				s->savedFog ? 0u : 1u, s->savedFog,
				s->savedFog ? 0u : 1u, s->savedFog);
			MirrorRestoreState3L(rwRENDERSTATEZTESTENABLE, D3DRS_ZENABLE,
				s->savedZTest ? 0u : 1u, s->savedZTest,
				s->savedZTest ? 0u : 1u, s->savedZTest);
			MirrorRestoreState3L(rwRENDERSTATEZWRITEENABLE, D3DRS_ZWRITEENABLE,
				s->savedZWrite ? 0u : 1u, s->savedZWrite,
				s->savedZWrite ? 0u : 1u, s->savedZWrite);
			// RwBlendFunction == D3DBLEND 1:1 for rwBLENDZERO..SRCALPHASAT;
			// a captured out-of-domain value (raw-only D3DBLEND_BLENDFACTOR
			// = 14, the SSS blend-back family) has no rw twin -> L1 leg
			// skipped, L2/L3 still restored verbatim.
			RwUInt32 srcPark = (s->savedSrcBlend == (RwUInt32)rwBLENDONE) ?
				(RwUInt32)rwBLENDZERO : (RwUInt32)rwBLENDONE;
			if(s->savedSrcBlend >= (RwUInt32)rwBLENDZERO &&
			   s->savedSrcBlend <= (RwUInt32)rwBLENDSRCALPHASAT)
				MirrorRestoreState3L(rwRENDERSTATESRCBLEND, D3DRS_SRCBLEND,
					srcPark, s->savedSrcBlend, srcPark, s->savedSrcBlend);
			else
				MirrorRestoreState3L(rwRENDERSTATENARENDERSTATE, D3DRS_SRCBLEND,
					0, 0, srcPark, s->savedSrcBlend);
			RwUInt32 dstPark = (s->savedDstBlend == (RwUInt32)rwBLENDONE) ?
				(RwUInt32)rwBLENDZERO : (RwUInt32)rwBLENDONE;
			if(s->savedDstBlend >= (RwUInt32)rwBLENDZERO &&
			   s->savedDstBlend <= (RwUInt32)rwBLENDSRCALPHASAT)
				MirrorRestoreState3L(rwRENDERSTATEDESTBLEND, D3DRS_DESTBLEND,
					dstPark, s->savedDstBlend, dstPark, s->savedDstBlend);
			else
				MirrorRestoreState3L(rwRENDERSTATENARENDERSTATE, D3DRS_DESTBLEND,
					0, 0, dstPark, s->savedDstBlend);

			// Shader-bind hygiene: the Im2D dispatch left Mirror_Composite
			// bound at the device (the Im2D hook only rewrites the PS on the
			// NEXT dispatch). Clear it through the rw cache — the same
			// postfx / chars.cpp SSS idiom — so a non-Im2D fullscreen blit
			// between here and RenderScene cannot run through it.
			RwD3D9SetPixelShader(NULL);
		}
		if(s->rawGeomSaved)
			RestoreRawGeomStates(s->rawGeom);
		if(s->savedPSConstsValid && d3d9device)
			d3d9device->SetPixelShaderConstantF(0, s->savedPSConsts, 2);
	} __except(EXCEPTION_EXECUTE_HANDLER){
		if(dbglog_throttle("mirror_crash"))
			dbglog("[MirrorPass] finally restore FAULT code=0x%08X", GetExceptionCode());
	}
}

static bool
MirrorPass_Composite(void)
{
	// Lazy one-shot shader load (makePS logs every failure — no spam).
	if(!mirrorPSLoadTried){
		mirrorPSLoadTried = true;
		makePS(IDR_MIRROR_COMPOSITE, &mirrorCompositePS);
	}
	if(!mirrorCompositePS){
		// Instrumentation (additive, throttled): the composite could not
		// run because the shader resource is missing — the vanilla blit
		// stands (reported by the absence of the ran-log below).
		if(dbglog_throttle("mirror_nops"))
			dbglog("[MirrorPass] composite skipped: IDR_MIRROR_COMPOSITE unavailable (sh=%p)", (void*)mirrorCompositePS);
		return false;
	}

	RwCamera *cam = Scene.camera;
	if(!cam) return false;
	RwRaster *screenRas = RwCameraGetRaster(cam);
	if(!screenRas) return false;
	RwRaster *mirrorRas = CMirrors__pBuffer;
	if(!mirrorRas) return false;

	// C2712: POD-only locals around __try. Hoisted so no SEH edge can
	// skip an initialization (C2360).
	RwIm2DVertex verts[4];
	RwImVertexIndex indices[6] = { 0, 1, 2, 0, 2, 3 };
	float w = (float)screenRas->width;
	float h = (float)screenRas->height;
	float nearz = RwIm2DGetNearScreenZ();
	float nearclip = RwCameraGetNearClipPlane(cam);
	if(!(nearclip > 1e-7f)) nearclip = 1e-7f;	// division safety
	float recipz = 1.0f / nearclip;
	// c0: rcpW = 1 (screen-aligned quad clip w), colourScale 1, alpha 1
	float c0[4] = { 1.0f, 1.0f, 1.0f, 0.0f };
	float c1[4] = { 0.5f, 0.5f, 0.0f, 0.0f };
	// third-layer snapshot: PS constants c0/c1 (2 registers)
	float savedPSConsts[8] = {};	// zero-init: copied into the restore struct even when the Get failed (flag-guarded consumer)
	bool savedPSConstsValid = false;
	DWORD rawGeom[9] = {};			// zero-init: same copy-unconditionally pattern as savedPSConsts
	bool rawGeomSaved = false;
	bool pushedStates = false;

	// ---- L1 entry captures (the baseline this pass must hand back) ----
	// The composite runs immediately after the game's RenderMirrorBuffer, so
	// the rw cache now holds that function's post-blit baseline
	// (Mirrors.cpp:228-234) — but NOT every forced state is covered by that
	// vanilla restore (TEXTUREFILTER/TEXTURERASTER are not), and the audit
	// found the hardcoded restore re-asserting values instead of capturing.
	// Capturing here makes the composite neutral: whatever the vanilla
	// function left (and whatever the caller had for states vanilla does not
	// touch) is handed back exactly. Pre-seeded with the vanilla baseline so
	// a failed/foreign Get keeps the pre-audit behaviour.
	// FILTER is domain-sanitized (a bad enum would index the driver's
	// conversion table out of bounds, same class as rw BLENDFACTOR(14)).
	RwUInt32 savedFilter = (RwUInt32)rwFILTERLINEAR;
	RwUInt32 savedFog = (RwUInt32)FALSE;
	RwUInt32 savedZTest = (RwUInt32)TRUE;
	RwUInt32 savedZWrite = (RwUInt32)TRUE;
	RwUInt32 savedVtxAlpha = (RwUInt32)FALSE;
	RwUInt32 savedSrcBlend = (RwUInt32)rwBLENDSRCALPHA;
	RwUInt32 savedDstBlend = (RwUInt32)rwBLENDINVSRCALPHA;
	void *savedTexRaster = NULL;
	void *savedIm2dOverride = overrideIm2dPixelShader;
	RwRenderStateGet(rwRENDERSTATETEXTUREFILTER, &savedFilter);
	RwRenderStateGet(rwRENDERSTATEFOGENABLE, &savedFog);
	RwRenderStateGet(rwRENDERSTATEZTESTENABLE, &savedZTest);
	RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &savedZWrite);
	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &savedVtxAlpha);
	RwRenderStateGet(rwRENDERSTATESRCBLEND, &savedSrcBlend);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, &savedDstBlend);
	RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &savedTexRaster);
	if(savedFilter < (RwUInt32)rwFILTERNEAREST ||
	   savedFilter > (RwUInt32)rwFILTERLINEARMIPLINEAR)
		savedFilter = (RwUInt32)rwFILTERLINEAR;

	// Corner table: screen pos (x,y) + the NDC the shader folds into UVs.
	static const float px[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	static const float py[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
	static const float nx[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
	static const float ny[4] = { -1.0f, 1.0f, 1.0f, -1.0f };
	for(int i = 0; i < 4; i++){
		RwIm2DVertex *v = &verts[i];
		RwIm2DVertexSetScreenX(v, px[i] * w);
		RwIm2DVertexSetScreenY(v, py[i] * h);
		RwIm2DVertexSetScreenZ(v, nearz);
		RwIm2DVertexSetRecipCameraZ(v, recipz);
		RwIm2DVertexSetIntRGBA(v, 255, 255, 255, 255);
		RwIm2DVertexSetU(v, nx[i], recipz);
		RwIm2DVertexSetV(v, ny[i], recipz);
	}

	// Nested SEH (envmap.cpp RenderReflectionMap_leeds idiom): sibling
	// __except+__finally on one __try is illegal MSVC — the inner
	// __try/__except swallows+logs renderer faults, the outer __finally
	// restores on EVERY exit (normal, __leave, swallowed fault, fault in
	// the handler itself).
	__try {
		__try {
			// Layer 2: raw D3D9 geom block (blend/Z/cull family) — the pass
			// writes SRCBLEND/DESTBLEND/VERTEXALPHA/Z*/FOG through the rw
			// cache, but the snapshot is the airtight floor.
			rawGeomSaved = SaveRawGeomStates(rawGeom);
			IDirect3DDevice9 *dev = d3d9device;
			if(dev)
				savedPSConstsValid = SUCCEEDED(dev->GetPixelShaderConstantF(0, savedPSConsts, 2));

			// Exactly the state set the game's own blit uses
			// (Mirrors.cpp:129-136) — opaque replace of the same quad.
			// Arm BEFORE the writes: the restore assigns this same baseline,
			// so running it after a mid-sequence fault is safe, while NOT
			// running it would leak partial blend/Z state.
			pushedStates = true;
			RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
			RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
			RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
			RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)mirrorRas);
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);

			RwD3D9SetPixelShaderConstant(0, c0, 1);
			RwD3D9SetPixelShaderConstant(1, c1, 1);
			overrideIm2dPixelShader = mirrorCompositePS;
			// Instrumentation (additive): profiler bind point for the
			// composite draw. Plain calls only — this function carries
			// SEH, so no destructible scope objects (C2712).
			PfBindPS(mirrorCompositePS, "IDR_MIRROR_COMPOSITE");
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, verts, 4, indices, 6);
			overrideIm2dPixelShader = nil;
		} __except(EXCEPTION_EXECUTE_HANDLER){
			// Per-frame path — throttled (AGENTS.md logging doctrine).
			if(dbglog_throttle("mirror_composite_fault"))
				dbglog("[MirrorPass] composite FAULT code=0x%08X — vanilla blit stands",
					GetExceptionCode());
		}
	} __finally {
		// Finally-style restore on EVERY exit (normal, fault, fault-in-handler).
		// Hand the caller's Im2D-override value back verbatim — the old
		// hardcoded `= nil` silently dropped a non-nil caller override.
		overrideIm2dPixelShader = savedIm2dOverride;
		// Guarded restore: a fault during unwind (device lost) would otherwise
		// terminate the process — C2702 forbids __except inside __finally, so
		// the swallow+log lives in MirrorPass_CompositeRestore (own SEH).
		MirrorCompositeSaved restore;
		restore.pushedStates = pushedStates;
		restore.rawGeomSaved = rawGeomSaved;
		restore.savedPSConstsValid = savedPSConstsValid;
		for(int i = 0; i < 9; i++) restore.rawGeom[i] = rawGeom[i];
		for(int i = 0; i < 8; i++) restore.savedPSConsts[i] = savedPSConsts[i];
		restore.savedFilter = savedFilter;
		restore.savedFog = savedFog;
		restore.savedZTest = savedZTest;
		restore.savedZWrite = savedZWrite;
		restore.savedVtxAlpha = savedVtxAlpha;
		restore.savedSrcBlend = savedSrcBlend;
		restore.savedDstBlend = savedDstBlend;
		restore.savedTexRaster = savedTexRaster;
		MirrorPass_CompositeRestore(&restore);
		// Instrumentation (additive, throttled): confirm the three
		// restore layers all ran after the composite draw — reads the
		// flags set above, changes nothing.
		if(dbglog_throttle("mirror_state_restore"))
			dbglog("[MirrorPass] composite state restore: pushedStates=%d rawGeomSaved=%d psConstsValid=%d overrideCleared=1",
				(int)pushedStates, (int)rawGeomSaved, (int)savedPSConstsValid);
	}
	return true;
}

// ============================================================
// Cull re-sync after the mirror span. pipeForceCullMode is the only
// writer that repairs all three layers (rw cache / RW driver cache /
// raw device). The game's own cull restore is an L1-only
// RwRenderStateSet, and the composite's Im2D / raw-geom snapshot
// restore can leave L3 on the mirror's NONE while L1 already holds
// BACK — a later main-view draw is then cache-hit-skipped and renders
// double-sided. Re-asserting from the rw-cache truth parks + writes
// every layer, so the device ends up matching the cache.
//
// NOTE: the hook now wraps its span in pipeCullScopeBegin(BACK) /
// pipeCullScopeEnd(). The scope End is the AUTHORITATIVE post-span
// re-assert (the RECORDED mode, all three layers); this resync is a
// redundant safety net that stays in place before every End.
// ============================================================
static void
MirrorPass_ResyncCull(void)
{
	// No device, no layer to repair — also skips the rw Get (the
	// caller-side truth read is pointless without a live device).
	if(!d3d9device)
		return;

	RwUInt32 rwCull = (RwUInt32)rwCULLMODECULLBACK;
	RwRenderStateGet(rwRENDERSTATECULLMODE, &rwCull);
	// Out-of-domain (failed/foreign Get) is NEVER pushed: BACK.
	if(rwCull != rwCULLMODECULLNONE && rwCull != rwCULLMODECULLBACK &&
	   rwCull != rwCULLMODECULLFRONT)
		rwCull = (RwUInt32)rwCULLMODECULLBACK;

	pipeForceCullMode(rwCull);

	if(dbglog_throttle("cull_resync"))
		dbglog("[CullResync] mirror exit rw=%u", (unsigned)rwCull);
}

// ============================================================
// The hook — CMain::Render's call to CMirrors::RenderMirrorBuffer
// (0x53EABA). Always runs the game's function, then decides whether to
// overdraw its fullscreen blit with the projective composite.
// ============================================================
static void
MirrorPass_RenderMirrorBuffer(void)
{
	PF_SCOPE("MirrorPass");

	// Cull span guard (authoritative post-span re-assert). The game runs
	// this whole call with CULLNONE and restores it through an L1-only
	// RwRenderStateSet, while our pipe callbacks inside the span preserve
	// NONE on all three layers and the composite's raw-geom restore can
	// leave the device on NONE. Record the mode the main view must resume
	// (BACK) here; pipeCullScopeEnd() re-asserts it on ALL THREE layers at
	// every exit below (4 early returns + the fallthrough tail = 5 sites,
	// one End per path, so the pair is balanced on every invocation).
	pipeCullScopeBegin(rwCULLMODECULLBACK);

	// Instrumentation (additive, throttled): the displayed gate inputs at
	// hook entry — TypeOfMirror drives the whole decision.
	if(dbglog_throttle("mirror_entry"))
		dbglog("[MirrorPass] hook: TypeOfMirror=%d mirrorDistortionFix=%d STAIRS=%d bFudgeNow=%d pBuffer=%p",
			(int)CMirrors__TypeOfMirror,
			config ? (int)config->mirrorDistortionFix : -1,
			(int)(CMirrors__MirrorFlags & MIRROR_FLAG_STAIRS),
			(int)bFudgeNow, (void*)CMirrors__pBuffer);

	// ---- band fix: capture + FFP-force BEFORE the vanilla blit ----
	// One-shot [mirror] receipt: the stale pair the blit would have run
	// with, captured (AddRef'd by Get*) on the first mirror-active frame
	// — before that, no pre-pass has run and the pair is meaningless.
	// The handles are deliberately NOT re-bound (leave-FFP doctrine, see
	// MirrorPass_ForceFFP); they are released on every exit path below.
	IDirect3DVertexShader9 *staleVS = NULL;
	IDirect3DPixelShader9  *stalePS = NULL;
	IDirect3DBaseTexture9  *staleS0 = NULL;
	static bool s_blitReceiptDone = false;
	if(!s_blitReceiptDone &&
	   CMirrors__TypeOfMirror != MIRROR_TYPE_NONE &&
	   d3d9device){
		s_blitReceiptDone = true;
		d3d9device->GetVertexShader(&staleVS);
		d3d9device->GetPixelShader(&stalePS);
		d3d9device->GetTexture(0, &staleS0);
		dbglog("[MirrorPass] blit site 0x53EABA: staleVS=%p stalePS=%p s0=%p -> FFP force applied (raw VS/PS NULL + seeds 0x8E2448/0x8E244C=NULL), leave-FFP on exit",
			(void*)staleVS, (void*)stalePS, (void*)staleS0);
	}
	MirrorPass_ForceFFP();

	// 1. game first — full behavioural parity (8-track screens, trailing
	//    stadium quad, state baseline).
	bool originalRan = false;
	if(CMirrors__RenderMirrorBuffer_orig){
		CMirrors__RenderMirrorBuffer_orig();
		originalRan = true;
	}

	// One-shot discriminator (next-run forensics): the device state right
	// after the game's function returns. NULL/NULL = the FFP force held
	// through the span (span holds only FFP consumers — exe-verified);
	// non-NULL = something inside the span re-bound object-pipe shaders
	// (would falsify the outside-the-span structure and demand a finer
	// hook at the blit call site 0x72627F).
	static bool s_postOrigReceiptDone = false;
	if(originalRan && !s_postOrigReceiptDone &&
	   CMirrors__TypeOfMirror != MIRROR_TYPE_NONE &&
	   d3d9device){
		s_postOrigReceiptDone = true;
		IDirect3DVertexShader9 *postVS = NULL;
		IDirect3DPixelShader9  *postPS = NULL;
		d3d9device->GetVertexShader(&postVS);
		d3d9device->GetPixelShader(&postPS);
		dbglog("[MirrorPass] post-orig device state: VS=%p PS=%p (NULL/NULL = FFP force held through the span)",
			(void*)postVS, (void*)postPS);
		if(postVS) postVS->Release();
		if(postPS) postPS->Release();
	}

	// 2. gates (cheap int reads, no RW calls — default-off costs one
	//    branch per frame, and only while a mirror is even active does
	//    the function run at all).
	if(!config || !config->mirrorDistortionFix){
		// Instrumentation (additive, throttled): fix off / no config —
		// no overdraw, vanilla output stands.
		if(dbglog_throttle("mirror_gate_fixoff"))
			dbglog("[MirrorPass] gate: mirrorDistortionFix off (config=%p) — no composite overdraw",
				(void*)config);
		// The original already ran its state baseline — re-sync cull
		// from the rw cache before handing control back to the game.
		// Leave-FFP exit (band fix): re-assert the force + release the
		// entry captures; the captured pair is never re-bound.
		MirrorPass_LeaveFFP(staleVS, stalePS, staleS0);
		if(originalRan)
			MirrorPass_ResyncCull();
		// Span exit: End is authoritative (Resync above is a safety net).
		pipeCullScopeEnd();
		return;
	}
	if(CMirrors__TypeOfMirror == MIRROR_TYPE_NONE){
		// Instrumentation (additive, throttled).
		if(dbglog_throttle("mirror_gate_none"))
			dbglog("[MirrorPass] gate: TypeOfMirror=MIRROR_TYPE_NONE — no mirror active, no composite overdraw");
		// Non-mirror branch: still re-sync if (and only if) the game's
		// function ran — it may have left the device off the cache.
		MirrorPass_LeaveFFP(staleVS, stalePS, staleS0);
		if(originalRan)
			MirrorPass_ResyncCull();
		// Span exit: End is authoritative (Resync above is a safety net).
		pipeCullScopeEnd();
		return;
	}
	// 8-track / fudged path drew Im3D screens instead of the blit — no
	// fullscreen quad to correct.
	if((CMirrors__MirrorFlags & MIRROR_FLAG_STAIRS) || bFudgeNow){
		// Instrumentation (additive, throttled).
		if(dbglog_throttle("mirror_gate_stairs"))
			dbglog("[MirrorPass] gate: 8-track/fudge path (STAIRS=%d bFudgeNow=%d MirrorFlags=0x%02X) — Im3D screens, no composite overdraw",
				(int)(CMirrors__MirrorFlags & MIRROR_FLAG_STAIRS), (int)bFudgeNow,
				(unsigned)CMirrors__MirrorFlags);
		MirrorPass_LeaveFFP(staleVS, stalePS, staleS0);
		if(originalRan)
			MirrorPass_ResyncCull();
		// Span exit: End is authoritative (Resync above is a safety net).
		pipeCullScopeEnd();
		return;
	}
	// Trailing stadium quad region — the game draws world-space geometry
	// with the mirror texture after its blit; don't bury it.
	{
		CVector cam = TheCamera.GetPosition();
		float dx = cam.x - kMirrorStadiumQuadPoint.x;
		float dy = cam.y - kMirrorStadiumQuadPoint.y;
		float dz = cam.z - kMirrorStadiumQuadPoint.z;
		if(dx*dx + dy*dy + dz*dz < kMirrorStadiumQuadRadiusSq){
			// Instrumentation (additive, throttled).
			if(dbglog_throttle("mirror_gate_stadium"))
				dbglog("[MirrorPass] gate: stadium quad region (cam=%.1f,%.1f,%.1f distSq=%.1f < %.1f) — no composite overdraw",
					cam.x, cam.y, cam.z, dx*dx + dy*dy + dz*dz, kMirrorStadiumQuadRadiusSq);
			MirrorPass_LeaveFFP(staleVS, stalePS, staleS0);
			if(originalRan)
				MirrorPass_ResyncCull();
			// Span exit: End is authoritative (Resync above is a safety net).
			pipeCullScopeEnd();
			return;
		}
	}

	// 3. overdraw the vanilla blit with the per-pixel projective fetch.
	// Instrumentation (additive, throttled): every gate passed.
	if(dbglog_throttle("mirror_gates_passed"))
		dbglog("[MirrorPass] gates passed (TypeOfMirror=%d STAIRS=0 bFudgeNow=0) — composite overdraw running",
			(int)CMirrors__TypeOfMirror);
	bool composed = MirrorPass_Composite();
	// Instrumentation (additive): profiler draw counter for the
	// fullscreen composite quad (4 verts, 2 tris).
	if(composed)
		PfDraw(4, 2);
	// Instrumentation (additive, throttled): did the overdraw actually
	// draw? false means a pre-flight bail (no camera/raster/buffer).
	if(dbglog_throttle("mirror_composite_ran"))
		dbglog("[MirrorPass] composite overdraw ran=%d (PS=IDR_MIRROR_COMPOSITE sh=%p screen=%dx%d mirror=%p %dx%d)",
			(int)composed, (void*)mirrorCompositePS,
			Scene.camera && RwCameraGetRaster(Scene.camera) ? RwCameraGetRaster(Scene.camera)->width : 0,
			Scene.camera && RwCameraGetRaster(Scene.camera) ? RwCameraGetRaster(Scene.camera)->height : 0,
			(void*)CMirrors__pBuffer,
			CMirrors__pBuffer ? CMirrors__pBuffer->width : 0,
			CMirrors__pBuffer ? CMirrors__pBuffer->height : 0);

	// Re-sync the three cull layers from the rw-cache truth after the
	// original render AND the composite/restore work above. The scope End
	// below is the AUTHORITATIVE re-assert; Resync is a redundant net.
	// Leave-FFP exit (band fix): re-assert the force + release the entry
	// captures AFTER the composite's own restore — the composite PS is
	// already NULLed through the rw cache by MirrorPass_CompositeRestore,
	// so the raw re-assert here is a pinned-layer no-op that guarantees
	// the leave-FFP contract on the fallthrough path too.
	MirrorPass_LeaveFFP(staleVS, stalePS, staleS0);
	if(originalRan)
		MirrorPass_ResyncCull();
	// Span exit (fallthrough): must pair with every other exit path.
	pipeCullScopeEnd();
}

// ============================================================
// Device-Reset latch reset — cross-lane contract (stages-0-3 L8).
// A D3D9 shader handle dies with the device: drop the cached handle
// AND re-arm the lazy one-shot load, so the next composite re-creates
// it; if that reload fails, the !mirrorCompositePS gate in
// MirrorPass_Composite falls back to the vanilla blit. Wired from
// depthhook.cpp's post-Reset block (L9 owns the extern + the call —
// declared there; no header change needed here).
// ============================================================
void
MirrorResetShaderLatches(void)
{
	mirrorCompositePS = NULL;
	mirrorPSLoadTried = false;
}

// ============================================================
// Install — call from main.cpp installDeferredHooks() (after
// envmaphooks). Idempotent on its own so a double install can never
// stack hooks.
// ============================================================
void
mirrorPass_init(void)
{
	static bool s_installed = false;
	if(s_installed) return;
	s_installed = true;

	InterceptCall(&CMirrors__RenderMirrorBuffer_orig, MirrorPass_RenderMirrorBuffer, 0x53EABA);
	dbglog("[MirrorPass] hook installed at 0x53EABA (CMirrors::RenderMirrorBuffer call site), mirrorDistortionFix=%d",
		config ? (int)config->mirrorDistortionFix : -1);
}

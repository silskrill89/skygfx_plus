// render_callbacks.cpp — default atomic render-callback machinery
// extracted from main.cpp (2026-11-09). Bodies byte-identical to the
// pre-extraction main.cpp. Owns:
//   * atomicHasAlpha() — colour/prelit/texture alpha probe for the dual-pass gate
//   * the PED OPAQUE CONTRACT (PedOpaqueState + pedOpaqueCapture/Diverged/
//     Enforce/Restore) — three-layer canonical-opaque signature for skin
//   * pedStage0Resync() — stage-0 texture-raster cache<->device repair
//   * vs_probe() — vertex-stage drift probe (ped_exit; frame_end via main.cpp)
//   * myDefaultCallback() — the game's AtomicDefaultRenderCallBack
//     (hooked at 0x7491C0 from main.cpp DllMain)
// No install hook: the 0x7491C0 intercept stays in main.cpp DllMain.
#include "skygfx.h"
#include "debug_handl.h"
#include "peds/peds_pipe.h"
#include "peds/skin.h"
#include "texdefs.h"

// ===== Local externs =====
// Symbols still owned by main.cpp (the alpha-ref trio / dual-pass helpers) and
// by other TUs (pipelinecommon.cpp). Local-extern idiom (no shared header),
// matching shadows.cpp / rw_fixes.cpp / config.cpp.
extern RwUInt32 pipeAlphaFuncToD3D(RwUInt32 rwFunc);          // pipelinecommon.cpp
extern int clampAlphaRef(int ref);                            // main.cpp
extern void setAlphaTestTwoLayer(RwUInt32 rwFunc, int ref);   // main.cpp
extern void setZWriteTwoLayer(RwBool on);                     // main.cpp

// buildingPipe.cpp — three-layer alpha/blend repair (block comment there).
// Forward-declared for the PED pipe-group entry contract below: the ped
// half needs the SAME layer-3 repair the building Switch gets, and the
// declaration near the skidmark draw (line ~1240) sits after this use.
extern void pipeForceAlphaBlock(void);

// Atomic-level analogue of D3D9RenderDual's per-mesh gate (see the
// `dual && hasAlpha && zwrite` test in D3D9RenderDual). That helper reads
// D3DRS_ALPHABLENDENABLE — the vertex-alpha state the callback has ALREADY
// set for the mesh about to draw — which does not exist yet here: this
// callback runs the whole pipeline as one unit, before any per-mesh state.
// Probe the geometry the instance callback derives that flag from instead:
// material colour alpha plus the prelit colour alpha channel (the same
// source _rpD3D9VertexDeclarationInstColor folds into instData->vertexAlpha).
// True only if the atomic can actually produce a blended mesh — the only
// case the two-leg pair helps: leg 1 seeds depth at the threshold, leg 2
// (LESS@threshold, zwrite OFF) paints the part below it. On an opaque mesh
// leg 2 is pure overdraw, and with blending off its LESS test rasterises
// every texture texel below the threshold into a solid quad.
// `includeTextureAlpha` is the PED-side extension (diagnostic only: the ped
// dual pass is unconditional, see the legacy gate below). CJ head/hair/
// bandana materials are alpha-255 over CUTOUT texture masks, which the
// colour/prelit probe above cannot see. TexAlpha_Classify caches per raster,
// so the extra walk is cheap.
static bool
atomicHasAlpha(RpAtomic *atomic, bool includeTextureAlpha)
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
			// Local extern (texalpha.cpp): TAC_CUTOUT=2, TAC_FADED=3
			// (enum TexAlphaClass in texalpha.h; not included here).
			extern int TexAlpha_Classify(const RwTexture *tex);
			for(RwInt32 i = 0; i < geom->matList.numMaterials; i++){
				RpMaterial *tm = geom->matList.materials[i];
				if(tm && tm->texture){
					int tac = TexAlpha_Classify(tm->texture);
					if(tac == 2 || tac == 3)
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

// ============================================================================
// SUBSYSTEM: PED OPAQUE CONTRACT   (extraction: render/pedContract.cpp)
// ============================================================================
// Scope-isolated for a mechanical later extraction. Helpers below are static,
// file-local, and called only from myDefaultCallback's isPedPipe branch. The
// callback itself stays in main.cpp. Do not add unrelated code here.
//
// TYPES / GLOBALS:
//   PedOpaqueState   POD snapshot: L1 rw cache, L2 RW driver cache, L3 device
//                    (scalar members only - C2712-safe)
//   No file-scope state: the live snapshot is `savedOpaque` + `pedContractOn`,
//   POD locals in myDefaultCallback (isPedPipe branch); the exit monitor keeps
//   `pedRsFirst`.
//
// ENTRY POINTS:
//   pedOpaqueCapture()   entry snapshot; pre-seeds canonical values, then
//                        range-sanitizes every Get result
//   pedOpaqueDiverged()  pure predicate: entry differs from the canonical
//                        opaque signature
//   pedOpaqueEnforce(alphaTestEnable)  canonical opaque signature through
//                        L1+L2+L3; alphaTestEnable 0 disables the test.
//                        Runs UNCONDITIONALLY (build-11 transparent-ped
//                        fix) — pedOpaqueContract gates only the exit.
//   pedOpaqueRestore()   per-layer exit restore; ZWRITE deliberately excluded
//   [PedRS] monitor      inline exit block in myDefaultCallback: throttled
//                        3-layer readback of cull/blend/src/dst/zw/afunc/aref
//
// INVARIANTS:
//   * Enforce is the LAST writer before the skin pipeline: it runs after
//     pipeEnterAlphaMode + pipeForceAlphaBlock + the saved alpha-pair restore.
//     It is UNCONDITIONAL (build-11 transparent-ped fix): the canonical
//     opaque signature (GE@1 + VERTEXALPHA/blend OFF) holds even with
//     pedOpaqueContract=0, which gates only the exit restore below.
//   * Restore runs after pipeExitAlphaMode. D3DRS_ZWRITEENABLE belongs to the
//     sticky three-layer ZWRITE repair, never to pedOpaqueRestore.
//   * The depth trio (ZWRITE/ZTEST/ZENABLE/ZFUNC) is forced UNCONDITIONALLY
//     at entry (build-11 draw-over-CJ fix) and sticky-repaired at exit: a
//     divergent capture is never re-emitted (canonical LESSEQUAL/TRUE left
//     behind), a clean capture restores verbatim.
//   * Capture/restore are unconditional when the contract ran; no early return
//     may skip the exit restore.
//
// FUTURE render/pedContract.cpp PUBLIC API (exact exports):
//   void PedOpaque_Capture(PedOpaqueState *s);
//   bool PedOpaque_Diverged(const PedOpaqueState *s);
//   void PedOpaque_Enforce(int alphaTestEnable);
//   void PedOpaque_Restore(const PedOpaqueState *s);
//   void PedRS_Monitor(void);   // NEW: carved out of the callback exit block
//   (PedOpaqueState moves to render/pedContract.h verbatim - passed by pointer.)
//
// MOVES TOGETHER (copy-paste set):
//   src/core/main.cpp   this region (PedOpaqueState + capture/diverged/enforce/
//                       restore)
//   src/core/main.cpp   myDefaultCallback isPedPipe entry block (capture, gate,
//                       divergence receipt, enforce)
//   src/core/main.cpp   myDefaultCallback exit block (pedOpaqueRestore call +
//                       [PedRS] monitor)
//   src/skygfx.h        pedOpaqueContract Config member + comment
//   Consumers stay put: modeManager.cpp preset rows; pipelinecommon.cpp exit
//   ordering comment.
// ============================================================================
// ============================================================
// PED OPAQUE CONTRACT (myDefaultCallback, isPedPipe branch)
// ============================================================
// Peds/skin are opaque geometry. Three independent leaks make CJ transparent
// or partially drawn in third person:
//   * ALPHAREF: envmap.cpp's outdoor world pass parks the rw cache at 140
//     (CRenderer::RenderRoadsAndBuildings, currArea==0) MID-FRAME for the
//     reflection map. The ped entry used to hand that value straight back to
//     the device, so every alpha-tested skin/clothing texel below 140 was
//     discarded — partial or fully missing peds on docks/beaches.
//   * ALPHABLENDENABLE/VERTEXALPHAENABLE: SA's skin instance callback derives
//     a whole-mesh vertexAlpha flag; a leaked TRUE blends the entire ped.
//   * ZWRITE/ZENABLE/ZFUNC: postfx/DepthHook raw writes drift the device
//     behind an unchanged rw cache -> ped depth never registers.
// The ped branch forces the canonical opaque signature through all three
// layers (rw cache / RW driver cache / raw device), logs the true pre-entry
// device state once per run, and restores each layer's own captured value on
// exit (the existing sticky ZWRITE repair keeps ownership of ZWRITE).
// PedOpaqueState is plain POD: scalar members only.
struct PedOpaqueState {
	// L1 rw render-state cache
	RwUInt32 rwVtxAlpha, rwZWrite, rwZTest, rwAlphaFunc, rwAlphaRef;
	// L2 RW D3D9 driver cache
	DWORD drvBlend, drvZWrite, drvZEnable, drvZFunc, drvAlphaTest, drvAlphaFunc, drvAlphaRef;
	// L3 raw device
	DWORD devBlend, devZWrite, devZEnable, devZFunc, devAlphaTest, devAlphaFunc, devAlphaRef;
};

static void
pedOpaqueCapture(PedOpaqueState *s)
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
pedOpaqueDiverged(const PedOpaqueState *s)
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
pedOpaqueEnforce(int alphaTestEnable)
{
	// CULLMODE is deliberately absent here: the cull writers are
	// pipeEnterCullMode() at entry, the pedCullMode block's unconditional
	// pipeForceCullMode + the draw-boundary re-assert in myDefaultCallback
	// (3-layer paired), and pipeExitCullMode() at exit — with
	// pedCullMode == -1 (default) that is CULLBACK for skin in the main
	// view (the foreign scope's live mode inside a mirror/shadow span).
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
pedOpaqueRestore(const PedOpaqueState *s)
{
	// L1 rw cache. ZWRITE is deliberately absent: the existing sticky-repair
	// block in myDefaultCallback owns D3DRS_ZWRITEENABLE (it restores the
	// canonical TRUE after a detected entry desync instead of recreating it).
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


// ============================================================
// Vertex-stage drift probe (FIX 3 diagnostic — log only, NO repair).
// RW's value-compare caches (_rwD3D9LastVertexShaderUsed 0x8E2448,
// _rwD3D9LastVertexDeclarationUsed 0x8E2444) must mirror the device after
// every draw (normalmap_plugin.cpp:469 documents the invariant: a stale cache
// makes the next RwD3D9SetVertexShader/SetVertexDeclaration no-op, so the
// previous VS/decl stays bound -> wrong constant layout / wrong vertex
// parsing). Sample cache vs device at callback exits so the next test round
// has evidence of WHERE they diverge. Read the game bookkeeping globals
// directly: the rwcore.h _rwD3D9Last* symbols resolve to normmap_stubs.cpp
// locals, not the game's values. Throttled per site (2 s bucket).
// POD locals only; Get* AddRefs, both pointers Released below.
// ============================================================
void
vs_probe(const char *site, const char *tag)
{
	if(!d3d9device) return;
	void *cacheVS = *(void**)0x8E2448;
	void *cacheDecl = *(void**)0x8E2444;
	IDirect3DVertexShader9 *devVS = NULL;
	IDirect3DVertexDeclaration9 *devDecl = NULL;
	d3d9device->GetVertexShader(&devVS);
	d3d9device->GetVertexDeclaration(&devDecl);
	if((void*)devVS != cacheVS || (void*)devDecl != cacheDecl){
		// First mismatch is UNTHROTTLED: throttle buckets are shared and can
		// starve, which made a missing vs_probe line inconclusive.
		static bool s_vsProbeFirstMismatchLogged = false;
		if(!s_vsProbeFirstMismatchLogged){
			s_vsProbeFirstMismatchLogged = true;
			dbglog("vs_probe: FIRST mismatch site=%s cacheVS=%p devVS=%p cacheDecl=%p devDecl=%p",
				site, cacheVS, (void*)devVS, cacheDecl, (void*)devDecl);
		}
		if(dbglog_throttle(tag))
			dbglog("vs_probe: site=%s cacheVS=%p devVS=%p cacheDecl=%p devDecl=%p",
				site, cacheVS, (void*)devVS, cacheDecl, (void*)devDecl);
	}
	if(devVS) devVS->Release();
	if(devDecl) devDecl->Release();
}

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
static void
pedStage0Resync(RpAtomic *atomic, PedPipeState *pdsState)
{
	if(!d3d9device)
		return;
	// PedsPipe absorb (design doc §3.1 — SkinStage0Resync state lane):
	// per-clump stage-0 latch bookkeeping. pdsState is non-NULL only when
	// the module is enabled AND owns this atomic (classification produced);
	// the receipts below are the legacy fallback and stay byte-identical.
	if(pdsState){
		pdsState->stage0Latch = 1;	// resync armed
		pdsState->lastResyncFrame = *(uint32_t*)0xB7CB4C;	// CTimer::m_FrameCounter
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
		RwTexture *resolved = recovered;
		if(resolved && TexDefsMissTextureKind(resolved) == TEXDEFS_MISS_TEX_SPRITE){
			if(dbglog_throttle("n9_pedcb_remap"))
				dbglog("[PedTex] N9: sprite-class miss sentinel re-mapped to grey world sentinel (was '%s')",
				       recovered->name[0] ? recovered->name : "?");
			resolved = TexDefsMissSentinelWorld();	// ONLY reached when kind==SPRITE: natural create
		}
		if(!resolved)
			resolved = gpWhiteTexture;
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
			if(pdsState)
				pdsState->stage0Latch = 2;	// recovered this clump
			if(dbglog_once("ped_tex_recovered"))
				dbglog("[PedTex] recovered %s (atomic=%p geom=%p nmats=%d)",
				       recovered->name[0] ? recovered->name : "?",
				       (void*)atomic, (void*)atomic ? (void*)atomic->geometry : NULL,
				       atomic && atomic->geometry ? (int)atomic->geometry->matList.numMaterials : -1);
		}else{
			if(pdsState)
				pdsState->stage0FailCount++;	// consecutive recovery failures
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
	// ext = raster + RwD3D9RasterExtOffset; ext+0 = IDirect3DTexture9*. The
	// ext+8 "valid" byte can be stale, so accept a plausible pointer on its own.
	IDirect3DTexture9 *tex = NULL;
	if(cur){
		DWORD extOff = RwD3D9RasterExtOffset;
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

// myDefaultCallback — the game's AtomicDefaultRenderCallBack (hooked at
// 0x7491C0). Every atomic without a custom renderCallBack dispatches here,
// which makes it the PED pipe group's render path: skinPipe atomics are
// recognised by `pipe == skinPipe` (the ambient override and dual pass
// below). The building and vehicle groups each own a Switch callback with a
// three-layer state entry contract; this one had none — see the ped entry
// block below.
RpAtomic*
myDefaultCallback(RpAtomic *atomic)
{
	RxPipeline *pipe;
	// Pre-seeded (never bare): a failed RwRenderStateGet leaves the out-param
	// untouched, and the dual-pass restore pushes these straight back — an
	// uninitialized func/ref must never reach the device. Same convention as
	// D3D9RenderDual / grassRenderCallback / CSkidmarks__Render.
	int zwrite = TRUE;
	int alphatest = rwALPHATESTFUNCTIONGREATEREQUAL;
	int alpharef = 1;
	int dodual = 0;
	int detach = 0;

	// Atomic guard (audit reorder): every atomic deref below (atomic->pipeline
	// here, atomic->clump / Skin_ClassifyAtomic in the PedsPipe acquire) is now
	// covered by this early-out — the old `&& atomic` term in the acquire sat
	// AFTER the atomic->pipeline read it was meant to protect. NULL return
	// matches the `pipe ? atomic : NULL` exit contract.
	if(atomic == NULL)
		return NULL;

	pipe = atomic->pipeline;
	// Ped pipe group flag, captured BEFORE the NULL-pipe branch overwrites
	// `pipe` with the default pipeline. Scoped to peds only: this callback
	// also runs every other default-pipeline atomic (props, pickups, marker
	// objects), which must keep their current (unguarded) behaviour.
	bool isPedPipe = (pipe == skinPipe);
	// ===== PedsPipe Phase 2 wrap (design doc §2.1 wrap-first) =====
	// The 0x7491C0 hook STAYS in main.cpp; the module only supplies
	// per-clump state. Acquire + classify at ped-span entry: both no-op
	// while pedsPipeEnable=0 (single bool read) and Skin_ClassifyAtomic is
	// a Phase-1 stub (always false), so every ped block below keeps its
	// legacy statics/behaviour byte-identical until real classification
	// lands in skin.cpp. pedsPipeState is the span-wide
	// PedsPipe_GetState(atomic) read (AcquireState is idempotent and
	// returns the live slot value).
	PedPipeState *pedsPipeState = NULL;
	bool pedsPipeClassified = false;
	if(isPedPipe && PedsPipe_GetEnabled() && atomic){
		pedsPipeState = PedsPipe_AcquireState(atomic->clump);
		if(pedsPipeState){
			pedsPipeState->lastSeenFrame = *(uint32_t*)0xB7CB4C;	// CTimer::m_FrameCounter
			pedsPipeClassified = Skin_ClassifyAtomic(atomic, pedsPipeState);
			// Design doc §7 Phase 1 acceptance probe: one-shot wrap
			// receipt correlating with the existing ped_opaque_pre.
			if(PedsPipe_ProbeOnce("peds_pipe_wrap"))
				dbglog("[PedsPipe] wrap active (atomic=%p clump=%p state=%p classified=%d slot=%d)",
					(void*)atomic, (void*)atomic->clump, (void*)pedsPipeState,
					(int)pedsPipeClassified, PedsPipe_GetSlotOffset());
		}
	}
	if(pipe == NULL){
		// NULL-pipe fallback (audit): guard the RW engine instance at
		// 0xC97B24 (same global texdefs.cpp polls before attach — NULL until
		// RwEngineInit completes) AND the resolved default pipeline. A NULL
		// after both attempts early-outs BEFORE the entry-contract writes
		// below: on this path nothing render-state has been touched yet (the
		// PedsPipe acquire above only writes its diagnostic struct), so the
		// plain NULL return is the same `pipe ? atomic : NULL` contract the
		// exit already uses — no restore needed.
		DWORD engine = *(DWORD*)0xC97B24;
		pipe = engine ? *(RxPipeline**)(engine + 0x3C + dword_C9BC60) : NULL;
		if(pipe == NULL)
			return NULL;
		RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, (void*)&zwrite);
		if(zwrite && config->dualPassDefault)
			dodual = 1;
		// ===== DEFAULT-pipe entry contract =====
		// The default pipeline (props, pickups, marker objects) had NO
		// entry contract at all — unlike the ped contract below, the
		// building Switch and the vehicle Switch — so it drew with
		// whatever the previous pass leaked: a CCoronas::Render exit
		// leaves additive ONE/ONE + VERTEXALPHA TRUE + ZWRITE FALSE live
		// (vanilla restores nothing there), and these atomics ghosted
		// (additive opaque geometry, no depth writes). Force the canonical
		// world signature through ALL THREE layers (rw cache / RW driver
		// cache / raw device, dev null-guarded) BEFORE the draw, mirroring
		// the ped entry contract above. The zwrite read above
		// deliberately stays FIRST: the dual-pass gate must keep sampling
		// the INHERITED state (vanilla semantics), not the forced value.
		// No save/restore: these ARE the world's own normal-state values,
		// so leaving them behind is the repair, not a leak. BLENDOP and
		// ALPHATESTENABLE have no rw render state (same rule as
		// pipeForceAlphaBlock) — they ride L2/L3 only. ALPHAFUNC/ALPHAREF
		// DO have rw twins and are forced through ALL THREE layers
		// (fix-34): without them the markers alpha-tested at whatever ref
		// the previous pass inherited (e.g. envmap.cpp:448's outdoor park
		// at 140), discarding every marker texel below the leaked ref.
		// (1) rw cache
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONGREATEREQUAL);
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)1);
		// (2) RW D3D9 driver cache (pending[], flushed pre-draw)
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		RwD3D9SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		RwD3D9SetRenderState(D3DRS_ZENABLE, TRUE);
		// ZFUNC has no rw render state (L2+L3 paired, same rule as
		// pipeForceAlphaBlock / pedOpaqueEnforce). Build-11 "props draw over
		// CJ": this contract forced ZTEST/ZENABLE but never ZFUNC, so a
		// postfx/DepthHook raw drift (pipelinecommon :1400 evidence
		// "ZFUNC drv=4/dev=8" — device at D3DCMP_ALWAYS) survived here and
		// every prop/markers draw after CJ passed its depth test over him.
		RwD3D9SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
		RwD3D9SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D(rwALPHATESTFUNCTIONGREATEREQUAL));
		RwD3D9SetRenderState(D3DRS_ALPHAREF, 1);
		// (3) raw device — repairs drift the caches can't see
		if(d3d9device){
			d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
			d3d9device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			d3d9device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
			d3d9device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
			d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
			d3d9device->SetRenderState(D3DRS_ZENABLE, TRUE);
			d3d9device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
			d3d9device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
			d3d9device->SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D(rwALPHATESTFUNCTIONGREATEREQUAL));
			d3d9device->SetRenderState(D3DRS_ALPHAREF, 1);
		}
		// One-shot throttled log tag "defpipe_contract".
		static bool defpipeLogged = false;
		if(!defpipeLogged && dbglog_throttle("defpipe_contract")){
			defpipeLogged = true;
			dbglog("[DefPipe] entry contract forced canonical world "
			       "signature (vtxalpha=0 src=SRCALPHA dst=INVSRCALPHA "
			       "op=ADD zwrite=1 ztest=1 zfunc=LE atest=1 afunc=GE "
			       "aref=1) via L1+L2+L3; victims if zfunc leaked: CJ "
			       "overdrawn by props/markers (draw-over-CJ)");
		}
	}else if(isPedPipe && ((config->dualPassPed && config->zwriteThresholdPed > 0) || (pedsPipeState && (pedsPipeState->dualPassMask & 1u)))){
		dodual = 1;
		// PedsPipe absorb (design doc §3.1 — SkinDrawDual gate): a
		// classified state vetoes the dual pass for atomics whose
		// dualPassMask bit0 is clear (bit set = needs the second leg).
		// When dualPassPed=0 (MODE_PS2 / MODE_GTAIV) the per-atomic
		// dualPassMask bit (set by Skin_ClassifyAtomic for every textured
		// atomic, head parts included — skin.h SKINDUAL_SECOND_LEG) keeps
		// the second leg for head parts; the config-only gate remains the
		// byte-identical fallback on the legacy (pedsPipeState == NULL)
		// rollback path, where the mask term is a constant FALSE.
		if(pedsPipeClassified && pedsPipeState && (pedsPipeState->dualPassMask & 1u) == 0)
			dodual = 0;
	}

	// ===== PED pipe group entry contract =====
	// Mirror of the building Switch (buildingPipe.cpp:1167-1204) and the
	// vehicle Switch (vehiclePipe.cpp:2385-2416): cull + alpha/blend + the
	// dual-layer ZWRITE force, all three layers (rw cache / driver pending[]
	// / device applied[]) per pipelinecommon.cpp:883. Until now the ped path
	// was the ONLY pipe group with no repair at all — it read and wrote
	// ZWRITE and alpha-test func+ref through the rw cache ONLY, so a DEVICE
	// that drifted behind an unchanged rw cache (postfx Save/RestoreRawGeomStates
	// raw writes, DepthHook raw ZENABLE, the fullscreen Im2D passes) stayed
	// wrong for the whole ped pass:
	//   rw ZWRITE=TRUE / dev ZWRITE=FALSE -> peds drew WITHOUT depth writes,
	//     the depth buffer never received the ped's depth, and everything
	//     drawn after peds (pickup marker/cone, effects, world geometry)
	//     depth-tested against a buffer with no peds in it: the marker
	//     painting in front of a ped's head, other pipes' geometry clipping
	//     through peds. Building/vehicle repair this at THEIR entry, so the
	//     hole only ever showed on the one pipe with no guard — the ped pipe.
	//   cull / alpha-test drift -> double-sided or wrongly-tested peds, plus
	//     the same stale state handed to the next consumer.
	//
	// Save/restore contract identical to the other two switches: force each
	// block through both layers for the duration of this cb; exit restores
	// each layer to its OWN pre-entry value (postfx manages the raw device
	// side itself with its own save/restore — syncing the sides here would
	// fight that contract).
	RwUInt32 savedCull = (RwUInt32)rwCULLMODECULLBACK;
	// Effective ped span cull for the draw-boundary re-assert: assigned in
	// the pedCullMode block below (the explicit 3-layer entry set), re-set
	// through pipeForceCullMode on the last line before the draw so no
	// shield/restore block in between can clobber cull mid-ped-draw.
	RwUInt32 pedCullDraw = (RwUInt32)rwCULLMODECULLBACK;
	// Pre-seeded with the canonical scene block (never `{}`): colourWrite=0
	// would blank every channel if pipeExitAlphaMode were ever reached
	// without a matching enter.
	PipeAlphaState savedAlpha = {
		FALSE, rwBLENDSRCALPHA, rwBLENDINVSRCALPHA,
		rwALPHATESTFUNCTIONGREATEREQUAL, 0,
		FALSE, D3DCOLORWRITEENABLE_ALL
	};
	RwBool savedZWriteRw = TRUE;
	DWORD savedZWriteDev = TRUE;
	DWORD savedZWriteDrv = TRUE;	// layer 2 (RW driver cache) pre-entry
	// Sticky-repair flag: set when entry found ZWRITE off/desynced. When set,
	// the exit below restores the REPAIRED canonical TRUE to all three layers
	// instead of re-creating the pre-entry desync — otherwise the exit
	// faithfully restored the leak every frame and the entry guard re-detected
	// and re-repaired it next frame (the recurring "[PED] ZWRITE" log).
	bool zwriteWasDesync = false;
	// Depth-trio sticky flag (ZTEST/ZENABLE/ZFUNC half of the same rule):
	// set when the entry depth state was divergent anywhere, so the exit
	// leaves the canonical depth signature behind instead of re-emitting the
	// captured leak (see the sticky-depth block in the exit contract).
	bool depthWasDesync = false;
	// Pre-entry snapshot for the ped opaque contract (capture() pre-seeds, so
	// a disabled contract or a failed Get still leaves a sane struct). The
	// gate is latched once so the exit restore always matches the entry force
	// even if the INI value changes mid-frame.
	PedOpaqueState savedOpaque = {};
	bool pedContractOn = false;
	bool pedAlphaTestOn = true;
	if(isPedPipe){
		// PedsPipe absorb point (design doc §3.1 — SkinEntryContract lane:
		// cull-mode A/B lever, pedAlphaTest A/B lever, sticky ZWRITE repair,
		// depth-trio force, opaque contract capture/enforce). Phase 3
		// FLIPPED: the module owns this span when pedsPipeState is non-NULL
		// (acquired above) — SkinEntryContract runs the whole absorbed chain
		// from skin.cpp, contract state in PedPipeState. pedsPipeState ==
		// NULL keeps the legacy inline block below byte-for-byte (the
		// pedsPipeEnable=0 rollback lever, skin.h:8 / peds_pipe.h:34).
		if(pedsPipeState){
			SkinEntryContract(pedsPipeState);
		}else{
		pedContractOn = (config->pedOpaqueContract != 0);
		pedAlphaTestOn = (config->pedAlphaTestEnable != 0);
		// pedAlphaTestEnable A/B receipt (tag ped_atest), re-armed on change
		// so an INI reload is visible in the log (dbglog_once cannot re-fire).
		static int pedAlphaTestLogged = -2;
		if(pedAlphaTestLogged != (pedAlphaTestOn ? 1 : 0)){
			pedAlphaTestLogged = pedAlphaTestOn ? 1 : 0;
			dbglog("[PedAlphaTest] enabled=%d", pedAlphaTestLogged);
		}
		pedOpaqueCapture(&savedOpaque);
		// One-shot per run: what the DEVICE actually had before any repair.
		// [PipeAlpha] only reports cache-layer drift; this is the ped-pipeline
		// truth (ref=140 here confirms the envmap outdoor leak reached peds).
		if(dbglog_once("ped_opaque_pre"))
			dbglog("[PedOpaque] pre-entry dev ALPHAREF=%u ALPHAFUNC=%u ALPHABLENDENABLE=%u ZWRITE=%u ZENABLE=%u ZFUNC=%u ALPHATEST=%u | rw ref=%u fn=%u vtx=%u zw=%u ztest=%u | drv ref=%u fn=%u blend=%u zw=%u",
				(unsigned)savedOpaque.devAlphaRef, (unsigned)savedOpaque.devAlphaFunc,
				(unsigned)savedOpaque.devBlend, (unsigned)savedOpaque.devZWrite,
				(unsigned)savedOpaque.devZEnable, (unsigned)savedOpaque.devZFunc,
				(unsigned)savedOpaque.devAlphaTest,
				(unsigned)savedOpaque.rwAlphaRef, (unsigned)savedOpaque.rwAlphaFunc,
				(unsigned)savedOpaque.rwVtxAlpha, (unsigned)savedOpaque.rwZWrite,
				(unsigned)savedOpaque.rwZTest,
				(unsigned)savedOpaque.drvAlphaRef, (unsigned)savedOpaque.drvAlphaFunc,
				(unsigned)savedOpaque.drvBlend, (unsigned)savedOpaque.drvZWrite);
		savedCull = pipeEnterCullMode();

		// ===== pedCullMode A/B lever + explicit CULL set (3-layer paired) =====
		// -1 (default) = the skin expectation CULLBACK in the main view: the
		// span mode pipeEnterCullMode enforced (BACK outside a foreign cull
		// scope; inside one — mirror/shadow spans — the scope's own live
		// mode, per the vehiclePipe "never a hardcoded BACK" rule,
		// vehiclePipe.cpp:2933-2940). 1/2/3 force CULLNONE/CULLBACK/CULLFRONT
		// through all three layers for this ped span; the exit's
		// pipeExitCullMode(savedCull) restores the pre-entry span mode.
		// Out-of-range behaves as -1.
		// The write is UNCONDITIONAL (build-11 transparent-ped fix): the ped
		// entry must SET cull explicitly and restore it — inheriting
		// "whatever the caches hold" is the CULLNONE/FRONT leak family
		// ("backfaces showing through" skin = angle-dependent see-through,
		// CULLFRONT = inside-out ped). pipeForceCullMode is THE 3-layer
		// paired write (L1 park+set / L2 park+set / L3 raw pin), so no
		// cache-hit skip can hold the force back.
		int pedCullMode = config->pedCullMode;
		if(pedCullMode < -1 || pedCullMode == 0 || pedCullMode > 3)
			pedCullMode = -1;
		RwUInt32 pedCullForced = savedCull;
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
		pedCullDraw = pedCullForced;	// draw-boundary re-assert value
		// One-shot receipt (tag ped_cull), re-armed whenever the effective
		// mode changes (dbglog_once alone cannot re-fire after an INI reload).
		static int pedCullLoggedMode = -2;
		if(pedCullLoggedMode != pedCullMode){
			pedCullLoggedMode = pedCullMode;
			dbglog("[PedCull] mode=%d saved=%u forced=%u",
				pedCullMode, (unsigned)savedCull, (unsigned)pedCullForced);
		}

		savedAlpha = pipeEnterAlphaMode();

		// ZWRITE three-layer guard — mirror of buildingPipe.cpp:1185-1195.
		// Pre-seeded TRUE: a failed Get leaves the out-param untouched, and
		// TRUE is the canonical scene value (same pre-seed as the others).
		RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, (void*)&savedZWriteRw);
		RwD3D9GetRenderState(D3DRS_ZWRITEENABLE, &savedZWriteDrv);
		if(d3d9device)
			d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &savedZWriteDev);
		// 3-layer parity with the building Switch (buildingPipe.cpp:3462):
		// ALL THREE captured layers count — a drv-only FALSE used to pass
		// this gate and was then faithfully re-emitted at exit.
		zwriteWasDesync = (!savedZWriteRw || !savedZWriteDrv || !savedZWriteDev);
		// ===== PED DEPTH-TEST CONTRACT (unconditional — build-11 fix) =====
		// "buildings/props draw over CJ" root family "Z-test state leaked
		// into the ped draw": the ped draw must ALWAYS run with ZTEST ON +
		// ZFUNC LESSEQUAL + ZWRITE ON — not only when pedOpaqueContract is
		// on (that gate below covers the alpha/blend signature). CJ's pixels
		// paint even with ZWRITE off, but his depth is never carved, so a
		// building/prop face drawn after him at its own face depth passes
		// LESSEQUAL and paints over the player. savedOpaque was captured
		// above (pedOpaqueCapture), so the receipt reports the true
		// pre-entry state. ZTEST TRUE has an rw twin (L1+L2+L3 paired);
		// ZFUNC has none (L2+L3 paired — pedOpaqueEnforce idiom).
		depthWasDesync = zwriteWasDesync ||
			!savedOpaque.rwZTest || !savedOpaque.drvZEnable || !savedOpaque.devZEnable ||
			savedOpaque.drvZFunc != D3DCMP_LESSEQUAL ||
			savedOpaque.devZFunc != D3DCMP_LESSEQUAL;
		if(depthWasDesync && dbglog_throttle("PedZDepth"))
			dbglog("[PedZW] depth drift on entry (rw zw=%d ztest=%d | drv zw=%d zen=%u zfunc=%u | dev zw=%d zen=%u zfunc=%u) — forced ZTEST=1 ZFUNC=LESSEQUAL ZWRITE=1 (3L); victims if leaked: CJ overdrawn by props/buildings (draw-over-CJ)",
				(int)savedZWriteRw, (int)savedOpaque.rwZTest,
				(int)savedZWriteDrv, (unsigned)savedOpaque.drvZEnable,
				(unsigned)savedOpaque.drvZFunc,
				(int)savedZWriteDev, (unsigned)savedOpaque.devZEnable,
				(unsigned)savedOpaque.devZFunc);
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
		// 0 -> 1) and pipeForceAlphaBlock re-pushes the CACHE's func/ref
		// through L2/L3 — not the caller's pre-entry pair. Put the captured
		// pair (savedAlpha, snapshotted before the entry contract ran) back
		// through L1 (rw setters) + L2 + raw L3 so the ped pass inherits
		// exactly what it found. ALPHATESTENABLE is deliberately NOT touched:
		// pipeForceAlphaBlock owns its force and the pre-entry value was
		// never captured.
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)savedAlpha.alphaFunc);
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)savedAlpha.alphaRef);
		RwD3D9SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D(savedAlpha.alphaFunc));
		RwD3D9SetRenderState(D3DRS_ALPHAREF, (RwUInt32)savedAlpha.alphaRef);
		if(d3d9device){
			d3d9device->SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D(savedAlpha.alphaFunc));
			d3d9device->SetRenderState(D3DRS_ALPHAREF, (DWORD)savedAlpha.alphaRef);
		}

		// ===== PED OPAQUE CONTRACT =====
		// Last writer before the skin pipeline executes: pipeForceAlphaBlock
		// and the saved-pair restore above deliberately put the PRE-ENTRY
		// alpha func/ref back, which is exactly how envmap's leaked ref=140
		// reached the device. Replace it with GREATEREQUAL@1 (rejects only
		// fully-transparent texels) and force blending off / depth writes on.
		// UNCONDITIONAL (build-11 transparent-ped fix, same rule the PED
		// DEPTH-TEST CONTRACT states above: "not only when
		// pedOpaqueContract is on"). With the bisect lever OFF (the live
		// INI ships pedOpaqueContract=0!) the old gate skipped this enforce
		// and the saved-pair restore above left the PRE-ENTRY pair live for
		// the draw — envmap's leaked GREATER@140 (envmap.cpp:448 outdoor
		// park) discards every skin/clothing texel <= 140 and the per-mesh
		// vertex-alpha decision blends whole meshes: partially transparent
		// peds ("alpha/blend decision varying per mesh"). The canonical
		// opaque signature — alpha test ON, GREATEREQUAL@1, VERTEXALPHA
		// OFF, blend OFF — must hold for every opaque skin span;
		// pedAlphaTestEnable=0 keeps its S5 test-off lane (blend by the
		// texel's own alpha). pedContractOn now gates ONLY the exit restore
		// below (pedOpaqueRestore vs the fix-34 sticky reset).
		{
			// Receipt: dbglog_once for the run's FIRST repair (cannot be
			// starved by bucket collisions — the old "ped_contract" tag
			// hashed to the same bucket as pipeline_cull_enter, which fired
			// ~9 ms earlier, so the contract line never appeared even though
			// the repair ran), then the throttled "ped_contract2" tag.
			if(pedOpaqueDiverged(&savedOpaque) &&
			   (dbglog_once("ped_contract_first") || dbglog_throttle("ped_contract2")))
				dbglog("[PedOpaque] contract repaired divergent entry: dev(ref=%u fn=%u blend=%u zw=%u zen=%u zfunc=%u atest=%u) rw(ref=%u fn=%u vtx=%u zw=%u ztest=%u) drv(ref=%u fn=%u blend=%u zw=%u)",
					(unsigned)savedOpaque.devAlphaRef, (unsigned)savedOpaque.devAlphaFunc,
					(unsigned)savedOpaque.devBlend, (unsigned)savedOpaque.devZWrite,
					(unsigned)savedOpaque.devZEnable, (unsigned)savedOpaque.devZFunc,
					(unsigned)savedOpaque.devAlphaTest,
					(unsigned)savedOpaque.rwAlphaRef, (unsigned)savedOpaque.rwAlphaFunc,
					(unsigned)savedOpaque.rwVtxAlpha, (unsigned)savedOpaque.rwZWrite,
					(unsigned)savedOpaque.rwZTest,
					(unsigned)savedOpaque.drvAlphaRef, (unsigned)savedOpaque.drvAlphaFunc,
					(unsigned)savedOpaque.drvBlend, (unsigned)savedOpaque.drvZWrite);
			if(!pedContractOn && dbglog_once("ped_enforce_uncond"))
				dbglog("[PedOpaque] entry enforce UNCONDITIONAL (pedOpaqueContract=0): canonical GE@1/blend-off/vertexalpha-off pinned for the span");
			pedOpaqueEnforce(pedAlphaTestOn ? 1 : 0);
		}
		}	// end legacy fallback (pedsPipeState == NULL)

		// Stage-0 texture-raster resync: the skin pipeline's per-material
		// bind must be a real transition (cache<->device split repair).
		// INI pedTexResync (default 1 = ON): 0 = skip the park/set resync —
		// bisect lever for the ped stage-0 texture repair.
		// PedsPipe absorb point (design doc §3.1 — SkinStage0Resync):
		// Phase 3 FLIPPED — SkinStage0Resync (skin.cpp) owns the bind chain
		// + stage-0 latch bookkeeping when the module owns this span
		// (pedsPipeState non-NULL, acquired at entry — classification is
		// produced for exactly that case, so this is the same key the
		// pdsState ternary below uses). pedsPipeState == NULL keeps the
		// legacy inline helper call byte-for-byte (pedStage0Resync, NULL
		// state lane — the pedsPipeEnable=0 rollback lever, skin.h:8 /
		// peds_pipe.h:34).
		if(!config || config->pedTexResync){
			if(pedsPipeState)
				SkinStage0Resync(atomic, pedsPipeState);
			else
				pedStage0Resync(atomic, NULL);	// legacy fallback (pedsPipeState == NULL -> NULL state lane)
		}
	}

	// ===== Default-callback shader hygiene =====
	// Shared by every atomic without a custom renderCallBack (peds, props,
	// pickups, marker objects). RW's D3D9Set*Shader are value-compare gated
	// against _rwD3D9Last*ShaderUsed (0x8E2448/0x8E244C): a stale PS/VS bound
	// by an earlier pass (postfx Im2D override, a pipe callback that
	// early-outs) can survive into a default-pipeline draw whose fixed-
	// function path never rebinds. Unbind through the RW setters (cache) AND
	// raw (device) so every layer agrees. Gated by defaultCbShaderHygiene.
	// One-shot log tag "defcb_shader" records the PRE-unbind device + RW-cache
	// pointers (Get* refs Released). POD locals only.
	if(config->defaultCbShaderHygiene){
		static bool defcbShaderLogged = false;
		// Unthrottled first-hit receipt: the "defcb_shader" throttle bucket
		// can be starved, which made the absence of a [DefCB] line ambiguous.
		static bool defcbShaderProbeLogged = false;
		if(!defcbShaderProbeLogged){
			defcbShaderProbeLogged = true;
			dbglog("[probe] defcb_shader path taken");
		}
		if(!defcbShaderLogged && dbglog_throttle("defcb_shader")){
			defcbShaderLogged = true;
			void *cacheVS = *(void**)0x8E2448;	// _rwD3D9LastVertexShaderUsed
			void *cachePS = *(void**)0x8E244C;	// _rwD3D9LastPixelShaderUsed
			IDirect3DVertexShader9 *devVS = NULL;
			IDirect3DPixelShader9 *devPS = NULL;
			if(d3d9device){
				d3d9device->GetVertexShader(&devVS);
				d3d9device->GetPixelShader(&devPS);
			}
			dbglog("[DefCB] shader hygiene: pre-unbind devVS=%p devPS=%p "
			       "cacheVS=%p cachePS=%p",
				(void*)devVS, (void*)devPS, cacheVS, cachePS);
			if(devVS) devVS->Release();
			if(devPS) devPS->Release();
		}
		RwD3D9SetPixelShader(NULL);
		RwD3D9SetVertexShader(NULL);
		if(d3d9device){
			d3d9device->SetPixelShader(NULL);
			d3d9device->SetVertexShader(NULL);
		}
	}else{
		// fix-34 always-on safety net: the INI lever above may stay OFF,
		// but a stale PS/VS bound by an earlier pass must never reach a
		// fixed-function default-pipe draw — correctness does not depend
		// on the lever. Cheap gate: two RW-cache dword reads (the same
		// value-compare globals the RW setters gate against); the unbind
		// (RW setters + raw device) only runs when a stale shader is
		// actually resident, so the steady-state cost is the reads.
		void *netCacheVS = *(void**)0x8E2448;	// _rwD3D9LastVertexShaderUsed
		void *netCachePS = *(void**)0x8E244C;	// _rwD3D9LastPixelShaderUsed
		if(netCacheVS || netCachePS){
			RwD3D9SetPixelShader(NULL);
			RwD3D9SetVertexShader(NULL);
			if(d3d9device){
				d3d9device->SetPixelShader(NULL);
				d3d9device->SetVertexShader(NULL);
			}
			if(dbglog_throttle("defcb_shader_net"))
				dbglog("[DefCB] safety net unbound stale shader "
				       "(cacheVS=%p cachePS=%p)", netCacheVS, netCachePS);
		}
	}

	// Override pAmbient with shared timecycle ambient for ped/skin rendering.
	// All pipelines (buildings, vehicles, peds) now use the same ambient source.
	// Sep-3 semantics (W3 revert — docs/plans/2026-10-30-sep3-correlation/
	// git-chars-lighting.md §4.2): plain GetTimecycleAmbient() — no max()
	// with the game's own ambient, no ambient-floor hue-preserving lift (the
	// pedAmbientFloor lever was removed 2026-11-10 - see wave-20-record §5),
	// no cutscene 0.15 special. The 0.20 floor saturated the FFP skin pipe
	// (final ≈ pAmbient + pDirect·NdotL) to flat white at noon.
	RwRGBAReal savedAmb = {};
	bool ambOverridden = false;
	if(pipe == skinPipe && pAmbient){
		// PedsPipe absorb point (design doc §3.1 — SkinAmbientOverride):
		// Phase 3 FLIPPED — when the module owns this span (pedsPipeState
		// non-NULL, acquired at entry), SkinAmbientOverrideEnter runs the
		// saved-capture / black-guard pair from skin.cpp (state in
		// PedPipeState). pedsPipeState == NULL keeps the legacy inline
		// block below byte-for-byte (rollback lever). The ped_ambient
		// receipts stay on both paths (skin.cpp logs the same tags).
		if(pedsPipeState)
			SkinAmbientOverrideEnter(pedsPipeState);
		else{
		savedAmb = pAmbient->color;
		RwRGBAReal tcAmbient = GetTimecycleAmbient();
		// Guard (log-forensics 2026-10-30 #4): never publish PURE BLACK
		// ambient to the skin pipe unless the black is genuine — i.e. the
		// game's own saved ambient is black too (true night / blackout).
		// The 04:38 run showed 11x `[Ped] pAmbient override (0,0,0)` in
		// the device-reset window (04:38:35-55) while savedAmb held real
		// light: a zeroed GetTimecycleAmbient() sample is transient
		// (reset / interior area switch), and publishing it greys every
		// ped to a featureless mannequin. Fall back to the savedAmb
		// capture in that case only; normal values pass through verbatim
		// (W3 revert semantics preserved).
		const float kAmbBlackEps = 1e-4f;
		bool tcBlack = tcAmbient.red   < kAmbBlackEps &&
		               tcAmbient.green < kAmbBlackEps &&
		               tcAmbient.blue  < kAmbBlackEps;
		bool savedBlack = savedAmb.red   < kAmbBlackEps &&
		                  savedAmb.green < kAmbBlackEps &&
		                  savedAmb.blue  < kAmbBlackEps;
		if(tcBlack && !savedBlack){
			tcAmbient = savedAmb;
			if(dbglog_throttle("ped_ambient"))
				dbglog("[Ped] pAmbient BLOCKED black override: timecycle=(0,0,0) with non-black saved ambient (%.3f,%.3f,%.3f) -> fallback savedAmb",
				       savedAmb.red, savedAmb.green, savedAmb.blue);
		}
		pAmbient->color = tcAmbient;
		ambOverridden = true;
		if(dbglog_throttle("ped_ambient"))
			dbglog("[Ped] pAmbient override: (%.3f,%.3f,%.3f)",
				tcAmbient.red, tcAmbient.green, tcAmbient.blue);
		}	// end legacy fallback (pedsPipeState == NULL)
	}

	// ===== DUAL PASS =====
	// PED (skin pipe): restore the ORIGINAL SkyGfx behaviour
	// (backup_original/src/main.cpp:422-436). The legacy callback ran the
	// two legs UNCONDITIONALLY whenever dualPassPed && zwriteThresholdPed>0:
	// leg 1 = GREATEREQUAL@threshold with ZWRITE ON (seeds depth at the
	// cutout edge), leg 2 = LESS@threshold with ZWRITE OFF (paints the
	// below-threshold fringe without disturbing that depth). It never asked
	// whether the atomic "has alpha": CJ head/hair/bandana materials are
	// alpha-255 over texture CUTOUT masks, so atomicHasAlpha() (colour +
	// prelit only) returned false and the hasAlpha/zwrite gate this replaces
	// starved the whole pass (field log: `[PED] dual pass skipped:
	// zwrite=1 hasAlpha=0` 9/9, zero "ran"). Keep that gate ONLY for the
	// default-pipe branch (pipe==NULL): default/vehicle/building dual-pass
	// behaviour is unchanged.
	if(dodual && !isPedPipe){
		int zwriteNow = TRUE;
		bool hasAlpha = atomicHasAlpha(atomic, false);
		RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, (void*)&zwriteNow);
		if(!zwriteNow || !hasAlpha){
			if(dbglog_throttle("ped_dual_skip"))
				dbglog("[PED] dual pass skipped: zwrite=%d hasAlpha=%d pipe=%p",
				       zwriteNow, (int)hasAlpha, (void*)pipe);
			dodual = 0;
		}
	}

	// S5 diagnostic: one-shot head-part (bandana/hair) texture receipt —
	// texname + TexAlpha class + attach frame (implemented in chars.cpp).
	// PedsPipe absorb point (design doc §3.1 — head-part receipt router):
	// Phase 3 FLIPPED — the module path routes the receipt through
	// SkinHeadPartReceipt (skin.cpp); pedsPipeState == NULL keeps the
	// legacy inline router byte-for-byte (the pedsPipeEnable=0 rollback
	// lever, skin.h:8 / peds_pipe.h:34). The receipt corpus is identical
	// on either leg.
	if(isPedPipe){
		if(pedsPipeState)
			SkinHeadPartReceipt(atomic);
		else{
		extern void chars_logHeadPartReceipt(const RpAtomic *atomic);
		chars_logHeadPartReceipt(atomic);
		}
	}

	// ===== ped draw-boundary cull re-assert (3-layer paired) =====
	// vehiclePipe.cpp:2389-2396 "Draw-boundary re-asserts" idiom, mirrored
	// for the ped span: every state-writing shield between the entry
	// contract and the draw (pipeEnterAlphaMode / pipeForceAlphaBlock
	// flushes, pedOpaqueEnforce, pedStage0Resync binds, the shader-hygiene
	// unbind, the chars receipt) is answered here on the LAST line before
	// RxPipelineExecute. Audit result: no shield/restore block in this
	// callback writes CULLMODE — but a foreign raw write mid-entry (the
	// Im2D/leftover class buildingPipe.cpp:2731-2735 and
	// vehiclePipe.cpp:2930-2940 re-assert past) would otherwise survive to
	// the draw: CULLNONE there = backfaces showing through skin
	// (angle-dependent see-through), CULLFRONT = inside-out ped.
	// Re-asserts the SAME mode the entry forced (pedCullDraw — CULLBACK in
	// the main view, the pedCullMode lever's override, or a foreign cull
	// scope's live mode), never a hardcoded BACK. Covers both dual-pass
	// legs: the leg states between them never touch cull.
	// Module path: the entry-forced value lives in PedPipeState
	// (contractCullDraw, written by SkinEntryContract); legacy path: the
	// local the inline entry filled.
	if(isPedPipe)
		pipeForceCullMode(pedsPipeState ? pedsPipeState->contractCullDraw
					       : pedCullDraw);

	if(dodual){
		RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, (void*)&alphatest);
		RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)&alpharef);
		// Sanitize the saved func/ref before the restore write (same rule as
		// D3D9RenderDual / CSkidmarks__Render): rw NA(0)/garbage must never
		// reach the device. The threshold is parse-clamped [0,255]; route it
		// through clampAlphaRef for the single bound (defense-in-depth).
		if(alphatest < rwALPHATESTFUNCTIONNEVER || alphatest > rwALPHATESTFUNCTIONALWAYS)
			alphatest = rwALPHATESTFUNCTIONGREATEREQUAL;
		alpharef = clampAlphaRef(alpharef);
		int zt = clampAlphaRef(config->zwriteThresholdPed);
		// zwriteThresholdPed, not zwriteThreshold: the ped key is parsed
		// (readIniFile, config.cpp) and menu'd (debugmenu_ui) but was never
		// consumed — peds silently shared the building/vehicle cut. Same
		// 0..255 clamp as its siblings, so the two are interchangeable
		// until the user actually moves the ped slider.
		// Two-layer leg states (see setAlphaTestTwoLayer): the old rw-only
		// writes let a drifted device run the same test for both legs.
		if(isPedPipe){
			// PedsPipe absorb point (design doc §3.1 — SkinDrawDual):
			// Phase 3 FLIPPED — SkinDrawDual (skin.cpp) owns the two-leg
			// draw + the leg-progression state lane when the module owns
			// this span (pedsPipeState non-NULL, acquired at entry).
			// pedsPipeState == NULL keeps the legacy inline block below
			// byte-for-byte (the pedsPipeEnable=0 rollback lever,
			// skin.h:8 / peds_pipe.h:34). The [PedDual] receipts stay on
			// both paths.
			if(pedsPipeState)
				pipe = SkinDrawDual(pipe, atomic, pedsPipeState,
						    alphatest, alpharef, zt);
			else{
			// Legacy skin-pipe sequence, transcribed from the original:
			//   ZWRITE TRUE -> leg1 GREATEREQUAL@zt (depth seeds) ->
			//   ZWRITE FALSE -> leg2 LESS@zt (fringe) ->
			//   ZWRITE TRUE -> restore caller func/ref.
			// The entry contract above already forced ZWRITE TRUE, but the
			// legacy callback re-forced it for the skin pipe (its
			// "parachute strings" fix) — keep that explicit write so the
			// sequence does not depend on the entry path.
			setZWriteTwoLayer(TRUE);
			setAlphaTestTwoLayer(rwALPHATESTFUNCTIONGREATEREQUAL, zt);
			// F6 fix: capture vertex-alpha before leg 1 so leg 2's restore
			// returns the caller's true value (pre-seeded per POD convention).
			BOOL blendSaved = TRUE;
			RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)&blendSaved);
			RxPipelineExecute(pipe, atomic, 1);
			setZWriteTwoLayer(FALSE);
			setAlphaTestTwoLayer(rwALPHATESTFUNCTIONLESS, zt);
			// F6 fix: leg 2 (LESS@zt fringe) previously ran with blending OFF,
			// painting sub-threshold texels as SOLID RGB fill — head-part
			// textures carry chroma-green RGB in their transparent regions
			// (green facet / black head in interiors). Blend the fringe by
			// its own alpha instead (L2 mirror per buildingPipe:1946 discipline).
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
			RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			pipe = RxPipelineExecute(pipe, atomic, 1);
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)blendSaved);
			RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, blendSaved ? TRUE : FALSE);
			setZWriteTwoLayer(TRUE);
			setAlphaTestTwoLayer((RwUInt32)alphatest, alpharef);
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
				       (int)atomicHasAlpha(atomic, false),
				       (int)atomicHasAlpha(atomic, true));
			}
			}	// end legacy fallback (pedsPipeState == NULL)
		}else{
			setAlphaTestTwoLayer(rwALPHATESTFUNCTIONGREATEREQUAL, zt);
			RxPipelineExecute(pipe, atomic, 1);
			setZWriteTwoLayer(FALSE);
			setAlphaTestTwoLayer(rwALPHATESTFUNCTIONLESS, zt);
			pipe = RxPipelineExecute(pipe, atomic, 1);
			setZWriteTwoLayer(TRUE);
			setAlphaTestTwoLayer((RwUInt32)alphatest, alpharef);
		}
	}else
		pipe = RxPipelineExecute(pipe, atomic, 1);

	// Restore original ambient after ped rendering — SkinAmbientOverrideExit
	// on the module path (re-entrant: no-op unless Enter ran this span),
	// legacy capture restore otherwise.
	if(pedsPipeState)
		SkinAmbientOverrideExit(pedsPipeState);
	else if(ambOverridden && pAmbient){
		pAmbient->color = savedAmb;
	}

	// ===== PED pipe group exit contract — mirror of buildingPipe.cpp:1223-1227
	// and vehiclePipe.cpp:2480-2484. Also closes the dual pass above, which
	// leaves rw ZWRITE forced TRUE (and alpha func/ref at whatever the pass
	// last wrote) — each layer goes back to its own pre-entry value here, so
	// the ped pipe can no longer hand a forced/derived state to the next
	// consumer (Im2D marker passes, effects, the next pipe group).
	if(isPedPipe){
		// PedsPipe absorb point (design doc §3.1 — SkinExitContract lane):
		// Phase 3 FLIPPED — when the module owns this span (pedsPipeState
		// non-NULL), SkinExitContract runs the absorbed exit chain from
		// skin.cpp (pipeExitAlphaMode / sticky-or-restore ZWRITE /
		// pipeExitCullMode / pedOpaqueRestore-or-GE@1-reset / sticky-depth
		// repair — a divergent capture is NEVER re-emitted), state in
		// PedPipeState. pedsPipeState == NULL keeps the legacy inline
		// blocks below byte-for-byte (rollback lever). The [PedRS] exit
		// monitor is absorbed on its own dual-path flip (SkinExitMonitor)
		// right below and runs after either path.
		if(pedsPipeState)
			SkinExitContract(pedsPipeState);
		else{
		pipeExitAlphaMode(savedAlpha);
		// Sticky ZWRITE repair: when entry found the state off/desynced, the
		// repaired canonical TRUE is written to ALL THREE layers here instead
		// of restoring each layer to its own (desynced) pre-entry value —
		// cache now matches device, so the entry drift detector stops
		// re-triggering every frame. A clean pre-entry state (all layers
		// agreed) still restores faithfully per layer.
		if(zwriteWasDesync){
			RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
			RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
			if(d3d9device)
				d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
			if(dbglog_throttle("PedZWrite"))
				dbglog("[PED] ZWRITE sticky repair: canonical TRUE written to rw/drv/dev at exit");
		}else{
			RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)savedZWriteRw);
			RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, savedZWriteDrv ? TRUE : FALSE);
			if(d3d9device)
				d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, savedZWriteDev);
		}
		pipeExitCullMode(savedCull);
		// Ped opaque contract exit: restore the true pre-entry values AFTER
		// pipeExitAlphaMode (which rewrites blend/alpha from the rw-cache
		// snapshot). Unconditional when the contract ran — no early return
		// above can skip it. D3DRS_ZWRITEENABLE is excluded; the sticky
		// block above owns it.
		if(pedContractOn)
			pedOpaqueRestore(&savedOpaque);
		else{
			// fix-34: pedOpaqueRestore gated off (INI pedOpaqueContract=0).
			// The ENTRY enforce is now unconditional (build-11 transparent-
			// ped fix), so the draw itself always carries the canonical
			// opaque signature — but pipeExitAlphaMode above still replays
			// the PRE-ENTRY func/ref through the rw-cache side (envmap's
			// leaked ref=140, envmap.cpp:448 outdoor park), which is how it
			// would reach and stay on the device. Sticky-repair rule (same
			// as the ZWRITE block above: never faithfully re-create a known
			// leak): reset the canonical GREATEREQUAL@1 through ALL THREE
			// layers so the alpharef/alphafunc contract no longer depends on
			// the INI lever. D3DRS_ZWRITEENABLE stays owned by the sticky
			// block.
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
		// pedOpaqueRestore above faithfully replays the per-layer capture —
		// correct for a CLEAN entry, but a divergent capture IS the leak
		// (ZTEST/ZENABLE off or ZFUNC drifted), and re-emitting it at exit
		// hands it to every later draw: buildings/props re-drawn after CJ
		// then pass their depth test over him (build-11 draw-over-CJ chain).
		// Same sticky rule as the ZWRITE block above — a divergent depth
		// entry ends canonical (ZTEST=1 ZENABLE=1 ZFUNC=LESSEQUAL), a clean
		// entry keeps its verbatim restore. ZWRITE stays owned by the block
		// above. 3L-paired: ZTEST rw+drv+dev, ZFUNC drv+dev (no rw twin).
		if(depthWasDesync){
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
		}	// end legacy fallback (pedsPipeState == NULL)

		// ===== [PedRS] exit state monitor =====
		// PedsPipe absorb point (design doc §3.1 — exit [PedRS] monitor):
		// Phase 3 FLIPPED — the module path runs the 21-read triplet
		// monitor from skin.cpp (SkinExitMonitor; ped_rs receipt corpus
		// unchanged). pedsPipeState == NULL keeps the legacy inline
		// monitor below byte-for-byte (the pedsPipeEnable=0 rollback
		// lever, skin.h:8 / peds_pipe.h:34). It still runs after EITHER
		// exit contract (module or legacy), before vs_probe.
		if(pedsPipeState)
			SkinExitMonitor();
		else{
		// Throttled receipt (tag "ped_rs", at most once per 2 s): the
		// post-restore value of every state this callback repairs, read from
		// all three layers (rw cache / RW driver cache / raw device). A
		// divergent triplet here is the fingerprint of a foreign raw write
		// that survived the exit restore. The throttle gates the reads too,
		// so the 21 GetRenderState calls only run when the line can actually
		// be emitted; the first ped exit always samples once so the log
		// proves the monitor is live even if the bucket was just taken.
		// POD scalars only.
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
		}	// end legacy fallback (pedsPipeState == NULL)
	}

	// FIX 3: sample the vertex stage at ped/default-pipe callback exit.
	vs_probe("ped_exit", "vs_probe_ped");
	return pipe ? atomic : NULL;
}

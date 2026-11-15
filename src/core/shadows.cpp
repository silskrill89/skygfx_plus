// shadows.cpp — CShadows / shadow render-state contract (near-camera BLACK
// SHADOW-BLOB fix). Mechanically extracted from main.cpp (byte-identical
// bodies; docs/plans/2026-11-08-master-plan.md §4). Owns:
//   * ShadowContractState + shadowContractCapture/Drift/Force/Restore/Run
//   * the four CShadows / CRealTimeShadowManager entry wrappers + orig ptrs
//   * shadows_installHooks(void) — the byte-verified intercept installs
//     (0x53E0BE call site + 0x708300 / 0x70A960 / 0x706AB0 entry shields).
// Called once from main.cpp DllMain at the original install position.
#include "skygfx.h"
#include <injector\hooking.hpp>
#include <string.h>

extern RwUInt32 pipeAlphaFuncToD3D(RwUInt32 rwFunc);
// ============================================================================
// SUBSYSTEM: SHADOW RENDER-STATE CONTRACT   (extraction: render/shadowContract.cpp)
// ============================================================================
// Scope-isolated for a mechanical later extraction. Self-contained: no config
// key, no file-scope state outside this region, only the four shadow hooks.
//
// TYPES / GLOBALS:
//   ShadowContractState   POD snapshot: L1 rw cache, L2 RW driver cache, L3
//                         device (scalars + stage-0 texture pointers,
//                         C2712-safe: no destructor, no unwinding needed)
//   CShadows__*_orig      saved call targets (two call-site intercepts + the
//                         RenderStaticShadows and real-time-manager entry
//                         trampolines)
//   No other file-scope state.
//
// ENTRY POINTS:
//   pipeBlendToD3D()          rwBlend -> D3DBLEND with canonical fallback
//                             (pipelinecommon.cpp, exported via skygfx.h)
//   shadowContractCapture()   entry snapshot; pre-seeds canonical shadow state
//   shadowContractDrift()     count of L3 states disagreeing with L1 (0 = clean)
//   shadowContractForce()     canonical shadow state through L1+L2+L3
//   shadowContractRestore()   per-layer exit restore; heals L2+L3 from L1 when
//                             drift > 0 (polluted-entry case); restores the
//                             stage-0 texture raster on every path
//   shadowContractRun()       capture -> receipt -> force -> orig -> restore
//   CShadows__UpdateStaticShadows_hook / RenderStaticShadows_hook /
//   RenderStoredShadows_hook / CRealTimeShadowManager__Update_hook
//                             the four wrappers (three function-entry
//                             shields + the 0x53E0BE call-site intercept)
//
// INVARIANTS:
//   * The call-site intercept (0x53E0BE) is byte-verified (E8 + target +
//     zero-arg) and all three function-entry hooks are prologue-verified
//     (0x708300: A1 24 7B C9 00, 0x706AB0: 51 53 57 8B F9 — 5-byte
//     trampoline; 0x70A960: 55 8B EC 83 E4 F8 — 6-byte prologue steal,
//     5-byte detour + 1 dead NOP byte, trampoline replays the frame
//     prologue); each is skipped on mismatch, so the vanilla shadow path
//     survives a bad exe.
//   * drift == 0 keeps the verbatim per-layer scalar restore (bit-identical to
//     the old behaviour); drift > 0 pins L2 and L3 to the L1 capture (heal),
//     because a verbatim restore would re-emit the pollution to every later
//     draw.
//   * ZFUNC and ALPHATESTENABLE have no rw twin: they are never counted as
//     drift and always restore their captured per-layer value.
//   * Stage 0 (rwRENDERSTATETEXTURERASTER): every vanilla shadow draw binds a
//     texture and none restores it, so the next consumer inherits NULL (static
//     paths) or the last shadow texture (stored path). Capture L1+raw L3, then
//     park-and-set L1 and raw-set L3 on exit. The pass itself is never forced
//     onto a texture.
//   * POD only, no SEH fence, no C++ objects (reached from a hooked game call).
//
// FUTURE render/shadowContract.cpp PUBLIC API (exact exports):
//   void ShadowContract_InstallHooks(void);   // NEW: owns the verified loop
//   (shadowContractRun + the four hook bodies + orig pointers become static.)
//
// MOVES TOGETHER (copy-paste set):
//   src/core/main.cpp   this region (ShadowContractState .. the four hooks)
//   src/core/main.cpp   DllMain shadow install block: call site 0x53E0BE
//                       (expects 0x707F40) + the verified entry shields
//                       0x708300 (RenderStaticShadows), 0x70A960
//                       (RenderStoredShadows — the 0x53E0C8 substitute:
//                       that call site is SilentPatch-owned and is NOT
//                       intercepted) and 0x706AB0
//                       (CRealTimeShadowManager::Update) entry hooks
//   src/render/pipelinecommon.cpp  pipeAlphaFuncToD3D (shared helper, STAYS)
// ============================================================================
// ============================================================
// Shadow render-state contract (near-camera BLACK SHADOW-BLOB fix)
// ============================================================
// Real labels (GTA SA 1.0 US): 0x707F40 = CShadows::UpdateStaticShadows (no
// draw at all), 0x708300 = CShadows::RenderStaticShadows, 0x70A960 =
// CShadows::RenderStoredShadows, 0x707FA0 = CShadows::RenderExtraPlayerShadows
// (vehicle-only, deliberately not wrapped) and 0x706AB0 =
// CRealTimeShadowManager::Update (called from 0x53EA0D), the real static
// rasterisation chain: 0x707CA0 -> 0x705790 draw at 0x7058BC (primType=2
// rwPRIMTYPEPOLYLINE, 4 verts / 5 indices, ZTEST=0, blend OFF,
// TEXTURERASTER=NULL; the texture is never restored).
// The game's shadow passes set their render state through RwRenderStateSet
// (L1 cache) ONLY. On this build the device frequently disagrees with the
// caches (postfx raw writes, DepthHook raw ZENABLE, Im2D passes — see the
// [FxAlpha]/[PED] desync evidence), so those L1-only sets no-op against an
// unchanged rw cache and the shadow quads draw with a stale ALPHAREF (140/1),
// a stale DESTBLEND/ZTEST, blend OFF or a leaked stage-0 texture — hard black
// blobs, acne, smears and flat/white peds. This wrapper runs at the two
// zero-arg call sites in CScene::Render (0x53E0BE / 0x53E0C8), at the
// CShadows::RenderStaticShadows function entry (0x708300), and at the
// CRealTimeShadowManager::Update entry (0x706AB0, see the install block),
// forces the canonical shadow state through all three layers (rw cache -> RW
// driver cache -> raw device) for the call, and restores L1/L2 from their own
// captures on exit. The pre-entry device is often already polluted (postfx raw
// writes, earlier consumers), so a verbatim L3 restore would faithfully
// re-emit that pollution to everything drawn after the pass. The exit
// therefore pins L2+L3 to the L1 rw-cache capture whenever the device
// disagrees with it (heal); only the no-drift case keeps the old verbatim
// per-layer scalar restore.
// ShadowContractState is plain POD: scalar and pointer members only
// (C2712-safe — this wrapper is reached from a hooked game call and must never
// need unwinding). The raw stage-0 texture is AddRef'd by GetTexture and
// released by shadowContractRun after the restore.
// Heal sentinel: ZFUNC and ALPHATESTENABLE have no rw twin, so they have no L1
// truth to pin to; the exit keeps restoring their captured layer values.
// Every other state is healed from L1 when drift > 0.

struct ShadowContractState {
	// L1 rw render-state cache (no rw twin for ZFUNC / ALPHATESTENABLE)
	RwUInt32 rwBlend, rwSrc, rwDst, rwZTest, rwZWrite, rwAlphaFunc, rwAlphaRef, rwFog;
	RwRaster *rwTexRaster;	// stage-0 texture raster (L1)
	// L2 RW D3D9 driver cache
	DWORD drvBlend, drvSrc, drvDst, drvZEnable, drvZFunc, drvZWrite,
	      drvAlphaTest, drvAlphaFunc, drvAlphaRef, drvFog;
	// L3 raw device
	DWORD devBlend, devSrc, devDst, devZEnable, devZFunc, devZWrite,
	      devAlphaTest, devAlphaFunc, devAlphaRef, devFog;
	IDirect3DTexture9 *devTex;	// stage-0 texture (L3, GetTexture AddRef)
};

static void
shadowContractCapture(ShadowContractState *s)
{
	// Pre-seed the canonical shadow values: a failed Get leaves the out-param
	// untouched, so the restore below must never push garbage to the device.
	s->rwBlend = TRUE;
	s->rwSrc = rwBLENDSRCALPHA;
	s->rwDst = rwBLENDINVSRCALPHA;
	s->rwZTest = TRUE;
	s->rwZWrite = FALSE;
	s->rwAlphaFunc = rwALPHATESTFUNCTIONGREATEREQUAL;
	s->rwAlphaRef = 0;
	s->rwFog = FALSE;
	// Stage-0 texture: the rw cache's own default is the white texture; the
	// raw device texture is only known after a successful GetTexture.
	s->rwTexRaster = gpWhiteTexture ? gpWhiteTexture->raster : NULL;
	s->devTex = NULL;
	s->drvBlend = s->devBlend = TRUE;
	s->drvSrc = s->devSrc = D3DBLEND_SRCALPHA;
	s->drvDst = s->devDst = D3DBLEND_INVSRCALPHA;
	s->drvZEnable = s->devZEnable = TRUE;
	s->drvZFunc = s->devZFunc = D3DCMP_LESSEQUAL;
	s->drvZWrite = s->devZWrite = FALSE;
	s->drvAlphaTest = s->devAlphaTest = TRUE;
	s->drvAlphaFunc = s->devAlphaFunc = D3DCMP_GREATEREQUAL;
	s->drvAlphaRef = s->devAlphaRef = 0;
	s->drvFog = s->devFog = FALSE;

	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)&s->rwBlend);
	RwRenderStateGet(rwRENDERSTATESRCBLEND, (void*)&s->rwSrc);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, (void*)&s->rwDst);
	RwRenderStateGet(rwRENDERSTATEZTESTENABLE, (void*)&s->rwZTest);
	RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, (void*)&s->rwZWrite);
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, (void*)&s->rwAlphaFunc);
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)&s->rwAlphaRef);
	RwRenderStateGet(rwRENDERSTATEFOGENABLE, (void*)&s->rwFog);
	RwRenderStateGet(rwRENDERSTATETEXTURERASTER, (void*)&s->rwTexRaster);

	RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE, &s->drvBlend);
	RwD3D9GetRenderState(D3DRS_SRCBLEND, &s->drvSrc);
	RwD3D9GetRenderState(D3DRS_DESTBLEND, &s->drvDst);
	RwD3D9GetRenderState(D3DRS_ZENABLE, &s->drvZEnable);
	RwD3D9GetRenderState(D3DRS_ZFUNC, &s->drvZFunc);
	RwD3D9GetRenderState(D3DRS_ZWRITEENABLE, &s->drvZWrite);
	RwD3D9GetRenderState(D3DRS_ALPHATESTENABLE, &s->drvAlphaTest);
	RwD3D9GetRenderState(D3DRS_ALPHAFUNC, &s->drvAlphaFunc);
	RwD3D9GetRenderState(D3DRS_ALPHAREF, &s->drvAlphaRef);
	RwD3D9GetRenderState(D3DRS_FOGENABLE, &s->drvFog);

	if(d3d9device){
		d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &s->devBlend);
		d3d9device->GetRenderState(D3DRS_SRCBLEND, &s->devSrc);
		d3d9device->GetRenderState(D3DRS_DESTBLEND, &s->devDst);
		d3d9device->GetRenderState(D3DRS_ZENABLE, &s->devZEnable);
		d3d9device->GetRenderState(D3DRS_ZFUNC, &s->devZFunc);
		d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &s->devZWrite);
		d3d9device->GetRenderState(D3DRS_ALPHATESTENABLE, &s->devAlphaTest);
		d3d9device->GetRenderState(D3DRS_ALPHAFUNC, &s->devAlphaFunc);
		d3d9device->GetRenderState(D3DRS_ALPHAREF, &s->devAlphaRef);
		d3d9device->GetRenderState(D3DRS_FOGENABLE, &s->devFog);
		// Stage 0 is a 2D texture in every shadow path. GetTexture AddRefs;
		// shadowContractRun Releases after the exit restore on every path.
		IDirect3DBaseTexture9 *tex0 = NULL;
		d3d9device->GetTexture(0, &tex0);
		s->devTex = (IDirect3DTexture9*)tex0;
	}

	// Sanitize whatever the Gets returned (the pre-seed covers total failure).
	if(s->rwBlend > 1) s->rwBlend = TRUE;
	if(s->rwZTest > 1) s->rwZTest = TRUE;
	if(s->rwZWrite > 1) s->rwZWrite = FALSE;
	if(s->rwAlphaFunc > rwALPHATESTFUNCTIONALWAYS) s->rwAlphaFunc = rwALPHATESTFUNCTIONGREATEREQUAL;
	if(s->rwAlphaRef > 255) s->rwAlphaRef = 0;
	if(s->rwFog > 1) s->rwFog = FALSE;
	if(s->drvZEnable > D3DZB_USEW) s->drvZEnable = TRUE;
	if(s->devZEnable > D3DZB_USEW) s->devZEnable = TRUE;
	if(s->drvZFunc < D3DCMP_NEVER || s->drvZFunc > D3DCMP_ALWAYS) s->drvZFunc = D3DCMP_LESSEQUAL;
	if(s->devZFunc < D3DCMP_NEVER || s->devZFunc > D3DCMP_ALWAYS) s->devZFunc = D3DCMP_LESSEQUAL;
	if(s->drvAlphaFunc < D3DCMP_NEVER || s->drvAlphaFunc > D3DCMP_ALWAYS) s->drvAlphaFunc = D3DCMP_GREATEREQUAL;
	if(s->devAlphaFunc < D3DCMP_NEVER || s->devAlphaFunc > D3DCMP_ALWAYS) s->devAlphaFunc = D3DCMP_GREATEREQUAL;
	if(s->drvAlphaRef > 255) s->drvAlphaRef = 0;
	if(s->devAlphaRef > 255) s->devAlphaRef = 0;
	if(s->rwTexRaster == (RwRaster*)0xFFFFFFFF)
		s->rwTexRaster = gpWhiteTexture ? gpWhiteTexture->raster : NULL;
}

// Count the contract states whose raw-device capture disagrees with the L1
// rw-cache belief. ZFUNC / ALPHATESTENABLE have no rw twin and are never
// counted (their L3 capture is restored verbatim). drift > 0 means the pass
// inherited a polluted device and the exit must heal L3 instead of echoing it.
static unsigned
shadowContractDrift(const ShadowContractState *s)
{
	unsigned n = 0;
	if((DWORD)(s->rwBlend ? TRUE : FALSE) != s->devBlend) n++;
	if(pipeBlendToD3D(s->rwSrc, D3DBLEND_SRCALPHA) != s->devSrc) n++;
	if(pipeBlendToD3D(s->rwDst, D3DBLEND_INVSRCALPHA) != s->devDst) n++;
	if((DWORD)(s->rwZWrite ? TRUE : FALSE) != s->devZWrite) n++;
	if((DWORD)(s->rwZTest ? TRUE : FALSE) != s->devZEnable) n++;
	if((DWORD)pipeAlphaFuncToD3D(s->rwAlphaFunc) != s->devAlphaFunc) n++;
	if((DWORD)s->rwAlphaRef != s->devAlphaRef) n++;
	if((DWORD)(s->rwFog ? TRUE : FALSE) != s->devFog) n++;
	return n;
}

static void
shadowContractForce(void)
{
	// L1 rw cache (ALPHATESTENABLE and ZFUNC have no rw twin).
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONGREATEREQUAL);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)0);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);

	// L2 RW driver cache.
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	RwD3D9SetRenderState(D3DRS_ZENABLE, TRUE);
	RwD3D9SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
	RwD3D9SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
	RwD3D9SetRenderState(D3DRS_ALPHAREF, 0);
	RwD3D9SetRenderState(D3DRS_FOGENABLE, FALSE);

	// L3 raw device (repairs drift the caches cannot see).
	if(d3d9device){
		d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		d3d9device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		d3d9device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		d3d9device->SetRenderState(D3DRS_ZENABLE, TRUE);
		d3d9device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
		d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		d3d9device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
		d3d9device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
		d3d9device->SetRenderState(D3DRS_ALPHAREF, 0);
		d3d9device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	}
}

// Returns the number of drifted states corrected at L2/L3 (0 = verbatim restore).
static int
shadowContractRestore(const ShadowContractState *s, unsigned drift)
{
	// L1 rw cache.
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)s->rwBlend);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)s->rwSrc);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)s->rwDst);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)s->rwZTest);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)s->rwZWrite);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)s->rwAlphaFunc);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)s->rwAlphaRef);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)s->rwFog);

	// L2 RW driver cache: each layer gets its own captured value.
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, s->drvBlend ? TRUE : FALSE);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, s->drvSrc);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, s->drvDst);
	RwD3D9SetRenderState(D3DRS_ZENABLE, s->drvZEnable);
	RwD3D9SetRenderState(D3DRS_ZFUNC, s->drvZFunc);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, s->drvZWrite ? TRUE : FALSE);
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, s->drvAlphaTest ? TRUE : FALSE);
	RwD3D9SetRenderState(D3DRS_ALPHAFUNC, s->drvAlphaFunc);
	RwD3D9SetRenderState(D3DRS_ALPHAREF, s->drvAlphaRef);
	RwD3D9SetRenderState(D3DRS_FOGENABLE, s->drvFog ? TRUE : FALSE);

	// Stage-0 texture raster: every vanilla shadow draw binds stage 0 and none
	// restores it, so the next consumer inherits NULL (static/aux) or the last
	// shadow texture (stored). Park through L1 with a value guaranteed != the
	// capture so the L1 set below is a real transition, never a cache-hit
	// no-op, then set the captured raster and the captured raw L3 texture.
	// The pass itself was never forced onto a texture.
	{
		RwRaster *whiteRas = gpWhiteTexture ? gpWhiteTexture->raster : NULL;
		RwRaster *parkRas = (s->rwTexRaster == whiteRas)
			? (RwRaster*)NULL : whiteRas;
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)parkRas);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)s->rwTexRaster);
		if(d3d9device)
			d3d9device->SetTexture(0, s->devTex);
	}

	// L3 raw device. Heal doctrine: the rw cache (L1) is the world's belief
	// about what the next consumer expects, so it wins over the polluted
	// device capture at the shadow exit; the device capture is only used when
	// the caches agree (drift == 0) or where no rw twin exists (ZFUNC /
	// ALPHATESTENABLE, documented in ShadowContractState).
	if(!d3d9device)
		return 0;
	if(!drift){
		// Clean entry: verbatim per-layer restore, bit-identical to the old
		// behaviour.
		d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, s->devBlend ? TRUE : FALSE);
		d3d9device->SetRenderState(D3DRS_SRCBLEND, s->devSrc);
		d3d9device->SetRenderState(D3DRS_DESTBLEND, s->devDst);
		d3d9device->SetRenderState(D3DRS_ZENABLE, s->devZEnable);
		d3d9device->SetRenderState(D3DRS_ZFUNC, s->devZFunc);
		d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, s->devZWrite ? TRUE : FALSE);
		d3d9device->SetRenderState(D3DRS_ALPHATESTENABLE, s->devAlphaTest ? TRUE : FALSE);
		d3d9device->SetRenderState(D3DRS_ALPHAFUNC, s->devAlphaFunc);
		d3d9device->SetRenderState(D3DRS_ALPHAREF, s->devAlphaRef);
		d3d9device->SetRenderState(D3DRS_FOGENABLE, s->devFog ? TRUE : FALSE);
		return 0;
	}

	// Polluted entry: pin L3 to the cache values instead of re-emitting the
	// capture. ALPHAFUNC maps through pipeAlphaFuncToD3D (the same helper the
	// entry comparison uses); the blend pair maps through pipeBlendToD3D.
	DWORD healBlend = s->rwBlend ? TRUE : FALSE;
	DWORD healSrc = pipeBlendToD3D(s->rwSrc, D3DBLEND_SRCALPHA);
	DWORD healDst = pipeBlendToD3D(s->rwDst, D3DBLEND_INVSRCALPHA);
	DWORD healZWrite = s->rwZWrite ? TRUE : FALSE;
	DWORD healZEnable = s->rwZTest ? TRUE : FALSE;
	DWORD healAlphaFunc = (DWORD)pipeAlphaFuncToD3D(s->rwAlphaFunc);
	DWORD healAlphaRef = (DWORD)s->rwAlphaRef;
	DWORD healFog = s->rwFog ? TRUE : FALSE;
	// Heal L2 as well as L3: the pass wrote through RwD3D9SetRenderState, so
	// the driver cache is post-pass, not pre-entry. Pin every cache-backed
	// state to the same L1-derived canonical value; ZFUNC / ALPHATESTENABLE
	// have no rw twin, so they keep their captured L2 value (same doctrine as
	// the L3 heal below).
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, healBlend);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, healSrc);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, healDst);
	RwD3D9SetRenderState(D3DRS_ZENABLE, healZEnable);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, healZWrite);
	RwD3D9SetRenderState(D3DRS_ALPHAFUNC, healAlphaFunc);
	RwD3D9SetRenderState(D3DRS_ALPHAREF, healAlphaRef);
	RwD3D9SetRenderState(D3DRS_FOGENABLE, healFog);
	RwD3D9SetRenderState(D3DRS_ZFUNC, s->drvZFunc);
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, s->drvAlphaTest ? TRUE : FALSE);
	d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, healBlend);
	d3d9device->SetRenderState(D3DRS_SRCBLEND, healSrc);
	d3d9device->SetRenderState(D3DRS_DESTBLEND, healDst);
	d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, healZWrite);
	d3d9device->SetRenderState(D3DRS_ZENABLE, healZEnable);
	d3d9device->SetRenderState(D3DRS_ZFUNC, s->devZFunc);
	d3d9device->SetRenderState(D3DRS_ALPHATESTENABLE, s->devAlphaTest ? TRUE : FALSE);
	d3d9device->SetRenderState(D3DRS_ALPHAFUNC, healAlphaFunc);
	d3d9device->SetRenderState(D3DRS_ALPHAREF, healAlphaRef);
	d3d9device->SetRenderState(D3DRS_FOGENABLE, healFog);
	// Unthrottled one-shot: the first heal of the run names the pollution it
	// replaced. dbglog_once is not time-based, so the first evidence survives.
	if(dbglog_once("shadow_heal"))
		dbglog("[ShadowHeal] L2+L3 pinned to cache: "
		       "dev(ref=%u func=%u blend=%u zw=%u fog=%u tex=%p) -> "
		       "rw(ref=%u func=%u blend=%u zw=%u fog=%u tex=%p)",
		       (unsigned)s->devAlphaRef, (unsigned)s->devAlphaFunc,
		       (unsigned)s->devBlend, (unsigned)s->devZWrite,
		       (unsigned)s->devFog, (void*)s->devTex,
		       (unsigned)s->rwAlphaRef, (unsigned)healAlphaFunc,
		       (unsigned)healBlend, (unsigned)healZWrite,
		       (unsigned)healFog, (void*)s->rwTexRaster);
	return (int)drift;
}

static void (*CShadows__UpdateStaticShadows_orig)(void);
static void (*CShadows__RenderStaticShadows_orig)(void);
static void (*CShadows__RenderStoredShadows_orig)(void);
static void (*CRealTimeShadowManager__Update_orig)(void);

// Resolve an rw raster to its D3D texture (same extension extraction as
// pedStage0Resync / texalpha.cpp:571). Returns a borrowed pointer, no AddRef.
static IDirect3DTexture9 *
shadowRasterToTex(RwRaster *ras)
{
	if(!ras)
		return NULL;
	DWORD extOff = *(DWORD*)0xB4E9E0;
	if(extOff == 0 || extOff >= 0x100000)
		return NULL;
	IDirect3DTexture9 *cand = *(IDirect3DTexture9**)((uint8*)ras + extOff);
	if(!cand || (uintptr_t)cand < 0x10000u || ((uintptr_t)cand & 3u))
		return NULL;
	return cand;
}

// Call a __thiscall original from a cdecl wrapper: self -> ECX, orig -> EAX.
// Needed only by the CRealTimeShadowManager::Update entry hook (ECX = this at
// 0x706AB0; its trampoline replays `push ecx; push ebx; push edi; mov edi,
// ecx`, so ECX must be intact). Naked: no prologue may clobber ECX.
static void __declspec(naked)
shadowCallThis(void *self, void (*orig)(void))
{
	__asm {
		mov ecx, [esp+4]
		mov eax, [esp+8]
		call eax
		ret
	}
}

// Shared wrapper body: capture -> (throttled divergence receipt with the
// drift count) -> force canonical -> original -> heal/restore. `self` is the
// __thiscall ECX for entry-hooked methods (NULL for zero-arg call sites);
// `texReceipt` emits the one-shot [ShadowTex] stage-0 receipt. POD only, no
// __try, no C++ objects.
static void
shadowContractRun(void (*orig)(void), const char *name, void *self,
                  bool texReceipt)
{
	// Contract gate (INI shadowContractEnable, default 0). This three-layer
	// contract is an ours-only layer added AFTER the Sept-3 look target, and
	// it wraps an OFFSCREEN shadow-camera render (CShadowCamera::InvertRaster
	// uses SRC=INVDESTCOLOR / DST=ZERO with ZTEST off) in an on-screen
	// normal-alpha contract. That mismatch corrupts the shadow raster and
	// leaves stage-0 rebound to NULL ([ShadowHeal] tex=00000000), which shows
	// up as baked-looking lightmap patches on floors. Default 0 = call the
	// original untouched (vanilla shadow passes, Sept-3 behaviour).
	if(!config || !config->shadowContractEnable){
		if(orig){
			if(self) shadowCallThis(self, orig);
			else orig();
		}
		return;
	}

	ShadowContractState pre;
	shadowContractCapture(&pre);

	// One-shot stage-0 receipt at the first RenderStoredShadows (0x70A960)
	// entry: proves whether the L1 raster and the raw device texture agree,
	// i.e. whether the next bind is a real transition or a no-op.
	if(texReceipt && dbglog_once("shadow_tex")){
		IDirect3DTexture9 *rwTex = shadowRasterToTex(pre.rwTexRaster);
		dbglog("[ShadowTex] rw=%p dev=%p match=%d",
			(void*)pre.rwTexRaster, (void*)pre.devTex,
			(rwTex == pre.devTex) ? 1 : 0);
	}

	// Drift = L3 contract states that entered the pass disagreeing with the
	// rw cache. Non-zero enables the exit heal; zero keeps the old verbatim
	// restore path bit-identical.
	unsigned drift = shadowContractDrift(&pre);

	// Divergence test: the three layers must agree on the states that cause
	// the blobs (blend/src/dst/ref/ztest). Any mismatch is evidence for the
	// diagnosis and names the pre-entry values the shadow pass would inherit.
	bool diverged =
		pre.rwBlend != pre.drvBlend || pre.drvBlend != pre.devBlend ||
		pre.rwSrc != pre.drvSrc || pre.drvSrc != pre.devSrc ||
		pre.rwDst != pre.drvDst || pre.drvDst != pre.devDst ||
		pre.rwAlphaRef != pre.drvAlphaRef || pre.drvAlphaRef != pre.devAlphaRef ||
		pre.rwZTest != pre.drvZEnable || pre.drvZEnable != pre.devZEnable ||
		pre.drvZFunc != pre.devZFunc;
	if((diverged || drift) && dbglog_throttle("shadow_contract"))
		dbglog("[shadow_contract] %s pre-entry layer drift: drift=%u "
		       "rw(blend=%u src=%u dst=%u ref=%u ztest=%u zw=%u fog=%u) "
		       "drv(blend=%u src=%u dst=%u ref=%u ztest=%u zfunc=%u zw=%u fog=%u) "
		       "dev(blend=%u src=%u dst=%u ref=%u ztest=%u zfunc=%u zw=%u fog=%u)",
		       name, drift,
		       (unsigned)pre.rwBlend, (unsigned)pre.rwSrc, (unsigned)pre.rwDst,
		       (unsigned)pre.rwAlphaRef, (unsigned)pre.rwZTest,
		       (unsigned)pre.rwZWrite, (unsigned)pre.rwFog,
		       (unsigned)pre.drvBlend, (unsigned)pre.drvSrc, (unsigned)pre.drvDst,
		       (unsigned)pre.drvAlphaRef, (unsigned)pre.drvZEnable,
		       (unsigned)pre.drvZFunc, (unsigned)pre.drvZWrite, (unsigned)pre.drvFog,
		       (unsigned)pre.devBlend, (unsigned)pre.devSrc, (unsigned)pre.devDst,
		       (unsigned)pre.devAlphaRef, (unsigned)pre.devZEnable,
		       (unsigned)pre.devZFunc, (unsigned)pre.devZWrite, (unsigned)pre.devFog);

	shadowContractForce();
	if(orig){
		if(self)
			shadowCallThis(self, orig);
		else
			orig();
	}
	int healed = shadowContractRestore(&pre, drift);
	// GetTexture AddRef'd the stage-0 texture; restore is done with it now.
	if(pre.devTex){
		pre.devTex->Release();
		pre.devTex = NULL;
	}
	if(dbglog_throttle("shadow_contract_exit"))
		dbglog("[shadow_contract] %s exit healed=%d", name, healed);
}

// 0x707F40 CShadows::UpdateStaticShadows — cleanup only, draws nothing.
static void
CShadows__UpdateStaticShadows_hook(void)
{
	shadowContractRun(CShadows__UpdateStaticShadows_orig,
		"update_static", NULL, false);
}

// 0x708300 CShadows::RenderStaticShadows — real static draw.
static void
CShadows__RenderStaticShadows_hook(void)
{
	shadowContractRun(CShadows__RenderStaticShadows_orig,
		"render_static", NULL, false);
}

// 0x70A960 CShadows::RenderStoredShadows — stored/registered draw. This is
// the first entry of the run and owns the one-shot [ShadowTex] receipt.
static void
CShadows__RenderStoredShadows_hook(void)
{
	shadowContractRun(CShadows__RenderStoredShadows_orig,
		"render_stored", NULL, true);
}

// 0x706AB0 CRealTimeShadowManager::Update — __thiscall, ECX = self
// (g_realTimeShadowMan 0xC40350). __fastcall receives ECX as `self`; the
// thunk re-loads ECX before the trampoline calls the original.
static void __fastcall
CRealTimeShadowManager__Update_hook(void *self)
{
	shadowContractRun(CRealTimeShadowManager__Update_orig,
		"realtime", self, false);
}

void
shadows_installHooks(void)
{
		// ---- shadow state contract install (black shadow-blob fix) ----
		// One zero-arg call in CScene::Render's shadow block is intercepted
		// at the call site (real GTA SA 1.0 US labels):
		//   0x53E0BE -> 0x707F40  CShadows::UpdateStaticShadows (no draw)
		//   0x53E0C8 -> 0x70A960  CShadows::RenderStoredShadows (draws)
		// CShadows::RenderStaticShadows (0x708300, draws) is shielded by a
		// FUNCTION-ENTRY hook instead, because its 0x53E0C3 call site
		// MISMATCHes at runtime on the report build (the displacement there
		// resolves to ~0x68E8DD00, so the site had already been rewritten).
		// The function itself is reached directly from 0x53DDE4 and 0x53E0C3
		// with no stack arguments, so the entry hook covers every caller.
		// CRealTimeShadowManager::Update (0x706AB0, the real static draw chain
		// called from 0x53EA0D) gets its own verified ENTRY hook below.
		// NOT wrapped: 0x707FA0 CShadows::RenderExtraPlayerShadows (vehicle-
		// only); if it ever needs the contract, hook its entry the same way.
		// (bytes read from E:\games\gtasa_skygfx_plus\gta_sa.exe, PE section
		//  .text VA 0x401000 / raw 0x400; file offset = VA - 0x400C00.)
		// Zero-arg qualification: the byte 5 before each site is 0xE8, i.e.
		// the immediately preceding instruction is the previous CALL — no
		// push can sit between a call and this site, so the callee receives
		// no arguments (verified for the site). If a site does not
		// verify (non-1.0 exe / unexpected prologue) it is SKIPPED: the
		// vanilla path remains, nothing is corrupted.
		// NOT intercepted: 0x707460 (a switch-on-int helper, called from
		// 0x7083A4 / 0x70AA2A with `push` — argument shape is not zero-arg).
		{
			const struct ShadowSite {
				DWORD site;
				DWORD expect;
				const char *name;
				void (**orig)(void);
				void (*hook)(void);
			} sites[2] = {
				{ 0x53E0BE, 0x707F40, "CShadows::UpdateStaticShadows",
				  &CShadows__UpdateStaticShadows_orig, CShadows__UpdateStaticShadows_hook },
				// 0x53E0C8 -> 0x70A960 removed: SilentPatch rewrites that call
				// site (CREDITS.md "do NOT patch its addresses"; unpatch
				// lineage 0x6FB17C/0x748AA8/0x53E0C8). SUBSTITUTE LANDED
				// (address migration 2026-11-08): CShadows::RenderStored-
				// Shadows is wrapped by its own 0x70A960 function-ENTRY
				// shield below (non-SP site, prologue byte-verified,
				// MISMATCH => skip) — the same wrapper indirection already
				// used for RenderStaticShadows (0x708300, its 0x53E0C3 call
				// site mismatches at runtime). CShadows__RenderStored-
				// Shadows_hook is installed there (was orphaned).
			};
			for(int i = 0; i < 2; i++){
				if(sites[i].site == 0)
					continue;	// empty slot: 0x53E0C8 is SilentPatch-owned
				BYTE op = *(BYTE*)sites[i].site;
				DWORD tgt = 0;
				if(op == 0xE8)
					tgt = *(DWORD*)(sites[i].site + 1) + sites[i].site + 5;
				// Previous instruction is a CALL (E8) => zero-arg site.
				bool zeroArg = (*(BYTE*)(sites[i].site - 5) == 0xE8);
				bool ok = (op == 0xE8 && tgt == sites[i].expect && zeroArg);
				dbglog("[shadow_install] 0x%X op=%02X orig=%08X %s "
				       "zeroarg=%d site=%s (expect op=E8 orig=%08X)",
				       (unsigned)sites[i].site, (unsigned)op, (unsigned)tgt,
				       ok ? "OK" : "MISMATCH", zeroArg ? 1 : 0,
				       sites[i].name, (unsigned)sites[i].expect);
				if(ok)
					InterceptCall(sites[i].orig, sites[i].hook, sites[i].site);
				else
					dbglog("[shadow_install] 0x%X SKIPPED — vanilla path kept",
					       (unsigned)sites[i].site);
			}
		}

		// ---- CShadows::RenderStaticShadows (0x708300) ENTRY shield ----
		// Entry prologue: A1 24 7B C9 00 = mov eax, ds:[0xC97B24] — exactly
		// one 5-byte instruction, so a PATCH_JUMP at the entry can be taken
		// without splitting an instruction. The trampoline executes those
		// 5 stolen bytes and jumps back to entry+5; the wrapper then calls
		// the trampoline via CShadows__RenderStaticShadows_orig.
		// MISMATCH => SKIP, vanilla RenderStaticShadows path kept.
		{
			const DWORD staticEntry = 0x708300;
			static const BYTE s_staticEntryExpect[5] = {
				0xA1, 0x24, 0x7B, 0xC9, 0x00
			};
			BYTE s_staticEntryActual[5];
			memcpy(s_staticEntryActual, (const void*)staticEntry,
				sizeof(s_staticEntryActual));
			const bool s_staticEntryOk = (memcmp(s_staticEntryActual,
				s_staticEntryExpect, sizeof(s_staticEntryActual)) == 0);
			char s_staticEntryHex[sizeof(s_staticEntryActual) * 3 + 1];
			s_staticEntryHex[sizeof(s_staticEntryHex) - 1] = 0;
			for(size_t i = 0; i < sizeof(s_staticEntryActual); i++)
				snprintf(s_staticEntryHex + i * 3, 4, "%02X ",
					(unsigned)s_staticEntryActual[i]);
			dbglog("[shadow_install] 0x%X entry bytes=%s %s "
			       "(expect A1 24 7B C9 00)",
				(unsigned)staticEntry, s_staticEntryHex,
				s_staticEntryOk ? "OK" : "MISMATCH");
			if(!s_staticEntryOk){
				dbglog("[shadow_install] 0x%X entry verify FAILED — "
				       "RenderStaticShadows entry hook SKIPPED, vanilla path kept",
					(unsigned)staticEntry);
			}else{
				BYTE *staticTramp = (BYTE*)VirtualAlloc(NULL, 32,
					MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
				if(staticTramp){
					memcpy(staticTramp, (const void*)staticEntry,
						sizeof(s_staticEntryExpect));
					staticTramp[5] = 0xE9;	// jmp entry+5
					*(DWORD*)(staticTramp + 6) =
						(staticEntry + 5) - (DWORD)(staticTramp + 5) - 5;
					CShadows__RenderStaticShadows_orig =
						(void (*)(void))staticTramp;
					InjectHook(staticEntry,
						CShadows__RenderStaticShadows_hook, PATCH_JUMP);
					dbglog("[shadow_install] 0x%X entry hook installed "
					       "(tramp=%p)", (unsigned)staticEntry,
						(void*)staticTramp);
				}else
					dbglog("[shadow_install] 0x%X entry hook: "
					       "VirtualAlloc failed — SKIPPED",
						(unsigned)staticEntry);
			}
		}

		// ---- CShadows::RenderStoredShadows (0x70A960) ENTRY shield ----
		// Address-migration substitute for the SilentPatch-owned call site
		// 0x53E0C8 (CREDITS.md: do NOT patch SilentPatch addresses) — the
		// call-site intercept was removed there and this wrapper indirection
		// covers EVERY caller of the real function instead (same rationale as
		// the 0x708300 shield, whose 0x53E0C3 call site mismatches at
		// runtime). Prologue (gta_sa.exe 1.0 US, PE .text VA 0x401000 /
		// raw 0x400, file offset = VA - 0x400C00):
		//   0x70A960: 55 8B EC 83 E4 F8 81 EC F0 00 00 00 A1 24 7B C9 00
		//   = push ebp / mov ebp,esp / and esp,-8 / sub esp,0F0h /
		//     mov eax, ds:[0xC97B24] — instruction boundaries at 0/1/3/6/12.
		// There is NO 5-byte boundary at entry, so the detour STEALS 6 BYTES
		// (E9 rel32 at entry + one dead NOP byte at entry+5; InjectHook only
		// writes the 5). The trampoline must replay those 6 bytes: the frame
		// prologue (`push ebp; mov ebp,esp; and esp,-8`) establishes EBP for
		// the body's ebp-relative addressing, and the body resumes only via
		// the trampoline's `jmp entry+6`, so EBP is re-established INSIDE the
		// orig() call — a detour further in (e.g. +12) would resume the body
		// with the wrapper chain's EBP (frame corruption). Stack-neutral:
		// the epilogue is `mov esp,ebp / pop ebp / ret` (0x70B724, byte-
		// verified — the standard MSVC pairing for a realigned frame), so
		// the trampoline call consumes exactly one return address (as the
		// 0x708300/0x706AB0 chains do). Zero-arg static: three back-to-back
		// calls in CScene::Render (0x53E0BE/0x53E0C3/0x53E0C8, no ECX setup
		// between them), hook signature void(void) like its siblings.
		// Behavior-neutral unless INI shadowContractEnable=1 (the contract
		// gate in shadowContractRun calls orig() untouched at 0). MISMATCH
		// (non-1.0 exe / SP claimed the entry) => SKIP, vanilla path kept.
		{
			const DWORD storedEntry = 0x70A960;
			static const BYTE s_storedEntryExpect[6] = {
				0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8
			};
			BYTE s_storedEntryActual[6];
			memcpy(s_storedEntryActual, (const void*)storedEntry,
				sizeof(s_storedEntryActual));
			const bool s_storedEntryOk = (memcmp(s_storedEntryActual,
				s_storedEntryExpect, sizeof(s_storedEntryActual)) == 0);
			char s_storedEntryHex[sizeof(s_storedEntryActual) * 3 + 1];
			s_storedEntryHex[sizeof(s_storedEntryHex) - 1] = 0;
			for(size_t i = 0; i < sizeof(s_storedEntryActual); i++)
				snprintf(s_storedEntryHex + i * 3, 4, "%02X ",
					(unsigned)s_storedEntryActual[i]);
			dbglog("[shadow_install] 0x%X bytes=%s %s "
			       "(expect 55 8B EC 83 E4 F8)",
				(unsigned)storedEntry, s_storedEntryHex,
				s_storedEntryOk ? "OK" : "MISMATCH");
			if(!s_storedEntryOk){
				dbglog("[shadow_install] 0x%X verify FAILED — "
				       "RenderStoredShadows entry hook SKIPPED, "
				       "vanilla path kept", (unsigned)storedEntry);
			}else{
				BYTE *storedTramp = (BYTE*)VirtualAlloc(NULL, 32,
					MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
				if(storedTramp){
					memcpy(storedTramp, (const void*)storedEntry,
						sizeof(s_storedEntryExpect));
					storedTramp[6] = 0xE9;	// jmp entry+6
					*(DWORD*)(storedTramp + 7) =
						(storedEntry + 6) - (DWORD)(storedTramp + 6) - 5;
					CShadows__RenderStoredShadows_orig =
						(void (*)(void))storedTramp;
					InjectHook(storedEntry,
						CShadows__RenderStoredShadows_hook, PATCH_JUMP);
					Nop(storedEntry + 5, 1);	// dead tail of the 6-byte steal
					dbglog("[shadow_install] 0x%X entry hook installed "
					       "(tramp=%p, 6-byte steal)", (unsigned)storedEntry,
						(void*)storedTramp);
				}else
					dbglog("[shadow_install] 0x%X entry hook: "
					       "VirtualAlloc failed — SKIPPED",
						(unsigned)storedEntry);
			}
		}

		// ---- CRealTimeShadowManager::Update (0x706AB0) ENTRY shield ----
		// The real static shadow rasterisation chain: 0x706AB0 (called from
		// 0x53EA0D with ECX = g_realTimeShadowMan 0xC40350) -> 0x707CA0 ->
		// the 0x705790 draw at 0x7058BC (primType=2 rwPRIMTYPEPOLYLINE, 4
		// verts / 5 indices, closed line loop). The draw sets ZTEST=0, blend
		// OFF and TEXTURERASTER=NULL and restores none of the texture state.
		// Entry prologue: 51 53 57 8B F9 = push ecx / push ebx / push edi /
		// mov edi, ecx — complete instructions, 5 bytes total, so a 5-byte
		// PATCH_JUMP trampoline can be taken. ECX is this; the __fastcall
		// wrapper receives it as `self` and shadowCallThis re-loads it before
		// the trampoline. MISMATCH => SKIP, vanilla manager path kept.
		{
			const DWORD rtEntry = 0x706AB0;
			static const BYTE s_rtEntryExpect[5] = {
				0x51, 0x53, 0x57, 0x8B, 0xF9
			};
			BYTE s_rtEntryActual[5];
			memcpy(s_rtEntryActual, (const void*)rtEntry,
				sizeof(s_rtEntryActual));
			const bool s_rtEntryOk = (memcmp(s_rtEntryActual,
				s_rtEntryExpect, sizeof(s_rtEntryActual)) == 0);
			char s_rtEntryHex[sizeof(s_rtEntryActual) * 3 + 1];
			s_rtEntryHex[sizeof(s_rtEntryHex) - 1] = 0;
			for(size_t i = 0; i < sizeof(s_rtEntryActual); i++)
				snprintf(s_rtEntryHex + i * 3, 4, "%02X ",
					(unsigned)s_rtEntryActual[i]);
			dbglog("[shadow_install] 0x%X bytes=%s %s "
			       "(expect 51 53 57 8B F9)",
				(unsigned)rtEntry, s_rtEntryHex,
				s_rtEntryOk ? "OK" : "MISMATCH");
			if(!s_rtEntryOk){
				dbglog("[shadow_install] 0x%X verify FAILED — "
				       "CRealTimeShadowManager::Update entry hook "
				       "SKIPPED, vanilla path kept", (unsigned)rtEntry);
			}else{
				BYTE *rtTramp = (BYTE*)VirtualAlloc(NULL, 32,
					MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
				if(rtTramp){
					memcpy(rtTramp, (const void*)rtEntry,
						sizeof(s_rtEntryExpect));
					rtTramp[5] = 0xE9;	// jmp entry+5
					*(DWORD*)(rtTramp + 6) =
						(rtEntry + 5) - (DWORD)(rtTramp + 5) - 5;
					CRealTimeShadowManager__Update_orig =
						(void (*)(void))rtTramp;
					InjectHook(rtEntry,
						CRealTimeShadowManager__Update_hook, PATCH_JUMP);
					dbglog("[shadow_install] 0x%X entry hook installed "
					       "(tramp=%p)", (unsigned)rtEntry,
						(void*)rtTramp);
				}else
					dbglog("[shadow_install] 0x%X entry hook: "
					       "VirtualAlloc failed — SKIPPED",
						(unsigned)rtEntry);
			}
		}
}

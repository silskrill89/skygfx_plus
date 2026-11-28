// clouds.cpp - CLOUD RENDER-STATE HYGIENE - extraction 2026-11-28 (soft-particle tree)
// Owns: restoreStage0Tex + CClouds__Render_hygiene (byte-identical from main.cpp).
// CClouds__Render_orig pointer stays in main.cpp (installs at main.cpp:3552-3553 write it).
// Granular gates: cloudNoise/Filter/Alpha/Fog/Stage0/ZFix (Config fields stay in skygfx.h).
// Receipts byte-stable: [CloudShield] restored stage-0, cloud_depthguard, sky_probe2.
// 
#include "skygfx.h"
#include "postfx.h"
#include "noise.h"	// SkyNoise::Init/UniversalSize/BindAndUpload (cloud s9 block)
#include <d3d9.h>
#include <injector\injector.hpp>

// local externs (no shared header idiom)
void _rwD3D9RenderStateFlushCache(void);
extern void spriteProducerWindowClouds(bool on);
extern void (*CClouds__Render_orig)(void);
extern void effectsForceAlphaBlock(const char *tag);
extern RwUInt32 pipeAlphaFuncToD3D(RwUInt32 rwFunc);
extern void fbWp_ArmOnce(void);
extern void pipeDeviceStateSet(D3DRENDERSTATETYPE state, DWORD value);

// Stage-0 texture-raster restore (R5, 2026-11-09): the two CClouds paths
// (pass-through and normal-exit) restore the pre-cloud stage-0 raster with the
// same park-and-set L1 + raw L3 sequence. Consolidated here; each path passes
// its own dbglog_once token so the two sites can never starve each other (the
// old shared "cloud_texraster" token let one path's firing suppress the
// other's). Receipt text unchanged.
static void
restoreStage0Tex(RwRaster *savedTexRaster, IDirect3DTexture9 *savedDevTex0, const char *onceToken)
{
	RwRaster *whiteRas = gpWhiteTexture ? gpWhiteTexture->raster : NULL;
	RwRaster *texPark = (savedTexRaster == whiteRas)
		? (RwRaster*)NULL : whiteRas;
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)texPark);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)savedTexRaster);
	if(d3d9device)
		d3d9device->SetTexture(0, savedDevTex0);
	if(savedTexRaster && savedTexRaster != whiteRas &&
	   dbglog_once(onceToken))
		dbglog("[CloudShield] restored stage-0 raster=%p dev=%p",
			(void*)savedTexRaster, (void*)savedDevTex0);
}

void
CClouds__Render_hygiene(void)
{
	// Granular sub-fix gates (2026-11-28): each defaults to 1 and disables
	// ONLY its own piece; no single switch can kill cloud correctness.
	const bool cloudNoiseFixOn  = (config != NULL) && (config->cloudNoiseFix != 0);
	const bool cloudFilterFixOn = (config != NULL) && (config->cloudFilterFix != 0);
	const bool cloudAlphaFixOn  = (config != NULL) && (config->cloudAlphaFix != 0);
	const bool cloudFogFixOn    = (config != NULL) && (config->cloudFogFix != 0);
	const bool cloudStage0FixOn = (config != NULL) && (config->cloudStage0Fix != 0);
	const bool cloudZFixOn      = (config != NULL) && (config->cloudZFix != 0);

	// Xbox-clouds lane hunk C: once per frame, stamp the cloud noise
	// parameter block (c49-c52, the reserved SkyNoise slots) and bind the
	// universal tile to sampler s9 for the clouds PS consumed by the
	// SpriteFlushIndexedDraw cloud path below. Guarded on cloudsPS: no
	// consumer shader loaded, no upload. Frame-guarded: this hook wraps BOTH
	// CClouds::Render call sites (0x53DCA2/0x53DFA0) and must upload once.
	// s9 is the project-wide noise reservation (noise.h / pipeBindNoise) —
	// same no-restore convention as buildingPipe's pipeBindNoise(9).
	if(cloudNoiseFixOn && cloudsPS && d3d9device){
		static unsigned s_cloudNoiseFrame = 0xFFFFFFFFu;
		unsigned frameIndex = (unsigned)RWSRCGLOBAL(renderFrame);
		if(frameIndex != s_cloudNoiseFrame){
			s_cloudNoiseFrame = frameIndex;
			SkyNoise::Init(1337u);	// idempotent; same fixed seed as pipeBindNoise
			float timeSeconds = (float)(GetTickCount() & 0x7FFFFFFFu) * 0.001f;
			// Weather knobs (CLOUDS H1): coverage drives both the noise
			// coverage slot (p49.z) and the density key (p51.w); p52.w
			// carries the same UniversalSize() stamp pipeBindNoise writes
			// (pipelinecommon.cpp:2269). Erosion (p49.w) scales down with
			// coverage so heavy weather thins the noise field.
			float coverage = CWeather__CloudCoverage;
			float p49[4] = { 1.0f, 2.31f, coverage, 0.35f * (1.0f - coverage) };
			float p50[4] = { 0.0f, 0.0f, timeSeconds, (float)frameIndex };
			float p51[4] = { 3.0f, 1.0f, 0.25f, coverage };
			float p52[4] = { 0.0f, 0.15f, 0.0f, (float)SkyNoise::UniversalSize() };
			SkyNoise::BindAndUpload(9, d3d9device, timeSeconds, frameIndex,
				p49, p50, p51, p52);
		}
	}

	// Sun corona / moon / streaks / lens flare draw inside this call and are
	// the FIRST sprites of the scene pass (before any pipe entry can repair
	// device drift) — align layers (1)/(2)/(3) first. CClouds::Render only
	// sets SRCBLEND/DESTBLEND/VERTEXALPHA through the rw cache, so a device
	// parked on SRCALPHA/INVSRCALPHA behind an unchanged cache turns the
	// additive sun glow into a black square (see effectsForceAlphaBlock).
	// Unconditional: the corona draw does not depend on any cloud sub-fix.
	effectsForceAlphaBlock("Clouds");

	// Sky/sky-dome entry probe (black-screen bisect). CClouds::Render is the
	// FIRST draw of the scene pass and the sky dome is drawn FIXED-FUNCTION —
	// but "fixed function" in D3D9 still executes whatever pixel/vertex shader
	// is currently bound. This hook neutralizes PS/VS on EXIT (see the restore
	// block below) but never on ENTRY, so a shader left armed by the LAST
	// postfx pass of the PREVIOUS frame (DrawFinalEffects: SSAO composite /
	// PipeChain pass 3 / grading / tonemap / YCbCr all bind and do not
	// unbind) would shade the entire sky dome. That signature matches the
	// report exactly: outdoors-only full-screen black, interiors perfect,
	// ~1px margin at the raster edge, geometry poking through at the screen
	// edges, additive sun/streaks still visible on top.
	// READ-ONLY probe (throttled) — records what is actually armed at sky
	// time so the next run answers this with evidence instead of inference.
	// A non-NULL ps/vs here is the smoking gun.
	if(dbglog_throttle("sky_probe2")){
		IDirect3DVertexShader9 *pSkyVS = NULL;
		IDirect3DPixelShader9  *pSkyPS = NULL;
		DWORD sAlphaTest = 0xDEAD, sAlphaFn = 0xDEAD, sAlphaRef = 0xDEAD, sBlend = 0xDEAD;
		if(d3d9device){
			d3d9device->GetVertexShader(&pSkyVS);
			d3d9device->GetPixelShader(&pSkyPS);
			d3d9device->GetRenderState(D3DRS_ALPHATESTENABLE, &sAlphaTest);
			d3d9device->GetRenderState(D3DRS_ALPHAFUNC, &sAlphaFn);
			d3d9device->GetRenderState(D3DRS_ALPHAREF, &sAlphaRef);
			d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &sBlend);
		}
		RwRaster *skyRs = (Scene.camera) ? RwCameraGetRaster(Scene.camera) : NULL;
		dbglog("[SKY] entry: camRas=%p %dx%d  DEV ps=%p vs=%p  alphaTest=%u fn=%u ref=%u blend=%u",
			(void*)skyRs,
			skyRs ? skyRs->width : -1, skyRs ? skyRs->height : -1,
			(void*)pSkyPS, (void*)pSkyVS,
			(unsigned)sAlphaTest, (unsigned)sAlphaFn,
			(unsigned)sAlphaRef, (unsigned)sBlend);
		// Get* AddRef'd — release (same pairing as vs_probe / the DefCB
		// probe; the old code leaked one VS+PS ref pair per throttle hit).
		if(pSkyVS) pSkyVS->Release();
		if(pSkyPS) pSkyPS->Release();
	}

	// DEBUG probe: secondary (re)arm point for the frameBuffer write
	// watchpoint. The primary install is now the first device hook in
	// RenderScene_before (+ DLL init) — sky audit rework item 1 — this
	// stays as a mid-frame refresh for the outdoor path. No-op unless
	// postfxDumpDebug=1; idempotent per frame.
	if(config && config->postfxDumpDebug)
		fbWp_ArmOnce();

	// ---- stage-0 texture raster save (both paths) ----
	// CClouds binds its cloud/sprite texture into stage 0 and the old restore
	// never put the raster back, so the rw cache and the raw device could
	// stay on the cloud atlas past the sky pass — peds then sampled it as a
	// flat texel at some camera angles. Captured BEFORE any gate so the
	// pass-through path restores it too. L1 value + L3 texture
	// (GetTexture AddRefs; Released after the restore). POD only.
	void *savedTexRasterRaw = NULL;
	RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &savedTexRasterRaw);
	// Failed-Get sentinel (same convention as pedStage0Resync /
	// flushBatchIdentity): 0xFFFFFFFF must never be pushed back into the
	// rw cache by the restore blocks below.
	if(savedTexRasterRaw == (void*)0xFFFFFFFF)
		savedTexRasterRaw = NULL;
	RwRaster *savedTexRaster = (RwRaster*)savedTexRasterRaw;
	IDirect3DTexture9 *savedDevTex0 = NULL;
	if(d3d9device){
		// GetTexture wants IDirect3DBaseTexture9**; stage 0 is a 2D texture.
		IDirect3DBaseTexture9 *baseTex = NULL;
		d3d9device->GetTexture(0, &baseTex);
		savedDevTex0 = (IDirect3DTexture9*)baseTex;
	}

	if(!CClouds__Render_orig){
		// Nothing forced on this path — no restore to protect, no guard needed.
		// The producer flag still wraps the original: the shared sprite flush
		// must never force a non-additive pair over clouds (which draw
		// natively ONE/ONE). Both producers now force the same ONE/ONE pair;
		// this flag is the receipt label. POD writes, no SEH on this path.
		if(CClouds__Render_orig){
			spriteProducerWindowClouds(true);
			CClouds__Render_orig();
			spriteProducerWindowClouds(false);
			// Stage-0 raster restore for this path too (park-and-set through
			// L1, then the raw L3 texture that was bound when we entered).
			if(cloudStage0FixOn)
				restoreStage0Tex(savedTexRaster, savedDevTex0, "cloud_texraster_passthru");
		}
		// Release on BOTH sub-paths (orig present, or orig NULL and no draw).
		if(savedDevTex0){
			savedDevTex0->Release();
			savedDevTex0 = NULL;
		}
		return;
	}

	// ---- save (pre-seeded so a failed Get restores sane values) ----
	RwUInt32 sFilter  = rwFILTERLINEAR;
	RwUInt32 sAlphaFn = rwALPHATESTFUNCTIONGREATEREQUAL;
	RwUInt32 sAlphaRf = 1;
	RwUInt32 sFog     = FALSE;
	RwRenderStateGet(rwRENDERSTATETEXTUREFILTER, &sFilter);
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &sAlphaFn);
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, &sAlphaRf);
	RwRenderStateGet(rwRENDERSTATEFOGENABLE, &sFog);

	DWORD sMinF = D3DTEXF_LINEAR, sMagF = D3DTEXF_LINEAR, sMipF = D3DTEXF_LINEAR;
	DWORD sFogDev = FALSE;
	// Z-test/Z-func (sky audit round-5 restore hole): CClouds::Render only
	// rw-cache-sets ZTEST=FALSE, so raw D3DRS_ZENABLE/ZFUNC drift (DepthHook
	// raw ZENABLE writes, postfx Save/RestoreRawGeomStates, foreign raw
	// writes) is invisible to the old restore and leaks straight into the
	// sky draw behind this hook. Save all layers now (pre-seeded, a failed
	// Get never reaches the device) — ZTEST has an rw state, ZFUNC does not
	// (raw layers only).
	RwUInt32 sZTest = TRUE;
	RwRenderStateGet(rwRENDERSTATEZTESTENABLE, &sZTest);
	DWORD sZEn = TRUE, sZFunc = D3DCMP_LESSEQUAL;
	if(d3d9device){
		d3d9device->GetSamplerState(0, D3DSAMP_MINFILTER, &sMinF);
		d3d9device->GetSamplerState(0, D3DSAMP_MAGFILTER, &sMagF);
		d3d9device->GetSamplerState(0, D3DSAMP_MIPFILTER, &sMipF);
		d3d9device->GetRenderState(D3DRS_FOGENABLE, &sFogDev);
		d3d9device->GetRenderState(D3DRS_ZENABLE, &sZEn);
		d3d9device->GetRenderState(D3DRS_ZFUNC, &sZFunc);
	}
	// L2 driver-cache legs for the depth pair (pre-seeded canonical): the
	// disagreement guard at the restore needs the cache consensus to tell a
	// device-only drift (the [zfix] dev zen=0 zfunc=8 signature) from a
	// genuine entry state — ZFUNC has no rw twin, so the driver cache is its
	// only cache leg. Same capture idiom as pipeProbeState's drvZEnable/
	// drvZFunc; a Get the driver cannot report keeps the pre-seeded value.
	DWORD sZEnDrv = TRUE, sZFuncDrv = D3DCMP_LESSEQUAL;
	RwD3D9GetRenderState(D3DRS_ZENABLE, &sZEnDrv);
	RwD3D9GetRenderState(D3DRS_ZFUNC, &sZFuncDrv);

	// ---- blend/write block (cloud-corruption restore completeness) ----
	// CClouds draws with ZWRITE=FALSE, ALPHABLEND ON, SRC/DEST=ONE/ONE and
	// sets stage-0 COLOROP/ALPHAOP for its sprite texture; vanilla restores
	// none of it. Left live, the following reflection/env/mirror sub-passes
	// inherit ZWRITE off + additive ONE/ONE (the "ZWRITE off/desynced"
	// poison). Captured per layer before the original runs; all POD locals,
	// C2712-safe. NB: the rw twin of D3DRS_ALPHABLENDENABLE is
	// rwRENDERSTATEVERTEXALPHAENABLE, so sBlendDrv is also the L2 capture
	// of VERTEXALPHAENABLE (same device state, one value).
	RwUInt32 sZWriteRw = TRUE, sVtxAlphaRw = TRUE;
	RwUInt32 sSrcRw = rwBLENDSRCALPHA, sDstRw = rwBLENDINVSRCALPHA;
	RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &sZWriteRw);
	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &sVtxAlphaRw);
	RwRenderStateGet(rwRENDERSTATESRCBLEND, &sSrcRw);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, &sDstRw);

	DWORD sZWriteDrv = TRUE, sBlendDrv = TRUE;
	DWORD sSrcDrv = (DWORD)D3DBLEND_SRCALPHA, sDstDrv = (DWORD)D3DBLEND_INVSRCALPHA;
	RwD3D9GetRenderState(D3DRS_ZWRITEENABLE, &sZWriteDrv);
	RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE, &sBlendDrv);
	RwD3D9GetRenderState(D3DRS_SRCBLEND, &sSrcDrv);
	RwD3D9GetRenderState(D3DRS_DESTBLEND, &sDstDrv);

	DWORD sZWriteDev = TRUE, sBlendDev = TRUE;
	DWORD sSrcDev = (DWORD)D3DBLEND_SRCALPHA, sDstDev = (DWORD)D3DBLEND_INVSRCALPHA;
	DWORD sStageColorOp = D3DTOP_MODULATE, sStageAlphaOp = D3DTOP_MODULATE;
	if(d3d9device){
		d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &sZWriteDev);
		d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &sBlendDev);
		d3d9device->GetRenderState(D3DRS_SRCBLEND, &sSrcDev);
		d3d9device->GetRenderState(D3DRS_DESTBLEND, &sDstDev);
		d3d9device->GetTextureStageState(0, D3DTSS_COLOROP, &sStageColorOp);
		d3d9device->GetTextureStageState(0, D3DTSS_ALPHAOP, &sStageAlphaOp);
	}

	// ---- ROUGH REDUCTION (2026-11-10): split-entry detection receipt removed
	// (pure logging — the restore writes ONE source, rw-capture through
	// L1+L2+L3, so a split at entry cannot survive the close). ----

	// ---- force (both layers; flush first so the rw sets are real
	// transitions instead of no-ops against a desynced cache) ----
	_rwD3D9RenderStateFlushCache();
	// ENTRY neutralize: CClouds::Render draws the sky dome, cloud sprites,
	// sun/moon corona, streaks and lens flare FIXED-FUNCTION (it binds no
	// shaders of its own — it sets only ZWRITE/ZTEST/VERTEXALPHA/SRCBLEND/
	// DESTBLEND). In D3D9 a still-bound pixel/vertex shader runs anyway, so
	// anything the previous frame's post chain left armed executes over the
	// whole sky. This hook already neutralized PS/VS on EXIT, but the entry
	// side was unguarded, so the last DrawFinalEffects binder of frame N
	// (SSAO composite / PipeChain pass 3 / grading / tonemap / YCbCr — none
	// of which unbind) was still live at the first draw of frame N+1.
	// Nulling is strictly safe: there is no game shader to preserve on this
	// path, and RW3.6's own fixed-function draws rely on PS/VS being unset.
	if(cloudStage0FixOn){
		RwD3D9SetPixelShader(NULL);
		RwD3D9SetVertexShader(NULL);
		if(d3d9device){
			d3d9device->SetPixelShader(NULL);
			d3d9device->SetVertexShader(NULL);
		}
	}
	if(cloudFilterFixOn)
		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	if(cloudAlphaFixOn)
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONALWAYS);
	if(cloudFogFixOn)
		RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	if(d3d9device){
		if(cloudFilterFixOn){
			d3d9device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
			d3d9device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
			d3d9device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
		}
		if(cloudAlphaFixOn){
			RwD3D9SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D(rwALPHATESTFUNCTIONALWAYS));
			RwD3D9SetRenderState(D3DRS_ALPHAREF, 0);
			d3d9device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS);
			d3d9device->SetRenderState(D3DRS_ALPHAREF, 0);
		}
		if(cloudFogFixOn){
			RwD3D9SetRenderState(D3DRS_FOGENABLE, FALSE);
			d3d9device->SetRenderState(D3DRS_FOGENABLE, FALSE);
		}
		if(cloudStage0FixOn){
			// Stage-0 colour/alpha ops: CClouds NEVER sets them, so whatever the
			// previous pass left (SELECTARG1/SELECTARG2 from an Im2D pass) would
			// otherwise govern the clouds. Raw device only (no rw twin); the
			// snapshot above already captured both and the restore block below
			// puts them back, so this force is fully covered.
			d3d9device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
			d3d9device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		}
	}

	// ROUGH REDUCTION (2026-11-10): SEH __try/__except removed — call the
	// original directly (AAP style; no fault-path restore gymnastics). The
	// producer flag still wraps the call so the flush shield sees ONE/ONE.
	spriteProducerWindowClouds(true);	// producer flag: flush shield must pick ONE/ONE
	CClouds__Render_orig();
	spriteProducerWindowClouds(false);

	// ---- restore: rw cache first (so later Gets agree), then the raw force
	// closes the gap where the cache already read the saved value ----
	if(cloudFilterFixOn)
		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)sFilter);
	if(cloudAlphaFixOn){
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)sAlphaFn);
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)sAlphaRf);
		RwD3D9SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D(sAlphaFn));
		RwD3D9SetRenderState(D3DRS_ALPHAREF, sAlphaRf);
	}
	if(cloudFogFixOn){
		RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)sFog);
		RwD3D9SetRenderState(D3DRS_FOGENABLE, sFogDev ? TRUE : FALSE);
	}
	// Z-test/Z-func restore — three-layer (rw cache + driver cache + raw
	// device), mirror of depthhook.cpp:470-472. Sanitize the saved values
	// first (same convention as D3D9RenderDual): a failed Get must never
	// push garbage Z state onto the device.
	if(sZEn > D3DZB_USEW) sZEn = TRUE;
	if(sZFunc < D3DCMP_NEVER || sZFunc > D3DCMP_ALWAYS) sZFunc = D3DCMP_LESSEQUAL;
	// Z-state disagreement guard (rw_fixes.cpp:463 spriteRestoreZWrite idiom,
	// same rule as spriteFlushShieldClose's ZWRITE replay and
	// pipeEnforceDepthContract's healthy-baseline check): the depth legs are
	// captured per layer, so a device-only drift at window entry (the [zfix]
	// signature rw/drv claim canonical while raw dev says zen=0 zfunc=8) used
	// to be replayed VERBATIM here — RwD3D9SetRenderState republishes the
	// device capture into the L2 driver cache and the raw L3 leg below pins
	// it, converting a device-only drift into a cemented 3-layer one (the
	// exact defect class rw_fixes.cpp:463-464 solves). When the legs
	// disagree, or any leg holds a non-canonical value, the corrupted leg is
	// NOT replayed: the canonical world baseline (ZENABLE=TRUE,
	// ZFUNC=LESSEQUAL) goes to every layer instead — what the next pipe
	// entry's [zfix] would enforce anyway. A capture whose legs all hold the
	// canonical pair (the normal case) replays verbatim, byte-identical to
	// the old behaviour. sZEnDrv/sZFuncDrv are capture-only (the L2 replay
	// below re-derives from sZEn/sZFunc via RwD3D9SetRenderState), so only
	// the three replayed locals are corrected.
	bool depthLegsCanonical = ((sZTest ? TRUE : FALSE) == TRUE) &&
		(sZEnDrv == TRUE) &&
		(sZFuncDrv == (DWORD)D3DCMP_LESSEQUAL) &&
		(sZEn == TRUE) &&
		(sZFunc == (DWORD)D3DCMP_LESSEQUAL);
	if(cloudZFixOn && !depthLegsCanonical){
		if(dbglog_throttle("cloud_depthguard"))
			dbglog("[CloudShield] ZFUNC guard: captured dev zfunc=%d zen=%d → canonical",
				(int)sZFunc, (int)sZEn);
		sZTest = TRUE;
		sZEn = TRUE;
		sZFunc = D3DCMP_LESSEQUAL;
	}
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)(sZTest ? TRUE : FALSE));
	RwD3D9SetRenderState(D3DRS_ZENABLE, sZEn);
	RwD3D9SetRenderState(D3DRS_ZFUNC, sZFunc);
	// Blend/write restore — SINGLE SOURCE (the rw-cache capture) written
	// through the shared L1+L2+L3 chokepoint. The states CClouds (and the
	// cloud-pair flush shield) overwrite are ZWRITE, ALPHABLENDENABLE (rw
	// twin VERTEXALPHAENABLE), SRCBLEND and DESTBLEND. The old restore
	// mixed sources — L1/L2 replayed the rw/driver caches while raw L3
	// replayed the device capture — so whenever those captures disagreed at
	// window entry (the split the shield exists to paper over) the close
	// re-installed a layer split for [FxAlpha] to repair downstream.
	// pipeDeviceStateSet derives L2/L3 from the SAME rw value, so the
	// cache's canonical block wins and the layers are coherent after close.
	// Stage-0 COLOROP/ALPHAOP have no rw twin — raw device save/restore only
	// (CClouds sets them for its sprite texture; leaving them breaks the
	// next world draw). Those stay on the raw L3 leg below.
	// SEPARATEALPHABLENDENABLE / COLORWRITEENABLE are deliberately NOT
	// captured here: coronaDrawRawShield parks them per forced draw and
	// spriteFlushShieldClose (per batch) + CoronaWindow_Close (frame
	// boundary) own their replay from their own snapshots. Replaying them
	// from this block would double-own that capture and clobber the flush
	// shield's wave — verified consistent with the CoronaWindow_Close fix.
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)sZWriteRw);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)sVtxAlphaRw);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)sSrcRw);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)sDstRw);
	if(d3d9device){
		pipeDeviceStateSet(D3DRS_ZWRITEENABLE,
			(DWORD)(sZWriteRw ? TRUE : FALSE));
		pipeDeviceStateSet(D3DRS_ALPHABLENDENABLE,
			(DWORD)(sVtxAlphaRw ? TRUE : FALSE));
		pipeDeviceStateSet(D3DRS_SRCBLEND, (DWORD)sSrcRw);
		pipeDeviceStateSet(D3DRS_DESTBLEND, (DWORD)sDstRw);
	}
	// Shader/stage neutralize (PS=NULL is the blessed neutral for the
	// fixed-function sky draw): a leaked pixel/vertex shader or the s5
	// cluster texture (ForwardPlus) would ride the clouds' raw state into
	// the next draw. RW API + raw device both, so raw-set drift can't hide
	// behind RW's shader cache.
	if(cloudStage0FixOn){
		RwD3D9SetPixelShader(NULL);
		RwD3D9SetVertexShader(NULL);
		if(d3d9device){
			d3d9device->SetPixelShader(NULL);
			d3d9device->SetVertexShader(NULL);
			d3d9device->SetTexture(5, NULL);
		}
	}
	if(d3d9device){
		if(cloudFilterFixOn){
			d3d9device->SetSamplerState(0, D3DSAMP_MINFILTER, sMinF);
			d3d9device->SetSamplerState(0, D3DSAMP_MAGFILTER, sMagF);
			d3d9device->SetSamplerState(0, D3DSAMP_MIPFILTER, sMipF);
		}
		if(cloudAlphaFixOn){
			d3d9device->SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D(sAlphaFn));
			d3d9device->SetRenderState(D3DRS_ALPHAREF, sAlphaRf);
		}
		if(cloudFogFixOn)
			d3d9device->SetRenderState(D3DRS_FOGENABLE, sFogDev ? TRUE : FALSE);
		d3d9device->SetRenderState(D3DRS_ZENABLE, sZEn);
		d3d9device->SetRenderState(D3DRS_ZFUNC, sZFunc);
		if(cloudStage0FixOn){
			// Stage-0 ops — raw-only (no rw twin). The blend/write L3 pin moved
			// to the pipeDeviceStateSet calls above (one source: rw capture).
			d3d9device->SetTextureStageState(0, D3DTSS_COLOROP, sStageColorOp);
			d3d9device->SetTextureStageState(0, D3DTSS_ALPHAOP, sStageAlphaOp);
		}
	}
	// Stage-0 texture raster restore — the missing half of this hygiene block.
	// CClouds binds its cloud/sprite texture and never puts the entry raster
	// back; left desynced, the next texture-less/phased consumer keeps the
	// cloud atlas (peds as a flat atlas texel at certain camera angles).
	// Park-and-set through L1 so a cache-hit cannot no-op, then the raw L3
	// texture captured before the original ran (GetTexture AddRef'd).
	if(cloudStage0FixOn)
		restoreStage0Tex(savedTexRaster, savedDevTex0, "cloud_texraster_normal");
	if(savedDevTex0){
		savedDevTex0->Release();
		savedDevTex0 = NULL;
	}
}

// postfx_smaa.cpp - SMAA passes + static resources (m0060 split).
#include "skygfx.h"
#include "debug_handl.h"
#include "postfx.h"
#include "depthhook.h"
// SMAA intermediate rasters (file-scope for cleanup on shutdown)
static RwRaster *g_smaaEdgeRaster = NULL;
static RwRaster *g_smaaBlendRaster = NULL;
static RwRaster *g_smaaPrevFrameRaster = NULL;
// Camera-sized copy of the final graded frame, refreshed at SMAA entry.
// SMAA sampling doctrine: every raster the SMAA shaders touch (scene input,
// edge, blend, prevFrame history, velocity, depth) must be CAMERA-sized with
// content filling UV 0..1, drawn with s_ffQuad (UV 0..1). The game's
// colorfilterVerts are a hardcoded 2048x2048 quad (positions 0..2048, UV
// 0..1) whose visible window only spans UV 0..0.9375 x 0..0.5273 — correct
// for the padded front buffer, but WRONG for camera-sized rasters: pass 3's
// prevFrame/velocity samples landed on the top-left 93.75%x52.7% sub-rect of
// the history (spatially compressed vs the current frame) -> ghosting/
// doubling; and the c1 texel size (1/1920,1/1080) was measured in the wrong
// UV space (1/2048,1/2048 in quad-UV space) -> edge taps off by 1.9x
// vertically. Camera-sized scene copy + UV 0..1 quad makes every tap exact.
static RwRaster *g_smaaSceneRaster = NULL;
static int g_smaaRtWidth = 0, g_smaaRtHeight = 0;
static RwTexture *g_smaaBlendTexRW = NULL;
static RwTexture *g_smaaPrevFrameTexRW = NULL;
static bool s_smaaRastersInitialized = false;
static bool s_smaaPendingInit = false;  // set by DrawSMAA, consumed by SMAATryInitRasters
static bool s_smaaBroken = false;       // fail-open latch: set on init failure OR any
                                        // guarded fault in DrawSMAA; skips until resolution change

// Raw geometry-state snapshot for DrawSMAA (file-scope so smaaGuardBail can
// restore from any early-exit without extra parameters). DrawSMAA raw-writes
// D3DRS_ALPHATESTENABLE/CULLMODE/ZENABLE/ZWRITEENABLE — it was the ONLY
// postfx pass without a SaveRawGeomStates/RestoreRawGeomStates wrap. Those
// raw writes are device-side only, so ImmediateModeRenderStatesReStore()
// (rw-cached) does not undo them: the values leaked past SMAA (last pass of
// the frame) into the next frame's scene and showed up at every pipe entry as
// "[BUILDING] ZWRITE off/desynced on entry (rw=1 dev=0)" and
// "[PipeAlpha] ... test=0". RestoreRawGeomStates re-pushes both layers.
static DWORD s_smaaRawGeom[9];
static bool s_smaaRawGeomSaved = false;

// SEV1: the final restore used to cover only samplers 1-4, never
// rwRENDERSTATETEXTURERASTER — the SMAA passes set it on stage 0 (scene
// copy / edge / history rasters) and ImmediateModeRenderStatesReStore does
// NOT cover TEXTURERASTER, so the next consumer sampled SMAA's raster.
// File-scope snapshot (like s_smaaRawGeom) so smaaGuardBail can restore it
// from any early-exit without extra parameters. Pre-seeded so a failed
// RwRenderStateGet leaves sane values; POD-only.
static void *s_smaaStage0Raster = NULL;
static DWORD s_smaaS0AddrU = D3DTADDRESS_WRAP;
static DWORD s_smaaS0AddrV = D3DTADDRESS_WRAP;
static DWORD s_smaaS0MinFilter = D3DTEXF_LINEAR;
static DWORD s_smaaS0MagFilter = D3DTEXF_LINEAR;
static bool s_smaaStage0Saved = false;

static void smaaRestoreRawGeom(void)
{
	if(s_smaaRawGeomSaved){
		RestoreRawGeomStates(s_smaaRawGeom);
		s_smaaRawGeomSaved = false;
	}
}

// SMAA temporal history: set to true after the first frame's history is written.
// On the first frame, the history raster is black (never rendered to), so the
// temporal blend would produce a very dark image. We use blendStrength=1.0
// (all current, no history) on the first frame to avoid this.
static bool g_smaaHistoryValid = false;
// Hard history TTL: count of valid-history frames since the last full refresh.
// Every 8 frames the temporal resolve forces blendStrength=1.0 (all current) to
// kill the accumulated ghost tail, then restarts the count. Reset to 0 whenever
// history is invalidated (resource release / resolution change).
static int s_smaaHistAge = 0;

// Invalidate the temporal history. Called wherever the history raster can no
// longer be trusted: ReleaseSMAAStaticResources (shutdown / device reset / SMAA
// disabled) and the resolution-change path in DrawSMAA. The raster itself is
// destroyed by those callers and a freshly created D3DPOOL_DEFAULT raster holds
// UNDEFINED content, so the next pass 3 must (a) run with blendStrength=1.0
// (guaranteed by the false flag below) and (b) clear the raster before reading
// it (see the pass-3 history-clear block). Without (b) undefined content could
// be blended into the presented camera raster if the shader floor ever drifted.
static void smaaInvalidateHistory(const char *why)
{
	bool wasValid = g_smaaHistoryValid;
	g_smaaHistoryValid = false;
	s_smaaHistAge = 0;
	if(wasValid)
		dbglog("[SMAA] history INVALIDATED (%s): next pass3 uses blendStrength=1.0 and clears the history raster", why);
}

// Scratch camera + Z raster used to force-init SMAA CAMERATEXTURE rasters.
// Created once, persists for the session (mirrors envmap.cpp pattern).
// Cleaned up on device reset and shutdown via ReleaseSMAAStaticResources.
// NOTE: The force-init must happen OUTSIDE the main camera's BeginUpdate
// (i.e., from RenderScene_before), not inline in DrawSMAA (which runs
// mid-frame from ColourFilter_switch/DrawFinalEffects). Even with a
// scratch camera, RW 3.6's D3D9 driver crashes creating render-target
// textures while the device is in an active render state.
static RwCamera *s_smaaInitCam = NULL;
static RwRaster *s_smaaInitZRas = NULL;

void ReleaseSMAAStaticResources(void)
{
	dbglog("ReleaseSMAAStaticResources: releasing...");

	// Release scratch camera created by SMAATryInitRasters()
	if(s_smaaInitZRas){ RwRasterDestroy(s_smaaInitZRas); s_smaaInitZRas = NULL; }
	if(s_smaaInitCam){
		RwFrame *f = RwCameraGetFrame(s_smaaInitCam);
		if(f){ RwFrameDestroy(f); }
		RwCameraDestroy(s_smaaInitCam);
		s_smaaInitCam = NULL;
	}
	if(g_smaaBlendTexRW){ RwTextureDestroy(g_smaaBlendTexRW); g_smaaBlendTexRW = NULL; }
	if(g_smaaPrevFrameTexRW){ RwTextureDestroy(g_smaaPrevFrameTexRW); g_smaaPrevFrameTexRW = NULL; }
	if(g_smaaEdgeRaster){ UntrackRaster(g_smaaEdgeRaster); RwRasterDestroy(g_smaaEdgeRaster); g_smaaEdgeRaster = NULL; }
	if(g_smaaBlendRaster){ UntrackRaster(g_smaaBlendRaster); RwRasterDestroy(g_smaaBlendRaster); g_smaaBlendRaster = NULL; }
	if(g_smaaPrevFrameRaster){ UntrackRaster(g_smaaPrevFrameRaster); RwRasterDestroy(g_smaaPrevFrameRaster); g_smaaPrevFrameRaster = NULL; }
	if(g_smaaSceneRaster){ UntrackRaster(g_smaaSceneRaster); RwRasterDestroy(g_smaaSceneRaster); g_smaaSceneRaster = NULL; }
	g_smaaRtWidth = 0; g_smaaRtHeight = 0;
	s_smaaRastersInitialized = false;
	s_smaaPendingInit = false;
	s_smaaBroken = false;
	// History raster destroyed above — flag it invalid so the next pass 3
	// runs with blendStrength=1.0 and clears the fresh (undefined-content)
	// raster before reading it.
	smaaInvalidateHistory("ReleaseSMAAStaticResources");
	dbglog("ReleaseSMAAStaticResources: done");
}
// SMAA D3D textures (D3DPOOL_DEFAULT - must be released on device reset)
IDirect3DTexture9 *g_smaaAreaTex = NULL;
IDirect3DTexture9 *g_smaaSearchTex = NULL;
static inline bool CheckDeviceState(void)
{
	// Fail fast while the device is lost — TestCooperativeLevel is cheap and
	// every RT bind / Im2D draw downstream would fault or fail anyway.
	if (!d3d9device || d3d9device->TestCooperativeLevel() == D3DERR_DEVICELOST)
		return false;
	// RW camera BeginUpdate handles device state internally
	// Just check if we have a valid camera and raster
	if (!Scene.camera) return false;
	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	return camRas != NULL;
}

#include "AreaTex.h"
#include "SearchTex.h"

void GenerateSMAAAreaTex(IDirect3DDevice9 *dev, IDirect3DTexture9 **outTex)
{
	if(*outTex) return;
	if(FAILED(dev->CreateTexture(AREATEX_WIDTH, AREATEX_HEIGHT, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8L8, D3DPOOL_DEFAULT, outTex, NULL))){
		dbglog("GenerateSMAAAreaTex: CreateTexture failed");
		return;
	}
	D3DLOCKED_RECT rect;
	if(FAILED((*outTex)->LockRect(0, &rect, NULL, D3DLOCK_DISCARD))){
		dbglog("GenerateSMAAAreaTex: LockRect failed");
		(*outTex)->Release();
		*outTex = NULL;
		return;
	}
	for(int y = 0; y < AREATEX_HEIGHT; y++){
		memcpy((char*)rect.pBits + y * rect.Pitch,
		       areaTexBytes + y * AREATEX_PITCH,
		       AREATEX_PITCH);
	}
	(*outTex)->UnlockRect(0);
	dbglog("GenerateSMAAAreaTex: OK %dx%d", AREATEX_WIDTH, AREATEX_HEIGHT);
}

void GenerateSMAASearchTex(IDirect3DDevice9 *dev, IDirect3DTexture9 **outTex)
{
	if(*outTex) return;
	if(FAILED(dev->CreateTexture(SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 1, D3DUSAGE_DYNAMIC, D3DFMT_L8, D3DPOOL_DEFAULT, outTex, NULL))){
		dbglog("GenerateSMAASearchTex: CreateTexture failed");
		return;
	}
	D3DLOCKED_RECT rect;
	if(FAILED((*outTex)->LockRect(0, &rect, NULL, D3DLOCK_DISCARD))){
		dbglog("GenerateSMAASearchTex: LockRect failed");
		(*outTex)->Release();
		*outTex = NULL;
		return;
	}
	for(int y = 0; y < SEARCHTEX_HEIGHT; y++){
		memcpy((char*)rect.pBits + y * rect.Pitch,
		       searchTexBytes + y * SEARCHTEX_PITCH,
		       SEARCHTEX_PITCH);
	}
	(*outTex)->UnlockRect(0);
	dbglog("GenerateSMAASearchTex: OK %dx%d", SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT);
}

// ============================================================
// SMAATryInitRasters — deferred force-init of CAMERATEXTURE
// rasters. Called from RenderScene_before (main.cpp) so the init
// runs OUTSIDE the main camera's BeginUpdate (RW 3.6 D3D9 driver
// crashes creating render-target textures mid-frame).
// Handles SMAA rasters + VCS radiosity rasters.
// Harmless to call every frame; only executes once per resource.
// After init, each raster is tracked via TrackRaster so
// RasterEnsureSurfaceReady can verify it.
// ============================================================
void SMAATryInitRasters(void)
{
	if(!s_smaaPendingInit && !s_vcsRadPendingInit)
		return;

	// Diagnostic: confirm this function is reached
	dbglog("[RasterInit] SMAATryInitRasters: s_smaaPendingInit=%d s_vcsRadPendingInit=%d",
		s_smaaPendingInit, s_vcsRadPendingInit);

	if(!s_smaaInitCam){
		s_smaaInitCam = RwCameraCreate();
		if(s_smaaInitCam){
			// Attach a frame (required by BeginUpdate — crashes without it)
			RwCameraSetFrame(s_smaaInitCam, RwFrameCreate());
			// Attach a small Z raster (BeginUpdate may need one)
			RwRaster *camRasForDepth = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
			int depth = camRasForDepth ? camRasForDepth->depth : 32;
			s_smaaInitZRas = RwRasterCreate(4, 4, depth, rwRASTERTYPECAMERATEXTURE);
			if(s_smaaInitZRas)
				RwCameraSetZRaster(s_smaaInitCam, s_smaaInitZRas);
			dbglog("[RasterInit] Scratch camera created %p with frame + Z raster", s_smaaInitCam);
		} else {
			dbglog("[RasterInit] ERROR: RwCameraCreate failed, cannot init rasters");
			s_smaaBroken = true;
			s_smaaPendingInit = false;
			s_vcsRadPendingInit = false;
			return;
		}
	}

	// Crash fix: save the exe's global camera pointer (0xC9BCC0) before we
	// call RwCameraBeginUpdate on our scratch camera. Each EndUpdate clears it
	// to NULL, and game code at 0x7F98DF dereferences [0xC9BCC0]+0x60 → crash.
	// Restore it after all BeginUpdate/EndUpdate pairs are done.
	void* savedCam = *(void**)0xC9BCC0;

	// ---- SMAA rasters ----
	if(s_smaaPendingInit && g_smaaEdgeRaster && g_smaaBlendRaster && g_smaaPrevFrameRaster && g_smaaSceneRaster){
		RwRaster *initRas[] = { g_smaaEdgeRaster, g_smaaBlendRaster, g_smaaPrevFrameRaster, g_smaaSceneRaster };
		bool allOk = true;
		for(int i = 0; i < 4; i++){
			RwCameraSetRaster(s_smaaInitCam, initRas[i]);
			allOk &= (RwCameraBeginUpdate(s_smaaInitCam) != NULL);
			RwCameraEndUpdate(s_smaaInitCam);
			if(allOk)
				TrackRaster(initRas[i]);
		}
		if(allOk){
			dbglog("[RasterInit] SMAA rasters force-initialized as render targets");
			s_smaaRastersInitialized = true;
			s_smaaPendingInit = false;
		} else {
			dbglog("[RasterInit] ERROR: SMAA raster init failed, disabling SMAA");
			s_smaaBroken = true;
			s_smaaPendingInit = false;
		}
	}

	// ---- VCS radiosity rasters ----
	if(s_vcsRadPendingInit && vcs_radiosity_target1 && vcs_radiosity_target2){
		RwRaster *vcsRas[] = { vcs_radiosity_target1, vcs_radiosity_target2 };
		for(int i = 0; i < 2; i++){
			RwCameraSetRaster(s_smaaInitCam, vcsRas[i]);
			RwCameraBeginUpdate(s_smaaInitCam);
			RwCameraEndUpdate(s_smaaInitCam);
			TrackRaster(vcsRas[i]);
		}
		dbglog("[RasterInit] VCS radiosity rasters force-initialized as render targets");
		s_vcsRadPendingInit = false;
	}

	// Restore the game's camera pointer so 0x7F98DF doesn't read NULL+0x60.
	*(void**)0xC9BCC0 = savedCam;

	// Crash fix (2026-09-14 #3): the scratch camera leaves the last init
	// raster bound as the D3D9 render target, corrupting all downstream
	// rendering (READ crash at 0x7FBD4A +0x60). Detach it and restore the
	// backbuffer so the main frame renders to the correct target.
	RwCameraSetRaster(s_smaaInitCam, NULL);
	if(d3d9device){
		IDirect3DSurface9 *bb = NULL;
		if(SUCCEEDED(d3d9device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))){
			d3d9device->SetRenderTarget(0, bb);
			bb->Release();
		}
		dbglog("[RasterInit] scratch camera detached, backbuffer restored");
	}
}

// SMAA edge-detect pass — extracted so the draw goes through the guardedIm2DRender
// choke-point (DrawSMAA itself can't take __try: error C2712) and so depth-hook
// suspend/restore brackets the draw.
// Returns true on success, false if the pass faulted (caller skips remaining passes).
static bool DrawSMAA_EdgeDetect(float smaaThreshold, float cameraMovement, const float screenParams[4],
                                RwIm2DVertex *verts)
{
	extern IDirect3DTexture9 *g_ssaoDepthTex;
	bool depthSuspended = false;

	// Suspend depth hook so INTZ can be sampled on s2
	if(g_ssaoDepthTex){
		DepthHook_Suspend();
		d3d9device->SetTexture(2, g_ssaoDepthTex);
		depthSuspended = true;
	} else {
		// Defined zero read — never let the edge shader sample foreign s2.
		d3d9device->SetTexture(2, NULL);
	}

	// Bind velocity buffer on stage 3 for combined motion detection.
	// Else-unbind: D3D9 sampler state persists across passes — an unbound
	// stage returns whatever the previous pass left on it, so the motion
	// term would run on foreign data. NULL is a defined (zero) read.
	if(g_velocityTex)
		d3d9device->SetTexture(3, g_velocityTex);
	else
		d3d9device->SetTexture(3, NULL);

	// Temporal always on — lower motion threshold for better stabilization
	float motionThresh = 0.5f;
	float edgeP[4] = {smaaThreshold, motionThresh, 2.0f, cameraMovement};
	RwD3D9SetPixelShaderConstant(0, edgeP, 1);
	RwD3D9SetPixelShaderConstant(1, screenParams, 1);

	// SEH containment + g_inGuardedIm2DPass save/restore now live in
	// guardedIm2DRender (pipelinecommon.cpp) — POD-only body, C2712-safe,
	// single guarded path (no double SEH).
	bool ok = guardedIm2DRender(
		SMAA_EdgeMotionDepth ? SMAA_EdgeMotionDepth : SMAA_Edge,
		rwPRIMTYPETRILIST, verts, 4, colorfilterIndices, 6,
		"smaa_edge");

	// Unbind velocity (s3) on ALL paths — it must not leak into Pass1/Pass2
	// sampler state (Pass2's cleanup also clears it; this keeps the window
	// between passes clean).
	d3d9device->SetTexture(3, NULL);
	// Restore depth hook after edge pass if it was suspended (blend weight doesn't sample depth)
	if(depthSuspended){
		d3d9device->SetTexture(2, NULL); // unbind INTZ before Restore (mirrors DrawHeightFog)
		DepthHook_Restore();
	}

	return ok;
}

static bool SMAA_DrawPass(IDirect3DPixelShader9 *shader, RwIm2DVertex *verts)
{
	// SEH containment + g_inGuardedIm2DPass save/restore live in
	// guardedIm2DRender (POD-only, C2712-safe) — one guarded path, no
	// duplicate __try here.
	return guardedIm2DRender(shader, rwPRIMTYPETRILIST,
		verts, 4, colorfilterIndices, 6, "smaa_draw");
}

// Sampler-state restore for every stage the SMAA chain touches (s1-s4).
// Pass1 sets s1 LINEAR/CLAMP + s2 POINT/CLAMP (area/search, :883-893), Pass3
// sets s2/s3/s4 POINT/CLAMP (velocity/depth/normals, :1048-1094). Stages 3
// (IBL cubemap) and 4 (normal buffer) — and the PBR mask on s2 — are
// raw-bound via dev->SetTexture by their scene-render consumers, so RW's
// per-texture filter/addressing re-application on the next bind does NOT
// cover them: a leaked POINT/CLAMP would point-sample the IBL cubemap and
// normal buffer for the whole frame (blocky reflections, mask artifacts).
// Restore the RW-ish LINEAR/WRAP default on all four — waterPipe_restore-
// RenderState idiom (waterPipe.cpp:1461), raw SetSamplerState.
static void smaaRestoreSamplerStates(IDirect3DDevice9 *dev)
{
	if(!dev)
		return;
	for(int s = 1; s <= 4; s++){
		dev->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		dev->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		dev->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
		dev->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		dev->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	}
}

// Restore the pre-SMAA stage-0 binding captured on DrawSMAA entry. Called
// from BOTH exits (normal tail + smaaGuardBail). Park-and-set through the RW
// cache first (a bare Set back to the cached value would no-op and never
// reach the device), then raw-pin the resolved device texture and the
// sampler-0 fields the passes changed (ADDRESSU/V CLAMP, LINEAR filters).
// POD-only, no SEH here.
static void smaaRestoreStage0(IDirect3DDevice9 *dev)
{
	if(!s_smaaStage0Saved)
		return;
	s_smaaStage0Saved = false;
	RwRaster *saved = (RwRaster*)s_smaaStage0Raster;
	RwRaster *park = g_smaaSceneRaster;
	if(park == saved){
		park = gpWhiteTexture ? gpWhiteTexture->raster : g_smaaEdgeRaster;
		if(park == saved)
			park = NULL;
	}
	if(park != saved){
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)park);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)saved);
	}
	if(!dev)
		return;
	// L3: resolve the restored raster to its D3D9 texture (tagged-pointer ext
	// block — same formula as pedStage0Resync/texalpha.cpp) and pin it raw so
	// a cache no-op cannot leave the device on SMAA's raster.
	IDirect3DTexture9 *tex = NULL;
	if(saved){
		DWORD extOff = *(DWORD*)0xB4E9E0;
		if(extOff > 0 && extOff < 0x100000){
			IDirect3DTexture9 *cand = *(IDirect3DTexture9**)((uint8*)saved + extOff);
			if(cand && (uintptr_t)cand >= 0x10000u && (((uintptr_t)cand & 3u) == 0))
				tex = cand;
		}
		if(tex)
			dev->SetTexture(0, tex);
	}
	dev->SetSamplerState(0, D3DSAMP_ADDRESSU, s_smaaS0AddrU);
	dev->SetSamplerState(0, D3DSAMP_ADDRESSV, s_smaaS0AddrV);
	dev->SetSamplerState(0, D3DSAMP_MINFILTER, s_smaaS0MinFilter);
	dev->SetSamplerState(0, D3DSAMP_MAGFILTER, s_smaaS0MagFilter);
	if(saved && dbglog_throttle("smaa_stage0"))
		dbglog("[SMAA] stage-0 restore: raster=%p devTex=%p", s_smaaStage0Raster, (void*)tex);
}

// Common fail-open bail for guarded DrawSMAA faults: put the frame back on
// the camera draw buffer (a fault mid-pass can leave an SMAA raster bound as
// RT0), restore viewport, clear the hoisted guard + phase tag, latch SMAA
// broken (fail-open until resolution change resets it), restore the game's
// immediate-mode render states.
// POD-only, no SEH here — safe to call from DrawSMAA (no __try in it).
static void smaaGuardBail(IDirect3DDevice9 *dev, RwRaster *drawBuffer, const D3DVIEWPORT9 *vp)
{
	// Restore the camera frame raster as RT0. Do NOT use RwD3D9SetRenderTarget
	// here: the game's frame raster is rwRASTERTYPECAMERA, which that entry
	// point dereferences as a CAMERATEXTURE (null parent -> 0xC0000005 at
	// 0x7F9ECB). End/BeginUpdate re-binds it through the camera driver path,
	// exactly like UpdateFrontBuffer.
	if(Scene.camera){
		if(drawBuffer && RwCameraGetRaster(Scene.camera) != drawBuffer)
			dbglog("[GUARD] smaaGuardBail: camera raster changed during SMAA");
		RwCameraEndUpdate(Scene.camera);
		RwCameraBeginUpdate(Scene.camera);
	}
	if(dev && vp)
		dev->SetViewport(vp);
	g_inGuardedIm2DPass = 0;
	g_renderPhase = "";
	s_smaaBroken = true;
	// Edge-detect raw-binds s2=g_ssaoDepthTex / s3=g_velocityTex and pass1
	// binds s1/s2 (area/search) — mirror the success-path unbind so a fault
	// mid-pass doesn't leave an INTZ/velocity texture on a stage while
	// DepthHook still re-binds it as the depth-stencil (feedback lock).
	if(dev){
		dev->SetTexture(1, NULL);
		dev->SetTexture(2, NULL);
		dev->SetTexture(3, NULL);
		// Bail paths return BEFORE the final cleanup block, so any
		// POINT/CLAMP set mid-chain (Pass1 s1/s2, Pass3 s2/s3/s4) would
		// leak into the scene render. Restore here — every DrawSMAA bail
		// funnels through this function, so one call covers them all.
		smaaRestoreSamplerStates(dev);
	}
	CPostEffects::ImmediateModeRenderStatesReStore();
	// AFTER ReStore: that call re-pushes the RW cache (which holds SMAA's
	// stage-0 raster), so the stage-0 restore must run last to win.
	smaaRestoreStage0(dev);
	smaaRestoreRawGeom();
}

// SEH-guarded RwTextureCreate for SMAA RT-backed textures — wrapping a
// CAMERATEXTURE raster without a live D3D9 surface can fault inside the
// driver's texture-create path. POD-only body (C2712-safe).
// *faulted distinguishes a caught SEH fault (caller must bail) from a plain
// NULL return (caller just skips the bind, as before).
static RwTexture *smaaTexCreate(RwRaster *ras, const char *phase, bool *faulted)
{
	*faulted = false;
	if(!ras)
		return NULL;
	const char *ph = phase ? phase : "?";
	RwTexture *tex = NULL;
	LONG outerGuard = g_inGuardedIm2DPass;
	InterlockedIncrement(&g_guardDepth);
	g_inGuardedIm2DPass = 1;
	__try {
		tex = RwTextureCreate(ras);
	} __except(EXCEPTION_EXECUTE_HANDLER){
		dbglog("[GUARD] smaaTexCreate: FAULT phase=%s code=0x%08X raster=%p",
			ph, GetExceptionCode(), ras);
		tex = NULL;
		*faulted = true;
	}
	g_inGuardedIm2DPass = outerGuard;
	InterlockedDecrement(&g_guardDepth);
	return tex;
}

void
CPostEffects::DrawSMAA(void)
{
	IDirect3DDevice9 *dev = d3d9device;

	// Guard: SMAA disabled or shaders not loaded
	if(!config->smaaEnable || !SMAA_Edge || !SMAA_BlendWeight || !SMAA_BlendNeighbor){
		// When SMAA is disabled, release resources so they're recreated cleanly on re-enable
		if(g_smaaRtWidth != 0 || g_smaaRtHeight != 0){
			ReleaseSMAAStaticResources();
			dbglog("[SMAA] Disabled — released resources (Edge=%p BlendW=%p BlendN=%p)",
				SMAA_Edge, SMAA_BlendWeight, SMAA_BlendNeighbor);
		}
		return;
	}

	// Guard: front buffer not ready
	if(pRasterFrontBuffer == NULL){
		if(dbglog_throttle("smaa_skip"))
			dbglog("[SMAA] SKIP: pRasterFrontBuffer=NULL");
		return;
	}

	// Guard: device state invalid
	if(!CheckDeviceState()){
		if(dbglog_throttle("smaa_skip"))
			dbglog("[SMAA] SKIP: CheckDeviceState failed");
		return;
	}

	// Guard: D3D9 device not available
	if(!dev){
		if(dbglog_throttle("smaa_skip"))
			dbglog("[SMAA] SKIP: d3d9device=NULL");
		return;
	}

	// All intermediates + metrics live on the camera raster's texel grid.
	// Size SMAA rasters to the camera raster, not pRasterFrontBuffer (which
	// is 2048x2048 while camera is 1920x1080). Using pRasterFrontBuffer size
	// wastes memory+bandwidth and the extra pixels are never sampled (viewports
	// are set to camera dimensions in the SMAA passes).
	RwRaster *camRasForSize = RwCameraGetRaster(Scene.camera);
	if(!camRasForSize){ if(dbglog_throttle("smaa_skip")) dbglog("[SMAA] SKIP: camRas=NULL"); return; }
	int w = camRasForSize->width;
	int h = camRasForSize->height;
	if(w < 8 || h < 8){ if(dbglog_throttle("smaa_skip")) dbglog("[SMAA] SKIP: dims too small %dx%d", w, h); return; }

	// Guard: skip first 2 frames to let the game's entity pool initialize.
	// SMAA Pass2 calls RwIm2DRenderIndexedPrimitive which triggers the game's
	// text/entity rendering pipeline. On frame 1-2, the entity pool at [0xB4E9E0]
	// has slots with NULL data pointers, causing a crash at 0x7F9ECB.
	static int smaaFrameCount = 0;
	smaaFrameCount++;
	if(smaaFrameCount <= 2){
		dbglog("[SMAA] SKIP: frame %d, letting game init entity pool", smaaFrameCount);
		return;
	}

	// --- First-frame / resolution-change detailed log ---
	// NOTE: resolution-change check MUST come before the s_smaaBroken check
	// because resolution change resets the broken flag, allowing retry after
	// device reset (alt-tab).
	static bool smaaFirstFrame = true;
	bool resolutionChanged = (g_smaaRtWidth != w || g_smaaRtHeight != h);

	if(smaaFirstFrame || resolutionChanged){
		static const float thresholds[] = { 0.15f, 0.1f, 0.1f, 0.05f };
		static const float maxSearchSteps[] = { 4.0f, 8.0f, 16.0f, 32.0f };
		// SMAA internal constants: HIGH preset, temporal always on
		static const int kSmaaPreset = 2; // HIGH
		int preset = kSmaaPreset;
		dbglog("[SMAA-DIAG] === INIT/FIRST-FRAME ===");
		dbglog("[SMAA-DIAG] pRasterFrontBuffer=%p hdr=%dx%d depth=%d",
			pRasterFrontBuffer, pRasterFrontBuffer->width, pRasterFrontBuffer->height,
			pRasterFrontBuffer->depth);
		RwRaster *camRas = RwCameraGetRaster(Scene.camera);
		dbglog("[SMAA-DIAG] camRaster=%p %dx%d", camRas,
			camRas ? RwRasterGetWidth(camRas) : 0, camRas ? RwRasterGetHeight(camRas) : 0);
		dbglog("[SMAA-DIAG] Edge=%p EdgeMotionDepth=%p BlendW=%p BlendN=%p Temporal=%p",
			SMAA_Edge, SMAA_EdgeMotionDepth, SMAA_BlendWeight, SMAA_BlendNeighbor, SMAA_Temporal);
		dbglog("[SMAA-DIAG] preset=%d thresh=%.3f searchSteps=%.0f temporal=1",
			preset, thresholds[preset], maxSearchSteps[preset]);
		// NOTE: velocityTex/prevFrame are deliberately NOT logged here —
		// this block runs BEFORE raster creation and before DrawVelocity-
		// Buffer's first unpaused frame, so both printed NULL even though
		// the wiring is correct (log dig 2026-10-17: menu-window artifact,
		// read as "velocity never reaches Pass3"). Real handles are logged
		// post-create below and at the Pass3 bind (smaa_vel tag).
		smaaFirstFrame = false;
	}

	// [SMAA-DIAG] rtsz — H2 size-identity instrumentation (read-only queries).
	// pRasterFrontBuffer's reported size disagrees between log paths
	// (2048x2048 from UpdateFrontBuffer vs 1920x1080 depth=16 here). Resolve
	// the ACTUAL D3D9 surface sizes (RT0 + depth stencil via GetDesc) and the
	// live viewport so a stale small viewport (luma 8x8 / envmap 256x256) is
	// visible when the black-frame + thin-top-sliver artifact occurs.
	// Window: first N frames after SMAA start, re-armed on resolution change
	// (alt-tab/reset). First occurrence logs unconditionally (init-style);
	// later frames in the window go through the 2s dbglog_throttle("SMAA-DIAG").
	// GetRenderTarget/GetDepthStencilSurface AddRef — both Released below.
	{
		static int rtszFrames = 0;
		if(resolutionChanged)
			rtszFrames = 0;
		if(rtszFrames < 8){
			rtszFrames++;
			if(rtszFrames == 1 || dbglog_throttle("SMAA-DIAG")){
				IDirect3DSurface9 *rtszRT0 = NULL;
				IDirect3DSurface9 *rtszDS = NULL;
				D3DVIEWPORT9 rtszVP = {};
				HRESULT hrRT = dev->GetRenderTarget(0, &rtszRT0);
				HRESULT hrDS = dev->GetDepthStencilSurface(&rtszDS);
				bool vpOk = SUCCEEDED(dev->GetViewport(&rtszVP));
				D3DSURFACE_DESC rtDesc = {};
				D3DSURFACE_DESC dsDesc = {};
				bool rtOk = SUCCEEDED(hrRT) && rtszRT0 && SUCCEEDED(rtszRT0->GetDesc(&rtDesc));
				bool dsOk = SUCCEEDED(hrDS) && rtszDS && SUCCEEDED(rtszDS->GetDesc(&dsDesc));
				dbglog("[SMAA-DIAG] rtsz f%d: RT0=%lux%lu fmt=%d (hr=0x%08X) DS=%lux%lu fmt=%d (hr=0x%08X) "
				       "vp=%lux%lu @(%lu,%lu) minz=%.3f | pRFB hdr=%dx%d depth=%d camRas hdr=%dx%d",
					rtszFrames,
					rtOk ? rtDesc.Width : 0, rtOk ? rtDesc.Height : 0,
					rtOk ? rtDesc.Format : 0, hrRT,
					dsOk ? dsDesc.Width : 0, dsOk ? dsDesc.Height : 0,
					dsOk ? dsDesc.Format : 0, hrDS,
					vpOk ? rtszVP.Width : 0, vpOk ? rtszVP.Height : 0,
					vpOk ? rtszVP.X : 0, vpOk ? rtszVP.Y : 0,
					vpOk ? rtszVP.MinZ : 0.0f,
					pRasterFrontBuffer ? pRasterFrontBuffer->width : 0,
					pRasterFrontBuffer ? pRasterFrontBuffer->height : 0,
					pRasterFrontBuffer ? pRasterFrontBuffer->depth : 0,
					camRasForSize ? camRasForSize->width : 0,
					camRasForSize ? camRasForSize->height : 0);
				if(rtszRT0) rtszRT0->Release();
				if(rtszDS)  rtszDS->Release();
			}
		}
	}

	// Recreate rasters if resolution changed (must happen before broken check
	// so resolution change can reset the broken flag and retry)
	if(resolutionChanged){
		dbglog("[SMAA-DIAG] RESOLUTION CHANGE %dx%d -> %dx%d (destroying all rasters)",
			g_smaaRtWidth, g_smaaRtHeight, w, h);
		if(g_smaaEdgeRaster){ UntrackRaster(g_smaaEdgeRaster); RwRasterDestroy(g_smaaEdgeRaster); g_smaaEdgeRaster = NULL; }
		if(g_smaaBlendRaster){ UntrackRaster(g_smaaBlendRaster); RwRasterDestroy(g_smaaBlendRaster); g_smaaBlendRaster = NULL; }
		if(g_smaaPrevFrameRaster){ UntrackRaster(g_smaaPrevFrameRaster); RwRasterDestroy(g_smaaPrevFrameRaster); g_smaaPrevFrameRaster = NULL; }
		if(g_smaaSceneRaster){ UntrackRaster(g_smaaSceneRaster); RwRasterDestroy(g_smaaSceneRaster); g_smaaSceneRaster = NULL; }
		g_smaaRtWidth = w; g_smaaRtHeight = h;
		if(g_smaaBlendTexRW){ RwTextureDestroy(g_smaaBlendTexRW); g_smaaBlendTexRW = NULL; }
		if(g_smaaPrevFrameTexRW){ RwTextureDestroy(g_smaaPrevFrameTexRW); g_smaaPrevFrameTexRW = NULL; }
		s_smaaRastersInitialized = false;
		s_smaaPendingInit = false;
		s_smaaBroken = false;
		// All rasters (history included) destroyed above — the replacement
		// prevFrameRaster holds undefined D3DPOOL_DEFAULT content until the
		// pass-3 history clear runs. Invalidate so pass 3 blends nothing in.
		smaaInvalidateHistory("resolution change");
	}

	// Check for permanent broken state (init failed previously).
	// Resolution-change reset above clears this, allowing retry on alt-tab.
	if(s_smaaBroken){
		if(dbglog_throttle("smaa_broken"))
			dbglog("[SMAA] SKIP: permanently broken (init failed), will not retry until resolution change");
		return;
	}

	// Create RW camera texture rasters at CAMERA size (w x h = camRas dims).
	// SMAA sampling doctrine (see g_smaaSceneRaster comment): all SMAA
	// rasters are camera-sized with content filling UV 0..1, drawn with
	// s_ffQuad (UV 0..1, positions 0..w x 0..h). The scene input is a
	// camera-sized copy of the front buffer's valid sub-rect (the FB itself
	// is padded 2048x2048 and only correct under colorfilterVerts' 0..2048
	// positions — under a UV 0..1 quad it would sample stretched).
	RwRaster *camRasForDepth = RwCameraGetRaster(Scene.camera);
	int camDepth = camRasForDepth ? camRasForDepth->depth : 32;
	bool smaaCreatedThisFrame = false;
	if(!g_smaaEdgeRaster){
		g_smaaEdgeRaster = RwRasterCreate(w, h, camDepth, rwRASTERTYPECAMERATEXTURE);
		if(!g_smaaEdgeRaster){ dbglog("[SMAA-DIAG] FATAL: edgeRaster create failed %dx%d", w, h); return; }
		dbglog("[SMAA-DIAG] Created edgeRaster=%p %dx%d (cam size)", g_smaaEdgeRaster, w, h);
		smaaCreatedThisFrame = true;
	}
	if(!g_smaaBlendRaster){
		g_smaaBlendRaster = RwRasterCreate(w, h, camDepth, rwRASTERTYPECAMERATEXTURE);
		if(!g_smaaBlendRaster){ dbglog("[SMAA-DIAG] FATAL: blendRaster create failed %dx%d", w, h); return; }
		dbglog("[SMAA-DIAG] Created blendRaster=%p %dx%d (cam size)", g_smaaBlendRaster, w, h);
		smaaCreatedThisFrame = true;
	}
	if(!g_smaaPrevFrameRaster){
		g_smaaPrevFrameRaster = RwRasterCreate(w, h, camDepth, rwRASTERTYPECAMERATEXTURE);
		if(!g_smaaPrevFrameRaster){ dbglog("[SMAA-DIAG] FATAL: prevFrameRaster create failed %dx%d", w, h); return; }
		dbglog("[SMAA-DIAG] Created prevFrameRaster=%p %dx%d (cam size)", g_smaaPrevFrameRaster, w, h);
		smaaCreatedThisFrame = true;
	}
	if(!g_smaaSceneRaster){
		g_smaaSceneRaster = RwRasterCreate(w, h, camDepth, rwRASTERTYPECAMERATEXTURE);
		if(!g_smaaSceneRaster){ dbglog("[SMAA-DIAG] FATAL: sceneRaster create failed %dx%d", w, h); return; }
		dbglog("[SMAA-DIAG] Created sceneRaster=%p %dx%d (cam size)", g_smaaSceneRaster, w, h);
		smaaCreatedThisFrame = true;
	}
	// Post-create handle diag (log-dig breadcrumb): the INIT/FIRST-FRAME
	// block above runs BEFORE these creations, so its velocityTex/prevFrame
	// print was always NULL on the first frame. Log the real handles once
	// the rasters exist so the next log dig can verify the wiring directly.
	// velocityTex NOTE (log dig 2026-10-17): g_velocityTex is lazy-created
	// inside DrawVelocityBuffer, which only executes on the first frame that
	// passes the menu/pause/cutscene gate — so velocityTex=00000000 here is
	// EXPECTED whenever SMAA's first frame precedes the first unpaused frame
	// (the "texture created" + "VP latched" pair in DrawVelocityBuffer is
	// the creation proof, not a failure).
	if(smaaCreatedThisFrame)
		dbglog("[SMAA-DIAG] rasters ready: edge=%p blend=%p prevFrame=%p scene=%p velocityTex=%p",
			g_smaaEdgeRaster, g_smaaBlendRaster, g_smaaPrevFrameRaster,
			g_smaaSceneRaster, g_velocityTex);

	// Self-check (bug 5): every SMAA raster must be camera-sized. A stale
	// raster — resolution change that destroyed/recreated only some of them,
	// a device Reset that dropped one, or a partial recreate after alt-tab —
	// makes UV 0..1 address the wrong texel grid: edge/blend/history then
	// sample a sub-rect (or a stretched copy) and the artifact shows as
	// residual edging/ghosting. Fail CLOSED: latch s_smaaBroken so SMAA
	// self-skips until the next resolution change instead of drawing a
	// wrong image.
	{
		struct { RwRaster *r; const char *n; } chk[4] = {
			{ g_smaaEdgeRaster,    "edge"    },
			{ g_smaaBlendRaster,   "blend"   },
			{ g_smaaPrevFrameRaster,"prevFrame" },
			{ g_smaaSceneRaster,   "scene"   },
		};
		for(int i = 0; i < 4; i++){
			RwRaster *r = chk[i].r;
			if(r && (r->width != w || r->height != h)){
				dbglog("[SMAA] SELF-SKIP: %s raster %dx%d != camera %dx%d (stale, latching broken until resolution change)",
					chk[i].n, r->width, r->height, w, h);
				s_smaaBroken = true;
				return;
			}
		}
	}

	// Deferred force-init of the CAMERATEXTURE rasters' D3D9 surfaces.
	// Log-dig root cause (2026-10-17): the pending flag was ONLY consumed by
	// SMAATryInitRasters() from RenderScene_before — which does not run in
	// menus/loading screens — while DrawSMAA (via DrawFinalEffects) DOES run
	// there. Result: "rasters pending init" stalled ~8.4s (43.49s->51.99s in
	// the 02:39 session) until gameplay rendering started. Fix: run the
	// force-init inline, but still OUTSIDE the main camera's BeginUpdate
	// (EndUpdate -> scratch-camera init -> BeginUpdate — same idiom as the
	// scene-copy block below), so the RW 3.6 "no RT work while a camera
	// update is active" doctrine is kept. RenderScene_before remains the
	// fallback consumer for any frame where the inline attempt fails.
	// Bail out this frame only if the inline init did not complete.
	if(!s_smaaRastersInitialized && g_smaaEdgeRaster && g_smaaBlendRaster &&
	   g_smaaPrevFrameRaster && g_smaaSceneRaster){
		s_smaaPendingInit = true;
		// CheckDeviceState above guarantees Scene.camera is valid and inside
		// BeginUpdate at this point (the scene-copy block relies on the same
		// invariant when it calls RwCameraEndUpdate unconditionally).
		RwCameraEndUpdate(Scene.camera);
		SMAATryInitRasters();
		RwCameraBeginUpdate(Scene.camera);
		if(!s_smaaRastersInitialized){
			// Inline init failed (SMAATryInitRasters latched s_smaaBroken or
			// left pending set) — skip; RenderScene_before retries next frame.
			if(dbglog_throttle("smaa_pending"))
				dbglog("[SMAA] SKIP: rasters pending init, will run next frame");
			return;
		}
	} else if(!s_smaaRastersInitialized) {
		// Rasters not created yet (shouldn't happen, but be safe)
		if(dbglog_throttle("smaa_skip"))
			dbglog("[SMAA] SKIP: rasters not created yet");
		return;
	}

	// Verify all rasters have valid D3D9 surfaces before proceeding
	if(!RasterEnsureSurfaceReady(g_smaaEdgeRaster, "SMAA") ||
	   !RasterEnsureSurfaceReady(g_smaaBlendRaster, "SMAA") ||
	   !RasterEnsureSurfaceReady(g_smaaPrevFrameRaster, "SMAA") ||
	   !RasterEnsureSurfaceReady(g_smaaSceneRaster, "SMAA")){
		if(dbglog_throttle("smaa_nosurf"))
			dbglog("[SMAA] SKIP: rasters not surface-ready, will retry next frame");
		return;
	}

	// Refresh the camera-sized scene copy from the front buffer's VALID
	// sub-rect. UpdateFrontBuffer just synced camRas -> FB (DrawFinalEffects
	// :3066), so the camera draw buffer holds the identical final graded
	// frame at 1:1 — copy from it (same-size blit, no padded-region math).
	// Same EndUpdate/PushContext/RenderFast/Pop/BeginUpdate idiom as
	// DrawIVGrade's frame copy. Fail-open: skip SMAA this frame on failure.
	{
		RwRaster *sceneSrc = RwCameraGetRaster(Scene.camera);
		if(!sceneSrc){
			if(dbglog_throttle("smaa_skip")) dbglog("[SMAA] SKIP: scene copy src NULL");
			return;
		}
		RwCameraEndUpdate(Scene.camera);
		RwRasterPushContext(g_smaaSceneRaster);
		RwRaster *copyResult = RwRasterRenderFast(sceneSrc, 0, 0);
		RwRasterPopContext();
		RwCameraBeginUpdate(Scene.camera);
		if(!copyResult){
			dbglog("[SMAA] scene copy failed (RenderFast NULL), skipping SMAA this frame");
			return;
		}
	}

	// UV 0..1 fullscreen quad on the camera texel grid — ALL SMAA draws use
	// this instead of colorfilterVerts (hardcoded 2048x2048 quad whose
	// visible window spans only UV 0..0.9375 x 0..0.5273, correct for the
	// padded FB but wrong for every camera-sized SMAA raster).
	SetupFullscreenQuad((float)w, (float)h);

	// Save/restore D3D9 state around all SMAA passes + texture creation.
	// Must be BEFORE area/search tex generation — those leave D3D9 state dirty,
	// and we need Restore to capture the original game state, not the post-creation state.
	// Raw geom states first: the rw-only Store/Set below cannot capture (or
	// later undo) the raw D3DRS writes at the pass0 block (ALPHATEST/CULL/
	// ZENABLE/ZWRITE off) — every other postfx pass pairs them like this.
	s_smaaRawGeomSaved = SaveRawGeomStates(s_smaaRawGeom);
	ImmediateModeRenderStatesStore();
	ImmediateModeRenderStatesSet();

	// Create D3D textures for area/search lookup
	if(!g_smaaAreaTex){
		if(dev){
			extern void GenerateSMAAAreaTex(IDirect3DDevice9*, IDirect3DTexture9**);
			extern void GenerateSMAASearchTex(IDirect3DDevice9*, IDirect3DTexture9**);
			GenerateSMAAAreaTex(dev, &g_smaaAreaTex);
			GenerateSMAASearchTex(dev, &g_smaaSearchTex);
			dbglog("[SMAA-DIAG] Generated areaTex=%p searchTex=%p", g_smaaAreaTex, g_smaaSearchTex);
		}
	}

	// Camera movement tracking for temporal stabilization
	static CVector prevCamPos = {0, 0, 0};
	static RwMatrix prevCamMatrix = {0};
	static bool camInitialized = false;

	RwMatrix *camMatrix = NULL;
	RwFrame *camFrame = Scene.camera ? RwCameraGetFrame(Scene.camera) : NULL;
	if(camFrame)
		camMatrix = RwFrameGetLTM(camFrame);
	
	CVector camPos = {0, 0, 0};
	if(camMatrix)
		camPos = {camMatrix->pos.x, camMatrix->pos.y, camMatrix->pos.z};

	float cameraVelocity = 0.0f;
	float cameraRotation = 0.0f;

	if(camInitialized && camMatrix){
		float dx = camPos.x - prevCamPos.x;
		float dy = camPos.y - prevCamPos.y;
		float dz = camPos.z - prevCamPos.z;
		cameraVelocity = sqrtf(dx*dx + dy*dy + dz*dz);

		// Normalize both forward vectors so the dot is a true cosine (the
		// rotation/acosf metric). Guard the reciprocal length against zero with the
		// project pattern max(x, 1e-7f): a degenerate 'at' vector would divide to
		// NaN, which SM3.0 renders as black. CPU-side math, keeps ps_3_0 legal.
		float curLen = sqrtf(camMatrix->at.x * camMatrix->at.x +
		                     camMatrix->at.y * camMatrix->at.y +
		                     camMatrix->at.z * camMatrix->at.z);
		float prvLen = sqrtf(prevCamMatrix.at.x * prevCamMatrix.at.x +
		                     prevCamMatrix.at.y * prevCamMatrix.at.y +
		                     prevCamMatrix.at.z * prevCamMatrix.at.z);
		float invCur = 1.0f / max(curLen, 1e-7f);
		float invPrv = 1.0f / max(prvLen, 1e-7f);
		float dot = (camMatrix->at.x * invCur) * (prevCamMatrix.at.x * invPrv) +
		            (camMatrix->at.y * invCur) * (prevCamMatrix.at.y * invPrv) +
		            (camMatrix->at.z * invCur) * (prevCamMatrix.at.z * invPrv);
		cameraRotation = 1.0f - max(-1.0f, min(1.0f, dot));
	}

	if(camMatrix){
		prevCamPos = camPos;
		prevCamMatrix = *camMatrix;
		camInitialized = true;
	}

	float cameraMovement = min(1.0f, (cameraVelocity * 0.1f) + (cameraRotation * 2.0f));

	// SMAA preset parameters — internal constants (HIGH preset)
	static const float thresholds[] = { 0.15f, 0.1f, 0.1f, 0.05f };
	static const float maxSearchSteps[] = { 4.0f, 8.0f, 16.0f, 32.0f };
	static const int kSmaaPreset = 2; // HIGH
	float smaaThreshold = thresholds[kSmaaPreset];
	float smaaSearchSteps = maxSearchSteps[kSmaaPreset];
	float screenParams[4] = { (float)w, (float)h, 1.0f/max((float)w, 1e-7f), 1.0f/max((float)h, 1e-7f) };

	// Save original camera raster (the draw buffer — back buffer)
	RwRaster *drawBuffer = RwCameraGetRaster(Scene.camera);
	int camW = drawBuffer ? RwRasterGetWidth(drawBuffer) : w;
	int camH = drawBuffer ? RwRasterGetHeight(drawBuffer) : h;

	// Hoisted guard: cover the Pass0-3 setup windows (RwTextureCreate, RT
	// switches) that run outside the SEH helpers. Helpers save/restore this
	// flag so the hoist survives across passes. Every return below clears it.
	g_inGuardedIm2DPass = 1;

	// Log pre-pass D3D9 state
	IDirect3DSurface9 *rt0 = NULL;
	dev->GetRenderTarget(0, &rt0);
	if(config->profilerEnable && dbglog_throttle("smaaPre"))
		dbglog("[SMAA-DIAG] PRE-PASS: drawBuf=%p(%dx%d) pRFB=%p(%dx%d) RT0=%p cam=%p",
			drawBuffer, drawBuffer ? drawBuffer->width : 0, drawBuffer ? drawBuffer->height : 0,
			pRasterFrontBuffer, w, h, rt0, Scene.camera);
	if(rt0) rt0->Release();

	// ---- Pass 0: Edge + Motion + Depth Detection ----
	if(config->profilerEnable && dbglog_throttle("smaaP0s"))
		dbglog("[SMAA-DIAG] Pass0-START: cam=%p camFrame=%p drawBuf=%p",
			Scene.camera,
			Scene.camera ? RwCameraGetFrame(Scene.camera) : NULL,
			drawBuffer);

	// RW-native RT switching — no camera Begin/EndUpdate, no raw surface handles.
	if(!g_smaaEdgeRaster || !g_smaaBlendRaster || !g_smaaPrevFrameRaster){
		dbglog("[SMAA-DIAG] FATAL: intermediate rasters NULL (edge=%p blend=%p prev=%p)",
			g_smaaEdgeRaster, g_smaaBlendRaster, g_smaaPrevFrameRaster);
		g_inGuardedIm2DPass = 0;
		g_renderPhase = "";
		ImmediateModeRenderStatesReStore();
		smaaRestoreRawGeom();
		return;
	}

	// Intermediates are camera-sized; keep rasterization on the camera
	// viewport so the quad writes the same texel region its UVs address
	// (s_ffQuad: positions 0..w x 0..h = UV 0..1 on a w x h raster).
	D3DVIEWPORT9 vpSaved;
	dev->GetViewport(&vpSaved);
	D3DVIEWPORT9 vpCam = { 0, 0, (DWORD)camW, (DWORD)camH, 0.0f, 1.0f };

	g_renderPhase = "smaa_p0_rt";
	if(!guardedSetRT(g_smaaEdgeRaster, "smaa_p0_rt")){
		// Guarded fault (or NULL raster) — fail open this frame, latch broken.
		smaaGuardBail(dev, drawBuffer, &vpSaved);
		return;
	}
	dev->SetViewport(&vpCam);
	dev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0,0,0,0), 0.0f, 0);

	// Render states for fullscreen passes — matches SSAO/SSS pattern
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	RwD3D9SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	RwD3D9SetRenderState(D3DRS_ZENABLE, FALSE);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	IDirect3DSurface9 *p0rt = NULL;
	dev->GetRenderTarget(0, &p0rt);
	if(config->profilerEnable && dbglog_throttle("smaaP0"))
		dbglog("[SMAA-DIAG] Pass0: edge=%p RT0=%p shader=%p (EdgeMotionDepth=%d)",
			g_smaaEdgeRaster, p0rt, SMAA_EdgeMotionDepth ? SMAA_EdgeMotionDepth : SMAA_Edge,
			SMAA_EdgeMotionDepth != NULL);
	if(p0rt) p0rt->Release();

	// SEV1 — capture the incoming stage-0 binding BEFORE the first SMAA bind
	// below. Every exit from here on (smaaGuardBail bails + the normal tail)
	// must put it back: ImmediateModeRenderStatesReStore does not cover
	// rwRENDERSTATETEXTURERASTER. Pre-seed NULL / WRAP+LINEAR; ignore failed
	// Gets (they leave the pre-seeded value). POD locals only.
	{
		void *saved0 = NULL;
		RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &saved0);
		if(saved0 == (void*)0xFFFFFFFFu)
			saved0 = NULL; // failed-Get sentinel (pedStage0Resync idiom)
		s_smaaStage0Raster = saved0;
		dev->GetSamplerState(0, D3DSAMP_ADDRESSU, &s_smaaS0AddrU);
		dev->GetSamplerState(0, D3DSAMP_ADDRESSV, &s_smaaS0AddrV);
		dev->GetSamplerState(0, D3DSAMP_MINFILTER, &s_smaaS0MinFilter);
		dev->GetSamplerState(0, D3DSAMP_MAGFILTER, &s_smaaS0MagFilter);
		s_smaaStage0Saved = true;
	}

	// Set camera-sized scene copy as input texture on stage 0 (UV 0..1 =
	// full valid frame; the padded FB would sample stretched under s_ffQuad)
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)g_smaaSceneRaster);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSU, (void*)rwTEXTUREADDRESSCLAMP);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSV, (void*)rwTEXTUREADDRESSCLAMP);

	// Bind previous frame for motion detection on stage 1
	if(g_smaaPrevFrameRaster){
		if(!g_smaaPrevFrameTexRW){
			g_renderPhase = "smaa_texcreate";
			bool texFaulted = false;
			g_smaaPrevFrameTexRW = smaaTexCreate(g_smaaPrevFrameRaster, "smaa_texcreate", &texFaulted);
			if(texFaulted){
				smaaGuardBail(dev, drawBuffer, &vpSaved);
				return;
			}
			if(g_smaaPrevFrameTexRW){
				RwTextureSetFilterMode(g_smaaPrevFrameTexRW, rwFILTERLINEAR);
				RwTextureSetAddressingU(g_smaaPrevFrameTexRW, rwTEXTUREADDRESSCLAMP);
				RwTextureSetAddressingV(g_smaaPrevFrameTexRW, rwTEXTUREADDRESSCLAMP);
			}
		}
		if(g_smaaPrevFrameTexRW)
			RwD3D9SetTexture(g_smaaPrevFrameTexRW, 1);
	}
	// Edge-detect pass — draw goes through the guardedIm2DRender choke-point
	// (C2712: DrawSMAA can't take __try). Suspend/Restore are inside the helper.
	g_renderPhase = "smaa_edge";
	if(!DrawSMAA_EdgeDetect(smaaThreshold, cameraMovement, screenParams, s_ffQuad)){
		// Edge detect faulted — depth hook already restored inside helper.
		// Skip remaining passes; fail open + latch broken.
		smaaGuardBail(dev, drawBuffer, &vpSaved);
		return;
	}

	// ---- Pass 1: Blend Weight Calculation ----
	g_renderPhase = "smaa_p1_rt";
	if(!guardedSetRT(g_smaaBlendRaster, "smaa_p1_rt")){
		smaaGuardBail(dev, drawBuffer, &vpSaved);
		return;
	}
	dev->SetViewport(&vpCam);
	dev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0,0,0,0), 0.0f, 0);

	IDirect3DSurface9 *p1rt = NULL;
	dev->GetRenderTarget(0, &p1rt);
	if(config->profilerEnable && dbglog_throttle("smaaP1"))
		dbglog("[SMAA-DIAG] Pass1: blend=%p RT0=%p shader=%p searchSteps=%.0f",
			g_smaaBlendRaster, p1rt, SMAA_BlendWeight, smaaSearchSteps);
	if(p1rt) p1rt->Release();

	// Bind edge raster as input texture on stage 0
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)g_smaaEdgeRaster);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSU, (void*)rwTEXTUREADDRESSCLAMP);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSV, (void*)rwTEXTUREADDRESSCLAMP);

	// Bind area/search textures on stages 1 and 2
	if(dev){
		if(g_smaaAreaTex){
			dev->SetTexture(1, g_smaaAreaTex);
			dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
			dev->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		}
		if(g_smaaSearchTex){
			dev->SetTexture(2, g_smaaSearchTex);
			dev->SetSamplerState(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(2, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
			dev->SetSamplerState(2, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		}
	}

	// Set blend weight shader constants
	float blendP[4] = {0.0f, smaaSearchSteps, 0.0f, 0.0f};
	RwD3D9SetPixelShaderConstant(0, blendP, 1);
	RwD3D9SetPixelShaderConstant(1, screenParams, 1);
	g_renderPhase = "smaa_draw";
	if(!SMAA_DrawPass((IDirect3DPixelShader9*)SMAA_BlendWeight, s_ffQuad)){
		smaaGuardBail(dev, drawBuffer, &vpSaved);
		return;
	}

	// Clean up texture stages after Pass 1
	if(dev){
		dev->SetTexture(1, NULL);
		dev->SetTexture(2, NULL);
	}

	// Pass2-entry guard (lane-A hardening): g_smaaSceneRaster is pass2's ONLY
	// colour input (bound at the "Bind camera-sized scene copy" line below).
	// If it went NULL or stale-sized since the entry self-check, the
	// neighbourhood blend renders NOTHING into edgeRaster and Pass3 (or the
	// no-temporal fallback presenting edgeRaster) would show a stale
	// breadcrumb frame. Fail CLOSED with the same latch as the entry
	// self-check: smaaGuardBail rebinds the camera RT, restores state and
	// sets s_smaaBroken — a stale edgeRaster can NEVER be presented; SMAA
	// self-skips until the next resolution change.
	if(!g_smaaSceneRaster ||
	   g_smaaSceneRaster->width != w || g_smaaSceneRaster->height != h){
		dbglog("[SMAA] PASS2 GUARD: scene=%p (%dx%d) != camera %dx%d — latching broken, edgeRaster NOT presented",
			(void*)g_smaaSceneRaster,
			g_smaaSceneRaster ? g_smaaSceneRaster->width : 0,
			g_smaaSceneRaster ? g_smaaSceneRaster->height : 0, w, h);
		smaaGuardBail(dev, drawBuffer, &vpSaved);
		return;
	}

	// ---- Pass 2: Neighborhood Blending → edgeRaster (temp reuse) ----
	// Render to edgeRaster instead of drawBuffer so Pass 3 temporal can read it
	// while writing to drawBuffer — avoids D3D9 read-write conflict on same surface.
	// edgeRaster is done being read after Pass 1, safe to reuse as temp.
	g_renderPhase = "smaa_p2_rt";
	if(!guardedSetRT(g_smaaEdgeRaster, "smaa_p2_rt")){
		smaaGuardBail(dev, drawBuffer, &vpSaved);
		return;
	}
	dev->SetViewport(&vpCam);

	IDirect3DSurface9 *p2rt = NULL;
	dev->GetRenderTarget(0, &p2rt);
	if(config->profilerEnable && dbglog_throttle("smaaP2"))
		dbglog("[SMAA-DIAG] Pass2: edgeRaster=%p RT0=%p shader=%p", g_smaaEdgeRaster, p2rt, SMAA_BlendNeighbor);
	if(p2rt) p2rt->Release();

	// Bind camera-sized scene copy as color input on stage 0 (UV 0..1)
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)g_smaaSceneRaster);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSU, (void*)rwTEXTUREADDRESSCLAMP);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSV, (void*)rwTEXTUREADDRESSCLAMP);

	// Bind blend raster on stage 1 via RwD3D9SetTexture
	if(!g_smaaBlendTexRW && g_smaaBlendRaster){
		g_renderPhase = "smaa_texcreate";
		bool texFaulted = false;
		g_smaaBlendTexRW = smaaTexCreate(g_smaaBlendRaster, "smaa_texcreate", &texFaulted);
		if(texFaulted){
			smaaGuardBail(dev, drawBuffer, &vpSaved);
			return;
		}
		if(g_smaaBlendTexRW){
			RwTextureSetFilterMode(g_smaaBlendTexRW, rwFILTERLINEAR);
			RwTextureSetAddressingU(g_smaaBlendTexRW, rwTEXTUREADDRESSCLAMP);
			RwTextureSetAddressingV(g_smaaBlendTexRW, rwTEXTUREADDRESSCLAMP);
		}
	}
	if(g_smaaBlendTexRW)
		RwD3D9SetTexture(g_smaaBlendTexRW, 1);

	// Set neighborhood blend shader
	RwD3D9SetPixelShaderConstant(1, screenParams, 1);
	g_renderPhase = "smaa_draw";
	if(!SMAA_DrawPass((IDirect3DPixelShader9*)SMAA_BlendNeighbor, s_ffQuad)){
		smaaGuardBail(dev, drawBuffer, &vpSaved);
		return;
	}

	// Cleanup texture stages after Pass 2
	RwD3D9SetTexture(NULL, 1);
	d3d9device->SetTexture(2, NULL);
	d3d9device->SetTexture(3, NULL);
	d3d9device->SetTexture(4, NULL);

	// ---- History invalidation: clear the temporal history raster ----
	// Whenever g_smaaHistoryValid is false (first frame after a raster
	// create / ReleaseSMAAStaticResources / resolution change / device
	// reset) the history raster holds UNDEFINED D3DPOOL_DEFAULT content.
	// Pass 3 runs with blendStrength=1.0 in that state, so the history is
	// ignored anyway — this clear is belt-and-braces: undefined content must
	// never be readable by the resolve, whatever the shader's blend floor
	// does. A clear failure does NOT latch SMAA broken: pass 3 is skipped
	// for this frame and the spatial-only pass-2 result is presented
	// instead (see the else branch below); the clear is retried next frame.
	bool histCleared = false;
	if(!g_smaaHistoryValid && g_smaaPrevFrameRaster){
		if(guardedSetRT(g_smaaPrevFrameRaster, "smaa_hist_clear")){
			dev->SetViewport(&vpCam);
			dev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0,0,0,0), 0.0f, 0);
			histCleared = true;
			dbglog("[SMAA] history raster CLEARED to black (invalid history: first frame "
			       "after init/release/reset) — pass3 resolves with blendStrength=1.0");
		} else {
			dbglog("[SMAA] history clear FAILED (guardedSetRT) — presenting spatial-only "
			       "pass2 result this frame, pass3 retries next frame");
		}
	}

	// ---- Pass 3: Temporal Resolve → drawBuffer (final output) ----
	// Reads edgeRaster (current SMAA result) + prevFrameRaster (history),
	// writes directly to drawBuffer. No extra copy-back blit needed.
	// Pause gate: DrawVelocityBuffer skips paused/menu frames (~:6512) so the
	// velocity buffer freezes, but reprojection against that stale velocity
	// ghosts while paused. Gate the temporal pass off in menus/pause so it falls
	// through to the spatial-only fallback below (unchanged).
	// Only resolve against a history that is either already valid or was
	// explicitly cleared this frame — never one holding undefined content.
	if(SMAA_Temporal && g_smaaPrevFrameRaster && !IsGameInMenuOrPaused() &&
	   (g_smaaHistoryValid || histCleared)){
		g_renderPhase = "smaa_p3_rt";
		// Re-bind the camera frame raster through the camera context (see
		// smaaGuardBail): RwD3D9SetRenderTarget faults on rwRASTERTYPECAMERA.
		if(Scene.camera){
			RwCameraEndUpdate(Scene.camera);
			RwCameraBeginUpdate(Scene.camera);
		}
		// BeginUpdate may have re-applied camera states — re-assert the
		// fullscreen-pass state set from Pass0 before the final draw.
		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		RwD3D9SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		RwD3D9SetRenderState(D3DRS_ZENABLE, FALSE);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		dev->SetViewport(&vpSaved);

		// Bind current SMAA result (edgeRaster) on s0
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)g_smaaEdgeRaster);
		RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSU, (void*)rwTEXTUREADDRESSCLAMP);
		RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSV, (void*)rwTEXTUREADDRESSCLAMP);

		// Bind previous frame on s1
		if(g_smaaPrevFrameTexRW)
			RwD3D9SetTexture(g_smaaPrevFrameTexRW, 1);

		// Bind velocity buffer on s2 for history reprojection (POINT/CLAMP) —
		// ONLY when this frame's motion vectors are trustworthy. There is no
		// dedicated velocity-valid latch in postfx_motionblur.cpp (no
		// g_velocityValid / PostfxVelocityRan): the pass owns only a static
		// one-frame VP latch and writes content whenever it is called. The
		// per-frame proof available cross-file is g_prevDepthValid
		// (postfx.h:144): postfx.cpp runs CopyDepthToPrev immediately after
		// DrawVelocityBuffer under the same velocity gate and clears the latch
		// (postfx.cpp:1552) when that chain is skipped/failed — so a true
		// latch means the depth source the velocity pass needs existed this
		// frame. A stale s2 (pass disabled or skipped) fed garbage motion
		// vectors into the temporal blend = ghosting, so bind NULL instead and
		// let c3.w=0 sample history unshifted.
		bool velValid = true;
		const char *velWhy = "ok";
		if(!config->velocityBufferEnable){ velValid = false; velWhy = "velEnable=0"; }
		else if(!VelocityReconstruct){ velValid = false; velWhy = "shader=NULL"; }
		else if(!g_velocityTex){ velValid = false; velWhy = "tex=NULL"; }
		else if(!g_prevDepthValid){ velValid = false; velWhy = "prevDepthInvalid"; }
		if(velValid){
			dev->SetTexture(2, g_velocityTex);
			dev->SetSamplerState(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(2, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
			dev->SetSamplerState(2, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		} else {
			// Never let the shader sample a foreign s2: D3D9 stage state is
			// persistent, NULL is a defined (zero) read.
			dev->SetTexture(2, NULL);
		}

		// ---- Pass 3 depth/normal inputs (Fix B) ----
		// SMAA_Temporal.hlsl reads depthTex (s3) + normalTex (s4) for
		// disocclusion rejection (inert when unbound). The depth source is
		// g_ssaoDepthTex — the INTZ depth buffer used by SSAO — and INTZ must
		// never sit on a sampler while it is the device DS, so the bind/draw
		// brackets in a depth-hook suspend window (same discipline as
		// DrawSMAA_EdgeDetect :5758). The raw ZENABLE write makes the window
		// save this pass's real intent (Z off, asserted above): Suspend/
		// Restore save + rewrite the RAW device ZENABLE, and RwCameraBeginUpdate
		// above re-applied camera Z on that layer — without the write, Restore
		// would re-enable Z under the history-save blit further down.
		// The normal buffer is the INTZ-derived (depth-reconstructed)
		// g_normalBufferTex used by SSAO. POINT/CLAMP like the velocity
		// stage — data taps, no filtering. Velocity logic (s2) untouched.
		bool depthSuspended = false;
		dev->SetRenderState(D3DRS_ZENABLE, FALSE);
		if(g_ssaoDepthTex){
			DepthHook_Suspend();
			depthSuspended = true;
			dev->SetTexture(3, g_ssaoDepthTex);
			dev->SetSamplerState(3, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(3, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(3, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
			dev->SetSamplerState(3, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		} else {
			// Never let the shader sample a foreign s3: D3D9 sampler state
			// persists across passes, so an unbound stage returns whatever
			// the previous pass left there and the depth-rejection term
			// would run on foreign data. NULL is a defined (zero) read.
			dev->SetTexture(3, NULL);
		}
		// Normal buffer must also have completed a real write: it is a
		// D3DPOOL_DEFAULT RT (driver garbage before the first
		// DrawNormalBufferToTexture) — same gate as the buildingPipe PBR
		// s4 bind (g_normalBufferHasContent).
		if(g_normalBufferTex && g_normalBufferHasContent){
			dev->SetTexture(4, g_normalBufferTex);
			dev->SetSamplerState(4, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(4, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(4, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
			dev->SetSamplerState(4, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		} else {
			// Foreign/garbage s4 would drive normalReject from data that is
			// not the current frame's normals — unbind for a defined zero.
			dev->SetTexture(4, NULL);
		}

		// Temporal constants: blendStrength = CURRENT-frame weight at rest
		// (0.4 = 60% history at rest — audit: rest-state retention 85.4%->46%,
		// ghost tail 34->7 frames), motionScale = how fast the
		// motion signal saturates luma/velocity difference.
		// On the first frame, the history raster contains undefined data (black).
		// Use blendStrength=1.0 (all current, no history) to avoid a dark flash.
		// Subsequent frames blend normally with accumulated history.
		// motionScale 2.0 -> 3.0: moderate camera motion (walking pans) only
		// produced lumaDiff ~0.1..0.3, which left blendFactor around 0.5..0.65
		// = 35..45% of every pixel taken from the PREVIOUS frame — reads as a
		// permanent smear/"motion blur that never turns off" plus trailing
		// artifacts, entirely independent of the motionBlurEnable checkbox
		// (DrawMotionBlur's own gate is correct — see :6494+).
		float blendStrength = g_smaaHistoryValid ? 0.4f : 1.0f;
		// Hard history TTL: every 8 valid-history frames force a full current-frame
		// refresh (blendStrength=1.0) to kill the accumulated ghost tail, then reset.
		if(g_smaaHistoryValid && ++s_smaaHistAge >= 8){
			blendStrength = 1.0f;
			s_smaaHistAge = 0;
		}
		float temporalP[4] = { blendStrength, 3.0f, 0.0f, 0.0f };
		RwD3D9SetPixelShaderConstant(0, temporalP, 1);
		RwD3D9SetPixelShaderConstant(1, screenParams, 1);

		// c2 = (velocity, rotation, tanFovX, tanFovY) — velocity-driven temporal
		// weight for the resolve shader. Velocity/rotation reuse the per-frame
		// camera tracking above (same static-prev pattern as DrawMotionBlur) with
		// the same gains so SMAA softens in sync with motion blur. FOV comes from
		// the camera view window (tan half-FOV, same as vehiclePipe.cpp iblParams).
		float tanX = 0.65f, tanY = 0.45f;
		if(Scene.camera){
			tanX = Scene.camera->viewWindow.x;
			tanY = Scene.camera->viewWindow.y;
		}
		float velFactor = min(1.0f, cameraVelocity * 0.25f);
		float rotFactor = min(1.0f, cameraRotation * 2.0f);
		float motionP[4] = { velFactor, rotFactor, tanX, tanY };
		RwD3D9SetPixelShaderConstant(2, motionP, 1);

		// c3 = (near, far, maxHistClamp, velToPx) — history reprojection params.
		// near/far mirror DrawNormalBufferToTexture's c0 source. velToPx is 1.0
		// when the velocity buffer is bound (velocity is stored in UV units) and
		// 0.0 otherwise, so the shader samples history unshifted when s2 is null.
		// maxHistClamp 0.9 -> 1.0: 0.9 is the shader's CEILING on the
		// current-frame weight, i.e. it forced >=10% of the previous frame into
		// every pixel on EVERY frame ("never fully discards history"). That
		// floor is the residual trailing/ghosting after lane3's scene-copy fix,
		// and it is what reads as motion blur while the motionBlur checkbox is
		// off. 1.0 lets the motion signal reach a full current frame at speed
		// while minBlend (= blendStrength, 0.1) still keeps the slow static
		// accumulation; it also makes the blendStrength=1.0 first frame a true
		// 100% current (0.9 used to leak 10% undefined history on frame 1).
		float smaaNear = Scene.camera ? RwCameraGetNearClipPlane(Scene.camera) : 0.1f;
		float smaaFar = Scene.camera ? RwCameraGetFarClipPlane(Scene.camera) : 500.0f;
		float histP[4] = { smaaNear, smaaFar, 1.0f, velValid ? 1.0f : 0.0f };
		RwD3D9SetPixelShaderConstant(3, histP, 1);

		if(dbglog_throttle("smaa_vel"))
			dbglog("[SMAA] velocity: vel=%.3f rot=%.3f tanFov=(%.3f, %.3f) velTex=%p velToPx=%.1f velGate=%s",
				velFactor, rotFactor, tanX, tanY, (void*)(velValid ? g_velocityTex : NULL), histP[3], velWhy);

		g_renderPhase = "smaa_draw";
		if(!SMAA_DrawPass((IDirect3DPixelShader9*)SMAA_Temporal, s_ffQuad)){
			RwD3D9SetTexture(NULL, 1);
			dev->SetTexture(2, NULL);
			dev->SetTexture(3, NULL);
			dev->SetTexture(4, NULL);
			if(depthSuspended)
				DepthHook_Restore(); // keep Suspend/Restore balanced on bail
			smaaGuardBail(dev, drawBuffer, &vpSaved);
			return;
		}

		// Cleanup — unbind INTZ (s3) before DepthHook_Restore (mirrors
		// DrawSMAA_EdgeDetect :5785 / DrawHeightFog)
		RwD3D9SetTexture(NULL, 1);
		dev->SetTexture(2, NULL);
		dev->SetTexture(3, NULL);
		dev->SetTexture(4, NULL);
		if(depthSuspended)
			DepthHook_Restore();

		if(config->profilerEnable && dbglog_throttle("smaaTemporal"))
			dbglog("[SMAA-DIAG] Pass3 Temporal: edge=%p + prev=%p -> drawBuf=%p shader=%p",
				g_smaaEdgeRaster, g_smaaPrevFrameRaster, drawBuffer, SMAA_Temporal);
	} else {
		// No temporal: Pass 2 result is in edgeRaster, copy it into the camera
		// frame raster with the same EndUpdate/PushContext/RenderFast/Pop/
		// BeginUpdate idiom as UpdateFrontBuffer. RwD3D9SetRenderTarget faults
		// on rwRASTERTYPECAMERA, so never bind drawBuffer that way.
		if(config->profilerEnable && dbglog_throttle("smaa_p3_fallback"))
			dbglog("[SMAA-DIAG] Pass3 skipped (Temporal=%p prev=%p): presenting edgeRaster (pass2 composite, NOT raw edge/blend)",
				SMAA_Temporal, g_smaaPrevFrameRaster);
		g_renderPhase = "smaa_p3_rt";
		RwCameraEndUpdate(Scene.camera);
		RwRasterPushContext(drawBuffer);
		RwRasterRenderFast(g_smaaEdgeRaster, 0, 0);
		RwRasterPopContext();
		RwCameraBeginUpdate(Scene.camera);
		dev->SetViewport(&vpSaved);
	}

	// Save current frame for next frame's temporal history. Bracket the blit
	// with EndUpdate/BeginUpdate exactly like every other RenderFast call site
	// (UpdateFrontBuffer :461, scene copy :613, pass-3 fallback :1150): running
	// RwRasterRenderFast while the camera is inside an active BeginUpdate (RT0
	// = camRas) rebinds the dest surface mid-camera-update and can leave the
	// device RT pointing at prevFrameRaster afterwards (same hazard class as
	// the scratch-camera init leak, see SMAATryInitRasters). Alignment: both
	// rasters are camera-sized, full-content blit at (0,0) — no offset math.
	RwCameraEndUpdate(Scene.camera);
	RwRasterPushContext(g_smaaPrevFrameRaster);
	RwRasterRenderFast(drawBuffer, 0, 0);
	RwRasterPopContext();
	RwCameraBeginUpdate(Scene.camera);
	g_smaaHistoryValid = true;

	// Clean up D3D9 state — textures AND sampler states. Stages 3 (IBL
	// cubemap) and 4 (normal buffer), plus the PBR mask on s2, are raw-bound
	// by their scene-render consumers, so the POINT/CLAMP set mid-chain
	// (Pass1 s1/s2, Pass3 s2/s3/s4) must not survive into the next frame's
	// scene draw — RW only re-applies per-texture state on RW-cache binds.
	if(dev){
		dev->SetTexture(1, NULL);
		dev->SetTexture(2, NULL);
		dev->SetTexture(3, NULL);
		dev->SetTexture(4, NULL);
		smaaRestoreSamplerStates(dev);
	}

	// Restore RW render states
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);

	g_inGuardedIm2DPass = 0;
	g_renderPhase = "";
	ImmediateModeRenderStatesReStore();
	// AFTER ReStore (see smaaGuardBail): the RW cache holds SMAA's stage-0
	// raster, so the captured pre-SMAA binding must be pushed back last.
	smaaRestoreStage0(dev);
	smaaRestoreRawGeom(); // push the raw ZENABLE/ZWRITE/ALPHATEST/CULL values
	                       // back on BOTH layers — the rw-only ReStore above
	                       // cannot see them, and SMAA is the last pass of the
	                       // frame (they would carry into the next scene).
}

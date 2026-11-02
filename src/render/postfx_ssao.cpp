// postfx_ssao.cpp - SSAO passes + depth/prev-depth/normal-buffer resources (m0060 split).
#include "skygfx.h"
#include "debug_handl.h"
#include "postfx.h"
#include "depthhook.h"
IDirect3DTexture9 *g_ssaoDepthTex = NULL;
IDirect3DSurface9 *g_ssaoDepthSurf = NULL;
static IDirect3DTexture9 *g_ssaoNoiseTex = NULL;
RwRaster *g_ssaoOutputRaster = NULL;

// Wave-2 temporal history: previous-frame depth (own R32F render target, never aliased)
IDirect3DTexture9 *g_prevDepthTex = NULL;
IDirect3DSurface9 *g_prevDepthSurf = NULL;
bool g_prevDepthValid = false;
int s_prevDepthW = 0, s_prevDepthH = 0;  // cached dims for res-aware recreate
static bool s_prevDepthLogDone = false;         // one-shot failure log

// Wave-2 helper defined below InitSSAOResources; forward-declared for its two call sites.
static void CreatePrevDepthTexture(IDirect3DDevice9 *dev, int w, int h);

// One throttled receipt shared by every persistent SSAO/history allocation:
// fires only when a resource is actually created. The self-gates in this file
// mean ssaoEnable=0 reaches no SSAO-owned CreateTexture/RwRasterCreate and
// therefore emits no line. prevDepth is the one shared resource that can still
// be created with SSAO off (velocity reconstruction consumes it), and the
// ssaoEnable field in the line makes that exception visible.
static void SSAOAllocReceipt(const char *res, int w, int h)
{
	if(dbglog_throttle("ssao_alloc"))
		dbglog("[SSAO] alloc receipt: %s %dx%d ssaoEnable=%d",
			res, w, h, (int)config->ssaoEnable);
}

// SSAO overhaul — quarter-res temporal pipeline
static RwRaster *g_ssaoQuarterRaster[2] = {NULL, NULL};
static RwTexture *g_ssaoQuarterTexRW[2] = {NULL, NULL};
static RwRaster *g_ssaoBlurTempRaster = NULL;
static RwTexture *g_ssaoBlurTempTexRW = NULL;
static int g_ssaoFrameIndex = 0;
static bool g_ssaoHistoryValid = false;
static int g_ssaoQuarterW = 0, g_ssaoQuarterH = 0;
IDirect3DTexture9 *g_normalBufferTex = NULL;
IDirect3DSurface9 *g_normalBufferSurf = NULL;
int s_normalBufW = 0, s_normalBufH = 0;  // cached dims for res-aware recreate
// True only after DrawNormalBufferToTexture completed at least one full write.
// A freshly created D3DPOOL_DEFAULT RT is driver garbage (cleared to the
// invalid-normal marker in GetNormalBufferTexture, but the marker must also
// never be blended into geometry normals), so consumers gate binding on this
// flag — see the s4 bind gate in buildingPipe.cpp PBR callback.
bool g_normalBufferHasContent = false;

// Wave-2 temporal history: previous-frame normals (half-res, same dims as g_normalBufferTex)
IDirect3DTexture9 *g_prevNormalTex = NULL;
IDirect3DSurface9 *g_prevNormalSurf = NULL;
bool g_normalHistoryValid = false;
void InitSSAOResources(void)
{
	// If noise exists but depth texture is NULL (e.g. after device Reset),
	// re-alias from depthhook's INTZ which was recreated by hook_SetDS.
	if(g_ssaoNoiseTex) {
		if(!g_ssaoDepthTex && g_intzTex) {
			g_ssaoDepthTex = g_intzTex;
			g_ssaoDepthSurf = g_intzSurf;
			dbglog("InitSSAOResources: re-aliased depthTex from depthhook after Reset");
		}
		return;
	}

	// Cheap alias with NO allocation: DrawNormalBufferToTexture calls this
	// with ssaoEnable=0 (it only needs the live depth texture) — keep that
	// path working without creating the SSAO noise/history resources.
	if(!g_ssaoDepthTex && g_intzTex) {
		g_ssaoDepthTex = g_intzTex;
		g_ssaoDepthSurf = g_intzSurf;
		dbglog("InitSSAOResources: aliased depthTex from depthhook (no SSAO alloc)");
	}

	// Self-gate: everything below is SSAO-owned. With SSAO disabled no pass
	// can sample it, so never create the noise / fallback INTZ / history
	// textures. (The depth alias above stays available to the normal buffer.)
	if(!config->ssaoEnable)
		return;

	__try {
		dbglog("InitSSAOResources: start");
		IDirect3DDevice9 *dev = d3d9device;
		if(dev == NULL){ dbglog("InitSSAOResources: no dev"); return; }
		if(Scene.camera == NULL){ dbglog("InitSSAOResources: no camera"); return; }
		RwRaster *camRas = RwCameraGetRaster(Scene.camera);
		if(camRas == NULL){ dbglog("InitSSAOResources: no camRas"); return; }
		int w = camRas->width;
		int h = camRas->height;
		dbglog("InitSSAOResources: camRas %dx%d", w, h);

		if(FAILED(dev->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_ssaoNoiseTex, NULL))){
			dbglog("InitSSAOResources: CreateTexture noise 4x4 MANAGED failed");
			return;
		}
		dbglog("InitSSAOResources: noise texture created (pool=MANAGED)");
		SSAOAllocReceipt("ssaoNoise(A8R8G8B8)", 4, 4);
		D3DLOCKED_RECT lr;
		if(FAILED(g_ssaoNoiseTex->LockRect(0, &lr, NULL, 0))){
			dbglog("InitSSAOResources: LockRect noise failed");
			return;
		}
		for(int y = 0; y < 4; y++){
			for(int x = 0; x < 4; x++){
				float rx = (rand()/(float)RAND_MAX)*2.0f - 1.0f;
				float ry = (rand()/(float)RAND_MAX)*2.0f - 1.0f;
				float len = sqrtf(rx*rx + ry*ry);
				if(len > 1.0f){ rx /= len; ry /= len; }
				((DWORD*)((BYTE*)lr.pBits + y*lr.Pitch))[x] =
					D3DCOLOR_ARGB(0, (int)((rx+1)*127.5f), (int)((ry+1)*127.5f), 0);
			}
		}
		g_ssaoNoiseTex->UnlockRect(0);
		dbglog("InitSSAOResources: noise texture filled");

		// Use depthhook's INTZ texture if available (it's bound as DS by the vtable hook)
		// This is the primary path — depthhook's INTZ has real depth data written by the game.
		if(g_intzTex) {
			g_ssaoDepthTex = g_intzTex;
			g_ssaoDepthSurf = g_intzSurf;
			dbglog("InitSSAOResources: using depthhook INTZ depth texture %dx%d", w, h);
			CreatePrevDepthTexture(dev, w, h);
			dbglog("InitSSAOResources: done");
			return;
		}

		// Fallback: depthhook not available — try creating our own INTZ.
		// NOTE: This texture is NEVER bound as DS, so depth consumers will read garbage.
		// This path exists only for the case where depthhook is disabled by INI.
		IDirect3D9 *d3d = NULL;
		if(SUCCEEDED(dev->GetDirect3D(&d3d)) && d3d){
			dbglog("InitSSAOResources: checking INTZ support (fallback, no depthhook)");
			HRESULT hr = d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
				D3DFMT_X8R8G8B8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE,
				(D3DFORMAT)MAKEFOURCC('I','N','T','Z'));
			if(SUCCEEDED(hr)){
				dbglog("InitSSAOResources: INTZ supported, creating depth texture (fallback)");
				hr = dev->CreateTexture(w, h, 1, D3DUSAGE_DEPTHSTENCIL,
					(D3DFORMAT)MAKEFOURCC('I','N','T','Z'),
					D3DPOOL_DEFAULT, &g_ssaoDepthTex, NULL);
				if(SUCCEEDED(hr)){
					g_ssaoDepthTex->GetSurfaceLevel(0, &g_ssaoDepthSurf);
					dbglog("InitSSAOResources: INTZ depth texture created OK (fallback)");
					SSAOAllocReceipt("ssaoDepth(INTZ-fallback)", w, h);
					CreatePrevDepthTexture(dev, w, h);
					d3d->Release();
					dbglog("InitSSAOResources: done");
					return;
				}else{
					dbglog("InitSSAOResources: CreateTexture INTZ failed hr=0x%08X", hr);
				}
			}else{
				dbglog("InitSSAOResources: INTZ format not supported, SSAO disabled");
			}
			d3d->Release();
		}
		if(!g_ssaoDepthTex){
			dbglog("InitSSAOResources: SSAO disabled - depth texture not available");
			return;
		}
		dbglog("InitSSAOResources: done");
	} __except(EXCEPTION_EXECUTE_HANDLER){
		dbglog("InitSSAOResources crashed! exception=0x%08X", GetExceptionCode());
	}
}

// Wave-2: create the previous-frame depth history texture (own R32F render target).
// A plain R32F render target is used instead of INTZ/DEPTHSTENCIL because INTZ is a
// texture format that can neither be read back via GetRenderTargetData (requires
// POOL_SYSTEMMEM) nor copied via StretchRect (requires a plain depth-stencil surface).
// The history is instead populated by rendering a fullscreen quad that samples the
// current scene depth (see CopyDepthToPrev).
// Res-aware: mirrors the velocity/normal buffer idiom — recreate when dims change.
static void CreatePrevDepthTexture(IDirect3DDevice9 *dev, int w, int h)
{
	if(!dev || w < 1 || h < 1) return;
	if(g_prevDepthTex && s_prevDepthW == w && s_prevDepthH == h) return;

	// Release on dim change (or re-init)
	if(g_prevDepthTex){ g_prevDepthTex->Release(); g_prevDepthTex = NULL; }
	if(g_prevDepthSurf){ g_prevDepthSurf->Release(); g_prevDepthSurf = NULL; }
	g_prevDepthValid = false;

	HRESULT hr = dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET,
		D3DFMT_R32F, D3DPOOL_DEFAULT, &g_prevDepthTex, NULL);
	if(SUCCEEDED(hr)){
		g_prevDepthTex->GetSurfaceLevel(0, &g_prevDepthSurf);
		s_prevDepthW = w;
		s_prevDepthH = h;
		dbglog("InitSSAOResources: prevDepth R32F texture created OK %dx%d", w, h);
		SSAOAllocReceipt("prevDepth(R32F)", w, h);
	}else{
		dbglog("InitSSAOResources: CreateTexture prevDepth R32F failed hr=0x%08X", hr);
		g_prevDepthTex = NULL;
		g_prevDepthSurf = NULL;
		s_prevDepthW = 0;
		s_prevDepthH = 0;
		g_prevDepthValid = false;
	}
}

// Wave-2: copy current depth (g_ssaoDepthTex) into the previous-frame history texture
// by rendering a fullscreen quad whose pixel shader samples the scene depth and writes
// it to the R32F render target. History is only marked valid when the draw succeeds.
void CopyDepthToPrev(void)
{
	if(!g_ssaoDepthTex){
		g_prevDepthValid = false;
		return;
	}
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev){ g_prevDepthValid = false; return; }
	if(!ClampShader){ g_prevDepthValid = false; return; }

	// Res-aware lazy (re)create of the history texture (full camera resolution).
	RwRaster *camRas = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
	if(!camRas){ g_prevDepthValid = false; return; }
	CreatePrevDepthTexture(dev, camRas->width, camRas->height);

	if(!g_prevDepthTex || !g_prevDepthSurf){ g_prevDepthValid = false; return; }

	// Save current RT, DS, viewport
	IDirect3DSurface9 *oldRT = NULL;
	IDirect3DSurface9 *oldDS = NULL;
	D3DVIEWPORT9 oldVP;
	dev->GetRenderTarget(0, &oldRT);
	dev->GetDepthStencilSurface(&oldDS);
	dev->GetViewport(&oldVP);

	bool ok = false;

	DWORD rawGeom[9];
	bool rawGeomSaved = SaveRawGeomStates(rawGeom); // before Store (raw Set below)
	CPostEffects::ImmediateModeRenderStatesStore();
	CPostEffects::ImmediateModeRenderStatesSet();
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	dev->SetRenderTarget(0, g_prevDepthSurf);
	dev->SetDepthStencilSurface(NULL);
	D3DVIEWPORT9 pdVP = { 0, 0, (DWORD)s_prevDepthW, (DWORD)s_prevDepthH, 0.0f, 1.0f };
	dev->SetViewport(&pdVP);

	// Sampler snapshot (stage 0): the pass raw-overrides ADDRESSU/V and
	// MIN/MAG with CLAMP/POINT; the old cleanup hardcoded LINEAR for MIN/MAG
	// only, so a non-default entry filter/address mode leaked past the copy.
	DWORD sampSaved[1][5];
	dev->GetSamplerState(0, D3DSAMP_ADDRESSU, &sampSaved[0][0]);
	dev->GetSamplerState(0, D3DSAMP_ADDRESSV, &sampSaved[0][1]);
	dev->GetSamplerState(0, D3DSAMP_ADDRESSW, &sampSaved[0][2]);
	dev->GetSamplerState(0, D3DSAMP_MINFILTER, &sampSaved[0][3]);
	dev->GetSamplerState(0, D3DSAMP_MAGFILTER, &sampSaved[0][4]);

	__try {
		// Suspend depth hook so g_ssaoDepthTex (INTZ) can be sampled while not bound as DS
		DepthHook_Suspend();

		dev->SetTexture(0, g_ssaoDepthTex);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);

		// ClampShader is the generic s0→RT copy shader; c0={min,max,tonemap,0} with
		// [0,1] clamp is a no-op for depth (already in [0,1]).
		float c0[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
		RwD3D9SetPixelShaderConstant(0, c0, 1);

		overrideIm2dPixelShader = ClampShader;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
		overrideIm2dPixelShader = nil;

		// Unbind INTZ from s0 BEFORE the hook re-binds it as DS (feedback-lock
		// guard, mirrors DrawSSAO/DrawHeightFog); the cleanup below stays as-is.
		dev->SetTexture(0, NULL);
		DepthHook_Restore();
		ok = true;
	} __except(EXCEPTION_EXECUTE_HANDLER){
		overrideIm2dPixelShader = nil;	// SEH: the `= nil` reset in the __try above is skipped on fault

		dev->SetTexture(0, NULL); // same unbind before the bail-out Restore
		DepthHook_Restore(); // keep Suspend/Restore balanced on fault
	}

	dev->SetTexture(0, NULL);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);

	CPostEffects::ImmediateModeRenderStatesReStore();
	// Raw sampler restore LAST (canonical order — ReStore re-emits the
	// rw-cached texture filter through RwD3D9 and would clobber a raw min/mag
	// restore done before it; same ordering as DrawVelocityBuffer/RenderLumaPass).
	dev->SetSamplerState(0, D3DSAMP_ADDRESSU, sampSaved[0][0]);
	dev->SetSamplerState(0, D3DSAMP_ADDRESSV, sampSaved[0][1]);
	dev->SetSamplerState(0, D3DSAMP_ADDRESSW, sampSaved[0][2]);
	dev->SetSamplerState(0, D3DSAMP_MINFILTER, sampSaved[0][3]);
	dev->SetSamplerState(0, D3DSAMP_MAGFILTER, sampSaved[0][4]);
	if(rawGeomSaved){
		RestoreRawGeomStates(rawGeom);
		rawGeomSaved = false;
	}

	dev->SetViewport(&oldVP);
	dev->SetRenderTarget(0, oldRT);
	dev->SetDepthStencilSurface(oldDS);
	if(oldRT) oldRT->Release();
	if(oldDS) oldDS->Release();

	g_prevDepthValid = ok;
	if(!ok && !s_prevDepthLogDone){
		s_prevDepthLogDone = true;
		dbglog("[PostFX] CopyDepthToPrev: depth copy failed, prev-depth history disabled (one-shot)");
	}
}

// ============================================================
// PostFX_CopyDepthToTexture — copy the live scene depth (INTZ depth-stencil,
// currently bound as the device DS) into dstTex (an R32F render target) as
// raw window-space z. Used by the water pipe at water-draw time for
// depth-based shore blending / translucency / shallow->deep colour.
//
// Unlike CopyDepthToPrev (postfx tail, Z irrelevant) this runs MID-SCENE,
// so the exact incoming depth-stencil surface is re-bound at the end instead
// of DepthHook_Restore's cached game DS — the water draw that follows must
// keep Z-testing against the live INTZ contents.
//
// Returns false (and touches nothing) when the copy cannot run: no INTZ,
// INTZ not the active DS (its contents would be stale), or a draw fault —
// callers fall back to depth-less shading.
// ============================================================
bool PostFX_CopyDepthToTexture(IDirect3DTexture9 *dstTex, int dstW, int dstH)
{
	// Self-gate: the water pipe is the only consumer, and it cannot shade
	// with the result while its parallax/depth path is off. Bail before the
	// RT/DS AddRefs and before any depth-stencil state is touched.
	if(!config->waterParallaxEnable)
		return false;
	if(!dstTex || dstW < 1 || dstH < 1) return false;
	if(!g_intzTex || !g_intzSurf) return false;
	if(!ClampShader) return false;
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev) return false;

	IDirect3DSurface9 *oldRT = NULL;
	IDirect3DSurface9 *oldDS = NULL;
	D3DVIEWPORT9 oldVP;
	dev->GetRenderTarget(0, &oldRT);
	dev->GetDepthStencilSurface(&oldDS);
	dev->GetViewport(&oldVP);

	// Only trust INTZ contents when INTZ is the surface receiving depth writes.
	if(oldDS != g_intzSurf){
		if(oldRT) oldRT->Release();
		if(oldDS) oldDS->Release();
		return false;
	}

	IDirect3DSurface9 *dstSurf = NULL;
	if(FAILED(dstTex->GetSurfaceLevel(0, &dstSurf)) || !dstSurf){
		if(oldRT) oldRT->Release();
		if(oldDS) oldDS->Release();
		return false;
	}

	bool ok = false;

	DWORD rawGeom[9];
	bool rawGeomSaved = SaveRawGeomStates(rawGeom); // before Store (raw Set below)
	CPostEffects::ImmediateModeRenderStatesStore();
	CPostEffects::ImmediateModeRenderStatesSet();
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);

	dev->SetRenderTarget(0, dstSurf);
	// Unbind the DS so INTZ can be sampled (hook_SetDS passes NULL straight
	// through to the original — no substitution on the unbind path).
	dev->SetDepthStencilSurface(NULL);
	D3DVIEWPORT9 vp = { 0, 0, (DWORD)dstW, (DWORD)dstH, 0.0f, 1.0f };
	dev->SetViewport(&vp);

	// Sampler snapshot (stage 0): ADDRESSU/V + MIN/MAG are raw-overridden
	// below; the old cleanup restored a hardcoded LINEAR filter only.
	DWORD sampSaved[1][5];
	dev->GetSamplerState(0, D3DSAMP_ADDRESSU, &sampSaved[0][0]);
	dev->GetSamplerState(0, D3DSAMP_ADDRESSV, &sampSaved[0][1]);
	dev->GetSamplerState(0, D3DSAMP_ADDRESSW, &sampSaved[0][2]);
	dev->GetSamplerState(0, D3DSAMP_MINFILTER, &sampSaved[0][3]);
	dev->GetSamplerState(0, D3DSAMP_MAGFILTER, &sampSaved[0][4]);

	__try {
		dev->SetTexture(0, g_intzTex);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);

		// Fullscreen quad in the dst RT's own pixel grid (colourfilterVerts
		// would only be valid once the colourfilter has run this frame).
		RwIm2DVertex quad[4];
		for(int i = 0; i < 4; i++){
			quad[i].z = 0.0f;
			quad[i].rhw = 1.0f;
			quad[i].emissiveColor = 0xFFFFFFFF;
		}
		quad[0].x = 0.0f;         quad[0].y = 0.0f;         quad[0].u = 0.0f; quad[0].v = 0.0f;
		quad[1].x = (float)dstW;  quad[1].y = 0.0f;         quad[1].u = 1.0f; quad[1].v = 0.0f;
		quad[2].x = 0.0f;         quad[2].y = (float)dstH;  quad[2].u = 0.0f; quad[2].v = 1.0f;
		quad[3].x = (float)dstW;  quad[3].y = (float)dstH;  quad[3].u = 1.0f; quad[3].v = 1.0f;
		RwImVertexIndex quadIdx[6] = { 0, 1, 2, 2, 1, 3 };

		// ClampShader is the generic s0→RT copy shader; c0={min,max,tonemap,0}
		// with a [0,1] clamp is a no-op for raw depth (already in [0,1]).
		float c0[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
		RwD3D9SetPixelShaderConstant(0, c0, 1);

		overrideIm2dPixelShader = ClampShader;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, quad, 4, quadIdx, 6);
		overrideIm2dPixelShader = nil;

		ok = true;
	} __except(EXCEPTION_EXECUTE_HANDLER){
		overrideIm2dPixelShader = nil;	// SEH: the `= nil` reset in the __try above is skipped on fault

		dbglog("[PostFX] CopyDepthToTexture fault code=0x%08X", GetExceptionCode());
		ok = false;
	}

	dev->SetTexture(0, NULL);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);

	CPostEffects::ImmediateModeRenderStatesReStore();
	// Raw sampler restore LAST (canonical order — ReStore re-emits the
	// rw-cached texture filter through RwD3D9 and would clobber a raw min/mag
	// restore done before it; same ordering as DrawVelocityBuffer/RenderLumaPass).
	dev->SetSamplerState(0, D3DSAMP_ADDRESSU, sampSaved[0][0]);
	dev->SetSamplerState(0, D3DSAMP_ADDRESSV, sampSaved[0][1]);
	dev->SetSamplerState(0, D3DSAMP_ADDRESSW, sampSaved[0][2]);
	dev->SetSamplerState(0, D3DSAMP_MINFILTER, sampSaved[0][3]);
	dev->SetSamplerState(0, D3DSAMP_MAGFILTER, sampSaved[0][4]);
	if(rawGeomSaved){
		RestoreRawGeomStates(rawGeom);
		rawGeomSaved = false;
	}

	dev->SetViewport(&oldVP);
	dev->SetRenderTarget(0, oldRT);
	// Re-bind the EXACT incoming DS (== g_intzSurf -> hook_SetDS passthrough)
	// so the following water draw Z-tests against live scene depth.
	dev->SetDepthStencilSurface(oldDS);
	dstSurf->Release();
	if(oldRT) oldRT->Release();
	if(oldDS) oldDS->Release();
	return ok;
}

void ReleaseSSAOOverhaulResources(void){
	for(int i = 0; i < 2; i++){
		if(g_ssaoQuarterTexRW[i]){ RwTextureDestroy(g_ssaoQuarterTexRW[i]); g_ssaoQuarterTexRW[i] = NULL; }
		if(g_ssaoQuarterRaster[i]){ RwRasterDestroy(g_ssaoQuarterRaster[i]); g_ssaoQuarterRaster[i] = NULL; }
	}
	if(g_ssaoBlurTempTexRW){ RwTextureDestroy(g_ssaoBlurTempTexRW); g_ssaoBlurTempTexRW = NULL; }
	if(g_ssaoBlurTempRaster){ RwRasterDestroy(g_ssaoBlurTempRaster); g_ssaoBlurTempRaster = NULL; }
	g_ssaoHistoryValid = false;
	g_ssaoQuarterW = g_ssaoQuarterH = 0;
}

static void InitSSAOOverhaulResources(void){
	// Self-gate: these rasters feed only the SSAO passes (quarter-res AO,
	// blur, upsample/composite). With SSAO off nothing can consume them.
	if(!config->ssaoEnable)
		return;
	if(g_ssaoQuarterRaster[0] && g_ssaoQuarterRaster[1] && g_ssaoBlurTempRaster)
		return;

	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	if(!camRas) return;

	int qw = (camRas->width + 3) / 4;
	int qh = (camRas->height + 3) / 4;

	if(g_ssaoQuarterW == qw && g_ssaoQuarterH == qh) return;

	ReleaseSSAOOverhaulResources();

	for(int i = 0; i < 2; i++){
		g_ssaoQuarterRaster[i] = RwRasterCreate(qw, qh, camRas->depth, rwRASTERTYPECAMERATEXTURE);
		if(!g_ssaoQuarterRaster[i]){
			dbglog("[SSAO] Failed to create quarter raster %d", i);
			return;
		}
		SSAOAllocReceipt(i == 0 ? "ssaoQuarterRT[0]" : "ssaoQuarterRT[1]", qw, qh);
		g_ssaoQuarterTexRW[i] = RwTextureCreate(g_ssaoQuarterRaster[i]);
		if(g_ssaoQuarterTexRW[i]){
			RwTextureSetFilterMode(g_ssaoQuarterTexRW[i], rwFILTERLINEAR);
			RwTextureSetAddressingU(g_ssaoQuarterTexRW[i], rwTEXTUREADDRESSCLAMP);
			RwTextureSetAddressingV(g_ssaoQuarterTexRW[i], rwTEXTUREADDRESSCLAMP);
		}
	}

	g_ssaoBlurTempRaster = RwRasterCreate(qw, qh, camRas->depth, rwRASTERTYPECAMERATEXTURE);
	if(!g_ssaoBlurTempRaster){
		dbglog("[SSAO] Failed to create blur temp raster");
		return;
	}
	SSAOAllocReceipt("ssaoBlurTempRT", qw, qh);
	g_ssaoBlurTempTexRW = RwTextureCreate(g_ssaoBlurTempRaster);
	if(g_ssaoBlurTempTexRW){
		RwTextureSetFilterMode(g_ssaoBlurTempTexRW, rwFILTERLINEAR);
		RwTextureSetAddressingU(g_ssaoBlurTempTexRW, rwTEXTUREADDRESSCLAMP);
		RwTextureSetAddressingV(g_ssaoBlurTempTexRW, rwTEXTUREADDRESSCLAMP);
	}

	g_ssaoQuarterW = qw;
	g_ssaoQuarterH = qh;
	g_ssaoHistoryValid = false;
}

// Throttled AO-output validation: read back a sparse grid from the occlusion
// RT and fail-open (skip the multiply composite) if it is near-black — under
// SRCBLEND=ZERO/DESTBLEND=SRCCOLOR a black AO texture *replaces* the whole
// scene with black. Reuses the DumpRasterToBMP GetRenderTargetData pattern,
// but runs regardless of postfxDumpDebug because it is a safety gate, not a
// debug dump. First 3 frames always validate (catch immediate failures),
// then once every 60 frames (~2s) to bound the GetRenderTargetData stall.
// Returns true = composite may proceed.
static bool ValidateSSAOOutput(IDirect3DSurface9 *aoRT)
{
	// Self-gate: this readback only exists to guard the SSAO multiply
	// composite; with SSAO off no composite runs, so no surface is created.
	if(!config->ssaoEnable)
		return true;
	static int s_validateCount = 0;
	s_validateCount++;
	if(s_validateCount > 3 && (s_validateCount % 60) != 0)
		return true;
	if(!aoRT)
		return true; // no surface captured — can't verify, don't block
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return true;

	D3DSURFACE_DESC desc;
	if(FAILED(aoRT->GetDesc(&desc)) || desc.Width < 1 || desc.Height < 1)
		return true;
	IDirect3DSurface9 *sysSurf = NULL;
	if(FAILED(dev->CreateOffscreenPlainSurface(desc.Width, desc.Height,
			D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sysSurf, NULL)) || !sysSurf)
		return true;
	if(FAILED(dev->GetRenderTargetData(aoRT, sysSurf))){
		sysSurf->Release();
		return true; // readback failed — don't skip the composite on a glitch
	}
	D3DLOCKED_RECT lr;
	if(FAILED(sysSurf->LockRect(&lr, NULL, D3DLOCK_READONLY))){
		sysSurf->Release();
		return true;
	}
	// Sparse 16x16 grid of the red channel (AO output is greyscale)
	float amin = 1.0f, amax = 0.0f, asum = 0.0f;
	int count = 0;
	for(int gy = 0; gy < 16; gy++){
		int y = (int)((size_t)gy * (desc.Height - 1) / 15);
		const BYTE *row = (const BYTE*)lr.pBits + (size_t)y * lr.Pitch;
		for(int gx = 0; gx < 16; gx++){
			int x = (int)((size_t)gx * (desc.Width - 1) / 15);
			float a = row[x * 4 + 2] / 255.0f; // A8R8G8B8 -> R
			if(a < amin) amin = a;
			if(a > amax) amax = a;
			asum += a;
			count++;
		}
	}
	sysSurf->UnlockRect();
	sysSurf->Release();

	// Healthy AO: mostly white (avg ~0.7+). avg < 0.02 = effectively all
	// black -> the multiply would black out the frame -> fail open.
	float aavg = asum / (float)count;
	bool ok = (aavg >= 0.02f);
	// Tag "ssao_aoread" (FNV bucket 12, collision-free). The old tag
	// "ssao_aoval" hashed to bucket 231 TOGETHER with "cf_pipeline" (the
	// every-frame ColourFilter_switch breadcrumb, evaluated earlier in the
	// same frame) — the shared 2s slot meant cf_pipeline won every window
	// and this receipt was PERMANENTLY STARVED (zero emissions in a 81k-line
	// log while the pass ran). This readback is the only evidence for
	// "AO buffer is white vs dark" — the key discriminator for a reported
	// "SSAO does nothing" — so it must never share a bucket with a tag that
	// is evaluated before it every frame.
	if(dbglog_throttle("ssao_aoread"))
		dbglog("[PostFX] SSAO ao validate: min=%.3f max=%.3f avg=%.3f -> %s",
			amin, amax, aavg, ok ? "composite" : "SKIP (near-black, fail-open)");
	return ok;
}

// Restore the stage-0 texture binding captured before an AO multiply pass.
// The composite binds g_ssaoOutputRaster through the RW cache, which
// ImmediateModeRenderStatesReStore does NOT cover, so the AO texture would
// stay bound after the pass. Park-and-set guarantees a real cache transition
// (setting the same value again can be a cache-hit no-op), then the raster is
// resolved to its D3D9 texture and pinned raw so layer (3) matches the cache
// instead of leaking the AO output texture.
static void RestoreSSAOStage0Raster(RwRaster *saved)
{
	if(saved == (RwRaster*)0xFFFFFFFF)
		saved = NULL;	// failed RwRenderStateGet sentinel
	RwRaster *whiteRas = gpWhiteTexture ? gpWhiteTexture->raster : NULL;
	RwRaster *park = (saved == whiteRas) ? (RwRaster*)NULL : whiteRas;
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)park);
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)saved);
	IDirect3DTexture9 *tex = NULL;
	if(saved){
		// Game RwD3D9SetTexture formula (0x7FDCD0): ext = raster +
		// *(DWORD*)0xB4E9E0; ext+0 = IDirect3DTexture9*. Guarded derefs.
		DWORD extOff = *(DWORD*)0xB4E9E0;
		if(extOff > 0 && extOff < 0x100000){
			IDirect3DTexture9 *cand = *(IDirect3DTexture9**)((BYTE*)saved + extOff);
			if(cand && (uintptr_t)cand >= 0x10000u && (((uintptr_t)cand & 3u) == 0))
				tex = cand;
		}
	}
	if(d3d9device)
		d3d9device->SetTexture(0, tex);
}

void
CPostEffects::DrawSSAO(void)
{
	if(IsGameInMenuOrPaused()) return;
	if(dbglog_throttle( "ssao_enter"))
		dbglog("[PostFX] DrawSSAO ENTER ssaoEnable=%d SSAO=%p", config->ssaoEnable, SSAO);
	if(!config->ssaoEnable || !SSAO){
		if(dbglog_throttle( "ssao_bail"))
			dbglog("[PostFX] DrawSSAO bailing: ssaoEnable=%d SSAO=%p", config->ssaoEnable, SSAO);
		return;
	}

	// Declared before __try so the __except path can restore them too
	DWORD rawGeom[9];
	bool rawGeomSaved = false;
	bool imStored = false; // set after ImmediateModeRenderStatesStore
	// Sampler fields this pass raw-overrides (stages 0-2): ADDRESSU/V, ADDRESSW,
	// MINFILTER and MAGFILTER are snapshotted and restored on normal + fault exits.
	DWORD sampSaved[3][5];
	bool sampSavedOk = false;
	// Stage-0 raster captured before the multiply binds g_ssaoOutputRaster;
	// ReStore does not cover TEXTURERASTER, so the pass must restore it itself.
	RwRaster *savedStage0Raster = NULL;
	bool stage0Saved = false;
	// Hoisted for the __except path: the AO output surface (AddRef'd by
	// GetRenderTarget below) and the camera-raster swap state. A fault between
	// a swap-in and its paired swap-out must not leave Scene.camera rendering
	// into the AO buffer (every later draw this frame — and the next frame's
	// scene render — would land in the AO target = black/garbage screen).
	IDirect3DSurface9 *aoRT = NULL;
	RwRaster *origRaster = NULL;
	bool camSwapped = false;	// camera currently points at an SSAO RT
	bool camInUpdate = false;	// ...and is mid-BeginUpdate on it

	__try {
		IDirect3DDevice9 *dev = d3d9device;
		if(dev == NULL){ dbglog("[PostFX] DrawSSAO bailing: dev is NULL"); return; }
		if(Scene.camera == NULL){ dbglog("[PostFX] DrawSSAO bailing: Scene.camera is NULL"); return; }
		RwRaster *camRas = RwCameraGetRaster(Scene.camera);
		if(camRas == NULL){ dbglog("[PostFX] DrawSSAO bailing: camRas is NULL"); return; }
		int w = camRas->width;
		int h = camRas->height;

		InitSSAOResources();

		if(!g_ssaoNoiseTex || !g_ssaoDepthTex){
			dbglog("[PostFX] DrawSSAO bailing: noiseTex=%p depthTex=%p", g_ssaoNoiseTex, g_ssaoDepthTex);
			return;
		}

		if(!g_ssaoOutputRaster || g_ssaoOutputRaster->width != w || g_ssaoOutputRaster->height != h){
			if(g_ssaoOutputRaster) RwRasterDestroy(g_ssaoOutputRaster);
			g_ssaoOutputRaster = RwRasterCreate(w, h, camRas->depth, rwRASTERTYPECAMERATEXTURE);
			if(!g_ssaoOutputRaster){ dbglog("[PostFX] DrawSSAO bailing: output raster creation failed"); return; }
			SSAOAllocReceipt("ssaoOutputRT", w, h);
		}

		// Execution receipt (one-shot + throttled): positive proof the pass
		// runs with live inputs. The white-clear + fail-open composite design
		// makes any silent draw failure look EXACTLY like "SSAO off" (white
		// AO buffer multiplies by 1.0), so the log must carry execution
		// evidence. "inputs=ok" = every guard above passed; the pointer list
		// exposes a NULL input at a glance. Tags: "ssao_run_once" is a true
		// one-shot (dbglog_once, never starved); "ssao_exec" is FNV bucket
		// 213, verified collision-free against the project's throttle tags.
		if(dbglog_once("ssao_run_once") || dbglog_throttle("ssao_exec"))
			dbglog("[SSAO] running %dx%d inputs=ok path=fullres noise=%p depth=%p norm=%p",
				w, h, (void*)g_ssaoNoiseTex, (void*)g_ssaoDepthTex,
				(void*)g_normalBufferTex);

		// Save raw geometry states BEFORE Suspend (it writes raw ZENABLE)
		rawGeomSaved = SaveRawGeomStates(rawGeom);

		// Suspend depth hook so g_ssaoDepthTex can be sampled while not bound as DS
		DepthHook_Suspend();

		ImmediateModeRenderStatesStore();
		imStored = true;
		ImmediateModeRenderStatesSet();

		// Render SSAO occlusion to output raster
		origRaster = camRas;
		RwCameraEndUpdate(Scene.camera);
		RwCameraSetRaster(Scene.camera, g_ssaoOutputRaster);
		RwCameraBeginUpdate(Scene.camera);
		camSwapped = true;
		camInUpdate = true;

		// White-clear the occlusion target: if the pass is skipped or fails
		// mid-frame the composite multiplies by 1.0 (graceful no-op) instead
		// of stale/garbage data — SSAO must degrade to "off", never black.
		dev->Clear(0, NULL, D3DCLEAR_TARGET, 0xFFFFFFFF, 1.0f, 0);

		// Capture the AO output surface while bound for the post-pass readback
		// validation (fail-open gate on the multiply composite below).
		// aoRT is the hoisted fault-path slot — released in the handler too.
		dev->GetRenderTarget(0, &aoRT);

		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
		RwD3D9SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		RwD3D9SetRenderState(D3DRS_ZENABLE, FALSE);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

		// Snapshot the sampler fields BEFORE raw-overriding them (stages 0-2).
		for(int st = 0; st < 3; st++){
			dev->GetSamplerState(st, D3DSAMP_ADDRESSU, &sampSaved[st][0]);
			dev->GetSamplerState(st, D3DSAMP_ADDRESSV, &sampSaved[st][1]);
			dev->GetSamplerState(st, D3DSAMP_ADDRESSW, &sampSaved[st][2]);
			dev->GetSamplerState(st, D3DSAMP_MINFILTER, &sampSaved[st][3]);
			dev->GetSamplerState(st, D3DSAMP_MAGFILTER, &sampSaved[st][4]);
		}
		sampSavedOk = true;

		dev->SetTexture(0, g_ssaoDepthTex);
		dev->SetTexture(1, g_ssaoNoiseTex);
		dev->SetTexture(2, g_normalBufferTex);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		dev->SetSamplerState(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

		float radius = config->ssaoRadius > 0.0f ? config->ssaoRadius : 1.0f;
		float power = config->ssaoPower > 0.0f ? config->ssaoPower : 2.0f;
		float noiseScale = 4.0f / w;
		float params[4] = { radius, power, noiseScale, 0.0f };
		RwD3D9SetPixelShaderConstant(0, params, 1);

		float screenSize[4] = { (float)w, (float)h, 1.0f/max((float)w, 1e-7f), 1.0f/max((float)h, 1e-7f) };
		RwD3D9SetPixelShaderConstant(1, screenSize, 1);

		RwCamera *cam = Scene.camera;
		float n = cam->nearPlane;
		float f = cam->farPlane;
		float projInfo[4] = {
			cam->recipViewWindow.x,
			cam->recipViewWindow.y,
			-n * f / (f - n),
			f / (f - n)
		};
		RwD3D9SetPixelShaderConstant(2, projInfo, 1);

		// RT-sized white quad: correct UV 0..1 on this raster, white vertex
		// colour so the fixed-function composite can't modulate to black.
		SetupFullscreenQuad((float)w, (float)h);

		overrideIm2dPixelShader = SSAO;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
		overrideIm2dPixelShader = nil;

		// Restore original camera raster
		RwCameraEndUpdate(Scene.camera);
		camInUpdate = false;
		RwCameraSetRaster(Scene.camera, origRaster);
		RwCameraBeginUpdate(Scene.camera);
		camSwapped = false;

		// Unbind SSAO samplers (s0-s2) so g_ssaoDepthTex doesn't leak past
		// DepthHook_Restore and stay bound while INTZ is re-bound as DS
		// (feedback lock risk), mirroring DrawSSAO_Overhaul/DrawHeightFog.
		dev->SetTexture(0, NULL);
		dev->SetTexture(1, NULL);
		dev->SetTexture(2, NULL);

		// Restore the sampler fields this pass overrode (both exit paths).
		if(sampSavedOk){
			for(int st = 0; st < 3; st++){
				dev->SetSamplerState(st, D3DSAMP_ADDRESSU, sampSaved[st][0]);
				dev->SetSamplerState(st, D3DSAMP_ADDRESSV, sampSaved[st][1]);
				dev->SetSamplerState(st, D3DSAMP_ADDRESSW, sampSaved[st][2]);
				dev->SetSamplerState(st, D3DSAMP_MINFILTER, sampSaved[st][3]);
				dev->SetSamplerState(st, D3DSAMP_MAGFILTER, sampSaved[st][4]);
			}
		}

		// Restore depth hook (re-binds INTZ as DS, re-enables Z)
		DepthHook_Restore();

		// Photoshop-multiply contract: final = scene x aoTexture, white =
		// no occlusion. Fail-open: skip the composite entirely if the readback
		// says the AO buffer is near-black (a black texture under
		// ZERO/SRCCOLOR *replaces* the scene with black).
		bool aoOk = ValidateSSAOOutput(aoRT);
		if(dbglog_throttle("ssao_comp"))
			dbglog("[PostFX] DrawSSAO composite: raster=%p aoOk=%d",
				g_ssaoOutputRaster, (int)aoOk);
		if(aoRT){ aoRT->Release(); aoRT = NULL; }

		if(aoOk){
			// Set blend through the RW cache FIRST (VERTEXALPHAENABLE /
			// SRCBLEND / DESTBLEND): RwIm2D re-emits cached states at draw
			// time and would clobber raw-only D3D9 factors set before the
			// draw — the old raw-only sequence is how the multiply could
			// silently degrade to a plain replace (showing the AO texture
			// alone / black). Mirror the factors with raw calls immediately
			// before the draw as a second layer.
			RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
			savedStage0Raster = NULL;
			RwRenderStateGet(rwRENDERSTATETEXTURERASTER, (void*)&savedStage0Raster);
			if(savedStage0Raster == (RwRaster*)0xFFFFFFFF)
				savedStage0Raster = NULL;
			stage0Saved = true;
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, g_ssaoOutputRaster);
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDZERO);        // src factor dropped
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDSRCCOLOR);   // dest x src = multiply
			RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
			RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
			RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);

			// White RT-sized quad — NOT colorfilterVerts (emissiveColor=0 would
			// modulate the SSAO texture to black and zero the whole scene).
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);

			// Restore blend defaults through the RW cache too (keep it coherent
			// with the D3D9 state before ImmediateModeRenderStatesReStore)
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
			RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		}else if(dbglog_throttle("ssao_skipped"))
			dbglog("[PostFX] DrawSSAO: multiply composite SKIPPED (AO buffer near-black — fail-open)");

		// Undo the composite's stage-0 TEXTURERASTER bind (ReStore does not
		// cover it): park-and-set back to the pre-composite raster + raw texture.
		if(stage0Saved)
			RestoreSSAOStage0Raster(savedStage0Raster);

		ImmediateModeRenderStatesReStore();
		if(rawGeomSaved){
			RestoreRawGeomStates(rawGeom);
			rawGeomSaved = false;
		}
		dbglog("[PostFX] DrawSSAO completed successfully");
	} __except(EXCEPTION_EXECUTE_HANDLER){
		overrideIm2dPixelShader = nil;	// SEH: the `= nil` reset in the __try above is skipped on fault

		dbglog("[PostFX] DrawSSAO CRASHED exception=0x%08X", GetExceptionCode());
		// COM: the AO output surface was AddRef'd by GetRenderTarget before the
		// draw; the normal path releases it after the readback — release here
		// too so a fault mid-pass cannot leak the reference.
		if(aoRT){ aoRT->Release(); aoRT = NULL; }
		// Fault-exit order: unbind the SSAO samplers (s0-s2) and clear the
		// stage-0 rw raster FIRST — the normal path unbinds before
		// DepthHook_Restore, and restoring the DS while g_ssaoDepthTex is
		// still sampled risks a feedback lock.
		IDirect3DDevice9 *dev = d3d9device;
		if(dev){
			dev->SetTexture(0, NULL);
			dev->SetTexture(1, NULL);
			dev->SetTexture(2, NULL);
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
			if(sampSavedOk){
				for(int st = 0; st < 3; st++){
					dev->SetSamplerState(st, D3DSAMP_ADDRESSU, sampSaved[st][0]);
					dev->SetSamplerState(st, D3DSAMP_ADDRESSV, sampSaved[st][1]);
					dev->SetSamplerState(st, D3DSAMP_ADDRESSW, sampSaved[st][2]);
					dev->SetSamplerState(st, D3DSAMP_MINFILTER, sampSaved[st][3]);
					dev->SetSamplerState(st, D3DSAMP_MAGFILTER, sampSaved[st][4]);
				}
			}
		}
		// Camera-raster fault restore (mirrors the normal-path swap-out): a
		// fault between the swap-in and its paired swap-out leaves Scene.camera
		// rendering into g_ssaoOutputRaster — every later draw this frame and
		// the next frame's scene render land in the AO buffer (black/garbage
		// screen). camInUpdate says whether the paired EndUpdate already ran,
		// so EndUpdate is only called when the camera is genuinely mid-update.
		// Own nested guard: a re-fault here must not skip the state restore below.
		if(camSwapped && origRaster && Scene.camera){
			__try {
				if(camInUpdate)
					RwCameraEndUpdate(Scene.camera);
				RwCameraSetRaster(Scene.camera, origRaster);
				RwCameraBeginUpdate(Scene.camera);
				camSwapped = false;
			} __except(EXCEPTION_EXECUTE_HANDLER){
				if(dbglog_throttle("ssao_camrest"))
					dbglog("[PostFX] DrawSSAO fault-exit camera restore failed code=0x%08X", GetExceptionCode());
			}
		}
		DepthHook_Restore(); // Restore depth hook on bail to keep Suspend/Restore balanced
		// ReStore BEFORE the raw restore (same order as the normal exit): a fault
		// between the composite's rw SRCBLEND=ZERO/DESTBLEND=SRCCOLOR sets and
		// their restore left the rw cache holding the multiply factors — the raw
		// restore alone cannot fix layer (1), and the next RwIm2D draw re-emits
		// the cached ZERO/SRCCOLOR over the world pass. Guarded: a second fault
		// inside the restore must be swallowed, not escape the handler.
		__try {
			if(imStored)
				ImmediateModeRenderStatesReStore();
			if(rawGeomSaved)
				RestoreRawGeomStates(rawGeom);
			if(stage0Saved)
				RestoreSSAOStage0Raster(savedStage0Raster);
		} __except(EXCEPTION_EXECUTE_HANDLER){
			if(dbglog_throttle("ssao_hrest"))
				dbglog("[PostFX] DrawSSAO fault-exit restore failed code=0x%08X", GetExceptionCode());
		}
	}
}

void DrawSSAO_Overhaul(void)
{
	DBGLOG_ENTER("DrawSSAO_Overhaul");
	if(IsGameInMenuOrPaused()) return;
	if(dbglog_throttle("ssao_ov_enter"))
		dbglog("[PostFX] DrawSSAO_Overhaul ENTER");
	if(!config->ssaoEnable || !SSAO_Temporal || !SSAO_BilateralBlur || !SSAO_Upsample){
		// Fallback to old SSAO if new shaders not loaded
		CPostEffects::DrawSSAO();
		return;
	}

	// Declared before __try so the __except path can restore them too
	DWORD rawGeom[9];
	bool rawGeomSaved = false;
	bool imStored = false; // set after ImmediateModeRenderStatesStore
	// Sampler fields this pass raw-overrides (stages 0-6): ADDRESSU/V, ADDRESSW,
	// MINFILTER and MAGFILTER are snapshotted and restored on normal + fault exits.
	DWORD sampSaved[7][5];
	bool sampSavedOk = false;
	// Stage-0 raster captured before the multiply binds g_ssaoOutputRaster;
	// ReStore does not cover TEXTURERASTER, so the pass must restore it itself.
	RwRaster *savedStage0Raster = NULL;
	bool stage0Saved = false;
	// Hoisted for the __except path: the AO output surface (AddRef'd by
	// GetRenderTarget in pass 3) and the camera-raster swap state. A fault
	// between any of this pass's swap-ins and its paired swap-out must not
	// leave Scene.camera rendering into an SSAO RT (black/garbage screen).
	IDirect3DSurface9 *aoRT = NULL;
	RwRaster *origRaster = NULL;
	bool camSwapped = false;	// camera currently points at an SSAO RT
	bool camInUpdate = false;	// ...and is mid-BeginUpdate on it

	__try {
		IDirect3DDevice9 *dev = d3d9device;
		if(dev == NULL){ dbglog("[PostFX] DrawSSAO_Overhaul bailing: dev is NULL"); return; }
		if(Scene.camera == NULL){ dbglog("[PostFX] DrawSSAO_Overhaul bailing: camera is NULL"); return; }
		RwRaster *camRas = RwCameraGetRaster(Scene.camera);
		if(camRas == NULL){ dbglog("[PostFX] DrawSSAO_Overhaul bailing: camRas is NULL"); return; }
		int w = camRas->width;
		int h = camRas->height;

		InitSSAOResources();
		InitSSAOOverhaulResources();

		if(!g_ssaoNoiseTex || !g_ssaoDepthTex){
			dbglog("[PostFX] DrawSSAO_Overhaul bailing: noiseTex=%p depthTex=%p", g_ssaoNoiseTex, g_ssaoDepthTex);
			return;
		}

		if(!g_ssaoQuarterRaster[0] || !g_ssaoQuarterRaster[1]){
			dbglog("[PostFX] DrawSSAO_Overhaul bailing: quarter rasters not ready");
			return;
		}

		if(!g_ssaoOutputRaster || g_ssaoOutputRaster->width != w || g_ssaoOutputRaster->height != h){
			if(g_ssaoOutputRaster) RwRasterDestroy(g_ssaoOutputRaster);
			g_ssaoOutputRaster = RwRasterCreate(w, h, camRas->depth, rwRASTERTYPECAMERATEXTURE);
			if(!g_ssaoOutputRaster){ dbglog("[PostFX] DrawSSAO_Overhaul bailing: output raster creation failed"); return; }
			SSAOAllocReceipt("ssaoOutputRT", w, h);
		}

		// Execution receipt (one-shot + throttled): positive proof the
		// overhaul pass runs with live inputs. The white-clear + fail-open
		// composite design makes any silent draw failure look EXACTLY like
		// "SSAO off" (white AO buffer multiplies by 1.0), so the log must
		// carry execution evidence. "inputs=ok" = every guard above passed;
		// the pointer/flag list exposes a NULL input or a dead history at a
		// glance. Tags: "ssao_ovrun_once" true one-shot (dbglog_once);
		// "ssao_exec" FNV bucket 213, verified collision-free against the
		// project's throttle tags (shared with the fullres path — only one
		// of the two passes runs per frame).
		if(dbglog_once("ssao_ovrun_once") || dbglog_throttle("ssao_exec"))
			dbglog("[SSAO] running %dx%d inputs=ok path=overhaul noise=%p depth=%p norm=%p vel=%p "
				"q0=%p q1=%p hist=%d prevDepth=%d prevNorm=%d blur=%d",
				w, h,
				(void*)g_ssaoNoiseTex, (void*)g_ssaoDepthTex, (void*)g_normalBufferTex,
				(void*)g_velocityTex,
				(void*)g_ssaoQuarterRaster[0], (void*)g_ssaoQuarterRaster[1],
				(int)g_ssaoHistoryValid, (int)g_prevDepthValid, (int)g_normalHistoryValid,
				config->ssaoBlurPasses);

		// Save raw geometry states BEFORE Suspend (it writes raw ZENABLE)
		rawGeomSaved = SaveRawGeomStates(rawGeom);

		// Suspend depth hook so g_ssaoDepthTex can be sampled (INTZ not bound as DS)
		DepthHook_Suspend();

		CPostEffects::ImmediateModeRenderStatesStore();
		imStored = true;
		CPostEffects::ImmediateModeRenderStatesSet();

		origRaster = camRas;
		float radius = config->ssaoRadius > 0.0f ? config->ssaoRadius : 1.0f;
		float power = config->ssaoPower > 0.0f ? config->ssaoPower : 2.0f;
		float noiseScale = 4.0f / w;
		int otherIdx = 1 - g_ssaoFrameIndex;

		// Camera projection info (shared by all passes)
		RwCamera *cam = Scene.camera;
		float n = cam->nearPlane;
		float f = cam->farPlane;
		float projInfo[4] = {
			cam->recipViewWindow.x,
			cam->recipViewWindow.y,
			-n * f / (f - n),
			f / (f - n)
		};

		// =====================================================================
		// Pass 1: SSAO_Temporal — quarter-res temporal SSAO
		// =====================================================================
		RwCameraEndUpdate(Scene.camera);
		RwCameraSetRaster(Scene.camera, g_ssaoQuarterRaster[g_ssaoFrameIndex]);
		RwCameraBeginUpdate(Scene.camera);
		camSwapped = true;
		camInUpdate = true;

		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
		RwD3D9SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		RwD3D9SetRenderState(D3DRS_ZENABLE, FALSE);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

		// Snapshot the sampler fields BEFORE raw-overriding them (stages 0-6;
		// the blur passes reuse stages 0/1, so one entry snapshot covers all).
		for(int st = 0; st < 7; st++){
			dev->GetSamplerState(st, D3DSAMP_ADDRESSU, &sampSaved[st][0]);
			dev->GetSamplerState(st, D3DSAMP_ADDRESSV, &sampSaved[st][1]);
			dev->GetSamplerState(st, D3DSAMP_ADDRESSW, &sampSaved[st][2]);
			dev->GetSamplerState(st, D3DSAMP_MINFILTER, &sampSaved[st][3]);
			dev->GetSamplerState(st, D3DSAMP_MAGFILTER, &sampSaved[st][4]);
		}
		sampSavedOk = true;

		// Textures: s0=depth, s1=noise, s2=normals, s3=velocity, s4=history
		dev->SetTexture(0, g_ssaoDepthTex);
		dev->SetTexture(1, g_ssaoNoiseTex);
		dev->SetTexture(2, g_normalBufferTex); // NULL-safe: D3D9 ignores NULL SetTexture
		dev->SetTexture(3, g_velocityTex);     // NULL-safe: D3D9 ignores NULL SetTexture
		RwD3D9SetTexture(g_ssaoQuarterTexRW[otherIdx], 4);
		// Wave-2: s5=prev-depth, s6=prev-normals (NULL-safe: D3D9 ignores NULL SetTexture)
		dev->SetTexture(5, g_prevDepthValid ? g_prevDepthTex : NULL);
		dev->SetTexture(6, g_normalHistoryValid ? g_prevNormalTex : NULL);

		dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		dev->SetSamplerState(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(3, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(3, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(4, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(4, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		// Wave-2: history samplers POINT/CLAMP. Verified from SSAO_Temporal.hlsl
		// usage (2026-10-17): s5/s6 are SINGLE-TAP samples at the reprojected
		// prevUV (prevDepth :231, prevNormal :255) — depth must never be
		// filtered (an interpolated depth across a silhouette never existed
		// and corrupts the confidence test), and the normal history is a
		// disocclusion-rejection test where POINT keeps edge crispness.
		// NOT LINEAR/CLAMP: a LINEAR depth blend invents phantom depths.
		dev->SetSamplerState(5, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(5, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(5, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		dev->SetSamplerState(5, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		dev->SetSamplerState(6, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(6, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(6, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		dev->SetSamplerState(6, D3DSAMP_MAGFILTER, D3DTEXF_POINT);

		// c0: {radius, power, noiseScale, temporalBlend}
		// temporalBlend is the HISTORY weight (SSAO_Temporal.hlsl lerps toward
		// historyOcclusion by this value), so it only applies when Temporal is
		// ON AND both SSAO history and prev-depth history are valid; otherwise
		// 0.0 = no history. This is the ONLY in-function temporal-accumulation
		// switch (the function never reads ssaoTemporalEnable elsewhere), and
		// the dispatch now routes blurPasses>0 here even with Temporal OFF —
		// gating on the flag keeps OFF = pure current-frame AO, no ghosting.
		float c0Temporal[4] = { radius, power, noiseScale,
			(config->ssaoTemporalEnable && g_ssaoHistoryValid && g_prevDepthValid) ? config->ssaoTemporalBlend : 0.0f };
		RwD3D9SetPixelShaderConstant(0, c0Temporal, 1);
		// c1: {quarterW, quarterH, 1/quarterW, 1/quarterH}
		float c1Temporal[4] = { (float)g_ssaoQuarterW, (float)g_ssaoQuarterH, 1.0f/max((float)g_ssaoQuarterW, 1e-7f), 1.0f/max((float)g_ssaoQuarterH, 1e-7f) };
		RwD3D9SetPixelShaderConstant(1, c1Temporal, 1);
		// c2: projInfo
		RwD3D9SetPixelShaderConstant(2, projInfo, 1);
		// c4: Wave-2 history flags {prevDepthValid, depthBlend, prevNormalValid, 0}
		float c4History[4] = { (float)g_prevDepthValid, 0.05f, (float)g_normalHistoryValid, 0.0f };
		RwD3D9SetPixelShaderConstant(4, c4History, 1);

		// Quarter-res RT needs quarter-sized white quad (colorfilterVerts are a
		// 2048x2048 quad whose UVs would sample only a corner of the depth map).
		SetupFullscreenQuad((float)g_ssaoQuarterW, (float)g_ssaoQuarterH);

		overrideIm2dPixelShader = SSAO_Temporal;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
		overrideIm2dPixelShader = nil;

		RwCameraEndUpdate(Scene.camera);
		camInUpdate = false;
		RwCameraSetRaster(Scene.camera, origRaster);
		RwCameraBeginUpdate(Scene.camera);
		camSwapped = false;

		// =====================================================================
		// Pass 2: SSAO_BilateralBlur — horizontal + vertical blur passes
		// =====================================================================
		for(int pass = 0; pass < config->ssaoBlurPasses; pass++){
			// Horizontal blur: render to blur temp, read from current quarter
			RwCameraEndUpdate(Scene.camera);
			RwCameraSetRaster(Scene.camera, g_ssaoBlurTempRaster);
			RwCameraBeginUpdate(Scene.camera);
			camSwapped = true;
			camInUpdate = true;

			RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
			RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
			RwD3D9SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
			RwD3D9SetRenderState(D3DRS_ZENABLE, FALSE);
			RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

			// s0: source, s1: depth
			RwD3D9SetTexture(g_ssaoQuarterTexRW[g_ssaoFrameIndex], 0);
			dev->SetTexture(1, g_ssaoDepthTex);
			dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

			// c0: {blurRadius, depthThreshold, 0, 0}  (0 = horizontal pass)
			float c0BlurH[4] = { config->ssaoBlurRadius, config->ssaoDepthThreshold, 0.0f, 0.0f };
			RwD3D9SetPixelShaderConstant(0, c0BlurH, 1);
			// c1: {quarterW, quarterH, 1/quarterW, 1/quarterH}
			float c1BlurH[4] = { (float)g_ssaoQuarterW, (float)g_ssaoQuarterH, 1.0f/max((float)g_ssaoQuarterW, 1e-7f), 1.0f/max((float)g_ssaoQuarterH, 1e-7f) };
			RwD3D9SetPixelShaderConstant(1, c1BlurH, 1);
			// c2: projInfo
			RwD3D9SetPixelShaderConstant(2, projInfo, 1);

			SetupFullscreenQuad((float)g_ssaoQuarterW, (float)g_ssaoQuarterH);
			overrideIm2dPixelShader = SSAO_BilateralBlur;
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
			overrideIm2dPixelShader = nil;

			RwCameraEndUpdate(Scene.camera);
			camInUpdate = false;
			RwCameraSetRaster(Scene.camera, origRaster);
			RwCameraBeginUpdate(Scene.camera);
			camSwapped = false;

			// Vertical blur: render to quarter frame, read from blur temp
			RwCameraEndUpdate(Scene.camera);
			RwCameraSetRaster(Scene.camera, g_ssaoQuarterRaster[g_ssaoFrameIndex]);
			RwCameraBeginUpdate(Scene.camera);
			camSwapped = true;
			camInUpdate = true;

			RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
			RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
			RwD3D9SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
			RwD3D9SetRenderState(D3DRS_ZENABLE, FALSE);
			RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

			// s0: source (blur temp), s1: depth
			RwD3D9SetTexture(g_ssaoBlurTempTexRW, 0);
			dev->SetTexture(1, g_ssaoDepthTex);
			dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

			// c0: {blurRadius, depthThreshold, 1, 0}  (1 = vertical pass flag)
			float c0BlurV[4] = { config->ssaoBlurRadius, config->ssaoDepthThreshold, 1.0f, 0.0f };
			RwD3D9SetPixelShaderConstant(0, c0BlurV, 1);
			// c1: {quarterW, quarterH, 1/quarterW, 1/quarterH}
			float c1BlurV[4] = { (float)g_ssaoQuarterW, (float)g_ssaoQuarterH, 1.0f/max((float)g_ssaoQuarterW, 1e-7f), 1.0f/max((float)g_ssaoQuarterH, 1e-7f) };
			RwD3D9SetPixelShaderConstant(1, c1BlurV, 1);
			// c2: projInfo
			RwD3D9SetPixelShaderConstant(2, projInfo, 1);

			SetupFullscreenQuad((float)g_ssaoQuarterW, (float)g_ssaoQuarterH);
			overrideIm2dPixelShader = SSAO_BilateralBlur;
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
			overrideIm2dPixelShader = nil;

			RwCameraEndUpdate(Scene.camera);
			camInUpdate = false;
			RwCameraSetRaster(Scene.camera, origRaster);
			RwCameraBeginUpdate(Scene.camera);
			camSwapped = false;
		}

		// =====================================================================
		// Pass 3: SSAO_Upsample — quarter-res → full-res
		// =====================================================================
		RwCameraEndUpdate(Scene.camera);
		RwCameraSetRaster(Scene.camera, g_ssaoOutputRaster);
		RwCameraBeginUpdate(Scene.camera);
		camSwapped = true;
		camInUpdate = true;

		// White-clear output so a failed upsample degrades to "no AO", never
		// black garbage through the multiply composite.
		dev->Clear(0, NULL, D3DCLEAR_TARGET, 0xFFFFFFFF, 1.0f, 0);

		// Capture the AO output surface while bound for the post-pass readback
		// validation (fail-open gate on the multiply composite below).
		// aoRT is the hoisted fault-path slot — released in the handler too.
		dev->GetRenderTarget(0, &aoRT);

		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
		RwD3D9SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		RwD3D9SetRenderState(D3DRS_ZENABLE, FALSE);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

		// s0: final V-pass result (quarter frame), s1: depth
		RwD3D9SetTexture(g_ssaoQuarterTexRW[g_ssaoFrameIndex], 0);
		dev->SetTexture(1, g_ssaoDepthTex);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

		// c0: {depthThreshold, 0, 0, 0}
		float c0Up[4] = { config->ssaoDepthThreshold, 0.0f, 0.0f, 0.0f };
		RwD3D9SetPixelShaderConstant(0, c0Up, 1);
		// c1: {fullW, fullH, 1/fullW, 1/fullH}
		float c1Up[4] = { (float)w, (float)h, 1.0f/max((float)w, 1e-7f), 1.0f/max((float)h, 1e-7f) };
		RwD3D9SetPixelShaderConstant(1, c1Up, 1);
		// c2: {quarterW, quarterH, 1/quarterW, 1/quarterH}
		float c2Up[4] = { (float)g_ssaoQuarterW, (float)g_ssaoQuarterH, 1.0f/max((float)g_ssaoQuarterW, 1e-7f), 1.0f/max((float)g_ssaoQuarterH, 1e-7f) };
		RwD3D9SetPixelShaderConstant(2, c2Up, 1);

		// Full-res RT: white quad sized to the camera raster (same UV fix as
		// the temporal pass; composite below reuses it).
		SetupFullscreenQuad((float)w, (float)h);

		overrideIm2dPixelShader = SSAO_Upsample;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
		overrideIm2dPixelShader = nil;

		RwCameraEndUpdate(Scene.camera);
		camInUpdate = false;
		RwCameraSetRaster(Scene.camera, origRaster);
		RwCameraBeginUpdate(Scene.camera);
		camSwapped = false;

		// Wave-2: unbind all Pass 1 samplers (s0-s6) so the depth texture and
		// history textures don't leak past DepthHook_Restore (feedback lock risk).
		// The raw sampler overrides (ADDRESS*/MIN/MAG) this pass applied across
		// stages 0-6 are restored from the entry snapshot immediately below.
		dev->SetTexture(0, NULL);
		dev->SetTexture(1, NULL);
		dev->SetTexture(2, NULL);
		dev->SetTexture(3, NULL);
		dev->SetTexture(4, NULL);
		dev->SetTexture(5, NULL);
		dev->SetTexture(6, NULL);

		// Restore the sampler fields this pass overrode (normal exit path).
		if(sampSavedOk){
			for(int st = 0; st < 7; st++){
				dev->SetSamplerState(st, D3DSAMP_ADDRESSU, sampSaved[st][0]);
				dev->SetSamplerState(st, D3DSAMP_ADDRESSV, sampSaved[st][1]);
				dev->SetSamplerState(st, D3DSAMP_ADDRESSW, sampSaved[st][2]);
				dev->SetSamplerState(st, D3DSAMP_MINFILTER, sampSaved[st][3]);
				dev->SetSamplerState(st, D3DSAMP_MAGFILTER, sampSaved[st][4]);
			}
		}

		// Restore depth hook (re-binds INTZ as DS, re-enables Z)
		DepthHook_Restore();

		// =====================================================================
		// Composite: multiply blend SSAO onto scene (Photoshop multiply:
		// final = scene x ao, white = no occlusion). Fail-open on a
		// near-black AO readback — see ValidateSSAOOutput.
		// =====================================================================
		bool aoOk = ValidateSSAOOutput(aoRT);
		if(dbglog_throttle("ssao_ov_comp"))
			dbglog("[PostFX] DrawSSAO_Overhaul composite: raster=%p aoOk=%d",
				g_ssaoOutputRaster, (int)aoOk);
		if(aoRT){ aoRT->Release(); aoRT = NULL; }

		if(aoOk){
			// Blend factors through the RW cache FIRST (RwIm2D re-emits cached
			// states at draw time and would clobber raw-only D3D9 factors),
			// then mirror them raw immediately before the draw — same contract
			// as DrawSSAO's composite.
			RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
			savedStage0Raster = NULL;
			RwRenderStateGet(rwRENDERSTATETEXTURERASTER, (void*)&savedStage0Raster);
			if(savedStage0Raster == (RwRaster*)0xFFFFFFFF)
				savedStage0Raster = NULL;
			stage0Saved = true;
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, g_ssaoOutputRaster);
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDZERO);        // src factor dropped
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDSRCCOLOR);   // dest x src = multiply
			RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
			RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
			RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);

			// White RT-sized quad — NOT colorfilterVerts (their emissiveColor is 0
			// under COLORFILTER_MODERN, which modulates SSAO to black).
			RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);

			// Restore blend defaults through the RW cache (keep it coherent
			// with the D3D9 state before ImmediateModeRenderStatesReStore)
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
			RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		}else if(dbglog_throttle("ssao_ov_skip"))
			dbglog("[PostFX] DrawSSAO_Overhaul: multiply composite SKIPPED (AO buffer near-black — fail-open)");

		// Undo the composite's stage-0 TEXTURERASTER bind (ReStore does not
		// cover it): park-and-set back to the pre-composite raster + raw texture.
		if(stage0Saved)
			RestoreSSAOStage0Raster(savedStage0Raster);

		CPostEffects::ImmediateModeRenderStatesReStore();
		if(rawGeomSaved){
			RestoreRawGeomStates(rawGeom);
			rawGeomSaved = false;
		}

		// Swap frame index and mark history valid
		g_ssaoFrameIndex = otherIdx;
		g_ssaoHistoryValid = true;

		if(dbglog_throttle("ssao_ov_done"))
			dbglog("[PostFX] DrawSSAO_Overhaul completed successfully");
		// Debug dump: after SSAO composite
		DumpCurrentRT("after_ssao");
	} __except(EXCEPTION_EXECUTE_HANDLER){
		overrideIm2dPixelShader = nil;	// SEH: the `= nil` reset in the __try above is skipped on fault

		dbglog("[PostFX] DrawSSAO_Overhaul CRASHED exception=0x%08X", GetExceptionCode());
		// COM: the AO output surface was AddRef'd by GetRenderTarget in pass 3;
		// the normal path releases it after the readback — release here too so
		// a fault mid-pass cannot leak the reference.
		if(aoRT){ aoRT->Release(); aoRT = NULL; }
		// Fault-exit order: unbind s0-s6 and clear the stage-0 rw raster FIRST
		// (the normal path unbinds before DepthHook_Restore) so the re-bound
		// INTZ DS can never coincide with the depth texture still sampled.
		IDirect3DDevice9 *dev = d3d9device;
		if(dev){
			dev->SetTexture(0, NULL);
			dev->SetTexture(1, NULL);
			dev->SetTexture(2, NULL);
			dev->SetTexture(3, NULL);
			dev->SetTexture(4, NULL);
			dev->SetTexture(5, NULL);
			dev->SetTexture(6, NULL);
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
			if(sampSavedOk){
				for(int st = 0; st < 7; st++){
					dev->SetSamplerState(st, D3DSAMP_ADDRESSU, sampSaved[st][0]);
					dev->SetSamplerState(st, D3DSAMP_ADDRESSV, sampSaved[st][1]);
					dev->SetSamplerState(st, D3DSAMP_ADDRESSW, sampSaved[st][2]);
					dev->SetSamplerState(st, D3DSAMP_MINFILTER, sampSaved[st][3]);
					dev->SetSamplerState(st, D3DSAMP_MAGFILTER, sampSaved[st][4]);
				}
			}
		}
		// Camera-raster fault restore (mirrors the normal-path swap-outs): a
		// fault between any of this pass's swap-ins and its paired swap-out
		// leaves Scene.camera rendering into an SSAO RT — every later draw this
		// frame and the next frame's scene render land in that buffer (black/
		// garbage screen). camInUpdate says whether the paired EndUpdate already
		// ran, so EndUpdate is only called when the camera is genuinely
		// mid-update. Own nested guard: a re-fault here must not skip the state
		// restore below.
		if(camSwapped && origRaster && Scene.camera){
			__try {
				if(camInUpdate)
					RwCameraEndUpdate(Scene.camera);
				RwCameraSetRaster(Scene.camera, origRaster);
				RwCameraBeginUpdate(Scene.camera);
				camSwapped = false;
			} __except(EXCEPTION_EXECUTE_HANDLER){
				if(dbglog_throttle("ssao_ov_camrest"))
					dbglog("[PostFX] DrawSSAO_Overhaul fault-exit camera restore failed code=0x%08X", GetExceptionCode());
			}
		}
		DepthHook_Restore(); // Restore depth hook on bail to keep Suspend/Restore balanced
		// Same three-layer contract gap as DrawSSAO: the rw cache can be left
		// holding the multiply-composite factors (SRCBLEND=ZERO/DESTBLEND=
		// SRCCOLOR) if the fault lands between the composite and its restore —
		// ReStore layer (1) first, then the raw snapshot. Guarded: a second
		// fault inside the restore must be swallowed, not escape the handler.
		__try {
			if(imStored)
				CPostEffects::ImmediateModeRenderStatesReStore();
			if(rawGeomSaved)
				RestoreRawGeomStates(rawGeom);
			if(stage0Saved)
				RestoreSSAOStage0Raster(savedStage0Raster);
		} __except(EXCEPTION_EXECUTE_HANDLER){
			if(dbglog_throttle("ssao_ov_hrest"))
				dbglog("[PostFX] DrawSSAO_Overhaul fault-exit restore failed code=0x%08X", GetExceptionCode());
		}
	}
}

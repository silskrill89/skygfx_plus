#include "skygfx.h"
#include "neo.h"
#include <DirectXMath.h>

extern int renderingWheel;

enum {
	LOC_combined    = 0,
	LOC_world       = 4,
	LOC_tex         = 8,
	LOC_eye         = 12,
	LOC_directDir   = 13,
	LOC_ambient     = 15,
	LOC_matCol      = 16,
	LOC_directCol   = 17,
	LOC_lightDir    = 18,
	LOC_lightCol    = 24,

	LOC_directSpec  = 30,	// for carpipe
	LOC_reflProps   = 31,
	LOC_surfProps	= 32,
};

//#define DEBUGTEX

WRAPPER void CRenderer__RenderRoads(void) { EAXJMP(0x553A10); }
WRAPPER void CRenderer__RenderEverythingBarRoads(void) { EAXJMP(0x553AA0); }
WRAPPER void CRenderer__RenderFadingInEntities(void) { EAXJMP(0x5531E0); }
WRAPPER void CRenderer__RenderFadingInUnderwaterEntities(void) { EAXJMP(0x553220); }

short &skyTopRed = *(short*)0xB7C4C4;
short &skyTopGreen = *(short*)0xB7C4C6;
short &skyTopBlue = *(short*)0xB7C4C8;
short &skyBotRed = *(short*)0xB7C4CA;
short &skyBotGreen = *(short*)0xB7C4CC;
short &skyBotBlue = *(short*)0xB7C4CE;


InterpolatedFloat CarPipe::fresnel(0.4f);
InterpolatedFloat CarPipe::power(18.0f);
InterpolatedLight CarPipe::diffColor(Color(0.0f, 0.0f, 0.0f, 0.0f));
InterpolatedLight CarPipe::specColor(Color(0.7f, 0.7f, 0.7f, 1.0f));
void *CarPipe::vertexShaderPass1;
void *CarPipe::vertexShaderPass2;
// reflection map
RwTexture *CarPipe::reflectionMask;
RwIm2DVertex CarPipe::screenQuad[4];
RwImVertexIndex CarPipe::screenindices[6] = { 0, 1, 2, 0, 2, 3 };

CarPipe carpipe;

void
neoCarPipeInit(void)
{
	ONCE;
	carpipe.Init();
}

//
// Reflection map
//

void
CarPipe::MakeQuadTexCoords(bool textureSpace)
{
	float minU, minV, maxU, maxV;
	if(textureSpace){
		minU = minV = 0.0f;
		maxU = maxV = 1.0f;
	}else{
		assert(0 && "not implemented");
	}
	screenQuad[0].u = minU;
	screenQuad[0].v = minV;
	screenQuad[1].u = minU;
	screenQuad[1].v = maxV;
	screenQuad[2].u = maxU;
	screenQuad[2].v = maxV;
	screenQuad[3].u = maxU;
	screenQuad[3].v = minV;
}

void
CarPipe::MakeScreenQuad(void)
{
	if(!reflectionCam) return;
	int width = reflectionTex->raster->width;
	int height = reflectionTex->raster->height;
	screenQuad[0].x = 0.0f;
	screenQuad[0].y = 0.0f;
	screenQuad[0].z = RwIm2DGetNearScreenZ();
	screenQuad[0].rhw = 1.0f / reflectionCam->nearPlane;
	screenQuad[0].emissiveColor = 0xFFFFFFFF;
	screenQuad[1].x = 0.0f;
	screenQuad[1].y = height;
	screenQuad[1].z = screenQuad[0].z;
	screenQuad[1].rhw = screenQuad[0].rhw;
	screenQuad[1].emissiveColor = 0xFFFFFFFF;
	screenQuad[2].x = width;
	screenQuad[2].y = height;
	screenQuad[2].z = screenQuad[0].z;
	screenQuad[2].rhw = screenQuad[0].rhw;
	screenQuad[2].emissiveColor = 0xFFFFFFFF;
	screenQuad[3].x = width;
	screenQuad[3].y = 0;
	screenQuad[3].z = screenQuad[0].z;
	screenQuad[3].rhw = screenQuad[0].rhw;
	screenQuad[3].emissiveColor = 0xFFFFFFFF;
	MakeQuadTexCoords(true);
}

// ============================================================
// FIX E — three-layer render-state snapshots for CarPipe's env-map
// pass (RenderReflectionScene + RenderEnvTex).
//
// Layers (see the pipeEnterCullMode block comment in
// pipelinecommon.cpp): (1) rw cache RwRenderStateGet/Set, (2) RW D3D9
// driver PENDING cache RwD3D9Get/SetRenderState (flushed pre-draw by
// _rwD3D9RenderStateFlushCache, applied[]-gated), (3) raw
// IDirect3DDevice9 state. The pre-fix code touched these states
// through layer (1) only and restored LITERALS
// (SRCALPHA/INVSRCALPHA/VERTEXALPHA=0) or nothing at all (fog,
// TEXTURERASTER) — a textbook three-layer desync: a caller running
// any other blend inherited the literals, and when the rw cache
// already held a literal the RwRenderStateSet no-ops, so layers
// (2)/(3) kept the mask-pass values forever.
//
// Save-before/restore-after below: every layer gets its OWN saved
// value, so the pass is invisible even when the layers were already
// out of sync on entry. All helpers are POD-only (C2712-safe).
// ============================================================
struct CarFogState3L {
	RwUInt32 rw;    // rwRENDERSTATEFOGENABLE (layer 1)
	RwUInt32 drv;   // D3DRS_FOGENABLE pending (layer 2)
	RwUInt32 dev;   // D3DRS_FOGENABLE device  (layer 3)
};

static void
carFogSave3L(CarFogState3L *f)
{
	// Pre-seeded (never bare): a failed Get leaves the out-param
	// untouched, so garbage must never reach a later restore. TRUE is
	// the game default; faithful capture is what matters here.
	f->rw = TRUE; f->drv = TRUE; f->dev = TRUE;
	RwRenderStateGet(rwRENDERSTATEFOGENABLE, &f->rw);
	RwD3D9GetRenderState(D3DRS_FOGENABLE, &f->drv);
	if(d3d9device){
		DWORD v = f->dev;
		d3d9device->GetRenderState(D3DRS_FOGENABLE, &v); f->dev = v;
	}
}

static void
carFogDisable3L(void)
{
	// Deterministic entry: force all three layers OFF — a plain
	// RwRenderStateSet no-ops when the cache already holds 0 and can
	// never repair a device-side drift.
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, 0);
	RwD3D9SetRenderState(D3DRS_FOGENABLE, FALSE);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_FOGENABLE, FALSE);
}

static void
carFogRestore3L(const CarFogState3L *f)
{
	// rw first (so nested/consumer Gets see the restore), then driver
	// pending, then raw device — same direction as pipeExitAlphaMode;
	// the raw write is what closes the gap when a layer already held
	// the saved value (cache-only restore cannot).
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)f->rw);
	RwD3D9SetRenderState(D3DRS_FOGENABLE, f->drv ? TRUE : FALSE);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_FOGENABLE, f->dev ? TRUE : FALSE);
}

struct CarBlendState3L {
	// layer 1 — rw cache
	RwUInt32 vtxRw, srcRw, dstRw;
	RwRaster *rasRw;
	// layer 2 — driver pending
	RwUInt32 vtxDrv, srcDrv, dstDrv;
	// layer 3 — device
	RwUInt32 vtxDev, srcDev, dstDev;
};

// D3D-domain blend factors span D3DBLEND_ZERO(1)..D3DBLEND_INVSRCCOLOR2(17);
// 0 and out-of-range are failed-Get / foreign-raw-write garbage and clamp
// to the caller-named fallback (same spirit as pipeBlendToD3D, but the
// device layers may legitimately hold 14..17 — chars.cpp SSS uses
// D3DBLEND_BLENDFACTOR — so the bound is the D3D range, not the rw one).
static RwUInt32
carBlendClampDev(RwUInt32 v, RwUInt32 fallback)
{
	if(v < 1 || v > 17)
		return fallback;
	return v;
}

static void
carBlendSave3L(CarBlendState3L *s)
{
	// Pre-seeded with the world-canonical trio (TagRenderCB /
	// pipeEnterAlphaMode baseline) so a failed Get can never leak
	// garbage into the restore below.
	s->vtxRw = FALSE; s->srcRw = rwBLENDSRCALPHA; s->dstRw = rwBLENDINVSRCALPHA;
	s->rasRw = NULL;
	s->vtxDrv = FALSE; s->srcDrv = D3DBLEND_SRCALPHA; s->dstDrv = D3DBLEND_INVSRCALPHA;
	s->vtxDev = FALSE; s->srcDev = D3DBLEND_SRCALPHA; s->dstDev = D3DBLEND_INVSRCALPHA;

	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &s->vtxRw);
	RwRenderStateGet(rwRENDERSTATESRCBLEND, &s->srcRw);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, &s->dstRw);
	RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &s->rasRw);

	RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE, &s->vtxDrv);
	RwD3D9GetRenderState(D3DRS_SRCBLEND, &s->srcDrv);
	RwD3D9GetRenderState(D3DRS_DESTBLEND, &s->dstDrv);

	if(d3d9device){
		DWORD v;
		v = s->vtxDev; d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &v); s->vtxDev = v;
		v = s->srcDev; d3d9device->GetRenderState(D3DRS_SRCBLEND, &v);         s->srcDev = v;
		v = s->dstDev; d3d9device->GetRenderState(D3DRS_DESTBLEND, &v);        s->dstDev = v;
	}
}

static void
carBlendRestore3L(const CarBlendState3L *s)
{
	// rw cache first, then driver pending, then raw device — each layer
	// gets its OWN saved value. This replaces the old hardcoded
	// SRCALPHA/INVSRCALPHA/VERTEXALPHA=0 literals (layer-1-only), which
	// (a) overwrote any caller that legitimately ran a different blend
	// and (b) double-no-op'd when the cache already held the literals,
	// permanently stranding layers (2)/(3) on the mask-pass values.
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)s->vtxRw);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)s->srcRw);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)s->dstRw);
	// TEXTURERASTER: the mask pass left the cache bound to
	// reflectionMask->raster, so this Set is a real cache transition
	// (unless the caller had the very same raster bound — then every
	// layer already agrees and the no-op is correct) and RW's own flush
	// re-binds the driver/device stage-0 texture from the restored
	// raster. No raw IDirect3DBaseTexture9 poke: AddRef discipline on a
	// borrowed COM pointer would be the wrong tool here.
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)s->rasRw);

	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, s->vtxDrv ? TRUE : FALSE);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, carBlendClampDev(s->srcDrv, D3DBLEND_SRCALPHA));
	RwD3D9SetRenderState(D3DRS_DESTBLEND, carBlendClampDev(s->dstDrv, D3DBLEND_INVSRCALPHA));
	if(d3d9device){
		d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, s->vtxDev ? TRUE : FALSE);
		d3d9device->SetRenderState(D3DRS_SRCBLEND, carBlendClampDev(s->srcDev, D3DBLEND_SRCALPHA));
		d3d9device->SetRenderState(D3DRS_DESTBLEND, carBlendClampDev(s->dstDev, D3DBLEND_INVSRCALPHA));
	}
}

void
CarPipe::RenderReflectionScene(void)
{
	// FIX E (fog leak, old :115): the original set FOGENABLE=0 through
	// the rw cache only and NEVER restored it — fog-off leaked into the
	// cache for everything rendered after this call, on BOTH callers
	// (CarPipe::RenderEnvTex below AND envmap.cpp
	// RenderSphereReflections:641, the live CAR_MOBILE/CAR_ENV/CAR_MODERN
	// path). Now: save all three layers, disable on all three layers,
	// restore unconditionally. The restore sits OUTSIDE the __try so a
	// fault inside the game scene renderers cannot skip it
	// (C2712-safe: the only local is the POD snapshot struct).
	CarFogState3L fog;
	carFogSave3L(&fog);
	carFogDisable3L();
	__try {
		CRenderer__RenderRoads();
		CRenderer__RenderEverythingBarRoads();
		CRenderer__RenderFadingInEntities();
	} __except(EXCEPTION_EXECUTE_HANDLER){
		if(dbglog_throttle("envtex_scene"))
			dbglog("[EnvTex] RenderReflectionScene FAULT code=0x%08X — env map partial, fog restored by guard",
				GetExceptionCode());
	}
	carFogRestore3L(&fog);
}

// File-level cache static (promoted from function-static so it can be nulled on device reset)
static RwMatrix *reflectionMatrix = NULL;

void
CarPipe::RenderEnvTex(void)
{
	if(!Scene.camera || !reflectionCam) return;
	// FIX 1: resolve the camera frame BEFORE RwCameraEndUpdate. The two
	// early-outs below used to run AFTER the EndUpdate and returned without
	// the paired RwCameraBeginUpdate, leaving the main camera in EndUpdate
	// state — the raster lifecycle broke on those paths and a non-screen
	// raster could stay live on Scene.camera into the effects pass
	// ([FxAlpha] Coronas.Reflections camRas=2048x1024 != screen).
	RwFrame *camFrame = RwCameraGetFrame(Scene.camera);
	if(!camFrame) return;

	RwCameraEndUpdate(Scene.camera);

	MakeEnvmapRasters();

	// MakeEnvmapRasters can fail and leave the reflection camera's rasters
	// NULL (first-ever creation failure); clearing/drawing into it would
	// fault. Bail — but re-pair with RwCameraBeginUpdate first, since we
	// are past the EndUpdate above.
	if(!RwCameraGetRaster(reflectionCam) || !RwCameraGetZRaster(reflectionCam)){
		RwCameraBeginUpdate(Scene.camera);
		return;
	}

	RwV2d oldvw, vw = { 2.0f, 2.0f };
	oldvw = reflectionCam->viewWindow;
	RwCameraSetViewWindow(reflectionCam, &vw);

	if(reflectionMatrix == NULL){
		reflectionMatrix = RwMatrixCreate();
		reflectionMatrix->right.x = -1.0f;
		reflectionMatrix->right.y = 0.0f;
		reflectionMatrix->right.z = 0.0f;
		reflectionMatrix->up.x = 0.0f;
		reflectionMatrix->up.y = -1.0f;
		reflectionMatrix->up.z = 0.0f;
		reflectionMatrix->at.x = 0.0f;
		reflectionMatrix->at.y = 0.0f;
		reflectionMatrix->at.z = 1.0f;
	}
	// FIX 1 (cont.): the old `if(!Scene.camera) return;` / `if(!camFrame)
	// return;` pair lived here — AFTER the EndUpdate with no BeginUpdate
	// re-pair. Both conditions are now resolved before the EndUpdate
	// (Scene.camera is single-thread-stable across this span), so the
	// unpaired-return paths no longer exist. Every remaining exit after
	// RwCameraEndUpdate(Scene.camera) flows through the paired
	// RwCameraBeginUpdate(Scene.camera) at the bottom of the function.
	RwMatrix *cammatrix = RwFrameGetMatrix(camFrame);
	reflectionMatrix->pos = cammatrix->pos;
	RwMatrixUpdate(reflectionMatrix);
	RwFrameTransform(RwCameraGetFrame(reflectionCam), reflectionMatrix, rwCOMBINEREPLACE);
	RwRGBA color = { skyBotRed, skyBotGreen, skyBotBlue, 255 };
	// blend a bit of white into the sky color, otherwise it tends to be very blue
	color.red = color.red*0.6f + 255*0.4f;
	color.green = color.green*0.6f + 255*0.4f;
	color.blue = color.blue*0.6f + 255*0.4f;
	RwCameraClear(reflectionCam, &color, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);

	RwCameraBeginUpdate(reflectionCam);
	RwCamera *savedcam = Scene.camera;
	Scene.camera = reflectionCam;	// they do some begin/end updates with this in the called functions :/

	// FIX E (breadcrumb): proves whether CAR_NEO's RenderEnvTex ran
	// during a repro and which raster the swap points Scene.camera at.
	// This pass swaps the whole camera POINTER (reflectionCam's own
	// rasters go live), unlike envmap.cpp's SphereReflectionGuard which
	// swaps the MAIN camera's rasters — so this line makes the
	// "[FxAlpha] Coronas.Reflections camRas 2048x1024" reports
	// attributable to one of the two.
	if(dbglog_throttle("envtex")){
		RwRaster *xr = RwCameraGetRaster(reflectionCam);
		dbglog("[EnvTex] CAR_NEO swap: Scene.camera->reflectionCam=%p camRas=%p %dx%d (screen %dx%d)",
			(void*)reflectionCam, (void*)xr, xr ? xr->width : 0, xr ? xr->height : 0,
			RsGlobal ? (int)RsGlobal->MaximumWidth : 0, RsGlobal ? (int)RsGlobal->MaximumHeight : 0);
	}

	// FIX 1: Qualify to CarPipe::RenderReflectionScene() so the env map includes
	// vehicles (RenderEverythingBarRoads) + fading entities, not just roads+buildings.
	// The unqualified call resolved to the GLOBAL RenderReflectionScene() in envmap.cpp
	// which only renders roads+buildings → no vehicle reflections.
	// Match envmap.cpp:524-536: while envFB is being rendered into,
	// reflectionTex's raster IS the current render target (MakeEnvmapRasters
	// attaches both to envFB) — scene pipes must not sample it or D3D9 hits
	// a feedback loop. gRenderingSpheremap is the flag those pipes check
	// (vehiclePipe.cpp Env cb ~1657/1928, buildingPipe.cpp ~885, postfx ~4378).
	//
	// FIX E (outer fog guard): the original FOGENABLE=0 leaked out of
	// RenderReflectionScene all the way past the mask composite below —
	// the mask pass has ALWAYS run fog-disabled. To keep that visual
	// bit-exact, the whole span (scene capture + mask draw) stays
	// fog-off here; RenderReflectionScene's inner save/restore sees 0
	// and re-disables (no-op transition, not a re-enable). The caller's
	// saved fog is restored at the single exit point below, on every
	// path including faults.
	CarFogState3L fogOuter;
	carFogSave3L(&fogOuter);
	carFogDisable3L();

	// FIX E (swap restore on EVERY exit path incl. faults): the inner
	// guard in RenderReflectionScene contains the game scene renderers,
	// but MakeScreenQuad (unguarded reflectionTex deref) and a fault
	// inside that function's __except handler still need this last-line
	// guard. C2712-safe: every local in this function is POD (no
	// destructible objects across the __try).
	__try {
		gRenderingSpheremap = true;
		CarPipe::RenderReflectionScene();
		Scene.camera = savedcam;
		MakeScreenQuad();
	} __except(EXCEPTION_EXECUTE_HANDLER){
		if(dbglog_throttle("envtex_fault"))
			dbglog("[EnvTex] guarded span FAULT code=0x%08X — unswapping Scene.camera, fog restored",
				GetExceptionCode());
	}
	// Unconditional un-swap: idempotent on the normal path, the rescue
	// on the fault path — Scene.camera can never leave this region on
	// reflectionCam and gRenderingSpheremap can never leak true.
	gRenderingSpheremap = false;
	Scene.camera = savedcam;

	// FIX E (three-layer blend/raster save-before-restore-after around
	// the mask composite — see carBlendSave3L/carBlendRestore3L above).
	CarBlendState3L blend;
	carBlendSave3L(&blend);
	if(reflectionMask && reflectionMask->raster){
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)1);
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDZERO);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDSRCCOLOR);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, reflectionMask->raster);
		// Guarded Im2D: NULL ps is the same NULL override
		// Im2dSetPixelShader_hook already applies today (override = nil
		// at this point in the frame), so behavior is unchanged — but a
		// fault inside the game's Im2D dispatch is contained and the
		// restores below still run.
		guardedIm2DRender(NULL, rwPRIMTYPETRILIST, screenQuad, 4, screenindices, 6, "envtex_mask");
	}else if(dbglog_throttle("envtex_mask")){
		dbglog("[EnvTex] reflectionMask/raster NULL — mask composite skipped (env tex = raw spheremap), state restored");
	}
	// Restores the ACTUAL pre-pass values on all three layers (blend
	// trio + TEXTURERASTER + fog), replacing the old hardcoded
	// SRCALPHA/INVSRCALPHA/VERTEXALPHA=0 literals and the never-restored
	// fog/raster bindings.
	carBlendRestore3L(&blend);
	carFogRestore3L(&fogOuter);

	RwCameraEndUpdate(reflectionCam);
	RwCameraSetViewWindow(reflectionCam, &oldvw);

	RwCameraBeginUpdate(Scene.camera);
#ifdef DEBUGTEX
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, reflectionTex->raster);
	RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, screenQuad, 4, screenindices, 6);
#endif
}

//
//
//

CarPipe::CarPipe(void)
{
//	rwPipeline = NULL;
}

void
CarPipe::Init(void)
{
//	CreateRwPipeline();
//	SetRenderCallback(RenderCallback);
	CreateShaders();
	LoadTweakingTable();
	reflectionMask = RwTextureRead("CarReflectionMask", NULL);
	// some camera begin/end stuff in barroads with the rw cam
//	Nop(0x553C68, 0x553C9F-0x553C68);
//	Nop(0x553CA4, 3);
//	Nop(0x553CCA, 0x553CF4-0x553CCA);
}

void
CarPipe::CreateShaders(void)
{
	// Neo vehicle shaders removed - using unified PBR pipeline
	vertexShaderPass1 = nullptr;
	vertexShaderPass2 = nullptr;
}

void
CarPipe::LoadTweakingTable(void)
{
	char *path;
	FILE *dat;
	path = getpath("neo\\carTweakingTable.dat");
	if(path == NULL)
		return;
	dat = fopen(path, "r");
	neoReadWeatherTimeBlock(dat, &fresnel);
	neoReadWeatherTimeBlock(dat, &power);
	neoReadWeatherTimeBlock(dat, &diffColor);
	neoReadWeatherTimeBlock(dat, &specColor);
	fclose(dat);
	iCanHasNeoCar = 1;
}

//
// Rendering
//

void
UploadLightColorWithSpecular(RpLight *light, int loc)
{
	float c[4];
	if(RpLightGetFlags(light) & rpLIGHTLIGHTATOMICS){
		Color s = CarPipe::specColor.Get();
		c[0] = light->color.red * s.a;
		c[1] = light->color.green * s.a;
		c[2] = light->color.blue * s.a;
		c[3] = 1.0f;
		RwD3D9SetVertexShaderConstant(loc, (void*)c, 1);
	}else
		pipeUploadZero(loc);
}

void
CarPipe::ShaderSetup(RpAtomic *atomic)
{
	float worldMat[16], combined[16];
	DirectX::XMMATRIX texMat;
	RwCamera *cam = (RwCamera*)RWSRCGLOBAL(curCamera);

	pipeGetComposedTransformMatrix(atomic, combined);
	RwFrame *atomicFrame = RpAtomicGetFrame(atomic);
	if(atomicFrame){
		RwMatrix *atomicLTM = RwFrameGetLTM(atomicFrame);
		if(atomicLTM)
			RwToD3DMatrix(&worldMat, atomicLTM);
		else
			memset(worldMat, 0, sizeof(worldMat));
	}else
		memset(worldMat, 0, sizeof(worldMat));
	RwD3D9SetVertexShaderConstant(LOC_combined, (void*)&combined, 4);
	RwD3D9SetVertexShaderConstant(LOC_world, (void*)&worldMat, 4);
	texMat = DirectX::XMMatrixIdentity();
	RwD3D9SetVertexShaderConstant(LOC_tex, (void*)&texMat, 4);

	RwMatrix *camfrm = NULL;
	RwFrame *camFrame = cam ? RwCameraGetFrame(cam) : NULL;
	if(camFrame)
		camfrm = RwFrameGetLTM(camFrame);
	
	RwV3d eyePos = {0, 0, 0};
	if(camfrm)
		eyePos = *RwMatrixGetPos(camfrm);
	RwD3D9SetVertexShaderConstant(LOC_eye, (void*)&eyePos, 1);

	// Use shared timecycle ambient (single source of truth for all pipelines)
	extern bool CCullZones__PlayerNoRain(void);
	extern int* CGame__currArea;
	bool isInterior = (*CGame__currArea != 0);
	bool isSheltered = CCullZones__PlayerNoRain() && !isInterior;
	RwRGBAReal tcAmbient = GetTimecycleAmbient();
	if(isSheltered){
		tcAmbient.red *= 0.55f;
		tcAmbient.green *= 0.55f;
		tcAmbient.blue *= 0.55f;
	}

	// Upload timecycle ambient directly
	RwD3D9SetVertexShaderConstant(LOC_ambient, &tcAmbient, 1);
	UploadLightColorWithSpecular(pDirect, LOC_directCol);	// NOT actually used
	pipeUploadLightDirection(pDirect, LOC_directDir);

	for(int i = 0; i < 6; i++)
		if(i < NumExtraDirLightsInWorld && RpLightGetType(pExtraDirectionals[i]) == rpLIGHTDIRECTIONAL){
			pipeUploadLightDirection(pExtraDirectionals[i], LOC_lightDir+i);
			UploadLightColorWithSpecular(pExtraDirectionals[i], LOC_lightCol+i);
		}else{
			pipeUploadZero(LOC_lightDir+i);
			pipeUploadZero(LOC_lightCol+i);
		}

	Color spec = specColor.Get();
	spec.r *= spec.a;
	spec.g *= spec.a;
	spec.b *= spec.a;
	RwD3D9SetVertexShaderConstant(LOC_directSpec, (void*)&spec, 1);
}

void
CarPipe::DiffusePass(RxD3D9ResEntryHeader *header, RpAtomic *atomic)
{
	RxD3D9InstanceData *inst = (RxD3D9InstanceData*)&header[1];
	CustomEnvMapPipeMaterialData *envData;
	int noRefl;

	RwUInt32 savedSrc, savedDst, savedVtxAlpha;
	RwRenderStateGet(rwRENDERSTATESRCBLEND, &savedSrc);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, &savedDst);
	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &savedVtxAlpha);

	RwD3D9SetTexture(reflectionTex, 1);
	RwD3D9SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_LERP);
//	RwD3D9SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	RwD3D9SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
	RwD3D9SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	RwD3D9SetTextureStageState(1, D3DTSS_COLORARG0, D3DTA_SPECULAR);
	RwD3D9SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	RwD3D9SetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_CURRENT);

	RwD3D9SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(3, D3DTSS_COLOROP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(3, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)1);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

	RwD3D9SetVertexShader(vertexShaderPass1);

	noRefl = CVisibilityPlugins__GetAtomicId(atomic) & 0x6000;

	for(uint i = 0; i < header->numMeshes; i++){
		RpMaterial *material = inst->material;
		// pipeSetTexture falls back to gpWhiteTexture on NULL material
		// textures — raw SetTexture(NULL, 0) leaves the sampler unbound
		// (white/undefined depending on driver state).
		pipeSetTexture(material->texture, 0);
		// have to set these after the texture, RW sets texture stage states automatically
		// ^ still true in d3d9? i think not but can't be bothered to find out
		RwD3D9SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		RwD3D9SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
		RwD3D9SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TEXTURE);
		RwD3D9SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		RwD3D9SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
		RwD3D9SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TEXTURE);

		envData = *RWPLUGINOFFSET(CustomEnvMapPipeMaterialData*, material, CCustomCarEnvMapPipeline__ms_envMapPluginOffset);

		RwUInt32 materialFlags = *(RwUInt32*)&material->surfaceProps.specular;
		bool hasEnv  = !!(materialFlags & 3);
		int matfx = RpMatFXMaterialGetEffects(material);
		if(matfx != rpMATFXEFFECTENVMAP)
			hasEnv = false;

		Color c = diffColor.Get();
		Color diff(c.r*c.a, c.g*c.a, c.b*c.a, 1.0f-c.a);
		RwRGBAReal mat;
		RwRGBARealFromRwRGBA(&mat, &inst->material->color);
		mat.red = mat.red*diff.a + diff.r;
		mat.green = mat.green*diff.a + diff.g;
		mat.blue = mat.blue*diff.a + diff.b;
		RwD3D9SetVertexShaderConstant(LOC_matCol, (void*)&mat, 1);

		// Rebuild by NAME in the documented c32 contract order instead of
		// memcpy'ing the 3-float RwSurfaceProperties: a 1-register upload of
		// that struct reads 4 bytes past its end (stack overread) and pinned
		// the shader to the struct layout. neoVehiclePass1VS reads
		// .x=ambient / .z=diffuse; .w is unread, zero-init for determinism.
		float surfprops[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		surfprops[0] = material->surfaceProps.ambient;
		surfprops[1] = material->surfaceProps.specular;
		surfprops[2] = material->surfaceProps.diffuse;
		RwD3D9SetVertexShaderConstant(LOC_surfProps, surfprops, 1);

		float reflProps[4];
		reflProps[0] = hasEnv && !noRefl ? envData->GetShininess() * 8.0f * config->neoShininessMult : 0.0f;
		reflProps[1] = fresnel.Get();
		reflProps[3] = power.Get();
		RwD3D9SetVertexShaderConstant(LOC_reflProps, (void*)reflProps, 1);

		D3D9RenderDual(config->dualPassVehicle, header, inst);
		inst++;
	}

	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)savedSrc);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)savedDst);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)savedVtxAlpha);
}

void
CarPipe::SpecularPass(RxD3D9ResEntryHeader *header, RpAtomic *atomic)
{
	RwUInt32 src, dst, fog, zwrite, alphatest;
	RwBool lighting;
	RwBool vtxAlpha;
	RxD3D9InstanceData *inst = (RxD3D9InstanceData*)&header[1];
	CustomSpecMapPipeMaterialData *specData;

	if(CVisibilityPlugins__GetAtomicId(atomic) & 0x6000)
		return;

	RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &zwrite);
	RwRenderStateGet(rwRENDERSTATEFOGENABLE, &fog);
	RwRenderStateGet(rwRENDERSTATESRCBLEND, &src);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, &dst);
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &alphatest);
	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &vtxAlpha);

	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONALWAYS);
	RwD3D9SetTexture(NULL, 0);
	RwD3D9SetTexture(NULL, 1);
	RwD3D9SetTexture(NULL, 2);
	RwD3D9SetTexture(NULL, 3);
	RwD3D9SetVertexShader(vertexShaderPass2);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)1);

	RwD3D9GetRenderState(D3DRS_LIGHTING, &lighting);

	float lightmult = 1.85f * CCustomCarEnvMapPipeline__m_EnvMapLightingMult;
	for(uint i = 0; i < header->numMeshes; i++){
		RwUInt32 materialFlags = *(RwUInt32*)&inst->material->surfaceProps.specular;
		bool hasSpec = !((materialFlags & 4) == 0 || !lighting) && inst->material->color.alpha > 0;
		int matfx = RpMatFXMaterialGetEffects(inst->material);
		if(matfx != rpMATFXEFFECTENVMAP)
			hasSpec = false;

		if(hasSpec){
			specData = *RWPLUGINOFFSET(CustomSpecMapPipeMaterialData*, inst->material, CCustomCarEnvMapPipeline__ms_specularMapPluginOffset);
			// c32 contract order (ambient, specular, diffuse, 0). Pass2 reads
			// only .y=specular, but the old uninitialized RwSurfaceProperties
			// uploaded stack garbage for .ambient/.diffuse and read 4 bytes
			// past the struct. Zero-init all four; clamp spec to [0,1].
			float surfprops[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			surfprops[1] = specData->specularity*5.0f*config->neoSpecularityMult*lightmult;
			if(surfprops[1] > 1.0f) surfprops[1] = 1.0f;
			if(surfprops[1] < 0.0f) surfprops[1] = 0.0f;
			RwD3D9SetVertexShaderConstant(LOC_surfProps, surfprops, 1);
			D3D9Render(header, inst);
		}
		inst++;
	}
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)zwrite);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)fog);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)src);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)dst);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)alphatest);
	// Close the VERTEXALPHAENABLE=1 leak (set at line ~433 above): restore
	// it alongside the other states instead of leaking it to the caller
	// (the Switch-level pipeExitAlphaMode also covers this, but the pass
	// must not depend on it).
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)vtxAlpha);
}

void
CarPipe::RenderCallback(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags)
{
//		{
//			static bool keystate = false;
//			if(GetAsyncKeyState(VK_F7) & 0x8000){
//				if(!keystate){
//					keystate = true;
//					LoadTweakingTable();
//				}
//			}else
//				keystate = false;
//		}
	if(!iCanHasNeoCar)
		return;

	// Belt-and-braces: vehiclePipe Switch already guards cull; this
	// covers any direct registration of this callback. Safe to nest.
	RwUInt32 savedCull = pipeEnterCullMode();

	RwUInt32 savedFog, savedZWrite;
	RwRenderStateGet(rwRENDERSTATEFOGENABLE, &savedFog);
	RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &savedZWrite);

	_rwD3D9EnableClippingIfNeeded(object, type);

	RxD3D9ResEntryHeader *header = (RxD3D9ResEntryHeader*)&repEntry[1];
	ShaderSetup((RpAtomic*)object);

	if(header->indexBuffer != NULL)
		RwD3D9SetIndices(header->indexBuffer);
	_rwD3D9SetStreams(header->vertexStream, header->useOffsets);
	RwD3D9SetVertexDeclaration(header->vertexDeclaration);

	RwD3D9SetPixelShader(NULL);

	DiffusePass(header, (RpAtomic*)object);
	if(!renderingWheel)
		SpecularPass(header, (RpAtomic*)object);
	RwD3D9SetTexture(NULL, 1);
	RwD3D9SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	RwD3D9SetTexture(NULL, 2);
	RwD3D9SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)savedFog);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)savedZWrite);
	pipeExitCullMode(savedCull);
}

// Release neo car pipe resources on device reset
void ReleaseNeoCarPipeResources(void)
{
	if(reflectionMatrix){ RwMatrixDestroy(reflectionMatrix); reflectionMatrix = NULL; }
}

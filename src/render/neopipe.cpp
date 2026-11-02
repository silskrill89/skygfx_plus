// neopipe.cpp - consolidated NEO pipe translation unit.
//
// Merged 2026-10-27 from THREE former TUs, concatenated in this order:
//   1. src/render/neo.cpp           - neo core init + weather-time interpolation
//   2. src/render/neoCarpipe.cpp    - CarPipe (NEO vehicle env/spec pipe)
//   3. src/render/neoWaterdrops.cpp - WaterDrops screen-space rain drops
//
// Bodies are verbatim; only the includes were deduped and section banners
// added. Documented cleanup (no behaviour change): the dead `#if 0` D3D8
// CustomPipe block (former neo.cpp:143-209, never compiled) was deleted.
// Public API: neopipe.h (was neo.h); neo.h forwards here.

// ---- Includes (deduped union of the three TUs) ----
#include "skygfx.h"
#include "neopipe.h"
#include <DirectXMath.h>
#include "MemoryMgr.h"
#include "d3d9.h"
#include "d3d9types.h"
#include <time.h>
#include <string.h>
#define _USE_MATH_DEFINES
#include <cmath>
#include <injector\injector.hpp>
#include <injector\hooking.hpp>
#include <injector\calling.hpp>
#include <injector\assembly.hpp>
#include <injector\utility.hpp>

// ---- NEO CORE (was src/render/neo.cpp) ----

RwTexDictionary *neoTxd;
int iCanHasNeoCar;

void
neoInit(void)
{
	ONCE;
//	if(xboxcarpipe >= 0 || xboxwaterdrops){
	{
		char *path = getpath("neo\\neo.txd");
		if(path == NULL)
			return;
		RwStream *stream = RwStreamOpen(rwSTREAMFILENAME, rwSTREAMREAD, path);
		// AUDIT: RwStreamOpen returns NULL for a missing file — FindChunk/
		// StreamRead/Close on NULL crash inside RW. Skip to the existing
		// neoTxd==NULL error box instead.
		if(stream && RwStreamFindChunk(stream, rwID_TEXDICTIONARY, NULL, NULL))
			neoTxd = RwTexDictionaryStreamRead(stream);
		if(stream)
			RwStreamClose(stream, NULL);
		if(neoTxd == NULL){
			MessageBox(NULL, "Couldn't find Tex Dictionary inside 'neo\\neo.txd'", "Error", MB_ICONERROR | MB_OK);
			exit(0);
		}
		// we can just set this to current because we're executing before CGame::Initialise
		// which sets up "generic" as the current TXD
		RwTexDictionarySetCurrent(neoTxd);
	}

	WaterDrops::ms_maskTex = RwTextureRead("dropmask", NULL);

	neoCarPipeInit();
}

#define INTERP_SETUP \
		int h1 = CClock__ms_nGameClockHours;								  \
		int h2 = (h1+1)%24;										  \
		int w1 = CWeather__OldWeatherType;								  \
		int w2 = CWeather__NewWeatherType;								  \
		float timeInterp = (CClock__ms_nGameClockSeconds/60.0f + CClock__ms_nGameClockMinutes)/60.0f;	  \
		float c0 = (1.0f-timeInterp)*(1.0f-CWeather__InterpolationValue);				  \
		float c1 = timeInterp*(1.0f-CWeather__InterpolationValue);					  \
		float c2 = (1.0f-timeInterp)*CWeather__InterpolationValue;					  \
		float c3 = timeInterp*CWeather__InterpolationValue;
#define INTERP(v) v[h1][w1]*c0 + v[h2][w1]*c1 + v[h1][w2]*c2 + v[h2][w2]*c3;
#define INTERPF(v,f) v[h1][w1].f*c0 + v[h2][w1].f*c1 + v[h1][w2].f*c2 + v[h2][w2].f*c3;


InterpolatedFloat::InterpolatedFloat(float init)
{
	curInterpolator = 61;	// compared against second
	for(int h = 0; h < 24; h++)
		for(int w = 0; w < NUMWEATHERS; w++)
			data[h][w] = init;
}

void
InterpolatedFloat::Read(char *s, int line, int field)
{
	sscanf(s, "%f", &data[line][field]);
}

float
InterpolatedFloat::Get(void)
{
	if(curInterpolator != CClock__ms_nGameClockSeconds){
		INTERP_SETUP
		curVal = INTERP(data);
		curInterpolator = CClock__ms_nGameClockSeconds;
	}
	return curVal;
}

InterpolatedColor::InterpolatedColor(const Color &init)
{
	curInterpolator = 61;	// compared against second
	for(int h = 0; h < 24; h++)
		for(int w = 0; w < NUMWEATHERS; w++)
			data[h][w] = init;
}

void
InterpolatedColor::Read(char *s, int line, int field)
{
	int r, g, b, a;
	sscanf(s, "%i, %i, %i, %i", &r, &g, &b, &a);
	data[line][field] = Color(r/255.0f, g/255.0f, b/255.0f, a/255.0f);
}

Color
InterpolatedColor::Get(void)
{
	if(curInterpolator != CClock__ms_nGameClockSeconds){
		INTERP_SETUP
		curVal.r = INTERPF(data, r);
		curVal.g = INTERPF(data, g);
		curVal.b = INTERPF(data, b);
		curVal.a = INTERPF(data, a);
		curInterpolator = CClock__ms_nGameClockSeconds;
	}
	return curVal;
}

void
InterpolatedLight::Read(char *s, int line, int field)
{
	int r, g, b, a;
	sscanf(s, "%i, %i, %i, %i", &r, &g, &b, &a);
	data[line][field] = Color(r/255.0f, g/255.0f, b/255.0f, a/100.0f);
}

void
neoReadWeatherTimeBlock(FILE *file, InterpolatedValue *interp)
{
	char buf[24], *p;
	int c;
	int line, field;

	line = 0;
	c = getc(file);
	while(c != EOF && line < 24){
		field = 0;
		if(c != EOF && c != '#'){
			while(c != EOF && c != '\n' && field < NUMWEATHERS){
				p = buf;
				while(c == '\t')
					c = getc(file);
				*p++ = c;
				// AUDIT: buf is 24 bytes — a corrupt/over-long field in the
				// tweaking table ran p past the array (stack smash). Drop
				// excess characters; sscanf still parses the truncated head.
				while(c = getc(file), c != EOF && c != '\t' && c != '\n')
					if(p - buf < (int)sizeof(buf) - 2)
						*p++ = c;
				*p++ = '\0';
				interp->Read(buf, line, field);
				field++;
			}
			line++;
		}
		while(c != EOF && c != '\n')
			c = getc(file);
		c = getc(file);
	}
	ungetc(c, file);
}
// ---- NEO CAR PIPE (was src/render/neoCarpipe.cpp) ----

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
// dead: VS passes removed, see fast_build — vertexShaderPass1/2 were only ever
// set to nullptr after neoVehiclePass1VS/neoVehiclePass2VS were dropped from
// fast_build.py's shader lists. Definitions kept only because neopipe.h still
// declares the members; do NOT chase the old neo VS passes — they are gone.
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
	// AUDIT: reflectionTex has a destroy site (envmap.cpp device-reset path)
	// and MakeEnvmapRasters only re-attaches a live texture, never recreates
	// a NULL one — the raw reflectionTex->raster deref below faulted (inside
	// RenderEnvTex's __try) on every frame while it stayed NULL.
	if(!reflectionCam || !reflectionTex || !reflectionTex->raster)
		return;
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
		// AUDIT: RwMatrixCreate can return NULL (allocation failure) — the
		// field writes below would fault. Bail, but re-pair the camera first:
		// we are past the RwCameraEndUpdate above, and every exit after it
		// must flow through a paired BeginUpdate (FIX 1 doctrine, same as
		// the MakeEnvmapRasters failure path above).
		if(!reflectionMatrix){
			RwCameraBeginUpdate(Scene.camera);
			return;
		}
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
	// dead: VS passes removed, see fast_build — neoVehiclePass1/2VS were purged
	// from fast_build.py's shader lists. vertexShaderPass1/2 are PERMANENTLY
	// NULL by design (unified PBR pipeline replaced the two-pass neo VS);
	// the nullptr writes below only document that contract (neo.h still
	// declares the members). Nothing loads those VSes anymore.
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
	// AUDIT: a missing file made getc(NULL) fault inside neoReadWeatherTimeBlock.
	// Same "no data" condition as the path==NULL early-out above: leave
	// iCanHasNeoCar at 0 (neo car pipe off) instead of crashing.
	if(dat == NULL)
		return;
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

	// dead: VS passes removed, see fast_build — was
	// RwD3D9SetVertexShader(vertexShaderPass1) with the (permanently NULL)
	// neoVehiclePass1VS handle; bind fixed-function VS explicitly instead so
	// no stale programmable VS can survive into this pass.
	RwD3D9SetVertexShader(NULL);

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
		// the shader to the struct layout. The removed neoVehiclePass1VS read
		// (dead: VS passes removed, see fast_build)
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
	// dead: VS passes removed, see fast_build — was
	// RwD3D9SetVertexShader(vertexShaderPass2) with the (permanently NULL)
	// neoVehiclePass2VS handle; bind fixed-function VS explicitly instead so
	// no stale programmable VS can survive into this pass.
	RwD3D9SetVertexShader(NULL);
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
			// c32 contract order (ambient, specular, diffuse, 0). The removed
			// pass2 VS read (dead: VS passes removed, see fast_build)
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
// ---- NEO WATER DROPS (was src/render/neoWaterdrops.cpp) ----


enum eCamMode
{
	MODE_NONE = 0,
	MODE_TOPDOWN, //=1, 
	MODE_GTACLASSIC,// =2, 
	MODE_BEHINDCAR, //=3, 
	MODE_FOLLOWPED, //=4, 
	MODE_AIMING, //=5, 
	MODE_DEBUG, //=6, 
	MODE_SNIPER, //=7, 
	MODE_ROCKETLAUNCHER, //=8,
	MODE_MODELVIEW, //=9, 
	MODE_BILL, //=10, 
	MODE_SYPHON, //=11, 
	MODE_CIRCLE, //=12, 
	MODE_CHEESYZOOM, //=13, 
	MODE_WHEELCAM, //=14, 
	MODE_FIXED, //=15, 
	MODE_1STPERSON, //=16, 
	MODE_FLYBY, //=17, 
	MODE_CAM_ON_A_STRING, //=18,
	MODE_REACTION, //=19, 
	MODE_FOLLOW_PED_WITH_BIND, //=20, 
	MODE_CHRIS, //21
	MODE_BEHINDBOAT, //=22, 
	MODE_PLAYER_FALLEN_WATER, //=23, 
	MODE_CAM_ON_TRAIN_ROOF, //=24, 
	MODE_CAM_RUNNING_SIDE_TRAIN, //=25,
	MODE_BLOOD_ON_THE_TRACKS, //=26, 
	MODE_IM_THE_PASSENGER_WOOWOO, //=27, 
	MODE_SYPHON_CRIM_IN_FRONT,// =28, 
	MODE_PED_DEAD_BABY, //=29, 
	MODE_PILLOWS_PAPS, //=30, 
	MODE_LOOK_AT_CARS, //=31, 
	MODE_ARRESTCAM_ONE, //=32, 
	MODE_ARRESTCAM_TWO, //=33, 
	MODE_M16_1STPERSON, //=34, 
	MODE_SPECIAL_FIXED_FOR_SYPHON, //=35, 
	MODE_FIGHT_CAM, //=36, 
	MODE_TOP_DOWN_PED, //=37,
	MODE_LIGHTHOUSE, //=38
	MODE_SNIPER_RUNABOUT, //=39, 
	MODE_ROCKETLAUNCHER_RUNABOUT, //=40,
	MODE_1STPERSON_RUNABOUT, //=41,
	MODE_M16_1STPERSON_RUNABOUT, //=42,
	MODE_FIGHT_CAM_RUNABOUT, //=43,
	MODE_EDITOR, //=44,
	MODE_HELICANNON_1STPERSON, //= 45
	MODE_CAMERA, //46
	MODE_ATTACHCAM,  //47
	MODE_TWOPLAYER,
	MODE_TWOPLAYER_IN_CAR_AND_SHOOTING,
	MODE_TWOPLAYER_SEPARATE_CARS,	//50
	MODE_ROCKETLAUNCHER_HS,
	MODE_ROCKETLAUNCHER_RUNABOUT_HS,
	MODE_AIMWEAPON,
	MODE_TWOPLAYER_SEPARATE_CARS_TOPDOWN,
	MODE_AIMWEAPON_FROMCAR, //55
	MODE_DW_HELI_CHASE,
	MODE_DW_CAM_MAN,
	MODE_DW_BIRDY,
	MODE_DW_PLANE_SPOTTER,
	MODE_DW_DOG_FIGHT, //60
	MODE_DW_FISH,
	MODE_DW_PLANECAM1,
	MODE_DW_PLANECAM2,
	MODE_DW_PLANECAM3,
	MODE_AIMWEAPON_ATTACHED //65
};

typedef uintptr_t addr;

#define MAXSIZE 15
#define MINSIZE 4
#define DROPFVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX2)
#define RAD2DEG(x) (180.0f*(x)/M_PI)

float scaling;
#define SC(x) ((int)((x)*scaling))

// debuggin
bool WaterDrops::sprayWater = false;
bool WaterDrops::sprayBlood = false;

float WaterDrops::ms_xOff, WaterDrops::ms_yOff;	// not quite sure what these are
WaterDrop WaterDrops::ms_drops[MAXDROPS];
int WaterDrops::ms_numDrops;
WaterDropMoving WaterDrops::ms_dropsMoving[MAXDROPSMOVING];
int WaterDrops::ms_numDropsMoving;

bool WaterDrops::ms_enabled;
bool WaterDrops::ms_movingEnabled;

int WaterDrops::ms_splashDuration = -1;

float WaterDrops::ms_distMoved, WaterDrops::ms_vecLen, WaterDrops::ms_rainStrength;
RwV3d WaterDrops::ms_vec;
RwV3d WaterDrops::ms_lastAt;
RwV3d WaterDrops::ms_lastPos;
RwV3d WaterDrops::ms_posDelta;

RwTexture *WaterDrops::ms_maskTex;
RwTexture *WaterDrops::ms_tex;
int WaterDrops::ms_fbWidth, WaterDrops::ms_fbHeight;
void *WaterDrops::ms_vertexBuf;
void *WaterDrops::ms_indexBuf;
VertexTex2 *WaterDrops::ms_vertPtr;
int WaterDrops::ms_numBatchedDrops;
int WaterDrops::ms_initialised;

extern IDirect3DDevice9 *&d3d9device;

short getCamMode()
{
	uchar activeCam = *((uint8*)&TheCamera + 0x59);
	uchar *cam;

	cam = (uint8*)&TheCamera + 0x174;
	cam += activeCam * 0x238;

	return *(short*)(cam + 0xC);
}

static uint32_t CCullZones__CamNoRain_A = (uint32_t)(0x72DDB0);
WRAPPER bool CCullZones__CamNoRain(void) { VARJMP(CCullZones__CamNoRain_A); }
static uint32_t CCullZones__PlayerNoRain_A = (uint32_t)(0x72DDC0);
WRAPPER bool CCullZones__PlayerNoRain(void) { VARJMP(CCullZones__PlayerNoRain_A); }
static uint32_t FindPlayerVehicle_A = (uint32_t)(0x56E0D0);
WRAPPER bool FindPlayerVehicle(uint32_t, uint8_t) { VARJMP(FindPlayerVehicle_A); }

struct CPad
{
	static CPad *GetPad(int);
	bool GetLookBehindForCar(void);
	bool GetLookLeft(void);
	bool GetLookRight(void);
};
static uint32_t GetPad_A = 0x53FB70;
WRAPPER CPad *CPad::GetPad(int id) { VARJMP(GetPad_A); }
static uint32_t GetLookBehindForCar_A = 0x53FE70;
WRAPPER bool CPad::GetLookBehindForCar(void) { VARJMP(GetLookBehindForCar_A); }
static uint32_t GetLookLeft_A = 0x53FDD0;
WRAPPER bool CPad::GetLookLeft(void) { VARJMP(GetLookLeft_A); }
static uint32_t GetLookRight_A = 0x53FE10;
WRAPPER bool CPad::GetLookRight(void) { VARJMP(GetLookRight_A); }

WRAPPER RwStream* RwStreamOpen(RwStreamType type, RwStreamAccessType accessType, const void* pData) { EAXJMP(0x7ECEF0); }
WRAPPER RwBool RwStreamFindChunk(RwStream* stream, RwUInt32 type, RwUInt32* lengthOut, RwUInt32* versionOut) { EAXJMP(0x7ED2D0); }
WRAPPER RwTexDictionary* RwTexDictionaryStreamRead(RwStream* stream) { EAXJMP(0x804C30); }
WRAPPER RwBool RwStreamClose(RwStream* stream, void* pData) { EAXJMP(0x7ECE20); }
WRAPPER RwTexDictionary* RwTexDictionarySetCurrent(RwTexDictionary* pDict) { EAXJMP(0x7F3A70); }
WRAPPER RwTexture* RwTextureRead(const char *name, const char *maskName) { EAXJMP(0x7F3AC0); }

typedef void(*voidfunc)(void);
// AUDIT: reset_call_N is never assigned anywhere in this tree (the reset
// hooks are not installed — inherited upstream scaffolding), so the BSS
// pointer is NULL and reset_hook_N would call through it if anyone ever
// wired the hook without a target. Guard the call; WaterDrops::Reset()
// itself is unconditional and safe.
#define INJECTRESET(x) \
	addr reset_call_##x; \
	void reset_hook_##x(void){ \
		if(reset_call_##x) \
			((voidfunc)reset_call_##x)(); \
		WaterDrops::Reset(); \
	}

INJECTRESET(1)
INJECTRESET(2)
INJECTRESET(3)
INJECTRESET(4)
INJECTRESET(5)

// ============================================================
// W2 splash-foam white blowout fix (finalgate-0042 findings W2).
//
// Root cause (disassembly + effects.fxp audit, 1.0 US):
//   * Every FX prim renders through the standard particle renderer
//     0x4A2C40 (the only render in the prim-class vtable 0x85A788; the
//     FLAT helper 0x4A1940 is dead code - its gate flag [prim+0x3d] has
//     no writer anywhere in the FX region, and its only call site
//     0x4A2CA2 sits behind an argument that is always 0 on the live
//     chain FxManager_c::Render 0x4A92A0 -> fxSystem_c::Render 0x4AA160,
//     which passes (0xA9AE00, 0, 0, 0) down to every prim render).
//   * Blend comes from the prim's own fxp bytes, read at 0x4A2D0B:
//     SRCBLENDID=4 (SRCALPHA) + DSTBLENDID=1 (ONE) for every water
//     splash prim. SRCALPHA/ONE is semi-additive (dst + src*src.a), so
//     overlapping sprites ACCUMULATE instead of blending.
//   * The swim-splash systems (water_splash_big/_splash/_sml/water_swim)
//     emit ~3 5x5 m "impact" sphere discs + ~100 0.5-2 m droplets per
//     burst, all at alpha 64; continuous swimming always keeps a fresh
//     burst alive, the overlaps saturate to a solid white dome over the
//     mod's brighter water (finalgate shots 7/8 - the small droplet
//     speckles at the dome crest were the sparse edge of the same cloud).
//   * See-through: FxManager_c::Render runs the whole particle batch
//     ZTEST on / ZWRITE off (0x4A92C2-0x4A92EA) and the translucent
//     water plane never writes Z, so from a submerged camera the discs
//     Z-test against the sea floor/sky and draw straight through the
//     surface (finalgate shot 7).
//
// Fix (system-level):
//   * Hook fxSystem_c::Render at its ONLY call site (0x4A930A, the
//     FxManager_c::Render loop - intercepted-call style, same as the
//     WaterDrops hooks below). A system counts as water splash iff any
//     of its prims binds one of the water-exclusive textures listed
//     below; the fxp audit shows no fire/explosion/spark system binds
//     any of them (their sphere/bullethitsmoke prims live in systems
//     that never bind a trigger texture).
//   * F4 (test2-0438) white smoke family: a SIBLING predicate matches the
//     saturating non-water white/grey smoke systems - vehicle exhaust and
//     tyre skid (prt_smoke_huge/prt_smokeII_3_expand/prt_collisionsmoke:
//     the wheel trio at 0x6AA86F/0x6AA87F/0x6AA8FA and the automobile
//     sites 0x6C942B..0x6CC0D4), muzzle/dust smoke (gunsmoke, heli_dust,
//     prt_wheeldirt, cigarette_smoke, riot_smoke) and the map smoke sheet
//     (WS_factorysmoke). NOTE: effects.fxp has NO system literally named
//     "exhaust"/"skid" - those producers are the prt_smoke_huge family
//     above (verified against all 82 fxp systems + all 59 named-create
//     sites in the exe). Fire-family textures are a HARD VETO so every
//     flame/spark system stays additive exactly as authored.
//   * Matched systems: every prim's dst blend byte is rewritten
//     ONE(1) -> INVSRCALPHA(5) around the original call (the bytes are
//     read inside the original at 0x4A2D25-0x4A2D50), giving proper
//     alpha blend - translucent streaks, no accumulation. Restore after
//     so a pooled/recycled prim object can never carry the rewrite into
//     a foreign blueprint. (Prims already authored INVSRCALPHA - the
//     prt_smoke_huge family - round-trip untouched: the rewrite only
//     flips byte==1.)
//   * Per-draw blend PIN (test2-0438 RCA-3a + F4): the game sets the
//     per-prim blend via RwRenderStateSet (0x4A2D0B) which NO-OPS when
//     the rw cache already holds the value - after the post-water/postfx
//     window leaves cache=(alpha pair) but device=(blend OFF/ONE), every
//     (5,6) request no-ops and the smoke/splash prims draw REPLACE
//     ("hard additive streaks", "solid white wedge"). Fire's (5,2) sets
//     transition and stay correct, which is why fire looked right. The
//     pin forces VERTEXALPHA=1 + SRCALPHA/INVSRCALPHA + BLENDOP=ADD on
//     all three layers around the matched draw and restores verbatim;
//     authored (5,2) prims inside a matched system still transition to
//     their own blend, so the pin can never de-addify a flame.
//   * Matched SPLASH systems are skipped entirely while the camera is
//     submerged (same 0.339731634 threshold as WaterDrops::NoDrops):
//     the surface foam stops drawing through the water plane. Only the
//     draw is gated - fxSystem_c::Render is draw-only (the particle
//     update runs elsewhere), so particles keep aging and dying. White
//     smoke is NOT underwater-gated (world smoke, not surface foam).
//   * WaterDrops (screen-space lens drops, drawn from
//     CMotionBlurStreaksRender) is a separate overlay - untouched here;
//     its own tail leak is paired in WaterDrops::Render below (cause #3).
//
// Crash hardening (RCA 0x4A8E49, post-screenshot systems-list crash):
//   * The fault was fxManager's list-REMOVE (0x4A8E49, node layout
//     +0=next/+4=prev, header 0xA9AE8C = fxManager 0xA9AE80+0xC = live
//     systems list) writing next->prev with a system whose ->next held
//     float-bits garbage (0x3F70ACE0 ~ 0.9688f) while ->prev stayed
//     valid. Systems-list links are written ONLY by INSERT/REMOVE/POPFN
//     with pointer values, so the header was overwritten by float data
//     from outside the list machinery - the particle-node data writes
//     (0x3C pool nodes, floats at +8..+0x27; a node at sys-8 covers
//     sys+0..0x1F) through a stale pool pointer, i.e. game-side FX
//     memory reuse. None of this hook's writes can reach a system
//     header (verified: single render-prim class, vtable 0x859B88 - the
//     only vtable in the image whose +0x14 slot is the renderer
//     0x4A2C40; prim+5/6/7 are standalone byte fields, packed from fxp
//     floats at 0x4A673F/0x4A6747/0x4A68CF).
//   * Residual hook-side hazard closed anyway: the rewrite/restore
//     dereferenced primList[i] with NO identity check, so a recycled or
//     foreign object in the array would have taken our byte write.
//     Every prim touch is now gated on the vtable signature
//     (IsFxRenderPrim below) - a freed/recycled prim fails the check
//     and is skipped, so the hook is structurally unable to write into
//     memory that is not a live render prim. Splash/smoke behaviour is
//     unchanged: every live prim passes the check.
// ============================================================

// Signature gate: the render-prim class vtable (0x859B88, sole +0x14 ->
// renderer 0x4A2C40 in the whole image). Anything else in primList[] is
// not a live render prim and must not be read as one or written to.
static const void *const FX_RENDER_PRIM_VTABLE = (void *)0x859B88;

static bool
IsFxRenderPrim(void *prim)
{
	return prim && *(void **)prim == FX_RENDER_PRIM_VTABLE;
}

static bool
IsWaterSplashSystem(void *sys)
{
	static const char *const triggerTex[] = {
		"carsplash_02",	// water_splash/_big/_sml/water_swim droplets
		"boatwake1",	// prt_wake circles
		"coronaringa",	// water_ripples circles
		"splash_up1",	// prt_splash
		"boatsplash",	// prt_boatsplash, water_speed
		"wjet2",	// water_hydrant/_fountain/_fnt_tme
		"wgush1",	// water_hydrant/_fountain/_fnt_tme
		"gensplash",	// prt_watersplash, water_speed, boat_prop
	};
	if(!sys)
		return false;
	signed char primCount = *(signed char*)((char*)sys + 0x1b);
	void **primList = *(void***)((char*)sys + 0x1c);
	if(primCount <= 0 || !primList)
		return false;
	for(int i = 0; i < primCount; i++){
		if(!IsFxRenderPrim(primList[i]))
			continue;
		void **texHolder = (void**)(((char*)primList[i]) + 0xC);
		if(!texHolder)
			continue;
		RwTexture *tex = (RwTexture*)*texHolder;
		if(!tex)
			continue;
		const char *name = tex->name;
		if(!name || !name[0])
			continue;
		for(unsigned t = 0; t < sizeof(triggerTex)/sizeof(triggerTex[0]); t++)
			if(strcmp(name, triggerTex[t]) == 0)
				return true;
	}
	return false;
}

// F4 (test2-0438): the saturating non-water white/grey smoke family.
// Triggers = the white-smoke textures; a fire-family texture anywhere in
// the system is a HARD VETO (fire/sparks must stay additive - the veto is
// what keeps prt_spark/prt_spark_2, gunflash, camflash, Flame, explosions,
// nitro, smoke_flare, extinguisher and overheat_car_electric untouched).
// Systems already authored SRCALPHA/INVSRCALPHA (the prt_smoke_huge
// exhaust/skid family) match too: their byte rewrite is a no-op, but the
// blend pin below repairs their split-cache draw (the F4 "hard additive
// streaks" are those prims rendering REPLACE after the post-water window).
static bool
IsWhiteSmokeSystem(void *sys)
{
	static const char *const triggerTex[] = {
		"smoke",		// spraycan mist
		"smoke4",		// smoke_flare (vetoed in practice - also binds sphere)
		"smoke5",		// WS_factorysmoke - the map smoke sheet
		"collisionsmoke",	// prt_smoke_huge/prt_collisionsmoke/prt_smokeII_3_expand,
					// cigarette_smoke, riot_smoke
		"smokeII_3",		// gunsmoke, heli_dust, prt_wheeldirt, exhale
	};
	static const char *const fireVetoTex[] = {
		"sphere",		// gunflash/camflash/Flame/flame/molotov_flame/
					// petrolcan/shootlight/tank_fire/extinguisher/
					// prt_spark/prt_spark_2
		"sphere_CJ",		// fire/fire_bike/fire_car/fire_large/fire_med/
					// flamethrower/jetpack/jetthrust/nitro
		"wjet6",		// fire flames
		"fireball6",		// explosion_* fireballs
		"bullethitsmoke",	// fire_bike/fire_car/flamethrower smoke prims,
					// coke_puff, puke, explosion_fuel_car
		"waterspark_16",	// overheat_car_electric sparks
	};
	if(!sys)
		return false;
	signed char primCount = *(signed char*)((char*)sys + 0x1b);
	void **primList = *(void***)((char*)sys + 0x1c);
	if(primCount <= 0 || !primList)
		return false;
	bool trigger = false;
	for(int i = 0; i < primCount; i++){
		if(!IsFxRenderPrim(primList[i]))
			continue;
		void **texHolder = (void**)(((char*)primList[i]) + 0xC);
		RwTexture *tex = texHolder ? (RwTexture*)*texHolder : NULL;
		if(!tex)
			continue;
		const char *name = tex->name;
		if(!name || !name[0])
			continue;
		for(unsigned t = 0; t < sizeof(fireVetoTex)/sizeof(fireVetoTex[0]); t++)
			if(strcmp(name, fireVetoTex[t]) == 0)
				return false;	// fire/spark family: stay additive
		for(unsigned t = 0; t < sizeof(triggerTex)/sizeof(triggerTex[0]); t++)
			if(strcmp(name, triggerTex[t]) == 0)
				trigger = true;
	}
	return trigger;
}

// Per-draw FX draw-state pin (test2-0438 RCA-3a/F4 + font-safety).
//
// The pin frames EVERY fxSystem_c::Render call we wrap (matched or not) with
// a three-layer capture and a verbatim restore of BOTH:
//   (a) the stage-0 TEXTURERASTER block          -> fx_s0leak receipt
//   (b) the alpha / blend block                  -> fx_blendleak receipt
//
// Why stage 0 must be framed by the hook (not just by matched systems):
// the fx prim renderer (0x4A2C40) binds its sprite texture through raw
// RwD3D9SetTexture / device paths, so the rw cache keeps whatever raster it
// held on entry while the DEVICE ends the draw holding the last particle
// texture. The next FFP consumer (font/HUD quads) then calls
// RwRenderStateSet(rwRENDERSTATETEXTURERASTER, <that same entry raster>):
// the cache comparison in RwRenderStateSet sees a HIT, the set no-ops, and
// every glyph samples the last smoke/splash texture. That is the exact
// "fonts completely broken" class already fixed in postfx.cpp's mb_s0leak
// pass and chars.cpp's RestoreSSSStage0AndSampler.
//
// Matched systems additionally get the canonical alpha pair FORCED for the
// duration of the draw (forceAlpha). The game's per-prim RwRenderStateSet
// (0x4A2D0B) no-ops when the rw cache already holds the value, so after the
// post-water/postfx window leaves cache=(alpha pair) but device=(blend
// OFF/ONE) every (5,6) request no-ops and the smoke/splash prims draw
// REPLACE. Authored (5,2) prims still transition to their own pair, so the
// pin can never de-addify a flame. Non-matched systems are state-neutral:
// captured and restored, never forced.
//
// Exit restores every captured layer (rw cache / RW driver pending / raw
// device) verbatim AND runs unconditionally - the caller guards the original
// call so a fault cannot skip it. POD-only (C2712-safe: the hook body owns
// __try blocks).
struct FxDrawPin {
	// layer 1 - rw cache
	RwUInt32 vtxRw, srcRw, dstRw;
	RwRaster *rasRw;
	// layer 2 - driver pending
	DWORD vtxDrv, srcDrv, dstDrv, opDrv;
	// layer 3 - raw device
	DWORD vtxDev, srcDev, dstDev, opDev;
};

// Resolve an rw raster to its D3D texture. Borrowed pointer, NO AddRef
// (the proven 0xB4E9E0 extension extraction used by pedStage0Resync,
// chars.cpp RestoreSSSStage0AndSampler, WaterDrops::Render and texdefs).
static IDirect3DTexture9 *
fxRasterToTex(RwRaster *ras)
{
	if(!ras)
		return NULL;
	DWORD extOff = *(DWORD*)0xB4E9E0;
	if(extOff == 0 || extOff >= 0x100000)
		return NULL;
	IDirect3DTexture9 *cand = *(IDirect3DTexture9**)((BYTE*)ras + extOff);
	if(!cand || (uintptr_t)cand < 0x10000u || ((uintptr_t)cand & 3u))
		return NULL;
	return cand;
}

static void
FxDrawPinEnter(FxDrawPin *p, bool forceAlpha)
{
	p->vtxRw = TRUE;
	p->srcRw = rwBLENDSRCALPHA;
	p->dstRw = rwBLENDINVSRCALPHA;
	p->rasRw = NULL;
	p->vtxDrv = TRUE; p->srcDrv = D3DBLEND_SRCALPHA;
	p->dstDrv = D3DBLEND_INVSRCALPHA; p->opDrv = D3DBLENDOP_ADD;
	p->vtxDev = p->vtxDrv; p->srcDev = p->srcDrv;
	p->dstDev = p->dstDrv; p->opDev = p->opDrv;

	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &p->vtxRw);
	if(p->vtxRw == 0xFFFFFFFF)	// failed-Get sentinel (see pedStage0Resync)
		p->vtxRw = TRUE;
	RwRenderStateGet(rwRENDERSTATESRCBLEND, &p->srcRw);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, &p->dstRw);
	RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &p->rasRw);
	if((uintptr_t)p->rasRw == (uintptr_t)0xFFFFFFFF)	// failed-Get sentinel
		p->rasRw = NULL;
	RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE, &p->vtxDrv);
	RwD3D9GetRenderState(D3DRS_SRCBLEND, &p->srcDrv);
	RwD3D9GetRenderState(D3DRS_DESTBLEND, &p->dstDrv);
	RwD3D9GetRenderState(D3DRS_BLENDOP, &p->opDrv);
	if(d3d9device){
		d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &p->vtxDev);
		d3d9device->GetRenderState(D3DRS_SRCBLEND, &p->srcDev);
		d3d9device->GetRenderState(D3DRS_DESTBLEND, &p->dstDev);
		d3d9device->GetRenderState(D3DRS_BLENDOP, &p->opDev);
	}

	if(!forceAlpha)
		return;

	// Pin: canonical alpha on all three layers. L1 is what the game's own
	// per-prim RwRenderStateSet reads against (0x4A2D0B no-ops when the
	// cache already holds the value); L2/L3 make the DEVICE obey even when
	// L1 no-ops. Authored (5,2) prims still transition to their own pair.
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	RwD3D9SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	if(d3d9device){
		d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		d3d9device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		d3d9device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		d3d9device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	}
}

static void
FxDrawPinExit(FxDrawPin *p)
{
	// --- blend block: rw cache -> driver pending -> raw device -------------
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)p->vtxRw);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)p->srcRw);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)p->dstRw);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, p->vtxDrv);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, p->srcDrv);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, p->dstDrv);
	RwD3D9SetRenderState(D3DRS_BLENDOP, p->opDrv);

	// --- stage 0: force the rw cache transition, then raw-pin the device ---
	// A plain RwRenderStateSet(entry raster) no-ops whenever the cache still
	// holds that raster (the raw fx binds never moved it), leaving the device
	// on the last particle texture. Park on NULL first so the restore is a
	// guaranteed cache miss -> real device bind. When the entry raster is
	// NULL both L1 writes are no-ops, so the raw clear below is the ONLY
	// thing that can unbind a foreign particle texture - it must run.
	if(p->rasRw){
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)p->rasRw);
		// L3 raw pin of that raster's resolved texture (authoritative layer).
		// When the extraction fails the park-and-set above has already forced
		// the bind, so there is nothing more to pin.
		if(d3d9device){
			IDirect3DTexture9 *tex = fxRasterToTex(p->rasRw);
			if(tex)
				d3d9device->SetTexture(0, tex);
		}
	}else if(d3d9device){
		d3d9device->SetTexture(0, NULL);
	}

	if(d3d9device){
		d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, p->vtxDev);
		d3d9device->SetRenderState(D3DRS_SRCBLEND, p->srcDev);
		d3d9device->SetRenderState(D3DRS_DESTBLEND, p->dstDev);
		d3d9device->SetRenderState(D3DRS_BLENDOP, p->opDev);
	}

	// --- receipts ----------------------------------------------------------
	// Sampled at most once per DBGLOG_THROTTLE_MS: the stage-0 comparison
	// needs a device read (GetTexture = AddRef) and must not run per draw.
	// A mismatch means this hook left the frame-boundary state different
	// from what it found - the exact condition that makes the following
	// font/HUD quads sample a foreign texture or draw with blend OFF.
	if(d3d9device && g_diagLevel < 2){
		static DWORD s_fxPinReceiptTick = 0;
		DWORD now = GetTickCount();
		if(now - s_fxPinReceiptTick >= (DWORD)DBGLOG_THROTTLE_MS){
			s_fxPinReceiptTick = now;

			RwRaster *exitRas = NULL;
			RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &exitRas);
			if((uintptr_t)exitRas == (uintptr_t)0xFFFFFFFF)
				exitRas = NULL;
			// Expected device bind = the entry raster's own texture (or NULL
			// when the entry had no raster). Resolved without AddRef via the
			// 0xB4E9E0 extraction; when it cannot be resolved only the rw
			// layer is compared (no false positives).
			IDirect3DTexture9 *expectTex = p->rasRw ? fxRasterToTex(p->rasRw) : NULL;
			bool s0Bad = (exitRas != p->rasRw);
			IDirect3DBaseTexture9 *exitDev = NULL;
			bool devRead = false;
			if(!s0Bad && (!p->rasRw || expectTex)){
				d3d9device->GetTexture(0, &exitDev);	// AddRef - released below
				devRead = true;
				if(exitDev != (IDirect3DBaseTexture9*)expectTex)
					s0Bad = true;
			}
			if(s0Bad){
				if(dbglog_throttle("fx_s0leak"))
					dbglog("[fx_s0leak] fx render left stage0 raster=%p devTex=%p%s but entry was raster=%p tex=%p "
					       "(incomplete restore -> following font/HUD quads may sample a foreign texture)",
						(void*)exitRas, (void*)exitDev, devRead ? "" : " (not sampled)",
						(void*)p->rasRw, (void*)expectTex);
			}
			if(exitDev)
				exitDev->Release();

			DWORD eAb = 0, eSrc = 0, eDst = 0, eOp = 0;
			RwUInt32 rVtx = 0, rSrc = 0, rDst = 0;
			d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &eAb);
			d3d9device->GetRenderState(D3DRS_SRCBLEND, &eSrc);
			d3d9device->GetRenderState(D3DRS_DESTBLEND, &eDst);
			d3d9device->GetRenderState(D3DRS_BLENDOP, &eOp);
			RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &rVtx);
			RwRenderStateGet(rwRENDERSTATESRCBLEND, &rSrc);
			RwRenderStateGet(rwRENDERSTATEDESTBLEND, &rDst);
			if(eAb != p->vtxDev || eSrc != p->srcDev || eDst != p->dstDev || eOp != p->opDev ||
			   rVtx != p->vtxRw || rSrc != p->srcRw || rDst != p->dstRw){
				if(dbglog_throttle("fx_blendleak"))
					dbglog("[fx_blendleak] fx render left blend rw(vtx=%u src=%u dst=%u) dev(ab=%u src=%u dst=%u op=%u) "
					       "but entry was rw(vtx=%u src=%u dst=%u) dev(ab=%u src=%u dst=%u op=%u)",
						rVtx, rSrc, rDst, eAb, eSrc, eDst, eOp,
						p->vtxRw, p->srcRw, p->dstRw, p->vtxDev, p->srcDev, p->dstDev, p->opDev);
			}
		}
	}
}

static injector::hook_back<void(__fastcall*)(void* _this, int edx, int a1, int a2, int a3)>
	s_fxSysRender;

static void __fastcall
FxSysRenderHook(void* _this, int edx, int a1, int a2, int a3)
{
	// POD locals only - SEH fence (C2712-safe), same doctrine as the
	// chars.cpp/postfx passes: a fault in the identification or the byte
	// rewrite must degrade to a vanilla pass-through, never skip the
	// restore of bytes already flipped.
	bool splash = false;
	bool smoke = false;
	bool submerged = false;
	bool pinAlpha = false;
	bool callFaulted = false;
	DWORD callFaultCode = 0;
	signed char primCount = 0;
	void **primList = NULL;
	unsigned rewriteMask = 0;
	int rewritten = 0;
	FxDrawPin pinState;

	__try{
		splash = IsWaterSplashSystem(_this);
		smoke = !splash && IsWhiteSmokeSystem(_this);
		if(splash || smoke){
			if(splash && CWeather__UnderWaterness > 0.339731634f){
				submerged = true;	// draw gate; nothing flipped
			}else{
				primCount = *(signed char*)((char*)_this + 0x1b);
				primList = *(void***)((char*)_this + 0x1c);
				if(primCount > 0 && primList){
					for(int i = 0; i < primCount && i < 32; i++){
						BYTE *prim = (BYTE*)primList[i];
						// vtable gate: only a live render prim's byte 6 is
						// the dst blend ID; anything else must not be
						// touched (recycled/foreign object protection).
						if(IsFxRenderPrim(prim) && prim[6] == 1){
							prim[6] = 5;	// rwBLENDONE -> rwBLENDINVSRCALPHA (LUT 0x8A6230)
							rewriteMask |= (1u << i);
							rewritten++;
						}
					}
				}
			}
		}
	}__except(EXCEPTION_EXECUTE_HANDLER){
		// Keep rewriteMask/primList/primCount exactly as they were: every
		// byte counted in the mask was already flipped and MUST be restored.
		// splash/smoke stay true so the restore path below still runs.
	}

	if(splash && submerged){
		// Log-hygiene (2026-11-02): recurring per-frame receipt ->
		// g_diagLevel>=1 only; the first hit is always logged (one-shot).
		if(dbglog_once("splashfoam_gate_first") ||
		   (g_diagLevel >= 1 && dbglog_throttle("splashfoam_gate")))
			dbglog("[splashfoam] underwater gate: splash system render skipped (uw=%.2f)",
				CWeather__UnderWaterness);
		return;
	}

	// Per-draw state pin: EVERY wrapped draw gets the three-layer stage-0 +
	// blend capture/restore (font-safety - the raw texture bind left behind
	// by the prim renderer makes the next font/HUD stage-0 set a cache hit
	// that no-ops, so the glyphs sample the last particle texture).
	// Matched draws ADDITIONALLY get the canonical alpha block forced, since
	// the game's per-prim RwRenderStateSet no-ops against a matching cache
	// and a split leaves the smoke/splash prims drawing REPLACE. The exit is
	// unconditional (also on the fault path below) - the pin must be
	// invisible to every later consumer.
	pinAlpha = (splash || smoke) && !submerged;
	FxDrawPinEnter(&pinState, pinAlpha);

	// Fault fence around the original: a fault inside the game's render must
	// not strand the pin (blend forced ON, stage-0 parked) for the rest of
	// the frame - that is the state that breaks every later FFP draw. Same
	// doctrine as RenderReflectionScene/WaterDrops::Render: degrade to a
	// pass-through, always run the restore.
	__try{
		s_fxSysRender.fun(_this, edx, a1, a2, a3);
	}__except(EXCEPTION_EXECUTE_HANDLER){
		callFaulted = true;
		callFaultCode = GetExceptionCode();
	}

	FxDrawPinExit(&pinState);

	if(rewriteMask){
		for(int i = 0; i < primCount && i < 32; i++){
			if(!(rewriteMask & (1u << i)))
				continue;
			// Per-prim fence: one bad/unmapped entry must never strand the
			// remaining flipped bytes (a half-restored primList would leave
			// dst=INVSRCALPHA on prims whose blueprint authored ONE). The
			// same __try also covers the primList[i] read: if the array was
			// freed during the draw, the next iteration re-faults and is
			// skipped, instead of abandoning the whole loop.
			__try{
				BYTE *prim = (BYTE*)primList[i];
				// Re-verify the signature AFTER the original call: if the
				// prim was freed/recycled during the draw, its first dword
				// no longer holds the render-prim vtable and writing our
				// byte back would corrupt the new occupant. Skipping the
				// restore in that case is correct - the object is no
				// longer the prim we flipped.
				if(IsFxRenderPrim(prim) && prim[6] == 5)
					prim[6] = 1;
			}__except(EXCEPTION_EXECUTE_HANDLER){
				if(dbglog_throttle("fx_primrestore"))
					dbglog("[splashfoam] prim[%d] restore faulted code=0x%08X - byte left at 5, "
					       "system re-seeds from its blueprint on next init",
						i, GetExceptionCode());
			}
		}
	}

	if(callFaulted && dbglog_throttle("fx_renderfault"))
		dbglog("[splashfoam] fxSystem_c::Render FAULT code=0x%08X - draw aborted, "
		       "pin restored (stage0 + blend), %d prim byte(s) restored",
			callFaultCode, rewritten);

	// Log-hygiene (2026-11-02): recurring per-draw receipt (splash +
	// white-smoke pin lines) -> g_diagLevel>=1 only; the first hit is
	// always logged (one-shot).
	if(pinAlpha &&
	   (dbglog_once("splashfoam_fix_first") ||
	    (g_diagLevel >= 1 && dbglog_throttle("splashfoam_fix"))))
		dbglog("[splashfoam] %s render: %d prim(s) dst ONE->INVSRCALPHA, blend pin ON for this draw",
			smoke ? "white-smoke system" : "splash system", rewritten);
}

void
hookSplashFoamFix(void)
{
	s_fxSysRender.fun = injector::MakeCALL(0x4A930A,
		static_cast<void(__fastcall*)(void*, int, int, int, int)>(FxSysRenderHook), true).get();
	dbglog("[splashfoam] fxSystem_c::Render hooked at call site 0x4A930A (orig=%p) - "
	       "water-splash + white-smoke dst blend ONE->INVSRCALPHA, per-draw blend pin, "
	       "per-draw stage-0 + blend capture/restore on every wrapped draw, "
	       "underwater splash draw gate",
		(void*)s_fxSysRender.fun);
}

void
hookWaterDrops()
{
	static injector::hook_back<void(*)()> CMotionBlurStreaksRender;
	auto CMotionBlurStreaksRenderHook = []()
	{
		CMotionBlurStreaksRender.fun();
		if(neoTxd && config->neoWaterDrops){
			WaterDrops::Process();
			WaterDrops::Render();
		}
	}; CMotionBlurStreaksRender.fun = injector::MakeCALL(0x726AD0, static_cast<void(*)()>(CMotionBlurStreaksRenderHook), true).get();

	static injector::hook_back<void(__fastcall*)(void* _this, int edx, int id, RwV3d* point)> CAEFireAudioEntityAddAudioEvent;
	auto CAEFireAudioEntityAddAudioEventHook = [](void* _this, int edx, int id, RwV3d* point)
	{
		if(config->neoWaterDrops){
			RwV3d dist;
			RwV3dSub(&dist, point, &WaterDrops::ms_lastPos);
			if(RwV3dLength(&dist) <= 10.0f)
				WaterDrops::RegisterSplash(point, 20.0f, 20);
		}
		return CAEFireAudioEntityAddAudioEvent.fun(_this, edx, id, point);
	}; CAEFireAudioEntityAddAudioEvent.fun = injector::MakeCALL(0x4AAE2D, static_cast<void(__fastcall*)(void*, int, int, RwV3d*)>(CAEFireAudioEntityAddAudioEventHook), true).get(); //water_hydrant
	injector::MakeCALL(0x4AAE4B, static_cast<void(__fastcall*)(void*, int, int, RwV3d*)>(CAEFireAudioEntityAddAudioEventHook), true); //water_fountain
	injector::MakeCALL(0x4AAE69, static_cast<void(__fastcall*)(void*, int, int, RwV3d*)>(CAEFireAudioEntityAddAudioEventHook), true); //water_fnt_tme

	//FxManager_c::CreateFxSystem
	static injector::hook_back<void*(__fastcall*)(void*, int, char*, RwV3d*, RwMatrix*, char)> CreateFxSystem;
	auto CreateFxSystemHook = [](void* _this, int edx, char* name, RwV3d* point, RwMatrix* m, char flag) -> void*
	{
		if(config->neoWaterDrops){
			RwV3d dist;
			RwV3dSub(&dist, point, &WaterDrops::ms_lastPos);
			if(RwV3dLength(&dist) <= 10.0f)
				WaterDrops::RegisterSplash(point, 10.0f, 1);
		}
		return CreateFxSystem.fun(_this, edx, name, point, m, flag);
	};
	auto f = static_cast<void*(__fastcall*)(void*, int, char*, RwV3d*, RwMatrix*, char)>(CreateFxSystemHook);
	CreateFxSystem.fun = injector::MakeCALL(0x4A10C9, f, true).get();// "water_splash_big"
	injector::MakeCALL(0x4A1139, f, true); // "water_splash"
	injector::MakeCALL(0x4A11A9, f, true); // "water_splsh_sml"
	//injector::MakeCALL(0x68AEBA, f, true); // "water_swim"
	//injector::MakeCALL(0x68AF15, f, true); // "water_swim"
	//injector::MakeCALL(0x68AF66, f, true); // "water_swim"
	//injector::MakeCALL(0x68AFB3, f, true); // "water_swim"

	//AddParticle
	struct Fx_c {
		void* prt_blood;
		void* prt_boatsplash;
		void* prt_bubble;
		void* prt_cardebris;
		void* prt_collisionsmoke;
		void* prt_gunshell;
		void* prt_sand;
		void* prt_sand2;
		void* prt_smoke_huge;
		void* prt_smokeII_3_expand;
		void* prt_spark;
		void* prt_spark_2;
		void* prt_splash;
		void* prt_wake;
		void* prt_watersplash;
		void* prt_wheeldirt;
		void* prt_glass;
		//...
	};
	static Fx_c &g_fx = *(Fx_c*)0xA9AE00;
	static injector::hook_back<void*(__fastcall*)(void*, int, RwV3d*, RwV3d*, float, void*, float, float, float, unsigned char)> AddParticle;
	auto AddParticleHook = [](void* _this, int edx, RwV3d* position, RwV3d* velocity, float arg2, void* prtMult, float arg4, float brightness, float arg6, unsigned char arg7) -> void*
	{
		if(config->neoWaterDrops){
			RwV3d dist;
			RwV3dSub(&dist, position, &WaterDrops::ms_lastPos);
			float pd = 20.0f;
			bool isBlood = false;
			if(_this == g_fx.prt_blood) { pd = 5.0; isBlood = true; }
			else if(_this == g_fx.prt_boatsplash) { pd = 40.0; }
			else if(_this == g_fx.prt_splash) { pd = 15.0; }
			else if(_this == g_fx.prt_wake) { pd = 10.0; }
			else if(_this == g_fx.prt_watersplash) { pd = 30.0; }

			float len = RwV3dLength(&dist);
			// AUDIT: len==0 makes 1.0f/(len/2.0f) = +inf and the int loop
			// count in FillScreenMoving an undefined float->int conversion.
			if(len > 0.0f && len <= pd)
				WaterDrops::FillScreenMoving(1.0f / (len / 2.0f), isBlood);
		}
		return AddParticle.fun(_this, edx, position, velocity, arg2, prtMult, arg4, brightness, arg6, arg7);
	};
	auto f2 = static_cast<void*(__fastcall*)(void*, int, RwV3d*, RwV3d*, float, void*, float, float, float, unsigned char)>(AddParticleHook);
	//injector::MakeCALL(0x72AD55, f2, true); // "prt_splash"
	AddParticle.fun = injector::MakeCALL(0x7294A6, f2, true).get(); // "prt_watersplash"
	//injector::MakeCALL(0x6DE21A, f2, true); // "prt_splash"
	injector::MakeCALL(0x6DD6C5, f2, true); // "prt_boatsplash"
	injector::MakeCALL(0x6DD589, f2, true); // "prt_boatsplash"
	injector::MakeCALL(0x6C3ABE, f2, true); // "prt_wake"
	injector::MakeCALL(0x6C3939, f2, true); // "prt_watersplash"
	injector::MakeCALL(0x6AA86F, f2, true); // "prt_watersplash"
	injector::MakeCALL(0x68ADE2, f2, true); // "prt_wake"
	injector::MakeCALL(0x5E7649, f2, true); // "prt_watersplash"
	//injector::MakeCALL(0x5E74DC, f2, true); // "prt_splash"
	//injector::MakeCALL(0x5E37AF, f2, true); // "prt_splash"
	//injector::MakeCALL(0x5E3782, f2, true); // "prt_splash"
	injector::MakeCALL(0x49FEEE, f2, true); // "prt_boatsplash"
	//injector::MakeCALL(0x49F01F, f2, true); // "prt_blood"
	injector::MakeCALL(0x49EC92, f2, true); // "prt_blood"

	//chainsaw
	static injector::hook_back<void(__fastcall*)(void* _this, int edx, int eventID)> CAEPedWeaponAudioEntityAddAudioEvent;
	auto CAEPedWeaponAudioEntityAddAudioEventHook = [](void* _this, int edx, int eventID)
	{
		if(config->neoWaterDrops)
			WaterDrops::FillScreenMoving(1.0f, true);
		return CAEPedWeaponAudioEntityAddAudioEvent.fun(_this, edx, eventID);
	}; CAEPedWeaponAudioEntityAddAudioEvent.fun = injector::MakeCALL(0x61CD73, static_cast<void(__fastcall*)(void*, int, int)>(CAEPedWeaponAudioEntityAddAudioEventHook), true).get();

	//harvester
	static injector::hook_back<void(__cdecl*)(CEntity*)> WorldAdd;
	auto WorldAddHook = [](CEntity* entity)
	{
		WorldAdd.fun(entity);
		
		if(config->neoWaterDrops){
			RwV3d dist;
			RwV3dSub(&dist, (RwV3d*)&entity->GetPosition(), &WaterDrops::ms_lastPos);
			if(RwV3dLength(&dist) <= 20.0f)
				WaterDrops::FillScreenMoving(0.5f, true);
		}
	};
	WorldAdd.fun = injector::MakeCALL(0x6A9BE2, static_cast<void(__cdecl*)(CEntity*)>(WorldAddHook), true).get();
}

void
WaterDrops::RegisterSplash(RwV3d* point, float distance, int duration)
{
	RwV3d dist;
	RwV3dSub(&dist, point, &ms_lastPos);

	if(RwV3dLength(&dist) <= distance)
		ms_splashDuration = duration;
}

void
WaterDrops::InitialiseRender(RwCamera *cam)
{
	srand(time(NULL));

	// AUDIT: every D3D9 call below was unchecked — a failed CreateVertexBuffer
	// stored a NULL/garbage pointer in ms_vertexBuf while ms_initialised was
	// still set to 1, so the next Render() crashed on vbuf->Lock. Check every
	// result; ms_initialised is only set once the buffers are real.
	if(!d3d9device)
		return;

	IDirect3DVertexBuffer9 *vbuf = NULL;
	IDirect3DIndexBuffer9 *ibuf = NULL;
	if(FAILED(d3d9device->CreateVertexBuffer(MAXDROPS * 4 * sizeof(VertexTex2), D3DUSAGE_WRITEONLY, DROPFVF, D3DPOOL_MANAGED, &vbuf, nullptr)) || !vbuf)
		return;
	if(FAILED(d3d9device->CreateIndexBuffer(MAXDROPS * 6 * sizeof(short), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ibuf, nullptr)) || !ibuf){
		vbuf->Release();
		return;
	}
	ms_vertexBuf = vbuf;
	ms_indexBuf = ibuf;
	RwUInt16 *idx = NULL;
	if(FAILED(ibuf->Lock(0, 0, (void**)&idx, 0)) || !idx){
		ibuf->Release();
		vbuf->Release();
		ms_vertexBuf = NULL;
		ms_indexBuf = NULL;
		return;
	}
	for(int i = 0; i < MAXDROPS; i++){
		idx[i * 6 + 0] = i * 4 + 0;
		idx[i * 6 + 1] = i * 4 + 1;
		idx[i * 6 + 2] = i * 4 + 2;
		idx[i * 6 + 3] = i * 4 + 0;
		idx[i * 6 + 4] = i * 4 + 2;
		idx[i * 6 + 5] = i * 4 + 3;
	}
	ibuf->Unlock();

	// We'll use the postfx frontbuffer as raster
	ms_tex = RwTextureCreate(nil);
	if(ms_tex)
		ms_tex->filterAddressing = 0x3302;

	ms_initialised = 1;
}

void
WaterDrops::Process(void)
{
	// AUDIT: Scene.camera/frameBuffer derefed unchecked — every other render
	// side entry in this TU (RenderEnvTex, Render) guards them first. Bail
	// before any state touch if the camera is not up yet.
	if(!Scene.camera || !Scene.camera->frameBuffer)
		return;
	// In case resolution changes
	ms_fbWidth = Scene.camera->frameBuffer->width;
	ms_fbHeight = Scene.camera->frameBuffer->height;
	scaling = ms_fbHeight / 480.0f;

	if(!ms_initialised)
		InitialiseRender(Scene.camera);
	WaterDrops::CalculateMovement();
	WaterDrops::SprayDrops();
	WaterDrops::ProcessMoving();
	WaterDrops::Fade();
}

void
WaterDrops::CalculateMovement(void)
{
	RwMatrix *modelMatrix;
	modelMatrix = &RwCameraGetFrame(Scene.camera)->modelling;
	RwV3dSub(&ms_posDelta, &modelMatrix->pos, &ms_lastPos);
	ms_distMoved = RwV3dLength(&ms_posDelta);
	// RwV3d pos;
	// pos.x = (modelMatrix->at.x - ms_lastAt.x) * 10.0f;
	// pos.y = (modelMatrix->at.y - ms_lastAt.y) * 10.0f;
	// pos.z = (modelMatrix->at.z - ms_lastAt.z) * 10.0f;
	// ^ result unused for now
	ms_lastAt = modelMatrix->at;
	ms_lastPos = modelMatrix->pos;

	ms_vec.x = -RwV3dDotProduct(&modelMatrix->right, &ms_posDelta);
	ms_vec.y = RwV3dDotProduct(&modelMatrix->up, &ms_posDelta);
	ms_vec.z = RwV3dDotProduct(&modelMatrix->at, &ms_posDelta);
	RwV3dScale(&ms_vec, &ms_vec, 10.0f);
	ms_vecLen = sqrt(ms_vec.y*ms_vec.y + ms_vec.x*ms_vec.x);

	short mode = getCamMode();
	bool istopdown = mode == MODE_TOPDOWN || mode == MODE_TWOPLAYER_SEPARATE_CARS_TOPDOWN;
	bool carlookdirection = 0;
	if(mode == MODE_1STPERSON && FindPlayerVehicle(-1, 0)){
		CPad *p = CPad::GetPad(0);
		if(p->GetLookBehindForCar() || p->GetLookLeft() || p->GetLookRight())
			carlookdirection = 1;
	}
	ms_enabled = !istopdown && !carlookdirection;
	ms_movingEnabled = !istopdown && !carlookdirection;

	float c = modelMatrix->at.z;
	if(c > 1.0f) c = 1.0f;
	if(c < -1.0f) c = -1.0f;
	ms_rainStrength = RAD2DEG(acos(c));
}

// Not quite sure what to do with these two...

bool
WaterDrops::NoDrops(void)
{
	return CWeather__UnderWaterness > 0.339731634f || *CEntryExitManager__ms_exitEnterState != 0;
}

bool
WaterDrops::NoRain(void)
{
	return CCullZones__CamNoRain() || CCullZones__PlayerNoRain() || *CGame__currArea != 0 || NoDrops();
	//	return CCullZones__CamNoRain() || CCullZones__PlayerNoRain() || CWeather__UnderWaterness > 0.0f || *CGame__currArea != 0 || *CEntryExitManager__ms_exitEnterState != 0;
}

WaterDrop*
WaterDrops::PlaceNew(float x, float y, float size, float ttl, bool fades, int R = 0xFF, int G = 0xFF, int B = 0xFF)
{
	WaterDrop *drop;
	int i;

	if(NoDrops())
		return NULL;

	for(i = 0, drop = ms_drops; i < MAXDROPS; i++, drop++)
		if(ms_drops[i].active == 0)
			goto found;
	return NULL;
found:
	ms_numDrops++;
	drop->x = x;
	drop->y = y;
	drop->size = size;
	drop->uvsize = (SC(MAXSIZE) - size + 1.0f) / (SC(MAXSIZE) - SC(MINSIZE) + 1.0f);
	drop->fades = fades;
	drop->active = 1;
	drop->r = R;
	drop->g = G;
	drop->b = B;
	drop->alpha = 0xFF;
	drop->time = 0.0f;
	drop->ttl = ttl;
	return drop;
}

void
WaterDrops::NewDropMoving(WaterDrop *drop)
{
	WaterDropMoving *moving;
	for(moving = ms_dropsMoving; moving < &ms_dropsMoving[MAXDROPSMOVING]; moving++)
		if(moving->drop == NULL)
			goto found;
	return;
found:
	ms_numDropsMoving++;
	moving->drop = drop;
	moving->dist = 0.0f;
}

void
WaterDrops::FillScreenMoving(float amount, bool isBlood = false)
{
	int n = (ms_vec.z <= 5.0f ? 1.0f : 1.5f)*amount*20.0f;
	float x, y, time;
	WaterDrop *drop;

	// AUDIT: ms_fbWidth/ms_fbHeight are 0 until the first Process() ran
	// (render side); the gameplay-side particle hooks reach here first, and
	// rand() % 0 is an integer divide-by-zero fault. Also guard the
	// SC(MAXSIZE)-SC(MINSIZE) divisor below: at degenerate scaling (window
	// height < ~32px) both SC() terms floor to 0 and the modulo faults too.
	if(ms_fbWidth <= 0 || ms_fbHeight <= 0 || SC(MAXSIZE) <= SC(MINSIZE))
		return;

	if(isBlood && !config->neoBloodDrops)
		return;

	while(n--)
		if(ms_numDrops < MAXDROPS && ms_numDropsMoving < MAXDROPSMOVING){
			x = rand() % ms_fbWidth;
			y = rand() % ms_fbHeight;
			time = rand() % (SC(MAXSIZE) - SC(MINSIZE)) + SC(MINSIZE);
			if(!isBlood)
				drop = PlaceNew(x, y, time, 2000.0f, 1);
			else
				drop = PlaceNew(x, y, time, 2000.0f, 1, 0xFF, 0x00, 0x00);
			if(drop)
				NewDropMoving(drop);
		}
}

void
WaterDrops::SprayDrops(void)
{
	static int ndrops[] = {
		125, 250, 500, 1000, 1000,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};

	if(!NoRain() && CWeather__Rain != 0.0f && ms_enabled){
		int tmp = 180.0f - ms_rainStrength;
		if(tmp < 40) tmp = 40;
		FillScreenMoving((tmp - 40.0f) / 150.0f * CWeather__Rain * 0.5f);
	}
	if(sprayWater)
		FillScreenMoving(0.5f, false);
	if(sprayBlood)
		FillScreenMoving(0.5f, true);
	if(ms_splashDuration >= 0){
		if(ms_numDrops < MAXDROPS){
			//if(isIII()){
			//	if(ms_splashObject){
			//	RwV3d dist;
			//		RwV3dSub(&dist, &ms_splashObject->matrix.matrix.pos, &ms_lastPos);
			//		int n = ndrops[ms_splashDuration] * (1.0f - (RwV3dLength(&dist) - 5.0f) / 15.0f);
			//		while(n--)
			//			if(ms_numDrops < MAXDROPS){
			//				float x = rand() % ms_fbWidth;
			//				float y = rand() % ms_fbHeight;
			//				float time = rand() % (SC(MAXSIZE) - SC(MINSIZE)) + SC(MINSIZE);
			//				PlaceNew(x, y, time, 10000.0f, 0);
			//			}
			//	}else
			//		FillScreenMoving(1.0f);
			//}else
			FillScreenMoving(1.0f); // VC does STRANGE things here
		}
		ms_splashDuration--;
	}
}

void
WaterDrops::NewTrace(WaterDropMoving *moving)
{
	if(ms_numDrops < MAXDROPS){
		moving->dist = 0.0f;
		PlaceNew(moving->drop->x, moving->drop->y, SC(MINSIZE), 500.0f, 1, moving->drop->r, moving->drop->g, moving->drop->b);
	}
}

void
WaterDrops::MoveDrop(WaterDropMoving *moving)
{
	WaterDrop *drop = moving->drop;
	if(!ms_movingEnabled)
		return;
	if(!drop->active){
		moving->drop = NULL;
		ms_numDropsMoving--;
		return;
	}
	if(ms_vec.z <= 0.0f || ms_distMoved <= 0.3f)
		return;

	short mode = getCamMode();
	if(ms_vecLen <= 0.5f || mode == MODE_1STPERSON){
		// movement out of center 
		float d = ms_vec.z*0.2f;
		float dx, dy, sum;
		dx = drop->x - ms_fbWidth * 0.5f + ms_vec.x;
		if(mode == MODE_1STPERSON)
			dy = drop->y - ms_fbHeight * 1.2f - ms_vec.y;
		else
			dy = drop->y - ms_fbHeight * 0.5f - ms_vec.y;
		sum = fabs(dx) + fabs(dy);
		if(sum >= 0.001f){
			dx *= (1.0 / sum);
			dy *= (1.0 / sum);
		}
		moving->dist += d;
		if(moving->dist > 20.0f)
			NewTrace(moving);
		drop->x += dx * d;
		drop->y += dy * d;
	}else{
		// movement when camera turns
		moving->dist += ms_vecLen;
		if(moving->dist > 20.0f)
			NewTrace(moving);
		drop->x -= ms_vec.x;
		drop->y += ms_vec.y;
	}

	if(drop->x < 0.0f || drop->y < 0.0f ||
		drop->x > ms_fbWidth || drop->y > ms_fbHeight){
		moving->drop = NULL;
		ms_numDropsMoving--;
	}
}

void
WaterDrops::ProcessMoving(void)
{
	WaterDropMoving *moving;
	if(!ms_movingEnabled)
		return;
	for(moving = ms_dropsMoving; moving < &ms_dropsMoving[MAXDROPSMOVING]; moving++)
		if(moving->drop)
			MoveDrop(moving);
}

void
WaterDrop::Fade(void)
{
	int delta = CTimer__ms_fTimeStep * 1000.0f / 50.0f;
	this->time += delta;
	if(this->time >= this->ttl){
		WaterDrops::ms_numDrops--;
		this->active = 0;
	}else if(this->fades)
		this->alpha = 255 - time / ttl * 255;
}

void
WaterDrops::Fade(void)
{
	WaterDrop *drop;
	for(drop = &ms_drops[0]; drop < &ms_drops[MAXDROPS]; drop++)
		if(drop->active)
			drop->Fade();
}

void
WaterDrops::AddToRenderList(WaterDrop *drop)
{
	static float xy[] = {
		-1.0f, -1.0f, -1.0f,  1.0f,
		1.0f,  1.0f,  1.0f, -1.0f
	};
	static float uv[] = {
		0.0f, 0.0f, 0.0f, 1.0f,
		1.0f, 1.0f, 1.0f, 0.0f
	};
	int i;
	float scale;

	float u1_1, u1_2;
	float v1_1, v1_2;
	float tmp;

	tmp = drop->uvsize*(300.0f - 40.0f) + 40.0f;
	u1_1 = drop->x + ms_xOff - tmp;
	v1_1 = drop->y + ms_yOff - tmp;
	u1_2 = drop->x + ms_xOff + tmp;
	v1_2 = drop->y + ms_yOff + tmp;
	u1_1 = (u1_1 <= 0.0f ? 0.0f : u1_1) / CPostEffects::pRasterFrontBuffer->width;
	v1_1 = (v1_1 <= 0.0f ? 0.0f : v1_1) / CPostEffects::pRasterFrontBuffer->height;
	u1_2 = (u1_2 >= ms_fbWidth ? ms_fbWidth : u1_2) / CPostEffects::pRasterFrontBuffer->width;
	v1_2 = (v1_2 >= ms_fbHeight ? ms_fbHeight : v1_2) / CPostEffects::pRasterFrontBuffer->height;

	scale = drop->size * 0.5f;

	for(i = 0; i < 4; i++){
		ms_vertPtr->x = drop->x + xy[i * 2] * scale + ms_xOff;
		ms_vertPtr->y = drop->y + xy[i * 2 + 1] * scale + ms_yOff;
		ms_vertPtr->z = 0.0f;
		ms_vertPtr->rhw = 1.0f;
		ms_vertPtr->emissiveColor = D3DCOLOR_ARGB(drop->alpha, drop->r, drop->g, drop->b);
		ms_vertPtr->u0 = uv[i * 2];
		ms_vertPtr->v0 = uv[i * 2 + 1];
		ms_vertPtr->u1 = i >= 2 ? u1_2 : u1_1;
		ms_vertPtr->v1 = i % 3 == 0 ? v1_2 : v1_1;
		ms_vertPtr++;
	}
	ms_numBatchedDrops++;
}

void
WaterDrops::Render(void)
{
	bool nofirstperson = FindPlayerVehicle(-1, 0) == 0 && getCamMode() == MODE_1STPERSON;
	if(!ms_enabled || ms_numDrops <= 0 || nofirstperson || CCutsceneMgr__ms_running)
		return;

	//when you put camera underwater, droplets disappear instantly instead of fading out
	if(NoDrops()){
		Clear();
		return;
	}

	IDirect3DVertexBuffer9 *vbuf = (IDirect3DVertexBuffer9*)ms_vertexBuf;
	// AUDIT: ms_vertexBuf/ms_tex can be NULL (InitialiseRender creation
	// failure) and pRasterFrontBuffer before postfx init — AddToRenderList
	// divides by pRasterFrontBuffer->width. Bail before any state touch.
	if(!vbuf || !ms_tex || !CPostEffects::pRasterFrontBuffer)
		return;
	// AUDIT: a failed Lock leaves ms_vertPtr undefined — null it first so a
	// stale pointer from a previous frame can never be written through.
	ms_vertPtr = NULL;
	if(FAILED(vbuf->Lock(0, 0, (void**)&ms_vertPtr, 0)) || !ms_vertPtr)
		return;
	ms_numBatchedDrops = 0;
	for(WaterDrop *drop = &ms_drops[0]; drop < &ms_drops[MAXDROPS]; drop++)
		if(drop->active)
			AddToRenderList(drop);
	vbuf->Unlock();

	ms_tex->raster = CPostEffects::pRasterFrontBuffer;
	CPostEffects::UpdateFrontBuffer();

	// Three-layer save of SRC/DESTBLEND BEFORE any state write in this pass
	// (incl. DefinedState — if it re-canonicalises blend we still restore the
	// true pre-entry values). This pass writes SRC/DEST at L2 only and
	// historically never restored them: pending==applied at the leaked value
	// makes the next RwD3D9SetRenderState to that value a no-op, and a
	// pending!=applied pair flushes the leak straight back over any raw
	// restore (see postfx.cpp Save/RestoreRawGeomStates). Pre-seeded, never bare.
	RwUInt32 savedSrcRw = rwBLENDSRCALPHA, savedDstRw = rwBLENDINVSRCALPHA;
	DWORD savedSrcDrv = D3DBLEND_SRCALPHA, savedDstDrv = D3DBLEND_INVSRCALPHA;
	DWORD savedSrcDev = D3DBLEND_SRCALPHA, savedDstDev = D3DBLEND_INVSRCALPHA;
	DWORD savedOpDrv = D3DBLENDOP_ADD, savedOpDev = D3DBLENDOP_ADD;
	RwRenderStateGet(rwRENDERSTATESRCBLEND, &savedSrcRw);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, &savedDstRw);
	RwD3D9GetRenderState(D3DRS_SRCBLEND, &savedSrcDrv);
	RwD3D9GetRenderState(D3DRS_DESTBLEND, &savedDstDrv);
	// BLENDOP is part of the same blend block and the tail's DefinedState can
	// move it (canonical ADD). Restoring SRC/DEST but not OP would leave a
	// caller that ran a non-ADD op (subtract/etc.) with a mixed block.
	RwD3D9GetRenderState(D3DRS_BLENDOP, &savedOpDrv);
	if(d3d9device){
		d3d9device->GetRenderState(D3DRS_SRCBLEND, &savedSrcDev);
		d3d9device->GetRenderState(D3DRS_DESTBLEND, &savedDstDev);
		d3d9device->GetRenderState(D3DRS_BLENDOP, &savedOpDev);
	}

	// test2-0438 cause #3 pair: the tail's hygiene writes
	// (rwRENDERSTATETEXTURERASTER=NULL + VERTEXALPHAENABLE=0) used to go
	// out through the L1 rw cache only and NEVER come back - they crossed
	// the frame boundary and drip-fed stage-0-NULL / vtx-alpha-off into
	// every later FFP consumer (the same class as the water restore
	// window). Capture their entry values HERE (before ANY state write in
	// this pass - DefinedState itself forces VERTEXALPHA=0, so a capture
	// later in the tail would capture the leak value and restore it) plus
	// the device twins: vertex-alpha <-> D3DRS_ALPHABLENDENABLE and the
	// raster <-> the raw stage-0 bind. Pre-seeded, like the blend capture.
	RwRaster *savedStage0Ras = NULL;
	RwUInt32 savedVtxAlphaRw = 0;
	DWORD savedAlphaEnableDrv = TRUE, savedAlphaEnableDev = TRUE;
	RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &savedStage0Ras);
	if(savedStage0Ras == (RwRaster*)0xFFFFFFFF)
		savedStage0Ras = NULL;
	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &savedVtxAlphaRw);
	if(savedVtxAlphaRw == 0xFFFFFFFF)
		savedVtxAlphaRw = 0;
	RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE, &savedAlphaEnableDrv);
	if(d3d9device)
		d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &savedAlphaEnableDev);

	// Same frame-boundary class for the geometry block the pass canonicalises:
	// the tail used to restore LITERALS (fog=0 / ztest=1 / zwrite=1) instead
	// of the entry values, so a caller that ran with fog on - or a 2D/HUD span
	// drawn with ztest off - inherited the literals forever after. These are
	// L1-only writes in this pass (RwRenderStateSet), so an L1-only faithful
	// restore is symmetric: a cache hit means the pass never changed the value.
	RwUInt32 savedFogRw = 0, savedZTestRw = 1, savedZWriteRw = 1;
	RwRenderStateGet(rwRENDERSTATEFOGENABLE, &savedFogRw);
	if(savedFogRw == 0xFFFFFFFF) savedFogRw = 0;
	RwRenderStateGet(rwRENDERSTATEZTESTENABLE, &savedZTestRw);
	if(savedZTestRw == 0xFFFFFFFF) savedZTestRw = 1;
	RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &savedZWriteRw);
	if(savedZWriteRw == 0xFFFFFFFF) savedZWriteRw = 1;

	// AUDIT test-4: the tail restore below is the frame-boundary pair for the
	// entry captures above — but this function had NO SEH guard, so a fault in
	// the canonicalise/binds/draw span skipped the ENTIRE restore and stranded
	// stage0=ms_maskTex / vtx-alpha=1 / pass blend across the frame boundary
	// (the persistent stage-0 NULL drip-feed class this pass was fixed for).
	// Same doctrine as RenderReflectionScene/RenderEnvTex/FxSysRenderHook:
	// guard the risky span, run EVERY restore unconditionally below, fault or
	// not. C2712-safe: all locals in this function are POD.
	__try{
		DefinedState();
		RwRenderStateSet(rwRENDERSTATEFOGENABLE, 0);
		RwRenderStateSet(rwRENDERSTATEZTESTENABLE, 0);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, 0);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)1);

		RwD3D9SetTexture(ms_maskTex, 0);
		RwD3D9SetTexture(ms_tex, 1);

		RwD3D9SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
		RwD3D9SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
		RwD3D9SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		RwD3D9SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
		RwD3D9SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TEXTURE);
		RwD3D9SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		RwD3D9SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
		RwD3D9SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TEXTURE);
		RwD3D9SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_MODULATE);
		RwD3D9SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		RwD3D9SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
		RwD3D9SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		RwD3D9SetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
		RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

		RwD3D9SetFVF(DROPFVF);
		RwD3D9SetStreamSource(0, vbuf, 0, sizeof(VertexTex2));
		RwD3D9SetIndices(ms_indexBuf);
		RwD3D9DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, ms_numBatchedDrops * 4, 0, ms_numBatchedDrops * 2);

		// Breadcrumb for the outdoor-occluder repro: proves the overlay was LIVE
		// this frame and which front-buffer dims it sampled/aliased.
		if(dbglog_throttle("waterdrops"))
			dbglog("[WATERDROPS] overlay drew %d quads, frontBuffer raster=%p %dx%d",
				ms_numBatchedDrops, (void*)CPostEffects::pRasterFrontBuffer,
				CPostEffects::pRasterFrontBuffer ? CPostEffects::pRasterFrontBuffer->width : 0,
				CPostEffects::pRasterFrontBuffer ? CPostEffects::pRasterFrontBuffer->height : 0);
	}__except(EXCEPTION_EXECUTE_HANDLER){
		// Receipt: the overlay aborted mid-pass, but the blend restore, the
		// hygiene writes and the paired stage-0/vtx-alpha restore below all
		// still run — the tail can never be skipped by a fault here.
		dbglog("[WATERDROPS] guarded span FAULT code=0x%08X — overlay aborted, tail restore still runs",
			GetExceptionCode());
	}

	RwD3D9SetTexture(NULL, 1);
	RwD3D9SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	RwD3D9SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	DefinedState();

	// Blend restore AFTER DefinedState so these values win on overlap
	// (same ordering rule as postfx ReStore). L1 rw cache (rw-domain value),
	// L2 driver pending (D3D domain — must re-sync or the next flush re-leaks),
	// L3 raw device (authoritative).
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)savedSrcRw);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)savedDstRw);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, savedSrcDrv);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, savedDstDrv);
	RwD3D9SetRenderState(D3DRS_BLENDOP, savedOpDrv);
	if(d3d9device){
		d3d9device->SetRenderState(D3DRS_SRCBLEND, savedSrcDev);
		d3d9device->SetRenderState(D3DRS_DESTBLEND, savedDstDev);
		d3d9device->SetRenderState(D3DRS_BLENDOP, savedOpDev);
	}

	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)savedFogRw);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)savedZTestRw);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)savedZWriteRw);
	// The two hygiene writes below are wrapped in a paired save/restore
	// (captured at the entry save block above): they still run as this
	// pass's cleanup, but the function ENDS on the entry values instead of
	// leaking NULL/0 across the frame boundary (test2-0438 cause #3).
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)0);

	// Paired restore of the two states - all three layers. Raster: L1
	// park-and-set (a plain RwRenderStateSet no-ops when the cache already
	// holds the value - park on a guaranteed-different raster first, the
	// chars.cpp RestoreSSSStage0AndSampler pattern) + the L3 raw pin of the
	// raster's resolved IDirect3DTexture9 (extraction offset 0xB4E9E0 -
	// same proven path as main.cpp pedStage0Resync). Vertex-alpha: L1 + the
	// D3DRS_ALPHABLENDENABLE twins (L2 driver pending + L3 raw). These
	// writes come LAST so they win on any overlap with the cleanup above.
	{
		RwRaster *park = ms_maskTex ? ms_maskTex->raster : NULL;
		if(park == savedStage0Ras) park = ms_tex ? ms_tex->raster : NULL;
		if(park == savedStage0Ras) park = NULL;
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)park);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)savedStage0Ras);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)(savedVtxAlphaRw ? 0 : 1));
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)savedVtxAlphaRw);
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, savedAlphaEnableDrv);
		if(d3d9device){
			// L3 raw pin - an empty entry raster must still clear the
			// device bind (ms_maskTex would otherwise survive the
			// rw-level NULL and leak as stage 0).
			IDirect3DTexture9 *tex = fxRasterToTex(savedStage0Ras);
			if(tex || !savedStage0Ras)
				d3d9device->SetTexture(0, tex);
			d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, savedAlphaEnableDev);
		}
	}
}

/////////////////
void
WaterDrops::FillScreen(int n)
{
	float x, y, time;
	WaterDrop *drop;

	// AUDIT: same divide-by-zero family as FillScreenMoving — ms_fbWidth is 0
	// until the first Process() ran and rand() % 0 faults; SC(MAXSIZE)-
	// SC(MINSIZE) can floor to 0 at degenerate scaling. Also clamp n so the
	// &ms_drops[n] bound below stays inside the array.
	if(!ms_initialised)
		return;
	if(ms_fbWidth <= 0 || ms_fbHeight <= 0 || SC(MAXSIZE) <= SC(MINSIZE))
		return;
	if(n < 0)
		n = 0;
	if(n > MAXDROPS)
		n = MAXDROPS;
	ms_numDrops = 0;
	for(drop = &ms_drops[0]; drop < &ms_drops[MAXDROPS]; drop++){
		drop->active = 0;
		if(drop < &ms_drops[n]){
			x = rand() % ms_fbWidth;
			y = rand() % ms_fbHeight;
			time = rand() % (SC(MAXSIZE) - SC(MINSIZE)) + SC(MINSIZE);
			PlaceNew(x, y, time, 2000.0f, 1);
		}
	}
}

void
WaterDrops::Clear(void)
{
	WaterDrop *drop;
	for(drop = &ms_drops[0]; drop < &ms_drops[MAXDROPS]; drop++)
		drop->active = 0;
	ms_numDrops = 0;
}

void
WaterDrops::Reset(void)
{
	Clear();
	ms_splashDuration = -1;
}

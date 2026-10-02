#include "skygfx.h"
#include "main_exports.h"
#include "neo.h"
#include "waterPipe.h"
#include "chars.h"
#include "weather.h"
#include "Ragdoll.h"  // living-ped secondary motion
#include "extras/ragdoll_death.h"  // death ragdoll (bullet)
#include "ini_parser.hpp"
#include "debugmenu_public.h"
#include "ModuleList.hpp"
#include "diagnostics.h"
#include "menu_inject.h"
#include "postfx.h"
#include <injector\hooking.hpp>
#include <stdarg.h>
#include <stdio.h>
#include <excpt.h>

// Debug menu stubs (legacy — actual implementation in debugmenu_ui.cpp)

// normalmap_init() and normalmap_shutdown() are now in normalmap.cpp

// Performance timing globals
LARGE_INTEGER perfFreq = {0};
double perfFreqInv = 0.0;

HMODULE dllModule;
DebugMenuAPI gDebugMenuAPI;
char asipath[MAX_PATH];
volatile int g_allowCrashPassThrough = 0;

extern std::map<std::string, TexInfo*> texdb;
extern int32 texdbOffset;
//std::fstream lg;

// Only-once settings
//bool ps2grassFiles;
bool disableClouds;
bool disableGamma;
bool fixPcCarLight;
int explicitBuildingPipe;
int transparentLockon;
int fixShadows;
bool iCanHasNeoDrops = true;
bool iCanHasbuildingPipe = true;
bool iCanHasvehiclePipe = true;
bool iCanHasSunGlare = true;
bool gHasExternalNormalMapPlugin = false;
bool g_hasMoonLoader = false;  // Detected at startup for deferred hook installation



bool privateHooks;
bool forceWindShader;

int fixingSAMP;
HMODULE UG_mod;
typedef bool(*UG_EventHook)(void* pData);
void (*UG_RegisterEventCallback)(const char *type, UG_EventHook hook);

int numConfigs;
int currentConfig = 0;
Config configs[10];
Config *config = &configs[0];
int original_bRadiosity = 0;

void *grassPixelShader;
void *simplePS;
void *gpCurrentPixelShaderForDefaultCallbacks;
void *gpCurrentVertexShaderForDefaultCallbacks;
bool gRenderingSpheremap;
CVector reflectionCamPos;

static int defaultColourLeftUOffset;
static int defaultColourRightUOffset;
static int defaultColourTopVOffset;
static int defaultColourBottomVOffset;

DWORD& TempBufferVerticesStored = *reinterpret_cast<DWORD*>(0xC4B950);
DWORD& TempBufferIndicesStored = *reinterpret_cast<DWORD*>(0xC4B954);
RwImVertexIndex* TempBufferRenderIndexList = reinterpret_cast<RwImVertexIndex*>(0xC4B958);
RwIm3DVertex* TempVertexBuffer = reinterpret_cast<RwIm3DVertex*>(0xC4D958);

CVector2D windPos;

extern "C" {
__declspec(dllexport) Config*
GetConfig(void)
{
	return config;
}
}

char*
getpath(char *path)
{
	static char tmppath[MAX_PATH];
	FILE *f;

	f = fopen(path, "r");
	if(f){
		fclose(f);
		return path;
	}
	extern char asipath[];
	strncpy(tmppath, asipath, MAX_PATH);
	strncat(tmppath, path, sizeof(tmppath) - strlen(tmppath) - 1);
	f = fopen(tmppath, "r");
	if(f){
		fclose(f);
		return tmppath;
	}
	return NULL;
}

WRAPPER void _rwD3D9RenderStateFlushCache(void) { EAXJMP(0x7FC200); }

// SAMP fucks with the render states directly instead of using RW. Synching the fog states
// before and after rendering and using SAMP graphics restore seems to make things work.
void
fixSAMP(void)
{
	static D3DCAPS9 *Caps=(D3DCAPS9*)0xC9BF00;
	static int *FogConvTable=(int*)0x8848FC;
	if(fixingSAMP){
		int fog, fogtype;
		RwRenderStateGet(rwRENDERSTATEFOGENABLE, &fog);
		RwRenderStateGet(rwRENDERSTATEFOGTYPE, &fogtype);
		_rwD3D9RenderStateFlushCache();
		RwD3D9SetRenderState(D3DRS_FOGENABLE, fog);
		d3d9device->SetRenderState(D3DRS_FOGENABLE, fog);
		int table, vertex;
		if((Caps->RasterCaps & D3DPRASTERCAPS_FOGTABLE) && (Caps->RasterCaps & D3DPRASTERCAPS_WFOG)){
			table = FogConvTable[fogtype];
			vertex = D3DFOG_NONE;
		}else{
			table = D3DFOG_NONE;
			vertex = FogConvTable[fogtype];
		}
		RwD3D9SetRenderState(D3DRS_FOGTABLEMODE, table);
		RwD3D9SetRenderState(D3DRS_FOGVERTEXMODE, vertex);
		d3d9device->SetRenderState(D3DRS_FOGTABLEMODE, table);
		d3d9device->SetRenderState(D3DRS_FOGVERTEXMODE, vertex);
	}
}

void
D3D9Render(RxD3D9ResEntryHeader *resEntryHeader, RxD3D9InstanceData *instanceData)
{
	fixSAMP();
	if(resEntryHeader->indexBuffer)
		RwD3D9DrawIndexedPrimitive(resEntryHeader->primType, instanceData->baseIndex, 0, instanceData->numVertices, instanceData->startIndex, instanceData->numPrimitives);
	else
		RwD3D9DrawPrimitive(resEntryHeader->primType, instanceData->baseIndex, instanceData->numPrimitives);
}

// ---- dual-pass leg-state two-layer helper ----
// The dual legs below used RwRenderStateSet only (layer 1: the rw cache). A
// device that had drifted behind an unchanged rw cache (postfx raw writes,
// DepthHook raw ZENABLE, the fullscreen Im2D passes) then ran the SAME alpha
// test for both legs, so the below-threshold geometry leg 2 is meant to paint
// rendered solid instead — the black jagged rotor triangles (rotors draw as
// vehicle geometry through this dual pass). Push every leg state through all
// three layers (rw cache -> RW driver cache -> raw device) so the two legs
// really differ on the device. rw func 1..8 maps identity to D3DCMP_* (the
// alpha LUT is 1..8 identity), so pipeAlphaFuncToD3D is safe here.
extern RwUInt32 pipeAlphaFuncToD3D(RwUInt32 rwFunc);

static void
setAlphaTestTwoLayer(RwUInt32 rwFunc, int ref)
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
setZWriteTwoLayer(RwBool on)
{
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)on);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, on ? TRUE : FALSE);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, on ? TRUE : FALSE);
}

void
D3D9RenderDual(int dual, RxD3D9ResEntryHeader *resEntryHeader, RxD3D9InstanceData *instanceData, TexInfo *texInfo)
{
	RwBool hasAlpha = FALSE;
	int alphafunc = rwALPHATESTFUNCTIONGREATEREQUAL;
	int alpharef = 1;
	int zwrite = TRUE;
	// this also takes texture alpha into account
	RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE, &hasAlpha);
	RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &zwrite);
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &alphafunc);
	// Read the ref up-front: the !zwrite branch below also restores it, and a
	// failed Get must never leave an uninitialised value on the device.
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, &alpharef);
	// Sanitize the saved state before any restore write: a failed Get (out-
	// param untouched) or a stale rw-NA(0) cache slot must not push garbage
	// ALPHAFUNC/ALPHAREF to the raw device — that parks the device on a
	// geometry-rejecting alpha test (all alpha-tested outdoor geometry
	// vanishes). Same convention as CSkidmarks__Render below.
	if(alphafunc < rwALPHATESTFUNCTIONNEVER || alphafunc > rwALPHATESTFUNCTIONALWAYS)
		alphafunc = rwALPHATESTFUNCTIONGREATEREQUAL;
	if(alpharef < 0) alpharef = 0;
	if(alpharef > 255) alpharef = 255;
	if (texInfo && !texInfo->dualPass) {
		dual = false;
	}
	if (dual && hasAlpha && zwrite) {
		int zwriteThreshold = config->zwriteThreshold;
		if (texInfo && texInfo->zwriteThreshold > 0) {
			zwriteThreshold = texInfo->zwriteThreshold;
		}
		// Clamp to 254: at 255 leg 1 (GREATEREQUAL@255) rejects nothing but
		// leg 2 (LESS@255) also rejects fully-opaque texels (alpha==255 fails
		// LESS) — a latent blank-everything mode. Cheap insurance; INI
		// default is 128.
		if(zwriteThreshold > 254) zwriteThreshold = 254;
		setAlphaTestTwoLayer(rwALPHATESTFUNCTIONGREATEREQUAL, zwriteThreshold);
		setZWriteTwoLayer(TRUE);
		D3D9Render(resEntryHeader, instanceData);
		setAlphaTestTwoLayer(rwALPHATESTFUNCTIONLESS, zwriteThreshold);
		setZWriteTwoLayer(FALSE);
		D3D9Render(resEntryHeader, instanceData);
		setZWriteTwoLayer(zwrite);
		setAlphaTestTwoLayer((RwUInt32)alphafunc, alpharef);
	}else if(!zwrite){
		setAlphaTestTwoLayer(rwALPHATESTFUNCTIONALWAYS, alpharef);
		D3D9Render(resEntryHeader, instanceData);
		setAlphaTestTwoLayer((RwUInt32)alphafunc, alpharef);
	}else
		D3D9Render(resEntryHeader, instanceData);
}

// Add a dual pass to the PC pipeline, the lazy way
void
D3D9RenderBlack_DUAL(RxD3D9ResEntryHeader *resEntryHeader, RxD3D9InstanceData *instanceData)
{
	RwD3D9SetPixelShader(NULL);
	RwD3D9SetVertexShader(instanceData->vertexShader);
	D3D9RenderDual(config->dualPassVehicle, resEntryHeader, instanceData);
}

void
D3D9RenderDefault_DUAL(RxD3D9ResEntryHeader *resEntryHeader, RxD3D9InstanceData *instanceData, RwUInt8 flags, RwTexture *texture)
{
	if(flags & (rxGEOMETRY_TEXTURED2 | rxGEOMETRY_TEXTURED)){
		RwD3D9SetTexture(texture, 0);
		RwD3D9SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		RwD3D9SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		RwD3D9SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		RwD3D9SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		RwD3D9SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		RwD3D9SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	}else{
		RwD3D9SetTexture(NULL, 0);
		RwD3D9SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
		RwD3D9SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		RwD3D9SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
		RwD3D9SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	}
	RwD3D9SetPixelShader(NULL);
	RwD3D9SetVertexShader(instanceData->vertexShader);
	D3D9RenderDual(config->dualPassVehicle, resEntryHeader, instanceData);
}

WRAPPER double CTimeCycle_GetAmbientRed(void) { EAXJMP(0x560330); }
WRAPPER double CTimeCycle_GetAmbientGreen(void) { EAXJMP(0x560340); }
WRAPPER double CTimeCycle_GetAmbientBlue(void) { EAXJMP(0x560350); }

void
resetValues(void)
{
	CPostEffects::m_bRadiosity = config->doRadiosity;
	CPostEffects::m_RadiosityFilterPasses = config->radiosityFilterPasses;
	CPostEffects::m_RadiosityRenderPasses = config->radiosityRenderPasses;
	CPostEffects::m_RadiosityIntensity = config->radiosityIntensity;

	CPostEffects::m_colourLeftUOffset = config->offLeft;
	CPostEffects::m_colourRightUOffset = config->offRight;
	CPostEffects::m_colourTopVOffset = config->offTop;
	CPostEffects::m_colourBottomVOffset = config->offBottom;

	if(config->grainFilter == 0){
		CPostEffects::m_InfraredVisionGrainStrength = 0x18;
		CPostEffects::m_NightVisionGrainStrength = 0x10;
	}else{
		CPostEffects::m_InfraredVisionGrainStrength = 0x40;
		CPostEffects::m_NightVisionGrainStrength = 0x30;
	}

	CPostEffects::m_bYCbCrFilter = config->bYCbCrFilter;
	CPostEffects::m_lumaScale = config->lumaScale;
	CPostEffects::m_lumaOffset = config->lumaOffset;
	CPostEffects::m_crScale = config->crScale;
	CPostEffects::m_crOffset = config->crOffset;
	CPostEffects::m_cbScale = config->cbScale;
	CPostEffects::m_cbOffset = config->cbOffset;

	// night vision ambient green
	// not if this is the correct switch, maybe ps2ModulateWorld?
	//if(config->nightVision == 0)
	//	Patch<float>(0x735F8B, 0.4f);
	//else
	//	Patch<float>(0x735F8B, 1.0f);

	//*(int*)0x8D37D0 = config->detailedWaterDist;

	// how to handle forced z-test in PC version
	switch(config->coronaZtest){
	case 0:
		// Disable (PS2)
		Nop(0x6FB17C, 3);
		break;
	case 1:
		// Enable (PC)
		Patch(0x6FB17C + 0, (uint8)0xFF);
		Patch(0x6FB17C + 1, (uint8)0x51);
		Patch(0x6FB17C + 2, (uint8)0x20);
		break;
	case -1:
		// don't touch
		break;
	}

}

// Env reflection sliders are hard-ranged [0..0.4] (ImGui sliders in
// debugmenu_ui.cpp; readIni clamps at load too). Single source of truth for
// that range, applied at INI load (readIniFile), after every reload/
// setConfig (refreshIni) and before every save (saveConfigTo) — no path can
// carry >0.4 into memory or onto disk (screenshot m0116 showed 2.700).
static void
clampEnvSliderRanges(Config *c)
{
	if(!c)
		return;
	if(c->envFresnel < 0.0f || c->envFresnel > 0.4f){
		if(dbglog_throttle("envf_clamp"))
			dbglog("envFresnel=%.4f outside [0..0.4], clamping", c->envFresnel);
		c->envFresnel = (c->envFresnel < 0.0f) ? 0.0f : 0.4f;
	}
	if(c->vehEnvIntensity < 0.0f || c->vehEnvIntensity > 0.4f){
		if(dbglog_throttle("envi_clamp"))
			dbglog("vehEnvIntensity=%.4f outside [0..0.4], clamping", c->vehEnvIntensity);
		c->vehEnvIntensity = (c->vehEnvIntensity < 0.0f) ? 0.0f : 0.4f;
	}
}

// Tonemap exposure + user-curve bounds (ImGui sliders in debugmenu_ui.cpp:
// Min Exposure [-0.5..0.5], Max Exposure ends at 1.0, both floors -0.5 for
// deliberate darkening; then Min <= Max enforced by pinning min = max;
// curve lows/highs [-1..1], curve mid [0..1]). Same env-clamp pattern:
// applied at INI load (readIniFile), after every reload/setConfig
// (refreshIni) and before every save (saveConfigTo) so no path persists
// out-of-range values (INI read default for max was 2.0).
static void
clampTonemapExposureRanges(Config *c)
{
	if(!c)
		return;
	if(c->tonemapMinExposure < -0.5f || c->tonemapMinExposure > 0.5f){
		if(dbglog_throttle("tmin_clamp"))
			dbglog("tonemapMinExposure=%.4f outside [-0.5..0.5], clamping", c->tonemapMinExposure);
		c->tonemapMinExposure = (c->tonemapMinExposure < -0.5f) ? -0.5f : 0.5f;
	}
	if(c->tonemapMaxExposure < -0.5f || c->tonemapMaxExposure > 1.0f){
		if(dbglog_throttle("tmax_clamp"))
			dbglog("tonemapMaxExposure=%.4f outside [-0.5..1.0], clamping", c->tonemapMaxExposure);
		c->tonemapMaxExposure = (c->tonemapMaxExposure < -0.5f) ? -0.5f : 1.0f;
	}
	// Enforce Min <= Max AFTER both are in range (pin min = max) — HLSL
	// clamp(x, min, max) is undefined when min > max.
	if(c->tonemapMinExposure > c->tonemapMaxExposure){
		if(dbglog_throttle("texp_order"))
			dbglog("tonemapMin>Max (%.3f > %.3f), pinning min = max",
				c->tonemapMinExposure, c->tonemapMaxExposure);
		c->tonemapMinExposure = c->tonemapMaxExposure;
	}
	// User pivot curve (TonemapPass c8): identity defaults 0/0/0.5
	if(c->tonemapCurveLows < -1.0f || c->tonemapCurveLows > 1.0f){
		if(dbglog_throttle("tclow_clamp"))
			dbglog("tonemapCurveLows=%.4f outside [-1..1], clamping", c->tonemapCurveLows);
		c->tonemapCurveLows = (c->tonemapCurveLows < -1.0f) ? -1.0f : 1.0f;
	}
	if(c->tonemapCurveHighs < -1.0f || c->tonemapCurveHighs > 1.0f){
		if(dbglog_throttle("tchig_clamp"))
			dbglog("tonemapCurveHighs=%.4f outside [-1..1], clamping", c->tonemapCurveHighs);
		c->tonemapCurveHighs = (c->tonemapCurveHighs < -1.0f) ? -1.0f : 1.0f;
	}
	if(c->tonemapCurveMid < 0.0f || c->tonemapCurveMid > 1.0f){
		if(dbglog_throttle("tcmid_clamp"))
			dbglog("tonemapCurveMid=%.4f outside [0..1], clamping", c->tonemapCurveMid);
		c->tonemapCurveMid = (c->tonemapCurveMid < 0.0f) ? 0.0f : 1.0f;
	}
	// HIGH-3: adaptSpeed/keyStrength rode the same function's call sites
	// (load :~1891 / refreshIni / saveConfigTo) but were never clamped —
	// ranges match the ImGui sliders + legacy DebugMenu entries
	// (debugmenu_ui :225-226, :605-606).
	if(c->tonemapAdaptSpeed < 0.01f || c->tonemapAdaptSpeed > 1.0f){
		if(dbglog_throttle("tspd_clamp"))
			dbglog("tonemapAdaptSpeed=%.4f outside [0.01..1.0], clamping", c->tonemapAdaptSpeed);
		c->tonemapAdaptSpeed = (c->tonemapAdaptSpeed < 0.01f) ? 0.01f : 1.0f;
	}
	if(c->tonemapKeyStrength < 0.0f || c->tonemapKeyStrength > 1.0f){
		if(dbglog_throttle("tkey_clamp"))
			dbglog("tonemapKeyStrength=%.4f outside [0..1], clamping", c->tonemapKeyStrength);
		c->tonemapKeyStrength = (c->tonemapKeyStrength < 0.0f) ? 0.0f : 1.0f;
	}
}

// ssaoPower is a gamma on occlusion (SSAO.hlsl pow(occlusion, power); both
// draw paths read it as `>0 ? v : 2.0` fallback). User's 9.237 amplified
// mild occlusion into hard object-shaped darkening (screenshot report) —
// clamp to [0.5..4.0] at INI load, refreshIni and saveConfigTo, same
// pattern as clampEnvSliderRanges / clampTonemapExposureRanges.
static void
clampSsaoPower(Config *c)
{
	if(!c)
		return;
	if(c->ssaoPower < 0.5f || c->ssaoPower > 4.0f){
		if(dbglog_throttle("ssaop_clamp"))
			dbglog("ssaoPower=%.4f outside [0.5..4.0], clamping", c->ssaoPower);
		c->ssaoPower = (c->ssaoPower < 0.5f) ? 0.5f : 4.0f;
	}
}

// Chrome breakdown (Part B/C) — same clamp-wave pattern as clampSsaoPower:
// six new fields, sane ranges asserted at load/reload/save/menu so no writer
// (legacy DebugMenu, hand-edited INI) can leave the sliders displaying (or the
// shader consuming) garbage. vehAutoChrome is a bool (0/1 pin).
static void
clampChromeParams(Config *c)
{
	if(!c)
		return;
	struct { float *v; float lo; float hi; const char *tag; } ranges[] = {
		{ &c->chromeF0,        0.0f, 1.0f, "chromeF0" },
		{ &c->chromeGloss,     0.0f, 1.0f, "chromeGloss" },
		{ &c->chromeMetallic,  0.0f, 1.0f, "chromeMetallic" },
		{ &c->chromeEnvBoost,  0.0f, 4.0f, "chromeEnvBoost" },
		{ &c->chromeClearcoat, 0.0f, 1.0f, "chromeClearcoat" },
	};
	for(auto &r : ranges){
		if(*r.v < r.lo || *r.v > r.hi){
			if(dbglog_throttle("chrome_clamp"))
				dbglog("%s=%.4f outside [%.1f..%.1f], clamping", r.tag, *r.v, r.lo, r.hi);
			*r.v = (*r.v < r.lo) ? r.lo : r.hi;
		}
	}
	if(c->vehAutoChrome != 0 && c->vehAutoChrome != 1){
		if(dbglog_throttle("chrome_clamp"))
			dbglog("vehAutoChrome=%d, pinning to 1", c->vehAutoChrome);
		c->vehAutoChrome = 1;
	}
	if(c->vehChromeEnvThreshold < 0.0f){
		if(dbglog_throttle("chrome_clamp"))
			dbglog("vehChromeEnvThreshold=%.4f < 0, clamping", c->vehChromeEnvThreshold);
		c->vehChromeEnvThreshold = 0.0f;
	}
}

// ============================================================
// colorFilterEnable user-intent latch (silent-write self-heal)
// ============================================================
// Bug (2026-10-01 boot log): skygfx.ini colorFilterEnable=1 yet at world
// entry (~frame 1329) the flag flipped to 0 with NO reload/preset/save
// activity in the log — a silent in-memory write. Exhaustive audit found
// only ONE named writer (readIniFile below) plus the debug-menu row; the
// remaining candidates are memory-level (pre-fix-6 duplicate
// `##ini_colorFilterEnable` ImGui rows, external debugmenu.asi, foreign
// stomp). Fix requirement: the user's INI value must persist across world
// entry and no path may override it (pipeline=Mobile must NOT zero it).
//
// Pattern is the established env-clamp convention (assert at load /
// per-frame / before save): legitimate writers record USER INTENT, and
// derivePipelineFromPipes (already every-frame from ColourFilter_switch)
// repairs any divergent value and logs the event — which also pins the
// true writer's timing on the next repro.
static RwBool s_cfEnableUser[10];	// user intent per configs[] slot
static bool  s_cfEnableSeen[10];	// slot has a recorded intent yet

// Legitimate writer recorder: readIniFile (INI load/reload) and the
// debug-menu row's onEdit. Writes through to the live value as well.
void
cfEnableSetUserIntent(Config *c, RwBool v)
{
	if(!c)
		return;
	int slot = (int)(c - configs);
	if(slot >= 0 && slot < 10){
		s_cfEnableUser[slot] = v;
		s_cfEnableSeen[slot] = true;
	}
	c->colorFilterEnable = v;
}

// Repair a silently-diverged flag back to user intent (no-op when they
// agree or the slot has no recorded intent). Called every frame from
// derivePipelineFromPipes and before every saveConfigTo write-back.
void
cfEnableHeal(Config *c)
{
	if(!c)
		return;
	int slot = (int)(c - configs);
	if(slot < 0 || slot >= 10 || !s_cfEnableSeen[slot])
		return;
	if(c->colorFilterEnable != s_cfEnableUser[slot]){
		if(dbglog_throttle("cfenable_guard"))
			dbglog("[Config] colorFilterEnable=%d diverged from user intent %d — silent write repaired (slot %d)",
				(int)c->colorFilterEnable, (int)s_cfEnableUser[slot], slot);
		c->colorFilterEnable = s_cfEnableUser[slot];
	}
}

// derivePipelineFromPipes — pipe-conjunction fix #4.
//
// config->pipeline is locked from the INI `pipeline` key ONCE at load
// (readIniFile :1457-1500) but buildingPipe/vehiclePipe can change
// independently afterwards: INI buildingPipe/vehiclePipe overrides, quality
// preset cases, ImGui combos, legacy DebugMenu vars, menu_inject cycles,
// ApplyPreset (Style combo), IVMode_ApplyDefaults. Pipeline-keyed gates then
// run against a STALE pipeline: postfx ColourFilter PBR force (:2768), the
// timecycle gate (:2673), DrawIVGrade (via ivMode, :2437), the load-time PBR
// colour-filter force (main :1785), and the PIPELINE_MOBILE light mult
// (main :596). Re-derive pipeline from ground truth (the actual pipes).
//
// Combo -> pipeline mapping (ordered, first match wins):
//   1. building=GTAIV (any vehicle)                     -> PIPELINE_GTAIV
//   2. vehicle=GTAIV AND building in {PS2, Xbox}        -> PIPELINE_GTAIV
//      (either pipe GTAIV with a PS2-ish partner; rule 1 covers both-GTAIV
//       and IV building + exotic vehicle such as Modern/Mobile)
//   3. building=PBR (incl. both-PBR with vehicle=Modern)-> PIPELINE_PBR
//      (PBR building dominates a mixed combo; a lone GTAIV vehicle under a
//       PBR building does NOT override the PBR gates — PBR isn't "PS2-ish")
//   4. building=Xbox AND vehicle=Mobile (mobile lock)   -> PIPELINE_MOBILE
//   5. building=Xbox (any other vehicle)                -> PIPELINE_XBOX
//   6. building=PS2 (any non-GTAIV vehicle)             -> PIPELINE_PS2
//   7. otherwise (unknown/out-of-range)                 -> PIPELINE_PBR
//      (matches the pipelineMap "" default at :1464; derives the pipeline
//       value ONLY — the PBR pipe/filter lock below never fires on this
//       fallback, it is reserved for the explicit rule 3)
// All five INI `pipeline=` locked presets map to THEMSELVES — derive is an
// identity for coherent configs; only divergent pipe states change.
//
// colorFilter: only the PBR->MODERN force is re-derived (mirrors load :1478/
// :1785 and the postfx :2768 safety net, which may remain as-is now that the
// key is correct). Non-PBR families NEVER touch colorFilter — it is an
// independent user/preset/INI choice (presets set it alongside their pipes).
//
// ivMode: re-derived ONLY when the pipes themselves changed since the last
// call (snapshot below). Pipes moved TO the GTAIV family -> ivMode=1
// (DrawIVGrade + the PS2-callback IV shader swap engage); pipes moved AWAY
// -> ivMode=0 (stops :2437 keying on ivMode alone over non-IV pipes). A
// direct ivMode write with pipes unchanged (ImGui checkbox, legacy var,
// menu_inject toggle, explicit INI read) is DELIBERATE and always honored —
// never stomped. First sight of a Config slot: snapshot only, no ivMode
// write (the family-aware read default at :1896 covers cold loads).
void
derivePipelineFromPipes(Config *c)
{
	if(!c)
		return;

	// colorFilterEnable self-heal first (see latch block above): repair any
	// silent write BEFORE the per-frame gates below consume the flag. This
	// is the world-entry coverage — derive runs every frame from
	// ColourFilter_switch, so a world-entry stomp cannot survive a frame.
	cfEnableHeal(c);

	// Snapshot of the last-seen Config slot: distinguishes "the PIPES
	// changed" (re-derive ivMode) from "someone wrote ivMode" (honor it).
	static Config *s_prevCfg = NULL;
	static int s_prevBp, s_prevVp, s_prevIv;

	int bp = c->buildingPipe;
	int vp = c->vehiclePipe;

	// --- mapping table (ordered, first match wins — see header) ---
	int derived;
	int rule = 0;	// which rule matched (1..7): the PBR lock below fires ONLY
					// on the explicit PBR rule (3), never on the rule-7
					// fallback — a fallback PBR must not stomp user pipes.
	if(bp == BUILDING_GTAIV){
		derived = PIPELINE_GTAIV; rule = 1;	// rule 1
	}else if(vp == CAR_GTAIV && (bp == BUILDING_PS2 || bp == BUILDING_XBOX)){
		derived = PIPELINE_GTAIV; rule = 2;	// rule 2: either GTAIV + PS2-ish partner
	}else if(bp == BUILDING_PBR){
		derived = PIPELINE_PBR; rule = 3;	// rule 3: both-PBR + PBR-building mixed
	}else if(bp == BUILDING_XBOX && vp == CAR_MOBILE){
		derived = PIPELINE_MOBILE; rule = 4;	// rule 4: canonical mobile lock combo
	}else if(bp == BUILDING_XBOX){
		derived = PIPELINE_XBOX; rule = 5;	// rule 5: building family
	}else if(bp == BUILDING_PS2){
		derived = PIPELINE_PS2; rule = 6;	// rule 6: building family
	}else{
		derived = PIPELINE_PBR; rule = 7;	// rule 7: unknown/out-of-range -> PBR default
	}

	if(c->pipeline != derived){
		if(dbglog_throttle("pipe_derive"))
			dbglog("[Config] pipeline re-derived %d -> %d (buildingPipe=%d vehiclePipe=%d)",
				c->pipeline, derived, bp, vp);
		c->pipeline = derived;
	}

	// PBR family is a LOCKED combo: the pipeline map (readIniFile :2170-2173)
	// pairs BUILDING_PBR with CAR_MODERN and COLORFILTER_MODERN. A divergent
	// pipe state used to leave PBR selected while the vehicle pipe stayed
	// Mobile(4) (log skygfx_dbg.log:5059: `pipeline re-derived 4 -> 0
	// (buildingPipe=3 vehiclePipe=4)`), so the PBR vehicle callbacks never
	// ran. Re-select the mapped pair whenever PBR wins the derivation.
	//
	// GATED on the matched rule: only the EXPLICIT PBR rule (3, building
	// pipe is BUILDING_PBR) may fire the lock. The rule-7 fallback (unknown/
	// out-of-range buildingPipe) derives PBR as the pipeline default but must
	// NEVER force-rewrite the pipes/filter — stomping a coherent user config
	// (e.g. INI vehiclePipe=Mobile) on a garbage building value parked the
	// rewrite permanently: after the user cycled buildingPipe away, the
	// forced CAR_MODERN/colorFilter never reverted (skygfx_dbg.log:19228/
	// :20250/:26466 episode). Genuine rule-3 PBR derivation keeps the force
	// (never-revert).
	if(derived == PIPELINE_PBR && rule == 3){
		if(c->buildingPipe != BUILDING_PBR){
			if(dbglog_throttle("pipe_derive"))
				dbglog("[Config] PBR selected -> buildingPipe %d -> %d", c->buildingPipe, BUILDING_PBR);
			c->buildingPipe = BUILDING_PBR;
		}
		if(c->vehiclePipe != CAR_MODERN){
			if(dbglog_throttle("pipe_derive"))
				dbglog("[Config] PBR selected -> vehiclePipe %d -> %d", c->vehiclePipe, CAR_MODERN);
			c->vehiclePipe = CAR_MODERN;
		}
		c->colorFilter = COLORFILTER_MODERN;
		// Keep the local snapshot in sync with the repaired pipes so the
		// ivMode pipe-change detector below does not fire on our own fix.
		bp = c->buildingPipe;
		vp = c->vehiclePipe;
	}

	// --- ivMode: pipe-change-driven only (never stomps deliberate writes) ---
	bool firstSight = (c != s_prevCfg);
	bool pipesChanged = !firstSight && (bp != s_prevBp || vp != s_prevVp);
	bool ivChanged = !firstSight && (c->ivMode != s_prevIv);

	if(pipesChanged && !ivChanged){
		// The pipes moved and nobody wrote ivMode alongside them: sync the
		// grade/IV-swap flag to the new family.
		int want = (derived == PIPELINE_GTAIV) ? 1 : 0;
		if(c->ivMode != want){
			if(dbglog_throttle("pipe_derive"))
				dbglog("[Config] pipes changed -> ivMode %d -> %d (pipeline family)",
					c->ivMode, want);
			c->ivMode = want;
		}
	}
	// else: pipes unchanged -> any ivMode difference is a deliberate write
	// (checkbox / legacy var / menu_inject / explicit INI): honor it.
	// pipesChanged && ivChanged -> preset/ApplyDefaults wrote both: honor
	// their explicit ivMode.

	s_prevCfg = c;
	s_prevBp = bp;
	s_prevVp = vp;
	s_prevIv = c->ivMode;
}

// ===== Utility Noise Texture (global, not per-config) =====
// [General] NoiseQuality -> noise tile resolution: 0->32, 1->128, 2->512,
// >=3->1024, default 0. Parsed in readIniFile below, APPLIED in refreshIni()
// (the shared readIni/readInis/F11-reload/loadConfigFile path) so raster
// creation never runs from the DllMain-time readIni — RW must be up.
// refreshIni regenerates ONLY when the resolution actually changed or
// g_pUtilityNoise == nullptr (effects-menu-overhaul.md §2).
static int g_noiseQuality = 0;

static int
utilityNoiseResForQuality(int quality)
{
	if(quality <= 0) return 32;
	if(quality == 1) return 128;
	if(quality == 2) return 512;
	return 1024;
}

void
refreshIni(void)
{
	clampEnvSliderRanges(config);		// re-assert after every reload/setConfig
	clampTonemapExposureRanges(config);
	clampSsaoPower(config);
	clampChromeParams(config);

	// Utility Noise Texture (§2): regenerate ONLY when the resolution
	// actually changed or the texture is missing. Covers startup
	// (afterStreamIni -> readInis -> here, even with no INI key = default 0
	// -> 32x32) and F11 reload alike. RefreshIni only ever runs with RW up,
	// and the call itself re-guards on d3d9device.
	{
		int noiseRes = utilityNoiseResForQuality(g_noiseQuality);
		if(g_pUtilityNoise == nullptr || g_CurrentNoiseSize != noiseRes)
			GenerateUtilityTexture(noiseRes);
	}

	// Pipe-conjunction #4 piggyback: every reload/setConfig/named-config
	// load re-derives pipeline/colorFilter/ivMode from the actual pipes —
	// this is also what covers ApplyPreset/IVMode/menu_inject writers without
	// editing their (out-of-scope) files.
	derivePipelineFromPipes(config);
	resetValues();
	refreshMenu();
}


RpAtomic *(*plantTab0)[4] = (RpAtomic *(*)[4])0xC039F0;
RpAtomic *(*plantTab1)[4] = (RpAtomic *(*)[4])0xC03A00;

RpAtomic*
grassRenderCallback(RpAtomic *atomic)
{
	RpAtomic *ret;
	int cullPatched = 0;
	RwRGBAReal color = { 0.0f, 0.0, 1.0f, 1.0f };

	if(config->ps2ModulateGrass){
		gpCurrentPixelShaderForDefaultCallbacks = grassPixelShader;
		RwRGBARealFromRwRGBA(&color, &atomic->geometry->matList.materials[0]->color);
		RwD3D9SetPixelShaderConstant(0, &color, 1);
	}

	// No RwRenderStateGet round-trip here: a failed Get leaves the
	// out-param untouched (uninitialized) and a device-domain value
	// (e.g. D3DCULL_CCW=3) re-entered into RwRenderStateSet is read
	// as rwCULLMODECULLFRONT — hiding one-sided geometry downstream.
	// Patch only when asked; restore to the known-good SA default.
	if(!config->backfaceCull){
		RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
		cullPatched = 1;
	}

	RxPipeline *pipe;
	// Pre-seeded (never bare): a failed RwRenderStateGet leaves the out-param
	// untouched, and the restore below pushes these straight back — an
	// uninitialized func/ref must never reach the device. Same convention as
	// D3D9RenderDual above and CSkidmarks__Render below.
	int alphatest = rwALPHATESTFUNCTIONGREATEREQUAL;
	int alpharef = 1;
	int dodual = 0;
	int detach = 0;
	pipe = atomic->pipeline;
	if(pipe == NULL)
		pipe = *(RxPipeline**)(*(DWORD*)0xC97B24+0x3C+dword_C9BC60);
	if(config->dualPassGrass && config->zwriteThresholdGrass > 0){
		// Two-layer leg states (see setAlphaTestTwoLayer): the old rw-only
		// writes let a drifted device run the same test for both legs.
		setZWriteTwoLayer(TRUE);
		RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, (void*)&alphatest);
		RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)&alpharef);
		// Sanitize the saved func/ref before the restore write (same rule as
		// D3D9RenderDual / CSkidmarks__Render): rw NA(0)/garbage must never
		// reach the device. Clamp the threshold to 254: at 255 leg 1
		// (GREATEREQUAL@255) rejects nothing but leg 2 (LESS@255) also
		// rejects fully-opaque texels — latent blank-everything mode.
		if(alphatest < rwALPHATESTFUNCTIONNEVER || alphatest > rwALPHATESTFUNCTIONALWAYS)
			alphatest = rwALPHATESTFUNCTIONGREATEREQUAL;
		if(alpharef < 0) alpharef = 0;
		if(alpharef > 255) alpharef = 255;
		int zt = config->zwriteThresholdGrass;
		if(zt > 254) zt = 254;
		setAlphaTestTwoLayer(rwALPHATESTFUNCTIONGREATEREQUAL, zt);
		RxPipelineExecute(pipe, atomic, 1);
		setZWriteTwoLayer(FALSE);
		setAlphaTestTwoLayer(rwALPHATESTFUNCTIONLESS, zt);
		pipe = RxPipelineExecute(pipe, atomic, 1);
		setZWriteTwoLayer(TRUE);
		setAlphaTestTwoLayer((RwUInt32)alphatest, alpharef);
	}else
		pipe = RxPipelineExecute(pipe, atomic, 1);
	ret = pipe ? atomic : NULL;


	if(cullPatched)
		RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
	gpCurrentPixelShaderForDefaultCallbacks = NULL;

	return ret;
}

// buildingPipe.cpp — three-layer alpha/blend repair (block comment there).
// Forward-declared for the PED pipe-group entry contract below: the ped
// half needs the SAME layer-3 repair the building Switch gets, and the
// declaration near the skidmark draw (line ~1240) sits after this use.
extern void pipeForceAlphaBlock(void);

// Atomic-level analogue of D3D9RenderDual's per-mesh gate (see the
// `dual && hasAlpha && zwrite` test at main.cpp:171). That helper reads
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
static bool
atomicHasAlpha(RpAtomic *atomic)
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
	}
	if(geom->preLitLum){
		const RwRGBA *c = geom->preLitLum;
		for(RwInt32 i = 0; i < geom->numVertices; i++)
			if(c[i].alpha != 255)
				return true;
	}
	return false;
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

	pipe = atomic->pipeline;
	// Ped pipe group flag, captured BEFORE the NULL-pipe branch overwrites
	// `pipe` with the default pipeline. Scoped to peds only: this callback
	// also runs every other default-pipeline atomic (props, pickups, marker
	// objects), which must keep their current (unguarded) behaviour.
	bool isPedPipe = (pipe == skinPipe);
	if(pipe == NULL){
		pipe = *(RxPipeline**)(*(DWORD*)0xC97B24+0x3C+dword_C9BC60);
		RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, (void*)&zwrite);
		if(zwrite && config->dualPassDefault)
			dodual = 1;
	}else if(isPedPipe && config->dualPassPed)
		dodual = 1;

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
	if(isPedPipe){
		savedCull = pipeEnterCullMode();
		savedAlpha = pipeEnterAlphaMode();

		// ZWRITE three-layer guard — mirror of buildingPipe.cpp:1185-1195.
		// Pre-seeded TRUE: a failed Get leaves the out-param untouched, and
		// TRUE is the canonical scene value (same pre-seed as the others).
		RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, (void*)&savedZWriteRw);
		RwD3D9GetRenderState(D3DRS_ZWRITEENABLE, &savedZWriteDrv);
		if(d3d9device)
			d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &savedZWriteDev);
		if(!savedZWriteRw || !savedZWriteDev){
			if(dbglog_throttle("PedZWrite"))
				dbglog("[PED] ZWRITE off/desynced on entry (rw=%d dev=%d) — forcing TRUE for cb",
					(int)savedZWriteRw, (int)savedZWriteDev);
		}
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		if(d3d9device)
			d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);

		// Layer-3 alpha/blend repair: pipeEnterAlphaMode's RwD3D9 writes are
		// applied[]-gated and cannot reach a device that drifted behind an
		// unchanged cache (same rationale as buildingPipe.cpp:1197-1204).
		pipeForceAlphaBlock();
	}

	// Override pAmbient with shared timecycle ambient for ped/skin rendering.
	// All pipelines (buildings, vehicles, peds) now use the same ambient source.
	// PBR floor applied so peds never drop to a black silhouette at night.
	RwRGBAReal savedAmb = {};
	bool ambOverridden = false;
	if(pipe == skinPipe && pAmbient){
		savedAmb = pAmbient->color;
		RwRGBAReal tcAmbient = GetTimecycleAmbientPBR();
		pAmbient->color = tcAmbient;
		ambOverridden = true;
		if(dbglog_throttle("ped_ambient"))
			dbglog("[Ped] pAmbient override: (%.3f,%.3f,%.3f)",
				tcAmbient.red, tcAmbient.green, tcAmbient.blue);
	}

	// D3D9RenderDual parity (main.cpp:171: `dual && hasAlpha && zwrite`) —
	// gate the PAIR the same way instead of always paying for leg 2.
	// `zwrite` is the same rw-cache read that branch makes (the ped entry
	// contract forced it TRUE above, so for isPedPipe this mainly mirrors
	// the default-pipe branch's own `zwrite && dualPassDefault` test at
	// :706, which ran BEFORE that force); `hasAlpha` comes from the atomic
	// probe above. When either fails we fall through to the single execute
	// exactly as if dualPassPed were off for this atomic.
	if(dodual){
		int zwriteNow = TRUE;
		bool hasAlpha = atomicHasAlpha(atomic);
		RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, (void*)&zwriteNow);
		if(!zwriteNow || !hasAlpha){
			if(dbglog_throttle("ped_dual_skip"))
				dbglog("[PED] dual pass skipped: zwrite=%d hasAlpha=%d pipe=%p",
				       zwriteNow, (int)hasAlpha, (void*)pipe);
			dodual = 0;
		}
	}

	if(dodual){
		RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, (void*)&alphatest);
		RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)&alpharef);
		// Sanitize the saved func/ref before the restore write (same rule as
		// D3D9RenderDual / CSkidmarks__Render): rw NA(0)/garbage must never
		// reach the device. Clamp the threshold to 254: at 255 leg 1
		// (GREATEREQUAL@255) rejects nothing but leg 2 (LESS@255) also
		// rejects fully-opaque texels — latent blank-everything mode.
		if(alphatest < rwALPHATESTFUNCTIONNEVER || alphatest > rwALPHATESTFUNCTIONALWAYS)
			alphatest = rwALPHATESTFUNCTIONGREATEREQUAL;
		if(alpharef < 0) alpharef = 0;
		if(alpharef > 255) alpharef = 255;
		int zt = config->zwriteThresholdPed;
		if(zt > 254) zt = 254;
		// zwriteThresholdPed, not zwriteThreshold: the ped key is parsed
		// (readIni :2266) and menu'd (debugmenu_ui :720/:1712) but was never
		// consumed — peds silently shared the building/vehicle cut. Same
		// 0..255 clamp as its siblings, so the two are interchangeable
		// until the user actually moves the ped slider.
		// Two-layer leg states (see setAlphaTestTwoLayer): the old rw-only
		// writes let a drifted device run the same test for both legs.
		setAlphaTestTwoLayer(rwALPHATESTFUNCTIONGREATEREQUAL, zt);
		RxPipelineExecute(pipe, atomic, 1);
		setZWriteTwoLayer(FALSE);
		setAlphaTestTwoLayer(rwALPHATESTFUNCTIONLESS, zt);
		pipe = RxPipelineExecute(pipe, atomic, 1);
		setZWriteTwoLayer(TRUE);
		setAlphaTestTwoLayer((RwUInt32)alphatest, alpharef);
	}else
		pipe = RxPipelineExecute(pipe, atomic, 1);

	// Restore original ambient after ped rendering
	if(ambOverridden && pAmbient){
		pAmbient->color = savedAmb;
	}

	// ===== PED pipe group exit contract — mirror of buildingPipe.cpp:1223-1227
	// and vehiclePipe.cpp:2480-2484. Also closes the dual pass above, which
	// leaves rw ZWRITE forced TRUE (and alpha func/ref at whatever the pass
	// last wrote) — each layer goes back to its own pre-entry value here, so
	// the ped pipe can no longer hand a forced/derived state to the next
	// consumer (Im2D marker passes, effects, the next pipe group).
	if(isPedPipe){
		pipeExitAlphaMode(savedAlpha);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)savedZWriteRw);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, savedZWriteDrv ? TRUE : FALSE);
		if(d3d9device)
			d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, savedZWriteDev);
		pipeExitCullMode(savedCull);
	}

	return pipe ? atomic : NULL;
}


void (*CTagManager__RenderTagForPC)(RpAtomic *atomic);
void (*CTagManager__SetupAtomic_orig)(RpAtomic *atomic);
void
CTagManager__SetupAtomic(RpAtomic *atomic)
{
	CTagManager__SetupAtomic_orig(atomic);
	/* Set the building pipeline so we have control over drawing.
	 * Note that we need the non-DN version. This works because this function
	 * is called after the building pipeline has already been set up. */
	SetPipelineID(atomic, RSPIPE_PC_CustomBuilding_PipeID);
	RpAtomicSetPipeline(atomic, CCustomBuildingPipeline__ObjPipeline);
	atomic->pipeline = CCustomBuildingPipeline__ObjPipeline;
}
void
CTagManager__RenderTag(RpAtomic *atomic)
{
	if(iCanHasbuildingPipe){
		/* building pipe can handle accurate PS2 behaviour */
		assert(atomic->pipeline == CCustomBuildingPipeline__ObjPipeline);
		atomic->renderCallBack(atomic);
	}else{
		/* Otherwise fall back */
		int alpharef;
		RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)&alpharef);
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)64);
		CTagManager__RenderTagForPC(atomic);
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)alpharef);
	}
}

float
clamp(float f, float max)
{
	return f > max ? max : f;
}

// For Mobile-style lights
//static RwRGBAReal savedAmb;
//static RwRGBAReal savedDir;
//void
//BrightenLights(void)
//{
//	savedAmb = pAmbient->color;
//	pAmbient->color.red = clamp(pAmbient->color.red*1.5f, 1.0f);
//	pAmbient->color.green = clamp(pAmbient->color.green*1.5f, 1.0f);
//	pAmbient->color.blue = clamp(pAmbient->color.blue*1.5f, 1.0f);
//	savedDir = pDirect->color;
//	pDirect->color.red = clamp(pDirect->color.red*1.5f, 1.0f);
//	pDirect->color.green = clamp(pDirect->color.green*1.5f, 1.0f);
//	pDirect->color.blue = clamp(pDirect->color.blue*1.5f, 1.0f);
//}
//
//void
//RestoreLights(void)
//{
//	pAmbient->color = savedAmb;
//	pDirect->color = savedDir;
//}

// no longer needed as we're using our own render functions now
//RxNodeDefinition *nodeD3D9SkinAtomicAllInOneCSL = (RxNodeDefinition*)0x8DED08;
//RxNodeBodyFn __rwSkinD3D9AtomicAllInOneNode_orig;
//RwBool __rwSkinD3D9AtomicAllInOneNode_hook(RxPipelineNode *self, const RxPipelineNodeParam *params)
//{
//	// Don't render peds when we're rendering reflections
//	if(gRenderingSpheremap)
//		return 1;
////	BrightenLights();
//	RwBool ret = __rwSkinD3D9AtomicAllInOneNode_orig(self, params);
////	RestoreLights();
//	return ret;
//}

RwRGBAReal &AmbientLightColourForFrame_PedsCarsAndObjects = *(RwRGBAReal*)0xC886C4;
RwRGBAReal &DirectionalLightColourForFrame = *(RwRGBAReal*)0xC886B4;

uint8 *ambRed = (uint8*)0xB7C3C8;
uint8 *ambGreen = (uint8*)0xB7C310;
uint8 *ambBlue = (uint8*)0xB7C258;

void (*SetLightsWithTimeOfDayColour_orig)(RpWorld*);
void
SetLightsWithTimeOfDayColour(RpWorld *world)
{
	if(GetAsyncKeyState(VK_F5) & 0x8000){
		memset(ambRed, 0xFF, 184);
		memset(ambGreen, 0xFF, 184);
		memset(ambBlue, 0xFF, 184);
	}else{
		memset(ambRed, 0, 184);
		memset(ambGreen, 0, 184);
		memset(ambBlue, 0, 184);
	}
	SetLightsWithTimeOfDayColour_orig(world);

	// Multiplied by 1.5 on mobile
	if(config->pipeline == PIPELINE_MOBILE){
		float mult = 1.5f;

		AmbientLightColourForFrame_PedsCarsAndObjects.red = clamp(AmbientLightColourForFrame_PedsCarsAndObjects.red*mult, 1.0f);
		AmbientLightColourForFrame_PedsCarsAndObjects.green = clamp(AmbientLightColourForFrame_PedsCarsAndObjects.green*mult, 1.0f);
		AmbientLightColourForFrame_PedsCarsAndObjects.blue = clamp(AmbientLightColourForFrame_PedsCarsAndObjects.blue*mult, 1.0f);

		DirectionalLightColourForFrame.red = clamp(DirectionalLightColourForFrame.red*mult, 1.0f);
		DirectionalLightColourForFrame.green = clamp(DirectionalLightColourForFrame.green*mult, 1.0f);
		DirectionalLightColourForFrame.blue = clamp(DirectionalLightColourForFrame.blue*mult, 1.0f);
		RpLightSetColor(pDirect, &DirectionalLightColourForFrame);
	}
}


int tmpintensity;
RwTexture **tmptexture = (RwTexture**)0xc02dc0;
RwRGBA *CPlantMgr_AmbientColor = (RwRGBA*)0xC03A44;

RpMaterial*
setTextureAndColor(RpMaterial *material, RwRGBA *color)
{
	RwTexture *texture;
	RwRGBA newcolor;
	uint col[3];

	texture = *tmptexture;
	col[0] = color->red;
	col[1] = color->green;
	col[2] = color->blue;
	if(config->grassAddAmbient){
		col[0] += CTimeCycle_GetAmbientRed()*255;
		col[1] += CTimeCycle_GetAmbientGreen()*255;
		col[2] += CTimeCycle_GetAmbientBlue()*255;
		if(col[0] > 255) col[0] = 255;
		if(col[1] > 255) col[1] = 255;
		if(col[2] > 255) col[2] = 255;
	}
	newcolor.red = (tmpintensity * col[0]) >> 8;
	newcolor.green = (tmpintensity * col[1]) >> 8;
	newcolor.blue = (tmpintensity * col[2]) >> 8;
	newcolor.alpha = color->alpha;
	material->color = newcolor;
	if(material->texture != texture)
		RpMaterialSetTexture(material, texture);
	return material;
}

void CMessages__AddMessageJumpQWithNumber(char* text, unsigned int time, unsigned short flag, int n1, int n2, int n3, int n4, int n5, int n6, bool bPreviousBrief)
{
	((void(__cdecl*)(char*, unsigned int, unsigned short, int, int, int, int, int, int, bool))0x69E4E0)(text, time, flag, n1, n2, n3, n4, n5, n6, bPreviousBrief);
}

void __declspec(naked)
fixSeed(void)
{
	_asm{
	// 0x5DADB7
		mov	ecx, [config]
		cmp	[ecx+8], 0	// fixGrassPlacement
		jle	dontfix
		mov	ecx, [esp+54h]
		mov	ebx, [ecx+eax*4]
		mov	ebp, [ebx+4]
		lea	edi, [ebp+10h]
		mov	eax, [esi+48h]

		push	5DADCCh
		retn

	dontfix:
		fld	dword ptr [esi+48h]
		mov	ecx, [esp+54h]
		push	5DADBEh
		retn

	}
}

// copy color as is and save intensity (eax) for use in setTextureAndColor() later
void __declspec(naked)
saveIntensity(void)
{
	_asm{
	// 0x5DAE61
		movzx	eax, byte ptr [esi+44h]
		mov	[tmpintensity], eax
		mov	al, byte ptr [esi+40h]	// color
		mov	cl, byte ptr [esi+41h]
		mov	dl, byte ptr [esi+42h]
		mov     byte ptr [esp+10h], al	// local variable color
		mov     byte ptr [esp+11h], cl
		mov     byte ptr [esp+12h], dl
		mov     eax, [esi+3Ch]	// code expects texture in eax
		push	5DAEB7h
		retn
	}
}

// from Silent
void __declspec(naked)
rxD3D9DefaultRenderCallback_Hook(void)
{
	_asm
	{
		mov	ecx, [gpCurrentPixelShaderForDefaultCallbacks]
		cmp	eax, ecx	// _rwD3D9LastPixelShaderUsed
		je	rxD3D9DefaultRenderCallback_Hook_Return
		mov	dword ptr ds:[8E244Ch], ecx
		push	ecx
		mov	eax, dword ptr ds:[0C97C28h]	// RwD3D9Device
		push	eax
		mov	ecx, [eax]
		call	dword ptr [ecx+1ACh]

	rxD3D9DefaultRenderCallback_Hook_Return:
		push	756E17h
		retn
	}
}

// Crash 0x7FAD4D: game default render callback clips with a NULL curCamera on frame 1.
// Both frustum tests are clip-only; return "inside" (eax=1) when camera is NULL.
void __declspec(naked)
FrustumTestSphere_Guard(void)
{
	_asm
	{
		cmp	dword ptr [esp+4], 0	// arg1 = camera
		je	inside
		mov	edx, [esp+8]		// replay overwritten prologue
		mov	eax, [esp+4]
		push	7FAD38h			// resume at original `push esi`
		retn
	inside:
		mov	eax, 1
		retn
	}
}

void __declspec(naked)
FrustumTestBox_Guard(void)
{
	_asm
	{
		cmp	dword ptr [esp+4], 0
		je	inside
		mov	eax, [esp+4]
		mov	edx, [esp+8]
		push	7FAD98h
		retn
	inside:
		mov	eax, 1
		retn
	}
}

// Crash 0x7618F5: game PS-constant uploader 0x7618B0 reads curCamera near/far
// ([curCamera+0x84]/[+0x88]) in its (arg1[3]&3)==1 path. curCamera is NULL before
// the first RwCameraBeginUpdate -> NULL deref. Replay the prologue and, when the
// camera is NULL, zero the two camera-derived slots and continue at the common
// upload tail (same semantics as the game's own al==0 path).
void __declspec(naked)
PSSetCameraConstantD_Guard(void)
{
	_asm
	{
		mov		eax, [esp+4]
		sub		esp, 10h
		mov		al, [eax+3]
		and		al, 3
		cmp		al, 1
		jne		PSCCD_notcam
		mov		edx, dword ptr ds:[0C97B24h]
		mov		eax, [edx]
		test	eax, eax
		jne		PSCCD_hascam
		mov		dword ptr [esp], 0
		mov		dword ptr [esp+4], 0
		push	761917h
		retn
	PSCCD_hascam:
		push	7618EDh
		retn
	PSCCD_notcam:
		push	7618C0h
		retn
	}
}

void __declspec(naked)
rxD3D9DefaultRenderCallback_VertexShaderHook(void)
{
	_asm
	{
		mov	eax, ecx
		mov	ecx, [gpCurrentVertexShaderForDefaultCallbacks]
		cmp	eax, ecx	// _rwD3D9LastVertexShaderUsed
		je	rxD3D9DefaultRenderCallback_VertexShaderHook_Return
		mov	dword ptr ds : [8E2448h] , ecx
		push	ecx
		mov	eax, dword ptr ds : [0C97C28h]	// RwD3D9Device
		push	eax
		mov	ecx, [eax]
		call	dword ptr[ecx + 170h]

		rxD3D9DefaultRenderCallback_VertexShaderHook_Return :
		push	7572E7h
		retn
	}
}

char
CPlantMgr_Initialise(void)
{
	char (*oldfunc)(void) = (char (*)(void))0x5DD910;
	char ret;
	ret = oldfunc();

	HRSRC resource = FindResource(dllModule, MAKEINTRESOURCE(IDR_GRASSPS), RT_RCDATA);
	RwUInt32 *shader = (RwUInt32*)LoadResource(dllModule, resource);
	RwD3D9CreatePixelShader(shader, &grassPixelShader);
	FreeResource(shader);

	RpAtomic *atomic;
	for(int i = 0; i < 4; i++){
		atomic = (*plantTab0)[i];
		atomic->renderCallBack = grassRenderCallback;
		atomic = (*plantTab1)[i];
		atomic->renderCallBack = grassRenderCallback;
	}
	return ret;
}

int
FX::GetFxQuality_ped(void)
{
	if(config->pedShadows >= 0)
		return config->pedShadows ? 3 : 0;
	return this->fxQuality;
}

int
FX::GetFxQuality_stencil(void)
{
	if(config->stencilShadows >= 0)
		return config->stencilShadows ? 3 : 0;
	return this->fxQuality;
}

unsigned __int64 rand_seed = 1;
float ps2randnormalize = 1.0f/0x80000000;

int ps2rand()
{
	rand_seed = 0x5851F42D4C957F2D * rand_seed + 1;
	return ((rand_seed >> 32) & 0x7FFFFFFF);
}

void ps2srand(unsigned int seed)
{
	rand_seed = seed;
}

void __declspec(naked) floatbitpattern(void)
{
	_asm {
		fstp [esp-4]
		mov eax, [esp-4]
		sar eax,1
		ret
	}
}

WRAPPER void gtasrand(unsigned int seed) { EAXJMP(0x821B11); }

void
mysrand(unsigned int seed)
{
	gtasrand(ps2rand());
//	gtasrand(seed);
}

WRAPPER void CVehicle__DoSunGlare(void *this_) { EAXJMP(0x6DD6F0); }


void __declspec(naked) doglare(void)
{
	_asm {
		mov	ecx, [config]
		cmp	[ecx+12], 0	// doglare
		jle	noglare
		mov	ecx,esi
		call	CVehicle__DoSunGlare
	noglare:
		mov     [esp+0D4h], edi
		push	6ABD04h
		retn
	}
}

struct PointLight
{
	RwV3d pos;
	RwV3d dir;
	float radius;
	float color[3];
	void *attachedTo;
	char type;
	char fogType;
	char generateExtraShadows;
	char pad;
};

PointLight *pointLights = (PointLight*)0xC3F0E0;

WRAPPER void
CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect_orig(float x, float y, float z, float a4, float a5, RwUInt8 r, RwUInt8 g, RwUInt8 b, RwInt16 f, int a10, float a11, RwUInt8 alpha) { EAXJMP(0x70E780); }

int currentLight;
char *stkp;

void
CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect(float x, float y, float z, float a4, float a5, RwUInt8 r, RwUInt8 g, RwUInt8 b, RwInt16 f, int a10, float a11, RwUInt8 alpha)
{
	_asm mov [currentLight], esi
	_asm mov [stkp], ebp
	float mult = *(float*)(stkp + 0x48);
	currentLight /= sizeof(PointLight);

	float add = pointLights[currentLight].fogType == 1 ? 0.0f : 16.0f;
	r = mult*pointLights[currentLight].color[0]+add;
	g = mult*pointLights[currentLight].color[1]+add;
	b = mult*pointLights[currentLight].color[2]+add;
	f = 0xFF;
	CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect_orig(x, y, z, a4, a5, r, g, b, f, a10, a11, alpha);
}

WRAPPER void
CreateRoadsignTexture_RwTextureSetName_orig(RwTexture* texture, char* name) { EAXJMP(0x7F38A0); }

void CreateRoadsignTexture_RwTextureSetName(RwTexture* texture, char* name)
{
	strcpy_s(texture->name, 32, "roadsign");
	*RWPLUGINOFFSET(TexInfo*, texture, texdbOffset) = FindTexInfo("roadsign");
	CreateRoadsignTexture_RwTextureSetName_orig(texture, name);
}

WRAPPER RpAtomic*
CreateRoadsignAtomicA_RpAtomicSetGeometry_orig(RpAtomic* atomic, RpGeometry* geometry, int sameBoundingSphere) { EAXJMP(0x749D40); }

void
CreateRoadsignAtomicA_RpAtomicSetGeometry(RpAtomic* atomic, RpGeometry* geometry, int sameBoundingSphere)
{
	// still not good, maybe create a new one for specially for roadsign
	// try looking at roadsign text without roadsign plate, the text should be smooth like that
	SetPipelineID(atomic, RSPIPE_PC_CustomBuilding_PipeID);
	RpAtomicSetPipeline(atomic, CCustomBuildingPipeline__ObjPipeline);
	atomic->pipeline = CCustomBuildingPipeline__ObjPipeline;

	CreateRoadsignAtomicA_RpAtomicSetGeometry_orig(atomic, geometry, sameBoundingSphere);
}


/*
struct CVector { float x, y, z; };
WRAPPER void CWaterLevel__CalculateWavesForCoordinate(int x, int y, float a3, float a4, float *z, float *colorMult, float *a7, CVector *vecnormal){ EAXJMP(0x6E6EF0); }
void
CWaterLevel__CalculateWavesForCoordinate_hook(int x, int y, float a3, float a4, float *z, float *colorMult, float *a7, CVector *vecnormal)
{
	CWaterLevel__CalculateWavesForCoordinate(x, y, a3, a4, z, colorMult, a7, vecnormal);
	*colorMult = 0.577f;
}
*/

static int alphafunc;
static void setMoonAlphaBlendStates(void){
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &alphafunc);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONALWAYS);
	RwD3D9SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, 1);
	RwD3D9SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);
	RwD3D9SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_SRCALPHA);
	RwD3D9SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ZERO);
}
static void restoreMoonAlphaBlendStates(void){
	RwD3D9SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, 0);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)alphafunc);
}
void __declspec(naked) renderMoonMask(void)
{
	_asm {
		call setMoonAlphaBlendStates
		mov  eax,0x70D000
		call eax
		call restoreMoonAlphaBlendStates
		push 0x713C51
		retn
	}
}

// buildingPipe.cpp — three-layer (rw cache / driver cache / raw device)
// alpha+blend block repair; the alpha half of pipeForceCullMode. Exported
// here because the alpha-forced skidmark draw below needs the SAME contract
// the building cbs get (pipelinecommon.cpp's pipeEnterAlphaMode is not
// exported for ad-hoc use outside a pipe entry/exit pair).
extern void pipeForceAlphaBlock(void);

void (*CSkidmarks__Render_orig)(void);
void CSkidmarks__Render(void)
{
	// Alpha-forced draw (see docs/obsidian/Hook Architecture.md
	// "0x53E175: CSkidmarks__Render (alpha test fix)").
	//
	// The original version of this hook pushed rwALPHATESTFUNCTIONALWAYS
	// through the rw cache ONLY and restored it the same way — a one-layer
	// write, exactly what the three-layer state rule forbids:
	//   - a state the driver cache already holds is a no-op, so a DEVICE
	//     that drifted (postfx Save/RestoreRawGeomStates raw-restores
	//     ALPHABLENDENABLE/SRCBLEND/DESTBLEND/BLENDOP, DepthHook raw
	//     ZENABLE) never sees either the force or the restore;
	//   - the states this draw actually DEPENDS on — D3DRS_ALPHATESTENABLE
	//     and D3DRS_ALPHAREF — have no rw render state at all, so an
	//     rw-only force could never touch them: the draw ran with whatever
	//     the previous pass left (test OFF → every transparent texel of the
	//     mark's cutout texture accepted; blending OFF → RGB=0 lands
	//     unmodulated = solid black pool).
	// Force and restore now go through rw cache + driver cache + raw device
	// via pipeForceAlphaBlock, so what the next consumer reads (rw cache) and
	// what the device is actually using agree on both sides of the call.
	int alphafunc = rwALPHATESTFUNCTIONGREATEREQUAL;
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &alphafunc);
	// rw NA(0)/garbage must not reach the device (same rule as
	// pipelinecommon pipeAlphaFuncToD3D).
	if(alphafunc < rwALPHATESTFUNCTIONNEVER || alphafunc > rwALPHATESTFUNCTIONALWAYS)
		alphafunc = rwALPHATESTFUNCTIONGREATEREQUAL;

	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONALWAYS);
	pipeForceAlphaBlock();

	CSkidmarks__Render_orig();

	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)alphafunc);
	pipeForceAlphaBlock();
}

static uint32_t RenderScene_A;
WRAPPER void RenderScene(void) { VARJMP(RenderScene_A); }

void RenderReflectionMap_leeds(void);
void RenderReflectionScene(void);
// DrawDebugEnvMap removed — CLASS 6: dead code, empty body

bool
RenderScene_before(void*)
{
	static int callCount = 0;
	if(callCount++ < 5)
		dbglog("RenderScene_before: frame %d", callCount);

	// Lazily install INTZ depth hook on first frame where d3d9device is valid.
	// Must be here (not in RenderScene_hook) because RenderScene_before is
	// called from both RenderScene_hook AND the UG mod callback path, but
	// RenderScene_hook itself may not be called if UG mod handles events.
	// NOTE (2026-09-14 crash fix #2): DepthHook_Install vtable-patches the
	// D3D9 device. Under MoonLoader this now runs for the first time ever
	// (previously dead code), and the very next device call in
	// ForwardPlus_CullAndUpload crashes at 0x7FBD4A (+0x60 null deref).
	// Disabled until hook timing/slots are verified. Depth effects degrade.
	// NOTE (2026-09-14): re-enabled — crash-fix #3 proved the 0x7FBD4A crash
	// was the SMAA scratch camera leaking its render target, not this hook
	// (game crashed identically with it disabled). Depth chain restores
	// SSAO / velocity buffer / normal buffer.
	static bool s_depthHookInstalled = false;
	if(!s_depthHookInstalled && d3d9device) {
		DepthHook_Install(d3d9device);
		s_depthHookInstalled = true;
	}

	// Deferred SMAA raster init: force-create D3D9 surfaces for CAMERATEXTURE
	// rasters. Must run OUTSIDE the main camera's BeginUpdate because RW 3.6's
	// D3D9 driver crashes when creating render-target textures mid-frame.
	// NOTE (2026-09-14 crash fix #3): trace proves ForwardPlus_CullAndUpload
	// completes fully (all 5 steps incl. tile upload) and the crash at
	// 0x7FBD4A (+0x60) happens downstream. Prime suspect is the scratch-camera
	// SMAA raster init leaving D3D render-target state behind. Disabled until
	// save/restore of device state is added. SMAA degrades to skip path.
	// NOTE (2026-09-14): re-enabled — SMAATryInitRasters now detaches the
	// scratch raster and restores the backbuffer render target (crash fix #3).
	SMAATryInitRasters();

	// NOTE (2026-09-14 crash fix): EndUpdate on a camera that hasn't begun
	// corrupts RW state -> READ crash at 0x7F98DF accessing 0x60 on the
	// next BeginUpdate. Under MoonLoader this hook now actually runs for the
	// first time (previously dead), exposing the latent bug. The far/fog
	// refresh dance is skipped until a safe begin-state query exists.
	// RwCameraEndUpdate(Scene.camera);
	// RwCameraBeginUpdate(Scene.camera);

	// Forward+ tiled light culling (before scene render)
	ForwardPlus_CullAndUpload();

	// One-shot: confirm FP cull completed on frame 1 (crash diagnostics)
	static bool s_fpCullLogOnce = false;
	if(!s_fpCullLogOnce) {
		s_fpCullLogOnce = true;
		dbglog("RenderScene_before: frame1 FP cull complete");
	}

	// Blank-world root-cause probe (m0170/m0171): a mid-frame
	// RwCameraSetRaster(Scene.camera, ...) swap makes the SCENE render into a
	// non-screen raster — the screen never receives the world (gray/white
	// void, only effects-phase sprites on top). Observed live: geometry-phase
	// reads alternated 2048x1024 / 1920x1080 twice per frame (IBL texture
	// 512x256 == 2048x1024/4, classify RT recreated at 2048x1024) while the
	// real screen is 1920x1080. Log the raster POINTER + size at scene entry
	// (throttled, mismatch only): correlating the pointer against the known
	// rasters in the same log (camRas=0488EFC0 pRasFB=04890A38 envFB=048911C8
	// ...) names the pass that owns the swap.
	if(dbglog_throttle("camras")){
		RwRaster *rs = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
		int scrW = RsGlobal ? RsGlobal->MaximumWidth : 0;
		int scrH = RsGlobal ? RsGlobal->MaximumHeight : 0;
		if(rs && scrW > 0 && (rs->width != scrW || rs->height != scrH))
			dbglog("[RENDER] scene-entry camRas=%p %dx%d != screen %dx%d — camera-raster swap active",
				(void*)rs, rs->width, rs->height, scrW, scrH);
	}

	// V7: Apply ragdoll hierarchy pose BEFORE rendering so bone
	// matrices are live when the scene draws. This fixes the 1-frame
	// stale pose that occurred when writing in RenderScene_after.
	Ragdoll_ApplyPose();

	// update wind
	float freq = 0.01f;
	float modifierX = freq + (freq * CWeather__WindDir.x);
	float modifierY = freq + (freq * CWeather__WindDir.y);
	windPos.x += modifierX * CTimer__ms_fTimeStep;
	windPos.y += modifierY * CTimer__ms_fTimeStep;

	return true;
}

float cloudAnimTimer = 0.0f;

// ============================================================
// Effects-phase three-layer alpha/Im2D resync
// (coronas, particles, glows — every alpha/additive sprite that draws
//  OUTSIDE the building/vehicle/ped pipes)
// ============================================================
// ROOT CAUSE of the black corona/particle quads: CCoronas::Render /
// CClouds::Render / g_fx.Render draw their glow sprites ADDITIVELY —
// gta-reversed Coronas.cpp:195-196, 382-383, 536-537 and Clouds.cpp:706-707
// all do
//     RwRenderStateSet(rwRENDERSTATESRCBLEND,  rwBLENDONE);
//     RwRenderStateSet(rwRENDERSTATEDESTBLEND, rwBLENDONE);
//     RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, TRUE);
// and those writes only reach LAYER (1) — the rw cache — first (three-layer
// model: pipelinecommon.cpp:883: rw cache / RW driver cache pending[] /
// raw device). The mod's canonical alpha block writes layers (2)/(3) from
// its own snapshots (pipeEnterAlphaMode, pipeForceAlphaBlock, postfx
// Save/RestoreRawGeomStates, the RwD3D9-only sets inside postfx passes), so
// layer (1) can keep reading ONE/ONE (or VERTEXALPHA=TRUE) while the device
// sits on SRCALPHA/INVSRCALPHA or blend OFF. The game's RwRenderStateSet is
// then a no-op against an unchanged rw cache, the device never moves, and
// the additive sprite's RGB=0 background lands unmodulated:
//   * sun corona      -> large black square, bright flare core still visible
//   * traffic-light / pickup-marker / headlight-line coronas -> black quads
//     (jagged where a high inherited ALPHAREF cuts the soft falloff)
//   * particles / glass / weapon FX -> same family
// Every pipe entry repairs this via pipeForceAlphaBlock; the effects phase
// had NO repair point at all — this is it. Same deterministic-entry
// contract as CSkidmarks__Render (main.cpp ~:1354) and the ped Switch
// (main.cpp ~:747).
//
// The same sweep clears a STUCK Im2D override: the manual
// `overrideIm2dPixelShader = ps; draw; = nil;` users in postfx.cpp skip
// their `= nil` when the draw faults into their __except (the class
// chars.cpp/SSS fixed with guardedIm2DRender), and Im2dSetPixelShader_hook
// re-applies that stale PS to EVERY later Im2D dispatch — i.e. straight
// onto the corona/particle draws below.
// ============================================================
extern void pipeForceAlphaBlock(void);	// buildingPipe.cpp
extern void *overrideIm2dPixelShader;	// postfx.cpp
extern int overrideColorMod;		// postfx.cpp
extern int overrideAlphaMod;		// postfx.cpp

static void
effectsForceAlphaBlock(const char *tag)
{
	// ---- throttled three-layer evidence, sampled BEFORE the repair ----
	// (1) rw cache, (2) RW driver cache, (3) raw device. Reads only happen
	// while the throttle window is open, so this is ~free per frame.
	if(dbglog_throttle("fx_alpha")){
		RwUInt32 rSrc = rwBLENDSRCALPHA, rDst = rwBLENDINVSRCALPHA, rVtx = FALSE;
		RwUInt32 rFn = rwALPHATESTFUNCTIONGREATEREQUAL, rRef = 1;
		RwRenderStateGet(rwRENDERSTATESRCBLEND, (void*)&rSrc);
		RwRenderStateGet(rwRENDERSTATEDESTBLEND, (void*)&rDst);
		RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)&rVtx);
		RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, (void*)&rFn);
		RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)&rRef);

		RwUInt32 pSrc = 0, pDst = 0, pVtx = 0, pFn = 0, pRef = 0;
		RwD3D9GetRenderState(D3DRS_SRCBLEND, &pSrc);
		RwD3D9GetRenderState(D3DRS_DESTBLEND, &pDst);
		RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE, &pVtx);
		RwD3D9GetRenderState(D3DRS_ALPHAFUNC, &pFn);
		RwD3D9GetRenderState(D3DRS_ALPHAREF, &pRef);

		DWORD dSrc = 0, dDst = 0, dVtx = 0, dFn = 0, dRef = 0, dOp = 0;
		if(d3d9device){
			d3d9device->GetRenderState(D3DRS_SRCBLEND, &dSrc);
			d3d9device->GetRenderState(D3DRS_DESTBLEND, &dDst);
			d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &dVtx);
			d3d9device->GetRenderState(D3DRS_ALPHAFUNC, &dFn);
			d3d9device->GetRenderState(D3DRS_ALPHAREF, &dRef);
			d3d9device->GetRenderState(D3DRS_BLENDOP, &dOp);
		}
		// drift = any layer pair disagreeing, or a non-ADD blend op (a leaked
		// SUBTRACT/REVSUBTRACT turns an additive glow into a black quad too)
		bool drift = (rSrc != pSrc) || (rDst != pDst) || (rVtx != pVtx) ||
		             (rFn != pFn) || (rRef != pRef) ||
		             (dSrc != pSrc) || (dDst != pDst) || (dVtx != pVtx) ||
		             (dFn != pFn) || (dRef != pRef) ||
		             (dOp != (DWORD)D3DBLENDOP_ADD);
		if(drift)
			dbglog("[FxAlpha] %s: layer drift BEFORE repair — "
			       "rw(src=%u dst=%u vtx=%u fn=%u ref=%u) "
			       "d3d(src=%u dst=%u vtx=%u fn=%u ref=%u) "
			       "dev(src=%u dst=%u vtx=%u fn=%u ref=%u op=%u)",
			       tag,
			       (unsigned)rSrc, (unsigned)rDst, (unsigned)rVtx, (unsigned)rFn, (unsigned)rRef,
			       (unsigned)pSrc, (unsigned)pDst, (unsigned)pVtx, (unsigned)pFn, (unsigned)pRef,
			       (unsigned)dSrc, (unsigned)dDst, (unsigned)dVtx, (unsigned)dFn, (unsigned)dRef,
			       (unsigned)dOp);

		// Camera-raster probe (blank-world hunt, m0171): effects-phase sites
		// run at the FIRST scene draw (CClouds) and around the frame's
		// postfx — a mismatch here names WHEN the non-screen raster swap is
		// live; the POINTER correlates against known rasters in this log
		// (camRas=0488EFC0 pRasFB=04890A38 envFB=048911C8 ...). Mismatch only.
		RwRaster *fxRas = Scene.camera ? RwCameraGetRaster(Scene.camera) : NULL;
		if(fxRas && RsGlobal && fxRas->width > 0 &&
		   (fxRas->width != RsGlobal->MaximumWidth || fxRas->height != RsGlobal->MaximumHeight))
			dbglog("[FxAlpha] %s camRas=%p %dx%d != screen %dx%d — non-screen raster live",
			       tag, (void*)fxRas, fxRas->width, fxRas->height,
			       (int)RsGlobal->MaximumWidth, (int)RsGlobal->MaximumHeight);
	}

	// ---- stale Im2D override sweep (see block comment above) ----
	if(overrideIm2dPixelShader || overrideColorMod >= 0 || overrideAlphaMod >= 0){
		dbglog("[FxAlpha] %s: clearing stale Im2D override ps=%p colMod=%d alphaMod=%d",
		       tag, overrideIm2dPixelShader, overrideColorMod, overrideAlphaMod);
		overrideIm2dPixelShader = nil;
		overrideColorMod = -1;
		overrideAlphaMod = -1;
	}

	// ---- the repair itself: layers (1)->(2)->(3) to one canonical block ----
	// (test ON, BLENDOP ADD, colour-write ALL, separate-alpha OFF, ref bumped;
	//  SRCBLEND/DESTBLEND/ALPHABLENDENABLE derived from the rw cache, so the
	//  game's own RwRenderStateSet calls after this always transition.)
	pipeForceAlphaBlock();
}

// ---- RenderEffects (app_game.cpp, 0x53E170; only call site 0x53EAD3) ----
// Runs CCoronas::Render, g_fx.Render (particles), CGlass, CWeaponEffects,
// CSpecialFX, search lights and fog effects, then CPostEffects::Render —
// i.e. the whole effects phase plus the frame's postfx in one call.
static void (*RenderEffects_orig)(void);
static void
RenderEffects_hook(void)
{
	effectsForceAlphaBlock("RenderEffects.enter");
	RenderEffects_orig();
	// Re-align after postfx (CPostEffects::Render is the last state writer
	// of the frame): HUD/radar/dialog sprites draw after this and would
	// otherwise inherit whatever layer split postfx left behind.
	effectsForceAlphaBlock("RenderEffects.exit");
}

// ---- CPostEffects::Render (0x7046E0) exit sweep ----
// RenderEffects tail-jumps here (0x53E227), so the RenderEffects.exit sweep
// normally covers it — but CPostEffects::Render is the frame's LAST state
// writer and the only place a postfx pass can leak a stuck Im2D override
// pixel shader (overrideIm2dPixelShader) that Im2dSetPixelShader_hook then
// re-applies to every later Im2D dispatch, i.e. straight onto the next
// frame's corona/particle quads. Hook the function itself so the stale-Im2D
// sweep + three-layer alpha repair run on postfx exit no matter which caller
// reached it (the tail jmp is the only reference in the exe).
static void (*CPostEffects__Render_orig)(void);
static void
CPostEffects__Render_hook(void)
{
	if(CPostEffects__Render_orig)
		CPostEffects__Render_orig();
	effectsForceAlphaBlock("PostFx.exit");
}

// ============================================================
// effectsForceAlphaBlock coverage audit (item 7)
// ============================================================
// Game-side effect draw entries and where they are repaired:
//   * RenderEffects (0x53E170) — CCoronas::Render, g_fx.Render (particles),
//     CGlass, CWeaponEffects, CSpecialFX, search lights, fog: enter+exit
//     sweeps via RenderEffects_hook.
//   * CClouds::Render (both call sites) — sun/moon/streaks/lens flare:
//     CClouds__Render_hygiene.
//   * CCoronas::RenderReflections (2 sites) + CCoronas::RenderSunReflection:
//     dedicated hooks.
//   * CSkidmarks::Render: dedicated hook.
//   * CPostEffects::Render exit: CPostEffects__Render_hook (above).
// Rotors are NOT an effects-phase entry: they draw as vehicle geometry
// through the vehicle pipe's dual pass (D3D9RenderDual / D3D9RenderDefault_
// DUAL / D3D9RenderBlack_DUAL), so they are covered by the two-layer leg
// states (setAlphaTestTwoLayer / setZWriteTwoLayer), not by this sweep.
// Residual: any effect entry reached outside RenderEffects/CClouds (e.g. a
// cutscene-only sprite pass) would still need its own hook.
// ============================================================

// ---- CCoronas::RenderReflections (wet-ground glows) ----
static void (*CCoronas__RenderReflections_orig)(void);
static void
CCoronas__RenderReflections_hook(void)
{
	effectsForceAlphaBlock("Coronas.Reflections");
	CCoronas__RenderReflections_orig();
}

// ---- CCoronas::RenderSunReflection (sun on water) ----
static void (*CCoronas__RenderSunReflection_orig)(void);
static void
CCoronas__RenderSunReflection_hook(void)
{
	effectsForceAlphaBlock("Coronas.SunReflection");
	CCoronas__RenderSunReflection_orig();
}

// ============================================================
// DEBUG-ONLY: hardware write-watchpoint on &Scene.camera->frameBuffer
// ============================================================
// RwCameraSetRaster is an RW 3.6 SDK INLINE MACRO (external/d3d9/rwcore.h:
// 4525 -> camera->frameBuffer = raster) so the mystery mid-frame swap to a
// 2048x1024 raster (see "camras" log above) cannot be hooked as a call.
// Instead: claim DR0 as a 4-byte WRITE watchpoint on the frameBuffer field
// itself, armed from the first outdoor scene entry (CClouds__Render_hygiene
// below), and let a dedicated VEH report the writer's EIP + owning module +
// static offset, then clear DR7.L0 so the re-executed store completes and
// each offender logs once (next outdoor frame re-arms).
//
// Gate: config->postfxDumpDebug — the existing debug switch that already
// gates the camera-raster dump diagnostics. Release users never install the
// VEH and never touch the DR registers.
//
// Exception safety: every Scene deref and DR-register access is
// __try/__except wrapped; any failure (OpenThread denied, SetThreadContext
// rejected, DR0 owned by a debugger, too many fires) latches the probe off
// silently. All locals are POD — no destructible objects in any __try
// scope, so C2712/C2713 cannot fire (same frame-SEH style as
// InitialiseGame_hook :2067 and the cleanup guards at :4097+).
// ============================================================

// DR7 bits for a DR0 dword-write watchpoint, locally enabled:
//   L0        = bit 0        (local enable)
//   RW0  (01) = bits 17:16   (write)
//   LEN0 (10) = bits 19:18   (4 bytes)
#define FBWP_DR7_MASK 0x000C0001u
#define FBWP_DR7_ARM  0x00090001u

enum { FBWP_OFF = 0, FBWP_ARMED = 1, FBWP_FIRED = 2, FBWP_UNAVAILABLE = 3 };
static volatile LONG s_fbWpState;         // FBWP_*
static int s_fbWpVehInstalled;            // our VEH is on the chain
static int s_fbWpFireCount;               // total fires (hard cap below)
static DWORD s_fbWpSeenEip[8];            // offenders already logged
static int s_fbWpSeenCount;

// Address of the watched field, 0 if Scene/camera unusable.
static DWORD
fbWp_TargetAddr(void)
{
	DWORD a = 0;
	__try {
		if(Scene.camera)
			a = (DWORD)&Scene.camera->frameBuffer;
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		a = 0;
	}
	return a;
}

static LONG WINAPI
fbWp_veh(EXCEPTION_POINTERS *ep)
{
	if(ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
		return EXCEPTION_CONTINUE_SEARCH;   // not ours — existing VEH chain proceeds
	if(s_fbWpState != FBWP_ARMED)
		return EXCEPTION_CONTINUE_SEARCH;

	// Verify the trap fired on our watched address when the record carries
	// the info ([0]=access type, [1]=address); skip foreign single-steps.
	DWORD hitAddr = 0, hitType = 0xFFFFFFFF;
	__try {
		if(ep->ExceptionRecord->NumberParameters >= 2){
			hitType = (DWORD)ep->ExceptionRecord->ExceptionInformation[0];
			hitAddr = (DWORD)ep->ExceptionRecord->ExceptionInformation[1];
		}
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	DWORD target = fbWp_TargetAddr();
	if(target && hitAddr && hitAddr != target)
		return EXCEPTION_CONTINUE_SEARCH;

	CONTEXT *ctx = ep->ContextRecord;
	DWORD eip = ctx->Eip;

	// Module owning the EIP (range check via GetModuleHandleEx, same
	// pattern as crashhandler.cpp selfDir) + static offset for RE lookup.
	char modName[64] = "?";
	DWORD modOff = 0;
	__try {
		HMODULE mod = NULL;
		if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)(uintptr_t)eip, &mod) && mod){
			modOff = eip - (DWORD)(DWORD_PTR)mod;
			char full[MAX_PATH];
			if(GetModuleFileNameA(mod, full, sizeof(full))){
				char *slash = strrchr(full, '\\');
				strncpy(modName, slash ? slash + 1 : full, sizeof(modName) - 1);
				modName[sizeof(modName) - 1] = 0;
			}
		}
	} __except(EXCEPTION_EXECUTE_HANDLER) { /* keep "?" */ }

	// Log once per unique offender EIP
	int seen = 0;
	for(int i = 0; i < s_fbWpSeenCount; i++)
		if(s_fbWpSeenEip[i] == eip){ seen = 1; break; }
	if(!seen){
		if(s_fbWpSeenCount < 8)
			s_fbWpSeenEip[s_fbWpSeenCount++] = eip;
		dbglog("[FBWP] WRITE to Scene.camera->frameBuffer addr=%p by EIP=%08X in %s+0x%X (access=%u, raster=%p)",
			(void*)target, eip, modName, modOff, hitType,
			Scene.camera ? (void*)Scene.camera->frameBuffer : NULL);
	}

	// Clear L0 so the re-executed store completes without re-trapping;
	// fires once per offender (fbWp_ArmOnce re-arms next outdoor entry).
	__try {
		ctx->Dr7 &= ~1u;
	} __except(EXCEPTION_EXECUTE_HANDLER) {}
	s_fbWpState = FBWP_FIRED;
	if(++s_fbWpFireCount >= 64)
		s_fbWpState = FBWP_UNAVAILABLE;   // persistent writer: stop paying dispatch cost
	return EXCEPTION_CONTINUE_EXECUTION;
}

// Install VEH (once) and (re-)arm DR0. Called from outdoor scene entry only.
static void
fbWp_ArmOnce(void)
{
	if(s_fbWpState == FBWP_UNAVAILABLE)
		return;

	if(!s_fbWpVehInstalled){
		// Priority 1 like diag_installVEH (diagnostics.cpp:374); ours lands
		// first on the chain but consumes ONLY our own single-steps — every
		// other exception is CONTINUE_SEARCH, so the existing pass-through
		// VEH behavior is untouched.
		if(!AddVectoredExceptionHandler(1, fbWp_veh)){
			s_fbWpState = FBWP_UNAVAILABLE;
			return;
		}
		s_fbWpVehInstalled = 1;
		dbglog("[FBWP] frameBuffer watchpoint probe active (postfxDumpDebug=1)");
	}

	if(s_fbWpState == FBWP_ARMED)
		return;

	DWORD target = fbWp_TargetAddr();
	if(!target){
		s_fbWpState = FBWP_UNAVAILABLE;
		return;
	}

	__try {
		HANDLE hThread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT,
			FALSE, GetCurrentThreadId());
		if(!hThread){
			s_fbWpState = FBWP_UNAVAILABLE;
			return;
		}
		CONTEXT ctx;
		memset(&ctx, 0, sizeof(ctx));
		ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
		int ok = GetThreadContext(hThread, &ctx) != 0;
		if(ok && (ctx.Dr7 & FBWP_DR7_MASK) != FBWP_DR7_ARM){
			// Only claim DR0 — never steal a slot a debugger owns at another
			// address, and never touch DR1-3.
			if((ctx.Dr7 & 1u) && ctx.Dr0 != target)
				ok = 0;
			else {
				ctx.Dr0 = target;
				ctx.Dr7 = (ctx.Dr7 & ~FBWP_DR7_MASK) | FBWP_DR7_ARM;
				ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
				ok = SetThreadContext(hThread, &ctx) != 0;
			}
		}
		CloseHandle(hThread);
		s_fbWpState = ok ? FBWP_ARMED : FBWP_UNAVAILABLE;
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		s_fbWpState = FBWP_UNAVAILABLE;
	}
}

// ============================================================
// CClouds::Render render-state hygiene (INI SkyGfx/cloudRenderFix, default 1)
// ============================================================
// CClouds::Render (0x713950..0x714640, called from 0x53DCA2 and 0x53DFA0 —
// both intercepted below) sets EXACTLY five render states on entry:
//   ZWRITE=FALSE, ZTEST=FALSE, VERTEXALPHA=TRUE, SRCBLEND=ONE, DESTBLEND=ONE
// and nothing else — no texture filter, no alpha-test function, no fog. It is
// the first thing drawn in the scene pass (RenderScene: CClouds::Render then
// the world), so it inherits whatever the previous frame's passes left, and
// this mod moves all three of those states around every frame:
//
//   * TEXTUREFILTER — several postfx passes raw-write stage-0
//     D3DSAMP_MAG/MINFILTER = D3DTEXF_POINT straight on the device (they
//     bypass the rw cache, so RwRenderStateSet can never repair them). Cloud
//     sprites are point-sampled then: a cloud near the camera is magnified
//     and its alpha edges stair-step into hard blocky cuts.
//
//   * ALPHATEST — the dual-pass / alpha-forced world draws push
//     GREATEREQUAL @ config->zwriteThreshold through the rw cache, and the
//     cache/device can disagree (raw D3DRS_ALPHATESTENABLE writes). An
//     alpha-cut cloud loses its soft fringe, which BOTH hardens the edge AND
//     shrinks coverage: a small cloud (i.e. seen from ground level, where
//     each cloud is far and tiny) keeps only its dense core and reads as
//     nearly invisible — while the very same cloud fills the view once you
//     are up at its altitude and it covers hundreds of pixels.
//
//   * FOGENABLE — a leaked scene fog dissolves the FAR cloud layer into the
//     horizon haze. From the ground every cloud is far away (fogged out);
//     from high up you are inside the layer (short range, barely any fog).
//
// All three are forced through BOTH layers (rw cache + raw device) because a
// plain RwRenderStateSet is a no-op when the cache already holds the value —
// exactly what a foreign raw write leaves behind (see pipeEnterAlphaMode).
// No brightness/alpha is touched, so clouds cannot over-blow when high up.
// States are saved (pre-seeded, a failed Get never reaches the device) and
// restored on exit. cloudRenderFix = 0 skips the whole thing.
// ============================================================
extern RwUInt32 pipeAlphaFuncToD3D(RwUInt32 rwFunc);

bool cloudRenderFix;
void (*CClouds__Render_orig)(void);

static void
CClouds__Render_hygiene(void)
{
	// Sun corona / moon / streaks / lens flare draw inside this call and are
	// the FIRST sprites of the scene pass (before any pipe entry can repair
	// device drift) — align layers (1)/(2)/(3) first. CClouds::Render only
	// sets SRCBLEND/DESTBLEND/VERTEXALPHA through the rw cache, so a device
	// parked on SRCALPHA/INVSRCALPHA behind an unchanged cache turns the
	// additive sun glow into a black square (see effectsForceAlphaBlock).
	// Runs before the cloudRenderFix gate: the corona draw does not depend
	// on that INI flag.
	effectsForceAlphaBlock("Clouds");

	// DEBUG probe: CClouds::Render only runs outdoors, so this is the
	// first-outdoor-scene-entry hook point for the frameBuffer write
	// watchpoint. No-op unless postfxDumpDebug=1; idempotent per frame.
	if(config && config->postfxDumpDebug)
		fbWp_ArmOnce();

	if(!cloudRenderFix || !CClouds__Render_orig){
		// Nothing forced on this path — no restore to protect, no guard needed.
		if(CClouds__Render_orig)
			CClouds__Render_orig();
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
	if(d3d9device){
		d3d9device->GetSamplerState(0, D3DSAMP_MINFILTER, &sMinF);
		d3d9device->GetSamplerState(0, D3DSAMP_MAGFILTER, &sMagF);
		d3d9device->GetSamplerState(0, D3DSAMP_MIPFILTER, &sMipF);
		d3d9device->GetRenderState(D3DRS_FOGENABLE, &sFogDev);
	}

	// ---- force (both layers; flush first so the rw sets are real
	// transitions instead of no-ops against a desynced cache) ----
	_rwD3D9RenderStateFlushCache();
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONALWAYS);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	if(d3d9device){
		d3d9device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		d3d9device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		d3d9device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
	}
	RwD3D9SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D(rwALPHATESTFUNCTIONALWAYS));
	RwD3D9SetRenderState(D3DRS_ALPHAREF, 0);
	RwD3D9SetRenderState(D3DRS_FOGENABLE, FALSE);
	if(d3d9device){
		d3d9device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS);
		d3d9device->SetRenderState(D3DRS_ALPHAREF, 0);
		d3d9device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	}

	// SEH guard (same frame-SEH precedent as InitialiseGame_hook): if the
	// cloud/sun-corona draw faults mid-call, the 3-layer restore below MUST
	// still run — letting the fault escape leaves ALPHAFUNC=ALWAYS /
	// ALPHAREF=0 armed for the rest of the frame, i.e. every later
	// alpha-cutout mesh accepts all texels (black sprites/decals covering
	// the screen — the "whole screen covered outdoors" symptom). All locals
	// in this function are POD (RwUInt32/DWORD/pointers), so __try/__except
	// compiles without C2712/C2713.
	static DWORD s_cloudFaultCode;
	__try {
		CClouds__Render_orig();
	} __except(
		(s_cloudFaultCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
	) {
		if(dbglog_throttle("clouds_fault"))
			dbglog("CClouds__Render_hygiene: FAULT code=0x%08X inside CClouds::Render — forcing 3-layer restore",
				s_cloudFaultCode);
	}

	// ---- restore: rw cache first (so later Gets agree), then the raw force
	// closes the gap where the cache already read the saved value ----
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)sFilter);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)sAlphaFn);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)sAlphaRf);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)sFog);
	RwD3D9SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D(sAlphaFn));
	RwD3D9SetRenderState(D3DRS_ALPHAREF, sAlphaRf);
	RwD3D9SetRenderState(D3DRS_FOGENABLE, sFogDev ? TRUE : FALSE);
	if(d3d9device){
		d3d9device->SetSamplerState(0, D3DSAMP_MINFILTER, sMinF);
		d3d9device->SetSamplerState(0, D3DSAMP_MAGFILTER, sMagF);
		d3d9device->SetSamplerState(0, D3DSAMP_MIPFILTER, sMipF);
		d3d9device->SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D(sAlphaFn));
		d3d9device->SetRenderState(D3DRS_ALPHAREF, sAlphaRf);
		d3d9device->SetRenderState(D3DRS_FOGENABLE, sFogDev ? TRUE : FALSE);
	}
}

bool
RenderScene_after(void*)
{
	// Debug dump: capture camera raster after scene render (INI-gated)
	PostFX_DumpSceneCamera();

	// Death ragdoll simulation + gib rendering — runs after game
	// anim processing. Hierarchy write-back moved to Ragdoll_ApplyPose
	// in RenderScene_before to avoid 1-frame stale poses.
	// V7: Ragdoll_Simulate is frame-gated internally.
	Ragdoll_Simulate();

	// Render sphere env map for pipelines that use CCustomCarEnvMapPipeline__CustomPipeRenderCB_Env
	// (CAR_ENV, CAR_MODERN) or CarPipe::RenderCallback (CAR_NEO).
	// NOTE: For CAR_ENV and CAR_MODERN, the env map is already rendered by
	// RenderSphereReflections (hooked on CRenderer__ConstructRenderList at 0x53E9F9,
	// envmap.cpp:652), which runs BEFORE the main scene render. That path handles
	// camera state properly (save/restore raster, view window, far/fog planes).
	// The redundant CarPipe::RenderEnvTex call here was corrupting D3D9 state
	// mid-frame (causing stretched/missing vehicle geometry and broken reflections).
	// CAR_NEO uses a different env map pipeline that needs RenderEnvTex.
	if(config->vehiclePipe == CAR_NEO)
		CarPipe::RenderEnvTex();
	else if(config->vehiclePipe == CAR_LCS || config->vehiclePipe == CAR_VCS)
		RenderReflectionMap_leeds();
	return true;
}
void
RenderScene_hook(void)
{
	// Frame counter for crash correlation
	static int s_renderFrame = 0;
	s_renderFrame++;

	// Ctrl+4 to toggle debug menu
	static bool s_f4Prev = false;
	bool ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	bool f4Now = ctrlHeld && (GetAsyncKeyState('4') & 0x8000) != 0;
	if(f4Now && !s_f4Prev){
		if(config->debugMenuOpen){
			config->debugMenuOpen = 0;
		}else{
			config->debugMenuOpen = 1;
		}
		dbglog("Debug menu toggled: %d (config=%p, &debugMenuOpen=%p)",
			config->debugMenuOpen, config, &config->debugMenuOpen);
	}
	s_f4Prev = f4Now;

	// Log first few frames for crash correlation
	if(s_renderFrame <= 3)
		dbglog("[RENDER] frame=%d world=%p camera=%p", s_renderFrame, Scene.world, Scene.camera);

	// Process ragdoll motion BEFORE render so modified matrices are visible
	g_ragdollMan.ProcessAllPeds(CTimer__ms_fTimeStep / 50.0f);
	g_ragdollMan.Update(CTimer__ms_fTimeStep / 50.0f);

	// Blend custom weathers2.dat timecyc into m_CurrentColours.
	// Must run AFTER CTimeCycle::Update() (already done at this point in the frame)
	// but BEFORE any rendering reads the values. This was previously never called,
	// so the alternate timecyc loaded but never applied (garages not black, dir ~0.05).
	Weather_Update();

	// Apply sun config multipliers (corona, core, streaks) to m_CurrentColours.
	// Must run AFTER CTimeCycle::Update() (already done at this point in the frame)
	// but BEFORE any rendering reads the values.
	Weather_ApplySunConfig();

	// Deferred normal-map plugin attach: polls until the RW engine is up,
	// then attaches once. No-op after success (guarded by normalmapInitialized).
	normalmap_tryAttach();

	RenderScene_before(nil);
	RenderScene();
	RenderScene_after(nil);
}

int (*PipelinePluginAttach)(void);
int
myPluginAttach(void)
{
	return PipelinePluginAttach() && PDSPipePluginAttach() && TexDBPluginAttach();
}

void (*InitialiseGame)(void);

// Scene-render hooks + envmaphooks were previously installed only from
// InitialiseGame_hook, which is skipped when MoonLoader is present (log:
// "SKIP: InitialiseGame (MoonLoader handles this)"). Consequence under
// MoonLoader: RenderScene_before never runs (ForwardPlus_CullAndUpload,
// DepthHook_Install, SMAA raster init, ragdoll, wind all dead) and
// envmaphooks() never runs (RenderSphereReflections never installed -> the
// real-time env map is never refreshed -> black vehicle reflections).
// installDeferredHooks() is called from BOTH InjectDelayedPatches (early,
// always) and InitialiseGame_hook (legacy path); the guard makes it idempotent.
static void
installDeferredHooks(void)
{
	static bool s_deferredHooksInstalled = false;
	if(s_deferredHooksInstalled)
		return;
	s_deferredHooksInstalled = true;

	if(!UG_RegisterEventCallback)
		InterceptCall(&RenderScene_A, RenderScene_hook, 0x53EABF);
	else{
		UG_RegisterEventCallback("EVENT_BEFORE_RENDERSCENE", RenderScene_before);
		UG_RegisterEventCallback("EVENT_AFTER_RENDERSCENE", RenderScene_after);
	}
	dbglog("installDeferredHooks: scene hooks installed");

	void envmaphooks(void);
	envmaphooks();
	dbglog("installDeferredHooks: envmaphooks done");
}

void
installLCMV2Hooks(void)
{
	// Removed - aap's skygfx doesn't hook these and works fine
}

void
InitialiseGame_hook(void)
{
	static int initCount = 0;
	initCount++;
	dbglog("InitialiseGame_hook: entered (call #%d)", initCount);

	if(initCount == 1){
		installDeferredHooks();
		neoInit();
		dbglog("InitialiseGame_hook: neoInit done");
		initTexDB();
		dbglog("InitialiseGame_hook: initTexDB done");
	}else{
		dbglog("InitialiseGame_hook: re-entry detected (call #%d), skipping hooks", initCount);
	}

	dbglog("InitialiseGame_hook: calling original CGame::Initialise...");
	static DWORD s_initCrashCode;
	static EXCEPTION_POINTERS* s_initCrashPtrs;
	g_allowCrashPassThrough = 1;
	__try {
		InitialiseGame();
		g_allowCrashPassThrough = 0;
		dbglog("InitialiseGame_hook: original returned successfully");
	} __except(
		(s_initCrashCode = GetExceptionCode(),
		 s_initCrashPtrs = GetExceptionInformation(),
		 EXCEPTION_EXECUTE_HANDLER)
	) {
		g_allowCrashPassThrough = 0;
		CONTEXT *ctx = s_initCrashPtrs->ContextRecord;
		DWORD storeCount = *(DWORD*)0xB1F650;
		dbglog("InitialiseGame_hook: CRASH in original! code=0x%08X addr=%p",
			s_initCrashCode, s_initCrashPtrs->ExceptionRecord->ExceptionAddress);
		dbglog("  EAX=%08X EBX=%08X ECX=%08X EDX=%08X", ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx);
		dbglog("  ESI=%08X EDI=%08X EBP=%08X ESP=%08X", ctx->Esi, ctx->Edi, ctx->Ebp, ctx->Esp);
		dbglog("  EIP=%08X vehicleStoreCount=%d (0x%X)", ctx->Eip, storeCount, storeCount);
		DWORD *sp = (DWORD*)ctx->Esp;
		for(int i = 0; i < 8; i++)
			dbglog("  [ESP+%d] = %08X", i*4, sp[i]);
		dbglog("InitialiseGame_hook: SEH caught crash, game state partial. Continuing...");
	}
	dbglog("InitialiseGame_hook: exiting (call #%d)", initCount);
}

void* RwIm3DTransform(RwIm3DVertex* pVerts, RwUInt32 numVerts, RwMatrix* ltm, RwUInt32 flags) {
	return ((void* (__cdecl*)(RwIm3DVertex*, RwUInt32, RwMatrix*, RwUInt32))0x7EF450)(pVerts, numVerts, ltm, flags);
}

RwBool RwIm3DEnd(void) {
	return ((RwBool(__cdecl*)(void))0x7EF520)();
}

RwBool RwIm3DRenderIndexedPrimitive(RwPrimitiveType primType, RwImVertexIndex* indices, RwInt32 numIndices) {
	return ((RwBool(__cdecl*)(RwPrimitiveType, RwImVertexIndex*, RwInt32))0x7EF550)(primType, indices, numIndices);
}

void (*CWaterLevel__RenderAndEmptyRenderBuffer)(void);
void
CWaterLevel__RenderAndEmptyRenderBuffer_hook(void)
{
	// Safety gate: only take over the water draw when the water pipe can
	// actually run (INI-enabled AND the style-selected shader compiled).
	// Otherwise fall through to the game's original draw — rendering the
	// water TempBuffer with stale PBR VS/PS left bound from a previous pipe
	// was producing garbage water.
	void *waterPS = waterPipe_pixelShader();
	bool canUseWaterPipe = TempBufferVerticesStored != 0 &&
	                       config->waterParallaxEnable &&
	                       waterPS && Water_VS;
	if(!canUseWaterPipe){
		if(TempBufferVerticesStored && dbglog_throttle("water_fallthru"))
			dbglog("[water] gate failed (enable=%d vs=%p ps=%p) -> original draw",
			       config->waterParallaxEnable ? 1 : 0, waterPS, Water_VS);
		// Original function renders AND empties the buffer (vanilla behaviour)
		if(CWaterLevel__RenderAndEmptyRenderBuffer)
			CWaterLevel__RenderAndEmptyRenderBuffer();
		return;
	}

	_rwD3D9RenderStateFlushCache();

	// Set water pipe render state (our VS/PS/constants)
	waterPipe_setRenderState();

	if(g_waterParallaxActive){
		// VERTEXALPHAENABLE is set inside waterPipe_setRenderState() — keep
		// set/restore paired inside this branch so an early-out can't leak it.
		// D3D9 primitive enum, NOT the RW one: this maps to
		// IDirect3DDevice9::DrawIndexedPrimitiveUP (rwcore.h
		// _rwD3D9DrawIndexedPrimitiveUPMacro forwards primitiveType raw), so
		// rwPRIMTYPETRILIST(3) lands on D3DPT_LINESTRIP(3) — the water quads
		// drew as one connected polyline ("blue wireframe streaks" bug).
		// D3DPT_TRIANGLELIST = 4. The 4th arg is a PRIMITIVE count (D3D9
		// convention), not an index count (RwIm3DRenderIndexedPrimitive's
		// convention): tri-list primitives = numIndices/3.
		RwD3D9DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, TempBufferVerticesStored,
		                            TempBufferIndicesStored / 3,
		                            TempBufferRenderIndexList, TempVertexBuffer, sizeof(RwIm3DVertex));

		// Restore previous state
		waterPipe_restoreRenderState();
		TempBufferVerticesStored = 0;
		TempBufferIndicesStored = 0;
	}else{
		// Setup failed mid-way (device lost / refraction RT unavailable) —
		// hand the buffer to the vanilla draw instead of drawing half-configured.
		if(CWaterLevel__RenderAndEmptyRenderBuffer)
			CWaterLevel__RenderAndEmptyRenderBuffer();
		return;
	}
}

/*void (*StartWaterRender)(void);
void
StartWaterRender_hook(void)
{
	RwD3D9SetPixelShader(simplePS);
	gpCurrentPixelShaderForDefaultCallbacks = simplePS;
	StartWaterRender();
}

void (*EndWaterRender)(void);
void
EndWaterRender_hook(void)
{
	EndWaterRender();
}*/

// not working yet
//void __declspec(naked) selectVM(void)
//{
//	_asm {
//		test	[esp+0x20],1
//		jz	window
//
//		push 0x7463B8
//		retn
//
//	window:
//		push 0x7462C5
//		retn
//	}
//}






int
readhex(char *str)
{
	int n = 0;
	if(strlen(str) > 2)
		sscanf(str+2, "%X", &n);
	return n;
}

BOOL FileExists(LPCTSTR szPath)
{
	DWORD dwAttrib = GetFileAttributes(szPath);
	return dwAttrib != INVALID_FILE_ATTRIBUTES && 
		   !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY);
}

struct StrAssoc
{
	const char *key;
	int val;

	static int get(StrAssoc *desc, const char *key);
};
int
StrAssoc::get(StrAssoc *desc, const char *key)
{
	for(; desc->key[0] != '\0'; desc++)
		if(strcmpi(desc->key, key) == 0)
			return desc->val;
	return desc->val;
}

void
findInis(void)
{
	char modulePath[MAX_PATH];
	GetModuleFileName(dllModule, modulePath, MAX_PATH);
	size_t nLen = strlen(modulePath);
	if (nLen + 1 < MAX_PATH) {
		modulePath[nLen+1] = L'\0';
	}
	modulePath[nLen] = L'i';
	modulePath[nLen-1] = L'n';
	modulePath[nLen-2] = L'i';
	modulePath[nLen-3] = L'.';
	modulePath[nLen-4] = '1';

	numConfigs = 0;
	while(numConfigs < 9 && FileExists(modulePath)){
		modulePath[nLen-4]++;
		numConfigs++;
	}
}

int
readhex(const char *str)
{
	int n = 0;
	if(strlen(str) > 2)
		sscanf(str+2, "%X", &n);
	return n;
}

int
readint(const std::string &s, int default = 0)
{
	try{
		return std::stoi(s);
	}catch(...){
		return default;
	}
}

float
readfloat(const std::string &s, float default = 0)
{
	try{
		return std::stof(s);
	}catch(...){
		return default;
	}
}

int explicitBuildingPipe_tmp;

// Parse one INI file into `c`. Shared by readIni (skygfx.ini / skygfxN.ini)
// and loadConfigFile (named configs under configs/).
// Missing-file semantics unchanged: writes the full default INI at that path.
static bool
readIniFile(const char *modulePath, Config *c)
{
	int tmpint;
	linb::ini cfg;
	bool iniExisted = cfg.load_file(modulePath);

	// Hotkeys: only overwrite when the INI actually carries them — named
	// configs (Save as New Config) don't write keySwitch/keyReload, so
	// missing keys must keep current bindings instead of resetting to 0x0.
	{
		std::string kval = cfg.get("SkyGfx", "keySwitch", "");
		if(!kval.empty())
			c->keys[0] = readhex(kval.c_str());
		kval = cfg.get("SkyGfx", "keyReload", "");
		if(!kval.empty())
			c->keys[1] = readhex(kval.c_str());
	}

	// [General] NoiseQuality (global utility noise texture): 0=32, 1=128,
	// 2=512, >=3=1024, default 0. Applied in refreshIni() -> the guarded
	// GenerateUtilityTexture call (regen only on actual resolution change).
	g_noiseQuality = readint(cfg.get("General", "NoiseQuality", ""), 0);

	// ===== Unified Pipeline (read first, locks building/vehicle pipes) =====
	// PBR = unified PBR for all assets, PS2/Xbox/Mobile/GTAIV = locked presets
	static StrAssoc pipelineMap[] = {
		{"PBR",    PIPELINE_PBR},
		{"Modern", PIPELINE_PBR},  // alias
		{"PS2",    PIPELINE_PS2},
		{"Xbox",   PIPELINE_XBOX},
		{"Mobile", PIPELINE_MOBILE},
		{"GTAIV",  PIPELINE_GTAIV},
		{"",       PIPELINE_PBR},  // default to PBR
	};
	c->pipeline = StrAssoc::get(pipelineMap, cfg.get("SkyGfx", "pipeline", "").c_str());

	// Pipeline override: force a specific pipeline for debugging (-1=disabled, 0=PBR, 1=PS2, 2=Xbox, 3=Mobile, 4=GTAIV)
	c->pipelineOverride = readint(cfg.get("SkyGfx", "pipelineOverride", ""), -1);
	if(c->pipelineOverride >= 0 && c->pipelineOverride <= 4)
		c->pipeline = c->pipelineOverride;

	// Map pipeline to internal building/vehicle pipes (locked presets)
	switch(c->pipeline){
	case PIPELINE_PBR:
		c->buildingPipe = BUILDING_PBR;
		c->vehiclePipe = CAR_MODERN;
		c->colorFilter = COLORFILTER_MODERN;
		break;
	case PIPELINE_PS2:
		c->buildingPipe = BUILDING_PS2;
		c->vehiclePipe = CAR_PS2;
		break;
	case PIPELINE_XBOX:
		c->buildingPipe = BUILDING_XBOX;
		c->vehiclePipe = CAR_XBOX;
		break;
	case PIPELINE_MOBILE:
		c->buildingPipe = BUILDING_XBOX;  // Mobile uses Xbox building pipe
		c->vehiclePipe = CAR_MOBILE;
		break;
	case PIPELINE_GTAIV:
		c->buildingPipe = BUILDING_GTAIV;
		c->vehiclePipe = CAR_GTAIV;
		break;
	default:
		c->buildingPipe = BUILDING_PBR;
		c->vehiclePipe = CAR_MODERN;
		break;
	}

	dbglog("Config: pipeline=%d buildingPipe=%d vehiclePipe=%d colorFilter=%d pipelineOverride=%d colorFilterEnable=%d",
		c->pipeline, c->buildingPipe, c->vehiclePipe, c->colorFilter, c->pipelineOverride, c->colorFilterEnable);

	// ===== Quality Preset (read second, sets feature defaults) =====
	// 0=LOW (PS2 classic), 1=MEDIUM (PC classic), 2=HIGH (Enhanced), 3=ULTRA (Full PBR)
	int preset = readint(cfg.get("SkyGfx", "qualityPreset", ""), 3);  // default to ULTRA

	// CLASS 1 FIX: After preset block, unconditional reads use c->field as default
	// so absent keys preserve the preset value instead of clobbering with hardcoded default.

	// Apply preset defaults — individual INI values override these
	switch(preset){
	case 0: // LOW - PS2 classic
		c->buildingPipe = BUILDING_PS2;
		c->vehiclePipe = CAR_PS2;
		c->colorFilter = COLORFILTER_PS2;
		c->smaaEnable = 0;
		c->ssaoEnable = 0;
		c->motionBlurEnable = 0;
		c->sssPostProcessEnable = 0;
		c->skinEnhanceEnable = 0;
		c->hairEnhanceEnable = 0;
		c->vegetationEnhanceEnable = 0;
		c->detailMaps = 0;
		c->stochastic = 0;
		c->dualPassBuilding = 0;
		c->dualPassVehicle = 0;
		c->dualPassGrass = 0;
		c->doglare = 0;
		c->neoWaterDrops = 0;
		c->ivDesaturation = 0.0f;
		c->ivGamma = 1.0f;
		c->ivVignetteIntensity = 0.0f;
		c->ivBloomIntensity = 0.0f;
		c->envMapSize = 128;
		c->envMapFarClipMult = 1.0f;
		break;
	case 1: // MEDIUM - PC classic
		c->buildingPipe = BUILDING_XBOX;
		c->vehiclePipe = CAR_PC;
		c->colorFilter = COLORFILTER_PC;
		c->smaaEnable = 1;
		c->ssaoEnable = 0;
		c->motionBlurEnable = 0;
		c->sssPostProcessEnable = 0;
		c->skinEnhanceEnable = 0;
		c->hairEnhanceEnable = 0;
		c->vegetationEnhanceEnable = 0;
		c->detailMaps = 1;
		c->stochastic = 0;
		c->dualPassBuilding = 1;
		c->dualPassVehicle = 1;
		c->doglare = 1;
		c->neoWaterDrops = 0;
		c->ivDesaturation = 0.0f;
		c->ivGamma = 1.0f;
		c->ivVignetteIntensity = 0.0f;
		c->ivBloomIntensity = 0.0f;
		c->envMapSize = 256;
		c->envMapFarClipMult = 1.0f;
		break;
	case 2: // HIGH - Enhanced
		c->buildingPipe = BUILDING_XBOX;
		c->vehiclePipe = CAR_MODERN;
		c->colorFilter = COLORFILTER_VCS;
		c->smaaEnable = 1;
		c->ssaoEnable = 1;
		c->ssaoRadius = 0.8f;
		c->ssaoPower = 1.5f;
		c->motionBlurEnable = 1;  // HIGH preset intentionally enables MB (with tuned strength); fresh-install INI defaults OFF
		c->motionBlurStrength = 0.3f;
		c->sssPostProcessEnable = 1;
		c->sssPostProcessStrength = 0.15f;
		c->skinEnhanceEnable = 1;
		c->hairEnhanceEnable = 1;
		c->vegetationEnhanceEnable = 1;
		c->detailMaps = 1;
		c->stochastic = 1;
		c->dualPassBuilding = 1;
		c->dualPassVehicle = 1;
		c->dualPassGrass = 1;
		c->doglare = 1;
		c->neoWaterDrops = 1;
		c->ivDesaturation = 0.2f;
		c->ivGamma = 1.0f;
		c->ivVignetteIntensity = 0.3f;
		c->ivVignetteRadius = 0.6f;
		c->ivVignetteContrast = 2.0f;
		c->ivBloomIntensity = 0.1f;
		c->ivExposure = 1.0f;
		c->envMapSize = 256;
		c->envMapFarClipMult = 1.5f;
		c->rgb1Mult = 0.9f;
		c->rgb2Mult = 0.9f;
		break;
	case 3: // ULTRA - Full PBR (settings from INI override below)
		// NOTE: buildingPipe and vehiclePipe are set by the pipeline switch above
		// Do NOT override them here — qualityPreset only sets features, not pipes
		c->colorFilter = COLORFILTER_VCS;
		c->smaaEnable = 1;
		c->ssaoEnable = 1;
		c->ssaoRadius = 1.0f;
		c->ssaoPower = 2.0f;
		c->ssaoKernelSize = 16;
		c->ssaoSampleCount = 16;
		c->motionBlurEnable = 1;  // ULTRA preset intentionally enables MB (full param set below); fresh-install INI defaults OFF
		c->motionBlurStrength = 0.4f;
		c->motionBlurRadial = 0.2f;
		c->motionBlurSpeedFactor = 0.3f;
		c->motionBlurCameraAware = 1;
		c->sssPostProcessEnable = 1;
		c->sssPostProcessStrength = 0.2f;
		c->sssPostProcessRadius = 3.0f;
		c->skinEnhanceEnable = 1;
		c->skinWrapFactor = 0.4f;
		c->skinSpecularPower = 24.0f;
		c->skinSpecularStrength = 0.2f;
		c->skinSSSStrength = 0.3f;
		c->hairEnhanceEnable = 1;
		c->hairAnisotropicPower = 48.0f;
		c->hairAnisotropicStrength = 0.4f;
		c->hairSSSStrength = 0.15f;
		c->vegetationEnhanceEnable = 1;
		c->vegetationSSSStrength = 0.2f;
		c->vegetationAmbientBoost = 1.3f;
		c->detailMaps = 1;
		c->stochastic = 1;
		c->dualPassBuilding = 1;
		c->dualPassVehicle = 1;
		c->dualPassGrass = 1;
		c->dualPassDefault = 1;
		c->dualPassPed = 1;
		c->doglare = 1;
		c->neoWaterDrops = 1;
		c->ivDesaturation = 0.15f; // was 1.0 = 50% grey wash under IVGrade saturate(desat*0.5)
		c->ivGamma = 1.0f;
		c->ivSaturation = 0.3f;
		c->ivCurves = 1.0f;
		c->ivVignetteIntensity = 0.15f;
		c->ivVignetteRadius = 0.70f;
		c->ivVignetteContrast = 1.5f;
		c->ivBloomIntensity = 0.05f;
		c->ivExposure = 2.5f;
		c->envMapSize = 512;
		c->envMapFarClipMult = 2.0f;
		c->envShininessMult = 1.0f;
		c->envSpecularityMult = 1.0f;
		c->envPower = 128.0f;
		// HIGH-2: was 0.95 — clampEnvSliderRanges pins envFresnel to [0..0.4]
		// (deliberate ceiling, see the read-site comment), so the preset writer
		// only ever produced an immediate clamp. Match the ceiling.
		c->envFresnel = 0.4f;
		c->rgb1Mult = 0.88f;
		c->rgb2Mult = 0.92f;
		break;
	}

	config->ps2ModulateGlobal = readint(cfg.get("SkyGfx", "ps2Modulate", ""), 0);
	config->dualPassGlobal = readint(cfg.get("SkyGfx", "dualPass", ""), 0);

	// Explicit buildingPipe/vehiclePipe INI values override pipeline mapping
	static StrAssoc buildingPipeMap[] = {
		{"PS2",    BUILDING_PS2},
		{"Xbox",   BUILDING_XBOX},
		{"GTAIV",  BUILDING_GTAIV},
		{"PBR",    BUILDING_PBR},
		{"",       -1},
	};
	int bpOverride = StrAssoc::get(buildingPipeMap, cfg.get("SkyGfx", "buildingPipe", "").c_str());
	if(bpOverride >= 0)
		c->buildingPipe = bpOverride;

	static StrAssoc vehiclePipeMap[] = {
		{"PS2",     CAR_PS2},
		{"PC",      CAR_PC},
		{"Xbox",    CAR_XBOX},
		{"Specular", CAR_SPEC},
		{"Mobile",  CAR_MOBILE},
		{"Neo",     CAR_NEO},
		{"Leeds",   CAR_LCS},
		{"VCS",     CAR_VCS},
		{"Env",     CAR_ENV},
		{"GTAIV",   CAR_GTAIV},
		{"Modern",  CAR_MODERN},
		{"",        -1},
	};
	int vpOverride = StrAssoc::get(vehiclePipeMap, cfg.get("SkyGfx", "vehiclePipe", "").c_str());
	if(vpOverride >= 0)
		c->vehiclePipe = vpOverride;

	// Pipe-conjunction #4: INI buildingPipe/vehiclePipe overrides (and the
	// qualityPreset case pipes above) diverge from the `pipeline` key's
	// locked combo — re-derive config->pipeline NOW so the PBR colour-filter
	// force below (:pipeline==PIPELINE_PBR -> MODERN) and the ivMode default
	// further down key on the ACTUAL pipes, not the stale INI pipeline.
	derivePipelineFromPipes(c);

	c->detailMaps = readint(cfg.get("SkyGfx", "detailMaps", ""), c->detailMaps);
	c->stochastic = readint(cfg.get("SkyGfx", "stochasticTexturing", ""), c->stochastic);

	c->ps2ModulateBuilding = readint(cfg.get("SkyGfx", "ps2ModulateBuilding", ""), config->ps2ModulateGlobal);
	c->dualPassBuilding = readint(cfg.get("SkyGfx", "dualPassBuilding", ""), config->dualPassGlobal);

	// PBR building env/reflection — ported from the GTA IV building path; see
	// skygfx.h and the env block at the end of main_building(). 0/0.0 = legacy
	// PBR (no reflection at all for env-mapped materials). Missing keys keep
	// the guarded defaults (1 / 0.15); out-of-range values are clamped so a
	// hand-edited INI cannot push a bad mask/strength into the shader.
	c->bldEnvReflect = readint(cfg.get("SkyGfx", "bldEnvReflect", ""), 1);
	if(c->bldEnvReflect < 0) c->bldEnvReflect = 0;
	if(c->bldEnvReflect > 1) c->bldEnvReflect = 1;
	c->bldSkyReflect = readfloat(cfg.get("SkyGfx", "bldSkyReflect", ""), 0.15f);
	if(c->bldSkyReflect < 0.0f) c->bldSkyReflect = 0.0f;
	if(c->bldSkyReflect > 1.0f) c->bldSkyReflect = 1.0f;

	c->dualPassVehicle = readint(cfg.get("SkyGfx", "dualPassVehicle", ""), config->dualPassGlobal);
	c->leedsShininessMult = readfloat(cfg.get("SkyGfx", "leedsShininessMult", ""), 1.0);
	c->neoShininessMult = readfloat(cfg.get("SkyGfx", "neoShininessMult", ""), 1.0);
	c->neoSpecularityMult = readfloat(cfg.get("SkyGfx", "neoSpecularityMult", ""), 1.0);
	c->envShininessMult = readfloat(cfg.get("SkyGfx", "envShininessMult", ""), 1.0);
	c->envSpecularityMult = readfloat(cfg.get("SkyGfx", "envSpecularityMult", ""), 1.0);
	c->envPower = readfloat(cfg.get("SkyGfx", "envPower", ""), 20.0);
	// envPower is a specular exponent: the default is ~20 and the ULTRA preset
	// uses 128. A hand-edited 380.36 (user report) collapses the highlight to a
	// pinprick / NaN-prone pow. Clamp to a sane [1..128] with a one-time note.
	if(c->envPower < 1.0f || c->envPower > 128.0f){
		static bool s_envPowerWarned = false;
		if(!s_envPowerWarned){
			s_envPowerWarned = true;
			dbglog("[Config] envPower=%.2f outside [1..128], clamping", c->envPower);
		}
		c->envPower = (c->envPower < 1.0f) ? 1.0f : 128.0f;
	}
	// HIGH-2: was 0.7 — the [0..0.4] ceiling in clampEnvSliderRanges (called
	// 7 lines below; comment: "Menu sliders ... 0.0-0.4 (user request)") is
	// deliberate, so align the absent-key default with it.
	c->envFresnel = readfloat(cfg.get("SkyGfx", "envFresnel", ""), 0.4f);
	// PBR vehicle layer bitmask (bit0=base,1=env,2=spec,3=rim,4=ibl,5=sky,6=clearcoat,7=normbuf)
	c->vehPBRLayers = readint(cfg.get("SkyGfx", "vehPBRLayers", ""), 255);
	c->vehEnvIntensity = readfloat(cfg.get("SkyGfx", "vehEnvIntensity", ""), 1.0f);
	// Menu sliders for these two are 0.0-0.4 (user request; useful values can
	// be as low as 0.02). Shared helper also re-asserted in refreshIni() and
	// saveConfigTo() so no load/save/runtime path can exceed the range.
	clampEnvSliderRanges(c);
	c->vehChromeEnvThreshold = readfloat(cfg.get("SkyGfx", "vehChromeEnvThreshold", ""), 0.0f);
	// Chrome breakdown (Part B/C) — literal defaults (self-fallback NOT used
	// here; keep in sync with skygfx.h comments and the Car Chrome BRDF row).
	c->vehAutoChrome = readint(cfg.get("SkyGfx", "vehAutoChrome", ""), 1);
	c->chromeF0 = readfloat(cfg.get("SkyGfx", "chromeF0", ""), 0.56f);
	c->chromeGloss = readfloat(cfg.get("SkyGfx", "chromeGloss", ""), 0.90f);
	c->chromeMetallic = readfloat(cfg.get("SkyGfx", "chromeMetallic", ""), 1.0f);
	c->chromeEnvBoost = readfloat(cfg.get("SkyGfx", "chromeEnvBoost", ""), 1.0f);
	c->chromeClearcoat = readfloat(cfg.get("SkyGfx", "chromeClearcoat", ""), 0.6f);
	clampChromeParams(c);
	c->tonemapAutoExposure = readint(cfg.get("SkyGfx", "tonemapAutoExposure", ""), 1);
	c->tonemapAdaptSpeed = readfloat(cfg.get("SkyGfx", "tonemapAdaptSpeed", ""), 0.12f);
	c->tonemapKeyStrength = readfloat(cfg.get("SkyGfx", "tonemapKeyStrength", ""), 1.0f);
	c->tonemapMinExposure = readfloat(cfg.get("SkyGfx", "tonemapMinExposure", ""), 0.5f);
	// HIGH-1: was 2.0 — contradicted the clampTonemapExposureRanges ceiling of
	// 1.0 (every load immediately re-pinned it, so the default was a lie).
	c->tonemapMaxExposure = readfloat(cfg.get("SkyGfx", "tonemapMaxExposure", ""), 1.0f);
	// User pivot curve (TonemapPass c8) — identity defaults 0/0/0.5
	c->tonemapCurveLows = readfloat(cfg.get("SkyGfx", "tonemapCurveLows", ""), 0.0f);
	c->tonemapCurveHighs = readfloat(cfg.get("SkyGfx", "tonemapCurveHighs", ""), 0.0f);
	c->tonemapCurveMid = readfloat(cfg.get("SkyGfx", "tonemapCurveMid", ""), 0.5f);
	// Exposure ranges Min [-0.5..0.5] / Max [-0.5..1.0] + Min<=Max + curve
	// bounds — clamped here + refreshIni() + saveConfigTo() (env-clamp pattern).
	clampTonemapExposureRanges(c);
	c->envMapSize = readint(cfg.get("SkyGfx", "envMapSize", ""), c->envMapSize);
	int i = 1;
	while(i < c->envMapSize) i *= 2;
	c->envMapSize = i;
	c->envMapFarClipMult = readfloat(cfg.get("SkyGfx", "envMapFarClipMult", ""), c->envMapFarClipMult);
	c->envMapUseLODs = readint(cfg.get("SkyGfx", "envMapUseLODs", ""), 0);

	// Normal mapping
	c->normalMapEnable = readint(cfg.get("SkyGfx", "normalMapEnable", ""), 0);
	c->normalMapIntensity = readfloat(cfg.get("SkyGfx", "normalMapIntensity", ""), 1.0f);
	c->normalMapPlayerOnly = readint(cfg.get("SkyGfx", "normalMapPlayerOnly", ""), 1);

	c->doglare = readint(cfg.get("SkyGfx", "sunGlare", ""), -1);
	if(c->doglare < 0){
		iCanHasSunGlare = false;
		c->doglare = 0;
	}

	c->ps2ModulateGrass = readint(cfg.get("SkyGfx", "ps2ModulateGrass", ""), config->ps2ModulateGlobal);
	c->dualPassGrass = readint(cfg.get("SkyGfx", "dualPassGrass", ""), config->dualPassGlobal);
	c->grassAddAmbient = readint(cfg.get("SkyGfx", "grassAddAmbient", ""), 0);
//	ps2grassFiles = readint(cfg.get("SkyGfx", "ps2grassFiles", ""), 0);
	c->fixGrassPlacement = readint(cfg.get("SkyGfx", "grassFixPlacement", ""), 0);
	c->backfaceCull = readint(cfg.get("SkyGfx", "grassBackfaceCull", ""), 1);

	static StrAssoc boolMap[] = {
		{"0",       0},
		{"false",   0},
		{"1",       1},
		{"true",    1},
		{"",       -1},
	};
	c->dualPassDefault = readint(cfg.get("SkyGfx", "dualPassDefault", ""), config->dualPassGlobal);
	c->dualPassPed = readint(cfg.get("SkyGfx", "dualPassPed", ""), config->dualPassGlobal);
	// pedShadows/stencilShadows are -1 (leave game default) / 0 / 1 toggles.
	// boolMap only knows the keyword forms, so a numeric "2" used to fall
	// through to the -1 sentinel (silently ignored). Parse the numeric form
	// too and clamp to [-1..1] (2 -> 1) with a one-time note.
	auto parseShadowToggle = [&](const char *key, const char *tag) -> int {
		std::string raw = cfg.get("SkyGfx", key, "");
		int v = StrAssoc::get(boolMap, raw.c_str());
		if(v < 0 && !raw.empty()){
			int n = readint(raw, -1);
			if(n >= 0){
				if(n > 1 || n < -1){
					static bool s_shadowWarned = false;
					if(!s_shadowWarned){
						s_shadowWarned = true;
						dbglog("[Config] %s=%d outside [-1..1], clamping", tag, n);
					}
					n = (n > 1) ? 1 : -1;
				}
				v = n;
			}
		}
		return v;
	};
	c->pedShadows = parseShadowToggle("pedShadows", "pedShadows");
	c->stencilShadows = parseShadowToggle("stencilShadows", "stencilShadows");
	disableClouds = readint(cfg.get("SkyGfx", "disableClouds", ""), 0);
	// Cloud render-state hygiene (CClouds__Render_hygiene): force texture
	// filter / alpha test / fog to known values for the cloud draw so the
	// inherited states cannot hard-cut or fog the clouds away. Default ON.
	cloudRenderFix = readint(cfg.get("SkyGfx", "cloudRenderFix", ""), 1) != 0;
	disableGamma = readint(cfg.get("SkyGfx", "disableGamma", ""), 0);
	transparentLockon = readint(cfg.get("SkyGfx", "transparentLockon", ""), 0);
	fixShadows = readint(cfg.get("SkyGfx", "fixShadows", ""), 0);
	c->lightningIlluminatesWorld = readint(cfg.get("SkyGfx", "lightningIlluminatesWorld", ""), 0);

	static StrAssoc colorFilterMap[] = {
		{"None",    COLORFILTER_NONE},
		{"PS2",     COLORFILTER_PS2},
		{"PC",      COLORFILTER_PC},
		{"Mobile",  COLORFILTER_MOBILE},
		{"III",     COLORFILTER_III},
		{"VC",      COLORFILTER_VC},
		{"VCS",     COLORFILTER_VCS},
		{"GTAIV",   COLORFILTER_GTAIV},
		{"Modern",  COLORFILTER_MODERN},
		{"",        COLORFILTER_PC},
	};
	static StrAssoc ps2pcMap[] = {
		{"PS2",     0},
		{"PC",      1},
		{"",        1},
	};
	c->colorFilter = StrAssoc::get(colorFilterMap, cfg.get("SkyGfx", "colorFilter", "").c_str());
	// PBR pipeline always uses Modern color filter (Hable filmic tonemap
	// applied in PostFX) — MANDATORY, independent of colorFilterEnable. An
	// INI `colorFilter=None` + `colorFilterEnable=1` under PBR is a conflict:
	// warn once and override. derivePipelineFromPipes() re-asserts the same
	// force on every reload/setConfig.
	if(c->pipeline == PIPELINE_PBR && c->colorFilter != COLORFILTER_MODERN){
		static bool s_pbrCfWarned = false;
		if(!s_pbrCfWarned){
			s_pbrCfWarned = true;
			dbglog("[Config] pipeline=PBR requires colorFilter=Modern(8); overriding INI colorFilter=%d",
				c->colorFilter);
		}
		c->colorFilter = COLORFILTER_MODERN;
	}
	ps2pcMap[2].val = c->colorFilter == COLORFILTER_PS2 ? 0 : 1;
	c->rgb1Mult = readfloat(cfg.get("SkyGfx", "rgb1Mult", ""), 1.0f);
	c->rgb2Mult = readfloat(cfg.get("SkyGfx", "rgb2Mult", ""), 1.0f);
	c->infraredVision = StrAssoc::get(ps2pcMap, cfg.get("SkyGfx", "infraredVision", "").c_str());
	c->nightVision = StrAssoc::get(ps2pcMap, cfg.get("SkyGfx", "nightVision", "").c_str());
	c->grainFilter = StrAssoc::get(ps2pcMap, cfg.get("SkyGfx", "grainFilter", "").c_str());
	c->usePCTimecyc = readint(cfg.get("SkyGfx", "usePCTimecyc", ""), 0);

	tmpint = readint(cfg.get("SkyGfx", "blurLeft", ""), 4000);
	c->offLeft = tmpint == 4000 ? defaultColourLeftUOffset : tmpint;
	tmpint = readint(cfg.get("SkyGfx", "blurTop", ""), 4000);
	c->offTop = tmpint == 4000 ? defaultColourTopVOffset : tmpint;
	tmpint = readint(cfg.get("SkyGfx", "blurRight", ""), 4000);
	c->offRight = tmpint == 4000 ? defaultColourRightUOffset : tmpint;
	tmpint = readint(cfg.get("SkyGfx", "blurBottom", ""), 4000);
	c->offBottom = tmpint == 4000 ? defaultColourBottomVOffset : tmpint;

	tmpint = readint(cfg.get("SkyGfx", "doRadiosity", ""), 4000);
	c->doRadiosity = tmpint == 4000 ? original_bRadiosity : tmpint;	// saved value from stream.ini

	static StrAssoc ps2shdrMap[] = {
		{"PS2",     0},
		{"Shader",  1},
		{"",        1},
	};
	c->radiosity = StrAssoc::get(ps2shdrMap, cfg.get("SkyGfx", "radiosity", "").c_str());

	c->vcsTrails = readint(cfg.get("SkyGfx", "vcsTrails", ""), 0);
	c->trailsLimit = readint(cfg.get("SkyGfx", "trailsLimit", ""), 80);
	c->trailsIntensity = readint(cfg.get("SkyGfx", "trailsIntensity", ""), 38);
	c->trailsResolution = readint(cfg.get("SkyGfx", "trailsResolution", ""), 1);

	c->radiosityFilterPasses = readint(cfg.get("SkyGfx", "radiosityFilterPasses", ""), 2);
	c->radiosityRenderPasses = readint(cfg.get("SkyGfx", "radiosityRenderPasses", ""), 1);
	c->radiosityIntensity = readint(cfg.get("SkyGfx", "radiosityIntensity", ""), 0x23);

	c->neoWaterDrops = readint(cfg.get("SkyGfx", "neoWaterDrops", ""), -1);
	if(c->neoWaterDrops < 0){
		iCanHasNeoDrops = false;
		c->neoWaterDrops = 0;
	}
	c->neoBloodDrops = readint(cfg.get("SkyGfx", "neoBloodDrops", ""), 0);
	fixPcCarLight = readint(cfg.get("SkyGfx", "fixPcCarLight", ""), 0);
	explicitBuildingPipe_tmp = readint(cfg.get("SkyGfx", "explicitBuildingPipe", ""), -1);
	// tagsBuildingPipe follows the same pipeline as main building pipe
	c->tagsBuildingPipe = c->buildingPipe;

	c->zwriteThreshold = readint(cfg.get("SkyGfx", "zwriteThreshold", ""), 128);
	if(c->zwriteThreshold < 0) c->zwriteThreshold = 0;
	if(c->zwriteThreshold > 255) c->zwriteThreshold = 255;

	c->zwriteThresholdGrass = readint(cfg.get("SkyGfx", "zwriteThresholdGrass", ""), 128);
	if(c->zwriteThresholdGrass < 0) c->zwriteThresholdGrass = 0;
	if(c->zwriteThresholdGrass > 255) c->zwriteThresholdGrass = 255;

	c->zwriteThresholdPed = readint(cfg.get("SkyGfx", "zwriteThresholdPed", ""), 128);
	if(c->zwriteThresholdPed < 0) c->zwriteThresholdPed = 0;
	if(c->zwriteThresholdPed > 255) c->zwriteThresholdPed = 255;

	c->coronaZtest = readint(cfg.get("SkyGfx", "coronaZtest", ""), -1);
	// resetValues() only handles -1/0/1; any other value (e.g. 2) silently
	// no-ops the corona z-test patch. Clamp to [-1..1] with a one-time note.
	if(c->coronaZtest < -1 || c->coronaZtest > 1){
		static bool s_coronaZtestWarned = false;
		if(!s_coronaZtestWarned){
			s_coronaZtestWarned = true;
			dbglog("[Config] coronaZtest=%d outside [-1..1], clamping", c->coronaZtest);
		}
		c->coronaZtest = (c->coronaZtest < -1) ? -1 : 1;
	}

	c->bYCbCrFilter = readint(cfg.get("SkyGfx", "YCbCrCorrection", ""), 0);
	c->lumaScale = readfloat(cfg.get("SkyGfx", "lumaScale", ""), 219.0f/255.0f);
	c->lumaOffset = readfloat(cfg.get("SkyGfx", "lumaOffset", ""), 16.0f/255.0f);
	c->cbScale = readfloat(cfg.get("SkyGfx", "CbScale", ""), 1.23f);
	c->cbOffset = readfloat(cfg.get("SkyGfx", "CbOffset", ""), 0.0f);
	c->crScale = readfloat(cfg.get("SkyGfx", "CrScale", ""), 1.23f);
	c->crOffset     = readfloat(cfg.get("SkyGfx", "CrOffset", ""), 0.0f);

	// SSAO
	c->ssaoEnable = readint(cfg.get("SkyGfx", "ssaoEnable", ""), c->ssaoEnable);
	c->ssaoRadius = readfloat(cfg.get("SkyGfx", "ssaoRadius", ""), c->ssaoRadius);
	c->ssaoPower = readfloat(cfg.get("SkyGfx", "ssaoPower", ""), c->ssaoPower);
	c->ssaoKernelSize = readfloat(cfg.get("SkyGfx", "ssaoKernelSize", ""), c->ssaoKernelSize);
	c->ssaoSampleCount = readint(cfg.get("SkyGfx", "ssaoSampleCount", ""), c->ssaoSampleCount);
	// ssaoPower is a gamma on occlusion: 9.237 amplified mild AO into the
	// hard object-shaped silhouette (sane range [0.5..4.0], presets 1.5/2.0).
	clampSsaoPower(c);

	c->smaaEnable = readint(cfg.get("SkyGfx", "smaaEnable", ""), c->smaaEnable);

	// Motion Blur
	c->motionBlurEnable = readint(cfg.get("SkyGfx", "motionBlurEnable", ""), c->motionBlurEnable);
	c->motionBlurStrength = readfloat(cfg.get("SkyGfx", "motionBlurStrength", ""), c->motionBlurStrength);
	c->motionBlurRadial = readfloat(cfg.get("SkyGfx", "motionBlurRadial", ""), c->motionBlurRadial);
	c->motionBlurSpeedFactor = readfloat(cfg.get("SkyGfx", "motionBlurSpeedFactor", ""), c->motionBlurSpeedFactor);
	c->motionBlurCameraAware = readint(cfg.get("SkyGfx", "motionBlurCameraAware", ""), c->motionBlurCameraAware);

	// Velocity Buffer
	c->velocityBufferEnable = readint(cfg.get("SkyGfx", "velocityBufferEnable", ""), 1);

	// Depth Hook (INTZ direct-binding, DXVK compatibility)
	c->depthHookEnable = readint(cfg.get("SkyGfx", "depthHookEnable", ""), 1);

	// Faux Normal Buffer (stereo disparity)
	c->normalBufferEnable = readint(cfg.get("SkyGfx", "normalBufferEnable", ""), 1);
	c->normalBufferOffset = readfloat(cfg.get("SkyGfx", "normalBufferOffset", ""), 0.5f);
	c->normalBufferScale = readfloat(cfg.get("SkyGfx", "normalBufferScale", ""), 1.0f);

	// 4-Pipe Chain
	c->pipeChainEnable = readint(cfg.get("SkyGfx", "pipeChainEnable", ""), 0);
	c->pipeChainIntensity = readfloat(cfg.get("SkyGfx", "pipeChainIntensity", ""), 0.3f); // was 0.5 — subtle/correct default

	// Debug toggles — set to 0 to bypass effects for black screen isolation
	// colorFilterEnable also records USER INTENT (latch above): every legit
	// INI path (readIni / reloadAllInis / loadConfigFile) flows through here.
	cfEnableSetUserIntent(c, (RwBool)readint(cfg.get("SkyGfx", "colorFilterEnable", ""), 1));
	c->radiosityEnable = readint(cfg.get("SkyGfx", "radiosityEnable", ""), 1);
	c->grainEnable = readint(cfg.get("SkyGfx", "grainEnable", ""), 1);
	c->postfxDumpDebug = readint(cfg.get("SkyGfx", "postfxDumpDebug", ""), 0);

	// GTA IV Mode — default follows the (possibly override-derived) pipe
	// family: pipeline=GTAIV with this key absent used to default to 0 and
	// left DrawIVGrade (:2437) + the building IV-shader swap inert. An
	// explicit ivMode key still wins (grade-off with GTAIV pipes is legal).
	c->ivMode = readint(cfg.get("SkyGfx", "ivMode", ""),
		(c->pipeline == PIPELINE_GTAIV) ? 1 : 0);
	c->ivDesaturation = readfloat(cfg.get("SkyGfx", "ivDesaturation", ""), c->ivDesaturation);
	c->ivGamma = readfloat(cfg.get("SkyGfx", "ivGamma", ""), c->ivGamma);
	c->ivSaturation = readfloat(cfg.get("SkyGfx", "ivSaturation", ""), c->ivSaturation);
	c->ivCurves = readfloat(cfg.get("SkyGfx", "ivCurves", ""), c->ivCurves);
	c->ivVignetteIntensity = readfloat(cfg.get("SkyGfx", "ivVignetteIntensity", ""), c->ivVignetteIntensity);
	c->ivVignetteRadius = readfloat(cfg.get("SkyGfx", "ivVignetteRadius", ""), c->ivVignetteRadius);
	c->ivVignetteContrast = readfloat(cfg.get("SkyGfx", "ivVignetteContrast", ""), c->ivVignetteContrast);
	c->ivBloomIntensity = readfloat(cfg.get("SkyGfx", "ivBloomIntensity", ""), c->ivBloomIntensity);
	c->ivExposure = readfloat(cfg.get("SkyGfx", "ivExposure", ""), c->ivExposure);

	privateHooks = readint(cfg.get("SkyGfx", "privateHooks", ""), 0);
	if (readint(cfg.get("SkyGfx", "forceWindShader", ""), 0) == 1) forceWindShader = true;

	// Death ragdoll
	c->ragdollEnable = readint(cfg.get("SkyGfx", "ragdollEnable", ""), 1);
	g_ragdollDeathEnable = c->ragdollEnable != 0;

	// PBR ambient floor / tonemap black lift (opt-in; 0.0 = off)
	c->pbrAmbientFloor = readfloat(cfg.get("SkyGfx", "pbrAmbientFloor", ""), 0.0f);
	c->pbrIblAmbientWeight = readfloat(cfg.get("SkyGfx", "pbrIblAmbientWeight", ""), 0.5f);
	c->tonemapBlackLift = readfloat(cfg.get("SkyGfx", "tonemapBlackLift", ""), 0.0f);
	// Non-prelit prelight fallback (VS c28.x, main_vehiclePBR) — default ON
	// at 0.15 because SA vehicle geometry is never prelit and the legacy 0
	// is exactly the black-silhouette bug; 0 restores legacy behaviour.
	c->vehPrelightFallback = readfloat(cfg.get("SkyGfx", "vehPrelightFallback", ""), 0.15f);
	if(c->vehPrelightFallback < 0.0f) c->vehPrelightFallback = 0.0f;
	if(c->vehPrelightFallback > 1.0f) c->vehPrelightFallback = 1.0f;

	// Modern/Env reflection-model terms (VehiclePBR_Modern main()); see the
	// field-by-field rationale in skygfx.h. 0 = legacy Modern behaviour for
	// the three switches, so the old look is always one key away.
	c->vehEnvFallback      = readint(cfg.get("SkyGfx", "vehEnvFallback", ""), 1);
	c->vehEnvStrengthMode  = readint(cfg.get("SkyGfx", "vehEnvStrengthMode", ""), 1);
	c->vehEnvMask          = readint(cfg.get("SkyGfx", "vehEnvMask", ""), 1);
	c->vehWetEnv           = readint(cfg.get("SkyGfx", "vehWetEnv", ""), 1);
	c->vehEnvGlint = readfloat(cfg.get("SkyGfx", "vehEnvGlint", ""), 0.0f);
	if(c->vehEnvGlint < 0.0f) c->vehEnvGlint = 0.0f;
	if(c->vehEnvGlint > 1.0f) c->vehEnvGlint = 1.0f;
	
	// CLASS 2: Missing INI reads (write-only keys, never read back)
	// Height Fog
	c->heightFogEnable = readint(cfg.get("SkyGfx", "heightFogEnable", ""), c->heightFogEnable);
	c->heightFogDensity = readfloat(cfg.get("SkyGfx", "heightFogDensity", ""), c->heightFogDensity);
	c->heightFogHeightFalloff = readfloat(cfg.get("SkyGfx", "heightFogHeightFalloff", ""), c->heightFogHeightFalloff);
	c->heightFogStartHeight = readfloat(cfg.get("SkyGfx", "heightFogStartHeight", ""), c->heightFogStartHeight);
	c->heightFogR = readfloat(cfg.get("SkyGfx", "heightFogR", ""), c->heightFogR);
	c->heightFogG = readfloat(cfg.get("SkyGfx", "heightFogG", ""), c->heightFogG);
	c->heightFogB = readfloat(cfg.get("SkyGfx", "heightFogB", ""), c->heightFogB);
	c->heightFogTimecycleScale = readfloat(cfg.get("SkyGfx", "heightFogTimecycleScale", ""), c->heightFogTimecycleScale);
	
	// God Rays
	c->godRaysEnable = readint(cfg.get("SkyGfx", "godRaysEnable", ""), c->godRaysEnable);
	c->godRaysExposure = readfloat(cfg.get("SkyGfx", "godRaysExposure", ""), c->godRaysExposure);
	c->godRaysDecay = readfloat(cfg.get("SkyGfx", "godRaysDecay", ""), c->godRaysDecay);
	c->godRaysDensity = readfloat(cfg.get("SkyGfx", "godRaysDensity", ""), c->godRaysDensity);
	c->godRaysWeight = readfloat(cfg.get("SkyGfx", "godRaysWeight", ""), c->godRaysWeight);
	c->godRaysNumSamples = readint(cfg.get("SkyGfx", "godRaysNumSamples", ""), c->godRaysNumSamples);
	
	// SSAO overhaul — fallback defaults are the DOCUMENTED skygfx.h:343-347
	// values, NOT the Config field. `Config configs[10]` zero-inits (main.cpp:64),
	// so self-fallback (`c->field`) inherited 0 whenever the key was missing:
	// temporal OFF, blurPasses 0 (blur dead), blurRadius 0, depthThreshold 0 —
	// the exact menu state that produced the unblurred hard AO silhouette.
	c->ssaoTemporalEnable = readint(cfg.get("SkyGfx", "ssaoTemporalEnable", ""), 1);
	c->ssaoTemporalBlend = readfloat(cfg.get("SkyGfx", "ssaoTemporalBlend", ""), 0.1f);
	c->ssaoBlurPasses = readint(cfg.get("SkyGfx", "ssaoBlurPasses", ""), 2);
	c->ssaoBlurRadius = readfloat(cfg.get("SkyGfx", "ssaoBlurRadius", ""), 3.0f);
	c->ssaoDepthThreshold = readfloat(cfg.get("SkyGfx", "ssaoDepthThreshold", ""), 0.01f);
	
	// SSS post-process
	c->sssPostProcessStrength = readfloat(cfg.get("SkyGfx", "sssPostProcessStrength", ""), c->sssPostProcessStrength);
	c->sssPostProcessRadius = readfloat(cfg.get("SkyGfx", "sssPostProcessRadius", ""), c->sssPostProcessRadius);
	c->sssPostProcessThreshold = readfloat(cfg.get("SkyGfx", "sssPostProcessThreshold", ""), c->sssPostProcessThreshold);
	// Ambient-match boost: documented default 1.0 (absent key = keep the
	// current look), literal because Config configs[10] zero-inits — c->field
	// would silently disable the SSS ambient match.
	c->sssAmbientBoost = readfloat(cfg.get("SkyGfx", "sssAmbientBoost", ""), 1.0f);
	
	// Skin enhancement
	c->skinWrapFactor = readfloat(cfg.get("SkyGfx", "skinWrapFactor", ""), c->skinWrapFactor);
	c->skinSpecularPower = readfloat(cfg.get("SkyGfx", "skinSpecularPower", ""), c->skinSpecularPower);
	c->skinSpecularStrength = readfloat(cfg.get("SkyGfx", "skinSpecularStrength", ""), c->skinSpecularStrength);
	c->skinSSSStrength = readfloat(cfg.get("SkyGfx", "skinSSSStrength", ""), c->skinSSSStrength);
	
	// Hair enhancement
	c->hairAnisotropicPower = readfloat(cfg.get("SkyGfx", "hairAnisotropicPower", ""), c->hairAnisotropicPower);
	c->hairAnisotropicStrength = readfloat(cfg.get("SkyGfx", "hairAnisotropicStrength", ""), c->hairAnisotropicStrength);
	c->hairSSSStrength = readfloat(cfg.get("SkyGfx", "hairSSSStrength", ""), c->hairSSSStrength);
	
	// Vegetation enhancement
	c->vegetationSSSStrength = readfloat(cfg.get("SkyGfx", "vegetationSSSStrength", ""), c->vegetationSSSStrength);
	c->vegetationAmbientBoost = readfloat(cfg.get("SkyGfx", "vegetationAmbientBoost", ""), c->vegetationAmbientBoost);

	// CLASS 3: round-trip fixes — keys saveConfig writes / the menu mutates
	// but readIni never read back (the preset block used to force them)
	c->sssPostProcessEnable = readint(cfg.get("SkyGfx", "sssPostProcessEnable", ""), c->sssPostProcessEnable);
	c->skinEnhanceEnable = readint(cfg.get("SkyGfx", "skinEnhanceEnable", ""), c->skinEnhanceEnable);
	c->hairEnhanceEnable = readint(cfg.get("SkyGfx", "hairEnhanceEnable", ""), c->hairEnhanceEnable);
	c->vegetationEnhanceEnable = readint(cfg.get("SkyGfx", "vegetationEnhanceEnable", ""), c->vegetationEnhanceEnable);

	// Screen-space SSS (menu-exposed; previously neither saved nor read)
	// sssEnable is now the master switch consulted by chars_drawSSSBlur —
	// absent key defaults to ON (1) so a fresh install keeps the previous
	// behaviour where sssPostProcessEnable alone gated the pass.
	c->sssEnable = readint(cfg.get("SkyGfx", "sssEnable", ""), 1);
	c->sssIntensity = readfloat(cfg.get("SkyGfx", "sssIntensity", ""), c->sssIntensity);
	c->sssVegIntensity = readfloat(cfg.get("SkyGfx", "sssVegIntensity", ""), c->sssVegIntensity);
	c->sssSkinIntensity = readfloat(cfg.get("SkyGfx", "sssSkinIntensity", ""), c->sssSkinIntensity);
	c->sssClothIntensity = readfloat(cfg.get("SkyGfx", "sssClothIntensity", ""), c->sssClothIntensity);

	// Style combo display only — the values themselves load via their own keys above
	c->preset = readint(cfg.get("SkyGfx", "gamePreset", ""), PRESET_CUSTOM);
	
	// Forward+ tiled lighting
	c->forwardPlusEnable = readint(cfg.get("SkyGfx", "forwardPlusEnable", ""), 1);
	
	// Sun corona
	c->sunCoronaIntensity = readfloat(cfg.get("SkyGfx", "sunCoronaIntensity", ""), c->sunCoronaIntensity);
	c->sunCoreIntensity = readfloat(cfg.get("SkyGfx", "sunCoreIntensity", ""), c->sunCoreIntensity);
	c->sunStreakIntensity = readfloat(cfg.get("SkyGfx", "sunStreakIntensity", ""), c->sunStreakIntensity);
	c->sunStreakSize = readfloat(cfg.get("SkyGfx", "sunStreakSize", ""), c->sunStreakSize);
	
	// Normal buffer
	c->normalBufferOffset = readfloat(cfg.get("SkyGfx", "normalBufferOffset", ""), c->normalBufferOffset);
	c->normalBufferScale = readfloat(cfg.get("SkyGfx", "normalBufferScale", ""), c->normalBufferScale);
	
	// Pipe chain
	c->pipeChainIntensity = readfloat(cfg.get("SkyGfx", "pipeChainIntensity", ""), c->pipeChainIntensity);
	
	// Water pipe (procedural PBR water — Water_VS / Water_Parallax).
	// Never wired before: configs[] zero-init left waterParallaxEnable=0, so the
	// gate in waterPipe_setRenderState() always failed while the hook still
	// replaced the game's water draw (stale PBR shaders = garbage water).
	c->waterParallaxEnable = readint(cfg.get("SkyGfx", "waterParallaxEnable", ""), 1);
	c->waterParallaxScale = readfloat(cfg.get("SkyGfx", "waterParallaxScale", ""), 1.0f);
	c->waterNormalStrength = readfloat(cfg.get("SkyGfx", "waterNormalStrength", ""), 1.0f);
	c->waterFresnelPower = readfloat(cfg.get("SkyGfx", "waterFresnelPower", ""), 5.0f);
	c->waterSpecularPower = readfloat(cfg.get("SkyGfx", "waterSpecularPower", ""), 512.0f);
	c->waterSpecularIntensity = readfloat(cfg.get("SkyGfx", "waterSpecularIntensity", ""), 1.0f);
	c->waterUnderwaterFog = readfloat(cfg.get("SkyGfx", "waterUnderwaterFog", ""), 1.0f);
	c->waterFoamThreshold = readfloat(cfg.get("SkyGfx", "waterFoamThreshold", ""), 0.6f);
	c->waterFoamSoftness = readfloat(cfg.get("SkyGfx", "waterFoamSoftness", ""), 0.2f);
	c->waterShallowR = readfloat(cfg.get("SkyGfx", "waterShallowR", ""), 0.13f);
	c->waterShallowG = readfloat(cfg.get("SkyGfx", "waterShallowG", ""), 0.45f);
	c->waterShallowB = readfloat(cfg.get("SkyGfx", "waterShallowB", ""), 0.55f);
	c->waterDeepR = readfloat(cfg.get("SkyGfx", "waterDeepR", ""), 0.01f);
	c->waterDeepG = readfloat(cfg.get("SkyGfx", "waterDeepG", ""), 0.03f);
	c->waterDeepB = readfloat(cfg.get("SkyGfx", "waterDeepB", ""), 0.08f);
	c->waterReflectionFarClip = readfloat(cfg.get("SkyGfx", "waterReflectionFarClip", ""), 1000.0f);
	// Water rewrite knobs (Xbox/IV-style water)
	c->waterTileScale = readfloat(cfg.get("SkyGfx", "waterTileScale", ""), 1.0f);
	c->waterShoreFade = readfloat(cfg.get("SkyGfx", "waterShoreFade", ""), 2.0f);
	c->waterTranslucency = readfloat(cfg.get("SkyGfx", "waterTranslucency", ""), 1.0f);
	c->waterReflectionStrength = readfloat(cfg.get("SkyGfx", "waterReflectionStrength", ""), 0.55f);
	c->waterUseTimecycle = readint(cfg.get("SkyGfx", "waterUseTimecycle", ""), 1);
	// Water style (Xbox core / GTA IV / GTA V) + quality tiers 0-3
	// (Low/Med/High/Ultra). Style switches the shader entry + constant
	// preset; quality gates cost inside every style. Both accept the numeric
	// spellings too ("0"/"1"/"2" for style).
	static StrAssoc waterStyleMap[] = {
		{"Xbox",  0}, {"0", 0},
		{"IV",    1}, {"GTAIV", 1}, {"1", 1},
		{"V",     2}, {"GTAV",  2}, {"2", 2},
		{"",      0},
	};
	c->waterStyle = StrAssoc::get(waterStyleMap, cfg.get("SkyGfx", "waterStyle", "").c_str());
	c->waterQuality = readint(cfg.get("SkyGfx", "waterQuality", ""), 3);
	if(c->waterQuality < 0) c->waterQuality = 0;
	if(c->waterQuality > 3) c->waterQuality = 3;
	// Fresnel-split water composite (defined in waterPipe.cpp, carried to the
	// shader in c11.w): 1 = reflection AND refraction live at every view
	// direction (default), 0 = the legacy lerp(scene, surf, bodyOpacity)
	// composite as a per-user rollback.
	g_waterFresnelSplit = readint(cfg.get("SkyGfx", "waterFresnelSplit", ""), 1);
	if(g_waterFresnelSplit < 0) g_waterFresnelSplit = 0;
	if(g_waterFresnelSplit > 1) g_waterFresnelSplit = 1;
	
	// Debug toggles (already read above, but ensure they use c->field default)


	if(!iniExisted){
		// ===== Brand new INI - write all defaults =====
		// ===== Unified Pipeline =====
		cfg.set("SkyGfx", "; =============================================================", "");
		cfg.set("SkyGfx", "; SkyGFX Plus - Ultra Max Deluxe Configuration", "");
		cfg.set("SkyGfx", "; Unified PBR pipeline for all assets", "");
		cfg.set("SkyGfx", "; =============================================================", "");
		cfg.set("SkyGfx", "", "");
		cfg.set("SkyGfx", "; ===== Pipeline (locks building/vehicle pipes) =====", "");
		cfg.set("SkyGfx", "; Options: PBR, PS2, Xbox, Mobile, GTAIV", "");
		cfg.set("SkyGfx", "pipeline", "PBR");
		cfg.set("SkyGfx", "qualityPreset", "3");  // ULTRA
		cfg.set("SkyGfx", "", "");

		// ===== Legacy/Unused Settings (commented out) =====
		cfg.set("SkyGfx", "; ===== Legacy Settings (ignored when pipeline is set) =====", "");
		cfg.set("SkyGfx", "; buildingPipe", "PBR");
		cfg.set("SkyGfx", "; vehiclePipe", "Modern");
		cfg.set("SkyGfx", "; colorFilter", "VCS");
		cfg.set("SkyGfx", "; radiosity", "Shader");
		cfg.set("SkyGfx", "; vcsTrails", "0");
		cfg.set("SkyGfx", "; doRadiosity", "1");
		cfg.set("SkyGfx", "; ps2ModulateBuilding", "0");
		cfg.set("SkyGfx", "; dualPassBuilding", "1");
		cfg.set("SkyGfx", "; ps2ModulateVehicle", "0");
		cfg.set("SkyGfx", "; dualPassVehicle", "1");
		cfg.set("SkyGfx", "; ps2ModulateGrass", "0");
		cfg.set("SkyGfx", "; grassAddAmbient", "1");
		cfg.set("SkyGfx", "; grassBackfaceCull", "1");
		cfg.set("SkyGfx", "; sunGlare", "1");
		cfg.set("SkyGfx", "; neoWaterDrops", "1");
		cfg.set("SkyGfx", "; detailMaps", "1");
		cfg.set("SkyGfx", "; stochasticTexturing", "1");
		cfg.set("SkyGfx", "; envMapSize", "512");
		cfg.set("SkyGfx", "; envMapUseLODs", "1");
		cfg.set("SkyGfx", "; envMapFarClipMult", "2.0");
		cfg.set("SkyGfx", "; neoShininessMult", "1.0");
		cfg.set("SkyGfx", "; neoSpecularityMult", "1.0");
		cfg.set("SkyGfx", "; privateHooks", "0");
		cfg.set("SkyGfx", "; forceWindShader", "0");
		cfg.set("SkyGfx", "", "");

		// ===== Active Settings (Ultra Max Deluxe) =====
		cfg.set("SkyGfx", "; ===== Active Settings (Ultra Max Deluxe) =====", "");
		cfg.set("SkyGfx", "ssaoEnable", "0");
		cfg.set("SkyGfx", "ssaoRadius", "1.0");
		cfg.set("SkyGfx", "ssaoPower", "2.0");
		cfg.set("SkyGfx", "ssaoKernelSize", "16");
		cfg.set("SkyGfx", "ssaoSampleCount", "16");
		cfg.set("SkyGfx", "smaaEnable", "0");
		cfg.set("SkyGfx", "motionBlurEnable", "0");  // fresh installs default OFF (was 1: unwanted motion blur)
		cfg.set("SkyGfx", "motionBlurStrength", "0.4");
		cfg.set("SkyGfx", "sssPostProcessEnable", "1");
		cfg.set("SkyGfx", "sssPostProcessStrength", "0.2");
		cfg.set("SkyGfx", "sssEnable", "1");
		cfg.set("SkyGfx", "sssAmbientBoost", "1");
		cfg.set("SkyGfx", "skinEnhanceEnable", "1");
		cfg.set("SkyGfx", "hairEnhanceEnable", "1");
		cfg.set("SkyGfx", "vegetationEnhanceEnable", "1");
		cfg.set("SkyGfx", "normalMapEnable", "0");
		cfg.set("SkyGfx", "normalMapIntensity", "1.0");
		cfg.set("SkyGfx", "normalMapPlayerOnly", "1");
		cfg.set("SkyGfx", "ivMode", "0");
		cfg.set("SkyGfx", "ivDesaturation", "0.15");
		cfg.set("SkyGfx", "ivGamma", "1.0");
		cfg.set("SkyGfx", "ivSaturation", "0.3");
		cfg.set("SkyGfx", "ivCurves", "1.0");
		cfg.set("SkyGfx", "ivVignetteIntensity", "0.15");
		cfg.set("SkyGfx", "ivVignetteRadius", "0.70");
		cfg.set("SkyGfx", "ivVignetteContrast", "1.5");
		cfg.set("SkyGfx", "ivBloomIntensity", "0.05");
		cfg.set("SkyGfx", "ivExposure", "2.5");

		// ===== Water pipe (procedural PBR water) =====
		cfg.set("SkyGfx", "waterParallaxEnable", "1");
		cfg.set("SkyGfx", "waterParallaxScale", "1.0");
		cfg.set("SkyGfx", "waterNormalStrength", "1.0");
		cfg.set("SkyGfx", "waterFresnelPower", "5.0");
		cfg.set("SkyGfx", "waterSpecularPower", "512.0");
		cfg.set("SkyGfx", "waterSpecularIntensity", "1.0");
		cfg.set("SkyGfx", "waterUnderwaterFog", "1.0");
		cfg.set("SkyGfx", "waterFoamThreshold", "0.6");
		cfg.set("SkyGfx", "waterFoamSoftness", "0.2");
		cfg.set("SkyGfx", "waterShallowR", "0.13");
		cfg.set("SkyGfx", "waterShallowG", "0.45");
		cfg.set("SkyGfx", "waterShallowB", "0.55");
		cfg.set("SkyGfx", "waterDeepR", "0.01");
		cfg.set("SkyGfx", "waterDeepG", "0.03");
		cfg.set("SkyGfx", "waterDeepB", "0.08");
		cfg.set("SkyGfx", "waterReflectionFarClip", "1000.0");
		// Water rewrite knobs (Xbox/IV-style water)
		cfg.set("SkyGfx", "waterTileScale", "1.0");
		cfg.set("SkyGfx", "waterShoreFade", "2.0");
		cfg.set("SkyGfx", "waterTranslucency", "1.0");
		cfg.set("SkyGfx", "waterReflectionStrength", "0.55");
		cfg.set("SkyGfx", "waterUseTimecycle", "1");
		cfg.set("SkyGfx", "; waterStyle: Xbox (default), IV, GTAIV, V, GTAV", "");
		cfg.set("SkyGfx", "waterStyle", "Xbox");
		cfg.set("SkyGfx", "; waterQuality: 0=Low, 1=Med, 2=High, 3=Ultra", "");
		cfg.set("SkyGfx", "waterQuality", "3");
		cfg.set("SkyGfx", "; waterFresnelSplit: 1=fresnel split (default), 0=legacy composite", "");
		cfg.set("SkyGfx", "waterFresnelSplit", "1");
		cfg.set("SkyGfx", "; cloudRenderFix: 1=cloud render-state hygiene (default), 0=vanilla inherited states", "");
		cfg.set("SkyGfx", "cloudRenderFix", "1");

		// ===== Utility Noise Texture (global) =====
		cfg.set("General", "NoiseQuality", "0");	// 0=32, 1=128, 2=512, >=3=1024
	} else {
		// ===== Existing INI - only add missing keys (preserve user settings) =====
		dbglog("skygfx: INI exists, adding any missing keys");
		
		// Helper lambda to add key only if not present
		#define ADD_IF_MISSING(section, key, default) \
			if(cfg.get(section, key, "").empty()) \
				cfg.set(section, key, default)
		
		// ===== Unified Pipeline =====
		ADD_IF_MISSING("SkyGfx", "pipeline", "PBR");
		ADD_IF_MISSING("SkyGfx", "qualityPreset", "3");  // ULTRA
		
		// ===== Active Settings (Ultra Max Deluxe) =====
		ADD_IF_MISSING("SkyGfx", "ssaoEnable", "0");
		ADD_IF_MISSING("SkyGfx", "ssaoRadius", "1.0");
		ADD_IF_MISSING("SkyGfx", "ssaoPower", "2.0");
		ADD_IF_MISSING("SkyGfx", "ssaoKernelSize", "16");
		ADD_IF_MISSING("SkyGfx", "ssaoSampleCount", "16");
		ADD_IF_MISSING("SkyGfx", "smaaEnable", "0");
		ADD_IF_MISSING("SkyGfx", "motionBlurEnable", "0");  // OFF by default; opt-in via preset/INI
		ADD_IF_MISSING("SkyGfx", "motionBlurStrength", "0.4");
		ADD_IF_MISSING("SkyGfx", "sssPostProcessEnable", "1");
		ADD_IF_MISSING("SkyGfx", "sssPostProcessStrength", "0.2");
		// Master switch for the SSS family (chars_drawSSSBlur gate). Default ON
		// so a fresh install keeps the pre-gate behaviour (blur ran off
		// sssPostProcessEnable alone).
		ADD_IF_MISSING("SkyGfx", "sssEnable", "1");
		ADD_IF_MISSING("SkyGfx", "sssAmbientBoost", "1");
		ADD_IF_MISSING("SkyGfx", "skinEnhanceEnable", "1");
		ADD_IF_MISSING("SkyGfx", "hairEnhanceEnable", "1");
		ADD_IF_MISSING("SkyGfx", "vegetationEnhanceEnable", "1");
		ADD_IF_MISSING("SkyGfx", "normalMapEnable", "0");
		// Family-aware migration default: must match what the :1896 read
		// just consumed for an absent key (GTAIV pipes -> "1"), otherwise the
		// next reload would flip ivMode back to the written "0".
		ADD_IF_MISSING("SkyGfx", "ivMode", (c->pipeline == PIPELINE_GTAIV) ? "1" : "0");
		ADD_IF_MISSING("SkyGfx", "ivDesaturation", "0.15");
		ADD_IF_MISSING("SkyGfx", "ivGamma", "1.0");
		ADD_IF_MISSING("SkyGfx", "ivSaturation", "0.3");
		ADD_IF_MISSING("SkyGfx", "ivCurves", "1.0");
		ADD_IF_MISSING("SkyGfx", "ivVignetteIntensity", "0.15");
		ADD_IF_MISSING("SkyGfx", "ivVignetteRadius", "0.70");
		ADD_IF_MISSING("SkyGfx", "ivVignetteContrast", "1.5");
		ADD_IF_MISSING("SkyGfx", "ivBloomIntensity", "0.05");
		ADD_IF_MISSING("SkyGfx", "ivExposure", "2.5");
		ADD_IF_MISSING("SkyGfx", "ragdollEnable", "1");
		ADD_IF_MISSING("SkyGfx", "depthHookEnable", "1");
		ADD_IF_MISSING("SkyGfx", "forwardPlusEnable", "1");
		ADD_IF_MISSING("SkyGfx", "velocityBufferEnable", "1");
		ADD_IF_MISSING("SkyGfx", "normalBufferEnable", "1");
		ADD_IF_MISSING("SkyGfx", "pipeChainEnable", "0");
		ADD_IF_MISSING("SkyGfx", "postfxDumpDebug", "0");
		ADD_IF_MISSING("SkyGfx", "pbrAmbientFloor", "0.0");
		ADD_IF_MISSING("SkyGfx", "pbrIblAmbientWeight", "0.5");
		ADD_IF_MISSING("SkyGfx", "tonemapBlackLift", "0.0");
		ADD_IF_MISSING("SkyGfx", "vehPrelightFallback", "0.15");
		ADD_IF_MISSING("SkyGfx", "vehEnvFallback", "1");
		ADD_IF_MISSING("SkyGfx", "vehEnvStrengthMode", "1");
		ADD_IF_MISSING("SkyGfx", "vehEnvMask", "1");
		ADD_IF_MISSING("SkyGfx", "vehWetEnv", "1");
		ADD_IF_MISSING("SkyGfx", "vehEnvGlint", "0.0");
		ADD_IF_MISSING("SkyGfx", "bldEnvReflect", "1");
		ADD_IF_MISSING("SkyGfx", "bldSkyReflect", "0.15");

		// ===== Water pipe (procedural PBR water) =====
		ADD_IF_MISSING("SkyGfx", "waterParallaxEnable", "1");
		ADD_IF_MISSING("SkyGfx", "waterParallaxScale", "1.0");
		ADD_IF_MISSING("SkyGfx", "waterNormalStrength", "1.0");
		ADD_IF_MISSING("SkyGfx", "waterFresnelPower", "5.0");
		ADD_IF_MISSING("SkyGfx", "waterSpecularPower", "512.0");
		ADD_IF_MISSING("SkyGfx", "waterSpecularIntensity", "1.0");
		ADD_IF_MISSING("SkyGfx", "waterUnderwaterFog", "1.0");
		ADD_IF_MISSING("SkyGfx", "waterFoamThreshold", "0.6");
		ADD_IF_MISSING("SkyGfx", "waterFoamSoftness", "0.2");
		ADD_IF_MISSING("SkyGfx", "waterShallowR", "0.13");
		ADD_IF_MISSING("SkyGfx", "waterShallowG", "0.45");
		ADD_IF_MISSING("SkyGfx", "waterShallowB", "0.55");
		ADD_IF_MISSING("SkyGfx", "waterDeepR", "0.01");
		ADD_IF_MISSING("SkyGfx", "waterDeepG", "0.03");
		ADD_IF_MISSING("SkyGfx", "waterDeepB", "0.08");
		ADD_IF_MISSING("SkyGfx", "waterReflectionFarClip", "1000.0");
		// Water rewrite knobs (Xbox/IV-style water)
		ADD_IF_MISSING("SkyGfx", "waterTileScale", "1.0");
		ADD_IF_MISSING("SkyGfx", "waterShoreFade", "2.0");
		ADD_IF_MISSING("SkyGfx", "waterTranslucency", "1.0");
		ADD_IF_MISSING("SkyGfx", "waterReflectionStrength", "0.55");
		ADD_IF_MISSING("SkyGfx", "waterUseTimecycle", "1");
		ADD_IF_MISSING("SkyGfx", "waterStyle", "Xbox");   // Xbox / IV / V
		ADD_IF_MISSING("SkyGfx", "waterQuality", "3");    // 0-3 Low..Ultra
		ADD_IF_MISSING("SkyGfx", "waterFresnelSplit", "1"); // 1=split (default), 0=legacy
		ADD_IF_MISSING("SkyGfx", "cloudRenderFix", "1");    // cloud render-state hygiene

		// ===== Utility Noise Texture (global) =====
		ADD_IF_MISSING("General", "NoiseQuality", "0");	// 0=32, 1=128, 2=512, >=3=1024

		#undef ADD_IF_MISSING
	}
	
	// Write back only if we made changes (always safe since linb::ini preserves existing keys)
	cfg.write_file(modulePath);
	return iniExisted;
}

void
readIni(int n)
{
	char modulePath[MAX_PATH];
	GetModuleFileName(dllModule, modulePath, MAX_PATH);
	strncpy(asipath, modulePath, MAX_PATH);
	char *p = strrchr(asipath, '\\');
	if (p) p[1] = '\0';

	GetModuleFileName(dllModule, modulePath, MAX_PATH);
	size_t nLen = strlen(modulePath);
	Config *c;
	if(n > 0){
		if (nLen + 1 < MAX_PATH) {
			modulePath[nLen+1] = L'\0';
		}
		modulePath[nLen] = L'i';
		modulePath[nLen-1] = L'n';
		modulePath[nLen-2] = L'i';
		modulePath[nLen-3] = L'.';
		modulePath[nLen-4] = n+'0';
		c = &configs[n-1];
	}else{
		modulePath[nLen-1] = L'i';
		modulePath[nLen-2] = L'n';
		modulePath[nLen-3] = L'i';
		c = &configs[n];
	}
	readIniFile(modulePath, c);
}

void
readInis(void)
{
	original_bRadiosity = CPostEffects::m_bRadiosity;
	if(numConfigs == 0)
		readIni(0);
	else
		for(int i = 1; i <= numConfigs; i++)
			readIni(i);
	refreshIni();
}

void
setConfig(void)
{
	if(currentConfig >= 0 && currentConfig < numConfigs){
		config = &configs[currentConfig];

		refreshIni();
	}
}

void
reloadAllInis(void)
{
	if(numConfigs == 0)
		readIni(0);
	else
		for(int i = 1; i <= numConfigs; i++)
			readIni(i);
	refreshIni();
}

bool
saveConfigTo(const char *path)
{
	linb::ini cfg;
	cfg.load_file(path);	// merge into existing file (preserves unknown keys/comments when present)

	Config *c = config;

	clampEnvSliderRanges(c);		// never persist out-of-range env values
	clampTonemapExposureRanges(c);	// never persist out-of-range tonemap exposure
	clampSsaoPower(c);				// never persist out-of-range SSAO gamma
	clampChromeParams(c);			// never persist out-of-range chrome params
	cfEnableHeal(c);				// never persist a silently-stomped colour-filter gate

	// Pipeline
	static const char* pipelineNames[] = {"PBR", "PS2", "Xbox", "Mobile", "GTAIV"};
	if(c->pipeline >= 0 && c->pipeline < 5)
		cfg.set("SkyGfx", "pipeline", pipelineNames[c->pipeline]);
	cfg.set("SkyGfx", "pipelineOverride", std::to_string(c->pipelineOverride));

	// Pipe overrides (written by readIni from INI)
	static const char* buildPipeNames[] = {"PS2", "Xbox", "GTAIV", "PBR"};
	if(c->buildingPipe >= 0 && c->buildingPipe < 4)
		cfg.set("SkyGfx", "buildingPipe", buildPipeNames[c->buildingPipe]);

	static const char* vehPipeNames[] = {"PS2", "PC", "Xbox", "Specular", "Mobile", "Neo", "Leeds", "VCS", "Env", "GTAIV", "Modern"};
	if(c->vehiclePipe >= 0 && c->vehiclePipe < 11)
		cfg.set("SkyGfx", "vehiclePipe", vehPipeNames[c->vehiclePipe]);

	static const char* colorFilterNames[] = {"None", "PS2", "PC", "Mobile", "III", "VC", "VCS", "GTAIV", "Modern"};
	if(c->colorFilter >= 0 && c->colorFilter < 9)
		cfg.set("SkyGfx", "colorFilter", colorFilterNames[c->colorFilter]);

	// Pipeline switches
	cfg.set("SkyGfx", "ps2Modulate", std::to_string(c->ps2ModulateGlobal));
	cfg.set("SkyGfx", "dualPass", std::to_string(c->dualPassGlobal));
	cfg.set("SkyGfx", "detailMaps", std::to_string(c->detailMaps));
	cfg.set("SkyGfx", "stochasticTexturing", std::to_string(c->stochastic));
	cfg.set("SkyGfx", "ps2ModulateBuilding", std::to_string(c->ps2ModulateBuilding));
	cfg.set("SkyGfx", "dualPassBuilding", std::to_string(c->dualPassBuilding));
	cfg.set("SkyGfx", "bldEnvReflect", std::to_string(c->bldEnvReflect));
	cfg.set("SkyGfx", "bldSkyReflect", std::to_string(c->bldSkyReflect));
	cfg.set("SkyGfx", "dualPassVehicle", std::to_string(c->dualPassVehicle));
	cfg.set("SkyGfx", "vehPBRLayers", std::to_string(c->vehPBRLayers));
	cfg.set("SkyGfx", "dualPassGrass", std::to_string(c->dualPassGrass));
	cfg.set("SkyGfx", "dualPassDefault", std::to_string(c->dualPassDefault));
	cfg.set("SkyGfx", "dualPassPed", std::to_string(c->dualPassPed));
	cfg.set("SkyGfx", "ps2ModulateGrass", std::to_string(c->ps2ModulateGrass));
	cfg.set("SkyGfx", "grassAddAmbient", std::to_string(c->grassAddAmbient));
	cfg.set("SkyGfx", "grassBackfaceCull", std::to_string(c->backfaceCull));
	cfg.set("SkyGfx", "grassFixPlacement", std::to_string(c->fixGrassPlacement));
	cfg.set("SkyGfx", "sunGlare", std::to_string(c->doglare));
	cfg.set("SkyGfx", "neoWaterDrops", std::to_string(c->neoWaterDrops));
	cfg.set("SkyGfx", "neoBloodDrops", std::to_string(c->neoBloodDrops));

	cfg.set("SkyGfx", "pedShadows", std::to_string(c->pedShadows));
	cfg.set("SkyGfx", "stencilShadows", std::to_string(c->stencilShadows));
	cfg.set("SkyGfx", "lightningIlluminatesWorld", std::to_string(c->lightningIlluminatesWorld));
	cfg.set("SkyGfx", "doRadiosity", std::to_string(c->doRadiosity));

	// Radiosity
	static const char* radiosityNames[] = {"PS2", "Shader"};
	if(c->radiosity >= 0 && c->radiosity < 2)
		cfg.set("SkyGfx", "radiosity", radiosityNames[c->radiosity]);
	cfg.set("SkyGfx", "vcsTrails", std::to_string(c->vcsTrails));
	cfg.set("SkyGfx", "trailsLimit", std::to_string(c->trailsLimit));
	cfg.set("SkyGfx", "trailsIntensity", std::to_string(c->trailsIntensity));
	cfg.set("SkyGfx", "trailsResolution", std::to_string(c->trailsResolution));
	cfg.set("SkyGfx", "radiosityFilterPasses", std::to_string(c->radiosityFilterPasses));
	cfg.set("SkyGfx", "radiosityRenderPasses", std::to_string(c->radiosityRenderPasses));
	cfg.set("SkyGfx", "radiosityIntensity", std::to_string(c->radiosityIntensity));

	// Shininess
	cfg.set("SkyGfx", "leedsShininessMult", std::to_string(c->leedsShininessMult));
	cfg.set("SkyGfx", "neoShininessMult", std::to_string(c->neoShininessMult));
	cfg.set("SkyGfx", "neoSpecularityMult", std::to_string(c->neoSpecularityMult));
	cfg.set("SkyGfx", "envShininessMult", std::to_string(c->envShininessMult));
	cfg.set("SkyGfx", "envSpecularityMult", std::to_string(c->envSpecularityMult));
	cfg.set("SkyGfx", "envPower", std::to_string(c->envPower));
	cfg.set("SkyGfx", "envFresnel", std::to_string(c->envFresnel));
	cfg.set("SkyGfx", "vehEnvIntensity", std::to_string(c->vehEnvIntensity));
	cfg.set("SkyGfx", "vehChromeEnvThreshold", std::to_string(c->vehChromeEnvThreshold));
	cfg.set("SkyGfx", "vehAutoChrome", std::to_string(c->vehAutoChrome));
	cfg.set("SkyGfx", "chromeF0", std::to_string(c->chromeF0));
	cfg.set("SkyGfx", "chromeGloss", std::to_string(c->chromeGloss));
	cfg.set("SkyGfx", "chromeMetallic", std::to_string(c->chromeMetallic));
	cfg.set("SkyGfx", "chromeEnvBoost", std::to_string(c->chromeEnvBoost));
	cfg.set("SkyGfx", "chromeClearcoat", std::to_string(c->chromeClearcoat));
	cfg.set("SkyGfx", "tonemapAutoExposure", std::to_string(c->tonemapAutoExposure));
	cfg.set("SkyGfx", "tonemapAdaptSpeed", std::to_string(c->tonemapAdaptSpeed));
	cfg.set("SkyGfx", "tonemapKeyStrength", std::to_string(c->tonemapKeyStrength));
	cfg.set("SkyGfx", "tonemapMinExposure", std::to_string(c->tonemapMinExposure));
	cfg.set("SkyGfx", "tonemapMaxExposure", std::to_string(c->tonemapMaxExposure));
	cfg.set("SkyGfx", "tonemapCurveLows", std::to_string(c->tonemapCurveLows));
	cfg.set("SkyGfx", "tonemapCurveHighs", std::to_string(c->tonemapCurveHighs));
	cfg.set("SkyGfx", "tonemapCurveMid", std::to_string(c->tonemapCurveMid));
	cfg.set("SkyGfx", "envMapSize", std::to_string(c->envMapSize));
	cfg.set("SkyGfx", "envMapFarClipMult", std::to_string(c->envMapFarClipMult));
	cfg.set("SkyGfx", "envMapUseLODs", std::to_string(c->envMapUseLODs));

	// Normal mapping
	cfg.set("SkyGfx", "normalMapEnable", std::to_string(c->normalMapEnable));
	cfg.set("SkyGfx", "normalMapIntensity", std::to_string(c->normalMapIntensity));
	cfg.set("SkyGfx", "normalMapPlayerOnly", std::to_string(c->normalMapPlayerOnly));

	// Dual-pass thresholds
	cfg.set("SkyGfx", "zwriteThreshold", std::to_string(c->zwriteThreshold));
	cfg.set("SkyGfx", "zwriteThresholdGrass", std::to_string(c->zwriteThresholdGrass));
	cfg.set("SkyGfx", "zwriteThresholdPed", std::to_string(c->zwriteThresholdPed));

	// Corona/sun
	cfg.set("SkyGfx", "coronaZtest", std::to_string(c->coronaZtest));
	cfg.set("SkyGfx", "sunCoronaIntensity", std::to_string(c->sunCoronaIntensity));
	cfg.set("SkyGfx", "sunCoreIntensity", std::to_string(c->sunCoreIntensity));
	cfg.set("SkyGfx", "sunStreakIntensity", std::to_string(c->sunStreakIntensity));
	cfg.set("SkyGfx", "sunStreakSize", std::to_string(c->sunStreakSize));

	// YCbCr
	cfg.set("SkyGfx", "YCbCrCorrection", std::to_string(c->bYCbCrFilter));
	cfg.set("SkyGfx", "lumaScale", std::to_string(c->lumaScale));
	cfg.set("SkyGfx", "lumaOffset", std::to_string(c->lumaOffset));
	cfg.set("SkyGfx", "CbScale", std::to_string(c->cbScale));
	cfg.set("SkyGfx", "CbOffset", std::to_string(c->cbOffset));
	cfg.set("SkyGfx", "CrScale", std::to_string(c->crScale));
	cfg.set("SkyGfx", "CrOffset", std::to_string(c->crOffset));

	// SSAO
	cfg.set("SkyGfx", "ssaoEnable", std::to_string(c->ssaoEnable));
	cfg.set("SkyGfx", "ssaoRadius", std::to_string(c->ssaoRadius));
	cfg.set("SkyGfx", "ssaoPower", std::to_string(c->ssaoPower));
	cfg.set("SkyGfx", "ssaoKernelSize", std::to_string((int)c->ssaoKernelSize));
	cfg.set("SkyGfx", "ssaoSampleCount", std::to_string(c->ssaoSampleCount));

	// SSAO overhaul
	cfg.set("SkyGfx", "ssaoTemporalEnable", std::to_string(c->ssaoTemporalEnable));
	cfg.set("SkyGfx", "ssaoTemporalBlend", std::to_string(c->ssaoTemporalBlend));
	cfg.set("SkyGfx", "ssaoBlurPasses", std::to_string(c->ssaoBlurPasses));
	cfg.set("SkyGfx", "ssaoBlurRadius", std::to_string(c->ssaoBlurRadius));
	cfg.set("SkyGfx", "ssaoDepthThreshold", std::to_string(c->ssaoDepthThreshold));

	// SMAA — single toggle
	cfg.set("SkyGfx", "smaaEnable", std::to_string(c->smaaEnable));

	// Motion blur
	cfg.set("SkyGfx", "motionBlurEnable", std::to_string(c->motionBlurEnable));
	cfg.set("SkyGfx", "motionBlurStrength", std::to_string(c->motionBlurStrength));
	cfg.set("SkyGfx", "motionBlurRadial", std::to_string(c->motionBlurRadial));
	cfg.set("SkyGfx", "motionBlurSpeedFactor", std::to_string(c->motionBlurSpeedFactor));
	cfg.set("SkyGfx", "motionBlurCameraAware", std::to_string(c->motionBlurCameraAware));

	// SSS post-process
	cfg.set("SkyGfx", "sssPostProcessEnable", std::to_string(c->sssPostProcessEnable));
	cfg.set("SkyGfx", "sssPostProcessStrength", std::to_string(c->sssPostProcessStrength));
	cfg.set("SkyGfx", "sssPostProcessRadius", std::to_string(c->sssPostProcessRadius));
	cfg.set("SkyGfx", "sssPostProcessThreshold", std::to_string(c->sssPostProcessThreshold));
	cfg.set("SkyGfx", "sssAmbientBoost", std::to_string(c->sssAmbientBoost));

	// Skin enhancement
	cfg.set("SkyGfx", "skinEnhanceEnable", std::to_string(c->skinEnhanceEnable));
	cfg.set("SkyGfx", "skinWrapFactor", std::to_string(c->skinWrapFactor));
	cfg.set("SkyGfx", "skinSpecularPower", std::to_string(c->skinSpecularPower));
	cfg.set("SkyGfx", "skinSpecularStrength", std::to_string(c->skinSpecularStrength));
	cfg.set("SkyGfx", "skinSSSStrength", std::to_string(c->skinSSSStrength));

	// Hair enhancement
	cfg.set("SkyGfx", "hairEnhanceEnable", std::to_string(c->hairEnhanceEnable));
	cfg.set("SkyGfx", "hairAnisotropicPower", std::to_string(c->hairAnisotropicPower));
	cfg.set("SkyGfx", "hairAnisotropicStrength", std::to_string(c->hairAnisotropicStrength));
	cfg.set("SkyGfx", "hairSSSStrength", std::to_string(c->hairSSSStrength));

	// Vegetation enhancement
	cfg.set("SkyGfx", "vegetationEnhanceEnable", std::to_string(c->vegetationEnhanceEnable));
	cfg.set("SkyGfx", "vegetationSSSStrength", std::to_string(c->vegetationSSSStrength));
	cfg.set("SkyGfx", "vegetationAmbientBoost", std::to_string(c->vegetationAmbientBoost));

	// GTA IV
	cfg.set("SkyGfx", "ivMode", std::to_string(c->ivMode));
	cfg.set("SkyGfx", "ivDesaturation", std::to_string(c->ivDesaturation));
	cfg.set("SkyGfx", "ivGamma", std::to_string(c->ivGamma));
	cfg.set("SkyGfx", "ivSaturation", std::to_string(c->ivSaturation));
	cfg.set("SkyGfx", "ivCurves", std::to_string(c->ivCurves));
	cfg.set("SkyGfx", "ivVignetteIntensity", std::to_string(c->ivVignetteIntensity));
	cfg.set("SkyGfx", "ivVignetteRadius", std::to_string(c->ivVignetteRadius));
	cfg.set("SkyGfx", "ivVignetteContrast", std::to_string(c->ivVignetteContrast));
	cfg.set("SkyGfx", "ivBloomIntensity", std::to_string(c->ivBloomIntensity));
	cfg.set("SkyGfx", "ivExposure", std::to_string(c->ivExposure));

	// Atmospheric: Height Fog
	cfg.set("SkyGfx", "heightFogEnable", std::to_string(c->heightFogEnable));
	cfg.set("SkyGfx", "heightFogDensity", std::to_string(c->heightFogDensity));
	cfg.set("SkyGfx", "heightFogHeightFalloff", std::to_string(c->heightFogHeightFalloff));
	cfg.set("SkyGfx", "heightFogStartHeight", std::to_string(c->heightFogStartHeight));
	cfg.set("SkyGfx", "heightFogR", std::to_string(c->heightFogR));
	cfg.set("SkyGfx", "heightFogG", std::to_string(c->heightFogG));
	cfg.set("SkyGfx", "heightFogB", std::to_string(c->heightFogB));
	cfg.set("SkyGfx", "heightFogTimecycleScale", std::to_string(c->heightFogTimecycleScale));

	// Atmospheric: God Rays
	cfg.set("SkyGfx", "godRaysEnable", std::to_string(c->godRaysEnable));
	cfg.set("SkyGfx", "godRaysExposure", std::to_string(c->godRaysExposure));
	cfg.set("SkyGfx", "godRaysDecay", std::to_string(c->godRaysDecay));
	cfg.set("SkyGfx", "godRaysDensity", std::to_string(c->godRaysDensity));
	cfg.set("SkyGfx", "godRaysWeight", std::to_string(c->godRaysWeight));
	cfg.set("SkyGfx", "godRaysNumSamples", std::to_string(c->godRaysNumSamples));

	// Velocity buffer
	cfg.set("SkyGfx", "velocityBufferEnable", std::to_string(c->velocityBufferEnable));

	// Depth Hook
	cfg.set("SkyGfx", "depthHookEnable", std::to_string(c->depthHookEnable));

	// Forward+ tiled lighting
	cfg.set("SkyGfx", "forwardPlusEnable", std::to_string(c->forwardPlusEnable));

	// Normal buffer
	cfg.set("SkyGfx", "normalBufferEnable", std::to_string(c->normalBufferEnable));
	cfg.set("SkyGfx", "normalBufferOffset", std::to_string(c->normalBufferOffset));
	cfg.set("SkyGfx", "normalBufferScale", std::to_string(c->normalBufferScale));

	// Pipe chain
	cfg.set("SkyGfx", "pipeChainEnable", std::to_string(c->pipeChainEnable));
	cfg.set("SkyGfx", "pipeChainIntensity", std::to_string(c->pipeChainIntensity));

	// Water pipe (procedural PBR water)
	cfg.set("SkyGfx", "waterParallaxEnable", std::to_string(c->waterParallaxEnable));
	cfg.set("SkyGfx", "waterParallaxScale", std::to_string(c->waterParallaxScale));
	cfg.set("SkyGfx", "waterNormalStrength", std::to_string(c->waterNormalStrength));
	cfg.set("SkyGfx", "waterFresnelPower", std::to_string(c->waterFresnelPower));
	cfg.set("SkyGfx", "waterSpecularPower", std::to_string(c->waterSpecularPower));
	cfg.set("SkyGfx", "waterSpecularIntensity", std::to_string(c->waterSpecularIntensity));
	cfg.set("SkyGfx", "waterUnderwaterFog", std::to_string(c->waterUnderwaterFog));
	cfg.set("SkyGfx", "waterFoamThreshold", std::to_string(c->waterFoamThreshold));
	cfg.set("SkyGfx", "waterFoamSoftness", std::to_string(c->waterFoamSoftness));
	cfg.set("SkyGfx", "waterShallowR", std::to_string(c->waterShallowR));
	cfg.set("SkyGfx", "waterShallowG", std::to_string(c->waterShallowG));
	cfg.set("SkyGfx", "waterShallowB", std::to_string(c->waterShallowB));
	cfg.set("SkyGfx", "waterDeepR", std::to_string(c->waterDeepR));
	cfg.set("SkyGfx", "waterDeepG", std::to_string(c->waterDeepG));
	cfg.set("SkyGfx", "waterDeepB", std::to_string(c->waterDeepB));
	cfg.set("SkyGfx", "waterReflectionFarClip", std::to_string(c->waterReflectionFarClip));
	// Water rewrite knobs (Xbox/IV-style water)
	cfg.set("SkyGfx", "waterTileScale", std::to_string(c->waterTileScale));
	cfg.set("SkyGfx", "waterShoreFade", std::to_string(c->waterShoreFade));
	cfg.set("SkyGfx", "waterTranslucency", std::to_string(c->waterTranslucency));
	cfg.set("SkyGfx", "waterReflectionStrength", std::to_string(c->waterReflectionStrength));
	cfg.set("SkyGfx", "waterUseTimecycle", std::to_string(c->waterUseTimecycle));

	// Debug toggles
	cfg.set("SkyGfx", "colorFilterEnable", std::to_string(c->colorFilterEnable));
	cfg.set("SkyGfx", "radiosityEnable", std::to_string(c->radiosityEnable));
	cfg.set("SkyGfx", "grainEnable", std::to_string(c->grainEnable));
	cfg.set("SkyGfx", "postfxDumpDebug", std::to_string(c->postfxDumpDebug));

	// Blur offsets
	cfg.set("SkyGfx", "blurLeft", std::to_string(c->offLeft));
	cfg.set("SkyGfx", "blurTop", std::to_string(c->offTop));
	cfg.set("SkyGfx", "blurRight", std::to_string(c->offRight));
	cfg.set("SkyGfx", "blurBottom", std::to_string(c->offBottom));

	// Timecycle
	cfg.set("SkyGfx", "usePCTimecyc", std::to_string(c->usePCTimecyc));

	// rgb multipliers
	cfg.set("SkyGfx", "rgb1Mult", std::to_string(c->rgb1Mult));
	cfg.set("SkyGfx", "rgb2Mult", std::to_string(c->rgb2Mult));

	// Death ragdoll
	cfg.set("SkyGfx", "ragdollEnable", std::to_string(c->ragdollEnable));
	cfg.set("SkyGfx", "pbrAmbientFloor", std::to_string(c->pbrAmbientFloor));
	cfg.set("SkyGfx", "pbrIblAmbientWeight", std::to_string(c->pbrIblAmbientWeight));
	cfg.set("SkyGfx", "tonemapBlackLift", std::to_string(c->tonemapBlackLift));
	cfg.set("SkyGfx", "vehPrelightFallback", std::to_string(c->vehPrelightFallback));
	cfg.set("SkyGfx", "vehEnvFallback", std::to_string(c->vehEnvFallback));
	cfg.set("SkyGfx", "vehEnvStrengthMode", std::to_string(c->vehEnvStrengthMode));
	cfg.set("SkyGfx", "vehEnvMask", std::to_string(c->vehEnvMask));
	cfg.set("SkyGfx", "vehWetEnv", std::to_string(c->vehWetEnv));
	cfg.set("SkyGfx", "vehEnvGlint", std::to_string(c->vehEnvGlint));

	// Round-trip additions: menu-mutated keys that were previously never saved
	cfg.set("SkyGfx", "infraredVision", std::to_string(c->infraredVision));
	cfg.set("SkyGfx", "nightVision", std::to_string(c->nightVision));
	cfg.set("SkyGfx", "grainFilter", std::to_string(c->grainFilter));
	cfg.set("SkyGfx", "sssEnable", std::to_string(c->sssEnable));
	cfg.set("SkyGfx", "sssIntensity", std::to_string(c->sssIntensity));
	cfg.set("SkyGfx", "sssVegIntensity", std::to_string(c->sssVegIntensity));
	cfg.set("SkyGfx", "sssSkinIntensity", std::to_string(c->sssSkinIntensity));
	cfg.set("SkyGfx", "sssClothIntensity", std::to_string(c->sssClothIntensity));
	cfg.set("SkyGfx", "gamePreset", std::to_string(c->preset));	// Style combo display

	bool ok = cfg.write_file(path);
	if(dbglog_throttle("cfg_save"))
		dbglog("skygfx: saveConfig %s path=%s", ok ? "ok" : "FAIL", path);
	return ok;
}

void
saveConfig(void)
{
	char modulePath[MAX_PATH];
	GetModuleFileName(dllModule, modulePath, MAX_PATH);
	size_t nLen = strlen(modulePath);
	if(nLen >= 4){
		// .asi -> .ini, the same extension rewrite readIni does. Previously
		// saveConfig used the raw ASI path: load_file parsed the binary and
		// write_file's fopen("w") on the loaded/mapped ASI failed (sharing
		// violation) — so the Save INI button silently did nothing.
		modulePath[nLen-4] = '.';
		modulePath[nLen-3] = 'i';
		modulePath[nLen-2] = 'n';
		modulePath[nLen-1] = 'i';
	}
	saveConfigTo(modulePath);
}

bool
loadConfigFile(const char *path)
{
	bool existed = readIniFile(path, config);
	refreshIni();	// mirror readInis()/reloadAllInis(): push values into postfx state + legacy menu
	if(dbglog_throttle("cfg_load"))
		dbglog("skygfx: loadConfig %s (%s)", path, existed ? "ok" : "FAIL missing/unreadable");
	return existed;
}

// load asi ini again after having read stream ini as we need to know radiosity settings
void __declspec(naked)
afterStreamIni(void)
{
	_asm{
		call readInis
		retn
	}
}

#define MENUSETTINGS \
	X(ps2ModulateGlobal)		\
	X(ps2ModulateBuilding)		\
	X(ps2ModulateGrass)			\
	X(dualPassGlobal)				\
	X(dualPassDefault)				\
	X(dualPassBuilding)			\
	X(dualPassVehicle)			\
	X(dualPassPed)			\
	X(dualPassGrass)				\
	X(buildingPipe)				\
	X(tagsBuildingPipe)				\
	X(detailMaps)				\
	X(stochastic)				\
	X(vehiclePipe)				\
	X(leedsShininessMult)				\
	X(neoShininessMult)				\
	X(neoSpecularityMult)			\
	X(envShininessMult)				\
	X(envSpecularityMult)			\
	X(envPower)			\
	X(envFresnel)			\
	X(doglare)						\
	X(fixGrassPlacement)			\
	X(grassAddAmbient)			\
	X(backfaceCull)			\
	X(pedShadows)					\
	X(stencilShadows)				\
	X(colorFilter)					\
	X(doRadiosity)					\
	X(radiosity)					\
	X(lightningIlluminatesWorld)		\
	X(neoWaterDrops)			\
	X(neoBloodDrops)			\
	X(infraredVision)				\
	X(nightVision)					\
	X(grainFilter)					\
	X(offLeft)					\
	X(offRight)				\
	X(offTop)					\
	X(offBottom)				\
	X(radiosityFilterPasses)		\
	X(radiosityRenderPasses)		\
	X(radiosityIntensity)			\
	X(zwriteThreshold)			\
	X(zwriteThresholdGrass)			\
	X(zwriteThresholdPed)			\
	X(coronaZtest)				\
	X(bYCbCrFilter)				\
	X(lumaScale)				\
	X(lumaOffset)				\
	X(cbScale)				\
	X(cbOffset)				\
	X(crScale)				\
	X(crOffset)				\
	X(rgb1Mult)				\
	X(rgb2Mult)				\
	X(envMapSize)			\
	X(envMapUseLODs)			\
	X(envMapFarClipMult)		\
	X(ssaoEnable)			\
	X(ssaoRadius)			\
	X(ssaoPower)			\
	X(ssaoKernelSize)			\
	X(ssaoSampleCount)			\
	X(smaaEnable)			\
	X(ivMode)				\
	X(ivDesaturation)			\
	X(ivGamma)				\
	X(ivVignetteIntensity)		\
	X(ivVignetteRadius)			\
	X(ivVignetteContrast)		\
	X(ivBloomIntensity)			\
	X(ivExposure)

// Debug menu functions are in debugmenu_ui.cpp (installMenu, refreshMenu, etc.)

// ============================================================
// HOOKS — inline, matching backup_original DllMain pattern
// ============================================================

void hooktexdb(void);
void installMenu(void);
// Debug-menu accessors (debugmenu_ui.cpp): [General] NoiseQuality lives in
// the TU-static g_noiseQuality above, so the in-game menu reaches it through
// these instead of a cross-TU extern. set runs refreshIni's regen path via
// GenerateUtilityTexture (RW must be up — only called while drawing).
int  menuGetNoiseQuality(void) { return g_noiseQuality; }
void menuSetNoiseQuality(int q) { g_noiseQuality = q; }
extern "C" bool RpNormMapPluginAttach(void);

int
InjectDelayedPatches()
{
	dbglog("InjectDelayedPatches entered");

	findInis();
	dbglog("  numConfigs=%d", numConfigs);
	if(numConfigs == 0)
		readIni(0);
	else
		readIni(1);
	dbglog("  ini loaded");

	fixingSAMP = ModuleList().Get(L"samp") || ModuleList().Get(L"SAMPGraphicRestore");
	UG_mod = ModuleList().Get(L"Underground_Core");
	if(UG_mod)
		UG_RegisterEventCallback = (void (*)(const char*, UG_EventHook))GetProcAddress(UG_mod, "RegisterEventCallback");

	// Detect MoonLoader — keep hook compatible
	g_hasMoonLoader = (GetModuleHandleA("MoonLoader.asi") != nullptr);
	if(g_hasMoonLoader)
		dbglog("  DETECT: MoonLoader — hooks will be installed immediately");

	// Install all hooks immediately regardless of MoonLoader
	if(UG_RegisterEventCallback){
		dbglog("  UG EVENTS: initposteffects");
		UG_RegisterEventCallback("EVENT_INITPOSTEFFECTS", CPostEffects::Initialise_skygfx);
	}else{
		dbglog("  HOOK: CPostEffects::Initialise -> 0x5BD779");
		InterceptCall(&CPostEffects::Initialise_orig, CPostEffects::Initialise, 0x5BD779);
	}

	// Only hook InitialiseGame if MoonLoader is NOT present
	if(!g_hasMoonLoader){
		dbglog("  HOOK: InitialiseGame -> 0x748CFB (no MoonLoader)");
		InterceptCall(&InitialiseGame, InitialiseGame_hook, 0x748CFB);
	}else{
		dbglog("  SKIP: InitialiseGame (MoonLoader handles this)");
	}

	// Install deferred init immediately (scene render hooks + envmaphooks).
	// Under MoonLoader InitialiseGame_hook never runs, which left Forward+ and
	// the real-time env-map reflections dead. Guarded -> idempotent.
	installDeferredHooks();

	installLCMV2Hooks();

	Nop(0x5BBF6F, 2);
	Nop(0x5BBF83, 2);

	explicitBuildingPipe = explicitBuildingPipe_tmp;

	dbglog("  HOOK: normalmap_init()");
	normalmap_init();

	if(iCanHasbuildingPipe){
		dbglog("  HOOK: hookBuildingPipe()");
		hookBuildingPipe();
	}
	if(iCanHasvehiclePipe){
		dbglog("  HOOK: hookVehiclePipe()");
		hookVehiclePipe();
	}

	dbglog("  INIT: Ragdoll manager");
	g_ragdollMan.Init();
	dbglog("Ragdoll manager initialized (%d ragdolls in pool)", MAX_RAGDOLLS);

	dbglog("  INIT: Death ragdoll");
	Ragdoll_Init();

	InjectHook(0x5E675E, &FX::GetFxQuality_ped);
	InjectHook(0x5E676D, &FX::GetFxQuality_ped);
	InjectHook(0x706BC4, &FX::GetFxQuality_ped);
	InjectHook(0x706BD3, &FX::GetFxQuality_ped);
	InjectHook(0x7113B8, &FX::GetFxQuality_stencil);
	InjectHook(0x711D95, &FX::GetFxQuality_stencil);
	InjectHook(0x70F9B8, &FX::GetFxQuality_stencil);

	if(fixPcCarLight){
		Patch<uint>(0x5D88D1 +6, 0);
		Patch<uint>(0x5D88DB +6, 0);
		Patch<uint>(0x5D88E5 +6, 0);
		Patch<uint>(0x5D88F9 +6, 0);
		Patch<uint>(0x5D8903 +6, 0);
		Patch<uint>(0x5D890D +6, 0);
	}

	if(disableClouds) InjectHook(0x714145, 0x71422A, PATCH_JUMP);
	// CClouds::Render: wrap BOTH call sites (the mirror-skipping one at
	// 0x53DFA0 and the plain one at 0x53DCA2) with the render-state hygiene
	// hook — see CClouds__Render_hygiene for the inherited-state analysis.
	// InterceptCall keeps the original target in CClouds__Render_orig, so
	// cloudRenderFix=0 simply passes straight through.
	InterceptCall(&CClouds__Render_orig, CClouds__Render_hygiene, 0x53DCA2);
	InterceptCall(&CClouds__Render_orig, CClouds__Render_hygiene, 0x53DFA0);
	if(disableGamma) InjectHook(0x74721C, 0x7472F3, PATCH_JUMP);
	if(iCanHasNeoDrops) hookWaterDrops();
	if(iCanHasSunGlare) InjectHook(0x6ABCFD, doglare, PATCH_JUMP);

	if(transparentLockon > 0){
		InjectHook(0x742E33, 0x742EC1, PATCH_JUMP);
		InjectHook(0x742FE0, 0x743085, PATCH_JUMP);
	}

	if(fixShadows){
		static float shadowoffset = 0.0f;
		Patch(0x709B2D + 2, &shadowoffset);
		Patch(0x709B8C + 2, &shadowoffset);
		Patch(0x709BC5 + 2, &shadowoffset);
		Patch(0x709BF4 + 2, &shadowoffset);
		Patch(0x709C91 + 2, &shadowoffset);
		Patch(0x709E9C + 2, &shadowoffset);
		Patch(0x709EBA + 2, &shadowoffset);
		Patch(0x709ED5 + 2, &shadowoffset);
		Patch(0x70B21F + 2, &shadowoffset);
		Patch(0x70B371 + 2, &shadowoffset);
		Patch(0x70B4CF + 2, &shadowoffset);
		Patch(0x70B633 + 2, &shadowoffset);
		Patch(0x7085A7 + 2, &shadowoffset);
		*(float*)0x8CD4F0 = 256.0f;
	}

	if(privateHooks){
		static const char *loadsc0 = "loadsc0";
		Patch(0x5901BD + 1, loadsc0);
		Nop(0x748AA8, 0x748AE7-0x748AA8);
	}

	// Inject SkyGFX settings into native pause menu
	// DISABLED: conflicts with MoonLoader's D3D9 hook (d3dhook::originalD3DDevice9 assertion)
	// menu_inject_init();

	// Water pipe hooks � intercept CWaterLevel::RenderAndEmptyRenderBuffer
	// at all four call sites to inject custom water rendering.
	dbglog("  HOOK: WaterLevel ::RenderAndEmptyRenderBuffer (4 sites)");
	waterPipe_init();
	InterceptCall(&CWaterLevel__RenderAndEmptyRenderBuffer, CWaterLevel__RenderAndEmptyRenderBuffer_hook, 0x6E8790);
	InterceptCall(&CWaterLevel__RenderAndEmptyRenderBuffer, CWaterLevel__RenderAndEmptyRenderBuffer_hook, 0x6E8EF1);
	InterceptCall(&CWaterLevel__RenderAndEmptyRenderBuffer, CWaterLevel__RenderAndEmptyRenderBuffer_hook, 0x6E91E4);
	InterceptCall(&CWaterLevel__RenderAndEmptyRenderBuffer, CWaterLevel__RenderAndEmptyRenderBuffer_hook, 0x6E9963);

	installMenu();
	dbglog("=== InjectDelayedPatches complete ===");
	return FALSE;
}

// ============================================================
// DIAGNOSTICS — inline from diagnostics.cpp
// ============================================================

// Normal map plugin hook — captures normal map textures on vehicle atomics
extern "C" {
	extern bool RpNormMapAtomicIsInitialized(const RpAtomic *atomic);
	extern RpMaterial* RpNormMapMaterialSetNormMapTexture(RpMaterial* material, RwTexture* normalmap);
	extern RwTexture* RpNormMapMaterialGetNormMapTexture(const RpMaterial* material);
	}

	// NOTE: 0x5DA610 (CustomPipeAtomicSetup) is hooked by normalmap_init()
	// via InjectHook when the RW rwnormal plugin is active. No passthrough needed
	// here — the RW SDK plugin handles normal map pipeline setup natively.

BOOL WINAPI
DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
	if(reason == DLL_PROCESS_ATTACH){
		dllModule = hInst;

		// Construct log path from DLL path (matching backup_original pattern)
		char logPath[MAX_PATH];
		GetModuleFileNameA(dllModule, logPath, MAX_PATH);
		char *p = strrchr(logPath, '.');
		if(p) strcpy(p, "_dbg.log");
		else strncat(logPath, "_dbg.log", sizeof(logPath) - strlen(logPath) - 1);

		diag_init(logPath);
		diag_installVEH();
		// diag_startWatchdog() removed — heartbeat() is never called, causes false freeze dialog
		dbglog("VEH handler + watchdog installed");
		dbglog("skygfx build DrawSMAA-hoist d0886dd+1 (guarded-Im2D VEH pass-through)");

		dbglog("=== skygfx loading ===");
		dbglog("dllModule=%p, logPath=%s", dllModule, diag_getLogPath());
		dbglog("DynBaseAddr=%p", GetModuleHandle(nullptr));

		DWORD v1 = *(DWORD*)DynBaseAddress(0x82457C);
		DWORD v2 = *(DWORD*)DynBaseAddress(0x8245BC);
		dbglog("ver check: 0x82457C=%08X, 0x8245BC=%08X (expect 0x94BF)", v1, v2);

		if(v1 != 0x94BF && v2 == 0x94BF){
			dbglog("version check WARNING - v1 mismatch (0x%08X != 0x94BF), continuing anyway", v1);
		}else{
			dbglog("version check OK");
		}

		if(GetAsyncKeyState(VK_F8) & 0x8000){
			AllocConsole();
			freopen("CONIN$", "r", stdin);
			freopen("CONOUT$", "w", stdout);
			freopen("CONOUT$", "w", stderr);
		}

		for(int i = 0; i < 10; i++)
			configs[i].version = VERSION;

		dbglog("applying hooks...");

		/* Fix order of multiplication */
		extern void _rwD3D9VSGetComposedTransformMatrix(void *transformMatrix);
		InjectHook(0x7646E0, _rwD3D9VSGetComposedTransformMatrix, PATCH_JUMP);

		defaultColourLeftUOffset = CPostEffects::m_colourLeftUOffset;
		defaultColourRightUOffset = CPostEffects::m_colourRightUOffset;
		defaultColourTopVOffset = CPostEffects::m_colourTopVOffset;
		defaultColourBottomVOffset = CPostEffects::m_colourBottomVOffset;

		// moon mask
		InjectHook(0x713C4C, renderMoonMask, PATCH_JUMP);
		dbglog("  moon mask OK");

		// Apply delayed patches directly from DllMain instead of hooking 0x74872D (IsAlreadyRunning).
		// This avoids clashing with SilentPatch which hooks the exact same address.
		// All delayed patches are just hook installations and memory patches — safe to apply here.
		dbglog("  applying delayed patches directly...");
		InjectDelayedPatches();
		dbglog("  delayed patches OK");

		InjectHook(0x5BCF14, afterStreamIni, PATCH_JUMP);
		InjectHook(0x7491C0, myDefaultCallback, PATCH_JUMP);
		InjectHook(0x5BF8EA, CPlantMgr_Initialise);
		InjectHook(0x756DFE, rxD3D9DefaultRenderCallback_Hook, PATCH_JUMP);
		InjectHook(0x7FAD30, FrustumTestSphere_Guard, PATCH_JUMP);
		InjectHook(0x7FAD90, FrustumTestBox_Guard,     PATCH_JUMP);
		InjectHook(0x7618B0, PSSetCameraConstantD_Guard, PATCH_JUMP);
		InjectHook(0x5DADB7, fixSeed, PATCH_JUMP);

		// NOTE: RpNormMapPluginAttach is called by normalmap_init() during game init.
		// Do NOT call it here — it would succeed, then normalmap_init would fail,
		// leaving gHasExternalNormalMapPlugin=false and normalmap hooks never installed.

		// 0x5DA610 is hooked by normalmap_init() when rwnormal plugin is active
		InjectHook(0x5DAE61, saveIntensity, PATCH_JUMP);
		Patch(0x5DAEC8, setTextureAndColor);

		// add dual pass for PC pipeline
		InjectHook(0x5D9EEB, D3D9RenderDefault_DUAL);
		InjectHook(0x5D9EFB, D3D9RenderBlack_DUAL);

		// give vehicle pipe to upgrade parts
		InjectHook(0x4C88F0, 0x5DA610, PATCH_JUMP);

		// jump over code that sets alpha ref to 140 (not on PS2)
		InjectHook(0x553AD1, 0x553AE5, PATCH_JUMP);
		InterceptCall(&CSkidmarks__Render_orig, CSkidmarks__Render, 0x53E175);

		// ---- effects-phase three-layer alpha/Im2D resync (coronas,
		// particles, glows — see effectsForceAlphaBlock) ----
		// 0x53EAD3  call RenderEffects (0x53E170) — CCoronas::Render,
		//           g_fx.Render (particles), glass, weapon/special FX
		// 0x53DCAC / 0x53DFD8  call CCoronas::RenderReflections (0x6FB630)
		// 0x53E12B  call CCoronas::RenderSunReflection (0x6FBAA0)
		// (CClouds::Render itself is already intercepted at 0x53DCA2 /
		//  0x53DFA0 and forces the same block in its hygiene hook.)
		InterceptCall(&RenderEffects_orig, RenderEffects_hook, 0x53EAD3);
		InterceptCall(&CCoronas__RenderReflections_orig, CCoronas__RenderReflections_hook, 0x53DCAC);
		InterceptCall(&CCoronas__RenderReflections_orig, CCoronas__RenderReflections_hook, 0x53DFD8);
		InterceptCall(&CCoronas__RenderSunReflection_orig, CCoronas__RenderSunReflection_hook, 0x53E12B);
		// 0x53E227  tail jmp RenderEffects -> CPostEffects::Render (0x7046E0):
		//           stale-Im2D sweep + alpha repair on postfx exit.
		InterceptCall(&CPostEffects__Render_orig, CPostEffects__Render_hook, 0x53E227);

		/* Don't change tag material */
		InterceptCall(&CTagManager__RenderTagForPC, CTagManager__RenderTag, 0x534335);
		InterceptCall(&CTagManager__SetupAtomic_orig, CTagManager__SetupAtomic, 0x4C4412);
		*(void**)0xA9AD78 = (void*)TagRenderCB;

		// postfx
		InjectHook(0x704D1E, CPostEffects::ColourFilter_switch);
		InjectHook(0x704D5D, CPostEffects::Radiosity);
		InjectHook(0x704FB3, CPostEffects::Radiosity);
		InjectHook(0x704D48, CPostEffects::DarknessFilter_fix);

		// infrared vision
		InjectHook(0x704F4B, CPostEffects::InfraredVision_PS2);
		InjectHook(0x704F59, CPostEffects::Grain_PS2);
		// night vision
		InjectHook(0x704EDA, CPostEffects::NightVision_PS2);
		InjectHook(0x704EE8, CPostEffects::Grain_PS2);
		// rain
		InjectHook(0x705078, CPostEffects::Grain_PS2);
		// unused
		InjectHook(0x705091, CPostEffects::Grain_PS2);

		InjectHook(0x53EBE9, CPostEffects::DrawFinalEffects);

		// fix pointlight fog
		InjectHook(0x700B6B, CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect);

		InjectHook(0x44E82E, ps2rand);
		InjectHook(0x44ECEE, ps2rand);
		InjectHook(0x42453B, ps2rand);
		InjectHook(0x42454D, ps2rand);

		InterceptCall(&PipelinePluginAttach, myPluginAttach, 0x53D903);

		// procobj placement
		InjectHook(0x5A3C7D, ps2srand);
		InjectHook(0x5A3DFB, ps2srand);
		InjectHook(0x5A3C75, ps2rand);
		InjectHook(0x5A3CB9, ps2rand);
		InjectHook(0x5A3CDB, ps2rand);
		InjectHook(0x5A3CF2, ps2rand);
		Patch(0x5A3CC8, &ps2randnormalize);
		Patch(0x5A3CEA, &ps2randnormalize);
		Patch(0x5A3D05, &ps2randnormalize);
		InjectHook(0x5A3476, ps2rand);
		InjectHook(0x5A34AB, ps2rand);
		InjectHook(0x5A34E0, ps2rand);
		InjectHook(0x5A3515, ps2rand);
		Patch(0x5A348D + 2, &ps2randnormalize);
		Patch(0x5A34C2 + 2, &ps2randnormalize);
		Patch(0x5A34FB + 2, &ps2randnormalize);
		Patch(0x5A352F + 2, &ps2randnormalize);

		// increase multipass distance
		static float multipassMultiplier = 1000.0f;
		Patch<float*>(0x73290A+2, &multipassMultiplier);

		// Get rid of the annoying dotproduct check in visibility renderCBs
		Nop(0x733313, 2);
		Nop(0x73405A, 2);
		Nop(0x733403, 2);
		Nop(0x73431A, 2);
		Nop(0x73444A, 2);

		// change grass close far to ps2 values
		Patch<float>(0x5DDB3D+1, 78.0f);

		// High detail water color multiplier
		Nop(0x6E716B, 6);
		Nop(0x6E7176, 6);

		// Camera planes in CRenderer::RenderEverythingBarRoads
		static float zoffset = 0.0f;
		Patch(0x553C7D + 2, &zoffset);
		Nop(0x553C78, 5);
		Nop(0x553C9A, 5);
		Nop(0x553CD1, 5);
		Nop(0x553CEC, 5);

		// Fix mirrors
		Patch(0x726516 + 6, 216.1f);
		Patch(0x726534 + 6, 216.1f);
		Patch(0x726552 + 6, 216.1f);
		Patch(0x726570 + 6, 216.1f);

		hooktexdb();

		dbglog("=== DllMain complete, all hooks applied ===");
	}

	if(reason == DLL_PROCESS_DETACH){
		// NOTE: At DLL_PROCESS_DETACH, the game's RW/D3D9 engine may already
		// be torn down. RwRasterDestroy calls into RW internals (game-exe code)
		// that may be invalid → crash. Wrap all cleanup in __try/__except and
		// skip ReleaseDefaultPoolResources entirely (OS reclaims all memory).
		// DepthHook_ReleaseResources is also skipped — the device is gone.
		// shutdownTexDB is safe (frees strdup'd strings, no RW/D3D9 calls).
		__try {
			shutdownTexDB();
		} __except(EXCEPTION_EXECUTE_HANDLER) {
			dbglog("DLL_DETACH: shutdownTexDB crashed, continuing");
		}
		dbglog("texdb shutdown complete");

		// Env map resources: RwTextureCreate/RwCameraCreate/RwRasterCreate
		// call into RW internals that may already be torn down at DLL_PROCESS_DETACH.
		__try {
			ShutdownEnvMap();
		} __except(EXCEPTION_EXECUTE_HANDLER) {
			dbglog("DLL_DETACH: ShutdownEnvMap crashed, continuing");
		}
		dbglog("envmap shutdown complete");

		// Water pipe refraction RT is D3DPOOL_DEFAULT — device may already be
		// gone at DLL_PROCESS_DETACH; guard the Release.
		__try {
			waterPipe_shutdown();
		} __except(EXCEPTION_EXECUTE_HANDLER) {
			dbglog("DLL_DETACH: waterPipe_shutdown crashed, continuing");
		}

		// Forward+ resources are raw D3D9 textures — the device may be gone.
		// Skip to avoid Release on invalid device.
		if(d3d9device) {
			__try {
				ForwardPlus_ReleaseResources();
			} __except(EXCEPTION_EXECUTE_HANDLER) {
				dbglog("DLL_DETACH: ForwardPlus_ReleaseResources crashed, continuing");
			}
		}

		// Ragdoll shutdown is pure CPU (Bullet physics) — safe.
		__try {
			g_ragdollMan.Exit();
		} __except(EXCEPTION_EXECUTE_HANDLER) {
			dbglog("DLL_DETACH: ragdollMan.Exit crashed, continuing");
		}
		__try {
			Ragdoll_Shutdown();
		} __except(EXCEPTION_EXECUTE_HANDLER) {
			dbglog("DLL_DETACH: Ragdoll_Shutdown crashed, continuing");
		}

		dbglog("DLL_DETACH complete");
	}

	return TRUE;
}

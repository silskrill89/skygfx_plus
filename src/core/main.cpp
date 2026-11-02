#include "skygfx.h"
#include "main_exports.h"
#include "neopipe.h"
#include "waterPipe.h"
#include "chars.h"
#include "weather.h"
#include "sftbdyphsx.h"  // living-ped secondary motion + Bullet gib dismemberment
#include "extras/ragdoll_death.h"  // death ragdoll (bullet)
#include "entities/softbody.h"  // P1 native softbody/ragdoll lifecycle (plan 2026-10-29 §5)
#include "ini_parser.hpp"
#include "ModuleList.hpp"
#include "debug_handl.h"	// merged: logging + ring + scope tags + crash VEH/watchdog + minidump filter + native menu injection
#include "modeManager.h"  // graphics mode manager (MODE_* state)
#include "postfx.h"
#include "renderstate_monitor.h"  // RSMon_* � three-layer (rw/driver/device) state monitor
#include "forwardplus.h"  // ForwardPlus_AddLight — corona/world-light feed (see coronaFeedForwardPlus)
#include "treeWind.h"     // W8-2: TreeWind_ModifyMatrixForTreeInWind, TreeAlpha_SetProfile
#include "profiler.h"     // pass/frame profiler: g_pfEnable, PfFrame() (ini + frame hook)
#include "grasssys.h"	// W8-3: CGrassInstancer::SetEnabled (grassInstancing gate)
#include "particles_gpu.h"	// m0336: GPU particle sprite path (wake first slice)
#include "noise.h"	// Xbox-clouds lane: SkyNoise::BindAndUpload (cloud c49-c52 block @ s9)
#include "texdefs.h"	// unified texture-definition layer (texdb.txt + NM pairing + DK22Pac lane)
#include "peds/peds_pipe.h"	// peds pipe control point — Phase 2 wiring (design doc 2026-10-31 §2)
#include "peds/skin.h"		// skin sub-module — Skin_ClassifyAtomic entry wrap (design doc §3)
#include <injector\hooking.hpp>
#include <stdarg.h>
#include <stdio.h>
#include <excpt.h>

// W8-3 (w78-grass-instancer.md staged Diff D): the plant-island master switch
// comes from grasssys.h (gated by the grassInstancing INI key, default
// 0). The header chain is grasssys.h -> PC_PlantsMgr.h ->
// ColData.h/PlantSurfPropMgr.h, all class/struct declarations only, so it is
// safe in this TU; the gate stays dormant (SetEnabled(false)) until the key is
// explicitly set to 1.

// Profiler frame boundary (below): DrawFinalEffects stays the frame boundary
// and PfFrame() snaps it — one call site, no double counting.

// External normal-map plugin lane (TexDefsExternalInit/Shutdown) now lives in
// texdefs.cpp, together with the texdb.txt and _n pairing layers.

// Performance timing globals
LARGE_INTEGER perfFreq = {0};
double perfFreqInv = 0.0;

HMODULE dllModule;
char asipath[MAX_PATH];
volatile int g_allowCrashPassThrough = 0;

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

// [Modes] active= (M1a) - the mode id resolved from the ACTIVE config's INI in
// readIniFile(). MODE_NONE keeps every legacy writer on its old path, so an INI
// without [Modes] stays bit-identical to the pre-persistence build.
static ModeId s_activeModeIni = MODE_NONE;
static bool s_activeModeIniSeen = false;	// a known [Modes] active= was read

// Stable INI alias for a mode (mirrors g_modeProfiles[].iniAlias in
// modeManager.cpp - keep in sync; that file is another lane's write scope).
static const char *
modeIniAlias(ModeId id)
{
	switch(id){
	case MODE_PS2:		return "PS2";
	case MODE_XBOX:		return "Xbox";
	case MODE_PC_VINTAGE:	return "PC";
	case MODE_GTAIV:	return "GTAIV";
	case MODE_GTAV:		return "GTAV";
	case MODE_MODERN:	return "Modern";
	case MODE_CUSTOM:	return "Custom";
	case MODE_MOBILE:	return "Mobile";
	default:		return "None";
	}
}

// True when the active mode's profile owns buildingPipe/vehiclePipe/colorFilter.
// MODE_NONE and MODE_CUSTOM own no profile fields, and MODE_GTAV is water-only
// (modeManager.h) - those modes keep the legacy top-level pipe keys.
static bool
modeOwnsPipes(void)
{
	if(s_activeModeIni != MODE_NONE){
		// GTA V is water-only and Custom has no profile fields (modeManager.h):
		// their top-level pipe keys still apply.
		if(s_activeModeIni != MODE_GTAV && s_activeModeIni != MODE_CUSTOM)
			return true;
	}
	return false;
}

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

// Tells moving-array limit adjusters we read the point-light base through game code.
extern "C" __declspec(dllexport) int SkyGfx_FollowsMovedArrays(void) { return 1; }

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

	// coronaZtest is PARSE-ONLY: SilentPatchSA owns the corona z-test and
	// rewrites the bytes at 0x6FB17C itself. Patching there fought that
	// write (and case 1 restored a `call [ecx+0x20]` SilentPatch removed).
	// No bytes are written from this plugin; the INI/menu key stays valid.
	if(config->coronaZtest != -1 && dbglog_once("corona_ztest_retired"))
		dbglog("coronaZtest=%d retired - SilentPatch owns the corona z-test at 0x6FB17C (no bytes written)",
			config->coronaZtest);

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
	if(c->tonemapMaxExposure < 0.2f || c->tonemapMaxExposure > 4.0f){
		if(dbglog_throttle("tmax_clamp"))
			dbglog("tonemapMaxExposure=%.4f outside [0.2..4.0], clamping", c->tonemapMaxExposure);
		c->tonemapMaxExposure = (c->tonemapMaxExposure < 0.2f) ? 0.2f : 4.0f;
	}
	// Enforce Min <= Max AFTER both are in range (pin min = max) — HLSL
	// clamp(x, min, max) is undefined when min > max.
	if(c->tonemapMinExposure > c->tonemapMaxExposure){
		if(dbglog_throttle("texp_order"))
			dbglog("tonemapMin>Max (%.3f > %.3f), pinning min = max",
				c->tonemapMinExposure, c->tonemapMaxExposure);
		c->tonemapMinExposure = c->tonemapMaxExposure;
	}
	// User pivot curve (TonemapPass c8): identity defaults 0/0/0.5.
	// FIX-2 (black-interior audit): intensities are only sane within
	// +/-0.35 — |highs|=1.0 maps pure white -> 0 and lows=-0.55 crushes
	// shadows (user INI shipped -0.548/-1.000). Clamp here so load /
	// reload / saveConfigTo all see the bounded value (the postfx upload
	// re-asserts the same bound per draw, so debug-menu writers that
	// bypass this function cannot escape it either).
	if(c->tonemapCurveLows < -0.35f || c->tonemapCurveLows > 0.35f){
		if(dbglog_throttle("tclow_clamp"))
			dbglog("tonemapCurveLows=%.4f outside [-0.35..0.35], clamping", c->tonemapCurveLows);
		c->tonemapCurveLows = (c->tonemapCurveLows < -0.35f) ? -0.35f : 0.35f;
	}
	if(c->tonemapCurveHighs < -0.35f || c->tonemapCurveHighs > 0.35f){
		if(dbglog_throttle("tchig_clamp"))
			dbglog("tonemapCurveHighs=%.4f outside [-0.35..0.35], clamping", c->tonemapCurveHighs);
		c->tonemapCurveHighs = (c->tonemapCurveHighs < -0.35f) ? -0.35f : 0.35f;
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
	if(!c){
		// FIX-4 blind-spot diagnostics: a silent early-return here means the
		// heal can never repair a stomp, and the absence of any
		// "[Config] ... diverged" line in the log becomes ambiguous (was the
		// flag never written, or is the heal blind?). Throttled so the
		// every-frame caller (derivePipelineFromPipes :580) stays quiet.
		if(dbglog_throttle("cfenable_guard"))
			dbglog("[Config] cfEnableHeal: NULL config — early-return, heal is blind");
		return;
	}
	int slot = (int)(c - configs);
	if(slot < 0 || slot >= 10){
		if(dbglog_throttle("cfenable_guard"))
			dbglog("[Config] cfEnableHeal: slot %d outside configs[0..10) — early-return, heal is blind", slot);
		return;
	}
	if(!s_cfEnableSeen[slot]){
		if(dbglog_throttle("cfenable_guard"))
			dbglog("[Config] cfEnableHeal: slot %d has no recorded user intent (s_cfEnableSeen=0) — early-return, heal is blind", slot);
		return;
	}
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
// ivMode: NOT re-derived here. The mode manager (modeManager.cpp) owns the
// field via whole-Config snapshots, and explicit writes (ImGui checkbox,
// legacy var, menu_inject toggle, explicit INI read) are user intent —
// per-frame re-derivation fought both. This function never writes ivMode.
// Trigger breadcrumb for the atomic re-derive receipt below (log-forensics
// 2026-10-30 #1: `pipeline re-derived 2 -> 0` at 04:38:48 with NO trigger
// logged). main.cpp call sites (refreshIni, readIniFile) set this around
// their derive call and clear it afterwards; the every-frame caller
// (postfx.cpp ColourFilter_switch) cannot, so a change without a tag is
// attributed as an external pipe write.
static const char *s_pipeDeriveTrigger = NULL;

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

	// Pre-derive snapshot for the single atomic receipt at the end: all
	// old->new values are logged in ONE line after every write (pipeline +
	// the PBR-lock pipe/filter writes), so a re-derive is never observable
	// in the log as a partial state.
	int oldPipe = c->pipeline;
	int oldBp = c->buildingPipe;
	int oldVp = c->vehiclePipe;
	int oldCf = c->colorFilter;
	// External-write detection: the values this function left at the end of
	// its previous call on THIS Config object vs. what it sees now. A drift
	// with no trigger tag = a writer outside main.cpp (debug-menu setting
	// row / mode manager / preset apply) changed a pipe between frames —
	// the exact 2->0->2 flip shape from the 04:38 run (buildingPipe
	// 1->3->1 with vehiclePipe untouched).
	static Config *s_prevExitCfg = NULL;
	static int s_prevExitBp = -9999, s_prevExitVp = -9999;
	bool externalWrite = (c == s_prevExitCfg) &&
	                     (oldBp != s_prevExitBp || oldVp != s_prevExitVp);

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

	if(c->pipeline != derived)
		c->pipeline = derived;

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
	// M1a / E3: the mode profile owns the LIVE config's pipes/colorFilter.
	// While a pipe-owning mode is active keep the derived `pipeline` mirror above
	// but never rewrite the profile's pipes/filter (the PBR lock would otherwise
	// undo PS2/Xbox/GTAIV via the every-frame ColourFilter_switch path). Other
	// config slots are not mode-owned and keep the legacy lock.
	if(derived == PIPELINE_PBR && rule == 3 &&
	   !(c == config && modeOwnsPipes())){
		if(c->buildingPipe != BUILDING_PBR)
			c->buildingPipe = BUILDING_PBR;
		if(c->vehiclePipe != CAR_MODERN)
			c->vehiclePipe = CAR_MODERN;
		c->colorFilter = COLORFILTER_MODERN;
	}

	// ---- atomic re-derive receipt (log-forensics 2026-10-30 #1) ----
	// One [Config] line per re-derive, EVERY time (never throttled — the2s
	// "pipe_derive" bucket starved the counterpart of the 04:38:48
	// `pipeline re-derived 2 -> 0` flip in the 04:38 run), with the trigger
	// source and old->new for pipeline AND the pipe/filter fields, emitted
	// only after every write above landed (atomic view — no partial state
	// is ever logged). Trigger attribution: main.cpp call sites tag
	// s_pipeDeriveTrigger around their derive call (refreshIni /
	// readIniFile); the every-frame ColourFilter_switch caller cannot, so
	// an untagged change with pipe drift is an EXTERNAL writer (debug-menu
	// setting row / mode manager / preset apply) — the exact 2->0->2 flip
	// shape (buildingPipe 1->3->1, vehiclePipe untouched).
	s_prevExitCfg = c;
	s_prevExitBp = c->buildingPipe;
	s_prevExitVp = c->vehiclePipe;
	if(oldPipe != c->pipeline || oldBp != c->buildingPipe ||
	   oldVp != c->vehiclePipe || oldCf != c->colorFilter){
		const char *trig = s_pipeDeriveTrigger;
		if(!trig)
			trig = externalWrite
				? "per-frame (ColourFilter_switch) + EXTERNAL pipe write (menu row / mode manager / preset)"
				: "per-frame (ColourFilter_switch)";
		dbglog("[Config] pipeline re-derived %d -> %d (buildingPipe=%d vehiclePipe=%d) [atomic: bp %d->%d vp %d->%d cf %d->%d rule=%d trigger=%s]",
		       oldPipe, c->pipeline, c->buildingPipe, c->vehiclePipe,
		       oldBp, c->buildingPipe, oldVp, c->vehiclePipe,
		       oldCf, c->colorFilter, rule, trig);
		// Pipeline nibble in the [#frame:code] log prefix is latched per
		// frame-id advance — force immediate re-derive so the prefix never
		// shows a stale mode after a mid-frame pipeline switch.
		DebugInvalidatePipelineCache();
	}

	// ivMode is NOT re-derived from pipes here anymore (see header): the
	// mode manager owns it via whole-Config snapshots and explicit user
	// writes (checkbox / legacy var / menu_inject / INI) are deliberate.
}

// ===== Utility Noise Texture quality knob (global, not per-config) =====
// [General] NoiseQuality: 0->32, 1->128, 2->512, >=3->1024, default 0.
// Parsed in readIniFile below, round-tripped by saveConfigTo, and reached
// from the in-game menu through menuGet/SetNoiseQuality (near the hooks
// below). The noise tile itself is owned by SkyNoise now (built lazily by
// pipeBindNoise); the old utility-noise regen refreshIni used to run is
// gone (stages-0-3 L4).
static int g_noiseQuality = 0;

void
refreshIni(void)
{
	clampEnvSliderRanges(config);		// re-assert after every reload/setConfig
	clampTonemapExposureRanges(config);
	clampSsaoPower(config);
	clampChromeParams(config);

	// L4: utility-noise regen removed (SkyNoise owns the tile) - the RK_NOISEQ "regenerates on F11" tooltip in debugmenu_ui.cpp is now stale (L3's file).

	// Pipe-conjunction #4 piggyback: every reload/setConfig/named-config
	// load re-derives pipeline/colorFilter from the actual pipes —
	// this is also what covers ApplyPreset/IVMode/menu_inject writers without
	// editing their (out-of-scope) files. ivMode is NOT re-derived here
	// anymore (modeManager owns it — see derivePipelineFromPipes header).
	s_pipeDeriveTrigger = "refreshIni (INI reload / setConfig / named-config)";
	derivePipelineFromPipes(config);
	s_pipeDeriveTrigger = NULL;
	// Profiler switches — keep the live globals in sync with the ACTIVE config
	// (readIniFile applies them itself for the single-config startup path; this
	// covers config switching / named-config loads).
	g_pfEnable = config->profilerEnable ? 1 : 0;
	g_pfGpuTimers = config->profilerGpuTimers ? 1 : 0;
	// Tree alpha profile + grass instancing — same rule as the profiler
	// switches above: readIniFile() only pushes these for c == config, so a
	// multi-slot reload/setConfig leaves the live gates to this re-apply.
	TreeAlpha_SetProfile(config->treeFx);
	CGrassInstancer::SetEnabled(config->grassInstancing != 0);
	resetValues();
}


RpAtomic *(*plantTab0)[4] = (RpAtomic *(*)[4])0xC039F0;
RpAtomic *(*plantTab1)[4] = (RpAtomic *(*)[4])0xC03A00;

RpAtomic*
grassRenderCallback(RpAtomic *atomic)
{
	RpAtomic *ret;
	int cullPatched = 0;
	RwRGBAReal color = { 0.0f, 0.0, 1.0f, 1.0f };

	// §4.1 grasssys.cpp: repair device Z drift before the plant legs.
	// PERF (2026-11-01): the contract is idempotent within a render frame,
	// but this callback runs once per PLANT ATOMIC (hundreds/frame), so the
	// ~dozens of state round-trips inside GrassEnforceDepthContract used to
	// run hundreds of times per frame. Frame-latch it: run the contract only
	// on the first grass atomic of each frame. DebugFrameId() is the widened
	// RWSRCGLOBAL(renderFrame) tick — the same per-rendered-frame counter
	// softbody.cpp gates its simulate/slerp slots on — so mirror/shadow
	// re-entries of the same frame share one latch hit. POD static (constant
	// init, DLL context); sentinel = always fire on the very first frame.
	static unsigned s_lastGrassZFrame = 0xFFFFFFFFu;
	unsigned grassFrameId = DebugFrameId();
	if(grassFrameId != s_lastGrassZFrame){
		GrassEnforceDepthContract();
		s_lastGrassZFrame = grassFrameId;
	}

	if(config->ps2ModulateGrass){
		gpCurrentPixelShaderForDefaultCallbacks = grassPixelShader;
		RpGeometry *g = atomic->geometry;
		RpMaterial *m0 = (g && g->matList.numMaterials > 0) ? g->matList.materials[0] : NULL;
		if(m0){
			RwRGBARealFromRwRGBA(&color, &m0->color);
			if(color.red == 0.0f && color.green == 0.0f && color.blue == 0.0f && dbglog_throttle("veg_c0"))
				dbglog("[veg] grassRenderCallback: ZERO modulate c0 (mat colour black — tmpintensity/saveIntensity chain), tex='%s'",
				       (m0->texture && m0->texture->name[0]) ? m0->texture->name : "(null)");
		}
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
	if(pipe == NULL){
		// NULL-pipe fallback (audit): the RW engine instance at 0xC97B24 (the
		// same global texdefs.cpp polls before its deferred attach) is NULL
		// until RwEngineInit completes — the unguarded deref crashed there
		// instead of falling back. Guard the whole chain; a NULL after both
		// attempts takes the early-out below, which makes the pre-existing
		// `ret = pipe ? atomic : NULL` contract actually reachable instead of
		// crashing inside RxPipelineExecute.
		DWORD engine = *(DWORD*)0xC97B24;
		if(engine)
			pipe = *(RxPipeline**)(engine + 0x3C + dword_C9BC60);
	}
	if(pipe == NULL){
		// Early-out: restore exactly what this callback wrote so far — the
		// cull patch (contract: CULLNONE is never left behind) and the grass
		// shader pin (same unconditional clear as the normal exit below).
		// No two-layer zwrite/alphatest state was touched yet: the dual-pass
		// branch below is the first writer of those.
		if(cullPatched)
			RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
		gpCurrentPixelShaderForDefaultCallbacks = NULL;
		return NULL;
	}
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
//                        L1+L2+L3; alphaTestEnable 0 disables the test
//   pedOpaqueRestore()   per-layer exit restore; ZWRITE deliberately excluded
//   [PedRS] monitor      inline exit block in myDefaultCallback: throttled
//                        3-layer readback of cull/blend/src/dst/zw/afunc/aref
//
// INVARIANTS:
//   * Enforce is the LAST writer before the skin pipeline: it runs after
//     pipeEnterAlphaMode + pipeForceAlphaBlock + the saved alpha-pair restore.
//   * Restore runs after pipeExitAlphaMode. D3DRS_ZWRITEENABLE belongs to the
//     sticky three-layer ZWRITE repair, never to pedOpaqueRestore.
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
	// CULLMODE is deliberately absent: with pedCullMode == -1 (default) the
	// ped span inherits the game's own raw saved cull; the only cull writers
	// are pipeEnterCullMode() and the gated pedCullMode override in the
	// myDefaultCallback entry block.
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
//                             the four game-entry wrappers
//
// INVARIANTS:
//   * The two call-site intercepts are byte-verified (E8 + target + zero-arg)
//     and both function-entry hooks are prologue-verified (0x708300:
//     A1 24 7B C9 00, 0x706AB0: 51 53 57 8B F9 + 5-byte trampoline); each is
//     skipped on mismatch, so the vanilla shadow path survives a bad exe.
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
//   src/core/main.cpp   DllMain shadow install block: call sites 0x53E0BE/
//                       0x53E0C8 (expects 0x707F40/0x70A960) + the verified
//                       0x708300 (RenderStaticShadows) and 0x706AB0
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
static void
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
		RwTexture *resolved = recovered ? recovered : gpWhiteTexture;
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
			       "op=ADD zwrite=1 ztest=1 atest=1 afunc=GE aref=1) "
			       "via L1+L2+L3");
		}
	}else if(isPedPipe && config->dualPassPed && config->zwriteThresholdPed > 0){
		dodual = 1;
		// PedsPipe absorb (design doc §3.1 — SkinDrawDual gate): a
		// classified state vetoes the dual pass for atomics whose
		// dualPassMask bit0 is clear (bit set = needs the second leg).
		// Stub classification never sets the mask, so the config-only
		// legacy gate is the fallback and byte-identical.
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
		// opaque contract capture/enforce). The whole entry block below runs
		// under module gating via pedsPipeState (acquired above; NULL =
		// legacy fallback, config/static-driven, byte-identical). Phase 3
		// routes the per-clump contract latch through PedPipeState once
		// skin.cpp owns SkinEntryContract.
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

		// ===== pedCullMode A/B lever =====
		// -1 (default) keeps exactly what pipeEnterCullMode just forced: the
		// sanitized RAW saved mode the game had on entry — no extra CULLMODE
		// write. 1/2/3 force CULLNONE/CULLBACK/CULLFRONT through all three
		// layers for this ped span; the exit's pipeExitCullMode(savedCull)
		// restores the raw pre-entry value. Out-of-range behaves as -1.
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
			pipeForceCullMode(pedCullForced);
		}
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
		zwriteWasDesync = (!savedZWriteRw || !savedZWriteDev);
		if(zwriteWasDesync){
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
		if(pedContractOn){
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
			pedOpaqueEnforce(pedAlphaTestOn ? 1 : 0);
		}

		// Stage-0 texture-raster resync: the skin pipeline's per-material
		// bind must be a real transition (cache<->device split repair).
		// INI pedTexResync (default 1 = ON): 0 = skip the park/set resync —
		// bisect lever for the ped stage-0 texture repair.
		if(!config || config->pedTexResync)
			pedStage0Resync(atomic, pedsPipeClassified ? pedsPipeState : NULL);
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
	// with the game's own ambient, no pedAmbientFloor hue-preserving lift,
	// no cutscene 0.15 special. The 0.20 floor saturated the FFP skin pipe
	// (final ≈ pAmbient + pDirect·NdotL) to flat white at noon.
	RwRGBAReal savedAmb = {};
	bool ambOverridden = false;
	if(pipe == skinPipe && pAmbient){
		// PedsPipe absorb point (design doc §3.1 — SkinAmbientOverride):
		// the saved-capture / black-guard / restore triplet below runs
		// under module gating via pedsPipeState (acquired at entry; NULL =
		// legacy fallback, byte-identical). Phase 3 routes the override
		// latch through PedPipeState once skin.cpp owns the ambient
		// override; the ped_ambient receipts stay on this path either way.
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
	if(isPedPipe){
		extern void chars_logHeadPartReceipt(const RpAtomic *atomic);
		chars_logHeadPartReceipt(atomic);
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
		if(isPedPipe){
			// PedsPipe absorb (design doc §3.1 — SkinDrawDual state lane):
			// record the leg progression in the per-clump state when the
			// module owns this atomic (diagnostic writes only — the legacy
			// sequence below is untouched and stays byte-identical).
			if(pedsPipeState){
				pedsPipeState->dualPassActive = 1;	// leg 1: GREATEREQUAL@zt ZWRITE-on
				pedsPipeState->dualZTest = (float)zt;
				pedsPipeState->lastDualFrame = *(uint32_t*)0xB7CB4C;	// CTimer::m_FrameCounter
			}
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
			if(pedsPipeState)
				pedsPipeState->dualPassActive = 2;	// leg 2: LESS@zt ZWRITE-off + blend-on
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)blendSaved);
			RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, blendSaved ? TRUE : FALSE);
			setZWriteTwoLayer(TRUE);
			setAlphaTestTwoLayer((RwUInt32)alphatest, alpharef);
			if(pedsPipeState)
				pedsPipeState->dualPassActive = 0;	// both legs done, restored
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
			// fix-34: contract skipped (INI-gated off) — the entry's
			// saved-pair restore (:2212-2219) still pushed the PRE-ENTRY
			// pair through L1+L2+raw L3, which is exactly how envmap's
			// leaked ref=140 (envmap.cpp:448 outdoor park) reaches and
			// stays on the device, and pipeExitAlphaMode heals only the
			// rw-cache side. Sticky-repair rule (same as the ZWRITE block
			// above: never faithfully re-create a known leak): reset the
			// canonical GREATEREQUAL@1 through ALL THREE layers so the
			// alpharef/alphafunc contract no longer depends on the INI
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

		// ===== [PedRS] exit state monitor =====
		// PedsPipe absorb point (design doc §3.1 — SkinExitContract lane):
		// the exit restore above plus this 21-read triplet monitor run
		// under module gating via pedsPipeState (acquired at entry; NULL =
		// legacy fallback, byte-identical). Phase 3 routes the exit
		// contract through PedPipeState once skin.cpp owns
		// SkinExitContract; the ped_rs receipt stays on this path.
		//
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
	}

	// FIX 3: sample the vertex stage at ped/default-pipe callback exit.
	vs_probe("ped_exit", "vs_probe_ped");
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
		// Pre-seeded (never bare): a failed RwRenderStateGet leaves the
		// out-param untouched, and the restore below pushes this straight
		// back — an uninitialised ref must never reach the device. Same
		// convention as D3D9RenderDual / grassRenderCallback.
		int alpharef = 1;
		RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)&alpharef);
		if(alpharef < 0) alpharef = 0;
		if(alpharef > 255) alpharef = 255;
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
	RwUInt32 *shader = resource ? (RwUInt32*)LoadResource(dllModule, resource) : NULL;
	if(shader == NULL){
		dbglog("[veg] CPlantMgr_Initialise: IDR_GRASSPS load FAILED — plants fall back to FFP");
		grassPixelShader = NULL;
	} else {
		RwD3D9CreatePixelShader(shader, &grassPixelShader);
		FreeResource(shader);
	}

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

// ============================================================================
// SUBSYSTEM: CORONA / SPRITE-STATE WINDOW   (extraction: weather/corona.cpp)
// ============================================================================
// Scope-isolated for a mechanical later extraction. Everything below through
// CoronaWindow_Close, plus CCoronas__Render_blendprobe_hook and the install
// sites named at the bottom, belongs to this subsystem. Do not add unrelated
// code inside this region.
//
// GLOBALS (file-scope POD, render thread only):
//   pointLights             live CPointLights entry base. Read through the
//                           immediate of the game's own `lea eax,[ecx+0xC3F0E0]`
//                           at 0x7001FD (bytes 8D 81 E0 F0 C3 00, 1.0 US), so
//                           an array-moving limit adjuster is followed.
//   g_inCoronasRender       window open: producer entry .. first post-body flush
//   g_coronaBodyDone        body finished; the next buffered flush closes it
//   g_inCloudsRender        producer flag: flush picks ONE/ONE for CClouds
//   g_coronaPreValid        pre-window capture completed this window
//   g_coronaPreFuncRw/RefRw, g_coronaPreFuncDev/RefDev
//                           pre-window ALPHAFUNC/ALPHAREF (L1 + L3 snapshots)
//   g_coronaPreSepAlpha, g_coronaPreColorWrite
//                           pre-window raw-only state snapshots
//   g_coronaPreBlend/SrcBlend/DstBlend/BlendOp/ZWrite
//                           pre-window blend block (device snapshot)
//   currentLight, stkp      asm ABI carriers for the 0x700B6B wrapper
//
// ENTRY POINTS:
//   coronaRwBlendFromD3D()                 D3DBLEND -> rwBlend mapping
//   coronaDrawRawShield()                  per-draw park-and-set, L1+L2+L3
//   SpriteFlushIndexedDraw_hook()          flush draw entries 0x70CF62/0x70CF9B;
//                                          owns force-shield + window close
//   CPointLights__Render_shield_hook()     re-arms at 0x53E21D and 0x53DE6B
//   CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect()
//                                          0x700B6B point-light colour fix
//   CCoronas__Render_blendprobe_hook()     0x6FAEC0 entry/exit shield (defined
//                                          later in the "CCoronas::Render
//                                          entry blend shield" region)
//   CoronaWindow_Close()                   frame-boundary safety close
//
// INVARIANTS:
//   * Window opens at producer entry, closes at the FIRST post-body flush
//     (g_coronaBodyDone), never at producer exit. SilentPatchSA defers the
//     corona batch past CCoronas::Render, so the window must survive the return.
//   * Every draw inside the window is repaired by coronaDrawRawShield
//     immediately before rasterisation; the buffered flush forces the block
//     for a non-empty batch owned by a corona/pointlight or cloud producer,
//     and spriteFlushShieldClose() replays the captured pre-force state
//     right after that draw.
//   * The close runs the canonical world resync (blend ON, SRCALPHA/
//     INVSRCALPHA, BLENDOP ADD, ZWRITE/ZTEST ON) through L1+L2+L3.
//   * Flags are POD and unconditionally re-armed at each producer entry; a
//     fault path therefore leaves a bounded state that the next flush or
//     CoronaWindow_Close clears.
//
// FUTURE weather/corona.cpp PUBLIC API (exact exports):
//   void CoronaWindow_Close(void);
//   void CoronaWindow_InstallHooks(void);               // NEW: owns installs
//   void CoronaWindow_SetCloudProducer(bool inClouds);  // NEW: CClouds hygiene
//   void CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect(...);
//
// MOVES TOGETHER (copy-paste set):
//   src/core/main.cpp   this region (PointLight .. CoronaWindow_Close)
//   src/core/main.cpp   CCoronas__Render_blendprobe_hook + trampoline/orig
//   src/core/main.cpp   installs: 0x6FAEC0 entry, 0x70CF62/0x70CF9B flush,
//                       0x53E21D/0x53DE6B CPointLights, 0x700B6B CSprite
//   src/skygfx.h        CoronaWindow_Close declaration
//   src/core/main_exports.h  CSprite wrapper declaration
//   Consumers stay put: depthhook.cpp CoronaWindow_Close call; main.cpp
//   RenderScene_before / effects-exit CoronaWindow_Close calls.
// ============================================================================
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

// Point-light base is read from the game's own instruction immediate, not a
// hardcoded address: 0x7001FD is `lea eax,[ecx+0xC3F0E0]` (bytes
// 8D 81 E0 F0 C3 00 in gta_sa.exe 1.0 US); the imm32 lives at 0x7001FF. A
// limit adjuster that moves the array patches that immediate, so binding the
// reference to the operand follows the moved base automatically.
PointLight *&pointLights = *(PointLight**)(0x7001FD + 2);

// ---- Corona draw-time raw shield --------------------------------------
// Scope flag: TRUE from the top of CCoronas__Render_blendprobe_hook until
// the buffered-flush that rasterises the corona batch. With vanilla RW the
// flush runs as CCoronas::Render's tail (0x6FB621 tail-jmp -> 0x70CF20),
// i.e. inside this window; with SilentPatchSA loaded that tail-jmp is
// word-patched to a RET (SP init: mov word [0x6FB621],0xE8C3) so the batch
// persists and is drawn by the NEXT sprite consumer's flush — AFTER this
// hook returned. The window therefore CLOSES in SpriteFlushIndexedDraw_hook
// (first flush after the body — g_coronaBodyDone), NOT at Render exit.
// Draw entries below raw-force the corona additive block (blend enable /
// ONE/ONE / ZWRITE off) plus ALPHAFUNC/ALPHAREF/SEPARATEALPHABLENDENABLE/
// COLORWRITEENABLE when the flag is set. The buffered-sprite flush forces
// the ONE/ONE additive block for EVERY non-empty batch (force=true) so
// sprites produced outside the window — the early CPointLights::Render call
// 0x53DE6B — are covered, including the SilentPatchSA-deferred batch whose
// shield window is already closed when the real rasterisation happens.
// Every forced batch is closed immediately after its draw by
// spriteFlushShieldClose(), so the force never survives its own raster.
static bool g_inCoronasRender = false;
// TRUE from the END of the CCoronas::Render trampoline body (or from the
// START of the CPointLights body — its tail flush runs inside its own orig
// call) until the first buffered-flush afterwards; the flush hook uses it to
// close the window. Remains FALSE if the trampoline faults — see the flush
// hook.
static bool g_coronaBodyDone = false;
// Producer flag for the flush-boundary shield: TRUE only while the
// CClouds::Render original is on the stack. CClouds natively draws its
// cloud sprites ADDITIVELY (ONE/ONE) through the SAME RW TempBuffer flush
// the corona/pointlight path uses. Both producers now force the same
// ONE/ONE pair; this flag only selects the close-receipt label
// ("/clouds" vs "/corona") and documents which producer owned the batch.
// POD, render-thread only; armed/cleared in CClouds__Render_hygiene.
static bool g_inCloudsRender = false;

// ---- Pre-window state captured at the CCoronas hook entry ---------------
// The entry "normalize" block used to force ALPHAFUNC=GREATEREQUAL /
// ALPHAREF=0 / SEPARATEALPHABLENDENABLE=0 / COLORWRITEENABLE=0xF ONCE per
// window and never restore them, leaking GREATER-EQUAL@0, a blanking colour
// mask and separate-alpha blend into the rest of the frame. Those writes now
// run per-draw inside coronaDrawRawShield (immediately before each
// rasterisation), and the PRE-WINDOW values are captured here at the
// CCoronas::Render entry, then put back at the hook exit and by
// CoronaWindow_Close (frame-boundary safety net). func/ref are captured on
// both layers: rw cache (L1 source) and device (L2/L3 source); the two
// raw-only states have no rw render state at all, so the device value is
// their only snapshot. All POD file-scope statics — C2712-safe.
static bool  g_coronaPreValid = false;	// capture completed this window
static int   g_coronaPreFuncRw = rwALPHATESTFUNCTIONGREATEREQUAL;
static int   g_coronaPreRefRw = 0;
static DWORD g_coronaPreFuncDev = (DWORD)D3DCMP_GREATEREQUAL;
static DWORD g_coronaPreRefDev = 0;
static DWORD g_coronaPreSepAlpha = 0;
static DWORD g_coronaPreColorWrite = 0xF;
// Pre-window BLEND block (device values captured at the CCoronas::Render
// entry, before the entry shield / per-draw shield overwrite them). The
// exit restore used to hard-set ALPHABLENDENABLE=FALSE (REPLACE), which is
// what manufactured the next frame's blend-off black quads; it now puts
// back the value the device actually held when the window opened. SRC/
// DEST/BLENDOP/ZWRITE are captured alongside (already fetched by the same
// entry probe) so the exit log reports the true pre-window state.
// File-scope PODs — C2712-safe.
static DWORD g_coronaPreBlend = TRUE;	// D3DRS_ALPHABLENDENABLE
static DWORD g_coronaPreSrcBlend = (DWORD)D3DBLEND_SRCALPHA;
static DWORD g_coronaPreDstBlend = (DWORD)D3DBLEND_INVSRCALPHA;
static DWORD g_coronaPreBlendOp = (DWORD)D3DBLENDOP_ADD;
static DWORD g_coronaPreZWrite = TRUE;

// Force an additive block through ALL THREE layers at the draw boundary
// (PARK-AND-SET — pipeForceCullMode idiom, same as the corona entry shield):
// for ALPHABLENDENABLE / SRCBLEND / DESTBLEND / ZWRITEENABLE the OPPOSITE
// value goes through L1 (rw cache) + L2 (RwD3D9 driver cache) FIRST — a
// guaranteed cache TRANSITION even when a cache already holds the target
// (the value-compare double-no-op that let vanilla body-internal transitions
// flush blend=0 (REPLACE) to the device while the caches still claimed
// additive) — then the TARGET through L1+L2, then raw L3 (repairs device
// drift the caches can't see). No draw happens between park and set, so the
// transient never reaches a rasteriser. BLENDOP has no rw render state (same
// rule as the entry shield) — it rides L2+L3 only. This is the per-draw
// boundary repair: it runs immediately before each rasterisation inside the
// shield window, so vanilla mid-body state transitions can no longer leave
// the following quads blend-off/black. Seeding the rw cache with the
// additive block also makes any consumer whose entry contract DIFFERS
// transition instead of no-op'ing (the cache<->device split fix).
// ADDITIVE PAIR: the default is ONE/ONE — the vanilla CCoronas /
// CPointLights / CClouds contract (all three set rwBLENDONE for both
// factors). Their glow textures carry a black RGB surround with opaque
// alpha; SRCALPHA would write that surround (the night-lamp black
// rectangle). SpriteFlushIndexedDraw_hook passes the same ONE/ONE pair
// explicitly for clouds.
// The park values are derived from the targets (src parks INVSRCALPHA,
// dst parks ZERO for a ONE target), so a cache-hit can never skip the
// transition for this pair.
static RwUInt32
coronaRwBlendFromD3D(DWORD d3db)
{
	switch(d3db){
	case D3DBLEND_ZERO:        return rwBLENDZERO;
	case D3DBLEND_ONE:         return rwBLENDONE;
	case D3DBLEND_INVSRCALPHA: return rwBLENDINVSRCALPHA;
	case D3DBLEND_SRCALPHA:
	default:                   return rwBLENDSRCALPHA;
	}
}

static void coronaDrawRawShield(const char *where, bool force = false,
                                DWORD srcBlend = (DWORD)D3DBLEND_ONE,
                                DWORD dstBlend = (DWORD)D3DBLEND_ONE)
{
	if((!g_inCoronasRender && !force) || !d3d9device)
		return;
	// DIAG (first-hit one-shot + throttled bucket "corona_draw"): read the
	// device BEFORE the forced write — the field log proves the mid-body
	// inversion (device sitting on REPLACE: blend=0, or src/dst wrong) at
	// the draw boundary. dbglog_once is not time-based, so the run's first
	// [CoronaDraw] can never be starved by a throttle-bucket collision.
	if(dbglog_once("corona_draw_first") || dbglog_throttle("corona_draw")){
		DWORD blendEn = 0, srcB = 0, dstB = 0;
		d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &blendEn);
		d3d9device->GetRenderState(D3DRS_SRCBLEND, &srcB);
		d3d9device->GetRenderState(D3DRS_DESTBLEND, &dstB);
		dbglog("[CoronaDraw] pre-force dev blend=%d src=%d dst=%d at %s "
		       "(D3DBLEND: 2=ONE 5=SRCALPHA 6=INVSRCALPHA; black-quad=blend 0)",
		       (int)blendEn, (int)srcB, (int)dstB, where);
	}
	// INI coronaBlendShield (default 1 = ON) gates ONLY the blend/ZWRITE
	// force below — the black-quad repair. 0 = leave the authored additive
	// pair (and ZWRITE) untouched. The spriteStateShield four-state block
	// further down has its own independent key.
	if(!config || config->coronaBlendShield){
		// ALPHABLENDENABLE: park FALSE -> TRUE
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
		RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		// SRCBLEND: park INVSRCALPHA (ZERO if that is the target) -> target
		DWORD parkSrc = (srcBlend == (DWORD)D3DBLEND_INVSRCALPHA)
			? (DWORD)D3DBLEND_ZERO : (DWORD)D3DBLEND_INVSRCALPHA;
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)coronaRwBlendFromD3D(parkSrc));
		RwD3D9SetRenderState(D3DRS_SRCBLEND, parkSrc);
		RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)coronaRwBlendFromD3D(srcBlend));
		RwD3D9SetRenderState(D3DRS_SRCBLEND, srcBlend);
		d3d9device->SetRenderState(D3DRS_SRCBLEND, srcBlend);
		// DESTBLEND: park ZERO (ONE if that is the target) -> target
		DWORD parkDst = (dstBlend == (DWORD)D3DBLEND_ZERO)
			? (DWORD)D3DBLEND_ONE : (DWORD)D3DBLEND_ZERO;
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)coronaRwBlendFromD3D(parkDst));
		RwD3D9SetRenderState(D3DRS_DESTBLEND, parkDst);
		RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)coronaRwBlendFromD3D(dstBlend));
		RwD3D9SetRenderState(D3DRS_DESTBLEND, dstBlend);
		d3d9device->SetRenderState(D3DRS_DESTBLEND, dstBlend);
		// ZWRITEENABLE: park TRUE -> FALSE
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
		RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	}
	// ALPHAFUNC/ALPHAREF + the two raw-only states (SEPARATEALPHABLENDENABLE,
	// COLORWRITEENABLE): applied PER DRAW here, immediately before the
	// rasterisation, instead of once per window at the CCoronas entry. The
	// old entry normalize left them live for the rest of the frame
	// (GREATER-EQUAL@0 kills the soft falloff of every later alpha-tested
	// draw, a blanking colour mask drops RGB, separate-alpha rewrites
	// destination alpha). Same layers as the old entry block: L1 (rw cache,
	// where an rw state exists) + L2 + raw L3. Canonical corona values only —
	// blend FACTORS are never touched here (additive pair stays as-is).
	// INI spriteStateShield=0 skips ONLY this four-state normalisation; the
	// blend/ZWRITE force above always runs (it is the black-quad repair).
	if(!config || config->spriteStateShield){
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONGREATEREQUAL);
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)0);
		RwD3D9SetRenderState(D3DRS_ALPHAFUNC, (DWORD)D3DCMP_GREATEREQUAL);
		RwD3D9SetRenderState(D3DRS_ALPHAREF, 0);
		RwD3D9SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, 0);
		RwD3D9SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
		d3d9device->SetRenderState(D3DRS_ALPHAFUNC, (DWORD)D3DCMP_GREATEREQUAL);
		d3d9device->SetRenderState(D3DRS_ALPHAREF, 0);
		d3d9device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, 0);
		d3d9device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
	}
	// BLENDOP: no rw twin — L2+L3 only (same rule as the entry shield).
	// Part of the same coronaBlendShield force (default 1 = ON).
	if(!config || config->coronaBlendShield){
		RwD3D9SetRenderState(D3DRS_BLENDOP, (DWORD)D3DBLENDOP_ADD);
		d3d9device->SetRenderState(D3DRS_BLENDOP, (DWORD)D3DBLENDOP_ADD);
	}
}

// ---- Flush-shield pre-force snapshot (file-scope POD, render thread) ----
// The flush-boundary force writes NINE states through L1 (rw cache) + L2
// (driver cache) + raw L3, so the rw cache itself ends up holding the
// corona block (blend ON, ONE/ONE, ZWRITE off). The undo used to sit
// inside the g_coronaBodyDone branch, which is armed only by the
// CCoronas/CPointLights bodies — cloud / sun-reflection / water-reflection
// / HUD / deferred mid-world batches were force-written and never closed.
// Every layer is captured here BEFORE the force and replayed by
// spriteFlushShieldClose() immediately after the draw. Pre-seeded with the
// world-canonical signature so a failed Get can never replay garbage.
struct SpriteFlushState
{
	DWORD blendEn, srcBlend, dstBlend, zwrite, alphaFunc, alphaRef;
	DWORD colorWrite, sepAlpha, blendOp;					// L3 raw device
	DWORD drvBlendEn, drvSrcBlend, drvDstBlend, drvZwrite;	// L2 driver cache
	DWORD drvAlphaFunc, drvAlphaRef, drvColorWrite, drvSepAlpha, drvBlendOp;
	RwUInt32 rwBlendEn, rwSrcBlend, rwDstBlend, rwZwrite;	// L1 rw cache
	RwUInt32 rwAlphaFunc, rwAlphaRef;
};
static SpriteFlushState s_spriteFlushState;
static bool s_spriteForceValid = false;

static void
spriteFlushShieldCapture(void)
{
	SpriteFlushState &s = s_spriteFlushState;
	// World-canonical pre-seed: blend ON, SRCALPHA/INVSRCALPHA, ZWRITE TRUE,
	// ALPHAFUNC GREATEREQUAL, ALPHAREF 2, CWR 0xF, sepA FALSE, BLENDOP ADD.
	s.blendEn = TRUE;
	s.srcBlend = (DWORD)D3DBLEND_SRCALPHA;
	s.dstBlend = (DWORD)D3DBLEND_INVSRCALPHA;
	s.zwrite = TRUE;
	s.alphaFunc = (DWORD)D3DCMP_GREATEREQUAL;
	s.alphaRef = 2;
	s.colorWrite = 0xF;
	s.sepAlpha = FALSE;
	s.blendOp = (DWORD)D3DBLENDOP_ADD;
	s.drvBlendEn = s.blendEn;
	s.drvSrcBlend = s.srcBlend;
	s.drvDstBlend = s.dstBlend;
	s.drvZwrite = s.zwrite;
	s.drvAlphaFunc = s.alphaFunc;
	s.drvAlphaRef = s.alphaRef;
	s.drvColorWrite = s.colorWrite;
	s.drvSepAlpha = s.sepAlpha;
	s.drvBlendOp = s.blendOp;
	s.rwBlendEn = (RwUInt32)TRUE;
	s.rwSrcBlend = (RwUInt32)rwBLENDSRCALPHA;
	s.rwDstBlend = (RwUInt32)rwBLENDINVSRCALPHA;
	s.rwZwrite = (RwUInt32)TRUE;
	s.rwAlphaFunc = (RwUInt32)rwALPHATESTFUNCTIONGREATEREQUAL;
	s.rwAlphaRef = (RwUInt32)2;
	// L1 (rw cache) — only the six states that have an rw twin. COLORWRITE-
	// ENABLE, SEPARATEALPHABLENDENABLE and BLENDOP have none.
	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &s.rwBlendEn);
	RwRenderStateGet(rwRENDERSTATESRCBLEND, &s.rwSrcBlend);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, &s.rwDstBlend);
	RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &s.rwZwrite);
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &s.rwAlphaFunc);
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, &s.rwAlphaRef);
	// L2 (RwD3D9 driver cache). A state the driver cannot report keeps the
	// pre-seeded canonical value (the Get is a no-op or device fallback).
	RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE, &s.drvBlendEn);
	RwD3D9GetRenderState(D3DRS_SRCBLEND, &s.drvSrcBlend);
	RwD3D9GetRenderState(D3DRS_DESTBLEND, &s.drvDstBlend);
	RwD3D9GetRenderState(D3DRS_ZWRITEENABLE, &s.drvZwrite);
	RwD3D9GetRenderState(D3DRS_ALPHAFUNC, &s.drvAlphaFunc);
	RwD3D9GetRenderState(D3DRS_ALPHAREF, &s.drvAlphaRef);
	RwD3D9GetRenderState(D3DRS_COLORWRITEENABLE, &s.drvColorWrite);
	RwD3D9GetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, &s.drvSepAlpha);
	RwD3D9GetRenderState(D3DRS_BLENDOP, &s.drvBlendOp);
	// Raw L3 (the value the rasteriser actually used).
	if(d3d9device){
		d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &s.blendEn);
		d3d9device->GetRenderState(D3DRS_SRCBLEND, &s.srcBlend);
		d3d9device->GetRenderState(D3DRS_DESTBLEND, &s.dstBlend);
		d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &s.zwrite);
		d3d9device->GetRenderState(D3DRS_ALPHAFUNC, &s.alphaFunc);
		d3d9device->GetRenderState(D3DRS_ALPHAREF, &s.alphaRef);
		d3d9device->GetRenderState(D3DRS_COLORWRITEENABLE, &s.colorWrite);
		d3d9device->GetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, &s.sepAlpha);
		d3d9device->GetRenderState(D3DRS_BLENDOP, &s.blendOp);
	}
	s_spriteForceValid = true;
}

// Replay the nine captured states through L1 + L2 + raw L3. PARK-AND-SET on
// every cached layer: the opposite value goes through the cache first, so a
// cache-hit skip cannot swallow the target (pipeForceCullMode idiom). A
// captured blend-OFF (REPLACE) is NEVER replayed verbatim — that state is
// the black-quad signature a deferred batch can leave behind, so it is
// replaced by the canonical world normal-alpha block (blend TRUE,
// SRCALPHA/INVSRCALPHA, BLENDOP ADD); ZWRITE keeps its captured value.
// Called after EVERY forced batch draw — NOT gated on g_coronaBodyDone. The
// window-close resync in SpriteFlushIndexedDraw_hook is a separate canonical
// push and still runs for its own producers.
static void
spriteFlushShieldClose(const char *path)
{
	if(!s_spriteForceValid)
		return;
	s_spriteForceValid = false;
	if(!d3d9device)
		return;
	SpriteFlushState &s = s_spriteFlushState;
	// Captured REPLACE -> canonical normal-alpha on all three layers (see
	// header comment). The snapshot fields are rewritten so the existing
	// park-and-set replay below targets the canonical block; ZWRITE is left
	// on its captured value.
	bool capturedReplace = (s.blendEn == FALSE);
	if(capturedReplace){
		s.blendEn = s.drvBlendEn = (DWORD)TRUE;
		s.rwBlendEn = (RwUInt32)TRUE;
		s.srcBlend = s.drvSrcBlend = (DWORD)D3DBLEND_SRCALPHA;
		s.rwSrcBlend = (RwUInt32)rwBLENDSRCALPHA;
		s.dstBlend = s.drvDstBlend = (DWORD)D3DBLEND_INVSRCALPHA;
		s.rwDstBlend = (RwUInt32)rwBLENDINVSRCALPHA;
		s.blendOp = s.drvBlendOp = (DWORD)D3DBLENDOP_ADD;
	}
	// ALPHABLENDENABLE: park the boolean inverse -> captured target.
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE,
		(void*)(s.rwBlendEn ? (RwUInt32)FALSE : (RwUInt32)TRUE));
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, s.drvBlendEn ? FALSE : TRUE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)s.rwBlendEn);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, s.drvBlendEn);
	d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, s.blendEn);
	// SRCBLEND: park a guaranteed-different rw / D3D value -> target.
	RwUInt32 parkRwSrc = (s.rwSrcBlend == (RwUInt32)rwBLENDSRCALPHA)
		? (RwUInt32)rwBLENDINVSRCALPHA : (RwUInt32)rwBLENDSRCALPHA;
	DWORD parkSrc = (s.drvSrcBlend == (DWORD)D3DBLEND_ZERO)
		? (DWORD)D3DBLEND_ONE : (DWORD)D3DBLEND_ZERO;
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)parkRwSrc);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, parkSrc);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)s.rwSrcBlend);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, s.drvSrcBlend);
	d3d9device->SetRenderState(D3DRS_SRCBLEND, s.srcBlend);
	// DESTBLEND: same park-and-set pair.
	RwUInt32 parkRwDst = (s.rwDstBlend == (RwUInt32)rwBLENDONE)
		? (RwUInt32)rwBLENDZERO : (RwUInt32)rwBLENDONE;
	DWORD parkDst = (s.drvDstBlend == (DWORD)D3DBLEND_ZERO)
		? (DWORD)D3DBLEND_ONE : (DWORD)D3DBLEND_ZERO;
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)parkRwDst);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, parkDst);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)s.rwDstBlend);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, s.drvDstBlend);
	d3d9device->SetRenderState(D3DRS_DESTBLEND, s.dstBlend);
	// ZWRITEENABLE: park the boolean inverse -> captured target.
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE,
		(void*)(s.rwZwrite ? (RwUInt32)FALSE : (RwUInt32)TRUE));
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, s.drvZwrite ? FALSE : TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)s.rwZwrite);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, s.drvZwrite);
	d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, s.zwrite);
	// ALPHAFUNC: park GREATEREQUAL<->ALWAYS (always a transition).
	RwUInt32 parkRwFn = (s.rwAlphaFunc == (RwUInt32)rwALPHATESTFUNCTIONALWAYS)
		? (RwUInt32)rwALPHATESTFUNCTIONGREATEREQUAL
		: (RwUInt32)rwALPHATESTFUNCTIONALWAYS;
	DWORD parkFn = (s.drvAlphaFunc == (DWORD)D3DCMP_ALWAYS)
		? (DWORD)D3DCMP_GREATEREQUAL : (DWORD)D3DCMP_ALWAYS;
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)parkRwFn);
	RwD3D9SetRenderState(D3DRS_ALPHAFUNC, parkFn);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)s.rwAlphaFunc);
	RwD3D9SetRenderState(D3DRS_ALPHAFUNC, s.drvAlphaFunc);
	d3d9device->SetRenderState(D3DRS_ALPHAFUNC, s.alphaFunc);
	// ALPHAREF: park 3 when the captured value is the canonical 2.
	RwUInt32 parkRef = (s.rwAlphaRef == (RwUInt32)2) ? (RwUInt32)3 : (RwUInt32)2;
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)parkRef);
	RwD3D9SetRenderState(D3DRS_ALPHAREF, parkRef);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)s.rwAlphaRef);
	RwD3D9SetRenderState(D3DRS_ALPHAREF, s.drvAlphaRef);
	d3d9device->SetRenderState(D3DRS_ALPHAREF, s.alphaRef);
	// COLORWRITEENABLE: raw-only twin pair — park the bitwise inverse.
	DWORD parkCwr = s.drvColorWrite ^ 0xF;
	RwD3D9SetRenderState(D3DRS_COLORWRITEENABLE, parkCwr);
	RwD3D9SetRenderState(D3DRS_COLORWRITEENABLE, s.drvColorWrite);
	d3d9device->SetRenderState(D3DRS_COLORWRITEENABLE, s.colorWrite);
	// SEPARATEALPHABLENDENABLE: raw-only twin pair — park the inverse.
	RwD3D9SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,
		s.drvSepAlpha ? FALSE : TRUE);
	RwD3D9SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, s.drvSepAlpha);
	d3d9device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, s.sepAlpha);
	// BLENDOP: no rw twin — L2+L3 only; park ADD<->REVSUBTRACT.
	DWORD parkOp = (s.drvBlendOp == (DWORD)D3DBLENDOP_ADD)
		? (DWORD)D3DBLENDOP_REVSUBTRACT : (DWORD)D3DBLENDOP_ADD;
	RwD3D9SetRenderState(D3DRS_BLENDOP, parkOp);
	RwD3D9SetRenderState(D3DRS_BLENDOP, s.drvBlendOp);
	d3d9device->SetRenderState(D3DRS_BLENDOP, s.blendOp);
	if(dbglog_once("flushshield_restore"))
		dbglog("[FlushShield] restore path=%s states=9 blend=%d src=%d dst=%d "
		       "zw=%d ref=%d%s", path ? path : "?", (int)s.blendEn,
		       (int)s.srcBlend, (int)s.dstBlend, (int)s.zwrite,
		       (int)s.alphaRef,
		       capturedReplace
		           ? " (captured REPLACE -> canonical normal-alpha)"
		           : "");
}

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

	// One-shot anchor receipt (unthrottled static, not a hash-bucket tag):
	// resolved base + live registry count (0xC3F0D0, the count the game's
	// Add/Render code reads). A mis-anchor or a moved array with an
	// unpatched immediate is visible here on the first sprite.
	static bool pointLightsAnchorLogged = false;
	if(!pointLightsAnchorLogged){
		pointLightsAnchorLogged = true;
		dbglog("[PointLights] base=%p count=%d (base from lea imm @0x7001FF)",
		       (void*)pointLights, *(int*)0xC3F0D0);
	}

	float add = pointLights[currentLight].fogType == 1 ? 0.0f : 16.0f;
	r = mult*pointLights[currentLight].color[0]+add;
	g = mult*pointLights[currentLight].color[1]+add;
	b = mult*pointLights[currentLight].color[2]+add;
	f = 0xFF;
	// Corona draw-time raw shield — raw-force immediately before the draw.
	// Uses the ONE/ONE default (no explicit pair override). The window gate
	// inside coronaDrawRawShield passes here because the 0x7002D0 entry hook
	// (CPointLights__Render_shield_hook) re-arms g_inCoronasRender before the
	// body issues this 0x700B6B call; the batch's actual raster is also
	// force-covered by SpriteFlushIndexedDraw_hook on every non-empty flush.
	coronaDrawRawShield("CSprite_70E780");
	CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect_orig(x, y, z, a4, a5, r, g, b, f, a10, a11, alpha);
}

// ---- Buffered-sprite flush: the innermost corona draw entry -------------
// Vanilla CCoronas::Render (0x6FAEC0) does NOT rasterise its quads through
// 0x70E780 — the corona body APPENDS quads to the RW TempBuffer sprite batch
// (0x70F440-family appender, called 3x from the body at 0x6FB461/0x6FB561/
// 0x6FB5EA; verts 0xC5F958, indices 0xC4B958, count 0xC6A158) and the batch
// is drawn in one indexed primitive by the buffered-flush (0x70CF20).
// VANILLA timing: the body's ONLY exit is the tail-jmp at 0x6FB621 ->
// 0x70CF20 (no ret anywhere in 0x6FAEC0..0x6FB630), and the 0x70F440-family
// appenders tail-jmp to the same flush (0x70F531 etc.) when the 384-sprite
// batch fills — every rasterisation happens INSIDE CCoronas::Render.
// SILENTPATCH OVERRIDE (SilentPatchSA.asi init, disassembled): it rewrites
// the tail-jmp — `mov dword [0x70F45A],0` + `mov word [0x6FB621],0xE8C3` +
// call-rel32 helper with edx=0x70CF20 — turning 0x6FB621 into a plain RET.
// The corona batch then persists and is drawn by the NEXT consumer's flush
// (CPointLights tail-jmp 0x700BCD, CClouds calls 0x714019/0x71422A/0x714387/
// 0x715EFB/0x719B26, RenderReflections call 0x6FB9C2) — i.e. AFTER
// CCoronas::Render returns. The flush's two draw calls (0x70CF62 / 0x70CF9B
// -> 0x734EA0, cdecl (primType, verts, nverts, indices, nindices)) are still
// the innermost draw entries for EVERY corona quad — but the window must
// span them: g_inCoronasRender stays TRUE past the Render exit and closes
// here on the first post-body flush (g_coronaBodyDone). Mid-body
// full-batch flushes keep the window open so remaining appends stay
// covered. Since 2026-10-24 the additive shield is ALSO forced at this flush
// boundary (force=true, non-empty batch only); since 2026-10-26 that force
// runs for EVERY non-empty batch — the silent-patch-deferred corona/pointlight
// raster, the early CPointLights::Render glow sprites at 0x53DE6B, and any
// other shared-flush producer — and is closed again right after its draw.
// Since 2026-11-01 (perf fix) the force is PRODUCER-GATED: only a flush that
// carries an undrained shared batch (0xC6A158 > 0 at hook entry — the flush
// drains the count at 0x70CF76, between the two hooked call sites) runs the
// capture/force/close; a drained-batch draw site passes through with ZERO
// added state ops. Capture/close stay paired exactly as before on the gated
// path.
typedef void (__cdecl *SpriteFlushIndexedDraw_t)(int primType, void *verts, int nverts, void *indices, int nindices);
static SpriteFlushIndexedDraw_t SpriteFlushIndexedDraw_orig;

static void __cdecl
SpriteFlushIndexedDraw_hook(int primType, void *verts, int nverts, void *indices, int nindices)
{
	// ---- Producer-ownership gate input (read at hook entry) ------------
	// The shared TempBuffer population counter is still live HERE: the flush
	// drains it at 0x70CF76, BETWEEN the two hooked call sites (0x70CF62 /
	// 0x70CF9B), and orig (0x734EA0) is the draw primitive itself. So
	// spriteBatch > 0 at entry == this draw carries an undrained
	// corona/pointlight-family batch — the only producers that need the
	// additive shield (the black-squares defect is their glow quads
	// rasterising with leaked REPLACE state). spriteBatch == 0 (the flush's
	// second draw site after its own 0x70CF76 reset, or any flush whose
	// batch an earlier flush already drained) passes through with ZERO
	// added state ops.
	int spriteBatch = *(int*)0xC6A158;
	// First-hit receipts BEFORE the shield writes: absence of these lines
	// means neither shield path engaged. blend/src/dst are the device
	// values the shield is about to overwrite.
	if(g_inCoronasRender){
		static bool s_probeSpriteFlushShield = false;
		if(!s_probeSpriteFlushShield){
			s_probeSpriteFlushShield = true;
			int blend = -1, src = -1, dst = -1;
			if(d3d9device){
				DWORD v = 0;
				if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &v))) blend = (int)v;
				if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_SRCBLEND, &v)))         src   = (int)v;
				if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_DESTBLEND, &v)))        dst   = (int)v;
			}
			dbglog("[probe] sprite_flush shield active pre-dev blend=%d src=%d dst=%d",
				blend, src, dst);
		}
	}
	// ---- Force the additive block on the corona-family carrier flushes --
	// PRODUCER GATING (perf fix 2026-11-01): the force below used to run on
	// EVERY hook invocation that reached the draw sites (~100 D3D9 ops each,
	// 10-50 flushes/frame). It is now gated on the producer-ownership test:
	// spriteBatch > 0 at hook entry (read above, BEFORE orig drains it).
	// 2026-10-24 diagnosis: the black squares with the tiny coloured core
	// are CPointLights glow sprites (0x700B6B -> 0x70E780) rasterised by
	// THIS shared flush at the EARLY CPointLights::Render call site
	// 0x53DE6B (sky/reflection phase). SilentPatchSA ALSO defers the
	// corona/pointlight batch past CCoronas::Render's return, so the shield
	// window is already CLOSED when the real rasterisation happens (field
	// log 2026-10-26: 230/232 flushes unforced with pre-force src=5 dst=6 —
	// that 5/6 pair writes the black rectangle around the coloured core).
	// A window tied to one body cannot cover a shared deferred queue: force
	// the vanilla additive block at the flush boundary itself for every
	// carrier batch (ALPHABLENDENABLE=TRUE, SRCBLEND=DESTBLEND=ONE,
	// ZWRITE=FALSE) through the park-and-set L1+L2+raw-L3 path.
	// SAFE because spriteFlushShieldCapture() snapshots all nine states
	// before the force and spriteFlushShieldClose() replays them right after
	// SpriteFlushIndexedDraw_orig on every forced path — a non-corona batch
	// sees the additive pair only for its own raster; the surrounding frame
	// is restored exactly.
	// SAFETY: 0x70CF27 returns before both draw calls when the batch count
	// is <= 0, so mirror that guard here — the force (and its one-shot
	// probe) must never run for an empty batch (orig would no-op anyway).
	// Count = buffered-sprite count at 0xC6A158, read as a dword.
	const char *spriteForcePath = NULL;	// close-receipt label, force-only
	bool spriteForced = false;
	{
		// One-shot gate receipt + throttled skipped-vs-shielded counter
		// (perf fix 2026-11-01). POD statics only — constant init, no
		// dynamic initialisation (DLL context).
		static bool s_flGateReceipt = false;
		static unsigned s_flShielded = 0, s_flSkipped = 0;
		if(!s_flGateReceipt){
			s_flGateReceipt = true;
			dbglog("[sprite_flush] producer gating active: shield only on spriteBatch>0");
		}
		// INI spriteFlushShield (default 1 = ON): 0 = no capture / force /
		// close at the flush boundary at all (the vanilla batch state is
		// drawn untouched — bisect lever for the producer-gated force).
		if(spriteBatch > 0 && d3d9device && (!config || config->spriteFlushShield)){
			if(dbglog_once("sprite_flush_force")){
				int blend = -1, src = -1, dst = -1, zw = -1;
				if(d3d9device){
					DWORD v = 0;
					if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &v))) blend = (int)v;
					if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_SRCBLEND, &v)))         src   = (int)v;
					if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_DESTBLEND, &v)))        dst   = (int)v;
					if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &v)))     zw    = (int)v;
				}
				dbglog("[probe] sprite_flush force=1 pre blend=%d src=%d dst=%d zw=%d",
					blend, src, dst, zw);
			}
			// Capture the PRE-force nine-state block on every layer BEFORE
			// the first shield write; spriteFlushShieldClose replays it
			// immediately after the draw.
			spriteFlushShieldCapture();
			// Vanilla CCoronas / CPointLights / CClouds all draw ONE/ONE
			// additive (their glow textures have a black RGB surround with
			// opaque alpha). Both producers now force the SAME pair; the
			// g_inCloudsRender flag only selects the close-path label below.
			if(g_inCloudsRender){
				coronaDrawRawShield("SpriteFlush_70CF20", true,
					(DWORD)D3DBLEND_ONE, (DWORD)D3DBLEND_ONE);
				spriteForcePath = "SpriteFlush_70CF20/clouds";
			}else{
				coronaDrawRawShield("SpriteFlush_70CF20", true);
				spriteForcePath = "SpriteFlush_70CF20/corona";
			}
			spriteForced = true;
			// One-shot receipt per producer: the first forced flush of each
			// kind names the pair. dbglog_once is not time-based.
			if(dbglog_once(g_inCloudsRender ? "cloud_shield_clouds"
			                                : "cloud_shield_sprites"))
				dbglog(g_inCloudsRender
					? "[CloudShield] flush batch=clouds forced ONE/ONE"
					: "[CloudShield] flush batch=sprites forced ONE/ONE");
		}else if(spriteBatch > 0 && d3d9device){
			// [PtLightGlow] UNSHIELDED-FLUSH diagnostic (03:01-run black
			// squares): the batch IS about to rasterise but the flush-
			// boundary force is disabled — INI spriteFlushShield=0 (the
			// 03:01 run had coronaBlendShield/spriteFlushShield/
			// spriteStateShield all =0, so the verified flush installs
			// above never forced and the batch drew with whatever the
			// device held: 181 samples SRCALPHA/INVSRCALPHA + 23 REPLACE,
			// the glow texture's opaque black surround -> black squares
			// with bright centers). Throttled: an every-frame batch must
			// not flood the log; dbglog_once makes the run's FIRST hit
			// unstarvable. Consistency under producer gating: this branch
			// requires spriteBatch>0 AND the shield lever OFF, so with
			// spriteFlushShield=1 (default) it can never fire — every
			// carrier batch is shielded by the branch above.
			if(dbglog_once("ptlightglow_unshielded") || dbglog_throttle("ptlightglow")){
				DWORD ub = 0, us = 0, ud = 0;
				d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &ub);
				d3d9device->GetRenderState(D3DRS_SRCBLEND, &us);
				d3d9device->GetRenderState(D3DRS_DESTBLEND, &ud);
				dbglog("[PtLightGlow] flush batch=%d drew UNSHIELDED dev blend=%d src=%d dst=%d "
				       "(spriteFlushShield=%d — 0 disables the additive force; "
				       "black-square producer whenever this is not blend=1 src=2 dst=2)",
				       spriteBatch, (int)ub, (int)us, (int)ud,
				       config ? config->spriteFlushShield : -1);
			}
		}
		// Throttled skipped-vs-shielded receipt for the producer gate:
		// "skipped" = this draw site reached the hook with an already-drained
		// batch (spriteBatch<=0 — the flush's second draw site after its own
		// 0x70CF76 reset, or a batch a previous flush drained) or with the
		// bisect lever off. Those sites add ZERO state ops.
		if(spriteForced)
			s_flShielded++;
		else
			s_flSkipped++;
		if(dbglog_throttle("sprite_flush_gate"))
			dbglog("[sprite_flush] gate shielded=%u skipped=%u "
			       "(skipped = spriteBatch<=0 at entry or spriteFlushShield=0)",
			       s_flShielded, s_flSkipped);
	}
	// Xbox-clouds lane hunk B: the cloud sprite flush must rasterise with the
	// clouds pixel shader armed. Bind through BOTH layers (RW cache + raw
	// device) before the orig call and unbind right after it under the same
	// guard — g_inCloudsRender is only true inside CClouds__Render_hygiene's
	// orig window, so the pair can never leak into corona/HUD/deferred flushes.
	if(g_inCloudsRender && cloudsPS && d3d9device){
		RwD3D9SetPixelShader(cloudsPS);
		d3d9device->SetPixelShader((IDirect3DPixelShader9*)cloudsPS);
	}
	SpriteFlushIndexedDraw_orig(primType, verts, nverts, indices, nindices);
	if(g_inCloudsRender && cloudsPS && d3d9device){
		RwD3D9SetPixelShader(NULL);
		d3d9device->SetPixelShader(NULL);
	}
	// Close the force on EVERY path that ran it — NOT gated on
	// g_coronaBodyDone. This is what keeps cloud/sun/water/HUD/deferred
	// flushes from leaving the corona block in the rw cache for the rest of
	// the frame. The window-close resync below is a separate canonical push
	// and still runs for its own producers.
	if(spriteForced)
		spriteFlushShieldClose(spriteForcePath);
	// Window close: the body owner arms g_coronaBodyDone (CCoronas::Render
	// after its trampoline, CPointLights BEFORE its orig call so its own tail
	// flush closes the window). The flush that rasterises the SP-deferred
	// corona batch — or the street-lamp cone/dot batch — closes the window
	// here. POD writes; runs for every flush user.
	if(g_coronaBodyDone){
		g_inCoronasRender = false;
		g_coronaBodyDone = false;
		// ---- CANONICAL RE-SYNC at window-close (L1+L2+L3) ----
		// The park-and-set shield above leaves every layer holding the
		// corona additive block (blend on, ONE/ONE, ZWRITE FALSE).
		// Any later consumer whose entry contract EQUALS that cached
		// value would no-op and inherit it, and any raw writer in
		// between re-creates the cache<->device split that produced the
		// black cones. Push the canonical world signature — normal-alpha
		// blending: blend ON + SRCALPHA(5)/INVSRCALPHA(6) + ZWRITE TRUE,
		// VERTEXALPHA matching the pushed blend state, BLENDOP ADD,
		// ZTEST on (the same push the corona exit-restore performs for
		// L1+L2) — through L1 + L2 + raw L3, so every later consumer
		// starts cache-consistent: its own differing sets TRANSITION
		// instead of no-op'ing against a stale cache. Throttled tag
		// "corona_resync" (fires at most once per 2s).
		// INI spriteFlushShield (default 1 = ON): 0 = clear the window flags
		// only and skip this canonical push (bisect lever for the re-sync).
		if(!config || config->spriteFlushShield){
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
			RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
			RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
			RwD3D9SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
			RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
			RwD3D9SetRenderState(D3DRS_ZENABLE, TRUE);
			if(d3d9device){
				d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
				d3d9device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
				d3d9device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
				d3d9device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
				d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
				d3d9device->SetRenderState(D3DRS_ZENABLE, TRUE);
			}
			if(dbglog_throttle("corona_resync"))
				dbglog("[CoronaResync] window closed — canonical world "
				       "signature pushed L1+L2+L3 (blend=1 src=SRCALPHA "
				       "dst=INVSRCALPHA op=ADD zwrite=1 vtxalpha=1 ztest=1)");
		}
	}
}

// ---- CPointLights::Render (0x7002D0) entry — shield-window re-arm -------
// CPointLights::Render draws the street-lamp FOG CONES (call 0x700817,
// fogType 1-2) + warm glow dots (call 0x700B6B) through the SAME buffered
// sprite flush 0x70CF20 the corona batch rides (draw sites 0x70CF62/
// 0x70CF9B), and its own entry contract (decoded 0x7002E7-0x700346) is
// byte-identical to CCoronas::Render's: FOGENABLE=0 ZWRITE=0 ZTEST=1
// VERTEXALPHA=1 SRCBLEND=ONE DESTBLEND=ONE. With SilentPatchSA deferring
// the corona flush, the shield window closed at the FIRST post-corona-body
// flush — so the later CPointLights cone batch drew UNSHIELDED, inheriting
// whatever stale device state the last raw writer left (field log:
// [CoronaDraw] pre-force blend=0 src=5 dst=6 = REPLACE -> black cones).
// Re-arm the SAME window here (per-window flags; the re-arm overwrites
// whatever the corona window left — no interaction with the corona hook's
// own lifecycle, which always re-arms at its own entry): every CPointLights
// flush is then inside the shield, and the EXISTING flush-close logic in
// SpriteFlushIndexedDraw_hook closes the window + runs the canonical
// re-sync on that flush. CPointLights' tail flush
// (0x700BCD routes through 0x70CF20) fires DURING the orig call, so
// g_coronaBodyDone is armed BEFORE the call — the tail flush itself now
// closes the window, closing the body's own draw path instead of leaving
// the window open until the NEXT frame's flush. Plain call through the
// saved pointer: Render is a normal function (reads the global registry
// 0xC3F0E0, no this-use).
static void (*CPointLights__Render_orig)(void);
static void
CPointLights__Render_shield_hook(void)
{
	// SAFETY-CLEAR (stuck-flag guard) + RE-ARM — same pattern as the
	// corona hook entry. Plain POD writes on the render thread; no flush
	// draw can interleave here.
	g_inCoronasRender = false;
	g_coronaBodyDone = false;
	g_inCoronasRender = true;
	// Body START, not body end: the tail flush runs INSIDE the orig call, so
	// a post-call assignment left that flush (and every draw it rasterised)
	// inside the shield while the window stayed open past the body. POD
	// write; left set even if orig faults, so the next flush still closes.
	g_coronaBodyDone = true;
	// ---- [PtLightColor] wave-6b diagnostic receipt (test-7, 2026-11-01) ----
	// Discriminates "lights REGISTERED black" from "blend broken at draw":
	// the glow-sprite colour is read straight from this registry by the
	// 0x70E780 wrapper (r/g/b = mult*color[i]+add, add=16 when fogType!=1),
	// so a walk of the registry BEFORE the orig body is the authoritative
	// pre-draw colour record. If the registry shows saturated warm colours
	// while the screen still shows black quads, the defect is device blend
	// state at rasterisation (the flush shields above are the fix); if the
	// registry itself holds zeros, the producer (CPointLights::Add) is the
	// bug. Cheap: 2s throttle + run's first hit unstarvable (dbglog_once),
	// walk capped at the registry capacity (32) and only the first 8
	// entries logged individually. Pure reads — no render state touched.
	{
		static bool s_plColorFirst = true;
		if(s_plColorFirst || dbglog_throttle("ptlight_color")){
			s_plColorFirst = false;
			int n = *(int*)0xC3F0D0;
			if(n < 0) n = 0;
			if(n > 32) n = 32;	// registry capacity (see Forward+ layout note)
			if(n == 0){
				dbglog("[PtLightColor] registry EMPTY count=0 "
				       "(no glow sprites can draw this frame)");
			}else{
				int black = 0;
				for(int i = 0; i < n; i++){
					const PointLight &pl = pointLights[i];
					if(pl.color[0] <= 0.01f && pl.color[1] <= 0.01f &&
					   pl.color[2] <= 0.01f)
						black++;
					if(i < 8)
						dbglog("[PtLightColor] [%d] type=%d fog=%d "
						       "color=(%.2f,%.2f,%.2f) radius=%.1f",
						       i, (int)pl.type, (int)pl.fogType,
						       pl.color[0], pl.color[1], pl.color[2],
						       pl.radius);
				}
				dbglog("[PtLightColor] registry count=%d black=%d "
				       "(black==count => lights registered black = "
				       "producer bug; saturated colors + black quads "
				       "on screen => blend broken at draw)",
				       n, black);
			}
		}
	}
	if(CPointLights__Render_orig)
		CPointLights__Render_orig();
}

// ---- Producer discrimination for the two wave-6b TempBuffer flush sites ---
// 0x701FE9 (postfx TempBuffer writer) and 0x7273D0 (call site 0x719B2B) draw
// whatever their caller accumulated into the shared 2D TempBuffer
// (0xC5F958 verts / 0xC4B958 indices). Those are NOT corona-only lanes: the
// game's CSprite2d / HUD / font / 2D-text batch drains through the same quad
// buffer, and the field log proves it — cloud_flush hit 1407x with a
// non-empty batch in 2 s while the game sat at the menu, far too hot for
// cloud rendering; that is the 2D text lane. Forcing the ONE/ONE additive
// block on such a batch rasterises glyph quads additively = washed-out
// "fonts completely broken". The force is therefore applied ONLY when the
// batch's stage-0 texture identity is one of the game's corona / pointlight
// / cloud glow rasters; every unknown producer FAILS OPEN (skip, no state
// write) instead of being corrupted.
// Family globals (clouds-ffp-audit §2.3 / corona diagnosis §1.2):
//   0xC3E000 gpCoronaTexture[0] / cloud glow A
//   0xC3E008 multiplicative pass glow texture
//   0xC6AA74 sun corona texture
//   0xC6AA78 shared 2D-sprite glow (CPointLights, cloud batch #2)
static RwTexture *
coronaFamilyTexture(int i)
{
	switch(i){
	case 0: return *(RwTexture**)0xC3E000;
	case 1: return *(RwTexture**)0xC3E008;
	case 2: return *(RwTexture**)0xC6AA74;
	case 3: return *(RwTexture**)0xC6AA78;
	default: return NULL;
	}
}
static const int CORONA_FAMILY_TEXTURE_COUNT = 4;

static bool
sameRasterFamily(RwRaster *a, RwRaster *b)
{
	if(!a || !b)
		return false;
	if(a == b)
		return true;
	// Glow textures can be bound through a parent or a sub-raster depending
	// on the producer path.
	if(a->parent && a->parent == b)
		return true;
	if(b->parent && b->parent == a)
		return true;
	return false;
}

// Capture the batch's stage-0 identity:
//   L1 rw cache -> RwRenderStateGet(rwRENDERSTATETEXTURERASTER)  [VERDICT]
//   L3 device   -> GetTexture(0) (AddRef; caller releases devTex) [diag only]
// The verdict is L1-ONLY. ORing the raw device layer into it was the font-
// regression false positive of 2026-11-01: the sprite flush runs 5
// instructions before the font flush and leaves the glow texture on device
// stage 0 while the rw cache has already moved to the font atlas, so every
// glyph batch tested as "corona family" — log proof: `[cloud_flush] gate
// shielded=23116 skipped=0`, zero rejections in 23,116 invocations, and every
// menu glyph rasterised as a solid rectangle (docs/baseline/2026-11-01-fonts/
// findings.md). The device read stays for receipts only.
struct FlushBatchId
{
	RwRaster *ras;
	IDirect3DBaseTexture9 *devTex;
	bool family;
};

static FlushBatchId
flushBatchIdentity(void)
{
	FlushBatchId id;
	id.ras = NULL;
	id.devTex = NULL;
	id.family = false;
	void *rawRas = NULL;
	RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &rawRas);
	if((void*)rawRas == (void*)0xFFFFFFFF)
		rawRas = NULL;	// failed-Get sentinel (same convention as postfx.cpp)
	id.ras = (RwRaster*)rawRas;
	if(d3d9device)
		d3d9device->GetTexture(0, &id.devTex);	// AddRef
	for(int i = 0; i < CORONA_FAMILY_TEXTURE_COUNT && !id.family; i++){
		RwTexture *tex = coronaFamilyTexture(i);
		// Defensive: the family globals are .data slots that are NULL before
		// the TXDs load; never follow a non-pointer. A bad slot is skipped
		// (fail open) rather than faulting the render thread.
		if(!tex || (uintptr_t)tex < 0x10000u || ((uintptr_t)tex & 3u))
			continue;
		if(sameRasterFamily(id.ras, tex->raster)){
			id.family = true;
			break;
		}
		// Device-layer OR test REMOVED (2026-11-02 audit): the verdict is
		// strictly L1-only. ORing the raw device texture into it was the
		// font-regression false positive of 2026-11-01 — the sprite flush
		// runs 5 instructions before the font flush and leaves the glow
		// texture on device stage 0 while the rw cache has already moved to
		// the font atlas, so every glyph batch tested as "corona family"
		// (23,116/23,116 shielded, zero rejections — see the block comment
		// above). id.devTex stays a diagnostics-only receipt field; the
		// caller Releases it after logging.
	}
	return id;
}

// ---- Wave-6b: the TWO unshielded inline TempBuffer flush sites ----------
// Test-7 (2026-11-01 18:49, Xbox mode C-nibble 0x2): giant black wedge with
// hard diagonals, near-total world blackout, while the 0x70CF20 flush shield
// was ACTIVE (sprite_flush gating shielded=44 skipped=0) and the statesync
// blend heal kept recurring — the black quads persist anyway. Root cause:
// TWO MORE draw sites rasterise TempBuffer batches OUTSIDE the 0x70CF20
// flush hook, with a broken device blend, and were left vanilla in wave-6:
//
//   (a) 0x701FE9 — inside the postfx TempBuffer writer (function 0x701780,
//       called from CPostEffects::Render 0x7046E0 at 0x705116). Byte-
//       verified (exe 1.0 US): E8 B2 2E 03 00 -> 0x734EA0, cdecl 5-arg
//       (primType 3 TRISTRIP, verts 0xC5F958, nverts [0xC4B950], indices
//       0xC4B958, nindices [0xC4B954]) — the SAME draw primitive the shared
//       flush sites call, but with the writer's OWN count globals
//       0xC4B950/0xC4B954 (TempBufferVerticesStored/IndicesStored — NOT the
//       shared 0xC6A158). The caller gates the call on 0xC4B950 > 0
//       (test eax,eax; jz @ 0x701FC5..) and resets the count AFTER the call
//       (0x701FF9 mov dword [0xC4B950],0), so the count is still live at
//       hook entry — producer-ownership test identical to the 0x70CF20 gate.
//
//   (b) 0x7273D0 — cloud-region companion flush, called at 0x719B2B right
//       after the cloud-region 0x70CF20 flush (0x719B26). Byte-verified:
//       0x7273D0 is a FUNCTION ENTRY (not a call site): reads its own
//       indices count [0xC8045C], returns when <= 0 (A1 5C 04 C8 00 / 85 C0
//       / 7E 42), then draws 0x734EA0 with verts 0xC5F958 / nverts
//       [0xC80458] / indices 0xC4B958 / nindices [0xC8045C] and zeroes both
//       counts before ret. Hooked via its CALL SITE 0x719B2B (E8 A0 D8 00
//       00 -> 0x7273D0, verified) with a void(void) hook — the count gate
//       at entry mirrors the function's own early-out exactly.
//
// Both sites draw unshielded with a broken device blend = the black light
// quads covering the scene. Fix: the SAME capture -> force -> close shield
// pattern as the 0x70CF20 forced path (spriteFlushShieldCapture /
// coronaDrawRawShield(force=true, ONE/ONE) / spriteFlushShieldClose), gated
// per site on ITS OWN count global > 0. Non-nesting with the shared-flush
// shield: each hook closes its force before returning, and neither orig
// draw re-enters another hooked flush (0x734EA0 is the raw primitive).
// INI spriteFlushShield=0 disables both forces (bisect lever, same as the
// 0x70CF20 gate).
static SpriteFlushIndexedDraw_t PostFxFlush_701FE9_orig;
static void __cdecl
PostFxFlush_701FE9_hook(int primType, void *verts, int nverts, void *indices, int nindices)
{
	// Producer-ownership gate input: the writer's own vert count, read at
	// hook entry BEFORE the caller's post-call reset (the reset is at
	// 0x701FF9, after this call returns).
	int batch = *(int*)0xC4B950;
	static bool s_gateReceipt = false;
	static unsigned s_shielded = 0, s_skipped = 0;
	if(!s_gateReceipt){
		s_gateReceipt = true;
		dbglog("[postfx_flush] producer gating active: shield only on "
		       "0xC4B950>0 AND stage-0 = corona-family glow raster "
		       "(unknown producer -> skip, fail open)");
	}
	const char *forcePath = NULL;
	bool forced = false;
	if(batch > 0 && d3d9device && (!config || config->spriteFlushShield)){
		// Producer discrimination (fail-open gate): the shared TempBuffer is
		// ALSO the game's 2D text/HUD/font lane. Only a corona/pointlight/
		// cloud glow batch may receive the ONE/ONE additive force; a glyph
		// batch keeps its authored blend (additive = washed-out text).
		FlushBatchId id = flushBatchIdentity();
		if(id.family){
			if(dbglog_once("postfx_flush_force")){
				int blend = -1, src = -1, dst = -1;
				if(d3d9device){
					DWORD v = 0;
					if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &v))) blend = (int)v;
					if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_SRCBLEND, &v)))         src   = (int)v;
					if(SUCCEEDED(d3d9device->GetRenderState(D3DRS_DESTBLEND, &v)))        dst   = (int)v;
				}
				dbglog("[probe] postfx_flush 0x701FE9 force=1 pre blend=%d src=%d dst=%d "
				       "(black-wedge producer when not blend=1)",
				       blend, src, dst);
			}
			spriteFlushShieldCapture();
			coronaDrawRawShield("PostFxFlush_701FE9", true);
			forcePath = "PostFxFlush_701FE9/postfx";
			forced = true;
		}
		// Per-site receipt: the batch's stage-0 identity + whether the force
		// ran. First hit of the run is unstarvable so a short log still
		// names the producer; then throttled.
		if(dbglog_once("postfx_flush_id_first") || dbglog_throttle("postfx_flush_id"))
			dbglog("[postfx_flush_id] 0x701FE9 batch=%d s0Raster=%p s0DevTex=%p "
			       "family=%d force=%d (unknown producer -> skip, fail open)",
			       batch, (void*)id.ras, (void*)id.devTex,
			       id.family ? 1 : 0, forced ? 1 : 0);
		if(id.devTex)
			id.devTex->Release();	// GetTexture AddRef
	}
	if(forced)
		s_shielded++;
	else
		s_skipped++;
	if(dbglog_throttle("postfx_flush_gate"))
		dbglog("[postfx_flush] gate shielded=%u skipped=%u "
		       "(skipped = 0xC4B950<=0 at entry, spriteFlushShield=0, "
		       "or unknown stage-0 producer)",
		       s_shielded, s_skipped);
	if(PostFxFlush_701FE9_orig)
		PostFxFlush_701FE9_orig(primType, verts, nverts, indices, nindices);
	if(forced)
		spriteFlushShieldClose(forcePath);
}

static void (*CloudRegionFlush_7273D0_orig)(void);
static void
CloudRegionFlush_7273D0_hook(void)
{
	// ==================================================================
	// 0x7273D0 IS THE FONT GLYPH FLUSH — NOT A CLOUD/REGION FLUSH.
	// Proven twice independently (2026-11-01):
	//   * 0x7273D0's ONLY caller is CFont::RenderFontBuffer (0x719840);
	//     its counts 0xC80458/0xC8045C are written only by the glyph
	//     appender 0x728200 (docs/plans/2026-11-01-sa-font-path.md).
	//   * Screenshot forensics: every menu glyph drew as a SOLID FILLED
	//     RECTANGLE at correct advance width, zero AA shades, 101px tall
	//     (docs/baseline/2026-11-01-fonts/findings.md).
	// The corona additive block (ONE/ONE, ZWRITE off, ALPHATEST GE@0) MUST
	// NEVER be forced here: it discards glyph coverage alpha. This lane is
	// pure FFP (no pixel shader) and needs SRCALPHA/INVSRCALPHA.
	//
	// Positive duty instead: the preceding 0x70CF20 sprite-flush close
	// replays SRC/DEST blend a few instructions before this call, and
	// CFont::RenderBuffer only sets VERTEXALPHAENABLE — so glyphs INHERIT
	// the corona/REPLACE pair. Pin the canonical glyph blend through all
	// three layers for the draw and hand the entry block back afterwards.
	// That makes text correct regardless of what leaked, and it is the
	// reason this hook stays installed.
	// ==================================================================
	int batch = *(int*)0xC8045C;
	static bool s_gateReceipt = false;
	static unsigned s_pinned = 0, s_empty = 0, s_fast = 0;
	if(!s_gateReceipt){
		s_gateReceipt = true;
		dbglog("[font_flush] 0x7273D0 = CFont::RenderFontBuffer glyph flush — "
		       "corona force DISABLED here; pinning SRCALPHA/INVSRCALPHA "
		       "(glyph coverage preserved)");
	}

	if(batch <= 0 || !d3d9device){
		s_empty++;
		if(dbglog_throttle("font_flush_gate"))
			dbglog("[font_flush] gate pinned=%u empty=%u (empty = batch<=0 "
			       "or no device — no state touched)", s_pinned, s_empty);
		if(CloudRegionFlush_7273D0_orig)
			CloudRegionFlush_7273D0_orig();
		return;
	}

	// ---- PERF R5: entry==canonical fast path ---------------------------
	// Read the three device-layer blend states ONCE. When the entry block
	// already IS the canonical glyph blend (ALPHABLENDENABLE on +
	// SRCALPHA/INVSRCALPHA — e.g. the preceding 0x70CF20 sprite flush left
	// exactly the pair the glyphs inherit), the park-and-set + verbatim
	// restore below is a state no-op: skip it entirely (3 reads + compare
	// instead of ~30 L1/L2/L3 ops per glyph flush) and just run the orig.
	// Any other entry state takes the full capture + pin + restore path,
	// unchanged. Any nonzero ALPHABLENDENABLE is TRUE to D3D9.
	DWORD eDevBlend = 1, eDevSrc = D3DBLEND_SRCALPHA, eDevDst = D3DBLEND_INVSRCALPHA;
	bool devReadOk = false;
	if(d3d9device){
		devReadOk =
			SUCCEEDED(d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &eDevBlend)) &&
			SUCCEEDED(d3d9device->GetRenderState(D3DRS_SRCBLEND,         &eDevSrc))  &&
			SUCCEEDED(d3d9device->GetRenderState(D3DRS_DESTBLEND,        &eDevDst));
	}
	if(devReadOk && eDevBlend != 0 &&
	   eDevSrc == D3DBLEND_SRCALPHA && eDevDst == D3DBLEND_INVSRCALPHA){
		s_fast++;
		if(dbglog_throttle("font_flush_gate"))
			dbglog("[font_flush] fast path: entry already canonical "
			       "(pinned=%u fast=%u empty=%u) — no state touched",
			       s_pinned, s_fast, s_empty);
		if(CloudRegionFlush_7273D0_orig)
			CloudRegionFlush_7273D0_orig();
		return;
	}

	// ---- entry capture (all three layers) ----
	// (device-layer values already read above for the fast-path probe)
	DWORD eDrvBlend = eDevBlend, eDrvSrc = eDevSrc, eDrvDst = eDevDst;
	(void)RwD3D9GetRenderState(D3DRS_ALPHABLENDENABLE, &eDrvBlend);
	(void)RwD3D9GetRenderState(D3DRS_SRCBLEND,         &eDrvSrc);
	(void)RwD3D9GetRenderState(D3DRS_DESTBLEND,        &eDrvDst);
	void *eRwSrc = (void*)rwBLENDSRCALPHA, *eRwDst = (void*)rwBLENDINVSRCALPHA;
	RwRenderStateGet(rwRENDERSTATESRCBLEND,  &eRwSrc);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, &eRwDst);
	if((void*)eRwSrc == (void*)0xFFFFFFFF) eRwSrc = (void*)rwBLENDSRCALPHA;
	if((void*)eRwDst == (void*)0xFFFFFFFF) eRwDst = (void*)rwBLENDINVSRCALPHA;

	// ---- pin canonical glyph blend: park-and-set so every L1 write is a
	//      real transition (never a value-compare no-op), then L2+L3 ----
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND,  (void*)rwBLENDONE);          // park
	RwRenderStateSet(rwRENDERSTATESRCBLEND,  (void*)rwBLENDSRCALPHA);     // target
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);         // park
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);  // target
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	RwD3D9SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	d3d9device->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
	d3d9device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

	if(dbglog_once("font_flush_first"))
		dbglog("[font_flush] first glyph batch=%d entryDev(blend=%lu src=%lu dst=%lu) "
		       "entryDrv(blend=%lu src=%lu dst=%lu) -> pinned SRCALPHA/INVSRCALPHA",
		       batch, (unsigned long)eDevBlend, (unsigned long)eDevSrc, (unsigned long)eDevDst,
		       (unsigned long)eDrvBlend, (unsigned long)eDrvSrc, (unsigned long)eDrvDst);

	if(CloudRegionFlush_7273D0_orig)
		CloudRegionFlush_7273D0_orig();

	// ---- hand the entry block back verbatim, all three layers ----
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, eDrvBlend ? TRUE : FALSE);
	RwD3D9SetRenderState(D3DRS_SRCBLEND,         eDrvSrc);
	RwD3D9SetRenderState(D3DRS_DESTBLEND,        eDrvDst);
	RwRenderStateSet(rwRENDERSTATESRCBLEND,  eRwSrc);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, eRwDst);
	d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, eDevBlend ? TRUE : FALSE);
	d3d9device->SetRenderState(D3DRS_SRCBLEND,         eDevSrc);
	d3d9device->SetRenderState(D3DRS_DESTBLEND,        eDevDst);

	s_pinned++;
	if(dbglog_throttle("font_flush_gate"))
		dbglog("[font_flush] gate pinned=%u empty=%u (glyph blend pinned, "
		       "entry block restored)", s_pinned, s_empty);
}

// ---- Frame-boundary safety close for the corona/cone shield window ------
// SilentPatchSA defers the corona batch past CCoronas::Render's return, so a
// window can legitimately outlive the hook. Any window still open when a
// frame boundary is reached is stale (the deferred flush never came) and
// would shield unrelated sprites next frame. Hard-clear the flags and, when
// the entry capture ran, put the pre-window ALPHAFUNC/ALPHAREF back through
// L1+L2+L3 (the CCoronas exit restores all four states; this is the fault-
// path / frame-boundary net for the alpha-test pair). The BLEND block is NOT
// replayed from the capture: it is pinned to the canonical world normal-alpha
// state, because the entry capture can itself be a REPLACE (blend=0)
// snapshot and replaying it re-installs the black-quad signature.
void
CoronaWindow_Close(void)
{
	g_inCoronasRender = false;
	g_coronaBodyDone = false;
	if(!g_coronaPreValid)
		return;	// no capture (window never entered) — nothing to restore
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)g_coronaPreFuncRw);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)g_coronaPreRefRw);
	RwD3D9SetRenderState(D3DRS_ALPHAFUNC, g_coronaPreFuncDev);
	RwD3D9SetRenderState(D3DRS_ALPHAREF, g_coronaPreRefDev);
	// ALPHABLENDENABLE / SRCBLEND / DESTBLEND / BLENDOP are pinned to the
	// CANONICAL world normal-alpha block — never replayed from the capture:
	// the field log proves the device can be on REPLACE (blend=0) when the
	// corona window opens, and replaying that snapshot re-installed the
	// black-quad signature at every frame boundary. Canonical: blend TRUE,
	// SRCALPHA(5)/INVSRCALPHA(6), BLENDOP ADD. ZWRITEENABLE keeps its
	// captured value (the close does not own depth). Every state below is
	// park-and-set (a guaranteed cache transition) then the canonical
	// target, with raw L3 last to repair device drift the caches cannot see.
	DWORD canonBlend = TRUE;
	DWORD canonSrc = (DWORD)D3DBLEND_SRCALPHA;
	DWORD canonDst = (DWORD)D3DBLEND_INVSRCALPHA;
	DWORD canonOp  = (DWORD)D3DBLENDOP_ADD;
	DWORD preZw  = g_coronaPreZWrite ? (DWORD)TRUE : (DWORD)FALSE;
	// ALPHABLENDENABLE: park FALSE -> canonical TRUE.
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	// SRCBLEND / DESTBLEND: park ZERO (guaranteed different) -> canonical.
	DWORD parkSrc = (DWORD)D3DBLEND_ZERO;	// canonSrc is SRCALPHA
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)coronaRwBlendFromD3D(parkSrc));
	RwD3D9SetRenderState(D3DRS_SRCBLEND, parkSrc);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)coronaRwBlendFromD3D(canonSrc));
	RwD3D9SetRenderState(D3DRS_SRCBLEND, canonSrc);
	DWORD parkDst = (DWORD)D3DBLEND_ZERO;	// canonDst is INVSRCALPHA
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)coronaRwBlendFromD3D(parkDst));
	RwD3D9SetRenderState(D3DRS_DESTBLEND, parkDst);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)coronaRwBlendFromD3D(canonDst));
	RwD3D9SetRenderState(D3DRS_DESTBLEND, canonDst);
	// ZWRITEENABLE: park the boolean inverse -> captured target.
	DWORD parkZw = preZw ? (DWORD)FALSE : (DWORD)TRUE;
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)parkZw);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, parkZw);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)preZw);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, preZw);
	// BLENDOP: no rw twin — L2 + raw L3 only. Park REVSUBTRACT -> ADD.
	DWORD parkOp = (DWORD)D3DBLENDOP_REVSUBTRACT;
	RwD3D9SetRenderState(D3DRS_BLENDOP, parkOp);
	RwD3D9SetRenderState(D3DRS_BLENDOP, canonOp);
	if(d3d9device){
		d3d9device->SetRenderState(D3DRS_ALPHAFUNC, g_coronaPreFuncDev);
		d3d9device->SetRenderState(D3DRS_ALPHAREF, g_coronaPreRefDev);
		d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, canonBlend);
		d3d9device->SetRenderState(D3DRS_SRCBLEND, canonSrc);
		d3d9device->SetRenderState(D3DRS_DESTBLEND, canonDst);
		d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, preZw);
		d3d9device->SetRenderState(D3DRS_BLENDOP, canonOp);
	}
	if(dbglog_once("corona_window_close"))
		dbglog("[CoronaExit] frame-boundary close: canonical-blend restore "
		       "(captured blend=%d src=%d dst=%d op=%d -> forced blend=1 "
		       "src=SRCALPHA(5) dst=INVSRCALPHA(6) op=ADD; zwrite=%d captured)",
		       (int)g_coronaPreBlend, (int)g_coronaPreSrcBlend,
		       (int)g_coronaPreDstBlend, (int)g_coronaPreBlendOp,
		       (int)preZw);
	// Snapshot consumed: the next corona window captures afresh at its entry
	// hook. Without this the FIRST-EVER snapshot was replayed at every frame
	// boundary, overwriting pipeApplyWorldBaseline.
	g_coronaPreValid = false;
}

WRAPPER void
CreateRoadsignTexture_RwTextureSetName_orig(RwTexture* texture, char* name) { EAXJMP(0x7F38A0); }

void CreateRoadsignTexture_RwTextureSetName(RwTexture* texture, char* name)
{
	strcpy_s(texture->name, 32, "roadsign");
	TexDefsAssignInfo(texture, TexDefsFindInfo("roadsign"));
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

// Pre-seeded + sanitized (never bare rw-NA): a failed RwRenderStateGet
// leaves the out-param untouched, and restoreMoonAlphaBlendStates pushes
// this straight back into the rw cache — a stale NA(0)/garbage func must
// never reach the device. Same convention as D3D9RenderDual /
// CSkidmarks__Render.
static int alphafunc = rwALPHATESTFUNCTIONGREATEREQUAL;
static void setMoonAlphaBlendStates(void){
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &alphafunc);
	if(alphafunc < rwALPHATESTFUNCTIONNEVER || alphafunc > rwALPHATESTFUNCTIONALWAYS)
		alphafunc = rwALPHATESTFUNCTIONGREATEREQUAL;
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
// SEH-fenced moon-mask bracket (fault-in-0x70D000 restore fix).
// 0x70D000 (moon-mask quad draw) is cdecl with its arguments ALREADY laid
// out on the stack by the vanilla caller (0x713C20-0x713C49 float stores
// into the pre-reserved arg block; the 0x713C4C site was a direct
// `call 0x70D000`) — a plain C wrapper would shift every argument by one
// slot. The asm below therefore re-bases esp onto the arg block ([ebp+8] in
// this frame == the vanilla pre-call esp) before the call, so 0x70D000 sees
// exactly the vanilla stack shape: [its ret][arg0][arg1]... The call's
// return slot is FORCED to overlap this frame's saved return address
// ([ebp+4]) — it is parked in POD statics and restored after the fence on
// BOTH the normal and the faulted (SEH) path. ebp stays valid throughout
// (the asm never touches it and 0x70D000 is ABI-bound to preserve it), the
// fence blocks hold POD only (C2712-safe), and the restore runs
// unconditionally after the fence — a fault in 0x70D000 can no longer skip
// the SEPARATEALPHABLENDENABLE=0 restore (the old naked
// call/call/sequence jumped straight to the game's SEH on a fault).
static DWORD moonMaskSavedRet;
static DWORD moonMaskSavedEsp;

static void __declspec(noinline) renderMoonMaskBody(void)
{
	// INI moonMaskAlpha (default 1 = ON): 0 = the moon-mask draw runs with
	// the game's untouched alpha states (no SEPARATEALPHA/ALPHAFUNC bracket)
	// — bisect lever for this repair. The set and the restore are gated by
	// the SAME expression so they can never get out of pair. Evaluated with
	// `config` directly (no local object inside a function that uses __try:
	// C2712).
	__try{
		if(!config || config->moonMaskAlpha)
			setMoonAlphaBlendStates();
	}__except(EXCEPTION_EXECUTE_HANDLER){
	}
	__try{
		_asm {
			mov  eax, [ebp+4]
			mov  dword ptr [moonMaskSavedRet], eax
			mov  dword ptr [moonMaskSavedEsp], esp
			lea  esp, [ebp+8]
			mov  eax, 0x70D000
			call eax
		}
	}__except(EXCEPTION_EXECUTE_HANDLER){
		// 0x70D000 faulted — swallowed here so the frame recovery +
		// restore below still run.
	}
	// Unconditional frame recovery (normal AND SEH paths): put the parked
	// return address back and re-base esp onto the compiler frame.
	_asm {
		mov  eax, dword ptr [moonMaskSavedRet]
		mov  [ebp+4], eax
		mov  esp, dword ptr [moonMaskSavedEsp]
	}
	__try{
		if(!config || config->moonMaskAlpha)
			restoreMoonAlphaBlendStates();
	}__except(EXCEPTION_EXECUTE_HANDLER){
	}
}
void __declspec(naked) renderMoonMask(void)
{
	_asm {
		call renderMoonMaskBody
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

	// INI skidmarkAlphaFix (default 1 = ON): 0 = neither the alpha-test force
	// nor its restore runs (the draw inherits whatever the previous pass left
	// — the vanilla behaviour) — bisect lever for this repair.
	if(!config || config->skidmarkAlphaFix){
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONALWAYS);
		pipeForceAlphaBlock();
	}

	CSkidmarks__Render_orig();

	if(!config || config->skidmarkAlphaFix){
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)alphafunc);
		pipeForceAlphaBlock();
	}
}

static uint32_t RenderScene_A;
WRAPPER void RenderScene(void) { VARJMP(RenderScene_A); }

void RenderReflectionMap_leeds(void);
void RenderReflectionScene(void);
// DrawDebugEnvMap removed — CLASS 6: dead code, empty body

// FBWP probe (defined below, ~fbWp_ArmOnce): install/refresh the frameBuffer
// write watchpoint at the first device hook — earlier than the old
// first-outdoor-CClouds arming, so pre-CClouds writers are captured too
// (sky audit round-5 rework item 1). No-op unless postfxDumpDebug=1.
static void fbWp_ArmOnce(void);

// ============================================================
// Scene-entry camera-raster repair cache (used by the block in
// RenderScene_before below)
// ============================================================
// FILE-SCOPE ON PURPOSE: this was a function-local static inside
// RenderScene_before, which made it unreachable from the device-reset path
// (depthhook hook_Reset -> postfx OnDeviceReset). RW destroys and recreates
// the game's camera raster on device reset / resolution change, so a cache
// that is never invalidated hands a FREED pointer to RwCameraSetRaster on the
// first scene entry where a non-screen raster is live — and that repair
// branch runs precisely when the screen-sized re-adoption branch CANNOT
// refresh the cache (a foreign raster is already live, so the freed game
// raster is never sighted again until the swap clears).
//
// Two independent staleness guards, so correctness does not hinge on any
// single hook being installed (DepthHook_Install is lazy/one-shot):
//   (1) InvalidateCameraRasterCache() — called from OnDeviceReset (device
//       Reset) and from RenderScene_before when the trusted RsGlobal screen
//       dims change (resolution change);
//   (2) the repair re-checks the ADOPTION-TIME dims (s_camRasGameW/H) against
//       the current screen dims and never dereferences s_camRasGame — the
//       pointer may be freed, so only the cached ints are read (the old code
//       logged s_camRasGame->width/height, itself a use-after-free read).
static RwRaster *s_camRasGame = NULL;   // game's original camera raster
static int s_camRasGameW = 0;           // dims captured at adoption time
static int s_camRasGameH = 0;
static int s_camRasScrW = 0;            // previous frame's trusted screen dims
static int s_camRasScrH = 0;

void
InvalidateCameraRasterCache(void)
{
	if(!s_camRasGame)
		return;	// nothing cached — nothing to drop (also the steady-state
		        // path: this is a no-op for every user every reset)
	if(dbglog_throttle("sky_raster_inv"))
		dbglog("[RENDER] camera-raster cache invalidated (%p %dx%d) — device reset / resolution change",
			(void*)s_camRasGame, s_camRasGameW, s_camRasGameH);
	s_camRasGame = NULL;
	s_camRasGameW = 0;
	s_camRasGameH = 0;
}

// ---- FIX 3 (perf 2026-11-01): shared RSMon frame divider ----------------
// RSMon_Sample costs 31 GetRenderState calls per hit and both frame-edge
// sites ("scene_start" / "frame_end") fired EVERY frame. Gate both to every
// 30th frame through ONE shared latch so the two sites always sample the
// SAME frame and their scene_start/frame_end pairs stay comparable:
//   - scene_start samples when the frame id has advanced >= 30 past the
//     latched id (multi-pass re-entries of the same frame dedupe for free:
//     0 < 30), then records its frame id;
//   - frame_end samples only when it observes that SAME frame id, so it
//     pairs 1:1 with the scene_start that just sampled.
// DebugFrameId() is the widened RWSRCGLOBAL(renderFrame) tick (monotonic,
// epoch-accumulated — no wrap math needed here). POD static only (constant
// init, DLL context). The 5s report logic inside renderstate_monitor.cpp is
// untouched — only these two call sites are gated.
static unsigned s_rsmonFrameDiv = 0xFFFFFFFFu;	// sentinel: sample on first scene_start
static bool
RsMon_SceneStartDue(void)
{
	unsigned f = DebugFrameId();
	if(s_rsmonFrameDiv != 0xFFFFFFFFu && f - s_rsmonFrameDiv < 30u)
		return false;
	s_rsmonFrameDiv = f;
	return true;
}
static bool
RsMon_FrameEndDue(void)
{
	return DebugFrameId() == s_rsmonFrameDiv;
}

bool
RenderScene_before(void*)
{
	// Frame START: canonical world baseline on all three layers, then expire
	// any corona/cone shield window left open by the previous frame (stale
	// deferred flush) and put its captured alpha-test pair back.
	// Baseline first — the window close is the LAST state word here, so the
	// restored pre-corona func/ref wins over the baseline's own block.
	// stateBaselineEnable=0 skips the baseline only; the window close is a
	// safety net and always runs.
	if(!config || config->stateBaselineEnable){
		pipeApplyWorldBaseline("SceneStart");
		static bool s_probeBaselineSceneStart = false;
		if(!s_probeBaselineSceneStart){
			s_probeBaselineSceneStart = true;
			dbglog("[probe] baseline scenestart called");
		}
	}
	CoronaWindow_Close();
	// FIX 3: 1-in-30 frame gate (shared s_rsmonFrameDiv latch — see the
	// helper above); 31 GetRenderState calls saved on the other 29 frames.
	if(RsMon_SceneStartDue())
		RSMon_Sample("scene_start", -1);
	// See-through fix lane (2026-11-02): detection promoted to REPAIR.
	// The standing scene_start receipt (blend rw=0/dev=1, ZWRITE rw=1/dev=0,
	// ZTEST rw=1/dev=0, ZFUNC drv=4/dev=8) is now repaired BEFORE the
	// frame's first draw: device-vs-rw-cache divergence on the blend/Z
	// family forces all layers back to the rw-cache baseline (L2
	// park-and-set + raw L3 pin — the L2 cache agrees with L1, so the
	// pre-draw flush value-compares into a no-op and only this repair
	// re-syncs the device). Runs EVERY frame (6 device Gets on the
	// healthy path, silent); the 1-in-30 RSMon sample above still
	// captures the divergence first as evidence.
	pipeRepairSceneStartSync("scene_start");
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

	// FBWP probe: first-device-hook install point (sky audit rework item 1).
	// The old arming ran at the first outdoor CClouds entry — every write
	// before that (the swap is already LIVE at scene entry) escaped it. Arm
	// as soon as the device exists, before SMAA/FP/scene geometry; idempotent
	// and free once armed. No-op unless postfxDumpDebug=1.
	if(config && config->postfxDumpDebug)
		fbWp_ArmOnce();

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

	// Blank-world root-cause probe (m0170/m0171) → ACTIVE REPAIR (sky audit
	// ROOT CAUSE #1): a mid-frame RwCameraSetRaster(Scene.camera, ...) swap
	// makes the SCENE render into a non-screen raster — the screen never
	// receives the world (gray/white void, only effects-phase sprites on
	// top) and the sky/outdoor pipes look broken. Observed live: geometry-
	// phase reads alternated 2048x1024 / 1920x1080 twice per frame (IBL
	// texture 512x256 == 2048x1024/4, classify RT recreated at 2048x1024)
	// while the real screen is 1920x1080, and a foreign 2048x1024 raster was
	// live on the render camera at scene entry (66 hits). The writer is NOT
	// a mod-owned RwRasterCreate (unidentified — game-side or foreign ASI),
	// so verify + REPAIR the active camera raster here, before any geometry
	// draws. Expected raster = the game's original camera raster: the
	// pointer the game keeps in the render camera's frameBuffer (Scene.
	// camera — the same SA camera pointer pattern used at :1788 and by the
	// FBWP probe below), captured whenever the live raster is screen-sized.
	// A screen-sized sighting (re-)adopts it; only a NON-screen-sized raster
	// (the 2048x1024 foreigner) triggers the RwCameraSetRaster repair.
	// STALENESS (the old comment claimed "device reset / legitimate game swap
	// keeps the cache honest" — nothing did): RW destroys + recreates the
	// camera raster on reset / resolution change, so the cached pointer
	// dangled and the repair could install a FREED raster. The cache now
	// lives at file scope (s_camRasGame, above) and is cleared by
	// InvalidateCameraRasterCache() from postfx OnDeviceReset (depthhook
	// hook_Reset) and from the RsGlobal dim-change check below; the repair
	// additionally re-validates the ADOPTION-TIME dims and never dereferences
	// the cached pointer. Detection logs are throttled ("sky_raster"); the
	// repair itself stays unconditional (gating it would change behavior for
	// current users — see the report).
	RwCamera *cam = Scene.camera;
	RwRaster *rs = cam ? RwCameraGetRaster(cam) : NULL;
	int scrW = RsGlobal ? (int)RsGlobal->MaximumWidth : 0;
	int scrH = RsGlobal ? (int)RsGlobal->MaximumHeight : 0;
	// Same plausibility bounds as forwardplus FpGetTrustedScreenSize: RsGlobal
	// is a fixed exe address (0xC17040), zero/garbage until the game's Rs init
	// runs — defer (no capture, no repair) until real dims exist.
	bool scrOk = (scrW >= 64 && scrW <= 8192 && scrH >= 64 && scrH <= 8192);
	// Resolution-change invalidation — MUST run before adopt/repair below:
	// the trusted screen dims moving means the screen raster changed identity
	// (the cached one was destroyed/recreated), so it must not be installed
	// again — not even if the mode is later switched back to the original
	// dims, where the adoption-time dims would otherwise re-validate a dead
	// pointer. First trusted sighting only records the dims (s_camRasScrW==0).
	// No-op every frame in normal play (dims constant).
	if(scrOk && s_camRasScrW && (s_camRasScrW != scrW || s_camRasScrH != scrH))
		InvalidateCameraRasterCache();
	if(scrOk){
		s_camRasScrW = scrW;
		s_camRasScrH = scrH;
	}
	if(rs && scrOk && rs->width == scrW && rs->height == scrH){
		// screen-sized: this IS the game's main camera raster — (re-)adopt.
		s_camRasGame = rs;
		s_camRasGameW = scrW;
		s_camRasGameH = scrH;
	}else if(rs && cam && scrOk && s_camRasGame &&
	         s_camRasGameW == scrW && s_camRasGameH == scrH){
		// foreign raster live on the render camera — restore the game's.
		// Re-validation: the cache is only ever filled from a screen-sized
		// sighting, so "adoption-time dims == current screen dims" is the
		// liveness contract. A mismatch means the screen mode changed since
		// adoption (raster destroyed/recreated) → fall through to the
		// disarmed log below and wait for a fresh screen-sized sighting.
		// Reads the cached ints only — never s_camRasGame->width/height
		// (the pointer may be freed).
		if(dbglog_throttle("sky_raster"))
			dbglog("[RENDER] scene-entry camRas=%p %dx%d != screen %dx%d — camera-raster swap active, repairing to game raster %p %dx%d",
				(void*)rs, rs->width, rs->height, scrW, scrH,
				(void*)s_camRasGame, s_camRasGameW, s_camRasGameH);
		// INI camRasterRepair (default 1 = ON): 0 = keep detecting (and
		// logging) the swap but leave the foreign raster live — bisect lever
		// for the scene-entry camera-raster repair. The adoption cache above
		// is unaffected, so re-enabling repairs on the next frame.
		if(config && !config->camRasterRepair){
			if(dbglog_throttle("sky_raster_off"))
				dbglog("[RENDER] camRasterRepair=0 — swap NOT repaired (foreign raster left live)");
		}else{
			RwCameraSetRaster(cam, s_camRasGame);
		}
	}else if(rs && scrOk && s_camRasGame && dbglog_throttle("sky_raster")){
		// Cache present but stale (adoption-time dims != current screen dims):
		// repair deliberately DISARMED — installing it could hand the camera a
		// freed raster. Detection log only; the next screen-sized sighting
		// re-adopts and re-arms.
		dbglog("[RENDER] scene-entry camRas=%p %dx%d != screen %dx%d — swap active, repair disarmed (cached game raster %p %dx%d stale)",
			(void*)rs, rs->width, rs->height, scrW, scrH,
			(void*)s_camRasGame, s_camRasGameW, s_camRasGameH);
	}else if(rs && scrOk && dbglog_throttle("sky_raster")){
		// Mismatch but no trusted game raster captured yet (startup window) —
		// detection log only; the first screen-sized sighting arms the repair.
		dbglog("[RENDER] scene-entry camRas=%p %dx%d != screen %dx%d — camera-raster swap active (no game raster captured yet)",
			(void*)rs, rs->width, rs->height, scrW, scrH);
	}

	// V7: Apply ragdoll hierarchy pose BEFORE rendering so bone
	// matrices are live when the scene draws. This fixes the 1-frame
	// stale pose that occurred when writing in RenderScene_after.
	Ragdoll_ApplyPose();

	// P1 native softbody write-back (BlendKeyframe slerp into interp
	// frames) - same before-render slot; fast no-op when disabled.
	SoftbodyPreRender();

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
	// ---- INI bisect lever (effectsAlphaBlock, default 1 = ON) ----
	// 0 = run NONE of this function at any of its call sites (RenderEffects
	// enter + exit, CPostEffects::Render exit, both CCoronas reflection
	// hooks, CClouds::Render): no canonical alpha sweep and no stale-Im2D
	// override sweep. Default 1 is the compiled always-on behaviour.
	if(config && !config->effectsAlphaBlock)
		return;
	// ---- producer gate (whole-function) ----
	// Inside a live additive producer window — CCoronas/CPointLights body
	// (g_inCoronasRender, incl. the SilentPatch-deferred batch), the CClouds
	// pass, or an in-flight forced sprite batch (s_spriteForceValid) — the
	// canonical normal-alpha sweep must not run at all. CCoronas /
	// CPointLights / CClouds glow textures are authored for additive
	// blending (black RGB surround + opaque alpha): rewriting the authored
	// ONE/ONE to SRCALPHA/INVSRCALPHA (whether via the 2/2 substitution or
	// pipeForceAlphaBlock's canonical block) paints the black rectangle
	// around the bright core. Return BEFORE any body write.
	if(g_inCoronasRender || g_inCloudsRender || s_spriteForceValid){
		if(dbglog_once("fxalpha_producer_skip"))
			dbglog("[FxAlpha] %s: producer owns additive state — canonical "
			       "alpha block skipped", tag);
		return;
	}
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
	//
	// ---- additive-leak substitution (outside producer windows only) ----
	// The sweep used to rewrite ANY observed ONE/ONE (2/2) pair to the
	// canonical normal-alpha 5/6 because a CCoronas::Render exit could leak
	// the additive pair into the next consumer (props/pickups/markers
	// ghosted). The whole-function producer gate above now returns before
	// any body write while a corona/cloud/sprite batch owns the state, so
	// this substitution is only the cleanup net for genuine additive leaks
	// from non-sprite writers. ALPHABLENDENABLE stays per the cache vtx flag
	// (pipeForceAlphaBlock pushes it); only the 2/2 pair is rewritten, any
	// other blend (e.g. the modulate corona passes' own sets) passes through
	// untouched. Pre-seeded canonical: a failed Get must not fabricate the
	// additive signature.
	{
		RwUInt32 cSrc = (RwUInt32)D3DBLEND_SRCALPHA;	// pre-seeded canonical
		RwUInt32 cDst = (RwUInt32)D3DBLEND_INVSRCALPHA;
		RwUInt32 wSrc = (RwUInt32)rwBLENDSRCALPHA;
		RwUInt32 wDst = (RwUInt32)rwBLENDINVSRCALPHA;
		RwD3D9GetRenderState(D3DRS_SRCBLEND, &cSrc);	// layer (2)
		RwD3D9GetRenderState(D3DRS_DESTBLEND, &cDst);
		RwRenderStateGet(rwRENDERSTATESRCBLEND, (void*)&wSrc);	// layer (1)
		RwRenderStateGet(rwRENDERSTATEDESTBLEND, (void*)&wDst);
		if((cSrc == (RwUInt32)D3DBLEND_ONE && cDst == (RwUInt32)D3DBLEND_ONE) ||
		   (wSrc == (RwUInt32)rwBLENDONE && wDst == (RwUInt32)rwBLENDONE)){
			// Throttled substitution receipt — the observed blend pair
			// (both cache layers) names the leaking call site in field logs.
			if(dbglog_throttle("blend_subst"))
				dbglog("[FxAlpha] %s: additive ONE/ONE leak substituted with "
				       "canonical normal-alpha — rw(src=%u dst=%u) "
				       "d3d(src=%u dst=%u)",
				       tag,
				       (unsigned)wSrc, (unsigned)wDst,
				       (unsigned)cSrc, (unsigned)cDst);
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
		}
	}
	pipeForceAlphaBlock();
}

// ============================================================
// Corona → Forward+ light feed (Project2DFX-style world lights)
// ============================================================
// Feed each registered corona into the Forward+ light list so extra world
// lights (2DFX-style) light the PBR materials at no extra perf cost. Pure
// memory reads — no render state touched, so the alpha hygiene above is
// unaffected (this section only ADDS calls to the hooks below).
//
// TIMING (verified against the 1.0 US exe, 2026-10-20 — Forward+ light
// collection work item):
//   * The CPointLights registry (0xC3F0D0 count / 0xC3F0E0 entries, 48-byte
//     stride, capacity 32) is CLEARED INLINE by the frame-render glue
//     (`mov dword ptr [0xC3F0D0], 0` @ 0x53E977) BEFORE it calls RenderScene
//     (0x53EABF — our RenderScene_before hook, where ForwardPlus_
//     CullAndUpload drains this buffer). A second, tiny standalone clear
//     helper exists at 0x53D580 but has no call sites.
//   * The per-frame dynamic lights are registered DURING the scene render by
//     CPointLights::Add (0x7000E0, 19 call sites: vehicle head/tail lights,
//     muzzle flashes, fires, explosions, 2DFX lamps in range) and consumed
//     per-entity during the same render by the lighting helper 0x6FFBB0
//     (CRenderer call sites 0x553DE4/0x553EAF); CPointLights::Render
//     (0x7002D0) draws the light sprites from the same registry (called
//     mid-glue 0x53DE6B and from RenderEffects 0x53E21D).
//   * The registry persists until the NEXT frame's clear, so every feed slot
//     below (reflection hooks — sky phase, before the clear; RenderEffects
//     entry — after this frame's scene render) reads a FULLY populated
//     registry. First slot per frame wins (frame-counter latch): the
//     sky-phase slot reads the previous frame's full set and its entries are
//     drained by the SAME frame's CullAndUpload; the RenderEffects slot
//     (when the reflection hooks did not run) rides next frame. One-frame
//     latency for everything, zero lights never. The CPointLights registry
//     is the PAYLOAD (see ForwardPlus_FeedGameLights); the corona walk below
//     is the secondary source (field sessions measured an empty corona
//     pool — all 64 slots id==0 — so the registry feed is what lights up).
//
// LAYOUT (verified — not guessed):
//   CCoronas::ms_aCoronas (aCoronas) = 0xC3E058, 64 entries x 0x3C bytes
//   (plugin-sdk-master/plugin_sa/game_sa/CCoronas.cpp + CRegisteredCorona.h:
//    per-field VALIDATE_OFFSETs and VALIDATE_SIZE(CRegisteredCorona, 0x3C);
//    cross-checked against gta-reversed-master source/game_sa/Coronas.h +
//    RegisteredCorona.h and GTA SA RE/source/game_sa/ — all agree:
//    0xC3E058 / 64 / stride 0x3C. plugin-sdk is the repo's own include
//    path reference for SA v1.0 US.)
//   Entry offsets (CRegisteredCorona):
//     0x00 m_vPosn (CVector x,y,z) | 0x0C m_dwId (0 = inactive slot)
//     0x10 m_pTexture              | 0x14 m_fSize (world size = radius)
//     0x18 m_fAngle | 0x1C m_fFarClip | 0x20 m_fNearClip
//     0x24 m_fHeightAboveGround    | 0x28 m_fFadeSpeed
//     0x2C m_Color (r,g,b,intensity bytes) | 0x30 m_nFadeState
//     0x31 m_bRegisteredThisFrame  | 0x32 m_nFlareType
//     0x33 m_bUsesReflection       | 0x34 flags (checkObstacles/offScreen)
//     0x35 m_bJustCreated          | 0x36 flags (flash/below/whiteCore/...)
//     0x38 m_pAttachedTo (CEntity*)
//   The entry does NOT encode eCoronaType (the type only selects the sprite
//   texture at registration), so every light is fed as type 0 = point.
//   CTimer::m_FrameCounter = 0xB7CB4C (plugin-sdk CTimer.cpp + gta-reversed
//   Timer.h — same block as the repo's CTimer__ms_fTimeStep at 0xB7CB5C).
// ============================================================

// Forward+ light feed API. A parallel lane declares this in forwardplus.h
// with this exact signature; when that lands the identical redeclaration
// here is harmless. Type 0 = point light.
extern void ForwardPlus_AddLight(float x, float y, float z, float radius, float r, float g, float b, float type);
extern int  ForwardPlus_FeedGameLights(void);   // registry -> AddLight buffer (see forwardplus.h)

// CRegisteredCorona, SA v1.0 US — offsets per plugin-sdk VALIDATE_OFFSET
// block (see layout comment above).
struct CoronaFeedEntry {
	float    x, y, z;             // 0x00 m_vPosn
	uint32_t id;                  // 0x0C m_dwId (unique per corona; 0 = inactive)
	void    *texture;             // 0x10 m_pTexture
	float    size;                // 0x14 m_fSize (world size = light radius)
	float    angle;               // 0x18 m_fAngle
	float    farClip;             // 0x1C m_fFarClip
	float    nearClip;            // 0x20 m_fNearClip
	float    heightAboveGround;   // 0x24 m_fHeightAboveGround
	float    fadeSpeed;           // 0x28 m_fFadeSpeed
	uint8_t  r, g, b, intensity;  // 0x2C m_Color (CRGBA: rgb + intensity byte)
	uint8_t  fadeState;           // 0x30 m_nFadeState
	uint8_t  registeredThisFrame; // 0x31 m_bRegisteredThisFrame
	uint8_t  flareType;           // 0x32 m_nFlareType
	uint8_t  usesReflection;      // 0x33 m_bUsesReflection
	uint8_t  flagsLo;             // 0x34 bit0=checkObstacles bit1=offScreen
	uint8_t  justCreated;         // 0x35 m_bJustCreated
	uint8_t  flagsHi;             // 0x36 bit0=flashWhileFading .. bit4=attached
	uint8_t  pad;                 // 0x37
	void    *attachedTo;          // 0x38 m_pAttachedTo
}; // 0x3C
static_assert(sizeof(CoronaFeedEntry) == 0x3C, "CRegisteredCorona stride mismatch");

// Walk CCoronas::ms_aCoronas and feed every light-worthy corona to
// Forward+. Once per game frame (CTimer::m_FrameCounter latch): the corona
// hooks below can run several times per frame (RenderReflections has two
// call sites) and each corona must be fed exactly once. Safe to call from
// any/all of them. ForwardPlus_AddLight caps at 256/frame internally
// (overflow dropped) — the pool is only 64 entries so the cap never bites
// here.
static void
coronaFeedForwardPlus(void)
{
	static uint32_t s_fedFrame = 0xFFFFFFFFu;
	uint32_t frame = *(uint32_t*)0xB7CB4C;	// CTimer::m_FrameCounter
	if(frame == s_fedFrame)
		return;
	s_fedFrame = frame;

	// 1) CPointLights registry — the REAL light source. The corona walk
	//    below feeds only the VISUAL glows, and field sessions measured an
	//    empty corona pool (all 64 slots id==0 — [CoronaFeed] rej(id0=64) —
	//    a source condition, not a feed bug), which is why the feed
	//    delivered 0: the registered point lights (vehicle head/tail
	//    lights, muzzle flashes, fires, 2DFX lamps in range) were never
	//    read here at all. Registry entries go FIRST so FpDedupAddLights
	//    keeps the registry copy (true attenuation radius) over a corona's
	//    visual size at the same 2DFX position.
	int regFed = ForwardPlus_FeedGameLights();

	CoronaFeedEntry *coronas = (CoronaFeedEntry*)0xC3E058;	// CCoronas::ms_aCoronas
	int numCoronas = (int)*(uint32_t*)0xC3E038;	// CCoronas::NumCoronas (dword — was wrongly read as uint16_t, printed pool=65524)
	int added = 0, rejId0 = 0, rejSize = 0, rejRgb = 0, rejInt = 0;
	for(int i = 0; i < 64; i++){	// MAX_CORONAS
		CoronaFeedEntry &c = coronas[i];
		if(c.id == 0){ rejId0++; continue; }			// inactive pool slot
		if(!(c.size > 0.0f) || !(c.size < 500.0f)){ rejSize++; continue; }	// sane radius (rejects NaN too)
		if((c.r | c.g | c.b) == 0){ rejRgb++; continue; }	// non-zero colour
		if(c.intensity == 0){ rejInt++; continue; }		// zero intensity = invisible
		// Colours as 0-1 floats with the corona intensity folded in —
		// AddLight has no separate intensity channel, and this matches the
		// 0-1 float colours CPointLights feeds the existing collector.
		float ci = (float)c.intensity * (1.0f / 255.0f);
		float cr = (float)c.r * (1.0f / 255.0f) * ci;
		float cg = (float)c.g * (1.0f / 255.0f) * ci;
		float cb = (float)c.b * (1.0f / 255.0f) * ci;
		ForwardPlus_AddLight(c.x, c.y, c.z, c.size, cr, cg, cb, 0.0f);	// type 0 = point
		added++;
	}
	// ---- corona pool counter repair (log-forensics 2026-10-30 #5) ----
	// 0xC3E038 (CCoronas::NumCoronas) is a pure ACCOUNTING scalar in SA
	// 1.0 US — exe scan: its only readers are its own RMW chains
	// (new-slot ++ in RegisterCorona @ 0x6FB75E; kill-path decs @
	// 0x6FA015 / 0x6FA1D8 / 0x6FB88C; Init zero @ 0x6F9EEE) and every
	// slot search uses FIXED 64 bounds (0x6FB6E0/0x6FB710 loops,
	// CCoronas::Update 0x6FA29F..0x6FB8), so it is never a search bound
	// and repairing it cannot affect rendering. The kill paths dec once
	// per deactivation; with the registration side never feeding (this
	// run: rej id0=64 at every one of 82 samples — the pool is
	// legitimately EMPTY, gym interior at daylight, so fed=0 is a SOURCE
	// condition) the counter decayed 0 -> -80730: leak-shaped, but
	// cosmetic accounting drift, not a pool-depth leak. Repair: keep it
	// equal to the true active-slot count (the walk above is ground
	// truth) so it can never go negative, and make both facts (empty
	// pool + counter skew) first-class receipts.
	int activeSlots = 64 - rejId0;	// ground truth: slots with id != 0
	if(numCoronas != activeSlots){
		if(dbglog_once("corona_pool_fix") || dbglog_throttle("corona_pool_fix"))
			dbglog("[CoronaPool] counter %d != active %d (id!=0 slots)%s -> repaired to %d (decays without matching registrations; fed=%d regFed=%d)",
			       numCoronas, activeSlots,
			       (numCoronas < 0 || numCoronas > 64) ? " [OUT OF RANGE]" : "",
			       activeSlots, added, regFed);
		*(int*)0xC3E038 = activeSlots;
		numCoronas = activeSlots;
	}
	if(dbglog_throttle("corona_feed"))
		dbglog("[CoronaFeed] pool=%d fed=%d rej(id0=%d sz=%d rgb0=%d int0=%d) regFed=%d active=%d fpEnable=%d%s",
		       numCoronas, added, rejId0, rejSize, rejRgb, rejInt, regFed,
		       activeSlots, (config && config->forwardPlusEnable) ? 1 : 0,
		       (added == 0 && regFed == 0)
				? " (fed=0: pool empty - no corona registrations in range; registry feed idle/filtered)"
				: "");
}

// ---- RenderEffects (app_game.cpp, 0x53E170; only call site 0x53EAD3) ----
// Runs CCoronas::Render, g_fx.Render (particles), CGlass, CWeaponEffects,
// CSpecialFX, search lights and fog effects, then CPostEffects::Render —
// i.e. the whole effects phase plus the frame's postfx in one call.
static void (*RenderEffects_orig)(void);
static void
RenderEffects_hook(void)
{
	// Corona + CPointLights → Forward+ light feed (pure reads, no state).
	// Slot detail: the reflection-hook slots (sky phase) normally win the
	// frame latch and read the PREVIOUS frame's fully populated registry
	// (drained by THIS frame's CullAndUpload); when they do not run, this
	// slot reads the current frame's registry (populated during RenderScene,
	// cleared next frame) and rides the next CullAndUpload. See
	// coronaFeedForwardPlus. Alpha hygiene below is untouched.
	coronaFeedForwardPlus();
	effectsForceAlphaBlock("RenderEffects.enter");
	RenderEffects_orig();
	// Re-align after postfx (CPostEffects::Render is the last state writer
	// of the frame): HUD/radar/dialog sprites draw after this and would
	// otherwise inherit whatever layer split postfx left behind.
	effectsForceAlphaBlock("RenderEffects.exit");
	// Frame boundary: close any corona/cone shield window still open (SP
	// defers that flush past CCoronas::Render) and re-push the canonical
	// world baseline before the HUD/postfx tail consumes it.
	CoronaWindow_Close();
	if(!config || config->stateBaselineEnable){
		pipeApplyWorldBaseline("EffectsExit");
		static bool s_probeBaselineRenderFx = false;
		if(!s_probeBaselineRenderFx){
			s_probeBaselineRenderFx = true;
			dbglog("[probe] baseline effects_exit_renderfx called");
		}
	}
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
	// Frame boundary: expire any deferred corona/cone window + re-push the
	// canonical world baseline (postfx is the frame's last state writer).
	CoronaWindow_Close();
	if(!config || config->stateBaselineEnable){
		pipeApplyWorldBaseline("EffectsExit");
		static bool s_probeBaselinePostFx = false;
		if(!s_probeBaselinePostFx){
			s_probeBaselinePostFx = true;
			dbglog("[probe] baseline effects_exit_postfx called");
		}
	}
	// AFTER-repair receipt (three-layer restore evidence): read the rw
	// cache + raw device blend/ZWrite state the next draw would inherit.
	// Throttle tag "postfx_after" — a free dbglog_throttle bucket (the
	// "fx_alpha" BEFORE-log tag shares bucket 49 with per-frame tags).
	// POD locals only in the __try scope (C2712-safe).
	__try{
		if(dbglog_throttle("postfx_after")){
			RwUInt32 rVtx = 0, rSrc = 0, rDst = 0, rZw = 0;
			RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)&rVtx);
			RwRenderStateGet(rwRENDERSTATESRCBLEND, (void*)&rSrc);
			RwRenderStateGet(rwRENDERSTATEDESTBLEND, (void*)&rDst);
			RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, (void*)&rZw);
			DWORD dVtx = 0, dSrc = 0, dDst = 0, dZw = 0;
			if(d3d9device){
				d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &dVtx);
				d3d9device->GetRenderState(D3DRS_SRCBLEND, &dSrc);
				d3d9device->GetRenderState(D3DRS_DESTBLEND, &dDst);
				d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &dZw);
			}
			dbglog("[FxAlpha] PostFx.repaired: rw(vtx=%u src=%u dst=%u zw=%u) dev(vtx=%u src=%u dst=%u zw=%u)",
				(unsigned)rVtx, (unsigned)rSrc, (unsigned)rDst, (unsigned)rZw,
				(unsigned)dVtx, (unsigned)dSrc, (unsigned)dDst, (unsigned)dZw);
		}
	}__except(EXCEPTION_EXECUTE_HANDLER){
		// read-only diagnostics — never propagate into the postfx exit path
	}
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
//
// Interleave audit (corona-shield round): can any effectsForceAlphaBlock
// call site execute BETWEEN CCoronas::Render's blend-set and its draws?
// Frame flow by call-site address (all in the same caller, ascending):
//   CClouds::Render 0x53DCA2/0x53DFA0
//   → CCoronas::RenderReflections 0x53DCAC/0x53DFD8   [sweep: BEFORE coronas]
//   → CCoronas::RenderSunReflection 0x53E12B          [sweep: BEFORE coronas]
//   → RenderEffects 0x53EAD3 (body 0x53E170+):
//       enter sweep at entry                          [sweep: BEFORE coronas]
//       CCoronas::Render 0x53E18E (blend-set + draws INSIDE, no hooked
//         call between — the entry shield at 0x6FAEC0 is the only writer)
//       ... particles/glass/weapon FX ...
//       CPostEffects::Render tail 0x53E227            [sweep: AFTER coronas]
//       exit sweep on return                          [sweep: AFTER coronas]
// VERDICTS: RenderEffects.enter / Coronas.Reflections /
// Coronas.SunReflection fire strictly BEFORE CCoronas::Render's blend-set;
// RenderEffects.exit / PostFx.exit fire strictly AFTER it (postfx is the
// frame's last state writer). NO call site can interleave between the
// corona blend-set and the corona draw, so no read-back gating was added
// — the sweeps also derive SRCBLEND/DESTBLEND/ALPHABLENDENABLE from the
// rw cache (pipeForceAlphaBlock), so they push the cache's blend, never a
// hardcoded non-additive one, and the corona renderer's own sets follow.
// The black-quad leak must therefore originate INSIDE CCoronas::Render's
// window — covered by the entry blend shield above, not by these sweeps.
// ============================================================

// ---- CCoronas::RenderReflections (wet-ground glows) ----
static void (*CCoronas__RenderReflections_orig)(void);
static void
CCoronas__RenderReflections_hook(void)
{
	coronaFeedForwardPlus();	// light feed; no-op if already fed this frame
	effectsForceAlphaBlock("Coronas.Reflections");
	CCoronas__RenderReflections_orig();
}

// ---- CCoronas::RenderSunReflection (sun on water) ----
static void (*CCoronas__RenderSunReflection_orig)(void);
static void
CCoronas__RenderSunReflection_hook(void)
{
	coronaFeedForwardPlus();	// light feed; no-op if already fed this frame
	effectsForceAlphaBlock("Coronas.SunReflection");
	CCoronas__RenderSunReflection_orig();
}

// ---- CCoronas::Render (0x6FAEC0) entry blend shield + exit probe ----
// Black-quad symptom (user-confirmed): every light sprite/corona draws as
// a BLACK QUAD + dark overlay — the signature of coronas blending with
// normal alpha (SRCALPHA/INVSRCALPHA) instead of vanilla additive
// (ONE/ONE): the glow texture's RGB=0 background must add nothing, while
// a 5/6 pair writes the opaque-alpha black surround.
// H3 rework: the old probe intercepted the single vanilla call site
// (RenderEffects 0x53E18E) and never logged once in a 17k-line session.
// Now hooked at the CCoronas::Render ENTRY POINT so it fires regardless
// of caller (cutscene + gameplay paths alike), via a 20-byte trampoline:
// the stolen prologue (sub esp,0xB8 / mov eax,[0xC1703C] / mov
// ecx,[eax+0x60] / mov eax,[ecx+0xC] / mov ecx,[ecx+0x10] — all
// position-independent, verified against the 1.0 US exe) runs from an RWX
// copy and jmps back to 0x6FAED4, so the original body's ret lands back
// in this hook.
//
// SHIELD (entry, BEFORE the renderer): read the live device blend state
// (GetRenderState is ground truth — the RwD3D9 wrapper cache can
// disagree). If it looks like the broken normal-alpha case — blend ON
// with DESTBLEND=INVSRCALPHA, or blend OFF entirely — i.e. NOT additive —
// force vanilla corona additive (ALPHABLENDENABLE=TRUE, SRCBLEND=ONE,
// DESTBLEND=ONE, BLENDOP=ADD, ZWRITEENABLE=FALSE) through all three
// layers (rw cache / RwD3D9 driver cache / raw device). Seeding the rw
// cache with ONE/ONE matches the renderer's own rwBLENDONE/rwBLENDONE
// sets; those may now no-op against an unchanged cache, but the raw L3
// pin has already delivered the same pair, so the device can never stay
// on the 5/6 split — the cache-noop/device-drift mechanism behind the
// black quads is still closed by the raw mirror.
// Deliberately non-additive corona types (modulate-blended shadow /
// night-light passes) set their own blend INSIDE CCoronas::Render after
// this point, so an entry-state force cannot clobber them; an entry
// state that is already additive (dst=ONE) is left untouched — the
// shield only fires on the broken normal-alpha signature.
//
// ZTEST ENFORCEMENT (always-on-top round): the vanilla body sets ZTEST
// itself at 0x6FB17C (the coronaZtest patch site: 1=PC `call [ecx+0x20]`
// re-patched, 0=PS2 NOP) through the rw cache ONLY — a raw device
// ZENABLE=FALSE left by any upstream writer (DepthHook Suspend, postfx
// raw geom states, foreign mods) no-ops that set against an
// already-TRUE rw cache, and coronas draw through geometry. The shield
// now reads D3DRS_ZENABLE as a 7th probe state and repairs the device
// to the coronaZtest target (0=PS2 off, 1/-1=PC vanilla on) through all
// three layers — inside the broken-blend force AND standalone when the
// blend is healthy but ZTEST drifted (the always-on-top symptom can
// occur without the blend signature). Body-internal ZTEST sets still
// transition normally afterwards (entry force only syncs the layers).
// Throttle tag "corona_shield" (new bucket — the shared per-frame
// buckets kept the old probe's window permanently shut).
//
// PROBE (after the original): this is the state the corona quads
// actually drew with (vanilla sets its blend right before drawing).
// Kept from the breadcrumb rounds; read-only.
static void (*CCoronas__Render_trampoline)(void);
static void
CCoronas__Render_blendprobe_hook(void)
{
	// POD locals only; the shared pre-hook alpha-test capture is hoisted to
	// function scope (outside BOTH __try blocks — C2712-safe) so the exit
	// restore fence below can restore it. Two SEH fences: the first wraps
	// shield + trampoline + probe, the second wraps ONLY the exit restore —
	// a fault inside the first (e.g. the trampoline) is swallowed there and
	// can no longer skip the restore.
	DWORD preAtest = TRUE;	// contract default (alpha test on) if never captured
	// SAFETY-CLEAR (stuck-flag guard): expire ANY window that never reached
	// a flush — e.g. the previous frame's trampoline faulted (bodyDone
	// unset) or no sprite consumer flushed since. Plain POD writes on the
	// render thread; no flush draw can interleave here. If the previous
	// window's deferred batch is still pending, its first flush after this
	// entry re-arm is simply unshielded (accepted defensive loss).
	g_inCoronasRender = false;
	g_coronaBodyDone = false;
	// Draw-time raw shield scope ON from this point. With SilentPatchSA
	// loaded the vanilla tail-flush is gone (0x6FB621 word-patched to RET),
	// so the flag SURVIVES this function's exit; SpriteFlushIndexedDraw_hook
	// closes the window at the first flush after the body (g_coronaBodyDone).
	g_inCoronasRender = true;
	__try{
		// ---- CORONA BLEND SHIELD (entry, BEFORE the renderer) ----
		if(d3d9device){
			DWORD blendEn = 0, srcB = 0, dstB = 0, bop = 0, zw = 0, atest = 0;
			DWORD ztest = 0;
			DWORD afn = 0, aref = 0, sepA = 0, cwr = 0;
			int rwFn = rwALPHATESTFUNCTIONGREATEREQUAL;
			int rwRef = 0;
			d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &blendEn);
			d3d9device->GetRenderState(D3DRS_SRCBLEND, &srcB);
			d3d9device->GetRenderState(D3DRS_DESTBLEND, &dstB);
			d3d9device->GetRenderState(D3DRS_BLENDOP, &bop);
			d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &zw);
			d3d9device->GetRenderState(D3DRS_ALPHATESTENABLE, &atest);
			d3d9device->GetRenderState(D3DRS_ZENABLE, &ztest);
			// Pre-window alpha-test + raw-only states: the ones the per-draw
			// shield (coronaDrawRawShield) now overwrites inside the window.
			// Captured here — both layers for func/ref, device only for the
			// two states that have no rw twin — and put back at the exit and
			// by CoronaWindow_Close.
			d3d9device->GetRenderState(D3DRS_ALPHAFUNC, &afn);
			d3d9device->GetRenderState(D3DRS_ALPHAREF, &aref);
			d3d9device->GetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, &sepA);
			d3d9device->GetRenderState(D3DRS_COLORWRITEENABLE, &cwr);
			RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &rwFn);
			RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, &rwRef);
			g_coronaPreFuncRw = rwFn;
			g_coronaPreRefRw = rwRef;
			g_coronaPreFuncDev = afn;
			g_coronaPreRefDev = aref;
			g_coronaPreSepAlpha = sepA;
			g_coronaPreColorWrite = cwr;
			// Pre-window BLEND block: the exit restore / CoronaWindow_Close
			// put ALPHABLENDENABLE back to what the device held here instead
			// of hard-setting FALSE (the frame-long REPLACE leak). The other
			// four are already probed above; capturing them costs nothing and
			// makes the exit log report the true pre-window state.
			g_coronaPreBlend = blendEn;
			g_coronaPreSrcBlend = srcB;
			g_coronaPreDstBlend = dstB;
			g_coronaPreBlendOp = bop;
			g_coronaPreZWrite = zw;
			g_coronaPreValid = true;
			preAtest = atest;	// pre-hook value — restored at exit (see below)
			// Broken normal-alpha signature: blend live with
			// DESTBLEND=INVSRCALPHA, or blend disabled entirely.
			// Already-additive (dst=ONE) is left untouched.
			DWORD broken = blendEn ?
				(dstB == (DWORD)D3DBLEND_INVSRCALPHA) : TRUE;
			// ZTEST target from the coronaZtest config (clamped [-1..1]
			// at INI load, main.cpp readIniFile): 0 = PS2 (z-test off),
			// 1/-1 = PC vanilla (z-test on). Drift = device disagrees.
			int ztTarget = (config && config->coronaZtest == 0) ? FALSE : TRUE;
			int ztestDrift = ((int)ztest != ztTarget);
			// ---- NORMALIZE (always — additive or not): blend-op only ----
			// ALPHAFUNC/ALPHAREF/SEPARATEALPHABLENDENABLE/COLORWRITEENABLE
			// moved to coronaDrawRawShield: they are applied PER DRAW,
			// immediately before each rasterisation inside the window, and
			// the pre-window values captured above are restored at the exit.
			// The old once-per-window force here never restored them, leaking
			// GREATEREQUAL@0 / a blanking colour mask / separate-alpha into
			// the rest of the frame. BLENDOP has no rw render state, so it
			// rides L2+L3; the additive blend FACTORS are not touched here.
			RwD3D9SetRenderState(D3DRS_BLENDOP, (DWORD)D3DBLENDOP_ADD);
			d3d9device->SetRenderState(D3DRS_BLENDOP, (DWORD)D3DBLENDOP_ADD);
			if(broken){
				// ---- PARK-AND-SET (pipeForceCullMode idiom,
				// pipelinecommon.cpp:1114): for ALPHABLENDENABLE /
				// SRCBLEND / DESTBLEND / ZWRITEENABLE write the OPPOSITE
				// value through L1 (rw cache) + L2 (driver cache) FIRST —
				// a guaranteed cache TRANSITION even when a cache already
				// holds the target (the double cache-hit no-op that left
				// L3 stale at blend=0) — then the ONE/ONE target through
				// L1+L2, then raw L3 (repairs drift the caches can't see).
				// No draw happens between park and set, so the transient
				// never reaches a rasteriser. Seeding the rw cache with
				// ONE/ONE matches the renderer's own additive sets; their
				// cache-hit no-op is harmless because L3 is already ONE/ONE.
				//
				// ALPHABLENDENABLE: park FALSE -> TRUE
				RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
				RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
				RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
				RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
				d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
				// SRCBLEND: park INVSRCALPHA -> ONE (vanilla additive)
				RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDINVSRCALPHA);
				RwD3D9SetRenderState(D3DRS_SRCBLEND, (DWORD)D3DBLEND_INVSRCALPHA);
				RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
				RwD3D9SetRenderState(D3DRS_SRCBLEND, (DWORD)D3DBLEND_ONE);
				d3d9device->SetRenderState(D3DRS_SRCBLEND, (DWORD)D3DBLEND_ONE);
				// DESTBLEND: park ZERO -> ONE (additive)
				RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
				RwD3D9SetRenderState(D3DRS_DESTBLEND, (DWORD)D3DBLEND_ZERO);
				RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
				RwD3D9SetRenderState(D3DRS_DESTBLEND, (DWORD)D3DBLEND_ONE);
				d3d9device->SetRenderState(D3DRS_DESTBLEND, (DWORD)D3DBLEND_ONE);
				// ZWRITEENABLE: park TRUE -> FALSE
				RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
				RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
				RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
				RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
				d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
				// ZTEST alongside the additive force (coronaZtest
				// wiring) — same three layers, only on drift.
				if(ztestDrift){
					RwRenderStateSet(rwRENDERSTATEZTESTENABLE,
						(void*)(ztTarget ? TRUE : FALSE));
					RwD3D9SetRenderState(D3DRS_ZENABLE, (DWORD)ztTarget);
					d3d9device->SetRenderState(D3DRS_ZENABLE, (DWORD)ztTarget);
				}
				// First-hit one-shot (unthrottled) + the 2s bucket line, so
				// the run's first [CoronaShield] force can never be starved.
				if(dbglog_once("corona_shield_first") || dbglog_throttle("corona_shield"))
					dbglog("[CoronaShield] forced additive ONE/ONE, was src=%d dst=%d blend=%d op=%d zw=%d atest=%d ztest=%d->%d",
					       (int)srcB, (int)dstB, (int)blendEn, (int)bop,
					       (int)zw, (int)atest, (int)ztest, ztTarget);
			}else if(ztestDrift){
				// Blend healthy but ZTEST drifted — standalone
				// repair (same three layers), so the always-on-top
				// symptom cannot hide behind a correct blend state.
				RwRenderStateSet(rwRENDERSTATEZTESTENABLE,
					(void*)(ztTarget ? TRUE : FALSE));
				RwD3D9SetRenderState(D3DRS_ZENABLE, (DWORD)ztTarget);
				d3d9device->SetRenderState(D3DRS_ZENABLE, (DWORD)ztTarget);
				if(dbglog_throttle("corona_ztest"))
					dbglog("[CoronaZ] forced ztest=%d, was=%d (coronaZtest=%d)",
					       ztTarget, (int)ztest,
					       config ? config->coronaZtest : -99);
			}
		}
		if(CCoronas__Render_trampoline)
			CCoronas__Render_trampoline();
		// Body complete — arm the flush hook's window-close: the next
		// buffered flush (SilentPatch defers the corona flush past this
		// return) draws the pending corona batch and then closes the
		// window. Left FALSE if the trampoline faulted, so a fault path
		// still closes the window at the next flush (POD write, SEH-safe).
		g_coronaBodyDone = true;
		// Probe AFTER orig: this is the state the corona quads actually
		// drew with (vanilla sets its blend right before drawing).
		if(d3d9device){
			DWORD blendEn = 0, srcB = 0, dstB = 0;
			d3d9device->GetRenderState(D3DRS_ALPHABLENDENABLE, &blendEn);
			d3d9device->GetRenderState(D3DRS_SRCBLEND, &srcB);
			d3d9device->GetRenderState(D3DRS_DESTBLEND, &dstB);
			if(dbglog_throttle("corona_probe2"))
				dbglog("[CoronaBlend] dev blend=%d src=%d dst=%d (D3DBLEND: 1=ZERO 2=ONE 4=INVSRCALPHA 5=SRCALPHA 6=INVSRCALPHA; REPLACE = src=ONE dst=ZERO blend=0)",
					(int)blendEn, (int)srcB, (int)dstB);
		}
	}__except(EXCEPTION_EXECUTE_HANDLER){
		// shield + trampoline + read-only probe diagnostics — never
		// propagate a fault into the corona render path. The exit restore
		// below sits in its OWN fence, so a fault here still runs it.
	}
	__try{
		// ---- EXIT RESTORE (corona-exit leak fix) ----
		// Vanilla CCoronas::Render (0x6FAEC0) exits restoring NOTHING
		// (gta-reversed: "NOTE/BUG: Renderstates not restored?") — it leaves
		// the corona additive block (ONE/ONE + VERTEXALPHA TRUE + ZWRITE
		// FALSE) live in every layer, and the next consumer without an
		// entry contract of its own (default-pipe atomics: props/pickups/
		// markers) drew with it. Mirror the restore vanilla
		// CCoronas::RenderReflections (0x6FB630) performs: canonical
		// normal-alpha + depth-on + blend-op ADD + alpha-test back to the
		// pre-hook value, through L1 (rw cache) + L2 (RW driver cache)
		// ONLY — deliberately NO raw L3 write for this normal-alpha block:
		// a raw device write can be silently undone by the cache-hit skip
		// (RW later re-pushing a stale cache value with no dirty flag leaves
		// the device stale), while an L2 set marks pending[] dirty so RW's
		// own pre-draw flush delivers the canonical state to the device.
		// (The captured-state restore below is the exception: it re-pins the
		// four shield-overwritten states raw as well.)
		// BLENDOP has no rw render state and the shield forced ADD through
		// L2/L3 (entry ~2354); ALPHATESTENABLE likewise has none and the
		// shield only reads it — restore the captured pre-hook value
		// (preAtest, TRUE default) so the next consumer inherits exactly
		// what CCoronas::Render found. One-shot UNTHROTTLED log (tag
		// "corona_exit" via a plain static bool — a throttled bucket starved
		// this line for ~20 s in the 19:06 run).
		{
			static bool coronaExitLogged = false;	// zero-init POD, C2712-safe
			// ALPHABLENDENABLE / VERTEXALPHAENABLE: restore the PRE-WINDOW
			// device value captured at this hook's entry. The old hard FALSE
			// (REPLACE) is exactly what leaked blend-off into the rest of the
			// frame and painted the next flush's sprites as black quads; the
			// fallback is TRUE when no capture ran (g_coronaPreBlend init).
			DWORD preBlendDev = g_coronaPreBlend ? (DWORD)TRUE : (DWORD)FALSE;
			RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)preBlendDev);
			RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
			RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
			RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
			RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, preBlendDev);
			RwD3D9SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			RwD3D9SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
			RwD3D9SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
			RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, preAtest);
			RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
			// Raw L3 mirror for ZWRITE (the one exception to the L1+L2-only
			// rule above): the driver cache can already claim TRUE while the
			// device still holds FALSE (a cache-hit skip swallows the L2
			// set), which is the 42 `rw=1 drv=1 dev=0` ped/vehicle receipts
			// in the 01:12 run. Pin the device explicitly.
			if(d3d9device)
				d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
			RwD3D9SetRenderState(D3DRS_ZENABLE, TRUE);
			// ---- pre-window ALPHAFUNC/ALPHAREF + raw-only states ----
			// The four states the per-draw shield overwrites. Restored to the
			// values captured at this hook's entry: func/ref through L1 (rw
			// cache) + L2 + raw L3; SEPARATEALPHABLENDENABLE and
			// COLORWRITEENABLE have no rw twin, so L2 + L3 only.
			// ALPHABLENDENABLE is instead restored to the captured pre-window
			// device value above; SRCBLEND/DESTBLEND keep the canonical
			// normal-alpha pair (the normal-alpha restore above owns them).
			if(g_coronaPreValid){
				RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)g_coronaPreFuncRw);
				RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)g_coronaPreRefRw);
				RwD3D9SetRenderState(D3DRS_ALPHAFUNC, g_coronaPreFuncDev);
				RwD3D9SetRenderState(D3DRS_ALPHAREF, g_coronaPreRefDev);
				RwD3D9SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, g_coronaPreSepAlpha);
				RwD3D9SetRenderState(D3DRS_COLORWRITEENABLE, g_coronaPreColorWrite);
				if(d3d9device){
					d3d9device->SetRenderState(D3DRS_ALPHAFUNC, g_coronaPreFuncDev);
					d3d9device->SetRenderState(D3DRS_ALPHAREF, g_coronaPreRefDev);
					d3d9device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, g_coronaPreSepAlpha);
					d3d9device->SetRenderState(D3DRS_COLORWRITEENABLE, g_coronaPreColorWrite);
				}
			}
			// One-shot, UNTHROTTLED (dbglog_throttle starved this line for
			// ~20 s in the 19:06 run): the first [CoronaExit] of the run
			// must always be visible.
			if(!coronaExitLogged){
				coronaExitLogged = true;
				dbglog("[CoronaExit] restored normal-alpha "
				       "(src=SRCALPHA dst=INVSRCALPHA op=ADD zwrite=1 "
				       "ztest=1) + pre-window blend=%d vtxalpha=%d src=%d "
				       "dst=%d op=%d zw=%d atest=%d fn=%d ref=%d "
				       "sepA=%u cwr=0x%X after CCoronas::Render",
				       (int)g_coronaPreBlend, (int)preBlendDev,
				       (int)g_coronaPreSrcBlend, (int)g_coronaPreDstBlend,
				       (int)g_coronaPreBlendOp, (int)g_coronaPreZWrite,
				       (int)preAtest, (int)g_coronaPreFuncRw,
				       (int)g_coronaPreRefRw,
				       (unsigned)g_coronaPreSepAlpha,
				       (unsigned)g_coronaPreColorWrite);
			}
		}
	}__except(EXCEPTION_EXECUTE_HANDLER){
		// exit-restore containment — the restore is best-effort; a fault
		// inside it must not propagate into the corona render path either
	}
	// NOTE: g_inCoronasRender is deliberately NOT cleared at this exit.
	// With SilentPatchSA loaded the corona batch is flushed AFTER this
	// function returns (SP word-patches the vanilla tail-jmp 0x6FB621 ->
	// RET; vanilla would tail-jmp into the flush inside this window), so
	// the window must survive the return. SpriteFlushIndexedDraw_hook
	// closes it at the first post-body flush (g_coronaBodyDone); the
	// safety-clear at this hook's entry expires any window that never
	// reaches a flush. Both flags are POD and unconditionally re-armed
	// on the next entry, so both SEH fault paths leave a bounded state.
}

// ============================================================
// DEBUG-ONLY: hardware write-watchpoint on &Scene.camera->frameBuffer
// ============================================================
// RwCameraSetRaster is an RW 3.6 SDK INLINE MACRO (external/d3d9/rwcore.h:
// 4525 -> camera->frameBuffer = raster) so the mystery mid-frame swap to a
// 2048x1024 raster (see the "sky_raster" repair above) cannot be hooked as
// a call. Instead: claim DR0 as a 4-byte WRITE watchpoint on the
// frameBuffer field itself and let a dedicated VEH report the writer's
// EIP + owning module + static offset.
//
// Rework (sky audit round-5: "can't name the writer"). The first version
// failed four ways at once:
//   (1) installed late — first outdoor scene entry (CClouds__Render_-
//       hygiene) armed it, so every write before CClouds in the frame
//       (the swap is already LIVE at scene entry) escaped entirely;
//   (2) deduped per-EIP — logged each offender once, then silence;
//   (3) cleared DR7.L0 after the FIRST fire and re-armed next outdoor
//       entry — one trap per frame, and the first write was usually a
//       LEGIT swap, so the real writer never trapped at all;
//   (4) access=0xFFFFFFFF records (NumberParameters<2 — the DBG_CONTINUE
//       style path) BYPASSED the address check (hitAddr==0): a foreign
//       single-step was consumed as our hit and disarmed the probe before
//       the real writer fired.
// Now:
//   (1) installed at DLL init (InjectDelayedPatches) and armed from the
//       first device hook in RenderScene_before — before any geometry draw;
//   (2) per-EIP hit COUNTS: first hit logs in full, every 128th repeat
//       logs the running count — repeats are counted, never suppressed;
//   (3) DR0 STAYS armed across fires (x86 data breakpoints are trap-class:
//       the store retires before #DB, so there is no re-executing store to
//       protect) — every writer in the frame is captured and attributed;
//   (4) the access check is WIDENED instead of bypassed: access 0 and
//       0xFFFFFFFF (unknown/no-info) are both attributed, and when the
//       record carries no address DR6 decides — B0 (bit 0) = our DR0
//       fired → attribute; BS (bit 14) = foreign single-step → untouched.
//
// Gate: config->postfxDumpDebug — the existing debug switch that already
// gates the camera-raster dump diagnostics. Release users never install the
// VEH and never touch the DR registers. A hard fire cap bounds the dispatch
// cost; at the cap a per-EIP summary is logged and the probe latches off.
//
// Exception safety: every Scene deref and DR-register access is
// __try/__except wrapped; any failure (OpenThread denied, SetThreadContext
// rejected, DR0 owned by a debugger) latches the probe off silently. A
// missing Scene.camera is NOT a failure — the probe retries later (the
// DLL-init install runs before the game has built its camera). All locals
// are POD — no destructible objects in any __try scope, so C2712/C2713
// cannot fire (same frame-SEH style as InitialiseGame_hook :2067 and the
// cleanup guards at :4097+).
// ============================================================

// DR7 bits for a DR0 dword-write watchpoint, locally enabled:
//   L0        = bit 0        (local enable)
//   RW0  (01) = bits 17:16   (write)
//   LEN0 (10) = bits 19:18   (4 bytes)
#define FBWP_DR7_MASK 0x000C0001u
#define FBWP_DR7_ARM  0x00090001u

// FBWP_FIRED is gone (rework item 3): the watchpoint no longer disarms on
// fire, so there is no transient "fired, waiting to re-arm" state.
enum { FBWP_OFF = 0, FBWP_ARMED = 1, FBWP_UNAVAILABLE = 2 };
static volatile LONG s_fbWpState;         // FBWP_*
static int s_fbWpVehInstalled;            // our VEH is on the chain
static DWORD s_fbWpTotalFires;            // total attributed fires (cap below)
static DWORD s_fbWpWatchAddr;             // field DR0 is currently targeting

// Hard cap on attributed fires: bounds VEH dispatch cost on a persistent
// writer. At the cap a per-EIP summary is logged and the probe latches off.
#define FBWP_FIRE_CAP 16384

// Per-EIP attribution with hit COUNTS (rework item 2): each distinct writer
// EIP keeps its own counter — first hit logs in full, every 128th repeat
// logs the running count (repeats are COUNTED, not suppressed). 16 slots
// name the top offenders; overflow EIPs still count into s_fbWpOverflow.
#define FBWP_MAX_EIPS 16
#define FBWP_LOG_EVERY 128
struct FbWpEip {
	DWORD eip;
	DWORD count;
	DWORD modOff;
	char  modName[48];
};
static FbWpEip s_fbWpEips[FBWP_MAX_EIPS];
static int s_fbWpEipCount;
static DWORD s_fbWpOverflow;              // hits at EIPs beyond the table

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

	// The record normally carries the info ([0]=access type, [1]=address);
	// NumberParameters<2 leaves access=0xFFFFFFFF / address unknown.
	DWORD hitAddr = 0, hitType = 0xFFFFFFFF;
	__try {
		if(ep->ExceptionRecord->NumberParameters >= 2){
			hitType = (DWORD)ep->ExceptionRecord->ExceptionInformation[0];
			hitAddr = (DWORD)ep->ExceptionRecord->ExceptionInformation[1];
		}
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		return EXCEPTION_CONTINUE_SEARCH;
	}

	CONTEXT *ctx = ep->ContextRecord;
	DWORD target = fbWp_TargetAddr();
	if(hitAddr){
		// Record carries the faulting address — strict identity check.
		if(target && hitAddr != target)
			return EXCEPTION_CONTINUE_SEARCH;   // foreign data breakpoint
	}else{
		// No info (access=0xFFFFFFFF, NumberParameters<2 — the DBG_CONTINUE
		// path the old probe let BYPASS the address check and then consume
		// the watchpoint). WIDENED instead of bypassed: DR6 says whether
		// OUR DR0 fired. B0 (bit 0) = our write watchpoint → still
		// attributed even though the access field is 0xFFFFFFFF (DR0 is
		// armed write-only, so a DR0 trap cannot be a read); BS (bit 14) or
		// another slot = plain foreign single-step → pass through untouched
		// so the probe survives to see the real writer.
		DWORD dr6 = 0;
		__try { dr6 = ctx->Dr6; } __except(EXCEPTION_EXECUTE_HANDLER) {
			return EXCEPTION_CONTINUE_SEARCH;
		}
		if(!(dr6 & 0x1u))
			return EXCEPTION_CONTINUE_SEARCH;
	}
	// Consume DR6.B0-B3 (hardware sets them sticky) so a stale bit can
	// never re-attribute a later foreign single-step to this probe.
	__try { ctx->Dr6 &= ~0xFu; } __except(EXCEPTION_EXECUTE_HANDLER) {}

	DWORD eip = ctx->Eip;

	// Module owning the EIP (range check via GetModuleHandleEx, same
	// pattern as crashhandler.cpp selfDir) + static offset for RE lookup.
	char modName[48] = "?";
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

	// Per-EIP attribution with COUNTS (rework item 2): never suppress a
	// repeat outright — first hit logs in full, every 128th logs the
	// running count so a per-frame writer stays visible all session.
	int slot = -1;
	for(int i = 0; i < s_fbWpEipCount; i++)
		if(s_fbWpEips[i].eip == eip){ slot = i; break; }
	if(slot < 0 && s_fbWpEipCount < FBWP_MAX_EIPS){
		slot = s_fbWpEipCount++;
		s_fbWpEips[slot].eip = eip;
		s_fbWpEips[slot].count = 0;
		s_fbWpEips[slot].modOff = modOff;
		strncpy(s_fbWpEips[slot].modName, modName, sizeof(s_fbWpEips[slot].modName) - 1);
		s_fbWpEips[slot].modName[sizeof(s_fbWpEips[slot].modName) - 1] = 0;
	}
	DWORD n = 0;
	if(slot >= 0)
		n = ++s_fbWpEips[slot].count;
	else
		s_fbWpOverflow++;
	if(n == 1 || (n % FBWP_LOG_EVERY) == 0){
		dbglog("[FBWP] WRITE to Scene.camera->frameBuffer addr=%p by EIP=%08X in %s+0x%X (access=%u, hit=%u, raster=%p)",
			(void*)target, eip, modName, modOff, hitType, n,
			Scene.camera ? (void*)Scene.camera->frameBuffer : NULL);
	}

	// Rework item 3: DR0 STAYS ARMED — x86 data breakpoints are trap-class
	// (the store retires before #DB is delivered), so the old "clear L0 so
	// the re-executed store completes" concern does not apply, and only an
	// armed watchpoint can COUNT repeated writes. The hard cap bounds the
	// dispatch cost on a persistent writer; at the cap log the per-EIP
	// summary and latch off (L0 cleared so the world moves on).
	if(++s_fbWpTotalFires >= FBWP_FIRE_CAP){
		for(int i = 0; i < s_fbWpEipCount; i++)
			dbglog("[FBWP] summary: EIP=%08X %s+0x%X hits=%u",
				s_fbWpEips[i].eip, s_fbWpEips[i].modName,
				s_fbWpEips[i].modOff, s_fbWpEips[i].count);
		if(s_fbWpOverflow)
			dbglog("[FBWP] summary: %u hits at EIPs beyond the table", s_fbWpOverflow);
		dbglog("[FBWP] fire cap %u reached — probe latching off", FBWP_FIRE_CAP);
		__try {
			ctx->Dr7 &= ~1u;
		} __except(EXCEPTION_EXECUTE_HANDLER) {}
		s_fbWpState = FBWP_UNAVAILABLE;
	}
	return EXCEPTION_CONTINUE_EXECUTION;
}

// Install VEH (once) and (re-)arm DR0. Called from DLL init
// (InjectDelayedPatches), the first device hook in RenderScene_before (each
// frame — cheap no-op once armed) and the outdoor scene entry
// (CClouds__Render_hygiene). Debug registers are per-thread: the per-frame
// RenderScene_before call is what guarantees the RENDER thread is armed even
// when the DLL-init call ran on another thread or before the camera existed.
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

	DWORD target = fbWp_TargetAddr();
	if(!target){
		// Scene.camera not built yet (the DLL-init install runs before game
		// init) or transiently unreadable — NOT a failure: retry on the next
		// call instead of latching the probe dead before its first frame.
		return;
	}

	if(s_fbWpState == FBWP_ARMED && target == s_fbWpWatchAddr)
		return;   // already armed on this field — per-frame refresh is free

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
		if(ok && ((ctx.Dr7 & FBWP_DR7_ARM) != FBWP_DR7_ARM || ctx.Dr0 != target)){
			// Only claim DR0 — never steal a slot another owner (debugger)
			// holds at a foreign address, and never touch DR1-3. A stale
			// target of OUR OWN (camera object recreated) may be re-targeted.
			if((ctx.Dr7 & 1u) && ctx.Dr0 != target && ctx.Dr0 != s_fbWpWatchAddr)
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
		s_fbWpWatchAddr = target;
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
	// Xbox-clouds lane hunk C: once per frame, stamp the cloud noise
	// parameter block (c49-c52, the reserved SkyNoise slots) and bind the
	// universal tile to sampler s9 for the clouds PS consumed by the
	// SpriteFlushIndexedDraw cloud path below. Guarded on cloudsPS: no
	// consumer shader loaded, no upload. Frame-guarded: this hook wraps BOTH
	// CClouds::Render call sites (0x53DCA2/0x53DFA0) and must upload once.
	// s9 is the project-wide noise reservation (noise.h / pipeBindNoise) —
	// same no-restore convention as buildingPipe's pipeBindNoise(9).
	if(cloudsPS && d3d9device){
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
	// Runs before the cloudRenderFix gate: the corona draw does not depend
	// on that INI flag.
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
	if(dbglog_throttle("sky_ps")){
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
	// flat texel at some camera angles. Captured BEFORE the cloudRenderFix
	// gate so the pass-through path restores it too. L1 value + L3 texture
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

	if(!cloudRenderFix || !CClouds__Render_orig){
		// Nothing forced on this path — no restore to protect, no guard needed.
		// The producer flag still wraps the original: the shared sprite flush
		// must never force a non-additive pair over clouds (which draw
		// natively ONE/ONE). Both producers now force the same ONE/ONE pair;
		// this flag is the receipt label. POD writes, no SEH on this path.
		if(CClouds__Render_orig){
			g_inCloudsRender = true;
			CClouds__Render_orig();
			g_inCloudsRender = false;
			// Stage-0 raster restore for this path too (park-and-set through
			// L1, then the raw L3 texture that was bound when we entered).
			RwRaster *whiteRas = gpWhiteTexture ? gpWhiteTexture->raster : NULL;
			RwRaster *texPark = (savedTexRaster == whiteRas)
				? (RwRaster*)NULL : whiteRas;
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)texPark);
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)savedTexRaster);
			if(d3d9device)
				d3d9device->SetTexture(0, savedDevTex0);
			if(savedTexRaster && savedTexRaster != whiteRas &&
			   dbglog_once("cloud_texraster"))
				dbglog("[CloudShield] restored stage-0 raster=%p dev=%p",
					(void*)savedTexRaster, (void*)savedDevTex0);
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
	RwD3D9SetPixelShader(NULL);
	RwD3D9SetVertexShader(NULL);
	if(d3d9device){
		d3d9device->SetPixelShader(NULL);
		d3d9device->SetVertexShader(NULL);
	}
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
		// Stage-0 colour/alpha ops: CClouds NEVER sets them, so whatever the
		// previous pass left (SELECTARG1/SELECTARG2 from an Im2D pass) would
		// otherwise govern the clouds. Raw device only (no rw twin); the
		// snapshot above already captured both and the restore block below
		// puts them back, so this force is fully covered.
		d3d9device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		d3d9device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
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
	g_inCloudsRender = true;	// producer flag: flush shield must pick ONE/ONE
	__try {
		CClouds__Render_orig();
	} __except(
		(s_cloudFaultCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
	) {
		g_inCloudsRender = false;	// never leave the cloud pair armed
		if(dbglog_throttle("clouds_fault"))
			dbglog("CClouds__Render_hygiene: FAULT code=0x%08X inside CClouds::Render — forcing 3-layer restore",
				s_cloudFaultCode);
	}
	// Cleared unconditionally after the original returns (the except path
	// above clears it too). POD write; no flush can draw between here and
	// the restore block, so the flag never leaks past this hook.
	g_inCloudsRender = false;

	// ---- restore: rw cache first (so later Gets agree), then the raw force
	// closes the gap where the cache already read the saved value ----
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)sFilter);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)sAlphaFn);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)sAlphaRf);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)sFog);
	RwD3D9SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D(sAlphaFn));
	RwD3D9SetRenderState(D3DRS_ALPHAREF, sAlphaRf);
	RwD3D9SetRenderState(D3DRS_FOGENABLE, sFogDev ? TRUE : FALSE);
	// Z-test/Z-func restore — three-layer (rw cache + driver cache + raw
	// device), mirror of depthhook.cpp:470-472. Sanitize the saved values
	// first (same convention as D3D9RenderDual): a failed Get must never
	// push garbage Z state onto the device.
	if(sZEn > D3DZB_USEW) sZEn = TRUE;
	if(sZFunc < D3DCMP_NEVER || sZFunc > D3DCMP_ALWAYS) sZFunc = D3DCMP_LESSEQUAL;
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)(sZTest ? TRUE : FALSE));
	RwD3D9SetRenderState(D3DRS_ZENABLE, sZEn);
	RwD3D9SetRenderState(D3DRS_ZFUNC, sZFunc);
	// Blend/write restore — L1 (rw cache) + L2 (driver cache) + raw L3 for
	// the states CClouds (and the cloud-pair flush shield) overwrite:
	// ZWRITE, ALPHABLENDENABLE (rw twin VERTEXALPHAENABLE), SRCBLEND and
	// DESTBLEND. Left unrestored these were the poison the next reflection/
	// env/mirror sub-pass inherited (ZWRITE off + ONE/ONE additive).
	// Stage-0 COLOROP/ALPHAOP have no rw twin — raw device save/restore only
	// (CClouds sets them for its sprite texture; leaving them breaks the
	// next world draw).
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)sZWriteRw);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)sVtxAlphaRw);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)sSrcRw);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)sDstRw);
	RwD3D9SetRenderState(D3DRS_ZWRITEENABLE, sZWriteDrv ? TRUE : FALSE);
	RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE, sBlendDrv ? TRUE : FALSE);
	RwD3D9SetRenderState(D3DRS_SRCBLEND, sSrcDrv);
	RwD3D9SetRenderState(D3DRS_DESTBLEND, sDstDrv);
	// Shader/stage neutralize (PS=NULL is the blessed neutral for the
	// fixed-function sky draw): a leaked pixel/vertex shader or the s5
	// cluster texture (ForwardPlus) would ride the clouds' raw state into
	// the next draw. RW API + raw device both, so raw-set drift can't hide
	// behind RW's shader cache.
	RwD3D9SetPixelShader(NULL);
	RwD3D9SetVertexShader(NULL);
	if(d3d9device){
		d3d9device->SetPixelShader(NULL);
		d3d9device->SetVertexShader(NULL);
		d3d9device->SetTexture(5, NULL);
	}
	if(d3d9device){
		d3d9device->SetSamplerState(0, D3DSAMP_MINFILTER, sMinF);
		d3d9device->SetSamplerState(0, D3DSAMP_MAGFILTER, sMagF);
		d3d9device->SetSamplerState(0, D3DSAMP_MIPFILTER, sMipF);
		d3d9device->SetRenderState(D3DRS_ALPHAFUNC, pipeAlphaFuncToD3D(sAlphaFn));
		d3d9device->SetRenderState(D3DRS_ALPHAREF, sAlphaRf);
		d3d9device->SetRenderState(D3DRS_FOGENABLE, sFogDev ? TRUE : FALSE);
		d3d9device->SetRenderState(D3DRS_ZENABLE, sZEn);
		d3d9device->SetRenderState(D3DRS_ZFUNC, sZFunc);
		// Blend/write + stage-0 ops — raw L3 half of the restore above.
		d3d9device->SetRenderState(D3DRS_ZWRITEENABLE, sZWriteDev ? TRUE : FALSE);
		d3d9device->SetRenderState(D3DRS_ALPHABLENDENABLE, sBlendDev ? TRUE : FALSE);
		d3d9device->SetRenderState(D3DRS_SRCBLEND, sSrcDev);
		d3d9device->SetRenderState(D3DRS_DESTBLEND, sDstDev);
		d3d9device->SetTextureStageState(0, D3DTSS_COLOROP, sStageColorOp);
		d3d9device->SetTextureStageState(0, D3DTSS_ALPHAOP, sStageAlphaOp);
	}
	// Stage-0 texture raster restore — the missing half of this hygiene block.
	// CClouds binds its cloud/sprite texture and never puts the entry raster
	// back; left desynced, the next texture-less/phased consumer keeps the
	// cloud atlas (peds as a flat atlas texel at certain camera angles).
	// Park-and-set through L1 so a cache-hit cannot no-op, then the raw L3
	// texture captured before the original ran (GetTexture AddRef'd).
	{
		RwRaster *whiteRas = gpWhiteTexture ? gpWhiteTexture->raster : NULL;
		RwRaster *texPark = (savedTexRaster == whiteRas)
			? (RwRaster*)NULL : whiteRas;
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)texPark);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)savedTexRaster);
		if(d3d9device)
			d3d9device->SetTexture(0, savedDevTex0);
		if(savedTexRaster && savedTexRaster != whiteRas &&
		   dbglog_once("cloud_texraster"))
			dbglog("[CloudShield] restored stage-0 raster=%p dev=%p",
				(void*)savedTexRaster, (void*)savedDevTex0);
	}
	if(savedDevTex0){
		savedDevTex0->Release();
		savedDevTex0 = NULL;
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

	// P1 native softbody per-frame integrator - same slot as death
	// ragdoll sim; fast no-op when disabled.
	SoftbodyUpdate();

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
	// A/B exclusivity (softbody.h): when the native softbody backend owns
	// corpses, retire the sftbdyphsx living overlay so both backends never
	// drive the same ped.
	if(!SoftbodyGetEnabled()){
		g_ragdollMan.ProcessAllPeds(CTimer__ms_fTimeStep / 50.0f);
		g_ragdollMan.Update(CTimer__ms_fTimeStep / 50.0f);
	}

	// Blend custom weathers2.dat timecyc into m_CurrentColours.
	// Must run AFTER CTimeCycle::Update() (already done at this point in the frame)
	// but BEFORE any rendering reads the values. This was previously never called,
	// so the alternate timecyc loaded but never applied (garages not black, dir ~0.05).
	Weather_Update();

	// Apply sun config multipliers (corona, core, streaks) to m_CurrentColours.
	// Must run AFTER CTimeCycle::Update() (already done at this point in the frame)
	// but BEFORE any rendering reads the values.
	Weather_ApplySunConfig();

	// Deferred external normal-map plugin attach: polls until the RW engine is
	// up, then attaches once. No-op after success (guarded by
	// externalNmInitialized in texdefs.cpp).
	TexDefsTryAttachExternal();

	RenderScene_before(nil);
	RenderScene();
	RenderScene_after(nil);
}

int (*PipelinePluginAttach)(void);
int
myPluginAttach(void)
{
	// The GAME's own RW pipeline plugin attach (captured by InterceptCall at
	// 0x53D903). It MUST run, and it must not short-circuit ours, so it is
	// called first and its result returned. All SkyGfx plugin slots now live
	// behind the single TexDefsPluginAttach() registry:
	//   atomic  0xEDED     DefinedVertexShader byte  (+ stream read/size)
	//   atomic  0x131      PDS copy-stream rights     (+ stream-rights cb)
	//   texture 0xbadeaffe texdb TexInfo* slot
	//   texture 0x4E4D3137 NmPair slot
	// It is idempotent and always returns 1, so a failed slot can never break
	// the game's chain. Previously EDEDPluginAttach() ran first and then
	// `A && B && C` followed: if A returned 0 every SkyGfx registration was
	// silently skipped.
	int ok = PipelinePluginAttach();
	TexDefsPluginAttach();
	// Peds pipe clump plugin slot 0x50647031 "Pdp1" (design doc §2.2) —
	// idempotent, guarded one-shot; on registration failure the module
	// degrades to the legacy ped path and NEVER breaks the game chain
	// (always-return-1 rule, texdefs.cpp:316 pattern).
	PedsPipe_PluginAttach();
	return ok;
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

	// W7-C: CMirrors composite distortion fix — intercepts the game's
	// RenderMirrorBuffer call site (0x53EABA) in CMain::Render. Self-gated
	// on config->mirrorDistortionFix (default 0), so install is always safe.
	void mirrorPass_init(void);
	mirrorPass_init();
	dbglog("installDeferredHooks: mirrorPass_init done");
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
		TexDefsLoadDatabase();
		dbglog("InitialiseGame_hook: TexDefsLoadDatabase done");
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

// ---- W1 teal-slab fix: the two INLINE Im3D flushes inside
// CWaterLevel::RenderWater (0x6EF650, exe 1.0 US) ----
// RenderWater flushes the TempBuffer in two places OUTSIDE the hooked
// RenderIfDoesntFit path (only 4 call sites of RenderIfDoesntFit exist and
// all are intercepted above; these two draws call RwIm3DTransform/
// RwIm3DRenderIndexedPrimitive/RwIm3DEnd directly):
//   site 1 (call 0x6EF8AC): the OUTSIDE-WORLD ocean sheet — the
//   m_BlocksToBeRenderedOutsideWorld quads pushed by 0x6E6A10/0x6E6870,
//   drawn BEFORE RenderWater binds the waterclear256 raster (0x6EFB78).
//   site 2 (call 0x6F0099): the FINAL remainder of the water.dat quad loop,
//   drawn AFTER the pipe flushes whose restore leaves stage 0 NULL.
// Both draws execute with whatever VS/PS the last object pipe left bound
// (the hook's unpark restores the pre-water stage) and no water texture:
// the world-space ocean verts (+-3000) project through a stale object WVP
// as giant screen-covering slabs shaded by the stale PS constant — the W1
// "flat untextured teal slabs" (2026-10-29 finalgate-0042; the pipe flushes
// themselves were healthy, verts=2044/indices=3066 sane).
// Fix (W1 seam wave, 2026-10-30): route through the water pipe FIRST via
// waterPipe_directFlushRoute() (waterPipe.cpp) — full Water_VS + style PS
// bind (WVP/time, c10 gate, c14/c15 body-foam contract, s0-s3, Z pin,
// classify) + the raw pinned-stage draw, so these flushes get the same
// parallax/caustics/SHORE-FOAM band as the hooked flushes (the band lives
// in the parallax PS; FFP water has none — that was the missing coast foam
// and the hard vertical seam). FFP + waterclear256Raster (0xC228A8) below
// is now only the fail-open fallback when the pipe cannot run. RW's Im3D
// transform (the call immediately before each site) owns decl/FVF/matrices;
// the raster bind is deliberately NOT restored (RW setter keeps cache+device
// in sync; vanilla leaves it bound too — water state block 0x6EFB78 /
// RenderBoatWakes' wake raster 0x6ED9AA re-bind their own).
RwBool (*CWaterLevel__RenderWaterDirectFlush1)(RwPrimitiveType, RwImVertexIndex*, RwInt32);
RwBool (*CWaterLevel__RenderWaterDirectFlush2)(RwPrimitiveType, RwImVertexIndex*, RwInt32);
static RwBool
WaterDirectFlush_Draw(int site, RwPrimitiveType primType, RwImVertexIndex *indices, RwInt32 numIndices)
{
	// ---- W1 diagnostic: water_flatpath (first-hit unconditional + 2s
	// throttle + counter — same starvation-proof pattern as water_badbuf).
	// Logs which site, the flush counts, and the stale device state the
	// draw would have run with (VS/PS handles + stage 0/s3 bindings).
	// The same capture doubles as the park (Get* AddRef — released below).
	static unsigned s_flatCount = 0;
	static unsigned s_siteHit[3] = {0, 0, 0};	// W1 gen-9: per-site running
	static bool s_siteFirst[3] = {true, true, true};	// counter + first-hit (cold-vs-starved proof)
	s_flatCount++;
	unsigned siteHits = 0;
	if(site >= 0 && site < 3){
		s_siteHit[site]++;
		siteHits = s_siteHit[site];
	}
	IDirect3DVertexShader9 *prevVS = NULL;
	IDirect3DPixelShader9 *prevPS = NULL;
	IDirect3DBaseTexture9 *staleS0 = NULL, *staleS3 = NULL;
	if(d3d9device){
		d3d9device->GetVertexShader(&prevVS);
		d3d9device->GetPixelShader(&prevPS);
		d3d9device->GetTexture(0, &staleS0);
		d3d9device->GetTexture(3, &staleS3);
	}
	if(site >= 0 && site < 3 && s_siteFirst[site]){
		s_siteFirst[site] = false;
		dbglog("[water] flatpath first hit: site=%d v=%u i=%u staleVS=%p stalePS=%p s0=%p s3=%p siteHits=%u total=%u",
		       site, (unsigned)TempBufferVerticesStored, (unsigned)TempBufferIndicesStored,
		       (void*)prevVS, (void*)prevPS, (void*)staleS0, (void*)staleS3,
		       siteHits, s_flatCount);
	}
	if(staleS0) staleS0->Release();
	if(staleS3) staleS3->Release();

	// ---- W1 seam fix: pipe route FIRST, FFP only as fail-open ----
	// waterPipe_directFlushRoute() binds Water_VS + the style parallax PS +
	// the full pipe constant set (WVP, time, c14/c15 foam contract, s0-s3)
	// and draws this SAME TempBuffer through the pipe; TRUE = consumed.
	// FALSE = pipe unavailable (disabled/lost/insane counts) — the FFP
	// fallback below keeps the old fail-open path. The route restores the
	// pre-route stage internally, so on TRUE we only release our capture.
	extern bool waterPipe_directFlushRoute(void);	// waterPipe.cpp (function-local extern idiom)
	bool routed = waterPipe_directFlushRoute();
	// ---- unpark (gen-9 FIX 1a): LEAVE FFP on every exit ----
	// The route just restored its pre-route stage = the captured object-pipe
	// shaders; re-publishing them (old behaviour) hands parked shader state
	// to the vanilla FFP draws that follow these sites inside
	// CWaterLevel::RenderWater. NULL VS/PS on BOTH layers (raw device +
	// _rwD3D9Last*Used seeds at 0x8E2448/0x8E244C) instead of prevVS/
	// prevPS; pipe consumers re-bind their shaders at their own callback
	// entries. AddRef/Release pairing unchanged — the captures are still
	// released below/at each exit.
	if(d3d9device){
		d3d9device->SetVertexShader(NULL);
		d3d9device->SetPixelShader(NULL);
	}
	*(void**)0x8E2448 = NULL;	// _rwD3D9LastVertexShaderUsed
	*(void**)0x8E244C = NULL;	// _rwD3D9LastPixelShaderUsed
	if(dbglog_throttle("water_flatpath"))
		dbglog("[water] flatpath site=%d v=%u i=%u staleVS=%p stalePS=%p s0=%p s3=%p (count=%u) -> %s",
		       site, (unsigned)TempBufferVerticesStored, (unsigned)TempBufferIndicesStored,
		       (void*)prevVS, (void*)prevPS, (void*)staleS0, (void*)staleS3,
		       s_flatCount,
		       routed ? "pipe route (Water_VS+parallax PS, foam live)"
			      : "FFP fallback (pipe unavailable)");
	if(routed){
		if(prevVS) prevVS->Release();
		if(prevPS) prevPS->Release();
		// m0336: once-per-tick particle update+render (latched inside; the
		// first flush of the frame wins, so wake-flush calls later in the
		// frame only spawn). No-op when gpuParticlesEnable=0.
		GPUParticles_FrameFlush("water");
		return TRUE;
	}

	// Force FFP: raw NULL + seed the RW value-compare caches so the next RW
	// bind sees a real transition (vanilla fall-through idiom).
	if(d3d9device){
		d3d9device->SetVertexShader(NULL);
		d3d9device->SetPixelShader(NULL);
	}
	*(void**)0x8E2448 = NULL;	// _rwD3D9LastVertexShaderUsed
	*(void**)0x8E244C = NULL;	// _rwD3D9LastPixelShaderUsed

	// Re-bind the vanilla water raster for the draw (see block comment).
	RwRaster *waterRaster = *(RwRaster**)0xC228A8;	// waterclear256Raster
	if(waterRaster)
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)waterRaster);

	// Flood-alpha parity (waterPipe M1 guard): the pipe route fades
	// out-of-poly verts via vertex alpha 13/255, but this FFP fallback ran
	// with VERTEXALPHAENABLE=false so the fade was ignored and the fallback
	// could still paint the over-land slab. Enable vertex-alpha blending for
	// the draw, then restore the captured trio (RW setter keeps cache+device
	// in sync; pre-seeded per POD convention).
	BOOL fvaSaved = FALSE;
	RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)&fvaSaved);
	void *fsrcSaved = (void*)rwBLENDSRCALPHA, *fdstSaved = (void*)rwBLENDINVSRCALPHA;
	RwRenderStateGet(rwRENDERSTATESRCBLEND, &fsrcSaved);
	RwRenderStateGet(rwRENDERSTATEDESTBLEND, &fdstSaved);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

	// Original RwIm3DRenderIndexedPrimitive (0x7EF550) — same direct-call
	// idiom as the RwIm3D wrappers above.
	RwBool res = ((RwBool(__cdecl*)(RwPrimitiveType, RwImVertexIndex*, RwInt32))0x7EF550)(primType, indices, numIndices);

	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)fvaSaved);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, fsrcSaved);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, fdstSaved);

	// ---- unpark (gen-9 FIX 1a): LEAVE FFP on exit ----
	// Do NOT restore prevVS/prevPS: RW fixed-function Im3D/Im2D consumers
	// (shadows, particles, wake/splash paths) do not unbind shaders, so a
	// parked object-pipe VS/PS would process every vanilla FFP draw that
	// follows this site inside CWaterLevel::RenderWater. Leave FFP NULL on
	// BOTH layers (raw device + _rwD3D9Last*Used cache seeds at
	// 0x8E2448/0x8E244C) so the next shader-owning pipeline's bind is a
	// real transition; pipe consumers re-bind their shaders at their own
	// callback entries. RA-style release of the parked handles only.
	if(d3d9device){
		d3d9device->SetVertexShader(NULL);
		d3d9device->SetPixelShader(NULL);
	}
	*(void**)0x8E2448 = NULL;	// _rwD3D9LastVertexShaderUsed
	*(void**)0x8E244C = NULL;	// _rwD3D9LastPixelShaderUsed
	if(prevVS) prevVS->Release();
	if(prevPS) prevPS->Release();
	// m0336: particle update+render fallback for the FFP path (latched —
	// see the routed exit above). No-op when gpuParticlesEnable=0.
	GPUParticles_FrameFlush("water");
	return res;
}

static RwBool __cdecl
WaterDirectFlush1_hook(RwPrimitiveType primType, RwImVertexIndex *indices, RwInt32 numIndices)
{
	// site 1: outside-world ocean sheet (before the water raster bind)
	return WaterDirectFlush_Draw(1, primType, indices, numIndices);
}

static RwBool __cdecl
WaterDirectFlush2_hook(RwPrimitiveType primType, RwImVertexIndex *indices, RwInt32 numIndices)
{
	// site 2: final remainder of the water.dat quad loop (after the pipe
	// flushes cleared stage 0)
	return WaterDirectFlush_Draw(2, primType, indices, numIndices);
}

// ---- W5 grey-smear fix: the two INLINE Im3D flushes in the wake path ----
// RenderWakeSegment (triplet 0x6EA2CE/0x6EA2E8/0x6EA2F0) and
// RenderBoatWakes (0x6EDD62/0x6EDD7C/0x6EDD84) call RwIm3DTransform/
// RwIm3DRenderIndexedPrimitive/RwIm3DEnd directly — outside every hooked
// RenderIfDoesntFit site — so the draws execute with whatever VS/PS the
// last skygfx object pipe left device-bound (vanilla Im3D sets FVF/decl
// through RW but never unbinds shaders). The world-space wake verts
// project through the stale WVP and get shaded by the stale PS constant:
// translucent grey-blue smear quads (W5, seen over pier railing,
// 2026-10-29 finalgate-0042). Same family as the W1 teal-slab defect
// above; byte-verified triplets (E8 rel32 scan, exe 1.0 US), both drawing
// TempBuffer strips (primType 3 = TRISTRIP, verts 0xC4D958 /
// TempBufferVerticesStored 0xC4B950 / indices 0xC4B958 /
// TempBufferIndicesStored 0xC4B954).
// Fix: identical park-and-set idiom as WaterDirectFlush_Draw — force FFP
// (raw NULL VS/PS + RW cache seeds) and re-bind the vanilla wake raster
// (0xC228B8, loaded by RenderBoatWakes at 0x6ED9AA) for the draw. The
// raster bind is deliberately NOT restored (same reasoning as the W1
// water raster: RW setter keeps cache+device in sync, vanilla leaves it
// bound across these draws, next RW consumer re-binds its own).
RwBool (*CWaterLevel__RenderWakeDirectFlush1)(RwPrimitiveType, RwImVertexIndex*, RwInt32);
RwBool (*CWaterLevel__RenderWakeDirectFlush2)(RwPrimitiveType, RwImVertexIndex*, RwInt32);
static RwBool
WakeDirectFlush_Draw(int site, RwPrimitiveType primType, RwImVertexIndex *indices, RwInt32 numIndices)
{
	// ---- W5 diagnostic: wake_flatpath (first-hit unconditional + 2s
	// throttle + counter — same starvation-proof pattern as
	// water_flatpath). Logs which site, the flush counts, and the stale
	// device state the draw would have run with (VS/PS handles + stage 0
	// binding). The same capture doubles as the park (Get* AddRef —
	// released below).
	static unsigned s_wakeFlatCount = 0;
	static unsigned s_wakeSiteHit[3] = {0, 0, 0};	// W5 gen-9: per-site running
	static bool s_wakeSiteFirst[3] = {true, true, true};	// counter + first-hit (cold-vs-starved proof)
	s_wakeFlatCount++;
	unsigned siteHits = 0;
	if(site >= 0 && site < 3){
		s_wakeSiteHit[site]++;
		siteHits = s_wakeSiteHit[site];
	}
	IDirect3DVertexShader9 *prevVS = NULL;
	IDirect3DPixelShader9 *prevPS = NULL;
	IDirect3DBaseTexture9 *staleS0 = NULL;
	if(d3d9device){
		d3d9device->GetVertexShader(&prevVS);
		d3d9device->GetPixelShader(&prevPS);
		d3d9device->GetTexture(0, &staleS0);
	}
	if(site >= 0 && site < 3 && s_wakeSiteFirst[site]){
		s_wakeSiteFirst[site] = false;
		dbglog("[wake] flatpath first hit: site=%d v=%u i=%u staleVS=%p stalePS=%p s0=%p siteHits=%u total=%u",
		       site, (unsigned)TempBufferVerticesStored, (unsigned)TempBufferIndicesStored,
		       (void*)prevVS, (void*)prevPS, (void*)staleS0,
		       siteHits, s_wakeFlatCount);
	}
	if(dbglog_throttle("wake_flatpath"))
		dbglog("[wake] flatpath site=%d v=%u i=%u staleVS=%p stalePS=%p s0=%p (count=%u siteHits=%u) -> FFP+vanilla raster",
		       site, (unsigned)TempBufferVerticesStored, (unsigned)TempBufferIndicesStored,
		       (void*)prevVS, (void*)prevPS, (void*)staleS0,
		       s_wakeFlatCount, siteHits);
	if(staleS0) staleS0->Release();

	// Force FFP: raw NULL + seed the RW value-compare caches so the next
	// RW bind sees a real transition (vanilla fall-through idiom).
	if(d3d9device){
		d3d9device->SetVertexShader(NULL);
		d3d9device->SetPixelShader(NULL);
	}
	*(void**)0x8E2448 = NULL;	// _rwD3D9LastVertexShaderUsed
	*(void**)0x8E244C = NULL;	// _rwD3D9LastPixelShaderUsed

	// Re-bind the vanilla wake raster for the draw (see block comment).
	RwRaster *wakeRaster = *(RwRaster**)0xC228B8;
	if(wakeRaster)
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)wakeRaster);

	// Original RwIm3DRenderIndexedPrimitive (0x7EF550) — same direct-call
	// idiom as the RwIm3D wrappers above.
	RwBool res = ((RwBool(__cdecl*)(RwPrimitiveType, RwImVertexIndex*, RwInt32))0x7EF550)(primType, indices, numIndices);

	// ---- unpark (gen-9 FIX 1a): LEAVE FFP on exit ----
	// Same defect class as WaterDirectFlush_Draw: these sites sit inside
	// vanilla RenderWakeSegment / RenderBoatWakes where everything after
	// the flush is FFP. Do NOT re-publish the captured object-pipe shaders
	// (that parked shader state into every following vanilla FFP draw) —
	// leave FFP NULL on BOTH layers (raw device + _rwD3D9Last*Used cache
	// seeds at 0x8E2448/0x8E244C); pipe consumers re-bind their shaders at
	// their own callback entries. RA-style release of the parked handles
	// only.
	if(d3d9device){
		d3d9device->SetVertexShader(NULL);
		d3d9device->SetPixelShader(NULL);
	}
	*(void**)0x8E2448 = NULL;	// _rwD3D9LastVertexShaderUsed
	*(void**)0x8E244C = NULL;	// _rwD3D9LastPixelShaderUsed
	if(prevVS) prevVS->Release();
	if(prevPS) prevPS->Release();
	// m0336 GPU particle lane: site 1 (RenderWakeSegment) spawns wake foam
	// from the TempBuffer verts; the update+render is latched once per game
	// tick inside (the water-flush calls earlier in the frame usually win
	// the render slot, so spawns show next frame). No-op when
	// gpuParticlesEnable=0 (default) — zero device-state traffic.
	if(config && config->gpuParticlesEnable){
		if(site == 1)
			GPUParticles_WakeSpawnFromTempBuffer(site);
		GPUParticles_FrameFlush("wake");
	}
	return res;
}

static RwBool __cdecl
WakeDirectFlush1_hook(RwPrimitiveType primType, RwImVertexIndex *indices, RwInt32 numIndices)
{
	// site 1: RenderWakeSegment flush (per-segment wake strip)
	return WakeDirectFlush_Draw(1, primType, indices, numIndices);
}

static RwBool __cdecl
WakeDirectFlush2_hook(RwPrimitiveType primType, RwImVertexIndex *indices, RwInt32 numIndices)
{
	// site 2: RenderBoatWakes flush (remainder pass at the end of the
	// wake loop; no call into RenderWakeSegment after it — independent
	// draw, byte-verified)
	return WakeDirectFlush_Draw(2, primType, indices, numIndices);
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
		// Vanilla Im3D is fixed-function: it sets FVF/decl through RW but
		// does NOT unbind VS/PS, so a stale pipe Water_VS/waterPS still
		// device-bound would project the water TempBuffer through the last
		// object's WVP as giant screen-covering slabs (the teal-quad defect,
		// 2026-10-29). Force FFP: raw NULL + seed the RW value-compare
		// caches so the next RW bind sees a real transition.
		if(d3d9device){
			d3d9device->SetVertexShader(NULL);
			d3d9device->SetPixelShader(NULL);
		}
		*(void**)0x8E2448 = NULL;	// _rwD3D9LastVertexShaderUsed
		*(void**)0x8E244C = NULL;	// _rwD3D9LastPixelShaderUsed
		if(dbglog_once("water_vanilla_fallthrough"))
			dbglog("water: vanilla fall-through with pipe shaders neutralised (site=%s)",
			       "gate");
		// Original function renders AND empties the buffer (vanilla behaviour)
		if(CWaterLevel__RenderAndEmptyRenderBuffer)
			CWaterLevel__RenderAndEmptyRenderBuffer();
		return;
	}

	_rwD3D9RenderStateFlushCache();

	// ---- Water raw-draw state park (coronaDrawRawShield discipline) ----
	// The flush below is a raw RwD3D9DrawIndexedPrimitiveUP: RW's value-
	// compare shader/FVF caches (_rwD3D9Last*Used, 0x8E2440-0x8E2450) are
	// only updated by the RW setters, so a raw-only pin leaves them naming
	// the previous pass's shader/decl and the next RW bind no-ops. Capture
	// the pre-water device state here (Get* AddRefs — released at the bottom)
	// and put it back after the draw so the flush cannot leak a NULL/foreign
	// stage into the next consumer.
	IDirect3DVertexShader9 *prevVS = NULL;
	IDirect3DPixelShader9 *prevPS = NULL;
	IDirect3DVertexDeclaration9 *prevDecl = NULL;
	DWORD prevFVF = 0;
	if(d3d9device){
		d3d9device->GetVertexShader(&prevVS);
		d3d9device->GetPixelShader(&prevPS);
		d3d9device->GetVertexDeclaration(&prevDecl);
		d3d9device->GetFVF(&prevFVF);
	}

	// Set water pipe render state (our VS/PS/constants)
	waterPipe_setRenderState();

	if(g_waterParallaxActive){
		// ---- pin the raw draw stage, immediately before the flush ----
		// (a) Force a NULL transition through BOTH layers first. The RW
		// setter is value-compare gated against 0x8E2448/0x8E244C, so a
		// raw-only NULL would leave the cache naming the previous pass's
		// shader. Then raw-bind the water shaders and seed the SAME cache
		// values: the NULL step guarantees a real cache transition, while
		// the water bind keeps Water_VS/Water_Parallax live for the draw
		// (a NULL pair here would silently switch the flush to fixed
		// function and bypass the WVP/constant upload the pipe just did).
		if(d3d9device){
			d3d9device->SetVertexShader(NULL);
			d3d9device->SetPixelShader(NULL);
		}
		*(void**)0x8E2448 = NULL;	// _rwD3D9LastVertexShaderUsed
		*(void**)0x8E244C = NULL;	// _rwD3D9LastPixelShaderUsed
		if(d3d9device){
			d3d9device->SetVertexShader((IDirect3DVertexShader9*)Water_VS);
			d3d9device->SetPixelShader((IDirect3DPixelShader9*)waterPS);
		}
		*(void**)0x8E2448 = Water_VS;	// _rwD3D9LastVertexShaderUsed
		*(void**)0x8E244C = waterPS;	// _rwD3D9LastPixelShaderUsed

		// (b) Pin the Im3D input layout. 0x152 = XYZ|NORMAL|DIFFUSE|TEX1,
		// the real 36-byte RwIm3DVertex layout (0x112 omits D3DFVF_DIFFUSE
		// and would read COLOR0/TEXCOORD0 at the wrong offsets).
		// SetVertexDeclaration(NULL) makes the FVF win over the foreign PBR
		// declaration left by the last atomic.
		if(d3d9device){
			d3d9device->SetVertexDeclaration(NULL);
			d3d9device->SetFVF(0x152);
		}
		*(void**)0x8E2444 = NULL;		// _rwD3D9LastVertexDeclarationUsed
		*(void**)0x8E2440 = (void*)0x152;	// _rwD3D9LastFVFUsed

		// (c) One-shot pre-draw depth-state receipt (plain static one-shot;
		// GetDepthStencilSurface AddRefs the surface — Released here).
		static bool waterStateLogged = false;
		if(!waterStateLogged){
			waterStateLogged = true;
			DWORD zen = 0, zf = 0, zw = 0;
			IDirect3DSurface9 *ds = NULL;
			if(d3d9device){
				d3d9device->GetRenderState(D3DRS_ZENABLE, &zen);
				d3d9device->GetRenderState(D3DRS_ZFUNC, &zf);
				d3d9device->GetRenderState(D3DRS_ZWRITEENABLE, &zw);
				d3d9device->GetDepthStencilSurface(&ds);
			}
			dbglog("[water] water_state pre-draw dev zenable=%d zfunc=%d zwrite=%d ds=%p",
			       (int)zen, (int)zf, (int)zw, (void*)ds);
			if(ds) ds->Release();
		}

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

		// ---- unpark: put the pre-water stage back on BOTH layers ----
		// waterPipe_restoreRenderState() unbinds VS/PS through the RW setter
		// (cache -> NULL); the raw restore below is followed by the matching
		// cache seeds so the next RW value-compare bind sees the same state
		// the device actually holds. Decl and FVF are mutually exclusive:
		// restore whichever mode prevDecl says (forwardplus.cpp pattern).
		if(d3d9device){
			d3d9device->SetVertexShader(prevVS);
			d3d9device->SetPixelShader(prevPS);
		}
		*(void**)0x8E2448 = prevVS;	// _rwD3D9LastVertexShaderUsed
		*(void**)0x8E244C = prevPS;	// _rwD3D9LastPixelShaderUsed
		if(prevDecl){
			if(d3d9device) d3d9device->SetVertexDeclaration(prevDecl);
			*(void**)0x8E2444 = prevDecl;		// _rwD3D9LastVertexDeclarationUsed
			*(void**)0x8E2440 = (void*)0xFFFFFFFF;	// _rwD3D9LastFVFUsed
		}else{
			if(d3d9device){
				d3d9device->SetVertexDeclaration(NULL);
				d3d9device->SetFVF(prevFVF);
			}
			*(void**)0x8E2444 = NULL;		// _rwD3D9LastVertexDeclarationUsed
			*(void**)0x8E2440 = (void*)prevFVF;	// _rwD3D9LastFVFUsed
		}

		TempBufferVerticesStored = 0;
		TempBufferIndicesStored = 0;

		if(prevVS) prevVS->Release();
		if(prevPS) prevPS->Release();
		if(prevDecl) prevDecl->Release();
	}else{
		// Setup failed mid-way (device lost / refraction RT unavailable /
		// buffer sanity) — hand the buffer to the vanilla draw instead of
		// drawing half-configured. Vanilla Im3D is fixed-function: it sets
		// FVF/decl through RW but does NOT unbind VS/PS, so a stale pipe
		// Water_VS/waterPS (or whatever the last pipe left bound) would
		// project the water TempBuffer through the last object's WVP as
		// giant screen-covering slabs (the teal-quad defect, 2026-10-29).
		// Force FFP BEFORE the vanilla call; prevVS/prevPS were captured
		// above (Get* AddRef) and every waterPipe_setRenderState() early-out
		// returns before touching device VS/PS, so they still name the
		// pre-water stage.
		if(d3d9device){
			d3d9device->SetVertexShader(NULL);
			d3d9device->SetPixelShader(NULL);
		}
		*(void**)0x8E2448 = NULL;	// _rwD3D9LastVertexShaderUsed
		*(void**)0x8E244C = NULL;	// _rwD3D9LastPixelShaderUsed
		if(dbglog_once("water_vanilla_fallthrough"))
			dbglog("water: vanilla fall-through with pipe shaders neutralised (site=%s)",
			       "mid_setup");
		if(CWaterLevel__RenderAndEmptyRenderBuffer)
			CWaterLevel__RenderAndEmptyRenderBuffer();
		// Unpark: restore the pre-water stage on BOTH layers before the
		// Releases so the neutralised NULL cannot leak into the next
		// consumer (same structure as the g_waterParallaxActive unpark).
		if(d3d9device){
			d3d9device->SetVertexShader(prevVS);
			d3d9device->SetPixelShader(prevPS);
		}
		*(void**)0x8E2448 = prevVS;	// _rwD3D9LastVertexShaderUsed
		*(void**)0x8E244C = prevPS;	// _rwD3D9LastPixelShaderUsed
		if(prevVS) prevVS->Release();
		if(prevPS) prevPS->Release();
		if(prevDecl) prevDecl->Release();
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
	// BOUNDED (init-heap-corruption audit): the extension rewrite below
	// indexes nLen-4..nLen — a path shorter than 5 chars would write at
	// negative indices. Cannot happen for a real DLL path; guard anyway.
	// Also guard nLen == MAX_PATH-1 (GetModuleFileName truncation): the
	// rewrite puts 'i' at [nLen] (the terminator slot) and restores the NUL
	// at [nLen+1] — with no room for [nLen+1] the buffer would be left
	// UNTERMINATED into FileExists/fopen. Skip the config scan entirely.
	if(nLen < 5 || nLen + 1 >= MAX_PATH)
		return;
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

// ===== Section-aware INI reads (lane sections + top-level fallback) =====
// m0197: config fixes that landed in one lane must not silently act global.
// Flagged keys (docs/plans/2026-10-31-source-inventory.md §4) are re-routed
// through these helpers so they can be scoped per lane section:
//   [Ped] / [Building] / [Vehicle] / [Grass] / [Water] / [PostFX] /
//   [Effects] / [Core]
//
// PRECEDENCE (contract):
//   1. A section value WINS over the top-level value when present.
//   2. A missing/empty section value falls back to [SkyGfx] <key> — the
//      top-level value keeps working exactly as before, so existing INIs
//      (no lane sections at all) see ZERO behavior change.
//   3. Neither present -> the caller's default.
// Note: a present-but-unparseable section value yields the default (same
// as today's behavior for an unparseable top-level value); it does NOT
// fall through to the top-level value.
static std::string
iniGet2(linb::ini &cfg, const char *section, const char *key)
{
	std::string v = cfg.get(section, key, "");
	if(v.empty())
		v = cfg.get("SkyGfx", key, "");
	return v;
}

static int
readint2(linb::ini &cfg, const char *section, const char *key, int def)
{
	return readint(iniGet2(cfg, section, key), def);
}

static float
readfloat2(linb::ini &cfg, const char *section, const char *key, float def)
{
	return readfloat(iniGet2(cfg, section, key), def);
}

// Number of keys re-routed through readint2/readfloat2 below (keep in sync
// with the receipt dbglog at the end of readIniFile).
static const int g_laneSectionReads = 14;

int explicitBuildingPipe_tmp;

// [SkyGfx] qualityPreset - load-time seed (parsed in readIniFile);
// there is no Config field, so capture the parsed value here purely so
// saveConfigTo can round-trip it instead of dropping the key.
static int g_qualityPreset = 3;

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

	// ===== [Modes] active= (M1a persistence) =====
	// Parsed BEFORE the pipe writers below so a pipe-owning mode loaded from the
	// INI already guards buildingPipe/vehiclePipe/colorFilter this pass. Only the
	// ACTIVE config's INI decides the mode; inactive config slots keep the legacy
	// path.
	// Absent/unknown values do nothing (no ApplyMode, no log): a plain skygfx.ini
	// stays bit-identical to the pre-mode build.
	bool iniModeSet = false;
	ModeId iniMode = MODE_NONE;
	if(c == config){
		static StrAssoc modeAliasMap[] = {
			{"PS2",    MODE_PS2},
			{"Xbox",   MODE_XBOX},
			{"PC",     MODE_PC_VINTAGE},
			{"GTAIV",  MODE_GTAIV},
			{"GTAV",   MODE_GTAV},
			{"Modern", MODE_MODERN},
			{"Custom", MODE_CUSTOM},
			{"Mobile", MODE_MOBILE},
			{"None",   MODE_NONE},
			{"",       -1},	// unknown/absent sentinel
		};
		int iniModeRaw = StrAssoc::get(modeAliasMap, cfg.get("Modes", "active", "").c_str());
		if(iniModeRaw >= 0){
			iniMode = (ModeId)iniModeRaw;
			iniModeSet = true;
			s_activeModeIni = iniMode;
			s_activeModeIniSeen = true;
		}
	}
	const bool modePipesOwned = (c == config && modeOwnsPipes());

	// [General] NoiseQuality (global utility noise texture): 0=32, 1=128,
	// 2=512, >=3=1024, default 0. Kept for the menu accessors and the
	// saveConfigTo round-trip; the regen that applied it is gone (SkyNoise).
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

	// Diagnostic verbosity latch (debug_handl.cpp g_diagLevel): 0=full,
	// 1=throttle intervals x5, 2=throttle-gated receipts silenced.
	c->diagLevel = readint(cfg.get("SkyGfx", "diagLevel", ""), 0);
	g_diagLevel = c->diagLevel;

	// Map pipeline to internal building/vehicle pipes (locked presets).
	// M1a / E2: while a pipe-owning mode is active the profile owns the pipes and
	// filter - the pipeline key above is still parsed (and pipelineOverride still
	// pins the mirror value) but must not lock the pipes. The debug-menu paths
	// (ApplyPipelineFromMenu / ApplyPipelineOverrideFromMenu in debugmenu_ui.cpp)
	// are still unguarded - pending M2.
	if(!modePipesOwned){
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
	}

	dbglog("Config: pipeline=%d buildingPipe=%d vehiclePipe=%d colorFilter=%d pipelineOverride=%d colorFilterEnable=%d",
		c->pipeline, c->buildingPipe, c->vehiclePipe, c->colorFilter, c->pipelineOverride, c->colorFilterEnable);

	// ===== Quality Preset (read second, sets feature defaults) =====
	// 0=LOW (PS2 classic), 1=MEDIUM (PC classic), 2=HIGH (Enhanced), 3=ULTRA (Full PBR)
	// qualityPreset parse kept (L12 seed + saveConfigTo round-trip; no Config field).
	g_qualityPreset = readint(cfg.get("SkyGfx", "qualityPreset", ""), 3);  // default to ULTRA

	// ===== Quality Preset (per-tier Config writer DELETED - stages-0-3 L4) =====
	// The switch(preset) block that seeded buildingPipe/vehiclePipe/
	// colorFilter/iv + per-tier feature fields into c-> is gone: L12 gives
	// those fields ModeManager ownership rows in this wave. Seed kept = the
	// parse into g_qualityPreset above (L12 anchor + saveConfigTo round-trip).
	// Feature seeds kept: NONE - every field the block wrote is L12-owned.
	// CLASS 1 FIX still governs the reads below: they use c->field as
	// default, so absent keys preserve the current value.

	// Lane-section reads (m0197): section value wins, [SkyGfx] fallback keeps
	// existing INIs byte-compatible. ps2Modulate seeds ps2ModulateGlobal, which
	// is consumed by BOTH the building and grass lanes via the per-lane keys'
	// defaults below — lane-scoped to [Grass] per source-inventory §4 (grass
	// was the motivating consumer); per-lane keys ps2ModulateBuilding /
	// ps2ModulateGrass still override for the building lane.
	// Cross-slot fix (audit): seed the PARSED slot (c->), not the live active
	// config. ps2ModulateGlobal/dualPassGlobal are per-Config seed fields —
	// the "global" is lane-scoped within one config, not process-global: the
	// only readers are this function's lane-default reads below, saveConfigTo's
	// round-trip (c = config), ApplyPreset (per-slot) and the modeManager /
	// debug-menu member-pointer rows (each applied to one slot). The old
	// config-> write let an INACTIVE slot's parse stomp the ACTIVE slot's seed
	// (corrupting its next saveConfigTo round-trip and menu edit), while the
	// lane defaults below kept reading the just-written value — right numbers,
	// wrong home. Per-slot write + per-slot reads are byte-identical when
	// c == config (single-INI startup, loadConfigFile) and strictly correct
	// for multi-slot readInis/reloadAllInis passes.
	c->ps2ModulateGlobal = readint2(cfg, "Grass", "ps2Modulate", 0);
	c->dualPassGlobal = readint2(cfg, "Core", "dualPass", 0);

	// Explicit buildingPipe/vehiclePipe INI values override pipeline mapping
	static StrAssoc buildingPipeMap[] = {
		{"PS2",    BUILDING_PS2},
		{"Xbox",   BUILDING_XBOX},
		{"GTAIV",  BUILDING_GTAIV},
		{"PBR",    BUILDING_PBR},
		{"",       -1},
	};
	int bpOverride = StrAssoc::get(buildingPipeMap, cfg.get("SkyGfx", "buildingPipe", "").c_str());
	if(!modePipesOwned && bpOverride >= 0)	// M1a: owned key ignored while a mode is active
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
	if(!modePipesOwned && vpOverride >= 0)	// M1a: owned key ignored while a mode is active
		c->vehiclePipe = vpOverride;

	// Pipe-conjunction #4: INI buildingPipe/vehiclePipe overrides diverge
	// from the `pipeline` key's
	// locked combo — re-derive config->pipeline NOW so the PBR colour-filter
	// force below (:pipeline==PIPELINE_PBR -> MODERN) and the ivMode default
	// further down key on the ACTUAL pipes, not the stale INI pipeline.
	s_pipeDeriveTrigger = "readIniFile (INI load / pipeline lock)";
	derivePipelineFromPipes(c);
	s_pipeDeriveTrigger = NULL;

	c->detailMaps = readint(cfg.get("SkyGfx", "detailMaps", ""), c->detailMaps);
	// Tiled-texture noise-detail jitter (formerly the legacy hash lane).
	// Current key: noiseDetail. Legacy keys stay readable so existing INIs
	// keep working; one dbglog notes when a deprecated key was honoured.
	{
		std::string nd = cfg.get("SkyGfx", "noiseDetail", "");
		const char *legacyKey = NULL;
		if(nd.empty()){
			nd = cfg.get("SkyGfx", "stochasticTexturing", "");
			if(!nd.empty()) legacyKey = "stochasticTexturing";
		}
		if(nd.empty()){
			nd = cfg.get("SkyGfx", "stochastic", "");
			if(!nd.empty()) legacyKey = "stochastic";
		}
		if(legacyKey)
			dbglog("skygfx: INI key '%s' is deprecated, use 'noiseDetail'", legacyKey);
		c->noiseDetail = readint(nd, c->noiseDetail);
	}

	c->ps2ModulateBuilding = readint(cfg.get("SkyGfx", "ps2ModulateBuilding", ""), c->ps2ModulateGlobal);
	c->dualPassBuilding = readint2(cfg, "Building", "dualPassBuilding", c->dualPassGlobal);

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

	c->dualPassVehicle = readint2(cfg, "Vehicle", "dualPassVehicle", c->dualPassGlobal);
	c->leedsShininessMult = readfloat(cfg.get("SkyGfx", "leedsShininessMult", ""), 1.0);
	c->neoShininessMult = readfloat(cfg.get("SkyGfx", "neoShininessMult", ""), 1.0);
	c->neoSpecularityMult = readfloat(cfg.get("SkyGfx", "neoSpecularityMult", ""), 1.0);
	c->envShininessMult = readfloat(cfg.get("SkyGfx", "envShininessMult", ""), 1.0);
	c->envSpecularityMult = readfloat(cfg.get("SkyGfx", "envSpecularityMult", ""), 1.0);
	c->envPower = readfloat(cfg.get("SkyGfx", "envPower", ""), c->envPower);
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
	c->vehEnvIntensity = readfloat(cfg.get("SkyGfx", "vehEnvIntensity", ""), 0.4f);
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
	// HIGH-1: default 1.0 is in-range [0.2..4.0]; clamp ceiling raised from
	// 1.0 to 4.0 to match debugmenu_ui.cpp (was a conflicting clamp).
	c->tonemapMaxExposure = readfloat(cfg.get("SkyGfx", "tonemapMaxExposure", ""), 1.0f);
	// User pivot curve (TonemapPass c8) — identity defaults 0/0/0.5
	c->tonemapCurveLows = readfloat(cfg.get("SkyGfx", "tonemapCurveLows", ""), 0.0f);
	c->tonemapCurveHighs = readfloat(cfg.get("SkyGfx", "tonemapCurveHighs", ""), 0.0f);
	c->tonemapCurveMid = readfloat(cfg.get("SkyGfx", "tonemapCurveMid", ""), 0.5f);
	// Exposure ranges Min [-0.5..0.5] / Max [0.2..4.0] + Min<=Max + curve
	// bounds — clamped here + refreshIni() + saveConfigTo() (env-clamp pattern).
	clampTonemapExposureRanges(c);
	c->envMapSize = readint(cfg.get("SkyGfx", "envMapSize", ""), c->envMapSize);
	// INT-overflow guard: the pow2 snap below is signed-overflow UB for a
	// hand-edited huge value (e.g. 2000000000 — i *= 2 wraps INT_MIN).
	// Clamp to the documented raster band first (downstream EnvMapRasterSize
	// caps at 2048² anyway — skygfx.h reflectionMsaaLevel comment).
	if(c->envMapSize < 1) c->envMapSize = 1;
	if(c->envMapSize > 4096) c->envMapSize = 4096;
	int i = 1;
	while(i < c->envMapSize) i *= 2;
	c->envMapSize = i;
	c->envMapFarClipMult = readfloat(cfg.get("SkyGfx", "envMapFarClipMult", ""), c->envMapFarClipMult);
	c->envMapUseLODs = readint(cfg.get("SkyGfx", "envMapUseLODs", ""), 0);
	// W7-C: reflection MSAA (env-map supersampling — EnvMapRasterSize in
	// envmap.cpp scales envMapSize × level, cap 2048²). Snap to {0,2,4,8};
	// anything else = 0 (off / 1×).
	c->reflectionMsaaLevel = readint(cfg.get("SkyGfx", "reflectionMsaaLevel", ""), 0);
	if(c->reflectionMsaaLevel != 2 && c->reflectionMsaaLevel != 4 && c->reflectionMsaaLevel != 8)
		c->reflectionMsaaLevel = 0;
	// W7-C: mirror distortion fix (CMirrors composite projective overdraw —
	// src/render/mirrorPass.cpp). Default OFF: new visual behaviour is opt-in.
	c->mirrorDistortionFix = readint(cfg.get("SkyGfx", "mirrorDistortionFix", ""), 0);

	// Normal mapping
	c->normalMapEnable = readint(cfg.get("SkyGfx", "normalMapEnable", ""), 0);
	c->normalMapIntensity = readfloat(cfg.get("SkyGfx", "normalMapIntensity", ""), 1.0f);
	c->normalMapPlayerOnly = readint(cfg.get("SkyGfx", "normalMapPlayerOnly", ""), 1);

	c->doglare = readint(cfg.get("SkyGfx", "sunGlare", ""), c->doglare);
	if(c->doglare < 0){
		iCanHasSunGlare = false;
		c->doglare = 0;
	}

	c->ps2ModulateGrass = readint(cfg.get("SkyGfx", "ps2ModulateGrass", ""), c->ps2ModulateGlobal);
	c->dualPassGrass = readint(cfg.get("SkyGfx", "dualPassGrass", ""), c->dualPassGlobal);
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
	c->dualPassDefault = readint2(cfg, "Core", "dualPassDefault", c->dualPassGlobal);
	c->dualPassPed = readint2(cfg, "Ped", "dualPassPed", c->dualPassGlobal);
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
	if(!modePipesOwned)	// M1a: the profile owns colorFilter while a mode is active
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
	c->rgb1Mult = readfloat(cfg.get("SkyGfx", "rgb1Mult", ""), c->rgb1Mult);
	c->rgb2Mult = readfloat(cfg.get("SkyGfx", "rgb2Mult", ""), c->rgb2Mult);
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

	c->neoWaterDrops = readint(cfg.get("SkyGfx", "neoWaterDrops", ""), c->neoWaterDrops);
	if(c->neoWaterDrops < 0){
		iCanHasNeoDrops = false;
		c->neoWaterDrops = 0;
	}
	c->neoBloodDrops = readint(cfg.get("SkyGfx", "neoBloodDrops", ""), 0);
	fixPcCarLight = readint(cfg.get("SkyGfx", "fixPcCarLight", ""), 0);
	explicitBuildingPipe_tmp = readint(cfg.get("SkyGfx", "explicitBuildingPipe", ""), -1);
	// tagsBuildingPipe follows the same pipeline as main building pipe
	c->tagsBuildingPipe = c->buildingPipe;

	c->zwriteThreshold = readint2(cfg, "Core", "zwriteThreshold", 128);
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
	c->pipeChainReflEnable = readint(cfg.get("SkyGfx", "pipeChainReflEnable", ""), 0);
	// Menu clamps [0..1]; clamp the INI read to the same band.
	if(c->pipeChainReflEnable < 0) c->pipeChainReflEnable = 0;
	if(c->pipeChainReflEnable > 1) c->pipeChainReflEnable = 1;

	// State-integrity lane (all default 1 = ON, clamped [0..1]):
	//   texAlphaClassify      — texture-alpha classification (tex alpha into
	//                           the PBR alpha channel instead of vertex alpha);
	//   stateBaselineEnable   — frame-boundary world baseline
	//                           (pipeApplyWorldBaseline at the four boundaries);
	//   defaultCbShaderHygiene— unbind stale VS/PS in myDefaultCallback;
	//   spriteStateShield     — per-draw ALPHAFUNC/ALPHAREF/SEPARATEALPHA/
	//                           COLORWRITEENABLE normalisation in
	//                           coronaDrawRawShield (blend/ZWRITE force is
	//                           not gated).
	c->texAlphaClassify = readint2(cfg, "Effects", "texAlphaClassify", 1);
	if(c->texAlphaClassify < 0) c->texAlphaClassify = 0;
	if(c->texAlphaClassify > 1) c->texAlphaClassify = 1;
	c->stateBaselineEnable = readint2(cfg, "Core", "stateBaselineEnable", 1);
	if(c->stateBaselineEnable < 0) c->stateBaselineEnable = 0;
	if(c->stateBaselineEnable > 1) c->stateBaselineEnable = 1;
	c->spriteStateShield = readint(cfg.get("SkyGfx", "spriteStateShield", ""), 1);
	if(c->spriteStateShield < 0) c->spriteStateShield = 0;
	if(c->spriteStateShield > 1) c->spriteStateShield = 1;

	// m0970 bisect lane — render-state systems that were compiled ON with no
	// gate at all. Default 1 = the previous always-on behaviour, so nothing
	// changes until one of these is set to 0. Clamped [0..1] at parse; each
	// also has an ADD_IF_MISSING emit (SKYGFX_EMIT_DEFAULT_KEYS), a
	// saveConfigTo write-back and a settings-menu row.
	c->coronaBlendShield = readint(cfg.get("SkyGfx", "coronaBlendShield", ""), 1);
	if(c->coronaBlendShield < 0) c->coronaBlendShield = 0;
	if(c->coronaBlendShield > 1) c->coronaBlendShield = 1;
	c->spriteFlushShield = readint(cfg.get("SkyGfx", "spriteFlushShield", ""), 1);
	if(c->spriteFlushShield < 0) c->spriteFlushShield = 0;
	if(c->spriteFlushShield > 1) c->spriteFlushShield = 1;
	c->effectsAlphaBlock = readint(cfg.get("SkyGfx", "effectsAlphaBlock", ""), 1);
	if(c->effectsAlphaBlock < 0) c->effectsAlphaBlock = 0;
	if(c->effectsAlphaBlock > 1) c->effectsAlphaBlock = 1;
	c->camRasterRepair = readint(cfg.get("SkyGfx", "camRasterRepair", ""), 1);
	if(c->camRasterRepair < 0) c->camRasterRepair = 0;
	if(c->camRasterRepair > 1) c->camRasterRepair = 1;
	c->moonMaskAlpha = readint(cfg.get("SkyGfx", "moonMaskAlpha", ""), 1);
	if(c->moonMaskAlpha < 0) c->moonMaskAlpha = 0;
	if(c->moonMaskAlpha > 1) c->moonMaskAlpha = 1;
	c->skidmarkAlphaFix = readint(cfg.get("SkyGfx", "skidmarkAlphaFix", ""), 1);
	if(c->skidmarkAlphaFix < 0) c->skidmarkAlphaFix = 0;
	if(c->skidmarkAlphaFix > 1) c->skidmarkAlphaFix = 1;
	c->pedTexResync = readint(cfg.get("SkyGfx", "pedTexResync", ""), 1);
	if(c->pedTexResync < 0) c->pedTexResync = 0;
	if(c->pedTexResync > 1) c->pedTexResync = 1;

	// Shadow render-state contract master gate. Default 0 = vanilla shadow
	// passes; the contract post-dates the look target and wraps an offscreen
	// InvertRaster render in an on-screen alpha contract (see
	// shadowContractRun). Set 1 only when diagnosing shadow state drift.
	c->shadowContractEnable = readint(cfg.get("SkyGfx", "shadowContractEnable", ""), 0);
	if(c->shadowContractEnable < 0) c->shadowContractEnable = 0;
	if(c->shadowContractEnable > 1) c->shadowContractEnable = 1;
	c->defaultCbShaderHygiene = readint2(cfg, "Core", "defaultCbShaderHygiene", 1);
	if(c->defaultCbShaderHygiene < 0) c->defaultCbShaderHygiene = 0;
	if(c->defaultCbShaderHygiene > 1) c->defaultCbShaderHygiene = 1;
	// Ped opaque contract (myDefaultCallback): 1 = enforce blend-off,
	// zwrite/ztest on, alpha test GREATEREQUAL@1 for the skin pipeline.
	c->pedOpaqueContract = readint(cfg.get("SkyGfx", "pedOpaqueContract", ""), 1);
	if(c->pedOpaqueContract < 0) c->pedOpaqueContract = 0;
	if(c->pedOpaqueContract > 1) c->pedOpaqueContract = 1;
	// Ped A/B levers for the view-angle visibility report (myDefaultCallback
	// isPedPipe branch). pedCullMode valid set: -1 preserve the game's raw
	// saved cull, 1 CULLNONE, 2 CULLBACK, 3 CULLFRONT; anything else -> -1.
	c->pedCullMode = readint(cfg.get("SkyGfx", "pedCullMode", ""), -1);
	if(c->pedCullMode < -1 || c->pedCullMode == 0 || c->pedCullMode > 3)
		c->pedCullMode = -1;
	// pedAlphaTestEnable: 1 = canonical GREATEREQUAL@1, 0 = test off.
	c->pedAlphaTestEnable = readint(cfg.get("SkyGfx", "pedAlphaTestEnable", ""), 1);
	if(c->pedAlphaTestEnable < 0) c->pedAlphaTestEnable = 0;
	if(c->pedAlphaTestEnable > 1) c->pedAlphaTestEnable = 1;

	// Absolute ambient floor (pipelinecommon.cpp ClampAmbientToSky tail);
	// 0 disables the lift. Clamped to a sane band at parse.
	// Lane-section note (m0197): ambientFloor feeds the SHARED light upload
	// (all lanes see it), but it was introduced for the ped near-camera black
	// fix — lane-scoped to [Ped] per source-inventory §4; [SkyGfx] fallback
	// preserves existing INIs.
	c->ambientFloor = readfloat2(cfg, "Ped", "ambientFloor", 0.03f);
	if(c->ambientFloor < 0.0f) c->ambientFloor = 0.0f;
	if(c->ambientFloor > 0.2f) c->ambientFloor = 0.2f;

	// Debug toggles — set to 0 to bypass effects for black screen isolation
	// colorFilterEnable also records USER INTENT (latch above): every legit
	// INI path (readIni / reloadAllInis / loadConfigFile) flows through here.
	cfEnableSetUserIntent(c, (RwBool)readint(cfg.get("SkyGfx", "colorFilterEnable", ""), 1));
	// PBR lock, same reason as the colorFilter=Modern force above: the 19:06
	// runtime log showed colorFilterEnable=0, which silently bypasses the
	// whole colour-filter/tonemap chain and confounds every PBR visual test.
	// Force the flag AND record intent=1 so cfEnableHeal() cannot restore a
	// stale 0 from the INI latch. Placed after cfEnableSetUserIntent so the
	// force is not overwritten by the INI value later in this function.
	if(c->pipeline == PIPELINE_PBR){
		static bool s_cfEnablePbrForced = false;
		if(!s_cfEnablePbrForced){
			s_cfEnablePbrForced = true;
			dbglog("[CF] colorFilterEnable forced 1 for PBR (was %d)", (int)c->colorFilterEnable);
		}
		cfEnableSetUserIntent(c, (RwBool)1);
	}
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
	// Lane-section note (m0197): pbrAmbientFloor / ambientMult /
	// pbrIblAmbientWeight feed the shared light upload but were introduced
	// for peds — lane-scoped to [Ped] per source-inventory §4; [SkyGfx]
	// fallback preserves existing INIs.
	c->pbrAmbientFloor = readfloat2(cfg, "Ped", "pbrAmbientFloor", 0.0f);
	// Menu bounds are [0..1]; clamp the INI read to the same band.
	if(c->pbrAmbientFloor < 0.0f) c->pbrAmbientFloor = 0.0f;
	if(c->pbrAmbientFloor > 1.0f) c->pbrAmbientFloor = 1.0f;
	// Ped (skin-pipe) ambient floor — hue-preserving peak-channel floor applied
	// to the pAmbient override in myDefaultCallback (near-camera black peds).
	// Ambient-like keys in this file are floats; the default is 0.20 (peak-
	// channel floor; set 0.0 to disable and restore reference-fork parity).
	// Clamp [0..1] at parse, at refreshIni (via this shared parser) and at save.
	// DEAD-BUT-PARSED (source-inventory D4): zero render consumers after the
	// W3 revert — the debug menu still references config->pedAmbientFloor, so
	// the field and this parse stay; cleanup deferred.
	c->pedAmbientFloor = readfloat(cfg.get("SkyGfx", "pedAmbientFloor", ""), 0.20f);
	if(c->pedAmbientFloor < 0.0f) c->pedAmbientFloor = 0.0f;
	if(c->pedAmbientFloor > 1.0f) c->pedAmbientFloor = 1.0f;
	// Global ambient multiplier (1.0 = vanilla; the old hardcoded 0.85 was
	// a daylight-darkness bug on the ambient-only Xbox building pipe).
	c->ambientMult = readfloat2(cfg, "Ped", "ambientMult", 1.0f);
	if(c->ambientMult < 0.0f) c->ambientMult = 0.0f;
	if(c->ambientMult > 2.0f) c->ambientMult = 2.0f;
	c->pbrIblAmbientWeight = readfloat2(cfg, "Ped", "pbrIblAmbientWeight", 0.5f);
	// [Ped] peds_pipe lane parse (design doc §6): the module reads ONLY its
	// own [Ped] keys into its file-static mirror — lane-scoped by
	// construction, never writes Config fields owned by other lanes. Last
	// parse wins for the active lane (c == config discipline). Placed after
	// the last [Ped] lane read above so a lane's full ped section is
	// visible to the module mirror.
	PedsPipe_ReadConfig(cfg);
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
	c->forwardPlusDebug = readint(cfg.get("SkyGfx", "forwardPlusDebug", ""), 0);
	
	// Sun corona
	c->sunCoronaIntensity = readfloat(cfg.get("SkyGfx", "sunCoronaIntensity", ""), 0.4f);
	c->sunCoreIntensity = readfloat(cfg.get("SkyGfx", "sunCoreIntensity", ""), 0.6f);
	c->sunStreakIntensity = readfloat(cfg.get("SkyGfx", "sunStreakIntensity", ""), 0.5f);
	c->sunStreakSize = readfloat(cfg.get("SkyGfx", "sunStreakSize", ""), 0.6f);

	// Alt timecyc overwrite gate (default 0 = vanilla ambient)
	c->altTimecycEnable = readint(cfg.get("SkyGfx", "altTimecycEnable", ""), 0);

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
	// W7-A smooth shorelines / foam gate + W7-B body-classification gate:
	// both default 1 (current always-on behaviour), 0 = switch that feature off.
	c->waterShoreFoam = readint(cfg.get("SkyGfx", "waterShoreFoam", ""), 1);
	c->waterBodyClassify = readint(cfg.get("SkyGfx", "waterBodyClassify", ""), 1);
	if(c->waterShoreFoam) c->waterShoreFoam = 1;		// 0|1 only (RwBool checkbox)
	if(c->waterBodyClassify) c->waterBodyClassify = 1;
	c->waterShallowR = readfloat(cfg.get("SkyGfx", "waterShallowR", ""), 0.13f);
	c->waterShallowG = readfloat(cfg.get("SkyGfx", "waterShallowG", ""), 0.45f);
	c->waterShallowB = readfloat(cfg.get("SkyGfx", "waterShallowB", ""), 0.55f);
	c->waterDeepR = readfloat(cfg.get("SkyGfx", "waterDeepR", ""), 0.01f);
	c->waterDeepG = readfloat(cfg.get("SkyGfx", "waterDeepG", ""), 0.03f);
	c->waterDeepB = readfloat(cfg.get("SkyGfx", "waterDeepB", ""), 0.08f);
	// REMOVED (W7-C): waterReflectionFarClip — dead key, zero consumers
	// (grep-verified across src/ + shaders/). Stale INI entries are ignored.
	// Water rewrite knobs (Xbox/IV-style water)
	c->waterTileScale = readfloat(cfg.get("SkyGfx", "waterTileScale", ""), 1.0f);
	c->waterShoreFade = readfloat(cfg.get("SkyGfx", "waterShoreFade", ""), 2.0f);
	c->waterTranslucency = readfloat(cfg.get("SkyGfx", "waterTranslucency", ""), 1.0f);
	c->waterReflectionStrength = readfloat(cfg.get("SkyGfx", "waterReflectionStrength", ""), 0.55f);
	c->waterReflectionMix = readfloat(cfg.get("SkyGfx", "waterReflectionMix", ""), 0.7f);
	c->waterUseTimecycle = readint(cfg.get("SkyGfx", "waterUseTimecycle", ""), 1);
	// W8-2 tree lane. treeFx: 0=Off 1=PC 2=PC+ 3=Console (FusionFix TreeFxText).
	// SetProfile applies immediately — the CPU half (ALPHAREF scaling) is live the
	// same frame; the Console lighting half additionally needs the D3 CSO load.
	c->treeFx = readint(cfg.get("SkyGfx", "treeFx", ""), 0);
	if(c->treeFx < 0) c->treeFx = 0;
	if(c->treeFx > 3) c->treeFx = 3;
	if(c == config){		// only the active config drives the live switch (refreshIni() re-applies)
		TreeAlpha_SetProfile(c->treeFx);
	}
	// treeWind: 1 (default in the lane spec) installs our ModifyMatrixForTreeInWind
	// port at InjectDelayedPatches; 0 = leave the stock routine alone — trees
	// still sway (stock), this is NOT a "disable wind" switch. DEFAULT 0 for the
	// current black-screen debugging round: the port is startup-gated, so it can
	// never change rendering until the game restarts with the key set to 1.
	c->treeWind = readint(cfg.get("SkyGfx", "treeWind", ""), 0);
	if(c->treeWind) c->treeWind = 1;
	// W8-3 (staged Diff D): grass instancing master switch, DEFAULT 0 (OFF).
	// The gate is consumed by CGrassInstancer::SetEnabled below; while it is 0
	// every island entry point no-ops and the game renders plants unchanged.
	c->grassInstancing = readint(cfg.get("SkyGfx", "grassInstancing", ""), 0);
	if(c->grassInstancing) c->grassInstancing = 1;
	// m0336 GPU particle lane (particles_gpu.cpp): master switch, DEFAULT 0
	// (stable-config rule: new features ship inert). While 0, the wake
	// spawn/frame-flush entry points no-op before touching any device state
	// and the effectsPC.txd harvest never runs.
	c->gpuParticlesEnable = readint(cfg.get("SkyGfx", "gpuParticlesEnable", ""), 0);
	if(c->gpuParticlesEnable) c->gpuParticlesEnable = 1;
	if(c == config){		// only the active config drives the live switch (refreshIni() re-applies)
		CGrassInstancer::SetEnabled(c->grassInstancing != 0);
	}
	// Pass/frame profiler (src/render/profiler.cpp). Applied here so startup,
	// F11 reload and config switches all reach the live globals; the imgui
	// Profiler tab writes the same globals directly (see profilerRowEdit).
	// refreshIni() re-applies them from the ACTIVE config as well.
	c->profilerEnable = readint(cfg.get("SkyGfx", "profilerEnable", ""), 1);
	c->profilerGpuTimers = readint(cfg.get("SkyGfx", "profilerGpuTimers", ""), 0);
	if(c->profilerEnable) c->profilerEnable = 1;
	if(c->profilerGpuTimers) c->profilerGpuTimers = 1;
	if(c == config){		// only the config the game is running drives the switches
		g_pfEnable = c->profilerEnable;
		g_pfGpuTimers = c->profilerGpuTimers;
	}
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

	// ===== New post-effects (Fx* passes in postfx.cpp) =====
	// Clamps: tonemapOperator [0,5], bloomQuality [0,3], 0-1 knobs bounded,
	// all other new floats floored at 0 (densities/intensities >= 0).
	c->dofEnable = readint(cfg.get("SkyGfx", "dofEnable", ""), 0);
	c->dofFocusDistance = readfloat(cfg.get("SkyGfx", "dofFocusDistance", ""), 20.0f);
	if(c->dofFocusDistance < 0.0f) c->dofFocusDistance = 0.0f;
	c->dofFocusRange = readfloat(cfg.get("SkyGfx", "dofFocusRange", ""), 15.0f);
	if(c->dofFocusRange < 0.0f) c->dofFocusRange = 0.0f;
	c->dofBokehRadius = readfloat(cfg.get("SkyGfx", "dofBokehRadius", ""), 4.0f);
	if(c->dofBokehRadius < 0.0f) c->dofBokehRadius = 0.0f;
	c->dofMaxBlur = readfloat(cfg.get("SkyGfx", "dofMaxBlur", ""), 0.5f);
	if(c->dofMaxBlur < 0.0f) c->dofMaxBlur = 0.0f;
	if(c->dofMaxBlur > 1.0f) c->dofMaxBlur = 1.0f;
	c->tonemapOperator = readint(cfg.get("SkyGfx", "tonemapOperator", ""), 0);
	if(c->tonemapOperator < 0) c->tonemapOperator = 0;
	if(c->tonemapOperator > 5) c->tonemapOperator = 5;
	c->bloomEnable = readint(cfg.get("SkyGfx", "bloomEnable", ""), 0);
	c->bloomQuality = readint(cfg.get("SkyGfx", "bloomQuality", ""), 2);
	if(c->bloomQuality < 0) c->bloomQuality = 0;
	if(c->bloomQuality > 3) c->bloomQuality = 3;
	c->bloomThreshold = readfloat(cfg.get("SkyGfx", "bloomThreshold", ""), 1.0f);
	if(c->bloomThreshold < 0.0f) c->bloomThreshold = 0.0f;
	c->bloomIntensity = readfloat(cfg.get("SkyGfx", "bloomIntensity", ""), 0.8f);
	if(c->bloomIntensity < 0.0f) c->bloomIntensity = 0.0f;
	c->sunShaftsEnable = readint(cfg.get("SkyGfx", "sunShaftsEnable", ""), 0);
	c->sunShaftsDensity = readfloat(cfg.get("SkyGfx", "sunShaftsDensity", ""), 0.84f);
	if(c->sunShaftsDensity < 0.0f) c->sunShaftsDensity = 0.0f;
	if(c->sunShaftsDensity > 1.0f) c->sunShaftsDensity = 1.0f;
	c->sunShaftsDecay = readfloat(cfg.get("SkyGfx", "sunShaftsDecay", ""), 1.0f);
	if(c->sunShaftsDecay < 0.0f) c->sunShaftsDecay = 0.0f;
	if(c->sunShaftsDecay > 1.0f) c->sunShaftsDecay = 1.0f;
	c->fxaaEnable = readint(cfg.get("SkyGfx", "fxaaEnable", ""), 0);
	c->grainIntensity = readfloat(cfg.get("SkyGfx", "grainIntensity", ""), 0.0f);
	if(c->grainIntensity < 0.0f) c->grainIntensity = 0.0f;
	if(c->grainIntensity > 1.0f) c->grainIntensity = 1.0f;
	c->caIntensity = readfloat(cfg.get("SkyGfx", "caIntensity", ""), 0.0f);
	if(c->caIntensity < 0.0f) c->caIntensity = 0.0f;
	if(c->caIntensity > 1.0f) c->caIntensity = 1.0f;
	// vignetteIntensity binds the PRE-EXISTING Config field (skygfx.h Vignette
	// triple) — same key/field postfx.cpp's FxVignette pass reads.
	c->vignetteIntensity = readfloat(cfg.get("SkyGfx", "vignetteIntensity", ""), 0.0f);
	if(c->vignetteIntensity < 0.0f) c->vignetteIntensity = 0.0f;
	if(c->vignetteIntensity > 1.0f) c->vignetteIntensity = 1.0f;
	
	// Debug toggles (already read above, but ensure they use c->field default)


	// ===== ONE default-key emission list for BOTH write paths =====
	// A brand-new INI (every key absent, so ADD_IF_MISSING writes it) and
	// an existing INI (upgrade path) consume this same list below, so a
	// fresh file can never omit a key the upgrade path would add. Defaults
	// are the read defaults; the preset-seeded gates (ssao/smaa/motionBlur/
	// noiseDetail/ivMode) emit the already-parsed c-> value so writing the
	// file cannot clobber the active quality preset.
	#define ADD_IF_MISSING(section, key, default) \
		if(cfg.get(section, key, "").empty()) \
			cfg.set(section, key, default)

	#define SKYGFX_EMIT_DEFAULT_KEYS(EMIT) \
		EMIT("SkyGfx", "pipeline", "PBR"); \
		EMIT("SkyGfx", "qualityPreset", "3"); \
		EMIT("SkyGfx", "ssaoEnable", c->ssaoEnable ? "1" : "0"); \
		EMIT("SkyGfx", "ssaoRadius", "1.0"); \
		EMIT("SkyGfx", "ssaoPower", "2.0"); \
		EMIT("SkyGfx", "ssaoKernelSize", "16"); \
		EMIT("SkyGfx", "ssaoSampleCount", "16"); \
		EMIT("SkyGfx", "smaaEnable", c->smaaEnable ? "1" : "0"); \
		EMIT("SkyGfx", "motionBlurEnable", c->motionBlurEnable ? "1" : "0"); \
		EMIT("SkyGfx", "motionBlurStrength", "0.4"); \
		EMIT("SkyGfx", "sssPostProcessEnable", "1"); \
		EMIT("SkyGfx", "sssPostProcessStrength", "0.2"); \
		EMIT("SkyGfx", "sssEnable", "1"); \
		EMIT("SkyGfx", "sssAmbientBoost", "1"); \
		EMIT("SkyGfx", "skinEnhanceEnable", "1"); \
		EMIT("SkyGfx", "hairEnhanceEnable", "1"); \
		EMIT("SkyGfx", "vegetationEnhanceEnable", "1"); \
		EMIT("SkyGfx", "normalMapEnable", "0"); \
		EMIT("SkyGfx", "ivMode", (c->pipeline == PIPELINE_GTAIV) ? "1" : "0"); \
		EMIT("SkyGfx", "ivDesaturation", "0.15"); \
		EMIT("SkyGfx", "ivGamma", "1.0"); \
		EMIT("SkyGfx", "ivSaturation", "0.3"); \
		EMIT("SkyGfx", "ivCurves", "1.0"); \
		EMIT("SkyGfx", "ivVignetteIntensity", "0.15"); \
		EMIT("SkyGfx", "ivVignetteRadius", "0.70"); \
		EMIT("SkyGfx", "ivVignetteContrast", "1.5"); \
		EMIT("SkyGfx", "ivBloomIntensity", "0.05"); \
		EMIT("SkyGfx", "ivExposure", "2.5"); \
		EMIT("SkyGfx", "ragdollEnable", "1"); \
		EMIT("SkyGfx", "depthHookEnable", "1"); \
		EMIT("SkyGfx", "forwardPlusEnable", "1"); \
		EMIT("SkyGfx", "forwardPlusFallback", "1"); \
		EMIT("SkyGfx", "forwardPlusDebug", "0"); \
		EMIT("SkyGfx", "velocityBufferEnable", "1"); \
		EMIT("SkyGfx", "normalBufferEnable", "1"); \
		EMIT("SkyGfx", "pipeChainEnable", "0"); \
		EMIT("SkyGfx", "pipeChainReflEnable", "0"); \
		EMIT("SkyGfx", "texAlphaClassify", "1"); \
		EMIT("SkyGfx", "stateBaselineEnable", "1"); \
		EMIT("SkyGfx", "spriteStateShield", "1"); \
		EMIT("SkyGfx", "coronaBlendShield", "1"); \
		EMIT("SkyGfx", "spriteFlushShield", "1"); \
		EMIT("SkyGfx", "effectsAlphaBlock", "1"); \
		EMIT("SkyGfx", "camRasterRepair", "1"); \
		EMIT("SkyGfx", "moonMaskAlpha", "1"); \
		EMIT("SkyGfx", "skidmarkAlphaFix", "1"); \
		EMIT("SkyGfx", "pedTexResync", "1"); \
		EMIT("SkyGfx", "shadowContractEnable", "0"); \
		EMIT("SkyGfx", "defaultCbShaderHygiene", "1"); \
		EMIT("SkyGfx", "pedOpaqueContract", "1"); \
		EMIT("SkyGfx", "pedCullMode", "-1"); \
		EMIT("SkyGfx", "pedAlphaTestEnable", "1"); \
		EMIT("SkyGfx", "ambientFloor", "0.03"); \
		EMIT("SkyGfx", "postfxDumpDebug", "0"); \
		EMIT("SkyGfx", "debugOverlay", "0"); \
		EMIT("SkyGfx", "pbrAmbientFloor", "0.0"); \
		EMIT("SkyGfx", "pedAmbientFloor", "0.20"); \
		EMIT("SkyGfx", "ambientMult", "1.0"); \
		EMIT("SkyGfx", "pbrIblAmbientWeight", "0.5"); \
		EMIT("SkyGfx", "tonemapBlackLift", "0.0"); \
		EMIT("SkyGfx", "vehPrelightFallback", "0.15"); \
		EMIT("SkyGfx", "vehEnvFallback", "1"); \
		EMIT("SkyGfx", "vehEnvStrengthMode", "1"); \
		EMIT("SkyGfx", "vehEnvMask", "1"); \
		EMIT("SkyGfx", "vehWetEnv", "1"); \
		EMIT("SkyGfx", "vehEnvGlint", "0.0"); \
		EMIT("SkyGfx", "bldEnvReflect", "1"); \
		EMIT("SkyGfx", "noiseDetail", c->noiseDetail ? "1" : "0"); \
		EMIT("SkyGfx", "bldSkyReflect", "0.15"); \
		EMIT("SkyGfx", "waterParallaxEnable", "1"); \
		EMIT("SkyGfx", "waterParallaxScale", "1.0"); \
		EMIT("SkyGfx", "waterNormalStrength", "1.0"); \
		EMIT("SkyGfx", "waterFresnelPower", "5.0"); \
		EMIT("SkyGfx", "waterSpecularPower", "512.0"); \
		EMIT("SkyGfx", "waterSpecularIntensity", "1.0"); \
		EMIT("SkyGfx", "waterUnderwaterFog", "1.0"); \
		EMIT("SkyGfx", "waterFoamThreshold", "0.6"); \
		EMIT("SkyGfx", "waterFoamSoftness", "0.2"); \
		EMIT("SkyGfx", "waterShallowR", "0.13"); \
		EMIT("SkyGfx", "waterShallowG", "0.45"); \
		EMIT("SkyGfx", "waterShallowB", "0.55"); \
		EMIT("SkyGfx", "waterDeepR", "0.01"); \
		EMIT("SkyGfx", "waterDeepG", "0.03"); \
		EMIT("SkyGfx", "waterDeepB", "0.08"); \
		EMIT("SkyGfx", "mirrorDistortionFix", "0"); \
		EMIT("SkyGfx", "reflectionMsaaLevel", "0"); \
		EMIT("SkyGfx", "waterTileScale", "1.0"); \
		EMIT("SkyGfx", "waterShoreFade", "2.0"); \
		EMIT("SkyGfx", "waterTranslucency", "1.0"); \
		EMIT("SkyGfx", "waterReflectionStrength", "0.55"); \
		EMIT("SkyGfx", "waterReflectionMix", "0.7"); \
		EMIT("SkyGfx", "waterUseTimecycle", "1"); \
		EMIT("SkyGfx", "waterStyle", "Xbox"); \
		EMIT("SkyGfx", "waterQuality", "3"); \
		EMIT("SkyGfx", "waterShoreFoam", "1"); \
		EMIT("SkyGfx", "waterBodyClassify", "1"); \
		EMIT("SkyGfx", "waterFresnelSplit", "1"); \
		EMIT("SkyGfx", "cloudRenderFix", "1"); \
		EMIT("SkyGfx", "dofEnable", "0"); \
		EMIT("SkyGfx", "dofFocusDistance", "20.0"); \
		EMIT("SkyGfx", "dofFocusRange", "15.0"); \
		EMIT("SkyGfx", "dofBokehRadius", "4.0"); \
		EMIT("SkyGfx", "dofMaxBlur", "0.5"); \
		EMIT("SkyGfx", "tonemapOperator", "0"); \
		EMIT("SkyGfx", "bloomEnable", "0"); \
		EMIT("SkyGfx", "bloomQuality", "2"); \
		EMIT("SkyGfx", "bloomThreshold", "1.0"); \
		EMIT("SkyGfx", "bloomIntensity", "0.8"); \
		EMIT("SkyGfx", "sunShaftsEnable", "0"); \
		EMIT("SkyGfx", "sunShaftsDensity", "0.84"); \
		EMIT("SkyGfx", "sunShaftsDecay", "1.0"); \
		EMIT("SkyGfx", "fxaaEnable", "0"); \
		EMIT("SkyGfx", "grainIntensity", "0.0"); \
		EMIT("SkyGfx", "caIntensity", "0.0"); \
		EMIT("SkyGfx", "vignetteIntensity", "0.0"); \
		EMIT("SkyGfx", "treeFx", "0"); \
		EMIT("SkyGfx", "treeWind", "0"); \
		EMIT("SkyGfx", "grassInstancing", "0"); \
		EMIT("SkyGfx", "gpuParticlesEnable", "0"); \
		EMIT("SkyGfx", "profilerEnable", "1"); \
		EMIT("SkyGfx", "profilerGpuTimers", "0"); \
		EMIT("General", "NoiseQuality", "0");
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
		cfg.set("SkyGfx", "; noiseDetail", "1");
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
		cfg.set("SkyGfx", "ssaoRadius", "1.0");
		cfg.set("SkyGfx", "ssaoPower", "2.0");
		cfg.set("SkyGfx", "ssaoKernelSize", "16");
		cfg.set("SkyGfx", "ssaoSampleCount", "16");
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
		// ===== W7-C: mirror distortion fix + reflection MSAA =====
		// (waterReflectionFarClip removed here — dead key, zero consumers)
		cfg.set("SkyGfx", "mirrorDistortionFix", "0");
		cfg.set("SkyGfx", "reflectionMsaaLevel", "0");
		// Water rewrite knobs (Xbox/IV-style water)
		cfg.set("SkyGfx", "waterTileScale", "1.0");
		cfg.set("SkyGfx", "waterShoreFade", "2.0");
		cfg.set("SkyGfx", "waterTranslucency", "1.0");
		cfg.set("SkyGfx", "waterReflectionStrength", "0.55");
		cfg.set("SkyGfx", "waterReflectionMix", "0.7");
		cfg.set("SkyGfx", "waterUseTimecycle", "1");
		cfg.set("SkyGfx", "waterShoreFoam", "1");
		cfg.set("SkyGfx", "waterBodyClassify", "1");
		cfg.set("SkyGfx", "; waterStyle: Xbox (default), IV, GTAIV, V, GTAV", "");
		cfg.set("SkyGfx", "waterStyle", "Xbox");
		cfg.set("SkyGfx", "; waterQuality: 0=Low, 1=Med, 2=High, 3=Ultra", "");
		cfg.set("SkyGfx", "waterQuality", "3");
		cfg.set("SkyGfx", "; waterFresnelSplit: 1=fresnel split (default), 0=legacy composite", "");
		cfg.set("SkyGfx", "waterFresnelSplit", "1");
		cfg.set("SkyGfx", "; cloudRenderFix: 1=cloud render-state hygiene (default), 0=vanilla inherited states", "");
		cfg.set("SkyGfx", "cloudRenderFix", "1");

		// ===== New post-effects (DOF / Bloom / Tonemap LUT / Sun shafts / FXAA / Grain / CA / Vignette) =====
		cfg.set("SkyGfx", "; ===== New post-effects (all off by default - opt in) =====", "");
		cfg.set("SkyGfx", "dofEnable", "0");
		cfg.set("SkyGfx", "dofFocusDistance", "20.0");
		cfg.set("SkyGfx", "dofFocusRange", "15.0");
		cfg.set("SkyGfx", "dofBokehRadius", "4.0");
		cfg.set("SkyGfx", "dofMaxBlur", "0.5");
		cfg.set("SkyGfx", "; tonemapOperator: 0=filmic, 1=ACES 1.3, 2=ACES 2.0, 3=AgX, 4=Frostbite, 5=GT7", "");
		cfg.set("SkyGfx", "tonemapOperator", "0");
		cfg.set("SkyGfx", "bloomEnable", "0");
		cfg.set("SkyGfx", "; bloomQuality: 0-3 = 2..6 mips", "");
		cfg.set("SkyGfx", "bloomQuality", "2");
		cfg.set("SkyGfx", "bloomThreshold", "1.0");
		cfg.set("SkyGfx", "bloomIntensity", "0.8");
		cfg.set("SkyGfx", "sunShaftsEnable", "0");
		cfg.set("SkyGfx", "sunShaftsDensity", "0.84");
		cfg.set("SkyGfx", "sunShaftsDecay", "1.0");
		cfg.set("SkyGfx", "fxaaEnable", "0");
		cfg.set("SkyGfx", "grainIntensity", "0.0");
		cfg.set("SkyGfx", "caIntensity", "0.0");
		cfg.set("SkyGfx", "vignetteIntensity", "0.0");

		// ===== Utility Noise Texture (global) =====
		cfg.set("General", "NoiseQuality", "0");	// 0=32, 1=128, 2=512, >=3=1024

		// ===== Shared default-key list (same list as the upgrade path) =====
		// Emitted here too so a brand-new INI can never omit a key the
		// ADD_IF_MISSING branch would add (already-written keys are kept).
		SKYGFX_EMIT_DEFAULT_KEYS(ADD_IF_MISSING);
	} else {
		// ===== Existing INI - only add missing keys (preserve user settings) =====
		dbglog("skygfx: INI exists, adding any missing keys");
		
		// ===== Shared default-key list (same emission as the fresh INI) =====
		SKYGFX_EMIT_DEFAULT_KEYS(ADD_IF_MISSING);
	}

	#undef SKYGFX_EMIT_DEFAULT_KEYS
	#undef ADD_IF_MISSING

	// ===== [Modes] active= - apply ONCE, at the very END =====
	// Every top-level key above has been parsed, so profile-owned fields now win
	// over the INI (doc section 5.3 precedence). Same-id applies are skipped:
	// ApplyMode(current) is the toggle-OFF path, and an F11 reload must keep the
	// mode latched. TODO(M1b-overrides): sparse [Mode.<alias>] overrides are out
	// of scope for this lane (main.cpp readIniFile/saveConfigTo only).
	if(iniModeSet && ModeManager_GetMode() != iniMode)
		ApplyMode(iniMode);

	// Post-INI base re-snapshot: on a reload the raw INI values just parsed become
	// the new per-slot base (no-op when ApplyMode already captured it this pass).
	if(c == config)
		ModeManager_RefreshBase();

	// One-shot receipt (m0197): how many keys are section-aware reads
	// (readint2/readfloat2 with lane section + [SkyGfx] fallback).
	dbglog("[Config] lane sections active: %d section-override reads installed",
	       g_laneSectionReads);

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
	// BOUNDED (init-heap-corruption audit): both branches below index
	// nLen-4..nLen — a path shorter than 5 chars would write at negative
	// indices. Cannot happen for a real DLL path; guard anyway (asipath is
	// already set above, so the game-dir prefix stays valid). Also guard
	// nLen == MAX_PATH-1 (GetModuleFileName truncation): the n>0 branch
	// puts 'i' at [nLen] (the terminator slot) and restores the NUL at
	// [nLen+1] — with no room for [nLen+1] the buffer would be left
	// UNTERMINATED into readIniFile/fopen. Skip the load entirely.
	if(nLen < 5 || nLen + 1 >= MAX_PATH)
		return;
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

	// [Modes] active= - persisted only once an INI or a menu apply named a mode
	// (a fresh INI must not gain the section). TODO(M1b-overrides): the sparse
	// [Mode.<alias>] diff is out of scope for this lane.
	if(s_activeModeIniSeen)
		cfg.set("Modes", "active", modeIniAlias(s_activeModeIni));

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
	cfg.set("SkyGfx", "noiseDetail", std::to_string(c->noiseDetail));
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
	// W7-C
	cfg.set("SkyGfx", "reflectionMsaaLevel", std::to_string(c->reflectionMsaaLevel));
	cfg.set("SkyGfx", "mirrorDistortionFix", std::to_string(c->mirrorDistortionFix));

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
	cfg.set("SkyGfx", "altTimecycEnable", std::to_string(c->altTimecycEnable));

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
	cfg.set("SkyGfx", "ssaoKernelSize", std::to_string(c->ssaoKernelSize));
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

	// New post-effects (Fx* passes in postfx.cpp)
	cfg.set("SkyGfx", "dofEnable", std::to_string(c->dofEnable));
	cfg.set("SkyGfx", "dofFocusDistance", std::to_string(c->dofFocusDistance));
	cfg.set("SkyGfx", "dofFocusRange", std::to_string(c->dofFocusRange));
	cfg.set("SkyGfx", "dofBokehRadius", std::to_string(c->dofBokehRadius));
	cfg.set("SkyGfx", "dofMaxBlur", std::to_string(c->dofMaxBlur));
	cfg.set("SkyGfx", "tonemapOperator", std::to_string(c->tonemapOperator));
	cfg.set("SkyGfx", "bloomEnable", std::to_string(c->bloomEnable));
	cfg.set("SkyGfx", "bloomQuality", std::to_string(c->bloomQuality));
	cfg.set("SkyGfx", "bloomThreshold", std::to_string(c->bloomThreshold));
	cfg.set("SkyGfx", "bloomIntensity", std::to_string(c->bloomIntensity));
	cfg.set("SkyGfx", "sunShaftsEnable", std::to_string(c->sunShaftsEnable));
	cfg.set("SkyGfx", "sunShaftsDensity", std::to_string(c->sunShaftsDensity));
	cfg.set("SkyGfx", "sunShaftsDecay", std::to_string(c->sunShaftsDecay));
	cfg.set("SkyGfx", "fxaaEnable", std::to_string(c->fxaaEnable));
	cfg.set("SkyGfx", "grainIntensity", std::to_string(c->grainIntensity));
	cfg.set("SkyGfx", "caIntensity", std::to_string(c->caIntensity));
	cfg.set("SkyGfx", "vignetteIntensity", std::to_string(c->vignetteIntensity));

	// Velocity buffer
	cfg.set("SkyGfx", "velocityBufferEnable", std::to_string(c->velocityBufferEnable));

	// Depth Hook
	cfg.set("SkyGfx", "depthHookEnable", std::to_string(c->depthHookEnable));

	// Forward+ tiled lighting
	cfg.set("SkyGfx", "forwardPlusEnable", std::to_string(c->forwardPlusEnable));
	cfg.set("SkyGfx", "forwardPlusDebug", std::to_string(c->forwardPlusDebug));

	// Normal buffer
	cfg.set("SkyGfx", "normalBufferEnable", std::to_string(c->normalBufferEnable));
	cfg.set("SkyGfx", "normalBufferOffset", std::to_string(c->normalBufferOffset));
	cfg.set("SkyGfx", "normalBufferScale", std::to_string(c->normalBufferScale));

	// Pipe chain
	cfg.set("SkyGfx", "pipeChainEnable", std::to_string(c->pipeChainEnable));
	cfg.set("SkyGfx", "pipeChainIntensity", std::to_string(c->pipeChainIntensity));
	cfg.set("SkyGfx", "pipeChainReflEnable", std::to_string(c->pipeChainReflEnable));
	cfg.set("SkyGfx", "texAlphaClassify", std::to_string(c->texAlphaClassify));
	cfg.set("SkyGfx", "stateBaselineEnable", std::to_string(c->stateBaselineEnable));
	cfg.set("SkyGfx", "spriteStateShield", std::to_string(c->spriteStateShield));
	cfg.set("SkyGfx", "coronaBlendShield", std::to_string(c->coronaBlendShield));
	cfg.set("SkyGfx", "spriteFlushShield", std::to_string(c->spriteFlushShield));
	cfg.set("SkyGfx", "effectsAlphaBlock", std::to_string(c->effectsAlphaBlock));
	cfg.set("SkyGfx", "camRasterRepair", std::to_string(c->camRasterRepair));
	cfg.set("SkyGfx", "moonMaskAlpha", std::to_string(c->moonMaskAlpha));
	cfg.set("SkyGfx", "skidmarkAlphaFix", std::to_string(c->skidmarkAlphaFix));
	cfg.set("SkyGfx", "pedTexResync", std::to_string(c->pedTexResync));
	cfg.set("SkyGfx", "shadowContractEnable", std::to_string(c->shadowContractEnable));
	cfg.set("SkyGfx", "defaultCbShaderHygiene", std::to_string(c->defaultCbShaderHygiene));
	cfg.set("SkyGfx", "pedOpaqueContract", std::to_string(c->pedOpaqueContract));
	cfg.set("SkyGfx", "pedCullMode", std::to_string(c->pedCullMode));
	cfg.set("SkyGfx", "pedAlphaTestEnable", std::to_string(c->pedAlphaTestEnable));
	cfg.set("SkyGfx", "ambientFloor", std::to_string(c->ambientFloor));

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
	// REMOVED (W7-C): waterReflectionFarClip save — dead key, zero consumers.
	// Water rewrite knobs (Xbox/IV-style water)
	cfg.set("SkyGfx", "waterTileScale", std::to_string(c->waterTileScale));
	cfg.set("SkyGfx", "waterShoreFade", std::to_string(c->waterShoreFade));
	cfg.set("SkyGfx", "waterTranslucency", std::to_string(c->waterTranslucency));
	cfg.set("SkyGfx", "waterReflectionStrength", std::to_string(c->waterReflectionStrength));
	cfg.set("SkyGfx", "waterReflectionMix", std::to_string(c->waterReflectionMix));
	cfg.set("SkyGfx", "waterUseTimecycle", std::to_string(c->waterUseTimecycle));
	// W7-A / W7-B water gates — round-trip the debug-menu rows.
	cfg.set("SkyGfx", "waterShoreFoam", std::to_string(c->waterShoreFoam));
	cfg.set("SkyGfx", "waterBodyClassify", std::to_string(c->waterBodyClassify));
	// W8-2 tree lane / W8-3 plant island / profiler switches — saved so the
	// debug-menu rows (Vegetation + Profiler tabs) round-trip across sessions.
	cfg.set("SkyGfx", "treeFx", std::to_string(c->treeFx));
	cfg.set("SkyGfx", "treeWind", std::to_string(c->treeWind));
	cfg.set("SkyGfx", "grassInstancing", std::to_string(c->grassInstancing));
	// m0336 GPU particle lane — round-trips the INI gate.
	cfg.set("SkyGfx", "gpuParticlesEnable", std::to_string(c->gpuParticlesEnable));
	cfg.set("SkyGfx", "profilerEnable", std::to_string(c->profilerEnable));
	cfg.set("SkyGfx", "profilerGpuTimers", std::to_string(c->profilerGpuTimers));

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
	cfg.set("SkyGfx", "pedAmbientFloor", std::to_string(c->pedAmbientFloor));
	cfg.set("SkyGfx", "ambientMult", std::to_string(c->ambientMult));
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

	// Round-trip additions #2: keys readIniFile parses that saveConfigTo
	// previously never wrote back (menu edits were reverted on next load).
	// Formats mirror each parse site: waterStyle uses the string enum
	// ("Xbox"/"IV"/"V"), waterFresnelSplit is float-formatted (readint's
	// std::stoi still reads the "1" prefix), hotkeys keep readhex's "0x%X"
	// spelling, and the startup switches are the same non-Config globals
	// readIniFile assigns.
	static const char* waterStyleNames[] = {"Xbox", "IV", "V"};
	if(c->waterStyle >= 0 && c->waterStyle < 3)
		cfg.set("SkyGfx", "waterStyle", waterStyleNames[c->waterStyle]);
	cfg.set("SkyGfx", "waterQuality", std::to_string(c->waterQuality));
	cfg.set("SkyGfx", "waterFresnelSplit", std::to_string((float)g_waterFresnelSplit));
	cfg.set("SkyGfx", "cloudRenderFix", std::to_string(cloudRenderFix ? 1 : 0));
	char keyBuf[16];
	snprintf(keyBuf, sizeof(keyBuf), "0x%X", c->keys[0]);
	cfg.set("SkyGfx", "keySwitch", keyBuf);
	snprintf(keyBuf, sizeof(keyBuf), "0x%X", c->keys[1]);
	cfg.set("SkyGfx", "keyReload", keyBuf);
	cfg.set("SkyGfx", "qualityPreset", std::to_string(g_qualityPreset));
	cfg.set("SkyGfx", "disableClouds", std::to_string(disableClouds ? 1 : 0));
	cfg.set("SkyGfx", "disableGamma", std::to_string(disableGamma ? 1 : 0));
	cfg.set("SkyGfx", "explicitBuildingPipe", std::to_string(explicitBuildingPipe_tmp));
	cfg.set("SkyGfx", "fixPcCarLight", std::to_string(fixPcCarLight ? 1 : 0));
	cfg.set("SkyGfx", "fixShadows", std::to_string(fixShadows));
	cfg.set("SkyGfx", "forceWindShader", std::to_string(forceWindShader ? 1 : 0));
	cfg.set("SkyGfx", "privateHooks", std::to_string(privateHooks ? 1 : 0));
	cfg.set("SkyGfx", "transparentLockon", std::to_string(transparentLockon));

	// [General] NoiseQuality — TU-static g_noiseQuality (menu accessors
	// menuGet/SetNoiseQuality); parsed at readIniFile but never saved before.
	cfg.set("General", "NoiseQuality", std::to_string(g_noiseQuality));

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
	refreshIni();	// mirror readInis()/reloadAllInis(): push values into postfx state
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

// ============================================================
// HOOKS — inline, matching backup_original DllMain pattern
// ============================================================

// Debug-menu accessors (debugmenu_ui.cpp): [General] NoiseQuality lives in
// the TU-static g_noiseQuality above, so the in-game menu reaches it through
// these instead of a cross-TU extern. The old regen path is gone (SkyNoise
// owns the tile; see the note in refreshIni).
int  menuGetNoiseQuality(void) { return g_noiseQuality; }
void menuSetNoiseQuality(int q) { g_noiseQuality = q; }
extern "C" bool RpNormMapPluginAttach(void);

// Profiler frame boundary. The 0x53EBE9 hook replaces CPostEffects::
// DrawFinalEffects wholesale, so the thunk below keeps the call byte-identical
// and only ADDS the profiler's end-of-frame snap. One call site, one snap per
// frame — PfFrame() measures the gap between two consecutive DrawFinalEffects,
// which is exactly one rendered frame.
// Debug correlation overlay (src/core/debugmenu_ui.cpp): top-left frame-id
// readout, drawn screen-final so it is never colour-filtered/tonemapped.
extern void DebugOverlayDraw(IDirect3DDevice9 *device);

static void __fastcall
DrawFinalEffects_pf(void)
{
	CPostEffects::DrawFinalEffects();
	PfFrame();
	// Frame END: expire any deferred corona/cone window and leave the
	// canonical world baseline behind for the next frame's first consumer.
	CoronaWindow_Close();
	if(!config || config->stateBaselineEnable){
		pipeApplyWorldBaseline("EffectsExit");
		static bool s_probeBaselineFinal = false;
		if(!s_probeBaselineFinal){
			s_probeBaselineFinal = true;
			dbglog("[probe] baseline effects_exit_final called");
		}
	}
	// FIX 3: frame-completion sample (this thunk IS the DrawFinalEffects hook).
	// 1-in-30 frame gate via the SAME shared s_rsmonFrameDiv latch the
	// scene_start site uses, so scene_start/frame_end always sample the same
	// frames and their pairs stay comparable.
	vs_probe("frame_end", "vs_probe_frame");
	if(RsMon_FrameEndDue())
		RSMon_Sample("frame_end", -1);
	RSMon_Report();
	// Debug correlation overlay: last draw of the frame (after the full
	// PostFX chain) so it is never colour-filtered / tonemapped / blurred.
	DebugOverlayDraw(d3d9device);
}

// ModeManager listener (M1a / E6): ApplyMode() has just rebuilt `config` and
// run resetValues(). Push the remaining renderer-visible mirrors: the menu
// path's renderer push plus tagsBuildingPipe (there is no
// tagsBuildingPipeExplicit field yet, so the mirror is unconditional and
// idempotent). One allocation-free log line per apply.
static void
ModeListener_OnApply(ModeId oldMode, ModeId newMode)
{
	(void)oldMode;
	if(!config)
		return;
	resetValues();
	config->tagsBuildingPipe = config->buildingPipe;
	s_activeModeIni = newMode;
	s_activeModeIniSeen = true;
	dbglog("[Mode] listener %s pipes=%d/%d/%d cf=%d",
		modeIniAlias(newMode), config->pipeline, config->buildingPipe,
		config->vehiclePipe, config->colorFilter);
}

int
InjectDelayedPatches()
{
	dbglog("InjectDelayedPatches entered");
	RSMon_Init();

	// Mode manager (M1a): initialise + subscribe BEFORE the INI read below.
	// readIniFile() applies [Modes] active= at its end; that apply must see a
	// live manager (pre-mode base snapshot) and this listener (tags mirror).
	dbglog("  INIT: Mode manager");
	ModeManager_Init();
	ModeManager_RegisterListener(ModeListener_OnApply);

	findInis();
	dbglog("  numConfigs=%d", numConfigs);
	if(numConfigs == 0)
		readIni(0);
	else
		readIni(1);
	dbglog("  ini loaded");

	// [Modes] receipt - one-shot per process, after the INI (and any startup
	// ApplyMode) has run.
	if(dbglog_once("mode_active")){
		if(s_activeModeIni != MODE_NONE)
			dbglog("[Mode] active=%s (from INI)", modeIniAlias(s_activeModeIni));
		else
			dbglog("[Mode] active=none");
	}

	// One-shot init evidence: the ambient values the mod will actually run
	// with (make INI load state visible without a debug-menu visit).
	// InjectDelayedPatches runs exactly once, from DllMain.
	if(config){
		dbglog("[INIT] ambientMult=%.4f (SkyGfx.ambientMult as loaded, clamped to [0..2])",
			config->ambientMult);
		// fix-9 night-ceiling rework — the blend-to-vanilla clamp constants
		// (mirror ClampAmbientToSky in pipelinecommon.cpp, which has no
		// init-time log point of its own):
		//   k >= 0.85    -> untouched (vanilla-equivalent, zero crush)
		//   skyL >= 0.45 -> bright-sky day release, never touched
		//   nightWeight  = saturate((0.85 - k) / 0.85)
		//   final        = lerp(ambient, ambient*k, nightWeight)
		dbglog("[INIT] ambientClamp: kThresh=0.85 skyRelease=0.45 blend=lerp(amb,amb*k,saturate((0.85-k)/0.85)) (fix-9 night ceiling, ClampAmbientToSky)");
	}

	// ---- BUILD FINGERPRINT (one-shot) ----
	// A test run used an accidental mid-wave build, which made every visual
	// result ambiguous. Identify the exact module ONCE per process: path,
	// byte size and on-disk mtime, plus the compiler's __DATE__/__TIME__.
	// Plain dbglog (never throttled) so the line cannot be starved away.
	{
		static bool s_buildFpLogged = false;
		if(!s_buildFpLogged){
			s_buildFpLogged = true;
			char fpPath[MAX_PATH] = {};
			DWORD fpLen = GetModuleFileNameA(dllModule, fpPath, MAX_PATH);
			WIN32_FILE_ATTRIBUTE_DATA fad = {};
			BOOL fpOk = fpLen ? GetFileAttributesExA(fpPath, GetFileExInfoStandard, &fad) : FALSE;
			unsigned long long fpSize = 0;
			SYSTEMTIME fpTime = {};
			bool fpTimeOk = false;
			if(fpOk){
				ULARGE_INTEGER sz = {};
				sz.LowPart = fad.nFileSizeLow;
				sz.HighPart = fad.nFileSizeHigh;
				fpSize = sz.QuadPart;
				FILETIME localFt = {};
				if(FileTimeToLocalFileTime(&fad.ftLastWriteTime, &localFt) &&
				   FileTimeToSystemTime(&localFt, &fpTime))
					fpTimeOk = true;
			}
			dbglog("[BUILD] asipath=%s size=%llu mtime=%04d-%02d-%02d %02d:%02d:%02d built=%s %s",
				fpPath[0] ? fpPath : "?", fpSize,
				fpTimeOk ? (int)fpTime.wYear : 0,   fpTimeOk ? (int)fpTime.wMonth : 0,
				fpTimeOk ? (int)fpTime.wDay : 0,    fpTimeOk ? (int)fpTime.wHour : 0,
				fpTimeOk ? (int)fpTime.wMinute : 0, fpTimeOk ? (int)fpTime.wSecond : 0,
				__DATE__, __TIME__);
		}
	}

	// W8-3 (staged Diff D) plant-island master switch + W8-2 (D4b) tree profile
	// are applied inside readIniFile; re-assert them here so a startup that
	// skipped the refreshIni path (InjectDelayedPatches reads the INI itself)
	// still reaches the gates before the first frame.
	CGrassInstancer::SetEnabled(config->grassInstancing != 0);
	TreeAlpha_SetProfile(config->treeFx);

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

	dbglog("  HOOK: TexDefsExternalInit()");
	TexDefsExternalInit();

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

	// P1 native softbody module - unconditional call; self-reads
	// [SkyGfx] softbodyNative (default 0 = fast no-op).
	SoftbodyInit();

	// Peds pipe skin sub-module (design doc §1.3/§7 Phase 2 wiring): one-shot
	// stub init from the same lifecycle slot as SoftbodyInit — loads nothing
	// in Phase 1 (no shaders/rasters/device objects); the SkinSSS.cso load
	// lands here in a later phase, device-reset-safe via PedsPipe_OnDeviceReset.
	Skin_Init();

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
	// 0x53D81C call CSky::Render (0x714650) — sky-dome stage, verified but
	// deliberately NOT intercepted. Disassembly (1.0 US) shows 0x714650 sets
	// its OWN state before any rasterisation: TEXRASTER=NULL / ZTEST=off /
	// ZWRITE=off / VERTEXALPHA(blend)=off / SRCALPHA/INVSRCALPHA / FOG=off /
	// CULLBACK for the opaque dome, then ZTEST=on / VERTEXALPHA=on /
	// SRCALPHA/INVSRCALPHA + its own raster for the textured draw, and
	// restores ZWRITE/ZTEST/blend/TEXRASTER on exit. There is no inherited
	// additive cloud signature to repair, and forcing ONE/ONE (the cloud
	// pair) would corrupt both the blend-off dome and the SRCALPHA/
	// INVSRCALPHA textured draw. One-shot site receipt only.
	{
		BYTE opSky = *(BYTE*)0x53D81C;
		DWORD tSky = 0;
		if(opSky == 0xE8) tSky = *(DWORD*)(0x53D81D) + 0x53D81C + 5;
		dbglog("[skydome_install] 0x53D81C op=%02X orig=%08X%s "
		       "(expect op=E8 orig=714650; NO hook installed — callee "
		       "self-sets ztest/zwrite/blend/raster, additive cloud force "
		       "not applicable)",
			(unsigned)opSky, (unsigned)tSky,
			(opSky == 0xE8 && tSky == 0x714650) ? "OK" : "MISMATCH");
	}
	if(disableGamma) InjectHook(0x74721C, 0x7472F3, PATCH_JUMP);
	if(iCanHasNeoDrops) hookWaterDrops();
	// W2 splash-foam blowout fix — water-splash FX systems: per-draw dst
	// blend ONE->INVSRCALPHA + underwater draw gate (see neopipe.cpp).
	// Independent of the neoWaterDrops lever on purpose: the blowout
	// happens with the screen-drop feature off too.
	hookSplashFoamFix();
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
		// 0x748AA8 NOP removed: SilentPatch installs its 5-byte jump there.
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

	// W1 teal-slab fix: the two INLINE Im3D flushes inside RenderWater
	// (outside-world ocean sheet 0x6EF8AC + final quad-loop remainder
	// 0x6F0099) bypass the RenderIfDoesntFit hook and drew with stale pipe
	// VS/PS + no water texture. Force FFP + vanilla raster per draw.
	dbglog("  HOOK: WaterLevel ::RenderWater inline flushes (2 sites)");
	InterceptCall(&CWaterLevel__RenderWaterDirectFlush1, WaterDirectFlush1_hook, 0x6EF8AC);
	InterceptCall(&CWaterLevel__RenderWaterDirectFlush2, WaterDirectFlush2_hook, 0x6F0099);

	// W5 grey-smear fix: the two INLINE Im3D flushes in the wake path
	// (RenderWakeSegment 0x6EA2E8 + RenderBoatWakes 0x6EDD7C) drew with
	// stale pipe VS/PS + no wake texture. Force FFP + vanilla raster per
	// draw (same idiom as the W1 water flushes above).
	dbglog("  HOOK: WaterLevel ::RenderWakeSegment/::RenderBoatWakes inline flushes (2 sites)");
	InterceptCall(&CWaterLevel__RenderWakeDirectFlush1, WakeDirectFlush1_hook, 0x6EA2E8);
	InterceptCall(&CWaterLevel__RenderWakeDirectFlush2, WakeDirectFlush2_hook, 0x6EDD7C);

	// W8-2: CEntity::ModifyMatrixForTreeInWind replacement (treeWind.cpp).
	// Stock has exactly one direct caller (0x536145: `mov ecx, esi; call
	// 0x534E90`), thiscall => __fastcall body needs no trampoline. Startup-gated
	// by treeWind (default 0 this round); treeWind=1 keeps the stock routine.
	if(config && config->treeWind){
		dbglog("  HOOK: TreeWind ::ModifyMatrixForTreeInWind (0x534E90)");
		InjectHook(0x534E90, TreeWind_ModifyMatrixForTreeInWind, PATCH_JUMP);
	}

	// FBWP probe: DLL-init install point (sky audit rework item 1) — VEH on
	// the chain before any game frame runs. Scene.camera does not exist yet,
	// so the first arm happens (and retries) at RenderScene_before's first
	// device hook. No-op unless postfxDumpDebug=1 (readIni ran above).
	if(config && config->postfxDumpDebug)
		fbWp_ArmOnce();

	dbglog("=== InjectDelayedPatches complete ===");
	return FALSE;
}

// ============================================================
// DIAGNOSTICS — inline from diagnostics.cpp
// ============================================================

// ---- PATCH_JUMP byte verification -------------------------------
// A blind 5-byte PATCH_JUMP assumes the stock prologue. If a foreign mod
// (SilentPatch etc.) already rewrote the site, installing anyway jumps
// through garbage. Verify first; on mismatch skip the hook and keep the
// vanilla path. One log line per site (mismatch or install receipt).
static bool
verifyBytes(void *addr, const unsigned char *expect, int n, const char *tag)
{
	bool ok = (addr != NULL) && (expect != NULL) && (n > 0) &&
	          memcmp(addr, expect, (size_t)n) == 0;
	if(dbglog_once(tag)){
		char receipt[128];
		snprintf(receipt, sizeof(receipt), "[patch_verify] %s %s", tag,
			ok ? "OK (hook installed)" : "MISMATCH -> skipped");
		dbglog("%s", receipt);
	}
	return ok;
}

// Normal map plugin hook — captures normal map textures on vehicle atomics
extern "C" {
	extern bool RpNormMapAtomicIsInitialized(const RpAtomic *atomic);
	extern RpMaterial* RpNormMapMaterialSetNormMapTexture(RpMaterial* material, RwTexture* normalmap);
	extern RwTexture* RpNormMapMaterialGetNormMapTexture(const RpMaterial* material);
	}

	// NOTE: 0x5DA610 (CustomPipeAtomicSetup) is hooked by
	// TexDefsTryAttachExternal() via InjectHook when the RW rwnormal plugin is
	// active. No passthrough needed here — the RW SDK plugin handles normal map
	// pipeline setup natively.

BOOL WINAPI
DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
	if(reason == DLL_PROCESS_ATTACH){
		dllModule = hInst;

		// Construct log path from DLL path (matching backup_original pattern)
		char logPath[MAX_PATH];
		GetModuleFileNameA(dllModule, logPath, MAX_PATH);
		char *p = strrchr(logPath, '.');
		// BOUNDED (init-heap-corruption audit): the old `strcpy(p, "_dbg.log")`
		// wrote 9 bytes at the last '.' with no room check — on a path that
		// fills MAX_PATH (GetModuleFileNameA truncates at MAX_PATH-1 chars)
		// with the '.' near the end this ran past the stack buffer. Only
		// replace in place when the extension position leaves room for
		// "_dbg.log" + NUL; otherwise fall back to the bounded append.
		if(p && (p - logPath) + sizeof("_dbg.log") <= sizeof(logPath))
			strcpy(p, "_dbg.log");
		else
			strncat(logPath, "_dbg.log", sizeof(logPath) - strlen(logPath) - 1);

		diag_init(logPath);
		diag_installVEH();
		// diag_startWatchdog() removed — heartbeat() is never called, causes false freeze dialog
		dbglog("VEH handler + watchdog installed");
		dbglog("skygfx build corona-flush-shield (CoronaBlend entry hook + flush-window close at SpriteFlushIndexedDraw_hook + corona_install verification)");

		dbglog("=== skygfx loading ===");
		dbglog("dllModule=%p, logPath=%s", dllModule, diag_getLogPath());
		dbglog("DynBaseAddr=%p", GetModuleHandle(nullptr));

		DWORD v1 = *(DWORD*)DynBaseAddress(0x82457C);
		DWORD v2 = *(DWORD*)DynBaseAddress(0x8245BC);
		dbglog("ver check: 0x82457C=%08X, 0x8245BC=%08X (expect 0x94BF)", v1, v2);

		// Truthful receipt (audit): the old condition logged OK whenever v1
		// matched — including the v2-mismatch and both-mismatch cases. OK now
		// means BOTH probes match the expected 0x94BF; any other combination
		// warns. Diagnostic-only: this check never gated anything downstream,
		// and "continuing anyway" semantics are preserved.
		if(v1 == 0x94BF && v2 == 0x94BF){
			dbglog("version check OK");
		}else{
			dbglog("version check WARNING - mismatch (v1=0x%08X v2=0x%08X, expect 0x94BF), continuing anyway", v1, v2);
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
		static const unsigned char s_exp_756DFE[5] = { 0x3B, 0xC7, 0x74, 0x15, 0xA1 };
		static const unsigned char s_exp_7FAD30[8] = { 0x8B, 0x54, 0x24, 0x08, 0x8B, 0x44, 0x24, 0x04 };
		static const unsigned char s_exp_7FAD90[8] = { 0x8B, 0x44, 0x24, 0x04, 0x8B, 0x54, 0x24, 0x08 };
		static const unsigned char s_exp_7618B0[7] = { 0x8B, 0x44, 0x24, 0x04, 0x83, 0xEC, 0x10 };
		static const unsigned char s_exp_5DADB7[7] = { 0xD9, 0x46, 0x48, 0x8B, 0x4C, 0x24, 0x54 };
		if(verifyBytes((void*)0x756DFE, s_exp_756DFE, 5, "0x756DFE"))
			InjectHook(0x756DFE, rxD3D9DefaultRenderCallback_Hook, PATCH_JUMP);
		if(verifyBytes((void*)0x7FAD30, s_exp_7FAD30, 8, "0x7FAD30"))
			InjectHook(0x7FAD30, FrustumTestSphere_Guard, PATCH_JUMP);
		if(verifyBytes((void*)0x7FAD90, s_exp_7FAD90, 8, "0x7FAD90"))
			InjectHook(0x7FAD90, FrustumTestBox_Guard,     PATCH_JUMP);
		if(verifyBytes((void*)0x7618B0, s_exp_7618B0, 7, "0x7618B0"))
			InjectHook(0x7618B0, PSSetCameraConstantD_Guard, PATCH_JUMP);
		if(verifyBytes((void*)0x5DADB7, s_exp_5DADB7, 7, "0x5DADB7"))
			InjectHook(0x5DADB7, fixSeed, PATCH_JUMP);

		// NOTE: RpNormMapPluginAttach is called by TexDefsTryAttachExternal()
		// during game init. Do NOT call it here — it would succeed, then the
		// deferred attach would fail, leaving gHasExternalNormalMapPlugin=false
		// and the DK22Pac hooks never installed.

		// 0x5DA610 is hooked by TexDefsTryAttachExternal() when rwnormal plugin is active
		static const unsigned char s_exp_5DAE61[8] = { 0x0F, 0xB6, 0x46, 0x44, 0x89, 0x44, 0x24, 0x28 };
		if(verifyBytes((void*)0x5DAE61, s_exp_5DAE61, 8, "0x5DAE61"))
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
				// site; the 0x70A960 function-entry hook covers every caller.
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
		// 0x6FAEC0  CCoronas::Render ENTRY — corona blend shield + probe
		//           ([CoronaShield] / [CoronaBlend] tags). H3: the old
		//           0x53E18E call-site intercept never logged; hook the
		//           entry point so it fires regardless of caller
		//           (cutscene + gameplay). Entry: forces vanilla additive
		//           blend through all 3 layers when the device sits on the
		//           broken normal-alpha signature (blend on + dst=
		//           INVSRCALPHA, or blend off). After: read-only probe.
		{
			const DWORD coronaEntry = 0x6FAEC0;
			const DWORD coronaStolen = 20;	// 6+5+3+3+3 prologue bytes
			// Pre-install verification (tag corona_entry_install): the RWX
			// trampoline copies these 20 prologue bytes VERBATIM and jumps
			// back to entry+20 (0x6FAED4), so a drifted exe would execute
			// foreign bytes from the copy and/or land the jump-back
			// mid-instruction. Bytes below are read from gta_sa.exe 1.0 US:
			//   81 EC B8 00 00 00   sub  esp, 0xB8
			//   A1 3C 70 C1 00      mov  eax, ds:[0xC1703C]
			//   8B 48 60            mov  ecx, [eax+0x60]
			//   8B 41 0C            mov  eax, [ecx+0x0C]
			//   8B 49 10            mov  ecx, [ecx+0x10]
			// MISMATCH => SKIP the entry hook only. The trampoline pointer is
			// consumed solely by CCoronas__Render_blendprobe_hook (NULL-
			// guarded) and the coronaZtest code patch is a separate site, so
			// the mod still runs — no other subsystem depends on this hook.
			static const BYTE s_coronaEntryExpect[20] = {
				0x81, 0xEC, 0xB8, 0x00, 0x00, 0x00,
				0xA1, 0x3C, 0x70, 0xC1, 0x00,
				0x8B, 0x48, 0x60,
				0x8B, 0x41, 0x0C,
				0x8B, 0x49, 0x10
			};
			BYTE s_coronaEntryActual[20];
			memcpy(s_coronaEntryActual, (const void*)coronaEntry,
				sizeof(s_coronaEntryActual));
			const bool s_coronaEntryOk = (memcmp(s_coronaEntryActual,
				s_coronaEntryExpect, sizeof(s_coronaEntryActual)) == 0);
			char s_coronaEntryHex[sizeof(s_coronaEntryActual) * 3 + 1];
			s_coronaEntryHex[sizeof(s_coronaEntryHex) - 1] = 0;
			for(size_t i = 0; i < sizeof(s_coronaEntryActual); i++)
				snprintf(s_coronaEntryHex + i * 3, 4, "%02X ",
					(unsigned)s_coronaEntryActual[i]);
			dbglog("[corona_entry_install] 0x%08X bytes=%s %s "
			       "(expect 81 EC B8 00 00 00 A1 3C 70 C1 00 8B 48 60 "
			       "8B 41 0C 8B 49 10)",
				(unsigned)coronaEntry, s_coronaEntryHex,
				s_coronaEntryOk ? "OK" : "MISMATCH");
			if(!s_coronaEntryOk){
				dbglog("[corona_entry_install] prologue verify FAILED — "
				       "CCoronas::Render ENTRY hook SKIPPED, vanilla entry "
				       "kept (coronaZtest code patch and all other "
				       "subsystems unaffected)");
			}else{
				BYTE *coronaTramp = (BYTE*)VirtualAlloc(NULL, 32,
					MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
				if(coronaTramp){
					memcpy(coronaTramp, (void*)coronaEntry, coronaStolen);
					coronaTramp[coronaStolen] = 0xE9;	// jmp entry+stolen
					*(DWORD*)(coronaTramp + coronaStolen + 1) =
						(coronaEntry + coronaStolen) -
						(DWORD)(coronaTramp + coronaStolen + 5);
					CCoronas__Render_trampoline = (void (*)(void))coronaTramp;
					InjectHook(coronaEntry, CCoronas__Render_blendprobe_hook, PATCH_JUMP);
					dbglog("[INIT] CoronaBlend probe: entry 0x6FAEC0 hooked, tramp=%p",
						(void*)coronaTramp);
				}else
					dbglog("[INIT] CoronaBlend probe: VirtualAlloc failed — probe NOT installed");
			}
		}
		// Corona draw-time raw shield: wrap the buffered-sprite flush's two
		// innermost draw entries (0x70CF20 -> 0x734EA0 call sites 0x70CF62 /
		// 0x70CF9B — see SpriteFlushIndexedDraw_hook). These are the draws
		// that rasterise the corona quad batch AND the CPointLights glow
		// batch; since 2026-10-24 the additive shield is forced at the
		// flush boundary (force=true, non-empty batch only, corona/cloud
		// producers only) so the early CPointLights call at 0x53DE6B —
		// which runs outside the CCoronas window — is covered.
		// One-shot install verification (tag corona_install): InterceptCall
		// (MemoryMgr.h) is SILENT on a bad site — InjectHook PATCH_NOTHING
		// just rewrites the rel32 of whatever instruction sits at the
		// address. Both sites must be E8 with extracted target 0x734EA0.
		{
			BYTE op62 = *(BYTE*)0x70CF62, op9B = *(BYTE*)0x70CF9B;
			DWORD t62 = 0, t9B = 0;
			if(op62 == 0xE8) t62 = *(DWORD*)(0x70CF63) + 0x70CF62 + 5;
			if(op9B == 0xE8) t9B = *(DWORD*)(0x70CF9C) + 0x70CF9B + 5;
			bool ok62 = (op62 == 0xE8 && t62 == 0x734EA0);
			bool ok9B = (op9B == 0xE8 && t9B == 0x734EA0);
			dbglog("[corona_install] flush draw sites: 0x70CF62 op=%02X orig=%08X%s | "
			       "0x70CF9B op=%02X orig=%08X%s (expect op=E8 orig=734EA0; "
			       "MISMATCH = flush hook never fires, corona quads unshielded)",
				(unsigned)op62, (unsigned)t62,
				ok62 ? "OK" : "MISMATCH",
				(unsigned)op9B, (unsigned)t9B,
				ok9B ? "OK" : "MISMATCH");
			// GATE the installs on the verification: InterceptCall is SILENT on
			// a bad site and InjectHook PATCH_NOTHING would rewrite the rel32
			// of whatever instruction sits there — on a non-1.0 exe that
			// corrupts unrelated code. On MISMATCH skip BOTH installs; the
			// vanilla flush path remains (coronas may stay unshielded, but
			// nothing gets corrupted).
			if(ok62 && ok9B){
				InterceptCall(&SpriteFlushIndexedDraw_orig, SpriteFlushIndexedDraw_hook, 0x70CF62);
				InterceptCall(&SpriteFlushIndexedDraw_orig, SpriteFlushIndexedDraw_hook, 0x70CF9B);
			}else
				dbglog("[corona_install] site verify FAILED — both flush intercepts SKIPPED, vanilla flush path kept");
		}
		// Wave-6b installs: the TWO unshielded inline TempBuffer flush sites
		// (see PostFxFlush_701FE9_hook / CloudRegionFlush_7273D0_hook above).
		// Test-7 (2026-11-01 18:49): statesync cf_enter blend L2=1 L3=0 heals
		// recur every ~1s (persistent creator), sprite_flush gating
		// shielded=44 skipped=0 (0x70CF20 shield works) — yet black quads
		// persist: these two sites rasterise TempBuffer batches OUTSIDE the
		// 0x70CF20 flush hook. Same byte-verification discipline as
		// corona_install: each site must be E8 with the expected extracted
		// target; on MISMATCH skip that install, the vanilla path remains
		// (site may stay unshielded, but nothing gets corrupted).
		{
			// (a) 0x701FE9 — postfx TempBuffer writer flush (func 0x701780,
			//     called from CPostEffects::Render 0x7046E0 at 0x705116).
			//     Expect E8 B2 2E 03 00 -> 0x734EA0. 5-arg cdecl, same
			//     signature as the shared flush (SpriteFlushIndexedDraw_t).
			BYTE opPF = *(BYTE*)0x701FE9;
			DWORD tPF = 0;
			if(opPF == 0xE8) tPF = *(DWORD*)(0x701FEA) + 0x701FE9 + 5;
			bool okPF = (opPF == 0xE8 && tPF == 0x734EA0);
			dbglog("[postfx_flush_install] 0x701FE9 op=%02X orig=%08X%s "
			       "(expect op=E8 orig=734EA0; MISMATCH = intercept "
			       "SKIPPED, vanilla path kept, postfx flush unshielded)",
			       (unsigned)opPF, (unsigned)tPF,
			       okPF ? "OK" : "MISMATCH");
			if(okPF)
				InterceptCall(&PostFxFlush_701FE9_orig,
					PostFxFlush_701FE9_hook, 0x701FE9);
			// (b) 0x719B2B — call site of the cloud-region companion
			//     flush 0x7273D0, right after the cloud-region 0x70CF20
			//     flush (0x719B26). Expect E8 A0 D8 00 00 -> 0x7273D0.
			//     void(void) hook — the count gate at hook entry mirrors
			//     the function's own prologue early-out.
			BYTE opCR = *(BYTE*)0x719B2B;
			DWORD tCR = 0;
			if(opCR == 0xE8) tCR = *(DWORD*)(0x719B2C) + 0x719B2B + 5;
			bool okCR = (opCR == 0xE8 && tCR == 0x7273D0);
			dbglog("[cloud_flush_install] 0x719B2B op=%02X orig=%08X%s "
			       "(expect op=E8 orig=7273D0; MISMATCH = intercept "
			       "SKIPPED, vanilla path kept, cloud-region flush unshielded)",
			       (unsigned)opCR, (unsigned)tCR,
			       okCR ? "OK" : "MISMATCH");
			if(okCR)
				InterceptCall(&CloudRegionFlush_7273D0_orig,
					CloudRegionFlush_7273D0_hook, 0x719B2B);
		}
		// [PtLightGlow] one-shot shield-lever receipt (03:01-run black squares):
		// the flush installs above verified OK, yet the force NEVER ran —
		// skygfx.ini had coronaBlendShield=0 / spriteFlushShield=0 /
		// spriteStateShield=0, so the buffered-sprite flush draws
		// (spriteBatch 2-20) rasterised with SRCALPHA/INVSRCALPHA (181
		// [corona_blend] samples) and REPLACE (23 samples) — the glow
		// texture's opaque black surround -> black squares with bright
		// centers. Print the lever state at EVERY boot so a
		// disabled-shield run is identifiable from the log alone.
		dbglog("[PtLightGlow] shield levers: coronaBlendShield=%d spriteFlushShield=%d spriteStateShield=%d "
		       "(0 = flush draws unshielded -> black-square producer; parser default 1)",
		       config ? config->coronaBlendShield : -1,
		       config ? config->spriteFlushShield : -1,
		       config ? config->spriteStateShield : -1);
		// 0x53E21D  call CPointLights::Render (0x7002D0) — shield-window
		//           re-arm for the street-lamp fog-cone/glow-dot batch
		//           (see CPointLights__Render_shield_hook). Same
		//           byte-verification discipline as corona_install: the
		//           site must be E8 with extracted target 0x7002D0 — on
		//           MISMATCH skip the install, the vanilla path remains
		//           (cones may stay unshielded, but nothing gets
		//           corrupted).
		{
			BYTE opPL = *(BYTE*)0x53E21D;
			DWORD tPL = 0;
			if(opPL == 0xE8) tPL = *(DWORD*)(0x53E21E) + 0x53E21D + 5;
			bool okPL = (opPL == 0xE8 && tPL == 0x7002D0);
			dbglog("[pointlights_install] 0x53E21D op=%02X orig=%08X%s "
			       "(expect op=E8 orig=7002D0; MISMATCH = intercept "
			       "SKIPPED, vanilla path kept, cone batch unshielded)",
				(unsigned)opPL, (unsigned)tPL,
				okPL ? "OK" : "MISMATCH");
			if(okPL)
				InterceptCall(&CPointLights__Render_orig,
					CPointLights__Render_shield_hook, 0x53E21D);
		}
		// 0x53DE6B  the EARLY call CPointLights::Render (0x7002D0) — sky/
		//           reflection phase, BEFORE RenderScene. Un-intercepted
		//           until now, which is why the glow-sprite batch it flushes
		//           was rasterised outside the shield window (the black
		//           squares of the 2026-10-24 diagnosis). Same byte-verified
		//           discipline, same hook function and the SAME orig pointer
		//           as 0x53E21D: both sites call 0x7002D0, so the extracted
		//           target is identical — re-assigning it is not a double
		//           wrap (the hook calls the real function directly).
		{
			BYTE opPL = *(BYTE*)0x53DE6B;
			DWORD tPL = 0;
			if(opPL == 0xE8) tPL = *(DWORD*)(0x53DE6C) + 0x53DE6B + 5;
			bool okPL = (opPL == 0xE8 && tPL == 0x7002D0);
			dbglog("[pointlights_install] 0x53DE6B op=%02X orig=%08X%s "
			       "(expect op=E8 orig=7002D0; MISMATCH = intercept "
			       "SKIPPED, vanilla path kept, early glow batch "
			       "unshielded)",
				(unsigned)opPL, (unsigned)tPL,
				okPL ? "OK" : "MISMATCH");
			if(okPL)
				InterceptCall(&CPointLights__Render_orig,
					CPointLights__Render_shield_hook, 0x53DE6B);
		}
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

		InjectHook(0x53EBE9, DrawFinalEffects_pf);

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

		TexDefsInit();	// texdefs: txd-name hooks + the ONE texture-find CB (texdb + NM pairing)

		dbglog("=== DllMain complete, all hooks applied ===");
	}

	if(reason == DLL_PROCESS_DETACH){
		// NOTE: At DLL_PROCESS_DETACH, the game's RW/D3D9 engine may already
		// be torn down. RwRasterDestroy calls into RW internals (game-exe code)
		// that may be invalid → crash. Wrap all cleanup in __try/__except and
		// skip ReleaseDefaultPoolResources entirely (OS reclaims all memory).
		// DepthHook_ReleaseResources is also skipped — the device is gone.
		// TexDefsShutdown is safe (frees strdup'd strings, no RW/D3D9 calls).
		__try {
			TexDefsShutdown();
		} __except(EXCEPTION_EXECUTE_HANDLER) {
			dbglog("DLL_DETACH: TexDefsShutdown crashed, continuing");
		}
		dbglog("texdefs shutdown complete");

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

		// P1 softbody module shutdown - pure CPU (no RW/D3D9), safe
		// unconditionally at detach.
		SoftbodyShutdown();

		dbglog("DLL_DETACH complete");
	}

	return TRUE;
}

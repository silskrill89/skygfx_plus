#include "skygfx.h"
#include "transparency.h"
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
#include "renderstate_monitor.h"  // RSMon_* — three-layer (rw/driver/device) state monitor
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

// Only-once settings
bool disableClouds;
bool disableGamma;
bool fixPcCarLight;
int explicitBuildingPipe;
extern int explicitBuildingPipe_tmp;	// config.cpp — INI explicitBuildingPipe mirror (readIniFile/saveConfigTo)
// render_callbacks.cpp moved symbols (2026-11-09):
extern RpAtomic* myDefaultCallback(RpAtomic *atomic);	// render_callbacks.cpp — AtomicDefaultRenderCallBack (hooked 0x7491C0)
extern void vs_probe(const char *site, const char *tag);	// render_callbacks.cpp — vertex-stage drift probe
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
ModeId s_activeModeIni = MODE_NONE;	// shared with config.cpp (local extern)
bool s_activeModeIniSeen = false;	// shared with config.cpp (local extern)

// Stable INI alias for a mode (mirrors g_modeProfiles[].iniAlias in
// modeManager.cpp - keep in sync; that file is another lane's write scope).
const char *
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
bool
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

int defaultColourLeftUOffset;	// shared with config.cpp (local extern)
int defaultColourRightUOffset;
int defaultColourTopVOffset;
int defaultColourBottomVOffset;

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

void
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

// Shared [0,255] bound for every alpha-test reference fed to the FFP ALPHAAREF
// via setAlphaTestTwoLayer (the zwriteThreshold/zwriteThresholdGrass/
// zwriteThresholdPed trio). The three Config keys are clamped to [0,255] at
// parse, but the per-texture TexInfo override (texdb) is runtime data that is
// NOT parse-clamped, so this is the single ceiling that keeps a foreign/out-of-
// range value from ever reaching the device.
// NOTE (no 254 ceiling — a prior comment claimed one and was wrong): the dual
// legs tile [0,255] exactly at ANY ref, including 255 — leg 1 GREATEREQUAL@255
// passes alpha==255, leg 2 LESS@255 passes alpha<255, union covers every texel.
// A 254 clamp was an arbitrary, comment-justified lie; 255 is a valid, complete
// split point.
int
clampAlphaRef(int ref)
{
	if(ref < 0) ref = 0;
	if(ref > 255) ref = 255;
	return ref;
}

void
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
	alpharef = clampAlphaRef(alpharef);
	if (texInfo && !texInfo->dualPass) {
		dual = false;
	}
	if (dual && hasAlpha && zwrite) {
		int zwriteThreshold = config->zwriteThreshold;
		if (texInfo && texInfo->zwriteThreshold > 0) {
			zwriteThreshold = texInfo->zwriteThreshold;
		}
		// Single [0,255] bound (see clampAlphaRef): the Config key is parse-
		// clamped, but the per-texture TexInfo override is runtime data.
		zwriteThreshold = clampAlphaRef(zwriteThreshold);
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

	// coronaZtest: NO CODE PATCH is written (SilentPatchSA owns the corona
	// z-test and rewrites the bytes at 0x6FB17C itself. Patching there fought
	// that write (and case 1 restored a `call [ecx+0x20]` SilentPatch removed).
	// No bytes are written from this plugin; the INI/menu key stays valid.
	// (D11 reconciled: the key is NOT parse-only — the corona shield's
	// ZTEST ENFORCEMENT block, CCoronas::Render wrapper below, reads it to
	// force the RUNTIME z-test render state: 0 = PS2 off, 1/-1 = PC vanilla
	// on. "retired" below refers only to the old 0x6FB17C byte-patch path.)
	if(config->coronaZtest != -1 && dbglog_once("corona_ztest_retired"))
		dbglog("coronaZtest=%d retired - SilentPatch owns the corona z-test at 0x6FB17C (no bytes written)",
			config->coronaZtest);

}

// Env reflection sliders are hard-ranged [0..0.4] (ImGui sliders in
// debugmenu_ui.cpp; readIni clamps at load too). Single source of truth for
// that range, applied at INI load (readIniFile), after every reload/
// setConfig (refreshIni) and before every save (saveConfigTo) — no path can
// carry >0.4 into memory or onto disk (screenshot m0116 showed 2.700).
void
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
void
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
void
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
void
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
// only ONE named writer (readIniFile, config.cpp) plus the debug-menu row; the
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
		// every-frame caller (derivePipelineFromPipes) stays quiet.
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
const char *s_pipeDeriveTrigger = NULL;	// shared with config.cpp (local extern)

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
	// m2300 (user complaint: "pbr pipe and modern ones are connected and can't
	// be chosen individually"): the every-frame caller (ColourFilter_switch,
	// postfx.cpp:1937) and the menu-open derive (debugmenu_ui.cpp:4491) re-run
	// this function with NO adoption intent — firing the rewrite there stomped
	// an independently chosen vehiclePipe/colorFilter the moment buildingPipe
	// sat at BUILDING_PBR (log: `vp 9->10 cf 7->8 rule=3 trigger=per-frame`,
	// one frame after the raw vehiclePipe row edit; never-revert then parked
	// it). User-intent latch: fire ONLY on adoption-time callers —
	// s_pipeDeriveTrigger is set around the derive call by readIniFile
	// (config.cpp:339, INI pipeline-key parse) and by refreshIni (below, INI
	// reload/setConfig/named-config) and cleared right after; per-frame/menu
	// callers leave it NULL, so they only update the `c->pipeline` mirror
	// above. Writers that legitimately change all three fields do not need
	// this rewrite: ApplyPipelineFromMenu (menu pipeline row) and the mode
	// profiles write the fields themselves, and mode-owned live configs stay
	// excluded by the guard below.
	if(derived == PIPELINE_PBR && rule == 3 &&
	   s_pipeDeriveTrigger != NULL &&
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
// Parsed in readIniFile (config.cpp), round-tripped by saveConfigTo, and reached
// from the in-game menu through menuGet/SetNoiseQuality (near the hooks
// below). The noise tile itself is owned by SkyNoise now (built lazily by
// pipeBindNoise); the old utility-noise regen refreshIni used to run is
// gone (stages-0-3 L4).
int g_noiseQuality = 0;	// shared with config.cpp (local extern)

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

	// Capture the caller's cull so the restore returns it verbatim (not the
	// hard-coded CULLBACK). Pre-seeded + sanitised: a failed Get leaves the
	// known-good BACK, and an out-of-domain value reads as BACK — an
	// uninitialised/foreign value must never reach the device (same
	// convention as envmap.cpp's cull park/restore).
	RwUInt32 savedCull = (RwUInt32)rwCULLMODECULLBACK;
	RwRenderStateGet(rwRENDERSTATECULLMODE, &savedCull);
	if(savedCull != rwCULLMODECULLNONE && savedCull != rwCULLMODECULLBACK &&
	   savedCull != rwCULLMODECULLFRONT)
		savedCull = (RwUInt32)rwCULLMODECULLBACK;
	// Patch only when asked.
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
			RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)savedCull);
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
		// reach the device. The threshold is parse-clamped [0,255]; route it
		// through clampAlphaRef for the single bound (defense-in-depth).
		if(alphatest < rwALPHATESTFUNCTIONNEVER || alphatest > rwALPHATESTFUNCTIONALWAYS)
			alphatest = rwALPHATESTFUNCTIONGREATEREQUAL;
		alpharef = clampAlphaRef(alpharef);
		int zt = clampAlphaRef(config->zwriteThresholdGrass);
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
		RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)savedCull);
	gpCurrentPixelShaderForDefaultCallbacks = NULL;

	return ret;
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
	// R4 (2026-11-09): iCanHasbuildingPipe has zero writers tree-wide
	// (constant true), so the else-fallback below is unreachable and was
	// removed. Retained record of the removed fallback (C16's 3-layer fix):
	// it park-and-set ALPHAREF on all three layers to 64 for the PC tag
	// draw, restoring each layer's captured value afterwards, so a drifted
	// driver/device ALPHAREF could not break the next alpha-tested geometry.
	/* building pipe can handle accurate PS2 behaviour */
	assert(atomic->pipeline == CCustomBuildingPipeline__ObjPipeline);
	atomic->renderCallBack(atomic);
}

float
clamp(float f, float max)
{
	return f > max ? max : f;
}

RwRGBAReal &AmbientLightColourForFrame_PedsCarsAndObjects = *(RwRGBAReal*)0xC886C4;
RwRGBAReal &DirectionalLightColourForFrame = *(RwRGBAReal*)0xC886B4;

uint8 *ambRed = (uint8*)0xB7C3C8;
uint8 *ambGreen = (uint8*)0xB7C310;
uint8 *ambBlue = (uint8*)0xB7C258;

void (*SetLightsWithTimeOfDayColour_orig)(RpWorld*);
// INHERITED verbatim from backup_original:527-535. The body zeroes the three
// ambient colour byte globals (ambRed/ambGreen/ambBlue, 184 bytes each) on the
// default path, and floods them to 0xFF while F5 is held — a light-flash debug
// backdoor, not a look feature. Behaviour is unchanged: the default path still
// clears, and F5 still floods.
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
// W1 H2 discriminator - raw plant-struct colour bytes parked by
// saveIntensity() at its read site, consumed by the one-shot [H2disc]
// receipt in setTextureAndColor() below. Named globals only: the asm
// references them by symbol, so NO new address literal enters this path
// (the single raw address stays the install-layer 0x5DAE61 byte-verify).
unsigned char s_h2RawR, s_h2RawG, s_h2RawB;	// [esi+40h..42h] colour bytes

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

	// ---- W1 H2 discriminator: one-shot [H2disc] receipt -------------------
	// Answers "is the near-black card (log [veg] plant0 mat=(8,8,6,1)) the
	// mod's intensity FACTOR or legitimately dark plant DATA":
	//   tmpintensity = raw [esi+44h] byte saveIntensity() parked (0..255),
	//                  applied here as (tmpintensity * col) >> 8;
	//   plantRaw     = the patch's own read of [esi+40h..42h] (parked by
	//                  saveIntensity, so these are the bytes as-of the site);
	//   inColor      = what this function received (colour term, unmodulated);
	//   col          = inColor after the optional grassAddAmbient term, i.e.
	//                  the value that actually entered the >>8 multiply;
	//   newcolor     = the result committed to material->color.
	// READ: plantRaw low  => plant data itself is dark (data-side).
	//       plantRaw bright but newcolor crushed => the >>8 factor /
	//       tmpintensity scale is wrong (mod-side).
	// One-shot: first plant material of the session decides it.
	if(dbglog_once("h2_intensity_discriminator"))
		dbglog("[H2disc] tmpintensity=%d plantRaw=(%u,%u,%u) inColor=(%u,%u,%u,%u) col=(%u,%u,%u) ambOn=%d amb=(%.3f,%.3f,%.3f) newcolor=(%u,%u,%u,%u) tex='%s'",
		       tmpintensity,
		       (unsigned)s_h2RawR, (unsigned)s_h2RawG, (unsigned)s_h2RawB,
		       (unsigned)color->red, (unsigned)color->green,
		       (unsigned)color->blue, (unsigned)color->alpha,
		       col[0], col[1], col[2],
		       config->grassAddAmbient ? 1 : 0,
		       CTimeCycle_GetAmbientRed(), CTimeCycle_GetAmbientGreen(),
		       CTimeCycle_GetAmbientBlue(),
		       (unsigned)newcolor.red, (unsigned)newcolor.green,
		       (unsigned)newcolor.blue, (unsigned)newcolor.alpha,
		       (texture && texture->name[0]) ? texture->name : "(null)");

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
		// W1 H2 receipt: park the raw plant-struct colour bytes already in
		// al/cl/dl for setTextureAndColor()'s one-shot [H2disc] line (the
		// intensity byte itself is already in tmpintensity above). Stores
		// only - no register, no flag and no address literal touched.
		mov	byte ptr [s_h2RawR], al
		mov	byte ptr [s_h2RawG], cl
		mov	byte ptr [s_h2RawB], dl
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

// ---- J2 roadsign transparency hooks (junior/SkyGfxExtended-derived) ----
// Provenance: JuniorDjjr/skygfx (E:\SDKs\skygfx_junior, HEAD fb69df1)
// main.cpp:733-756 — feature v1.4 "Correction of transparency now does not
// cause errors in the texts of road signs". NOT in aap original. Both bodies
// are behaviour-identical to junior except the texdb rename: junior wrote
//   *RWPLUGINOFFSET(TexInfo*, texture, texdbOffset) = FindTexInfo("roadsign");
// which our TexDefsAssignInfo/TexDefsFindInfo pair replaces (same plugin-slot
// write, now NULL-guarded by TexDefsAssignInfo's texdbOffset<=0 early-out; a
// NULL TexInfo write is the safe no-override case, identical to junior).
// Audited 2026-11-09: NOT defective — left byte-identical. The RW0 rewrite
// (byte-verified rw_fixes.cpp wrappers, docs/plans/2026-11-09-junior-rewrite-
// plan.md §3 RW0) is a separate lane.
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
// Shared cache-coherent 3-layer (rw cache / RW driver cache / raw device)
// write chokepoint (pipelinecommon.cpp:1234). The CClouds blend/write
// restore below routes its four states through this so a single captured
// source can never leave L1/L2-vs-L3 split after the shield closes.
extern void pipeDeviceStateSet(D3DRENDERSTATETYPE state, DWORD value);

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
void fbWp_ArmOnce(void);	// externalised for clouds.cpp (moved cloud hygiene calls it)

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

// Shared frame-boundary sequence (R1, 2026-11-09): expire any deferred
// corona/cone shield window and (re-)push the canonical world baseline on all
// three layers. Triplicated across the four frame boundaries below; one
// helper so a gate/ordering fix can never miss a site.
// Ordering (both are load-bearing, not cosmetic):
//   * frame START (SceneStart) - baseline FIRST, window close LAST: the
//     restored pre-corona func/ref must win over the baseline's own block.
//   * frame END (EffectsExit) - window close FIRST, baseline LAST: the
//     canonical baseline must win for the next frame's first consumer.
// stateBaselineEnable=0 skips the baseline only; the window close is a safety
// net and always runs. One-shot probe per site (per-tag dbglog_once token)
// preserves each site's original "[probe] baseline <site> called" receipt.
static void
frameBoundaryCloseAndBaseline(const char *tag, const char *onceToken, const char *receipt, bool baselineFirst)
{
	if(baselineFirst){
		if(!config || config->stateBaselineEnable){
			pipeApplyWorldBaseline(tag);
			if(dbglog_once(onceToken))
				dbglog("[probe] baseline %s called", receipt);
		}
		CoronaWindow_Close();
	}else{
		CoronaWindow_Close();
		if(!config || config->stateBaselineEnable){
			pipeApplyWorldBaseline(tag);
			if(dbglog_once(onceToken))
				dbglog("[probe] baseline %s called", receipt);
		}
	}
}

bool
RenderScene_before(void*)
{
	// Frame START: canonical world baseline first, then expire any corona/
	// cone shield window the previous frame left open (baselineFirst — see
	// frameBoundaryCloseAndBaseline).
	frameBoundaryCloseAndBaseline("SceneStart", "probe_baseline_scenestart", "scenestart", true);
	// DEPTH-STATE REPAIR (2026-11-15): force ZENABLE=TRUE / ZFUNC=LESSEQUAL
	// on all three layers (rw cache / RwD3D9 driver cache / raw device) before
	// any scene geometry draws. The [SpriteDepthFix] repair at the sprite flush
	// was too late — the sky dome and buildings were already drawn with the
	// corrupted ZENABLE=0/ZFUNC=ALWAYS state, producing a black screen.
	// This matches AAP's simpler approach: AAP never suspends Z for postfx
	// samples, so the device stays on the correct depth state.
	// TRANSPARENCY REPAIR DISABLED (2026-11-15): user directive — running
	// vanilla game settings, should not need state-integrity fixes. The mod
	// should work like AAP's simpler approach without patching state.
	// Transparency_RepairDepthState();
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

	// aap parity #5 (2026-11-09): the game sets the far and fog plane AFTER
	// RwCameraBeginUpdate in Idle(), so the camera's cached planes are stale
	// at scene entry (aap re-updates here for exactly this reason — see
	// E:\SDKs\skygfx_original\src\main.cpp:738-740). Restored with the
	// begun-camera query the 2026-09-14 crash fix lacked: the exe global
	// pCameraBeingUpdated (0xC9BCC0) is set by RwCameraBeginUpdate and cleared
	// to NULL by RwCameraEndUpdate (postfx_smaa.cpp:237-241); game code at
	// 0x7F98DF dereferences [0xC9BCC0]+0x60 and faults on NULL. The old
	// unguarded EndUpdate+BeginUpdate ran under MoonLoader's first-frame
	// ordering with the camera NOT begun, so EndUpdate cleared
	// pCameraBeingUpdated and the next game BeginUpdate read NULL+0x60 — the
	// 2026-09-14 crash. Only re-update when the BEGUN camera is exactly our
	// Scene.camera (never unbalance a camera the game has not begun).
	if(Scene.camera && pCameraBeingUpdated == (void*)Scene.camera){
		RwCameraEndUpdate(Scene.camera);
		RwCameraBeginUpdate(Scene.camera);
		if(dbglog_once("far_fog_reupdate_live"))
			dbglog("[SceneStart] far/fog re-update restored (aap parity #5): EndUpdate+BeginUpdate ran");
	}

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
// contract as CSkidmarks__Render and the ped Switch (myDefaultCallback,
// render_callbacks.cpp).
//
// The same sweep clears a STUCK Im2D override: the manual
// `overrideIm2dPixelShader = ps; draw; = nil;` users in postfx.cpp skip
// their `= nil` when the draw faults into their __except (the class
// chars.cpp/SSS fixed with guardedIm2DRender), and Im2dSetPixelShader_hook
// re-applies that stale PS to EVERY later Im2D dispatch — i.e. straight
// onto the corona/particle draws below.
// ============================================================
extern void *overrideIm2dPixelShader;	// postfx.cpp
extern int overrideColorMod;		// postfx.cpp
extern int overrideAlphaMod;		// postfx.cpp
extern bool g_inCoronasRender;	// rw_fixes.cpp - corona shield window flag
extern bool g_inCloudsRender;	// rw_fixes.cpp - clouds producer flag (flush label)
extern bool s_spriteForceValid;	// rw_fixes.cpp - in-flight forced sprite batch

extern bool spriteProducerWindowActive(void);
extern void spriteProducerWindowClouds(bool);

void
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
	if(spriteProducerWindowActive()){
		if(dbglog_once("fxalpha_producer_skip"))
			dbglog("[FxAlpha] %s: producer owns additive state — canonical "
			       "alpha block skipped", tag);
		return;
	}
	// ---- stale Im2D override sweep (see block comment above) ----
	if(overrideIm2dPixelShader || overrideColorMod >= 0 || overrideAlphaMod >= 0){
		dbglog("[FxAlpha] %s: clearing stale Im2D override ps=%p colMod=%d alphaMod=%d",
		       tag, overrideIm2dPixelShader, overrideColorMod, overrideAlphaMod);
		overrideIm2dPixelShader = nil;
		overrideColorMod = -1;
		overrideAlphaMod = -1;
	}

	// ---- ROUGH REDUCTION (2026-11-10): 3-layer evidence sampling + additive
	// ONE/ONE substitution net removed. Each pass sets its own state (AAP
	// direct-state approach); the producer gate above is the only whole-function
	// skip kept (coronas/clouds/sprites own their additive state). The canonical
	// block push stays — one state pusher, no drift-comparison layer.
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
	frameBoundaryCloseAndBaseline("EffectsExit", "probe_baseline_renderfx", "effects_exit_renderfx", false);
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
	frameBoundaryCloseAndBaseline("EffectsExit", "probe_baseline_postfx", "effects_exit_postfx", false);
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
void
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
// CClouds::Render render-state hygiene (granular INI SkyGfx/cloud*Fix, all default 1)
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
// restored on exit. Six granular gates (cloudNoise/Filter/Alpha/Fog/Stage0/ZFix)
// default ON; each disables ONLY its own sub-fix. There is no master switch.
// ============================================================
void (*CClouds__Render_orig)(void);


// CClouds__Render_hygiene moved to clouds.cpp (2026-11-28 mechanical extraction).
void CClouds__Render_hygiene(void);

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
	// TODO(g_allowCrashPassThrough, m0271): the flag's only read — the
	// `if(g_allowCrashPassThrough)` in the VEH — was DEAD (both branches
	// returned EXCEPTION_CONTINUE_SEARCH) and was removed at the
	// crashhandler.cpp de-merge.  The reads are gone but these writes are
	// kept: main.cpp is single-writer-sensitive here.  Remove flag + these
	// writes + the debug_handl.h decl as ONE unit when this file is safe to
	// touch for that purpose (see the DEAD READ note in debug_handl.h).
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

BOOL FileExists(LPCTSTR szPath)
{
	DWORD dwAttrib = GetFileAttributes(szPath);
	return dwAttrib != INVALID_FILE_ATTRIBUTES && 
		   !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY);
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
// g_noiseQuality (main.cpp global; config.cpp externs it for INI load/save).
// The in-game menu reaches it through these accessors. The old regen path is
// gone (SkyNoise owns the tile; see the note in refreshIni).
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
extern void StateBlockEnforceCanonical(void);	// pipelinecommon.cpp — frame-end canonical contract assert

static void __fastcall
DrawFinalEffects_pf(void)
{
	CPostEffects::DrawFinalEffects();
	PfFrame();
	// Frame END: expire any deferred corona/cone window and leave the
	// canonical world baseline behind for the next frame's first consumer.
	frameBoundaryCloseAndBaseline("EffectsExit", "probe_baseline_final", "effects_exit_final", false);
	// Frame-end canonical contract assert (curative lever, 2026-11-09): the
	// frame-end twin of the scene-start repair. Asserts the canonical
	// depth+blend contract {ALPHABLEND=0, ZWRITE=1, ZTEST=1, ZFUNC=LESSEQUAL}
	// on ALL THREE layers after the last 2D write (DrawFinalEffects above),
	// so the residue the 2D/effects tail manufactures can never survive to
	// the next scene_start. Unconditional (not gated on stateBaselineEnable);
	// silent when healthy, [frame_repair] receipt only when a repair fires.
	StateBlockEnforceCanonical();
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

extern void water_installHooks(void);	// water.cpp — water/wake/temp-buffer hook installs
extern void underwater_installHooks(void);	// underwater.cpp — neoWaterDrops + splash-foam installs

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

	Nop(0x5BBF6F, 2);
	Nop(0x5BBF83, 2);

	explicitBuildingPipe = explicitBuildingPipe_tmp;

	dbglog("  HOOK: TexDefsExternalInit()");
	TexDefsExternalInit();

	// R4 (2026-11-09): iCanHasbuildingPipe/iCanHasvehiclePipe are constant
	// true (zero writers tree-wide), so the guards are dropped.
	dbglog("  HOOK: hookBuildingPipe()");
	hookBuildingPipe();
	dbglog("  HOOK: hookVehiclePipe()");
	hookVehiclePipe();

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
	// InterceptCall keeps the original target in CClouds__Render_orig. The
	// hygiene is now ALWAYS installed — the six granular cloud*Fix settings
	// gate individual sub-fixes inside it (all default 1); there is no master
	// switch that can turn cloud correctness off.
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
	// water-droplet/splash-foam installs moved to underwater.cpp (underwater_installHooks)
	underwater_installHooks();
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

	// water/wake/temp-buffer installs moved to water.cpp (water_installHooks)
	water_installHooks();

	// W8-2: CEntity::ModifyMatrixForTreeInWind replacement (treeWind.cpp).
	// Stock has exactly one direct caller (0x536145: `mov ecx, esi; call
	// 0x534E90`), thiscall => __fastcall body needs no trampoline. Startup-gated
	// by treeWind (default 0 this round); treeWind=1 keeps the stock routine.
	if(config && config->treeWind){
		dbglog("  HOOK: TreeWind ::ModifyMatrixForTreeInWind (0x534E90)");
		if(config->treeWind)
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

extern void shadows_installHooks(void);	// shadows.cpp — shadow state-contract installs
extern void rwFixes_installHooks(void);	// rw_fixes.cpp - RW fix-machine installs (rwFixesEnable gate)

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

		// moon mask (gated on its lever)
		if(config->moonMaskAlpha)
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
		if(config->grassAddAmbient || config->fixGrassPlacement ||
		   config->ps2ModulateGrass || config->grassInstancing)
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
		if(config->grassAddAmbient || config->fixGrassPlacement ||
		   config->ps2ModulateGrass || config->grassInstancing){
			static const unsigned char s_exp_5DAE61[8] = { 0x0F, 0xB6, 0x46, 0x44, 0x89, 0x44, 0x24, 0x28 };
			if(verifyBytes((void*)0x5DAE61, s_exp_5DAE61, 8, "0x5DAE61"))
				InjectHook(0x5DAE61, saveIntensity, PATCH_JUMP);
			Patch(0x5DAEC8, setTextureAndColor);
		}

		// add dual pass for PC pipeline
		InjectHook(0x5D9EEB, D3D9RenderDefault_DUAL);
		InjectHook(0x5D9EFB, D3D9RenderBlack_DUAL);

		// give vehicle pipe to upgrade parts
		InjectHook(0x4C88F0, 0x5DA610, PATCH_JUMP);

		// jump over code that sets alpha ref to 140 (not on PS2)
		InjectHook(0x553AD1, 0x553AE5, PATCH_JUMP);
		InterceptCall(&CSkidmarks__Render_orig, CSkidmarks__Render, 0x53E175);

		// shadow state-contract installs moved to shadows.cpp (shadows_installHooks)
		shadows_installHooks();

		// ---- effects-phase three-layer alpha/Im2D resync (coronas,
		// particles, glows — see effectsForceAlphaBlock) ----
		// 0x53EAD3  call RenderEffects (0x53E170) — CCoronas::Render,
		//           g_fx.Render (particles), glass, weapon/special FX
		// 0x53DCAC / 0x53DFD8  call CCoronas::RenderReflections (0x6FB630)
		// 0x53E12B  call CCoronas::RenderSunReflection (0x6FBAA0)
		// (CClouds::Render itself is already intercepted at 0x53DCA2 /
		//  0x53DFA0 and forces the same block in its hygiene hook.)
		// Vanilla-fallback doctrine: corona/state-repair intercepts only when
		// the corona blend shield lever is on; zeroed = stock CCoronas::Render.
		if(config->coronaBlendShield){
			InterceptCall(&RenderEffects_orig, RenderEffects_hook, 0x53EAD3);
			InterceptCall(&CCoronas__RenderReflections_orig, CCoronas__RenderReflections_hook, 0x53DCAC);
			InterceptCall(&CCoronas__RenderReflections_orig, CCoronas__RenderReflections_hook, 0x53DFD8);
			InterceptCall(&CCoronas__RenderSunReflection_orig, CCoronas__RenderSunReflection_hook, 0x53E12B);
		}
		// RenderWare-level FIX machinery (corona/flush/font/pointlight shields)
		// moved to rw_fixes.cpp - rwFixes_installHooks(), gated on rwFixesEnable.
		rwFixes_installHooks();
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

		// End-of-session crash-correlation summary (the final diagnostic
		// line before the module unloads), then remove the vectored
		// exception handler installed at diag_installVEH().
		diag_dumpStats();
		diag_removeVEH();

		dbglog("DLL_DETACH complete");
	}

	return TRUE;
}

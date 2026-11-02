#pragma once
#define _CRT_SECURE_NO_WARNINGS
#pragma warning(disable: 4244)	// int to float
#pragma warning(disable: 4800)	// int to bool
#pragma warning(disable: 4838)  // narrowing conversion
#pragma warning(disable: 4996)  // strcmpi

#define _USE_MATH_DEFINES

#include <windows.h>
#include <rwcore.h>
#include <rwplcore.h>
#include <rpworld.h>
#include <rpmatfx.h>
#include <d3d9.h>
#include <d3d9types.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include "resource.h"
#include "MemoryMgr.h"
#include "Pools.h"
#include "LinkList.h"

// d3d9types.h only defines the four D3DCOLORWRITEENABLE_* channel bits —
// there is no "ALL" macro in the SDK. Colour-write repair (pipeEnterAlphaMode
// / pipeForceAlphaBlock) needs the mask that enables every channel.
#ifndef D3DCOLORWRITEENABLE_ALL
#define D3DCOLORWRITEENABLE_ALL \
	(D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_GREEN| \
	 D3DCOLORWRITEENABLE_BLUE|D3DCOLORWRITEENABLE_ALPHA)
#endif

typedef uint8_t uint8, uchar;
typedef uint16_t uint16, ushort;
typedef uint32_t uint32, uint;
typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef bool bool8;

// Fix for _mm_loadu_si64 intrinsic - x64 only, map to x86 equivalent
#ifdef _M_X86
#define _mm_loadu_si64 _mm_loadu_si32
#endif

extern HMODULE dllModule;

// ---- Frame correlation prefix (dbglog / dbglog_loc / warn / err) ----
// Every line written to skygfx_dbg.log carries this prefix immediately
// before the message text, e.g.
//     [HH:MM:SS.mmm] [INFO ] :0 () - [#12345:3000ABCD] message
// <frameId> = widened RW render-frame tick (decimal), <code> = packed
// frame/pipeline correlation code (8 hex digits).  The on-screen overlay
// lane prints the same <code> via DebugOverlayCode(), so one displayed
// number maps a screen capture back to the log lines of that frame.
// Injected at the single log sink (dbglog_internal, debug_handl.cpp) into
// its fixed stack buffer — no allocation, and no call-site macro
// indirection: dbglog stays a plain variadic function (a few call sites
// pass non-literal formats).  Frame id API, overlay helper and the exact
// <code> bit layout: debug_handl.h "FRAME CORRELATION".
#define DBGLOG_FRAME_PREFIX_FMT "[#%u:%08X] "

void dbglog(const char *fmt, ...);	// prefix added writer-side; signature unchanged

enum LogLevel { LOG_TRACE = -1, LOG_INFO = 0, LOG_WARN = 1, LOG_ERROR = 2, LOG_FATAL = 3 };
void dbglog_loc(int level, const char *file, int line, const char *func, const char *fmt, ...);

#define dbglog_warn(...) dbglog_loc(LOG_WARN, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define dbglog_err(...) dbglog_loc(LOG_ERROR, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

// ---- Performance timing ----
extern LARGE_INTEGER perfFreq;
extern double perfFreqInv;
inline void perfInit(){
	if(!perfFreq.QuadPart){
		QueryPerformanceFrequency(&perfFreq);
		perfFreqInv = 1.0 / (double)perfFreq.QuadPart;
	}
}
inline double perfNow(){
	// Self-initialize on first call (perfFreq is zero-initialized at file scope)
	if(!perfFreq.QuadPart){
		QueryPerformanceFrequency(&perfFreq);
		perfFreqInv = 1.0 / (double)perfFreq.QuadPart;
	}
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return (double)t.QuadPart * perfFreqInv * 1000.0;
}
struct PerfTimer {
	const char *name;
	double startMs;
	PerfTimer(const char *n) : name(n), startMs(perfNow()) {}
	~PerfTimer(){ dbglog("PERF [%s] %.2f ms", name, perfNow() - startMs); }
};
#define PERF_SCOPE(name) PerfTimer _perf##__LINE__(name)

// Rate-limited logging: logs at most once per interval (ms)
#define DBGLOG_THROTTLE_MS 2000

// Diagnostic verbosity latch (canonical decl + docs: debug_handl.h).
// 0 = current behavior; >=1 stretches dbglog_throttle's interval 5x;
// >=2 silences every throttle-gated per-frame receipt.  Lanes can also
// read it directly around their own per-frame dbglog() calls.
extern int g_diagLevel;

// Returns true if the log should be emitted this call
// 2s gate semantics unchanged; the gated dbglog() line carries the same
// "[#<frameId>:<code>] " prefix as every other log line (writer-side).
//
// Cost: the hot path is a pointer-identity memo (string literals are
// stable addresses) — a multiply, one probe, one compare and GetTickCount;
// no per-character hash walk.  The canonical FNV-1a slot (256 buckets;
// the old 64-bucket x31 hash collided across the project's ~276 tags,
// permanently starving critical per-frame tags) remains the source of
// truth, so the interval stays GLOBAL per tag text even when two call
// sites pass equal literals from different addresses.  A pointer-hash
// collision just evicts the memo — that call falls back to the canonical
// walk (old cost) and re-installs.
inline bool dbglog_throttle(const char *tag) {
	struct ThrottleSlot { const char *lit; DWORD last; int canon; };
	static ThrottleSlot s_fast[256] = {};
	static DWORD s_lastTick[256] = {};

	if(g_diagLevel >= 2) return false;

	DWORD interval = (g_diagLevel >= 1)
		? (DWORD)DBGLOG_THROTTLE_MS * 5 : (DWORD)DBGLOG_THROTTLE_MS;
	DWORD now = GetTickCount();

	// fast path: pointer-identity memo (no string walk)
	uint32 fh = (uint32)((((uintptr_t)tag) >> 4) * 2654435761u);
	ThrottleSlot &f = s_fast[fh & 255];
	if(f.lit == tag){
		if(now - f.last < interval) return false;
		f.last = now;
		s_lastTick[f.canon] = now;   // keep the canonical slot in sync
		return true;
	}

	// slow path: canonical FNV-1a slot (global per tag text)
	uint32 h = 2166136261u;
	for(const char *p = tag; *p; p++){
		h ^= (uint32)(unsigned char)*p;
		h *= 16777619u;
	}
	int idx = (int)(h & 255u);
	if(now - s_lastTick[idx] < interval) return false;
	s_lastTick[idx] = now;
	f.lit = tag;      // (re)install the pointer memo
	f.last = now;
	f.canon = idx;
	return true;
}

// ---- First-run-only guard: logs exactly once per tag ----
inline bool dbglog_first(const char *tag) {
	static bool fired[64] = {};
	int idx = 0;
	for(const char *p = tag; *p; p++) idx = idx * 31 + (unsigned char)*p;
	idx = (idx & 0x7FFFFFFF) % 64;
	if(fired[idx]) return false;
	fired[idx] = true;
	return true;
}

// ---- True one-shot per tag: fires exactly once for the process lifetime ----
// Not time-based, so critical first-hit probes can never be throttled away.
inline bool dbglog_once(const char *tag) {
	static bool fired[256] = {};
	uint32 h = 2166136261u;
	for(const char *p = tag; *p; p++){
		h ^= (uint32)(unsigned char)*p;
		h *= 16777619u;
	}
	int idx = (int)(h & 255u);
	if(fired[idx]) return false;
	fired[idx] = true;
	return true;
}

// ---- Scope tag registry: maps address→name for crash backtrace hints ----
#define SCOPE_TAG_MAX 64
struct ScopeTag {
	const char *name;
	void *addr;
};
extern ScopeTag g_scopeTags[SCOPE_TAG_MAX];
extern int g_scopeTagCount;
extern int g_scopeTagLock; // simple volatile int used as spinlock from single thread

void diag_registerScope(const char *name, void *addr);
void diag_lookupScope(void *addr, char *outName, int outSize, int *outOffset);

// Logger ring buffer for crash dump
void diag_ringPut(const char *msg);
void diag_ringDump(void);

// First-run-only guard macros for pass lifecycle
#define DBGLOG_ENTER(name) do { \
	static bool _entered = false; \
	if(!_entered) { _entered = true; dbglog("%s enter", name); } \
} while(0)

#define DBGLOG_EXIT_OK(name) do { \
	static bool _exited = false; \
	if(!_exited) { _exited = true; dbglog("%s exit ok", name); } \
} while(0)

#define DBGLOG_BAIL(name, reason) do { \
	static bool _bailed = false; \
	if(!_bailed) { _bailed = true; dbglog("%s exit BAIL: %s", name, reason); } \
} while(0)

#define nil NULL
#define VERSION 0x370

#include "gta.h"


enum CarPipeline
{
	CAR_PS2,
	CAR_PC,
	CAR_XBOX,
	CAR_SPEC,
	CAR_MOBILE,
	CAR_NEO,
	CAR_LCS,
	CAR_VCS,
	CAR_ENV,
	CAR_GTAIV,
	CAR_MODERN, // PBR with glass, 4 color channels, GGX specular

	NUMCARPIPES
};

enum BuildingPipeline
{
	BUILDING_PS2,
	BUILDING_XBOX,
	BUILDING_GTAIV,
	BUILDING_PBR,

	NUMBUILDINGPIPES
};

// Unified Pipeline - single setting controls all asset types
enum Pipeline
{
	PIPELINE_PBR,      // Unified PBR for all assets (buildings, vehicles, peds, terrain, vegetation, water)
	PIPELINE_PS2,      // PS2-style rendering (locked preset)
	PIPELINE_XBOX,     // Xbox-style rendering (locked preset)
	PIPELINE_MOBILE,   // Mobile-style rendering (locked preset)
	PIPELINE_GTAIV,    // GTA IV-style rendering (locked preset)

	NUMPIPELINES
};

enum DefinedVertexShader
{
	DEFAULT,
	WIND,

	NUMSHADERS
};

// Game presets - emulates specific game/platform combinations
enum GamePreset
{
	PRESET_CUSTOM = -1,    // Manual settings (legacy behavior)

	// GTA III presets
	PRESET_III_PS2,
	PRESET_III_XBOX,
	PRESET_III_PC,

	// GTA Vice City presets
	PRESET_VC_PS2,
	PRESET_VC_XBOX,
	PRESET_VC_PC,

	// GTA San Andreas presets
	PRESET_SA_PS2,
	PRESET_SA_XBOX,
	PRESET_SA_PC,

	// GTA Liberty City Stories
	PRESET_LCS_PS2,

	// GTA Vice City Stories
	PRESET_VCS_PS2,

	// GTA IV presets
	PRESET_IV_XBOX360,
	PRESET_IV_PC,

	// Best-of-all default (PC pipe with best settings from all versions)
	PRESET_BEST_PC,

	// skygfxplusultramaxdeluxe — everything maxed, all features on
	PRESET_SKYGFXPLUSULTRAMAXDELUXE,

	NUM_PRESETS
};

// Preset configuration - defines what each preset sets
struct PresetConfig
{
	const char *name;
	int buildingPipe;
	int vehiclePipe;
	int colorFilter;
	int ps2ModulateGlobal;
	int dualPassGlobal;
	int radiosity;
	int doRadiosity;
	int vcsTrails;
	int pedShadows;
	int stencilShadows;
	int grainFilter;
	int infraredVision;
	int nightVision;
	int ssaoEnable;
	int smaaEnable;
	int smaaPreset;
	int ivMode;
};

extern const PresetConfig presetConfigs[NUM_PRESETS];
extern const char *presetNames[NUM_PRESETS + 1];

struct Config {
	// these are at fixed offsets
	int version;			// for other modules
	int preset;				// GamePreset enum (-1 = custom/manual)
	RwBool fixGrassPlacement;	// offset +8, pinned: fixSeed asm reads [ecx+8] in main.cpp
	RwBool doglare;			// offset +12, pinned: doglare asm reads [ecx+12] in main.cpp

	// Unified Pipeline - single setting controls all asset types
	int pipeline;			// Pipeline enum (PBR, PS2, Xbox, Mobile, GTAIV)

	int buildingPipe;		// Internal: mapped from pipeline
	int tagsBuildingPipe;
	RwBool ps2ModulateBuilding;
	RwBool dualPassBuilding;

	// ---- PBR building env / reflection (ported from the GTA IV building path)
	// Consumed by main_building() in VehiclePBR_Modern.hlsl through PS c47 and
	// uploaded per mesh by the PBR cb (buildingPipe.cpp); full contract in the
	// env block at the end of main_building.
	//
	// bldEnvReflect (default 1): master switch for the building env-map lane.
	//   Gates the PBR cb (buildingPipe.cpp ~:2895 -- env texture AND the
	//   c47.x-gated sky reflection in main_building) AND the Xbox cb
	//   (~:2006 envOk). It does NOT gate the PS2 or GTA IV building lanes, nor
	//   any vehicle reflection. 0 = off (both lanes fall back to the non-env
	//   pixel shader / no env or sky term). Historically PBR-only; the Xbox
	//   reuse landed 2026-10-26.
	RwBool bldEnvReflect;
	// bldSkyReflect (0.0-1.0, default 0.15, 0 = off): the superset term IV does
	//   NOT have — a Fresnel-weighted reflection of the already-bound IBL sky
	//   capture for materials without their own env map. Energy-consistent
	//   (F*env + (1-F)*body, weight forced to 0 when the capture is empty) so
	//   it can never darken a wall.
	float bldSkyReflect;

	RwBool usePCTimecyc;
	RwBool ps2ModulateGrass;
	RwBool grassAddAmbient;
	RwBool backfaceCull;
	RwBool dualPassDefault, dualPassGrass, dualPassVehicle, dualPassPed;
	int vehiclePipe;
	float neoShininessMult, neoSpecularityMult;
	int colorFilter;
	int infraredVision, nightVision, grainFilter;
	RwBool doRadiosity;
	int radiosityFilterPasses, radiosityRenderPasses;
	int radiosityIntensity;
	int offLeft, offRight, offTop, offBottom;
	RwBool vcsTrails;
	int trailsLimit, trailsIntensity, trailsResolution;
	int pedShadows, stencilShadows;
	int lightningIlluminatesWorld;
	RwBool neoWaterDrops;
	RwBool neoBloodDrops;

	// m0336 GPU particle lane (src/render/particles_gpu.cpp): effectsPC.txd
	// texture harvest + SM3.0 quad-batch sprite path (boat wake first slice,
	// live-generated from the WakeDirectFlush hooks, scaled with the
	// c11/c12 wind-sea constants). DEFAULT 0 — stable-config rule: new
	// features ship inert; every entry point no-ops while this is 0.
	RwBool gpuParticlesEnable;

	RwBool ps2ModulateGlobal, dualPassGlobal;

	int keys[2];

	bool bYCbCrFilter;
	float lumaScale, lumaOffset;
	float cbScale, cbOffset;
	float crScale, crOffset;
	float rgb1Mult, rgb2Mult;
	int zwriteThreshold;
	int zwriteThresholdGrass;
	int zwriteThresholdPed;

	float leedsShininessMult;
	RwBool detailMaps;
	RwBool noiseDetail;
	RwBool envMapUseLODs;
	int envMapSize;
	float envMapFarClipMult;
	// W7-C: reflection MSAA via env-map SUPERsampling — RW3.6 rasters have no
	// multisample API, so {0,2,4,8} scales the env-map raster resolution
	// (envMapSize × level, cap 2048² — EnvMapRasterSize in envmap.cpp).
	// 0 = off (1×). Snapped in readIniFile; anything not in {0,2,4,8} = 0.
	int reflectionMsaaLevel;
	// W7-C: CMirrors composite distortion fix (FusionFix mirrorPS1.asm port —
	// src/render/mirrorPass.cpp + Mirror_Composite.hlsl). 0 = the game's
	// fixed-UV fullscreen blit, 1 = per-pixel projective composite overdraw.
	RwBool mirrorDistortionFix;

	int radiosity;
	int coronaZtest;

	// Sun flare control (PS2→PC conversion fix)
	// On PS2, GS modulation (A×B)/128 ≈ D3D MODULATE2X made sun corona look correct.
	// On PC, D3D9 (A×B)/255 is dimmer, but R* kept the same timecycle values.
	// The PBR tonemap amplifies these values further, causing massive flare blowout.
	float sunCoronaIntensity;	// 0.0-2.0, scales sunCoronaR/G/B (default 0.4)
	float sunCoreIntensity;		// 0.0-2.0, scales sunCoreR/G/B (default 0.6)
	float sunStreakIntensity;	// 0.0-2.0, scales spriteBrightness (default 0.5) — controls lens flare streak brightness
	float sunStreakSize;		// 0.0-2.0, scales spriteSize (default 0.6) — controls lens flare streak size

	// Alt timecyc (weathers2.dat set-2) overwrite of m_CurrentColours.
	// 0 = OFF (default, vanilla): game's own CColourSet ambient/direct flow
	// through untouched. 1 = legacy exp-19 behavior (set-2 applied at t=1.0).
	int altTimecycEnable;

	float envShininessMult;
	float envSpecularityMult;
	float envPower;
	float envFresnel;

	// Normal mapping
	RwBool normalMapEnable;
	float normalMapIntensity;
	RwBool normalMapPlayerOnly;

	// Subsurface Scattering
	RwBool sssEnable;
	float sssIntensity;		// global intensity multiplier (0..1)
	float sssVegIntensity;	// vegetation-specific override
	float sssSkinIntensity;	// skin-specific override
	float sssClothIntensity;	// cloth-specific override

	// SSAO
	RwBool ssaoEnable;
	float ssaoRadius;
	float ssaoPower;
	float ssaoKernelSize;
	int ssaoSampleCount;

	// SSAO overhaul (quarter-res temporal)
	RwBool ssaoTemporalEnable;     // default 1
	float  ssaoTemporalBlend;      // default 0.1 (90% history)
	int    ssaoBlurPasses;         // default 2
	float  ssaoBlurRadius;         // default 3.0
	float  ssaoDepthThreshold;     // default 0.01

	// SMAA — single toggle, preset/temporal are internal constants
	RwBool smaaEnable;
	int _reserved_smaaPreset;      // padding — NOT live: no Config reader exists
	                               // (PresetConfig::smaaPreset is unread too).
	                               // Kept only for ABI.
	RwBool _reserved_smaaPredication; // padding — was smaaPredication
	RwBool _reserved_smaaTemporal;    // padding — was smaaTemporal, now internal constant

	// Motion Blur (Burnout Paradise style)
	RwBool motionBlurEnable;
	float motionBlurStrength;		// 0.0-1.0, overall intensity
	float motionBlurRadial;			// 0.0-1.0, radial component from screen center
	float motionBlurSpeedFactor;		// 0.0-1.0, how much camera velocity affects blur
	RwBool motionBlurCameraAware;	// reduce blur when camera is moving fast

	// SSS Post-Process Blur (for skin translucency)
	// NOTE: This is a screen-space effect, not per-material. It blurs the entire
	// scene and preserves edges using depth. Best used with low strength values.
	// Does NOT conflict with the per-material SSS fields above.
	RwBool sssPostProcessEnable;
	float sssPostProcessStrength;	// 0.0-1.0, how much SSS blur to apply
	float sssPostProcessRadius;	// blur radius in pixels (higher = softer skin)
	float sssPostProcessThreshold;	// depth threshold for edge preservation
	float sssAmbientBoost;		// 0.0-1.0 (default 1.0): scales the timecycle
					// ambient the SSS blur adds to character pixels
					// (shader c21). 0 = pure blur, no glow.

	// Skin Enhancement - wrap lighting for SSS approximation
	// NOTE: Works ON TOP of existing Rpskin rendering. Does NOT replace it.
	// Adds warm tint to shadow areas and improves specular highlights.
	RwBool skinEnhanceEnable;
	float skinWrapFactor;		// 0.0-1.0, how much light wraps around surface
	float skinSpecularPower;	// specular highlight sharpness
	float skinSpecularStrength;	// specular highlight intensity
	float skinSSSStrength;		// 0.0-1.0, SSS effect strength

	// Hair Enhancement - anisotropic highlights
	// NOTE: Works ON TOP of existing hair rendering. Uses depth derivatives
	// to estimate tangent direction for Kajiya-Kay anisotropic highlights.
	RwBool hairEnhanceEnable;
	float hairAnisotropicPower;		// highlight sharpness
	float hairAnisotropicStrength;	// highlight intensity
	float hairSSSStrength;			// 0.0-1.0, hair SSS strength

	// Vegetation Enhancement - improved grass/plant rendering
	// NOTE: Works ON TOP of existing grass rendering. Adds SSS-like translucency
	// and improved ambient lighting to vegetation.
	RwBool vegetationEnhanceEnable;
	float vegetationSSSStrength;	// 0.0-1.0, translucency strength
	float vegetationAmbientBoost;	// ambient light multiplier

	// GTA IV Mode
	RwBool ivMode;
	float ivDesaturation;
	float ivGamma;
	float ivSaturation;
	float ivCurves;
	float ivVignetteIntensity;
	float ivVignetteRadius;
	float ivVignetteContrast;
	float ivBloomIntensity;
	float ivExposure;

	// Expanded Weather / Timecycle (GTA V style)
	// Sky colors
	float skyZenithR, skyZenithG, skyZenithB, skyZenithInten;
	float skyZenithTransR, skyZenithTransG, skyZenithTransB, skyZenithTransInten;
	float skyAzimuthEastR, skyAzimuthEastG, skyAzimuthEastB, skyAzimuthEastInten;
	float skyAzimuthTransR, skyAzimuthTransG, skyAzimuthTransB, skyAzimuthTransInten;
	float skyAzimuthWestR, skyAzimuthWestG, skyAzimuthWestB, skyAzimuthWestInten;
	float skyPlaneR, skyPlaneG, skyPlaneB, skyPlaneInten;
	
	// Sun
	float sunR, sunG, sunB;
	float sunDiscR, sunDiscG, sunDiscB;
	float sunDiscSize;
	float sunMiePhase, sunMieScatter, sunMieIntenMult;
	float sunInfluenceRadius, sunScatterInten;
	
	// Moon/Stars
	float moonR, moonG, moonB;
	float moonDiscSize;
	float moonInten, starsInten;
	float moonInfluenceRadius, moonScatterInten;
	
	// Clouds
	float cloudGenFreq, cloudGenScale, cloudGenThresh, cloudGenSoftness;
	float cloudDensityMult, cloudDensityBias;
	float cloudMidR, cloudMidG, cloudMidB;
	float cloudBaseR, cloudBaseG, cloudBaseB;
	float cloudBaseStrength;
	float cloudShadowR, cloudShadowG, cloudShadowB;
	float cloudShadowStrength;
	float cloudGenDensityOffset, cloudOffset;
	float cloudOverallStrength, cloudOverallColor, cloudEdgeStrength;
	float cloudFadeout, cloudHDR, cloudDitherStrength;
	float smallCloudR, smallCloudG, smallCloudB;
	float smallCloudDetailStrength, smallCloudDetailScale;
	float smallCloudDensityMult, smallCloudDensityBias;
	
	// Light
	float lightDirR, lightDirG, lightDirB, lightDirMult;
	float lightDirAmbR, lightDirAmbG, lightDirAmbB, lightDirAmbInten, lightDirAmbIntenMult, lightDirAmbBounce;
	float lightAmbDownWrap;
	float lightNatAmbDownR, lightNatAmbDownG, lightNatAmbDownB, lightNatAmbDownInten;
	float lightNatAmbBaseR, lightNatAmbBaseG, lightNatAmbBaseB, lightNatAmbBaseInten, lightNatAmbBaseIntenMult;
	float lightArtifIntAmbDownR, lightArtifIntAmbDownG, lightArtifIntAmbDownB, lightArtifIntAmbDownInten;
	float lightArtifIntAmbBaseR, lightArtifIntAmbBaseG, lightArtifIntAmbBaseB, lightArtifIntAmbBaseInten;
	float lightArtifExtAmbDownR, lightArtifExtAmbDownG, lightArtifExtAmbDownB, lightArtifExtAmbDownInten;
	float lightArtifExtAmbBaseR, lightArtifExtAmbBaseG, lightArtifExtAmbBaseB, lightArtifExtAmbBaseInten;
	float pedLightR, pedLightG, pedLightB, pedLightMult;
	float pedLightDirX, pedLightDirY, pedLightDirZ;
	
	// PostFX
	float postfxExposure, postfxExposureMin, postfxExposureMax;
	float postfxBrightPassThreshWidth, postfxBrightPassThresh;
	float postfxIntensityBloom;
	float postfxCorrectR, postfxCorrectG, postfxCorrectB, postfxCorrectCutoff;
	float postfxShiftR, postfxShiftG, postfxShiftB, postfxShiftCutoff;
	float postfxDesaturation;
	float postfxNoise, postfxNoiseSize;
	
	// Vignette
	float vignetteIntensity, vignetteRadius, vignetteContrast;
	float vignetteR, vignetteG, vignetteB;
	
	// Color grading
	float gradTopR, gradTopG, gradTopB;
	float gradMidR, gradMidG, gradMidB;
	float gradBotR, gradBotG, gradBotB;
	float gradMidpoint, gradTopMidMidpoint, gradMidBotMidpoint;
	
	// Lens
	float lensDistortionCoeff, lensDistortionCubeCoeff;
	float lensChromaticAberrationCoeff, lensChromaticAberrationCubeCoeff;
	float lensArtefactsInten, lensArtefactsIntenMinExp, lensArtefactsIntenMaxExp;
	
	// Water
	// REMOVED (W7-C): `float waterReflectionFarClip` — dead key since
	// introduction: parsed (main.cpp), saved, menu'd, but never read by any
	// consumer. Deleted from struct/parse/save/menu; stale INI entries are
	// simply ignored by the reader.
	RwBool waterParallaxEnable;
	float waterParallaxScale;
	float waterNormalStrength;
	float waterFresnelPower;
	float waterSpecularPower;
	float waterSpecularIntensity;
	float waterUnderwaterFog;
	RwBool grassInstancing;         // W8-3: batched plant island master switch (default 0)
	float waterFoamThreshold;
	float waterFoamSoftness;
	float waterShallowR, waterShallowG, waterShallowB;
	float waterDeepR, waterDeepG, waterDeepB;
	// Water rewrite knobs (Xbox/IV-style water)
	float waterTileScale;          // detail normal UV frequency (higher = finer tiles)
	float waterShoreFade;          // metres of depth ramp for soft shore blending
	float waterTranslucency;       // master translucency/opacity scale
	float waterReflectionStrength; // reflection reflectivity cap (< 1, anti-chrome)
	float waterReflectionMix;      // reflection/colour mix (0 = colour only, 1 = full mirror)
	int   waterUseTimecycle;       // 1 = water colour/alpha from timecyc, 0 = INI overrides
	// W7-A smooth shorelines / foam — master gate (default 1 = current behaviour).
	// 0 = turn the ported LR smooth-shoreline coverage + foam band OFF by
	// pushing their ranges out of reach (waterPipe.cpp consumer, no shader edit).
	RwBool waterShoreFoam;
	// W7-B body classification gate (waterPipe VS c10.x / PS c14). 0 = skip
	// WbClassifyFlush entirely -> TEXCOORD5 stays 0, c14/c15 0 -> the PS's
	// WaterBodyContract pins every scale to exactly 1.0 (vanilla identity).
	RwBool waterBodyClassify;
	// W8-2 tree lane
	RwBool treeWind;               // 1 (default) = install the ModifyMatrixForTreeInWind port; 0 = stock routine
	int    treeFx;                 // 0=Off 1=PC 2=PC+ 3=Console (TreeAlpha_SetProfile)
	int   waterStyle;              // 0 = Xbox (default), 1 = GTA IV, 2 = GTA V
	int   waterQuality;            // 0-3 (Low/Med/High/Ultra) cost tiers
	
	// Weather cycle control
	int currentWeatherType;
	float weatherTransition;
	RwBool weatherCycleEnabled;
	int timecycleOverrideHour;
	RwBool timecycleOverrideEnabled;

	// Debug menu
	RwBool debugMenuOpen;

	// Faux Normal Buffer (stereo disparity)
	RwBool normalBufferEnable;
	float normalBufferOffset;
	float normalBufferScale;

	// 4-Pipe Chain
	RwBool pipeChainEnable;
	float pipeChainIntensity;
	int pipeChainReflEnable;

	// State-integrity lane (INI: [SkyGfx], all int 0/1, default 1 = ON,
	// clamped [0..1] at parse in main.cpp readIniFile):
	//   texAlphaClassify      — 1 = classify texture alpha into the PBR
	//                           alpha channel instead of relying on vertex
	//                           alpha alone (fixes cutout foliage/decals
	//                           whose texture alpha never reached the shader);
	//   stateBaselineEnable   — 1 = run pipeApplyWorldBaseline at the four
	//                           frame boundaries (RenderScene entry,
	//                           RenderEffects exit, CPostEffects::Render
	//                           exit, DrawFinalEffects) so every frame starts
	//                           from the canonical three-layer world state;
	//   defaultCbShaderHygiene— 1 = unbind stale VS/PS in myDefaultCallback
	//                           before a default-pipeline atomic draws
	//                           (RW value-compare caches otherwise skip the
	//                           bind and inherit the previous pass's shader);
	// NOTE: spriteStateShield lives at EOF (appended, see below).
	int texAlphaClassify;
	int stateBaselineEnable;
	int defaultCbShaderHygiene;
	//   pedOpaqueContract     — 1 = force the ped opaque state contract in
	//                           myDefaultCallback: blend/vertex-alpha OFF,
	//                           ZWRITE/ZENABLE ON, ZFUNC LESSEQUAL, alpha
	//                           test GREATEREQUAL@1 (NOT the leaked 140 from
	//                           envmap's outdoor world pass).
	int pedOpaqueContract;
	//   pedCullMode          — ped A/B lever for the view-angle report
	//                           (default -1): -1 = keep the game's own cull
	//                                for the ped span (pipeEnterCullMode
	//                                re-asserts the sanitized raw saved mode,
	//                                current behaviour), 1 = force CULLNONE
	//                                (two-sided), 2 = force CULLBACK,
	//                                3 = force CULLFRONT. Out of range = -1.
	int pedCullMode;
	//   pedAlphaTestEnable   — ped A/B lever (default 1): 1 = canonical alpha
	//                           test (current behaviour), 0 = force
	//                           ALPHATESTENABLE FALSE for the ped span and
	//                           skip the ALPHAFUNC/ALPHAREF writes.
	int pedAlphaTestEnable;

	// Absolute ambient floor (ClampAmbientToSky tail, pipelinecommon.cpp) —
	// INI: SkyGfx.ambientFloor, default 0.03, clamped [0..0.2]; 0 disables
	float ambientFloor;

	// Debug toggles — individual effect enable/disable for isolation testing
	// Set to 0 in INI to bypass specific effects (helps find black screen cause)
	RwBool colorFilterEnable;		// 0=bypass colour filter entirely
	RwBool radiosityEnable;		// 0=disable radiosity postfx
	RwBool grainEnable;			// 0=disable film grain
	int pipelineOverride;		// -1=use normal pipeline, 0-4=force specific pipeline
	int diagLevel;				// 0=full diagnostics (default), 1=throttle x5, 2=per-frame receipts silenced

	// Debug dump — when 1, saves BMP checkpoints of key surfaces on first 5 frames
	RwBool postfxDumpDebug;		// 0=off, 1=on (default 0)

	// Pass/frame profiler (src/render/profiler.cpp — PF_SCOPE/QPC + optional
	// D3D9 TIMESTAMP gpu timers). Instrumentation is compiled in and every
	// entry point early-outs on a single volatile read when disabled, so the
	// runtime cost of leaving it OFF is ~zero.
	// profilerEnable   : 1 = collect/write the summary log + feed the imgui
	//                    "Profiler" tab (default ON for the refactor testing round).
	// profilerGpuTimers: 1 = additionally bracket each scope with D3D9
	//                    TIMESTAMP/TIMESTAMPDISJOINT queries (default OFF:
	//                    not universally supported, costs driver round-trips).
	// The imgui rows write both keys live (main.cpp's row onEdit copies them
	// into g_pfEnable/g_pfGpuTimers); they are re-applied from the INI on every
	// readIniFile (startup, F11 reload, config switch).
	int profilerEnable;			// 0=off, 1=on (default 1)
	int profilerGpuTimers;		// 0=cpu timings only, 1=+d3d9 gpu timers (default 0)

	// Atmospheric: Height Fog (Crytek exponential)
	RwBool heightFogEnable;		// 0=disable height fog
	float heightFogDensity;		// fog density (default 0.002)
	float heightFogHeightFalloff;	// height falloff factor (default 0.8)
	float heightFogStartHeight;	// fog start height in world units (default 0.0)
	float heightFogR, heightFogG, heightFogB; // fog color (from timecycle)
	float heightFogTimecycleScale;	// timecycle fog influence (0-5, default 1.0)

	// Atmospheric: God Rays (screen-space radial blur)
	RwBool godRaysEnable;		// 0=disable god rays
	float godRaysExposure;		// brightness per sample (default 0.0034)
	float godRaysDecay;		// falloff per sample (default 1.0)
	float godRaysDensity;		// ray density (default 0.84)
	float godRaysWeight;		// ray weight (default 1.0)
	int godRaysNumSamples;		// number of samples (default 20)

	// Velocity Buffer (per-pixel motion vectors via depth reconstruction)
	RwBool velocityBufferEnable;		// 0=disable velocity buffer

	// Forward+ Tiled Lighting (O3DE Atom-inspired 16×16 screen tiles)
	RwBool forwardPlusEnable;		// 0=disable forward+ lighting
	RwBool forwardPlusDebug;		// 0=off 1=tile overlay 2=tiles+legend

	// INTZ Direct-Binding Depth Hook (DXVK compatibility)
	// When enabled, hooks SetDepthStencilSurface to bind an INTZ texture as
	// the scene depth-stencil, eliminating all StretchRect depth copies.
	RwBool depthHookEnable;			// 0=disable, 1=enable (default)

	// Death ragdoll — verlet-based ragdoll for dead peds (src/extras/ragdoll_death.cpp)
	RwBool ragdollEnable;			// 0=disable, 1=enable (default 1)

	// PBR ambient floor — minimum ambient luminance for ped/vehicle PBR paths.
	// Keeps peds/vehicles visible at night without a gray veil. Default 0.0
	// (= off); clamped [0..1]. readIniFile default, ADD_IF_MISSING and
	// skygfx.default.ini all use 0.0, so any non-zero value is opt-in.
	float pbrAmbientFloor;
	// Ped (skin-pipe) ambient floor — myDefaultCallback overrides pAmbient with
	// the timecycle ambient; at hour 12 the raw value can be ~(0.033,0,0) and
	// vanilla skin peds (no Forward+/IBL) render flat black up close. This is
	// the minimum PEAK channel of the final pAmbient colour, applied as a
	// hue-preserving scale (all channels multiplied by floorV/maxChannel) so a
	// warm dusk ambient stays warm instead of washing to gray. 0.0 = off.
	// INI: SkyGfx.pedAmbientFloor, default 0.20, clamped [0..1].
	// DEAD-BUT-PARSED (source-inventory D4): zero render consumers after the
	// W3 revert — the debug menu still references this field
	// (debugmenu_ui.cpp rFn row + reset table), so it stays; cleanup deferred.
	float pedAmbientFloor;
	// Global ambient multiplier applied to timecycle ambient at upload
	// (GetTimecycleAmbient, pipelinecommon.cpp). Vanilla SA applies NO such
	// factor; 1.0 = vanilla-equivalent. Values < 1 darken every pipe that
	// reads ambient — including the ambient-only Xbox building pipe (the
	// old hardcoded 0.85 blacked out dawn). 0.0-2.0, default 1.0.
	float ambientMult;
	// Tonemap black-level lift — preserves deep shadow detail (sRGB domain).
	// Small value only affects near-black (0.0-0.05, default 0.015)
	float tonemapBlackLift;

	// PBR vehicle layer toggles — bitmask for modular layer isolation/tuning.
	// bit0=base diffuse, bit1=env reflection, bit2=sun/light specular,
	// bit3=rim light, bit4=IBL blend, bit5=sky tint, bit6=clearcoat spec,
	// bit7=normal buffer. Default 255 = all layers on.
	int vehPBRLayers;

	// Non-prelit prelight fallback for the PBR/Modern vehicle VS
	// (main_vehiclePBR, VS c28.x). SA vehicle geometry ships WITHOUT
	// rpGEOMETRYPRELIT (1793/1793 meshes in skygfx_dbg.log), so the VS
	// prelight term (IN.Color * surfProps.w) is identically 0 and the only
	// shade-side light left is timecycle ambient * material ambient
	// (0.03*0.5 ≈ 0.02) -> black silhouette boxes. When the geometry has no
	// baked prelight the VS floors its lit colour at this value BEFORE the
	// material-colour multiply. Because the floor is a max(), prelit
	// geometry and sun-lit faces (already >= floor) are bit-identical.
	// 0.0-1.0, default 0.15; 0 = off (exact legacy behaviour).
	float vehPrelightFallback;

	// ---- Modern/Env vehicle REFLECTION MODEL terms -------------------------
	// One knob per term, all consumed by VehiclePBR_Modern.hlsl main() and
	// uploaded once per Env-cb invocation (PS c20.y/z/w + c44.z/w — see the
	// "reflection register contract" comment at the upload sites). Each
	// restores a term Modern was missing relative to the other car pipes
	// (Mobile / PS2 / Specular / Neo-xbox); 0 = legacy Modern behaviour.

	// Env-source VALIDITY FALLBACK (default on). When reflectionTex comes back
	// empty (sphere render skipped/failed) the old code did
	// layer2 = lerp(body, envTerm≈0, kr) — i.e. it REPLACED up to kr of the
	// paint with black and darkened the car. Now: env -> ibl -> sky source
	// chain, and kr is forced to 0 when none of them carry content, so an
	// empty environment can never darken the body.
	RwBool vehEnvFallback;

	// Energy-consistent strength model (default on). vehEnvIntensity used to
	// scale the env CONTENT while kr still removed kr of the body, so lowering
	// the strength slider DARKENED the paint by kr instead of weakening the
	// reflection. Mode 1 puts the intensity on the Fresnel COVERAGE
	// (kr *= intensity, content full) — at intensity 1.0 it is bit-identical
	// to the legacy path, at low intensity the body stays intact.
	RwBool vehEnvStrengthMode;

	// Apply CarReflectionMask at the env UV (default on). The PS2/Specular
	// shaders and CarPipe::RenderEnvTex (NEO) all modulate the reflection by
	// this mask (RenderEnvTex multiplies it into reflectionTex with
	// SRCBLEND=ZERO/DESTBLEND=SRCCOLOR over a 0..1 screen quad); the
	// RenderSphereReflections path Modern/Mobile use does NOT bake it, and
	// Modern bound the texture on s2 without ever sampling it.
	RwBool vehEnvMask;

	// Wet-road reflection boost (default on). Ported from main_building's
	// wet block: WetRoads raises env coverage, raises glossiness and
	// darkens the base slightly. Folded into PS c20.z on the CPU (0 when
	// this toggle is off).
	RwBool vehWetEnv;

	// Standalone Mobile-style sun glint strength, 0.0-1.0, default 0 (off).
	// Mobile adds pow(dot(reflectV, sunDir), 10) * strength * 2 * sunColor on
	// top of its env lerp — texture-INDEPENDENT, so it survives with no env
	// map at all. Modern's envGlint is multiplied by the env source, so this
	// is the always-available version. Also gated by LF(2) (sun/spec layer).
	float vehEnvGlint;

	// PBR vehicle env-reflection strength multiplier (0=off, 1=default).
	// Scales the env reflection term in VehiclePBR_Modern.hlsl main().
	float vehEnvIntensity;

	// Optional chrome promotion by MatFX env-map strength. Stock SA assigns an
	// env map to ALL car paint, so name-only classification misses untextured
	// chrome trim; when > 0, body materials whose env-map shininess is >= this
	// value are promoted to SURFACE_CAR_CHROME (metal reflection path).
	// 0 = disabled (default), try 1.5-3.0. Compares the RAW env-map shininess
	// (captured BEFORE the ×8×mult shaping) — raw semantics, per this comment.
	float vehChromeEnvThreshold;
	// Auto-chrome by part/frame name (default 1): RpAtomic frame-name keyword
	// walk (chrome/bumper/trim/...) OR material specular float bit 3 marks the
	// atomic's materials SURFACE_CAR_CHROME. Toggle off = never frame-chrome
	// even if bits were pre-set.
	RwBool vehAutoChrome;
	// Chrome material breakdown (Part B) — defaults match the Car Chrome BRDF
	// row (brdfLibrary.h). Folded into c22/c3/chromeParams instead of hardcoded.
	float chromeF0;         // chrome specular/F0 (c22.y), default 0.56, [0..1]
	float chromeGloss;      // chrome glossiness (c22.x), default 0.90, [0..1]
	float chromeMetallic;   // chrome metallicness (c22.w), default 1.0, [0..1]
	float chromeEnvBoost;   // chrome env-reflection mult folded into c3.x, default 1.0, [0..4]
	float chromeClearcoat;  // chrome clearcoat strength (PS c44.x), default 0.6, [0..1]

	// Unified tonemap (CryEngine-style): timecyc supplies COLOUR, the rendered
	// frame supplies BRIGHTNESS via a GPU luminance measure + temporal eye
	// adaptation. 1 = frame-adaptive exposure on, 0 = timecyc-only exposure.
	int tonemapAutoExposure;
	// Eye-adaptation speed (0.01-1.0, default 0.12) — how fast exposure follows
	// frame luminance changes. Lower = slower/more cinematic.
	float tonemapAdaptSpeed;
	// Key strength (0-1, default 1.0) — blend between timecyc exposure and the
	// frame-derived auto exposure. 0 = timecyc only, 1 = full auto.
	float tonemapKeyStrength;
	// Auto-exposure clamp bounds (min [-0.5..0.5] default 0.5,
	// max [0.2..4.0] default 1.0). Negative min is allowed for deliberate
	// darkening; max must stay positive. Min <= Max is enforced by pinning
	// min = max in clampTonemapExposureRanges (load/reload/save/menu) and in
	// the parser; debug menu sliders use the same ranges.
	float tonemapMinExposure;
	float tonemapMaxExposure;
	// User pivot curve applied at the OUTPUT of TonemapPass (identity —
	// bit-exact passthrough — when both intensities are 0.0, defaults):
	// region below mid driven by lows, region above mid by highs, midpoint
	// anchors at gain 1.0 for continuity. Negative lows darkens shadows /
	// pulls highlights down, positive lifts them (per-channel gain ramp).
	float tonemapCurveLows;		// [-0.35..0.35], default 0.0
	float tonemapCurveHighs;	// [-0.35..0.35], default 0.0
	float tonemapCurveMid;		// pivot [0..1], default 0.5

	// Universal dynamic-sky hemisphere ambient weight (0=off, 0.5 default, 1=full).
	// Samples the per-frame DynamicSky capture by surface normal and adds the
	// ZERO-CENTERED deviation from the flat timecycle ambient (already baked into
	// vertex color) so buildings/vehicles share one ambient PBR timeline with no
	// double-ambient or global brightening. Set 0 to reproduce the previous look.
	float pbrIblAmbientWeight;

	// ==== New post-effects (Fx* passes in postfx.cpp) — APPEND-ONLY block ====
	// Config surface for the new DOF/Bloom/TonemapLUT/SunShafts/FXAA/Grain/CA/
	// Vignette passes. Every key below is parsed in readIniFile ([SkyGfx]) and
	// shown in the debug menu "New Effects" group (debugmenu_ui.cpp).
	// NOTE: the contract key `vignetteIntensity` is NOT re-declared here — the
	// float of that exact name already exists in the legacy Vignette triple
	// above (float vignetteIntensity, vignetteRadius, vignetteContrast) and
	// postfx.cpp's FxVignette pass binds config->vignetteIntensity to THAT
	// member; a second member with the same name would not compile. The INI
	// key stays exactly `vignetteIntensity` per the postfx contract.

	// Depth of field (CoC -> bokeh -> tent -> combine)
	int   dofEnable;		// 0=off (default), 1=on
	float dofFocusDistance;	// metres to the focal plane (default 20.0, >= 0)
	float dofFocusRange;	// depth window kept sharp (default 15.0, >= 0)
	float dofBokehRadius;	// bokeh disc scale (default 4.0, >= 0)
	float dofMaxBlur;		// max blur/CoC mix 0..1 (default 0.5)

	// Tonemap LUT operator (TonemapLUT.hlsl):
	// 0 = filmic (default), 1 = ACES 1.3, 2 = ACES 2.0, 3 = AgX, 4 = Frostbite, 5 = GT7
	int   tonemapOperator;

	// Bloom (bright-pass -> mip down/up chain)
	int   bloomEnable;		// 0=off (default), 1=on
	int   bloomQuality;		// 0-3 = 2..6 mips (default 2)
	float bloomThreshold;	// bright-pass threshold (default 1.0, >= 0)
	float bloomIntensity;	// additive strength (default 0.8, >= 0)

	// Sun shafts (screen-space radial scatter from the sun position)
	int   sunShaftsEnable;	// 0=off (default), 1=on
	float sunShaftsDensity;	// ray density 0..1 (default 0.84 — postfx self-fallback)
	float sunShaftsDecay;	// per-sample falloff 0..1 (default 1.0 — postfx self-fallback)

	// FXAA3 (late AA slot; SMAA wins when both are on) + lens finishing
	int   fxaaEnable;		// 0=off (default), 1=on
	float grainIntensity;	// film grain strength 0..1 (0 = off, default 0)
	float caIntensity;	// chromatic aberration strength 0..1 (0 = off, default 0)

	//   shadowContractEnable  — 1 = run the three-layer shadow render-state
	//                           contract around the CShadows / realtime-shadow
	//                           passes; 0 (DEFAULT) = vanilla shadow passes.
	//                           The contract is an ours-only layer added after
	//                           the Sept-3 look we are restoring, and it wraps
	//                           an OFFSCREEN shadow-camera render
	//                           (CShadowCamera::InvertRaster: SRC=INVDESTCOLOR,
	//                           DST=ZERO, ZTEST off) in an on-screen
	//                           normal-alpha contract — prime suspect for the
	//                           baked-looking floor patches.
	// APPENDED (not inserted) to preserve the Config ABI: the project rule is
	// append-only, VERSION unchanged (docs/plans/2026-10-24-mode-preset-architecture.md).
	int shadowContractEnable;

	// State-integrity lane (cont.) — moved here from the block above.
	//   spriteStateShield     — 1 = coronaDrawRawShield also normalises
	//                           ALPHAFUNC/ALPHAREF/SEPARATEALPHABLENDENABLE/
	//                           COLORWRITEENABLE per draw (0 = skip those
	//                           four writes; the blend/ZWRITE force is not
	//                           affected). Clamped [0..1] at parse.
	// APPENDED (not inserted) to preserve the Config ABI: the project rule is
	// append-only, VERSION unchanged.
	int spriteStateShield;

	// ---- m0970 bisect lane: render-state systems that were COMPILED ON
	// unconditionally in main.cpp (no gate existed), each now exposed as an
	// INI toggle so a renderer-wide corruption can be bisected one system at
	// a time. Every default is 1 = exactly the previous always-on behaviour,
	// so a stock INI renders identically until a key is flipped to 0.
	// APPENDED (not inserted) to preserve the Config ABI: the project rule is
	// append-only, VERSION unchanged.
	int coronaBlendShield;	// 1 = coronaDrawRawShield forces ALPHABLENDENABLE/
							// SRCBLEND/DESTBLEND/ZWRITE/BLENDOP (the black-quad
							// repair). Independent of spriteStateShield, which
							// only gates the four-state normalisation.
	int spriteFlushShield;	// 1 = force the additive block at EVERY non-empty
							// sprite flush + the window-close canonical
							// re-sync (SpriteFlushIndexedDraw_hook).
	int effectsAlphaBlock;	// 1 = effectsForceAlphaBlock canonical alpha sweeps
							// at RenderEffects/PostFX/corona-reflection/Clouds
							// boundaries.
	int camRasterRepair;	// 1 = scene-entry repair of a non-screen camera
							// raster (m0170/m0171 blank-world fix).
	int moonMaskAlpha;		// 1 = moon-mask bracket forces SEPARATEALPHA /
							// ALPHAFUNC around the 0x70D000 draw.
	int skidmarkAlphaFix;	// 1 = CSkidmarks::Render alpha-test force through
							// pipeForceAlphaBlock (cutout marks).
	int pedTexResync;		// 1 = ped stage-0 texture-raster resync in
							// myDefaultCallback (cache<->device split repair).
};
static_assert(offsetof(Config, version) == 0, "pinned: asm reads in main.cpp");
static_assert(offsetof(Config, fixGrassPlacement) == 8, "pinned: fixSeed asm in main.cpp");
static_assert(offsetof(Config, doglare) == 12, "pinned: doglare asm in main.cpp");
extern int numConfigs;
extern int currentConfig;
extern Config *config, configs[10];
void readIni(int n);
void findInis(void);
void readInis(void);
void resetValues(void);
void refreshIni(void);
void reloadAllInis(void);
void saveConfig(void);
bool saveConfigTo(const char *path);	// Save as New Config: writes live config to an explicit path
bool loadConfigFile(const char *path);	// Config selector: reads a config file into the live config (false = file missing/unreadable)
void setConfig(void);
void ApplyPreset(Config *c, int preset);

extern bool iCanHasbuildingPipe;
extern bool iCanHasvehiclePipe;
extern bool iCanHasSunGlare;
extern bool iCanHasNeoDrops;
extern int explicitBuildingPipe;
extern bool gHasExternalNormalMapPlugin;

/* Env map */
extern RwCamera *reflectionCam;
extern RwRaster *envFB, *envZB;
extern RwTexture *reflectionTex;
void MakeEnvmapRasters(void);
void MakeEnvmapCam(void);
void ShutdownEnvMap(void);

extern struct IDirect3DTexture9 *g_normalBufferTex;

enum {
	COLORFILTER_NONE   = 0,
	COLORFILTER_PS2    = 1,
	COLORFILTER_PC     = 2,
	COLORFILTER_MOBILE = 3,
	COLORFILTER_III    = 4,
	COLORFILTER_VC     = 5,
	COLORFILTER_VCS    = 6,
	COLORFILTER_GTAIV  = 7,
	COLORFILTER_MODERN = 8,
};

struct CPostEffects
{
	// effects:
	//          III ColourFilter/Blur
	//          VC  ColourFilter/Blur
	//          SA  ColourFilter/Blur
	//          SA  Radiosity
	//          VCS Radiosity
	//          VCS Blur
	static void Radiosity_VCS_init(void);
	static void Radiosity_VCS(int limit, int intensity);
	static void Blur_VCS(void);

	static void Radiosity(int intensityLimit, int filterPasses, int renderPasses, int intensity);
	static void Radiosity_shader(int intensityLimit, int filterPasses, int renderPasses, int intensity);
	static void DarknessFilter(uint8 alpha);
	static void DarknessFilter_fix(uint8 alpha);
	static void InfraredVision(RwRGBA c1, RwRGBA c2);
	static void InfraredVision_PS2(RwRGBA c1, RwRGBA c2);
	static void NightVision(RwRGBA color);
	static void NightVision_PS2(RwRGBA color);
	static void Grain(int strength, bool generate);
	static void Grain_PS2(int strength, bool generate);
	static void ColourFilter(RwRGBA rgb1, RwRGBA rgb2);
	static void ColourFilter_Mobile(RwRGBA rgb1, RwRGBA rgb2);
	static void ColourFilter_Modern(RwRGBA rgb1, RwRGBA rgb2);
	static void ColourFilter_PS2(RwRGBA rgb1, RwRGBA rgb2);
	static void ColourFilter_Generic(RwRGBA rgb1, RwRGBA rgb2, void *ps);
	static void ColourFilter_switch(RwRGBA rgb1, RwRGBA rgb2);
	static void SetFilterMainColour_PS2(RwRaster *raster, RwRGBA color);
	static void (*Initialise_orig)(void);
	static void Initialise(void);
	static bool Initialise_skygfx(void*);
	static void ImmediateModeRenderStatesStore(void);
	static void ImmediateModeRenderStatesSet(void);
	static void ImmediateModeRenderStatesReStore(void);
	static void SetFilterMainColour(RwRaster *raster, RwRGBA color);
	static void DrawQuad(float x1, float y1, float x2, float y2, uchar r, uchar g, uchar b, uchar alpha, RwRaster *ras);
	static void DrawQuadSetUVs(float utl, float vtl, float utr, float vtr, float ubr, float vbr, float ubl, float vbl);
	static void DrawQuadSetDefaultUVs(void);
	static void SpeedFX(float);
	static void DrawFinalEffects(void);
	static void DrawSSAO(void);
	static void DrawSMAA(void);
	static void DrawMotionBlur(void);

	static Imf &ms_imf;

	static RwRaster *&pRasterFrontBuffer;
	static float &m_fInfraredVisionFilterRadius;;
	static RwRaster *&m_pGrainRaster;
	static int &m_InfraredVisionGrainStrength;
	static int &m_NightVisionGrainStrength;
	static float &m_fNightVisionSwitchOnFXCount;
	static bool &m_bInfraredVision;

	static bool &m_bDisableAllPostEffect;

	static bool &m_bColorEnable;
	static int &m_colourLeftUOffset;
	static int &m_colourRightUOffset;
	static int &m_colourTopVOffset;
	static int &m_colourBottomVOffset;
	static float &m_colour1Multiplier;
	static float &m_colour2Multiplier;
	static float &SCREEN_EXTRA_MULT_CHANGE_RATE;
	static float &SCREEN_EXTRA_MULT_BASE_CAP;
	static float &SCREEN_EXTRA_MULT_BASE_MULT;

	static bool &m_bRadiosity;
	static bool &m_bRadiosityDebug;
	static int &m_RadiosityFilterPasses;
	static int &m_RadiosityRenderPasses;
	static int &m_RadiosityIntensityLimit;
	static int &m_RadiosityIntensity;
	static bool &m_bRadiosityBypassTimeCycleIntensityLimit;
	static int &m_RadiosityFilterUCorrection;
	static int &m_RadiosityFilterVCorrection;

	static bool &m_bDarknessFilter;
	static int &m_DarknessFilterAlpha;
	static int &m_DarknessFilterAlphaDefault;
	static int &m_DarknessFilterRadiosityIntensityLimit;

	static bool &m_bCCTV;
	static bool &m_bFog;
	static bool &m_bNightVision;
	static bool &m_bHeatHazeFX;
	static bool &m_bHeatHazeMaskModeTest;
	static bool &m_bGrainEnable;
	static bool &m_waterEnable;

	static bool &m_bSpeedFX;
	static bool &m_bSpeedFXTestMode;
	static uint8 &m_SpeedFXAlpha;

	/* My own */
	static bool m_bBlurColourFilter;
	// YCbCr color filter
	static bool m_bYCbCrFilter;
	static float m_lumaScale, m_lumaOffset;
	static float m_cbScale, m_cbOffset;
	static float m_crScale, m_crOffset;

	static void UpdateFrontBuffer(void);
};

// D3DPOOL_DEFAULT resource cleanup (call on DLL_PROCESS_DETACH and device lost)
void ReleaseDefaultPoolResources(void);
// Water pipe D3DPOOL_DEFAULT RT release (scene colour / R32F depth /
// reflection) — pre-Reset lifecycle, defined in waterPipe.cpp. The RTs are
// recreated lazily by the tier>=2 flush (EnsureSceneTexture /
// EnsureDepthTexture / EnsureReflTexture); g_detailTex (MANAGED) survives.
// Startup device-Reset crash fix (0x7F9A3C): a live DEFAULT-pool water RT
// fails the Reset with D3DERR_INVALIDCALL and RW's restore path then derefs
// a NULL back buffer.
void waterPipe_OnDeviceReset(void);
void ReleaseSSAOOverhaulResources(void);
// SMAA resource cleanup (called internally by ReleaseDefaultPoolResources)
void ReleaseSMAAStaticResources(void);

// Forward+ tiled lighting (O3DE Atom-inspired)
void ForwardPlus_CullAndUpload(void);
void ForwardPlus_ReleaseResources(void);
void ForwardPlus_SetConstants(void);

char *getpath(char *path);


// Material type IDs for SSS (matches SubsurfaceScattering.hlsl defines)
enum MaterialType
{
	MATTYPE_NONE = 0,
	MATTYPE_SKIN = 1,
	MATTYPE_CLOTH = 2,
	MATTYPE_VEGETATION = 3,
};

// Tex DB
struct TexInfo
{
	char *name;	// not strictly needed
	char *affiliate;
	TexInfo *affiliateTex;
	uint8 detailnum;
	RwTexture *detail;
	uint8 detailtile;
	uint8 alphamode;
	bool hassibling;
	bool noiseDetail;
	bool dualPass;
	uint8 zwriteThreshold;
	uint8 materialType;	// MaterialType enum: 0=none, 1=skin, 2=cloth, 3=vegetation
};

extern bool gRenderingSpheremap;
extern CVector reflectionCamPos;

extern RxPipeline *&CCustomBuildingDNPipeline__ObjPipeline;
extern RxPipeline *&CCustomBuildingPipeline__ObjPipeline;
void TagRenderCB(RpAtomic *atomic, RxD3D9ResEntryHeader *resEntryHeader, RxD3D9InstanceData *instanceData);
RxPipeline *CCustomBuildingPipeline__CreateCustomObjPipe_PS2(void);
RxPipeline *CCustomBuildingDNPipeline__CreateCustomObjPipe_PS2(void);
// ============================================================
// Vehicle shader bridge (veh_shaders.cpp → vehicles.cpp)
// ============================================================
extern void VehShaders_Init(const char *gameDir);
extern int  VehShaders_SelectPaintType(int modelID, unsigned int hash);
extern void VehShaders_GetPaintPBR(int paintType, float *specular, float *glossiness,
                                   float *specularTintR, float *specularTintG, float *specularTintB,
                                   float *noiseScale, float *edgeBlend);
extern void VehShaders_GetHeadlightTint(int modelID, float *r, float *g, float *b);
extern void VehShaders_GetTaillightTint(int modelID, float *r, float *g, float *b);
extern void VehShaders_GetGlassTint(int modelID, float *r, float *g, float *b, float *strength);
extern void VehShaders_GetTireProps(int modelID, float *specular, float *glossiness,
                                    float *tintR, float *tintG, float *tintB);
extern bool VehShaders_IsTireTexture(const char *texName);
extern bool VehShaders_IsHeadlightTexture(const char *texName);
extern bool VehShaders_IsTaillightTexture(const char *texName);
extern bool VehShaders_IsGlassTexture(const char *texName, bool hasAlpha, unsigned char alpha);
extern int  VehShaders_GetModelIndex(void *atomic);
	extern int  VehShaders_GetSurfaceType(const char *texName);
	// Part C: chrome by atomic frame-name keyword walk (+ material specular bit 3
	// marker + 64-slot direct-mapped cache). Checks config->vehAutoChrome first.
	extern bool VehShaders_FrameNameIsChrome(RpAtomic *atomic);

	void hookVehiclePipe(void);
void hookBuildingPipe(void);
void D3D9Render(RxD3D9ResEntryHeader *resEntryHeader, RxD3D9InstanceData *instanceData);
void D3D9RenderDual(int dual, RxD3D9ResEntryHeader *resEntryHeader, RxD3D9InstanceData *instancedData, TexInfo *textInfo = nullptr);

void CMessages__AddMessageJumpQWithNumber(char* text, unsigned int time, unsigned short flag, int n1, int n2, int n3, int n4, int n5, int n6, bool bPreviousBrief);

void fixSAMP(void);
extern HMODULE UG_mod;

//////// Pipelines
///// Shaders
// misc
extern void *simplePS;
// postfx
extern void *vcTrailsPS;
// m0336 GPU particle lane: WakeParticlePS.cso handle (particles_gpu.cpp),
// created in CreateShaders alongside the other PostFX handles.
extern void *wakeParticlePS;
extern void *modernColorFilterPS;
extern void *gradingPS, *contrastPS, *tonemapPassPS;
extern void *luminanceReducePS, *luminanceAdaptPS;
extern void *blurPS, *radiosityPS;
extern void *SMAA;
extern void *SMAA_Edge;
extern void *SMAA_EdgeMotionDepth;
extern void *SMAA_BlendWeight;
extern void *SMAA_BlendNeighbor;
extern void *SMAA_Temporal;
extern void *SSAO;
extern void *SSAO_VertexDepth;
extern void *SSAO_Temporal;
extern void *SSAO_BilateralBlur;
extern void *SSAO_Upsample;
extern void *MotionBlur_Burnout;
extern void *ColorFilter_CrossMix;
extern void *Water_Parallax;
extern void *Water_IV;
extern void *Water_V;
extern void *Water_VS;
extern void *VehiclePBR_Modern;
extern void *Glass_Vehicle;
extern void *Rubber_Vehicle;
extern void *CarPaint_Reflections;
extern void *PBR_Lighting;
extern void *ClampShader;
extern void *DynamicSky;
extern void *cloudsPS;
extern void *HeightFog;
extern void *GodRays;
extern void *VelocityReconstruct;

// INTZ Direct-Binding Depth Hook (DXVK compatibility)
extern struct IDirect3DTexture9 *g_intzTex;
extern struct IDirect3DSurface9 *g_intzSurf;
void DepthHook_Install(struct IDirect3DDevice9 *device);
void DepthHook_Suspend(void);
void DepthHook_Restore(void);
void DepthHook_ReleaseResources(void);

// Vehicle legacy
extern void *vehiclePipeVS;
extern void *vehiclePBRVS;
extern void *ps2CarFxVS;
extern void *specCarFxVS, *specCarFxPS;
extern void *xboxCarVS;

// Wheel extender
extern void *leedsCarFxVS;
extern void *mobileVehiclePipeVS, *mobileVehiclePipePS;

// GTAIV vehicle (ivMode path)
extern void *gtaivVehicleVS, *gtaivVehiclePS;

// Building legacy
extern void *ps2BuildingVS, *ps2BuildingFxVS, *ps2BuildingWindVS;
extern void *xboxBuildingVS, *xboxBuildingPS, *xboxBuildingNoiseDetailPS, *xboxBuildingWindVS;
extern void *buildingPBRVS, *buildingPBRPS;
extern void *sphereBuildingVS;
extern void *simpleDetailPS, *simpleDetailNoiseDetailPS, *simpleFogPS;

void DrawUnifiedDebugMenu(IDirect3DDevice9 *device);
// REMOVED (rw-convention cleanup): UploadUnifiedConstants (upload path
// deleted; the unified* config fields went with it) and UpdateVehicleRing
// (never defined, never called) — stale declarations, grep-verified.
void RenderIBLBuffer(void);

void CreateShaders(void);
void RwToD3DMatrix(void *d3d, RwMatrix *rw);
void MakeProjectionMatrix(void *d3d, RwCamera *cam, float nbias = 0.0f, float fbias = 0.0f);
void pipeGetComposedTransformMatrix(RpAtomic *atomic, float *out);
void pipeGetWorldMatrix(float *out);
void pipeGetCameraTransformMatrix(float *out);
void pipeGetLeedsEnvMapMatrix(RpAtomic *atomic, float *out);
void pipeUploadMatCol(int flags, RpMaterial *m, int loc);
void pipeUploadZero(int loc);
void pipeUploadZeroPS(int loc);
void pipeUploadLightColor(RpLight *light, int loc);
void pipeUploadLightColorForce(RpLight *light, int loc);
void pipeUploadLightColorPS(RpLight *light, int loc);
void pipeUploadLightColorForcePS(RpLight *light, int loc);
void pipeUploadLightDirection(RpLight *light, int loc);
void pipeUploadLightDirectionForce(RpLight *light, int loc);
void pipeUploadLightDirectionPS(RpLight *light, int loc);
void pipeUploadLightDirectionForcePS(RpLight *light, int loc);
void pipeUploadLightDirectionLocal(RpLight *light, RwMatrix *m, int loc);
void pipeUploadLightDirectionInv(RpLight *light, int loc);
inline void pipeSetTexture(RwTexture *t, int n) { RwD3D9SetTexture(t ? t : gpWhiteTexture, n); };


// dword_C02C20 / CCustomCarEnvMapPipeline__ObjPipeline removed
// (rw-convention cleanup): defined in gta.cpp, grep-verified zero readers
// outside their definitions.
extern int &dword_C9BC60;
extern RxPipeline *&skinPipe;

// reversed
void D3D9RenderNotLit(RxD3D9ResEntryHeader *resEntryHeader, RxD3D9InstanceData *instanceData);
void D3D9RenderPreLit(RxD3D9ResEntryHeader *resEntryHeader, RxD3D9InstanceData *instanceData, RwUInt8 flags, RwTexture *texture);
RwBool DNInstance_default(void *object, RxD3D9ResEntryHeader *resEntryHeader, RwBool reinstance);
void CCustomCarEnvMapPipeline__CustomPipeRenderCB(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags);

void CCustomCarEnvMapPipeline__Env1Xform_PC(RpAtomic *atomic,
	CustomEnvMapPipeMaterialData *envData, float *envXform);
void CCustomCarEnvMapPipeline__Env2Xform_PC(RpAtomic *atomic,
	CustomEnvMapPipeMaterialData *envData, CustomEnvMapPipeAtomicData *atmEnvData, float *envXform);

// from the exe
RwBool DNInstance(void *object, RxD3D9ResEntryHeader *resEntryHeader, RwBool reinstance);
RwBool D3D9SetRenderMaterialProperties(RwSurfaceProperties*, RwRGBA *color, RwUInt32 flags, RwReal specularLighting, RwReal specularPower);
RwBool D3D9RestoreSurfaceProperties(void);
RwUInt16 CVisibilityPlugins__GetAtomicId(RpAtomic *atomic);
RpAtomic *CCustomCarEnvMapPipeline__CustomPipeAtomicSetup(RpAtomic *atomic);
char *GetFrameNodeName(RwFrame *frame);
int gtaGetPipelineID(RpAtomic* atomic);
RpAtomic *AtomicDefaultRenderCallBack(RpAtomic*);
void CCustomCarEnvMapPipeline__CustomPipeRenderCB_exe(RwResEntry *repEntry, void *object, RwUInt8 type, RwUInt32 flags);
void GTAfree(void *data);

// ============================================================
// Unified PBR constant upload — single source of truth for c22/c23 layout.
// Both vehicle and building pipes MUST call this to avoid param-order bugs.
//
// c22 = {glossiness, specular, pipeParam3, pipeParam4}
// c23 = {pipeParam5, pipeParam6, pipeParam7, 0}
//
// Vehicle: c22 = {glossiness, specular, specTint, metallicness}
//          c23 = {wheelFlag, noiseScale, edgeBlend, 0}
// Building: c22 = {glossiness, specular, clearcoat, subsurface}
//           c23 = {tintR, tintG, tintB, 0}
// ============================================================
void pipeUploadPBR(float glossiness, float specular, float c22_3, float c22_4,
                   float c23_1, float c23_2, float c23_3);

// ============================================================
// Canonical park-and-set primitives — pipelinecommon.cpp.
//
// ONE park order for the whole tree: PARK-THEN-TARGET PER LAYER.
//   L1: park -> target (pipeRwParkSet)
//   L2: park -> target (pipeL2ParkSet)
//   L3: target only    (pipeL3Set, unconditional raw pin)
// A state's L1 pair completes before its L2 pair; the raw pin is last.
// Park values must always differ from the target on every layer they
// are written through (layers 1/2 skip cache-hits; the flush is
// applied[]-gated). No draw may occur between the writes — callers own
// the flush and the layer interleaving. See the pipelinecommon.cpp
// block comment for the full doctrine. Exported so postfx_utils.cpp
// RestoreOneRawGeomState uses the same primitive instead of a private
// copy. Remaining private copy: mirrorPass.cpp:116 MirrorRestoreState3L
// (same order; other lane).
// ============================================================
void pipeRwParkSet(RwRenderState state, RwUInt32 park, RwUInt32 target);
void pipeL2ParkSet(D3DRENDERSTATETYPE state, RwUInt32 park, RwUInt32 target);
void pipeL3Set(D3DRENDERSTATETYPE state, DWORD value);

// ============================================================
// Cull-mode guard for pipe render callbacks — pipelinecommon.cpp.
// Saves rwRENDERSTATECULLMODE RAW and forces the sanitized saved
// value (not BACK unconditionally); on exit it restores that saved
// value verbatim. All three rw cull modes are valid and survive
// untouched: NONE for the game's mirror pass, FRONT for stencil
// shadow volumes (forcing BACK there draws the mirrored
// negative-determinant CJ inside-out and destroys shadow volumes).
// Only invalid/out-of-domain values — a failed Get or a foreign
// raw write — are sanitized to BACK, so garbage can never leak
// into pipe draws. Prevents a stale cull value from hiding
// one-sided geometry. Safe to nest.
// Every set goes through pipeForceCullMode(), which syncs ALL THREE
// cull layers (rw cache / D3D9 driver cache / raw device): a plain
// RwRenderStateSet is a no-op when the rw cache already holds the
// value, so a raw device drift (postfx Save/RestoreRawGeomStates,
// device reset) could otherwise never be repaired — see the
// pipelinecommon.cpp block comment for the disassembly-backed
// details. pipeForceCullMode is also the re-assert to call after
// any fullscreen Im2D pass (buildingPipe:731, vehicle Env cb).
// ============================================================
RwUInt32 pipeEnterCullMode(void);
void pipeExitCullMode(RwUInt32 saved);
void pipeForceCullMode(RwUInt32 rwCull);

// Scope guard for a span that legitimately changes CULLMODE (e.g. the game's
// mirror render, which runs with CULLNONE). Begin() records the mode the world
// must be back in when the span ends; End() re-asserts it through all three
// state layers. Nesting is depth-limited (8) with overflow counting.
void pipeCullScopeBegin(RwUInt32 rwCull);
void pipeCullScopeEnd(void);

// ============================================================
// Alpha/blend-mode guard for pipe render callbacks — sibling of
// pipeEnterCullMode (pipelinecommon.cpp). Saves the rw-cached
// vertex-alpha / src+dst blend / alpha-test func+ref states (all
// pre-seeded so a failed Get is a known-good value), then forces a
// deterministic entry through BOTH layers (rw-set + raw device):
// blend factors back to the world-canonical SRCALPHA/INVSRCALPHA,
// D3DBLENDOP back to ADD (no rw state exists for it), vertex alpha
// OFF as the per-mesh baseline, and the alpha-test group re-aligned
// to the rw-cached func with ref==0 repaired to 1. Exit restores the
// caller's states through both layers — and since legacy-parity item 6,
// restores the blend enable / alpha-test enable / blend op VERBATIM from
// the enter-time captures below instead of re-asserting derived values
// (enable<-vtxAlpha, test<-TRUE, op<-ADD); that derived triple is only
// the fallback when the captures are absent (capMask == 0). Needed because
// RwRenderStateSet/Get are no-ops when the rw cache already holds
// the value — foreign raw RwD3D9SetRenderState(D3DRS_*) writes
// (postfx, SSS, radiosity, moon helpers) bypass that cache and
// leave the device disagreeing with it.
// ============================================================
// capMask bits (PipeAlphaState) — set by pipeEnterAlphaMode for every
// capture group it takes. Zero means "no captures taken" (a caller-side
// pre-seeded struct that never went through pipeEnterAlphaMode) and makes
// pipeExitAlphaMode fall back to the pre-parity derived restore.
enum {
	PAACAP_BLENDENABLE = 0x1,	// alphaBlendEnable(+Dev)
	PAACAP_TESTENABLE  = 0x2,	// alphaTestEnable(+Dev)
	PAACAP_BLENDOP     = 0x4,	// blendOp(+Dev)
};
struct PipeAlphaState {
	RwUInt32 vtxAlpha;   // rwRENDERSTATEVERTEXALPHAENABLE
	RwUInt32 srcBlend;   // rwRENDERSTATESRCBLEND
	RwUInt32 dstBlend;   // rwRENDERSTATEDESTBLEND
	RwUInt32 alphaFunc;  // rwRENDERSTATEALPHATESTFUNCTION
	RwUInt32 alphaRef;   // rwRENDERSTATEALPHATESTFUNCTIONREF
	// Raw-only states — no rw render state exists for these, so layers (1)/(2)
	// can never carry them and a foreign raw write is invisible to every
	// RwRenderStateGet. They are therefore snapshotted from the DEVICE (layer 3,
	// the only layer that ever sees them) at enter and restored raw at exit:
	//   sepAlphaBlend — D3DRS_SEPARATEALPHABLENDENABLE. setMoonAlphaBlendStates
	//     (main.cpp) pushes it ON with DESTBLENDALPHA=ZERO and only restores the
	//     enable bit; if that leaks, every later draw's DESTINATION ALPHA is
	//     recomputed separately, which the postfx chain reads back.
	//   colorWrite    — D3DRS_COLORWRITEENABLE. A leaked channel mask silently
	//     drops RGB writes so the frame keeps its cleared (black) content.
	RwUInt32 sepAlphaBlend;
	RwUInt32 colorWrite;
	// Faithful-restore captures (legacy-parity item 6) — the three remaining
	// alpha-block states, which have NO rw (L1) twin either. The exit used to
	// DERIVE them (blend enable from vtxAlpha, alpha-test hard TRUE, BLENDOP
	// hard ADD), which re-asserted derived values over the caller's truth: a
	// shadow-polluted device state was reproduced after exit, and consumers
	// that had the alpha test off were force-enabled by every restore. Each
	// is therefore captured on BOTH layers that can hold it, at enter, BEFORE
	// the canonical force, and restored verbatim at exit. The two captures
	// may legitimately differ (a foreign raw write leaves layer 3 away from
	// layer 2) — each layer goes back to its OWN value:
	//   alphaBlendEnable(Dev) — D3DRS_ALPHABLENDENABLE. base = L2
	//     RwD3D9GetRenderState (driver cache), Dev = L3 raw
	//     IDirect3DDevice9::GetRenderState.
	//   alphaTestEnable(Dev)  — D3DRS_ALPHATESTENABLE, same split.
	//   blendOp(Dev)          — D3DRS_BLENDOP, same split.
	// capMask (PAACAP_* above) marks which capture groups are present.
	// Fields are appended at the END of the struct ON PURPOSE (internal
	// struct, no INI/config layout impact): the one aggregate initialiser
	// (main.cpp ped pre-seed) stays compile-safe and zero-fills these —
	// exactly the "captures absent" state the exit fallback detects.
	RwUInt32 alphaBlendEnable;
	RwUInt32 alphaBlendEnableDev;
	RwUInt32 alphaTestEnable;
	RwUInt32 alphaTestEnableDev;
	RwUInt32 blendOp;
	RwUInt32 blendOpDev;
	RwUInt32 capMask;
};
PipeAlphaState pipeEnterAlphaMode(void);
void pipeExitAlphaMode(PipeAlphaState saved);

// ============================================================
// Canonical rw->D3D blend mapping — pipelinecommon.cpp, SINGLE SOURCE.
//
// RwBlendFunction is D3DBLEND 1:1 for rwBLENDZERO(1)..rwBLENDSRCALPHASAT(11);
// rwBLENDNABLEND(0) and anything above SRCALPHASAT are not valid D3D
// values. Do NOT re-derive either rule at a call site:
//   pipeBlendHasRwTwin — TRUE when the value has an rw (L1) twin, i.e.
//     the L1 leg of a park-and-set is legal. FALSE for NA/garbage and
//     for raw-only extras (D3DBLEND_BLENDFACTOR=14) — those are
//     restored L2/L3-only, verbatim.
//   pipeBlendToD3D — identity inside the domain, caller `fallback`
//     outside it (pipe entry uses SRCALPHA/INVSRCALPHA).
// Remaining duplicate sites (other lanes, not converted here):
//   mirrorPass.cpp:316-331 inline domain test (its MirrorRestoreState3L
//     helper is also a private park-and-set copy).
// The buildingPipe.cpp / vehiclePipe.cpp / main.cpp private copies
// (buildingPipe_blendToD3D, vehiclePipe_blendToD3D, shadowBlendToD3D) were
// deleted 2026-10-27; those TUs now call this pair directly.
// NOT this mapping: neoCarpipe.cpp:198 carBlendClampDev is a D3D-domain
// 1..17 clamp (device layers may hold BLENDFACTOR..INVSRCCOLOR2) — keep
// it separate.
// ============================================================
RwBool pipeBlendHasRwTwin(RwUInt32 rwBlend);
RwUInt32 pipeBlendToD3D(RwUInt32 rwBlend, RwUInt32 fallback);

// ============================================================
// Frame-boundary world baseline — implemented in pipelinecommon.cpp.
// Re-pushes the canonical world signature (blend pair, depth block,
// vertex-alpha OFF, additive BLENDOP, colour write mask) through the rw
// cache + RW driver cache (park-and-set) + raw device. Runs ONCE per frame
// boundary, never per draw; `tag` labels the throttled receipt log.
// Declared here for main.cpp's four boundary call sites (SceneStart /
// EffectsExit) and depthhook.cpp's DeviceReset path.
// ============================================================
void pipeApplyWorldBaseline(const char *tag);

// ============================================================
// Scene-start repair-to-cache — implemented in pipelinecommon.cpp.
// Detection promoted to REPAIR (see-through fix lane, 2026-11-02):
// at the scene_start edge, if the raw device (L3 GetRenderState)
// disagrees with the rw cache (L1) on the blend/Z family
// {ALPHABLENDENABLE, SRCBLEND, DESTBLEND, ZWRITEENABLE, ZTESTENABLE,
// ZFUNC}, all layers are forced back to the rw-cache/world-baseline
// values BEFORE the frame's first draw (L2 park-and-set + raw L3 pin;
// L1 is the repair source and is not touched). Mirror image of
// PostfxStateSyncCheck (postfx.cpp): that heals L2 to device truth in
// the postfx phase; this repairs L2+L3 to CACHE truth scene-side.
// Cost: 6 device Gets/frame on the healthy path (silent); writes only
// on a real divergence. Called from main.cpp RenderScene_before right
// after RSMon_Sample("scene_start") so the rs_mon receipt still
// captures the divergence before the repair.
// ============================================================
void pipeRepairSceneStartSync(const char *tag);

// ============================================================
// Universal noise bind + parameter upload — implemented in
// pipelinecommon.cpp.
//
// Binds the packed SkyNoise tile (R=Perlin G=Worley B=white A=blue) to
// `samplerStage` (s9 by project reservation) with WRAP/LINEAR/no-mips/
// MAXANISOTROPY=1, then uploads the reserved knob block:
//   c49 = { noiseScale,   worleyScale, coverage,     erosion }
//   c50 = { whiteScale,   blueScale,   timeSeconds,  frameIndex }
//   c51 = { cloudScaleA,  cloudScaleB, cloudScaleC,  cloudDensity }
//   c52 = { jitterAmount, ditherAmount, detailStrength, 0 }
// Each vector is written to the raw device FIRST and then to the RW driver
// cache (RwD3D9SetPixelShaderConstant), so the RW cache can never be left
// stale by the raw write and cache-skip a later identical set.
//
// Lazily SkyNoise::Init(1337u) once; seed is intentionally fixed here (the
// config lane owns any user-facing seed). No-op with d3d9device == NULL.
// ============================================================
void pipeBindNoise(int samplerStage);

// Corona / street-lamp-cone sprite shield window (main.cpp). SilentPatchSA
// defers the corona batch past CCoronas::Render's return, so the shield
// window can outlive the hook; this hard-clears both window flags and, when
// the entry capture ran, restores the pre-window ALPHAFUNC/ALPHAREF through
// L1+L2+L3. Called at the four frame boundaries (see
// pipeApplyWorldBaseline's call sites).
void CoronaWindow_Close(void);

// ============================================================
// Class-proof SEH choke-points — implemented in pipelinecommon.cpp.
// The __try bodies live in these POD-only helpers so callers with C++
// objects in scope (error C2712) still get SEH containment.
//
// guardedSetRT:      bind a raster as RT0. NULL raster -> false; fault ->
//                    dbglog(phase + code) and false. Callers fail open.
// guardedIm2DRender: fullscreen Im2D draw with an override pixel shader.
//                    Pre-checks verts/idx, Scene.camera and the begun-camera
//                    gate (*(void**)0xC9BCC0); fault -> dbglog(phase + code)
//                    and false.
//
// Both save/restore g_inGuardedIm2DPass (hoisted-draw pattern from postfx)
// and bump g_guardDepth so the VEH can report nesting depth on GUARD-FAULT.
// ============================================================
extern volatile LONG g_guardDepth;
bool guardedSetRT(RwRaster *ras, const char *phase);
bool guardedIm2DRender(void *ps, RwInt32 primType, RwIm2DVertex *verts, RwInt32 numVerts,
                       RwImVertexIndex *indices, RwInt32 numIndices, const char *phase);

// ============================================================
// Raw geometry render-state guard — postfx.cpp (shared with
// chars.cpp, whose SSS blur pass raw-writes ALPHATEST/Z*/blend
// states that ImmediateModeRenderStatesReStore never round-trips).
// Save the 9-state device snapshot (s_rawGeomStateIds) BEFORE
// ImmediateModeRenderStatesStore, restore AFTER ReStore so the
// restore wins on any overlap. Snapshot type is D3DRENDERSTATETYPE
// values as unsigned long (DWORD), RAW_GEOM_STATE_COUNT entries.
// ============================================================
#define RAW_GEOM_STATE_COUNT 9
bool SaveRawGeomStates(DWORD *out);
void RestoreRawGeomStates(const DWORD *in);

// ============================================================
// Shared timecycle lighting — single source of truth for all pipelines
// ============================================================
// LANDMINE (rw-convention cleanup, 2026-11-02): GetTimecycleAmbientRaw,
// GetLightsMult and UpdateTimecycleLighting have ZERO callers (grep-verified)
// and UpdateTimecycleLighting is DELIBERATELY never called (hard rule, see
// buildingPipe.cpp:163) — so s_tcAmbient stays {0,0,0} and s_lightsMult
// stays 1.0 forever. GetTimecycleAmbientRaw would return a permanent zero;
// any future caller must read the game directly like GetTimecycleAmbient
// does. Documented at the definitions in pipelinecommon.cpp.
// ============================================================
RwRGBAReal GetTimecycleAmbient(void);       // ambient WITH lightsMult applied
RwRGBAReal GetTimecycleAmbientRaw(void);    // UNCALLED — returns permanent {0,0,0} (see note above)
RwRGBAReal GetTimecycleAmbientPBR(void);    // ambient WITH lightsMult + PBR floor (ped/vehicle)
float GetLightsMult(void);                  // UNCALLED — returns permanent 1.0 (see note above)
void UpdateTimecycleLighting(void);         // UNCALLED — deliberate (hard rule); only writer of the statics

// Death ragdoll (src/extras/ragdoll_death.cpp)
void Ragdoll_Init(void);
void Ragdoll_Update(void);
bool Ragdoll_Activate(void *pPed);
void Ragdoll_Deactivate(void *pPed);
void Ragdoll_Shutdown(void);
extern bool g_ragdollDeathEnable;

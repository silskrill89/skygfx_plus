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
void dbglog(const char *fmt, ...);

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

// Returns true if the log should be emitted this call
inline bool dbglog_throttle(const char *tag) {
	static double lastTime[64] = {};
	int idx = 0;
	for(const char *p = tag; *p; p++) idx = idx * 31 + (unsigned char)*p;
	idx = (idx & 0x7FFFFFFF) % 64;
	double now = perfNow();
	if(now - lastTime[idx] < DBGLOG_THROTTLE_MS) return false;
	lastTime[idx] = now;
	return true;
}

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
	RwBool fixGrassPlacement;	// fixed for fixSeed in main.cpp
	RwBool doglare;			// fixed for doglare in main.cpp

	// Unified Pipeline - single setting controls all asset types
	int pipeline;			// Pipeline enum (PBR, PS2, Xbox, Mobile, GTAIV)

	int buildingPipe;		// Internal: mapped from pipeline
	int tagsBuildingPipe;
	RwBool ps2ModulateBuilding;
	RwBool dualPassBuilding;

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
	RwBool stochastic;
	RwBool envMapUseLODs;
	int envMapSize;
	float envMapFarClipMult;

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

	// SMAA
	RwBool smaaEnable;
	int smaaPreset; // 0=LOW, 1=MEDIUM, 2=HIGH, 3=ULTRA
	RwBool smaaPredication;
	RwBool smaaTemporal;

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

	// Edge Tessellation - smooths sharp edges
	// NOTE: Displaces vertices along normals at edges. Works best on vehicles
	// and characters with sharp polygon edges. Requires SMAA edge buffer.
	RwBool edgeTessEnable;
	float edgeTessStrength;		// displacement strength
	float edgeTessThreshold;	// edge detection threshold

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
	float waterReflectionFarClip;
	RwBool waterParallaxEnable;
	float waterParallaxScale;
	float waterNormalStrength;
	float waterFresnelPower;
	float waterSpecularPower;
	float waterSpecularIntensity;
	float waterUnderwaterFog;
	float waterFoamThreshold;
	float waterFoamSoftness;
	float waterShallowR, waterShallowG, waterShallowB;
	float waterDeepR, waterDeepG, waterDeepB;
	
	// Weather cycle control
	int currentWeatherType;
	float weatherTransition;
	RwBool weatherCycleEnabled;
	int timecycleOverrideHour;
	RwBool timecycleOverrideEnabled;

	// Debug menu
	RwBool debugMenuOpen;

	// Unified Pipeline
	bool unifiedEnable;
	int unifiedVersion;
	float unifiedSatBoost, unifiedIblTintStrength;
	float unifiedSsaoNoiseScale;
	float unifiedShadowSoftness;
	float unifiedCloudShadowStr, unifiedSunShadowStr;
	float unifiedVertexAOBoost, unifiedDayReduction, unifiedPointLightOverride;
	float unifiedSmaaThreshold, unifiedSmaaCornerRounding, unifiedSmaaMaxSearchSteps;
	bool unifiedShowMenu, unifiedShowOverlay, unifiedDebugOcclusion;
	bool unifiedEnablePrePass, unifiedEnableEdgeDetect, unifiedEnableOcclusion;
	bool unifiedEnableStoredShadows, unifiedEnableCloudShadows, unifiedEnableSunShadows;
	bool unifiedEnableTimeOfDay, unifiedEnableVertexAO, unifiedEnablePointLightOverride;
	bool unifiedEnablePostPass, unifiedEnableIBL, unifiedEnableIBLTint;
	bool unifiedEnableSurfaceWeights, unifiedEnableGrading, unifiedEnableGamma;

	// Faux Normal Buffer (stereo disparity)
	RwBool normalBufferEnable;
	float normalBufferOffset;
	float normalBufferScale;

	// 4-Pipe Chain
	RwBool pipeChainEnable;
	float pipeChainIntensity;

	// Debug toggles — individual effect enable/disable for isolation testing
	// Set to 0 in INI to bypass specific effects (helps find black screen cause)
	RwBool colorFilterEnable;		// 0=bypass colour filter entirely
	RwBool radiosityEnable;		// 0=disable radiosity postfx
	RwBool grainEnable;			// 0=disable film grain
	int pipelineOverride;		// -1=use normal pipeline, 0-4=force specific pipeline

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
};
extern int numConfigs;
extern int currentConfig;
extern Config *config, configs[10];
void readIni(int n);
void findInis(void);
void readInis(void);
void resetValues(void);
void refreshIni(void);
void refreshMenu(void);
void reloadAllInis(void);
void saveConfig(void);
void installMenu(void);
void setConfig(void);

struct Hooks
{
};

extern bool iCanHasbuildingPipe;
extern bool iCanHasvehiclePipe;
extern bool iCanHasSunGlare;
extern bool iCanHasNeoDrops;
extern int explicitBuildingPipe;
extern bool gHasExternalNormalMapPlugin;

/* Normal map */
struct RxPipeline;
extern RxPipeline *gNormalMapAtomicPipelines[2];
void normalmap_init(void);
void normalmap_tryAttach(void);
void normalmap_shutdown(void);

/* Env map */
extern RwCamera *reflectionCam;
extern RwRaster *envFB, *envZB;
extern RwTexture *reflectionTex;
void MakeEnvmapRasters(void);
void MakeEnvmapCam(void);

/* Normal buffer (stereo disparity) */
extern RwCamera *normalCam;
extern RwRaster *normalFB, *normalZB;
extern RwTexture *normalTex;
void MakeNormalRasters(void);
void MakeNormalCam(void);
void RenderNormalBuffer(void);
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
	bool stochastic;
	bool dualPass;
	uint8 zwriteThreshold;
	uint8 materialType;	// MaterialType enum: 0=none, 1=skin, 2=cloth, 3=vegetation
};
TexInfo *RwTextureGetTexDBInfo(RwTexture *tex);
int TexDBPluginAttach(void);
void initTexDB(void);

extern bool gRenderingSpheremap;
extern CVector reflectionCamPos;

extern RxPipeline *&CCustomBuildingDNPipeline__ObjPipeline;
extern RxPipeline *&CCustomBuildingPipeline__ObjPipeline;
void TagRenderCB(RpAtomic *atomic, RxD3D9ResEntryHeader *resEntryHeader, RxD3D9InstanceData *instanceData);
RxPipeline *CCustomBuildingPipeline__CreateCustomObjPipe_PS2(void);
RxPipeline *CCustomBuildingDNPipeline__CreateCustomObjPipe_PS2(void);
int PDSPipePluginAttach(void);
int EDEDPluginAttach(void);
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
extern float VehShaders_GetDirtLevel(void *vehicle);
extern void VehShaders_ApplyDirtToPBR(float dirtLevel, float *specular, float *glossiness, float *specularTintR, float *specularTintG, float *specularTintB);

// Area-based color saturation system
extern bool VehShaders_CheckColorSaturation(float r, float g, float b, int area);
extern void VehShaders_GenerateColor(unsigned int hash, int area,
                                     float *outR, float *outG, float *outB);
extern int  VehShaders_GetColorArea(float posX, float posY);
extern float VehShaders_GetMinSaturation(float posX, float posY);

void hookVehiclePipe(void);
void hookBuildingPipe(void);
void D3D9Render(RxD3D9ResEntryHeader *resEntryHeader, RxD3D9InstanceData *instanceData);
void D3D9RenderDual(int dual, RxD3D9ResEntryHeader *resEntryHeader, RxD3D9InstanceData *instancedData, TexInfo *textInfo = nullptr);
TexInfo* FindTexInfo(char* name);

void CMessages__AddMessageJumpQWithNumber(char* text, unsigned int time, unsigned short flag, int n1, int n2, int n3, int n4, int n5, int n6, bool bPreviousBrief);

void fixSAMP(void);
extern HMODULE UG_mod;

extern RwInt32 pdsOffset;

//////// Pipelines
///// Shaders
// misc
extern void *simplePS;
// postfx
extern void *vcTrailsPS;
extern void *modernColorFilterPS;
extern void *gradingPS, *contrastPS, *tonemapPassPS;
extern void *blurPS, *radiosityPS;
extern void *SMAA;
extern void *SMAA_Edge;
extern void *SMAA_EdgeNormal;
extern void *SMAA_EdgeDepth;
extern void *SMAA_EdgeCombined;
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
extern void *VehiclePaint_GTAIV;
extern void *Water_Parallax;
extern void *Water_VS;
extern void *VehiclePBR_Modern;
extern void *Glass_Vehicle;
extern void *Rubber_Vehicle;
extern void *CarPaint_Reflections;
extern void *PBR_Lighting;
extern void *ClampShader;
extern void *DynamicSky;
extern void *SkinPBR;
extern void *GTAIV_PS;
extern void *HeightFog;
extern void *GodRays;
extern void *VelocityReconstruct;

// Vehicle legacy
extern void *vehiclePipeVS;
extern void *vehiclePBRVS;
extern void *ps2CarFxVS;
extern void *specCarFxVS, *specCarFxPS;
extern void *xboxCarVS;

// Wheel extender
#include "wheels_extender.h"
extern void *leedsCarFxVS;
extern void *mobileVehiclePipeVS, *mobileVehiclePipePS;

// Building legacy
extern void *ps2BuildingVS, *ps2BuildingFxVS, *ps2BuildingWindVS;
extern void *xboxBuildingVS, *xboxBuildingPS, *xboxBuildingStochasticPS, *xboxBuildingWindVS;
extern void *buildingPBRVS, *buildingPBRPS;
extern void *sphereBuildingVS;
extern void *simpleDetailPS, *simpleDetailStochasticPS, *simpleFogPS;

void DrawUnifiedDebugMenu(IDirect3DDevice9 *device);
void UploadUnifiedConstants(IDirect3DDevice9 *device);
void UpdateVehicleRing();
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


extern int &dword_C02C20, &dword_C9BC60;
extern RxPipeline *&skinPipe, *&CCustomCarEnvMapPipeline__ObjPipeline;

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
// Vehicle: c22 = {glossiness, specular, tintR, tintG}
//          c23 = {wheelFlag, noiseScale, edgeBlend, 0}
// Building: c22 = {glossiness, specular, clearcoat, subsurface}
//           c23 = {tintR, tintG, tintB, 0}
// ============================================================
void pipeUploadPBR(float glossiness, float specular, float c22_3, float c22_4,
                   float c23_1, float c23_2, float c23_3);

// ============================================================
// Rendering mode interfaces (ps2_mode.cpp, xbox_mode.cpp, etc.)
// Each mode provides: ApplyDefaults, GetPreset
// ============================================================
struct PresetConfig;

void PS2Mode_ApplyDefaults(Config *c);
const PresetConfig* PS2Mode_GetPreset(void);

void XboxMode_ApplyDefaults(Config *c);
const PresetConfig* XboxMode_GetPreset(void);

void IVMode_ApplyDefaults(Config *c);
const PresetConfig* IVMode_GetPreset(void);

void MobileMode_ApplyDefaults(Config *c);
const PresetConfig* MobileMode_GetPreset(void);

void PCPatchedMode_ApplyDefaults(Config *c);
const PresetConfig* PCPatchedMode_GetPreset(void);

void CustomMode_ApplyDefaults(Config *c);
const PresetConfig* CustomMode_GetPreset(void);

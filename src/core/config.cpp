// config.cpp — INI parse / save / EMIT machinery extracted from main.cpp
// (2026-11-09). Bodies byte-identical to the pre-extraction main.cpp. Owns:
//   * the parse helper family: readhex / readint / readfloat / iniGet2 /
//     readint2 / readfloat2, plus the StrAssoc enum-map helper
//   * readIniFile() — single-INI parser + fresh-INI default writer
//     (SKYGFX_EMIT_DEFAULT_KEYS emission list) + [Modes] active= apply
//   * IniPathForSlot / readIni / readInis / setConfig / reloadAllInis
//   * saveConfigTo / saveConfig / loadConfigFile
// No install/entry hook: these are called functions reached from main.cpp
// (DllMain -> InjectDelayedPatches / afterStreamIni) and the debug menu.
#include "skygfx.h"
#include "ini_parser.hpp"
#include "debug_handl.h"
#include "modeManager.h"
#include "peds/peds_pipe.h"
#include "treeWind.h"
#include "grasssys.h"
#include "profiler.h"
#include <string.h>
#include <stdio.h>

// ===== Local externs (main.cpp) =====
// config.cpp owns the INI machinery; the symbols below stay in main.cpp.
// Local-extern idiom (no shared header), matching shadows.cpp / rw_fixes.cpp.
extern char asipath[];
extern int original_bRadiosity;
extern bool disableClouds;
extern bool disableGamma;
extern bool fixPcCarLight;
extern int transparentLockon;
extern int fixShadows;
extern bool privateHooks;
extern bool forceWindShader;
extern bool cloudRenderFix;
extern int g_noiseQuality;             // utility noise knob (menuGet/SetNoiseQuality)
extern bool g_waterTempBufferHook;     // water temp-buffer kill-switch
extern int g_waterFresnelSplit;        // waterPipe.cpp
extern ModeId s_activeModeIni;
extern bool s_activeModeIniSeen;
extern int defaultColourLeftUOffset;
extern int defaultColourRightUOffset;
extern int defaultColourTopVOffset;
extern int defaultColourBottomVOffset;
extern const char *modeIniAlias(ModeId id);
extern bool modeOwnsPipes(void);
extern void clampEnvSliderRanges(Config *c);
extern void clampTonemapExposureRanges(Config *c);
extern void clampSsaoPower(Config *c);
extern void clampChromeParams(Config *c);
extern void cfEnableSetUserIntent(Config *c, RwBool v);
extern void cfEnableHeal(Config *c);
extern void derivePipelineFromPipes(Config *c);
extern const char *s_pipeDeriveTrigger;

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
static const int g_laneSectionReads = 13;

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
		{"PS2",     BUILDING_PS2},
		{"Xbox",    BUILDING_XBOX},
		{"GTAIV",   BUILDING_GTAIV},
		{"PBR",     BUILDING_PBR},
		{"Vanilla", BUILDING_VANILLA},
		{"",        -1},
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
		{"Vanilla", CAR_VANILLA},
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
	// Tiled-texture "noise based variation" detail jitter (the former
	// "stochastic texturing" lane, now served by the noise service).
	// Current key: noiseDetail. Legacy keys (stochasticTexturing /
	// stochastic) stay readable so existing INIs keep working; one dbglog
	// notes when a deprecated key was honoured.
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

	c->zwriteThreshold = readint(cfg.get("SkyGfx", "zwriteThreshold", ""), 128);
	if(c->zwriteThreshold < 0) c->zwriteThreshold = 0;
	if(c->zwriteThreshold > 255) c->zwriteThreshold = 255;

	c->zwriteThresholdGrass = readint(cfg.get("SkyGfx", "zwriteThresholdGrass", ""), 128);
	if(c->zwriteThresholdGrass < 0) c->zwriteThresholdGrass = 0;
	if(c->zwriteThresholdGrass > 255) c->zwriteThresholdGrass = 255;

	c->zwriteThresholdPed = readint(cfg.get("SkyGfx", "zwriteThresholdPed", ""), 128);
	if(c->zwriteThresholdPed < 0) c->zwriteThresholdPed = 0;
	if(c->zwriteThresholdPed > 255) c->zwriteThresholdPed = 255;
	// [S2] One-shot attribution receipt: log the parsed alpha-test refs so a
	// wrong-high GREATEREQUAL value (facade/foliage holes) is visible at
	// startup. Read/save/EMIT are now symmetric ([SkyGfx] everywhere).
	if(c == config){
		static bool s_zwriteLogged = false;
		if(!s_zwriteLogged){
			s_zwriteLogged = true;
			dbglog("[Config] alpha-ref thresholds: zwrite=%d grass=%d ped=%d",
			       c->zwriteThreshold, c->zwriteThresholdGrass, c->zwriteThresholdPed);
		}
	}

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
	c->ssaoRadius = readfloat(cfg.get("SkyGfx", "ssaoRadius", ""), 1.0f);
	c->ssaoPower = readfloat(cfg.get("SkyGfx", "ssaoPower", ""), 2.0f);
	c->ssaoKernelSize = readfloat(cfg.get("SkyGfx", "ssaoKernelSize", ""), 16.0f);
	c->ssaoSampleCount = readint(cfg.get("SkyGfx", "ssaoSampleCount", ""), 16);
	// ssaoPower is a gamma on occlusion: 9.237 amplified mild AO into the
	// hard object-shaped silhouette (sane range [0.5..4.0], presets 1.5/2.0).
	clampSsaoPower(c);

	c->smaaEnable = readint(cfg.get("SkyGfx", "smaaEnable", ""), c->smaaEnable);

	// Motion Blur
	c->motionBlurEnable = readint(cfg.get("SkyGfx", "motionBlurEnable", ""), c->motionBlurEnable);
	c->motionBlurStrength = readfloat(cfg.get("SkyGfx", "motionBlurStrength", ""), 0.4f);
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

	// State-integrity lane (SAFETY DEFAULTS: default 1 = ON, floored to 1 at
	// parse — not user-opt-in; H1 pattern, docs/plans/2026-11-09-modern-mode-
	// breakage.md §4/§5):
	//   texAlphaClassify      — texture-alpha classification (tex alpha into
	//                           the PBR alpha channel instead of vertex alpha);
	//   stateBaselineEnable   — frame-boundary world baseline
	//                           (pipeApplyWorldBaseline at the four boundaries).
	//                           SAFETY DEFAULT, not user-opt-in: the parse
	//                           floors it to 1 (see the clamp below), so no
	//                           INI/mode path can ever park it at 0.
	//   defaultCbShaderHygiene— unbind stale VS/PS in myDefaultCallback;
	//   spriteStateShield     — per-draw ALPHAFUNC/ALPHAREF/SEPARATEALPHA/
	//                           COLORWRITEENABLE normalisation in
	//                           coronaDrawRawShield (blend/ZWRITE force is
	//                           not gated). Default 1 (2026-11-16 particle-
	//                           alpha cleanup) but NOT floored — toggleable
	//                           lever, unlike the floored members above.
	c->texAlphaClassify = readint2(cfg, "Effects", "texAlphaClassify", 1);
	// Vanilla-fallback doctrine (2026-11-27): 0 means OFF — no parse floor.
	// Default 1 keeps stock-enhanced behaviour; a user who zeroes the lever
	// (or runs with all settings zeroed) gets the stock game path.
	if(c->texAlphaClassify < 0) c->texAlphaClassify = 0;
	if(c->texAlphaClassify > 1) c->texAlphaClassify = 1;
	c->stateBaselineEnable = readint2(cfg, "Core", "stateBaselineEnable", 1);
	// Vanilla-fallback doctrine: 0 means OFF — no parse floor. Default 1.
	if(c->stateBaselineEnable < 0) c->stateBaselineEnable = 0;
	if(c->stateBaselineEnable > 1) c->stateBaselineEnable = 1;
	// 2026-11-16 particle-alpha cleanup: default restored to 1 (a8aecb7
	// flipped it to 0 under "run vanilla settings"; with the shields OFF the
	// black-RGB/opaque-alpha glow textures rasterise as opaque boxes).
	// Toggleable lever, deliberately NOT floored (every fix keeps its own
	// toggle): 0 = skip the four-state normalisation (AAP parity).
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
	c->rwFixesEnable = readint(cfg.get("SkyGfx", "rwFixesEnable", ""), 1);
	if(c->rwFixesEnable < 0) c->rwFixesEnable = 0;
	if(c->rwFixesEnable > 1) c->rwFixesEnable = 1;
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
	// Vanilla-fallback doctrine: 0 means OFF — no parse floor. Default 1.
	if(c->defaultCbShaderHygiene < 0) c->defaultCbShaderHygiene = 0;
	if(c->defaultCbShaderHygiene > 1) c->defaultCbShaderHygiene = 1;
	// Ped opaque contract (myDefaultCallback): 1 = enforce blend-off,
	// zwrite/ztest on, alpha test GREATEREQUAL@1 for the skin pipeline.
	c->pedOpaqueContract = readint(cfg.get("SkyGfx", "pedOpaqueContract", ""), 1);
	// Vanilla-fallback doctrine: 0 means OFF — no parse floor. Default 1.
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
	c->ambientFloor = readfloat2(cfg, "Ped", "ambientFloor", 0.10f);
	if(c->ambientFloor < 0.0f) c->ambientFloor = 0.0f;
	if(c->ambientFloor > 0.2f) c->ambientFloor = 0.2f;
	c->ambientClampEnable = readint2(cfg, "Ped", "ambientClampEnable", 0);
	if(c->ambientClampEnable < 0) c->ambientClampEnable = 0;
	if(c->ambientClampEnable > 1) c->ambientClampEnable = 1;
	c->transparencyEnable = readint2(cfg, "Ped", "transparencyEnable", 0);
	if(c->transparencyEnable < 0) c->transparencyEnable = 0;
	if(c->transparencyEnable > 1) c->transparencyEnable = 1;

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
	c->ivDesaturation = readfloat(cfg.get("SkyGfx", "ivDesaturation", ""), 0.15f);
	c->ivGamma = readfloat(cfg.get("SkyGfx", "ivGamma", ""), 1.0f);
	c->ivSaturation = readfloat(cfg.get("SkyGfx", "ivSaturation", ""), 0.3f);
	c->ivCurves = readfloat(cfg.get("SkyGfx", "ivCurves", ""), 1.0f);
	c->ivVignetteIntensity = readfloat(cfg.get("SkyGfx", "ivVignetteIntensity", ""), 0.15f);
	c->ivVignetteRadius = readfloat(cfg.get("SkyGfx", "ivVignetteRadius", ""), 0.70f);
	c->ivVignetteContrast = readfloat(cfg.get("SkyGfx", "ivVignetteContrast", ""), 1.5f);
	c->ivBloomIntensity = readfloat(cfg.get("SkyGfx", "ivBloomIntensity", ""), 0.05f);
	c->ivExposure = readfloat(cfg.get("SkyGfx", "ivExposure", ""), 2.5f);

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
 	// ROUGH CLEANUP (2026-11-10): pedAmbientFloor removed. It has been
 	// DEAD-BUT-PARSED since the W3 revert (zero render consumers; the ped path
 	// uses the timecycle ambient directly) and only survived because the debug
 	// menu row + reset table pointed at it. Key, field, row and mode entries
 	// are all gone.
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
	c->sssPostProcessStrength = readfloat(cfg.get("SkyGfx", "sssPostProcessStrength", ""), 0.2f);
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
	
 	// ROUGH CLEANUP (2026-11-10): the Hair enhancement and Vegetation
 	// enhancement parse blocks were removed. Both were dead levers - declared,
 	// parsed, saved and menu-exposed with zero render consumers anywhere in the
 	// tree (whole-tree grep), so the values only ever round-tripped.

	// CLASS 3: round-trip fixes — keys saveConfig writes / the menu mutates
	// but readIni never read back (the preset block used to force them)
	c->sssPostProcessEnable = readint(cfg.get("SkyGfx", "sssPostProcessEnable", ""), c->sssPostProcessEnable);
	c->skinEnhanceEnable = readint(cfg.get("SkyGfx", "skinEnhanceEnable", ""), c->skinEnhanceEnable);

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
	// Sun corona — clamp to [0,255] at parse (weather.cpp's ScaleColourByte
	// clamps only the >255 side; a negative intensity would otherwise push
	// negative CColourSet shorts).
	c->sunCoronaIntensity = readfloat(cfg.get("SkyGfx", "sunCoronaIntensity", ""), 0.4f);
	if(c->sunCoronaIntensity < 0.0f) c->sunCoronaIntensity = 0.0f;
	if(c->sunCoronaIntensity > 255.0f) c->sunCoronaIntensity = 255.0f;
	c->sunCoreIntensity = readfloat(cfg.get("SkyGfx", "sunCoreIntensity", ""), 0.6f);
	if(c->sunCoreIntensity < 0.0f) c->sunCoreIntensity = 0.0f;
	if(c->sunCoreIntensity > 255.0f) c->sunCoreIntensity = 255.0f;
	c->sunStreakIntensity = readfloat(cfg.get("SkyGfx", "sunStreakIntensity", ""), 0.5f);
	if(c->sunStreakIntensity < 0.0f) c->sunStreakIntensity = 0.0f;
	if(c->sunStreakIntensity > 255.0f) c->sunStreakIntensity = 255.0f;
	c->sunStreakSize = readfloat(cfg.get("SkyGfx", "sunStreakSize", ""), 0.6f);

	// Alt timecyc overwrite gate (default 0 = vanilla ambient)
	c->altTimecycEnable = readint(cfg.get("SkyGfx", "altTimecycEnable", ""), 0);

	// Normal buffer
	c->normalBufferOffset = readfloat(cfg.get("SkyGfx", "normalBufferOffset", ""), c->normalBufferOffset);
	c->normalBufferScale = readfloat(cfg.get("SkyGfx", "normalBufferScale", ""), c->normalBufferScale);
	
	// Pipe chain
	c->pipeChainIntensity = readfloat(cfg.get("SkyGfx", "pipeChainIntensity", ""), c->pipeChainIntensity);
	
	// Water pipe (procedural PBR water — Water_VS / Water_Parallax).
	c->waterParallaxEnable = readint(cfg.get("SkyGfx", "waterParallaxEnable", ""), 1);
	// S1 kill-switch for the junior temp-buffer re-render (see the hook gate
	// above): default 0 (OFF) — the M-series water renderer is the single
	// surface; 1 restores the legacy pipe draw for A/B testing.
	g_waterTempBufferHook = readint(cfg.get("SkyGfx", "waterTempBufferHook", ""), 0) != 0;
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
	// Water milestone toggles M4/M5/M7/M8 (spec 2026-10-30-water-overhaul):
	// default 1 = ON, technically-gated like M1-M3 (the water pipe ANDs
	// these onto its capability gates - waterPipe.cpp c17/c18 - so 0 forces
	// the pre-milestone identity; see skygfx.h). Menu rows pending
	// (designer lane).
	c->waterRefractionValid = readint(cfg.get("SkyGfx", "waterRefractionValid", ""), 1);
	c->waterCaustics = readint(cfg.get("SkyGfx", "waterCaustics", ""), 1);
	c->waterSss = readint(cfg.get("SkyGfx", "waterSss", ""), 1);
	c->waterVolume = readint(cfg.get("SkyGfx", "waterVolume", ""), 1);
	// Normalize to 0/1 properly: a negative INI value must read OFF (0), not
	// get coerced to ON by the old `if(x) x = 1;` (which mapped any nonzero —
	// including -1 — to 1).
	if(c->waterShoreFoam < 0) c->waterShoreFoam = 0;
	if(c->waterShoreFoam > 1) c->waterShoreFoam = 1;
	if(c->waterBodyClassify < 0) c->waterBodyClassify = 0;
	if(c->waterBodyClassify > 1) c->waterBodyClassify = 1;
	if(c->waterRefractionValid < 0) c->waterRefractionValid = 0;
	if(c->waterRefractionValid > 1) c->waterRefractionValid = 1;
	if(c->waterCaustics < 0) c->waterCaustics = 0;
	if(c->waterCaustics > 1) c->waterCaustics = 1;
	if(c->waterSss < 0) c->waterSss = 0;
	if(c->waterSss > 1) c->waterSss = 1;
	if(c->waterVolume < 0) c->waterVolume = 0;
	if(c->waterVolume > 1) c->waterVolume = 1;
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
		EMIT("SkyGfx", "rwFixesEnable", "1"); \
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
		EMIT("SkyGfx", "zwriteThreshold", "128"); \
		EMIT("SkyGfx", "zwriteThresholdGrass", "128"); \
		EMIT("SkyGfx", "zwriteThresholdPed", "128"); \
		EMIT("SkyGfx", "ambientFloor", "0.10"); \
		EMIT("SkyGfx", "ambientClampEnable", "0"); \
		EMIT("SkyGfx", "transparencyEnable", "0"); \
		EMIT("SkyGfx", "postfxDumpDebug", "0"); \
		EMIT("SkyGfx", "debugOverlay", "0"); \
		EMIT("SkyGfx", "pbrAmbientFloor", "0.0"); \
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
		EMIT("SkyGfx", "waterTempBufferHook", "0"); \
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
		EMIT("SkyGfx", "waterRefractionValid", "1"); \
		EMIT("SkyGfx", "waterCaustics", "1"); \
		EMIT("SkyGfx", "waterSss", "1"); \
		EMIT("SkyGfx", "waterVolume", "1"); \
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
		// Water milestone toggles M4/M5/M7/M8 - default ON (1).
		cfg.set("SkyGfx", "waterRefractionValid", "1");
		cfg.set("SkyGfx", "waterCaustics", "1");
		cfg.set("SkyGfx", "waterSss", "1");
		cfg.set("SkyGfx", "waterVolume", "1");
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

// ONE slot -> INI filename mapping, shared by readIni / saveConfig /
// CurrentIniPath (debugmenu_ui.cpp). slot is a 0-based index into configs[].
//   numConfigs == 0 -> only the legacy skygfx.ini exists: slot 0 -> "skygfx.ini".
//   numConfigs  > 0 -> skygfx1.ini..skygfx{numConfigs}.ini are the slots:
//                      slot k -> "skygfx{k+1}.ini".
void
IniPathForSlot(int slot, char *out, int outSize)
{
	if(slot == 0 && numConfigs == 0)
		snprintf(out, outSize, "%sskygfx.ini", asipath);
	else
		snprintf(out, outSize, "%sskygfx%d.ini", asipath, slot + 1);
}

void
readIni(int n)
{
	char modulePath[MAX_PATH];
	GetModuleFileName(dllModule, modulePath, MAX_PATH);
	// BOUNDED: strncpy copies at most MAX_PATH-1 and we force the NUL, so a
	// truncated GetModuleFileName result can never leave asipath
	// unterminated into the strrchr / snprintf below.
	strncpy(asipath, modulePath, MAX_PATH - 1);
	asipath[MAX_PATH - 1] = '\0';
	char *p = strrchr(asipath, '\\');
	if (p) p[1] = '\0';

	// n is the 1-based file number: n==0 -> skygfx.ini (slot 0), n>0 ->
	// skygfx{n}.ini (slot n-1). IniPathForSlot expresses the same mapping
	// from the slot side, so read / save / report can never drift apart.
	const int slot = (n == 0) ? 0 : (n - 1);
	IniPathForSlot(slot, modulePath, sizeof(modulePath));
	readIniFile(modulePath, &configs[slot]);
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
	static const char* buildPipeNames[] = {"PS2", "Xbox", "GTAIV", "PBR", "Vanilla"};
	if(c->buildingPipe >= 0 && c->buildingPipe < 5)
		cfg.set("SkyGfx", "buildingPipe", buildPipeNames[c->buildingPipe]);

	static const char* vehPipeNames[] = {"PS2", "PC", "Xbox", "Specular", "Mobile", "Neo", "Leeds", "VCS", "Env", "GTAIV", "Modern", "Vanilla"};
	if(c->vehiclePipe >= 0 && c->vehiclePipe < 12)
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

	// Vegetation enhancement

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
	cfg.set("SkyGfx", "rwFixesEnable", std::to_string(c->rwFixesEnable));
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
	cfg.set("SkyGfx", "ambientClampEnable", std::to_string(c->ambientClampEnable));
	cfg.set("SkyGfx", "transparencyEnable", std::to_string(c->transparencyEnable));

	// Water pipe (procedural PBR water)
	cfg.set("SkyGfx", "waterParallaxEnable", std::to_string(c->waterParallaxEnable));
	cfg.set("SkyGfx", "waterTempBufferHook", std::to_string(g_waterTempBufferHook ? 1 : 0));
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
	// Water milestone toggles M4/M5/M7/M8 - round-trip (no INI save gap).
	cfg.set("SkyGfx", "waterRefractionValid", std::to_string(c->waterRefractionValid));
	cfg.set("SkyGfx", "waterCaustics", std::to_string(c->waterCaustics));
	cfg.set("SkyGfx", "waterSss", std::to_string(c->waterSss));
	cfg.set("SkyGfx", "waterVolume", std::to_string(c->waterVolume));
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
	// Save to the ACTIVE slot's INI (the same file readIni loaded it from),
	// not the hard-coded skygfx.ini — with numbered configs present the load
	// path reads skygfx1..N.ini and never the base file, so a base-file save
	// was silently never read back.
	char modulePath[MAX_PATH];
	IniPathForSlot(currentConfig, modulePath, sizeof(modulePath));
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

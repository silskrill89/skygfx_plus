// modeManager.cpp — graphics mode manager: single-active-mode state
// machine + the canonical per-mode Config profile table.
//
// Exactly ONE mode is active at a time (or none). The manager never stacks
// profiles: every mode change is FIELD-LEVEL layering onto the live Config —
//
//     engage A : config[fields owned by A] = profile(A).value
//     switch A->B : config[fields owned by A] = base[A]; then profile(B)
//     disengage : config[fields owned by A] = base[A]; latch MODE_NONE
//
// No non-owned field is ever written, so user/menu changes to settings a mode
// does not own survive the whole run. The base is the post-INI (user) state:
// [Modes] active= is applied at the very END of readIniFile(), after every
// other key (including qualityPreset). The base is keyed by the Config's slot
// in `configs[]` (NOT one process-global snapshot: `setConfig()` can repoint
// `config` to another slot) and is captured BEFORE a profile runs, so a
// disengage restores exactly the INI values with no profile residue.
//
// ApplyMode() is the ONE writer of mode-owned fields. It also calls
// resetValues() and notifies ModeManager_RegisterListener subscribers.
// ModeManager_SetMode() is a thin alias kept for existing callers;
// ModeManager_Disengage() is the explicit UI toggle-off path and
// ModeManager_RefreshBase() re-snapshots the base after an INI reload.
//
// Mode-owned field list: docs/plans/2026-10-24-mode-preset-architecture.md
// §3.1-§3.6. The ~35 NEW parity fields from §4 are intentionally absent —
// they do not exist in Config yet (M1b+ work). `pipeline` is written as a
// compatibility mirror; derivePipelineFromPipes() remains the derivation
// authority in main.cpp (which is out of this lane's write scope).
//
// PHYSICS OWNERSHIP (m0971): sky_physics.ini keys live in the PhysicsCfg
// store (src/core/physicscfg.{h,cpp}) — NOT in struct Config — so they cannot
// ride the MV_* member-pointer table below (which binds Config::* fields and
// has no string support: MV_STR is only a stringize helper for field names).
// Each profile therefore may carry a PARALLEL ModePhysicsValue row list,
// applied/restored through the PhysicsCfg accessors with the same
// field-level contract: only keys a profile owns are ever written; everything
// else keeps the user's sky_physics.ini value. See physicscfg.h "SINGLE
// SOURCE OF TRUTH" for why PhysicsCfg (not Config) owns the values.

#include "modeManager.h"
#include "physicscfg.h"	// m0971: ModePhysicsValue rows apply via these accessors
#include <string.h>	// _stricmp — StrAssoc::get key-comparison semantics

// Per-config-slot pre-mode base snapshot (static storage, no heap).
#define MODE_CONFIG_SLOTS ((int)(sizeof(configs) / sizeof(configs[0])))
static Config s_baseConfig[MODE_CONFIG_SLOTS];
static bool s_haveBase[MODE_CONFIG_SLOTS];

static ModeId s_mode = MODE_NONE;

typedef void (*ModeListenerFn)(ModeId oldMode, ModeId newMode);
static ModeListenerFn s_listeners[MODEMANAGER_MAX_LISTENERS];
static int s_numListeners = 0;

// ============================================================
// CANONICAL MODE MODEL — data + a compile-time-checked setter table.
// ============================================================

// One entry per owned Config field. Exactly one of iField/fField/bField is
// non-null; the member-pointer form makes a reference to a missing/renamed
// Config field a compile error instead of a silent no-op. iniKey is the exact
// key written to skygfx.ini and is stringized from the field name unless an
// MV_*K() alias row overrides it (the seven renamed keys in main.cpp).
struct ModeValue
{
	const char *iniKey;	// exact skygfx.ini key (case-insensitive match)
	int   Config::*iField;	// int / RwBool / enum-typed fields
	float Config::*fField;	// float fields
	bool  Config::*bField;	// bool fields (bYCbCrFilter)
	int   ival;
	float fval;
	bool  bval;
};

// One owned sky_physics.ini key (m0971). Exactly one of sval/ival is live:
// sval non-null = string row (softbodyBackend), else int row. These address
// the PhysicsCfg store, not Config, so they are applied via
// PhysicsCfgSetBackend/PhysicsCfgSetInt with the same owned-only contract as
// ModeValue.
struct ModePhysicsValue
{
	const char *iniKey;	// [SkyPhysics] key (case-insensitive match)
	const char *sval;	// string value, or NULL for an int row
	int   ival;
};

struct ModeProfile
{
	ModeId id;
	const char *name;	// display name (UI / log)
	const char *iniAlias;	// stable INI alias for the future [Modes] active= key
	const ModeValue *values;
	int numValues;
	const ModePhysicsValue *physics;	// owned sky_physics.ini rows (m0971)
	int numPhysics;
};

// The key is the Config member name by default (main.cpp writes that exact
// name for every owned field except the aliases below), stringized so the
// 126 existing rows need no extra argument. MV_IK/MV_FK/MV_BK are the same
// rows with an explicit INI key for the renamed fields. Spell the K first
// because MV_I is a prefix of MV_IK.
#define MV_STR_(x) #x
#define MV_STR(x) MV_STR_(x)
#define MV_IK(field, key, value) { (key), &Config::field, nullptr, nullptr, (int)(value), 0.0f, false }
#define MV_FK(field, key, value) { (key), nullptr, &Config::field, nullptr, 0, (float)(value), false }
#define MV_BK(field, key, value) { (key), nullptr, nullptr, &Config::field, 0, 0.0f, (value) != 0 }
#define MV_I(field, value) { MV_STR(field), &Config::field, nullptr, nullptr, (int)(value), 0.0f, false }
#define MV_F(field, value) { MV_STR(field), nullptr, &Config::field, nullptr, 0, (float)(value), false }
#define MV_B(field, value) { MV_STR(field), nullptr, nullptr, &Config::field, 0, 0.0f, (value) != 0 }
#define MV_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

// sky_physics.ini row builders (m0971). MP_STR is deliberately NOT the field
// stringizer above — these rows carry VALUES, not member names.
#define MP_I(key, value) { (key), NULL, (int)(value) }
#define MP_S(key, value) { (key), (value), 0 }
#define MP_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

// ------------------------------------------------------------
// MODE_PS2 — doc §3.1. Vintage PS2: no enhancements, PS2 hardware look.
// Note: dualPass* is 0 here (PS2 does not dual-pass; ps2_mode.cpp:74-79 and
// doc §3.1). coronaZtest is 0 = "Disable (PS2)" per the aap switch
// (backup_original/src/main.cpp:330-346); doc §3.1 agrees.
// ------------------------------------------------------------
static const ModeValue kValuesPS2[] = {
	// Pipeline identity + tags (pipeline is a compat mirror, see header)
	MV_I(pipeline, PIPELINE_PS2),
	MV_I(buildingPipe, BUILDING_PS2),
	MV_I(vehiclePipe, CAR_PS2),
	MV_I(colorFilter, COLORFILTER_PS2),
	MV_I(tagsBuildingPipe, BUILDING_PS2),
	MV_IK(preset, "gamePreset", PRESET_SA_PS2),
	MV_I(ivMode, 0),

	// Building
	MV_I(ps2ModulateBuilding, 1),
	MV_I(detailMaps, 0),
	MV_I(noiseDetail, 0),
	MV_I(dualPassBuilding, 0),
	MV_I(usePCTimecyc, 0),

	// Vehicle / env map
	MV_I(dualPassVehicle, 0),
	MV_I(envMapSize, 256),
	MV_F(envMapFarClipMult, 1.0f),
	MV_I(envMapUseLODs, 0),
	MV_I(reflectionMsaaLevel, 0),

	// Water (only implemented base style; PS2 vector water is absent)
	MV_I(waterStyle, 0),
	// Own the tier + pipe gate: a fresh/ULTRA snapshot would otherwise leave
	// the Ultra tier-3 full-res depth copies (and the custom water pipe) live
	// under a PS2 look.
	MV_I(waterQuality, 1),		// 1 = Medium
	MV_I(waterParallaxEnable, 1),
	// Legacy extras the preset writer pinned off (sun glare, water drops)
	MV_IK(doglare, "sunGlare", 0),
	MV_I(neoWaterDrops, 0),

	// PostFX
	MV_I(radiosity, 0),
	MV_I(doRadiosity, 1),
	MV_I(grainFilter, 0),
	MV_I(infraredVision, 0),
	MV_I(nightVision, 0),
	MV_I(ssaoEnable, 0),
	MV_I(smaaEnable, 0),
	MV_I(motionBlurEnable, 0),
	MV_I(sssPostProcessEnable, 0),
	MV_I(skinEnhanceEnable, 0),
	MV_I(hairEnhanceEnable, 0),
	MV_I(vegetationEnhanceEnable, 0),
	MV_I(normalMapEnable, 0),
	MV_I(bloomEnable, 0),
	MV_I(dofEnable, 0),
	MV_I(sunShaftsEnable, 0),
	MV_I(fxaaEnable, 0),
	MV_F(grainIntensity, 0.0f),
	MV_F(caIntensity, 0.0f),
	MV_I(vcsTrails, 0),
	MV_I(colorFilterEnable, 1),
	MV_F(rgb1Mult, 1.0f),
	MV_F(rgb2Mult, 1.0f),
	MV_BK(bYCbCrFilter, "YCbCrCorrection", false),
	MV_F(ivDesaturation, 0.0f),
	MV_F(ivGamma, 1.0f),
	MV_F(ivVignetteIntensity, 0.0f),
	MV_F(ivBloomIntensity, 0.0f),

	// Coronas (PS2 no-Z-test; identity sun-corona gains — doc TBD-probe)
	MV_I(coronaZtest, 0),
	MV_F(sunCoronaIntensity, 1.0f),
	MV_F(sunCoreIntensity, 1.0f),
	MV_F(sunStreakIntensity, 1.0f),
	MV_F(sunStreakSize, 1.0f),

	// Alpha / state
	MV_IK(ps2ModulateGlobal, "ps2Modulate", 1),
	MV_I(ps2ModulateGrass, 1),
	MV_IK(dualPassGlobal, "dualPass", 0),
	MV_I(dualPassPed, 0),
	MV_I(dualPassGrass, 0),
	MV_I(dualPassDefault, 0),
	MV_I(grassAddAmbient, 0),
	MV_IK(backfaceCull, "grassBackfaceCull", 1),
	MV_IK(fixGrassPlacement, "grassFixPlacement", 0),
	MV_I(pedShadows, -1),
	MV_I(stencilShadows, -1),
	MV_I(zwriteThreshold, 128),
	MV_I(zwriteThresholdGrass, 128),
	MV_I(zwriteThresholdPed, 128),
	// Safety systems, not look knobs — must stay ON in every profile so
	// vintage modes get the same solidity guarantees as Modern. This includes
	// stateBaselineEnable: it is a safety default, ON in every profile.
	MV_I(stateBaselineEnable, 1),
	MV_I(pedOpaqueContract, 1),
	MV_I(texAlphaClassify, 1),
	MV_I(defaultCbShaderHygiene, 1),

	// FPS / perf
	MV_I(grassInstancing, 0),
	MV_I(treeWind, 0),
	MV_I(treeFx, 0),
	MV_I(forwardPlusEnable, 0),
	MV_I(pipeChainEnable, 0),
	MV_I(normalBufferEnable, 0),
	MV_I(depthHookEnable, 1),
	MV_I(profilerEnable, 0),
};

// ------------------------------------------------------------
// MODE_XBOX — doc §3.2: same shape as PS2 with the Xbox deltas.
// noiseDetail stays 1 (aap first-run template; OVERRIDES the stale, dead
// xbox_mode.cpp:94 which set 0 — doc A D8).
// ------------------------------------------------------------
static const ModeValue kValuesXBOX[] = {
	// Pipeline identity + tags
	MV_I(pipeline, PIPELINE_XBOX),
	MV_I(buildingPipe, BUILDING_XBOX),
	MV_I(vehiclePipe, CAR_XBOX),
	MV_I(colorFilter, COLORFILTER_PC),
	MV_I(tagsBuildingPipe, BUILDING_XBOX),
	MV_IK(preset, "gamePreset", PRESET_SA_XBOX),
	MV_I(ivMode, 0),

	// Building
	MV_I(ps2ModulateBuilding, 0),
	MV_I(detailMaps, 1),
	MV_I(noiseDetail, 1),
	MV_I(dualPassBuilding, 1),
	MV_I(usePCTimecyc, 1),

	// Vehicle / env map
	MV_I(dualPassVehicle, 0),
	MV_I(envMapSize, 256),
	MV_F(envMapFarClipMult, 1.0f),
	MV_I(envMapUseLODs, 0),
	MV_I(reflectionMsaaLevel, 0),

	// Water
	MV_I(waterStyle, 0),
	// Xbox keeps High, not the inherited Ultra tier-3 depth copies.
	MV_I(waterQuality, 2),		// 2 = High
	MV_I(waterParallaxEnable, 1),
	// Legacy extras the preset writer pinned off (sun glare, water drops)
	MV_IK(doglare, "sunGlare", 0),
	MV_I(neoWaterDrops, 0),

	// PostFX
	MV_I(radiosity, 1),
	MV_I(doRadiosity, 1),
	MV_I(grainFilter, 0),
	MV_I(infraredVision, 0),
	MV_I(nightVision, 0),
	MV_I(ssaoEnable, 0),
	MV_I(smaaEnable, 0),
	MV_I(motionBlurEnable, 0),
	MV_I(sssPostProcessEnable, 0),
	MV_I(skinEnhanceEnable, 0),
	MV_I(hairEnhanceEnable, 0),
	MV_I(vegetationEnhanceEnable, 0),
	MV_I(normalMapEnable, 0),
	MV_I(bloomEnable, 0),
	MV_I(dofEnable, 0),
	MV_I(sunShaftsEnable, 0),
	MV_I(fxaaEnable, 0),
	MV_F(grainIntensity, 0.0f),
	MV_F(caIntensity, 0.0f),
	MV_I(vcsTrails, 0),
	MV_I(colorFilterEnable, 1),
	MV_F(rgb1Mult, 1.0f),
	MV_F(rgb2Mult, 1.0f),
	MV_BK(bYCbCrFilter, "YCbCrCorrection", false),
	MV_F(ivDesaturation, 0.0f),
	MV_F(ivGamma, 1.0f),
	MV_F(ivVignetteIntensity, 0.0f),
	MV_F(ivBloomIntensity, 0.0f),

	// Coronas (PC z-test on; identity sun-corona gains — doc TBD-probe)
	MV_I(coronaZtest, 1),
	MV_F(sunCoronaIntensity, 1.0f),
	MV_F(sunCoreIntensity, 1.0f),
	MV_F(sunStreakIntensity, 1.0f),
	MV_F(sunStreakSize, 1.0f),

	// Alpha / state
	MV_IK(ps2ModulateGlobal, "ps2Modulate", 0),
	MV_I(ps2ModulateGrass, 0),
	MV_IK(dualPassGlobal, "dualPass", 1),
	MV_I(dualPassPed, 1),
	MV_I(dualPassGrass, 1),
	MV_I(dualPassDefault, 1),
	MV_I(grassAddAmbient, 0),
	MV_IK(backfaceCull, "grassBackfaceCull", 1),
	MV_IK(fixGrassPlacement, "grassFixPlacement", 0),
	MV_I(pedShadows, -1),
	MV_I(stencilShadows, -1),
	MV_I(zwriteThreshold, 128),
	MV_I(zwriteThresholdGrass, 128),
	MV_I(zwriteThresholdPed, 128),
	// Safety systems, not look knobs — must stay ON in every profile so
	// vintage modes get the same solidity guarantees as Modern. This includes
	// stateBaselineEnable: it is a safety default, ON in every profile.
	MV_I(stateBaselineEnable, 1),
	MV_I(pedOpaqueContract, 1),
	MV_I(texAlphaClassify, 1),
	MV_I(defaultCbShaderHygiene, 1),

	// FPS / perf
	MV_I(grassInstancing, 0),
	MV_I(treeWind, 0),
	MV_I(treeFx, 0),
	MV_I(forwardPlusEnable, 0),
	MV_I(pipeChainEnable, 0),
	MV_I(normalBufferEnable, 0),
	MV_I(depthHookEnable, 1),
	MV_I(profilerEnable, 0),
};

// ------------------------------------------------------------
// MODE_PC_VINTAGE — doc §3.3. aap maps "PC" to BUILDING_XBOX
// (backup_original/src/main.cpp:1273-1277) and keeps CAR_PC for vehicles;
// that pair is what makes legacy PC distinct from Xbox. usePCTimecyc is
// marked TBD-probe in the doc but its table value is 1.
// ------------------------------------------------------------
static const ModeValue kValuesPCVintage[] = {
	// Pipeline identity + tags
	MV_I(pipeline, PIPELINE_XBOX),	// derived family: legacy PC has no PIPELINE_*
	MV_I(buildingPipe, BUILDING_XBOX),
	MV_I(vehiclePipe, CAR_PC),
	MV_I(colorFilter, COLORFILTER_PC),
	MV_I(tagsBuildingPipe, BUILDING_XBOX),
	MV_IK(preset, "gamePreset", PRESET_SA_PC),
	MV_I(ivMode, 0),

	// Building
	MV_I(ps2ModulateBuilding, 0),
	MV_I(detailMaps, 1),
	MV_I(noiseDetail, 1),
	MV_I(dualPassBuilding, 1),
	MV_I(usePCTimecyc, 1),

	// Vehicle / env map
	MV_I(dualPassVehicle, 1),
	MV_I(envMapSize, 256),
	MV_F(envMapFarClipMult, 1.0f),

	// Water — legacy PC keeps its inherited style, but must not inherit the
	// Ultra tier-3 depth copies from a Modern/IV snapshot.
	MV_I(waterQuality, 2),		// 2 = High
	MV_I(waterParallaxEnable, 1),

	// PostFX
	MV_I(radiosity, 1),
	MV_I(doRadiosity, 1),
	MV_I(grainFilter, 1),
	MV_I(infraredVision, 0),
	MV_I(nightVision, 0),
	MV_I(ssaoEnable, 0),
	MV_I(smaaEnable, 0),
	MV_I(motionBlurEnable, 0),
	MV_I(sssPostProcessEnable, 0),
	MV_I(skinEnhanceEnable, 0),
	MV_I(hairEnhanceEnable, 0),
	MV_I(vegetationEnhanceEnable, 0),
	MV_I(vcsTrails, 0),
	// Legacy extras the preset writer pinned off (sun glare, water drops)
	MV_IK(doglare, "sunGlare", 0),
	MV_I(neoWaterDrops, 0),
	// IV-grade neutrals: legacy PC does not use the IV grade path
	MV_F(ivDesaturation, 0.0f),
	MV_F(ivGamma, 1.0f),
	MV_F(ivVignetteIntensity, 0.0f),
	MV_F(ivBloomIntensity, 0.0f),

	// Coronas (aap PC path forces the PC z-test)
	MV_I(coronaZtest, 1),

	// Alpha / state
	MV_IK(ps2ModulateGlobal, "ps2Modulate", 0),
	MV_I(ps2ModulateGrass, 0),
	MV_IK(dualPassGlobal, "dualPass", 1),
	MV_I(dualPassPed, 1),
	MV_I(dualPassGrass, 1),
	MV_I(dualPassDefault, 1),
	MV_I(pedShadows, -1),
	MV_I(stencilShadows, -1),
	// Safety systems, not look knobs — must stay ON in every profile so
	// vintage modes get the same solidity guarantees as Modern.
	MV_I(pedOpaqueContract, 1),
	// stateBaselineEnable is a safety default, not user-opt-in: it stays ON
	// in every profile (overriding authored effect state is intentional).
	MV_I(stateBaselineEnable, 1),
	MV_I(texAlphaClassify, 1),
	MV_I(defaultCbShaderHygiene, 1),

	// FPS / perf
	MV_I(forwardPlusEnable, 0),
};

// ------------------------------------------------------------
// MODE_GTAIV — doc §3.4 + the 2026-10-25 GTA IV parity audit (§3.1, §5.2 P1).
//
// OWNS: pipeline identity/tags, the PS2-era building/vehicle contract, the
// four safety flags, the IV water style, and the SM3 postfx stack that was
// already implemented but never wired (bloom, DoF, sun shafts, height fog,
// film grain, CA, motion blur, SSAO, SMAA, tonemap) plus the two buffers
// those passes consume (normal for SSAO, velocity for MB). Every gate is
// owned explicitly so IV mode never inherits a vintage/Modern-only effect
// from the pre-mode snapshot.
//
// INHERITS (user-tuned, keep snapshot values): only ivVignetteRadius /
// ivVignetteContrast (inert while ivVignetteIntensity is 0) and coronaZtest
// (doc says TBD-probe — do not invent). Every other IV knob is owned below.
//
// The IV look does NOT come from the colour filter: COLORFILTER_GTAIV is a
// pure bypass (src/render/postfx.cpp:1842-1850) and the grade is DrawIVGrade
// (src/render/postfx_colourfilter.cpp:1354-1547), whose embedded PS keeps
// reading the iv* sliders every frame. That is exactly why the grade sliders
// are owned here: a fresh-INI ULTRA base (ivExposure 2.5, ivSaturation 0.3,
// ivCurves 0.15, ivBloomIntensity 0.05) would otherwise leak through the
// bypass into the IV image and overexpose/wash it.
//
// Consequence: ivVignetteIntensity/ivBloomIntensity are pinned to 0, so the
// standalone vignette (also 0 below) and DrawIVGrade's cheap 4-tap bloom
// cannot double up on top of the full bloom pyramid owned above.
// ------------------------------------------------------------
static const ModeValue kValuesGTAIV[] = {
	// Pipeline identity + tags
	MV_I(pipeline, PIPELINE_GTAIV),
	MV_I(buildingPipe, BUILDING_GTAIV),
	MV_I(vehiclePipe, CAR_GTAIV),
	MV_I(colorFilter, COLORFILTER_GTAIV),
	MV_IK(preset, "gamePreset", PRESET_IV_PC),
	MV_I(ivMode, 1),

	// Building
	MV_I(ps2ModulateBuilding, 0),
	MV_I(detailMaps, 0),
	MV_I(noiseDetail, 0),
	MV_I(dualPassBuilding, 0),

	// Vehicle / env map
	MV_I(dualPassVehicle, 0),
	MV_I(envMapSize, 256),
	MV_F(envMapFarClipMult, 1.0f),

	// PostFX base toggles (SA-era effects IV does not use)
	MV_I(radiosity, 0),
	MV_I(doRadiosity, 0),
	MV_I(grainFilter, 0),		// legacy SA grain path keeps the IV preset
	MV_I(infraredVision, 0),
	MV_I(nightVision, 0),
	MV_I(vcsTrails, 0),		// trails are VC-only, IV has none

	// PostFX: bloom (Jimenez pyramid, audit §5.2 P1). The IV path is LDR, so
	// the bright-pass threshold must stay below 1.0.
	MV_I(bloomEnable, 1),
	MV_I(bloomQuality, 2),		// 4 mips — cost/quality mid tier
	MV_F(bloomThreshold, 0.90f),	// IV probe — audit §5.2 P1 LDR threshold
	MV_F(bloomIntensity, 0.35f),	// IV probe — audit §5.2 P1

	// PostFX: depth of field (4-pass CoC -> bokeh -> tent -> combine)
	MV_I(dofEnable, 1),
	MV_F(dofFocusDistance, 20.0f),	// IV probe — documented default focal plane
	MV_F(dofFocusRange, 15.0f),	// IV probe — documented default in-focus window
	MV_F(dofBokehRadius, 4.0f),	// IV probe — documented default bokeh disc
	MV_F(dofMaxBlur, 0.5f),		// IV probe — documented default max CoC mix

	// PostFX: atmosphere
	MV_I(sunShaftsEnable, 1),
	MV_F(sunShaftsDensity, 0.84f),	// IV probe — audit §5.2 P1
	MV_F(sunShaftsDecay, 0.97f),	// IV probe — audit §5.2 P1
	MV_I(godRaysEnable, 0),		// legacy DrawGodRays pinned off — SunShafts is the IV pass
	MV_I(heightFogEnable, 1),

	// PostFX: lens finishing
	MV_I(grainEnable, 1),		// legacy grain gate (Grain_PS2) — keep SA grain available
	MV_F(grainIntensity, 0.03f),	// IV probe — audit §5.2 P1 subtle IGN film grain
	MV_F(caIntensity, 0.12f),	// IV probe — audit §5.2 P1
	MV_F(vignetteIntensity, 0.0f),	// IV vignette is DrawIVGrade's (ivVignette*); pin the
					// standalone pass off so the two never double up
	MV_I(motionBlurEnable, 1),
	MV_F(motionBlurStrength, 0.35f),	// IV probe — Burnout-style radial MB, mid strength

	// PostFX: ambient occlusion + AA (SMAA wins when FXAA is also on)
	MV_I(ssaoEnable, 1),
	MV_F(ssaoRadius, 1.0f),		// IV probe — audit §5.2 P1
	MV_F(ssaoPower, 2.0f),		// IV probe — audit §5.2 P1
	MV_I(smaaEnable, 1),
	MV_I(fxaaEnable, 0),		// SMAA covers the AA slot; FXAA stays the cheap fallback

	// PostFX buffers the gates above consume — own them so SSAO/MB cannot go
	// inert by inheriting a snapshot where the user disabled the buffer.
	MV_I(normalBufferEnable, 1),
	MV_I(velocityBufferEnable, 1),

	// PostFX: tonemap — only ColourFilter_Modern consumes these and
	// COLORFILTER_GTAIV bypasses it, but owning them keeps IV from inheriting
	// an unrelated preset if/when the IV path gets its own tonemap (audit P7).
	MV_I(tonemapAutoExposure, 1),
	MV_I(tonemapOperator, 1),	// IV probe — ACES 1.3 (0=filmic .. 5=GT7)

	// IV grade sliders (DrawIVGrade, live every frame; ivMode=1 above makes
	// the pass run). Owned so the grade never reads a leaked ULTRA base.
	MV_F(ivExposure, 2.5f),		// shipped IV preset value; DrawIVGrade applies
					// pow(ex,1/2.2) => 1.52x display-space, not raw 2.5x
	MV_F(ivSaturation, 0.3f),	// IV look: moderate grade desaturation
	MV_F(ivCurves, 1.0f),		// neutral curves (1.0 = no S-curve push)
	MV_F(ivDesaturation, 0.0f),	// no extra luma desat on top of ivSaturation
	MV_F(ivGamma, 1.0f),		// neutral gamma (grade input is display-referred)
	MV_F(ivVignetteIntensity, 0.0f),	// pin the grade vignette off — the
					// standalone pass owns vignetting in this profile
	MV_F(ivBloomIntensity, 0.0f),	// grade's 4-tap bloom off — full bloom
					// pyramid above already runs

	// Water — GTA IV style entry (Water_IV.cso, main_iv). quality >= 2 is what
	// turns on the scene/depth copy the IV refraction path needs.
	MV_I(waterStyle, 1),		// 1 = GTA IV
	MV_I(waterParallaxEnable, 1),
	MV_I(waterQuality, 3),		// 3 = Ultra

	// Alpha / state + screen extras
	MV_IK(ps2ModulateGlobal, "ps2Modulate", 0),
	MV_I(ps2ModulateGrass, 0),
	MV_IK(dualPassGlobal, "dualPass", 0),
	MV_I(dualPassPed, 0),
	MV_I(dualPassGrass, 0),
	MV_I(dualPassDefault, 0),
	// Safety systems, not look knobs — must stay ON in every profile so
	// vintage modes get the same solidity guarantees as Modern. This includes
	// stateBaselineEnable: it is a safety default, ON in every profile.
	MV_I(stateBaselineEnable, 1),
	MV_I(texAlphaClassify, 1),
	MV_I(pedOpaqueContract, 1),
	MV_I(defaultCbShaderHygiene, 1),
	MV_IK(doglare, "sunGlare", 0),
	MV_I(neoWaterDrops, 0),
};

// ------------------------------------------------------------
// MODE_GTAV — doc §3.5, probe-gated. There is no GTA V pipe set in the tree
// (Pipeline stops at PIPELINE_GTAIV); the only implemented V renderer is the
// V water shader. The profile therefore owns ONLY the water knobs.
//
// DELIBERATE: pipeline / buildingPipe / vehiclePipe / colorFilter / ivMode
// INTENTIONALLY inherit the entering snapshot. Forcing pipe=PIPELINE_XBOX +
// building=XBOX + vehicle=XBOX + colorFilter=PC here would silently downgrade
// the world renderer when switching Modern (PIPELINE_PBR) -> V, because V has
// no pipe implementation of its own — instantiating a vintage pipe just to
// make the mode look "owned" would change the look, not preserve it. V mode
// means "current renderer + V water", and the legacy-effect gates inherit for
// the same reason.
// ------------------------------------------------------------
static const ModeValue kValuesGTAV[] = {
	MV_I(waterStyle, 2),	// 2 = GTA V (Water_V)
	MV_I(waterQuality, 3),	// 3 = Ultra
	MV_I(waterParallaxEnable, 1),	// water pipe master gate — owned so V water
					// cannot go inert on a snapshot with it disabled
};

// ------------------------------------------------------------
// MODE_MODERN — doc §3.6 / qualityPreset=3 (ULTRA) semantics. NOTE:
// `qualityPreset` itself is a main.cpp file-static (g_qualityPreset), not a
// Config field; this profile applies the ULTRA feature values directly.
// ------------------------------------------------------------
static const ModeValue kValuesModern[] = {
	// Pipeline identity + tags
	MV_I(pipeline, PIPELINE_PBR),
	MV_I(buildingPipe, BUILDING_PBR),
	MV_I(vehiclePipe, CAR_MODERN),
	MV_I(colorFilter, COLORFILTER_MODERN),
	MV_IK(preset, "gamePreset", PRESET_SKYGFXPLUSULTRAMAXDELUXE),
	MV_I(ivMode, 0),

	// Building
	MV_I(ps2ModulateBuilding, 0),
	MV_I(detailMaps, 1),
	MV_I(noiseDetail, 1),
	MV_I(dualPassBuilding, 1),
	MV_I(usePCTimecyc, 1),
	// ULTRA does not write normalMapIntensity; pin the INI/save default
	MV_F(normalMapIntensity, 1.0f),
	MV_I(normalMapEnable, 1),	// ULTRA normal-map pipeline gate

	// Vehicle / env map
	MV_I(dualPassVehicle, 0),
	MV_I(envMapSize, 512),
	MV_F(envMapFarClipMult, 2.0f),
	MV_F(envShininessMult, 1.0f),
	MV_F(envSpecularityMult, 1.0f),
	MV_F(envPower, 128.0f),
	MV_F(envFresnel, 0.4f),
	MV_F(vehEnvIntensity, 0.4f),	// clampEnvSliderRanges pins [0,0.4]; owning
					// 1.0 only produced an immediate clamp
	MV_I(vehPBRLayers, 255),

	// PostFX
	MV_I(radiosity, 1),
	MV_I(doRadiosity, 1),
	MV_I(grainFilter, 1),
	MV_I(infraredVision, 0),
	MV_I(nightVision, 0),
	MV_I(ssaoEnable, 1),
	MV_F(ssaoRadius, 1.0f),
	MV_F(ssaoPower, 2.0f),
	MV_F(ssaoKernelSize, 16.0f),
	MV_I(ssaoSampleCount, 16),
	MV_I(smaaEnable, 1),
	MV_I(motionBlurEnable, 1),
	MV_F(motionBlurStrength, 0.4f),
	MV_F(motionBlurRadial, 0.2f),
	MV_F(motionBlurSpeedFactor, 0.3f),
	MV_I(motionBlurCameraAware, 1),
	MV_I(sssPostProcessEnable, 1),
	MV_F(sssPostProcessStrength, 0.2f),
	MV_F(sssPostProcessRadius, 3.0f),
	MV_I(skinEnhanceEnable, 1),
	MV_F(skinWrapFactor, 0.4f),
	MV_F(skinSpecularPower, 24.0f),
	MV_F(skinSpecularStrength, 0.2f),
	MV_F(skinSSSStrength, 0.3f),
	MV_I(hairEnhanceEnable, 1),
	MV_F(hairAnisotropicPower, 48.0f),
	MV_F(hairAnisotropicStrength, 0.4f),
	MV_F(hairSSSStrength, 0.15f),
	MV_I(vegetationEnhanceEnable, 1),
	MV_F(vegetationSSSStrength, 0.2f),
	MV_F(vegetationAmbientBoost, 1.3f),

	// PostFX gates the ULTRA base relies on. Without owned rows a snapshot
	// taken from a vintage profile would leave these OFF and silently strip
	// the Modern lens/atmosphere stack.
	MV_I(bloomEnable, 1),
	MV_I(dofEnable, 1),
	MV_I(sunShaftsEnable, 1),
	MV_I(grainEnable, 1),
	MV_I(heightFogEnable, 1),
	MV_F(caIntensity, 0.10f),	// subtle chromatic aberration
	MV_F(vignetteIntensity, 0.15f),	// subtle finishing vignette
	MV_I(fxaaEnable, 0),		// SMAA owns the AA slot

	// Water — V style surface at Ultra, owned so a vintage/PS2 snapshot cannot
	// leave the base water draw (or a lower depth-copy tier) in place.
	MV_I(waterStyle, 2),		// 2 = GTA V (Water_V)
	MV_I(waterQuality, 3),		// 3 = Ultra
	MV_I(waterParallaxEnable, 1),	// water pipe master gate

	// IV grade path used by the Modern colour filter. NOTE: ivMode=0 above
	// makes DrawIVGrade return early, so every iv* row below is INERT in
	// Modern. Kept (harmless) so the values are deterministic if ivMode is
	// ever forced on by the user.
	MV_F(ivDesaturation, 0.15f),
	MV_F(ivGamma, 1.0f),
	MV_F(ivSaturation, 0.3f),
	MV_F(ivCurves, 1.0f),
	MV_F(ivVignetteIntensity, 0.15f),
	MV_F(ivVignetteRadius, 0.70f),
	MV_F(ivVignetteContrast, 1.5f),
	MV_F(ivBloomIntensity, 0.05f),
	MV_F(ivExposure, 2.5f),

	// Tonemap / IBL / glint
	MV_I(tonemapAutoExposure, 1),
	MV_F(tonemapAdaptSpeed, 0.12f),
	MV_F(tonemapKeyStrength, 1.0f),
	MV_F(tonemapMinExposure, 0.5f),
	MV_F(tonemapMaxExposure, 1.0f),
	MV_F(pbrIblAmbientWeight, 0.5f),
	MV_F(vehEnvGlint, 0.0f),
	MV_F(vehChromeEnvThreshold, 0.0f),

	MV_I(vcsTrails, 0),
	// Legacy extras the preset writer pinned off (sun glare, water drops)
	MV_IK(doglare, "sunGlare", 0),
	MV_I(neoWaterDrops, 0),

	// Coronas (modern z-tested path)
	MV_I(coronaZtest, 1),

	// Alpha / state
	MV_IK(ps2ModulateGlobal, "ps2Modulate", 0),
	MV_I(ps2ModulateGrass, 0),
	MV_IK(dualPassGlobal, "dualPass", 1),
	MV_I(dualPassPed, 1),
	MV_I(dualPassGrass, 1),
	MV_I(dualPassDefault, 1),
	// stateBaselineEnable is a safety default, not user-opt-in: it stays ON
	// in every profile (overriding authored effect state is intentional).
	MV_I(stateBaselineEnable, 1),
	MV_I(texAlphaClassify, 1),
	MV_I(pedOpaqueContract, 1),
	MV_I(defaultCbShaderHygiene, 1),

	// FPS / perf
	MV_I(forwardPlusEnable, 1),
	MV_I(pipeChainEnable, 0),
	MV_I(normalBufferEnable, 1),
	MV_I(velocityBufferEnable, 1),
	MV_I(depthHookEnable, 1),
	MV_I(grassInstancing, 0),
	MV_I(treeWind, 0),
	MV_I(treeFx, 0),
	MV_I(profilerEnable, 1),
};

// ------------------------------------------------------------
// MODE_MOBILE — a first-class 8-chip picker mode (59+ owned fields): aap's
// mobile lock is building=Xbox + vehicle=Mobile + COLORFILTER_MOBILE, so this
// profile owns the full vintage field set and never inherits an effect from
// the pre-mode snapshot. Two deliberate choices:
//   - noiseDetail is 0 here (always-on detail noise is an Xbox/PC trait; the
//     mobile pipe does its own diffuse detail in the shader).
//   - `preset` is NOT owned: GamePreset has no mobile id, and the pipeline/
//     pipe triple is the identity (main.cpp derives PIPELINE_MOBILE from
//     building=Xbox + vehicle=Mobile, rule 4).
// ------------------------------------------------------------
static const ModeValue kValuesMobile[] = {
	// Pipeline identity + tags
	MV_I(pipeline, PIPELINE_MOBILE),
	MV_I(buildingPipe, BUILDING_XBOX),	// Mobile uses the Xbox building pipe
	MV_I(vehiclePipe, CAR_MOBILE),
	MV_I(colorFilter, COLORFILTER_MOBILE),
	MV_I(tagsBuildingPipe, BUILDING_XBOX),
	MV_I(ivMode, 0),

	// Building
	MV_I(ps2ModulateBuilding, 0),
	MV_I(detailMaps, 1),
	MV_I(noiseDetail, 0),
	MV_I(dualPassBuilding, 1),
	MV_I(usePCTimecyc, 1),

	// Vehicle / env map
	MV_I(dualPassVehicle, 1),
	MV_I(envMapSize, 256),
	MV_F(envMapFarClipMult, 1.0f),

	// Water
	MV_I(waterStyle, 0),
	// Mobile uses the Medium tier; own it so the Ultra tier-3 depth copies are
	// not inherited from the pre-mode snapshot.
	MV_I(waterQuality, 1),		// 1 = Medium
	MV_I(waterParallaxEnable, 1),
	// Legacy extras the preset writer pinned off (sun glare, water drops)
	MV_IK(doglare, "sunGlare", 0),
	MV_I(neoWaterDrops, 0),

	// PostFX
	MV_I(radiosity, 0),
	MV_I(doRadiosity, 0),
	MV_I(grainFilter, 0),
	MV_I(infraredVision, 0),
	MV_I(nightVision, 0),
	MV_I(ssaoEnable, 0),
	MV_I(smaaEnable, 0),
	MV_I(motionBlurEnable, 0),
	MV_I(sssPostProcessEnable, 0),
	MV_I(skinEnhanceEnable, 0),
	MV_I(hairEnhanceEnable, 0),
	MV_I(vegetationEnhanceEnable, 0),
	MV_I(normalMapEnable, 0),
	MV_I(bloomEnable, 0),
	MV_I(dofEnable, 0),
	MV_I(sunShaftsEnable, 0),
	MV_I(fxaaEnable, 0),
	MV_I(vcsTrails, 0),
	// IV-grade neutrals: Mobile does not use the IV grade path
	MV_F(ivDesaturation, 0.0f),
	MV_F(ivGamma, 1.0f),
	MV_F(ivVignetteIntensity, 0.0f),
	MV_F(ivBloomIntensity, 0.0f),

	// Coronas (PC z-test on; identity sun-corona gains)
	MV_I(coronaZtest, 1),
	MV_F(sunCoronaIntensity, 1.0f),
	MV_F(sunCoreIntensity, 1.0f),
	MV_F(sunStreakIntensity, 1.0f),
	MV_F(sunStreakSize, 1.0f),

	// Alpha / state
	MV_IK(backfaceCull, "grassBackfaceCull", 1),
	MV_I(pedShadows, -1),
	MV_I(stencilShadows, -1),
	// Global gates — Mobile owns all five per-surface dualPass flags, so the
	// gate must be pinned ON or the profile can inherit dualPassGlobal=0 and
	// silently disable its own dual pass.
	MV_IK(ps2ModulateGlobal, "ps2Modulate", 0),
	MV_I(ps2ModulateGrass, 0),
	MV_IK(dualPassGlobal, "dualPass", 1),
	MV_I(dualPassPed, 1),
	MV_I(dualPassGrass, 1),
	MV_I(dualPassDefault, 1),
	MV_I(zwriteThreshold, 128),
	MV_I(zwriteThresholdGrass, 128),
	MV_I(zwriteThresholdPed, 128),
	// Safety systems, not look knobs — must stay ON in every profile so
	// vintage modes get the same solidity guarantees as Modern.
	MV_I(stateBaselineEnable, 1),
	MV_I(texAlphaClassify, 1),
	MV_I(pedOpaqueContract, 1),
	MV_I(defaultCbShaderHygiene, 1),

	// FPS / perf
	MV_I(grassInstancing, 0),
	MV_I(treeWind, 0),
	MV_I(treeFx, 0),
	MV_I(forwardPlusEnable, 0),
	MV_I(pipeChainEnable, 0),
	MV_I(normalBufferEnable, 0),
	MV_I(depthHookEnable, 1),
	MV_I(profilerEnable, 0),
};

// ------------------------------------------------------------
// OWNED sky_physics.ini ROWS (m0971, parallel to the MV_ tables above —
// PhysicsCfg keys cannot be Config member pointers, see the file header).
//
// GTA IV OWNS the crash-FX subset + the softbody backend: "flicking GTA IV
// mode enables the physics" (user need). The vintage/Modern profiles pin
// crashFxEnable=0 so switching AWAY from IV always disables the master gate
// (field-level restore alone would put back a stale user=1 base).
// softbodyBackend is a STRING row — the MV_ table has no string support
// (ModeValue only binds int/float/bool Config fields), which is exactly why
// these rows live in ModePhysicsValue instead. backend stays user-set under
// every other profile (restored to base on switch-out).
// ------------------------------------------------------------
static const ModePhysicsValue kPhysicsGTAIV[] = {
	MP_I("crashFxEnable", 1),
	MP_I("crashFxTirePops", 1),
	MP_I("crashFxBurnoutSmoke", 1),
	MP_S("softbodyBackend", "native"),
};

// Shared "no crash FX" pin for every non-IV profile (one table, six rows of
// intent — applied to PS2 / Xbox / Legacy PC / Modern / Mobile; GTAV is
// probe-gated water-only and owns nothing here).
static const ModePhysicsValue kPhysicsCrashFxOff[] = {
	MP_I("crashFxEnable", 0),
};

// ONE table, indexed by ModeId. NONE and CUSTOM own no fields (an empty
// delta); entering either is a pure per-slot base snapshot.
static const ModeProfile g_modeProfiles[] = {
	/* MODE_NONE       */ { MODE_NONE,       "None",       "None",   nullptr, 0, nullptr, 0 },
	/* MODE_PS2        */ { MODE_PS2,        "PS2",        "PS2",    kValuesPS2,     MV_COUNT(kValuesPS2),     kPhysicsCrashFxOff, MP_COUNT(kPhysicsCrashFxOff) },
	/* MODE_XBOX       */ { MODE_XBOX,       "Xbox",       "Xbox",   kValuesXBOX,    MV_COUNT(kValuesXBOX),    kPhysicsCrashFxOff, MP_COUNT(kPhysicsCrashFxOff) },
	/* MODE_PC_VINTAGE */ { MODE_PC_VINTAGE, "Legacy PC",  "PC",     kValuesPCVintage, MV_COUNT(kValuesPCVintage), kPhysicsCrashFxOff, MP_COUNT(kPhysicsCrashFxOff) },
	/* MODE_GTAIV      */ { MODE_GTAIV,      "GTA IV",     "GTAIV",  kValuesGTAIV,   MV_COUNT(kValuesGTAIV),   kPhysicsGTAIV, MP_COUNT(kPhysicsGTAIV) },
	/* MODE_GTAV       */ { MODE_GTAV,       "GTA V",      "GTAV",   kValuesGTAV,    MV_COUNT(kValuesGTAV),    nullptr, 0 },
	/* MODE_MODERN     */ { MODE_MODERN,     "Modern",     "Modern", kValuesModern,  MV_COUNT(kValuesModern),  kPhysicsCrashFxOff, MP_COUNT(kPhysicsCrashFxOff) },
	/* MODE_CUSTOM     */ { MODE_CUSTOM,     "Custom",     "Custom", nullptr, 0, nullptr, 0 },
	/* MODE_MOBILE     */ { MODE_MOBILE,     "Mobile",     "Mobile", kValuesMobile,  MV_COUNT(kValuesMobile),  kPhysicsCrashFxOff, MP_COUNT(kPhysicsCrashFxOff) },
};
static_assert(sizeof(g_modeProfiles) / sizeof(g_modeProfiles[0]) == MODE_COUNT,
	"g_modeProfiles must have one entry per ModeId (index = ModeId)");

#undef MV_I
#undef MV_F
#undef MV_B
#undef MV_IK
#undef MV_FK
#undef MV_BK
#undef MV_STR
#undef MV_STR_
#undef MV_COUNT

static const ModeProfile *
profileFor(ModeId id)
{
	if((int)id < 0 || id >= MODE_COUNT)
		return nullptr;
	return &g_modeProfiles[id];
}

static const char *
modeName(ModeId id)
{
	const ModeProfile *p = profileFor(id);
	return p ? p->name : "?";
}

// Case-insensitive key compare — matches StrAssoc::get (strcmpi) in main.cpp,
// i.e. the INI reader's key lookup semantics.
static bool
keyEquals(const char *a, const char *b)
{
	if(!a || !b)
		return false;
	return _stricmp(a, b) == 0;
}

// One row's profile value as float (int/RwBool/enum/bool/float alike).
static float
valueAsFloat(const ModeValue *v)
{
	if(v->iField)
		return (float)v->ival;
	if(v->fField)
		return v->fval;
	if(v->bField)
		return v->bval ? 1.0f : 0.0f;
	return 0.0f;
}

// The profile row owning iniKey, or nullptr. Linear scan over a static table
// (at most ~130 rows) — no heap, no map, no allocation.
static const ModeValue *
findOwnedValue(const ModeProfile *p, const char *iniKey)
{
	if(!p || !p->values || !iniKey)
		return nullptr;
	for(int i = 0; i < p->numValues; i++)
		if(keyEquals(p->values[i].iniKey, iniKey))
			return &p->values[i];
	return nullptr;
}

// Apply every owned field to the target Config. The member-pointer table is
// the single field setter; adding a field is one table row.
static void
applyProfile(const ModeProfile *p, Config *c)
{
	if(!p || !c || !p->values)
		return;
	for(int i = 0; i < p->numValues; i++){
		const ModeValue *v = &p->values[i];
		if(v->iField)
			c->*(v->iField) = v->ival;
		else if(v->fField)
			c->*(v->fField) = v->fval;
		else if(v->bField)
			c->*(v->bField) = v->bval;
	}
}

// Revert ONLY the profile-owned fields to the slot base. Non-owned fields are
// never written, so a mode change can never stomp a setting the mode does not
// own. This is the disengage/switch-out half of applyProfile() — same table,
// so ownership can never drift between apply and restore.
static void
restoreProfile(const ModeProfile *p, const Config *base, Config *c)
{
	if(!p || !p->values || !base || !c)
		return;
	for(int i = 0; i < p->numValues; i++){
		const ModeValue *v = &p->values[i];
		if(v->iField)
			c->*(v->iField) = base->*(v->iField);
		else if(v->fField)
			c->*(v->fField) = base->*(v->fField);
		else if(v->bField)
			c->*(v->bField) = base->*(v->bField);
	}
}

// ============================================================
// PHYSICS ROWS (m0971) — the ModePhysicsValue half of a profile.
//
// Same field-level contract as the Config tables, two differences:
//   - the store is the global PhysicsCfg (one sky_physics.ini), so the base
//     is a SINGLE snapshot, not per-config-slot;
//   - only owned keys are ever written, so restoring "the base" means
//     restoring exactly the outgoing profile's key list.
// ============================================================
#define MODE_PHYS_BASE_MAX 16
struct PhysBaseEntry
{
	const char *key;	// static string from the profile tables
	bool isStr;		// string row (softbodyBackend) vs int row
	char sval[16];
	int ival;
};
static PhysBaseEntry s_physBase[MODE_PHYS_BASE_MAX];
static int s_numPhysBase = 0;
static bool s_physBaseValid = false;

static PhysBaseEntry *
physicsBaseFind(const char *key)
{
	if(!key)
		return NULL;
	for(int i = 0; i < s_numPhysBase; i++)
		if(keyEquals(s_physBase[i].key, key))
			return &s_physBase[i];
	return NULL;
}

// Snapshot the live PhysicsCfg values of every key ANY profile owns, before
// the first profile row writes (the pre-mode user state). Re-snapshot is a
// full rebuild — entries are keyed by static table strings, never by slot.
static void
physicsSnapshotBase(void)
{
	s_numPhysBase = 0;
	// (sizeof walk, not MV_COUNT — the MV_ macros are #undef'd above this point)
	for(int p = 0; p < (int)(sizeof(g_modeProfiles) / sizeof(g_modeProfiles[0])); p++){
		const ModeProfile *prof = &g_modeProfiles[p];
		for(int i = 0; prof->physics && i < prof->numPhysics; i++){
			const ModePhysicsValue *v = &prof->physics[i];
			if(physicsBaseFind(v->iniKey))
				continue;	// already captured
			if(s_numPhysBase >= MODE_PHYS_BASE_MAX){
				if(dbglog_throttle("mode_physbase_full"))
					dbglog("[mode] physics base table full (%d) - key '%s' not snapshotted",
						MODE_PHYS_BASE_MAX, v->iniKey);
				continue;
			}
			PhysBaseEntry *e = &s_physBase[s_numPhysBase++];
			e->key = v->iniKey;
			if(v->sval){
				// string rows address the backend token by construction
				// (softbodyBackend is the only string key today)
				e->isStr = true;
				strncpy(e->sval, PhysicsCfgGetBackend(), sizeof(e->sval) - 1);
				e->sval[sizeof(e->sval) - 1] = '\0';
				e->ival = 0;
			}else{
				e->isStr = false;
				e->sval[0] = '\0';
				e->ival = PhysicsCfgGetInt(v->iniKey, 0);
			}
		}
	}
	s_physBaseValid = true;
}

// Revert the OUTGOING profile's owned keys to the base. Keys the outgoing
// profile never owned are left exactly as they are (non-owned never written).
static void
physicsRestoreOwned(const ModeProfile *outgoing)
{
	if(!s_physBaseValid || !outgoing || !outgoing->physics)
		return;
	for(int i = 0; i < outgoing->numPhysics; i++){
		const ModePhysicsValue *v = &outgoing->physics[i];
		const PhysBaseEntry *e = physicsBaseFind(v->iniKey);
		if(!e)
			continue;
		if(v->sval)
			PhysicsCfgSetBackend(e->isStr ? e->sval : "off");
		else
			PhysicsCfgSetInt(v->iniKey, e->ival);
	}
}

// Apply the INCOMING profile's owned rows (the ONE write path for profiles,
// mirroring applyProfile()).
static void
physicsApplyProfile(const ModeProfile *p)
{
	if(!p || !p->physics)
		return;
	for(int i = 0; i < p->numPhysics; i++){
		const ModePhysicsValue *v = &p->physics[i];
		if(v->sval)
			PhysicsCfgSetBackend(v->sval);
		else
			PhysicsCfgSetInt(v->iniKey, v->ival);
	}
	if(p->numPhysics > 0)
		dbglog("[Mode] physics apply %s owned=%d backend=%s",
			p->name, p->numPhysics, PhysicsCfgGetBackend());
}

// Fold direct user edits into the physics base right before a switch (the
// adoptLiveEdits equivalent): an OUTGOING-owned key whose live value differs
// from the profile value was set by a direct writer (menu / PhysicsCfgSet*),
// and an INCOMING-owned key the outgoing profile never wrote is user state by
// definition. A->NONE skips this (exact-restore semantics).
static void
physicsAdoptLiveEdits(const ModeProfile *outgoing, const ModeProfile *incoming)
{
	if(!s_physBaseValid)
		return;
	for(int side = 0; side < 2; side++){
		const ModeProfile *p = side ? incoming : outgoing;
		if(!p || !p->physics)
			continue;
		for(int i = 0; i < p->numPhysics; i++){
			const ModePhysicsValue *v = &p->physics[i];
			// Does the OUTGOING profile own this key? (both sides need it:
			// outgoing side adopts only on divergence, incoming side never
			// re-adopts a key the outgoing pass already handled)
			bool outgoingOwns = false;
			if(outgoing && outgoing->physics){
				for(int j = 0; j < outgoing->numPhysics && !outgoingOwns; j++)
					outgoingOwns = keyEquals(outgoing->physics[j].iniKey, v->iniKey);
			}
			if(!side){
				// outgoing-owned keys: adopt only when the live value
				// diverged from the profile (a direct writer touched it)
				if(outgoingOwns){
					bool matches = v->sval
						? _stricmp(PhysicsCfgGetBackend(), v->sval) == 0
						: PhysicsCfgGetInt(v->iniKey, v->ival) == v->ival;
					if(matches)
						continue;
				}
			}else{
				// incoming side: keys the outgoing profile owned were handled
				// above; only NEVER-owned keys are user state by definition
				if(outgoingOwns)
					continue;
			}
			PhysBaseEntry *e = physicsBaseFind(v->iniKey);
			if(!e)
				continue;
			if(v->sval){
				strncpy(e->sval, PhysicsCfgGetBackend(), sizeof(e->sval) - 1);
				e->sval[sizeof(e->sval) - 1] = '\0';
			}else{
				e->ival = PhysicsCfgGetInt(v->iniKey, e->ival);
			}
		}
	}
}

// True when every physics row still holds the profile's value (the physics
// half of configMatchesProfile). Used by ModeManager_RefreshBase.
static bool
physicsMatchesProfile(const ModeProfile *p)
{
	if(!p || !p->physics || p->numPhysics <= 0)
		return true;	// nothing owned — nothing to prove
	for(int i = 0; i < p->numPhysics; i++){
		const ModePhysicsValue *v = &p->physics[i];
		if(v->sval){
			if(_stricmp(PhysicsCfgGetBackend(), v->sval) != 0)
				return false;
		}else if(PhysicsCfgGetInt(v->iniKey, v->ival) != v->ival)
			return false;
	}
	return true;
}

// True when the live field still holds the profile's value (no direct writer
// touched it since ApplyMode).
static bool
fieldMatchesProfile(const ModeValue *v, const Config *c)
{
	if(!v || !c)
		return true;
	if(v->iField)
		return c->*(v->iField) == v->ival;
	if(v->fField)
		return c->*(v->fField) == v->fval;
	if(v->bField)
		return c->*(v->bField) == v->bval;
	return true;
}

// Do two table rows address the same Config field? Member pointers, not INI
// keys, because a renamed key (e.g. gamePreset -> preset) must still match.
static bool
sameField(const ModeValue *a, const ModeValue *b)
{
	if(a->iField || b->iField)
		return a->iField == b->iField;
	if(a->fField || b->fField)
		return a->fField == b->fField;
	return a->bField == b->bField;
}

// Copy one live field into the base.
static void
copyLiveField(const ModeValue *v, Config *base, const Config *live)
{
	if(v->iField)
		base->*(v->iField) = live->*(v->iField);
	else if(v->fField)
		base->*(v->fField) = live->*(v->fField);
	else if(v->bField)
		base->*(v->bField) = live->*(v->bField);
}

// Fold the user's live values into the slot base right before a switch, so the
// base stays "the user's pre-mode state" even when a mode is already latched:
//   - an OUTGOING-owned field whose live value differs from the outgoing
//     profile was set by a direct writer (menu edit, or readIniFile on an F11
//     reload) — that is the user's new preference, so it becomes the base
//     value instead of being reverted to a stale snapshot;
//   - an INCOMING-owned field the OUTGOING profile does not own was never
//     written by a profile, so its live value is user state by definition
//     (this is the reload-with-different-mode case: the new mode's exclusive
//     fields must land in the base, not stay at pre-reload values).
// Fields still holding the outgoing profile's value are residue: the base is
// left untouched for them and restoreProfile() reverts them.
static void
adoptLiveEdits(Config *base, const Config *live,
	const ModeProfile *outgoing, const ModeProfile *incoming)
{
	if(!base || !live)
		return;
	if(outgoing && outgoing->values){
		for(int i = 0; i < outgoing->numValues; i++){
			const ModeValue *v = &outgoing->values[i];
			if(!fieldMatchesProfile(v, live))
				copyLiveField(v, base, live);
		}
	}
	if(incoming && incoming->values){
		for(int i = 0; i < incoming->numValues; i++){
			const ModeValue *v = &incoming->values[i];
			bool outgoingOwns = false;
			if(outgoing && outgoing->values){
				for(int j = 0; j < outgoing->numValues && !outgoingOwns; j++)
					outgoingOwns = sameField(v, &outgoing->values[j]);
			}
			if(!outgoingOwns)
				copyLiveField(v, base, live);
		}
	}
}

// True when every field the profile owns already holds the profile's value,
// i.e. the live Config is exactly `base + profile` as ApplyMode() left it.
// Used by ModeManager_RefreshBase() to tell "ApplyMode already ran this pass
// (base is fresh — do NOT re-snapshot)" from "the INI reload overwrote Config
// with the user's raw values (re-snapshot)". A zero-owned profile (NONE /
// CUSTOM) always returns false: there is nothing to prove, so refresh.
static bool
configMatchesProfile(const Config *c, const ModeProfile *p)
{
	if(!c || !p || !p->values || p->numValues <= 0)
		return false;
	for(int i = 0; i < p->numValues; i++)
		if(!fieldMatchesProfile(&p->values[i], c))
			return false;
	return true;
}

// Index of the live Config inside the game's configs[] array, or -1 when the
// caller bound a Config that is not one of the slots (defensive; in that case
// ApplyMode works on the live Config without a restore base).
static int
configSlot(const Config *c)
{
	if(!c)
		return -1;
	for(int i = 0; i < MODE_CONFIG_SLOTS; i++)
		if(c == &configs[i])
			return i;
	return -1;
}

static void
notifyListeners(ModeId oldMode, ModeId newMode)
{
	for(int i = 0; i < s_numListeners; i++)
		if(s_listeners[i])
			s_listeners[i](oldMode, newMode);
}

// ============================================================
// API
// ============================================================

void
ModeManager_Init(void)
{
	s_mode = MODE_NONE;
	for(int i = 0; i < MODE_CONFIG_SLOTS; i++)
		s_haveBase[i] = false;
	// listeners are intentionally NOT cleared: Init is reload-safe and
	// subscribers registered earlier keep their slots.
	dbglog("[mode] manager init");
}

ModeId
ModeManager_GetMode(void)
{
	return s_mode;
}

bool
ModeManager_GetOwnedValue(const char *configKey, float *out)
{
	const ModeValue *v = findOwnedValue(profileFor(s_mode), configKey);
	if(!v)
		return false;
	if(out)
		*out = valueAsFloat(v);
	return true;
}

const char *
ModeManager_GetAlias(ModeId id)
{
	const ModeProfile *p = profileFor(id);
	return p ? p->iniAlias : "None";
}

bool
ModeManager_IsOwnedKey(const char *configKey)
{
	return findOwnedValue(profileFor(s_mode), configKey) != nullptr;
}

int
ModeManager_GetOwnedCount(ModeId id)
{
	const ModeProfile *p = profileFor(id);
	return (p && p->values) ? p->numValues : 0;
}

void
ModeManager_ClearLatch(void)
{
	if(s_mode == MODE_NONE)
		return;
	ModeId old = s_mode;
	s_mode = MODE_NONE;
	// Per-slot base deliberately NOT restored: the caller just wrote the
	// Config it wants (direct write bypassing ApplyMode). The base flags are
	// left as-is on purpose — with s_mode == MODE_NONE no apply path can
	// resurrect a stale base: the toggle-off branch early-returns on
	// id == s_mode == MODE_NONE, and a NONE->A activation re-captures a
	// fresh base before any profile runs.
	if(dbglog_throttle("modelatch"))
		dbglog("[mode] %s latch cleared (direct Config write, base kept)", modeName(old));
}

void
ModeManager_RegisterListener(void (*fn)(ModeId oldMode, ModeId newMode))
{
	if(!fn)
		return;
	for(int i = 0; i < s_numListeners; i++)
		if(s_listeners[i] == fn)
			return;	// already registered — never notify twice
	if(s_numListeners >= MODEMANAGER_MAX_LISTENERS){
		if(dbglog_first("mode_listener_full"))
			dbglog("[mode] listener table full (%d) - registration dropped",
				MODEMANAGER_MAX_LISTENERS);
		return;
	}
	s_listeners[s_numListeners++] = fn;
}

// Effective pipe triple / colour filter of the live Config. Shared by the
// engage, disengage, refresh and legacy apply receipts so a mismatch (a later
// writer such as pipelineOverride or qualityPreset 0-2) is visible in one
// glance.
static void
logModeReceipt(const char *kind, const char *name, int owned)
{
	dbglog("[Mode] %s %s owned=%d pipes=%d/%d/%d cf=%d",
		kind, name, owned,
		config->pipeline, config->buildingPipe,
		config->vehiclePipe, config->colorFilter);
}

// Disengage receipt: `[Mode] disengage restored=<alias|none> owned=%d ...`.
// `name` is the mode whose owned fields were reverted (or "none").
static void
logDisengageReceipt(const char *name, int owned)
{
	dbglog("[Mode] disengage restored=%s owned=%d pipes=%d/%d/%d cf=%d",
		name, owned,
		config->pipeline, config->buildingPipe,
		config->vehiclePipe, config->colorFilter);
}

void
ApplyMode(ModeId id)
{
	if((int)id < 0 || id >= MODE_COUNT){
		if(dbglog_throttle("mode_oob"))
			dbglog("[mode] ApplyMode(%d) out of range - ignored", (int)id);
		return;
	}
	if(!config){
		if(dbglog_throttle("mode_nocfg"))
			dbglog("[mode] ApplyMode(%d) ignored - no config bound", (int)id);
		return;
	}

	const int slot = configSlot(config);
	const ModeId old = s_mode;

	// --- toggle OFF: clicking the row/chip of the ACTIVE mode ---
	// One implementation for every UI toggle path (same id, or MODE_NONE
	// while a mode is latched): field-level restore from the slot base.
	if(id == old){
		if(old == MODE_NONE)
			return;	// already off — never restore a stale base
		ModeManager_Disengage();
		return;
	}

	const ModeProfile *outgoing = profileFor(old);
	const ModeProfile *incoming = profileFor(id);
	const bool haveBase = (slot >= 0 && s_haveBase[slot]);
	int revertedOwned = 0;

	// --- switch: NONE->A, A->B, or A->NONE ---
	// 1) Refresh the observable part of the base (user edits / reloaded INI
	//    values), then revert the OUTGOING mode's owned fields to it. The
	//    incoming profile is layered on the pre-mode config, never on the
	//    outgoing one — A->B->A cannot stack. Non-owned fields are not
	//    touched (a value the user changed while A was on survives).
	//    A->NONE (a disengage) deliberately skips the adoption: requirement
	//    is an EXACT restore of the base the user's INI produced, so a
	//    profile-influenced live value can never be folded back in.
	if(old != MODE_NONE && haveBase){
		if(id != MODE_NONE)
			adoptLiveEdits(&s_baseConfig[slot], config, outgoing, incoming);
		restoreProfile(outgoing, &s_baseConfig[slot], config);
		revertedOwned = (outgoing && outgoing->values) ? outgoing->numValues : 0;
	}

	// Physics half (m0971): same switch semantics over the global PhysicsCfg
	// base — fold direct user edits in, revert the OUTGOING profile's owned
	// sky_physics.ini keys, and drop the base on a full disengage (A->NONE
	// skips the adoption for the same exact-restore reason as the Config half).
	if(old != MODE_NONE && s_physBaseValid){
		if(id != MODE_NONE)
			physicsAdoptLiveEdits(outgoing, incoming);
		physicsRestoreOwned(outgoing);
		if(id == MODE_NONE)
			s_physBaseValid = false;	// run over — next NONE->A re-captures
	}

	// 2) First activation of a mode run: snapshot the pre-mode Config BEFORE
	//    the profile writes anything. The snapshot is the post-INI / user
	//    state, so a disengage restores those exact values. A base kept
	//    across ModeManager_ClearLatch() is still that user state and is
	//    deliberately NOT overwritten here; only RefreshBase() replaces it.
	if(old == MODE_NONE && id != MODE_NONE && slot >= 0 && !s_haveBase[slot]){
		s_baseConfig[slot] = *config;
		s_haveBase[slot] = true;
	}
	// Physics base: captured before the first profile row writes (independent
	// of the Config slot base — one sky_physics.ini, one snapshot).
	if(old == MODE_NONE && id != MODE_NONE && !s_physBaseValid)
		physicsSnapshotBase();

	// 3) Apply the incoming profile. MODE_NONE/CUSTOM own no fields, so an
	//    A->NONE switch is exactly the restore in step 1.
	applyProfile(incoming, config);
	physicsApplyProfile(incoming);	// m0971: the sky_physics.ini half
	s_mode = id;

	if(id == MODE_NONE){
		if(slot >= 0)
			s_haveBase[slot] = false;	// run over — next NONE->A re-captures
		resetValues();
		if(old != MODE_NONE)
			logDisengageReceipt(ModeManager_GetAlias(old), revertedOwned);
		dbglog("[Mode] apply %s pipes=%d/%d/%d cf=%d iv=%d",
			modeName(MODE_NONE), config->pipeline, config->buildingPipe,
			config->vehiclePipe, config->colorFilter, config->ivMode);
		notifyListeners(old, MODE_NONE);
		return;
	}

	resetValues();
	logModeReceipt("engage", ModeManager_GetAlias(id),
		(incoming && incoming->values) ? incoming->numValues : 0);
	dbglog("[Mode] apply %s pipes=%d/%d/%d cf=%d iv=%d",
		modeName(id), config->pipeline, config->buildingPipe,
		config->vehiclePipe, config->colorFilter, config->ivMode);
	notifyListeners(old, id);

	// TODO(M1b): persist/read `[Modes] active=<iniAlias>` and the sparse
	// `[Mode.<alias>]` overrides. Both live in main.cpp's readIniFile() /
	// saveConfigTo() and are outside this lane's write scope; the iniAlias
	// strings in g_modeProfiles are the stable keys for that reader/writer.
}

void
ModeManager_Disengage(void)
{
	if(!config){
		if(dbglog_throttle("mode_nocfg"))
			dbglog("[mode] disengage ignored - no config bound");
		return;
	}

	const ModeId old = s_mode;
	if(old == MODE_NONE){
		if(dbglog_throttle("mode_disengage_none"))
			logDisengageReceipt("none", 0);
		return;
	}

	const int slot = configSlot(config);
	const ModeProfile *outgoing = profileFor(old);
	int restoredOwned = 0;

	// Field-level restore: every field the mode owns goes back to the base
	// snapshot (captured BEFORE the profile ran) — the user's INI value —
	// and nothing else is written. With no base (non-slot Config or a base
	// dropped by RefreshBase) the Config is left untouched rather than
	// half-restored.
	if(slot >= 0 && s_haveBase[slot] && outgoing && outgoing->values){
		restoreProfile(outgoing, &s_baseConfig[slot], config);
		restoredOwned = outgoing->numValues;
	}else if(dbglog_throttle("mode_nobase")){
		dbglog("[mode] disengage %s has no %d-slot base - Config left as-is",
			ModeManager_GetAlias(old), slot);
	}
	if(slot >= 0)
		s_haveBase[slot] = false;

	// Physics half (m0971): exact restore of the outgoing profile's owned
	// sky_physics.ini keys, then drop the base (next engage re-captures).
	// Independent of the Config slot base, same as on the switch path.
	if(s_physBaseValid){
		physicsRestoreOwned(outgoing);
		s_physBaseValid = false;
	}

	s_mode = MODE_NONE;
	resetValues();
	logDisengageReceipt(ModeManager_GetAlias(old), restoredOwned);
	dbglog("[Mode] apply %s pipes=%d/%d/%d cf=%d iv=%d",
		modeName(MODE_NONE), config->pipeline, config->buildingPipe,
		config->vehiclePipe, config->colorFilter, config->ivMode);
	notifyListeners(old, MODE_NONE);
}

// INI reload hook (main.cpp owner: one line at the END of readIniFile, after
// the `if(iniModeSet && ModeManager_GetMode() != iniMode) ApplyMode(iniMode);`
// block). Two reload shapes must both end with base = post-INI state:
//   - same id latched: ApplyMode was SKIPPED, so Config now holds the raw INI
//     values but s_baseConfig is still the pre-reload snapshot -> re-snapshot
//     and re-layer the active profile over the fresh values.
//   - different id: ApplyMode just ran, folded the reloaded values into the
//     base and re-applied the active profile -> the owned fields match the
//     profile, so re-snapshotting would bake the profile into the base;
//     detect and no-op.
// With no mode active the live Config is the user's state by definition, so a
// snapshot here is always safe (and repairs a base left by ClearLatch).
void
ModeManager_RefreshBase(void)
{
	if(!config){
		if(dbglog_throttle("mode_nocfg"))
			dbglog("[mode] RefreshBase ignored - no config bound");
		return;
	}

	const int slot = configSlot(config);
	const ModeId active = s_mode;
	const ModeProfile *p = profileFor(active);

	if(active == MODE_NONE){
		if(slot >= 0){
			s_baseConfig[slot] = *config;
			s_haveBase[slot] = true;
		}
		// Physics (m0971): with no mode live, the PhysicsCfg store is the
		// user's state by definition — re-snapshot (repairs a base dropped
		// by ClearLatch, same rationale as the Config half).
		physicsSnapshotBase();
		dbglog("[mode] base refreshed slot=%d active=none pipes=%d/%d/%d cf=%d",
			slot, config->pipeline, config->buildingPipe,
			config->vehiclePipe, config->colorFilter);
		return;
	}

	// ApplyMode already produced `base + profile` this pass: base is fresh.
	// Physics half: sky_physics.ini is NOT re-read by readIniFile (one-shot
	// PhysicsCfgLoad), so matching rows mean the base is still the pre-mode
	// snapshot; a mismatch is a direct writer (menu/PhysicsCfgSet*), adopted
	// below exactly like a Config field edit on reload.
	if(slot >= 0 && s_haveBase[slot] && configMatchesProfile(config, p)
	   && physicsMatchesProfile(p)){
		if(dbglog_throttle("mode_refresh_noop"))
			dbglog("[mode] base refresh no-op slot=%d active=%s (already applied this pass)",
				slot, ModeManager_GetAlias(active));
		return;
	}

	// The reload wrote the user's INI values into Config: that is the new
	// pre-mode state. Snapshot it, then re-layer the active profile.
	if(slot >= 0){
		s_baseConfig[slot] = *config;
		s_haveBase[slot] = true;
	}
	applyProfile(p, config);
	if(!physicsMatchesProfile(p)){
		// Direct physics edit while latched: fold it into the base, then
		// re-layer — never bake the profile value into the base.
		physicsAdoptLiveEdits(p, NULL);
		physicsApplyProfile(p);
	}
	resetValues();
	dbglog("[mode] base refreshed slot=%d active=%s owned=%d pipes=%d/%d/%d cf=%d",
		slot, ModeManager_GetAlias(active),
		(p && p->values) ? p->numValues : 0,
		config->pipeline, config->buildingPipe,
		config->vehiclePipe, config->colorFilter);
	notifyListeners(active, active);
}

void
ModeManager_SetMode(ModeId id)
{
	// Compatibility alias: the debug-menu Modes rows and the IV toggle still
	// call this name. ApplyMode is the one implementation.
	if(id == s_mode && s_mode != MODE_NONE){
		// UI toggle path: clicking the LIT chip must disengage the mode.
		// This is explicit here (and equivalent inside ApplyMode) so the
		// intent is provable at the call site; readIniFile() does NOT come
		// through here — it guards with GetMode() != iniMode, which keeps a
		// same-id reload idempotent instead of toggling the mode off.
		ModeManager_Disengage();
		return;
	}
	ApplyMode(id);
}

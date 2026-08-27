// iv_mode.cpp — GTA IV rendering style.
// Emulates the look of GTA IV on Xbox 360 / PC.
//
// Key characteristics:
//   - GTA IV vehicle pipeline (CarPipe class from neoCarpipe.cpp)
//   - GTA IV building pipeline (ivMode shader swap in BUILDING_PS2)
//   - GTA IV color filter (bypass/no-op — relies on timecycle)
//   - IV post-processing (desaturation, vignette, bloom, exposure)
//   - IV mode flag enabled (switches shaders to gtaivVehicleVS/PS)
//   - No radiosity (GTA IV doesn't use it)
//   - No dual-pass
//
// Pipeline config:
//   buildingPipe  = BUILDING_GTAIV  (falls through to BUILDING_PS2 + ivMode)
//   vehiclePipe   = CAR_GTAIV       (no handler — uses CAR_NEO via presets)
//   colorFilter   = COLORFILTER_GTAIV (bypass)
//   ps2Modulate   = 0
//   dualPass      = 0
//   radiosity     = None
//   ivMode        = 1
//
// Shader objects used:
//   Building: gtaivBuildingVS, gtaivBuildingPS (swapped via ivMode in BUILDING_PS2)
//   Vehicle:  CarPipe class shaders (gtaivVehicleVS, gtaivVehiclePS)
//   Color:    None (bypass mode)
//
// References:
//   neoCarpipe.cpp:417-453     — CarPipe::RenderCallback
//   buildingPipe.cpp:290-292   — ivMode shader swap in BUILDING_PS2
//   postfx.cpp:1423-1431       — COLORFILTER_GTAIV (bypass)
//   presets.cpp:58-61          — "IV Xbox 360" / "IV PC" presets

#include "skygfx.h"

// IV mode enables the ivMode flag which causes:
//   - Building pipe: PS2 callback swaps to gtaivBuildingVS/PS shaders
//   - Vehicle pipe: CarPipe::RenderCallback (neoCarpipe.cpp) handles GTA IV rendering
//   - Color filter: bypass (no filter applied, relies on SA timecycle)
//   - PostFX: IV desaturation, vignette, bloom, exposure (postfx.cpp)

static const PresetConfig iv_preset = {
	"GTA IV",
	BUILDING_GTAIV,
	CAR_NEO,                    // GTA IV presets use NEO pipe (CarPipe class)
	COLORFILTER_GTAIV,
	0,                          // ps2ModulateGlobal
	0,                          // dualPassGlobal
	0,                          // radiosity
	0,                          // doRadiosity
	0,                          // vcsTrails
	-1,                         // pedShadows
	-1,                         // stencilShadows
	0,                          // grainFilter
	0,                          // infraredVision
	0,                          // nightVision
	0,                          // ssaoEnable
	0,                          // smaaEnable
	0,                          // smaaPreset
	1,                          // ivMode  ← CRITICAL: enables GTA IV shader path
};

void
IVMode_ApplyDefaults(Config *c)
{
	c->buildingPipe = BUILDING_GTAIV;
	c->vehiclePipe = CAR_NEO;
	c->colorFilter = COLORFILTER_GTAIV;

	c->ps2ModulateGlobal = 0;
	c->ps2ModulateBuilding = 0;
	c->ps2ModulateGrass = 0;
	c->dualPassGlobal = 0;
	c->dualPassBuilding = 0;
	c->dualPassVehicle = 0;
	c->dualPassPed = 0;
	c->dualPassGrass = 0;
	c->dualPassDefault = 0;

	c->radiosity = 0;
	c->doRadiosity = 0;

	c->grainFilter = 0;
	c->infraredVision = 0;
	c->nightVision = 0;

	c->ssaoEnable = 0;
	c->smaaEnable = 0;

	c->ivMode = 1;
	c->ivDesaturation = 1.0f;
	c->ivGamma = 1.0f;
	c->ivSaturation = 0.0f;
	c->ivCurves = 0.0f;
	c->ivVignetteIntensity = 0.75f;
	c->ivVignetteRadius = 0.75f;
	c->ivVignetteContrast = 1.5f;
	c->ivBloomIntensity = 0.0f;
	c->ivExposure = 1.0f;

	c->detailMaps = 0;
	c->stochastic = 0;
	c->doglare = 0;
	c->neoWaterDrops = 0;
	c->envMapSize = 256;
	c->envMapFarClipMult = 1.0f;
}

const PresetConfig*
IVMode_GetPreset(void)
{
	return &iv_preset;
}

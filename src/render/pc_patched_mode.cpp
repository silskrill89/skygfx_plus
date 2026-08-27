// pc_patched_mode.cpp — Enhanced PC rendering (the original skygfx "Best PC").
// Combines the best elements from all versions for the definitive PC experience.
//
// Key characteristics:
//   - Neo vehicle pipeline (CarPipe class — best car rendering)
//   - Xbox building pipe (better than PS2, supports stochastic textures)
//   - VCS color filter (modern, clean look)
//   - Dual-pass enabled (better alpha rendering)
//   - Shader radiosity
//   - SSAO + SMAA enabled (ULTRA preset)
//   - Sun glare, water drops, detail maps
//   - No IV mode (clean PC look)
//
// Pipeline config:
//   buildingPipe  = BUILDING_XBOX
//   vehiclePipe   = CAR_NEO
//   colorFilter   = COLORFILTER_VCS
//   ps2Modulate   = 0
//   dualPass      = 1
//   radiosity     = Shader
//   ivMode        = 0
//
// Shader objects used:
//   Building: xboxBuildingVS, xboxBuildingWindVS, xboxBuildingStochasticPS
//   Vehicle:  CarPipe class (neoCarpipe.cpp) — best car rendering
//   Color:    vcTrailsPS (VCS color filter)
//
// References:
//   buildingPipe.cpp:390-533  — BUILDING_XBOX callback
//   neoCarpipe.cpp:417-453    — CarPipe::RenderCallback (NEO pipe)
//   postfx.cpp:1420-1421      — COLORFILTER_VCS
//   presets.cpp:63-82         — "Best PC (Default)" preset

#include "skygfx.h"

// This is the mode that skygfx was originally designed to provide:
// the best-looking version of SA on PC by combining the best elements
// from PS2, Xbox, PC, and GTA IV.

static const PresetConfig pcpatched_preset = {
	"PC Patched",
	BUILDING_XBOX,
	CAR_NEO,                    // Neo pipe — best car rendering
	COLORFILTER_VCS,            // VCS color filter — modern, clean
	0,                          // ps2ModulateGlobal
	1,                          // dualPassGlobal
	1,                          // radiosity (shader)
	1,                          // doRadiosity
	0,                          // vcsTrails
	-1,                         // pedShadows
	-1,                         // stencilShadows
	1,                          // grainFilter (PC grain)
	0,                          // infraredVision
	0,                          // nightVision
	1,                          // ssaoEnable
	1,                          // smaaEnable
	3,                          // smaaPreset (ULTRA)
	0,                          // ivMode
};

void
PCPatchedMode_ApplyDefaults(Config *c)
{
	c->buildingPipe = BUILDING_XBOX;
	c->vehiclePipe = CAR_NEO;
	c->colorFilter = COLORFILTER_VCS;

	c->ps2ModulateGlobal = 0;
	c->ps2ModulateBuilding = 0;
	c->ps2ModulateGrass = 0;
	c->dualPassGlobal = 1;
	c->dualPassBuilding = 1;
	c->dualPassVehicle = 1;
	c->dualPassPed = 1;
	c->dualPassGrass = 1;
	c->dualPassDefault = 1;

	c->radiosity = 1;       // Shader
	c->doRadiosity = 1;

	c->grainFilter = 1;     // PC grain
	c->infraredVision = 0;
	c->nightVision = 0;

	c->ssaoEnable = 1;
	c->smaaEnable = 1;

	c->ivMode = 0;
	c->ivDesaturation = 0.0f;
	c->ivGamma = 1.0f;
	c->ivVignetteIntensity = 0.0f;
	c->ivBloomIntensity = 0.0f;

	c->detailMaps = 1;
	c->stochastic = 0;
	c->doglare = 1;
	c->neoWaterDrops = 1;
	c->envMapSize = 256;
	c->envMapFarClipMult = 1.5f;
}

const PresetConfig*
PCPatchedMode_GetPreset(void)
{
	return &pcpatched_preset;
}

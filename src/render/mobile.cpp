// mobile.cpp — Mobile rendering style.
// Emulates the look of GTA SA Mobile / GTA III Mobile.
//
// Key characteristics:
//   - Mobile vehicle pipeline (mobileVehiclePipeVS/PS)
//   - Xbox building pipe (shared with Xbox mode)
//   - Mobile color filter (color grading with gradingPS shader)
//   - Colorcycle system for mobile-style color grading
//   - No PS2 modulation
//   - Dual-pass enabled
//   - Shader radiosity
//
// Pipeline config:
//   buildingPipe  = BUILDING_XBOX  (Mobile uses Xbox building pipe)
//   vehiclePipe   = CAR_MOBILE
//   colorFilter   = COLORFILTER_MOBILE
//   ps2Modulate   = 0
//   dualPass      = 1
//   radiosity     = Shader
//   ivMode        = 0
//
// Shader objects used:
//   Building: xboxBuildingVS, xboxBuildingWindVS (same as Xbox)
//   Vehicle:  mobileVehiclePipeVS, mobileVehiclePipePS
//   Color:    gradingPS (mobile color grading)
//
// References:
//   buildingPipe.cpp:390-533  — BUILDING_XBOX callback (shared)
//   vehiclePipe.cpp:1186-1352 — CAR_MOBILE callback
//   postfx.cpp:864-948        — COLORFILTER_MOBILE
//   presets.cpp: (no dedicated mobile preset — added via pipeline system)

#include "skygfx.h"

// Mobile mode uses the Xbox building pipe (which supports spheremap)
// and the mobile vehicle pipeline with color grading.
// The mobile vehicle pipe falls back to simplePS for non-env meshes.

static const PresetConfig mobile_preset = {
	"Mobile",
	BUILDING_XBOX,              // Mobile uses Xbox building pipe
	CAR_MOBILE,
	COLORFILTER_MOBILE,
	0,                          // ps2ModulateGlobal
	1,                          // dualPassGlobal
	1,                          // radiosity
	1,                          // doRadiosity
	0,                          // vcsTrails
	-1,                         // pedShadows
	-1,                         // stencilShadows
	0,                          // grainFilter
	0,                          // infraredVision
	0,                          // nightVision
	0,                          // ssaoEnable
	0,                          // smaaEnable
	0,                          // smaaPreset
	0,                          // ivMode
};

void
MobileMode_ApplyDefaults(Config *c)
{
	c->buildingPipe = BUILDING_XBOX;
	c->vehiclePipe = CAR_MOBILE;
	c->colorFilter = COLORFILTER_MOBILE;

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

	c->grainFilter = 0;
	c->infraredVision = 0;
	c->nightVision = 0;

	c->ssaoEnable = 0;
	c->smaaEnable = 0;
	c->smaaPreset = 0;

	c->ivMode = 0;
	c->ivDesaturation = 0.0f;
	c->ivGamma = 1.0f;
	c->ivVignetteIntensity = 0.0f;
	c->ivBloomIntensity = 0.0f;

	c->detailMaps = 1;
	c->stochastic = 0;
	c->doglare = 0;
	c->neoWaterDrops = 0;
	c->envMapSize = 256;
	c->envMapFarClipMult = 1.0f;
}

const PresetConfig*
MobileMode_GetPreset(void)
{
	return &mobile_preset;
}

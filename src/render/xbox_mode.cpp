// xbox_mode.cpp — Xbox rendering style.
// Emulates the look of GTA SA on Xbox (enhanced PC).
//
// Key characteristics:
//   - Xbox vertex lighting pipeline (fixed-function texture combiners)
//   - PC color filter (original SA color filter)
//   - Xbox building pipe with stochastic/detail textures
//   - Xbox envmap vehicle pipeline
//   - Dual-pass enabled (better alpha rendering)
//   - Shader radiosity
//   - No PS2 modulation
//
// Pipeline config:
//   buildingPipe  = BUILDING_XBOX
//   vehiclePipe   = CAR_XBOX
//   colorFilter   = COLORFILTER_PC
//   ps2Modulate   = 0
//   dualPass      = 1
//   radiosity     = Shader
//   ivMode        = 0
//
// Shader objects used:
//   Building: xboxBuildingVS, xboxBuildingWindVS, xboxBuildingStochasticPS, xboxBuildingPS
//   Vehicle:  xboxCarVS
//   Color:    Original SA PC color filter
//
// References:
//   buildingPipe.cpp:390-533  — BUILDING_XBOX callback
//   vehiclePipe.cpp:861-1037  — CAR_XBOX callback
//   postfx.cpp:24             — COLORFILTER_PC (wrapper)
//   presets.cpp:45            — "SA Xbox" preset

#include "skygfx.h"

// Xbox mode uses the Xbox building pipe and Xbox vehicle pipe,
// which are fully implemented in buildingPipe.cpp and vehiclePipe.cpp.
// The color filter is the original PC filter.

static const PresetConfig xbox_preset = {
	"Xbox",
	BUILDING_XBOX,
	CAR_XBOX,
	COLORFILTER_PC,
	0,                          // ps2ModulateGlobal  (no PS2 modulation)
	1,                          // dualPassGlobal     (Xbox dual-pass)
	1,                          // radiosity          (shader radiosity)
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
XboxMode_ApplyDefaults(Config *c)
{
	c->buildingPipe = BUILDING_XBOX;
	c->vehiclePipe = CAR_XBOX;
	c->colorFilter = COLORFILTER_PC;

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
XboxMode_GetPreset(void)
{
	return &xbox_preset;
}

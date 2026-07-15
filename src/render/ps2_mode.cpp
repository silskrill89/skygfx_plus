// ps2_mode.cpp — PlayStation 2 rendering style.
// Emulates the look of GTA SA / GTA III / GTA VC on PS2.
//
// Key characteristics:
//   - PS2 vertex lighting pipeline (MODULATE2X blending)
//   - PS2 color filter (MODULATE2X + additive second pass)
//   - PS2 building pipe with wind vertex shaders
//   - PS2 envmap vehicle pipeline
//   - No dual-pass, no shader radiosity
//   - PS2 grain filter, infrared/night vision
//   - PS2 grass modulation
//
// Pipeline config:
//   buildingPipe  = BUILDING_PS2
//   vehiclePipe   = CAR_PS2
//   colorFilter   = COLORFILTER_PS2
//   ps2Modulate   = 1
//   dualPass      = 0
//   radiosity     = PS2 (hardware)
//   ivMode        = 0
//
// Shader objects used:
//   Building: ps2BuildingVS, ps2BuildingWindVS, ps2BuildingFxVS
//   Vehicle:  ps2CarVS, ps2CarFxVS
//   Color:    PS2 modulate2x color filter
//
// References:
//   buildingPipe.cpp:188-386  — BUILDING_PS2 callback
//   vehiclePipe.cpp:444-632   — CAR_PS2 callback
//   postfx.cpp:951-1025       — COLORFILTER_PS2
//   presets.cpp:43            — "SA PS2" preset

#include "skygfx.h"

// PS2 mode is the baseline — all code lives in the existing
// buildingPipe.cpp (BUILDING_PS2), vehiclePipe.cpp (CAR_PS2),
// and postfx.cpp (COLORFILTER_PS2). No separate init needed
// beyond what the existing pipeline system already does.

// Preset configuration for SA PS2 style
// This is the canonical PS2 look: no enhancements, pure hardware rendering.
static const PresetConfig ps2_preset = {
	"PS2",                      // name
	BUILDING_PS2,               // buildingPipe
	CAR_PS2,                    // vehiclePipe
	COLORFILTER_PS2,            // colorFilter
	1,                          // ps2ModulateGlobal  (PS2-style modulation ON)
	0,                          // dualPassGlobal     (PS2 doesn't dual-pass)
	0,                          // radiosity          (PS2 hardware radiosity)
	1,                          // doRadiosity
	0,                          // vcsTrails
	-1,                         // pedShadows         (use default)
	-1,                         // stencilShadows     (use default)
	0,                          // grainFilter        (PS2 grain)
	0,                          // infraredVision     (PS2 infrared)
	0,                          // nightVision        (PS2 night vision)
	0,                          // ssaoEnable
	0,                          // smaaEnable
	0,                          // smaaPreset
	0,                          // ivMode
};

// Apply PS2 mode to a Config struct
void
PS2Mode_ApplyDefaults(Config *c)
{
	c->buildingPipe = BUILDING_PS2;
	c->vehiclePipe = CAR_PS2;
	c->colorFilter = COLORFILTER_PS2;

	c->ps2ModulateGlobal = 1;
	c->ps2ModulateBuilding = 1;
	c->ps2ModulateGrass = 1;
	c->dualPassGlobal = 0;
	c->dualPassBuilding = 0;
	c->dualPassVehicle = 0;
	c->dualPassPed = 0;
	c->dualPassGrass = 0;
	c->dualPassDefault = 0;

	c->radiosity = 0;       // PS2 hardware
	c->doRadiosity = 1;

	c->grainFilter = 0;     // PS2 grain
	c->infraredVision = 0;  // PS2 infrared
	c->nightVision = 0;     // PS2 night vision

	c->ssaoEnable = 0;
	c->smaaEnable = 0;
	c->smaaPreset = 0;

	c->ivMode = 0;
	c->ivDesaturation = 0.0f;
	c->ivGamma = 1.0f;
	c->ivVignetteIntensity = 0.0f;
	c->ivBloomIntensity = 0.0f;

	c->detailMaps = 0;
	c->stochastic = 0;
	c->doglare = 0;
	c->neoWaterDrops = 0;
	c->envMapSize = 128;
	c->envMapFarClipMult = 1.0f;
}

const PresetConfig*
PS2Mode_GetPreset(void)
{
	return &ps2_preset;
}

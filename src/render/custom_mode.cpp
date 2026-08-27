// custom_mode.cpp — Custom / PBR rendering mode.
// The unified PBR pipeline for all asset types.
//
// This is the mode we've been building: CryEngine/Diffuse/Specular/Gloss
// workflow applied to GTA SA's rendering pipeline.
//
// Key characteristics:
//   - PBR vehicle pipeline (VehiclePBR_Modern.hlsl — GGX specular, env map primary)
//   - PBR building pipeline (BUILDING_PBR — VehiclePBR_Modern pixel shader)
//   - IV or bypass color filter (user choice)
//   - All enhancements enabled (SSAO, SMAA, motion blur, SSS, etc.)
//   - Normal maps, detail maps, stochastic texturing
//   - Universal dirt/wear system
//   - Wheel extender (156 unique wheel geometries)
//
// Pipeline config:
//   buildingPipe  = BUILDING_PBR
//   vehiclePipe   = CAR_MODERN (CAR_ENV)
//   colorFilter   = VCS (clean modern look)
//   ps2Modulate   = 0
//   dualPass      = 1
//   radiosity     = Shader
//   ivMode        = 0 (PBR has its own tone mapping)
//
// Shader objects used:
//   Building: ps2BuildingVS + VehiclePBR_Modern (PBR pixel shader)
//   Vehicle:  vehiclePBRVS + VehiclePBR_Modern.hlsl (7 entry points)
//             - main: PBR env map + IBL additive tint
//             - main_glass: Glass refraction
//             - main_rubber: Tire shader
//   Color:    User selected (default: VCS - clean modern look)
//
// References:
//   buildingPipe.cpp:614-701  — BUILDING_PBR callback
//   vehiclePipe.cpp:1355-1734 — CAR_ENV/CAR_MODERN callback
//   shaders/ps/VehiclePBR_Modern.hlsl — 7 entry points
//   brdfLibrary.h — 270+ surface BRDF database

#include "skygfx.h"

// Custom mode is the entry point for the PBR pipeline.
// All the heavy lifting is done in vehiclePipe.cpp (CAR_MODERN),
// buildingPipe.cpp (BUILDING_PBR), and the HLSL shaders.
// This file provides the mode's default configuration.

static const PresetConfig custom_preset = {
	"Custom (PBR)",
	BUILDING_PBR,
	CAR_MODERN,
	COLORFILTER_VCS,
	0,                          // ps2ModulateGlobal
	1,                          // dualPassGlobal
	1,                          // radiosity
	1,                          // doRadiosity
	0,                          // vcsTrails
	1,                          // pedShadows
	1,                          // stencilShadows
	1,                          // grainFilter
	0,                          // infraredVision
	0,                          // nightVision
	1,                          // ssaoEnable
	1,                          // smaaEnable
	3,                          // smaaPreset (ULTRA)
	0,                          // ivMode
};

void
CustomMode_ApplyDefaults(Config *c)
{
	c->buildingPipe = BUILDING_PBR;
	c->vehiclePipe = CAR_MODERN;
	c->colorFilter = COLORFILTER_MODERN;

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

	c->grainFilter = 1;
	c->infraredVision = 0;
	c->nightVision = 0;

	c->ssaoEnable = 1;
	c->ssaoRadius = 1.0f;
	c->ssaoPower = 2.0f;
	c->ssaoKernelSize = 16;
	c->ssaoSampleCount = 16;
	c->smaaEnable = 1;

	// Motion blur (Burnout Paradise style)
	c->motionBlurEnable = 1;
	c->motionBlurStrength = 0.4f;
	c->motionBlurRadial = 0.2f;
	c->motionBlurSpeedFactor = 0.3f;
	c->motionBlurCameraAware = 1;

	// SSS post-process
	c->sssPostProcessEnable = 1;
	c->sssPostProcessStrength = 0.2f;
	c->sssPostProcessRadius = 3.0f;

	// Skin enhancement
	c->skinEnhanceEnable = 1;
	c->skinWrapFactor = 0.4f;
	c->skinSpecularPower = 24.0f;
	c->skinSpecularStrength = 0.2f;
	c->skinSSSStrength = 0.3f;

	// Hair enhancement
	c->hairEnhanceEnable = 1;
	c->hairAnisotropicPower = 48.0f;
	c->hairAnisotropicStrength = 0.4f;
	c->hairSSSStrength = 0.15f;

	// Vegetation enhancement
	c->vegetationEnhanceEnable = 1;
	c->vegetationSSSStrength = 0.2f;
	c->vegetationAmbientBoost = 1.3f;

	// Normal mapping
	c->normalMapIntensity = 1.0f;

	c->ivMode = 0;
	c->ivDesaturation = 0.15f;
	c->ivGamma = 1.0f;
	c->ivSaturation = 0.3f;
	c->ivCurves = 1.0f;
	c->ivVignetteIntensity = 0.15f;
	c->ivVignetteRadius = 0.70f;
	c->ivVignetteContrast = 1.5f;
	c->ivBloomIntensity = 0.05f;
	c->ivExposure = 2.5f;

	c->detailMaps = 1;
	c->stochastic = 1;
	c->doglare = 1;
	c->neoWaterDrops = 1;
	c->envMapSize = 512;
	c->envMapFarClipMult = 2.0f;
	c->envShininessMult = 1.0f;
	c->envSpecularityMult = 1.0f;
	c->envPower = 128.0f;
	c->envFresnel = 0.95f;
}

const PresetConfig*
CustomMode_GetPreset(void)
{
	return &custom_preset;
}

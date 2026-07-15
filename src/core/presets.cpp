#include "skygfx.h"

// Preset name strings (NULL-terminated array for UI)
const char *presetNames[NUM_PRESETS + 1] = {
	"III PS2",
	"III Xbox",
	"III PC",
	"VC PS2",
	"VC Xbox",
	"VC PC",
	"SA PS2",
	"SA Xbox",
	"SA PC",
	"LCS PS2",
	"VCS PS2",
	"IV Xbox 360",
	"IV PC",
	"Best PC (Default)",
	NULL
};

// Preset configurations
// Format: name, buildingPipe, vehiclePipe, colorFilter, ps2Modulate, dualPass, radiosity, doRadiosity, vcsTrails, pedShadows, stencilShadows, grainFilter, infraredVision, nightVision, ssao, smaa, smaaPreset, ivMode
const PresetConfig presetConfigs[NUM_PRESETS] = {
	// GTA III presets
	{ "III PS2",
		BUILDING_PS2, CAR_PS2, COLORFILTER_III, 1, 0, 0, 1, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ "III Xbox",
		BUILDING_XBOX, CAR_PS2, COLORFILTER_III, 0, 0, 0, 1, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ "III PC",
		BUILDING_XBOX, CAR_PC, COLORFILTER_III, 0, 0, 0, 1, 0, -1, -1, 1, 0, 0, 0, 0, 0, 0 },

	// GTA Vice City presets
	{ "VC PS2",
		BUILDING_PS2, CAR_PS2, COLORFILTER_VC, 1, 0, 0, 1, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ "VC Xbox",
		BUILDING_XBOX, CAR_PS2, COLORFILTER_VC, 0, 0, 0, 1, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ "VC PC",
		BUILDING_XBOX, CAR_PC, COLORFILTER_VC, 0, 0, 0, 1, 0, -1, -1, 1, 0, 0, 0, 0, 0, 0 },

	// GTA San Andreas presets
	{ "SA PS2",
		BUILDING_PS2, CAR_PS2, COLORFILTER_PS2, 1, 0, 0, 1, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ "SA Xbox",
		BUILDING_XBOX, CAR_XBOX, COLORFILTER_PC, 0, 1, 1, 1, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ "SA PC",
		BUILDING_XBOX, CAR_PC, COLORFILTER_PC, 0, 1, 1, 1, 0, -1, -1, 1, 0, 0, 0, 0, 0, 0 },

	// GTA Liberty City Stories
	{ "LCS PS2",
		BUILDING_PS2, CAR_LCS, COLORFILTER_VCS, 1, 0, 0, 1, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0 },

	// GTA Vice City Stories
	{ "VCS PS2",
		BUILDING_PS2, CAR_VCS, COLORFILTER_VCS, 1, 0, 1, 1, 1, -1, -1, 0, 0, 0, 0, 0, 0, 0 },

	// GTA IV presets
	{ "IV Xbox 360",
		BUILDING_GTAIV, CAR_GTAIV, COLORFILTER_GTAIV, 0, 0, 0, 0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 1 },
	{ "IV PC",
		BUILDING_GTAIV, CAR_GTAIV, COLORFILTER_GTAIV, 0, 0, 0, 0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 1 },

	// Best PC - combines best settings from all versions (default)
	{ "Best PC (Default)",
		BUILDING_XBOX,    // Xbox building pipe (better than PS2)
		CAR_NEO,          // Neo vehicle pipe (best car rendering)
		COLORFILTER_VCS,  // VCS color filter (modern, clean)
		0,                // No PS2 modulation (cleaner look)
		1,                // Dual-pass enabled (better alpha rendering)
		1,                // Shader radiosity (better quality)
		1,                // Radiosity enabled
		0,                // No VCS trails (cleaner)
		-1,               // Default ped shadows
		-1,               // Default stencil shadows
		1,                // PC grain filter (subtle)
		0,                // PS2 infrared (more accurate)
		0,                // PS2 night vision (more accurate)
		1,                // SSAO enabled
		1,                // SMAA enabled
		3,                // SMAA ULTRA
		0,                // No IV mode
	},

	// skygfxplusultramaxdeluxe — EVERYTHING maxed, all features on
	{ "skygfxplusultramaxdeluxe",
		BUILDING_XBOX,    // Xbox building pipe
		CAR_MODERN,       // Modern PBR vehicle pipe
		COLORFILTER_VCS,  // VCS color filter
		0,                // No PS2 modulation
		1,                // Dual-pass enabled
		1,                // Shader radiosity
		1,                // Radiosity enabled
		1,                // VCS trails enabled
		1,                // Ped shadows ON
		1,                // Stencil shadows ON
		1,                // Grain filter ON
		0,                // No infrared
		0,                // No night vision
		1,                // SSAO enabled
		1,                // SMAA enabled
		3,                // SMAA ULTRA
		0,                // No IV mode
	},
};

// Apply a preset to the config
void ApplyPreset(Config *c, int preset)
{
	if(preset < 0 || preset >= NUM_PRESETS){
		c->preset = PRESET_CUSTOM;
		return;
	}

	const PresetConfig *p = &presetConfigs[preset];
	c->preset = preset;

	c->buildingPipe = p->buildingPipe;
	c->vehiclePipe = p->vehiclePipe;
	c->colorFilter = p->colorFilter;
	c->ps2ModulateGlobal = p->ps2ModulateGlobal;
	c->dualPassGlobal = p->dualPassGlobal;
	c->radiosity = p->radiosity;
	c->doRadiosity = p->doRadiosity;
	c->vcsTrails = p->vcsTrails;
	c->grainFilter = p->grainFilter;
	c->infraredVision = p->infraredVision;
	c->nightVision = p->nightVision;
	c->ssaoEnable = p->ssaoEnable;
	c->smaaEnable = p->smaaEnable;
	c->smaaPreset = p->smaaPreset;
	c->ivMode = p->ivMode;

	// Apply cascading settings
	c->ps2ModulateBuilding = c->ps2ModulateGlobal;
	c->ps2ModulateGrass = c->ps2ModulateGlobal;
	c->dualPassBuilding = c->dualPassGlobal;
	c->dualPassVehicle = c->dualPassGlobal;
	c->dualPassPed = c->dualPassGlobal;
	c->dualPassGrass = c->dualPassGlobal;
	c->dualPassDefault = c->dualPassGlobal;

	// Ped/stencil shadows: -1 means use default
	if(p->pedShadows >= 0)
		c->pedShadows = p->pedShadows;
	if(p->stencilShadows >= 0)
		c->stencilShadows = p->stencilShadows;
}

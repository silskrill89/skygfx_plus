#include "skygfx.h"

const char *presetNames[NUM_PRESETS + 1] = {
	"PS2",
	"Xbox",
	"PC",
	"PBR",
	NULL
};

const PresetConfig presetConfigs[NUM_PRESETS] = {
	{ "PS2",
		BUILDING_PS2, CAR_PS2, COLORFILTER_PS2,
		1, 0, 0, 1, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ "Xbox",
		BUILDING_XBOX, CAR_XBOX, COLORFILTER_PC,
		0, 1, 1, 1, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0 },
	{ "PC",
		BUILDING_XBOX, CAR_PC, COLORFILTER_VCS,
		0, 1, 1, 1, 0, -1, -1, 1, 0, 0, 1, 1, 3, 0 },
	{ "PBR",
		BUILDING_PBR, CAR_MODERN, COLORFILTER_PBR,
		0, 1, 1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 3, 0 },
};

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

	c->ps2ModulateBuilding = c->ps2ModulateGlobal;
	c->ps2ModulateGrass = c->ps2ModulateGlobal;
	c->dualPassBuilding = c->dualPassGlobal;
	c->dualPassVehicle = c->dualPassGlobal;
	c->dualPassPed = c->dualPassGlobal;
	c->dualPassGrass = c->dualPassGlobal;
	c->dualPassDefault = c->dualPassGlobal;

	if(p->pedShadows >= 0)
		c->pedShadows = p->pedShadows;
	if(p->stencilShadows >= 0)
		c->stencilShadows = p->stencilShadows;
}

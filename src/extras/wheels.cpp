// wheels.cpp — Wheel DFF management system
// Extends GTA SA's existing wheels.DFF/wheels.txd system
// Adds more wheel atomics + hash-based random selection per vehicle
// Pattern: same as veh_shaders paint/tire randomizer

#include "skygfx.h"
#include "wheels.h"
#include <stdio.h>
#include <string.h>

// ============================================================
// Wheel database
// ============================================================

#define MAX_WHEELS 512

static WheelEntry g_wheels[MAX_WHEELS];
static int g_wheelCount = 0;
static bool g_wheelsInitialized = false;

// ============================================================
// Init — load wheel metadata and classify
// ============================================================

void Wheels_Init(const char *gameDir){
	if(g_wheelsInitialized) return;
	g_wheelsInitialized = true;

	// Load metadata from wheel extractor output
	char metaPath[MAX_PATH];
	snprintf(metaPath, MAX_PATH, "%s\\models\\wheels\\wheels_meta.json", gameDir);

	FILE *f = fopen(metaPath, "r");
	if(!f){
		dbglog("Wheels_Init: no wheels_meta.json found, using game defaults");
		return;
	}

	// Parse JSON (simple parser — just look for "index", "vehicle", "frame")
	char line[512];
	while(fgets(line, sizeof(line), f)){
		if(g_wheelCount >= MAX_WHEELS) break;

		// Look for wheel entries
		int index;
		char filename[32], vehicle[64], frameName[64];
		if(sscanf(line, "    {\"index\": %d, \"file\": \"%31[^\"]\", \"vehicle\": \"%63[^\"]\", \"frame\": \"%63[^\"]\"}",
		          &index, filename, vehicle, frameName) == 4){

			WheelEntry *w = &g_wheels[g_wheelCount];
			w->index = index;
			strncpy(w->filename, filename, 31);
			strncpy(w->vehicle, vehicle, 63);
			strncpy(w->frameName, frameName, 63);
			w->filename[31] = '\0';		// strncpy does not NUL-terminate on truncation
			w->vehicle[63] = '\0';
			w->frameName[63] = '\0';
			w->atomic = NULL;
			w->loaded = false;

			// Classify by vehicle name
			// This is a simple heuristic — can be refined
			if(strstr(vehicle, "infernus") || strstr(vehicle, "turismo") || strstr(vehicle, "banshee"))
				w->wheelClass = WHEEL_CLASS_SPORT;
			else if(strstr(vehicle, "sabre") || strstr(vehicle, "stallion") || strstr(vehicle, "phoenix"))
				w->wheelClass = WHEEL_CLASS_MUSCLE;
			else if(strstr(vehicle, "huntley") || strstr(vehicle, "landstal") || strstr(vehicle, "mesa"))
				w->wheelClass = WHEEL_CLASS_SUV;
			else if(strstr(vehicle, "pcj") || strstr(vehicle, "nrg") || strstr(vehicle, "fcr") || strstr(vehicle, "sanchez"))
				w->wheelClass = WHEEL_CLASS_BIKE;
			else if(strstr(vehicle, "linerun") || strstr(vehicle, "roadtrain") || strstr(vehicle, "packer"))
				w->wheelClass = WHEEL_CLASS_TRUCK;
			else if(strstr(vehicle, "savanna") || strstr(vehicle, "voodoo") || strstr(vehicle, "tornado"))
				w->wheelClass = WHEEL_CLASS_LOWRIDER;
			else if(strstr(vehicle, "sultan") || strstr(vehicle, "jester") || strstr(vehicle, "elegy"))
				w->wheelClass = WHEEL_CLASS_TUNER;
			else
				w->wheelClass = WHEEL_CLASS_SEDAN;

			g_wheelCount++;
		}
	}

	fclose(f);
	dbglog("Wheels_Init: loaded %d wheel entries", g_wheelCount);
}

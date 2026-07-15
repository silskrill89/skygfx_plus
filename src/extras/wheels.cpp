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

// Wheel class weights for randomizer (same pattern as paint weights)
// Each row: probability of selecting each class for a vehicle class
static const float wheelClassWeights[][NUM_WHEEL_CLASSES] = {
	//                    sport  muscle  suv   sedan  bike  truck  lowrider tuner
	/* VGROUP_STANDARD */ { 0.05f, 0.10f, 0.10f, 0.50f, 0.00f, 0.10f, 0.05f, 0.10f },
	/* VGROUP_SPORT    */ { 0.50f, 0.15f, 0.00f, 0.05f, 0.00f, 0.00f, 0.00f, 0.30f },
	/* VGROUP_MUSCLE   */ { 0.10f, 0.50f, 0.05f, 0.10f, 0.00f, 0.05f, 0.10f, 0.10f },
	/* VGROUP_LOWRIDER */ { 0.00f, 0.05f, 0.00f, 0.10f, 0.00f, 0.00f, 0.75f, 0.10f },
	/* VGROUP_TRUCK    */ { 0.00f, 0.05f, 0.40f, 0.10f, 0.00f, 0.40f, 0.00f, 0.05f },
	/* VGROUP_VAN      */ { 0.00f, 0.05f, 0.15f, 0.40f, 0.00f, 0.30f, 0.00f, 0.10f },
	/* VGROUP_LUXURY   */ { 0.30f, 0.05f, 0.05f, 0.30f, 0.00f, 0.00f, 0.00f, 0.30f },
	/* VGROUP_CLASSIC  */ { 0.05f, 0.30f, 0.05f, 0.20f, 0.00f, 0.05f, 0.25f, 0.10f },
	/* VGROUP_BIKE     */ { 0.00f, 0.00f, 0.00f, 0.00f, 1.00f, 0.00f, 0.00f, 0.00f },
	/* VGROUP_EMERGENCY*/ { 0.00f, 0.10f, 0.30f, 0.30f, 0.00f, 0.30f, 0.00f, 0.00f },
};

// ============================================================
// Vehicle group lookup (external)
// ============================================================

extern int GetVehicleGroup(int modelID); // from veh_shaders.cpp

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
		if(sscanf(line, "    {\"index\": %d, \"file\": \"%[^\"]\", \"vehicle\": \"%[^\"]\", \"frame\": \"%[^\"]\"}",
		          &index, filename, vehicle, frameName) == 4){

			WheelEntry *w = &g_wheels[g_wheelCount];
			w->index = index;
			strncpy(w->filename, filename, 31);
			strncpy(w->vehicle, vehicle, 63);
			strncpy(w->frameName, frameName, 63);
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

// ============================================================
// Hash-based wheel selection (same pattern as paint/tire)
// ============================================================

int Wheels_SelectForVehicle(int modelID, unsigned int hash){
	if(g_wheelCount == 0) return -1; // no custom wheels

	int group = GetVehicleGroup(modelID);
	if(group < 0 || group >= 10) group = 0;

	// Get class weights for this vehicle group
	const float *weights = wheelClassWeights[group];

	// Hash-based random selection
	float r = (float)(hash & 0xFFFF) / 65535.0f;
	float cum = 0.0f;
	WheelClass selectedClass = WHEEL_CLASS_SEDAN;

	for(int i = 0; i < NUM_WHEEL_CLASSES; i++){
		cum += weights[i];
		if(r <= cum){
			selectedClass = (WheelClass)i;
			break;
		}
	}

	// Find wheels of the selected class
	int candidates[MAX_WHEELS];
	int numCandidates = 0;
	for(int i = 0; i < g_wheelCount; i++){
		if(g_wheels[i].wheelClass == selectedClass){
			candidates[numCandidates++] = i;
		}
	}

	if(numCandidates == 0){
		// Fallback: select from all wheels
		return (hash % g_wheelCount);
	}

	// Select from candidates using secondary hash
	unsigned int hash2 = hash * 2654435761u;
	int selected = candidates[hash2 % numCandidates];
	return selected;
}

// ============================================================
// Wheel queries
// ============================================================

WheelEntry *Wheels_GetEntry(int index){
	if(index < 0 || index >= g_wheelCount) return NULL;
	return &g_wheels[index];
}

WheelClass Wheels_GetClassForModel(int modelID){
	// This would need the vehicle database — for now return standard
	return WHEEL_CLASS_SEDAN;
}

int Wheels_GetCount(void){
	return g_wheelCount;
}

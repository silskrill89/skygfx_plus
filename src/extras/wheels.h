// wheels.h — Wheel DFF management system
// Hash-based wheel selection (same pattern as veh_shaders paint/tire)
// Loads extracted wheel DFFs, provides random selection per vehicle

#pragma once

// Wheel class — groups wheels by vehicle type
enum WheelClass {
	WHEEL_CLASS_SPORT = 0,     // low-profile, alloy
	WHEEL_CLASS_MUSCLE,        // wide, classic
	WHEEL_CLASS_SUV,           // large, off-road
	WHEEL_CLASS_SEDAN,         // standard, hubcap
	WHEEL_CLASS_BIKE,          // motorcycle
	WHEEL_CLASS_TRUCK,         // heavy-duty
	WHEEL_CLASS_LOWRIDER,      // wire, spoked
	WHEEL_CLASS_TUNER,         // aftermarket
	NUM_WHEEL_CLASSES
};

// Wheel entry — one extracted wheel DFF
struct WheelEntry {
	int index;              // sequential index (wheel001.dff = 1)
	char filename[32];      // "wheel001.dff"
	char vehicle[64];       // source vehicle name
	char frameName[64];     // "wheel_lf" etc
	WheelClass wheelClass;  // classified by vehicle type
	RpAtomic *atomic;       // loaded atomic (lazy-loaded)
	bool loaded;
};

// Initialize wheel system (loads metadata, classifies wheels)
void Wheels_Init(const char *gameDir);

// Select a wheel for a given vehicle model (deterministic hash)
int Wheels_SelectForVehicle(int modelID, unsigned int hash);

// Get wheel entry by index
WheelEntry *Wheels_GetEntry(int index);

// Get wheel class for a vehicle model
WheelClass Wheels_GetClassForModel(int modelID);

// Total number of extracted wheels
int Wheels_GetCount(void);

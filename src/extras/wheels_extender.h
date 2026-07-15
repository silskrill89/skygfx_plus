// wheels_extender.h — Wheel DFF extender for SkyGFX Plus
// Extends the existing vehicle pipe system to support more wheel styles

#pragma once

// Initialize wheel extender (load DFFs from models/wheels/)
void WheelsExtender_Init(const char* gameDir);

// Install hooks
void WheelsExtender_Install(void);

// Select wheel for a given vehicle model
struct WheelEntry;
WheelEntry* Wheels_SelectForVehicle(int modelID);

// Get wheel count
int WheelsExtender_GetCount(void);

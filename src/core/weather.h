// weather.h — Multi-timecyc weather system
// Sits on top of game's CTimeCycle, overrides m_CurrentColours
// RW-style: uses game's CColourSet interpolation, adds hash-based set selection

#pragma once

// Weather set IDs
enum WeatherSet {
	WEATHER_SET_DEFAULT = 0,  // timecyc.dat (main)
	WEATHER_SET_2 = 1,        // weathers2.dat (alternate)
	NUM_WEATHER_SETS
};

// Initialize weather system (loads timecyc files)
void Weather_Init(const char *gameDir);

// Called each frame after CTimeCycle::Update()
// Overrides m_CurrentColours with blended values from both sets
void Weather_Update(void);

// Select weather set for a given weather type + time slot (deterministic hash)
int Weather_SelectSet(int weatherType, int timeSlot);

// Get the blend factor between sets (0.0 = set1, 1.0 = set2)
float Weather_GetBlendFactor(int weatherType, int timeSlot);

// Apply sun config multipliers (corona, core, streaks) to m_CurrentColours.
// Called every frame from RenderScene_hook, AFTER CTimeCycle::Update() writes
// m_CurrentColours but BEFORE rendering reads it.
// Runs unconditionally — no weather blend checks.
void Weather_ApplySunConfig(void);

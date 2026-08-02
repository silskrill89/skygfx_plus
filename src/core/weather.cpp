// weather.cpp — Multi-timecyc weather system
// Sits on top of game's CTimeCycle, overrides m_CurrentColours
// Pattern: hash-based deterministic set selection (same as veh_shaders paint/tire)
// RW-style: reads game's CColourSet arrays, blends between two timecyc sets

#include "skygfx.h"
#include "weather.h"
#include <stdio.h>
#include <string.h>

// ============================================================
// Game's timecyc data (184 entries = 23 weathers × 8 time slots)
// These are the static arrays that CTimeCycle::Initialise() fills
// ============================================================

// We store our own copy of the alternate timecyc data
// Format matches CTimeCycle static arrays exactly

struct TimecycEntry {
	unsigned char ambR, ambG, ambB;
	unsigned char ambObjR, ambObjG, ambObjB;
	unsigned char dirR, dirG, dirB;
	unsigned char skyTopR, skyTopG, skyTopB;
	unsigned char skyBotR, skyBotG, skyBotB;
	unsigned char sunCoreR, sunCoreG, sunCoreB;
	unsigned char sunCoronaR, sunCoronaG, sunCoronaB;
	signed char sunSz, sprSz, sprBght;
	unsigned char shdw, lightShd, poleShd;
	signed short farClp, fogSt;
	unsigned char lightOnGround;
	unsigned char lowCloudsR, lowCloudsG, lowCloudsB;
	unsigned char fluffyCloudR, fluffyCloudG, fluffyCloudB;
	unsigned char waterR, waterG, waterB, waterA;
	unsigned char postFx1R, postFx1G, postFx1B, postFx1A;
	unsigned char postFx2R, postFx2G, postFx2B, postFx2A;
	unsigned char cloudAlpha;
	unsigned int highLightMinIntensity;
	unsigned short waterFogAlpha;
	float illumination;
};

// 23 weather types × 8 time slots = 184 entries
static TimecycEntry g_altTimecyc[184];
static bool g_altLoaded = false;
static bool g_weatherInitialized = false;

// Current blend state (smooth transition)
static float g_currentBlend = 0.0f;
static float g_targetBlend = 0.0f;

// ============================================================
// Hash-based randomizer (same pattern as veh_shaders)
// ============================================================

static unsigned int WeatherHash(int weatherType, int timeSlot){
	unsigned int h = (unsigned int)(weatherType * 2654435761u);
	h ^= (unsigned int)(timeSlot * 2246822519u);
	h = (h ^ (h >> 16)) * 0x45d9f3b;
	h = (h ^ (h >> 16)) * 0x45d9f3b;
	h = h ^ (h >> 16);
	return h;
}

// Select which weather set to use for a given type+slot
int Weather_SelectSet(int weatherType, int timeSlot){
	unsigned int hash = WeatherHash(weatherType, timeSlot);
	float r = (float)(hash & 0xFFFF) / 65535.0f;
	// 70% default, 30% alternate
	return (r < 0.7f) ? WEATHER_SET_DEFAULT : WEATHER_SET_2;
}

// Get blend factor (smooth transition)
float Weather_GetBlendFactor(int weatherType, int timeSlot){
	int set = Weather_SelectSet(weatherType, timeSlot);
	return (set == WEATHER_SET_2) ? 1.0f : 0.0f;
}

// ============================================================
// Timecyc parser — reads weathers2.dat into g_altTimecyc
// Format matches game's timecyc.dat exactly (tab-separated values)
// ============================================================

static bool ParseTimecycFile(const char *path){
	FILE *f = fopen(path, "r");
	if(!f) return false;

	char line[1024];
	int entryIdx = 0;
	memset(g_altTimecyc, 0, sizeof(g_altTimecyc));

	while(fgets(line, sizeof(line), f)){
		// Skip comments and empty lines
		if(line[0] == '/' || line[0] == '\n' || line[0] == '\r' || line[0] == '\t')
			continue;

		// Skip weather type headers (////////// WEATHER_NAME)
		if(strncmp(line, "//////////", 10) == 0)
			continue;

		// Skip time slot comments (//Midnight, //5AM, etc)
		if(line[0] == '/' && line[1] == '/')
			continue;

		// Parse data line
		if(entryIdx < 184){
			TimecycEntry *e = &g_altTimecyc[entryIdx];
			int r, g, b;
			int ambObjR, ambObjG, ambObjB;
			int dirR, dirG, dirB;

			// Parse: Amb\tAmbObj\tDir\tSkyTop\tSkyBot\tSunCore\tSunCorona\tSunSz SprSz SprBght\tShdw LightShd PoleShd\tFarClp FogSt LightOnGround\tLowCloudsRGB\tBottomCloudRGB\tWaterRGBA\tAlpha1 RGB1\tAlpha2 RGB2\tCloudAlpha Illumination
			int n = sscanf(line,
				"%d %d %d\t%d %d %d\t%d %d %d\t"
				"%hhu %hhu %hhu\t%hhu %hhu %hhu\t%hhu %hhu %hhu\t"
				"%hhu %hhu %hhu\t%hhd %hhd %hhd\t%hhu %hhu %hhu\t"
				"%hd %hd %hhu\t%hhu %hhu %hhu\t%hhu %hhu %hhu\t"
				"%hhu %hhu %hhu %hhu\t"
				"%hhu %hhu %hhu %hhu\t"
				"%hhu %hhu %hhu %hhu\t"
				"%hhu\t%u\t%hu\t%f",
				&r, &g, &b,
				&ambObjR, &ambObjG, &ambObjB,
				&dirR, &dirG, &dirB,
				&e->skyTopR, &e->skyTopG, &e->skyTopB,
				&e->skyBotR, &e->skyBotG, &e->skyBotB,
				&e->sunCoreR, &e->sunCoreG, &e->sunCoreB,
				&e->sunCoronaR, &e->sunCoronaG, &e->sunCoronaB,
				&e->sunSz, &e->sprSz, &e->sprBght,
				&e->shdw, &e->lightShd, &e->poleShd,
				&e->farClp, &e->fogSt, &e->lightOnGround,
				&e->lowCloudsR, &e->lowCloudsG, &e->lowCloudsB,
				&e->fluffyCloudR, &e->fluffyCloudG, &e->fluffyCloudB,
				&e->waterR, &e->waterG, &e->waterB, &e->waterA,
				&e->postFx1R, &e->postFx1G, &e->postFx1B, &e->postFx1A,
				&e->postFx2R, &e->postFx2G, &e->postFx2B, &e->postFx2A,
				&e->cloudAlpha,
				&e->highLightMinIntensity,
				&e->waterFogAlpha,
				&e->illumination
			);

			if(n >= 20){
				e->ambR = (unsigned char)r;
				e->ambG = (unsigned char)g;
				e->ambB = (unsigned char)b;
				e->ambObjR = (unsigned char)ambObjR;
				e->ambObjG = (unsigned char)ambObjG;
				e->ambObjB = (unsigned char)ambObjB;
				e->dirR = (unsigned char)dirR;
				e->dirG = (unsigned char)dirG;
				e->dirB = (unsigned char)dirB;
				entryIdx++;
			}
		}
	}

	fclose(f);
	dbglog("ParseTimecycFile: loaded %d entries from %s", entryIdx, path);
	return entryIdx > 0;
}

// ============================================================
// Init — loads alternate timecyc
// ============================================================

void Weather_Init(const char *gameDir){
	if(g_weatherInitialized) return;
	g_weatherInitialized = true;

	char path[MAX_PATH];

	// Load alternate timecyc (weathers2.dat)
	snprintf(path, sizeof(path), "%s\\data\\weathers2.dat", gameDir);
	g_altLoaded = ParseTimecycFile(path);
	dbglog("Weather_Init: weathers2=%s (%d entries)", g_altLoaded ? "OK" : "FAIL",
	       g_altLoaded ? 184 : 0);
}

// ============================================================
// Apply sun config multipliers to m_CurrentColours.
// Called every frame from RenderScene_hook, after CTimeCycle::Update()
// but before any rendering reads the values.
// Runs unconditionally — no weather blend or alt-timecyc checks.
// ============================================================

void Weather_ApplySunConfig(void){
	extern CColourSet &CTimeCycle__m_CurrentColours;
	CColourSet &tc = CTimeCycle__m_CurrentColours;

	// Scale sun corona color (PS2→PC conversion fix)
	if(config->sunCoronaIntensity != 1.0f){
		tc.sunCoronaR = (short)(tc.sunCoronaR * config->sunCoronaIntensity);
		tc.sunCoronaG = (short)(tc.sunCoronaG * config->sunCoronaIntensity);
		tc.sunCoronaB = (short)(tc.sunCoronaB * config->sunCoronaIntensity);
		if(tc.sunCoronaR > 255) tc.sunCoronaR = 255;
		if(tc.sunCoronaG > 255) tc.sunCoronaG = 255;
		if(tc.sunCoronaB > 255) tc.sunCoronaB = 255;
	}
	// Scale sun core color
	if(config->sunCoreIntensity != 1.0f){
		tc.sunCoreR = (short)(tc.sunCoreR * config->sunCoreIntensity);
		tc.sunCoreG = (short)(tc.sunCoreG * config->sunCoreIntensity);
		tc.sunCoreB = (short)(tc.sunCoreB * config->sunCoreIntensity);
		if(tc.sunCoreR > 255) tc.sunCoreR = 255;
		if(tc.sunCoreG > 255) tc.sunCoreG = 255;
		if(tc.sunCoreB > 255) tc.sunCoreB = 255;
	}
	// Scale lens flare streak intensity and size
	if(config->sunStreakIntensity != 1.0f){
		tc.spriteBrightness *= config->sunStreakIntensity;
	}
	if(config->sunStreakSize != 1.0f){
		tc.spriteSize *= config->sunStreakSize;
	}
}

// ============================================================
// Update — called each frame after CTimeCycle::Update()
// Blends game's m_CurrentColours with alternate timecyc values
// ============================================================

void Weather_Update(void){
	if(!g_altLoaded) return;

	// Get current weather state from game
	extern int16 &CWeather__OldWeatherType;
	extern int16 &CWeather__NewWeatherType;
	extern float &CWeather__InterpolationValue;

	int oldW = CWeather__OldWeatherType;
	int newW = CWeather__NewWeatherType;
	float wInterp = CWeather__InterpolationValue;

	// Get current time slot from game's timecycle
	// Game uses 8 slots: 0=0h, 1=5h, 2=6h, 3=7h, 4=12h, 5=19h, 6=20h, 7=22h
	static int timecycleHours[] = { 0, 5, 6, 7, 12, 19, 20, 22, 24 };
	extern uint8 &CClock__ms_nGameClockHours;
	extern uint8 &CClock__ms_nGameClockMinutes;
	int hour = (int)CClock__ms_nGameClockHours;
	int minute = (int)CClock__ms_nGameClockMinutes;
	float timeOfDay = (float)hour + (float)minute / 60.0f;

	// Find current time slot
	int curSlot = 0;
	for(int i = 0; i < 8; i++){
		if(timeOfDay >= (float)timecycleHours[i] && timeOfDay < (float)timecycleHours[i+1]){
			curSlot = i;
			break;
		}
	}

	// Get blend target from hash-based selector
	// Use old weather type for selection (the "current" weather)
	g_targetBlend = Weather_GetBlendFactor(oldW, curSlot);

	// Smooth transition (slow lerp toward target)
	g_currentBlend += (g_targetBlend - g_currentBlend) * 0.01f;

	// If blend is negligible, skip override
	if(fabs(g_currentBlend) < 0.01f && fabs(g_targetBlend) < 0.01f)
		return;

	// Read game's current interpolated colour set
	extern CColourSet &CTimeCycle__m_CurrentColours;
	CColourSet &tc = CTimeCycle__m_CurrentColours;

	// Calculate index into alternate timecyc array
	// 23 weathers × 8 slots, indexed as: weatherType * 8 + timeSlot
	int altIdxOld = oldW * 8 + curSlot;
	int altIdxNew = newW * 8 + curSlot;

	if(altIdxOld < 0 || altIdxOld >= 184) return;
	if(altIdxNew < 0 || altIdxNew >= 184) return;

	TimecycEntry *altOld = &g_altTimecyc[altIdxOld];
	TimecycEntry *altNew = &g_altTimecyc[altIdxNew];

	// Interpolate between old and new weather in alternate set
	float altR = (altOld->skyTopR * (1.0f - wInterp) + altNew->skyTopR * wInterp);
	float altG = (altOld->skyTopG * (1.0f - wInterp) + altNew->skyTopG * wInterp);
	float altB = (altOld->skyTopB * (1.0f - wInterp) + altNew->skyTopB * wInterp);

	// Blend game values with alternate values
	float t = g_currentBlend;
	tc.skyTopR   = (short)(tc.skyTopR   * (1.0f - t) + altR * t);
	tc.skyTopG = (short)(tc.skyTopG * (1.0f - t) + altG * t);
	tc.skyTopB  = (short)(tc.skyTopB  * (1.0f - t) + altB * t);

	// Sky bottom
	altR = altOld->skyBotR * (1.0f - wInterp) + altNew->skyBotR * wInterp;
	altG = altOld->skyBotG * (1.0f - wInterp) + altNew->skyBotG * wInterp;
	altB = altOld->skyBotB * (1.0f - wInterp) + altNew->skyBotB * wInterp;
	tc.skyBotR   = (short)(tc.skyBotR   * (1.0f - t) + altR * t);
	tc.skyBotG = (short)(tc.skyBotG * (1.0f - t) + altG * t);
	tc.skyBotB  = (short)(tc.skyBotB  * (1.0f - t) + altB * t);
	// Clamp to valid range (0-255)
	if(tc.skyBotR < 0) tc.skyBotR = 0; if(tc.skyBotR > 255) tc.skyBotR = 255;
	if(tc.skyBotG < 0) tc.skyBotG = 0; if(tc.skyBotG > 255) tc.skyBotG = 255;
	if(tc.skyBotB < 0) tc.skyBotB = 0; if(tc.skyBotB > 255) tc.skyBotB = 255;

	// Also clamp sky top
	if(tc.skyTopR < 0) tc.skyTopR = 0; if(tc.skyTopR > 255) tc.skyTopR = 255;
	if(tc.skyTopG < 0) tc.skyTopG = 0; if(tc.skyTopG > 255) tc.skyTopG = 255;
	if(tc.skyTopB < 0) tc.skyTopB = 0; if(tc.skyTopB > 255) tc.skyTopB = 255;

	// Ambient
	altR = altOld->ambR * (1.0f - wInterp) + altNew->ambR * wInterp;
	altG = altOld->ambG * (1.0f - wInterp) + altNew->ambG * wInterp;
	altB = altOld->ambB * (1.0f - wInterp) + altNew->ambB * wInterp;
	tc.ambientR   = tc.ambientR   * (1.0f - t) + (altR / 255.0f) * t;
	tc.ambientG = tc.ambientG * (1.0f - t) + (altG / 255.0f) * t;
	tc.ambientB  = tc.ambientB  * (1.0f - t) + (altB / 255.0f) * t;
	// Clamp ambient (float, 0.0-1.0 range typically)
	if(tc.ambientR < 0.0f) tc.ambientR = 0.0f; if(tc.ambientR > 1.0f) tc.ambientR = 1.0f;
	if(tc.ambientG < 0.0f) tc.ambientG = 0.0f; if(tc.ambientG > 1.0f) tc.ambientG = 1.0f;
	if(tc.ambientB < 0.0f) tc.ambientB = 0.0f; if(tc.ambientB > 1.0f) tc.ambientB = 1.0f;

	// Directional
	altR = altOld->dirR * (1.0f - wInterp) + altNew->dirR * wInterp;
	altG = altOld->dirG * (1.0f - wInterp) + altNew->dirG * wInterp;
	altB = altOld->dirB * (1.0f - wInterp) + altNew->dirB * wInterp;
	tc.directionalR   = tc.directionalR   * (1.0f - t) + (altR / 255.0f) * t;
	tc.directionalG = tc.directionalG * (1.0f - t) + (altG / 255.0f) * t;
	tc.directionalB  = tc.directionalB  * (1.0f - t) + (altB / 255.0f) * t;
	// Clamp directional
	if(tc.directionalR < 0.0f) tc.directionalR = 0.0f; if(tc.directionalR > 1.0f) tc.directionalR = 1.0f;
	if(tc.directionalG < 0.0f) tc.directionalG = 0.0f; if(tc.directionalG > 1.0f) tc.directionalG = 1.0f;
	if(tc.directionalB < 0.0f) tc.directionalB = 0.0f; if(tc.directionalB > 1.0f) tc.directionalB = 1.0f;

	// Sun core
	altR = altOld->sunCoreR * (1.0f - wInterp) + altNew->sunCoreR * wInterp;
	altG = altOld->sunCoreG * (1.0f - wInterp) + altNew->sunCoreG * wInterp;
	altB = altOld->sunCoreB * (1.0f - wInterp) + altNew->sunCoreB * wInterp;
	tc.sunCoreR   = (short)(tc.sunCoreR   * (1.0f - t) + altR * t);
	tc.sunCoreG = (short)(tc.sunCoreG * (1.0f - t) + altG * t);
	tc.sunCoreB  = (short)(tc.sunCoreB  * (1.0f - t) + altB * t);
	if(tc.sunCoreR < 0) tc.sunCoreR = 0; if(tc.sunCoreR > 255) tc.sunCoreR = 255;
	if(tc.sunCoreG < 0) tc.sunCoreG = 0; if(tc.sunCoreG > 255) tc.sunCoreG = 255;
	if(tc.sunCoreB < 0) tc.sunCoreB = 0; if(tc.sunCoreB > 255) tc.sunCoreB = 255;

	// Sun corona
	altR = altOld->sunCoronaR * (1.0f - wInterp) + altNew->sunCoronaR * wInterp;
	altG = altOld->sunCoronaG * (1.0f - wInterp) + altNew->sunCoronaG * wInterp;
	altB = altOld->sunCoronaB * (1.0f - wInterp) + altNew->sunCoronaB * wInterp;
	tc.sunCoronaR   = (short)(tc.sunCoronaR   * (1.0f - t) + altR * t);
	tc.sunCoronaG = (short)(tc.sunCoronaG * (1.0f - t) + altG * t);
	tc.sunCoronaB  = (short)(tc.sunCoronaB  * (1.0f - t) + altB * t);
	if(tc.sunCoronaR < 0) tc.sunCoronaR = 0; if(tc.sunCoronaR > 255) tc.sunCoronaR = 255;
	if(tc.sunCoronaG < 0) tc.sunCoronaG = 0; if(tc.sunCoronaG > 255) tc.sunCoronaG = 255;
	if(tc.sunCoronaB < 0) tc.sunCoronaB = 0; if(tc.sunCoronaB > 255) tc.sunCoronaB = 255;

	// === PS2→PC conversion fix: scale sun corona/core by config multipliers ===
	// On PS2, GS modulation (A×B)/128 ≈ MODULATE2X made sun sprites look correct.
	// On PC, D3D9 (A×B)/255 is dimmer, but R* kept the same timecycle values.
	// The PBR tonemap amplifies these values further, causing massive flare blowout.
	// Scale after interpolation so the game engine uses the corrected values.
	if(config->sunCoronaIntensity != 1.0f){
		tc.sunCoronaR = (short)(tc.sunCoronaR * config->sunCoronaIntensity);
		tc.sunCoronaG = (short)(tc.sunCoronaG * config->sunCoronaIntensity);
		tc.sunCoronaB = (short)(tc.sunCoronaB * config->sunCoronaIntensity);
		if(tc.sunCoronaR > 255) tc.sunCoronaR = 255;
		if(tc.sunCoronaG > 255) tc.sunCoronaG = 255;
		if(tc.sunCoronaB > 255) tc.sunCoronaB = 255;
	}
	if(config->sunCoreIntensity != 1.0f){
		tc.sunCoreR = (short)(tc.sunCoreR * config->sunCoreIntensity);
		tc.sunCoreG = (short)(tc.sunCoreG * config->sunCoreIntensity);
		tc.sunCoreB = (short)(tc.sunCoreB * config->sunCoreIntensity);
		if(tc.sunCoreR > 255) tc.sunCoreR = 255;
		if(tc.sunCoreG > 255) tc.sunCoreG = 255;
		if(tc.sunCoreB > 255) tc.sunCoreB = 255;
	}

	// Scale lens flare streak intensity and size (spriteBrightness / spriteSize)
	// These control the horizontal streak quads and lens flare sprites drawn by CCoronas::DoSunCorona
	if(config->sunStreakIntensity != 1.0f){
		tc.spriteBrightness *= config->sunStreakIntensity;
	}
	if(config->sunStreakSize != 1.0f){
		tc.spriteSize *= config->sunStreakSize;
	}

	// Fog — clamp fogStart to valid range
	float altFog = altOld->fogSt * (1.0f - wInterp) + altNew->fogSt * wInterp;
	tc.fogStart = tc.fogStart * (1.0f - t) + altFog * t;
	if(tc.fogStart < 0.0f) tc.fogStart = 0.0f;

	// Low clouds
	altR = altOld->lowCloudsR * (1.0f - wInterp) + altNew->lowCloudsR * wInterp;
	altG = altOld->lowCloudsG * (1.0f - wInterp) + altNew->lowCloudsG * wInterp;
	altB = altOld->lowCloudsB * (1.0f - wInterp) + altNew->lowCloudsB * wInterp;
	tc.lowCloudsR   = (short)(tc.lowCloudsR   * (1.0f - t) + altR * t);
	tc.lowCloudsG = (short)(tc.lowCloudsG * (1.0f - t) + altG * t);
	tc.lowCloudsB  = (short)(tc.lowCloudsB  * (1.0f - t) + altB * t);
	if(tc.lowCloudsR < 0) tc.lowCloudsR = 0; if(tc.lowCloudsR > 255) tc.lowCloudsR = 255;
	if(tc.lowCloudsG < 0) tc.lowCloudsG = 0; if(tc.lowCloudsG > 255) tc.lowCloudsG = 255;
	if(tc.lowCloudsB < 0) tc.lowCloudsB = 0; if(tc.lowCloudsB > 255) tc.lowCloudsB = 255;
}

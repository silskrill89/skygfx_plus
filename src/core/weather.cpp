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

// Select which weather set to use for a given type+slot
// ALWAYS returns WEATHER_SET_2 (weathers2.dat) so the custom timecyc drives
// lighting every frame. Previously hash-based (~30% alternate), which meant the
// custom low-ambient/Dir=255 values only applied intermittently — garages were
// not consistently black and reflections were unreliable. User confirmed
// always-apply is the desired behavior.
int Weather_SelectSet(int weatherType, int timeSlot){
	(void)weatherType; (void)timeSlot;
	return WEATHER_SET_2;
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
			int skyTopR, skyTopG, skyTopB;
			int skyBotR, skyBotG, skyBotB;
			int sunCoreR, sunCoreG, sunCoreB;
			int sunCoronaR, sunCoronaG, sunCoronaB;
			// Float temps — the game's own parser (CTimeCycle::Initialise,
			// gta-reversed TimeCycle.cpp 0x5BBAC0) reads these columns as
			// FLOATS. The old format read them with %hhd/%hd, which stops
			// dead on "2.20" (parses '2', fails at '.') — sscanf returned 22
			// of 52 fields and everything after sunSz stayed 0 (fogStart
			// blended to 0.0 every frame by Weather_Update).
			float sunSzF, sprSzF, sprBghtF;
			int shdwI, lightShdI, poleShdI;
			float farClpF, fogStF, lightOnGroundF;
			int lowR, lowG, lowB, botR, botG, botB;
			float watR, watG, watB, watA;
			float fx1A, fx1R, fx1G, fx1B;
			float fx2A, fx2R, fx2G, fx2B;
			float cloudAlphaF;
			int highLightI, waterFogI;
			float dirMultF;

			// Column order mirrors CTimeCycle::Initialise exactly (52 fields):
			// Amb AmbObj Dir SkyTop SkyBot SunCore SunCorona (RGB)
			// SunSz SprSz SprBght (floats) Shdw LightShd PoleShd (ints)
			// FarClp FogSt LightOnGround (floats) LowClouds BottomCloud (RGB)
			// Water RGBA PostFx1 ARGB PostFx2 ARGB (floats)
			// CloudAlpha HighLightMinIntensity WaterFogAlpha (ints) DirMult (float)
			int n = sscanf(line,
				"%d %d %d %d %d %d %d %d %d "		// amb ambObj dir (9)
				"%d %d %d %d %d %d %d %d %d %d %d %d "	// skyTop skyBot sunCore sunCorona (12)
				"%f %f %f "				// sunSz sprSz sprBght (floats, as the game reads them)
				"%d %d %d "				// shdw lightShd poleShd
				"%f %f %f "				// farClp fogSt lightOnGround (floats)
				"%d %d %d %d %d %d "			// lowClouds bottomCloud RGB
				"%f %f %f %f "				// water RGBA
				"%f %f %f %f "				// postFx1 A RGB
				"%f %f %f %f "				// postFx2 A RGB
				"%f %d %d %f",				// cloudAlpha highLight waterFog dirMult
				&r, &g, &b,
				&ambObjR, &ambObjG, &ambObjB,
				&dirR, &dirG, &dirB,
				&skyTopR, &skyTopG, &skyTopB,
				&skyBotR, &skyBotG, &skyBotB,
				&sunCoreR, &sunCoreG, &sunCoreB,
				&sunCoronaR, &sunCoronaG, &sunCoronaB,
				&sunSzF, &sprSzF, &sprBghtF,
				&shdwI, &lightShdI, &poleShdI,
				&farClpF, &fogStF, &lightOnGroundF,
				&lowR, &lowG, &lowB, &botR, &botG, &botB,
				&watR, &watG, &watB, &watA,
				&fx1A, &fx1R, &fx1G, &fx1B,
				&fx2A, &fx2R, &fx2G, &fx2B,
				&cloudAlphaF, &highLightI, &waterFogI, &dirMultF
			);

			// Game warns at n<51 (R* line-320 typo); we require the same so a
			// truncated parse can never silently zero the tail columns.
			if(n >= 51){
				e->ambR = (unsigned char)r;
				e->ambG = (unsigned char)g;
				e->ambB = (unsigned char)b;
				e->ambObjR = (unsigned char)ambObjR;
				e->ambObjG = (unsigned char)ambObjG;
				e->ambObjB = (unsigned char)ambObjB;
				e->dirR = (unsigned char)dirR;
				e->dirG = (unsigned char)dirG;
				e->dirB = (unsigned char)dirB;
				e->skyTopR = (unsigned char)skyTopR;
				e->skyTopG = (unsigned char)skyTopG;
				e->skyTopB = (unsigned char)skyTopB;
				e->skyBotR = (unsigned char)skyBotR;
				e->skyBotG = (unsigned char)skyBotG;
				e->skyBotB = (unsigned char)skyBotB;
				e->sunCoreR = (unsigned char)sunCoreR;
				e->sunCoreG = (unsigned char)sunCoreG;
				e->sunCoreB = (unsigned char)sunCoreB;
				e->sunCoronaR = (unsigned char)sunCoronaR;
				e->sunCoronaG = (unsigned char)sunCoronaG;
				e->sunCoronaB = (unsigned char)sunCoronaB;
				// Same quantization the game applies to its static arrays
				e->sunSz = (signed char)(sunSzF * 10.0f + 0.5f);
				e->sprSz = (signed char)(sprSzF * 10.0f + 0.5f);
				e->sprBght = (signed char)(sprBghtF * 10.0f + 0.5f);
				e->shdw = (unsigned char)shdwI;
				e->lightShd = (unsigned char)lightShdI;
				e->poleShd = (unsigned char)poleShdI;
				e->farClp = (signed short)farClpF;
				e->fogSt = (signed short)fogStF;
				e->lightOnGround = (unsigned char)(lightOnGroundF * 10.0f + 0.5f);
				e->lowCloudsR = (unsigned char)lowR;
				e->lowCloudsG = (unsigned char)lowG;
				e->lowCloudsB = (unsigned char)lowB;
				e->fluffyCloudR = (unsigned char)botR;
				e->fluffyCloudG = (unsigned char)botG;
				e->fluffyCloudB = (unsigned char)botB;
				e->waterR = (unsigned char)watR;
				e->waterG = (unsigned char)watG;
				e->waterB = (unsigned char)watB;
				e->waterA = (unsigned char)watA;
				e->postFx1A = (unsigned char)(fx1A * 2.0f);
				e->postFx1R = (unsigned char)fx1R;
				e->postFx1G = (unsigned char)fx1G;
				e->postFx1B = (unsigned char)fx1B;
				e->postFx2A = (unsigned char)(fx2A * 2.0f);
				e->postFx2R = (unsigned char)fx2R;
				e->postFx2G = (unsigned char)fx2G;
				e->postFx2B = (unsigned char)fx2B;
				e->cloudAlpha = (unsigned char)cloudAlphaF;
				e->highLightMinIntensity = (unsigned int)highLightI;
				e->waterFogAlpha = (unsigned short)waterFogI;
				e->illumination = dirMultF;
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

	// altTimecycEnable=0 (default): vanilla behavior. The weathers2.dat
	// set-2 overwrite below forces g_currentBlend=1.0 and replaces the
	// game's CColourSet ambient (near-black daytime) — skip the entire
	// overwrite block so the game's own ambient/direct values flow through.
	if(!config->altTimecycEnable){
		static bool s_loggedDisabled = false;
		if(!s_loggedDisabled){
			s_loggedDisabled = true;
			dbglog("[wx_amb] alt timecyc DISABLED (altTimecycEnable=0) - vanilla ambient in use");
		}
		return;
	}

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

	// Always apply weathers2.dat (set 2) immediately. Weather_SelectSet now
	// always returns WEATHER_SET_2, so g_targetBlend is always 1.0. The old
	// slow lerp (* 0.01f) ramped g_currentBlend 0→1 over ~7.6s, leaving a
	// startup transient where garages weren't yet black. Set directly instead.
	g_currentBlend = g_targetBlend;

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
	float preAmbR = tc.ambientR, preAmbG = tc.ambientG, preAmbB = tc.ambientB;
	altR = altOld->ambR * (1.0f - wInterp) + altNew->ambR * wInterp;
	altG = altOld->ambG * (1.0f - wInterp) + altNew->ambG * wInterp;
	altB = altOld->ambB * (1.0f - wInterp) + altNew->ambB * wInterp;
	float altAmbR = altR, altAmbG = altG, altAmbB = altB;
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

	// Discriminating log for the "outdoor cutscene dark" dig: what the
	// overwrite did to ambient/directional this frame (2s throttle).
	if(dbglog_throttle("wx_amb"))
		dbglog("[wx_amb] h=%d:%02d slot=%d w=%d/%d t=%.2f pre=(%.3f,%.3f,%.3f) "
		       "post=(%.3f,%.3f,%.3f) alt=(%.3f,%.3f,%.3f) dir post=(%.3f,%.3f,%.3f)",
		       hour, minute, curSlot, oldW, newW, t,
		       preAmbR, preAmbG, preAmbB,
		       tc.ambientR, tc.ambientG, tc.ambientB,
		       altAmbR / 255.0f, altAmbG / 255.0f, altAmbB / 255.0f,
		       tc.directionalR, tc.directionalG, tc.directionalB);

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

	// === Config multipliers (corona/core/streaks) are NOT applied here ===
	// Weather_ApplySunConfig() runs unconditionally right after this
	// (main.cpp: Weather_Update(); Weather_ApplySunConfig();) and owns all
	// of them. Multiplying in BOTH functions double-applied every field
	// per frame (spriteBrightness, spriteSize, sunCore, sunCorona each
	// squared by config->sunStreakIntensity etc.).

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

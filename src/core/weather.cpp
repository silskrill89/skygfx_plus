// weather.cpp — Multi-timecyc weather system
// Sits on top of game's CTimeCycle, overrides m_CurrentColours
// Pattern: fixed set selection — always weathers2.dat (see Weather_SelectSet)
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
static TimecycEntry g_altTimecyc[184];    // set 2 — weathers2.dat
static TimecycEntry g_baseTimecyc[184];   // set 1 — data\timecyc.dat (vanilla)
static bool g_altLoaded = false;
static bool g_baseLoaded = false;
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
// Timecyc parser — reads a timecycle file (weathers2.dat or timecyc.dat)
// into a TimecycEntry[184] block supplied by the caller.
// Format matches game's timecyc.dat exactly (tab-separated values)
// ============================================================

static bool ParseTimecycFile(const char *path, TimecycEntry *dst){
	FILE *f = fopen(path, "r");
	if(!f) return false;

	char line[1024];
	int entryIdx = 0;
	memset(dst, 0, sizeof(TimecycEntry) * 184);

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
			TimecycEntry *e = &dst[entryIdx];
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
	g_altLoaded = ParseTimecycFile(path, g_altTimecyc);
	dbglog("Weather_Init: weathers2=%s (%d entries)", g_altLoaded ? "OK" : "FAIL",
	       g_altLoaded ? 184 : 0);

	// Load the vanilla timecyc (data\timecyc.dat) too — READ-ONLY use of the
	// shipped file, nothing in data/ is modified. We need it for the "Dir"
	// column, which the GAME throws away at load (CTimeCycle::Initialise
	// parses dirR/dirG/dirB into stack locals and never stores them) — see
	// Weather_ApplyDirColumn for the full root-cause chain. Without our own
	// copy there is no way to recover the authored sun colour at runtime.
	snprintf(path, sizeof(path), "%s\\data\\timecyc.dat", gameDir);
	g_baseLoaded = ParseTimecycFile(path, g_baseTimecyc);
	dbglog("Weather_Init: timecyc=%s (%d entries)", g_baseLoaded ? "OK" : "FAIL",
	       g_baseLoaded ? 184 : 0);
}

// ============================================================
// Apply sun config multipliers to m_CurrentColours.
// Called every frame from RenderScene_hook, after CTimeCycle::Update()
// but before any rendering reads the values.
// Runs unconditionally — no weather blend or alt-timecyc checks.
// ============================================================

// Byte-valued CColourSet fields (sunCore/sunCorona are `short` 0..255 in
// gta.h): scale, then clamp to the byte range in BOTH directions.
// The old code clamped only the >255 side, so a negative
// sunCoreIntensity/sunCoronaIntensity INI value pushed negative shorts
// straight into every consumer (corona/core sprites, tonemap sun term).
// Truncation on the way back to short is unchanged (same `(short)` cast as
// before for any value that already fits). `!(v >= 0.0f)` also folds NaN
// into 0 instead of letting the cast produce an undefined short.
static short ScaleColourByte(short value, float mult){
	float v = (float)value * mult;
	if(!(v >= 0.0f)) return 0;      // negative or NaN -> 0
	if(v > 255.0f) return 255;
	return (short)v;
}

void Weather_ApplySunConfig(void){
	extern CColourSet &CTimeCycle__m_CurrentColours;
	CColourSet &tc = CTimeCycle__m_CurrentColours;

	// Scale sun corona color (PS2→PC conversion fix)
	if(config->sunCoronaIntensity != 1.0f){
		tc.sunCoronaR = ScaleColourByte(tc.sunCoronaR, config->sunCoronaIntensity);
		tc.sunCoronaG = ScaleColourByte(tc.sunCoronaG, config->sunCoronaIntensity);
		tc.sunCoronaB = ScaleColourByte(tc.sunCoronaB, config->sunCoronaIntensity);
	}
	// Scale sun core color
	if(config->sunCoreIntensity != 1.0f){
		tc.sunCoreR = ScaleColourByte(tc.sunCoreR, config->sunCoreIntensity);
		tc.sunCoreG = ScaleColourByte(tc.sunCoreG, config->sunCoreIntensity);
		tc.sunCoreB = ScaleColourByte(tc.sunCoreB, config->sunCoreIntensity);
	}
	// Scale lens flare streak intensity and size. These are floats, not byte
	// fields, so the [0,255] clamp does not apply — but a negative multiplier
	// would still push an invalid negative sprite value, so clamp the lower
	// bound too. Valid (non-negative) configs are unchanged.
	if(config->sunStreakIntensity != 1.0f){
		float v = tc.spriteBrightness * config->sunStreakIntensity;
		tc.spriteBrightness = (v >= 0.0f) ? v : 0.0f;
	}
	if(config->sunStreakSize != 1.0f){
		float v = tc.spriteSize * config->sunStreakSize;
		tc.spriteSize = (v >= 0.0f) ? v : 0.0f;
	}
}

// Push a corrected directional colour into the game's live sun light.
//
// The game caches m_CurrentColours.directional* into
// DirectionalLightColourForFrame (0xC886B4) and pDirect inside
// SetLightsWithTimeOfDayColour (0x7354E0), called at 0x53E997 in CScene::Render.
// That happens BEFORE our RenderScene hook (0x53EABF, see main.cpp
// installDeferredHooks), i.e. before Weather_Update can repair the field — so
// writing m_CurrentColours alone fixes waterPipe/colourfilter/underwater (they
// read the struct during drawing) but leaves buildingPipe's pDirect black/red
// for the whole frame. Re-push the corrected value here, mirroring exactly what
// the game's own light update would have produced from the authored Dir column.
// Only the directional colour is touched; pAmbient and every other light cached
// by SetLightsWithTimeOfDayColour keep the game's values.
static void Weather_SyncDirectionalLight(const CColourSet &tc){
	// Interiors run the game's extra-colour/interior lighting path; the sun term
	// is legitimately ~0 there. Never inject the outdoor Dir column indoors.
	if(CGame__currArea && *CGame__currArea != 0) return;

	extern RwRGBAReal &DirectionalLightColourForFrame;	// main.cpp:2981 (0xC886B4)
	DirectionalLightColourForFrame.red   = tc.directionalR;
	DirectionalLightColourForFrame.green = tc.directionalG;
	DirectionalLightColourForFrame.blue  = tc.directionalB;
	if(pDirect)
		RpLightSetColor(pDirect, &DirectionalLightColourForFrame);
}

// ============================================================
// Dir-column restore (the m_CurrentColours.directional fix)
//
// ROOT CAUSE (traced 2026-11-08, see report):
//   1. SA's CTimeCycle::Initialise (0x5BBAC0) sscanf's the timecyc "Dir"
//      column into stack locals dirR/dirG/dirB and NEVER stores them
//      (gta-reversed TimeCycle.cpp: sscanf comment "Direct light color -
//      NOP"; the store is absent — "// code dir RGB?"). There is no
//      m_nDirectionalRed-style array in the game at all (only
//      m_nDirectionalMult = the trailing DirMult column).
//   2. CColourSet's third float triplet (offset 0x18) — the field gta.h:303
//      names directionalR/G/B — is the slot gta-reversed documents as
//      m_fAmbientBeforeBrightness* (// m_fDirectional); plugin-sdk hedges by
//      naming it m_fDirectionalRed while also exposing
//      CTimeCycle::GetAmbient*_BeforeBrightness for the same value. The game
//      fills it from the AMBIENT column (before-brightness ambient), never
//      from Dir: EXTRASUNNY_LA Midday has Amb=11 0 0 / Dir=255 255 255, yet
//      the live slot reports (0.039, 0, 0) — G=B exactly 0, tracking Amb, not
//      the white Dir column. Verified in skygfx_dbg.log line 9095:
//      "[Tonemap] TC ... dir=(0.039,0.000,0.000) ... hour=12".
//   3. Hence data Amb=11 0 0 + Dir=255 255 255 at EXTRASUNNY_LA Midday
//      reports tc.directional = (0.039, 0.000, 0.000) at hour 12 — a
//      near-black RED "sun term" — for every consumer that reads it as
//      the directional sun colour (waterPipe.cpp:2145, colourfilter
//      sceneLuma at postfx_colourfilter.cpp:363, postfx_underwater.cpp:412).
//
// The alt (weathers2.dat) path already re-applies Dir in the directional
// block of Weather_Update below; this helper does the same for the vanilla
// (altTimecycEnable=0) path from data\timecyc.dat, which previously left
// the game's ambient-derived value in place. It writes ONLY
// tc.directionalR/G/B — every other CColourSet field keeps whatever the
// game produced — so ambient/sky/water/postfx behaviour is untouched.
// ============================================================
static void Weather_ApplyDirColumn(const TimecycEntry *set, int oldW, int newW, int slot, float wInterp){
	// 23 weathers × 8 slots, same indexing as the rest of this file
	if(oldW < 0 || oldW >= 23 || newW < 0 || newW >= 23) return;
	if(slot < 0 || slot >= 8) return;
	// Interiors (shops, safehouses, mission rooms) run on the game's interior /
	// extra-colour path, not the outdoor weather cycle, and their sun term is
	// legitimately ~0. Leave the game's value untouched there; garages have
	// currArea==0 and still receive the authored sun, which is correct.
	if(CGame__currArea && *CGame__currArea != 0) return;
	// Defensive: CWeather::InterpolationValue should be [0,1]; `!(v >= 0)`
	// also folds NaN so a bad game value can never poison the lerp.
	if(!(wInterp >= 0.0f)) wInterp = 0.0f;
	if(wInterp > 1.0f) wInterp = 1.0f;

	const TimecycEntry *a = &set[oldW * 8 + slot];
	const TimecycEntry *b = &set[newW * 8 + slot];

	// Same old/new-weather blend the alt path uses for every other column
	float r = (float)a->dirR * (1.0f - wInterp) + (float)b->dirR * wInterp;
	float g = (float)a->dirG * (1.0f - wInterp) + (float)b->dirG * wInterp;
	float bl = (float)a->dirB * (1.0f - wInterp) + (float)b->dirB * wInterp;

	// byte -> 0..1 float, the convention CColourSet ambient/directional use
	// (the game itself divides by 255). Clamp BOTH directions so a bad file
	// value can never push the field outside [0,1].
	r /= 255.0f; g /= 255.0f; bl /= 255.0f;
	if(!(r >= 0.0f)) r = 0.0f; else if(r > 1.0f) r = 1.0f;
	if(!(g >= 0.0f)) g = 0.0f; else if(g > 1.0f) g = 1.0f;
	if(!(bl >= 0.0f)) bl = 0.0f; else if(bl > 1.0f) bl = 1.0f;

	extern CColourSet &CTimeCycle__m_CurrentColours;
	CColourSet &tc = CTimeCycle__m_CurrentColours;
	tc.directionalR = r;
	tc.directionalG = g;
	tc.directionalB = bl;

	// m_CurrentColours is not enough for the RpLight consumer — re-push the
	// corrected colour into pDirect too (see Weather_SyncDirectionalLight).
	Weather_SyncDirectionalLight(tc);

	extern uint8 &CClock__ms_nGameClockHours;
	extern uint8 &CClock__ms_nGameClockMinutes;
	if(dbglog_throttle("wx_dir"))
		dbglog("[wx_dir] h=%d:%02d slot=%d w=%d/%d wi=%.2f dir=(%.3f,%.3f,%.3f) src=timecyc.dat",
		       (int)CClock__ms_nGameClockHours, (int)CClock__ms_nGameClockMinutes,
		       slot, oldW, newW, wInterp, tc.directionalR, tc.directionalG, tc.directionalB);
}

// ============================================================
// Update — called each frame after CTimeCycle::Update()
// Blends game's m_CurrentColours with alternate timecyc values
// ============================================================

void Weather_Update(void){
	// Need at least one parsed set: weathers2.dat (alt path) or
	// timecyc.dat (Dir-column-only path below).
	if(!g_altLoaded && !g_baseLoaded) return;

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

	// altTimecycEnable=0 (default): vanilla behavior. The weathers2.dat
	// set-2 overwrite below forces g_currentBlend=1.0 and replaces the
	// game's CColourSet ambient (near-black daytime) — skip the entire
	// overwrite block so the game's own ambient/sky/water values flow through.
	// EXCEPTION: directional. The game NEVER carries the timecyc Dir column
	// into m_CurrentColours (CalcColoursForPoint leaves ambient-before-
	// brightness in that slot — see Weather_ApplyDirColumn), so "flowing
	// through" here shipped the black/red sun term (0.039,0,0 at noon).
	// Restore ONLY the Dir column from data\timecyc.dat for that field.
	if(!config->altTimecycEnable){
		static bool s_loggedDisabled = false;
		if(!s_loggedDisabled){
			s_loggedDisabled = true;
			dbglog("[wx_amb] alt timecyc DISABLED (altTimecycEnable=0) - vanilla ambient in use");
		}
		if(g_baseLoaded)
			Weather_ApplyDirColumn(g_baseTimecyc, oldW, newW, curSlot, wInterp);
		return;
	}

	if(!g_altLoaded) return;

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
	// The RpLight path runs before this hook; re-push so buildingPipe's pDirect
	// sees the same corrected value (interior-guarded inside the helper).
	Weather_SyncDirectionalLight(tc);

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

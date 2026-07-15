// wheels_extender.cpp — Wheel DFF extender for SkyGFX Plus
// Extends the existing vehicle pipe system to support more wheel styles
// Uses WRAPPER pattern from gta.cpp for RW functions
//
// Config: wheels_extender.ini (lives next to skygfx.ini)

#include "skygfx.h"
#include "ini_parser.hpp"
#include <stdio.h>
#include <string.h>
#include <io.h>
#include <stdlib.h>

// ============================================================
// Configuration
// ============================================================

#define WHEEL_MAX_CLASSES 8
#define WHEEL_DEFAULT_MAX 512

struct WheelsExtenderConfig {
	int enabled;
	int bypass;                     // 1 = skip all initialization (safety switch)
	char wheelsDir[MAX_PATH];
	int maxWheels;
	int debugLogging;
	float classWeights[8][WHEEL_MAX_CLASSES];
};

static WheelsExtenderConfig weConfig;

// Default class weights (matches veh_shaders paint/tire pattern)
static const float defaultClassWeights[8][WHEEL_MAX_CLASSES] = {
	// sport  muscle  suv   sedan  bike  truck  lowrider tuner
	{  0.05f, 0.10f, 0.10f, 0.50f, 0.00f, 0.10f, 0.05f, 0.10f }, // standard
	{  0.50f, 0.15f, 0.00f, 0.05f, 0.00f, 0.00f, 0.00f, 0.30f }, // sport
	{  0.10f, 0.50f, 0.05f, 0.10f, 0.00f, 0.05f, 0.10f, 0.10f }, // muscle
	{  0.00f, 0.05f, 0.00f, 0.10f, 0.00f, 0.00f, 0.75f, 0.10f }, // lowrider
	{  0.00f, 0.05f, 0.40f, 0.10f, 0.00f, 0.40f, 0.00f, 0.05f }, // truck
	{  0.30f, 0.05f, 0.05f, 0.30f, 0.00f, 0.00f, 0.00f, 0.30f }, // luxury
	{  0.05f, 0.30f, 0.05f, 0.20f, 0.00f, 0.05f, 0.25f, 0.10f }, // classic
	{  0.00f, 0.00f, 0.00f, 0.00f, 1.00f, 0.00f, 0.00f, 0.00f }, // bike
};

static float readfloat_we(const std::string &s, float def) {
	try { return std::stof(s); } catch(...) { return def; }
}

static int readint_we(const std::string &s, int def) {
	try { return std::stoi(s); } catch(...) { return def; }
}

void WheelsExtender_LoadConfig(const char *iniPath) {
	// Defaults
	weConfig.enabled = 0;
	weConfig.bypass = 1;
	strncpy(weConfig.wheelsDir, "models\\wheels\\", MAX_PATH);
	weConfig.maxWheels = WHEEL_DEFAULT_MAX;
	weConfig.debugLogging = 0;
	memcpy(weConfig.classWeights, defaultClassWeights, sizeof(defaultClassWeights));

	linb::ini cfg;
	bool existed = cfg.load_file(iniPath);

	if(!existed) {
		dbglog("WheelsExtender: no wheels_extender.ini found, bypassed (safe defaults)");
		return;
	}

	weConfig.enabled = readint_we(cfg.get("WheelExtender", "enabled", "0"), 0);
	weConfig.bypass = readint_we(cfg.get("WheelExtender", "bypass", "1"), 1);

	std::string dir = cfg.get("WheelExtender", "wheelsDir", "models\\wheels\\");
	if(!dir.empty()) strncpy(weConfig.wheelsDir, dir.c_str(), MAX_PATH - 1);
	weConfig.wheelsDir[MAX_PATH - 1] = '\0';

	weConfig.maxWheels = readint_we(cfg.get("WheelExtender", "maxWheels", "512"), 512);
	if(weConfig.maxWheels < 1) weConfig.maxWheels = 1;
	if(weConfig.maxWheels > 512) weConfig.maxWheels = 512;

	weConfig.debugLogging = readint_we(cfg.get("WheelExtender", "debugLogging", "0"), 0);

	// Class weight overrides: weights0..weights7
	// Format: "sport,muscle,suv,sedan,bike,truck,lowrider,tuner"
	for(int g = 0; g < 8; g++) {
		char key[16];
		snprintf(key, sizeof(key), "weights%d", g);
		std::string val = cfg.get("WheelExtender", key, "");
		if(val.empty()) continue;

		// Parse comma-separated floats
		char buf[256];
		strncpy(buf, val.c_str(), sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';

		int c = 0;
		char *tok = strtok(buf, ",");
		while(tok && c < WHEEL_MAX_CLASSES) {
			weConfig.classWeights[g][c] = (float)atof(tok);
			tok = strtok(NULL, ",");
			c++;
		}
	}

	dbglog("WheelsExtender: config loaded (enabled=%d, bypass=%d, dir=%s, max=%d, debug=%d)",
	       weConfig.enabled, weConfig.bypass, weConfig.wheelsDir, weConfig.maxWheels, weConfig.debugLogging);

	// Write back with any missing keys
	if(!existed || cfg.get("WheelExtender", "enabled", "").empty()) {
		cfg.set("WheelExtender", "; wheels_extender.ini — Wheel Extender Config (lives next to skygfx.ini)", "");
		cfg.set("WheelExtender", "", "");
		cfg.set("WheelExtender", "; Enable wheel extender (0=off, 1=on)", "");
		cfg.set("WheelExtender", "enabled", "0");
		cfg.set("WheelExtender", "; Bypass: skip ALL initialization (safety switch for debugging)", "");
		cfg.set("WheelExtender", "bypass", "1");
		cfg.set("WheelExtender", "; Directory for wheel DFFs (relative to game root)", "");
		cfg.set("WheelExtender", "wheelsDir", "models\\wheels\\");
		cfg.set("WheelExtender", "; Maximum number of wheels to load", "");
		cfg.set("WheelExtender", "maxWheels", "512");
		cfg.set("WheelExtender", "; Debug logging (0=off, 1=log every selection)", "");
		cfg.set("WheelExtender", "debugLogging", "0");
		cfg.set("WheelExtender", "", "");
		cfg.set("WheelExtender", "; Vehicle group wheel class weight overrides", "");
		cfg.set("WheelExtender", "; Format: sport,muscle,suv,sedan,bike,truck,lowrider,tuner", "");
		cfg.set("WheelExtender", "; Groups: 0=standard 1=sport 2=muscle 3=lowrider 4=truck 5=luxury 6=classic 7=bike", "");
		cfg.write_file(iniPath);
	}
}

const WheelsExtenderConfig* WheelsExtender_GetConfig(void) {
	return &weConfig;
}

// ============================================================
// Wheel DFF pool
// ============================================================

struct WheelEntry {
	char name[64];
	char frameName[64];
	RpAtomic* atomic;
	RpClump* clump;
	int wheelClass;
	bool loaded;
};

static WheelEntry *g_wheels = NULL;
static int g_wheelCount = 0;
static int g_wheelCapacity = 0;
static bool g_wheelsInitialized = false;

// ============================================================
// Vehicle classification (same as veh_shaders.cpp)
// ============================================================

extern int GetVehicleGroup(int modelID);

// ============================================================
// Hash-based wheel selection
// ============================================================

static int SelectWheelClass(int modelID) {
	int group = GetVehicleGroup(modelID);
	if(group < 0 || group >= 8) group = 0;

	unsigned int hash = (unsigned int)(modelID * 2654435761u);
	hash = (hash ^ (hash >> 16)) * 0x45d9f3b;
	float r = (float)(hash & 0xFFFF) / 65535.0f;

	const float *weights = weConfig.classWeights[group];
	float cum = 0.0f;
	for(int i = 0; i < WHEEL_MAX_CLASSES; i++) {
		cum += weights[i];
		if(r <= cum) return i;
	}
	return 0;
}

WheelEntry* Wheels_SelectForVehicle(int modelID) {
	if(g_wheelCount == 0) return NULL;

	int wheelClass = SelectWheelClass(modelID);

	int candidates[512];
	int numCandidates = 0;
	for(int i = 0; i < g_wheelCount; i++) {
		if(g_wheels[i].wheelClass == wheelClass && g_wheels[i].loaded) {
			candidates[numCandidates++] = i;
		}
	}

	if(numCandidates == 0) {
		unsigned int hash = (unsigned int)(modelID * 2654435761u);
		hash = (hash ^ (hash >> 16)) * 0x45d9f3b;
		return &g_wheels[hash % g_wheelCount];
	}

	unsigned int hash = (unsigned int)(modelID * 2654435761u);
	hash = (hash ^ (hash >> 16)) * 0x45d9f3b;
	return &g_wheels[candidates[hash % numCandidates]];
}

// ============================================================
// Load wheel DFFs
// ============================================================

static RpAtomic* FindFirstAtomicCB(RpAtomic* atomic, void* pData) {
	RpAtomic** found = (RpAtomic**)pData;
	*found = atomic;
	return NULL;
}

void WheelsExtender_Init(const char *gameDir) {
	if(g_wheelsInitialized) return;
	g_wheelsInitialized = true;

	// Load config from wheels_extender.ini (next to skygfx.ini)
	char iniPath[MAX_PATH];
	extern char asipath[];
	snprintf(iniPath, MAX_PATH, "%swheels_extender.ini", asipath);
	WheelsExtender_LoadConfig(iniPath);

	if(weConfig.bypass || !weConfig.enabled) {
		dbglog("WheelsExtender: bypassed (bypass=%d, enabled=%d)", weConfig.bypass, weConfig.enabled);
		return;
	}

	// Allocate pool
	g_wheelCapacity = weConfig.maxWheels;
	g_wheels = (WheelEntry*)calloc(g_wheelCapacity, sizeof(WheelEntry));
	if(!g_wheels) {
		dbglog("WheelsExtender: failed to allocate wheel pool (%d entries)", g_wheelCapacity);
		return;
	}

	char wheelsDir[MAX_PATH];
	snprintf(wheelsDir, MAX_PATH, "%s\\%s", gameDir, weConfig.wheelsDir);

	WIN32_FIND_DATA fd;
	char search[MAX_PATH];
	snprintf(search, MAX_PATH, "%s*.dff", wheelsDir);

	HANDLE h = FindFirstFile(search, &fd);
	if(h == INVALID_HANDLE_VALUE) {
		dbglog("WheelsExtender: no wheel DFFs found in %s", wheelsDir);
		return;
	}

	do {
		if(g_wheelCount >= g_wheelCapacity) break;

		char path[MAX_PATH];
		snprintf(path, MAX_PATH, "%s%s", wheelsDir, fd.cFileName);

		RwStream *stream = RwStreamOpen(rwSTREAMFILENAME, rwSTREAMREAD, path);
		if(!stream) continue;

		if(!RwStreamFindChunk(stream, rwID_CLUMP, NULL, NULL)) {
			RwStreamClose(stream, NULL);
			continue;
		}

		RpClump *clump = RpClumpStreamRead(stream);
		RwStreamClose(stream, NULL);

		if(!clump) continue;

		RpAtomic *atomic = NULL;
		RpClumpForAllAtomics(clump, FindFirstAtomicCB, &atomic);
		if(!atomic) {
			RpClumpDestroy(clump);
			continue;
		}

		WheelEntry *w = &g_wheels[g_wheelCount];
		strncpy(w->name, fd.cFileName, 63);
		w->name[63] = '\0';
		char *dot = strrchr(w->name, '.');
		if(dot) *dot = '\0';

		RwFrame *frame = RpAtomicGetFrame(atomic);
		if(frame) {
			char *frameName = GetFrameNodeName(frame);
			if(frameName) strncpy(w->frameName, frameName, 63);
		}else{
			dbglog("WheelsExtender: SKIP %s (no frame)", fd.cFileName);
			RpClumpDestroy(clump);
			continue;
		}

		w->atomic = atomic;
		w->clump = clump;
		w->wheelClass = SelectWheelClass(0);
		w->loaded = true;

		g_wheelCount++;
	}while(FindNextFile(h, &fd));

	FindClose(h);

	dbglog("WheelsExtender: loaded %d wheel DFFs (capacity %d)", g_wheelCount, g_wheelCapacity);
}

// ============================================================
// Wheel render hook
// ============================================================

static RpAtomic* g_currentWheelAtomic = NULL;
static int g_currentWheelFrame = -1;

static void (*RpAtomicSetGeometry_orig)(RpAtomic *atomic, RpGeometry *geometry, int sameBoundingSphere) =
	(void (*)(RpAtomic*, RpGeometry*, int))0x749D40;

RpAtomic* WheelsExtender_RenderWheelCB(RpAtomic *atomic) {
	if(!atomic) return atomic;

	extern int VehShaders_GetModelIndex(void *atomic);
	int modelIndex = VehShaders_GetModelIndex(atomic);

	WheelEntry *selectedWheel = Wheels_SelectForVehicle(modelIndex);

	if(selectedWheel && selectedWheel->atomic && selectedWheel->atomic != atomic) {
		static int logCount = 0;
		if(weConfig.debugLogging || logCount < 10) {
			dbglog("WheelsExtender: vehicle %d -> wheel '%s' (frame: %s)",
			       modelIndex, selectedWheel->name, selectedWheel->frameName);
			logCount++;
		}

		// Geometry swap disabled — causes crash mid-frame
		// TODO: implement safe swap using double-buffer or deferred swap
	}

	extern int renderingWheel;
	renderingWheel = 1;
	AtomicDefaultRenderCallBack(atomic);
	renderingWheel = 0;

	return atomic;
}

// ============================================================
// Wire into SkyGFX system
// ============================================================

void WheelsExtender_Install(void) {
	// Install wheel render hook
	// TODO: InterceptCall(&orig_RenderWheelAtomicCB, WheelsExtender_RenderWheelCB, 0x7323C0);
}

int WheelsExtender_GetCount(void) {
	return g_wheelCount;
}

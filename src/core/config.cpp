#include "skygfx.h"
#include "ini_parser.hpp"
#include <string>

void refreshIni(void);

extern Config configs[10];
extern Config *config;
extern int numConfigs;
extern int currentConfig;
extern HMODULE dllModule;
extern char asipath[MAX_PATH];
extern int original_bRadiosity;
extern int defaultColourLeftUOffset;
extern int defaultColourRightUOffset;
extern int defaultColourTopVOffset;
extern int defaultColourBottomVOffset;
extern bool iCanHasbuildingPipe;
extern bool iCanHasvehiclePipe;
extern bool iCanHasSunGlare;
extern bool iCanHasNeoDrops;
extern bool disableClouds;
extern bool disableGamma;
extern bool fixPcCarLight;
extern int transparentLockon;
extern int fixShadows;
extern bool privateHooks;

extern int explicitBuildingPipe_tmp;
extern void refreshMenu(void);

// Normal Mapping Configuration
extern bool iCanHasNormalMapping;
extern bool iCanHasBuildingNormalMap;
extern bool iCanHasVehicleNormalMap;
extern float normalMapIntensity;
extern int normalMapPipeline;
extern bool normalMapDebug;
extern bool normalMapPlayerOnly;
extern int normalMapDebugMode;

BOOL FileExists(LPCTSTR szPath)
{
	DWORD dwAttrib = GetFileAttributes(szPath);
	return dwAttrib != INVALID_FILE_ATTRIBUTES && 
		   !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY);
}

struct StrAssoc
{
	const char *key;
	int val;

	static int get(StrAssoc *desc, const char *key);
};
int
StrAssoc::get(StrAssoc *desc, const char *key)
{
	for(; desc->key[0] != '\0'; desc++)
		if(strcmpi(desc->key, key) == 0)
			return desc->val;
	return desc->val;
}

void
findInis(void)
{
	char modulePath[MAX_PATH];
	GetModuleFileName(dllModule, modulePath, MAX_PATH);
	size_t nLen = strlen(modulePath);
	if (nLen + 1 < MAX_PATH) {
		modulePath[nLen+1] = L'\0';
	}
	modulePath[nLen] = L'i';
	modulePath[nLen-1] = L'n';
	modulePath[nLen-2] = L'i';
	modulePath[nLen-3] = L'.';
	modulePath[nLen-4] = '1';

	numConfigs = 0;
	while(numConfigs < 9 && FileExists(modulePath)){
		modulePath[nLen-4]++;
		numConfigs++;
	}
}

int
readhex(const char *str)
{
	int n = 0;
	if(strlen(str) > 2)
		sscanf(str+2, "%X", &n);
	return n;
}

int
readint(const std::string &s, int default = 0)
{
	try{
		return std::stoi(s);
	}catch(...){
		return default;
	}
}

float
readfloat(const std::string &s, float default = 0)
{
	try{
		return std::stof(s);
	}catch(...){
		return default;
	}
}

void
readIni(int n)
{
	int tmpint;
	char modulePath[MAX_PATH];
	GetModuleFileName(dllModule, modulePath, MAX_PATH);
	strncpy(asipath, modulePath, MAX_PATH);
	char *p = strrchr(asipath, '\\');
	if (p) p[1] = '\0';

	GetModuleFileName(dllModule, modulePath, MAX_PATH);
	size_t nLen = strlen(modulePath);
	Config *c;
	if(n > 0){
		if (nLen + 1 < MAX_PATH) {
			modulePath[nLen+1] = L'\0';
		}
		modulePath[nLen] = L'i';
		modulePath[nLen-1] = L'n';
		modulePath[nLen-2] = L'i';
		modulePath[nLen-3] = L'.';
		modulePath[nLen-4] = n+'0';
		c = &configs[n-1];
	}else{
		modulePath[nLen-1] = L'i';
		modulePath[nLen-2] = L'n';
		modulePath[nLen-3] = L'i';
		c = &configs[n];
	}
	linb::ini cfg;
	bool iniExisted = cfg.load_file(modulePath);

	c->keys[0] = readhex(cfg.get("SkyGfx", "keySwitch", "0x0").c_str());
	c->keys[1] = readhex(cfg.get("SkyGfx", "keyReload", "0x0").c_str());

	config->ps2ModulateGlobal = readint(cfg.get("SkyGfx", "ps2Modulate", ""), 0);
	config->dualPassGlobal = readint(cfg.get("SkyGfx", "dualPass", ""), 0);

	static StrAssoc buildPipeMap[] = {
		{"PS2",     BUILDING_PS2},
		{"PC",      BUILDING_XBOX},
		{"Xbox",    BUILDING_XBOX},
		{"GTAIV",   BUILDING_GTAIV},
		{"PBR",     BUILDING_PBR},
		{"",       -1},
	};
	c->buildingPipe = StrAssoc::get(buildPipeMap, cfg.get("SkyGfx", "buildingPipe", "").c_str());
	if(c->buildingPipe < 0){
		iCanHasbuildingPipe = false;
		c->buildingPipe = 0;
	}
	c->detailMaps = readint(cfg.get("SkyGfx", "detailMaps", ""), 0);

	c->ps2ModulateBuilding = readint(cfg.get("SkyGfx", "ps2ModulateBuilding", ""), config->ps2ModulateGlobal);
	c->dualPassBuilding = readint(cfg.get("SkyGfx", "dualPassBuilding", ""), config->dualPassGlobal);

	static StrAssoc vehPipeMap[] = {
		{"PS2",     CAR_PS2},
		{"PC",      CAR_PC},
		{"Xbox",    CAR_XBOX},
		{"Spec",    CAR_SPEC},
		{"Neo",     CAR_NEO},
		{"Leeds",   CAR_LCS},
		{"LCS",     CAR_LCS},
		{"VCS",     CAR_VCS},
		{"Mobile",  CAR_MOBILE},
		{"Env",     CAR_ENV},
		{"GTAIV",   CAR_GTAIV},
		{"Modern",  CAR_MODERN},
		{"",       -1},
	};
	c->vehiclePipe = StrAssoc::get(vehPipeMap, cfg.get("SkyGfx", "vehiclePipe", "").c_str());
	if(c->vehiclePipe < 0){
		iCanHasvehiclePipe = false;
		c->vehiclePipe = 0;
	}

	c->dualPassVehicle = readint(cfg.get("SkyGfx", "dualPassVehicle", ""), config->dualPassGlobal);
	c->leedsShininessMult = readfloat(cfg.get("SkyGfx", "leedsShininessMult", ""), 1.0);
	c->neoShininessMult = readfloat(cfg.get("SkyGfx", "neoShininessMult", ""), 1.0);
	c->neoSpecularityMult = readfloat(cfg.get("SkyGfx", "neoSpecularityMult", ""), 1.0);
	c->envShininessMult = readfloat(cfg.get("SkyGfx", "envShininessMult", ""), 1.0);
	c->envSpecularityMult = readfloat(cfg.get("SkyGfx", "envSpecularityMult", ""), 1.0);
	c->envPower = readfloat(cfg.get("SkyGfx", "envPower", ""), 20.0);
	c->envFresnel = readfloat(cfg.get("SkyGfx", "envFresnel", ""), 0.7f);
	c->envMapSize = readint(cfg.get("SkyGfx", "envMapSize", ""), 256);
	int i = 1;
	while(i < c->envMapSize) i *= 2;
	c->envMapSize = i;
	c->doglare = readint(cfg.get("SkyGfx", "sunGlare", ""), -1);
	if(c->doglare < 0){
		iCanHasSunGlare = false;
		c->doglare = 0;
	}

	c->ps2ModulateGrass = readint(cfg.get("SkyGfx", "ps2ModulateGrass", ""), config->ps2ModulateGlobal);
	c->dualPassGrass = readint(cfg.get("SkyGfx", "dualPassGrass", ""), config->dualPassGlobal);
	c->grassAddAmbient = readint(cfg.get("SkyGfx", "grassAddAmbient", ""), 0);
	c->fixGrassPlacement = readint(cfg.get("SkyGfx", "grassFixPlacement", ""), 0);
	c->backfaceCull = readint(cfg.get("SkyGfx", "grassBackfaceCull", ""), 1);

	static StrAssoc boolMap[] = {
		{"0",       0},
		{"false",   0},
		{"1",       1},
		{"true",    1},
		{"",       -1},
	};
	c->dualPassDefault = readint(cfg.get("SkyGfx", "dualPassDefault", ""), config->dualPassGlobal);
	c->dualPassPed = readint(cfg.get("SkyGfx", "dualPassPed", ""), config->dualPassGlobal);
	c->pedShadows = StrAssoc::get(boolMap, cfg.get("SkyGfx", "pedShadows", "").c_str());
	c->stencilShadows = StrAssoc::get(boolMap, cfg.get("SkyGfx", "stencilShadows", "").c_str());
	disableClouds = readint(cfg.get("SkyGfx", "disableClouds", ""), 0);
	disableGamma = readint(cfg.get("SkyGfx", "disableGamma", ""), 0);
	transparentLockon = readint(cfg.get("SkyGfx", "transparentLockon", ""), 0);
	fixShadows = readint(cfg.get("SkyGfx", "fixShadows", ""), 0);
	c->lightningIlluminatesWorld = readint(cfg.get("SkyGfx", "lightningIlluminatesWorld", ""), 0);

	static StrAssoc colorFilterMap[] = {
		{"None",    COLORFILTER_NONE},
		{"PS2",     COLORFILTER_PS2},
		{"PC",      COLORFILTER_PC},
		{"Mobile",  COLORFILTER_MOBILE},
		{"III",     COLORFILTER_III},
		{"VC",      COLORFILTER_VC},
		{"VCS",     COLORFILTER_VCS},
		{"GTAIV",   COLORFILTER_GTAIV},
		{"",        COLORFILTER_PC},
	};
	static StrAssoc ps2pcMap[] = {
		{"PS2",     0},
		{"PC",      1},
		{"",        1},
	};
	c->colorFilter = StrAssoc::get(colorFilterMap, cfg.get("SkyGfx", "colorFilter", "").c_str());
	if(c->pipeline == PIPELINE_PBR)
		c->colorFilter = COLORFILTER_MODERN;
	ps2pcMap[2].val = c->colorFilter == COLORFILTER_PS2 ? 0 : 1;
	c->infraredVision = StrAssoc::get(ps2pcMap, cfg.get("SkyGfx", "infraredVision", "").c_str());
	c->nightVision = StrAssoc::get(ps2pcMap, cfg.get("SkyGfx", "nightVision", "").c_str());
	c->grainFilter = StrAssoc::get(ps2pcMap, cfg.get("SkyGfx", "grainFilter", "").c_str());

	iCanHasNormalMapping = readint(cfg.get("SkyGfx", "enableNormalMaps", ""), 1) == 1;
	c->normalMapIntensity = readfloat(cfg.get("SkyGfx", "normalMapIntensity", ""), 1.0f);
	c->normalMapPlayerOnly = readint(cfg.get("SkyGfx", "normalMapPlayerOnly", ""), 1) == 1;

	iCanHasBuildingNormalMap = iCanHasNormalMapping && readint(cfg.get("SkyGfx", "normalMapBuilding", ""), 1) == 1;
	iCanHasVehicleNormalMap = iCanHasNormalMapping && readint(cfg.get("SkyGfx", "normalMapVehicle", ""), 1) == 1;
	normalMapDebug = readint(cfg.get("SkyGfx", "normalMapDebug", ""), 0) == 1;
	normalMapDebugMode = readint(cfg.get("SkyGfx", "normalMapDebugMode", ""), 0);
	c->usePCTimecyc = readint(cfg.get("SkyGfx", "usePCTimecyc", ""), 0);

	tmpint = readint(cfg.get("SkyGfx", "blurLeft", ""), 4000);
	c->offLeft = tmpint == 4000 ? defaultColourLeftUOffset : tmpint;
	tmpint = readint(cfg.get("SkyGfx", "blurTop", ""), 4000);
	c->offTop = tmpint == 4000 ? defaultColourTopVOffset : tmpint;
	tmpint = readint(cfg.get("SkyGfx", "blurRight", ""), 4000);
	c->offRight = tmpint == 4000 ? defaultColourRightUOffset : tmpint;
	tmpint = readint(cfg.get("SkyGfx", "blurBottom", ""), 4000);
	c->offBottom = tmpint == 4000 ? defaultColourBottomVOffset : tmpint;

	tmpint = readint(cfg.get("SkyGfx", "doRadiosity", ""), 4000);
	c->doRadiosity = tmpint == 4000 ? original_bRadiosity : tmpint;

	static StrAssoc ps2shdrMap[] = {
		{"PS2",     0},
		{"Shader",  1},
		{"",        1},
	};
	c->radiosity = StrAssoc::get(ps2shdrMap, cfg.get("SkyGfx", "radiosity", "").c_str());

	c->vcsTrails = readint(cfg.get("SkyGfx", "vcsTrails", ""), 0);
	c->trailsLimit = readint(cfg.get("SkyGfx", "trailsLimit", ""), 80);
	c->trailsIntensity = readint(cfg.get("SkyGfx", "trailsIntensity", ""), 38);

	c->radiosityFilterPasses = readint(cfg.get("SkyGfx", "radiosityFilterPasses", ""), 2);
	c->radiosityRenderPasses = readint(cfg.get("SkyGfx", "radiosityRenderPasses", ""), 1);
	c->radiosityIntensity = readint(cfg.get("SkyGfx", "radiosityIntensity", ""), 0x23);

	c->neoWaterDrops = readint(cfg.get("SkyGfx", "neoWaterDrops", ""), -1);
	if(c->neoWaterDrops < 0){
		iCanHasNeoDrops = false;
		c->neoWaterDrops = 0;
	}
	c->neoBloodDrops = readint(cfg.get("SkyGfx", "neoBloodDrops", ""), 0);
	fixPcCarLight = readint(cfg.get("SkyGfx", "fixPcCarLight", ""), 0);
	explicitBuildingPipe_tmp = readint(cfg.get("SkyGfx", "explicitBuildingPipe", ""), -1);

	c->zwriteThreshold = readint(cfg.get("SkyGfx", "zwriteThreshold", ""), 128);
	if(c->zwriteThreshold < 0) c->zwriteThreshold = 0;
	if(c->zwriteThreshold > 255) c->zwriteThreshold = 255;

	c->coronaZtest = readint(cfg.get("SkyGfx", "coronaZtest", ""), -1);

	// Sun flare control (PS2→PC conversion fix)
	// Default 0.4 for corona, 0.6 for core — reduces massive flare from PS2 timecycle values
	c->sunCoronaIntensity = readfloat(cfg.get("SkyGfx", "sunCoronaIntensity", ""), 0.4f);
	c->sunCoreIntensity = readfloat(cfg.get("SkyGfx", "sunCoreIntensity", ""), 0.6f);
	if(c->sunCoronaIntensity < 0.0f) c->sunCoronaIntensity = 0.0f;
	if(c->sunCoronaIntensity > 2.0f) c->sunCoronaIntensity = 2.0f;
	if(c->sunCoreIntensity < 0.0f) c->sunCoreIntensity = 0.0f;
	if(c->sunCoreIntensity > 2.0f) c->sunCoreIntensity = 2.0f;
	c->sunStreakIntensity = readfloat(cfg.get("SkyGfx", "sunStreakIntensity", ""), 0.5f);
	c->sunStreakSize = readfloat(cfg.get("SkyGfx", "sunStreakSize", ""), 0.6f);
	if(c->sunStreakIntensity < 0.0f) c->sunStreakIntensity = 0.0f;
	if(c->sunStreakIntensity > 2.0f) c->sunStreakIntensity = 2.0f;
	if(c->sunStreakSize < 0.0f) c->sunStreakSize = 0.0f;
	if(c->sunStreakSize > 2.0f) c->sunStreakSize = 2.0f;

	c->bYCbCrFilter = readint(cfg.get("SkyGfx", "YCbCrCorrection", ""), 0);
	c->lumaScale = readfloat(cfg.get("SkyGfx", "lumaScale", ""), 219.0f/255.0f);
	c->lumaOffset = readfloat(cfg.get("SkyGfx", "lumaOffset", ""), 16.0f/255.0f);
	c->cbScale = readfloat(cfg.get("SkyGfx", "CbScale", ""), 1.23f);
	c->cbOffset = readfloat(cfg.get("SkyGfx", "CbOffset", ""), 0.0f);
	c->crScale = readfloat(cfg.get("SkyGfx", "CrScale", ""), 1.23f);
	c->crOffset     = readfloat(cfg.get("SkyGfx", "CrOffset", ""), 0.0f);

	c->ssaoEnable = readint(cfg.get("SkyGfx", "ssaoEnable", ""), 1);
	c->ssaoRadius = readfloat(cfg.get("SkyGfx", "ssaoRadius", ""), 0.8f);
	c->ssaoPower = readfloat(cfg.get("SkyGfx", "ssaoPower", ""), 1.5f);
	c->ssaoKernelSize = readfloat(cfg.get("SkyGfx", "ssaoKernelSize", ""), 16.0f);
	c->ssaoSampleCount = readint(cfg.get("SkyGfx", "ssaoSampleCount", ""), 16);

	// SSAO overhaul (quarter-res temporal)
	c->ssaoTemporalEnable = readint(cfg.get("SkyGfx", "ssaoTemporalEnable", ""), 1);
	c->ssaoTemporalBlend = readfloat(cfg.get("SkyGfx", "ssaoTemporalBlend", ""), 0.1f);
	c->ssaoBlurPasses = readint(cfg.get("SkyGfx", "ssaoBlurPasses", ""), 2);
	c->ssaoBlurRadius = readfloat(cfg.get("SkyGfx", "ssaoBlurRadius", ""), 3.0f);
	c->ssaoDepthThreshold = readfloat(cfg.get("SkyGfx", "ssaoDepthThreshold", ""), 0.01f);

	c->smaaEnable = readint(cfg.get("SkyGfx", "smaaEnable", ""), 1);

	// Motion Blur
	c->motionBlurEnable = readint(cfg.get("SkyGfx", "motionBlurEnable", ""), 0);
	c->motionBlurStrength = readfloat(cfg.get("SkyGfx", "motionBlurStrength", ""), 0.5f);
	c->motionBlurRadial = readfloat(cfg.get("SkyGfx", "motionBlurRadial", ""), 0.3f);
	c->motionBlurSpeedFactor = readfloat(cfg.get("SkyGfx", "motionBlurSpeedFactor", ""), 0.5f);
	c->motionBlurCameraAware = readint(cfg.get("SkyGfx", "motionBlurCameraAware", ""), 1);

	// SSS Post-Process Blur
	c->sssPostProcessEnable = readint(cfg.get("SkyGfx", "sssPostProcessEnable", ""), 0);
	c->sssPostProcessStrength = readfloat(cfg.get("SkyGfx", "sssPostProcessStrength", ""), 0.3f);
	c->sssPostProcessRadius = readfloat(cfg.get("SkyGfx", "sssPostProcessRadius", ""), 4.0f);
	c->sssPostProcessThreshold = readfloat(cfg.get("SkyGfx", "sssPostProcessThreshold", ""), 0.1f);

	// Skin Enhancement
	c->skinEnhanceEnable = readint(cfg.get("SkyGfx", "skinEnhanceEnable", ""), 0);
	c->skinWrapFactor = readfloat(cfg.get("SkyGfx", "skinWrapFactor", ""), 0.5f);
	c->skinSpecularPower = readfloat(cfg.get("SkyGfx", "skinSpecularPower", ""), 16.0f);
	c->skinSpecularStrength = readfloat(cfg.get("SkyGfx", "skinSpecularStrength", ""), 0.3f);
	c->skinSSSStrength = readfloat(cfg.get("SkyGfx", "skinSSSStrength", ""), 0.4f);

	// Hair Enhancement
	c->hairEnhanceEnable = readint(cfg.get("SkyGfx", "hairEnhanceEnable", ""), 0);
	c->hairAnisotropicPower = readfloat(cfg.get("SkyGfx", "hairAnisotropicPower", ""), 32.0f);
	c->hairAnisotropicStrength = readfloat(cfg.get("SkyGfx", "hairAnisotropicStrength", ""), 0.5f);
	c->hairSSSStrength = readfloat(cfg.get("SkyGfx", "hairSSSStrength", ""), 0.2f);

	// Vegetation Enhancement
	c->vegetationEnhanceEnable = readint(cfg.get("SkyGfx", "vegetationEnhanceEnable", ""), 0);
	c->vegetationSSSStrength = readfloat(cfg.get("SkyGfx", "vegetationSSSStrength", ""), 0.3f);
	c->vegetationAmbientBoost = readfloat(cfg.get("SkyGfx", "vegetationAmbientBoost", ""), 1.2f);

	// Edge Tessellation
	c->edgeTessEnable = readint(cfg.get("SkyGfx", "edgeTessEnable", ""), 0);
	c->edgeTessStrength = readfloat(cfg.get("SkyGfx", "edgeTessStrength", ""), 0.01f);
	c->edgeTessThreshold = readfloat(cfg.get("SkyGfx", "edgeTessThreshold", ""), 0.1f);

	// Atmospheric: Height Fog (Crytek exponential)
	c->heightFogEnable = readint(cfg.get("SkyGfx", "heightFogEnable", ""), 0);
	c->heightFogDensity = readfloat(cfg.get("SkyGfx", "heightFogDensity", ""), 0.002f);
	c->heightFogHeightFalloff = readfloat(cfg.get("SkyGfx", "heightFogHeightFalloff", ""), 0.8f);
	c->heightFogStartHeight = readfloat(cfg.get("SkyGfx", "heightFogStartHeight", ""), 0.0f);
	c->heightFogR = readfloat(cfg.get("SkyGfx", "heightFogR", ""), 0.5f);
	c->heightFogG = readfloat(cfg.get("SkyGfx", "heightFogG", ""), 0.5f);
	c->heightFogB = readfloat(cfg.get("SkyGfx", "heightFogB", ""), 0.5f);
	c->heightFogTimecycleScale = readfloat(cfg.get("SkyGfx", "heightFogTimecycleScale", ""), 1.0f);

	// Atmospheric: God Rays (screen-space radial blur)
	c->godRaysEnable = readint(cfg.get("SkyGfx", "godRaysEnable", ""), 0);
	c->godRaysExposure = readfloat(cfg.get("SkyGfx", "godRaysExposure", ""), 0.0034f);
	c->godRaysDecay = readfloat(cfg.get("SkyGfx", "godRaysDecay", ""), 1.0f);
	c->godRaysDensity = readfloat(cfg.get("SkyGfx", "godRaysDensity", ""), 0.84f);
	c->godRaysWeight = readfloat(cfg.get("SkyGfx", "godRaysWeight", ""), 1.0f);
	c->godRaysNumSamples = readint(cfg.get("SkyGfx", "godRaysNumSamples", ""), 20);

	// Velocity buffer (per-pixel motion vectors via depth reconstruction)
	c->velocityBufferEnable = readint(cfg.get("SkyGfx", "velocityBufferEnable", ""), 1);

	// Forward+ tiled lighting (O3DE Atom-inspired 16×16 screen tiles)
	c->forwardPlusEnable = readint(cfg.get("SkyGfx", "forwardPlusEnable", ""), 1);

	c->ivMode = readint(cfg.get("SkyGfx", "ivMode", ""), 0);
	c->ivDesaturation = readfloat(cfg.get("SkyGfx", "ivDesaturation", ""), 1.0f);
	c->ivGamma = readfloat(cfg.get("SkyGfx", "ivGamma", ""), 1.0f);
	c->ivSaturation = readfloat(cfg.get("SkyGfx", "ivSaturation", ""), 0.0f);
	c->ivCurves = readfloat(cfg.get("SkyGfx", "ivCurves", ""), 0.0f);
	c->ivVignetteIntensity = readfloat(cfg.get("SkyGfx", "ivVignetteIntensity", ""), 0.0f);
	c->ivVignetteRadius = readfloat(cfg.get("SkyGfx", "ivVignetteRadius", ""), 0.75f);
	c->ivVignetteContrast = readfloat(cfg.get("SkyGfx", "ivVignetteContrast", ""), 1.5f);
	c->ivBloomIntensity = readfloat(cfg.get("SkyGfx", "ivBloomIntensity", ""), 0.0f);
	c->ivExposure = readfloat(cfg.get("SkyGfx", "ivExposure", ""), 1.0f);

	privateHooks = readint(cfg.get("SkyGfx", "privateHooks", ""), 0);

	if(!iniExisted){
		// ===== GTA III Features =====
		cfg.set("SkyGfx", "; ===== GTA III Features =====", "");
		cfg.set("SkyGfx", "; colorFilter", "III");
		cfg.set("SkyGfx", "; buildingPipe", "PS2");
		cfg.set("SkyGfx", "; vehiclePipe", "PS2");

		// ===== GTA Vice City Features =====
		cfg.set("SkyGfx", "; ===== GTA Vice City Features =====", "");
		cfg.set("SkyGfx", "; colorFilter", "VC");
		cfg.set("SkyGfx", "; radiosity", "Shader");
		cfg.set("SkyGfx", "; doRadiosity", "1");

		// ===== GTA San Andreas Features =====
		cfg.set("SkyGfx", "; ===== GTA San Andreas Features =====", "");
		cfg.set("SkyGfx", "; colorFilter", "PS2");
		cfg.set("SkyGfx", "; ps2ModulateBuilding", "0");
		cfg.set("SkyGfx", "; ps2ModulateVehicle", "0");
		cfg.set("SkyGfx", "; ps2ModulateGrass", "0");
		cfg.set("SkyGfx", "; dualPassBuilding", "1");
		cfg.set("SkyGfx", "; dualPassVehicle", "1");
		cfg.set("SkyGfx", "; dualPassGrass", "1");
		cfg.set("SkyGfx", "; grassAddAmbient", "1");
		cfg.set("SkyGfx", "; grassBackfaceCull", "1");
		cfg.set("SkyGfx", "; detailMaps", "1");
		cfg.set("SkyGfx", "; stochasticTexturing", "1");
		cfg.set("SkyGfx", "; envMapSize", "256");
		cfg.set("SkyGfx", "; envMapUseLODs", "1");
		cfg.set("SkyGfx", "; envMapFarClipMult", "1.0");
		cfg.set("SkyGfx", "; neoShininessMult", "1.0");
		cfg.set("SkyGfx", "; neoSpecularityMult", "1.0");
		cfg.set("SkyGfx", "; normalMapIntensity", "1.0");
		cfg.set("SkyGfx", "; normalMapBuilding", "1");
		cfg.set("SkyGfx", "; normalMapPlayerOnly", "1");
		cfg.set("SkyGfx", "; sunGlare", "0");
		cfg.set("SkyGfx", "; neoWaterDrops", "0");

		// ===== GTA Liberty City Stories / Vice City Stories =====
		cfg.set("SkyGfx", "; ===== GTA LCS / VCS Features =====", "");
		cfg.set("SkyGfx", "; colorFilter", "VCS");
		cfg.set("SkyGfx", "; vcsTrails", "0");

		// ===== GTA IV Features =====
		cfg.set("SkyGfx", "; ===== GTA IV Features =====", "");
		cfg.set("SkyGfx", "ivMode", "0");
		cfg.set("SkyGfx", "ivDesaturation", "1.0");
		cfg.set("SkyGfx", "ivGamma", "1.0");
		cfg.set("SkyGfx", "ivSaturation", "0.3");
		cfg.set("SkyGfx", "ivCurves", "1.0");
		cfg.set("SkyGfx", "ivVignetteIntensity", "0.15");
		cfg.set("SkyGfx", "ivVignetteRadius", "0.70");
		cfg.set("SkyGfx", "ivVignetteContrast", "1.5");
		cfg.set("SkyGfx", "ivBloomIntensity", "0.05");
		cfg.set("SkyGfx", "ivExposure", "2.5");

		// ===== Modern Enhancements =====
		cfg.set("SkyGfx", "; ===== Modern Enhancements =====", "");
		cfg.set("SkyGfx", "; --- Anti-Aliasing (SMAA) ---", "");
		cfg.set("SkyGfx", "; Enables SMAA (Subpixel Morphological Anti-Aliasing)", "");
		cfg.set("SkyGfx", "; SMAA — single toggle, preset/temporal are internal", "");
		cfg.set("SkyGfx", "smaaEnable", "1");

		cfg.set("SkyGfx", "; --- Ambient Occlusion (SSAO) ---", "");
		cfg.set("SkyGfx", "; Screen-space ambient occlusion using depth buffer", "");
		cfg.set("SkyGfx", "ssaoEnable", "1");
		cfg.set("SkyGfx", "ssaoRadius", "0.8");
		cfg.set("SkyGfx", "ssaoPower", "1.5");
		cfg.set("SkyGfx", "ssaoKernelSize", "16");
		cfg.set("SkyGfx", "ssaoSampleCount", "16");

		cfg.set("SkyGfx", "; --- Motion Blur (Burnout Paradise style) ---", "");
		cfg.set("SkyGfx", "; Speed-based radial + directional motion blur", "");
		cfg.set("SkyGfx", "motionBlurEnable", "0");
		cfg.set("SkyGfx", "motionBlurStrength", "0.5");
		cfg.set("SkyGfx", "motionBlurRadial", "0.3");
		cfg.set("SkyGfx", "motionBlurSpeedFactor", "0.5");
		cfg.set("SkyGfx", "motionBlurCameraAware", "1");

		cfg.set("SkyGfx", "; --- Subsurface Scattering (SSS) Post-Process ---", "");
		cfg.set("SkyGfx", "; Post-process blur for skin translucency effect", "");
		cfg.set("SkyGfx", "; NOTE: Screen-space effect, blurs entire scene with edge preservation", "");
		cfg.set("SkyGfx", "; Does NOT conflict with per-material SSS fields (sssIntensity, etc.)", "");
		cfg.set("SkyGfx", "sssPostProcessEnable", "0");
		cfg.set("SkyGfx", "sssPostProcessStrength", "0.3");
		cfg.set("SkyGfx", "sssPostProcessRadius", "4.0");
		cfg.set("SkyGfx", "sssPostProcessThreshold", "0.1");

		cfg.set("SkyGfx", "; --- Skin Enhancement ---", "");
		cfg.set("SkyGfx", "; Works ON TOP of Rpskin rendering", "");
		cfg.set("SkyGfx", "; Adds wrap lighting for SSS approximation + improved specular", "");
		cfg.set("SkyGfx", "skinEnhanceEnable", "0");
		cfg.set("SkyGfx", "skinWrapFactor", "0.5");
		cfg.set("SkyGfx", "skinSpecularPower", "16.0");
		cfg.set("SkyGfx", "skinSpecularStrength", "0.3");
		cfg.set("SkyGfx", "skinSSSStrength", "0.4");

		cfg.set("SkyGfx", "; --- Hair Enhancement ---", "");
		cfg.set("SkyGfx", "; Works ON TOP of existing hair rendering", "");
		cfg.set("SkyGfx", "; Adds anisotropic highlights (Kajiya-Kay) + SSS", "");
		cfg.set("SkyGfx", "hairEnhanceEnable", "0");
		cfg.set("SkyGfx", "hairAnisotropicPower", "32.0");
		cfg.set("SkyGfx", "hairAnisotropicStrength", "0.5");
		cfg.set("SkyGfx", "hairSSSStrength", "0.2");

		cfg.set("SkyGfx", "; --- Vegetation Enhancement ---", "");
		cfg.set("SkyGfx", "; Works ON TOP of existing grass rendering", "");
		cfg.set("SkyGfx", "; Adds SSS-like translucency + improved ambient", "");
		cfg.set("SkyGfx", "vegetationEnhanceEnable", "0");
		cfg.set("SkyGfx", "vegetationSSSStrength", "0.3");
		cfg.set("SkyGfx", "vegetationAmbientBoost", "1.2");

		cfg.set("SkyGfx", "; --- Edge Tessellation ---", "");
		cfg.set("SkyGfx", "; Displaces vertices along normals at edges", "");
		cfg.set("SkyGfx", "; Requires SMAA edge buffer (smaaEnable=1)", "");
		cfg.set("SkyGfx", "edgeTessEnable", "0");
		cfg.set("SkyGfx", "edgeTessStrength", "0.01");
		cfg.set("SkyGfx", "edgeTessThreshold", "0.1");

		cfg.set("SkyGfx", "; --- Height Fog (Crytek Exponential) ---", "");
		cfg.set("SkyGfx", "; Exponential height-based atmospheric fog", "");
		cfg.set("SkyGfx", "heightFogEnable", "0");
		cfg.set("SkyGfx", "heightFogDensity", "0.002");
		cfg.set("SkyGfx", "heightFogHeightFalloff", "0.8");
		cfg.set("SkyGfx", "heightFogStartHeight", "0.0");
		cfg.set("SkyGfx", "heightFogR", "0.5");
		cfg.set("SkyGfx", "heightFogG", "0.5");
		cfg.set("SkyGfx", "heightFogB", "0.5");
		cfg.set("SkyGfx", "heightFogTimecycleScale", "1.0");

		cfg.set("SkyGfx", "; --- God Rays (Screen-Space Radial Blur) ---", "");
		cfg.set("SkyGfx", "; Radial blur toward sun for volumetric light rays", "");
		cfg.set("SkyGfx", "godRaysEnable", "0");
		cfg.set("SkyGfx", "godRaysExposure", "0.0034");
		cfg.set("SkyGfx", "godRaysDecay", "1.0");
		cfg.set("SkyGfx", "godRaysDensity", "0.84");
		cfg.set("SkyGfx", "godRaysWeight", "1.0");
		cfg.set("SkyGfx", "godRaysNumSamples", "20");

		cfg.set("SkyGfx", "; --- Velocity Buffer (Per-Pixel Motion Vectors) ---", "");
		cfg.set("SkyGfx", "; Depth-reconstructed motion vectors for motion blur", "");
		cfg.set("SkyGfx", "velocityBufferEnable", "1");

		cfg.set("SkyGfx", "; --- Forward+ Tiled Lighting ---", "");
		cfg.set("SkyGfx", "; O3DE Atom-inspired 16×16 screen tile light culling", "");
		cfg.set("SkyGfx", "forwardPlusEnable", "1");

		cfg.set("SkyGfx", "; --- Faux Normal Buffer ---", "");
		cfg.set("SkyGfx", "; Stereo disparity-derived normal map for effects", "");
		cfg.set("SkyGfx", "normalBufferEnable", "0");
		cfg.set("SkyGfx", "normalBufferOffset", "0.5");
		cfg.set("SkyGfx", "normalBufferScale", "1.0");

		cfg.set("SkyGfx", "; --- 4-Pipe Chain ---", "");
		cfg.set("SkyGfx", "; Multi-pass post-processing using normal buffer", "");
		cfg.set("SkyGfx", "pipeChainEnable", "0");
		cfg.set("SkyGfx", "pipeChainIntensity", "0.5");

		// ===== Unused / Legacy =====
		cfg.set("SkyGfx", "; ===== Unused / Legacy =====", "");
		cfg.set("SkyGfx", "; privateHooks", "0");
		cfg.set("SkyGfx", "; forceWindShader", "0");

		cfg.write_file(modulePath);
	}
}

void
readInis(void)
{
	original_bRadiosity = CPostEffects::m_bRadiosity;
	if(numConfigs == 0)
		readIni(0);
	else
		for(int i = 1; i <= numConfigs; i++)
			readIni(i);
	refreshIni();
}

void
setConfig(void)
{
	if(currentConfig >= 0 && currentConfig < numConfigs){
		config = &configs[currentConfig];

		refreshIni();
	}
}

void
reloadAllInis(void)
{
	if(numConfigs == 0)
		readIni(0);
	else
		for(int i = 1; i <= numConfigs; i++)
			readIni(i);
	refreshIni();
}

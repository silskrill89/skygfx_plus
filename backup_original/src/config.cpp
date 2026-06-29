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
extern bool forceWindShader;
extern int explicitBuildingPipe_tmp;
extern void refreshMenu(void);

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
		{"",       -1},
	};
	c->buildingPipe = StrAssoc::get(buildPipeMap, cfg.get("SkyGfx", "buildingPipe", "").c_str());
	if(c->buildingPipe < 0){
		iCanHasbuildingPipe = false;
		c->buildingPipe = 0;
	}
	c->detailMaps = readint(cfg.get("SkyGfx", "detailMaps", ""), 0);
	c->stochastic = readint(cfg.get("SkyGfx", "stochasticTexturing", ""), 0);

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
		{"Env",  CAR_ENV},
		{"GTAIV",   CAR_GTAIV},
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
	c->envMapFarClipMult = readfloat(cfg.get("SkyGfx", "envMapFarClipMult", ""), 1.0);
	c->envMapUseLODs = readint(cfg.get("SkyGfx", "envMapUseLODs", ""), 0);
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
	ps2pcMap[2].val = c->colorFilter == COLORFILTER_PS2 ? 0 : 1;
	c->rgb1Mult = readfloat(cfg.get("SkyGfx", "rgb1Mult", ""), 1.0f);
	c->rgb2Mult = readfloat(cfg.get("SkyGfx", "rgb2Mult", ""), 1.0f);
	c->infraredVision = StrAssoc::get(ps2pcMap, cfg.get("SkyGfx", "infraredVision", "").c_str());
	c->nightVision = StrAssoc::get(ps2pcMap, cfg.get("SkyGfx", "nightVision", "").c_str());
	c->grainFilter = StrAssoc::get(ps2pcMap, cfg.get("SkyGfx", "grainFilter", "").c_str());
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
	c->trailsResolution = readint(cfg.get("SkyGfx", "trailsResolution", ""), 1);

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
	c->tagsBuildingPipe = StrAssoc::get(buildPipeMap, cfg.get("SkyGfx", "tagsBuildingPipe", "").c_str());

	c->zwriteThreshold = readint(cfg.get("SkyGfx", "zwriteThreshold", ""), 128);
	if(c->zwriteThreshold < 0) c->zwriteThreshold = 0;
	if(c->zwriteThreshold > 255) c->zwriteThreshold = 255;

	c->zwriteThresholdGrass = readint(cfg.get("SkyGfx", "zwriteThresholdGrass", ""), 128);
	if(c->zwriteThresholdGrass < 0) c->zwriteThresholdGrass = 0;
	if(c->zwriteThresholdGrass > 255) c->zwriteThresholdGrass = 255;

	c->zwriteThresholdPed = readint(cfg.get("SkyGfx", "zwriteThresholdPed", ""), 128);
	if(c->zwriteThresholdPed < 0) c->zwriteThresholdPed = 0;
	if(c->zwriteThresholdPed > 255) c->zwriteThresholdPed = 255;

	c->coronaZtest = readint(cfg.get("SkyGfx", "coronaZtest", ""), -1);

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

	c->smaaEnable = readint(cfg.get("SkyGfx", "smaaEnable", ""), 1);
	c->smaaPreset = readint(cfg.get("SkyGfx", "smaaPreset", ""), 2);
	c->smaaPredication = readint(cfg.get("SkyGfx", "smaaPredication", ""), 0);
	c->smaaTemporal = readint(cfg.get("SkyGfx", "smaaTemporal", ""), 0);

	c->ivMode = readint(cfg.get("SkyGfx", "ivMode", ""), 0);
	c->ivDesaturation = readfloat(cfg.get("SkyGfx", "ivDesaturation", ""), 0.3f);
	c->ivGamma = readfloat(cfg.get("SkyGfx", "ivGamma", ""), 1.0f);
	c->ivVignetteIntensity = readfloat(cfg.get("SkyGfx", "ivVignetteIntensity", ""), 0.5f);
	c->ivVignetteRadius = readfloat(cfg.get("SkyGfx", "ivVignetteRadius", ""), 0.5f);
	c->ivVignetteContrast = readfloat(cfg.get("SkyGfx", "ivVignetteContrast", ""), 2.0f);
	c->ivBloomIntensity = readfloat(cfg.get("SkyGfx", "ivBloomIntensity", ""), 0.15f);
	c->ivExposure = readfloat(cfg.get("SkyGfx", "ivExposure", ""), 1.0f);

	privateHooks = readint(cfg.get("SkyGfx", "privateHooks", ""), 0);
	if (readint(cfg.get("SkyGfx", "forceWindShader", ""), 0) == 1) forceWindShader = true;

	c->unifiedEnable = readint(cfg.get("SkyGfx", "unifiedEnable", ""), 1) != 0;
	c->unifiedVersion = readint(cfg.get("SkyGfx", "unifiedVersion", ""), 2);
	c->unifiedSatBoost = readfloat(cfg.get("SkyGfx", "unifiedSatBoost", ""), 0.08f);
	c->unifiedIblTintStrength = readfloat(cfg.get("SkyGfx", "unifiedIblTintStrength", ""), 0.3f);
	c->unifiedSsaoNoiseScale = readfloat(cfg.get("SkyGfx", "unifiedSsaoNoiseScale", ""), 4.0f);
	c->unifiedShadowSoftness = readfloat(cfg.get("SkyGfx", "unifiedShadowSoftness", ""), 0.5f);
	c->unifiedCloudShadowStr = readfloat(cfg.get("SkyGfx", "unifiedCloudShadowStr", ""), 0.3f);
	c->unifiedSunShadowStr = readfloat(cfg.get("SkyGfx", "unifiedSunShadowStr", ""), 0.8f);
	c->unifiedVertexAOBoost = readfloat(cfg.get("SkyGfx", "unifiedVertexAOBoost", ""), 1.4f);
	c->unifiedDayReduction = readfloat(cfg.get("SkyGfx", "unifiedDayReduction", ""), 0.15f);
	c->unifiedPointLightOverride = readfloat(cfg.get("SkyGfx", "unifiedPointLightOverride", ""), 0.2f);
	c->unifiedSmaaThreshold = readfloat(cfg.get("SkyGfx", "unifiedSmaaThreshold", ""), 0.1f);
	c->unifiedSmaaCornerRounding = readfloat(cfg.get("SkyGfx", "unifiedSmaaCornerRounding", ""), 25.0f);
	c->unifiedSmaaMaxSearchSteps = readfloat(cfg.get("SkyGfx", "unifiedSmaaMaxSearchSteps", ""), 8.0f);
	c->unifiedShowMenu = readint(cfg.get("SkyGfx", "unifiedShowMenu", ""), 0) != 0;
	c->unifiedShowOverlay = readint(cfg.get("SkyGfx", "unifiedShowOverlay", ""), 0) != 0;
	c->unifiedDebugOcclusion = readint(cfg.get("SkyGfx", "unifiedDebugOcclusion", ""), 0) != 0;
	c->unifiedEnablePrePass = readint(cfg.get("SkyGfx", "unifiedEnablePrePass", ""), 1) != 0;
	c->unifiedEnableEdgeDetect = readint(cfg.get("SkyGfx", "unifiedEnableEdgeDetect", ""), 1) != 0;
	c->unifiedEnableOcclusion = readint(cfg.get("SkyGfx", "unifiedEnableOcclusion", ""), 1) != 0;
	c->unifiedEnableStoredShadows = readint(cfg.get("SkyGfx", "unifiedEnableStoredShadows", ""), 1) != 0;
	c->unifiedEnableCloudShadows = readint(cfg.get("SkyGfx", "unifiedEnableCloudShadows", ""), 1) != 0;
	c->unifiedEnableSunShadows = readint(cfg.get("SkyGfx", "unifiedEnableSunShadows", ""), 1) != 0;
	c->unifiedEnableTimeOfDay = readint(cfg.get("SkyGfx", "unifiedEnableTimeOfDay", ""), 1) != 0;
	c->unifiedEnableVertexAO = readint(cfg.get("SkyGfx", "unifiedEnableVertexAO", ""), 1) != 0;
	c->unifiedEnablePointLightOverride = readint(cfg.get("SkyGfx", "unifiedEnablePointLightOverride", ""), 1) != 0;
	c->unifiedEnablePostPass = readint(cfg.get("SkyGfx", "unifiedEnablePostPass", ""), 1) != 0;
	c->unifiedEnableIBL = readint(cfg.get("SkyGfx", "unifiedEnableIBL", ""), 1) != 0;
	c->unifiedEnableIBLTint = readint(cfg.get("SkyGfx", "unifiedEnableIBLTint", ""), 1) != 0;
	c->unifiedEnableSurfaceWeights = readint(cfg.get("SkyGfx", "unifiedEnableSurfaceWeights", ""), 1) != 0;
	c->unifiedEnableGrading = readint(cfg.get("SkyGfx", "unifiedEnableGrading", ""), 1) != 0;
	c->unifiedEnableGamma = readint(cfg.get("SkyGfx", "unifiedEnableGamma", ""), 1) != 0;

	bool changed = false;
	auto setIfMissing = [&](const char* key, const char* val){
		if(cfg.get("SkyGfx", key, "").empty()){
			cfg.set("SkyGfx", key, val);
			changed = true;
		}
	};
	setIfMissing("unifiedEnable", "1");
	setIfMissing("unifiedVersion", "2");
	setIfMissing("unifiedSatBoost", "0.08");
	setIfMissing("unifiedIblTintStrength", "0.3");
	setIfMissing("unifiedSsaoNoiseScale", "4.0");
	setIfMissing("unifiedShadowSoftness", "0.5");
	setIfMissing("unifiedCloudShadowStr", "0.3");
	setIfMissing("unifiedSunShadowStr", "0.8");
	setIfMissing("unifiedVertexAOBoost", "1.4");
	setIfMissing("unifiedDayReduction", "0.15");
	setIfMissing("unifiedPointLightOverride", "0.2");
	setIfMissing("unifiedSmaaThreshold", "0.1");
	setIfMissing("unifiedSmaaCornerRounding", "25.0");
	setIfMissing("unifiedSmaaMaxSearchSteps", "8.0");
	setIfMissing("unifiedShowMenu", "0");
	setIfMissing("unifiedShowOverlay", "0");
	setIfMissing("unifiedDebugOcclusion", "0");
	setIfMissing("unifiedEnablePrePass", "1");
	setIfMissing("unifiedEnableEdgeDetect", "1");
	setIfMissing("unifiedEnableOcclusion", "1");
	setIfMissing("unifiedEnableStoredShadows", "1");
	setIfMissing("unifiedEnableCloudShadows", "1");
	setIfMissing("unifiedEnableSunShadows", "1");
	setIfMissing("unifiedEnableTimeOfDay", "1");
	setIfMissing("unifiedEnableVertexAO", "1");
	setIfMissing("unifiedEnablePointLightOverride", "1");
	setIfMissing("unifiedEnablePostPass", "1");
	setIfMissing("unifiedEnableIBL", "1");
	setIfMissing("unifiedEnableIBLTint", "1");
	setIfMissing("unifiedEnableSurfaceWeights", "1");
	setIfMissing("unifiedEnableGrading", "1");
	setIfMissing("unifiedEnableGamma", "1");
	if(changed) cfg.write_file(modulePath);

	if(!iniExisted){
		cfg.set("SkyGfx", "buildingPipe", "PC");
		cfg.set("SkyGfx", "colorFilter", "VCS");
		cfg.set("SkyGfx", "vehiclePipe", "VCS");
		cfg.set("SkyGfx", "radiosity", "Shader");
		cfg.set("SkyGfx", "vcsTrails", "0");
		cfg.set("SkyGfx", "doRadiosity", "1");
		cfg.set("SkyGfx", "ps2ModulateBuilding", "0");
		cfg.set("SkyGfx", "dualPassBuilding", "1");
		cfg.set("SkyGfx", "ps2ModulateVehicle", "0");
		cfg.set("SkyGfx", "dualPassVehicle", "1");
		cfg.set("SkyGfx", "ps2ModulateGrass", "0");
		cfg.set("SkyGfx", "grassAddAmbient", "1");
		cfg.set("SkyGfx", "grassBackfaceCull", "1");
		cfg.set("SkyGfx", "sunGlare", "0");
		cfg.set("SkyGfx", "neoWaterDrops", "0");
		cfg.set("SkyGfx", "detailMaps", "1");
		cfg.set("SkyGfx", "stochasticTexturing", "1");
		cfg.set("SkyGfx", "envMapSize", "256");
		cfg.set("SkyGfx", "envMapUseLODs", "1");
		cfg.set("SkyGfx", "envMapFarClipMult", "1.0");
		cfg.set("SkyGfx", "neoShininessMult", "1.0");
		cfg.set("SkyGfx", "neoSpecularityMult", "1.0");
		cfg.set("SkyGfx", "ssaoEnable", "1");
		cfg.set("SkyGfx", "ssaoRadius", "0.8");
		cfg.set("SkyGfx", "ssaoPower", "1.5");
		cfg.set("SkyGfx", "ssaoKernelSize", "16");
		cfg.set("SkyGfx", "ssaoSampleCount", "16");

		cfg.set("SkyGfx", "smaaEnable", "1");
		cfg.set("SkyGfx", "smaaPreset", "2");
		cfg.set("SkyGfx", "smaaPredication", "0");
		cfg.set("SkyGfx", "smaaTemporal", "0");
		cfg.set("SkyGfx", "ivMode", "0");
		cfg.set("SkyGfx", "ivDesaturation", "0.3");
		cfg.set("SkyGfx", "ivGamma", "1.0");
		cfg.set("SkyGfx", "ivVignetteIntensity", "0.5");
		cfg.set("SkyGfx", "ivVignetteRadius", "0.5");
		cfg.set("SkyGfx", "ivVignetteContrast", "2.0");
		cfg.set("SkyGfx", "ivBloomIntensity", "0.15");
		cfg.set("SkyGfx", "ivExposure", "1.0");
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

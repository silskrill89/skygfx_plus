#include "skygfx.h"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "debugmenu_public.h"
#include "neo.h"
#include <d3dx9.h>

extern bool iCanHasvehiclePipe;
extern bool iCanHasSunGlare;
extern bool iCanHasNeoDrops;

#define MENUSETTINGS \
	X(ps2ModulateGlobal)		\
	X(ps2ModulateBuilding)		\
	X(ps2ModulateGrass)			\
	X(dualPassGlobal)				\
	X(dualPassDefault)				\
	X(dualPassBuilding)			\
	X(dualPassVehicle)			\
	X(dualPassPed)			\
	X(dualPassGrass)				\
	X(buildingPipe)				\
	X(detailMaps)				\
	X(vehiclePipe)				\
	X(leedsShininessMult)				\
	X(neoShininessMult)				\
	X(neoSpecularityMult)			\
	X(envShininessMult)				\
	X(envSpecularityMult)			\
	X(envPower)			\
	X(envFresnel)			\
	X(doglare)						\
	X(fixGrassPlacement)			\
	X(grassAddAmbient)			\
	X(backfaceCull)			\
	X(pedShadows)					\
	X(stencilShadows)				\
	X(colorFilter)					\
	X(doRadiosity)					\
	X(radiosity)					\
	X(lightningIlluminatesWorld)		\
	X(neoWaterDrops)			\
	X(neoBloodDrops)			\
	X(infraredVision)				\
	X(nightVision)					\
	X(grainFilter)					\
	X(offLeft)					\
	X(offRight)				\
	X(offTop)					\
	X(offBottom)				\
	X(radiosityFilterPasses)		\
	X(radiosityRenderPasses)		\
	X(radiosityIntensity)			\
	X(zwriteThreshold)			\
	X(coronaZtest)				\
	X(bYCbCrFilter)				\
	X(lumaScale)				\
	X(lumaOffset)				\
	X(cbScale)				\
	X(cbOffset)				\
	X(crScale)				\
	X(crOffset)				\
	X(envMapSize)			\
	X(envMapUseLODs)			\
	X(envMapFarClipMult)		\
	X(ssaoEnable)			\
	X(ssaoRadius)			\
	X(ssaoPower)			\
	X(ssaoKernelSize)			\
	X(ssaoSampleCount)			\
	X(smaaEnable)			\
	X(smaaPreset)			\
	X(smaaPredication)			\
	X(smaaTemporal)			\
	X(ivMode)				\
	X(ivDesaturation)			\
	X(ivGamma)				\
	X(ivVignetteIntensity)		\
	X(ivVignetteRadius)			\
	X(ivVignetteContrast)		\
	X(ivBloomIntensity)			\
	X(ivExposure)

struct SkyGfxMenu
{
#define X(NAME) DebugMenuEntry *NAME;
MENUSETTINGS
#undef X
};
SkyGfxMenu menu;
bool hasMenu = false;

void
refreshMenu(void)
{
	if(hasMenu){
#define X(NAME) DebugMenuEntrySetAddress(menu.NAME, &config->NAME);
MENUSETTINGS
#undef X
	}
}

void
toggledDual(void)
{
	// override, we can't do better
	config->dualPassBuilding = config->dualPassGlobal;
	config->dualPassVehicle = config->dualPassGlobal;
	config->dualPassPed = config->dualPassGlobal;
	config->dualPassGrass = config->dualPassGlobal;
	config->dualPassDefault = config->dualPassGlobal;
}

void
toggledModulation(void)
{
	// override, we can't do better
	config->ps2ModulateBuilding = config->ps2ModulateGlobal;
	config->ps2ModulateGrass = config->ps2ModulateGlobal;
}

void
changeEnvMapSize(void)
{
	int i = 1;
	// increase or decrease by power of two. doesn't work for 4 or below
	if(config->envMapSize+1 & config->envMapSize)
		while(i < config->envMapSize) i *= 2;
	else
		while(i < config->envMapSize/2) i *= 2;
	config->envMapSize = i;
}

void
installMenu(void)
{
	DebugMenuEntry *e;
	if(DebugMenuLoad()){
		static const char *ps2pcStr[] = { "PS2", "PC" };
		static const char *buildPipeStr[] = { "PS2", "Xbox", "GTAIV", "Mobile" };
		static const char *vehPipeStr[] = { "PS2", "PC", "Xbox", "Spec", "Mobile", "Neo", "LCS", "VCS", "Env", "GTAIV" };
		static const char *colFilterStr[] = { "None", "PS2", "PC", "Mobile", "III", "VC", "VCS", "GTAIV" };
		static const char *lightningStr[] = { "Sky only", "Sky and objects" };
		static const char *shadStr[] = { "Default", "PS2", "PC" };
		static const char *radStr[] = { "PS2", "Shader" };
		static const char *coronaStr[] = { "-", "default (PS2)", "Force (PC)" };
		e = DebugMenuAddVar("SkyGFX", "Config", &currentConfig, setConfig, 1, 0, numConfigs-1, nil);
		DebugMenuEntrySetWrap(e, true);
		DebugMenuAddCmd("SkyGFX", "Reload Inis", reloadAllInis);

		menu.dualPassGlobal = DebugMenuAddVarBool32("SkyGFX", "Dual-pass Global", &config->dualPassGlobal, toggledDual);
		menu.ps2ModulateGlobal = DebugMenuAddVarBool32("SkyGFX", "PS2-modulate Global", &config->ps2ModulateGlobal, toggledModulation);
		if(iCanHasbuildingPipe){
			menu.buildingPipe = DebugMenuAddVar("SkyGFX", "Building Pipeline", &config->buildingPipe, nil, 1, BUILDING_PS2, NUMBUILDINGPIPES-1, buildPipeStr);
DebugMenuEntrySetWrap(menu.buildingPipe, true);
		//menu.tagsBuildingPipe = DebugMenuAddVar("SkyGFX", "Tags Building Pipeline", &config->tagsBuildingPipe, nil, 1, BUILDING_PS2, NUMBUILDINGPIPES - 1, buildPipeStr);
		//DebugMenuEntrySetWrap(menu.tagsBuildingPipe, true);
		menu.detailMaps = DebugMenuAddVarBool32("SkyGFX", "Detail Maps", &config->detailMaps, nil);
		//menu.stochastic = DebugMenuAddVarBool32("SkyGFX", "Stochastic Texturing", &config->stochastic, nil);
		}
		if(iCanHasvehiclePipe){
			menu.vehiclePipe = DebugMenuAddVar("SkyGFX", "Vehicle Pipeline", &config->vehiclePipe, nil, 1, CAR_PS2, NUMCARPIPES-1, vehPipeStr);
			DebugMenuEntrySetWrap(menu.vehiclePipe, true);
		}
		menu.envMapSize = DebugMenuAddVar("SkyGFX", "Vehicle Env Map Size", &config->envMapSize, changeEnvMapSize, 1, 4, 2048, nil);
		menu.envMapFarClipMult = DebugMenuAddVar("SkyGFX|Misc", "Vehicle Env Map Far Clip Mult", &config->envMapFarClipMult, nil, 0.1f, 0.0f, 10.0f);
		//menu.envMapUseLODs = DebugMenuAddVarBool32("SkyGFX", "Vehicle Env Map Use LODs", &config->envMapUseLODs, nil);
		menu.grassAddAmbient = DebugMenuAddVarBool32("SkyGFX", "Add Ambient to Grass", &config->grassAddAmbient, nil);
		menu.backfaceCull = DebugMenuAddVarBool32("SkyGFX", "Grass Backface Culling", &config->backfaceCull, nil);
		menu.pedShadows = DebugMenuAddVar("SkyGFX", "Ped Shadows", &config->pedShadows, nil, 1, -1, 1, shadStr);
		DebugMenuEntrySetWrap(menu.pedShadows, true);
		menu.stencilShadows = DebugMenuAddVar("SkyGFX", "Stencil Shadows", &config->stencilShadows, nil, 1, -1, 1, shadStr);
		DebugMenuEntrySetWrap(menu.stencilShadows, true);
		// TODO: allow III/VC somehow?
		menu.colorFilter = DebugMenuAddVar("SkyGFX", "Colour filter", &config->colorFilter, resetValues, 1, COLORFILTER_NONE, COLORFILTER_MOBILE, colFilterStr);
		DebugMenuEntrySetWrap(menu.colorFilter, true);
		menu.doRadiosity = DebugMenuAddVarBool32("SkyGFX", "Radiosity", &config->doRadiosity, resetValues);
		menu.radiosity = DebugMenuAddVar("SkyGFX", "Radiosity type", &config->radiosity, nil, 1, 0, 1, radStr);
		DebugMenuEntrySetWrap(menu.radiosity, true);
		if(iCanHasNeoDrops){
			menu.neoWaterDrops = DebugMenuAddVarBool32("SkyGFX", "Neo Water drops", &config->neoWaterDrops, nil);
			menu.neoBloodDrops = DebugMenuAddVarBool32("SkyGFX", "Neo-style Blood drops", &config->neoBloodDrops, nil);
#ifdef DEBUG
			DebugMenuAddVarBool8("SkyGFX", "Spray Water drops", (int8*)&WaterDrops::sprayWater, nil);
			DebugMenuAddVarBool8("SkyGFX", "Spray Blood drops", (int8*)&WaterDrops::sprayBlood, nil);
#endif
		}

		DebugMenuAddVarBool8("SkyGFX|Misc", "Blur PS2 Colour Filter", (int8_t*)&CPostEffects::m_bBlurColourFilter, nil);
		if(iCanHasSunGlare)
			menu.doglare = DebugMenuAddVarBool32("SkyGFX|Misc", "Sun Glare", &config->doglare, nil);
		menu.leedsShininessMult = DebugMenuAddVar("SkyGFX|Misc", "Leeds Car Shininess", &config->leedsShininessMult, nil, 0.1f, 0.0f, 10.0f);
		menu.neoShininessMult = DebugMenuAddVar("SkyGFX|Misc", "Neo Car Shininess", &config->neoShininessMult, nil, 0.1f, 0.0f, 10.0f);
		menu.neoSpecularityMult = DebugMenuAddVar("SkyGFX|Misc", "Neo Car Specularity", &config->neoSpecularityMult, nil, 0.1f, 0.0f, 10.0f);
		menu.envShininessMult = DebugMenuAddVar("SkyGFX|Misc", "Env Car Shininess", &config->envShininessMult, nil, 0.1f, 0.0f, 10.0f);
		menu.envSpecularityMult = DebugMenuAddVar("SkyGFX|Misc", "Env Car Specularity", &config->envSpecularityMult, nil, 0.1f, 0.0f, 10.0f);
		menu.envPower = DebugMenuAddVar("SkyGFX|Misc", "Env Car Power", &config->envPower, nil, 1.0f, 0.0f, 2000.0f);
		menu.envFresnel = DebugMenuAddVar("SkyGFX|Misc", "Env Car Fresnel", &config->envFresnel, nil, 0.1f, 0.0f, 10.0f);
		menu.fixGrassPlacement = DebugMenuAddVarBool32("SkyGFX|Misc", "Fix Grass Placement", &config->fixGrassPlacement, nil);
		menu.lightningIlluminatesWorld = DebugMenuAddVar("SkyGFX|Misc", "Lightning illuminates", &config->lightningIlluminatesWorld, nil, 1, 0, 1, lightningStr);
		DebugMenuEntrySetWrap(menu.lightningIlluminatesWorld, true);
		menu.coronaZtest = DebugMenuAddVar("SkyGFX|Misc", "Corona Z test", &config->coronaZtest, resetValues, 1, -1, 1, coronaStr);
		DebugMenuEntrySetWrap(menu.coronaZtest, true);

		menu.dualPassDefault = DebugMenuAddVarBool32("SkyGFX|Advanced", "Dual-pass Default", &config->dualPassDefault, nil);
		menu.dualPassBuilding = DebugMenuAddVarBool32("SkyGFX|Advanced", "Dual-pass Buildings", &config->dualPassBuilding, nil);
		menu.dualPassVehicle = DebugMenuAddVarBool32("SkyGFX|Advanced", "Dual-pass Vehicles", &config->dualPassVehicle, nil);
		menu.dualPassPed = DebugMenuAddVarBool32("SkyGFX|Advanced", "Dual-pass Peds", &config->dualPassPed, nil);
		menu.dualPassGrass = DebugMenuAddVarBool32("SkyGFX|Advanced", "Dual-pass Grass", &config->dualPassGrass, nil);
		menu.zwriteThreshold = DebugMenuAddVar("SkyGFX|Advanced", "Dual-pass Alpha Threshold", &config->zwriteThreshold, nil, 1, 0, 255, nil);
		//menu.zwriteThresholdGrass = DebugMenuAddVar("SkyGFX|Advanced", "Dual-pass Alpha Grass Threshold", &config->zwriteThresholdGrass, nil, 1, 0, 255, nil);
		//menu.zwriteThresholdPed = DebugMenuAddVar("SkyGFX|Advanced", "Dual-pass Alpha Ped Threshold", &config->zwriteThresholdPed, nil, 1, 0, 255, nil);
		menu.ps2ModulateBuilding = DebugMenuAddVarBool32("SkyGFX|Advanced", "PS2-modulate Buildings", &config->ps2ModulateBuilding, nil);
		menu.ps2ModulateGrass = DebugMenuAddVarBool32("SkyGFX|Advanced", "PS2-modulate Grass", &config->ps2ModulateGrass, nil);
		menu.infraredVision = DebugMenuAddVar("SkyGFX|Advanced", "Infrared vision", &config->infraredVision, nil, 1, 0, 1, ps2pcStr);
		DebugMenuEntrySetWrap(menu.infraredVision, true);
		menu.nightVision = DebugMenuAddVar("SkyGFX|Advanced", "Night vision", &config->nightVision, nil, 1, 0, 1, ps2pcStr);
		DebugMenuEntrySetWrap(menu.nightVision, true);
		menu.grainFilter = DebugMenuAddVar("SkyGFX|Advanced", "Grain filter", &config->grainFilter, resetValues, 1, 0, 1, ps2pcStr);
		DebugMenuEntrySetWrap(menu.grainFilter, true);

		//menu.rgb1Mult = DebugMenuAddVar("SkyGFX|Advanced", "RGB1 Mult", &config->rgb1Mult, resetValues, 1.0f, 0.0f, 10.0f);
		//menu.rgb2Mult = DebugMenuAddVar("SkyGFX|Advanced", "RGB2 Mult", &config->rgb2Mult, resetValues, 1.0f, 0.0f, 10.0f);

		menu.bYCbCrFilter = DebugMenuAddVarBool8("SkyGFX|ScreenFX", "Enable YCbCr tweak", (int8_t*)&config->bYCbCrFilter, resetValues);
		menu.lumaScale    = DebugMenuAddVar("SkyGFX|ScreenFX", "Y scale", &config->lumaScale, resetValues, 0.004f, 0.0f, 10.0f);
		menu.lumaOffset   = DebugMenuAddVar("SkyGFX|ScreenFX", "Y offset", &config->lumaOffset, resetValues, 0.004f, -1.0f, 1.0f);
		menu.cbScale      = DebugMenuAddVar("SkyGFX|ScreenFX", "Cb scale", &config->cbScale, resetValues, 0.004f, 0.0f, 10.0f);
		menu.cbOffset     = DebugMenuAddVar("SkyGFX|ScreenFX", "Cb offset", &config->cbOffset, resetValues, 0.004f, -1.0f, 1.0f);
		menu.crScale      = DebugMenuAddVar("SkyGFX|ScreenFX", "Cr scale", &config->crScale, resetValues, 0.004f, 0.0f, 10.0f);
		menu.crOffset     = DebugMenuAddVar("SkyGFX|ScreenFX", "Cr offset", &config->crOffset, resetValues, 0.004f, -1.0f, 1.0f);

		// SSAO Settings
		menu.ssaoEnable = DebugMenuAddVarBool32("SkyGFX|SSAO", "Enable SSAO", &config->ssaoEnable, nil);
		menu.ssaoRadius = DebugMenuAddVar("SkyGFX|SSAO", "SSAO Radius", &config->ssaoRadius, nil, 0.01f, 0.0f, 5.0f);
		menu.ssaoPower = DebugMenuAddVar("SkyGFX|SSAO", "SSAO Power", &config->ssaoPower, nil, 0.1f, 0.0f, 10.0f);
		menu.ssaoKernelSize = DebugMenuAddVar("SkyGFX|SSAO", "SSAO Kernel Size", &config->ssaoKernelSize, nil, 1.0f, 1.0f, 64.0f);
		menu.ssaoSampleCount = DebugMenuAddVar("SkyGFX|SSAO", "SSAO Sample Count", &config->ssaoSampleCount, nil, 1, 1, 64, nil);

		// SMAA Settings
		static const char *smaaPresetStr[] = { "LOW", "MEDIUM", "HIGH", "ULTRA" };
		menu.smaaEnable = DebugMenuAddVarBool32("SkyGFX|SMAA", "Enable SMAA", &config->smaaEnable, nil);
		menu.smaaPreset = DebugMenuAddVar("SkyGFX|SMAA", "SMAA Preset", &config->smaaPreset, nil, 1, 0, 3, smaaPresetStr);
		DebugMenuEntrySetWrap(menu.smaaPreset, true);
		menu.smaaPredication = DebugMenuAddVarBool32("SkyGFX|SMAA", "SMAA Predication", &config->smaaPredication, nil);
		menu.smaaTemporal = DebugMenuAddVarBool32("SkyGFX|SMAA", "SMAA Temporal", &config->smaaTemporal, nil);

		// GTA IV Mode Settings
		menu.ivMode = DebugMenuAddVarBool32("SkyGFX|GTA IV", "Enable GTA IV Mode", &config->ivMode, nil);
		menu.ivDesaturation = DebugMenuAddVar("SkyGFX|GTA IV", "Desaturation", &config->ivDesaturation, nil, 0.01f, 0.0f, 1.0f);
		menu.ivGamma = DebugMenuAddVar("SkyGFX|GTA IV", "Gamma", &config->ivGamma, nil, 0.01f, 0.1f, 3.0f);
		menu.ivVignetteIntensity = DebugMenuAddVar("SkyGFX|GTA IV", "Vignette Intensity", &config->ivVignetteIntensity, nil, 0.01f, 0.0f, 2.0f);
		menu.ivVignetteRadius = DebugMenuAddVar("SkyGFX|GTA IV", "Vignette Radius", &config->ivVignetteRadius, nil, 0.01f, 0.0f, 2.0f);
		menu.ivVignetteContrast = DebugMenuAddVar("SkyGFX|GTA IV", "Vignette Contrast", &config->ivVignetteContrast, nil, 0.01f, 0.0f, 5.0f);
		menu.ivBloomIntensity = DebugMenuAddVar("SkyGFX|GTA IV", "Bloom Intensity", &config->ivBloomIntensity, nil, 0.01f, 0.0f, 2.0f);
		menu.ivExposure = DebugMenuAddVar("SkyGFX|GTA IV", "Exposure", &config->ivExposure, nil, 0.01f, 0.0f, 3.0f);

/*
		DebugMenuAddVarBool32("SkyGFX", "Timecycle usePC", &config->usePCTimecyc, nil);
		DebugMenuAddCmd("SkyGFX", "Timecycle PostFX Alpha *1", [](){
				Nop(0x5BBF6F, 2);
				Nop(0x5BBF83, 2);
			});
		DebugMenuAddCmd("SkyGFX", "Timecycle PostFX Alpha *2", [](){
				Patch<uint16>(0x5BBF6F, 0xC0DC);
				Patch<uint16>(0x5BBF83, 0xC0DC);
			});
		DebugMenuAddCmd("SkyGFX", "Load PS2 timecyc.dat", [](){ LoadTimecycle("timecyc.dat"); });
		DebugMenuAddCmd("SkyGFX", "Load PS2 timecyc_pc.dat", [](){ LoadTimecycle("timecyc_pc.dat"); });
*/

//#ifdef DEBUG
//		DebugMenuAddVar("Debug", "Mirror Z", &mirrorVal, fixMirrors, 0.05f, 200.0f, 250.0f);
//#endif

		hasMenu = true;
		//void privatepatches(void);
		//privatepatches();
	}
}

// ============================================================
// Unified Pipeline — ImGui debug menu + constants upload
// ============================================================

static bool unifiedImGuiInited = false;

static void EnsureImGuiInit(IDirect3DDevice9 *device)
{
	if(unifiedImGuiInited) return;
	if(!device) return;
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ImGui_ImplDX9_Init(device);
	unifiedImGuiInited = true;
}

void UploadUnifiedConstants(IDirect3DDevice9 *device)
{
	return; // TODO: recover unified pipeline from JuniorDjjr fork
	if(!config->unifiedEnable) return;
	if(!device) return;

	float vpW = 640, vpH = 480;
	D3DVIEWPORT9 vp;
	if(SUCCEEDED(device->GetViewport(&vp))){
		vpW = (float)vp.Width;
		vpH = (float)vp.Height;
	}

	float c37[4] = { vpW, vpH, 1.0f/vpW, 1.0f/vpH };
	device->SetPixelShaderConstantF(37, c37, 1);

	float c28[4] = { config->unifiedSatBoost, config->unifiedIblTintStrength, 0, 0 };
	device->SetPixelShaderConstantF(28, c28, 1);

	float c35[4] = {
		config->unifiedShadowSoftness,
		config->unifiedCloudShadowStr,
		config->unifiedSunShadowStr,
		config->unifiedDayReduction
	};
	device->SetPixelShaderConstantF(35, c35, 1);

	float c36[4] = { config->unifiedVertexAOBoost, config->unifiedPointLightOverride, 0, 0 };
	device->SetPixelShaderConstantF(36, c36, 1);

	float c40[4] = {
		config->unifiedEnablePrePass ? 1.0f : 0.0f,
		config->unifiedEnableEdgeDetect ? 1.0f : 0.0f,
		config->unifiedEnableOcclusion ? 1.0f : 0.0f,
		1.0f
	};
	device->SetPixelShaderConstantF(40, c40, 1);

	float c41[4] = {
		config->unifiedEnableStoredShadows ? 1.0f : 0.0f,
		config->unifiedEnableCloudShadows ? 1.0f : 0.0f,
		config->unifiedEnableSunShadows ? 1.0f : 0.0f,
		config->unifiedEnableTimeOfDay ? 1.0f : 0.0f
	};
	device->SetPixelShaderConstantF(41, c41, 1);

	float c42[4] = {
		config->unifiedEnableVertexAO ? 1.0f : 0.0f,
		config->unifiedEnablePointLightOverride ? 1.0f : 0.0f,
		config->unifiedEnableIBL ? 1.0f : 0.0f,
		config->unifiedEnableIBLTint ? 1.0f : 0.0f
	};
	device->SetPixelShaderConstantF(42, c42, 1);

	float c43[4] = {
		config->unifiedEnableSurfaceWeights ? 1.0f : 0.0f,
		config->unifiedEnableGrading ? 1.0f : 0.0f,
		config->unifiedEnableGamma ? 1.0f : 0.0f,
		1.0f
	};
	device->SetPixelShaderConstantF(43, c43, 1);

	float c6[4] = { (float)config->unifiedVersion, 1.0f, 0, 0 };
	device->SetPixelShaderConstantF(6, c6, 1);
}

#define RB(x) reinterpret_cast<bool*>(&(x))

void DrawUnifiedDebugMenu(IDirect3DDevice9 *device)
{
	if(!device) return;
	EnsureImGuiInit(device);
	if(!unifiedImGuiInited) return;

	ImGui_ImplDX9_NewFrame();
	ImGui::NewFrame();

	if(config->debugMenuOpen){
		bool open = true;
		ImGui::SetNextWindowSize(ImVec2(380, 520), ImGuiCond_FirstUseEver);
		if(ImGui::Begin("SkyGFX", &open)){

			if(ImGui::CollapsingHeader("Pipelines", ImGuiTreeNodeFlags_DefaultOpen)){
				static const char *buildPipeStr[] = { "PS2", "Xbox", "GTAIV" };
				ImGui::Combo("Building", &config->buildingPipe, buildPipeStr, 3);
				static const char *vehPipeStr[] = { "PS2", "PC", "Xbox", "Spec", "Mobile", "Neo", "LCS", "VCS", "Env", "GTAIV" };
				ImGui::Combo("Vehicle", &config->vehiclePipe, vehPipeStr, 10);
				static const char *colFilterStr[] = { "None", "PS2", "PC", "Mobile", "III", "VC", "VCS", "GTAIV" };
				ImGui::Combo("Colour Filter", &config->colorFilter, colFilterStr, 8);
				static const char *buildPipeStr2[] = { "PS2", "Xbox", "GTAIV" };
				ImGui::Combo("Tags Building", &config->tagsBuildingPipe, buildPipeStr2, 3);
			}

			if(ImGui::CollapsingHeader("Building")){
				ImGui::Checkbox("PS2 Modulate", RB(config->ps2ModulateBuilding));
				ImGui::Checkbox("Dual-pass", RB(config->dualPassBuilding));
				ImGui::Checkbox("Detail Maps", RB(config->detailMaps));
				ImGui::Checkbox("Stochastic", RB(config->stochastic));
			}

			if(ImGui::CollapsingHeader("Vehicle")){
				ImGui::Checkbox("Dual-pass", RB(config->dualPassVehicle));
				ImGui::SliderInt("Env Map Size", &config->envMapSize, 4, 2048);
				ImGui::Checkbox("Env Map LODs", RB(config->envMapUseLODs));
				ImGui::SliderFloat("Env Far Clip", &config->envMapFarClipMult, 0.1f, 10.0f);
				ImGui::SliderFloat("Leeds Shininess", &config->leedsShininessMult, 0.0f, 10.0f);
				ImGui::SliderFloat("Neo Shininess", &config->neoShininessMult, 0.0f, 10.0f);
				ImGui::SliderFloat("Neo Specularity", &config->neoSpecularityMult, 0.0f, 10.0f);
				ImGui::SliderFloat("Env Shininess", &config->envShininessMult, 0.0f, 10.0f);
				ImGui::SliderFloat("Env Specularity", &config->envSpecularityMult, 0.0f, 10.0f);
				ImGui::SliderFloat("Env Power", &config->envPower, 0.0f, 2000.0f);
				ImGui::SliderFloat("Env Fresnel", &config->envFresnel, 0.0f, 10.0f);
			}

			if(ImGui::CollapsingHeader("Grass")){
				ImGui::Checkbox("PS2 Modulate", RB(config->ps2ModulateGrass));
				ImGui::Checkbox("Dual-pass", RB(config->dualPassGrass));
				ImGui::Checkbox("Add Ambient", RB(config->grassAddAmbient));
				ImGui::Checkbox("Backface Cull", RB(config->backfaceCull));
				ImGui::Checkbox("Fix Placement", RB(config->fixGrassPlacement));
			}

			if(ImGui::CollapsingHeader("Post Effects")){
				ImGui::Checkbox("Radiosity", RB(config->doRadiosity));
				static const char *radStr[] = { "PS2", "Shader" };
				ImGui::Combo("Rad Type", &config->radiosity, radStr, 2);
				ImGui::SliderInt("Rad Filter Passes", &config->radiosityFilterPasses, 0, 8);
				ImGui::SliderInt("Rad Render Passes", &config->radiosityRenderPasses, 0, 8);
				ImGui::SliderInt("Rad Intensity", &config->radiosityIntensity, 0, 255);
				ImGui::Separator();
				ImGui::Checkbox("VCS Trails", RB(config->vcsTrails));
				ImGui::SliderInt("Trails Limit", &config->trailsLimit, 0, 255);
				ImGui::SliderInt("Trails Intensity", &config->trailsIntensity, 0, 255);
				ImGui::SliderInt("Trails Resolution", &config->trailsResolution, 1, 4);
				ImGui::Separator();
				ImGui::SliderInt("Blur Left", &config->offLeft, -1000, 1000);
				ImGui::SliderInt("Blur Right", &config->offRight, -1000, 1000);
				ImGui::SliderInt("Blur Top", &config->offTop, -1000, 1000);
				ImGui::SliderInt("Blur Bottom", &config->offBottom, -1000, 1000);
			}

			if(ImGui::CollapsingHeader("Dual Pass")){
				ImGui::Checkbox("Global", RB(config->dualPassGlobal));
				ImGui::Checkbox("Default", RB(config->dualPassDefault));
				ImGui::Checkbox("Buildings", RB(config->dualPassBuilding));
				ImGui::Checkbox("Vehicles", RB(config->dualPassVehicle));
				ImGui::Checkbox("Peds", RB(config->dualPassPed));
				ImGui::Checkbox("Grass", RB(config->dualPassGrass));
				ImGui::SliderInt("Alpha Threshold", &config->zwriteThreshold, 0, 255);
				ImGui::SliderInt("Grass Threshold", &config->zwriteThresholdGrass, 0, 255);
				ImGui::SliderInt("Ped Threshold", &config->zwriteThresholdPed, 0, 255);
			}

			if(ImGui::CollapsingHeader("Effects")){
				static const char *shadStr[] = { "Default", "PS2", "PC" };
				ImGui::Combo("Ped Shadows", &config->pedShadows, shadStr, 3);
				ImGui::Combo("Stencil Shadows", &config->stencilShadows, shadStr, 3);
				static const char *ps2pcStr[] = { "PS2", "PC" };
				ImGui::Combo("Infrared Vision", &config->infraredVision, ps2pcStr, 2);
				ImGui::Combo("Night Vision", &config->nightVision, ps2pcStr, 2);
				ImGui::Combo("Grain Filter", &config->grainFilter, ps2pcStr, 2);
				ImGui::Checkbox("Sun Glare", RB(config->doglare));
				static const char *lightningStr[] = { "Sky only", "Sky and objects" };
				ImGui::Combo("Lightning", &config->lightningIlluminatesWorld, lightningStr, 2);
				ImGui::Checkbox("PS2 Modulate Global", RB(config->ps2ModulateGlobal));
			}

			if(ImGui::CollapsingHeader("SSAO")){
				ImGui::Checkbox("Enable", RB(config->ssaoEnable));
				ImGui::SliderFloat("Radius", &config->ssaoRadius, 0.0f, 5.0f);
				ImGui::SliderFloat("Power", &config->ssaoPower, 0.0f, 10.0f);
				ImGui::SliderFloat("Kernel Size", &config->ssaoKernelSize, 1.0f, 64.0f);
				ImGui::SliderInt("Sample Count", &config->ssaoSampleCount, 1, 64);
			}

			if(ImGui::CollapsingHeader("SMAA")){
				ImGui::Checkbox("Enable", RB(config->smaaEnable));
				static const char *smaaPresetStr[] = { "LOW", "MEDIUM", "HIGH", "ULTRA" };
				ImGui::Combo("Preset", &config->smaaPreset, smaaPresetStr, 4);
				ImGui::Checkbox("Predication", RB(config->smaaPredication));
				ImGui::Checkbox("Temporal", RB(config->smaaTemporal));
			}

			if(ImGui::CollapsingHeader("GTA IV")){
				ImGui::Checkbox("Enable IV Mode", RB(config->ivMode));
				ImGui::SliderFloat("Desaturation", &config->ivDesaturation, 0.0f, 1.0f);
				ImGui::SliderFloat("Gamma", &config->ivGamma, 0.1f, 3.0f);
				ImGui::SliderFloat("Vignette Intensity", &config->ivVignetteIntensity, 0.0f, 2.0f);
				ImGui::SliderFloat("Vignette Radius", &config->ivVignetteRadius, 0.0f, 2.0f);
				ImGui::SliderFloat("Vignette Contrast", &config->ivVignetteContrast, 0.0f, 5.0f);
				ImGui::SliderFloat("Bloom Intensity", &config->ivBloomIntensity, 0.0f, 2.0f);
				ImGui::SliderFloat("Exposure", &config->ivExposure, 0.0f, 3.0f);
			}

			if(ImGui::CollapsingHeader("SSS")){
				ImGui::Checkbox("Enable", RB(config->sssEnable));
				ImGui::SliderFloat("Global Intensity", &config->sssIntensity, 0.0f, 1.0f);
				ImGui::SliderFloat("Vegetation", &config->sssVegIntensity, 0.0f, 1.0f);
				ImGui::SliderFloat("Skin", &config->sssSkinIntensity, 0.0f, 1.0f);
				ImGui::SliderFloat("Cloth", &config->sssClothIntensity, 0.0f, 1.0f);
			}

			if(ImGui::CollapsingHeader("Screen FX")){
				ImGui::Checkbox("YCbCr Filter", &config->bYCbCrFilter);
				ImGui::SliderFloat("Luma Scale", &config->lumaScale, 0.0f, 2.0f);
				ImGui::SliderFloat("Luma Offset", &config->lumaOffset, -1.0f, 1.0f);
				ImGui::SliderFloat("Cb Scale", &config->cbScale, 0.0f, 5.0f);
				ImGui::SliderFloat("Cb Offset", &config->cbOffset, -1.0f, 1.0f);
				ImGui::SliderFloat("Cr Scale", &config->crScale, 0.0f, 5.0f);
				ImGui::SliderFloat("Cr Offset", &config->crOffset, -1.0f, 1.0f);
				ImGui::SliderFloat("RGB1 Mult", &config->rgb1Mult, 0.0f, 10.0f);
				ImGui::SliderFloat("RGB2 Mult", &config->rgb2Mult, 0.0f, 10.0f);
			}

			if(ImGui::CollapsingHeader("Unified Pipeline")){
				ImGui::Checkbox("Enable", &config->unifiedEnable);
				static const char *verStr[] = { "PS2", "PC 1.0", "Steam", "Mobile" };
				ImGui::Combo("Version", &config->unifiedVersion, verStr, 4);
				ImGui::SliderFloat("Sat Boost", &config->unifiedSatBoost, -0.5f, 1.0f);
				ImGui::SliderFloat("IBL Tint", &config->unifiedIblTintStrength, 0.0f, 1.0f);
				ImGui::Separator();
				ImGui::Checkbox("Pre-Pass", &config->unifiedEnablePrePass);
				ImGui::Checkbox("Edge Detect", &config->unifiedEnableEdgeDetect);
				ImGui::Checkbox("Occlusion", &config->unifiedEnableOcclusion);
				ImGui::Checkbox("Stored Shadows", &config->unifiedEnableStoredShadows);
				ImGui::Checkbox("Cloud Shadows", &config->unifiedEnableCloudShadows);
				ImGui::Checkbox("Sun Shadows", &config->unifiedEnableSunShadows);
				ImGui::Checkbox("Time of Day", &config->unifiedEnableTimeOfDay);
				ImGui::Checkbox("Vertex AO", &config->unifiedEnableVertexAO);
				ImGui::Checkbox("Point Light Override", &config->unifiedEnablePointLightOverride);
				ImGui::Checkbox("Post-Pass", &config->unifiedEnablePostPass);
				ImGui::Checkbox("IBL", &config->unifiedEnableIBL);
				ImGui::Checkbox("IBL Tint", &config->unifiedEnableIBLTint);
				ImGui::Checkbox("Surface Weights", &config->unifiedEnableSurfaceWeights);
				ImGui::Checkbox("Grading", &config->unifiedEnableGrading);
				ImGui::Checkbox("Gamma", &config->unifiedEnableGamma);
				ImGui::Separator();
				ImGui::SliderFloat("SSAO Noise", &config->unifiedSsaoNoiseScale, 0.5f, 16.0f);
				ImGui::SliderFloat("Shadow Softness", &config->unifiedShadowSoftness, 0.0f, 1.0f);
				ImGui::SliderFloat("Cloud Shadow", &config->unifiedCloudShadowStr, 0.0f, 1.0f);
				ImGui::SliderFloat("Sun Shadow", &config->unifiedSunShadowStr, 0.0f, 1.0f);
				ImGui::SliderFloat("Vertex AO Boost", &config->unifiedVertexAOBoost, 0.5f, 3.0f);
				ImGui::SliderFloat("Day Reduction", &config->unifiedDayReduction, 0.0f, 1.0f);
				ImGui::SliderFloat("Point Light OVR", &config->unifiedPointLightOverride, 0.0f, 1.0f);
				ImGui::SliderFloat("SMAA Threshold", &config->unifiedSmaaThreshold, 0.01f, 0.5f);
				ImGui::SliderFloat("SMAA Corner", &config->unifiedSmaaCornerRounding, 0.0f, 100.0f);
				ImGui::Checkbox("Show Overlay", &config->unifiedShowOverlay);
				ImGui::Checkbox("Debug Occlusion", &config->unifiedDebugOcclusion);
			}

			if(ImGui::CollapsingHeader("Actions")){
				if(ImGui::Button("Reload INI")){
					reloadAllInis();
				}
				ImGui::SameLine();
				if(ImGui::Button("Reset Values")){
					resetValues();
				}
				ImGui::Text("Config: %d / %d", currentConfig, numConfigs);
			}
		}
		ImGui::End();
		if(!open) config->debugMenuOpen = 0;
	}

	ImGui::Render();
	ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

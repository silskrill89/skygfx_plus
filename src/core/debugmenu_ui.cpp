#include "skygfx.h"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
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
	X(sunCoronaIntensity)			\
	X(sunCoreIntensity)				\
	X(sunStreakIntensity)			\
	X(sunStreakSize)				\
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
		menu.sunCoronaIntensity = DebugMenuAddVar("SkyGFX|Misc", "Sun Corona Intensity", &config->sunCoronaIntensity, nil, 0.01f, 0.0f, 10.0f);
		menu.sunCoreIntensity = DebugMenuAddVar("SkyGFX|Misc", "Sun Core Intensity", &config->sunCoreIntensity, nil, 0.01f, 0.0f, 10.0f);
		menu.sunStreakIntensity = DebugMenuAddVar("SkyGFX|Misc", "Sun Streak Intensity", &config->sunStreakIntensity, nil, 0.01f, 0.0f, 10.0f);
		menu.sunStreakSize = DebugMenuAddVar("SkyGFX|Misc", "Sun Streak Size", &config->sunStreakSize, nil, 0.01f, 0.0f, 10.0f);
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

		// SMAA Settings — single toggle
		menu.smaaEnable = DebugMenuAddVarBool32("SkyGFX|SMAA", "Enable SMAA", &config->smaaEnable, nil);

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

// Forward-declare ImGui's WndProc handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static bool unifiedImGuiInited = false;
static WNDPROC s_originalWndProc = nullptr;

static LRESULT CALLBACK ImGuiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if(ImGui::GetCurrentContext()){
		// Always let ImGui track mouse/keyboard state
		if(ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)){
			// Only consume input when menu is open
			if(config->debugMenuOpen)
				return 0;
		}
	}
	return CallWindowProc(s_originalWndProc, hWnd, msg, wParam, lParam);
}

static void EnsureImGuiInit(IDirect3DDevice9 *device)
{
	if(unifiedImGuiInited) return;
	if(!device) return;
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.MouseDrawCursor = true;  // GTA SA hides OS cursor — ImGui draws its own
	ImGui_ImplDX9_Init(device);
	// Get game window handle for Win32 input
	D3DDEVICE_CREATION_PARAMETERS dcp;
	device->GetCreationParameters(&dcp);
	if(dcp.hFocusWindow){
		ImGui_ImplWin32_Init(dcp.hFocusWindow);
		// Subclass to forward input to ImGui
		s_originalWndProc = (WNDPROC)SetWindowLongPtr(dcp.hFocusWindow, GWL_WNDPROC, (LONG_PTR)ImGuiWndProc);
	}
	unifiedImGuiInited = true;
}

// DEAD CODE: This function is never called from anywhere in the codebase.
// The early return was added because the unified pipeline from the JuniorDjjr
// fork was never integrated. The implementation below is retained for reference
// in case the feature is revived. See docs/Future Features.md.
void UploadUnifiedConstants(IDirect3DDevice9 *device)
{
	return; // Not called — unified pipeline feature was never completed
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

	ImGui_ImplWin32_NewFrame();
	ImGui_ImplDX9_NewFrame();
	ImGui::NewFrame();

	if(config->debugMenuOpen){
		bool open = true;
		ImGui::SetNextWindowSize(ImVec2(420, 580), ImGuiCond_FirstUseEver);
		if(ImGui::Begin("SkyGFX", &open)){

			// === GTA IV-style two-screen layout ===
			// "Graphics" = user-facing visual settings (like GTA IV SCR_DISPLAY + SCR_ADVANCED)
			// "Advanced" = technical fine-tuning (like GTA IV SCR_TGRAPHICS + FusionFix)

			if(ImGui::BeginTabBar("SkyGFXTabs")){

			// ============================================================
			// TAB 1: GRAPHICS — User-facing visual settings
			// ============================================================
			if(ImGui::BeginTabItem("Graphics")){

				// --- Preset ---
				if(ImGui::CollapsingHeader("Preset", ImGuiTreeNodeFlags_DefaultOpen)){
					static const char *presetStr[] = {
						"III PS2", "III Xbox", "III PC",
						"VC PS2", "VC Xbox", "VC PC",
						"SA PS2", "SA Xbox", "SA PC",
						"LCS PS2", "VCS PS2",
						"IV Xbox 360", "IV PC",
						"Best PC (Default)", "Custom"
					};
					int presetIdx = (config->preset >= 0 && config->preset < NUM_PRESETS) ? config->preset : NUM_PRESETS;
					if(ImGui::Combo("Style", &presetIdx, presetStr, NUM_PRESETS + 1)){
						if(presetIdx < NUM_PRESETS)
							ApplyPreset(config, presetIdx);
					}
				}

				// --- Rendering ---
				if(ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)){
					static const char *buildPipeStr[] = { "PS2", "Xbox", "GTAIV", "PBR" };
					ImGui::Combo("Building", &config->buildingPipe, buildPipeStr, 4);
					static const char *vehPipeStr[] = { "PS2", "PC", "Xbox", "Spec", "Mobile", "Neo", "LCS", "VCS", "Env", "GTAIV", "Modern" };
					ImGui::Combo("Vehicle", &config->vehiclePipe, vehPipeStr, 11);
					static const char *colFilterStr[] = { "None", "PS2", "PC", "Mobile", "III", "VC", "VCS", "GTAIV" };
					ImGui::Combo("Colour Filter", &config->colorFilter, colFilterStr, 8);
				}

				// --- Anti-Aliasing ---
				if(ImGui::CollapsingHeader("Anti-Aliasing")){
					ImGui::Checkbox("SMAA", RB(config->smaaEnable));
				}

				// --- Ambient Occlusion ---
				if(ImGui::CollapsingHeader("Ambient Occlusion")){
					ImGui::Checkbox("SSAO", RB(config->ssaoEnable));
				}

				// --- Post Processing ---
				if(ImGui::CollapsingHeader("Post Processing")){
					ImGui::Checkbox("Motion Blur", RB(config->motionBlurEnable));
					ImGui::Checkbox("Velocity Buffer", RB(config->velocityBufferEnable));
					ImGui::Checkbox("Radiosity", RB(config->doRadiosity));
					ImGui::Checkbox("VCS Trails", RB(config->vcsTrails));
				}

				// --- Atmosphere ---
				if(ImGui::CollapsingHeader("Atmosphere")){
					ImGui::Checkbox("Height Fog", RB(config->heightFogEnable));
					ImGui::Checkbox("God Rays", RB(config->godRaysEnable));
				}

				// --- GTA IV Mode ---
				if(ImGui::CollapsingHeader("GTA IV Mode")){
					ImGui::Checkbox("Enable IV Mode", RB(config->ivMode));
					ImGui::SliderFloat("Desaturation", &config->ivDesaturation, 0.0f, 2.0f);
					ImGui::SliderFloat("Gamma", &config->ivGamma, 0.5f, 2.0f);
					ImGui::SliderFloat("Exposure", &config->ivExposure, 0.0f, 3.0f);
					ImGui::SliderFloat("Bloom", &config->ivBloomIntensity, 0.0f, 2.0f);
					ImGui::SliderFloat("Vignette", &config->ivVignetteIntensity, 0.0f, 2.0f);
				}

				// --- Shadows ---
				if(ImGui::CollapsingHeader("Shadows")){
					static const char *shadStr[] = { "Default", "PS2", "PC" };
					ImGui::Combo("Ped Shadows", &config->pedShadows, shadStr, 3);
					ImGui::Combo("Stencil Shadows", &config->stencilShadows, shadStr, 3);
				}

				ImGui::EndTabItem();
			}

			// ============================================================
			// TAB 2: ADVANCED — Technical fine-tuning
			// ============================================================
			if(ImGui::BeginTabItem("Advanced")){

				// --- Building Detail ---
				if(ImGui::CollapsingHeader("Building Detail")){
					ImGui::Checkbox("PS2 Modulate", RB(config->ps2ModulateBuilding));
					ImGui::Checkbox("PS2 Modulate Global", RB(config->ps2ModulateGlobal));
					ImGui::Checkbox("Dual-pass", RB(config->dualPassBuilding));
					ImGui::Checkbox("Detail Maps", RB(config->detailMaps));
					ImGui::Checkbox("Stochastic", RB(config->stochastic));
				}

				// --- Vehicle Detail ---
				if(ImGui::CollapsingHeader("Vehicle Detail")){
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

				// --- Grass ---
				if(ImGui::CollapsingHeader("Grass")){
					ImGui::Checkbox("PS2 Modulate", RB(config->ps2ModulateGrass));
					ImGui::Checkbox("Add Ambient", RB(config->grassAddAmbient));
					ImGui::Checkbox("Backface Cull", RB(config->backfaceCull));
					ImGui::Checkbox("Fix Placement", RB(config->fixGrassPlacement));
				}

				// --- Dual Pass ---
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

				// --- SSAO Tuning ---
				if(ImGui::CollapsingHeader("SSAO Tuning")){
					ImGui::SliderFloat("Radius", &config->ssaoRadius, 0.0f, 5.0f);
					ImGui::SliderFloat("Power", &config->ssaoPower, 0.0f, 10.0f);
					ImGui::SliderFloat("Kernel Size", &config->ssaoKernelSize, 1.0f, 64.0f);
					ImGui::SliderInt("Sample Count", &config->ssaoSampleCount, 1, 64);
					ImGui::Separator();
					ImGui::Text("Temporal:");
					ImGui::Checkbox("Enable Temporal##ssao", RB(config->ssaoTemporalEnable));
					ImGui::SliderFloat("Temporal Blend##ssao", &config->ssaoTemporalBlend, 0.01f, 0.5f);
					ImGui::SliderInt("Blur Passes##ssao", &config->ssaoBlurPasses, 0, 3);
					ImGui::SliderFloat("Blur Radius##ssao", &config->ssaoBlurRadius, 1.0f, 8.0f);
					ImGui::SliderFloat("Depth Threshold##ssao", &config->ssaoDepthThreshold, 0.001f, 0.1f);
				}

				// --- Motion Blur Detail ---
				if(ImGui::CollapsingHeader("Motion Blur Detail")){
					ImGui::SliderFloat("Strength", &config->motionBlurStrength, 0.0f, 1.0f);
					ImGui::SliderFloat("Radial", &config->motionBlurRadial, 0.0f, 1.0f);
					ImGui::SliderFloat("Speed Factor", &config->motionBlurSpeedFactor, 0.0f, 2.0f);
					ImGui::Checkbox("Camera Aware", RB(config->motionBlurCameraAware));
				}

				// --- SSS ---
				if(ImGui::CollapsingHeader("SSS")){
					if(ImGui::TreeNode("Screen-Space SSS")){
						ImGui::Checkbox("Enable", RB(config->sssEnable));
						ImGui::SliderFloat("Global Intensity", &config->sssIntensity, 0.0f, 1.0f);
						ImGui::SliderFloat("Vegetation", &config->sssVegIntensity, 0.0f, 1.0f);
						ImGui::SliderFloat("Skin", &config->sssSkinIntensity, 0.0f, 1.0f);
						ImGui::SliderFloat("Cloth", &config->sssClothIntensity, 0.0f, 1.0f);
						ImGui::TreePop();
					}
					ImGui::Separator();
					if(ImGui::TreeNode("Post-Process Blur")){
						ImGui::Checkbox("Enable", RB(config->sssPostProcessEnable));
						ImGui::SliderFloat("Strength", &config->sssPostProcessStrength, 0.0f, 1.0f);
						ImGui::SliderFloat("Radius", &config->sssPostProcessRadius, 0.0f, 20.0f);
						ImGui::SliderFloat("Threshold", &config->sssPostProcessThreshold, 0.0f, 1.0f);
						ImGui::TreePop();
					}
					ImGui::Separator();
					if(ImGui::TreeNode("Skin Enhancement")){
						ImGui::Checkbox("Enable", RB(config->skinEnhanceEnable));
						ImGui::SliderFloat("Wrap Factor", &config->skinWrapFactor, 0.0f, 1.0f);
						ImGui::SliderFloat("Specular Power", &config->skinSpecularPower, 0.0f, 100.0f);
						ImGui::SliderFloat("Specular Strength", &config->skinSpecularStrength, 0.0f, 2.0f);
						ImGui::SliderFloat("SSS Strength", &config->skinSSSStrength, 0.0f, 1.0f);
						ImGui::TreePop();
					}
				}

				// --- Effects ---
				if(ImGui::CollapsingHeader("Effects")){
					static const char *ps2pcStr[] = { "PS2", "PC" };
					ImGui::Combo("Infrared Vision", &config->infraredVision, ps2pcStr, 2);
					ImGui::Combo("Night Vision", &config->nightVision, ps2pcStr, 2);
					ImGui::Combo("Grain Filter", &config->grainFilter, ps2pcStr, 2);
					ImGui::Checkbox("Sun Glare", RB(config->doglare));
					ImGui::SliderFloat("Sun Corona", &config->sunCoronaIntensity, 0.0f, 10.0f);
					ImGui::SliderFloat("Sun Core", &config->sunCoreIntensity, 0.0f, 10.0f);
					ImGui::SliderFloat("Sun Streak Intensity", &config->sunStreakIntensity, 0.0f, 10.0f);
					ImGui::SliderFloat("Sun Streak Size", &config->sunStreakSize, 0.0f, 10.0f);
					static const char *lightningStr[] = { "Sky only", "Sky and objects" };
					ImGui::Combo("Lightning", &config->lightningIlluminatesWorld, lightningStr, 2);
					static const char *coronaStr[] = { "-", "default (PS2)", "Force (PC)" };
					ImGui::Combo("Corona Z test", &config->coronaZtest, coronaStr, 3);
				}

				// --- Radiosity Detail ---
				if(ImGui::CollapsingHeader("Radiosity Detail")){
					static const char *radStr[] = { "PS2", "Shader" };
					ImGui::Combo("Type", &config->radiosity, radStr, 2);
					ImGui::SliderInt("Filter Passes", &config->radiosityFilterPasses, 0, 8);
					ImGui::SliderInt("Render Passes", &config->radiosityRenderPasses, 0, 8);
					ImGui::SliderInt("Intensity", &config->radiosityIntensity, 0, 255);
					ImGui::Separator();
					ImGui::SliderInt("Off Left", &config->offLeft, -1000, 1000);
					ImGui::SliderInt("Off Right", &config->offRight, -1000, 1000);
					ImGui::SliderInt("Off Top", &config->offTop, -1000, 1000);
					ImGui::SliderInt("Off Bottom", &config->offBottom, -1000, 1000);
				}

				// --- Atmospheric Detail ---
				if(ImGui::CollapsingHeader("Atmospheric Detail")){
					ImGui::Text("Height Fog:");
					ImGui::SliderFloat("Density", &config->heightFogDensity, 0.0f, 0.1f);
					ImGui::SliderFloat("Height Falloff", &config->heightFogHeightFalloff, 0.0f, 5.0f);
					ImGui::SliderFloat("Start Height", &config->heightFogStartHeight, -100.0f, 500.0f);
					ImGui::SliderFloat("Fog R", &config->heightFogR, 0.0f, 1.0f);
					ImGui::SliderFloat("Fog G", &config->heightFogG, 0.0f, 1.0f);
					ImGui::SliderFloat("Fog B", &config->heightFogB, 0.0f, 1.0f);
					ImGui::SliderFloat("TC Fog Scale", &config->heightFogTimecycleScale, 0.0f, 5.0f);
					ImGui::Separator();
					ImGui::Text("God Rays:");
					ImGui::SliderFloat("Exposure", &config->godRaysExposure, 0.0f, 0.05f);
					ImGui::SliderFloat("Decay", &config->godRaysDecay, 0.0f, 2.0f);
					ImGui::SliderFloat("Density", &config->godRaysDensity, 0.0f, 2.0f);
					ImGui::SliderFloat("Weight", &config->godRaysWeight, 0.0f, 5.0f);
					ImGui::SliderInt("Samples", &config->godRaysNumSamples, 1, 64);
				}

				// --- GTA IV Detail ---
				if(ImGui::CollapsingHeader("GTA IV Detail")){
					ImGui::SliderFloat("Saturation", &config->ivSaturation, -1.0f, 2.0f);
					ImGui::SliderFloat("Curves", &config->ivCurves, 0.0f, 2.0f);
					ImGui::SliderFloat("Vignette Radius", &config->ivVignetteRadius, 0.0f, 2.0f);
					ImGui::SliderFloat("Vignette Contrast", &config->ivVignetteContrast, 0.0f, 5.0f);
				}

				// --- Screen FX ---
				if(ImGui::CollapsingHeader("Screen FX")){
					ImGui::Checkbox("YCbCr Filter", RB(config->bYCbCrFilter));
					ImGui::SliderFloat("RGB1 Mult", &config->rgb1Mult, 0.0f, 2.0f);
					ImGui::SliderFloat("RGB2 Mult", &config->rgb2Mult, 0.0f, 2.0f);
				}

				// --- Experimental ---
				if(ImGui::CollapsingHeader("Experimental")){
					ImGui::BeginDisabled();
					ImGui::TextDisabled("Normal Mapping [NOT IMPLEMENTED]");
					ImGui::SliderFloat("NM Intensity", &config->normalMapIntensity, 0.0f, 2.0f);
					ImGui::EndDisabled();
					ImGui::Separator();
					ImGui::Checkbox("Normal Buffer", RB(config->normalBufferEnable));
					ImGui::SliderFloat("NB Offset", &config->normalBufferOffset, 0.0f, 2.0f);
					ImGui::SliderFloat("NB Scale", &config->normalBufferScale, 0.0f, 2.0f);
					ImGui::Separator();
					ImGui::Checkbox("Pipe Chain", RB(config->pipeChainEnable));
					ImGui::SliderFloat("PC Intensity", &config->pipeChainIntensity, 0.0f, 1.0f);
					ImGui::Separator();
					ImGui::Checkbox("Forward+ Tiled Lighting", RB(config->forwardPlusEnable));
					extern int g_fpGpuLightCount;
					ImGui::Text("Active lights: %d", g_fpGpuLightCount);
				}

				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
			}

			// === Actions (outside tabs) ===
			ImGui::Separator();
			if(ImGui::Button("Save INI")){
				saveConfig();
			}
			ImGui::SameLine();
			if(ImGui::Button("Reload INI")){
				reloadAllInis();
			}
			ImGui::SameLine();
			if(ImGui::Button("Reset Values")){
				resetValues();
			}
			ImGui::SameLine();
			ImGui::Text("Config: %d / %d", currentConfig, numConfigs);
		}
		ImGui::End();
		if(!open) config->debugMenuOpen = 0;
	}

	ImGui::Render();
	ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

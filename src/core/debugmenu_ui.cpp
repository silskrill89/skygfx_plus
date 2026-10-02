#include "skygfx.h"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "debugmenu_public.h"
#include "neo.h"
#include <d3dx9.h>
#include <string.h>
#include <ctype.h>	// tolower — INI-key filter match
#include <string>
#include <vector>
#include <algorithm>

extern bool iCanHasvehiclePipe;
extern bool iCanHasSunGlare;
extern bool iCanHasNeoDrops;
extern char asipath[MAX_PATH];	// game dir with trailing '\', set by readIni
// main.cpp (pipe-conjunction fix #4) — re-derives config->pipeline/
// colorFilter/ivMode from the actual building/vehicle pipes.
extern void derivePipelineFromPipes(Config *c);
// main.cpp: colorFilterEnable user-intent latch (silent-write self-heal).
// The row below records every toggle as USER INTENT so the per-frame guard
// never fights legitimate edits and the value persists across world entry.
extern void cfEnableSetUserIntent(Config *c, RwBool v);
static void cfEnableRowIntent(void) { cfEnableSetUserIntent(config, config->colorFilterEnable); }

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
	X(vehEnvIntensity)		\
	X(vehChromeEnvThreshold)	\
	X(pbrIblAmbientWeight)		\
	X(tonemapAutoExposure)	\
	X(tonemapAdaptSpeed)	\
	X(tonemapKeyStrength)	\
	X(tonemapMinExposure)	\
	X(tonemapMaxExposure)	\
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
		menu.envFresnel = DebugMenuAddVar("SkyGFX|Misc", "Env Car Fresnel", &config->envFresnel, nil, 0.01f, 0.0f, 0.4f);
		menu.vehEnvIntensity = DebugMenuAddVar("SkyGFX|Misc", "Env Reflection Intensity", &config->vehEnvIntensity, nil, 0.01f, 0.0f, 0.4f);
		menu.vehChromeEnvThreshold = DebugMenuAddVar("SkyGFX|Misc", "Chrome Env Threshold", &config->vehChromeEnvThreshold, nil, 0.1f, 0.0f, 10.0f);
		menu.pbrIblAmbientWeight = DebugMenuAddVar("SkyGFX|Misc", "IBL Ambient Weight", &config->pbrIblAmbientWeight, nil, 0.05f, 0.0f, 2.0f);
		menu.tonemapAutoExposure = DebugMenuAddVarBool32("SkyGFX|Misc", "Frame-adaptive Tonemap", (int32*)&config->tonemapAutoExposure, nil);
		menu.tonemapAdaptSpeed = DebugMenuAddVar("SkyGFX|Misc", "Tonemap Adapt Speed", &config->tonemapAdaptSpeed, nil, 0.01f, 0.01f, 1.0f);
		menu.tonemapKeyStrength = DebugMenuAddVar("SkyGFX|Misc", "Tonemap Key Strength", &config->tonemapKeyStrength, nil, 0.05f, 0.0f, 1.0f);
		menu.tonemapMinExposure = DebugMenuAddVar("SkyGFX|Misc", "Tonemap Min Exposure", &config->tonemapMinExposure, nil, 0.05f, -0.5f, 0.5f);
		menu.tonemapMaxExposure = DebugMenuAddVar("SkyGFX|Misc", "Tonemap Max Exposure", &config->tonemapMaxExposure, nil, 0.05f, -0.5f, 1.0f);
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
		menu.ssaoPower = DebugMenuAddVar("SkyGFX|SSAO", "SSAO Power", &config->ssaoPower, nil, 0.1f, 0.5f, 4.0f);
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

// ============================================================
// Named configs: files under <game>\configs\*.ini
// (Save as New Config / Saved configs selector)
// ============================================================
static char s_cfgNameBuf[64] = "";
static std::vector<std::string> s_cfgList;
static std::string s_cfgSel;

static void
RefreshCfgList(void)
{
	s_cfgList.clear();
	WIN32_FIND_DATAA fd;
	char pattern[MAX_PATH];
	snprintf(pattern, MAX_PATH, "%sconfigs\\*.ini", asipath);
	HANDLE h = FindFirstFileA(pattern, &fd);
	if(h == INVALID_HANDLE_VALUE)
		return;	// configs/ missing or empty — empty list, shown gracefully
	do{
		if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;
		std::string nm = fd.cFileName;
		size_t len = nm.size();
		if(len > 4 && _stricmp(nm.c_str() + len - 4, ".ini") == 0)
			nm.resize(len - 4);
		if(!nm.empty())
			s_cfgList.push_back(nm);
	}while(FindNextFileA(h, &fd));
	FindClose(h);
	std::sort(s_cfgList.begin(), s_cfgList.end());
}

static bool
IsValidCfgName(const char *s)
{
	size_t len = strlen(s);
	if(len == 0 || len >= sizeof(s_cfgNameBuf))
		return false;
	if(!strcmp(s, ".") || !strcmp(s, ".."))
		return false;
	// reject path separators / Windows-illegal filename characters
	return strpbrk(s, "\\/:*?\"<>|") == NULL;
}

// Custom Maxed resolution (Style combo): load the NEWEST file under
// <game>\configs\*.ini — the user's latest "Save as New Config". No
// configs directory yet (only skygfx.ini exists) => no-op: Custom Maxed
// = the current in-memory/INI state. loadConfigFile re-runs readIniFile
// (incl. the envFresnel/vehEnvIntensity clamps) + refreshIni.
static void
LoadNewestCustomConfig(void)
{
	WIN32_FIND_DATAA fd;
	char pattern[MAX_PATH];
	snprintf(pattern, MAX_PATH, "%sconfigs\\*.ini", asipath);
	HANDLE h = FindFirstFileA(pattern, &fd);
	if(h == INVALID_HANDLE_VALUE){
		if(dbglog_throttle("preset_custom"))
			dbglog("[Menu] Custom Maxed: no configs\\*.ini found - keeping current state");
		return;
	}
	char newestPath[MAX_PATH] = "";
	FILETIME newest;
	ZeroMemory(&newest, sizeof(newest));
	do{
		if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;
		size_t len = strlen(fd.cFileName);
		if(len <= 4 || _stricmp(fd.cFileName + len - 4, ".ini") != 0)
			continue;
		if(newestPath[0] == '\0' || CompareFileTime(&fd.ftLastWriteTime, &newest) > 0){
			snprintf(newestPath, MAX_PATH, "%sconfigs\\%s", asipath, fd.cFileName);
			newest = fd.ftLastWriteTime;
		}
	}while(FindNextFileA(h, &fd));
	FindClose(h);
	if(newestPath[0] == '\0'){
		if(dbglog_throttle("preset_custom"))
			dbglog("[Menu] Custom Maxed: configs\\ empty - keeping current state");
		return;
	}
	loadConfigFile(newestPath);	// may overwrite config->preset; caller sets PRESET_CUSTOM after
	if(dbglog_throttle("preset_custom"))
		dbglog("[Menu] Custom Maxed loaded newest config: %s", newestPath);
}

// ============================================================
// DEPLOYED-SETTINGS INVENTORY
//   Tabs: Pipes / Effects / Water / PostFX / Vehicle / Utility /
//         Presets / INI Keys
//
// Goal (user): "I know what settings are actually deployed" — every key
// skygfx.ini can carry is listed in-game next to the INI key it maps to,
// and every edit writes the SAME variable readIniFile()/refreshIni() read,
// so F11 "Reload Inis" and this menu can never disagree.
//
// Rows whose INI key has NO live variable (startup-only patches, the
// load-time-only quality preset, keys readIniFile never parses) are listed
// GREYED so nothing is invisible.
//
// Rows are built lazily and REBUILT whenever `config` changes — `config`
// is a pointer that moves with the Config selector, exactly the reason
// refreshMenu() re-points the legacy DebugMenu entries.
// ============================================================

// globals owned by main.cpp (non-static -> menu writes go through the very
// same variables readIniFile reads)
extern bool disableClouds, disableGamma, fixPcCarLight, privateHooks, forceWindShader;
extern int  transparentLockon, fixShadows;
extern int  explicitBuildingPipe_tmp;
// [General] NoiseQuality lives in a TU-static in main.cpp — accessors live
// in the debug-menu registration block of main.cpp.
extern int  menuGetNoiseQuality(void);
extern void menuSetNoiseQuality(int q);

enum RowKind {
	RK_BOOL,		// int-sized bool (RwBool / int 0|1) -> checkbox
	RK_BOOL8,		// 1-byte bool -> checkbox
	RK_INT,			// int -> slider
	RK_FLOAT,		// float -> slider
	RK_ENUM,		// int -> combo, value = base + index
	RK_STATIC,		// int shown GREYED (no live edit path)
	RK_STATICB,		// bool shown GREYED
	RK_NONE,		// no variable at all -> GREYED
	RK_NOISEQ		// [General] NoiseQuality -> main.cpp accessors
};

struct SettingRow {
	const char *tab;	// tab this row lives on
	const char *grp;	// collapsing header inside that tab
	const char *key;	// exact INI key
	const char *label;	// UI label
	RowKind     kind;
	void       *ptr;	// NULL => no live variable
	float       lo, hi;
	const char *const *names;	// NULL-terminated (RK_ENUM)
	int         base;			// value of names[0]
	void      (*onEdit)(void);	// extra write-through after a change
	const char *note;			// tooltip extra
};

// --- enum label tables (same spellings readIniFile's StrAssoc maps use) ---
static const char *const s_pipeNames[]   = { "PBR / Modern", "PS2", "Xbox", "Mobile", "GTAIV", NULL };
static const char *const s_buildNames[]  = { "PS2", "Xbox", "GTAIV", "PBR", NULL };
static const char *const s_vehNames[]    = { "PS2", "PC", "Xbox", "Specular", "Mobile", "Neo", "Leeds/LCS", "VCS", "Env", "GTAIV", "Modern", NULL };
static const char *const s_cfNames[]     = { "None", "PS2", "PC", "Mobile", "III", "VC", "VCS", "GTAIV", "Modern", NULL };
static const char *const s_waterSty[]    = { "Xbox", "GTA IV", "GTA V", NULL };
static const char *const s_waterQ[]      = { "Low", "Medium", "High", "Ultra", NULL };
static const char *const s_ps2pc[]       = { "PS2", "PC", NULL };
static const char *const s_shadow[]      = { "Default (key absent)", "0 / PS2", "1 / PC", NULL };	// base -1
static const char *const s_corona[]      = { "- (leave alone)", "PS2 (disable z-test)", "PC (force z-test)", NULL };	// base -1
static const char *const s_rad[]         = { "PS2", "Shader", NULL };
static const char *const s_lightning[]   = { "Sky only", "Sky and objects", NULL };
static const char *const s_noise[]       = { "32 px", "128 px", "512 px", "1024 px", NULL };

// pipeline -> pipes mapping, byte-for-byte the switch readIniFile applies to
// the `pipeline` key. Without this the combo would write a value
// derivePipelineFromPipes() re-derives away on the next frame.
static void
ApplyPipelineFromMenu(void)
{
	switch(config->pipeline){
	case PIPELINE_PBR:
		config->buildingPipe = BUILDING_PBR;	config->vehiclePipe = CAR_MODERN;
		config->colorFilter = COLORFILTER_MODERN;
		break;
	case PIPELINE_PS2:
		config->buildingPipe = BUILDING_PS2;	config->vehiclePipe = CAR_PS2;
		break;
	case PIPELINE_XBOX:
		config->buildingPipe = BUILDING_XBOX;	config->vehiclePipe = CAR_XBOX;
		break;
	case PIPELINE_MOBILE:
		config->buildingPipe = BUILDING_XBOX;	config->vehiclePipe = CAR_MOBILE;
		break;
	case PIPELINE_GTAIV:
		config->buildingPipe = BUILDING_GTAIV;	config->vehiclePipe = CAR_GTAIV;
		break;
	default:
		config->buildingPipe = BUILDING_PBR;	config->vehiclePipe = CAR_MODERN;
		break;
	}
}

static void
ApplyPipelineOverrideFromMenu(void)
{
	// same gate readIniFile applies (0=PBR 1=PS2 2=Xbox 3=Mobile 4=GTAIV,
	// -1 = disabled); -1 keeps whatever the pipes say.
	if(config->pipelineOverride >= 0 && config->pipelineOverride <= 4){
		config->pipeline = config->pipelineOverride;
		ApplyPipelineFromMenu();
	}
}

static int
NoiseResForQuality(int q)
{
	return q <= 0 ? 32 : (q == 1 ? 128 : (q == 2 ? 512 : 1024));	// main.cpp utilityNoiseResForQuality
}

static std::vector<SettingRow>
BuildSettingRows(void)
{
	std::vector<SettingRow> v;
	const char *tab = "";
	const char *grp = "";

	#define G(T, GR)	do { tab = (T); grp = (GR); } while(0)
	#define A(K,L,KD,P,LO,HI,NM,BS,OE,NT) \
		v.push_back(SettingRow{ tab, grp, (K), (L), (KD), (void*)(P), (float)(LO), (float)(HI), (NM), (BS), (OE), (NT) })
	#define rW(K,L,P)					A(K,L,RK_BOOL,(P),0,0,NULL,0,NULL,NULL)
	#define rW8(K,L,P)					A(K,L,RK_BOOL8,(P),0,0,NULL,0,NULL,NULL)
	#define rI(K,L,P,LO,HI)				A(K,L,RK_INT,(P),(LO),(HI),NULL,0,NULL,NULL)
	#define rF(K,L,P,LO,HI)				A(K,L,RK_FLOAT,(P),(LO),(HI),NULL,0,NULL,NULL)
	#define rE(K,L,P,NM,BS)				A(K,L,RK_ENUM,(P),0,0,(NM),(BS),NULL,NULL)
	#define rS(K,L,P)						A(K,L,RK_STATIC,(P),0,0,NULL,0,NULL,NULL)
	#define rS8(K,L,P)						A(K,L,RK_STATICB,(P),0,0,NULL,0,NULL,NULL)
	#define rN(K,L,NT)						A(K,L,RK_NONE,NULL,0,0,NULL,0,NULL,(NT))
	#define rWn(K,L,P,NT)					A(K,L,RK_BOOL,(P),0,0,NULL,0,NULL,(NT))
	#define rIn(K,L,P,LO,HI,NT)			A(K,L,RK_INT,(P),(LO),(HI),NULL,0,NULL,(NT))
	#define rFn(K,L,P,LO,HI,NT)			A(K,L,RK_FLOAT,(P),(LO),(HI),NULL,0,NULL,(NT))
	#define rEn(K,L,P,NM,BS,NT)			A(K,L,RK_ENUM,(P),0,0,(NM),(BS),NULL,(NT))
	#define rSn(K,L,P,NT)					A(K,L,RK_STATIC,(P),0,0,NULL,0,NULL,(NT))
	#define rIo(K,L,P,LO,HI,OE,NT)		A(K,L,RK_INT,(P),(LO),(HI),NULL,0,(OE),(NT))
	#define rEo(K,L,P,NM,BS,OE,NT)		A(K,L,RK_ENUM,(P),0,0,(NM),(BS),(OE),(NT))

	// ================= TAB: Pipes =================
	G("Pipes", "Unified pipeline");
	rEo("pipeline", "Unified pipeline", &config->pipeline, s_pipeNames, 0, ApplyPipelineFromMenu,
		"readIniFile maps this key onto buildingPipe+vehiclePipe+colorFilter;\nthe menu applies the identical mapping, then derivePipelineFromPipes()\nre-derives it from the pipes each frame.");
	rIo("pipelineOverride", "Pipeline override", &config->pipelineOverride, -1, 4, ApplyPipelineOverrideFromMenu,
		"-1 = off. 0=PBR 1=PS2 2=Xbox 3=Mobile 4=GTAIV — forces `pipeline` at load;\nhere it applies the same mapping immediately.");
	rN("qualityPreset", "Quality preset",
		"LOAD-TIME ONLY: seeds feature defaults before individual keys override them.\nreadIniFile reads it into a local — there is no live Config field to edit.");
	rSn("gamePreset", "Game preset (index)", &config->preset,
		"config->preset — written by the Style combo / presets and saved as gamePreset.\nRaw index shown; use the Presets tab or Graphics > Style to change it.");

	G("Pipes", "Rendering pipes");
	rE("buildingPipe", "Building pipe", &config->buildingPipe, s_buildNames, 0);
	rE("vehiclePipe", "Vehicle pipe", &config->vehiclePipe, s_vehNames, 0);
	rE("waterStyle", "Water style", &config->waterStyle, s_waterSty, 0);
	rE("colorFilter", "Colour filter", &config->colorFilter, s_cfNames, 0);
	// colorFilterEnable gate: bound ONCE, in Effects > Effect switches (was
	// duplicated here as a second identical row writing the same var — fix-6).

	G("Pipes", "PBR building reflection");
	rWn("bldEnvReflect", "Env-mapped materials reflect", &config->bldEnvReflect,
		"Restores the additive Envcolor = 192/128 * shininess reflection that the GTA IV / PS2\n"
		"/ Xbox building cbs run for materials with the envmap flag — the PBR cb had no\n"
		"branch for them, so Building=PBR rendered those materials flat.\n"
		"0 = legacy PBR (no building reflections at all).");
	rFn("bldSkyReflect", "Sky reflection strength", &config->bldSkyReflect, 0.0f, 1.0f,
		"Superset term the GTA IV pipe does NOT have: Fresnel-weighted reflection of the\n"
		"IBL sky capture for materials without their own env map. Energy-consistent\n"
		"(F*env + (1-F)*body, weight forced to 0 when the capture is empty). 0 = off.");

	// ================= TAB: Effects =================
	G("Effects", "Effect switches");
	// ssaoEnable also appears on PostFX > SSAO next to its detail sliders —
	// intentional master/detail mirror (same var as waterParallaxEnable's
	// Effects/Water pair). Tabs render one at a time and the reference view
	// dedups by key, so a click always writes exactly once.
	rW("ssaoEnable", "SSAO", &config->ssaoEnable);
	rW("smaaEnable", "SMAA", &config->smaaEnable);
	rW("godRaysEnable", "God rays", &config->godRaysEnable);
	rWn("pipeChainEnable", "Pipe chain / SSR", &config->pipeChainEnable,
		"BRDF-classified screen-space reflections packed by the geometry pipes.");
	rF("pipeChainIntensity", "Pipe chain intensity", &config->pipeChainIntensity, 0.0f, 1.0f);
	rW("doRadiosity", "Radiosity", &config->doRadiosity);
	rW("radiosityEnable", "Radiosity postfx gate", &config->radiosityEnable);
	rW("vcsTrails", "VCS trails", &config->vcsTrails);
	rW("motionBlurEnable", "Motion blur", &config->motionBlurEnable);
	rW("motionBlurCameraAware", "Motion blur camera-aware", &config->motionBlurCameraAware);
	rW("heightFogEnable", "Height fog", &config->heightFogEnable);
	rW("grainEnable", "Film grain gate", &config->grainEnable);
	rE("grainFilter", "Grain filter", &config->grainFilter, s_ps2pc, 0);
	A("colorFilterEnable", "Colour filter gate", RK_BOOL, &config->colorFilterEnable, 0, 0, NULL, 0, cfEnableRowIntent,
		"Postfx switch for the colour-filter pass; the ONLY menu binding for this\n"
		"field (the filter style enum lives on Pipes > Rendering pipes).\n"
		"0 = bypass the pass entirely (black-screen isolation). Toggles record\n"
		"user intent via main.cpp's latch (silent-write self-heal).");
	rW("sssEnable", "Screen-space SSS", &config->sssEnable);
	rW("sssPostProcessEnable", "SSS post-process blur", &config->sssPostProcessEnable);
	rW("usePCTimecyc", "PC timecyc", &config->usePCTimecyc);
	rE("infraredVision", "Infrared vision", &config->infraredVision, s_ps2pc, 0);
	rE("nightVision", "Night vision", &config->nightVision, s_ps2pc, 0);

	G("Effects", "Buffers / geometry switches");
	rWn("normalBufferEnable", "Normal buffer", &config->normalBufferEnable,
		"Screen-space normal target (SSAO/SSR input).");
	rF("normalBufferOffset", "Normal buffer offset", &config->normalBufferOffset, 0.0f, 2.0f);
	rF("normalBufferScale", "Normal buffer scale", &config->normalBufferScale, 0.0f, 2.0f);
	rW("velocityBufferEnable", "Velocity buffer", &config->velocityBufferEnable);
	rW("forwardPlusEnable", "Forward+ tiled lighting", &config->forwardPlusEnable);
	rW("depthHookEnable", "INTZ depth hook", &config->depthHookEnable);
	rW("ragdollEnable", "Death ragdoll", &config->ragdollEnable);
	rWn("stochasticTexturing", "Stochastic texturing", &config->stochastic,
		"config->stochastic — tiled-texture noise randomisation.");
	rW("detailMaps", "Detail maps", &config->detailMaps);
	rWn("waterParallaxEnable", "Water parallax", &config->waterParallaxEnable,
		"Master switch (also listed on the Water tab with its scale).");
	rW("postfxDumpDebug", "PostFX dump debug", &config->postfxDumpDebug);
	rW("vehAutoChrome", "Auto chrome by part name", &config->vehAutoChrome);

	G("Effects", "Dual-pass / PS2 modulate");
	rWn("dualPass", "Dual-pass global", &config->dualPassGlobal,
		"Master; per-type rows below default to it when the INI key is absent.");
	rW("dualPassDefault", "Dual-pass default", &config->dualPassDefault);
	rW("dualPassBuilding", "Dual-pass buildings", &config->dualPassBuilding);
	rW("dualPassVehicle", "Dual-pass vehicles", &config->dualPassVehicle);
	rW("dualPassPed", "Dual-pass peds", &config->dualPassPed);
	rW("dualPassGrass", "Dual-pass grass", &config->dualPassGrass);
	rI("zwriteThreshold", "Alpha threshold", &config->zwriteThreshold, 0, 255);
	rI("zwriteThresholdGrass", "Alpha threshold (grass)", &config->zwriteThresholdGrass, 0, 255);
	rI("zwriteThresholdPed", "Alpha threshold (ped)", &config->zwriteThresholdPed, 0, 255);
	rWn("ps2Modulate", "PS2 modulate global", &config->ps2ModulateGlobal,
		"Master; per-type rows below default to it when the INI key is absent.");
	rW("ps2ModulateBuilding", "PS2 modulate buildings", &config->ps2ModulateBuilding);
	rW("ps2ModulateGrass", "PS2 modulate grass", &config->ps2ModulateGrass);

	G("Effects", "Grass");
	rW("grassAddAmbient", "Add ambient", &config->grassAddAmbient);
	rW("grassBackfaceCull", "Backface cull", &config->backfaceCull);
	rW("grassFixPlacement", "Fix placement", &config->fixGrassPlacement);

	G("Effects", "Shadows");
	rEn("pedShadows", "Ped shadows", &config->pedShadows, s_shadow, -1,
		"INI accepts 0/1 (readIni's boolMap); 2 = deployed value that maps to\n\"key absent\" (-1) — shown raw so nothing is hidden.");
	rEn("stencilShadows", "Stencil shadows", &config->stencilShadows, s_shadow, -1,
		"INI accepts 0/1; 2 = deployed value that maps to -1 (leave alone).");

	G("Effects", "Radiosity detail");
	rE("radiosity", "Radiosity type", &config->radiosity, s_rad, 0);
	rI("radiosityFilterPasses", "Filter passes", &config->radiosityFilterPasses, 0, 8);
	rI("radiosityRenderPasses", "Render passes", &config->radiosityRenderPasses, 0, 8);
	rI("radiosityIntensity", "Intensity", &config->radiosityIntensity, 0, 255);

	G("Effects", "VCS trails");
	rI("trailsLimit", "Trail limit", &config->trailsLimit, 0, 255);
	rI("trailsIntensity", "Trail intensity", &config->trailsIntensity, 0, 255);
	rI("trailsResolution", "Trail resolution", &config->trailsResolution, 1, 4);

	G("Effects", "Motion blur detail");
	rF("motionBlurStrength", "Strength", &config->motionBlurStrength, 0.0f, 1.0f);
	rF("motionBlurRadial", "Radial", &config->motionBlurRadial, 0.0f, 1.0f);
	rF("motionBlurSpeedFactor", "Speed factor", &config->motionBlurSpeedFactor, 0.0f, 2.0f);

	G("Effects", "Height fog detail");
	rF("heightFogDensity", "Density", &config->heightFogDensity, 0.0f, 0.01f);
	rF("heightFogHeightFalloff", "Height falloff", &config->heightFogHeightFalloff, 0.0f, 5.0f);
	rF("heightFogStartHeight", "Start height", &config->heightFogStartHeight, -100.0f, 500.0f);
	rF("heightFogR", "Fog R", &config->heightFogR, 0.0f, 1.0f);
	rF("heightFogG", "Fog G", &config->heightFogG, 0.0f, 1.0f);
	rF("heightFogB", "Fog B", &config->heightFogB, 0.0f, 1.0f);
	rFn("heightFogTimecycleScale", "Timecycle scale", &config->heightFogTimecycleScale, 0.0f, 5.0f,
		"All-zero R/G/B = use the timecycle horizon colour.");

	G("Effects", "God rays detail");
	rF("godRaysExposure", "Exposure", &config->godRaysExposure, 0.0f, 0.05f);
	rF("godRaysDecay", "Decay", &config->godRaysDecay, 0.0f, 2.0f);
	rF("godRaysDensity", "Density", &config->godRaysDensity, 0.0f, 2.0f);
	rF("godRaysWeight", "Weight", &config->godRaysWeight, 0.0f, 5.0f);
	rI("godRaysNumSamples", "Samples", &config->godRaysNumSamples, 1, 64);

	G("Effects", "Normal mapping");
	rWn("normalMapEnable", "Normal mapping", &config->normalMapEnable,
		"Needs the RW normal-map plugin (gHasExternalNormalMapPlugin).");
	rF("normalMapIntensity", "NM intensity", &config->normalMapIntensity, 0.0f, 2.0f);
	rW("normalMapPlayerOnly", "Player only", &config->normalMapPlayerOnly);

	G("Effects", "Sun & lens");
	rWn("sunGlare", "Sun glare", &config->doglare,
		"config->doglare — hook installed at startup when the game has sun glare.");
	rF("sunCoronaIntensity", "Corona intensity", &config->sunCoronaIntensity, 0.0f, 10.0f);
	rF("sunCoreIntensity", "Core intensity", &config->sunCoreIntensity, 0.0f, 10.0f);
	rF("sunStreakIntensity", "Streak intensity", &config->sunStreakIntensity, 0.0f, 10.0f);
	rF("sunStreakSize", "Streak size", &config->sunStreakSize, 0.0f, 10.0f);

	G("Effects", "Weather hooks");
	rE("lightningIlluminatesWorld", "Lightning illuminates", &config->lightningIlluminatesWorld, s_lightning, 0);
	rEn("coronaZtest", "Corona z-test", &config->coronaZtest, s_corona, -1,
		"Patches game code — applied through resetValues() when changed.");

	G("Effects", "Material enhancement (skin/hair/vegetation)");
	rW("skinEnhanceEnable", "Skin enhance", &config->skinEnhanceEnable);
	rF("skinWrapFactor", "Skin wrap factor", &config->skinWrapFactor, 0.0f, 1.0f);
	rF("skinSpecularPower", "Skin specular power", &config->skinSpecularPower, 0.0f, 100.0f);
	rF("skinSpecularStrength", "Skin specular strength", &config->skinSpecularStrength, 0.0f, 2.0f);
	rF("skinSSSStrength", "Skin SSS strength", &config->skinSSSStrength, 0.0f, 1.0f);
	rW("hairEnhanceEnable", "Hair enhance", &config->hairEnhanceEnable);
	rF("hairAnisotropicPower", "Hair aniso power", &config->hairAnisotropicPower, 0.0f, 128.0f);
	rF("hairAnisotropicStrength", "Hair aniso strength", &config->hairAnisotropicStrength, 0.0f, 2.0f);
	rF("hairSSSStrength", "Hair SSS strength", &config->hairSSSStrength, 0.0f, 1.0f);
	rW("vegetationEnhanceEnable", "Vegetation enhance", &config->vegetationEnhanceEnable);
	rF("vegetationSSSStrength", "Veg SSS strength", &config->vegetationSSSStrength, 0.0f, 1.0f);
	rF("vegetationAmbientBoost", "Veg ambient boost", &config->vegetationAmbientBoost, 0.0f, 3.0f);

	G("Effects", "Neo effects");
	rW("neoWaterDrops", "Neo water drops", &config->neoWaterDrops);
	rW("neoBloodDrops", "Neo blood drops", &config->neoBloodDrops);

	// ================= TAB: Water =================
	G("Water", "Water style & quality");
	rE("waterStyle", "Water style", &config->waterStyle, s_waterSty, 0);
	rE("waterQuality", "Water quality", &config->waterQuality, s_waterQ, 0);
	rWn("waterUseTimecycle", "Colour from timecycle", &config->waterUseTimecycle,
		"1 = water colour/alpha from timecyc, 0 = the Deep/Shallow keys below win.");

	G("Water", "Water appearance");
	rW("waterParallaxEnable", "Parallax", &config->waterParallaxEnable);
	rF("waterParallaxScale", "Parallax scale", &config->waterParallaxScale, 0.0f, 4.0f);
	rF("waterNormalStrength", "Normal strength", &config->waterNormalStrength, 0.0f, 4.0f);
	rF("waterFresnelPower", "Fresnel power", &config->waterFresnelPower, 0.0f, 12.0f);
	rF("waterSpecularPower", "Specular power", &config->waterSpecularPower, 1.0f, 1024.0f);
	rF("waterSpecularIntensity", "Specular intensity", &config->waterSpecularIntensity, 0.0f, 4.0f);
	rF("waterUnderwaterFog", "Underwater fog", &config->waterUnderwaterFog, 0.0f, 4.0f);
	rF("waterFoamThreshold", "Foam threshold", &config->waterFoamThreshold, 0.0f, 1.0f);
	rF("waterFoamSoftness", "Foam softness", &config->waterFoamSoftness, 0.0f, 1.0f);
	rF("waterShallowR", "Shallow R", &config->waterShallowR, 0.0f, 1.0f);
	rF("waterShallowG", "Shallow G", &config->waterShallowG, 0.0f, 1.0f);
	rF("waterShallowB", "Shallow B", &config->waterShallowB, 0.0f, 1.0f);
	rF("waterDeepR", "Deep R", &config->waterDeepR, 0.0f, 1.0f);
	rF("waterDeepG", "Deep G", &config->waterDeepG, 0.0f, 1.0f);
	rF("waterDeepB", "Deep B", &config->waterDeepB, 0.0f, 1.0f);
	rF("waterReflectionFarClip", "Reflection far clip", &config->waterReflectionFarClip, 0.0f, 4000.0f);
	rF("waterReflectionStrength", "Reflection strength", &config->waterReflectionStrength, 0.0f, 1.0f);
	rF("waterTileScale", "Tile scale", &config->waterTileScale, 0.1f, 8.0f);
	rF("waterShoreFade", "Shore fade", &config->waterShoreFade, 0.0f, 20.0f);
	rF("waterTranslucency", "Translucency", &config->waterTranslucency, 0.0f, 2.0f);

	// ================= TAB: PostFX / Tonemap =================
	G("PostFX", "Tonemap");
	rW("tonemapAutoExposure", "Frame-adaptive exposure", &config->tonemapAutoExposure);
	rF("tonemapAdaptSpeed", "Adapt speed", &config->tonemapAdaptSpeed, 0.01f, 1.0f);
	rF("tonemapKeyStrength", "Key strength", &config->tonemapKeyStrength, 0.0f, 1.0f);
	rF("tonemapMinExposure", "Min exposure", &config->tonemapMinExposure, -0.5f, 0.5f);
	rF("tonemapMaxExposure", "Max exposure", &config->tonemapMaxExposure, -0.5f, 1.0f);
	rF("tonemapCurveLows", "Curve lows", &config->tonemapCurveLows, -1.0f, 1.0f);
	rF("tonemapCurveHighs", "Curve highs", &config->tonemapCurveHighs, -1.0f, 1.0f);
	rF("tonemapCurveMid", "Curve mid", &config->tonemapCurveMid, 0.0f, 1.0f);
	rF("tonemapBlackLift", "Black lift", &config->tonemapBlackLift, 0.0f, 0.1f);

	G("PostFX", "GTA IV grade");
	rWn("ivMode", "IV mode", &config->ivMode,
		"Toggling ON applies the IV default pipe/filter set (IVMode_ApplyDefaults);\nthe grade sliders below are NOT reset by it.");
	rF("ivDesaturation", "Desaturation", &config->ivDesaturation, 0.0f, 2.0f);
	rF("ivGamma", "Gamma", &config->ivGamma, 0.5f, 2.0f);
	rF("ivSaturation", "Saturation", &config->ivSaturation, -1.0f, 2.0f);
	rF("ivCurves", "Curves", &config->ivCurves, 0.0f, 2.0f);
	rF("ivVignetteIntensity", "Vignette intensity", &config->ivVignetteIntensity, 0.0f, 2.0f);
	rF("ivVignetteRadius", "Vignette radius", &config->ivVignetteRadius, 0.0f, 2.0f);
	rF("ivVignetteContrast", "Vignette contrast", &config->ivVignetteContrast, 0.0f, 5.0f);
	rF("ivBloomIntensity", "Bloom", &config->ivBloomIntensity, 0.0f, 2.0f);
	rF("ivExposure", "Exposure", &config->ivExposure, 0.0f, 3.0f);

	G("PostFX", "Screen-space SSS");
	rW("sssEnable", "SSS on", &config->sssEnable);
	rF("sssIntensity", "Global intensity", &config->sssIntensity, 0.0f, 1.0f);
	rF("sssVegIntensity", "Vegetation", &config->sssVegIntensity, 0.0f, 1.0f);
	rF("sssSkinIntensity", "Skin", &config->sssSkinIntensity, 0.0f, 1.0f);
	rF("sssClothIntensity", "Cloth", &config->sssClothIntensity, 0.0f, 1.0f);
	rW("sssPostProcessEnable", "Post-process blur", &config->sssPostProcessEnable);
	rF("sssPostProcessStrength", "PP strength", &config->sssPostProcessStrength, 0.0f, 1.0f);
	rF("sssPostProcessRadius", "PP radius", &config->sssPostProcessRadius, 0.0f, 20.0f);
	rF("sssPostProcessThreshold", "PP threshold", &config->sssPostProcessThreshold, 0.0f, 1.0f);
	rF("sssAmbientBoost", "Ambient boost", &config->sssAmbientBoost, 0.0f, 2.0f);

	G("PostFX", "SSAO");
	rW("ssaoEnable", "SSAO on", &config->ssaoEnable);	// mirrors Effects > Effect switches (same var; one write per click)
	rF("ssaoRadius", "Radius", &config->ssaoRadius, 0.0f, 5.0f);
	rF("ssaoPower", "Power", &config->ssaoPower, 0.5f, 4.0f);
	rFn("ssaoKernelSize", "Kernel size", &config->ssaoKernelSize, 1.0f, 64.0f,
		"Not wired to the shader — AO spread is Radius. Kept for INI compat.");
	rIn("ssaoSampleCount", "Sample count", &config->ssaoSampleCount, 1, 64,
		"Fixed at 16 samples in the shader. Kept for INI compat.");
	rW("ssaoTemporalEnable", "Temporal", &config->ssaoTemporalEnable);
	rF("ssaoTemporalBlend", "Temporal blend", &config->ssaoTemporalBlend, 0.01f, 0.95f);
	rI("ssaoBlurPasses", "Blur passes", &config->ssaoBlurPasses, 0, 3);
	rF("ssaoBlurRadius", "Blur radius", &config->ssaoBlurRadius, 1.0f, 8.0f);
	rF("ssaoDepthThreshold", "Depth threshold", &config->ssaoDepthThreshold, 0.001f, 0.1f);

	G("PostFX", "Colour filter / YCbCr");
	rW8("YCbCrCorrection", "YCbCr correction", &config->bYCbCrFilter);
	rF("lumaScale", "Y scale", &config->lumaScale, 0.0f, 10.0f);
	rF("lumaOffset", "Y offset", &config->lumaOffset, -1.0f, 1.0f);
	rF("CbScale", "Cb scale", &config->cbScale, 0.0f, 10.0f);
	rF("CbOffset", "Cb offset", &config->cbOffset, -1.0f, 1.0f);
	rF("CrScale", "Cr scale", &config->crScale, 0.0f, 10.0f);
	rF("CrOffset", "Cr offset", &config->crOffset, -1.0f, 1.0f);
	rF("rgb1Mult", "RGB1 mult", &config->rgb1Mult, 0.0f, 2.0f);
	rF("rgb2Mult", "RGB2 mult", &config->rgb2Mult, 0.0f, 2.0f);

	G("PostFX", "Colour filter crop (blur offsets)");
	rIn("blurLeft", "Off left", &config->offLeft, -1000, 1000, "config->offLeft");
	rIn("blurTop", "Off top", &config->offTop, -1000, 1000, "config->offTop");
	rIn("blurRight", "Off right", &config->offRight, -1000, 1000, "config->offRight");
	rIn("blurBottom", "Off bottom", &config->offBottom, -1000, 1000, "config->offBottom");

	// ================= TAB: Vehicle / Chrome =================
	G("Vehicle", "Chrome BRDF");
	rF("chromeF0", "Chrome F0", &config->chromeF0, 0.0f, 1.0f);
	rF("chromeGloss", "Chrome gloss", &config->chromeGloss, 0.0f, 1.0f);
	rF("chromeMetallic", "Chrome metallic", &config->chromeMetallic, 0.0f, 1.0f);
	rF("chromeEnvBoost", "Chrome env boost", &config->chromeEnvBoost, 0.0f, 4.0f);
	rF("chromeClearcoat", "Chrome clearcoat", &config->chromeClearcoat, 0.0f, 1.0f);
	rW("vehAutoChrome", "Auto chrome (part names)", &config->vehAutoChrome);
	rFn("vehChromeEnvThreshold", "Chrome env threshold", &config->vehChromeEnvThreshold, 0.0f, 10.0f,
		"0 = disabled; promotes materials whose raw env-map shininess is >= this.");

	G("Vehicle", "Env map / reflections");
	rI("envMapSize", "Env map size", &config->envMapSize, 4, 2048);
	rW("envMapUseLODs", "Env map use LODs", &config->envMapUseLODs);
	rF("envMapFarClipMult", "Env far clip mult", &config->envMapFarClipMult, 0.1f, 10.0f);
	rF("envPower", "Env power", &config->envPower, 0.0f, 2000.0f);
	rF("envFresnel", "Env fresnel", &config->envFresnel, 0.0f, 0.4f);
	rF("envShininessMult", "Env shininess mult", &config->envShininessMult, 0.0f, 10.0f);
	rF("envSpecularityMult", "Env specularity mult", &config->envSpecularityMult, 0.0f, 10.0f);
	rF("vehEnvIntensity", "Env reflection intensity", &config->vehEnvIntensity, 0.0f, 0.4f);

	// Modern/Env reflection-model terms — one control per term ported from the
	// other car pipes; rationale lives in skygfx.h next to each field and in
	// the "reflection register contract" note in VehiclePBR_Modern.hlsl
	// (consumed through PS c44.z/w and c20.y/z/w). The three switches are
	// 0 = exact legacy Modern behaviour, so the old look is always one click
	// away while testing.
	rW("vehEnvFallback", "Env validity fallback (no black lerp)", &config->vehEnvFallback);
	rW("vehEnvStrengthMode", "Energy-consistent strength (F-coverage)", &config->vehEnvStrengthMode);
	rW("vehEnvMask", "Reflection mask (CarReflectionMask)", &config->vehEnvMask);
	rW("vehWetEnv", "Wet-road reflections", &config->vehWetEnv);
	rFn("vehEnvGlint", "Mobile sun glint strength", &config->vehEnvGlint, 0.0f, 1.0f,
		"Ported from main_mobileVehicle: pow(dot(reflectV, sunDir), 10) * 2 * sunColor,\n"
		"texture-independent so it survives a dead reflectionTex. 0 = off (Modern already\n"
		"has the GGX sun lobe and envGlint); also gated by layer bitmask bit2.");

	G("Vehicle", "PBR vehicle");
	rIn("vehPBRLayers", "PBR layer bitmask", &config->vehPBRLayers, 0, 255,
		"bit0 diffuse, 1 env, 2 sun/spec, 3 rim, 4 IBL, 5 sky tint, 6 clearcoat, 7 normal buffer. 255 = all on.");
	rF("pbrIblAmbientWeight", "IBL ambient weight", &config->pbrIblAmbientWeight, 0.0f, 2.0f);
	rF("pbrAmbientFloor", "Ambient floor", &config->pbrAmbientFloor, 0.0f, 1.0f);
	rF("vehPrelightFallback", "Non-prelit prelight floor", &config->vehPrelightFallback, 0.0f, 1.0f);

	G("Vehicle", "Legacy car-pipe shading");
	rF("leedsShininessMult", "Leeds shininess", &config->leedsShininessMult, 0.0f, 10.0f);
	rF("neoShininessMult", "Neo shininess", &config->neoShininessMult, 0.0f, 10.0f);
	rF("neoSpecularityMult", "Neo specularity", &config->neoSpecularityMult, 0.0f, 10.0f);

	// ================= TAB: Utility =================
	G("Utility", "Utility noise texture");
	A("NoiseQuality", "Noise quality", RK_NOISEQ, NULL, 0, 0, s_noise, 0, NULL,
		"[General] NoiseQuality -> tile resolution 0=32, 1=128, 2=512, >=3=1024.\nStatic in main.cpp: written through the menu accessors, then the tile is\nregenerated (same path refreshIni takes on F11).");

	G("Utility", "Hotkeys");
	rIn("keySwitch", "Menu hotkey (scan code)", &config->keys[0], 0, 512,
		"Hex in the INI (readhex). Applied live — the key poll reads config->keys.");
	rIn("keyReload", "Reload hotkey (scan code)", &config->keys[1], 0, 512,
		"Hex in the INI (readhex). Applied live — the key poll reads config->keys.");

	G("Utility", "Startup-only keys (restart required)");
	rS8("disableClouds", "disableClouds", &disableClouds);
	rS8("disableGamma", "disableGamma", &disableGamma);
	rS8("fixPcCarLight", "fixPcCarLight", &fixPcCarLight);
	rS8("privateHooks", "privateHooks", &privateHooks);
	rSn("forceWindShader", "forceWindShader", &forceWindShader,
		"Only ever set true by readIniFile (==1); never cleared by a reload.");
	rS("transparentLockon", "transparentLockon", &transparentLockon);
	rS("fixShadows", "fixShadows", &fixShadows);
	rSn("explicitBuildingPipe", "explicitBuildingPipe", &explicitBuildingPipe_tmp,
		"Copied to explicitBuildingPipe once at startup (InjectDelayedPatches).\nShown greyed: the live variable only moves on restart.");

	G("Utility", "Read by readIniFile but absent from skygfx.ini");
	rN("ps2grassFiles", "ps2grassFiles",
		"PARSER DISABLED: the read line is commented out in readIniFile.\nListed so it is not silently invisible.");

	#undef G
	#undef A
	#undef rW
	#undef rW8
	#undef rI
	#undef rF
	#undef rE
	#undef rS
	#undef rS8
	#undef rN
	#undef rWn
	#undef rIn
	#undef rFn
	#undef rEn
	#undef rSn
	#undef rIo
	#undef rEo
	return v;
}

static std::vector<SettingRow> &
GetSettingRows(void)
{
	static std::vector<SettingRow> rows;
	static Config *rowsCfg = NULL;
	if(!config)
		return rows;
	if(rows.empty() || rowsCfg != config){	// Config selector moved -> re-resolve pointers
		rows = BuildSettingRows();
		rowsCfg = config;
	}
	return rows;
}

static bool
RowIsGrey(const SettingRow &r)
{
	return r.kind == RK_STATIC || r.kind == RK_STATICB || r.kind == RK_NONE;
}

static const char *
RowSection(const SettingRow &r)
{
	return r.kind == RK_NOISEQ ? "General" : "SkyGfx";
}

static void
RowValueStr(const SettingRow &r, char *buf, int n)
{
	switch(r.kind){
	case RK_BOOL:
		snprintf(buf, n, "%d", (*(int*)r.ptr) ? 1 : 0);
		break;
	case RK_BOOL8:
		snprintf(buf, n, "%d", (*(bool*)r.ptr) ? 1 : 0);
		break;
	case RK_INT:
		snprintf(buf, n, "%d", *(int*)r.ptr);
		break;
	case RK_FLOAT:
		snprintf(buf, n, "%g", (double)*(float*)r.ptr);
		break;
	case RK_ENUM:{
		int v = *(int*)r.ptr;
		int i = v - r.base;
		if(r.names && i >= 0 && r.names[i])
			snprintf(buf, n, "%s (%d)", r.names[i], v);
		else
			snprintf(buf, n, "%d (out of list)", v);
		break;}
	case RK_STATIC:
		snprintf(buf, n, "%d", *(int*)r.ptr);
		break;
	case RK_STATICB:
		snprintf(buf, n, "%d", (*(bool*)r.ptr) ? 1 : 0);
		break;
	case RK_NOISEQ:{
		int q = menuGetNoiseQuality();
		if(q >= 0 && q <= 3 && s_noise[q])
			snprintf(buf, n, "%s (q=%d)", s_noise[q], q);
		else
			snprintf(buf, n, "%d (>=3 -> 1024)", q);
		break;}
	default:
		snprintf(buf, n, "n/a");
		break;
	}
}

// One row: [label] [control] [INI key]  — hover the key for value + note.
// Returns true when the user changed something (caller runs resetValues).
static bool
DrawSettingRow(const SettingRow &r)
{
	static const float kCtlX = 240.0f;
	static const float kCtlW = 155.0f;
	static const float kKeyX = 410.0f;
	bool changed = false;
	bool grey = RowIsGrey(r);
	char wid[96];

	snprintf(wid, sizeof(wid), "##ini_%s", r.key);

	// column 1 — label
	ImGui::AlignTextToFramePadding();
	if(grey)
		ImGui::TextDisabled("%s", r.label);
	else
		ImGui::TextUnformatted(r.label);

	// column 2 — control
	ImGui::SameLine(kCtlX);
	switch(r.kind){
	case RK_BOOL:{
		// ImGui::Checkbox takes bool* — stage through a bool, store back the
		// int-sized RwBool (little-endian 0/1, same as readIniFile writes).
		bool t = (*(int*)r.ptr) != 0;
		if(ImGui::Checkbox(wid, &t)){
			*(int*)r.ptr = t ? 1 : 0;
			changed = true;
		}
		break;}
	case RK_BOOL8:{
		bool t = *(bool*)r.ptr;
		if(ImGui::Checkbox(wid, &t)){
			*(bool*)r.ptr = t;
			changed = true;
		}
		break;}
	case RK_INT:{
		int t = *(int*)r.ptr;
		ImGui::SetNextItemWidth(kCtlW);
		if(ImGui::SliderInt(wid, &t, (int)r.lo, (int)r.hi)){
			*(int*)r.ptr = t;
			changed = true;
		}
		break;}
	case RK_FLOAT:{
		float t = *(float*)r.ptr;
		ImGui::SetNextItemWidth(kCtlW);
		if(ImGui::SliderFloat(wid, &t, r.lo, r.hi, "%.3f")){
			*(float*)r.ptr = t;
			changed = true;
		}
		break;}
	case RK_ENUM:{
		int v = *(int*)r.ptr;
		int i = v - r.base;
		char preview[64];
		if(r.names && i >= 0 && r.names[i])
			snprintf(preview, sizeof(preview), "%s", r.names[i]);
		else
			snprintf(preview, sizeof(preview), "%d (out of list)", v);
		ImGui::SetNextItemWidth(kCtlW);
		if(ImGui::BeginCombo(wid, preview)){
			for(int k = 0; r.names && r.names[k]; k++)
				if(ImGui::Selectable(r.names[k], k == i)){
					if(r.base + k != v){
						*(int*)r.ptr = r.base + k;
						changed = true;
					}
				}
			ImGui::EndCombo();
		}
		break;}
	case RK_NOISEQ:{
		int q = menuGetNoiseQuality();
		int shown = (q >= 0 && q <= 3) ? q : 0;
		ImGui::SetNextItemWidth(kCtlW);
		if(ImGui::BeginCombo(wid, q >= 0 && q <= 3 ? s_noise[shown] : ">=3 -> 1024 px")){
			for(int k = 0; k < 4; k++)
				if(ImGui::Selectable(s_noise[k], k == shown)){
					menuSetNoiseQuality(k);
					GenerateUtilityTexture(NoiseResForQuality(k));
					changed = true;
				}
			ImGui::EndCombo();
		}
		break;}
	case RK_STATIC:{
		char b[32];
		RowValueStr(r, b, sizeof(b));
		ImGui::TextDisabled("%s", b);
		break;}
	case RK_STATICB:{
		char b[32];
		RowValueStr(r, b, sizeof(b));
		ImGui::TextDisabled("%s", b);
		break;}
	case RK_NONE:
		ImGui::TextDisabled("(no live variable)");
		break;
	}

	// column 3 — INI key + value tooltip
	ImGui::SameLine(kKeyX);
	ImGui::TextDisabled("%s", r.key);
	if(ImGui::IsItemHovered()){
		char val[64];
		RowValueStr(r, val, sizeof(val));
		ImGui::SetTooltip("INI key: [%s] %s\nValue: %s%s%s%s",
			RowSection(r), r.key, val,
			"\nWritten straight into the variable readIniFile() reads —",
			grey ? "\nNO live edit path (greyed): " : "\n",
			r.note ? r.note : "");
	}

	if(changed){
		if(r.onEdit)
			r.onEdit();
		resetValues();	// config -> CPostEffects copy, same as refreshIni()
	}
	return changed;
}

static bool
RowMatches(const SettingRow &r, const char *filter)
{
	if(filter == NULL || filter[0] == '\0')
		return true;
	if(_stricmp(r.key, filter) == 0 || strstr(r.label, filter) != NULL)
		return true;
	// case-insensitive substring over key
	const char *h = r.key;
	while(*h){
		const char *a = h, *b = filter;
		while(*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)){ a++; b++; }
		if(!*b)
			return true;
		h++;
	}
	return false;
}

// Draw every row of one tab (group headers = the group field).
static bool
DrawRowsForTab(const char *tab, const char *filter)
{
	std::vector<SettingRow> &rows = GetSettingRows();
	bool changed = false;

	for(size_t i = 0; i < rows.size(); ){
		const SettingRow &first = rows[i];
		if(strcmp(first.tab, tab) != 0){
			i++;
			continue;
		}
		std::string t0 = first.tab, g0 = first.grp;
		size_t j = i;
		bool anyVisible = false;
		for(; j < rows.size() && t0 == rows[j].tab && g0 == rows[j].grp; j++)
			if(RowMatches(rows[j], filter))
				anyVisible = true;

		if(anyVisible){
			bool open = ImGui::CollapsingHeader(g0.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
			if(open)
				for(size_t k = i; k < j; k++){
					if(!RowMatches(rows[k], filter))
						continue;
					if(DrawSettingRow(rows[k]))
						changed = true;
				}
		}
		i = j;
	}
	return changed;
}

// Preset buttons — the five pipeline presets the plan asks for. PS2/Xbox/PC
// and IV go through ApplyPreset (the same path the Style combo uses);
// "Modern" has no GamePreset entry yet, so it sets the pipes directly
// (derivePipelineFromPipes() then locks pipeline=PBR + Modern colour filter).
static void
DrawPresetsTab(void)
{
	static const char *styleStr[] = { "PS2", "Xbox", "PC", "Custom Maxed" };
	int presetIdx = 3;
	if(config->preset == PRESET_SA_PS2) presetIdx = 0;
	else if(config->preset == PRESET_SA_XBOX) presetIdx = 1;
	else if(config->preset == PRESET_SA_PC) presetIdx = 2;
	// Gate on the Combo result (same as Graphics > Style): applying the
	// preset / loading the newest custom config EVERY FRAME while the tab
	// is visible continuously stomped the live config and fought every
	// other config writer (pipes, filter, toggles churned per frame).
	if(ImGui::Combo("Style (gamePreset)", &presetIdx, styleStr, 4)){
		if(presetIdx <= 2){
			static const int kStylePreset[3] = { PRESET_SA_PS2, PRESET_SA_XBOX, PRESET_SA_PC };
			ApplyPreset(config, kStylePreset[presetIdx]);
			resetValues();
		}else{
			// same "Custom Maxed" behaviour as Graphics > Style: newest
			// configs\*.ini (no-op when none exist — keeps current state)
			LoadNewestCustomConfig();
			config->preset = PRESET_CUSTOM;
		}
	}
	if(ImGui::IsItemHovered())
		ImGui::SetTooltip("Same ApplyPreset path as Graphics > Style; saved as gamePreset.");

	ImGui::Separator();
	ImGui::Text("Pipeline presets (set pipes + colour filter):");
	if(ImGui::Button("PS2")){ ApplyPreset(config, PRESET_SA_PS2); resetValues(); }
	ImGui::SameLine();
	if(ImGui::Button("Xbox")){ ApplyPreset(config, PRESET_SA_XBOX); resetValues(); }
	ImGui::SameLine();
	if(ImGui::Button("PC")){ ApplyPreset(config, PRESET_SA_PC); resetValues(); }
	ImGui::SameLine();
	if(ImGui::Button("IV")){ ApplyPreset(config, PRESET_IV_PC); resetValues(); }
	ImGui::SameLine();
	if(ImGui::Button("Modern")){
		config->buildingPipe = BUILDING_PBR;
		config->vehiclePipe = CAR_MODERN;
		config->colorFilter = COLORFILTER_MODERN;
		config->preset = PRESET_CUSTOM;
		resetValues();
	}
	if(ImGui::IsItemHovered())
		ImGui::SetTooltip("No GamePreset entry for PBR yet — sets the pipes directly.\nderivePipelineFromPipes() then reports pipeline=PBR.");
	ImGui::SameLine();
	if(ImGui::Button("Reload INI"))
		reloadAllInis();

	ImGui::Separator();
	ImGui::TextDisabled("PS2    -> building=PS2    vehicle=PS2     filter=PS2");
	ImGui::TextDisabled("Xbox   -> building=Xbox   vehicle=Xbox    filter=PC");
	ImGui::TextDisabled("PC     -> building=Xbox   vehicle=PC      filter=PC");
	ImGui::TextDisabled("IV     -> building=GTAIV  vehicle=GTAIV   filter=GTAIV");
	ImGui::TextDisabled("Modern -> building=PBR    vehicle=Modern  filter=Modern");
	ImGui::Separator();
	ImGui::Text("Current: building=%s  vehicle=%s  water=%s  filter=%s",
		s_buildNames[config->buildingPipe >= 0 && config->buildingPipe < 4 ? config->buildingPipe : 3],
		(config->vehiclePipe >= 0 && config->vehiclePipe < 11 && s_vehNames[config->vehiclePipe]) ? s_vehNames[config->vehiclePipe] : "?",
		(config->waterStyle >= 0 && config->waterStyle < 3) ? s_waterSty[config->waterStyle] : "?",
		(config->colorFilter >= 0 && config->colorFilter < 9) ? s_cfNames[config->colorFilter] : "?");
}

// INI Keys tab — every row, every key, greys included, with a filter.
static void
DrawReferenceTab(void)
{
	static char s_filter[64] = "";
	std::vector<SettingRow> &rows = GetSettingRows();

	ImGui::PushItemWidth(200.0f);
	ImGui::InputTextWithHint("##reffilter", "filter by INI key / label...", s_filter, sizeof(s_filter));
	ImGui::PopItemWidth();
	ImGui::SameLine();
	if(ImGui::Button("Clear"))
		s_filter[0] = '\0';
	if(ImGui::IsItemHovered())
		ImGui::SetTooltip("Unfiltered view lists every key skygfx.ini can carry.\nGreyed rows have no live variable (startup-only / load-time-only).");
	ImGui::SameLine();
	ImGui::TextDisabled("%d rows", (int)rows.size());
	ImGui::Separator();

	const char *filter = s_filter;
	std::vector<std::string> seen;	// a key lives on >1 tab on purpose — show it once here
	std::string curTab, curGrp;
	bool headerOpen = false;
	bool haveHeader = false;

	for(size_t i = 0; i < rows.size(); i++){
		const SettingRow &r = rows[i];
		if(!RowMatches(r, filter))
			continue;
		bool dup = false;
		for(size_t k = 0; k < seen.size(); k++)
			if(seen[k] == r.key){ dup = true; break; }
		if(dup)
			continue;
		seen.push_back(r.key);

		std::string t0 = r.tab, g0 = r.grp;
		if(!haveHeader || t0 != curTab || g0 != curGrp){
			curTab = t0; curGrp = g0;
			haveHeader = true;
			std::string title = curTab + " / " + curGrp;
			headerOpen = ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
		}
		if(headerOpen)
			DrawSettingRow(r);
	}
	if(haveHeader)
		ImGui::TextDisabled("%d unique keys listed", (int)seen.size());
}

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
		// wider than the old 420px: the deployed-settings tabs need
		// [label | control | INI key] columns side by side
		ImGui::SetNextWindowSize(ImVec2(760, 640), ImGuiCond_FirstUseEver);
		if(ImGui::Begin("SkyGFX", &open)){

			// === Deployed-settings tabs + GTA IV-style two-screen layout ===
			// First block: every INI key skygfx.ini can carry (see the
			// inventory above) grouped into the plan's sections.
			// "Graphics" = user-facing visual settings (like GTA IV SCR_DISPLAY + SCR_ADVANCED)
			// "Advanced" = technical fine-tuning (like GTA IV SCR_TGRAPHICS + FusionFix)

			if(ImGui::BeginTabBar("SkyGFXTabs")){

			// ============================================================
			// TAB: PIPES — building/vehicle/water/colour-filter + pipeline
			// ============================================================
			if(ImGui::BeginTabItem("Pipes")){
				DrawRowsForTab("Pipes", NULL);
				ImGui::EndTabItem();
			}

			// ============================================================
			// TAB: EFFECTS — every effect switch + its parameters
			// ============================================================
			if(ImGui::BeginTabItem("Effects")){
				DrawRowsForTab("Effects", NULL);
				ImGui::EndTabItem();
			}

			// ============================================================
			// TAB: WATER — style/quality + every water* key
			// ============================================================
			if(ImGui::BeginTabItem("Water")){
				DrawRowsForTab("Water", NULL);
				ImGui::EndTabItem();
			}

			// ============================================================
			// TAB: POSTFX — tonemap* / iv* / sss* / ssao* / colour filter
			// ============================================================
			if(ImGui::BeginTabItem("PostFX")){
				DrawRowsForTab("PostFX", NULL);
				ImGui::EndTabItem();
			}

			// ============================================================
			// TAB: VEHICLE — chrome* / env* / veh* + car-pipe shading
			// ============================================================
			if(ImGui::BeginTabItem("Vehicle")){
				DrawRowsForTab("Vehicle", NULL);
				ImGui::EndTabItem();
			}

			// ============================================================
			// TAB: UTILITY — noise quality, hotkeys, startup-only keys
			// ============================================================
			if(ImGui::BeginTabItem("Utility")){
				ImGui::TextDisabled("Noise tile: %dx%d (%s)",
					g_CurrentNoiseSize, g_CurrentNoiseSize,
					g_pUtilityNoise ? "resident" : "MISSING");
				ImGui::SameLine();
				if(ImGui::Button("Regenerate noise now"))
					GenerateUtilityTexture(NoiseResForQuality(menuGetNoiseQuality()));
				if(ImGui::IsItemHovered())
					ImGui::SetTooltip("Runs the same GenerateUtilityTexture() path\nrefreshIni() takes on F11 (RW must be up — it is, we're drawing).");
				ImGui::Separator();
				DrawRowsForTab("Utility", NULL);
				ImGui::EndTabItem();
			}

			// ============================================================
			// TAB: PRESETS — the five pipeline presets (pipe combos reachable)
			// ============================================================
			if(ImGui::BeginTabItem("Presets")){
				DrawPresetsTab();
				ImGui::EndTabItem();
			}

			// ============================================================
			// TAB: INI KEYS — full inventory, greys included, filterable
			// ============================================================
			if(ImGui::BeginTabItem("INI Keys")){
				DrawReferenceTab();
				ImGui::EndTabItem();
			}

			// ============================================================
			// TAB 1: GRAPHICS — User-facing visual settings
			// ============================================================
			if(ImGui::BeginTabItem("Graphics")){

				// --- Preset ---
				if(ImGui::CollapsingHeader("Preset", ImGuiTreeNodeFlags_DefaultOpen)){
					// Intentionally shrunk (user request): exactly four offers.
					// Legacy enum values survive for gamePreset round-trip
					// compat; anything that isn't SA PS2/Xbox/PC (incl.
					// PRESET_CUSTOM and old IDs like ultramaxdeluxe) displays
					// as Custom Maxed.
					static const char *presetStr[] = { "PS2", "Xbox", "PC", "Custom Maxed" };
					int presetIdx = 3;
					if(config->preset == PRESET_SA_PS2) presetIdx = 0;
					else if(config->preset == PRESET_SA_XBOX) presetIdx = 1;
					else if(config->preset == PRESET_SA_PC) presetIdx = 2;
					if(ImGui::Combo("Style", &presetIdx, presetStr, 4)){
						if(presetIdx <= 2){
							static const int kStylePreset[3] = {
								PRESET_SA_PS2, PRESET_SA_XBOX, PRESET_SA_PC
							};
							ApplyPreset(config, kStylePreset[presetIdx]);
						}else{
							// Custom Maxed = the user's latest tuned config:
							// load the NEWEST configs\*.ini; none yet => keep
							// current in-memory/INI state (helper no-ops).
							LoadNewestCustomConfig();
							config->preset = PRESET_CUSTOM;
						}
						if(dbglog_throttle("style_combo"))
							dbglog("[Menu] Style -> %s (gamePreset=%d)",
								presetStr[presetIdx], config->preset);
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

				// --- Lighting (moved from Advanced > Experimental) ---
				if(ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen)){
					ImGui::Checkbox("Forward+ Tiled Lighting", RB(config->forwardPlusEnable));
					extern int g_fpGpuLightCount;
					extern int g_fpNumLights;
					ImGui::Text("Active lights: %d (collected %d)", g_fpGpuLightCount, g_fpNumLights);
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

				// --- Tonemapper (moved from Advanced > Vehicle Detail; same
				// top-level section level as the other primary categories) ---
				if(ImGui::CollapsingHeader("Tonemapper")){
					ImGui::Checkbox("Frame-adaptive Tonemap", RB(config->tonemapAutoExposure));
					ImGui::SliderFloat("Tonemap Adapt Speed", &config->tonemapAdaptSpeed, 0.01f, 1.0f);
					ImGui::SliderFloat("Tonemap Key Strength", &config->tonemapKeyStrength, 0.0f, 1.0f);
					// Runtime re-clamp to the SAME bounds readIni/refreshIni/
					// saveConfigTo enforce (match the envFresnel pattern):
					// Min [-0.5..0.5], Max [-0.5..1.0] — negative floors are
					// deliberate darkening — then Min <= Max pinned (min = max)
					// so the shader's HLSL clamp(x, min, max) stays defined.
					if(config->tonemapMinExposure < -0.5f || config->tonemapMinExposure > 0.5f){
						if(dbglog_throttle("tmin_clamp"))
							dbglog("[Menu] tonemapMinExposure=%.3f outside [-0.5..0.5], clamping", config->tonemapMinExposure);
						config->tonemapMinExposure = (config->tonemapMinExposure < -0.5f) ? -0.5f : 0.5f;
					}
					if(config->tonemapMaxExposure < -0.5f || config->tonemapMaxExposure > 1.0f){
						if(dbglog_throttle("tmax_clamp"))
							dbglog("[Menu] tonemapMaxExposure=%.3f outside [-0.5..1.0], clamping", config->tonemapMaxExposure);
						config->tonemapMaxExposure = (config->tonemapMaxExposure < -0.5f) ? -0.5f : 1.0f;
					}
					if(config->tonemapMinExposure > config->tonemapMaxExposure){
						if(dbglog_throttle("texp_order"))
							dbglog("[Menu] tonemapMin>Max (%.3f > %.3f), pinning min = max",
								config->tonemapMinExposure, config->tonemapMaxExposure);
						config->tonemapMinExposure = config->tonemapMaxExposure;
					}
					ImGui::SliderFloat("Tonemap Min Exposure", &config->tonemapMinExposure, -0.5f, 0.5f);
					ImGui::SliderFloat("Tonemap Max Exposure", &config->tonemapMaxExposure, -0.5f, 1.0f);
					// User pivot curve — same throttled menu-side re-clamp
					// pattern (clampTonemapExposureRanges covers load/reload/
					///save; keys tonemapCurveLows/Highs/Mid).
					if(config->tonemapCurveLows < -1.0f || config->tonemapCurveLows > 1.0f){
						if(dbglog_throttle("tclow_clamp"))
							dbglog("[Menu] tonemapCurveLows=%.3f outside [-1..1], clamping", config->tonemapCurveLows);
						config->tonemapCurveLows = (config->tonemapCurveLows < -1.0f) ? -1.0f : 1.0f;
					}
					if(config->tonemapCurveHighs < -1.0f || config->tonemapCurveHighs > 1.0f){
						if(dbglog_throttle("tchig_clamp"))
							dbglog("[Menu] tonemapCurveHighs=%.3f outside [-1..1], clamping", config->tonemapCurveHighs);
						config->tonemapCurveHighs = (config->tonemapCurveHighs < -1.0f) ? -1.0f : 1.0f;
					}
					if(config->tonemapCurveMid < 0.0f || config->tonemapCurveMid > 1.0f){
						if(dbglog_throttle("tcmid_clamp"))
							dbglog("[Menu] tonemapCurveMid=%.3f outside [0..1], clamping", config->tonemapCurveMid);
						config->tonemapCurveMid = (config->tonemapCurveMid < 0.0f) ? 0.0f : 1.0f;
					}
					ImGui::SliderFloat("Curve Intensity Lows", &config->tonemapCurveLows, -1.0f, 1.0f);
					if(ImGui::IsItemHovered()) ImGui::SetTooltip("Shadows (region below Curve Mid Point):\nnegative darkens, positive lifts.");
					ImGui::SliderFloat("Curve Intensity Highs", &config->tonemapCurveHighs, -1.0f, 1.0f);
					if(ImGui::IsItemHovered()) ImGui::SetTooltip("Highlights (region above Curve Mid Point):\nnegative pulls down, positive boosts.");
					ImGui::SliderFloat("Curve Mid Point", &config->tonemapCurveMid, 0.0f, 1.0f);
					if(ImGui::IsItemHovered()) ImGui::SetTooltip("Pivot where the low-curve region ends and\nthe high-curve region begins.");
				}

				// --- Atmosphere ---
				if(ImGui::CollapsingHeader("Atmosphere")){
					ImGui::Checkbox("Height Fog", RB(config->heightFogEnable));
					ImGui::Checkbox("God Rays", RB(config->godRaysEnable));
				}

				// --- GTA IV Mode ---
				if(ImGui::CollapsingHeader("GTA IV Mode")){
					// One checkbox is the whole default surface: Enabling applies the
					// full IV preset (pipes + colour-filter routing) via
					// IVMode_ApplyDefaults, and the IV grade pass in postfx.cpp
					// (DrawIVGrade) uploads ivMode + every slider below as live
					// pixel-shader constants each frame. Grade sliders live behind
					// "Advanced" and only show while IV mode is on. Grade values
					// are NOT reset by the toggle — they are user-tunable INI
					// values (stomping them on toggle-ON caused the grey-wash bug).
					if(ImGui::Checkbox("Enable IV Mode", RB(config->ivMode))){
						if(config->ivMode)
							IVMode_ApplyDefaults(config);
						dbglog("[Menu] IV Mode toggled -> %d", config->ivMode);
					}
					if(config->ivMode && ImGui::TreeNode("Advanced")){
						ImGui::SliderFloat("Desaturation", &config->ivDesaturation, 0.0f, 2.0f);
						if(ImGui::IsItemHovered()) ImGui::SetTooltip("Colour wash toward grey.\n0 = full colour, 1 = heavily desaturated IV-grade look.");
						ImGui::SliderFloat("Gamma", &config->ivGamma, 0.5f, 2.0f);
						if(ImGui::IsItemHovered()) ImGui::SetTooltip("Midtone brightness curve of the IV grade (1.0 = neutral).");
						ImGui::SliderFloat("Exposure", &config->ivExposure, 0.0f, 3.0f);
						if(ImGui::IsItemHovered()) ImGui::SetTooltip("Overall exposure multiplier (1.0 = neutral).");
						ImGui::SliderFloat("Bloom", &config->ivBloomIntensity, 0.0f, 2.0f);
						if(ImGui::IsItemHovered()) ImGui::SetTooltip("Highlight bloom strength around bright pixels.");
						ImGui::SliderFloat("Vignette", &config->ivVignetteIntensity, 0.0f, 2.0f);
						if(ImGui::IsItemHovered()) ImGui::SetTooltip("Darkening strength at the screen corners (0 = off).");
						ImGui::SliderFloat("Vignette Radius", &config->ivVignetteRadius, 0.0f, 2.0f);
						if(ImGui::IsItemHovered()) ImGui::SetTooltip("How far from the centre the vignette starts.");
						ImGui::SliderFloat("Vignette Contrast", &config->ivVignetteContrast, 0.0f, 5.0f);
						if(ImGui::IsItemHovered()) ImGui::SetTooltip("Hardness of the vignette falloff edge.");
						ImGui::TreePop();
					}
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
					// Runtime re-clamp to the SAME fields readIni clamps
					// (envFresnel / vehEnvIntensity, [0..0.4]): a writer the
					// load-time clamp can't see (legacy DebugMenu entry, dead
					// mode-apply code) could otherwise leave the slider
					// DISPLAYING out-of-range (screenshot m0116: 2.700 pinned
					// at the right end of a 0..0.4 slider).
					if(config->envFresnel < 0.0f || config->envFresnel > 0.4f){
						if(dbglog_throttle("envf_clamp"))
							dbglog("[Menu] envFresnel=%.3f outside [0..0.4], clamping", config->envFresnel);
						config->envFresnel = (config->envFresnel < 0.0f) ? 0.0f : 0.4f;
					}
					if(config->vehEnvIntensity < 0.0f || config->vehEnvIntensity > 0.4f){
						if(dbglog_throttle("envi_clamp"))
							dbglog("[Menu] vehEnvIntensity=%.3f outside [0..0.4], clamping", config->vehEnvIntensity);
						config->vehEnvIntensity = (config->vehEnvIntensity < 0.0f) ? 0.0f : 0.4f;
					}
					ImGui::SliderFloat("Env Fresnel", &config->envFresnel, 0.0f, 0.4f);
					ImGui::SliderFloat("Env Intensity", &config->vehEnvIntensity, 0.0f, 0.4f);
					ImGui::SliderFloat("Chrome Env Threshold", &config->vehChromeEnvThreshold, 0.0f, 10.0f);
					// Menu-side re-clamp of the six chrome fields (same pattern as
					// envFresnel/vehEnvIntensity above): a writer the load-time
					// clampChromeParams can't see could otherwise leave the slider
					// DISPLAYING out-of-range. Ranges must match main.cpp.
					struct { float *v; float lo; float hi; } chromeRanges[] = {
						{ &config->chromeF0,        0.0f, 1.0f },
						{ &config->chromeGloss,     0.0f, 1.0f },
						{ &config->chromeMetallic,  0.0f, 1.0f },
						{ &config->chromeEnvBoost,  0.0f, 4.0f },
						{ &config->chromeClearcoat, 0.0f, 1.0f },
					};
					for(auto &cr : chromeRanges){
						if(*cr.v < cr.lo || *cr.v > cr.hi){
							if(dbglog_throttle("chrome_clamp"))
								dbglog("[Menu] chrome param=%.3f outside [%.1f..%.1f], clamping", *cr.v, cr.lo, cr.hi);
							*cr.v = (*cr.v < cr.lo) ? cr.lo : cr.hi;
						}
					}
					if(config->vehChromeEnvThreshold < 0.0f)
						config->vehChromeEnvThreshold = 0.0f;
					ImGui::Checkbox("Auto Chrome (part names)", RB(config->vehAutoChrome));
					ImGui::SliderFloat("Chrome F0", &config->chromeF0, 0.0f, 1.0f);
					ImGui::SliderFloat("Chrome Gloss", &config->chromeGloss, 0.0f, 1.0f);
					ImGui::SliderFloat("Chrome Metallic", &config->chromeMetallic, 0.0f, 1.0f);
					ImGui::SliderFloat("Chrome Env Boost", &config->chromeEnvBoost, 0.0f, 4.0f);
					ImGui::SliderFloat("Chrome Clearcoat", &config->chromeClearcoat, 0.0f, 1.0f);
					ImGui::SliderFloat("IBL Ambient Weight", &config->pbrIblAmbientWeight, 0.0f, 2.0f);
					// Frame-adaptive Tonemap group moved to Graphics > Tonemapper
					// (user request: it was nested under Vehicle Detail).
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
					// Runtime re-clamp to the same [0.5..4.0] bounds enforced
					// at load/reload/save (clampSsaoPower): 9.237 turned mild
					// AO into a hard object-shaped silhouette. Menu-side so
					// the slider can never DISPLAY out-of-range.
					if(config->ssaoPower < 0.5f || config->ssaoPower > 4.0f){
						if(dbglog_throttle("ssaop_clamp"))
							dbglog("[Menu] ssaoPower=%.3f outside [0.5..4.0], clamping", config->ssaoPower);
						config->ssaoPower = (config->ssaoPower < 0.5f) ? 0.5f : 4.0f;
					}
					ImGui::SliderFloat("Power", &config->ssaoPower, 0.5f, 4.0f);
					ImGui::SliderFloat("Kernel Size", &config->ssaoKernelSize, 1.0f, 64.0f);
					if(ImGui::IsItemHovered())
						ImGui::SetTooltip("Not wired to the shader — AO spread is\ncontrolled by Radius. Kept for INI compat.");
					ImGui::SliderInt("Sample Count", &config->ssaoSampleCount, 1, 64);
					if(ImGui::IsItemHovered())
						ImGui::SetTooltip("Fixed at 16 samples in the shader.\nSlider kept for INI compatibility.");
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
						ImGui::SliderFloat("Ambient boost", &config->sssAmbientBoost, 0.0f, 2.0f);
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
					ImGui::SliderFloat("Density", &config->heightFogDensity, 0.0f, 0.01f);
					if(ImGui::IsItemHovered()) ImGui::SetTooltip("Exponential distance density. 0 = default 0.0015; clamped to 0.004 in-game so fog never becomes a wall.");
					ImGui::SliderFloat("Height Falloff", &config->heightFogHeightFalloff, 0.0f, 5.0f);
					if(ImGui::IsItemHovered()) ImGui::SetTooltip("exp(-falloff * height). 0 = default 0.1 in-game — a literal 0 means NO height falloff (fog at every altitude), that was the blue-wash bug.");
					ImGui::SliderFloat("Start Height", &config->heightFogStartHeight, -100.0f, 500.0f);
					ImGui::SliderFloat("Fog R", &config->heightFogR, 0.0f, 1.0f);
					ImGui::SliderFloat("Fog G", &config->heightFogG, 0.0f, 1.0f);
					ImGui::SliderFloat("Fog B", &config->heightFogB, 0.0f, 1.0f);
					if(ImGui::IsItemHovered()) ImGui::SetTooltip("All three at 0 = use the timecycle horizon colour (skyBot). Non-zero = custom fog colour.");
					ImGui::SliderFloat("TC Fog Scale", &config->heightFogTimecycleScale, 0.0f, 5.0f);
					if(ImGui::IsItemHovered()) ImGui::SetTooltip("Adds timecycleScale/fogStart density (capped at 0.001) — atmosphere, not the main density.");
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
					// Vignette Radius/Contrast moved to the "GTA IV Mode" section's
					// Advanced sub-tree (single home for all live grade sliders).
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
					if(ImGui::IsItemHovered()) ImGui::SetTooltip("BRDF material-classified screen-space reflections: geometry pipes pack gloss/spec/metal per pixel, pass 3 blends env reflections weighted by that pack (dull wood/road ~invisible, chrome mirror, car paint a clearcoat hint).");
					ImGui::SliderFloat("PC Intensity", &config->pipeChainIntensity, 0.0f, 1.0f);
					// Forward+ Tiled Lighting moved to Graphics > Lighting
					// (user request: surface it on the main Graphics tab).
				}

				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
			}

			// Pipe-conjunction #4: re-derive every frame the menu is open,
			// AFTER all widgets above have written config — covers the
			// building/vehicle combos (:Rendering header), the Style combo's
			// ApplyPreset, and the IV-mode checkbox's IVMode_ApplyDefaults in
			// one call. (ColourFilter_switch's every-frame derive also covers
			// these next frame; this makes the update same-frame for save/
			// display and matches the spec'd call site.)
			derivePipelineFromPipes(config);

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

			// --- Named configs: save current settings as configs/<name>.ini ---
			ImGui::PushItemWidth(170);
			ImGui::InputText("Config name##cfg", s_cfgNameBuf, sizeof(s_cfgNameBuf));
			ImGui::PopItemWidth();
			ImGui::SameLine();
			if(ImGui::Button("Save as New Config")){
				if(!IsValidCfgName(s_cfgNameBuf)){
					if(dbglog_throttle("cfg_saveas"))
						dbglog("[Menu] Save as New Config: rejected invalid name '%s'", s_cfgNameBuf);
				}else{
					char dir[MAX_PATH], path[MAX_PATH];
					snprintf(dir, MAX_PATH, "%sconfigs", asipath);
					CreateDirectoryA(dir, NULL);	// fine if it already exists
					snprintf(path, MAX_PATH, "%sconfigs\\%s.ini", asipath, s_cfgNameBuf);
					saveConfigTo(path);	// logs path + ok/fail itself (throttled)
					RefreshCfgList();
				}
			}

			// --- Selector: pick a previously saved config and load it ---
			int cfgIdx = -1;
			for(int i = 0; i < (int)s_cfgList.size(); i++)
				if(s_cfgList[i] == s_cfgSel){ cfgIdx = i; break; }
			if(ImGui::BeginCombo("Saved configs##cfg", s_cfgSel.empty() ? "(select...)" : s_cfgSel.c_str())){
				RefreshCfgList();	// rescan on open — picks up files added while the game runs
				cfgIdx = -1;
				for(int i = 0; i < (int)s_cfgList.size(); i++){
					if(s_cfgList[i] == s_cfgSel)
						cfgIdx = i;
					if(ImGui::Selectable(s_cfgList[i].c_str(), i == cfgIdx))
						s_cfgSel = s_cfgList[i];
				}
				if(s_cfgList.empty())
					ImGui::TextDisabled("(no saved configs — use \"Save as New Config\")");
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if(ImGui::Button("Load Config") && !s_cfgSel.empty()){
				char path[MAX_PATH];
				snprintf(path, MAX_PATH, "%sconfigs\\%s.ini", asipath, s_cfgSel.c_str());
				loadConfigFile(path);	// logs path + ok/fail itself (throttled)
			}
		}
		ImGui::End();
		if(!open) config->debugMenuOpen = 0;
	}

	ImGui::Render();
	ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

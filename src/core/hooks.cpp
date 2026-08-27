// hooks.cpp — Game hook installation.
// All InjectHook / Patch / Nop / InterceptCall calls live here.
// Callback functions they install stay in main.cpp.

#include "skygfx.h"
#include "main_exports.h"
#include "hooks.h"
#include "diagnostics.h"

#include "neo.h"
#include "ini_parser.hpp"
#include "ModuleList.hpp"

#ifndef HOOKS_DISABLED

void hooktexdb(void);

void
InstallAllHooks(void)
{

	// moon mask — wraps moon rendering with custom alpha blend states
	InjectHook(0x713C4C, renderMoonMask, PATCH_JUMP);
	dbglog("  HOOK: renderMoonMask -> 0x713C4C (moon alpha blend)");

	// NOTE: InjectDelayedPatches is called directly from DllMain (not via IsAlreadyRunning hook)
	// to avoid clashing with SilentPatch which hooks the same address (0x74872D).

		// afterStreamIni — runs after streaming system initializes
		InjectHook(0x5BCF14, afterStreamIni, PATCH_JUMP);
		dbglog("  HOOK: afterStreamIni -> 0x5BCF14");

		// myDefaultCallback — default atomic render callback (peds, fallback)
		InjectHook(0x7491C0, myDefaultCallback, PATCH_JUMP);
		dbglog("  HOOK: myDefaultCallback -> 0x7491C0 (ped/fallback rendering)");

		// CPlantMgr_Initialise — grass/plant system init
		InjectHook(0x5BF8EA, CPlantMgr_Initialise);
		dbglog("  HOOK: CPlantMgr_Initialise -> 0x5BF8EA");

		// rxD3D9DefaultRenderCallback_Hook — D3D9 render callback hook
		InjectHook(0x756DFE, rxD3D9DefaultRenderCallback_Hook, PATCH_JUMP);
		dbglog("  HOOK: rxD3D9DefaultRenderCallback -> 0x756DFE");

		// fixSeed — fixes random seed for deterministic rendering
		InjectHook(0x5DADB7, fixSeed, PATCH_JUMP);
		dbglog("  HOOK: fixSeed -> 0x5DADB7");

		// saveIntensity — saves light intensity for building pipe
		InjectHook(0x5DAE61, saveIntensity, PATCH_JUMP);
		dbglog("  HOOK: saveIntensity -> 0x5DAE61");

		Patch(0x5DAEC8, setTextureAndColor);
		dbglog("  PATCH: setTextureAndColor -> 0x5DAEC8");

	// Fix matrix multiplication order (W*V*P instead of W*(V*P))
	// Both aap and Junior hook this at 0x7646E0
	extern void _rwD3D9VSGetComposedTransformMatrix(void *transformMatrix);
	InjectHook(0x7646E0, _rwD3D9VSGetComposedTransformMatrix, PATCH_JUMP);

	// normalmap_init() is called in InjectDelayedPatches before hookBuildingPipe()
	// It calls RpNormMapPluginAttach(), sets gHasExternalNormalMapPlugin, and hooks 0x5DA610/0x5D7F40/0x5D5B80

		// 0x5DA610 is CustomPipeAtomicSetup - DO NOT hook it with InjectHook + EAXJMP wrapper
		// that creates an infinite loop (wrapper jumps to hooked address which calls wrapper again)

		// add dual pass for PC pipeline
	InjectHook(0x5D9EEB, D3D9RenderDefault_DUAL);
	InjectHook(0x5D9EFB, D3D9RenderBlack_DUAL);

		// give vehicle pipe to upgrade parts
		InjectHook(0x4C88F0, 0x5DA610, PATCH_JUMP);

		// jump over code that sets alpha ref to 140 (not on PS2).
		// This caused skidmarks to disappear when rendering the neo reflection scene
		InjectHook(0x553AD1, 0x553AE5, PATCH_JUMP);
		// ... was not enough. disable alpha test for skidmarks
		InterceptCall(&CSkidmarks__Render_orig, CSkidmarks__Render, 0x53E175);

		/* Don't change tag material. Instead handle it by special code in the render CB */
		InterceptCall(&CTagManager__RenderTagForPC, CTagManager__RenderTag, 0x534335);
		InterceptCall(&CTagManager__SetupAtomic_orig, CTagManager__SetupAtomic, 0x4C4412);
		*(void**)0xA9AD78 = (void*)TagRenderCB;	/* This is the (unused) material pipeline of player tags */

		// postfx
		InjectHook(0x704D1E, CPostEffects::ColourFilter_switch);
		InjectHook(0x704D5D, CPostEffects::Radiosity);
		InjectHook(0x704FB3, CPostEffects::Radiosity);
		InjectHook(0x704D48, CPostEffects::DarknessFilter_fix);

		// infrared vision
		InjectHook(0x704F4B, CPostEffects::InfraredVision_PS2);
		InjectHook(0x704F59, CPostEffects::Grain_PS2);
		// night vision
		InjectHook(0x704EDA, CPostEffects::NightVision_PS2);
		InjectHook(0x704EE8, CPostEffects::Grain_PS2);
		// rain
		InjectHook(0x705078, CPostEffects::Grain_PS2);
		// unused
		InjectHook(0x705091, CPostEffects::Grain_PS2);

		InjectHook(0x53EBE9, CPostEffects::DrawFinalEffects);

		// fix pointlight fog
		InjectHook(0x700B6B, CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect);

		InjectHook(0x44E82E, ps2rand);
		InjectHook(0x44ECEE, ps2rand);
		InjectHook(0x42453B, ps2rand);
		InjectHook(0x42454D, ps2rand);

		///
		InterceptCall(&PipelinePluginAttach, myPluginAttach, 0x53D903);

		// procobj placement. Not really broken but whatever
		InjectHook(0x5A3C7D, ps2srand);
		InjectHook(0x5A3DFB, ps2srand);
		InjectHook(0x5A3C75, ps2rand);
		InjectHook(0x5A3CB9, ps2rand);
		InjectHook(0x5A3CDB, ps2rand);
		InjectHook(0x5A3CF2, ps2rand);
		Patch(0x5A3CC8, &ps2randnormalize);
		Patch(0x5A3CEA, &ps2randnormalize);
		Patch(0x5A3D05, &ps2randnormalize);
		// a few more procobjs
		InjectHook(0x5A3476, ps2rand);
		InjectHook(0x5A34AB, ps2rand);
		InjectHook(0x5A34E0, ps2rand);
		InjectHook(0x5A3515, ps2rand);
		Patch(0x5A348D + 2, &ps2randnormalize);
		Patch(0x5A34C2 + 2, &ps2randnormalize);
		Patch(0x5A34FB + 2, &ps2randnormalize);
		Patch(0x5A352F + 2, &ps2randnormalize);

		// increase multipass distance
		static float multipassMultiplier = 1000.0f;
		Patch<float*>(0x73290A+2, &multipassMultiplier);

		// Get rid of the annoying dotproduct check in visibility renderCBs
		Nop(0x733313, 2);
		Nop(0x73405A, 2);
		Nop(0x733403, 2);
		Nop(0x73431A, 2);
		Nop(0x73444A, 2);

		// change grass close far to ps2 values
		Patch<float>(0x5DDB3D+1, 78.0f);

		// High detail water color multiplier
		Nop(0x6E716B, 6);
		Nop(0x6E7176, 6);

		// Camera planes in CRenderer::RenderEverythingBarRoads
		static float zoffset = 0.0f;
		Patch(0x553C7D + 2, &zoffset);
		Nop(0x553C78, 5);
		Nop(0x553C9A, 5);	// begin update
		Nop(0x553CD1, 5);
		Nop(0x553CEC, 5);	// begin update

		// Fix mirrors
		Patch(0x726516 + 6, 216.1f);
		Patch(0x726534 + 6, 216.1f);
		Patch(0x726552 + 6, 216.1f);
		Patch(0x726570 + 6, 216.1f);

		hooktexdb();

	dbglog("=== InstallAllHooks complete ===");
}

int
InjectDelayedPatches()
{
	dbglog("InjectDelayedPatches entered");

	dbglog("  findInis...");
	findInis();
	dbglog("  numConfigs=%d", numConfigs);
	if(numConfigs == 0)
		readIni(0);
	else
		readIni(1);
	dbglog("  ini loaded");

	fixingSAMP = ModuleList().Get(L"samp") || ModuleList().Get(L"SAMPGraphicRestore");
	UG_mod = ModuleList().Get(L"Underground_Core");
	if(UG_mod)
		UG_RegisterEventCallback = (void (*)(const char*, bool(*)(void*)))GetProcAddress(UG_mod, "RegisterEventCallback");

	if(UG_RegisterEventCallback){
		dbglog("  UG EVENTS: initposteffects");
		UG_RegisterEventCallback("EVENT_INITPOSTEFFECTS", CPostEffects::Initialise_skygfx);
	}else{
		dbglog("  InterceptCall Initialise at 0x5BD779");
		InterceptCall(&CPostEffects::Initialise_orig, CPostEffects::Initialise, 0x5BD779);
	}
	InterceptCall(&InitialiseGame, InitialiseGame_hook, 0x748CFB);

	// Hook LoadCollisionModelVer2 at entry point to catch collision crashes
	// from ALL paths (streaming system, COLFILE handler, etc.)
	installLCMV2Hooks();

	// Stop timecycle from converting colour filter alphas
	Nop(0x5BBF6F, 2);
	Nop(0x5BBF83, 2);

	// don't assign building pipe just because a model has two sets of prelight
	// or when it already has a pipeline
	explicitBuildingPipe = explicitBuildingPipe_tmp;

	// NOTE: normalmap_init() is NO LONGER called here.
	// It calls RpNormMapPluginAttach() which requires RwEngineInit() to have
	// completed. At this point in boot (IsAlreadyRunning hook), RW is NOT ready.
	// Instead, it's called from InitialiseGame_hook() after CGame::Initialise starts.

	// custom building pipeline
	if(iCanHasbuildingPipe)
		hookBuildingPipe();

	// custom vehicle pipeline
	if(iCanHasvehiclePipe)
		hookVehiclePipe();

	// use static ped shadows
	InjectHook(0x5E675E, &FX::GetFxQuality_ped);
	InjectHook(0x5E676D, &FX::GetFxQuality_ped);
	InjectHook(0x706BC4, &FX::GetFxQuality_ped);
	InjectHook(0x706BD3, &FX::GetFxQuality_ped);
	// stencil???
	InjectHook(0x7113B8, &FX::GetFxQuality_stencil);
	InjectHook(0x711D95, &FX::GetFxQuality_stencil);
	// vehicle, pole
	InjectHook(0x70F9B8, &FX::GetFxQuality_stencil);

	if(fixPcCarLight){
		// carenv light diffuse
		Patch<uint>(0x5D88D1 +6, 0);
		Patch<uint>(0x5D88DB +6, 0);
		Patch<uint>(0x5D88E5 +6, 0);
		// carenv light ambient
		Patch<uint>(0x5D88F9 +6, 0);
		Patch<uint>(0x5D8903 +6, 0);
		Patch<uint>(0x5D890D +6, 0);
	}

	if(disableClouds)
		// jump over cloud loop
		InjectHook(0x714145, 0x71422A, PATCH_JUMP);

	if(disableGamma)
		InjectHook(0x74721C, 0x7472F3, PATCH_JUMP);

	if(iCanHasNeoDrops)
		hookWaterDrops();

	// sun glare on cars
	if(iCanHasSunGlare)
		InjectHook(0x6ABCFD, doglare, PATCH_JUMP);

	// remove black background of lockon siphon
	if(transparentLockon > 0){
		InjectHook(0x742E33, 0x742EC1, PATCH_JUMP);
		InjectHook(0x742FE0, 0x743085, PATCH_JUMP);
	}


	if(fixShadows){
		// Remove 0.06 z-offset from shadows, PS2 doesn't do it
		static float shadowoffset = 0.0f;
		Patch(0x709B2D + 2, &shadowoffset);
		Patch(0x709B8C + 2, &shadowoffset);
		Patch(0x709BC5 + 2, &shadowoffset);
		Patch(0x709BF4 + 2, &shadowoffset);
		Patch(0x709C91 + 2, &shadowoffset);

		Patch(0x709E9C + 2, &shadowoffset);
		Patch(0x709EBA + 2, &shadowoffset);
		Patch(0x709ED5 + 2, &shadowoffset);

		Patch(0x70B21F + 2, &shadowoffset);
		Patch(0x70B371 + 2, &shadowoffset);
		Patch(0x70B4CF + 2, &shadowoffset);
		Patch(0x70B633 + 2, &shadowoffset);

		Patch(0x7085A7 + 2, &shadowoffset);

		// Change z-hack multiplier from 2.0 to 256.0 as on PS2
		*(float*)0x8CD4F0 = 256.0f;
	}

	if(privateHooks){
		// PS2 splash
		static const char *loadsc0 = "loadsc0";
		Patch(0x5901BD + 1, loadsc0);

		// nvidia is not the way it's meant to be played
		Nop(0x748AA8, 0x748AE7-0x748AA8);
	}

	// Water pipe hooks
	InterceptCall(&CWaterLevel__RenderAndEmptyRenderBuffer, CWaterLevel__RenderAndEmptyRenderBuffer_hook, 0x6E8790);
	InterceptCall(&CWaterLevel__RenderAndEmptyRenderBuffer, CWaterLevel__RenderAndEmptyRenderBuffer_hook, 0x6E8EF1);
	InterceptCall(&CWaterLevel__RenderAndEmptyRenderBuffer, CWaterLevel__RenderAndEmptyRenderBuffer_hook, 0x6E91E4);
	InterceptCall(&CWaterLevel__RenderAndEmptyRenderBuffer, CWaterLevel__RenderAndEmptyRenderBuffer_hook, 0x6E9963);

	dbglog("  installMenu...");
	installMenu();
	dbglog("=== InjectDelayedPatches complete ===");

	return FALSE;
}

#else // HOOKS_DISABLED

void InstallAllHooks(void) { }
int  InjectDelayedPatches(void) { return FALSE; }

#endif // HOOKS_DISABLED

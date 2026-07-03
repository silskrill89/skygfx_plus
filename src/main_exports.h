// main_exports.h — Symbols from main.cpp that hooks.cpp needs.
// This file is the bridge between the two translation units.
// Only used during hook setup; can be stripped for release builds.

#ifndef MAIN_EXPORTS_H
#define MAIN_EXPORTS_H

#include "skygfx.h"

// ============================================================
// Config flags (set by INI, read by hooks)
// ============================================================
extern int  numConfigs;
extern int  currentConfig;
extern int  fixingSAMP;
extern bool privateHooks;
extern bool forceWindShader;
extern bool disableClouds;
extern bool disableGamma;
extern bool fixPcCarLight;
extern int  explicitBuildingPipe;
extern bool iCanHasbuildingPipe;
extern bool iCanHasvehiclePipe;
extern bool iCanHasNeoDrops;
extern bool iCanHasSunGlare;
extern int  transparentLockon;
extern int  fixShadows;
extern bool debugMenuOpen;

// ============================================================
// Callback function pointers (set by hooks, used by game)
// ============================================================
extern void (*InitialiseGame)(void);
extern void (*CSkidmarks__Render_orig)(void);
extern void (*CTagManager__RenderTagForPC)(RpAtomic *atomic);
extern void (*CTagManager__SetupAtomic_orig)(RpAtomic *atomic);
extern int  (*PipelinePluginAttach)(void);
extern float ps2randnormalize;
extern void (*CWaterLevel__RenderAndEmptyRenderBuffer)(void);
extern HMODULE UG_mod;
extern void (*UG_RegisterEventCallback)(const char*, bool(*)(void*));

// ============================================================
// Hook callback functions (defined in main.cpp, hooked into game)
// ============================================================

// DllMain path callbacks
void renderMoonMask(void);
void afterStreamIni(void);
RpAtomic* myDefaultCallback(RpAtomic *atomic);
char CPlantMgr_Initialise(void);
void rxD3D9DefaultRenderCallback_Hook(void);
void fixSeed(void);
void saveIntensity(void);
RpMaterial* setTextureAndColor(RpMaterial *material, RwRGBA *color);
void D3D9RenderDefault_DUAL(RxD3D9ResEntryHeader*, RxD3D9InstanceData*, unsigned char, RwTexture*);
void D3D9RenderBlack_DUAL(RxD3D9ResEntryHeader*, RxD3D9InstanceData*);
void CSkidmarks__Render(void);
void CTagManager__RenderTag(RpAtomic *atomic);
void CTagManager__SetupAtomic(RpAtomic *atomic);
void CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect(float, float, float, float, float, unsigned char, unsigned char, unsigned char, short, int, float, unsigned char);
int  ps2rand(void);
void ps2srand(unsigned int seed);
void doglare(void);

// InjectDelayedPatches callbacks
void findInis(void);
void readIni(int n);
void installMenu(void);
void InitialiseGame_hook(void);
void CWaterLevel__RenderAndEmptyRenderBuffer_hook(void);
int  myPluginAttach(void);

// Previously static — now extern for hooks.cpp
extern int explicitBuildingPipe_tmp;
extern int (*IsAlreadyRunning_orig)(void);
void installLCMV2Hooks(void);

// ============================================================
// FX quality (struct with methods, used by InjectDelayedPatches)
// ============================================================
struct FX {
	char data[0x54];
	int fxQuality;
	int GetFxQuality_ped(void);
	int GetFxQuality_stencil(void);
};

#endif // MAIN_EXPORTS_H

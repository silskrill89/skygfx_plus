#pragma once
#define _CRT_SECURE_NO_WARNINGS
#pragma warning(disable: 4244)
#pragma warning(disable: 4800)
#pragma warning(disable: 4838)
#pragma warning(disable: 4996)

#include <windows.h>
#include <rwcore.h>
#include <rwplcore.h>
#include <rpworld.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdint.h>

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;

extern HMODULE dllModule;
void dbglog(const char *fmt, ...);

// skygfx_fix version
#define SKYGFX_FIX_VERSION 1

// Configuration
struct FixConfig {
	// SSS (Subsurface Scattering)
	bool sssEnable;
	float sssStrength;		// 0.0-1.0
	float sssRadius;		// blur radius in pixels
	float sssThreshold;		// depth threshold for edge preservation

	// Skin enhancement
	bool skinEnable;
	float skinSpecularPower;
	float skinSpecularStrength;
	float skinWrapLighting;		// wrap lighting factor for SSS approximation

	// Hair enhancement
	bool hairEnable;
	float hairAnisotropicPower;	// anisotropic highlight power
	float hairAnisotropicStrength;
	float hairSSSStrength;

	// Vegetation enhancement
	bool vegetationEnable;
	float vegetationWindStrength;
	float vegetationSSSStrength;
};

extern FixConfig fixConfig;

// Shader pointers
extern void *SSS_BlurH;
extern void *SSS_BlurV;
extern void *SkinEnhance;
extern void *HairEnhance;
extern void *VegetationEnhance;

// RenderWare hooks
void InstallHooks(void);

// SSS system
void InitSSS(IDirect3DDevice9 *dev);
void RenderSSS(IDirect3DDevice9 *dev);

// Material identification
bool IsSkinMaterial(RpMaterial *material);
bool IsHairMaterial(RpMaterial *material);
bool IsVegetationMaterial(RpMaterial *material);

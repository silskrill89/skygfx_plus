#define _SKIP_RW_FUNCTIONS
#include <Windows.h>
#include <patch\CPatch.h>
#include <stdio.h>
#include <plugin\plugin.h>
#include <game_sa\CTxdStore.h>
#include <list>
#include "FileSearch.h"
#include "CFile.h"
#include <game_sa\CVector.h>
#include <game_sa\RenderWare.h>

#pragma comment(lib, "User32")

using namespace plugin;
using namespace plugin::Core;
using namespace plugin::System;

struct sPlayerTxdFile
{
	char filename[64];
	unsigned int hash;
};

RpLight *gDirectionalLight;
RxPipeline *gNormalMapAtomicPipelines[2] = {NULL, NULL};
unsigned int gNumStoredPipelines = 0;
std::list<sPlayerTxdFile> playerTxdFiles;

// Settings
RwRGBAReal ambientMult;
RwRGBAReal directionalMult;
unsigned int useSunColors;
float sunColorsContribution;
float moonIntensity;

float GetSettingsParam(char *appName, char *keyName, float defaultVal)
{
	float result;
	static char outString[16], defaultValStr[16];
	sprintf_s(defaultValStr, "%f", defaultVal);
	GetPrivateProfileString(appName, keyName, defaultValStr, outString, 16, MAKE_PATH("normalmap_byDK.ini"));
	sscanf(outString, "%f", &result);
	return result;
}

int GetSettingsParam(char *appName, char *keyName, int defaultVal)
{
	return GetPrivateProfileInt(appName, keyName, defaultVal, MAKE_PATH("normalmap_byDK.ini"));
}

void GetSettingsParam(char *appName, char *keyName, char *dst, unsigned int count, char *defaultVal)
{
	GetPrivateProfileString(appName, keyName, defaultVal, dst, count, MAKE_PATH("normalmap_byDK.ini"));
}

void ReadSettings()
{
	ambientMult.red = GetSettingsParam("AMBIENT", "AmbientMultR", 1.0f);
	ambientMult.green = GetSettingsParam("AMBIENT", "AmbientMultG", 1.0f);
	ambientMult.blue = GetSettingsParam("AMBIENT", "AmbientMultB", 1.0f);
	directionalMult.red = GetSettingsParam("DIRECTIONAL", "DirectionalMultR", 0.5f);
	directionalMult.green = GetSettingsParam("DIRECTIONAL", "DirectionalMultG", 0.5f);
	directionalMult.blue = GetSettingsParam("DIRECTIONAL", "DirectionalMultB", 0.5f);
	useSunColors = GetSettingsParam("DIRECTIONAL", "UseSunColors", 1);
	sunColorsContribution = GetSettingsParam("DIRECTIONAL", "SunColorsContribution", 0.3f);
	moonIntensity = GetSettingsParam("DIRECTIONAL", "MoonIntensity", 0.2f);
}

enum RpNormMapAtomicPipeline
{
    rpNANORMMAPATOMICPIPELINE          = 0,
    rpNORMMAPATOMICSTATICPIPELINE      = 1, /**<Static atomics normal map rendering pipeline. */
    rpNORMMAPATOMICSKINNEDPIPELINE     = 2, /**<Skinned atomics normal map rendering pipeline. */
    rpNORMMAPATOMICPIPELINEMAX,
    rpNORMMAPATOMICPIPELINEFORCEENUMSIZEINT = RWFORCEENUMSIZEINT
};

extern "C" bool RpNormMapPluginAttach(void);
extern "C" void RpNormMapAtomicInitialize(RpAtomic *atomic, RpNormMapAtomicPipeline pipeline);
extern "C" bool RpNormMapAtomicIsInitialized(const RpAtomic *atomic);
extern "C" void RpNormMapWorldEnable(RpWorld *world);
extern "C" void RpNormMapWorldSectorInitialize(RpWorldSector *worldsector);
extern "C" bool RpNormMapWorldSectorIsInitialized(const RpWorldSector *worldsector);
extern "C" bool RpNormMapWorldIsEnabled(const RpWorld *world);
extern "C" RxPipeline *RpNormMapGetAtomicPipeline(RpNormMapAtomicPipeline pipeline);
extern "C" RxPipeline *RpNormMapGetWorldSectorPipeline(void);
extern "C" RpMaterial *RpNormMapMaterialSetNormMapTexture(RpMaterial *material, RwTexture *normalmap);
extern "C" RwTexture *RpNormMapMaterialGetNormMapTexture(const RpMaterial *material);
extern "C" RpMaterial *RpNormMapMaterialSetEnvMapTexture(RpMaterial *material, RwTexture *envmap);
extern "C" RwTexture *RpNormMapMaterialGetEnvMapTexture(const RpMaterial *material);
extern "C" RpMaterial *RpNormMapMaterialSetEnvMapCoefficient(RpMaterial *material, float coef);
extern "C" float RpNormMapMaterialGetEnvMapCoefficient(const RpMaterial *material);
extern "C" RpMaterial *RpNormMapMaterialSetEnvMapFrame(RpMaterial *material, RwFrame *frame);
extern "C" RwFrame *RpNormMapMaterialGetEnvMapFrame(RpMaterial *material);
extern "C" RpMaterial *RpNormMapMaterialModulateEnvMap(RpMaterial *material, bool modulate);
extern "C" bool RpNormMapMaterialIsEnvMapModulated(const RpMaterial *material);
extern "C" void RpNormMapSetActiveLights(RpLight *mainLight, RpLight *ambientLight);

// interface for rpnormalmap

extern "C" void RpD3D9GeometrySetUsageFlags(int a1, int a2)
{
	((void (__cdecl *)(int, int))0x7588B0)(a1, a2);
}

extern "C" int RpD3D9GeometryGetUsageFlags(int a1)
{
	return ((int (__cdecl *)(int))0x7588D0)(a1);
}

extern "C" void _rxPipelineDestroy(int a1)
{
	((void (__cdecl *)(int))0x805820)(a1);
}

extern "C" bool RwTextureDestroy(int a1)
{
	return ((bool (__cdecl *)(int))0x7F3820)(a1);
}

extern "C" int RpWorldSectorSetStreamRightsCallBack(int a1, int a2)
{
	return ((int (__cdecl *)(int, int))0x761D10)(a1, a2);
}

extern "C" int RpWorldSectorRegisterPlugin(int a1, int a2, int a3, int a4, int a5)
{
	return ((int (__cdecl *)(int, int, int, int, int))0x761C90)(a1, a2, a3, a4, a5);
}

extern "C" int RpWorldRegisterPluginStream(int a1, int a2, int a3, int a4)
{
	return ((int (__cdecl *)(int, int, int, int))0x74FD00)(a1, a2, a3, a4);
}

extern "C" int RpWorldRegisterPlugin(int a1, int a2, int a3, int a4, int a5)
{
	return ((int (__cdecl *)(int, int, int, int, int))0x74FCD0)(a1, a2, a3, a4, a5);
}

extern "C" int RpAtomicSetStreamRightsCallBack(int a1, void *a2)
{
	return ((int (__cdecl *)(int, void *))0x74BE50)(a1, a2);
}

extern "C" int RpAtomicRegisterPlugin(int a1, int a2, int a3, int a4, int a5)
{
	return ((int (__cdecl *)(int, int, int, int, int))0x74BDA0)(a1, a2, a3, a4, a5);
}

extern "C" int RpMaterialRegisterPluginStream(int a1, int a2, int a3, int a4)
{
	return ((int (__cdecl *)(int, int, int, int))0x74DC20)(a1, a2, a3, a4);
}

extern "C" int RpMaterialRegisterPlugin(int a1, int a2, int a3, int a4, int a5)
{
	return ((int (__cdecl *)(int, int, int, int, int))0x74DBF0)(a1, a2, a3, a4, a5);
}

extern "C" int RwEngineRegisterPlugin(int a1, int a2, int a3, int a4, int a5)
{
	return ((int (__cdecl *)(int, int, int, int, int))0x7F2BB0)(a1, a2, a3, a4, a5);
}

extern "C" int RwStreamWriteReal(int a1, int a2, int a3)
{
	return ((int (__cdecl *)(int, int, int))0x7ED3D0)(a1, a2, a3);
}

extern "C" int RwTextureStreamWrite(int a1, int a2)
{
	return ((int (__cdecl *)(int, int))0x8045E0)(a1, a2);
}

extern "C" int RwStreamWriteInt32(int a1, int a2, int a3)
{
	return ((int (__cdecl *)(int, int, int))0x7ED460)(a1, a2, a3);
}

extern "C" int RwStreamReadReal(int a1, int a2, int a3)
{
	return ((int (__cdecl *)(int, int, int))0x7ED4F0)(a1, a2, a3);
}

void RwError(unsigned int *error)
{
	//sprintf(err, "RwError 0x%X 0x%X", error[0], error[1]);
	//AddMessageJumpQ(err, 1000, 0, 1);
	return;
}

extern "C" int RwErrorSet(int a1)
{
	//RwError((unsigned int *)a1);
	return ((int (__cdecl *)(int))0x808820)(a1);
}

extern "C" int RwTextureStreamRead(int a1)
{
	return ((int (__cdecl *)(int))0x8046E0)(a1);
}

extern "C" int RwErrorGet(int a1)
{
	return ((int (__cdecl *)(int))0x808880)(a1);
}

extern "C" int RwStreamFindChunk(int a1, int a2, int a3, int a4)
{
	return ((int (__cdecl *)(int, int, int, int))0x7ED2D0)(a1, a2, a3, a4);
}

extern "C" int RwStreamReadInt32(int a1, int a2, int a3)
{
	return ((int (__cdecl *)(int, int, int))0x7ED540)(a1, a2, a3);
}

extern "C" int RwTextureStreamGetSize(int a1)
{
	return ((int (__cdecl *)(int))0x8045A0)(a1);
}

extern "C" int RpWorldSectorGetWorld(int a1)
{
	return ((int (__cdecl *)(int))0x74F4E0)(a1);
}

extern "C" int RxPipelineFindNodeByName(int a1, int a2, int a3, int a4)
{
	return ((int (__cdecl *)(int, int, int, int))0x806B30)(a1, a2, a3, a4);
}

extern "C" int RxLockedPipeUnlock(int a1)
{
	return ((int (__cdecl *)(int))0x805F40)(a1);
}

extern "C" int RxLockedPipeAddFragment(int a1, int a2, int a3, int a4)
{
	return ((int (__cdecl *)(int, int, int, int))0x806BE0)(a1, a2, a3, a4);
}

extern "C" int RxNodeDefinitionGetD3D9AtomicAllInOne()
{
	return ((int (__cdecl *)())0x7582E0)();
}

extern "C" int RxPipelineLock(int a1)
{
	return ((int (__cdecl *)(int))0x806990)(a1);
}

extern "C" RxPipeline *RxPipelineCreate()
{
	RxPipeline *pipeline = ((RxPipeline *(__cdecl *)())0x8057B0)();
	if(gNumStoredPipelines < 2)
	{
		gNormalMapAtomicPipelines[gNumStoredPipelines] = pipeline;
		gNumStoredPipelines++;
	}
	return pipeline;
}

extern "C" int RwD3D9SetRenderState(int a1, int a2)
{
	return ((int (__cdecl *)(int, int))0x7FC2D0)(a1, a2);
}

extern "C" int RpAtomicGetWorldBoundingSphere(int a1)
{
	return ((int (__cdecl *)(int))0x749330)(a1);
}

extern "C" int RwFrameGetLTM(int a1)
{
	return ((int (__cdecl *)(int))0x7F0990)(a1);
}

extern "C" int RwD3D9SetTextureStageState(int a1, int a2, int a3)
{
	return ((int (__cdecl *)(int, int, int))0x7FC340)(a1, a2, a3);
}

extern "C" int _rwD3D9TextureHasAlpha(int a1)
{
	return ((int (__cdecl *)(int))0x4C9EA0)(a1);
}

extern "C" int _rwD3D9RenderStateFlushCache()
{
	return ((int (__cdecl *)())0x7FC200)();
}

extern "C" int _rwD3D9VSGetWorldNormalizedViewMultiplyTransposeMatrix(int a1, int a2)
{
	return ((int (__cdecl *)(int, int))0x764B50)(a1, a2);
}

extern "C" int _rwD3D9VSGetWorldNormalizedMultiplyTransposeMatrix(int a1, int a2)
{
	return ((int (__cdecl *)(int, int))0x764A70)(a1, a2);
}

extern "C" int RwMatrixMultiply(int a1, int a2, int a3)
{
	return ((int (__cdecl *)(int, int, int))0x7F18B0)(a1, a2, a3);
}

extern "C" int RwMatrixInvert(int a1, int a2)
{
	return ((int (__cdecl *)(int, int))0x7F2070)(a1, a2);
}

extern "C" int RwMatrixOrthoNormalize(int a1, int a2)
{
	return ((int (__cdecl *)(int, int))0x7F1920)(a1, a2);
}

extern "C" int _rpD3D9GetVertexShader(int a1, int a2)
{
	return ((int (__cdecl *)(int, int))0x75EED0)(a1, a2);
}

extern "C" float RpLightGetConeAngle(int a1)
{
	return ((float (__cdecl *)(int))0x751AE0)(a1);
}

extern "C" int _rwD3D9VSGetPointInLocalSpace(int a1, int a2)
{
	return ((int (__cdecl *)(int, int))0x764E70)(a1, a2);
}

extern "C" void _rwD3D9VSGetRadiusInLocalSpace(int a1, int a2)
{
	((void (__cdecl *)(int, int))0x764F60)(a1, a2);
}

extern "C" float _rwInvSqrt(float a1)
{
	return ((float (__cdecl *)(float))0x7EDB90)(a1);
}

extern "C" int _rwD3D9VSGetNormalInLocalSpace(int a1, int a2)
{
	return ((int (__cdecl *)(int, int))0x764D30)(a1, a2);
}

extern "C" int _rwD3D9VSGetComposedTransformMatrix(int a1)
{
	return ((int (__cdecl *)(int))0x7646E0)(a1);
}

extern "C" int _rwD3D9VSSetActiveWorldMatrix(int a1)
{
	return ((int (__cdecl *)(int))0x764650)(a1);
}

extern "C" int _rxD3D9VertexShaderDefaultBeginCallBack(int a1, int a2, int a3)
{
	return ((int (__cdecl *)(int, int, int))0x760DF0)(a1, a2, a3);
}

extern "C" int RwD3D9SetTexture(int a1, int a2)
{
	return ((int (__cdecl *)(int, int))0x7FDE70)(a1, a2);
}

extern "C" int RwD3D9SetSamplerState(int a1, int a2, int a3)
{
	return ((int (__cdecl *)(int, int, int))0x7FC3C0)(a1, a2, a3);
}

extern "C" int _rwD3D9SetStreams(int a1, int a2)
{
	return ((int (__cdecl *)(int, int))0x7FA090)(a1, a2);
}

extern "C" int RxNodeDefinitionGetD3D9WorldSectorAllInOne()
{
	return ((int (__cdecl *)())0x75E9F0)();
}

extern "C" int RwD3D9CreatePixelShader(int a1, int a2)
{
	return ((int (__cdecl *)(int, int))0x7FACC0)(a1, a2);
}

extern "C" int RwD3D9GetCaps()
{
	return ((int (__cdecl *)())0x7FAD20)();
}

extern "C" int RwD3D9DeletePixelShader(int a1)
{
	return ((int (__cdecl *)(int))0x7FACF0)(a1);
}

extern "C" int RpWorldForAllWorldSectors(int a1, int a2, int a3)
{
	return ((int (__cdecl *)(int, int, int))0x74FC70)(a1, a2, a3);
}

extern "C" int RpD3D9WorldSectorSetUsageFlags(int a1, int a2)
{
	return ((int (__cdecl *)(int, int))0x7588E0)(a1, a2);
}

extern "C" int RpD3D9WorldSectorGetUsageFlags(int a1)
{
	return ((int (__cdecl *)(int))0x758900)(a1);
}

extern "C" int RwV3dTransformVector(int a1, int a2, int a3)
{
	return ((int (__cdecl *)(int, int, int))0x7EDDC0)(a1, a2, a3);
}

extern "C" float RwV3dLength(int a1)
{
	return ((float (__cdecl *)(int))0x7EDAC0)(a1);
}

extern "C" int RwV3dTransformPoint(int a1, int a2, int a3)
{
	return ((int (__cdecl *)(int, int, int))0x7EDD60)(a1, a2, a3);
}

extern "C" int RwEngineInstance;
extern "C" int _RwD3DDevice;
extern "C" int _rwD3D9LastVertexShaderUsed;
extern "C" int _rwD3D9LastPixelShaderUsed;
extern "C" int _rwD3D9LastFVFUsed;
extern "C" int _rwD3D9LastVertexDeclarationUsed;
extern "C" int _rwD3D9LastIndexBufferUsed;

int RwEngineInstance;
int _RwD3DDevice;
int _rwD3D9LastVertexShaderUsed;
int _rwD3D9LastPixelShaderUsed;
int _rwD3D9LastFVFUsed;
int _rwD3D9LastVertexDeclarationUsed;
int _rwD3D9LastIndexBufferUsed;
int setLightCallingState = 0;

int (__cdecl *NormalMapRenderCallBack)(int a1, int a2, int a3, int a4);
int (__cdecl *NormalMapSkinEnumerateLights)(int a1, int a2, int a3, int a4);
int (__cdecl *NormalMapSkinRenderCallBack)(int a1, int a2, int a3, int a4);
int (__cdecl *NormalMapAtomicLightingCallBack)(int a1);
int (__cdecl *NormalMapWorldSectorLightingCallBack)(int a1);

void SetupDirectionalLight();
void UnsetDirectionalLight();
void SetupAmbientLight();
void UnsetAmbientLight();

#define RwFrameGetLTM(frame) ((RwMatrix *(__cdecl *)(RwFrame *))0x7F0990)(frame)
#define RwFrameCreate() ((RwFrame *(__cdecl *)())0x7F0410)()
#define RwFrameDestroy(frame) ((void (__cdecl *)(RwFrame *))0x7F05A0)(frame)
#define RwFrameRotate(frame, axis, angle, combine) ((void (__cdecl *)(RwFrame *, RwV3d *, float, int))0x7F1010)(frame, axis, angle, combine)
#define RwFrameTranslate(frame, pos, combine) ((void (__cdecl *)(RwFrame *, RwV3d *, int))0x7F0E30)(frame, pos, combine)
#define RwFrameUpdateObjects(frame) ((RwFrame *(__cdecl *)(RwFrame *))0x7F0910)(frame)

#define RwObjectHasFrameSetFrame(object, frame) ((void (__cdecl *)(RwObject *, RwFrame *))0x804EF0)(object, frame)

#define RpLightCreate(type) ((RpLight *(__cdecl *)(int))0x752110)(type)
#define RpLightDestroy(light) ((void (__cdecl *)(RpLight *))0x7520D0)(light)
#define RpLightSetColor(light, color) ((void (__cdecl *)(RpLight *, RwRGBAReal *))0x751A90)(light, color)
#define RpLightSetRadius(light, r) ((void (__cdecl *)(RpLight *, float))0x751A70)(light, r)
#define gfNightState (*(float *)0x8D12C0) 
RwRGBAReal gAmbientColor = {0.3f, 0.3f, 0.3f, 0.0f};
RwRGBAReal *gpAmbientColor = (RwRGBAReal *)0xC886D4;
RwRGBAReal gAmbClr;

int MyNormalMapRenderCallBack(int a1, int a2, int a3, int a4)
{
	RwEngineInstance = *(int *)0xC97B24;
	_RwD3DDevice = *(int *)0xC97C28;
	_rwD3D9LastVertexShaderUsed = *(int *)0x8E2448;
	_rwD3D9LastPixelShaderUsed = *(int *)0x8E244C;
	_rwD3D9LastFVFUsed = *(int *)0x8E2440;
	_rwD3D9LastVertexDeclarationUsed = *(int *)0x8E2444;
	_rwD3D9LastIndexBufferUsed = *(int *)0x8E2450;
	if(gDirectionalLight)
	{
		SetupDirectionalLight();
		RpNormMapSetActiveLights(gDirectionalLight, NULL);
	}
	else
		RpNormMapSetActiveLights(NULL, NULL);
	int res = NormalMapRenderCallBack(a1, a2, a3, a4);
	UnsetDirectionalLight();
	*(int *)0x8E2448 = _rwD3D9LastVertexShaderUsed;
	*(int *)0x8E244C = _rwD3D9LastPixelShaderUsed;
	*(int *)0x8E2440 = _rwD3D9LastFVFUsed;
	*(int *)0x8E2444 = _rwD3D9LastVertexDeclarationUsed;
	*(int *)0x8E2450 = _rwD3D9LastIndexBufferUsed;
	return res;
}

int MyNormalMapAtomicLightingCallBack(int a1)
{
	SetupAmbientLight();
	RwEngineInstance = *(int *)0xC97B24;
	return NormalMapAtomicLightingCallBack(a1);
	UnsetAmbientLight();
}

int MyNormalMapWorldSectorLightingCallBack(int a1)
{
	SetupAmbientLight();
	RwEngineInstance = *(int *)0xC97B24;
	return NormalMapWorldSectorLightingCallBack(a1);
	UnsetAmbientLight();
}

int MyNormalMapSkinEnumerateLights(int a1, int a2, int a3, int a4)
{
	SetupAmbientLight();
	RwEngineInstance = *(int *)0xC97B24;
	return NormalMapSkinEnumerateLights(a1, a2, a3, a4);
	UnsetAmbientLight();
}

int MyNormalMapSkinRenderCallBack(int a1, int a2, int a3, int a4)
{
	RwEngineInstance = *(int *)0xC97B24;
	_RwD3DDevice = *(int *)0xC97C28;
	_rwD3D9LastVertexShaderUsed = *(int *)0x8E2448;
	_rwD3D9LastPixelShaderUsed = *(int *)0x8E244C;
	_rwD3D9LastFVFUsed = *(int *)0x8E2440;
	_rwD3D9LastVertexDeclarationUsed = *(int *)0x8E2444;
	_rwD3D9LastIndexBufferUsed = *(int *)0x8E2450;
	if(gDirectionalLight)
	{
		SetupDirectionalLight();
		RpNormMapSetActiveLights(gDirectionalLight, NULL);
	}
	else
		RpNormMapSetActiveLights(NULL, NULL);
	int res = NormalMapSkinRenderCallBack(a1, a2, a3, a4);
	UnsetDirectionalLight();
	*(int *)0x8E2448 = _rwD3D9LastVertexShaderUsed;
	*(int *)0x8E244C = _rwD3D9LastPixelShaderUsed;
	*(int *)0x8E2440 = _rwD3D9LastFVFUsed;
	*(int *)0x8E2444 = _rwD3D9LastVertexDeclarationUsed;
	*(int *)0x8E2450 = _rwD3D9LastIndexBufferUsed;
	return res;
}

extern "C" int RxD3D9AllInOneSetRenderCallBack(int a1, void *a2)
{
	NormalMapRenderCallBack = (int (__cdecl *)(int, int, int, int))a2;
	return ((int (__cdecl *)(int, void *))0x7573E0)(a1, MyNormalMapRenderCallBack);
}

extern "C" int RxD3D9AllInOneSetLightingCallBack(int a1, void *a2)
{
	if(setLightCallingState == 0)
	{
		NormalMapAtomicLightingCallBack = (int (__cdecl *)(int))a2;
		setLightCallingState++;
		if(setLightCallingState > 1)
			setLightCallingState = 0;
		return ((int (__cdecl *)(int, void *))0x7573C0)(a1, MyNormalMapAtomicLightingCallBack);
	}
	else
	{
		NormalMapWorldSectorLightingCallBack = (int (__cdecl *)(int))a2;
		setLightCallingState++;
		if(setLightCallingState > 1)
			setLightCallingState = 0;
		return ((int (__cdecl *)(int, void *))0x7573C0)(a1, MyNormalMapWorldSectorLightingCallBack);
	}
}

extern "C" int _rxD3D9SkinVertexShaderSetMeshRenderCallBack(int a1, int a2)
{
	NormalMapSkinRenderCallBack = (int (__cdecl *)(int, int, int, int))a2;
	*(void **)(*(unsigned int *)(a1 + 20) + 20) = MyNormalMapSkinRenderCallBack;
	return a1;
}

extern "C" int _rxD3D9SkinVertexShaderSetGetMaterialShaderCallBack(int a1, int a2)
{
	*(unsigned int *)(*(unsigned int *)(a1 + 20) + 16) = a2;
	return a1;
}

extern "C" int _rxD3D9SkinVertexShaderSetLightingCallBack(int a1, int a2)
{
	NormalMapSkinEnumerateLights = (int (__cdecl *)(int, int, int, int))a2;
	*(void **)(*(unsigned int *)(a1 + 20) + 12) = MyNormalMapSkinEnumerateLights;
	return a1;
}

extern "C" int RxNodeDefinitionGetD3D9SkinAtomicAllInOne()
{
	return ((int (__cdecl *)())0x7CB2A0)();
}




/*
    Fix tangents generation in RW
*/

void FixRwTangents(RpGeometry *geometry, int type, void *mem, RwV3d *verts, RwTexCoords *texCoords, RxD3D9ResEntryHeader *resEntry, int stride)
{
 ((void (__cdecl *)(int, void *, RwV3d *, RwTexCoords *, RxD3D9ResEntryHeader *, int))0x754E20)
  (type, mem, verts, geometry->texCoords[0], resEntry, stride);
}

void __declspec(naked)FixRwTangentsE()
{
	__asm{
		push esi
		call FixRwTangents
		add esp, 0x1C
		mov eax, 0x7581FE
		jmp eax
	}
}


/*
    Lighting
*/

void CreateLight()
{
	gDirectionalLight = RpLightCreate(rpLIGHTDIRECTIONAL);
    if(gDirectionalLight)
    {
		RwRGBAReal color;
		gDirectionalLight->object.object.flags = 0;
		color.red = 1.0f;
		color.green = 1.0f;
		color.blue = 1.0f;
		RpLightSetColor(gDirectionalLight, &color);
		RpLightSetRadius(gDirectionalLight, 2.0f);
		RwFrame *frame = RwFrameCreate();
		if(frame)
			RwObjectHasFrameSetFrame(&gDirectionalLight->object.object, frame);
		else
		{
			RpLightDestroy(gDirectionalLight);
			gDirectionalLight = NULL;
		}
    }
}

void DestroyLight()
{
    if(gDirectionalLight)
	{
		if(gDirectionalLight->object.object.parent)
			RwFrameDestroy(gDirectionalLight->object.object.parent);
		RpLightDestroy(gDirectionalLight);
		gDirectionalLight = NULL;
	}
}

#define numExtraDirectionalLights (*(int *)0xC88708)
#define extraDirectionalLights ((RpLight **)0xC886F0)
#define VectorToSun ((CVector *)0xB7CA50)
#define CurrentTimecycValue (*(unsigned int *)0xB79FD0)
#define AmbientR ((float)*(unsigned __int16 *)0xB7C4D0 / 255.0f)
#define AmbientG ((float)*(unsigned __int16 *)0xB7C4D2 / 255.0f)
#define AmbientB ((float)*(unsigned __int16 *)0xB7C4D4 / 255.0f)
#define _LIGHTINGVALUE 0.5f

void SetupDirectionalLight()
{
	if(gDirectionalLight)
	{
		gDirectionalLight->object.object.flags = 1;
		if(numExtraDirectionalLights > 0)
		{
			RpLight *pLight = ((RpLight **)0xC886F0)[0];
			float clrmax = 0.0f;
			for(int i = 0; i < numExtraDirectionalLights; i++)
			{
				if(((float *)0xC8867C)[i] > clrmax)
				{
					clrmax = ((float *)0xC8867C)[i];
					pLight = ((RpLight **)0xC886F0)[i];
				}
			}
			RpLightSetColor(gDirectionalLight, &pLight->color);
			memcpy(&gDirectionalLight->object.object.parent->modelling, &pLight->object.object.parent->modelling, sizeof(RwMatrix));
		}
		else
		{
			if(gfNightState <= 0.99f)
			{
				// if we have no directional lights
				float dayState = 1.0f - gfNightState;
				if(useSunColors)
				{
					RwRGBAReal color;
					color.red = dayState * (AmbientR * sunColorsContribution + directionalMult.red * (1.0f - sunColorsContribution));
					color.green = dayState * (AmbientG * sunColorsContribution + directionalMult.green * (1.0f - sunColorsContribution));
					color.blue = dayState * (AmbientB * sunColorsContribution + directionalMult.blue * (1.0f - sunColorsContribution));
					RpLightSetColor(gDirectionalLight, &color);
				}
				else
				{
					RwRGBAReal color = {directionalMult.red * dayState, directionalMult.green * dayState, directionalMult.blue * dayState, 1.0f};
					RpLightSetColor(gDirectionalLight, &color);
				}
				gDirectionalLight->object.object.parent->modelling.at.x = -(VectorToSun[CurrentTimecycValue].x);
				gDirectionalLight->object.object.parent->modelling.at.y = -(VectorToSun[CurrentTimecycValue].y);
				gDirectionalLight->object.object.parent->modelling.at.z = -(VectorToSun[CurrentTimecycValue].z);
			}
			else
			{
				// moon
				float value = directionalMult.red * gfNightState * moonIntensity;
				RwRGBAReal color = {value, value, value, 1.0f};
				RpLightSetColor(gDirectionalLight, &color);
				gDirectionalLight->object.object.parent->modelling.at.x = 0.0f;
				gDirectionalLight->object.object.parent->modelling.at.y = 0.98893635f;
				gDirectionalLight->object.object.parent->modelling.at.z = -0.14834045f;
			}
		}
		RwFrameUpdateObjects(gDirectionalLight->object.object.parent);
	}
}

void UnsetDirectionalLight()
{
	if(gDirectionalLight)
		gDirectionalLight->object.object.flags = 0;
}

void SetupAmbientLight()
{
	gAmbClr = (*(RpLight **)0xC886E8)->color;
	RwRGBAReal clr = {gAmbClr.red * ambientMult.red, gAmbClr.green * ambientMult.green, gAmbClr.blue * ambientMult.blue, 1.0f};
	RpLightSetColor((*(RpLight **)0xC886E8), &clr);
}

void UnsetAmbientLight()
{
	if((*(RpLight **)0xC886E8))
		RpLightSetColor((*(RpLight **)0xC886E8), &gAmbClr);
}


/*
    Player normal maps
*/

struct sMaterialFx
{
	char envMapTexName[32];
	float envMapCoeff;
	unsigned int modulateColor;
};

sMaterialFx fxMaterials[10];
float gEnvRainFactor;
unsigned int gCurrentMaterial;
RwTexDictionary *gPlayerTxd;

#define RpMaterialSetTexture(material, texture) ((void (__cdecl *)(RpMaterial *, RwTexture *))0x74DBC0)(material, texture)
#define RwTexDictionaryFindNamedTexture(dict, name) ((RwTexture *(__cdecl *)(RwTexDictionary *, char *))0x7F39F0)(dict, name)
#define RwTexDictionaryForAllTextures(dict, callback, data) ((RwTexDictionary *(__cdecl *)(RwTexDictionary *, void *, void *))0x7F3730)(dict, callback, data)
#define ConstructTextures(dict, hashes, factorA, factorB, factorC) ((void (__cdecl *)(RwTexDictionary *, unsigned int *, float, float, float))0x5A6040)(dict, hashes, factorA, factorB, factorC)
#define RwTextureSetName(texture, name) ((void (__cdecl *)(RwTexture *, char *))0x7F38A0)(texture, name)
#define RequestTxd(crc) ((int (__cdecl *)(unsigned int))0x5A4220)(crc)
#define LoadRequestedModels() ((void (__cdecl *)())0x40E3A0)()
#define GetUppercaseKey(str) ((unsigned int (__cdecl *)(char *))0x53CF30)(str)
#define GetTextureFromTxdAndLoadNextTxd(composedTorsoTex, currTxdId, next_CRC, pNextTxdId) ((RwTexture *(__cdecl *)(RwTexture *, int, unsigned int, int *))0x5A5F70)\
	(composedTorsoTex, currTxdId, next_CRC, pNextTxdId)
#define LoadAllRequestedModels(flag) ((void (__cdecl *)(bool))0x40EA10)(flag)
#define PlaceTextureOnTopOfTexture(tex1, tex2) ((void (__cdecl *)(RwTexture *, RwTexture *))0x5A57B0)(tex1, tex2)
#define CopyTexture(tex) ((RwTexture *(__cdecl *)(RwTexture *))0x5A5730)(tex)
#define RemoveModel(model) ((void (__cdecl *)(int))0x4089A0)(model)
#define BlendTextures(a1, a2, a3, a4, a5, a6, a7, a8) ((void (__cdecl *)(RwTexture *, RwTexture *, RwTexture *, float, float, float, int, RwTexture *))0x5A5BC0)\
	(a1, a2, a3, a4, a5, a6, a7, a8)
#define _RwTextureDestroy(texture) ((void (__cdecl *)(RwTexture *))0x7F3820)(texture)
#define Blend(a1, a2, a3, a4, a5, a6) ((void (__cdecl *)(RwTexture *, RwTexture *, RwTexture *, float, float, float))0x5A59C0)(a1, a2, a3, a4, a5, a6)
#define RwTexDictionaryAddTexture(dict, texture) ((RwTexture *(__cdecl *)(RwTexDictionary *, RwTexture *))0x7F3980)(dict, texture)
#define Blend2(a1, a2, a3, a4) ((void (__cdecl *)(RwTexture *, RwTexture *, float, float))0x5A5820)(a1, a2, a3, a4)

void PlayerTxdFileCB(char *filename)
{
	sPlayerTxdFile file;
	_splitpath(filename, 0, 0, file.filename, 0);
	file.hash = GetUppercaseKey(file.filename);
	playerTxdFiles.push_back(file);
}

char *GetPlayerTxdName(unsigned int hash)
{
	for(auto i = playerTxdFiles.begin(); i != playerTxdFiles.end(); i++)
	{
		if((*i).hash == hash)
			return (*i).filename;
	}
	return NULL;
}

char *GetPlayerTxdName(char *name)
{
	for(auto i = playerTxdFiles.begin(); i != playerTxdFiles.end(); i++)
	{
		if(!strcmp((*i).filename, name))
			return (*i).filename;
	}
	return NULL;
}

void NormalTextureSetName(RwTexture *texture, char *name)
{
	unsigned int len = strlen(name) + 3;
	char *normName = new char[len];
	memset(normName, 0, len);
	strcpy(normName, name);
	strcat(normName, "_n");
	RwTextureSetName(texture, normName);
	delete[] normName;
}

RwTexture *FindDiffuseTextureCB(RwTexture *texture, RwTexture **result)
{
	if(!strncmp(&texture->name[strlen(texture->name) - 2], "_n", 2))
	{
		return texture;
	}
	*result = texture;
	return NULL;
}

RwTexture *FindNormalTextureCB(RwTexture *texture, RwTexture **result)
{
	if(!strncmp(&texture->name[strlen(texture->name) - 2], "_n", 2))
	{
		*result = texture;
		return 0;
	}
	return texture;
}

RwTexture *GetDiffuseTexture(RwTexDictionary *dict)
{
	RwTexture *diffuseTexture = NULL;
	RwTexDictionaryForAllTextures(dict, FindDiffuseTextureCB, &diffuseTexture);
	return diffuseTexture;
}

RwTexture *GetNormalTexture(RwTexDictionary *dict)
{
	RwTexture *normalTexture = NULL;
	RwTexDictionaryForAllTextures(dict, FindNormalTextureCB, &normalTexture);
	return normalTexture;
}

RwTexture *__cdecl NormalMapCompose(RwTexture *destTexture, CTexDictionary *txd)
{
	RwTexture *texture = GetNormalTexture(txd->m_pRwDictionary);
	if(texture)
	{
		if(destTexture)
			PlaceTextureOnTopOfTexture(destTexture, texture);
		else
			destTexture = CopyTexture(texture);
	}
	return destTexture;
}

CTexDictionary *LoadTxd(unsigned int hash)
{
	char filename[128];
	char *name = GetPlayerTxdName(hash);
	if(name)
	{
		strcpy(filename, MAKE_PATH("normalmap_player\\"));
		strcat(filename, name);
		strcat(filename, ".txd");
		unsigned int txdId = CTxdStore::AddTxdSlot("tmp_player_txd");
		if(CTxdStore::LoadTxd(txdId, filename))
			return &CTxdStore::ms_pTxdPool->m_Objects[txdId];
	}
	return NULL;
}

CTexDictionary *LoadTxd(char *txdname)
{
	char filename[128];
	char *name = GetPlayerTxdName(txdname);
	if(name)
	{
		strcpy(filename, MAKE_PATH("normalmap_player\\"));
		strcat(filename, name);
		strcat(filename, ".txd");
		unsigned int txdId = CTxdStore::AddTxdSlot("tmp_player_txd");
		if(CTxdStore::LoadTxd(txdId, filename))
			return &CTxdStore::ms_pTxdPool->m_Objects[txdId];
	}
	return NULL;
}

void UnLoadTxd(CTexDictionary *txd)
{
	if(txd)
	{
		unsigned int txdId = ((unsigned int)txd - (unsigned int)CTxdStore::ms_pTxdPool->m_Objects) / sizeof(CTexDictionary);
		if(!CTxdStore::ms_pTxdPool->IsFreeSlotAtIndex(txdId))
			CTxdStore::RemoveTxd(txdId);
	}
}

void BuildPlayerNormalMapTextures(RwTexDictionary *dict, unsigned int *hashes, float factorA, float factorB, float factorC)
{
	CTexDictionary *txd = NULL;
	RwTexture *composedTorsoTex = NULL;
	for(int i = 4; i <= 12; i++)
	{
		txd = LoadTxd(hashes[i]);
		if(txd)
		{
			composedTorsoTex = NormalMapCompose(composedTorsoTex, txd);
			UnLoadTxd(txd);
		}
	}
	if(factorB < 0.0)
	{
		float v13 = factorA + factorB;
		factorB = 0.0;
		factorA = v13;
	}
	// Torso
	txd = LoadTxd("player_torso");
	RwTexture *texCombined = NULL;
	if(txd)
	{
		RwTexture *torsoTex = RwTexDictionaryFindNamedTexture(txd->m_pRwDictionary, "torso_n");
		RwTexture *torsoFatTex = RwTexDictionaryFindNamedTexture(txd->m_pRwDictionary, "torso_fat_n");
		RwTexture *torsoRippedTex = RwTexDictionaryFindNamedTexture(txd->m_pRwDictionary, "torso_ripped_n");
		if(torsoTex && torsoFatTex && torsoRippedTex)
		{
			texCombined = CopyTexture(torsoTex);
			if(composedTorsoTex )
			{
				BlendTextures(texCombined, torsoFatTex, torsoRippedTex, factorA, factorB, factorC, 108, composedTorsoTex);
				_RwTextureDestroy(composedTorsoTex);
			}
			else
				Blend(texCombined, torsoFatTex, torsoRippedTex, factorA, factorB, factorC);
		}
		UnLoadTxd(txd);
	}
	txd = LoadTxd(hashes[0]);
	if(txd)
	{
		texCombined = NormalMapCompose(texCombined, txd);
		UnLoadTxd(txd);
	}
	if(texCombined)
	{
		RwTextureSetName(texCombined, "torso_n");
		RwTexDictionaryAddTexture(dict, texCombined);
	}
	// Legs
	txd = LoadTxd("player_legs");
	texCombined = NULL;
	if(txd)
	{
		RwTexture *legsTex = RwTexDictionaryFindNamedTexture(txd->m_pRwDictionary, "legs_n");
		RwTexture *legsFatTex = RwTexDictionaryFindNamedTexture(txd->m_pRwDictionary, "legs_fat_n");
		RwTexture *legsRippedTex = RwTexDictionaryFindNamedTexture(txd->m_pRwDictionary, "legs_ripped_n");
		if(legsTex && legsFatTex && legsRippedTex)
			texCombined = CopyTexture(legsTex);
		UnLoadTxd(txd);
	}
	txd = LoadTxd(hashes[2]);
	if(txd)
	{
		texCombined = NormalMapCompose(texCombined, txd);
		UnLoadTxd(txd);
	}
	if(texCombined)
	{
		RwTextureSetName(texCombined, "legs_n");
		RwTexDictionaryAddTexture(dict, texCombined);
	}
	// Head
	if(hashes[1])
		txd = LoadTxd(hashes[1]);
	else
		txd = LoadTxd("player_face");
	if(txd)
	{
		texCombined = NULL;
		RwTexture *faceTex = RwTexDictionaryFindNamedTexture(txd->m_pRwDictionary, "face_n");
		RwTexture *faceFatTex = RwTexDictionaryFindNamedTexture(txd->m_pRwDictionary, "face_fat_n");
		if(faceTex)
		{
			texCombined = CopyTexture(faceTex);
			if(faceFatTex)
			{
				float v36 = factorA + factorB + factorC;
				Blend2(texCombined, faceFatTex, (factorA + factorC) / v36, factorB / v36);
			}
		}
		else
		{
			RwTexture *anyFaceTex = GetNormalTexture(txd->m_pRwDictionary);
			if(anyFaceTex)
				texCombined = CopyTexture(anyFaceTex);
		}
		UnLoadTxd(txd);
		if(texCombined)
		{
			RwTextureSetName(texCombined, "head_n");
			RwTexDictionaryAddTexture(dict, texCombined);
		}
	}
	// Feet
	if(hashes[3])
		txd = LoadTxd(hashes[3]);
	else
		txd = LoadTxd("player_feet");
	if(txd)
	{
		texCombined = NormalMapCompose(0, txd);
		if(texCombined)
		{
			RwTextureSetName(texCombined, "feet_n");
			RwTexDictionaryAddTexture(dict, texCombined);
		}
		UnLoadTxd(txd);
	}
	// Necklace
	if(hashes[13])
	{
		txd = LoadTxd(hashes[13]);
		if(txd)
		{
			texCombined = NormalMapCompose(0, txd);
			if(texCombined)
			{
				RwTextureSetName(texCombined, "necklace_n");
				RwTexDictionaryAddTexture(dict, texCombined);
			}
			UnLoadTxd(txd);
		}
	}
	// Watch
	if(hashes[14])
	{
		txd = LoadTxd(hashes[14]);
		if(txd)
		{
			texCombined = NormalMapCompose(0, txd);
			if(texCombined)
			{
				RwTextureSetName(texCombined, "watch_n");
				RwTexDictionaryAddTexture(dict, texCombined);
			}
			UnLoadTxd(txd);
		}
	}
	// Glasses
	if(hashes[15])
	{
		txd = LoadTxd(hashes[15]);
		if(txd)
		{
			texCombined = NormalMapCompose(0, txd);
			if(texCombined)
			{
				RwTextureSetName(texCombined, "glasses_n");
				RwTexDictionaryAddTexture(dict, texCombined);
			}
			UnLoadTxd(txd);
		}
	}
	// Hat
	if(hashes[16])
	{
		txd = LoadTxd(hashes[16]);
		if(txd)
		{
			texCombined = NormalMapCompose(0, txd);
			if(texCombined)
			{
				RwTextureSetName(texCombined, "hat_n");
				RwTexDictionaryAddTexture(dict, texCombined);
			}
			UnLoadTxd(txd);
		}
	}
	// Extra
	if(hashes[17])
	{
		txd = LoadTxd(hashes[17]);
		if(txd)
		{
			texCombined = NormalMapCompose(0, txd);
			if(texCombined)
			{
				RwTextureSetName(texCombined, "extra1_n");
				RwTexDictionaryAddTexture(dict, texCombined);
			}
			UnLoadTxd(txd);
		}
	}
}

void BuildPlayerTextures(RwTexDictionary *dict, unsigned int *hashes, float factorA, float factorB, float factorC)
{
	gPlayerTxd = dict;
	ConstructTextures(dict, hashes, factorA, factorB, factorC);
	// normal maps
	BuildPlayerNormalMapTextures(dict, hashes, factorA, factorB, factorC);
	// env maps
}

void __declspec(naked) StoreMaterialId()
{
	__asm{
		mov gCurrentMaterial, esi
		mov ecx, esi
		add ecx, 0x8D0A7C
		mov ecx, [ecx]
		mov edi, 0x5A6B36
		jmp edi
	}
}

void SetupMaterial(RpMaterial *material, RwTexture *texture)
{
	gCurrentMaterial = gCurrentMaterial / 4;
	RwTexture *envTex;
	RpMaterialSetTexture(material, texture);
	if(!texture)
		return;
	unsigned int len = strlen(texture->name) + 6;
	char *normName = new char[len];
	memset(normName, 0, len);
	strcpy(normName, texture->name);
	strcat(normName, "_n");
	//char str[256];
	//sprintf(str, "%d %s", gPlayerTxd, normName);
	//MessageBox(0, str, 0, 0);
	RwTexture *normTex = RwTexDictionaryFindNamedTexture(gPlayerTxd, normName);
	delete[] normName;
	if(normTex)
	{
		RpNormMapMaterialSetNormMapTexture(material, normTex);
		if(fxMaterials[gCurrentMaterial].envMapTexName[0])
		{
			envTex = RwTexDictionaryFindNamedTexture(gPlayerTxd, fxMaterials[gCurrentMaterial].envMapTexName);
			if(envTex)
			{
				RpNormMapMaterialSetEnvMapTexture(material, envTex);
				RpNormMapMaterialSetEnvMapCoefficient(material, fxMaterials[gCurrentMaterial].envMapCoeff);
				if(fxMaterials[gCurrentMaterial].modulateColor)
					RpNormMapMaterialIsEnvMapModulated(material);
			}
		}
	}
}

void SetupAtomic(RpAtomic *atomic, unsigned int skinType)
{
	((void (__cdecl *)(RpAtomic *, unsigned int))0x7C7830)(atomic, skinType);
	RpNormMapAtomicInitialize(atomic, rpNORMMAPATOMICSKINNEDPIPELINE);
}



/*

    Custom pipelines setup

*/

#define RpMatFXAtomicQueryEffects(atomic) ((unsigned int (__cdecl *)(RpAtomic *))0x811C30)(atomic)
#define RpMatFXMaterialGetEffects(material) ((unsigned int (__cdecl *)(RpMaterial *))0x812140)(material)
#define RpGeometryForAllMaterials(geometry, callback, data) ((void (__cdecl *)(RpGeometry *, void *, void *))0x74C790)(geometry, callback, data)

RpMaterial *MaterialHasDefaultMatFXEffect(RpMaterial *material, unsigned int *hasDefaultEffect)
{
	// check if material has material effects
	unsigned int effect = RpMatFXMaterialGetEffects(material);
	if(effect)
	{
		if(effect == 2)
		{
			if(*(unsigned int *)((unsigned int)material + *(unsigned int *)0x8D12C4))
				return material;
		}
		*hasDefaultEffect = 1;
		return 0;
	}
	return material;
}

RpAtomic *CustomPipeAtomicSetup(RpAtomic *atomic)
{
	unsigned int hasDefaultEffect;
	if(atomic->pipeline && atomic->pipeline == gNormalMapAtomicPipelines[0])
		return atomic;
	if(atomic->pipeline && atomic->pipeline == gNormalMapAtomicPipelines[1])
		return atomic;
	if(RpMatFXAtomicQueryEffects(atomic))
	{
		hasDefaultEffect = 0;
		RpGeometryForAllMaterials(atomic->geometry, MaterialHasDefaultMatFXEffect, &hasDefaultEffect);
		if(hasDefaultEffect)
			return atomic;
	}
	RpGeometryForAllMaterials(atomic->geometry, (void *)0x5DA560, 0);
	atomic->pipeline = *(RxPipeline **)0xC02D24;
	((void (__cdecl *)(RpAtomic *, int))0x72FC50)(atomic, 0x53F2009A);
	return atomic;
}

unsigned int IsCBPCPipelineAttached(RpAtomic *atomic)
{
	unsigned int hasDefaultEffect;
	if(gNormalMapAtomicPipelines[0] && atomic->pipeline == gNormalMapAtomicPipelines[0])
		return 0;
	if(gNormalMapAtomicPipelines[1] && atomic->pipeline == gNormalMapAtomicPipelines[1])
		return 0;
	if(RpMatFXAtomicQueryEffects(atomic))
	{
		hasDefaultEffect = 0;
		RpGeometryForAllMaterials(atomic->geometry, MaterialHasDefaultMatFXEffect, &hasDefaultEffect);
		if(hasDefaultEffect)
			return false;
	}
	unsigned int pipelineId = ((unsigned int (__cdecl *)(RpAtomic *))0x72FC40)(atomic);
	RpGeometry *geom = atomic->geometry;
	return pipelineId == 0x53F2009C || pipelineId == 0x53F20098 || (((unsigned int (__cdecl *)(RpGeometry *))0x5D6E90)(geom) && geom->preLitLum);
}

bool IsCCPCPipelineAttached(RpAtomic *atomic)
{
	unsigned int hasDefaultEffect;
	if(gNormalMapAtomicPipelines[0] && atomic->pipeline == gNormalMapAtomicPipelines[0])
		return false;
	if(gNormalMapAtomicPipelines[1] && atomic->pipeline == gNormalMapAtomicPipelines[1])
		return false;
	if(RpMatFXAtomicQueryEffects(atomic))
	{
		hasDefaultEffect = 0;
		RpGeometryForAllMaterials(atomic->geometry, MaterialHasDefaultMatFXEffect, &hasDefaultEffect);
		if(hasDefaultEffect)
			return false;
	}
	return ((unsigned int (__cdecl *)(RpAtomic *))0x72FC40)(atomic) == 0x53F2009A;
}



/*

    Read reflection map settings for player

*/
void ReadTexturesDat()
{
	char line[512];
	FILE *f = fopen(MAKE_PATH("normalmap_player\\normalmap_env_settings.dat"), "rt");
	if(!f)
		return;
	//do fgets(line, 256, f);
	//while(*line == '#' || *line == '\0' || *line == '\n');
	//sscanf(line, "%*s %f", &gEnvRainFactor);
	for(int i = 0; i < 10; i++)
	{
		do fgets(line, 256, f);
		while(*line == '#' || *line == '\0' || *line == '\n');
		sscanf(line, "%*d %s %f %d", fxMaterials[i].envMapTexName, fxMaterials[i].envMapCoeff, fxMaterials[i].modulateColor);
		if(fxMaterials[i].envMapTexName[0] == '-')
			fxMaterials[i].envMapTexName[0] = '\0';
	}
	fclose(f);
}



BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if(reason == DLL_PROCESS_ATTACH)
	{
		RegisterPlugin("Normal Mapping", "DK22Pac", "normalmap_byDK.asi", "1.0", 0x100, GAME_SA_1_0_US, 0);
		CFile::Init("normalmap_byDK.asi");

		RegisterFunc(FUNC_REGISTER_RW_PLUGIN, (tRegisteredFunction)RpNormMapPluginAttach);
		RegisterFunc(FUNC_INITIALISE_RW, CreateLight); // Create directional light
		RegisterFunc(FUNC_SHUTDOWN_RW, DestroyLight); // Destroy directional light

		/* check vehicles/buildings for normal mapping */
		CPatch::RedirectJump(0x5DA610, CustomPipeAtomicSetup); // car
		CPatch::RedirectJump(0x5D7F40, IsCBPCPipelineAttached);
		CPatch::RedirectJump(0x5D5B80, IsCCPCPipelineAttached);

		/*tangents*/
		// CPatch::RedirectJump(0x7581F6, FixRwTangentsE);

		/* player normal maps */
		ReadTexturesDat();
		SearchFiles(MAKE_PATH("normalmap_player\\*.txd"), (LPSEARCHFUNC)PlayerTxdFileCB, 0);
		if(playerTxdFiles.size() > 0)
		{
			CPatch::RedirectCall(0x5A6B24, BuildPlayerTextures);
			CPatch::RedirectCall(0x5A6B77, SetupMaterial);
			CPatch::RedirectJump(0x5A6B30, StoreMaterialId);
			CPatch::RedirectCall(0x5A6D30, SetupAtomic);
		}

		ReadSettings();
		if(GetSettingsParam("GENERAL", "UpdateSettingsEachFrame", 0))
			RegisterFunc(FUNC_GAME_PROCESS_AFTER_SCRIPTS, ReadSettings);
	}
    return TRUE;
}
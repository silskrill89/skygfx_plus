// Normal map integration for skygfx
// Based on DK22Pac's Normal Map Plugin for GTA SA
// Recreates the plugin's functionality built into skygfx

#include "skygfx.h"
#include "MemoryMgr.h"
#include <Windows.h>
#include <cstdio>
#include <rwcore.h>
#include <rpworld.h>
#include <rpnormmap.h>

// Game function wrappers (by address, since we don't link game symbols)
static RpGeometry *(__cdecl *game_RpAtomicGetGeometry)(void *atomic) = (RpGeometry *(__cdecl*)(void *))0x749AB0;
static int(__cdecl *game_GetPipelineID)(void *atomic) = (int(__cdecl*)(void *))0x72FC40;
static int(__cdecl *game_RpMatFXAtomicQueryEffects)(void *atomic) = (int(__cdecl*)(void *))0x811C30;
static int(__cdecl *game_RpMatFXMaterialGetEffects)(void *material) = (int(__cdecl*)(void *))0x812140;
static void(__cdecl *game_RpGeometryForAllMaterials)(void *geom, void *cb, void *data) = (void(__cdecl*)(void *, void *, void *))0x74C790;

// Pipeline IDs
#define RSPIPE_PC_CustomBuilding_PipeID    0x53F20098
#define RSPIPE_PC_CustomBuildingDN_PipeID  0x53F2009C

// Game globals
#define gfNightState (*(float *)0x8D12C0)
#define numExtraDirectionalLights (*(int *)0xC88708)
#define extraDirectionalLights ((RpLight **)0xC886F0)
#define extraDirectionalLightIntensities ((float *)0xC8867C)
#define VectorToSun ((RwV3d *)0xB7CA50)
#define CurrentTimecycValue (*(unsigned int *)0xB79FD0)
#define AmbientR ((float)*(unsigned __int16 *)0xB7C4D0 / 255.0f)
#define AmbientG ((float)*(unsigned __int16 *)0xB7C4D2 / 255.0f)
#define AmbientB ((float)*(unsigned __int16 *)0xB7C4D4 / 255.0f)

// Normal map globals
RxPipeline *gNormalMapAtomicPipelines[2] = {NULL, NULL};
static RpLight *gDirectionalLight = NULL;
static RwRGBAReal gAmbClr;

// Settings
static RwRGBAReal ambientMult = {1.0f, 1.0f, 1.0f, 0.0f};
static RwRGBAReal directionalMult = {0.5f, 0.5f, 0.5f, 0.0f};
static unsigned int useSunColors = 1;
static float sunColorsContribution = 0.3f;
static float moonIntensity = 0.2f;

// Game function wrappers for light/frame
#define rpLIGHTDIRECTIONAL 1
static RpLight *(__cdecl *game_RpLightCreate)(int type) = (RpLight *(__cdecl*)(int))0x752110;
static void(__cdecl *game_RpLightDestroy)(RpLight *light) = (void(__cdecl*)(RpLight *))0x7520D0;
static void(__cdecl *game_RpLightSetColor)(void *light, RwRGBAReal *color) = (void(__cdecl*)(void *, RwRGBAReal *))0x751A90;
static void(__cdecl *game_RpLightSetRadius)(void *light, float r) = (void(__cdecl*)(void *, float))0x751A70;
static RwFrame *(__cdecl *game_RwFrameCreate)() = (RwFrame *(__cdecl*)())0x7F0410;
static void(__cdecl *game_RwFrameDestroy)(void *frame) = (void(__cdecl*)(void *))0x7F05A0;
static void(__cdecl *game_RwObjectHasFrameSetFrame)(void *obj, void *frame) = (void(__cdecl*)(void *, void *))0x804EF0;
static void(__cdecl *game_RwFrameUpdateObjects)(void *frame) = (void(__cdecl*)(void *))0x7F0910;

static RpLight *GetAmbientLight() { return *(RpLight **)0xC886E8; }

// Helper: get light flags (RwObject.flags at offset 2)
static unsigned char &LightFlags(RpLight *light) { return ((unsigned char *)light)[2]; }
// Helper: get light parent frame (RwObject.parent at offset 4)
static void *&LightParent(RpLight *light) { return *(void **)((unsigned char *)light + 4); }

// ===== CustomPipeAtomicSetup (replaces 0x5DA610) =====

static void MaterialHasDefaultMatFXEffect_cb(void *material, int *hasDefaultEffect)
{
    int effect = game_RpMatFXMaterialGetEffects(material);
    if(effect)
    {
        if(effect == 2)
        {
            int matfx_offset = *(int *)0x8D12C4;
            if(*(int *)((int)material + matfx_offset))
            {
                *hasDefaultEffect = 1;
                return;
            }
        }
        *hasDefaultEffect = 1;
    }
}

static RpAtomic *__cdecl CustomPipeAtomicSetup_hook(RpAtomic *atomic)
{
    if(gNormalMapAtomicPipelines[0] && atomic->pipeline == gNormalMapAtomicPipelines[0])
        return atomic;
    if(gNormalMapAtomicPipelines[1] && atomic->pipeline == gNormalMapAtomicPipelines[1])
        return atomic;

    if(game_RpMatFXAtomicQueryEffects(atomic))
    {
        int hasDefaultEffect = 0;
        game_RpGeometryForAllMaterials(atomic->geometry, (void *)MaterialHasDefaultMatFXEffect_cb, &hasDefaultEffect);
        if(hasDefaultEffect)
            return atomic;
    }

    game_RpGeometryForAllMaterials(atomic->geometry, (void *)0x5DA560, 0);
    atomic->pipeline = *(RxPipeline **)0xC02D24;
    ((void(__cdecl *)(void *, int))0x72FC50)(atomic, 0x53F2009A);
    return atomic;
}

// ===== IsCBPCPipelineAttached (replaces 0x5D7F40) =====

extern int explicitBuildingPipe;

static int __cdecl IsCBPCPipelineAttached_hook(RpAtomic *atomic)
{
    if(gNormalMapAtomicPipelines[0] && atomic->pipeline == gNormalMapAtomicPipelines[0])
        return 0;
    if(gNormalMapAtomicPipelines[1] && atomic->pipeline == gNormalMapAtomicPipelines[1])
        return 0;

    if(game_RpMatFXAtomicQueryEffects(atomic))
    {
        int hasDefaultEffect = 0;
        game_RpGeometryForAllMaterials(atomic->geometry, (void *)MaterialHasDefaultMatFXEffect_cb, &hasDefaultEffect);
        if(hasDefaultEffect)
            return 0;
    }

    int pipelineId = game_GetPipelineID(atomic);
    void *geom = game_RpAtomicGetGeometry(atomic);
    return pipelineId == RSPIPE_PC_CustomBuildingDN_PipeID || pipelineId == RSPIPE_PC_CustomBuilding_PipeID || (((int(__cdecl *)(void *))0x5D6E90)(geom) && ((RpGeometry *)geom)->preLitLum);
}

// ===== IsCCPCPipelineAttached (replaces 0x5D5B80) =====

static int __cdecl IsCCPCPipelineAttached_hook(RpAtomic *atomic)
{
    if(gNormalMapAtomicPipelines[0] && atomic->pipeline == gNormalMapAtomicPipelines[0])
        return 0;
    if(gNormalMapAtomicPipelines[1] && atomic->pipeline == gNormalMapAtomicPipelines[1])
        return 0;

    if(game_RpMatFXAtomicQueryEffects(atomic))
    {
        int hasDefaultEffect = 0;
        game_RpGeometryForAllMaterials(atomic->geometry, (void *)MaterialHasDefaultMatFXEffect_cb, &hasDefaultEffect);
        if(hasDefaultEffect)
            return 0;
    }

    return game_GetPipelineID(atomic) == 0x53F2009A;
}

// ===== Lighting =====

static void CreateLight()
{
    gDirectionalLight = game_RpLightCreate(rpLIGHTDIRECTIONAL);
    if(gDirectionalLight)
    {
        RwRGBAReal color = {1.0f, 1.0f, 1.0f, 0.0f};
        LightFlags(gDirectionalLight) = 0;
        game_RpLightSetColor(gDirectionalLight, &color);
        game_RpLightSetRadius(gDirectionalLight, 2.0f);
        RwFrame *frame = game_RwFrameCreate();
        if(frame)
            game_RwObjectHasFrameSetFrame((void *)((unsigned char *)gDirectionalLight + 4), frame);
        else
        {
            game_RpLightDestroy(gDirectionalLight);
            gDirectionalLight = NULL;
        }
    }
}

static void DestroyLight()
{
    if(gDirectionalLight)
    {
        void *&parent = LightParent(gDirectionalLight);
        if(parent)
            game_RwFrameDestroy(parent);
        game_RpLightDestroy(gDirectionalLight);
        gDirectionalLight = NULL;
    }
}

static void SetupDirectionalLight()
{
    if(!gDirectionalLight)
        return;

    LightFlags(gDirectionalLight) = 1;

    if(numExtraDirectionalLights > 0)
    {
        float bestIntensity = 0.0f;
        RpLight *best = extraDirectionalLights[0];
        for(int i = 0; i < numExtraDirectionalLights; i++)
        {
            if(extraDirectionalLightIntensities[i] > bestIntensity)
            {
                bestIntensity = extraDirectionalLightIntensities[i];
                best = extraDirectionalLights[i];
            }
        }
        game_RpLightSetColor(gDirectionalLight, &best->color);
        void *ourFrame = LightParent(gDirectionalLight);
        void *theirFrame = LightParent(best);
        if(ourFrame && theirFrame)
            memcpy((unsigned char *)ourFrame + 0x8, (unsigned char *)theirFrame + 0x8, sizeof(RwMatrix));
    }
    else
    {
        if(gfNightState <= 0.99f)
        {
            float dayState = 1.0f - gfNightState;
            if(useSunColors)
            {
                RwRGBAReal color;
                color.red = dayState * (AmbientR * sunColorsContribution + directionalMult.red * (1.0f - sunColorsContribution));
                color.green = dayState * (AmbientG * sunColorsContribution + directionalMult.green * (1.0f - sunColorsContribution));
                color.blue = dayState * (AmbientB * sunColorsContribution + directionalMult.blue * (1.0f - sunColorsContribution));
                game_RpLightSetColor(gDirectionalLight, &color);
            }
            else
            {
                RwRGBAReal color = {directionalMult.red * dayState, directionalMult.green * dayState, directionalMult.blue * dayState, 1.0f};
                game_RpLightSetColor(gDirectionalLight, &color);
            }
            void *ourFrame = LightParent(gDirectionalLight);
            if(ourFrame)
            {
                RwMatrix *ltm = (RwMatrix *)((unsigned char *)ourFrame + 0x8);
                ltm->at.x = -VectorToSun[CurrentTimecycValue].x;
                ltm->at.y = -VectorToSun[CurrentTimecycValue].y;
                ltm->at.z = -VectorToSun[CurrentTimecycValue].z;
            }
        }
        else
        {
            float value = directionalMult.red * gfNightState * moonIntensity;
            RwRGBAReal color = {value, value, value, 1.0f};
            game_RpLightSetColor(gDirectionalLight, &color);
            void *ourFrame = LightParent(gDirectionalLight);
            if(ourFrame)
            {
                RwMatrix *ltm = (RwMatrix *)((unsigned char *)ourFrame + 0x8);
                ltm->at.x = 0.0f;
                ltm->at.y = 0.98893635f;
                ltm->at.z = -0.14834045f;
            }
        }
    }
    void *ourFrame = LightParent(gDirectionalLight);
    if(ourFrame)
        game_RwFrameUpdateObjects(ourFrame);
}

static void UnsetDirectionalLight()
{
    if(gDirectionalLight)
        LightFlags(gDirectionalLight) = 0;
}

static void SetupAmbientLight()
{
    RpLight *ambient = GetAmbientLight();
    if(!ambient)
        return;
    gAmbClr = ambient->color;
    RwRGBAReal clr = {gAmbClr.red * ambientMult.red, gAmbClr.green * ambientMult.green, gAmbClr.blue * ambientMult.blue, 1.0f};
    game_RpLightSetColor(ambient, &clr);
}

static void UnsetAmbientLight()
{
    RpLight *ambient = GetAmbientLight();
    if(ambient)
        game_RpLightSetColor(ambient, &gAmbClr);
}

// ===== Init / Shutdown =====

static bool normalmapInitialized = false;

void normalmap_init()
{
    if(normalmapInitialized)
        return;

    dbglog("normalmap: attaching RpNormMapPlugin...");
    if(!RpNormMapPluginAttach())
    {
        dbglog("normalmap: RpNormMapPluginAttach FAILED");
        return;
    }
    dbglog("normalmap: RpNormMapPluginAttach OK");

    gHasExternalNormalMapPlugin = true;

    CreateLight();
    dbglog("normalmap: light created");

    // Capture pipelines created by rpnormmap.lib
    gNormalMapAtomicPipelines[0] = RpNormMapGetAtomicPipeline(rpNORMMAPATOMICSTATICPIPELINE);
    gNormalMapAtomicPipelines[1] = RpNormMapGetAtomicPipeline(rpNORMMAPATOMICSKINNEDPIPELINE);
    dbglog("normalmap: pipelines %p %p", gNormalMapAtomicPipelines[0], gNormalMapAtomicPipelines[1]);

    // Hook pipe setup functions
    InjectHook(0x5DA610, CustomPipeAtomicSetup_hook, PATCH_JUMP);
    InjectHook(0x5D7F40, IsCBPCPipelineAttached_hook, PATCH_JUMP);
    InjectHook(0x5D5B80, IsCCPCPipelineAttached_hook, PATCH_JUMP);
    dbglog("normalmap: hooks installed");

    normalmapInitialized = true;
    dbglog("normalmap: initialization complete");
}

void normalmap_shutdown()
{
    if(!normalmapInitialized)
        return;

    DestroyLight();
    normalmapInitialized = false;
}

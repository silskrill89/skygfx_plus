// Normal map integration for skygfx
// Based on DK22Pac's Normal Map Plugin for GTA SA
// Uses RW SDK 3.7 RpNormMap plugin API for clean integration

#include "skygfx.h"
#include "MemoryMgr.h"
#include <Windows.h>
#include <cstdio>
#include <rwcore.h>
#include <rpworld.h>
#include <rpnormmap.h>

// Normal map globals
RxPipeline *gNormalMapAtomicPipelines[2] = {NULL, NULL};

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

    // Capture pipelines created by rpnormmap.lib
    gNormalMapAtomicPipelines[0] = RpNormMapGetAtomicPipeline(rpNORMMAPATOMICSTATICPIPELINE);
    gNormalMapAtomicPipelines[1] = RpNormMapGetAtomicPipeline(rpNORMMAPATOMICSKINNEDPIPELINE);
    dbglog("normalmap: pipelines %p %p", gNormalMapAtomicPipelines[0], gNormalMapAtomicPipelines[1]);

    normalmapInitialized = true;
    dbglog("normalmap: initialization complete");
}

void normalmap_shutdown()
{
    if(!normalmapInitialized)
        return;

    normalmapInitialized = false;
}

# Hook Architecture

> Complete reference for all skygfx_plus hooks: installation order, addresses, targets, and conflicts.
> Connects to RW SDK architecture and explains why each hook exists.
>
> **Note**: GTA SA uses RW 3.6.0.3. skygfx_plus uses RW 3.7 SDK headers (`E:\SDKs\rwsdk`) and backports 3.7 features.

## Hook Installation Timeline

```
DLL_PROCESS_ATTACH (DllMain)
│
├─ Phase 1: Immediate hooks (DllMain body)
│   ├─ diag_init(), diag_installVEH(), diag_startWatchdog()
│   ├─ 0x7646E0: _rwD3D9VSGetComposedTransformMatrix (fix mul order)
│   ├─ 0x713C4C: renderMoonMask
│   ├─ InjectDelayedPatches() ← called directly, NOT deferred
│   ├─ 0x5BCF14: afterStreamIni (re-read INIs after stream.ini)
│   ├─ 0x7491C0: myDefaultCallback (dual-pass rendering)
│   ├─ 0x5BF8EA: CPlantMgr_Initialise (grass PS + render CB)
│   ├─ 0x756DFE: rxD3D9DefaultRenderCallback_Hook (pixel shader switching)
│   ├─ 0x5DADB7: fixSeed (grass placement)
│   ├─ 0x5DAE61: saveIntensity
│   ├─ 0x5DAEC8: setTextureAndColor
│   ├─ 0x5D9EEB: D3D9RenderDefault_DUAL
│   ├─ 0x5D9EFB: D3D9RenderBlack_DUAL
│   ├─ 0x4C88F0 → 0x5DA610 (vehicle pipe to upgrade parts)
│   ├─ 0x553AD1: skip alpha ref=140 patch
│   ├─ 0x53E175: CSkidmarks__Render (alpha test fix)
│   ├─ 0x534335: CTagManager__RenderTag
│   ├─ 0x4C4412: CTagManager__SetupAtomic (force building pipe)
│   ├─ 0xA9AD78: TagRenderCB pointer
│   ├─ PostFX hooks (0x704xxx series)
│   ├─ 0x700B6B: point light fog fix
│   ├─ ps2rand hooks (0x44Exxx, 0x424xxx)
│   ├─ 0x53D903: myPluginAttach (intercept PipelinePluginAttach)
│   ├─ procobj hooks (0x5A3xxx)
│   └─ hooktexdb()
│
├─ Phase 2: InjectDelayedPatches (called from DllMain)
│   ├─ 0x748CFB: InitialiseGame_hook (DEFERRED — runs at game init)
│   ├─ hookBuildingPipe()
│   │   ├─ 0x53C15E: CustomBuildingPipeline__Update
│   │   ├─ 0x5D7100: CreateCustomObjPipe_PS2 (DN)
│   │   ├─ 0x5D7D90: CreateCustomObjPipe_PS2 (standard)
│   │   ├─ 0x5D7200: Patched to RET (disable interpolation)
│   │   └─ 0x5D7F40: IsCBPCPipelineAttached ← CONFLICT with normalmap_init
│   ├─ hookVehiclePipe() (conditional)
│   ├─ FX quality hooks
│   └─ installMenu()
│
└─ Phase 3: InitialiseGame_hook (runs at CGame::Initialise)
    ├─ normalmap_init() ← CALL #1 ONLY
    │   ├─ RpNormMapPluginAttach()
    │   ├─ 0x5DA610: CustomPipeAtomicSetup_hook
    │   ├─ 0x5D7F40: IsCBPCPipelineAttached_hook ← OVERWRITES Phase 2 hook
    │   └─ 0x5D5B80: IsCCPCPipelineAttached_hook
    ├─ RenderScene_hook
    ├─ envmaphooks()
    ├─ neoInit()
    ├─ initTexDB()
    └─ Original InitialiseGame()
```

## Hook Conflict: 0x5D7F40

### Two Hooks, Second Wins

**Phase 2** (DllMain → hookBuildingPipe): Installs `CCustomBuildingRenderer__IsCBPCPipelineAttached`
- Guard: `if(explicitBuildingPipe >= 0 && !gHasExternalNormalMapPlugin)`
- At DllMain time, `gHasExternalNormalMapPlugin` is false → hook IS installed

**Phase 3** (InitialiseGame_hook → normalmap_init): Installs `IsCBPCPipelineAttached_hook`
- Unconditionally hooks 0x5D7F40
- **Overwrites** the Phase 2 hook

**Result**: `CCustomBuildingRenderer__IsCBPCPipelineAttached` in buildingPipe.cpp is dead code when normalmap is active.

### Original skygfx Behavior

Original skygfx (`E:\SDKs\skygfx_original`):
- `hookBuildingPipe()` hooks 0x5D7F40 WITHOUT `!gHasExternalNormalMapPlugin` guard
- No normalmap code exists — only buildingPipe hook runs
- No conflict

## Key Hook Functions

### IsCBPCPipelineAttached (buildingPipe.cpp:1073-1090)

Determines if an atomic should use the custom building pipeline:

```cpp
RwBool CCustomBuildingRenderer__IsCBPCPipelineAttached(RpAtomic *atomic) {
    // NULL frame check (defensive)
    if(!RpAtomicGetFrame(atomic))
        return FALSE;
    
    uint32 pipeID = GetPipelineID(atomic);
    RpGeometry *geo = RpAtomicGetGeometry(atomic);
    RxPipeline *pipe;
    RpAtomicGetPipeline(atomic, &pipe);
    
    if(pipeID == RSPIPE_PC_CustomBuilding_PipeID || 
       pipeID == RSPIPE_PC_CustomBuildingDN_PipeID)
        return TRUE;
    
    if(explicitBuildingPipe > 0)
        return FALSE;
    
    return pipe == nil && GetExtraVertColourPtr(geo) && 
           RpGeometryGetPreLightColors(geo);
}
```

**Logic**: Returns TRUE if atomic has no pipeline + has extra vertex colors + has prelight data.

### IsCBPCPipelineAttached_hook (normalmap.cpp:109-127)

Normalmap version — adds MatFX/pipeline checks:

```cpp
static int __cdecl IsCBPCPipelineAttached_hook(RpAtomic *atomic) {
    // Skip if already has normal map pipeline
    if(gNormalMapAtomicPipelines[0] && 
       atomic->pipeline == gNormalMapAtomicPipelines[0])
        return 0;
    if(gNormalMapAtomicPipelines[1] && 
       atomic->pipeline == gNormalMapAtomicPipelines[1])
        return 0;
    
    // Skip if has default MatFX effect
    if(game_RpMatFXAtomicQueryEffects(atomic)) {
        int hasDefaultEffect = 0;
        game_RpGeometryForAllMaterials(atomic->geometry, 
            (void *)MaterialHasDefaultMatFXEffect_cb, &hasDefaultEffect);
        if(hasDefaultEffect)
            return 0;
    }
    
    // Check pipeline ID
    int pipelineId = game_GetPipelineID(atomic);
    void *geom = game_RpAtomicGetGeometry(atomic);
    return pipelineId == RSPIPE_PC_CustomBuildingDN_PipeID
        || pipelineId == RSPIPE_PC_CustomBuilding_PipeID
        || (((int(__cdecl *)(void *))0x5D6E90)(geom) && 
            ((RpGeometry *)geom)->preLitLum);
}
```

**Difference from buildingPipe version**: Checks normal map pipelines and MatFX effects before the pipeline ID check.

### MaterialHasDefaultMatFXEffect_cb (normalmap.cpp:67-82)

**Callback type**: `RpMaterial* (*)(RpMaterial*, int*)` — must return `RpMaterial*` to continue iteration, NULL to stop.

```cpp
static RpMaterial *MaterialHasDefaultMatFXEffect_cb(
    RpMaterial *material, int *hasDefaultEffect) 
{
    int effect = game_RpMatFXMaterialGetEffects(material);
    if(effect) {
        if(effect == 2) {  // rpMATFXEFFECTBUMPENVMAP
            int matfx_offset = *(int *)0x8D12C4;
            if(*(int *)((int)material + matfx_offset))
                return material;  // Has envmap → continue
        }
        *hasDefaultEffect = 1;
        return NULL;  // Stop iteration
    }
    return material;  // Continue iteration
}
```

**Critical**: DK22Pac's original returns `RpMaterial*` (correct). Our callback MUST match this signature for `RpGeometryForAllMaterials` to work correctly.

## DllMain vs InjectDelayedPatches

### Original skygfx Architecture
```
DllMain → hooks at 0x74872D (IsAlreadyRunning) → InjectDelayedPatches
```
- Hooks are DEFERRED — installed when game reaches 0x74872D
- RW engine is fully initialized before hooks run
- `RpNormMapPluginAttach()` called at the right time

### Our Architecture
```
DllMain → InjectDelayedPatches() called directly
```
- Hooks installed IMMEDIATELY during DLL load
- RW engine may NOT be fully initialized
- `RpNormMapPluginAttach()` called before RW engine ready → FAILS

**This is why normalmap_init() always fails** — the plugin is already attached from DllMain.

## Game Address Reference

### Building Pipeline

| Address | Function | Purpose |
|---------|----------|---------|
| `0x5D7F40` | `IsCBPCPipelineAttached` | Check if atomic should use custom building pipe |
| `0x5D5B80` | `IsCCPCPipelineAttached` | Check if atomic should use custom car pipe |
| `0x5DA610` | `CustomPipeAtomicSetup` | Set up atomic pipeline assignment |
| `0x5D7100` | `CreateCustomObjPipe_PS2` | Create DN building pipe |
| `0x5D7D90` | `CreateCustomObjPipe_PS2` | Create standard building pipe |
| `0x5D7200` | Interpolation code | Patched to RET to disable |
| `0x53C15E` | `CustomBuildingPipeline__Update` | Building ambient update |

### Vehicle Pipeline

| Address | Function | Purpose |
|---------|----------|---------|
| `0x5D5B80` | `IsCCPCPipelineAttached` | Car pipeline check |
| `0x5D9020` | `CreateCustomObjPipe_PS2` | Vehicle pipe creation |
| `0x4C88F0` | Upgrade parts | Jumped to 0x5DA610 |

### RW SDK Functions

| Address | Function | SDK Source |
|---------|----------|-----------|
| `0x7F0410` | `RwFrameCreate` | rwcore |
| `0x7F0990` | `RwFrameGetLTM` | rwcore |
| `0x804EF0` | `RwObjectHasFrameSetFrame` | rwcore |
| `0x7F10B0` | `RwFrameSetIdentity` | rwcore |
| `0x74C790` | `RpGeometryForAllMaterials` | rpworld |
| `0x72FC40` | `GetPipelineID` | rpworld |
| `0x72FC50` | `SetPipelineID` | rpworld |
| `0x811C30` | `RpMatFXAtomicQueryEffects` | rpmatfx |

### Global Variables

| Address | Type | Purpose |
|---------|------|---------|
| `0xC8800C` | `CTxdPool*` | TXD store pool |
| `0xC02C68` | `RxPipeline*` | Building pipeline (Obj) |
| `0xC02C1C` | `RxPipeline*` | Building DN pipeline |
| `0xC02D24` | `RxPipeline*` | Default vehicle pipeline |
| `0xC02D2C` | `CPool*` | EnvMap atomic data pool |
| `0x8D12C4` | `int` | MatFX material offset |
| `0xC886EC` | `RpLight*` | Directional light pointer |
| `0x8D0A5C` | Function ptr | RW SDK sync function 1 |
| `0x8D0A60` | Function ptr | RW SDK sync function 2 |
| `0x8D0A64` | Function ptr | RW SDK sync function 3 |

## SilentPatch Compatibility

SilentPatch is the universal compatibility patch for GTA SA — skygfx MUST run on top of it. SilentPatch has explicit skygfx detection and compatibility code.

### How SilentPatch Detects skygfx

SilentPatch uses `ModuleList` to enumerate loaded DLLs, then calls `GetProcAddress(skygfxModule, "GetConfig")` to get the `Config*` struct. It checks `config->version >= 0x360` to determine skygfx capabilities:

```cpp
// SilentPatchSA.cpp:48-75
namespace ModCompat::SkyGfx {
    bool PatchesMoonphases(HMODULE module) {
        const Config* config = GetConfig(module);  // GetProcAddress("GetConfig")
        return config->version >= 0x360;  // SKYGFX_VERSION_WITH_MOONPHASES
    }
}
```

**Our skygfx exports**: `extern "C" __declspec(dllexport) Config* GetConfig(void)` at `main.cpp:82-88`. VERSION = `0x370` (`skygfx.h:70`). SilentPatch threshold = `0x360`. **Compatible.**

### Hook Conflict Analysis

SilentPatch uses a two-phase init: `Patch_SA_10()` (immediate at DllMain) and `InjectDelayedPatches_10()` (deferred via `0x74872D` hook).

| Address | SilentPatch | skygfx | Conflict? |
|---------|------------|--------|-----------|
| `0x74872D` (IsAlreadyRunning) | Hooks in Patch_SA_10 (DllMain) | **NOT hooked** — calls InjectDelayedPatches directly | **NO** |
| `0x5D7F1E` (CALL in CustomBuildingDNPipeline) | InterceptCall — SkinBuildingPipelineFix (skip building pipe for skinned atomics) | **NOT hooked** — skygfx hooks `0x5D7F40` (different function) | **NO** |
| `0x713C4C` (RenderOneXLUSprite) | Moonphases hook — **SKIPPED** when skygfx detected (version >= 0x360) | renderMoonMask hook | **NO** (SilentPatch yields) |
| `0x7491C0` (AtomicDefaultRenderCallBack) | ExternalFunc binding only (reads address, doesn't hook) | myDefaultCallback hook | **NO** |
| `0x748D9B/0x748D1F` (frame limiter) | NewFrameRender + GetTimeSinceLastFrame | **NOT hooked** | **NO** |
| `0x7F39F0` (RW SDK function) | **NOT hooked** (SP hooks 0x7F6xxx for MSAA only) | safe_RwFrameSyncObject | **NO** |
| `0x5DA610` (CustomPipeAtomicSetup) | **NOT hooked** | normalmap_init (when enabled) | **NO** |
| `0x5D7F40` (IsCBPCPipelineAttached) | **NOT hooked** | hookBuildingPipe or normalmap_init | **NO** |
| `0x5D5B80` (IsCCPCPipelineAttached) | **NOT hooked** | normalmap_init | **NO** |
| `0x5D9xxx` (DarkVehiclesFix) | Multiple hooks for blown-up car rendering | **NOT hooked** | **NO** |

### Key Compatibility Points

1. **No 0x74872D conflict**: skygfx calls `InjectDelayedPatches()` directly from DllMain instead of hooking `IsAlreadyRunning`. SilentPatch hooks 0x74872D — no clash.
2. **Moonphases yields to skygfx**: SilentPatch checks `config->version >= 0x360` and skips its moonphases hook when skygfx is present.
3. **0x5D7F1E is a CALL site**: SilentPatch intercepts a CALL inside `CustomBuildingDNPipeline_CustomPipeAtomicSetup` (not the function entry). skygfx hooks `0x5D7F40` (IsCBPCPipelineAttached) — different function, no overlap.
4. **SilentPatch uses InterceptCall**: Most hooks preserve the original function and call through. skygfx uses `InjectHook` with `PATCH_JUMP` for most hooks — one-directional replacement.

### SilentPatch Hook Summary (SA 1.0)

**Phase 1 (Immediate — Patch_SA_10):**
- `0x74872D` — IsAlreadyRunning → InjectDelayedPatches_10
- `0x748D9B` — RsEventHandler → NewFrameRender (precise frame limiter)
- `0x748D1F` — GetTimeSinceLastFrame
- `0x5D993F-0x5D9CB2` — DarkVehiclesFix (4 hooks for blown-up car rendering)
- `0x5D88D1-0x5D9F1F` — D3DLIGHT float patches (zero out vehicle lights)
- `0x44E82E/0x44ECEE` — ps2rand (proper randomization)
- `0x7491C0` — AtomicDefaultRenderCallBack (ExternalFunc binding)
- Various: DirectInput, MSAA, resolution, mouse, audio, collision fixes

**Phase 2 (Deferred — InjectDelayedPatches_10):**
- `0x5D7F1E` — SkinBuildingPipelineFix (skip building pipe for skinned atomics)
- `0x5D5DB0` — RemapDirt (car dirt remapping)
- `0x713C4C` — MoonPhases (conditional: skipped if skygfx present)
- `0x706662` — ShadowCamera::Update
- Various: speech, script, zone name, scaling fixes

## See Also

- [[RenderWare SDK Architecture]]
- [[Crash Analysis Reference]]
- [[Pipelines/Building Pipelines]]

# GTA:SA Render Core Audit — PS2 / Xbox / PC Paths

**Date:** 2026-10-24  
**Target tree:** `E:\dev(dave)\releases\GTASource\gtasa_src-main`  
**Mode:** read-only source audit. This document is the only file written.

---

## 0. Scope, provenance, and caveats

The tree is a **partial, mixed-branch source snapshot**, not a clean single GTA:SA v1.0 tree:

- `Main.cpp`, `Game.cpp`, `ScreenFX.cpp`, `lights.c`, `PointLights.cpp`, `ps2all.cpp`, `sky.cpp`, `CPS2Peek.*`, `RwCam.cpp`, `GtaPlugins.cpp`, and `pipes/*` are PS2/PC/Xbox-era GTA:SA code.
- Some `modelinfo/*` files contain **later-branch / Rage-era** code (for example `modelinfo/VehicleModelInfo.cpp:13-19` includes `grcore`, `crskeleton`, `phbound` headers), so the modelinfo material should be treated as *indicative of the model/pipeline contract*, not as exact SA 1.0 source.
- Several called subsystems are **not present** in this dump: `CRenderer`, `CCoronas`, `CWaterLevel`, `CShadows`, `CClouds`, `CEntity::SetupLighting`, and the `CVu*` renderers. Their call sites and declarations are present; their bodies are not. Where that matters it is called out explicitly.

The most important structural finding:

> **There is no `CGame::Render` in this tree.** The per-frame render graph is the pair of global functions `RenderScene()` and `RenderEffects()` in `Main.cpp`, called from the platform loop (`TheGame` on PS2, `Idle` on PC/Xbox). `Game.cpp` only has `CGame::Process` (simulation/update), which ends with `CWaterLevel::PreRenderWater()` (`Game.cpp:1442`, `Game.cpp:2010`).

A second, alternate tiled graph exists in `NewTileRendererCB()` (`Main.cpp:1783-2010`), but it is not called from either main loop in this dump. It is useful as evidence of the minimal render order and for the PS2 tile path.

---

## 1. Frame render graph

### 1.1 PC / Xbox frame (`Idle`, `Main.cpp:3954-4418`)

`Idle` is the PC/Xbox per-frame entry (`rsIDLE` → `Idle`, `Main.cpp:4628-4634`).

| # | Step | Function / call | Evidence |
|---|------|-----------------|----------|
| 1 | Time / input update | `CTimer::Update()` | `Main.cpp:3970` |
| 2 | Per-frame UI init | `CSprite2d::InitPerFrame`, `CFont::InitPerFrame` | `Main.cpp:4061-4064` |
| 3 | Reset point lights | `CPointLights::InitForFrame()` | `Main.cpp:4066` |
| 4 | Simulation / update | `CGame::Process()` | `Main.cpp:4072`; body `Game.cpp:1442` |
| 5 | Update global lights | `SetLightsWithTimeOfDayColour(Scene.world)` | `Main.cpp:4152` |
| 6 | Build render list | `CRenderer::ConstructRenderList()` | `Main.cpp:4200` |
| 7 | Pre-render: shadows + lights | `CRenderer::PreRender()` | `Main.cpp:4211` |
| 8 | Ped post-pre-render | `CWorld::ProcessPedsAfterPreRender()` | `Main.cpp:4212` |
| 9 | Real-time shadow update | `g_realTimeShadowMan.Update()` | `Main.cpp:4223` |
| 10 | Mirror pre-pass | `CMirrors::BeforeMainRender()` | `Main.cpp:4230` |
| 11 | Start frame / clear / sky | `DoRWStuffStartOfFrame_Horizon()` | `Main.cpp:4240`, `4248`; body `1134-1166` |
| 12 | Canonical state + fog | `DefinedState()`; far clip; fog distance | `Main.cpp:4257-4260` |
| 13 | Mirror buffer render | `CMirrors::RenderMirrorBuffer()` | `Main.cpp:4269` |
| 14 | **World scene** | `RenderScene()` | `Main.cpp:4287`; body `3194-3439` |
| 15 | Debug overlays | `RenderDebugShit()` | `Main.cpp:4292`; body `3446-3555` |
| 16 | PC/Xbox muzzle-flash fix | `CVisibilityPlugins::RenderWeaponPedsForPC()` then `ResetWeaponPedsForPC()` | `Main.cpp:4300-4302` |
| 17 | **Effects / post-FX** | `RenderEffects()` | `Main.cpp:4305`; body `3562-3728` |
| 18 | Motion blur | `TheCamera.RenderMotionBlur()` | `Main.cpp:4319`; body `Camera.cpp:16006-16018` |
| 19 | **2D / HUD** | `Render2dStuff()` | `Main.cpp:4321`; body `3730-3874` |
| 20 | Menus | `RenderMenus()` | `Main.cpp:4365`; body `3876-3925` |
| 21 | Fade | `DoFade()` | `Main.cpp:4377`; body `1169-1340` |
| 22 | Post-fade 2D | `Render2dStuffAfterFade()` | `Main.cpp:4386`; body `3927-3946` |
| 23 | Credits | `CCredits::Render()` | `Main.cpp:4387` |
| 24 | End frame / present | `DoRWStuffEndOfFrame()` → `RwCameraEndUpdate`, `RsCameraShowRaster` | `Main.cpp:4403`; body `1348-1377` |

Key ordering consequence: **`CPostEffects::Render()` is inside `RenderEffects()` at `Main.cpp:3723`, so post-processing runs before HUD/menus/fade.** HUD is drawn later in `Render2dStuff()` (`CHud::Draw()` at `Main.cpp:3836`) and after fade in `Render2dStuffAfterFade()` (`CHud::DrawAfterFade()` at `Main.cpp:3929`).

Start-of-frame specifics (`DoRWStuffStartOfFrame_Horizon`, `Main.cpp:1134-1166`):

- `CameraSize()` (`Main.cpp:1143`)
- `CVisibilityPlugins::SetRenderWareCamera()` (`Main.cpp:1145`)
- `RwCameraClear(..., rwCAMERACLEARZ)` — Z only, not colour (`Main.cpp:1149-1152`)
- `RsCameraBeginUpdate()` (`Main.cpp:1154`)
- `DefinedState()` (`Main.cpp:1159`)
- `TheCamera.UpdateViewMatrix()` (`Main.cpp:1161`)
- `CClouds::RenderSkyPolys()` (`Main.cpp:1163`)

The sky is therefore *drawn* after the Z clear, not used as a clear colour.

`CGame::Process` update ordering that feeds the renderer:

- `CWeather::Update()` `Game.cpp:1585`
- `CWorld::Process()` `Game.cpp:1715`
- `TheCamera.Process()` `Game.cpp:1775`
- `CCoronas::DoSunAndMoon()` / `CCoronas::Update()` `Game.cpp:1805-1806`
- `CShadows::UpdatePermanentShadows()` `Game.cpp:1808`
- `CCustomBuildingRenderer::Update()` (PC/Xbox) or `CVuCustomBuildingRenderer::Update()` (PS2) `Game.cpp:1829-1836`
- `CWaterLevel::PreRenderWater()` `Game.cpp:2010`

### 1.2 `RenderScene()` internal order (`Main.cpp:3194-3439`)

| # | Step | Function | Evidence |
|---|------|----------|----------|
| 1 | Neutralize states | no texture, Z-test off, Z-write off, vertex alpha off | `Main.cpp:3217-3220` |
| 2 | Background clouds (not in mirror) | `CMovingThings::Render_BeforeClouds()`, `CClouds::Render()` | `Main.cpp:3229-3233` |
| 3 | Restore Z / Gouraud | `rwRENDERSTATEZTESTENABLE`, `ZWRITEENABLE`, `SHADEMODE` | `Main.cpp:3239-3241` |
| 4 | Car FX pre-update | `CVuCarFXRenderer::PreRenderUpdate()` / `CCarFXRenderer::PreRenderUpdate()` | `Main.cpp:3244-3249` |
| 5 | Seam remover begin | `CPostEffects::SeamRemoverBegin()` | `Main.cpp:3259`; body `ScreenFX.cpp:2138-2208` |
| 6 | Roads | `CRenderer::RenderRoads()` | `Main.cpp:3262` |
| 7 | Corona reflections | `CCoronas::RenderReflections()` | `Main.cpp:3266` |
| 8 | Everything except roads | `CRenderer::RenderEverythingBarRoads()` | `Main.cpp:3270` |
| 9 | Seam remover end | `CPostEffects::SeamRemoverEnd()` | `Main.cpp:3275`; body `ScreenFX.cpp:2210-2514` |
| 10 | Breakables opaque | `g_breakMan.Render(false)` | `Main.cpp:3284` |
| 11 | Underwater fading entities | `CRenderer::RenderFadingInUnderwaterEntities()` | `Main.cpp:3297` |
| 12 | Water (only if not underwater) | cull NONE → `CWaterLevel::RenderWater()` → cull BACK | `Main.cpp:3301-3308` |
| 13 | Fading entities | `CRenderer::RenderFadingInEntities()` | `Main.cpp:3312` |
| 14 | Shadows (only if not mirror) | `CShadows::UpdateStaticShadows()`, `RenderStaticShadows()`, `RenderStoredShadows()` | `Main.cpp:3316-3359`; exact calls `3343-3350` |
| 15 | Breakables alpha | `g_breakMan.Render(true)` | `Main.cpp:3367` |
| 16 | Plants | `CPlantMgr::Render()` | `Main.cpp:3379` |
| 17 | Bottom clouds / rain / sun corona | `CClouds::RenderBottomFromHeight()`, `CWeather::RenderRainStreaks()`, `CCoronas::RenderSunReflection()` | `Main.cpp:3386-3395` |
| 18 | Water when underwater | cull NONE → `CWaterLevel::RenderWater()` → cull BACK | `Main.cpp:3402-3409` |
| 19 | **PS2 only:** water fog, moving fog, volumetric clouds | `CWaterLevel::RenderWaterFog()`, `CClouds::MovingFogRender()`, `CClouds::VolumetricCloudsRender()` | `Main.cpp:3411-3430` |
| 20 | Volumetric shadows | `CVolumetricShadowMgr::Render()` | `Main.cpp:3432-3436` |

Notes:

- `CCoronas::RenderReflections()` and `CCoronas::RenderSunReflection()` are **inside `RenderScene`**, but `CCoronas::Render()` is **not** — it is in `RenderEffects` (`Main.cpp:3595`).
- `CWaterLevel::RenderWaterFog()` is PS2-only here (`Main.cpp:3411-3418`). On PC/Xbox the same call is in `RenderEffects` (`Main.cpp:3621-3628`).
- Shadows have a Z-buffer bias hack around them (`Main.cpp:3319-3339`, restore `3354-3358`), and are skipped entirely while rendering reflections (`Main.cpp:3316`).
- The PS2 path explicitly re-uploads lighting once per frame before scene rendering: `PS2AllRecalculateLighting()` (`Main.cpp:2917`; definition `ps2all.cpp:626-629`).

### 1.3 `RenderEffects()` internal order (`Main.cpp:3562-3728`)

| # | Step | Function | Evidence |
|---|------|----------|----------|
| 1 | Birds | `CBirds::Render()` | `Main.cpp:3565` |
| 2 | Skidmarks / ropes | `CSkidmarks::Render()`, `CRopes::Render()` | `Main.cpp:3570-3571` |
| 3 | Glass | `CGlass::Render()` | `Main.cpp:3579` |
| 4 | Moving things | `CMovingThings::Render()` | `Main.cpp:3585` |
| 5 | Really-draw-last list | `CVisibilityPlugins::RenderReallyDrawLastObjects()` | `Main.cpp:3590`; body `GtaPlugins.cpp:2075-2095` |
| 6 | Coronas | `CCoronas::Render()` | `Main.cpp:3595` |
| 7 | FX system | `g_fx.Render(TheCamera.m_pRwCamera, false)` | `Main.cpp:3604` |
| 8 | Water cannons | `CWaterCannons::Render()` | `Main.cpp:3615` |
| 9 | **PC/Xbox only:** water fog, moving fog, volumetric clouds | `CWaterLevel::RenderWaterFog()`, `CClouds::MovingFogRender()`, `CClouds::VolumetricCloudsRender()` | `Main.cpp:3621-3640` |
| 10 | Heli / script search lights | `CHeli::Pre_SearchLightCone()`, `RenderAllHeliSearchLights()`, `CTheScripts::RenderAllSearchLights()`, `Post_SearchLightCone()` | `Main.cpp:3644-3652` |
| 11 | Weapon effects | `CWeaponEffects::Render()`; PC mouse-recruit triangle | `Main.cpp:3655-3665` |
| 12 | Special FX / vehicle recording | `CSpecialFX::Render()`, `CVehicleRecording::Render()` | `Main.cpp:3668-3674` |
| 13 | **Point-light fog sprites** | `CPointLights::RenderFogEffect()` | `Main.cpp:3684-3688` |
| 14 | Cover debug | `CCover::Render()` (non-final) | `Main.cpp:3697-3699` |
| 15 | First-person vehicle | `CRenderer::RenderFirstPersonVehicle()` | `Main.cpp:3717` |
| 16 | **Post-processing** | `CPostEffects::Render()` | `Main.cpp:3721-3726`; body `ScreenFX.cpp:2520-3132` |

`CPostEffects::Render()` itself:

- PC: ends the camera update, pushes `pRasterFrontBuffer`, renders the camera raster into it with `RwRasterRenderFast`, pops, then begins the camera update again (`ScreenFX.cpp:2531-2544`).
- Xbox: `RasterCopyPostEffects()` (`ScreenFX.cpp:2545-2548`).
- PS2: `RwCameraEndUpdate(Scene.camera)` before filters (`ScreenFX.cpp:2609-2611`) and `RsCameraBeginUpdate()` again at the end (`ScreenFX.cpp:3126-3131`).
- PS2 also performs a direct GS-state restore via `CPostEffects::ResetRwRenderState()` (`ScreenFX.cpp:3030-3037`, body `3141-3163`).
- Colour-filter enable is PC-only gated (`ScreenFX.cpp:2717-2729`); Xbox/PS2 fall through and always call `ColourFilter()`.

### 1.4 PS2 frame loop (`TheGame`, `Main.cpp:2734-3121`)

The PS2 loop is a separate copy of the frame graph. Differences from PC/Xbox `Idle`:

| Step | PS2 behavior | Evidence |
|------|--------------|----------|
| Point-light reset | `CPointLights::InitForFrame()` | `Main.cpp:2759` |
| Simulation | `CGame::Process()` | `Main.cpp:2766` |
| Lights | `SetLightsWithTimeOfDayColour()` | `Main.cpp:2875` |
| Render list | `CRenderer::ConstructRenderList()` | `Main.cpp:2889` |
| Pre-render | `CRenderer::PreRender()` + `CWorld::ProcessPedsAfterPreRender()` | `Main.cpp:2905-2907` |
| PS2 lighting reset | `PS2AllRecalculateLighting()` | `Main.cpp:2917` |
| Mirror pre-pass | `CMirrors::BeforeMainRender()` | `Main.cpp:2923` |
| Smokey fog override | If `CPostEffects::m_smokeyEnable`, overwrite far clip and fog distance/colour | `Main.cpp:2928-2938` |
| Start frame | `DoRWStuffStartOfFrame_Horizon()` | `Main.cpp:2947`, `2951` |
| Canonical state | `DefinedState()` | `Main.cpp:2958` |
| Mirror buffer | `CMirrors::RenderMirrorBuffer()` | `Main.cpp:2967` |
| Scene | `RenderScene()` | `Main.cpp:2973` |
| Debug | `RenderDebugShit()` | `Main.cpp:2985` |
| Effects | `RenderEffects()` | `Main.cpp:2994` |
| Motion blur | `TheCamera.RenderMotionBlur()` | `Main.cpp:3012` |
| 2D / HUD | `Render2dStuff()` | `Main.cpp:3026` |
| Menus | `RenderMenus()` | `Main.cpp:3062` |
| Fade | `DoFade()` | `Main.cpp:3082` |
| After fade | `Render2dStuffAfterFade()` | `Main.cpp:3092` |
| Load monitor bars | `g_LoadMonitor.Render()` | `Main.cpp:3108` |
| End frame | `DoRWStuffEndOfFrame()` | `Main.cpp:3111` |

Differences worth noting:

- PS2 does **not** call `g_realTimeShadowMan.Update()` in this loop (that is PC/Xbox only, `Main.cpp:4223`; real-time ped shadows themselves are PC/Xbox-only at `Ped.cpp:8877-8958`).
- PS2 does **not** call `CVisibilityPlugins::RenderWeaponPedsForPC()` in the frame loop (that block is PC/Xbox-only, `Main.cpp:4300-4303`).
- PS2 calls `CPS2Peek::Profile()` at the top of the loop when the profiler is enabled (`Main.cpp:2749-2751`).
- PS2 has `g_LoadMonitor.Render()`; PC/Xbox `Idle` does not in this dump (`Main.cpp:3108` vs `4305-4403`).

### 1.5 Alternate tiled graph (`NewTileRendererCB`, `Main.cpp:1783-2010`)

This is a secondary render callback that sets up and tears down the camera itself (`Main.cpp:1783-1805`, `2002-2008`). Its order is the minimal old-school sequence:

1. `DefinedState()` and far/fog (`Main.cpp:1894-1898`)
2. `CMovingThings::Render_BeforeClouds()` and `CClouds::Render()` (`Main.cpp:1903-1905`)
3. `CRenderer::RenderRoads()` (`Main.cpp:1911`)
4. `CCoronas::RenderReflections()` (`Main.cpp:1913`)
5. `DefinedState()`, re-set far/fog, enable fog (`Main.cpp:1915-1922`)
6. `CRenderer::RenderEverythingBarRoads()` (`Main.cpp:1924`)
7. `CWaterLevel::RenderWater()` (`Main.cpp:1928`)
8. `CShadows::UpdateStaticShadows()` + `RenderStaticShadows()` + `RenderStoredShadows()` (`Main.cpp:1949-1951`)
9. Birds, glass, water cannons, special FX, ropes, vehicle recording (`Main.cpp:1959-1964`)
10. Heli/script search lights (`Main.cpp:1966-1972`)
11. Skidmarks, weapon effects, point-light fog, moving things (`Main.cpp:1975-1985`)
12. FX system (`Main.cpp:1990`)

It is not referenced by the current main loops. Treat it as a historical/alternate path, not the shipping one.

### 1.6 Global gates that choose platform and optional paths

| Gate | Effect | Evidence |
|------|--------|----------|
| `GTA_PS2`, `GTA_PC`, `GTA_XBOX` | Top-level platform compile switches | `Game.cpp:219-248`, `Main.cpp:3411`, `3621` |
| `GTA_SA` | Enables PS2 VU CarFX/SkinPed and PC/Xbox volumetric shadows + CarFX | `Game.cpp:223-238` |
| `USE_VU_TURBO_SPRITE`, `USE_VU_WATER_RENDERER`, `USE_VU_CARFX_RENDERER`, `USE_CVSKINPED_RENDERER` | PS2-only renderer features | `Game.cpp:219-226`, `250-289` |
| `USE_PC_CARFX_RENDERER`, `USE_VOLUMETRIC_SHADOW_RENDERER` | PC/Xbox-only renderer features | `Game.cpp:231-238` |
| `USE_CVBUILDING_RENDERER`, `USE_PLANT_MGR` | All platforms; implementation branches by platform | `Game.cpp:246-248`, `280-286`; `Init_overlay.cpp:789-804` |
| `CMirrors::TypeOfMirror`, `CMirrors::bRenderingReflection` | Mirror/reflection pass gating | `Main.cpp:3229`, `3316`, `3390` |
| `CWeather::UnderWaterness` | Water/underwater pass selection | `Main.cpp:3196`, `3301`, `3402` |
| `CGame::currArea` | Area-dependent alpha-test and extra-light limits | `GtaPlugins.cpp:1941`; `lights.c:278-281` |
| `FrontEndMenuManager.m_MenuActive`, `TheCamera.GetScreenFadeStatus()` | Skip world render while menu/dark-screen | `Main.cpp:4171`; PS2 equivalent `Main.cpp:2897` |
| `g_fx.GetFxQuality()` | Real-time ped shadow quality gate | `Ped.cpp:8879` |
| `CTimer::bSkipProcessThisFrame` | Skip accident/FX update block | `Game.cpp:1727` |
| `gbDontRenderPeds`, `gbDontRenderVehicles`, `gbDontRenderBuildings`, `gbDontRenderBigBuildings`, `gbDontRenderWater`, `gbDontRenderObjects` | Debug render toggles (declarations not in dump; only registration and key toggles) | `Game.cpp:697-702`, `1545-1549` |
| `FINAL`, `FINALBUILD`, `MASTER` | Remove debug/editor paths | `Main.cpp:3448`, `3475`; `GtaPlugins.cpp:1931-1934` |

---

## 2. Pipeline selection per platform

### 2.1 The enums and flags

Model type enum (`CBaseModelInfo`):

```cpp
// modelinfo/BaseModelInfo.h:69-80
enum ModelInfoType {
    MI_TYPE_NONE = AF_TYPE_USER,
    MI_TYPE_BASE,        // 1
    MI_TYPE_MLO,         // 2
    MI_TYPE_TIME,        // 3
    MI_TYPE_WEAPON,      // 4
    MI_TYPE_VEHICLE,     // 5
    MI_TYPE_PED,         // 6
    MI_TYPE_COMPOSITE,   // 7
    NUM_MI_TYPES,
    MI_HIGHEST_FACTORY_ID = NUM_MI_TYPES-1
};
```

Archive/load flags (`FLAG_*`) and internal model flags (`MODEL_*`):

- `FLAG_DONT_WRITE_ZBUFFER = (1<<6)` (`BaseModelInfo.h:140`)
- `MODEL_DONT_WRITE_ZBUFFER = (1<<3)` (`BaseModelInfo.h:174`)
- `GetDontWriteZBuffer()` / `SetDontWriteZBuffer()` (`BaseModelInfo.h:341-342`)
- Conversion from load flags to model flags: `ConvertLoadFlagsAndAttributes()` (`BaseModelInfo.cpp:166-207`; the Z-buffer line is `BaseModelInfo.cpp:177`)

There is **no literal `bDontWriteZBuffer`** in this dump. The actual code is `GetDontWriteZBuffer()` / `MODEL_DONT_WRITE_ZBUFFER`. The name `bDontWriteZBuffer` is an older/alternate spelling, not the one used here.

Vehicle enums:

- `VehicleType` (`VEHICLE_TYPE_CAR`, `PLANE`, `HELI`, `BOAT`, `TRAIN`, etc.) (`modelinfo/VehicleModelInfo.h:169-199`)
- `VehicleAtomicType` (`VEHICLE_ATOMIC_DAMAGED = 1<<1`, `ALPHA`, `FLAT`, `DONTCULL`, `PIPE_NO_EXTRA_PASSES`, etc.) (`modelinfo/VehicleModelInfo.h:202-224`)

Damage state:

- `bIsDamaged` is a **CVehicle runtime flag**, not the model/pipeline selector. In this dump it only appears being reset (`Automobile.cpp:9407`); the actual visual damage model path is `CDamageAtomicModelInfo` (`modelinfo/AtomicModelInfo.cpp:41`, `459-527`) and `VEHICLE_ATOMIC_DAMAGED` visibility (`Automobile.cpp:3462`, `3476`).
- `CDamageAtomicModelInfo::m_bCreateDamagedVersion` switches `CreateInstance()` between damaged and normal atomics (`modelinfo/AtomicModelInfo.cpp:41`, `535-548`).
- `CDamageAtomicModelInfo::SetDamagedAtomic()` re-runs the same platform pipeline attach logic as `CAtomicModelInfo::SetAtomic()` (`modelinfo/AtomicModelInfo.cpp:469-527`).

### 2.2 Load-time dispatch (`AtomicModelInfo` / `ClumpModelInfo`)

Pipelines are attached **when the model is loaded**, not during rendering.

`CAtomicModelInfo::SetAtomic()` (`modelinfo/AtomicModelInfo.cpp:58-150`) does:

```cpp
// modelinfo/AtomicModelInfo.cpp:89-143 (condensed; comments removed)
#if (defined (GTA_PC) || defined (GTA_XBOX))
if(CCustomBuildingRenderer::IsCBPCPipelineAttached(pAtomic))
    CCustomBuildingRenderer::AtomicSetup(pAtomic);
else if(CCarFXRenderer::IsCCPCPipelineAttached(pAtomic))
    CCarFXRenderer::SetCustomFXAtomicRenderPipelinesVMICB(pAtomic, NULL);
#endif

#if defined (GTA_PS2)
if(CVuCustomBuildingRenderer::IsCVBPipelineAttached(pAtomic))
    CVuCustomBuildingRenderer::AtomicSetup(pAtomic);
else if(CVuCarFXRenderer::IsCVCPipelineAttached(pAtomic))
    CVuCarFXRenderer::SetCustomFXAtomicRenderPipelinesVMICB(pAtomic, NULL);
else if(CVuCustomSkinPedRenderer::IsCVSPPipelineAttached(pAtomic))
    ASSERTMSG(FALSE, "CVSkinPedPipe: Peds shouldn't be loaded as atomics! See Andrzej.");
else
    PS2AllSetAtomicPipe(pAtomic);
#endif
```

`CClumpModelInfo::SetClump()` (`modelinfo/ClumpModelInfo.cpp:194-291`) iterates every atomic through `SetAtomicPipeCB()` (`ClumpModelInfo.cpp:135-186`) and performs the same branch:

```cpp
// modelinfo/ClumpModelInfo.cpp:139-182 (condensed)
#if (defined (GTA_PC) || defined(GTA_XBOX))
if(CCustomBuildingRenderer::IsCBPCPipelineAttached(pAtomic))
    CCustomBuildingRenderer::AtomicSetup(pAtomic);
else if(CCarFXRenderer::IsCCPCPipelineAttached(pAtomic))
    CCarFXRenderer::CustomCarPipeAtomicSetup(pAtomic);
#endif

#if defined (GTA_PS2)
if(CVuCustomSkinPedRenderer::IsCVSPPipelineAttached(pAtomic))
    CVuCustomSkinPedRenderer::AtomicSetup(pAtomic);
else if(CVuCustomBuildingRenderer::IsCVBPipelineAttached(pAtomic))
    CVuCustomBuildingRenderer::AtomicSetup(pAtomic);
else if(CVuCarFXRenderer::IsCVCPipelineAttached(pAtomic))
    CVuCarFXRenderer::SetCustomFXAtomicRenderPipelinesVMICB(pAtomic, NULL);
else
    PS2AllSetAtomicPipe(pAtomic);
#endif
```

Additional dispatch details:

- `CVisibilityPlugins::SetClumpModelInfo()` stores the modelinfo on the clump frame and selects `VehicleVisibilityCB` / `VehicleVisibilityCB_BigVehicle` for vehicle classes (`GtaPlugins.cpp:1655-1681`); trains and fake planes use the big-vehicle frustum-only callback (`GtaPlugins.cpp:1668-1676`).
- `CVisibilityPlugins::SetAtomicRenderCallback()` installs the per-atomic render callback (`GtaPlugins.cpp:1510-1515`); `CClumpModelInfo::SetAtomicRendererCB()` is the generic helper (`ClumpModelInfo.cpp:388-400`).
- `CVisibilityPlugins::RenderEntity()` handles the `DontWriteZBuffer` state toggle, distance-fade alpha, backface-cull toggles, and calls `CRenderer::RenderOneNonRoad()` (`GtaPlugins.cpp:1886-1959`).
- `CVisibilityPlugins::RenderVehicleHiDetailCB()` / `AlphaCB()` / `ReallyLowDetailCB()` implement LOD/cull/alpha ordering before calling `DEFAULT_RENDER_CALLBACK` (`GtaPlugins.cpp:1056-1140`, `985-1053`, `1276-1350`).
- `CVisibilityPlugins::RenderReallyDrawLastObjects()` sets the final state block and then renders the really-draw-last list (`GtaPlugins.cpp:2075-2095`).

### 2.3 Building pipeline selection (PC vs Xbox vs PS2)

`CCustomBuildingRenderer` owns both the standard and DN pipes on PC/Xbox:

- `Initialise()` creates `CCustomBuildingPipeline::CreatePipe()` and `CCustomBuildingDNPipeline::CreatePipe()` (`pipes/CustomBuildingRenderer.cpp:62-75`).
- `AtomicSetup()` chooses DN vs standard:

```cpp
// pipes/CustomBuildingRenderer.cpp:105-139 (condensed)
RpAtomic* CCustomBuildingRenderer::AtomicSetup(RpAtomic *pAtomic)
{
#if defined(GTA_XBOX)
    if(CCustomBuildingRenderer::IsCBPCPipelineAttached(pAtomic))
        return CCustomBuildingDNPipeline::CustomPipeAtomicSetup(pAtomic); // DN
    else
        return CCustomBuildingPipeline::CustomPipeAtomicSetup(pAtomic);   // standard
#endif
#if defined(GTA_PC)
    if(CCustomBuildingDNPipeline::GetExtraVertColourPtr(pGeom) &&
       RpGeometryGetPreLightColors(pGeom))
        return CCustomBuildingDNPipeline::CustomPipeAtomicSetup(pAtomic); // DN
    else
        return CCustomBuildingPipeline::CustomPipeAtomicSetup(pAtomic);   // standard
#endif
}
```

Detection:

- PC: pipeline IDs `PDSPC_CBuildingEnvMap_AtmID` / `PDSPC_CBuildingDNEnvMap_AtmID` (`pipes/CustomBuildingRenderer.cpp:164-172`) **or** extra vertex-colour plugin + prelit colours (`CustomBuildingRenderer.cpp:174-175`).
- Xbox: pipeline ID `PDS_CustomXBoxColourNormal_AtmID` (`pipes/CustomBuildingRenderer.cpp:32`, `153-159`). On Xbox the night colours are stored in the vertex normals and the day colours in the vertex colours; the DN vertex shader interpolates them (`pipes/Xbox/CustomBuildingDNPipeline_Xbox.h:27-39`).

Actual pipeline attach is `RpAtomicSetPipeline()` plus `SetPipelineID()`:

- Standard building: `pipes/PC/CustomBuildingPipeline.cpp:146-157`; Xbox copy `pipes/Xbox/CustomBuildingPipeline_Xbox.cpp:147-158`.
- DN building: `pipes/PC/CustomBuildingDNPipeline.cpp:233-248` (`SetPipelineID(..., PDSPC_CBuildingDNEnvMap_AtmID)` at `246`); Xbox copy `pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:233-248`.
- Vehicle env map: `pipes/PC/CustomCarEnvMapPipeline.cpp:171-188` (`SetPipelineID(..., PDSPC_CarFXPipe_AtmID)` at `185`); Xbox copy `pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp` equivalents.

PS2 has no `CCustomBuildingRenderer`:

- PS2 initializes `CVuCustomBuildingRenderer::Initialise()` (`Init_overlay.cpp:789-796`).
- PS2 model attach uses `CVuCustomBuildingRenderer::AtomicSetup()` (`modelinfo/AtomicModelInfo.cpp:112-117`; `ClumpModelInfo.cpp:159-164`).
- PS2 fallback for all non-custom atomics is `PS2AllSetAtomicPipe()` (`modelinfo/AtomicModelInfo.cpp:138-141`; `ps2all.cpp:637-652`), which uses `pOptimisedLightPipe` / `pOptimisedLightSkinnedPipe`.
- PS2 custom pipelines are created in `PS2AllOptimizedPipelinesCreate()` (`Init_overlay.cpp:1456-1519`) and use `RpPDSSkyObjPipeCreate` / `RpPDSSkyMatPipeCreate` (`Init_overlay.cpp:1504-1506`).
- The `CVu*` renderer sources are **not in this dump**; only their call sites and `Get/SetDayNightBalanceParam` wrappers remain (`pipes/CustomBuildingRenderer.h:93-111`).

Important DN detail:

- PC DN uses an optional vertex shader (`GTA_PC_USE_DN_VS`), extra colour stream, and `m_fDNBalanceParam` (`pipes/PC/CustomBuildingDNPipeline.cpp:84-119`, `1152-1167`). It otherwise interpolates the prelit colour tables (`UpdateAtomicPrelitLum`, `pipes/PC/CustomBuildingDNPipeline.cpp` equivalent; Xbox copy `pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:1512-1575`).
- Xbox DN *always* uses `DN_USE_VERTEX_SHADER` (`pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:92-112`) and passes `1-DN` / `DN` in vertex-shader constants c4/c5 (`pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:453-498`); the render callback swaps every mesh to `DNVertexShader` (`pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:500-525`).

Day/night **model swap** is a separate system from DN vertex colours: `CTimeInfo::FindOtherTimeModel()` swaps `_nt` (night) and `_dy` (day) model names at load (`modelinfo/AtomicModelInfo.cpp:396-450`). This is how SA does whole-object day/night variants, in addition to per-vertex DN.

### 2.4 Vehicle pipeline selection

`CCarFXRenderer` wraps `CCustomCarEnvMapPipeline` on PC/Xbox:

- `CCarFXRenderer::Initialise()` creates the env-map pipe (`pipes/CarFXRenderer.cpp:40-46`).
- `CustomCarPipeAtomicSetup()` calls `CCustomCarEnvMapPipeline::CustomPipeAtomicSetup()` (`pipes/CarFXRenderer.cpp:98-103`).
- `SetCustomFXAtomicRenderPipelinesVMICB()` validates the pipe ID and then sets the pipe (`pipes/CarFXRenderer.cpp:136-157`).
- `IsCCPCPipelineAttached()` checks `PDSPC_CarFXPipe_AtmID` (`pipes/CarFXRenderer.cpp:164-172`).
- The vehicle material passes are encoded in the material's `surfaceProps.specular` low bits (`CARFX_ENVMAP1_PASS_BIT`, `ENVMAP2`, `SPECMAP`) (`pipes/PC/CustomCarEnvMapPipeline.h` definitions; usage `pipes/PC/CustomCarEnvMapPipeline.cpp:1320-1337`).

PS2 uses `CVuCarFXRenderer` instead (`modelinfo/AtomicModelInfo.cpp:118-131`; `modelinfo/ClumpModelInfo.cpp:165-177`), with the same `SetCustomFXAtomicRenderPipelinesVMICB` entry point. The PS2 vehicle-colour material pipe is `pVehicleColourMaterialPipe` (`ps2all.cpp:661-676`), and the per-mesh vehicle colour update is `CVehicleModelInfo::UpdateVehicleColour()` inside `RpMeshPS2AllVehicleBridgeCallBack()` (`ps2all.cpp:553-560`).

### 2.5 `ModelInfoAccel.cpp` — not render dispatch

`modelinfo/ModelInfoAccel.cpp` is a **model-info name→ID load accelerator**, not a renderer:

- It caches model IDs in `MODELS\MINFO.BIN` (`modelinfo/ModelInfoAccel.h:23-38`; `ModelInfoAccel.cpp:37-74`, `77-99`).
- `GetEntry()` either uses `CModelInfo::GetModelInfoFast()` or falls back to the slow name lookup and records IDs (`ModelInfoAccel.cpp:147-168`).
- The whole feature is **disabled on PC** and enabled on PS2/Xbox (`modelinfo/ModelInfoAccel.h:18-21`); its only platform branch is the file-open mode (`ModelInfoAccel.cpp:47-53`).
- It has no `RpAtomic`, `RpAtomicSetPipeline`, render callback, or draw call. Render dispatch is entirely in `AtomicModelInfo.cpp` / `ClumpModelInfo.cpp` / `GtaPlugins.cpp` per §2.2.

### 2.6 Platform-path table

| Concern | PS2 | PC | Xbox | Key evidence |
|---------|-----|----|------|--------------|
| Frame host | `TheGame` loop | `Idle()` via `rsIDLE` | `Idle()` via `rsIDLE` | `Main.cpp:2734-3121`; `3954-4418`; `4628-4634` |
| Camera/size | Custom `CameraSize` recreates rasters as `rwRASTERTYPECAMERATEXTURE` | Custom `CameraSize` resizes rasters in place | RW stock (file excluded from Xbox) | `RwCam.cpp:6`, `56-105`; `RwCam.cpp:6` gate |
| Building pipe | `CVuCustomBuildingRenderer` + PS2All fallback | `CCustomBuildingRenderer` (`CustomBuildingPipeline` + optional DN) | Same class, `_Xbox` sources; DN always VS | `Init_overlay.cpp:789-804`; `pipes/CustomBuildingRenderer.cpp:15-26`, `105-139` |
| Vehicle pipe | `CVuCarFXRenderer` | `CCarFXRenderer` + D3D9 env-map | `CCarFXRenderer` + D3D8/Xbox env-map | `pipes/CarFXRenderer.cpp:22-27`, `40-46`; `pipes/Xbox/CustomCarEnvMapPipeline_Xbox.h:1-16` |
| Skinned peds | VU skin renderer + PS2All skinned callback | Default D3D9 skinning + weight normalisation | Xbox all-in-one skin fix + shader cache | `ps2all.cpp:387-541`; `ClumpModelInfo.cpp:269-283`; `pipes/Xbox/xboxallinone.c:1-9`, `238-421` |
| Grass/plants | `CVuGrassRenderer` | `PC_GrassRenderer`/`PC_PlantsMgr` | Same as PC | `Game.cpp:270-278`; `pipes/PC_PlantsMgr.cpp` |
| Water | `CWaterLevel::RenderWater` + optional `CVuWaterRenderer` | `CWaterLevel::RenderWater` | Same as PC | `Main.cpp:3301-3308`, `3402-3409`; `Init_overlay.cpp:756-762` |
| Shadows | Static/stored in scene; no real-time ped shadow block | Static/stored + optional real-time ped | Same as PC | `Main.cpp:3316-3359`; `Ped.cpp:8877-8958` |
| Post-FX | Camera end/begin + direct GS filters + `ResetRwRenderState` | Front-buffer raster copy + `RsCameraBeginUpdate` | `RasterCopyPostEffects()` | `ScreenFX.cpp:2531-2548`, `2609-2611`, `3030-3037`, `3126-3163` |
| Fog/clouds extra passes | Inside `RenderScene` | Inside `RenderEffects` | Inside `RenderEffects` | `Main.cpp:3411-3430` vs `3621-3640` |
| Photos | Save to buffer | Save to `Gallery\*.jpg` | Save to buffer | `ScreenFX.cpp:3051-3113` |
| Prelit/vertex colour | PS2All/VU colour packets | Prelit required for many things | Prelit often not required | `ps2all.cpp:553-560`; `pipes/PC_PlantsMgr_overlay.cpp:349-357` |
| Pipeline source dir | `pipes/` + `ps2all.cpp` | `pipes/PC` + `pipes/MBR` (OpenGL MBR variant) | `pipes/Xbox` | `pipes/*`; `pipes/MBR/CustomPipelinesCommon.h:11-12` errors for Xbox |

---

## 3. PS2-specific rendering still present

### 3.1 `CPS2Peek` is a profiler, not a renderer

`CPS2Peek` is a thin wrapper around **PS2Peek** by Stormfront Studios. It inserts VIF `MARK` packets into the current DMA stream for profiling, not geometry:

- Header notes explicitly say it is a profiler and that VIF marks only make sense **inside** `RwCameraBeginUpdate`/`EndUpdate` (`CPS2Peek.h:16-25`).
- `CPS2PEEK_INSERT_VIFMARK(MARK)` is a no-op unless both `GTA_PS2PEEK_PROFILER` and `GTA_PS2` are defined (`CPS2Peek.h:73-78`). In `FINAL`/`MASTER`, profiling support is disabled (`CPS2Peek.h:39-60`).
- `InsertVifMarkPkt()` builds `VIFCMD_FLUSH` / `VIFCMD_NOP` / `VIFCMD_MARK` and appends it to the DMA packet (`CPS2Peek.cpp:346-362`).
- `Initialise()` installs VBlank and Timer1 handlers only when the full profiler is enabled; under `GTA_PS2PEEK_VIFCODESONLY` it does nothing except assert the marker count (`CPS2Peek.cpp:241-288`).
- `Profile()` starts/stops `ClTimer1IntHandler` profiling; it does not draw (`CPS2Peek.cpp:370-399`).
- `VBlankInterruptHandler()` only advances profiler state (`CPS2Peek.cpp:410-442`).

**Conclusion:** `CPS2Peek` contains **no game render logic**. It is instrumentation. It is safe to strip/ignore on PC; it is not a source of PS2 visual behavior.

### 3.2 `ps2all.cpp` is real PS2 render logic

`ps2all.cpp` is a custom, optimized PS2 `ps2all` pipeline implementation. It is real PS2 rendering, not just data:

- **Lighting upload:** `ApplyAmbientLight()` / `ApplyDirectionalLight()` write colours, light types, and directions directly into the VU light packet (`_rwSkyLightFillPos`) (`ps2all.cpp:71-141`).
- **Per-atomic lighting setup:** `PS2AllApplyDirectionalLighting()` builds the inverse object matrix and appends ambient + directional + extra directional lights (`ps2all.cpp:148-193`).
- **Object setup callbacks:** `CustomPipelineObjSetupCB_RpAtomicPS2AllLightingSetup()` and the skinned variant (`ps2all.cpp:204-278`); `PS2AllObjectSetupCB()` does mesh cache, metrics, instancing, transform, frustum, prim type, material modulation, and lighting (`ps2all.cpp:324-377`); `PS2AllSkinnedObjectSetupCB()` adds bone matrix retrieval and VIF upload (`ps2all.cpp:467-541`).
- **Skinning:** `_rpSkinVIFTransferMatricesSize`, `_rpSkinDMATransferMatrices`, and `PS2AllSkinnedObjectSetupCB()` upload up to 64 bone matrices via VIF (`ps2all.cpp:387-447`, `524-539`).
- **Mesh bridge:** `RpMeshPS2AllVehicleBridgeCallBack()` does vehicle colour update, async texture upload, GIF tag, material colour, surface properties, clip info, texture state, VU1 code upload, and geometry kickoff (`ps2all.cpp:553-616`).
- **Pipeline fallback:** `PS2AllSetAtomicPipe()` selects `pOptimisedLightPipe` for non-skinned atomics and leaves CVB/CVSP pipes alone (`ps2all.cpp:637-652`); `PS2AllSetVehicleColourMaterialPipe()` sets `pVehicleColourMaterialPipe` unless it is a CVB material (`ps2all.cpp:661-676`).
- **Pipeline creation:** `PS2AllOptimizedPipelinesCreate()` creates `pOptimisedLightPipe`, `pOptimisedLightSkinnedPipe`, and `pVehicleColourMaterialPipe` from `RpPDSSkyObjPipeCreate` / `RpPDSSkyMatPipeCreate` (`Init_overlay.cpp:1456-1519`).
- **Per-frame lighting invalidation:** `PS2AllRecalculateLighting()` sets `bPS2AllSetupLighting = TRUE` so the next atomic re-uploads lighting (`ps2all.cpp:626-629`), called in the PS2 frame loop (`Main.cpp:2917`) and in `RenderReallyDrawLastObjects()` (`GtaPlugins.cpp:2086-2088`).

Some PS2All optimization paths are disabled or experimental:

- The cached-lighting early-out is guarded by `&& FALSE` (`ps2all.cpp:216`), so the persistent lighting cache is effectively dead.
- Default MatFX pipe callbacks are commented out to save memory (`Init_overlay.cpp:1511-1513`).
- `CVuCustomSkinPedRenderer` is referenced but its source is absent; the atomic path asserts if a skin ped atomic is loaded as an atomic (`AtomicModelInfo.cpp:132-137`).

### 3.3 `sky.cpp` is PS2 platform glue, not sky rendering

Despite the name, `sky.cpp` is the PS2 `ps.c` skeleton/platform layer:

- `psMouseSetVisibility`, `psMouseSetPos`, `psWindowSetText`, `psErrorMessage`, `psWarningMessage` (`sky.cpp:125-158`)
- `psCameraBeginUpdate()` / `psCameraShowRaster()` (`sky.cpp:160-172`)
- `psInitialize`, `psTerminate`, `psAlwaysOnTop`, `psGetMemoryFunctions`, `psInstallFileSystem` (`sky.cpp:714-750`)
- `psSelectDevice()` sets the PS2 video mode; it either finds a matching mode or calls `CPostEffects::SetUpVideoModeForEngine()` (`sky.cpp:752-782`).
- `psGrabScreen()` locks the camera raster and copies it to an `RwImage`; this is PS2-specific because PS2 camera rasters normally have no CPU-visible pixels (`sky.cpp:784-860`).
- `psNativeTextureSupport()` returns TRUE (`sky.cpp:862-865`).
- `main()` is entirely commented out (`sky.cpp:920-1001`).

There is **no sky geometry** here. Sky rendering is `CClouds::RenderSkyPolys()` in `DoRWStuffStartOfFrame_Horizon` (`Main.cpp:1163`) and `CClouds::Render()` (`Main.cpp:3232`).

### 3.4 PS2-specific state and GS hacks

PS2 bypasses RenderWare state tracking and writes GS registers directly in several places:

- `CPostEffects::ResetRwRenderState()` writes `SCE_GS_TEST_1`, `GS_ALPHA_1`, `GS_FOGCOL`, and `SCE_GS_ZBUF_1` into a GIF packet (`ScreenFX.cpp:3141-3163`).
- `DrawScreenIn32PixelStrips` emits `SCE_GS_PRIM`, `SCE_GS_RGBAQ`, `SCE_GS_UV`, `SCE_GS_XYZ2` for full-screen effects (`ScreenFX.cpp:3170-3193`).
- `CPostEffects::FinishRendering()` waits for DMA and emits `GS_TEXFLUSH` (`ScreenFX.cpp:2022-2042`).
- `CPostEffects::SetUpVideoModeForEngine()` sets the PS2 draw/display mode, 16-bit display, and `SCE_GS_SET_DTHE(1)` dithering (`ScreenFX.cpp:854-919`).
- PS2 water post colour is handled separately from PC/Xbox (`ScreenFX.cpp:2971-3009`).
- PS2 point-light fog sprites use `CVuTurboSprite` instead of `CSprite` (`PointLights.cpp:393-403`, `511-527`, `577-593`, `606-610`).

### 3.5 What would be needed to emulate the PS2 look on PC

The PS2 look is **not a set of flags**. It is a different rasterizer and a different lighting/mesh path. A faithful PC emulation would need:

1. **GS raster semantics**
   - 16-bit display path and dithering (`ScreenFX.cpp:869-871`, `895`).
   - GS alpha/blend/test/fog register behavior, including the direct `SCE_GS_*` writes (`ScreenFX.cpp:3141-3163`).
   - CLUT/8-bit texture paths where used (the old PS2 motion-blur code shows the expected CLUT setup, `ScreenFX.cpp:3199-3287`, though it is commented out).

2. **VU1 mesh + lighting equivalent**
   - Port the `ps2all` lighting packet layout (`ps2all.cpp:71-141`), per-atomic inverse-matrix lighting (`148-193`), mesh cache/frustum/prim-type setup (`324-377`), and skinning matrix upload (`387-447`, `467-541`) to PC vertex/pixel shaders.
   - Recreate `RpMeshPS2AllVehicleBridgeCallBack()` material/surface/texture/clip/VU1 sequencing (`ps2all.cpp:553-616`).

3. **The missing `CVu*` renderers**
   - `CVuCustomBuildingRenderer`, `CVuCarFXRenderer`, `CVuWaterRenderer`, `CVuTurboSprite`, `CVuCustomSkinPedRenderer`, and `VuGrassRenderer` are referenced by `Game.cpp:250-290`, `Init_overlay.cpp:756-813`, and `pipes/CustomBuildingRenderer.h:93-111`, but their source files are absent from this dump.
   - Their APIs include day/night balance (`CVuCustomBuildingRenderer::Get/SetDayNightBalanceParam`) and PS2-specific UV/CLUT/alpha behavior. They must be recreated, not merely recompiled.

4. **PS2 ordering**
   - Move `CWaterLevel::RenderWaterFog`, `CClouds::MovingFogRender`, and `CClouds::VolumetricCloudsRender` from the PC/Xbox `RenderEffects` position (`Main.cpp:3621-3640`) into the PS2 `RenderScene` position (`Main.cpp:3411-3430`).
   - Keep PS2's smokey-fog far-clip override (`Main.cpp:2928-2938`) and PS2-specific water colour (`ScreenFX.cpp:2971-3009`).
   - Recreate PS2 true-TS clipper selection and fast ped culling (`Init_overlay.cpp:550-552`; `Ped.cpp:9491-9505`).

5. **PS2 data**
   - DN/day-night vertex data and PS2 prelit tables must be exported in the same layout expected by the VU path. The PC DN path expects an extra vertex-colour plugin + prelit colours (`CustomBuildingRenderer.cpp:123-134`), while Xbox packs night colours into normals (`pipes/Xbox/CustomBuildingDNPipeline_Xbox.h:27-39`); PS2's expected packing is not in this dump.

In short: a PC "PS2 mode" is a **renderer+data port**, not just a shader permutation. `ps2all` gives a concrete template for the lighting/mesh side; the missing `CVu*` files are the largest gap.

---

## 4. Xbox-specific rendering

### 4.1 Source fork and gating

Xbox shares the `CCustomBuildingRenderer`/`CCarFXRenderer` class names but uses separate source files:

- `pipes/CustomBuildingRenderer.cpp` includes `CustomBuildingPipeline_Xbox.h` / `CustomBuildingDNPipeline_Xbox.h` under `#ifdef GTA_XBOX` (`pipes/CustomBuildingRenderer.cpp:15-26`).
- `pipes/Xbox/CustomCarEnvMapPipeline_Xbox.h` is explicitly D3D8 and errors if compiled for PC (`pipes/Xbox/CustomCarEnvMapPipeline_Xbox.h:1-16`).
- `Main.cpp` uses the same `Idle` for PC/Xbox but separate branches for features (`Main.cpp:3948`, `3972`, `4300-4303`, `4522`, `4628`).
- `RwCam.cpp` is compiled only for PC/PS2 (`RwCam.cpp:6`), so Xbox uses stock RW camera sizing rather than the custom `CameraSize` path.

### 4.2 Xbox shaders and all-in-one node

- Standard Xbox building pipe uses `RxNodeDefinitionGetXboxAtomicAllInOne()` (`pipes/Xbox/CustomBuildingPipeline_Xbox.cpp:918-926`), while PC uses `RxNodeDefinitionGetD3D9AtomicAllInOne()`.
- Xbox DN pipe uses `RxNodeDefinitionGetXboxAtomicAllInOne()` and sets `RxXboxAllInOneSetRenderCallBack(allinone, VShaderRenderCB)` (`pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:1441-1484`).
- `VShaderRenderCB()` switches to `D3DSCM_96CONSTANTS`, sets shader constants, assigns `DNVertexShader` to every mesh, and calls the default Xbox render callback (`pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:500-525`).
- DN vertex shader constants: c0-c3 = view/proj/world; c4 = `1-DN`; c5 = `DN` (`pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:453-498`).
- Xbox vehicle pipeline uses dedicated pixel shaders, e.g. `RwXboxSetCurrentPixelShader(EnvMapPixelShader)` for base-mapped env-map and a fallback no-base-texture shader (`pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1284-1294`), plus `RwXboxSetCurrentVertexShader(mesh->vertexShader)` (`1384`).
- Xbox uses `RwXboxSetCached*` state wrappers instead of raw D3D calls in many places (`pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1230`, `1309-1321`, `1438-1441`, `1544`).

### 4.3 Vertex colours and DN night colours

- Xbox vertex declarations include position, UV, normal, and vertex colour; the colour element is created when `rpGEOMETRYPRELIT` is set (`pipes/Xbox/CustomBuildingPipeline_Xbox.cpp:480-504`; upload `647-691`).
- DN is the distinctive Xbox behavior: **day colours live in the vertex colour, night colours live in the vertex normal**, and the DN vertex shader interpolates using `m_fCurrentDNValue` (`pipes/Xbox/CustomBuildingDNPipeline_Xbox.h:27-39`).
- The DN vertex shader declaration reflects that: register v1 is `NORMPACKED3` (night colour), register v2 is `D3DCOLOR` (day colour) (`pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:103-110`).
- Note the contradiction in the dump: `CDNGeomExtension` still declares `m_pNightColTable`/`m_pDayColTable` pointers (`pipes/Xbox/CustomBuildingDNPipeline_Xbox.h:33-39`), but the header comment says those tables do not exist on Xbox and the shipping path is the vertex shader (`pipes/Xbox/CustomBuildingDNPipeline_Xbox.h:27-31`). The table-interpolation code is guarded by `DN_USE_VERTEX_SHADER`/`GTA_PC_USE_DN_VS` and is not the Xbox shipping path (`pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:1512-1533`).
- PC prelit is often required for correct alpha blending (grass example: `pipes/PC_PlantsMgr_overlay.cpp:349-357`); Xbox can skip prelit for the same grass (`pipes/PC_PlantsMgr_overlay.cpp:349-352`), relying on vertex colour/state behavior.

### 4.4 Env map and vehicle specular

- Xbox env-map uses a custom pixel shader and cached texture-stage state (`pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1284-1310`).
- Specular uses a dedicated D3D8 light index (`CARFX_D3D8_SPEC_LIGHT_INDEX`), `D3DDevice_SetLight`, `D3DDevice_LightEnable`, and `D3DRS_SPECULARENABLE` (`pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1487-1502`); it is disabled per mesh after drawing (`1556-1557`).
- Alpha/vertex-alpha toggling uses `RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, ...)` plus `RwXboxSetCachedRenderState(D3DRS_SRCBLEND, ...)` (`pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1438-1441`).

### 4.5 Skinning

- Xbox skinning relies on the Xbox all-in-one node and a shader-cache workaround. `xboxallinone.c` overloads `XBoxSkinAtomicAllInOneNode` because RW 3.6 shaders did not restore constants used by other shaders, causing clipped-looking custom-shaded skinned peds (`pipes/Xbox/xboxallinone.c:1-9`).
- The node switches to 192 constants, purges the shader cache, computes proj/view/world, uploads per-atomic/bone constants, and restores 96-constant mode at the end (`pipes/Xbox/xboxallinone.c:238-421`).
- PC has no such path and instead normalizes skin weights in `CClumpModelInfo::SetClump` (`modelinfo/ClumpModelInfo.cpp:269-283`).

### 4.6 Fog, corona, water, and other frame-level differences

- **Fog**: global timecycle fog/far clip is common (`Main.cpp:4259-4260`; `DefinedState()` sets fog type/colour `handy.cpp:128-130`). PS2 adds water fog / moving fog / volumetric clouds inside `RenderScene` (`Main.cpp:3411-3430`); Xbox/PC add them inside `RenderEffects` (`Main.cpp:3621-3640`). `CPostEffects::Fog()` itself uses the immediate-mode save/restore path and PS2-only draw dimensions (`ScreenFX.cpp:5461-5480`).
- **Corona**: the call order is common; `CCoronas::RenderReflections()`/`RenderSunReflection()` are in `RenderScene`, `CCoronas::Render()` in `RenderEffects` (`Main.cpp:3266`, `3394`, `3595`). No separate Xbox corona implementation is present in this dump.
- **Water**: the common `CWaterLevel::RenderWater()` is used on all platforms (`Main.cpp:3305`, `3406`); PS2 additionally has `CVuWaterRenderer` initialization (`Init_overlay.cpp:756-762`) and a PS2-only underwater colour/ripple branch (`ScreenFX.cpp:2971-3009`). No Xbox-specific water renderer exists in this dump.
- **Photos**: PS2/Xbox save the screen to a memory buffer; PC saves to `Gallery\*.jpg` (`ScreenFX.cpp:3051-3113`).
- **Weapon peds**: PC/Xbox defer weapon rendering to fix muzzle-flash alpha (`Main.cpp:4300-4303`; `GtaPlugins.cpp:521-615`). PS2 does not use this block.

---

## 5. Render-state doctrine

### 5.1 The doctrine in one sentence

The original code does **not** have a general render-state stack. It relies on:

1. a canonical reset (`DefinedState`) at frame/pass boundaries,
2. per-pass state setup with hardcoded restoration,
3. one symmetric save/restore helper for immediate-mode post-effects,
4. and "caller beware" leaks in a few effect passes.

### 5.2 Canonical reset helpers

`DefinedState()` (`handy.cpp:116-138`) sets:

- `rwRENDERSTATETEXTUREADDRESS` = WRAP
- `rwRENDERSTATETEXTUREPERSPECTIVE` = TRUE
- `rwRENDERSTATEZTESTENABLE` = TRUE
- `rwRENDERSTATEZWRITEENABLE` = TRUE
- `rwRENDERSTATESHADEMODE` = GOURAUD
- `rwRENDERSTATETEXTUREFILTER` = LINEAR
- `rwRENDERSTATEVERTEXALPHAENABLE` = FALSE
- `rwRENDERSTATESRCBLEND` = SRCALPHA
- `rwRENDERSTATEDESTBLEND` = INVSRCALPHA
- `rwRENDERSTATEBORDERCOLOR` = black
- `rwRENDERSTATEFOGENABLE` = FALSE
- `rwRENDERSTATEFOGCOLOR` = timecycle fog colour
- `rwRENDERSTATEFOGTYPE` = LINEAR
- PC/Xbox only: `rwRENDERSTATECULLMODE` = NONE, `rwRENDERSTATEALPHATESTFUNCTION` = GREATER, `rwRENDERSTATEALPHATESTFUNCTIONREF` = 2

`DefinedState2d()` (`handy.cpp:140-160`) is similar but disables perspective, Z-test, and Z-write for 2D.

`Render2dStuff()` performs its own 2D state reset: Z-test off, Z-write off, vertex alpha on, SRCALPHA/INVSRCALPHA, fog off, and PC/Xbox cull NONE (`Main.cpp:3732-3740`).

### 5.3 The one full save/restore: immediate-mode post-effects

`CPostEffects::ImmediateModeRenderStatesStore()` / `Set()` / `ReStore()` store and restore exactly **10** states (`ScreenFX.cpp:1747-1796`):

1. `rwRENDERSTATESRCBLEND`
2. `rwRENDERSTATEDESTBLEND`
3. `rwRENDERSTATEFOGENABLE`
4. `rwRENDERSTATECULLMODE`
5. `rwRENDERSTATEZTESTENABLE`
6. `rwRENDERSTATEZWRITEENABLE`
7. `rwRENDERSTATESHADEMODE`
8. `rwRENDERSTATEVERTEXALPHAENABLE`
9. `rwRENDERSTATETEXTUREADDRESS`
10. `rwRENDERSTATETEXTUREFILTER`

`Set()` forces `SRCALPHA/INVSRCALPHA`, fog off, cull NONE, Z-test off, Z-write off, Gouraud, vertex alpha on, clamp, nearest (`ScreenFX.cpp:1765-1778`). Calls are paired across many effects (`ScreenFX.cpp:2145-2158`, `2167-2207`, `2222-2308`, `3315-3332`, `3503-3516`, `3684-3791`, `3980-4056`, `5274-5286`, etc.). `SeamRemoverBegin/End` use this pair around the full-screen Z-reset (`ScreenFX.cpp:2138-2207`, `2210-2514`).

### 5.4 Ad hoc per-pass save/restore

| Helper / pass | States handled | Behavior | Evidence |
|---------------|----------------|----------|----------|
| `CVisibilityPlugins::RenderReallyDrawLastObjects` | texture = NULL, Z-test on, Z-write on, vertex alpha on, SRCALPHA/INVSRCALPHA, fog on, cull NONE | Leaves all but fog changed; only fog is restored to FALSE | `GtaPlugins.cpp:2077-2094` |
| `RenderBoatAlphaAtomics` | cull mode | Sets NONE then restores hardcoded BACK | `GtaPlugins.cpp:1868-1871` |
| `RenderEntity` | Z-write, alpha-test ref, cull mode | Saves only by setting known values; restores `ZWRITEENABLE` to TRUE and cull to BACK; alpha-test ref is set to 100/0, not saved | `GtaPlugins.cpp:1894-1958` |
| `RenderWeaponPedsForPC` | Z-test, Z-write, fog, blend, alpha-test ref | Sets values; does **not** restore them | `GtaPlugins.cpp:534-541` |
| `CAutomobile::Render` | alpha-test ref | Saves with `RwRenderStateGet`, sets 1, restores at end | `Automobile.cpp:8067-8069`, `8139` |
| `CPed::Render` (player only) | alpha-test ref | Saves with `RwRenderStateGet`, sets 1, restores at end | `Ped.cpp:9452`, `9708` |
| `CPed::Render` (PS2) | true-TS clipper | Saves `RpSkyGetTrueTSClipper()`, disables for non-player peds, restores | `Ped.cpp:9491-9505`, `9710-9714` |
| `CPointLights::RenderFogEffect` | fog, Z-write, Z-test, vertex alpha, SRC/DEST blend, texture | Sets all; the restore block is **commented out** | `PointLights.cpp:379-391`, `613-618` |

### 5.5 Raw device vs `RwRenderStateSet`

- **Game/gameplay code** uses `RwRenderStateSet` almost exclusively. This keeps RenderWare's internal state cache authoritative. Examples: `DefinedState` (`handy.cpp:118-135`), `RenderScene` (`Main.cpp:3217-3241`), `Render2dStuff` (`Main.cpp:3732-3740`), visibility callbacks (`GtaPlugins.cpp:1868-2094`), post-effects (`ScreenFX.cpp:1749-1795`).
- **PC/Xbox custom pipelines** use raw D3D calls because they are inside `RxD3D9AllInOne`/`RxXboxAllInOne` callbacks:
  - `RwD3D9GetRenderState(D3DRS_LIGHTING, &bLightingEnabled)` before enabling surface properties (`pipes/PC/CustomBuildingPipeline.cpp:770-771`; `pipes/PC/CustomCarEnvMapPipeline.cpp:1259-1260`).
  - `RwD3D9SetRenderState`, `RwD3D9SetTextureStageState`, `RwD3D9SetTexture`, `RwD3D9SetVertexDeclaration` (`pipes/PC/CustomBuildingPipeline.cpp:780-802`, `845-861`, `898-906`).
  - After changing `D3DRS_AMBIENT` / `D3DRS_COLORVERTEX` / `D3DRS_EMISSIVEMATERIALSOURCE`, they call `CustomD3D9Pipeline_RwSetSurfacePropertiesCacheResync()` to re-sync RW's cached material state (`pipes/PC/CustomPipelinesCommon.cpp:180-303`; call site `pipes/PC/CustomCarEnvMapPipeline.cpp:1580-1581`).
  - Xbox uses `RwXboxSetCachedRenderState`, `RwXboxSetCachedTextureStageState`, and `RwXboxSetCurrentVertexShader` to keep its cache coherent (`pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1230`, `1309-1321`, `1438-1441`, `1544`).
- **PS2** bypasses both and writes GS registers directly through DMA:
  - `CPostEffects::ResetRwRenderState()` (`ScreenFX.cpp:3141-3163`)
  - `CPostEffects::FinishRendering()` (`ScreenFX.cpp:2022-2042`)
  - 32-pixel strip full-screen draws (`ScreenFX.cpp:3170-3193`)
  - `ps2all` VIF lighting/skin/mesh uploads (`ps2all.cpp:71-141`, `387-447`, `553-616`)

### 5.6 Practical implication

If you insert a custom pass between original passes, assume:

- Z-test/Z-write, cull, vertex alpha, and blend may be left in an arbitrary state by `CPointLights::RenderFogEffect`, `RenderReallyDrawLastObjects`, or `RenderWeaponPedsForPC`.
- The only safe canonical reset is `DefinedState()`; the post-effect path expects its own 10-state save/restore pair.
- `CPostEffects::Render()` assumes the camera update is active (PC/Xbox) or will end/begin it itself (PS2), and after `RenderEffects()` the front buffer contains the post-processed scene.
- Any hook that changes raw D3D state outside `RwRenderStateSet` must either restore it or call the relevant cache-resync helper before returning to RW code.

---

## 6. Lighting

### 6.1 Global light objects

`LightsCreate()` creates:

- `pAmbient` = `RpLightCreate(rpLIGHTAMBIENT)`, flags `rpLIGHTLIGHTATOMICS`, initial colour (0.25, 0.25, 0.20) (`Init_overlay.cpp:1612-1618`).
- `pDirect` = `RpLightCreate(rpLIGHTDIRECTIONAL)`, flags `rpLIGHTLIGHTATOMICS`, initial colour (1.0, 0.85, 0.45), radius 2.0, frame rotated 160° (`Init_overlay.cpp:1621-1638`).
- Six extra directional lights `pExtraDirectionals[0..5]`, initially flags 0, added to the world but not lighting until used (`Init_overlay.cpp:1640-1662`; declaration `lights.c:50`).

`LightsDestroy()` removes/destroys all of them (`lights.c:169-202`).

### 6.2 Per-frame colour update

`SetLightsWithTimeOfDayColour()` (`lights.c:60-159`) is called every frame before building the render list:

- Ambient building colour = `CTimeCycle::GetAmbientRed/Green/Blue()` × `CCoronas::LightsMult` (`lights.c:77-79`).
- Ambient peds/cars/objects = `CTimeCycle::GetAmbientRed_Obj/Green_Obj/Blue_Obj()` × `CCoronas::LightsMult` (`lights.c:90-92`).
- Directional colour = `CTimeCycle::GetDirectionalRed/Green/Blue()` × `CCoronas::LightsMult` (`lights.c:112-114`).
- Lightning flash overrides ambient to white (`lights.c:95-103`).
- Directional light orientation is derived from `CTimeCycle::m_vecDirnLightToSun`, transformed into a frame matrix (`lights.c:118-136`).
- `CCoronas::LightsMult` is therefore a global multiplier on both ambient and directional lighting; it is not a separate light.

Frame call sites:

- PS2: `Main.cpp:2875`
- PC/Xbox: `Main.cpp:4152`
- Model viewer: `Main.cpp:2661`

Scorched/vehicle-blow-up state is handled by `WorldReplaceNormalLightsWithScorched()` / `WorldReplaceScorchedLightsWithNormal()` (`lights.c:205-220`). The directional light flag `rpLIGHTLIGHTATOMICS` is cleared for scorched lighting; vehicle pipes check that flag to decide whether to draw blown-up atomics (`pipes/PC/CustomCarEnvMapPipeline.cpp:1265-1275`).

### 6.3 Ambient/directional helper functions

| Function | Purpose | Evidence |
|----------|---------|----------|
| `SetAmbientColours()` | Set ambient to building/road ambient | `lights.c:537-540` |
| `SetAmbientColours(RwRGBAReal*)` | Set ambient to explicit colour | `lights.c:547-550` |
| `SetDirectionalColours(RwRGBAReal*)` | Set directional to explicit colour | `lights.c:552-555` |
| `SetAmbientAndDirectionalColours(float)` | Scale both by multiplier | `lights.c:393-404` |
| `SetLightColoursForPedsCarsAndObjects(float)` | Use object ambient + directional, plus brightness add | `lights.c:557-577` |
| `SetBrightMarkerColours(float)` | Flat marker lighting | `lights.c:458-474` |
| `SetFullAmbient()` | Full white ambient | `lights.c:527-530` |
| `ActivateDirectional()` / `DeActivateDirectional()` | Toggle `rpLIGHTLIGHTATOMICS` on `pDirect` | `lights.c:493-506` |
| `SetFlashyColours` / `_Mild` | Scripted flash effects | `lights.c:411-456` |

`RenderReallyDrawLastObjects()` explicitly uses the flat path: `SetAmbientColours(); DeActivateDirectional();` (`GtaPlugins.cpp:2089-2090`).

### 6.4 How lighting reaches buildings, vehicles, peds

**Buildings**

- The standard PC/Xbox building pipe uses the global RW lights and `RwD3D9SetSurfaceProperties()` with the material's `surfaceProps` and colour (`pipes/PC/CustomBuildingPipeline.cpp:878-888`).
- The PC/Xbox DN building pipe has an explicit lighting callback that copies `pAmbient` into `AmbientSaturated` and enables the directional light via `_rwD3D9LightDirectionalEnable(pDirect)` (`pipes/PC/CustomBuildingDNPipeline.cpp:1020-1037`; same pattern in `pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:1021-1038`).
- DN buildings bypass the normal per-frame light colour and interpolate day/night vertex colours (`CCustomBuildingRenderer::UpdateDayNightBalanceParam()` at `pipes/CustomBuildingRenderer.cpp:194-259`; `UpdateAtomicPrelitLum()` at `pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:1512-1575`). `CCustomBuildingRenderer::Update()` is called every frame (`Game.cpp:1833-1835`).
- PS2 buildings use `CVuCustomBuildingRenderer` + `ps2all`; lighting is uploaded to VU (`ps2all.cpp:148-193`).

**Vehicles**

- Vehicle atomics use the same `pAmbient`/`pDirect` RW lights, plus extra directional lights generated from point lights.
- `CCarFXRenderer` env-map/specular passes add a dedicated specular light (`CARFX_D3D9_SPEC_LIGHT_INDEX` / `CARFX_D3D8_SPEC_LIGHT_INDEX`) per material (`pipes/PC/CustomCarEnvMapPipeline.cpp:1356-1368`; Xbox `pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1487-1502`).
- Vehicle material colours are remapped by `CVehicleModelInfo::UpdateVehicleColour()` in the PS2 bridge (`ps2all.cpp:553-560`) and by the PC pipe material setup.
- `SetLightColoursForPedsCarsAndObjects()` exists for the object/vehicle/ped ambient variant (`lights.c:557-577`), although its callers are outside this dump.

**Peds**

- Per-entity lighting is performed by `CEntity::SetupLighting()` / `RemoveLighting()` around the render call (call sites `GtaPlugins.cpp:1917`, `1924`; weapon peds `GtaPlugins.cpp:561`). The implementation is not in this dump.
- `CPointLights::GenerateLightsAffectingObject()` is explicitly described as generating lights "that affect" cars/peds later on (`PointLights.cpp:5-7`, `141-243`).
- `CPed::Render()` applies vision-FX light overrides around `CEntity::Render()`: `InfraredVisionStoreAndSetLightsForHeatObjects()`, `NightVisionSetLights()`, then restore (`Ped.cpp:9498-9511`; definitions `ScreenFX.cpp:3483`, `3802-3874`).
- Player ped alpha-test ref is saved/restored (`Ped.cpp:9452`, `9708`).

### 6.5 Point lights → extra directionals

- `CPointLights::AddLight()` stores a light only if it is within `MAX_DIST_POINT_LIGHT + Range` of the camera; intensity fades beyond 75% of that range (`PointLights.cpp:88-137`). `MAX_DIST_POINT_LIGHT` is 15 m (`PointLights.cpp:88`).
- `CPointLights::GenerateLightsAffectingObject()` iterates the stored lights, computes distance/angle/intensity, handles `PLTYPE_ANTILIGHT` as a multiplier, and calls `AddAnExtraDirectionalLight()` (`PointLights.cpp:149-243`).
- `AddAnExtraDirectionalLight()` selects the weakest slot, sets colour/direction, and enables the light (`lights.c:266-328`).
- `RemoveLightsAffectingObject()` calls `RemoveExtraDirectionalLights()` (`PointLights.cpp:327-330`; `lights.c:335-344`).
- `CPointLights::InitForFrame()` resets the list at the top of each frame (`Main.cpp:4066`, `2759`); its implementation is not in this dump.
- PS2 uploads extra directionals in the `ps2all` lighting packet: `for(i=0; i<NumExtraDirLightsInWorld; i++) ApplyDirectionalLight(pExtraDirectionals[i])` (`ps2all.cpp:178-182`).

### 6.6 Night/day vertex-colour path

Three distinct mechanisms:

1. **Whole-model swap**: `CTimeInfo::FindOtherTimeModel()` swaps `_nt` / `_dy` model names at load (`modelinfo/AtomicModelInfo.cpp:396-450`).
2. **DN prelit interpolation (PC/Xbox)**: `CCustomBuildingRenderer::UpdateDayNightBalanceParam()` computes `fBDN` from `CClock::GetMinutesSinceMidnight()` with 6:00→7:00 and 20:00→21:00 fades (`pipes/CustomBuildingRenderer.cpp:194-259`); `UpdateAtomicPrelitLum()` interpolates the day/night colour tables into `RpGeometryGetPreLightColors()` (`pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:1512-1575`). `CCustomBuildingRenderer::Update()` runs each frame (`Game.cpp:1833-1835`).
3. **Xbox DN vertex shader**: day colour in vertex colour, night colour in normal, shader interpolates using `m_fCurrentDNValue` (`pipes/Xbox/CustomBuildingDNPipeline_Xbox.h:27-39`; declaration `CustomBuildingDNPipeline_Xbox.cpp:103-110`; constants `453-498`; render callback `500-525`).

PS2 also exposes a day/night balance API (`cbrGetDayNightBalanceParam()` / `cbrSetDayNightBalanceParam()` in `pipes/CustomBuildingRenderer.h:93-111`), but the `CVuCustomBuildingRenderer` implementation is absent from this dump.

### 6.7 Vision FX lighting overrides

- `CPostEffects::NightVisionSetLights()` sets lights for night vision (`ScreenFX.cpp:3483-3494`).
- `SetLightsForInfraredVisionHeatObjects()` sets ambient/direct to the heat colour (`ScreenFX.cpp:3802-3808`).
- `InfraredVisionStoreAndSetLightsForHeatObjects()` / `RestoreLightsForHeatObjects()` save and restore the two light colours (`ScreenFX.cpp:3810-3854`).
- `InfraredVisionSetLightsForDefaultObjects()` uses blue for cold objects (`ScreenFX.cpp:3855-3872`).
- These are applied per ped in `CPed::Render()` when `CPostEffects::IsVisionFXActive()` (`Ped.cpp:9498-9511`).

---

## 7. Naming conventions

The subsystem follows a consistent in-house style:

### 7.1 Classes

- `C`-prefixed classes: `CGame`, `CPostEffects`, `CPointLights`, `CVisibilityPlugins`, `CCustomBuildingRenderer`, `CCustomBuildingPipeline`, `CCustomCarEnvMapPipeline`, `CPS2Peek`, `CModelInfoAccelerator`.
  - Examples: `Game.cpp:525` (`CGame::Initialise`); `ScreenFX.cpp:2520` (`CPostEffects::Render`); `PointLights.cpp:353` (`CPointLights::RenderFogEffect`); `GtaPlugins.cpp:1510` (`CVisibilityPlugins::SetAtomicRenderCallback`); `pipes/CustomBuildingRenderer.h:27`; `CPS2Peek.h:263`.
- `CVu*` prefix for PS2 VU renderers: `CVuCustomBuildingRenderer`, `CVuCarFXRenderer`, `CVuWaterRenderer`, `CVuTurboSprite`, `CVuCustomSkinPedRenderer` (`Game.cpp:250-290`; `Init_overlay.cpp:756-813`).
- `_Xbox` / `_Xbox` suffix for platform forks in file names (`pipes/Xbox/CustomBuildingPipeline_Xbox.cpp`, `CustomBuildingDNPipeline_Xbox.h`, `CustomCarEnvMapPipeline_Xbox.h`).

### 7.2 Members and globals

- Instance members: `m_` prefix. Examples: `m_pRwObject`, `m_num2dEffects`, `m_modelName` (`modelinfo/AtomicModelInfo.cpp:58-79`); `m_bNightVision`, `m_bInfraredVision`, `m_bSpeedFX` (`ScreenFX.cpp:138-220`); `m_bCreateDamagedVersion` (`AtomicModelInfo.cpp:41`).
- Static members: `ms_` prefix. Examples: `ms_atomicPluginOffset`, `ms_pCamera`, `ms_vehicleLod0Dist`, `ms_alphaEntityList` (`GtaPlugins.cpp:115-135`); `ms_imf`, `ms_Highlight...` in `ScreenFX.cpp`.
- Booleans: `b` or `m_b` prefix. Examples: `bPS2AllSetupLighting`, `bLastGeomLit` (`ps2all.cpp:37-38`); `m_bFileFound`, `m_bHasRun` (`ModelInfoAccel.h:33-35`); `bLightingEnabled`, `forceBlack` (`pipes/PC/CustomBuildingPipeline.cpp:756-758`); `m_bDeviceSupportsVS11` (`pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:80`).
- Function-local booleans: `bInstanceXYZ`, `bInstanceUV`, `bInstanceNormal`, `bInstanceRGBA` (`pipes/Xbox/CustomBuildingPipeline_Xbox.cpp:190-193`).
- Global pointers: `p` prefix (`pAmbient`, `pDirect`, `pExtraDirectionals`, `pOptimisedLightPipe`, `pVehicleColourMaterialPipe`) (`lights.c:46-50`; `ps2all.cpp:56-58`).
- Other globals: `g_` prefix (`g_realTimeShadowMan`, `g_fx`, `g_breakMan`, `g_interiorMan`, `g_procObjMan`, `g_waterCreatureMan`) (`Game.cpp:1406`, `1912`, `1926`, `1938`, `1952`, `1965`).

### 7.3 Accessors and verbs

- Accessors are `Get*` / `Set*`, usually inline in headers. Examples:
  - `GetDontWriteZBuffer()` / `SetDontWriteZBuffer()` (`modelinfo/BaseModelInfo.h:341-342`)
  - `GetHasPreRenderEffects()` / `SetHasPreRenderEffects()` (`BaseModelInfo.h:379-380`)
  - `SetDayNightBalanceParam()` / `GetDayNightBalanceParam()` (`pipes/CustomBuildingRenderer.h:53-54`)
  - `SetPipe...` / `GetFxEnv...` in the custom pipelines (`pipes/PC/CustomBuildingPipeline.cpp:989-1166`)
- Render/update verbs are PascalCase and descriptive:
  - `RenderScene`, `RenderEffects`, `Render2dStuff`, `RenderMenus`, `RenderDebugShit` (`Main.cpp:3194`, `3562`, `3730`, `3876`, `3446`)
  - `PreRender`, `PreRenderWater`, `PreRenderUpdate` (`Main.cpp:4211`, `Game.cpp:2010`, `pipes/CarFXRenderer.cpp:87`)
  - `ConstructRenderList`, `RenderEverythingBarRoads`, `RenderRoads`, `RenderFadingInEntities` (`Main.cpp:4200`, `3270`, `3262`, `3312`)
  - `SetupLighting`, `RemoveLighting`, `SetLightsWithTimeOfDayColour` (`GtaPlugins.cpp:1917`, `lights.c:60`)
  - `Initialise`, `Shutdown`, `Init`, `Update`, `Process`.

### 7.4 Enums and constants

- ALLCAPS enum values with subsystem prefix:
  - `MI_TYPE_*` (`BaseModelInfo.h:69-80`)
  - `FLAG_*`, `MODEL_*` (`BaseModelInfo.h:132-198`)
  - `VEHICLE_TYPE_*`, `VEHICLE_ATOMIC_*` (`VehicleModelInfo.h:169-224`)
  - `CPS2PEEK_VIFMARK_*` (`CPS2Peek.h:96-253`)
  - `PDSPC_*`, `PDS_*` pipeline IDs (`pipes/PC/CustomBuildingPipeline.h`, `pipes/Xbox/CustomBuildingDNPipeline_Xbox.h:22`)
  - `CARFX_*_PASS_BIT` (`pipes/Xbox/CustomCarEnvMapPipeline_Xbox.h:36-44`)
- Local constants are often ALLCAPS macros or `static const`: `MAX_DIST_POINT_LIGHT` (`PointLights.cpp:88`), `EFFECTSIZEFRONT` (`PointLights.cpp:341`), `DN_NUM_FRAMES_FOR_PROCESS` (`pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:68`), `MIN_FRAME_TIME` (`Main.cpp:3957`).

### 7.5 File naming

- PascalCase class-per-file names: `Game.cpp`, `PointLights.cpp`, `ScreenFX.cpp`, `CPS2Peek.cpp`, `RwCam.cpp`, `GtaPlugins.cpp`.
- Lowercase legacy C/RenderWare/platform files: `lights.c`, `sky.cpp`, `ps2all.cpp`, `ps2dma.cpp`, `handy.cpp`, `setup.c`.
- Platform forks use `_Xbox` suffix: `CustomBuildingPipeline_Xbox.cpp`, `CustomBuildingDNPipeline_Xbox.cpp`, `CustomCarEnvMapPipeline_Xbox.cpp`.
- Overlay/addition files use `_overlay`: `Init_overlay.cpp`, `PC_PlantsMgr_overlay.cpp`.
- Metadata definitions use `.psc`: `modelinfo/VehicleModelInfoFlags.psc`, `VehicleModelInfoMetaData.psc`, `PedModelInfoData.psc`.
- Header comments include filename, creator, and often a dated change log (e.g. `pipes/CustomBuildingRenderer.cpp:1-8`, `CPS2Peek.h:3-8`).

### 7.6 Comment style

Functions generally have a box comment with `name:` and `description:` above them, e.g.:

- `// name: RenderFogEffect` / `// PURPOSE : Render cool sprites.` (`PointLights.cpp:335-340`)
- `// name: Initialise` / `// description: Initialises game` (`Game.cpp:523-524`)
- `// name: RenderEffects` (implicit) and `// end of RenderEffects()` (`Main.cpp:3728`)

This style is useful for locating original intent when matching hooks: the comment immediately above a function usually states whether it is a render pass, an update, or a helper.

---

## 8. Diff vs our mod (hook-order audit)

The following uses only the hook list supplied for our mod: `RenderScene`, `RenderEffects`, `CPostEffects::Render`, `DrawFinalEffects`, `CCoronas::Render`, `CPointLights::Render`, `CWaterLevel::Render`, `CShadows::Render*`. No deep read of our repo was performed.

| Our hook | Original counterpart | Original location / order | Divergence risk |
|----------|----------------------|---------------------------|-----------------|
| `RenderScene` | Global `RenderScene()` | `Main.cpp:3194-3439`; called at `Main.cpp:4287` (PC/Xbox) and `Main.cpp:2973` (PS2), **after** `CMirrors::RenderMirrorBuffer()` (`4269`/`2967`) and **before** `RenderDebugShit()` and `RenderEffects()` | There is no `CGame::Render` in this source. If our hook is installed on a symbol found by pattern/address rather than the actual global `RenderScene`, verify. If we wrap/replace it, we must preserve `SeamRemoverBegin/End` (`3259`/`3275`), the water cull wrapper (`3304-3306`, `3405-3407`), the mirror gate for shadows (`3316`), and the PS2-only fog/cloud tail (`3411-3430`). |
| `RenderEffects` | Global `RenderEffects()` | `Main.cpp:3562-3728`; called at `4305`/`2994`, **after** `RenderDebugShit()` and PC/Xbox weapon-ped pass, **before** motion blur and `Render2dStuff()` | This function already contains `CPostEffects::Render()` at `3723`. If our `RenderEffects` hook also calls our own post-FX, we risk double post-processing. It also contains `CCoronas::Render()` and `CPointLights::RenderFogEffect()`; hooking those separately inside `RenderEffects` must not duplicate them. |
| `CPostEffects::Render` | `CPostEffects::Render()` | `ScreenFX.cpp:2520-3132`; called from `RenderEffects()` at `Main.cpp:3723` | It is **not** a standalone post pass: PC ends the camera update and copies the front buffer (`2531-2544`); Xbox uses `RasterCopyPostEffects()` (`2545-2548`); PS2 ends/restarts the camera (`2609-2611`, `3126-3131`). Calling it outside the original camera-update window can corrupt the copy. Hooking it separately while also hooking `RenderEffects` can double-run it. |
| `DrawFinalEffects` | **Not present in this dump** | No `DrawFinalEffects`/`FinalEffects` symbol or call site in `Main.cpp`, `ScreenFX.cpp`, or `Game.cpp` | Cannot validate order from this source. The nearest original frame tail is `RenderEffects()` → `RenderMotionBlur()` → `Render2dStuff()` → `RenderMenus()` → `DoFade()` → `Render2dStuffAfterFade()` (`Main.cpp:4305-4387`). If our `DrawFinalEffects` is meant to run after `CPostEffects::Render`, it must run before HUD (`Render2dStuff`), or it will affect HUD/2D incorrectly. If it is meant to run after HUD, it differs from all original post-FX behavior. Verify against the binary/address source. |
| `CCoronas::Render` | `CCoronas::Render()` | `Main.cpp:3595`, inside `RenderEffects`, **after** `RenderReallyDrawLastObjects()` (`3590`) and **before** `g_fx.Render()` (`3604`), water cannons (`3615`), point-light fog (`3686`), and post-FX (`3723`) | If our hook runs coronas earlier (e.g. inside `RenderScene`), they draw before water/fading/shadows and before the really-draw-last list. Original reflections/sun corona are separate calls inside `RenderScene` (`3266`, `3394`). A single `CCoronas::Render` hook does not cover those. |
| `CPointLights::Render` | **No `CPointLights::Render` in this dump**; only `RenderFogEffect()` and `RenderSmogEffect()` | `CPointLights::RenderFogEffect()` is called at `Main.cpp:3686` inside `RenderEffects`; `RenderSmogEffect()` is commented out (`3710`); an old `RenderFogEffect` body is fully commented out (`PointLights.cpp:763-913`) | Name mismatch: original has no `Render` entry point. `RenderFogEffect()` intentionally leaves fog/Z/blend/texture state dirty (restore commented, `PointLights.cpp:613-618`); a wrapper/replacement must either reproduce that leak or restore state carefully. If our hook is on the actual fog effect, it must not run before additive corona/FX passes that expect the original state. |
| `CWaterLevel::Render` | **No `CWaterLevel::Render` in this dump**; the entry points are `RenderWater()`, `RenderWaterFog()`, `PreRenderWater()` | `RenderWater()` at `Main.cpp:3305` (not underwater) and `3406` (underwater), both inside `RenderScene`; `RenderWaterFog()` at `3417` (PS2) or `3627` (PC/Xbox); `PreRenderWater()` at `Game.cpp:2010` | Name mismatch. If our hook is an alias for `RenderWater`, preserve the `!bUnderWater` vs `bUnderWater` two-call structure and the cull NONE/BACK wrapper (`3304-3306`, `3405-3407`). If it replaces water entirely, PS2 fog placement and reflections (`CCoronas::RenderReflections`, `RenderSunReflection`) may break. A `CWaterLevel::Render` symbol may exist only in the mod, not the original. |
| `CShadows::Render*` | `CShadows::RenderStaticShadows()` / `RenderStoredShadows()` | `Main.cpp:3344` / `3350`, inside `RenderScene`, **only if `!CMirrors::bRenderingReflection`** (`3316`); `UpdateStaticShadows()` immediately before (`3343`); also `UpdatePermanentShadows()` in `CGame::Process` (`Game.cpp:1808`) | If our shadow hook does not check `CMirrors::bRenderingReflection`, shadows will be drawn into the mirror/reflection pass. If it calls `RenderStoredShadows()` without `UpdateStaticShadows()` first, stored shadows may lag by a frame. The original shadow block also applies a near-clip bias around both calls (`3319-3339`, restore `3354-3358`); skipping it can cause Z-fighting. |

Additional structural differences to check:

1. **Hook naming vs original symbols**
   - `CGame::Render`, `CWaterLevel::Render`, `CPointLights::Render`, and `DrawFinalEffects` do not exist in this dump under those names. This is the most important compatibility risk: our hook names may be wrappers or may target a different build. Verify each address against the original binary before assuming the ordering in this document applies.

2. **Original post-FX is inside `RenderEffects`**
   - `CPostEffects::Render()` is called at `Main.cpp:3723`, i.e. the last step of `RenderEffects()`. If our mod hooks `RenderEffects` and `CPostEffects::Render` independently, any custom geometry drawn in the `RenderEffects` hook after the original call can end up **after** post-processing but **before** HUD, which is a different visual lane from the original scene.
   - Our `DrawFinalEffects` hook, if it is the SA `DrawFinalEffects` at `0x53EBE9`, likely sits in this tail too (not in this source). Its placement relative to `CPostEffects::Render` and `Render2dStuff()` is the single highest-risk ordering question.

3. **Original `RenderScene` is one atomic unit**
   - It includes `SeamRemoverBegin/End`, roads, reflections, everything-bar-roads, water, fading entities, shadows, plants, rain/sun, and PS2 fog/clouds. Replacing it entirely means re-implementing all of those; hooking only its start/end means the original still does all of them, but our added passes land at the boundaries.
   - The original `RenderScene` is called inside the camera update begun in `DoRWStuffStartOfFrame_Horizon()` (`Main.cpp:1154`) and ended in `DoRWStuffEndOfFrame()` (`Main.cpp:1357`); it must not call `RwCameraBeginUpdate/EndUpdate` unless it restores the state.

4. **Pipeline selection is load-time, not render-time**
   - `CCustomBuildingRenderer`/`CCarFXRenderer`/`PS2All` selection happens in `CAtomicModelInfo::SetAtomic()`/`CClumpModelInfo::SetClump()` (`AtomicModelInfo.cpp:89-143`; `ClumpModelInfo.cpp:135-186`). A PBR renderer that only hooks `RenderScene` will not change which pipeline an already-loaded model uses. To override pipelines, hook the load-time setup (`SetAtomic`/`SetClump`) or replace the pipeline at `RpAtomicSetPipeline`/`RpAtomicRender` level, or re-run setup after model load.
   - The original pipeline IDs (`PDSPC_CBuildingEnvMap_AtmID`, `PDSPC_CBuildingDNEnvMap_AtmID`, `PDSPC_CarFXPipe_AtmID`, Xbox `PDS_CustomXBoxColourNormal_AtmID`) are the canonical way the game recognises a model as building/car/DN. Our mod should preserve or intentionally replace those IDs if it relies on `IsCBPCPipelineAttached`-style checks.

5. **Lighting upload timing**
   - `SetLightsWithTimeOfDayColour()` runs **before** `CRenderer::ConstructRenderList()` (`Main.cpp:4152` vs `4200`), and `PS2AllRecalculateLighting()` runs **after** `CRenderer::PreRender()` (`Main.cpp:2917`). If our PBR shaders upload ambient/directional constants in `RenderScene`, they must use the values from this frame's `pAmbient`/`pDirect`, not a stale copy. Our `pipeUploadPBR()` equivalent should be fed after `SetLightsWithTimeOfDayColour()`.
   - DN balance is updated in `CGame::Process` via `CCustomBuildingRenderer::Update()` (`Game.cpp:1833-1835`), not in the render function. If our PBR path has its own DN blend, it should use `CCustomBuildingRenderer::GetDayNightBalanceParam()` (or equivalent) rather than deriving a second time-of-day curve.

6. **Point-light reset**
   - `CPointLights::InitForFrame()` runs at the very top of the frame (`Main.cpp:4066`/`2759`) before `CGame::Process`. If our mod adds point lights during rendering or after updates, they will leak into the next frame unless reset at the same point.

7. **HUD is after post-FX**
   - Original order is `RenderEffects()` (including `CPostEffects::Render`) → motion blur → `Render2dStuff()` (HUD) → menus → fade → after-fade HUD (`Main.cpp:4305-4387`). A `DrawFinalEffects` or `CPostEffects::Render` hook placed after HUD will colour-filter the HUD, unlike the original.

8. **PS2/PC path swap for fog/clouds**
   - If our mod aims for PS2 look on PC, note that original PS2 puts water fog/moving fog/volumetric clouds inside `RenderScene` (`Main.cpp:3411-3430`), while PC/Xbox puts them inside `RenderEffects` (`3621-3640`). A single PC hook order cannot reproduce both; an explicit "PS2 mode" ordering switch is required.

9. **State ownership**
   - Original passes rely on `DefinedState()` and leave some states dirty (`PointLights`, `RenderReallyDrawLastObjects`, `RenderWeaponPedsForPC`). If our hook is called between these passes, it must not assume PBR-friendly state; set and restore the D3D9 states it needs, preferably with `RwRenderStateSet` and the `DefinedState` conventions, or use raw D3D with explicit save/restore and cache resync.

10. **Missing source caveat**
    - `DrawFinalEffects`, `CCoronas::Render`, `CWaterLevel::RenderWater`, `CShadows::Render*`, `CRenderer::*`, `CClouds::*` bodies are absent. The ordering above is reliable at the **call-site** level, but not at the implementation level. Any hook that replaces these functions must be validated against the binary/disassembly, not against this dump.

---

## 9. Condensed 10-bullet summary

- **No `CGame::Render` exists here.** The frame graph is `RenderScene()` + `RenderEffects()` in `Main.cpp`, driven by `TheGame` on PS2 and `Idle` on PC/Xbox; `CGame::Process` is simulation/update only (`Game.cpp:1442`, `Main.cpp:3194`, `3562`, `4072`).
- **Core order:** `SetLights` → `ConstructRenderList` → `PreRender` → mirror prep → start-of-frame/sky → mirror buffer → `RenderScene` (clouds, roads, reflections, everything-bar-roads, water, fading, shadows, plants, rain/sun, PS2 fog/clouds) → `RenderEffects` (birds, skidmarks, glass, moving things, really-draw-last, coronas, FX, water cannons, fog/clouds, searchlights, weapons, special FX, point-light fog, post-FX) (`Main.cpp:4152-4305`, `3194-3439`, `3562-3728`).
- **Post-FX is the last step of `RenderEffects`**, before motion blur and HUD; `CPostEffects::Render()` is not a standalone pass and manages its own camera update/front-buffer copy (`Main.cpp:3723`; `ScreenFX.cpp:2520-2548`, `2609-2611`, `3126-3131`).
- **Pipeline selection is load-time**, in `CAtomicModelInfo::SetAtomic` / `CClumpModelInfo::SetClump`, not in the frame loop: PC/Xbox use `CCustomBuildingRenderer`/`CCarFXRenderer`; PS2 uses `CVuCustomBuildingRenderer`/`CVuCarFXRenderer`/`CVuCustomSkinPedRenderer` and falls back to `PS2AllSetAtomicPipe` (`AtomicModelInfo.cpp:89-143`; `ClumpModelInfo.cpp:135-186`).
- **`bDontWriteZBuffer` is not the actual name here** — the code uses `GetDontWriteZBuffer()`/`MODEL_DONT_WRITE_ZBUFFER` and `FLAG_DONT_WRITE_ZBUFFER`; it toggles Z-write and alpha-test ref in `CVisibilityPlugins::RenderEntity` (`BaseModelInfo.h:140`, `174`, `341-342`; `GtaPlugins.cpp:1894-1958`).
- **`CPS2Peek` is only a profiler** inserting VIF `MARK` packets; `ps2all.cpp` is the real PS2 renderer (VU lighting, VIF skinning, mesh packets); `sky.cpp` is PS2 platform glue, not sky rendering (`CPS2Peek.h:73-78`; `CPS2Peek.cpp:346-362`; `ps2all.cpp:71-193`, `387-447`, `553-616`; `sky.cpp:752-865`).
- **Xbox differs mainly in the D3D8 all-in-one pipelines and DN:** separate `pipes/Xbox` sources, Xbox all-in-one node, DN vertex shader with day colour in vertex colour and night colour in normal, custom vehicle pixel shaders, and a shader-cache skinning fix; gates are `GTA_XBOX` plus `USE_PC_CARFX_RENDERER`/`USE_VOLUMETRIC_SHADOW_RENDERER` (`pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:92-112`, `453-525`, `1441-1484`; `pipes/Xbox/xboxallinone.c:1-9`; `Game.cpp:231-238`).
- **Render states are canonical-reset, not stacked:** `DefinedState()` is the reset; only `CPostEffects::ImmediateModeRenderStatesStore/Set/ReStore` has a symmetric 10-state save/restore; `PointLights`, `RenderReallyDrawLastObjects`, and `RenderWeaponPedsForPC` deliberately leak state (`handy.cpp:116-160`; `ScreenFX.cpp:1747-1796`; `PointLights.cpp:379-391`, `613-618`; `GtaPlugins.cpp:2077-2094`, `534-541`).
- **Lighting is global + per-entity:** `pAmbient`/`pDirect`+6 extra directionals, scaled by `CCoronas::LightsMult`; `SetLightsWithTimeOfDayColour()` runs before the render list; point lights become extra directionals via `CPointLights::GenerateLightsAffectingObject()`; buildings use RW/DN/PS2All paths differently (`Init_overlay.cpp:1602-1669`; `lights.c:60-159`, `266-328`; `PointLights.cpp:149-243`; `ps2all.cpp:148-193`).
- **Hook-order risks for our mod:** original names are `RenderScene`, `RenderEffects`, `CPostEffects::Render`, `CCoronas::Render`, `CPointLights::RenderFogEffect`, `CWaterLevel::RenderWater`, `CShadows::RenderStaticShadows`/`RenderStoredShadows`; `DrawFinalEffects`, `CWaterLevel::Render`, and `CPointLights::Render` are **not in this dump**. Preserve `SeamRemoverBegin/End`, the mirror gate on shadows, the water cull/underwater branch, and the post-FX-before-HUD order; do not double-call `CPostEffects::Render` from both `RenderEffects` and a separate hook (`Main.cpp:3259`, `3275`, `3316`, `3304-3306`, `3405-3407`, `3723`, `4305-4321`).

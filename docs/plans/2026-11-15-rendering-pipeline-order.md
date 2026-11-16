# Rendering Pipeline Order — skygfx_plus_expIV

## Overview
This document maps the complete frame rendering pipeline from outermost to innermost layer, including all hooks, state management, and UI integration. Use this to isolate which system causes which visual defect.

---

## LAYER 0: FRAME ENTRY

**Function:** `RenderScene_before()` (`src/core/main.cpp:1606`)

| Step | Function | Purpose | State Impact |
|------|----------|---------|-------------|
| 0a | `frameBoundaryCloseAndBaseline("SceneStart")` | Push canonical world state + close corona window | Forces ZWRITE=1, ZTEST=1, ZFUNC=LE, CULL=BACK, ALPHABLEND=0 on all 3 layers |
| 0b | `pipeRepairSceneStartSync("scene_start")` | Repair depth drift (ZWRITE/ZTEST/ZFUNC) | Device-side repair if cache disagrees |
| 0c | Far/fog camera re-update | `RwCameraEndUpdate` + `RwCameraBeginUpdate` (if begun) | Refreshes frustum/fog from current frame |
| 0d | `ForwardPlus_CullAndUpload()` | Tiled light culling | Uploads light data to GPU |
| 0e | `SMAATryInitRasters()` | Deferred SMAA raster init | Creates render-target textures |
| 0f | `RSMon_Sample("scene_start")` | Profiler sample (1-in-30 frames) | Read-only |

---

## LAYER 1: SCENE GEOMETRY

**Entry:** Game's `RenderScene()` → hooked at `0x53EABF` → `RenderScene_hook`

### 1a. Sky Dome (CClouds)
- **Hook:** `0x53DCA2`, `0x53DFA0` → `CClouds__Render_hygiene`
- **State:** Saves 12 states (rw cache + driver cache + raw device), forces PS/VS NULL, TEXTUREFILTER LINEAR, ALPHATEST ALWAYS, FOGENABLE FALSE, restores after
- **Produces:** Sky/atmosphere rendering
- **Known issues:** SEH removed (rough reduction), split-entry detection removed

### 1b. Buildings
- **Hook:** `0x53C15E` → `CustomBuildingPipeline__Update` (InterceptCall — **NOT a CALL in this exe!**)
- **InjectHook:** `0x5D7100`, `0x5D7D90` (CreateCustomObjPipe)
- **Patch:** `0x5D7200` (disable interpolation)
- **Callback:** `CCustomBuildingDNPipeline__CustomPipeRenderCB_Switch`
  - → PS2/Xbox/PBR/Sphere variants
  - → `pipeEnterCullMode()` + `pipeEnterAlphaMode()`
  - → ZWRITE 3-layer guard (force TRUE)
  - → `Transparency_SetZEnabled/ZFunc` (ZTEST=1, ZFUNC=LE)
  - → `pipeForceAlphaBlock(&savedAlpha)` (canonical alpha)
  - → `D3D9RenderDual` (PS2 mode only)
  - → decal park/restore (ZWRITE FALSE on blended meshes)
  - → `pipeExitAlphaMode(savedAlpha)`
- **State:** Forces ZWRITE=TRUE, ZTEST=TRUE, ZFUNC=LESSEQUAL, CULL=BACK, ALPHABLEND=FALSE, SRC=SRCALPHA, DST=INVSRCALPHA
- **Produces:** Building geometry, textures, envmaps, detail maps
- **Known issues:** Hook at 0x53C15E is not a CALL — InterceptCall corrupts instruction. Buildings don't render.

### 1c. Vehicles
- **Hook:** `0x5D9FE9` (InjectHook), `0x5D5B10` (InterceptCall), `0x7323C0` (InjectHook)
- **Callback:** `CCustomCarEnvMapPipeline__CustomPipeRenderCB_Switch`
- **State:** pipeEnterCullMode + pipeEnterAlphaMode + ZWRITE guard + pipeForceAlphaBlock
- **Produces:** Vehicle geometry, envmaps, glass, wheels
- **Status:** Working (callback fires per log)

### 1d. Peds
- **Hook:** `myDefaultCallback` (default pipe, skinPipe)
- **Entry:** `SkinEntryContract` (cull, alpha, zwrite, depth, opaque contract)
- **Ambient:** `SkinAmbientOverrideEnter` (pAmbient = timecycle ambient)
- **Stage-0:** `SkinStage0Resync` (texture raster recovery)
- **Exit:** `SkinExitContract` (restore alpha, sticky zwrite, cull, opaque)
- **Produces:** Ped geometry, skin, clothing, hair
- **Status:** Working (peds render per user)

### 1e. Default Pipe (props, pickups, markers)
- **Callback:** `myDefaultCallback` with pipe==NULL
- **Entry contract:** Forces canonical world signature (VERTEXALPHA=0, SRC=SRCALPHA, DST=INVSRCALPHA, ZWRITE=1, ZTEST=1, ZFUNC=LE, ATEST=1, AREF=1) on all 3 layers
- **Produces:** Props, pickups, markers, misc geometry

### 1f. Water
- **Hook:** `0x6E8790`, `0x6E8EF1`, `0x6E91E4`, `0x6E9963` (RenderAndEmptyRenderBuffer), `0x6EF8AC`, `0x6F0099` (RenderWater), `0x6EA2E8`, `0x6EDD7C` (RenderWake)
- **State:** EnsureSceneTexture/ReflTexture/DepthTexture, CopySceneToTexture
- **Produces:** Water surface, wakes, splashes

### 1g. Mirrors
- **Hook:** `0x53EABA` → `MirrorPass`
- **State:** MirrorDepthSnap (ZWRITE/ZTEST/ZFUNC capture/restore), pipeCullScopeBegin/End
- **Produces:** Mirror reflections

### 1h. Skidmarks
- **Hook:** `0x53E175` → `CSkidmarks__Render`
- **State:** Alpha-func ALWAYS wrap
- **Produces:** Tire marks

### 1i. Tags
- **Hook:** `0x534335`, `0x4C4412` → `CTagManager__RenderTag` / `SetupAtomic`
- **Produces:** Wall tags/graffiti

### 1j. RenderEffects (Coronas, Particles, Glass, Weapon FX)
- **Hook:** `0x53EAD3` → `RenderEffects_hook`
- **State:** effectsForceAlphaBlock (canonical alpha sweep + stale Im2D clear)
- **Produces:** Corona glows, particles, glass, weapon effects

### 1k. Tree Wind
- **Hook:** `0x534E90`
- **Produces:** Tree/palm wind animation (visual only)

---

## LAYER 2: POSTFX (CPostEffects::Render)

**Entry:** `0x53E227` → `CPostEffects__Render_hook`

### 2a. ColourFilter_switch
- **Hook:** `0x704D1E`
- **Steps:**
  1. `PostfxResetTonemapHandshake()`
  2. `PostfxStateSyncCheck("cf_enter")` (L2/L3 divergence heal)
  3. `derivePipelineFromPipes(config)` (per-frame pipe re-derive)
  4. `EnsureColorfilterVertsVisible()` (reset verts to white 0xFFFFFFFF)
  5. `CaptureScreenSize()`
  6. Normal buffer generation (if enabled)
  7. Velocity buffer (if enabled)
  8. SSAO dispatch (DrawSSAO / DrawSSAO_Overhaul)
  9. Prev-depth history copy
  10. Pipe-chain classify
  11. Sun shafts / DOF / Bloom group
  12. Motion blur
  13. IV grade
  14. God rays
  15. Height fog
  16. `ColourFilter_Modern` (grade + tonemap) if MODERN
  17. `UpdateFrontBuffer()` (camera raster → front buffer)

### 2b. Colour Filter Dispatch
| Case | Action |
|------|--------|
| NONE | Vanilla `CPostEffects::ColourFilter` (full-screen composite) |
| PS2 | `ColourFilter_PS2` (MODULATE2X, ×2) |
| PC | Shader/texture unbind only (no draw) |
| MOBILE | `ColourFilter_Mobile` (if not UG_mod) |
| III | `ColourFilter_Generic` with iiiTrailsPS |
| VC | `ColourFilter_Generic` with vcTrailsPS |
| VCS | `ColourFilter_Generic` with vcTrailsPS |
| MODERN | `ColourFilter_Modern` (grade + tonemap + IV grade) |
| GTAIV | Unbind only (bypass) |

### 2c. Post-Tonemap Lens Passes
- Chromatic Aberration (CA) → `DrawChromaticAberration`
- Grain → `CPostEffects::Grain`
- Vignette → vignette pass

### 2d. IV Grade / Motion Blur / Atmosphere Tail
- IV color grading
- Motion blur (if enabled)
- Height fog
- God rays

### 2e. Frame Exit
- `DrawFinalEffects_pf` (SMAA → FXAA3 → ImGui → Forward+ debug → YCbCr)
- `StateBlockEnforceCanonical` (frame-end canonical assert)
- `frameBoundaryCloseAndBaseline("EffectsExit")` (window close + baseline push)

---

## LAYER 3: FRAME EXIT

| Step | Function | Purpose |
|------|----------|---------|
| 3a | `RSMon_Report()` | Profiler report |
| 3b | `DebugOverlayDraw()` | Debug menu overlay (ImGui) |
| 3c | `PfFrame()` | Profiler frame boundary |

---

## UI LAYERING (ImGui Debug Menu)

The debug menu (`src/core/debugmenu_ui.cpp`, 232K) is drawn **last** in `DrawFinalEffects_pf`, after all postfx. It overlays on top of the final frame.

### Menu Structure
- **Main menu** (F1 toggle): tabbed interface
- **Tabs:** General, SkyGfx, Effects, Pipeline, Lighting, Water, Shadows, Debug, etc.
- **Per-section:** Each config field has a row with label, value, min/max, help text
- **Debug overlay:** Top-left frame-id readout, drawn screen-final (never filtered/tonemapped)

### UI → Pipeline Mapping
| UI Tab | Controls | Pipeline Impact |
|--------|----------|----------------|
| General | pipeline, pipelineOverride | Locks buildingPipe/vehiclePipe/colorFilter |
| SkyGfx | colorFilter, colorFilterEnable | ColourFilter_switch dispatch |
| Effects | ssaoEnable, smaaEnable, motionBlurEnable, fxaaEnable, dofEnable, bloomEnable, caEnable, grainEnable, vignetteEnable, godRaysEnable, heightFogEnable | PostFX pass gating |
| Pipeline | buildingPipe, vehiclePipe, explicitBuildingPipe, explicitVehiclePipe | Building/vehicle pipe selection |
| Lighting | ambientFloor, ambientMult, ambientClampEnable, pbrAmbientFloor, rgb1Mult, rgb2Mult | Ambient/tonemap |
| Water | waterQuality, waterReflectionFarClip, etc. | Water RT sizing + quality |
| Shadows | fixShadows, shadowContractEnable, etc. | Shadow rendering |
| Debug | debugOverlay, diagLevel, transparencyEnable, spriteStateShield, spriteFlushShield, effectsAlphaBlock, etc. | State-integrity toggles |

### UI State Flow
1. User changes a value in ImGui → `config->field = newValue`
2. `adoptLiveEdits()` folds live edits into the active config (skips safety fields)
3. Next frame, `derivePipelineFromPipes(config)` re-derives pipeline from pipes
4. PostFX passes check `config->xxxEnable` gates
5. Building/vehicle pipes check `config->buildingPipe`/`vehiclePipe`

---

## STATE MANAGEMENT (Cross-Cutting)

### pipeApplyWorldBaseline (4 call sites)
Forces canonical world state: SRC/DST=SRCALPHA/INVSRCALPHA, VERTEXALPHA=0, ZWRITE=1, ZTEST=1, ZFUNC=LE, CULL=BACK, ALPHATEST=1, COLORWRITE=0xF, BLENDOP=ADD, SEPARATEALPHA=0. **On all 3 layers.**

### pipeRepairSceneStartSync (every frame, unconditional)
Repairs device-vs-cache divergence on blend/Z family. 6 device Gets on healthy path.

### pipeForceAlphaBlock (call sites: RenderEffects enter/exit, PostFx exit, Coronas, Clouds)
Forces canonical alpha: VERTEXALPHA=0, SRC=SRCALPHA, DST=INVSRCALPHA, BLENDOP=ADD, ZWRITE=1, ZTEST=1, ZFUNC=LE, ATEST=1, AREF=1. **On all 3 layers.**

### pipeForceCullMode (building/vehicle/ped entry)
Forces cull mode through L1+L2+L3 (park-and-set idiom).

### pipeEnterAlphaMode / pipeExitAlphaMode
Save/restore alpha/blend state per pipe entry/exit.

### Transparency_SetZEnabled/ZFunc/ZWrite
Centralized depth state (ZENABLE/ZFUNC/ZWRITE) on all 3 layers.

### StateBlockEnforceCanonical (frame-end)
Asserts canonical depth+blend contract on all 3 layers. Silent when healthy, `[frame_repair]` receipt when repair fires.

### effectsForceAlphaBlock
Canonical alpha sweep + stale Im2D override clear at effects boundaries.

---

## KEY HOOK ADDRESSES

| Function | Address | Type | Notes |
|----------|---------|------|-------|
| RenderScene | 0x53EABF | InterceptCall | Scene entry |
| CClouds::Render | 0x53DCA2, 0x53DFA0 | InterceptCall | Sky dome (hygiene hook) |
| CCoronas::Render | 0x6FAEC0 | InjectHook | Corona entry shield |
| RenderEffects | 0x53EAD3 | InterceptCall | Effects phase |
| CCoronas::RenderReflections | 0x53DCAC, 0x53DFD8 | InterceptCall | |
| CCoronas::RenderSunReflection | 0x53E12B | InterceptCall | |
| CPostEffects::Render | 0x53E227 | InterceptCall | PostFX entry |
| CPostEffects::ColourFilter | 0x704D1E | InjectHook | ColourFilter_switch |
| CPostEffects::Radiosity | 0x704D5D, 0x704FB3 | InjectHook | |
| CPostEffects::DarknessFilter | 0x704D48 | InjectHook | |
| CPostEffects::Initialise | 0x5BD779 | InterceptCall | Shaders |
| CSkidmarks::Render | 0x53E175 | InterceptCall | |
| CTagManager::RenderTag | 0x534335 | InterceptCall | |
| CTagManager::SetupAtomic | 0x4C4412 | InterceptCall | |
| CWaterLevel::RenderAndEmptyRenderBuffer | 0x6E8790, 0x6E8EF1, 0x6E91E4, 0x6E9963 | InterceptCall | |
| CWaterLevel::RenderWater | 0x6EF8AC, 0x6F0099 | InterceptCall | |
| CWaterLevel::RenderWake | 0x6EA2E8, 0x6EDD7C | InterceptCall | |
| fxSystem::Render | 0x4A930A | InterceptCall | Particles |
| CMirrors::RenderMirrorBuffer | 0x53EABA | InterceptCall | |
| Tree Wind | 0x534E90 | InjectHook | |
| CVisibilityPlugins::RenderWheelAtomicCB | 0x7323C0 | InjectHook | |
| CCustomBuildingPipeline__Update | 0x53C15E | InterceptCall | **NOT a CALL in this exe!** |
| hookBuildingPipe | 0x5D7100, 0x5D7D90, 0x5D7200 | InjectHook/Patch | Valid |
| hookVehiclePipe | 0x5D9FE9, 0x5D5B10, 0x7323C0 | InjectHook/InterceptCall/InjectHook | Valid |

---

## WHY BUILDINGS DON'T RENDER

1. **0x53C15E is NOT a CALL instruction** — bytes are `0D D9 44 24 08` (`OR EAX, imm32`), not `E8` (CALL)
2. `InterceptCall` overwrites this instruction with a JMP, corrupting the building render path
3. The building pipe's state management (`CustomBuildingPipeline__Update`) never runs
4. Building geometry is drawn through the game's default path, which may be broken by the misplaced patch
5. Vehicle pipe hook at 0x5D5B10 IS a CALL and works correctly

**Fix:** Skip the InterceptCall at 0x53C15E (guard with byte check). Building pipe renders via game default path without mod's state management.

---

## DEBUGGING STRATEGY

To isolate which system causes which visual defect:

1. **Disable all postfx** (set all `*Enable=0` in skygfx.ini) → if black screen persists, issue is in scene geometry or state
2. **Disable building pipe** (`buildingPipe=0` / PS2) → if buildings appear, issue is in Xbox/PBR pipe
3. **Disable vehicle pipe** (`vehiclePipe=0`) → if vehicles appear, issue is in vehicle pipe
4. **Disable state integrity** (`transparencyEnable=0`, `spriteStateShield=0`, `spriteFlushShield=0`) → if corruption persists, issue is architectural
5. **Check log for `[frame_repair]` / `[zfix]` / `[BldZW]`** → identifies which state is drifting
6. **Check log for `ColourFilter_switch`** → identifies which colour filter case is active
7. **Check log for `EnsureColorfilterVertsVisible`** → black verts blacken all post passes
8. **Check log for `bpipe` / `XBOX-CB`** → confirms building pipe callback is firing

---

## CONFIG REFERENCE (skygfx.ini)

| Key | Default | Effect |
|-----|---------|--------|
| `buildingPipe` | Xbox | Building pipe selection (PS2/Xbox/GTAIV/PBR) |
| `vehiclePipe` | Xbox | Vehicle pipe selection (PS2/Xbox/Env/Modern/GTAIV) |
| `pipeline` | Xbox | Locks buildingPipe/vehiclePipe/colorFilter |
| `colorFilter` | Modern | Colour filter mode (0-8) |
| `colorFilterEnable` | 0 | Enable colour filter processing |
| `transparencyEnable` | 0 | Centralized depth state management |
| `spriteStateShield` | 0 | Sprite state integrity shield |
| `spriteFlushShield` | 0 | Sprite flush shield |
| `effectsAlphaBlock` | 1 | Effects-phase alpha force |
| `ambientFloor` | 0.10 | Minimum ambient luminance |
| `ambientClampEnable` | 0 | Night ceiling clamp |
| `rwFixesEnable` | 1 | RW fix machine (corona/flush/font/pointlight shields) |
| `shadowContractEnable` | 0 | Shadow state contract |
| `stateBaselineEnable` | 1 | Frame-boundary world baseline (floored to 1) |
| `diagLevel` | 0 | Debug log verbosity (0=off, 1+=on) |
| `debugOverlay` | 0 | Debug overlay display |

---

*Generated from codebase analysis. All line numbers reference `E:\dev(dave)\skygfx_plus_expIV`.*

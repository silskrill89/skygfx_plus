# SkyGfx Plus expIV - Complete Codebase Documentation

> [!warning] Legacy status doc
> **Feature/status authority is docs/FEATURE_LIST.md (§12 Source-of-Truth MD Index); build/layout
> rules are AGENTS.md.** This file is the codebase map and is listed there as legacy
> ("PLAN.md, CODEBASE_DOCUMENTATION.md … stale where they conflict").

> [!success] Reconciled 2026-11-07
> Paths, the Key Source Files table, the hook list, the shader list and the build description below
> were re-verified against the working tree on 2026-11-07:
> - Layout is the post-split tree `src/core|render|entities|peds|extras|rw` (+ `src/texdefs.{cpp,h}`
>   and `src/skygfx.h` at `src/` root). The old flat `src/*.cpp` set is gone; merged/deleted TUs are
>   named inline below.
> - All hook addresses listed under **Hooks** were grepped in `src/` — **0 absent**.
> - `src/extras/wheels.cpp` is **present** on disk *and* in `build/skygfx.vcxproj` (the "wheels.cpp
>   was deleted" assumption is false for this tree — the row is kept).
> - Build is `python tools/fast_build.py`; shaders are compiled by that script (not by an MSBuild
>   pre-build step); output/ASI copy is `E:\games\gtasa_skygfx_plus\`.

> [!note] W0.3 wave (landing 2026-11-09 — build pending)
> **"Stochastic texturing" is renamed project-wide to "noise based variation"** and routed through
> `src/render/noise.cpp` / `noise.h` (`NoiseMaterialVariation`) over the packed RGBA utility tile in
> `shaders/include/Noise.hlsl`; the building shaders are rewritten and `buildingPipe.cpp` plumbing is
> rewired. Wind animation leaves the solo tree-wind feature and becomes part of a motion-FX family:
> new `src/render/motion_fx.cpp` / `.h` (parent: motion orchestration + wind animation +
> randomness/physics-family integration) and new `src/render/wind.cpp` (child: wind field / gust
> math); `src/render/treeWind.cpp` keeps only the `TreeAlpha` path. Both new TUs join
> `build/skygfx.vcxproj`. `src/core/main.cpp` is 7,805 lines after the `rw_fixes.cpp` extraction.

> [!info] Migrated
> This document has been refactored into the Obsidian vault.
> Start at [[00 - Index]] for the current documentation.

---

## Key Source Files (post-split set — verified 2026-11-07)

| File | Purpose |
|------|---------|
| `src/skygfx.h` | Config struct, pipeline enums, shader externs, debug macros (1,658 lines) |
| `src/texdefs.cpp` / `src/texdefs.h` | Unified texture-definition layer — merges texdb.txt metadata (`TexInfo`), `<diffuse>_n` normal-map pairing, atomic plugin slots (former `texdb.cpp` + `normalmap.cpp` + `extendedplg.cpp` slots) |
| `src/core/main.cpp` | DllMain, INI config (linb::ini), pipeline switch, hook installation, residual shields (7,805 lines; RW fix machines extracted to `src/core/rw_fixes.cpp`) |
| `src/core/modeManager.cpp` / `.h` | Graphics mode manager: single-active-mode state machine + per-mode Config profile table |
| `src/core/debug_handl.cpp` / `.h` | Merged diagnostics TU: logging, ring buffer, scope tags, crash VEH/watchdog, minidump filter |
| `src/core/rw_fixes.cpp` | RW-level fix machinery extracted from `main.cpp` (corona/sprite/font/shadow render-state machines); `rwFixesEnable` kill-switch, receipts `[rwfix] install OK (6 machines)` / `install SKIPPED` |
| `src/core/debugmenu_ui.cpp`, `src/core/debugmenu_theme.cpp` | ImGui debug menu (master-detail sections) + theme |
| `src/core/presets.cpp` | Preset name table + preset/mode-latch handling |
| `src/core/weather.cpp` / `.h` | Multi-timecyc weather system (hash-based set selection over `CTimeCycle`) |
| `src/core/physicscfg.cpp` / `.h` | `sky_physics.ini` service |
| `src/render/buildingPipe.cpp` | Building render callbacks (PS2/Xbox/PBR/GTAIV); pipe-creation hook `0x5D7100` |
| `src/render/vehiclePipe.cpp` | Vehicle render callbacks (PS2/Xbox/PBR/modern); pipe-creation hook `0x5D9FE9` |
| `src/render/pipelinecommon.cpp` | Matrix helpers, light/material/world-view-proj uploads, `CreateShaders` |
| `src/render/postfx.cpp` / `src/render/postfx.h` | PostFX chain core: `DrawFinalEffects`, radiosity, grain, IR/night vision, front-buffer update (5,380 lines) |
| `src/render/postfx_colourfilter.cpp` | Colour-filter chain, IV grade, colour cycle |
| `src/render/postfx_luminance.cpp` | Luminance measure/adapt targets + auto exposure |
| `src/render/postfx_motionblur.cpp` | Velocity buffer + `DrawVelocityBuffer` |
| `src/render/postfx_newfx.cpp` | Sun shafts, DOF, bloom, chromatic aberration, film grain, vignette, FXAA3 |
| `src/render/postfx_smaa.cpp` | SMAA passes (+ `src/render/AreaTex.h`, `src/render/SearchTex.h`) |
| `src/render/postfx_ssao.cpp` | SSAO passes + depth/prev-depth/normal-buffer resources |
| `src/render/postfx_underwater.cpp` / `.h` | Underwater volume fog + subsurface scatter |
| `src/render/postfx_utils.cpp` | Shared postFX helpers (fullscreen quad, screen-size cache, geometry save/restore) |
| `src/render/texalpha.cpp` / `.h` | Per-texture alpha classification for the PBR pipes |
| `src/render/envmap.cpp` | Reflection camera/raster creation, vehicle reflections, mirror fixes |
| `src/render/neopipe.cpp` / `.h` | NEO pipe (merged former `neo.cpp` + `neoCarpipe.cpp` + `neoWaterdrops.cpp`) |
| `src/render/waterPipe.cpp` / `.h` | Water pipeline |
| `src/render/forwardplus.cpp` / `.h` | Forward+ light passes |
| `src/render/grasssys.cpp` / `.h` | Plant/grass system (merged former `PC_GrassRenderer.*` + `PC_PlantsMgr.*`) |
| `src/render/treeWind.cpp` / `.h` | Fixed-function tree lighting/alpha (`TreeAlpha`) only; wind sway/animation moved to the motion-FX family (W0.3) |
| `src/render/motion_fx.cpp` / `.h` | **NEW (landing W0.3)** — motion-FX parent: motion orchestration, wind animation, randomness/physics-family integration (one motion clock `MotionFxClockSeconds()`, randomness seam `NoiseMaterialVariation`, read-only physics coupling) |
| `src/render/wind.cpp` | **NEW (landing W0.3)** — wind field / gust math (child of `motion_fx`; pure math, no family/state knowledge) |
| `src/render/particles_gpu.cpp` / `.h` | GPU particle sprite path |
| `src/render/depthhook.cpp` / `.h` | INTZ direct-binding depth hook (DXVK compatibility) |
| `src/render/mirrorPass.cpp` / `.h` | Mirror composite distortion fix |
| `src/render/renderstate_monitor.cpp` / `.h` | Three-layer render-state divergence monitor (rw cache / RW driver / D3D9) |
| `src/render/profiler.cpp` / `.h` | Pass/frame profiler (`Pf*` / `PF_*`) |
| `src/render/noise.cpp` / `.h` | Universal noise service: CPU generators + packed RGBA utility tile; drives **noise based variation** (`NoiseMaterialVariation`) — the replacement for stochastic texturing (W0.3) |
| `src/peds/peds_pipe.cpp` / `.h` | Ped pipe control point (design: `docs/plans/2026-10-31-peds-pipe-design.md`) |
| `src/peds/skin.cpp` / `.h` | Skin/skin-material sub-module (Phase 3 SKIN absorb) |
| `src/entities/sftbdyphsx.cpp` / `.h` | Merged living-ped ragdoll overlay + Bullet gib dismemberment |
| `src/entities/softbody.cpp` / `.h` | Native RW-physics softbody/ragdoll core |
| `src/entities/chars.cpp` / `.h` | Character/asset helpers |
| `src/entities/PlantSurfPropMgr.h` | Plant surface property manager |
| `src/extras/ragdoll_death.cpp` / `.h` | Death ragdoll (Bullet) |
| `src/extras/wheels.cpp` / `.h` | Wheel DFF management (present on disk and in `build/skygfx.vcxproj` — **not** deleted) |
| `src/extras/vehicles.cpp` | Master vehicle registry (vehicles.ide / carcols.dat / handling.cfg) |
| `src/extras/veh_shaders.cpp` | Vehicle→shader data bridge |
| `src/rw/gta.h` / `src/rw/gta.cpp` | RW SDK globals (`pDirect`, `pAmbient`, `Scene`, `d3d9device`) + WRAPPER/EAXJMP declarations |
| `src/rw/pipeplg.cpp` | Pipeline plugin attachment |
| `src/rw/normmap_stubs.cpp` | Forwarding stubs for rpnormmap dependencies |

Rows removed vs. the old flat listing (TUs deleted or merged away): `src/main.cpp`,
`src/buildingPipe.cpp`, `src/vehiclePipe.cpp`, `src/pipelinecommon.cpp`, `src/postfx.cpp`,
`src/SMAA.cpp`, `src/envmap.cpp`, `src/neo.cpp`, `src/neoCarpipe.cpp`, `src/neoWaterdrops.cpp`,
`src/texdb.cpp`, `src/extendedplg.cpp`, `src/PC_GrassRenderer.cpp/.h`, `src/config.cpp`,
`src/pipeplg.cpp`, `src/defaultFuncs.cpp`, `src/gta.h` — each now lives (or died) at the path shown
in the table above.

---

## Source Code Structure

### Core Headers

#### src/skygfx.h
Main configuration and type definitions. Key features:
- **CarPipeline enum**: PS2, PC, XBOX, SPEC, MOBILE, NEO, LCS, VCS, ENV, GTAIV
- **BuildingPipeline enum**: PS2, XBOX
- **DefinedVertexShader enum**: DEFAULT, WIND
- **Config struct**: fields covering all rendering features:
  - Building/vehicle pipeline selection
  - PS2 modulation, dual-pass settings
  - Color filters (PS2, PC, Mobile, III, VC, VCS, GTAIV)
  - Radiosity, grain, infrared/night vision
  - SSAO (enable, radius, power, kernel, samples)
  - SMAA (enable, preset, predication, temporal)
  - GTA IV Mode (desaturation, gamma, vignette, bloom, exposure)
  - Weather/timecycle expansion (GTA V style sky, sun, moon, clouds, light)
  - PostFX, vignette, color grading, lens effects
  - Water, weather cycle, debug menu
- **TexInfo struct**: Texture database with detail maps, noise-variation/dual-pass flags
- **CPostEffects**: static members and function declarations
- **Shader externs**: shader pointers for all pipelines

#### src/rw/gta.h
Game structure definitions (CVector, CMatrix, CEntity, etc.) and RW SDK globals.

---

### Core Implementation

#### src/core/main.cpp (7,805 lines)
**Key Systems**:
1. **Hook Framework**: InjectHook, InterceptCall, Patch helpers (inline, matching the backup_original DllMain pattern)
2. **Pipelines**: building/vehicle pipeline hookup, dual-pass rendering, grass/plant callbacks
3. **Shields**: residual call-site wiring; the RW-level render-state machines (corona/sprite/font/shadow) now live in `src/core/rw_fixes.cpp` and are gated by `rwFixesEnable` (see **Hooks**)
4. **Initialisation**: DllMain (line 9556) → version check, one-shot delayed patches (`InjectDelayedPatches`), mode/preset wiring
5. **Config System**: `linb::ini` parsing (`ini_parser.hpp`), config reload, INI→Config field mapping
6. **Menu System**: ImGui debug menu integration (`debugmenu_ui.cpp`)
7. **Profiler frame boundary** at the `0x53EBE9` hook

> Older flat-layout notes ("src/main.cpp (2266 lines)", its line-numbered subsystem list, and
> "`RwEngineInstance` at main.cpp:1-11") no longer hold — see **Fixes Applied** for the current
> locations of those two expIV fixes.

---

### Pipeline Implementations (src/render/)

#### src/render/buildingPipe.cpp
- PS2, Xbox, GTAIV/PBR building pipelines
- Noise-based material variation (noise-detail PS family: `buildingPipe_bindNoiseFor` binds the s9 packed tile + c49-c52 knob block before the PS bind), wind VS binding
- PS2 modulation, dual-pass support
- Owns the building pipe-creation hook `0x5D7100`

#### src/render/vehiclePipe.cpp
- CAR_PS2, CAR_PC, CAR_XBOX, CAR_SPEC, CAR_MOBILE, CAR_NEO, CAR_LCS, CAR_VCS, CAR_ENV, CAR_GTAIV (+ CAR_MODERN/PBR)
- Environment mapping (Leeds, Neo)
- Specular, Fresnel, custom shaders; upgrade-parts pipeline assignment
- Owns the vehicle pipe-creation hook `0x5D9FE9`

#### src/render/pipelinecommon.cpp
- Matrix helpers (RwToD3DMatrix, MakeProjectionMatrix)
- Shader constant uploaders (light, material, world/view/proj)
- `CreateShaders` — shader loading for all pipelines

---

### Post-Processing (src/render/postfx* — the m0060 split)

#### src/render/postfx.cpp (5,380 lines) + split units
The former single `postfx.cpp` was split into focused translation units:

| Unit | Contents |
|------|----------|
| `postfx_colourfilter.cpp` | ColourFilter (PS2, PC, Mobile, III, VC, VCS, GTAIV/IV grade, generic), DarknessFilter, colour cycle |
| `postfx_luminance.cpp` | Luminance measure/adapt targets, auto exposure |
| `postfx_motionblur.cpp` | Velocity buffer, `DrawVelocityBuffer`, tier-0 framerate normalisation |
| `postfx_newfx.cpp` | Sun shafts, DOF, bloom, chromatic aberration, film grain, vignette, FXAA3 |
| `postfx_smaa.cpp` | SMAA passes + static area/search textures (LOW/MEDIUM/HIGH/ULTRA presets) |
| `postfx_ssao.cpp` | Screen-space ambient occlusion + depth/normal buffer resources |
| `postfx_underwater.cpp` | Underwater volume fog + subsurface scatter |
| `postfx_utils.cpp` | Shared helpers: fullscreen quad, tracked rasters, screen-size cache, geom save/restore |
| `postfx.cpp` (core) | `DrawFinalEffects` chain, radiosity (PS2/Shader/VCS), infrared/night vision, grain, front-buffer update |

---

### Environment Mapping / Neo / Water

- `src/render/envmap.cpp` — reflection camera/raster creation, vehicle reflections (Leeds, Neo), mirror fixes
- `src/render/neopipe.cpp` — NEO vehicle pipeline + water/blood drop effects (merged TU)
- `src/render/mirrorPass.cpp` — mirror composite distortion fix (FusionFix port)
- `src/render/waterPipe.cpp` — water pipeline

---

### Texture Database

#### src/texdefs.cpp / src/texdefs.h
- TXD slot / texdb.txt metadata management (`TexInfo`)
- Texture name associations, detail map system, noise-based variation (formerly stochastic)/dual-pass per-texture settings; the legacy `stochastic=` key is read as an alias
- `<diffuse>_n` / `<diffuse>_norm` normal-map pairing (former `src/render/normalmap.cpp` + `src/rw/normalmap.cpp`)
- Atomic plugin slots (defined vertex shader, moved from `extendedplg.cpp` / `pipeplg.cpp`)
- Registers the texture-find callback at `0x731FD5+1` → `TexDefsFindCB`

---

### Extended Features

- `src/render/grasssys.cpp` / `.h` — PC plant island translation unit (merged `PC_GrassRenderer` + `PC_PlantsMgr`), PS2 modulation, custom plant models
- `src/render/treeWind.cpp` / `.h` — fixed-function tree lighting/alpha only (`TreeAlpha`); W0.3 slims it as wind moves to the motion-FX family
- `src/render/motion_fx.cpp` / `.h` — **NEW (landing W0.3)** motion-FX parent: motion orchestration + wind animation + randomness/physics-family integration (one clock `MotionFxClockSeconds()`, randomness seam `NoiseMaterialVariation`)
- `src/render/wind.cpp` — **NEW (landing W0.3)** wind field / gust math, pure-math child of `motion_fx`
- `src/render/particles_gpu.cpp` / `.h` — GPU particle sprite path
- `src/render/forwardplus.cpp` / `.h` — GTA IV–style forward/Forward+ passes

---

### Utilities

- `src/core/modeManager.cpp` — single-active-mode graphics mode state machine (replaces the old flat `config.cpp` mode handling)
- `src/core/presets.cpp` — preset table
- `src/core/debug_handl.cpp` — logging/diagnostics (merged `diagnostics.cpp`-era code)
- `src/core/weather.cpp` — multi-timecyc weather
- `src/rw/pipeplg.cpp` — pipeline plugin attachment (moved from `src/pipeplg.cpp`)
- `src/rw/normmap_stubs.cpp` — rpnormmap forwarding stubs

> Deleted TUs (per `premake5.lua` header comments): `defaultFuncs.cpp` (2026-10-27, zero cross-TU
> references), `normalmap_plugin.cpp`, `HLSL_hook.cpp`, `PC_PlantsMgr.*`/`PC_GrassRenderer.*`
> (merged into `grasssys`), `texdb.cpp`/`normalmap.*` (merged into `texdefs`).

---

## Hooks (GTA SA v1.0 US) — every address re-grepped in `src/` on 2026-11-07 (0 absent)

| Address | Hook / role | Where it appears |
|---------|-------------|------------------|
| `0x704D1E` | `InjectHook(0x704D1E, CPostEffects::ColourFilter_switch)` — colour-filter switch | `src/core/main.cpp:10083` (also referenced in `src/render/postfx.cpp`) |
| `0x53EBE9` | `InjectHook(0x53EBE9, DrawFinalEffects_pf)` — profiler frame boundary wrapping `CPostEffects::DrawFinalEffects` | `src/core/main.cpp:10099`, `src/core/main.cpp:9173` |
| `0x5D7100` | Building pipe creation → `CCustomBuildingDNPipeline__CreateCustomObjPipe_PS2` | `src/render/buildingPipe.cpp:4014` |
| `0x5D9FE9` | Vehicle pipe creation → `setVehiclePipeCB` | `src/render/vehiclePipe.cpp:4282` |
| `0x70CF20` | Corona indexed-sprite flush — shield entry (CCoronas::Render tail-jmp) | `src/core/main.cpp` (install + byte checks ~9931-9963) |
| `0x70CF62`, `0x70CF9B` → `0x734EA0` | Corona flush draw sites — `InterceptCall` → `SpriteFlushIndexedDraw_hook`, targets byte-verified `E8 → 0x734EA0` (cdecl primitive draw) | `src/core/main.cpp:9943-9963` |
| `0x53DE6B` | `CPointLights::Render` early glow-sprite site — shield re-arm (also `0x53E21D`) | `src/core/main.cpp:3254`, `src/render/postfx_colourfilter.cpp` |
| `0x53E0BE` | Shadow call site → `0x707F40` `CShadows::UpdateStaticShadows` (hooked) | `src/core/main.cpp:9677`, `9707` |
| `0x53E0C3` | Shadow path mid-call site (documented; function reached from `0x53DDE4`/`0x53E0C3`) | `src/core/main.cpp:9680-9683` |
| `0x53E0C8` | Shadow call site → `0x70A960` `CShadows::RenderStoredShadows` — **slot left empty: SilentPatch-owned** | `src/core/main.cpp:9709-9718` |
| `0x7273D0` | Font glyph flush pin — `0x7273D0` is `CFont::RenderFontBuffer` glyph flush (its only caller is `0x719840`) | `src/core/main.cpp:4301-4344` |
| `0x731FD5` / `0x731FD5+1` | Texture-find callback registration (`push imm32` captured, patched to `TexDefsFindCB`) — the former normalmap chain, now in `texdefs` | `src/texdefs.cpp:8-10`, `432-443` |

---

## Module / Agent Architecture (brief)

- **Modules follow the directory split**: `core` (entry/config/modes/menu), `render` (pipes, postFX, render systems, motion-FX/wind), `peds` (ped pipe/skin), `entities` (physics, chars, plant props), `extras` (vehicle data, wheels, death ragdoll), `rw` (RW SDK glue/stubs), plus `src/texdefs.*` and `src/skygfx.h` at `src/` root.
- **8 specialist agents** live in `.mimocode/agents/` (one file each): `shader-expert`, `pbr-specialist`, `postfx-specialist`, `crash-debugger`, `knowledge-manager`, `build-deploy`, `researcher`, `render-auditor`. Routing: `python tools/agent_coordinator.py route "task" scope`; swarm dispatch via `workflow({ name: "swarm-dispatch", args: { task, scope } })`.
- This file does not add agent rules of its own — **AGENTS.md is authoritative** for build commands, shader pipeline, hooks and safety rules.

---

## Shaders

- **Source**: `shaders/ps/`, `shaders/vs/`, `shaders/include/` (+ `shaders/shaders.bat`)
- **Compiled by**: `python tools/fast_build.py --shaders` (FXC, SM3.0 `ps_3_0`/`vs_3_0`) — *not* by an MSBuild pre-build step
- **Outputs**: `resources/cso/` (124 `.cso` files) embedded via `resources/resource.h` + `resources/Resource.rc`

Families present on disk (all under `resources/cso/`):
- **Building**: `ps2BuildingVS`, `ps2BuildingFxVS`, `ps2BuildingWindVS`, `xboxBuildingVS`, `xboxBuildingWindVS`, `xboxBuildingNoiseDetailPS`, `buildingPipePS`, `buildingPBRVS`, `BuildingPBRPS`, `GTAIVBuilding_vs/_ps`, `customBuildingVS`, `mobileBuildingVS`, `sphereBuildingVS`, `normMapBuildingPS`, `unifiedPipe*`
- **Vehicle**: `vehicleVS`, `vehiclePipeVS`, `vehiclePBRVS`, `ps2CarFxVS`, `xboxCarVS`, `leedsCarFxVS`, `mobileVehicleVS/PS`, `neoVehiclePass1VS/2VS`, `specCarFxVS/PS`, `ps2EnvSpecFxPS`, `GTAIVVehicle_vs/_ps`, `VehiclePBR_Modern`, `normMapVehiclePS`, `pedPBRVS`
- **PostFX**: `postfxVS`, `radiosityPS`, `blurPS`, `contrastPS`, `gradingPS`, `SMAA*` (8 passes), `SSAO*` (8 passes), `Bloom_*`, `DOF_*`, `GodRays`, `FXAA3`, `Grain`, `CA`, `Vignette`, `MotionBlur_Burnout`, `vectorMotionBlur`, `LuminanceAdapt/Reduce`, `TonemapPass`, `TonemapLUT`, `IVGrade`, `ModernColorFilterPS`, `GTA_SA_ModernColor`, `Mirror_Composite`
- **Scene**: `grassPS`, `iiiTrailsPS`, `vcTrailsPS`, `CloudsPS`, `cloudsSkyPS`, `DynamicSky`, `HeightFog`, `Water_*`, `GrassInstanced`, `TreeAlpha`, `Glass_Vehicle`, `WakeParticlePS`

(Names in the previous flat-layout shader list such as `gtaivVehiclePS`, `gtaivBuildingVS`,
`gtaivFPVS`, `envCarVS/PS`, `ps2BuildingPS`, `xboxBuildingStochasticPS`,
`simpleDetailStochasticPS` no longer exist — they were renamed as shown above.)

---

## Build Configuration

### Primary build — `tools/fast_build.py` (AGENTS.md)
```bash
python tools/fast_build.py              # smart incremental (default)
python tools/fast_build.py --fastest    # skip detection, always build+deploy
python tools/fast_build.py --rebuild    # clean full rebuild + deploy
python tools/fast_build.py --shaders    # only compile HLSL → CSO
python tools/fast_build.py --launch     # build + launch game
```
- FXC: `C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\Utilities\bin\x86\fxc.exe`
- MSBuild: VS2022 Enterprise `MSBuild\Current\Bin\MSBuild.exe`
- Build log: `premake_log.txt`; diag log: `E:\games\gtasa_skygfx_plus\skygfx_dbg.log`

### premake5.lua
- Workspace: `skygfx`; Configurations: Release, Debug; `location "build"`
- Target: SharedLib `.dll` (`targetname skygfx`), CharacterSet MBCS, toolset v143, systemversion 10.0.19041.0
- `targetdir "E:/games/gtasa_skygfx_plus"`; post-build `copy $(TargetPath) → skygfx.asi && del .dll`
- **Pre-build**: `rem Shaders precompiled manually` — shaders are built by `tools/fast_build.py --shaders`, never by an fxc loop in MSBuild (110 outputs, `VehiclePBR_Modern.hlsl` alone has 7 entry points)
- Options: `/FS`, `/Zc:threadSafeInit-`; `Directory.Build.props` sets `EnableEnhancedInstructionSet=StreamingSIMDExtensions2`

### build\build.bat (manual/legacy path — still present)
```batch
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x86
set RWSDK36=E:\SDKs\Renderware 3.7 SDK (For Windows) Full\RWSDK37\Graphics\rwsdk\include\d3d9
set DXSDK_DIR=C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)
MSBuild.exe skygfx.sln /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v143
```

---

## Dependencies

| Dependency | Path |
|------------|------|
| RenderWare SDK 3.7 | E:\SDKs\Renderware 3.7 SDK (For Windows) Full\RWSDK37\Graphics\rwsdk\include\d3d9 |
| DirectX SDK | C:\Program Files (x86)\Microsoft DirectX SDK (June 2010) |
| ImGui | E:\SDKs\imgui-master (vendored copy also under `external/imgui`) |
| plugin-sdk | E:\SDKs\plugin-sdk-master |

(All four verified present on disk 2026-11-07.)

---

## Fixes Applied (expIV)

### 1. SSE2 Intrinsic Fix (`src/skygfx.h:42-44`)
```cpp
// Fix for _mm_loadu_si64 intrinsic - x64 only, map to x86 equivalent
#define _mm_loadu_si64 _mm_loadu_si32
```
Maps the x64-only `_mm_loadu_si64` to the x86-compatible `_mm_loadu_si32`.
(Previously documented at `skygfx.h:26-30` — the file has since grown; the macro now sits at 42-44.)

### 2. RwEngineInstance for rpnormmap
Now provided by `src/rw/normmap_stubs.cpp:79` (`__declspec(selectany) int RwEngineInstance = 0;`),
with `src/rw/gta.h:280` defining `RwEngineInstance (*rwengine)` for renderFrame access.
(Originally an `extern "C" void*` in `src/main.cpp:1-11`; that flat file is gone.)

---

## Known Working Configuration

**PS2 Config** (aap's v4.2b):
```
buildingPipe = PS2
vehiclePipe = PS2
colorFilter = PS2
doRadiosity = 1
radiosity = Shader
```

---

## Deployment
- **Build + deploy**: `python tools/fast_build.py` (builds `skygfx.dll` and copies it as `skygfx.asi` into the game dir via `premake5.lua` `targetdir`/post-build)
- **Deploy To / live ASI**: `E:\games\gtasa_skygfx_plus\skygfx.asi` (present on disk 2026-11-07)
- **Diag log**: `E:\games\gtasa_skygfx_plus\skygfx_dbg.log`
- **Plugins**: SilentPatch, CLEO, GInput, MoonLoader, modloader

---

## Next Steps
1. Build: `python tools/fast_build.py` (or `--rebuild` for a clean full rebuild)
2. Verify `skygfx.asi` timestamp in `E:\games\gtasa_skygfx_plus\`
3. Launch `gta_sa.exe`, check `E:\games\gtasa_skygfx_plus\skygfx_dbg.log`
4. Feature/status questions → `docs/FEATURE_LIST.md` (§12 index), not this file.

# Three-Codebase Comparison

A comprehensive comparison of all three skygfx codebases for multi-agent reference.

## Lineage

```
aap (original v4.2b) → junior_dr (fork, backup_original/) → skygfx_plus (expIV rewrite)
```

- **aap Original**: ~22 source files, flat directory, 9 vehicle pipes, 2 building pipes
- **junior Fork (backup_original/)**: 33 files, ~10K LOC (.cpp), flat directory, 10 vehicle pipes, 2 building pipes
- **skygfx_plus**: 67 source files, ~18K LOC (.cpp), organized subdirectories, 11 vehicle pipes, 4 building pipes

---

## 1. Source File Mapping

### Core Files (shared across all three)

| File Name | aap Original | junior Fork (backup_original/) | skygfx_plus (src/) | Notes |
|-----------|-------------|-------------------------------|-------------------|-------|
| main.cpp | `src/main.cpp` | `src/main.cpp` 1982 LOC | `src/Core/main.cpp` 1996 LOC | Entry point, hooks, config, crash handler |
| skygfx.h | `src/skygfx.h` | `src/skygfx.h` 484 LOC | `src/skygfx.h` 739 LOC | Main header, Config struct, Pipeline enum |
| gta.h | `src/gta.h` | `src/gta.h` 332 LOC | `src/rw/gta.h` 335 LOC | GTA SA type definitions, reversed functions |
| gta.cpp | `src/gta.cpp` | `src/gta.cpp` 157 LOC | `src/rw/gta.cpp` 171 LOC | GTA SA function hooks and wrappers |
| vehiclePipe.cpp | `src/vehiclePipe.cpp` | `src/vehiclePipe.cpp` 1287 LOC | `src/render/vehiclePipe.cpp` 1608 LOC | Vehicle render callbacks. junior adds Env pipe. expIV adds GTAIV + Modern PBR |
| buildingPipe.cpp | `src/buildingPipe.cpp` | `src/buildingPipe.cpp` 808 LOC | `src/render/buildingPipe.cpp` 948 LOC | Building render callbacks. expIV adds GTAIV + PBR |
| pipelinecommon.cpp | `src/pipelinecommon.cpp` | `src/pipelinecommon.cpp` 385 LOC | `src/render/pipelinecommon.cpp` 460 LOC | Matrix helpers, shader loading, light uploaders |
| postfx.cpp | `src/postfx.cpp` | `src/postfx.cpp` 1639 LOC | `src/render/postfx.cpp` 2387 LOC | Post-processing effects |
| envmap.cpp | `src/envmap.cpp` | `src/envmap.cpp` 413 LOC | `src/render/envmap.cpp` 560 LOC | Reflection map rendering |
| pipeplg.cpp | `src/pipeplg.cpp` | `src/pipeplg.cpp` 63 LOC | `src/rw/pipeplg.cpp` 63 LOC | Pipeline plugin attach |
| defaultFuncs.cpp | `src/defaultFuncs.cpp` | `src/defaultFuncs.cpp` 439 LOC | `src/rw/defaultFuncs.cpp` 439 LOC | Default render callback functions |
| texdb.cpp | `src/texdb.cpp` | `src/texdb.cpp` 260 LOC | `src/extras/texdb.cpp` 251 LOC | Texture database with per-texture overrides |
| neo.cpp | `src/neo.cpp` | `src/neo.cpp` 187 LOC | `src/render/neo.cpp` 187 LOC | Neo vehicle pipe implementation |
| neo.h | `src/neo.h` | `src/neo.h` 171 LOC | `src/render/neo.h` 171 LOC | Neo pipe header |
| neoCarpipe.cpp | `src/neoCarpipe.cpp` | `src/neoCarpipe.cpp` 401 LOC | `src/render/neoCarpipe.cpp` 407 LOC | Neo car pipe render callbacks |
| neoWaterdrops.cpp | `src/neoWaterdrops.cpp` | `src/neoWaterdrops.cpp` 693 LOC | `src/render/neoWaterdrops.cpp` 693 LOC | Neo water/blood drop effects |
| MemoryMgr.h | `src/MemoryMgr.h` | `src/MemoryMgr.h` 89 LOC | `src/rw/MemoryMgr.h` 89 LOC | Memory management utilities |
| Pools.h | `src/Pools.h` | `src/Pools.h` 136 LOC | `src/rw/Pools.h` 136 LOC | Object pool management |
| LinkList.h | `src/LinkList.h` | `src/LinkList.h` 115 LOC | `src/rw/LinkList.h` 115 LOC | Linked list implementation |
| ModuleList.hpp | `src/ModuleList.hpp` | `src/ModuleList.hpp` 185 LOC | `src/rw/ModuleList.hpp` 185 LOC | Module enumeration for hooking |
| debugmenu_public.h | `src/debugmenu_public.h` | `src/debugmenu_public.h` 141 LOC | `src/extras/debugmenu_public.h` 141 LOC | Debug menu API |
| normmap_stubs.cpp | `src/normmap_stubs.cpp` | `src/normmap_stubs.cpp` 47 LOC | `src/rw/normmap_stubs.cpp` 70 LOC | Normal map stubs |

### junior-only Files

| File Name | Path | LOC | Notes |
|-----------|------|-----|-------|
| extendedplg.cpp | `src/extendedplg.cpp` | 42 LOC | EDED plugin for per-atomic shader assignment |

### skygfx_plus-only Files (expIV additions)

| File Name | Path | LOC | Notes |
|-----------|------|-----|-------|
| hooks.cpp | `src/Core/hooks.cpp` | 243 LOC | Standalone hook installation |
| hooks.h | `src/Core/hooks.h` | — | Hooks header |
| diagnostics.cpp | `src/Core/diagnostics.cpp` | 145 LOC | Debug logging, performance timers, crash handler |
| diagnostics.h | `src/Core/diagnostics.h` | — | Diagnostics header |
| main_exports.h | `src/Core/main_exports.h` | — | Main exports header |
| config.cpp | `src/Core/config.cpp` | 483 LOC | Expanded INI parsing (239+ config fields) |
| weather.cpp | `src/Core/weather.cpp` | 282 LOC | Weather/timecycle expansion (GTA V style) |
| weather.h | `src/Core/weather.h` | — | Weather header |
| presets.cpp | `src/Core/presets.cpp` | 133 LOC | Game preset definitions (16 presets) |
| debugmenu_ui.cpp | `src/Core/debugmenu_ui.cpp` | 514 LOC | Debug menu UI |
| chars.cpp | `src/entities/chars.cpp` | 368 LOC | Character rendering |
| chars.h | `src/entities/chars.h` | — | Character header |
| PlantSurfPropMgr.h | `src/entities/PlantSurfPropMgr.h` | — | Plant surface property manager |
| brdfLibrary.h | `src/extras/brdfLibrary.h` | — | BRDF library header |
| wheels.cpp | `src/extras/wheels.cpp` | 139 LOC | Wheel rendering |
| wheels.h | `src/extras/wheels.h` | — | Wheels header |
| wheels_extender.cpp | `src/extras/wheels_extender.cpp` | 283 LOC | Extended wheel system |
| wheels_extender.h | `src/extras/wheels_extender.h` | — | Wheel extender header |
| vehicles.cpp | `src/extras/vehicles.cpp` | 314 LOC | Vehicle registry |
| veh_shaders.cpp | `src/extras/veh_shaders.cpp` | 375 LOC | Vehicle shader data bridge |
| skygfx_bridge.cpp | `src/extras/skygfx_bridge.cpp` | 108 LOC | Shared memory IPC bridge |
| SMAA.cpp | `src/render/SMAA.cpp` | 316 LOC | SMAA anti-aliasing implementation |
| SMAA.h | `src/render/SMAA.h` | — | SMAA header |
| AreaTex.h | `src/render/AreaTex.h` | — | SMAA area texture |
| SearchTex.h | `src/render/SearchTex.h` | — | SMAA search texture |
| waterPipe.cpp | `src/render/waterPipe.cpp` | 138 LOC | Parallax water rendering |
| waterPipe.h | `src/render/waterPipe.h` | — | Water pipe header |
| PC_PlantsMgr.cpp | `src/render/PC_PlantsMgr.cpp` | 1736 LOC | Plant/vegetation management |
| PC_PlantsMgr.h | `src/render/PC_PlantsMgr.h` | — | Plants manager header |
| PC_PlantsMgr_overlay.cpp | `src/render/PC_PlantsMgr_overlay.cpp` | 857 LOC | Plant overlay rendering |
| PC_GrassRenderer.cpp | `src/render/PC_GrassRenderer.cpp` | 468 LOC | Grass rendering |
| PC_GrassRenderer.h | `src/render/PC_GrassRenderer.h` | — | Grass renderer header |
| GrassSystem.h | `src/render/GrassSystem.h` | — | Grass system header |
| postfx.h | `src/render/postfx.h` | — | PostFX header |
| iv_mode.cpp | `src/render/iv_mode.cpp` | 102 LOC | GTA IV rendering mode |
| ps2_mode.cpp | `src/render/ps2_mode.cpp` | 99 LOC | PS2 rendering mode defaults |
| xbox_mode.cpp | `src/render/xbox_mode.cpp` | 95 LOC | Xbox rendering mode defaults |
| mobile.cpp | `src/render/mobile.cpp` | 95 LOC | Mobile rendering mode defaults |
| custom_mode.cpp | `src/render/custom_mode.cpp` | 142 LOC | Custom rendering mode |
| pc_patched_mode.cpp | `src/render/pc_patched_mode.cpp` | 96 LOC | PC patched mode defaults |
| normalmap.cpp | `src/rw/normalmap.cpp` | 280 LOC | Normal map pipeline implementation |
| HLSL_hook.cpp | `src/rw/HLSL_hook.cpp` | 32 LOC | HLSL shader hook |
| HLSL_hook.h | `src/rw/HLSL_hook.h` | — | HLSL hook header |
| ColData.h | `src/rw/ColData.h` | — | Collision data header |

---

## 2. Shader Comparison

### Vertex Shaders

| Shader Name | aap? | junior? | skygfx_plus? | Entry Points | Purpose |
|-------------|------|---------|--------------|--------------|---------|
| vehicleVS.hlsl | Y (inline) | Y (inline) | Y | `main_vehicle` | Basic vehicle vertex transform |
| vehiclePipeVS.hlsl | — | — | Y | `main_vehicle`, `main_vehiclePBR`, `main_ps2CarFx`, `main_specCarFx`, `main_xboxCar`, `main_leedsCarFx`, `main_mobileVehicle`, `main_neoPass1`, `main_neoPass2`, `main_edgeTessellation` | Consolidated vehicle VS (all modes) |
| ps2BuildingVS.hlsl | Y (inline) | Y | Y | `main_ps2Building` | PS2 building vertex transform |
| ps2BuildingFxVS.hlsl | — | Y | Y | `main_ps2BuildingFx` | PS2 building effects VS |
| ps2BuildingWindVS.hlsl | — | Y | Y | `main_ps2BuildingWind` | PS2 building wind animation |
| ps2BuildingSSSVS.hlsl | — | — | Y | `main_ps2BuildingSSS` | PS2 building subsurface scattering |
| xboxBuildingVS.hlsl | — | Y | Y | `main_xboxBuilding` | Xbox building vertex transform |
| xboxBuildingWindVS.hlsl | — | Y | Y | `main_xboxBuildingWind` | Xbox building wind animation |
| sphereBuildingVS.hlsl | — | Y | Y | `main_sphereBuilding` | Sphere-mapped building VS |
| mobileBuildingVS.hlsl | — | — | Y | `main_mobileBuilding` | Mobile building VS |
| mobileVehicleVS.hlsl | — | — | Y | `main_mobileVehicle` | Mobile vehicle VS |
| neoVehiclePass1VS.hlsl | — | — | Y | `main_neoPass1` | Neo vehicle pass 1 VS |
| neoVehiclePass2VS.hlsl | — | — | Y | `main_neoPass2` | Neo vehicle pass 2 VS |
| postfxVS.hlsl | — | — | Y | `main_postfx` | Post-processing fullscreen quad |
| Water_VS.hlsl | — | — | Y | `main_water` | Water vertex transform |
| EdgeTessellationVS.hlsl | — | — | Y | `main_edgeTess` | Edge tessellation displacement |

### Pixel Shaders

| Shader Name | aap? | junior? | skygfx_plus? | Entry Points | Purpose |
|-------------|------|---------|--------------|--------------|---------|
| simplePS.hlsl | Y (inline) | Y | Y | `main_simple` | Basic texture * color |
| buildingPipePS.hlsl | — | — | Y | `main_simple`, `main_simpleDetail`, `main_simpleFog`, `main_xboxBuilding`, `main_normMapBuilding` | Consolidated building PS (5 entry points) |
| stochasticBuildingPS.hlsl | — | — | Y | `main_simpleStochastic`, `main_simpleDetailStochastic`, `main_xboxBuildingStochastic` | Consolidated stochastic building PS (3 entry points) |
| grassPS.hlsl | — | — | Y | `main_grass` | Grass rendering |
| VehiclePBR_Modern.hlsl | — | — | Y | `main`, `main_envCar`, `main_ps2EnvSpecFx`, `main_specCarFx`, `main_mobileVehicle`, `main_normMapVehicle` | Unified vehicle PBR (6 entry points) |
| Glass_Vehicle.hlsl | — | — | Y | `main_glass` | Vehicle glass rendering |
| Rubber_Vehicle.hlsl | — | — | Y | `main_rubber` | Tire rendering |
| CarPaint_Reflections.hlsl | — | — | Y | `main_carPaintReflections` | Car paint reflection |
| VehiclePaint_GTAIV.hlsl | — | — | Y | `main_vehiclePaintGTAIV` | GTA IV vehicle paint |
| GTAIV_ps20.hlsl | — | — | Y | `main_gtaiv` | GTA IV post-process |
| mobileVehiclePS.hlsl | — | — | Y | `main_mobileVehicle` | Mobile vehicle rendering |
| normMapVehiclePS.hlsl | — | — | Y | `main_normMapVehicle` | Normal-mapped vehicle |
| iiiTrailsPS.hlsl | — | Y | Y | `main_iiiTrails` | GTA III trails |
| vcTrailsPS.hlsl | — | Y | Y | `main_vcTrails` | GTA VC trails |
| radiosityPS.hlsl | — | Y | Y | `main_radiosity` | Shader-based radiosity |
| blurPS.hlsl | — | Y | Y | `main_blur` | Blur filter |
| gradingPS.hlsl | — | Y | Y | `main_grading` | Color grading |
| contrastPS.hlsl | — | Y | Y | `main_contrast` | Contrast adjustment |
| SMAA_Edge.hlsl | — | — | Y | `main_SMAAEdge` | SMAA edge detection |
| SMAA_BlendWeight.hlsl | — | — | Y | `main_SMAABlendWeight` | SMAA blend weight |
| SMAA_BlendNeighbor.hlsl | — | — | Y | `main_SMAABlendNeighbor` | SMAA neighborhood blend |
| SMAA_Temporal.hlsl | — | — | Y | `main_SMAATemporal` | SMAA temporal |
| SMAA_EdgeNormal.hlsl | — | — | Y | `main_SMAAEdgeNormal` | SMAA normal-based edge |
| SMAA_EdgeDepth.hlsl | — | — | Y | `main_SMAAEdgeDepth` | SMAA depth-based edge |
| SMAA_EdgeCombined.hlsl | — | — | Y | `main_SMAAEdgeCombined` | SMAA combined edge |
| SMAA_EdgeMotionDepth.hlsl | — | — | Y | `main_SMAAEdgeMotionDepth` | SMAA motion+depth edge |
| SSAO_ps20.hlsl | — | — | Y | `main_SSAO` | Screen-space AO |
| SSAO_ps20_simple.hlsl | — | — | Y | `main_SSAOSimple` | Simple SSAO |
| SSAO_VertexDepth.hlsl | — | — | Y | `main_SSADepth` | SSAO vertex depth |
| SSAO_ps20_depthonly.hlsl | — | — | Y | `main_SSAODepthOnly` | SSAO depth only |
| MotionBlur_Burnout.hlsl | — | — | Y | `main_motionBlur` | Burnout-style motion blur |
| ColorFilter_CrossMix.hlsl | — | — | Y | `main_crossMix` | Color filter cross-mixing |
| SkinEnhance.hlsl | — | — | Y | `main_skinEnhance` | Skin SSS enhancement |
| HairEnhance.hlsl | — | — | Y | `main_hairEnhance` | Hair anisotropic highlights |
| NormalBuffer.hlsl | — | — | Y | `main_normalBuffer` | Stereo disparity normals |
| PipeChain.hlsl | — | — | Y | `main_pipeChain` | 4-pass post-processing |
| ModernColorFilterPS.hlsl | — | — | Y | `main_modernColorFilter` | Modern color filter |
| GTA_SA_ModernColor.hlsl | — | — | Y | `main_gtaSaModernColor` | GTA SA modern color |
| PBR_Lighting.hlsl | — | — | Y | `main_pbrLighting` | PBR lighting functions |
| DynamicSky.hlsl | — | — | Y | `main_dynamicSky` | Dynamic sky rendering |
| IBL_SkyCloud.hlsl | — | — | Y | `main_iblSkyCloud` | IBL sky/cloud rendering |
| SkinSSS.hlsl | — | — | Y | `main_skinSSS` | Skin subsurface scattering |
| SSS_Blur.hlsl | — | — | Y | `main_sssBlur` | SSS blur pass |
| vectorMotionBlur.hlsl | — | — | Y | `main_vectorMotionBlur` | Vector motion blur |
| Water_Parallax.hlsl | — | — | Y | `main_waterParallax` | Parallax water |
| unifiedPipe.hlsl | — | — | Y | `main_unified` | 4-pass unified pipeline |
| Clamp.hlsl | — | — | Y | — | Clamp utility |

### Shader Includes

| Include Name | aap? | junior? | skygfx_plus? | Purpose |
|--------------|------|---------|--------------|---------|
| PBR_Common.hlsl | — | — | Y | Shared GGX/Smith/Schlick functions |
| StochasticSamplerPS.hlsl | — | Y | Y | Hash-based stochastic sampling |
| SubsurfaceScattering.hlsl | — | — | Y | SSS material IDs |
| CarPaintNoise.hlsl | — | — | Y | Paint noise functions |
| colorSpace.hlsl | — | — | Y | Color space conversions |
| PerlinNoise.hlsl | — | — | Y | Procedural noise |

### GTA IV CSOs (pre-compiled, in shaders/)

| CSO Name | Location | Notes |
|----------|----------|-------|
| GTAIVBuilding_ps.cso | `shaders/ps/` | GTA IV building pixel shader |
| GTAIVForwardPlus_ps.cso | `shaders/ps/` | GTA IV forward+ pixel shader |
| GTAIVVehicle_ps.cso | `shaders/ps/` | GTA IV vehicle pixel shader |
| GTAIVBuilding_vs.cso | `shaders/vs/` | GTA IV building vertex shader |
| GTAIVForwardPlus_vs.cso | `shaders/vs/` | GTA IV forward+ vertex shader |
| GTAIVVehicle_vs.cso | `shaders/vs/` | GTA IV vehicle vertex shader |
| normMapBuildingPS.cso | `shaders/ps/` | Normal-mapped building |
| normMapVehiclePS.cso | `shaders/ps/` | Normal-mapped vehicle |

---

## 3. Pipeline Mode Comparison

### Vehicle Pipeline Modes

| Mode | aap? | junior? | skygfx_plus? | INI Value | Shader Used | Description |
|------|------|---------|--------------|-----------|-------------|-------------|
| PS2 | Y | Y | Y | `PS2` | `simplePS` + `ps2EnvSpecFxPS` | Classic PS2 env+spec dual-layer |
| PC | Y | Y | Y | `PC` | Original game function | Unmodified GTA SA |
| Xbox | Y | Y | Y | `Xbox` | NULL (FFP) | Vertex-shader-only |
| Spec | Y | Y | Y | `Spec` | `simplePS` + `specCarFxPS` | Specular highlights |
| Neo | Y | Y | Y | `Neo` | Neo pipe shaders | Enhanced reflections |
| LCS/Leeds | Y | Y | Y | `LCS`/`Leeds` | `simplePS` + leeds VS | Leeds engine style |
| VCS | Y | Y | Y | `VCS` | `simplePS` + leeds VS | VCS variant |
| Mobile | Y | Y | Y | `Mobile` | `mobileVehiclePipePS` | Mobile-style env lerp |
| Env | — | Y | Y | `Env` | `envCarPS` / PBR | Environment mapping |
| GTAIV | — | — | Y | `GTAIV` | `gtaivVehiclePS` | GTA IV forward pass |
| Modern | — | — | Y | `Modern` | `VehiclePBR_Modern` + `Glass_Vehicle` + `Rubber_Vehicle` | Full PBR (GGX/Smith/Schlick) |

### Building Pipeline Modes

| Mode | aap? | junior? | skygfx_plus? | INI Value | Shader Used | Description |
|------|------|---------|--------------|-----------|-------------|-------------|
| PS2 | Y | Y | Y | `PS2` | `ps2BuildingVS` + `simplePS` | Classic PS2 building |
| Xbox/PC | Y | Y | Y | `PC`/`Xbox` | `xboxBuildingVS` + `xboxBuildingPS` | Xbox/PC enhanced with env maps |
| GTAIV | — | — | Y | `GTAIV` | GTAIV CSOs | GTA IV forward pass |
| PBR | — | — | Y | `PBR` | PBR building shaders | Physically-based rendering |

### Unified Pipeline Modes (skygfx_plus only)

| Mode | INI Value | Description |
|------|-----------|-------------|
| PBR | `PBR` | Unified PBR for all assets (buildings, vehicles, peds, terrain, vegetation, water) |
| PS2 | `PS2` | PS2-style rendering (locked preset) |
| Xbox | `Xbox` | Xbox-style rendering (locked preset) |
| Mobile | `Mobile` | Mobile-style rendering (locked preset) |
| GTAIV | `GTAIV` | GTA IV-style rendering (locked preset) |

---

## 4. INI Configuration Comparison

### Pipeline Selection

| INI Key | aap Default | junior Default | skygfx_plus Default | Notes |
|---------|-------------|---------------|-------------------|-------|
| `buildingPipe` | *(none)* | `PC` | *(none)* | Building rendering mode |
| `vehiclePipe` | *(none)* | `VCS` | *(none)* | Vehicle rendering mode |
| `colorFilter` | *(none)* | `VCS` | *(none)* | Color filter mode |
| `pipeline` | — | — | *(none)* | Unified pipeline mode (skygfx_plus only) |

### Core Settings

| INI Key | aap Default | junior Default | skygfx_plus Default | Notes |
|---------|-------------|---------------|-------------------|-------|
| `ps2Modulate` | 0 | 0 | 0 | Global PS2 modulate |
| `dualPass` | 0 | 0 | 0 | Global dual-pass |
| `detailMaps` | 0 | 1 | 1 | Detail map rendering |
| `stochasticTexturing` | — | 1 | — (in config) | Stochastic sampling |
| `sunGlare` | -1 | 0 | -1 | Sun glare effect |
| `neoWaterDrops` | -1 | 0 | -1 | Neo water drops |
| `neoBloodDrops` | 0 | 0 | 0 | Neo blood drops |
| `usePCTimecyc` | 0 | 0 | 0 | Use PC timecycle |
| `grassAddAmbient` | 0 | 1 | 0 | Grass add ambient |
| `grassBackfaceCull` | 1 | 1 | 1 | Grass backface culling |
| `grassFixPlacement` | *(none)* | *(none)* | 0 | Grass placement fix |
| `disableClouds` | 0 | 0 | 0 | Disable clouds |
| `disableGamma` | 0 | 0 | 0 | Disable gamma |
| `fixPcCarLight` | 0 | 0 | 0 | Fix PC car lighting |
| `lightningIlluminatesWorld` | 0 | 0 | 0 | Lightning illuminates world |

### Environment Mapping

| INI Key | aap Default | junior Default | skygfx_plus Default | Notes |
|---------|-------------|---------------|-------------------|-------|
| `envMapSize` | 256 | 256 | 256 | Reflection map size (power of 2) |
| `envShininessMult` | 1.0 | 1.0 | 1.0 | Environment shininess |
| `envSpecularityMult` | 1.0 | 1.0 | 1.0 | Environment specularity |
| `envPower` | 20.0 | 20.0 | 20.0 | Environment power |
| `envFresnel` | 0.7 | 0.7 | 0.7 | Fresnel reflection |
| `neoShininessMult` | 1.0 | 1.0 | 1.0 | Neo shininess |
| `neoSpecularityMult` | 1.0 | 1.0 | 1.0 | Neo specularity |
| `leedsShininessMult` | 1.0 | 1.0 | 1.0 | Leeds shininess |

### Radiosity

| INI Key | aap Default | junior Default | skygfx_plus Default | Notes |
|---------|-------------|---------------|-------------------|-------|
| `radiosity` | *(none)* | `Shader` | `Shader` | Radiosity mode (PS2/Shader) |
| `doRadiosity` | *(from game)* | 1 | *(from game)* | Enable radiosity |
| `radiosityFilterPasses` | 2 | 2 | 2 | Filter passes |
| `radiosityRenderPasses` | 1 | 1 | 1 | Render passes |
| `radiosityIntensity` | 0x23 | 0x23 | 0x23 | Intensity |

### VCS Trails

| INI Key | aap Default | junior Default | skygfx_plus Default | Notes |
|---------|-------------|---------------|-------------------|-------|
| `vcsTrails` | 0 | 0 | 0 | Enable VCS trails |
| `trailsLimit` | 80 | 80 | 80 | Trail limit |
| `trailsIntensity` | 38 | 38 | 38 | Trail intensity |

### Z-Write Threshold

| INI Key | aap Default | junior Default | skygfx_plus Default | Notes |
|---------|-------------|---------------|-------------------|-------|
| `zwriteThreshold` | 128 | 128 | 128 | Z-write threshold |

### Misc

| INI Key | aap Default | junior Default | skygfx_plus Default | Notes |
|---------|-------------|---------------|-------------------|-------|
| `keySwitch` | 0x0 | 0x0 | 0x0 | Key to switch configs |
| `keyReload` | 0x0 | 0x0 | 0x0 | Key to reload INI |
| `explicitBuildingPipe` | *(none)* | *(none)* | -1 | Explicit building pipe override |
| `coronaZtest` | *(none)* | *(none)* | -1 | Corona Z-test |
| `fixShadows` | *(none)* | *(none)* | 0 | Fix shadow rendering |
| `transparentLockon` | *(none)* | *(none)* | 0 | Transparent lock-on |
| `privateHooks` | *(none)* | *(none)* | 0 | Private hooks |

### SSAO (skygfx_plus only)

| INI Key | Default | Notes |
|---------|---------|-------|
| `ssaoEnable` | 1 | Enable SSAO |
| `ssaoRadius` | 0.8 | AO radius |
| `ssaoPower` | 1.5 | AO power |
| `ssaoKernelSize` | 16 | Kernel size |
| `ssaoSampleCount` | 16 | Sample count |

### SMAA

| INI Key | aap Default | junior Default | skygfx_plus Default | Notes |
|---------|-------------|---------------|-------------------|-------|
| `smaaEnable` | — | 1 | 1 | Enable SMAA |
| `smaaPreset` | — | 2 | 3 | Quality (0=LOW, 1=MED, 2=HIGH, 3=ULTRA) |
| `smaaPredication` | — | 0 | 0 | Depth-based edge detection |
| `smaaTemporal` | — | 0 | 0 | Temporal AA |

### GTA IV Mode

| INI Key | aap Default | junior Default | skygfx_plus Default | Notes |
|---------|-------------|---------------|-------------------|-------|
| `ivMode` | — | 0 | 0 | Enable GTA IV mode |
| `ivDesaturation` | — | 0.3 | 1.0 | Desaturation strength |
| `ivGamma` | — | 1.0 | 1.0 | Gamma correction |
| `ivVignetteIntensity` | — | 0.5 | 0.0 | Vignette intensity |
| `ivVignetteRadius` | — | 0.5 | 0.75 | Vignette radius |
| `ivVignetteContrast` | — | 2.0 | 1.5 | Vignette contrast |
| `ivBloomIntensity` | — | 0.15 | 0.0 | Bloom intensity |
| `ivExposure` | — | 1.0 | 1.0 | Exposure |

### Motion Blur (skygfx_plus only)

| INI Key | Default | Notes |
|---------|---------|-------|
| `motionBlurEnable` | 0 | Enable motion blur |
| `motionBlurStrength` | 0.5 | Blur strength |
| `motionBlurRadial` | 0.3 | Radial component |
| `motionBlurSpeedFactor` | 0.5 | Speed factor |
| `motionBlurCameraAware` | 1 | Reduce on fast camera |

### SSS (skygfx_plus only)

| INI Key | Default | Notes |
|---------|---------|-------|
| `sssPostProcessEnable` | 0 | Enable post-process SSS |
| `sssPostProcessStrength` | 0.3 | SSS strength |
| `sssPostProcessRadius` | 4.0 | Blur radius |
| `sssPostProcessThreshold` | 0.1 | Depth threshold |
| `skinEnhanceEnable` | 0 | Enable skin enhancement |
| `skinWrapFactor` | 0.5 | Wrap lighting factor |
| `skinSpecularPower` | 16.0 | Specular sharpness |
| `skinSpecularStrength` | 0.3 | Specular intensity |
| `skinSSSStrength` | 0.4 | SSS strength |
| `hairEnhanceEnable` | 0 | Enable hair enhancement |
| `hairAnisotropicPower` | 32.0 | Highlight sharpness |
| `hairAnisotropicStrength` | 0.5 | Highlight intensity |
| `hairSSSStrength` | 0.2 | Hair SSS strength |
| `vegetationEnhanceEnable` | 0 | Enable vegetation enhancement |
| `vegetationSSSStrength` | 0.3 | Translucency strength |
| `vegetationAmbientBoost` | 1.2 | Ambient multiplier |

### Normal Mapping (skygfx_plus only)

| INI Key | Default | Notes |
|---------|---------|-------|
| `enableNormalMaps` | 1 | Enable normal mapping |
| `normalMapIntensity` | 1.0 | Normal map intensity |
| `normalMapPlayerOnly` | 1 | Player-only normals |
| `normalMapBuilding` | 1 | Building normal maps |
| `normalMapVehicle` | 1 | Vehicle normal maps |
| `normalMapDebug` | 0 | Debug visualization |
| `normalMapDebugMode` | 0 | Debug mode |

### Edge Tessellation (skygfx_plus only)

| INI Key | Default | Notes |
|---------|---------|-------|
| `edgeTessEnable` | 0 | Enable edge tessellation |
| `edgeTessStrength` | 0.01 | Displacement strength |
| `edgeTessThreshold` | 0.1 | Edge detection threshold |

### Normal Buffer (skygfx_plus only)

| INI Key | Default | Notes |
|---------|---------|-------|
| `normalBufferEnable` | 0 | Enable faux normal buffer |
| `normalBufferOffset` | 0.5 | Stereo disparity offset |
| `normalBufferScale` | 1.0 | Normal scale |

### Pipe Chain (skygfx_plus only)

| INI Key | Default | Notes |
|---------|---------|-------|
| `pipeChainEnable` | 0 | Enable 4-pass pipe chain |
| `pipeChainIntensity` | 0.5 | Chain intensity |

### Unified Pipeline (skygfx_plus only)

| INI Key | Default | Notes |
|---------|---------|-------|
| `unifiedEnable` | 1 | Enable unified pipeline |
| `unifiedVersion` | 2 | Pipeline version |
| `unifiedSatBoost` | 0.08 | Saturation boost |
| `unifiedIblTintStrength` | 0.3 | IBL tint strength |
| `unifiedSsaoNoiseScale` | 4.0 | SSAO noise |
| `unifiedShadowSoftness` | 0.5 | Shadow softness |
| `unifiedCloudShadowStr` | 0.3 | Cloud shadow strength |
| `unifiedSunShadowStr` | 0.8 | Sun shadow strength |
| `unifiedVertexAOBoost` | 1.4 | Vertex AO boost |
| `unifiedDayReduction` | 0.15 | Day reduction |
| `unifiedPointLightOverride` | 0.2 | Point light override |
| `unifiedSmaaThreshold` | 0.1 | SMAA threshold |
| `unifiedSmaaCornerRounding` | 25.0 | SMAA corner rounding |
| `unifiedSmaaMaxSearchSteps` | 8.0 | SMAA max search steps |
| `unifiedShowMenu` | 0 | Show debug menu |
| `unifiedShowOverlay` | 0 | Show debug overlay |
| `unifiedDebugOcclusion` | 0 | Debug occlusion |
| `unifiedEnablePrePass` | 1 | Enable pre-pass |
| `unifiedEnableEdgeDetect` | 1 | Enable edge detection |
| `unifiedEnableOcclusion` | 1 | Enable occlusion |
| `unifiedEnableStoredShadows` | 1 | Enable stored shadows |
| `unifiedEnableCloudShadows` | 1 | Enable cloud shadows |
| `unifiedEnableSunShadows` | 1 | Enable sun shadows |
| `unifiedEnableTimeOfDay` | 1 | Enable time-of-day |
| `unifiedEnableVertexAO` | 1 | Enable vertex AO |
| `unifiedEnablePointLightOverride` | 1 | Enable point light override |
| `unifiedEnablePostPass` | 1 | Enable post-pass |
| `unifiedEnableIBL` | 1 | Enable IBL |
| `unifiedEnableIBLTint` | 1 | Enable IBL tint |
| `unifiedEnableSurfaceWeights` | 1 | Enable surface weights |
| `unifiedEnableGrading` | 1 | Enable color grading |
| `unifiedEnableGamma` | 1 | Enable gamma |

---

## 5. Feature Status

### Rendering Features

| Feature | aap | junior | skygfx_plus | Notes |
|---------|-----|--------|-------------|-------|
| PS2 Vehicle Pipe | Y | Y | Y | Classic PS2 env+spec |
| PC Vehicle Pipe | Y | Y | Y | Original game |
| Xbox Vehicle Pipe | Y | Y | Y | Vertex-shader-only |
| Spec Vehicle Pipe | Y | Y | Y | Specular highlights |
| Neo Vehicle Pipe | Y | Y | Y | Enhanced reflections |
| LCS Vehicle Pipe | Y | Y | Y | Leeds engine |
| VCS Vehicle Pipe | Y | Y | Y | VCS variant |
| Mobile Vehicle Pipe | Y | Y | Y | Mobile-style |
| Env Vehicle Pipe | — | Y | Y | Environment mapping |
| GTAIV Vehicle Pipe | — | — | Y | GTA IV forward pass |
| Modern PBR Vehicle Pipe | — | — | Y | GGX/Smith/Schlick PBR |
| PS2 Building Pipe | Y | Y | Y | Classic PS2 |
| Xbox Building Pipe | Y | Y | Y | Xbox/PC enhanced |
| GTAIV Building Pipe | — | — | Y | GTA IV forward pass |
| PBR Building Pipe | — | — | Y | Physically-based |
| Wind Animation | — | Y | Y | Building wind |
| Stochastic Texturing | — | Y | Y | Grain-free detail |
| Normal Mapping | — | — | Y | Per-pixel normals |
| Environment Mapping | Y | Y | Y | Reflection maps |
| Sphere Map Buildings | — | Y | Y | Sphere-mapped env |
| Detail Maps | — | Y | Y | Per-texture detail |
| Per-Atomic Shading | — | Y | Y | EDED plugin |
| Per-Texture Overrides | — | Y | Y | TexDB system |

### Post-Processing Features

| Feature | aap | junior | skygfx_plus | Notes |
|---------|-----|--------|-------------|-------|
| PS2 Color Filter | Y | Y | Y | Classic PS2 |
| PC Color Filter | Y | Y | Y | PC gamma 2.2 |
| Mobile Color Filter | — | Y | Y | Mobile-style |
| III Color Filter | — | Y | Y | GTA III |
| VC Color Filter | — | Y | Y | GTA Vice City |
| VCS Color Filter | — | Y | Y | GTA VCS |
| GTAIV Color Filter | — | — | Y | Filmic tonemapping |
| Modern Color Filter | — | — | Y | Modern grading |
| Radiosity (Fixed) | Y | Y | Y | Fixed-function |
| Radiosity (Shader) | — | Y | Y | Shader-based |
| VCS Radiosity | — | Y | Y | VCS variant |
| VCS Trails | — | Y | Y | Light trails |
| III Trails | — | Y | Y | GTA III trails |
| Grain Filter | Y | Y | Y | Film grain |
| Infrared Vision | Y | Y | Y | Special vision |
| Night Vision | Y | Y | Y | Special vision |
| Darkness Filter | Y | Y | Y | Screen darkening |
| Color Grading | — | Y | Y | Mobile grading |
| Contrast Adjustment | — | Y | Y | Screen contrast |
| SMAA | — | Y | Y | Anti-aliasing |
| SSAO | — | — | Y | Ambient occlusion |
| Motion Blur | — | — | Y | Burnout-style |
| SSS Post-Process | — | — | Y | Skin translucency |
| Skin Enhancement | — | — | Y | Wrap lighting |
| Hair Enhancement | — | — | Y | Anisotropic highlights |
| Vegetation Enhancement | — | — | Y | SSS-like translucency |
| Edge Tessellation | — | — | Y | Vertex displacement |
| Normal Buffer | — | — | Y | Stereo disparity |
| Pipe Chain | — | — | Y | 4-pass processing |
| Neo Water Drops | Y | Y | Y | Rain drops |
| Neo Blood Drops | — | Y | Y | Blood drops |
| SpeedFX | Y | Y | Y | Speed effects |
| YCbCr Correction | — | Y | Y | Color space correction |

### Vehicle System Features

| Feature | aap | junior | skygfx_plus | Notes |
|---------|-----|--------|-------------|-------|
| Per-Vehicle Classification | — | — | Y | Paint/glass/tire detection |
| Glass Tint System | — | — | Y | Taxi/cop/gang tints |
| PBR Material System | — | — | Y | GGX specular |
| Car Paint Reflections | — | — | Y | Sphere env + noise |
| GTA IV Vehicle Paint | — | — | Y | GTA IV style |
| Rubber/Tire Shader | — | — | Y | Tire-specific |
| Wheel Extender | — | — | Y | Extended wheel system |
| Vehicle Registry | — | — | Y | Model ID mapping |

### Quality System

| Feature | aap | junior | skygfx_plus | Notes |
|---------|-----|--------|-------------|-------|
| Quality Presets | — | — | Y | LOW/MED/HIGH/ULTRA |
| Game Presets | — | — | Y | 16 game presets |
| Config Profiles | Y | Y | Y | INI cycling |
| Multiple INIs | Y | Y | Y | skygfx.1.ini - skygfx.9.ini |

### Infrastructure

| Feature | aap | junior | skygfx_plus | Notes |
|---------|-----|--------|-------------|-------|
| Crash Handler | — | Y | Y | Exception handling |
| Debug Logging | — | Y | Y | File-based logging |
| Performance Timers | — | — | Y | QueryPerformanceCounter |
| Debug Menu | — | Y | Y | Dear ImGui |
| SAMP Compatibility | Y | Y | Y | Multiplayer fix |
| Subdirectory Structure | — | — | Y | Organized src/ |
| HLSL Shader Consolidation | — | — | Y | Merged entry points |
| Build Automation | — | — | Y | fix_build.py |
| Shared Memory Bridge | — | — | Y | IPC bridge |

---

## See Also

- [[Project Lineage]] — Version evolution and technical decisions
- [[File Inventory]] — Complete file listing for skygfx_plus
- [[INI Configuration]] — Full INI reference
- [[Vehicle Pipeline]] — Vehicle rendering details
- [[Building Pipeline]] — Building rendering details
- [[Shader Architecture]] — How shaders compile and load
- [[Implemented Features]] — Current feature status

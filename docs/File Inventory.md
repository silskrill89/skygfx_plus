# File Inventory

## Source Files (src/) — 67 files

### Root
| File | Lines | Purpose |
|------|-------|---------|
| `skygfx.h` | 739 | Root umbrella header, Config struct, externs |

### Core (src/Core/) — 11 files
| File | Lines | Purpose |
|------|-------|---------|
| `main.cpp` | 1996 | Entry point, hooks, config, crash handler |
| `config.cpp` | 483 | Expanded INI parsing (239+ config fields) |
| `hooks.cpp` | 243 | Standalone hook installation |
| `hooks.h` | — | Hooks header |
| `diagnostics.cpp` | 145 | Debug logging, performance timers, crash handler |
| `diagnostics.h` | — | Diagnostics header |
| `main_exports.h` | — | Main exports header |
| `weather.cpp` | 282 | Weather/timecycle expansion (GTA V style) |
| `weather.h` | — | Weather header |
| `presets.cpp` | 133 | Game preset definitions (16 presets) |
| `debugmenu_ui.cpp` | 514 | ImGui debug menu |

### Entities (src/entities/) — 3 files + 4 planned
| File | Lines | Purpose |
|------|-------|---------|
| `chars.cpp` | 368 | Character rendering |
| `chars.h` | — | Character header |
| `PlantSurfPropMgr.h` | — | Plant surface property manager |
| `ragdoll.h` | — | **PLANNED:** Ragdoll_c + RagdollManager_c class declarations |
| `ragdoll.cpp` | — | **PLANNED:** Core ragdoll physics (gravity, torque, friction) |
| `ragdoll_bone.h` | — | **PLANNED:** RagdollBoneData (velocity + keyframe quat wrapper) |
| `bone_data.cpp` | — | **PLANNED:** ms_boneInfos[32] + aBONETAG_ENUM_TAB[32] |

### Extras (src/extras/) — 10 files
| File | Lines | Purpose |
|------|-------|---------|
| `texdb.cpp` | 251 | Texture database with per-texture overrides |
| `debugmenu_public.h` | 141 | Debug menu API |
| `wheels.cpp` | 139 | Wheel rendering |
| `wheels.h` | — | Wheels header |
| `wheels_extender.cpp` | 283 | Extended wheel system |
| `wheels_extender.h` | — | Wheel extender header |
| `vehicles.cpp` | 314 | Vehicle registry |
| `veh_shaders.cpp` | 375 | Vehicle shader data bridge |
| `skygfx_bridge.cpp` | 108 | Shared memory IPC bridge |
| `brdfLibrary.h` | — | BRDF library header |

### Render (src/render/) — 28 files
| File | Lines | Purpose |
|------|-------|---------|
| `vehiclePipe.cpp` | 1608 | All vehicle pipeline render callbacks |
| `buildingPipe.cpp` | 948 | Building pipeline render callbacks |
| `postfx.cpp` | 2387 | SSAO, SMAA, motion blur, SSS, color filters |
| `postfx.h` | — | PostFX header |
| `envmap.cpp` | 560 | Reflection map rendering |
| `pipelinecommon.cpp` | 460 | Matrix helpers, shader loading, light uploaders |
| `SMAA.cpp` | 316 | SMAA anti-aliasing implementation |
| `SMAA.h` | — | SMAA header |
| `AreaTex.h` | — | SMAA area texture data |
| `SearchTex.h` | — | SMAA search texture data |
| `waterPipe.cpp` | 138 | Parallax water rendering |
| `waterPipe.h` | — | Water pipe header |
| `PC_PlantsMgr.cpp` | 1736 | Plant/vegetation management |
| `PC_PlantsMgr.h` | — | Plants manager header |
| `PC_PlantsMgr_overlay.cpp` | 857 | Plant overlay rendering |
| `PC_GrassRenderer.cpp` | 468 | Grass rendering |
| `PC_GrassRenderer.h` | — | Grass renderer header |
| `GrassSystem.h` | — | Grass system header |
| `neo.cpp` | 187 | Neo vehicle pipe implementation |
| `neo.h` | 171 | Neo pipe header |
| `neoCarpipe.cpp` | 407 | Neo car pipe render callbacks |
| `neoWaterdrops.cpp` | 693 | Neo water/blood drop effects |
| `iv_mode.cpp` | 102 | GTA IV rendering mode |
| `ps2_mode.cpp` | 99 | PS2 rendering mode defaults |
| `xbox_mode.cpp` | 95 | Xbox rendering mode defaults |
| `mobile.cpp` | 95 | Mobile rendering mode defaults |
| `custom_mode.cpp` | 142 | Custom rendering mode |
| `pc_patched_mode.cpp` | 96 | PC patched mode defaults |

### RenderWare (src/rw/) — 16 files
| File | Lines | Purpose |
|------|-------|---------|
| `gta.h` | 335 | GTA SA type definitions, reversed functions |
| `gta.cpp` | 171 | GTA SA function hooks and wrappers |
| `pipeplg.cpp` | 63 | Pipeline plugin attach |
| `defaultFuncs.cpp` | 439 | Default render callback functions |
| `normalmap.cpp` | 280 | Normal map pipeline implementation |
| `normmap_stubs.cpp` | 70 | Normal map stubs |
| `HLSL_hook.cpp` | 32 | HLSL shader hook |
| `HLSL_hook.h` | — | HLSL hook header |
| `extendedplg.cpp` | 42 | EDED plugin for per-atomic shader assignment |
| `ModuleList.hpp` | 185 | Module enumeration for hooking |
| `MemoryMgr.h` | 89 | Memory management utilities |
| `Pools.h` | 136 | Object pool management |
| `LinkList.h` | 115 | Linked list implementation |
| `ColData.h` | — | Collision data header |

---

## Shaders (shaders/) — 69 HLSL files

### Root
| File | Purpose |
|------|---------|
| `buildingPipePS.hlsl` | Consolidated building PS (5 entry points) |
| `stochasticBuildingPS.hlsl` | Consolidated stochastic building PS (3 entry points) |
| `vehiclePipeVS.hlsl` | Consolidated vehicle VS (10 entry points) |
| `unifiedPipe.hlsl` | Forward+ unified pipeline (WIP) |
| `shaders.bat` | Shader compilation batch script |

### Pixel Shaders (shaders/ps/) — 44 files
| File | Purpose |
|------|---------|
| `VehiclePBR_Modern.hlsl` | Unified vehicle PBR (6 entry points) |
| `Glass_Vehicle.hlsl` | Vehicle glass rendering |
| `Rubber_Vehicle.hlsl` | Tire rendering |
| `CarPaint_Reflections.hlsl` | Car paint reflections |
| `VehiclePaint_GTAIV.hlsl` | GTA IV vehicle paint |
| `GTAIV_ps20.hlsl` | GTA IV post-process |
| `mobileVehiclePS.hlsl` | Mobile vehicle rendering |
| `normMapVehiclePS.hlsl` | Normal-mapped vehicle |
| `simplePS.hlsl` | Basic texture * color |
| `grassPS.hlsl` | Grass rendering |
| `DynamicSky.hlsl` | Dynamic sky rendering |
| `IBL_SkyCloud.hlsl` | IBL sky/cloud rendering |
| `PBR_Lighting.hlsl` | PBR lighting functions |
| `SkinSSS.hlsl` | Skin subsurface scattering |
| `SSS_Blur.hlsl` | SSS blur pass |
| `SkinEnhance.hlsl` | Skin SSS enhancement |
| `HairEnhance.hlsl` | Hair anisotropic highlights |
| `NormalBuffer.hlsl` | Stereo disparity normals |
| `PipeChain.hlsl` | 4-pass post-processing |
| `MotionBlur_Burnout.hlsl` | Burnout-style motion blur |
| `vectorMotionBlur.hlsl` | Vector motion blur |
| `ColorFilter_CrossMix.hlsl` | Color filter cross-mixing |
| `ModernColorFilterPS.hlsl` | Modern color filter |
| `GTA_SA_ModernColor.hlsl` | GTA SA modern color |
| `radiosityPS.hlsl` | Shader-based radiosity |
| `blurPS.hlsl` | Blur filter |
| `gradingPS.hlsl` | Color grading |
| `contrastPS.hlsl` | Contrast adjustment |
| `iiiTrailsPS.hlsl` | GTA III trails |
| `vcTrailsPS.hlsl` | GTA VC trails |
| `Water_Parallax.hlsl` | Parallax water |
| `Clamp.hlsl` | Clamp utility |
| `SMAA_Edge.hlsl` | SMAA edge detection |
| `SMAA_BlendWeight.hlsl` | SMAA blend weight |
| `SMAA_BlendNeighbor.hlsl` | SMAA neighborhood blend |
| `SMAA_Temporal.hlsl` | SMAA temporal |
| `SMAA_EdgeNormal.hlsl` | SMAA normal-based edge |
| `SMAA_EdgeDepth.hlsl` | SMAA depth-based edge |
| `SMAA_EdgeCombined.hlsl` | SMAA combined edge |
| `SMAA_EdgeMotionDepth.hlsl` | SMAA motion+depth edge |
| `SSAO_ps20.hlsl` | Screen-space AO |
| `SSAO_ps20_simple.hlsl` | Simple SSAO |
| `SSAO_VertexDepth.hlsl` | SSAO vertex depth |
| `SSAO_ps20_depthonly.hlsl` | SSAO depth only |
| `SMAA.hlsli` | SMAA shared include |
| `SSAO.hlsli` | SSAO shared include |

### Vertex Shaders (shaders/vs/) — 14 files
| File | Purpose |
|------|---------|
| `ps2BuildingVS.hlsl` | PS2 building vertex transform |
| `ps2BuildingFxVS.hlsl` | PS2 building effects VS |
| `ps2BuildingWindVS.hlsl` | PS2 building wind animation |
| `ps2BuildingSSSVS.hlsl` | PS2 building subsurface scattering |
| `xboxBuildingVS.hlsl` | Xbox building vertex transform |
| `xboxBuildingWindVS.hlsl` | Xbox building wind animation |
| `sphereBuildingVS.hlsl` | Sphere-mapped building VS |
| `mobileBuildingVS.hlsl` | Mobile building VS |
| `mobileVehicleVS.hlsl` | Mobile vehicle VS |
| `neoVehiclePass1VS.hlsl` | Neo vehicle pass 1 VS |
| `neoVehiclePass2VS.hlsl` | Neo vehicle pass 2 VS |
| `postfxVS.hlsl` | Post-processing fullscreen quad |
| `Water_VS.hlsl` | Water vertex transform |
| `EdgeTessellationVS.hlsl` | Edge tessellation displacement |
| `vehicleVS.hlsl` | Basic vehicle vertex transform |

### Shader Includes (shaders/include/) — 6 files
| File | Purpose |
|------|---------|
| `PBR_Common.hlsl` | Shared GGX/Smith/Schlick functions |
| `StochasticSamplerPS.hlsl` | Hash-based stochastic sampling |
| `SubsurfaceScattering.hlsl` | SSS material IDs |
| `CarPaintNoise.hlsl` | Paint noise functions |
| `colorSpace.hlsl` | Color space conversions |
| `PerlinNoise.hlsl` | Procedural noise |

### Pre-compiled GTAIV CSOs (shaders/ps/ and shaders/vs/)
| File | Location | Purpose |
|------|----------|---------|
| `GTAIVBuilding_ps.cso` | `shaders/ps/` | GTA IV building pixel shader |
| `GTAIVForwardPlus_ps.cso` | `shaders/ps/` | GTA IV forward+ pixel shader |
| `GTAIVVehicle_ps.cso` | `shaders/ps/` | GTA IV vehicle pixel shader |
| `normMapBuildingPS.cso` | `shaders/ps/` | Normal-mapped building |
| `normMapVehiclePS.cso` | `shaders/ps/` | Normal-mapped vehicle |
| `GTAIVBuilding_vs.cso` | `shaders/vs/` | GTA IV building vertex shader |
| `GTAIVForwardPlus_vs.cso` | `shaders/vs/` | GTA IV forward+ vertex shader |
| `GTAIVVehicle_vs.cso` | `shaders/vs/` | GTA IV vehicle vertex shader |

---

## Installer (installer/)
| File | Purpose |
|------|---------|
| `__init__.py` | Package init |
| `config.py` | Installer configuration |
| `downloader.py` | File downloader |
| `extractor.py` | Archive extractor |
| `backup.py` | Backup handler |
| `cache.py` | Download cache |
| `sa_detector.py` | GTA SA installation detector |
| `sa_hashes.py` | GTA SA file hash verification |
| `installer_stages.py` | Installation stage orchestration |
| `gamecopyworld.py` | GameCopyWorld mirror support |
| `scraper.py` | Download page scraper |

Also: `installer.py` (top-level entry point), `installer.spec` (PyInstaller spec)

---

## Resources (resources/)
| File | Purpose |
|------|---------|
| `resource.h` | IDR defines |
| `Resource.rc` | CSO → IDR mappings |
| `Resource.aps` | Resource editor data |
| `VersionInfo.h` | Version info header |
| `VersionInfo.rc` | Version resource |
| `textures.txd` | Texture dictionary |

### resources/cso/ — 73 pre-compiled shader objects
Compiled .cso files for all shaders. Used as fallback when runtime HLSL compilation fails.

### resources/textures/ — Texture assets
| File | Purpose |
|------|---------|
| `env_chrome.png/raw` | Chrome environment map |
| `env_glass.png/raw` | Glass environment map |
| `env_paint.png/raw` | Paint environment map |
| `env_rubber.png/raw` | Rubber environment map |
| `vehicles_effects.txd` | Vehicle effect textures |

---

## Build (build/)
| File | Purpose |
|------|---------|
| `skygfx.sln` | Visual Studio solution |
| `skygfx.vcxproj` | MSBuild project |
| `skygfx.vcxproj.filters` | Project file filters |
| `skygfx.vcxproj.user` | User-specific project settings |

---

## Tools (tools/)
| File | Purpose |
|------|---------|
| `fix_build.py` | Build automation (SDK detection, shader compilation, MSBuild) |
| `fast_build.py` | Quick build script |
| `build.bat` | Build batch script |
| `compile_shaders.bat` | Shader compilation script |
| `release_packager.py` | Release packaging |
| `dff_analyzer.py` | DFF model analyzer |
| `wheel_audit.py` | Wheel system audit tool |
| `wheel_extractor.py` | Wheel mesh extractor |
| `wheel_extractor.cpp` | Wheel extractor (native) |
| `wheel_pipeline.py` | Wheel processing pipeline |
| `generate_envmaps.py` | Environment map generator |
| `generate_normalmap.py` | Normal map generator |
| `gta_normalmap_gen.py` | GTA-specific normal map generator |
| `txd_builder.py` | Texture dictionary builder |
| `extract_pdf_info.py` | PDF info extractor |
| `proto.py` | Prototype script |

---

## Backup (backup_original/)
33 files from the junior_dr fork, preserved for reference. Flat `src/` layout with ~10K LOC (.cpp).

---

## See Also
- [[Shader Architecture]] — How shaders are organized
- [[Build System]] — How to build
- [[Three-Codebase Comparison]] — File mapping across all three codebases
- [[Roadmap to Ultimate Mod]] — Development roadmap

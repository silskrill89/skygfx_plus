# File Inventory

#reference #files

## Source Files (src/)

### Root
| File | Lines | Purpose |
|------|-------|---------|
| `skygfx.h` | ~770 | Root umbrella header, Config struct, externs |
| `main.cpp` | ~2200 | Entry point, hooks, config, crash handler |

### Core (src/Core/)
| File | Lines | Purpose |
|------|-------|---------|
| `config.cpp` | — | INI parsing, preset configs |
| `debugmenu_ui.cpp` | — | ImGui debug menu (deferred) |
| `envmap.cpp` | ~500 | Reflection map rendering, normal camera |
| `pipelinecommon.cpp` | ~475 | Matrix helpers, shader loading, light uploaders |
| `postfx.cpp` | ~800 | SSAO, SMAA, color filters, SSS |
| `presets.cpp` | — | Game preset definitions |
| `texdb.cpp` | — | Texture database |
| `veh_shaders.cpp` | — | Vehicle shader data bridge |
| `vehicles.cpp` | — | Vehicle registry |
| `wheels_extender.cpp` | — | Wheel extension logic |
| `RenderPipeline.h/cpp` | — | Unified pipeline controller |

### Entities (src/entities/)
| File | Purpose |
|------|---------|
| `ped.cpp` | Pedestrian rendering |
| `vehiclePipe.cpp` | All vehicle pipeline render callbacks (~1675 lines) |
| `buildingPipe.cpp` | Building pipeline render callbacks (~950 lines) |

### Extras (src/extras/)
| File | Purpose |
|------|---------|
| `normalmap.cpp` | Normal map integration (deferred) |
| `normalmap_plugin.cpp` | Normal map plugin (deferred) |
| `carpaint.cpp` | Car paint extras |
| `clouds.cpp` | Cloud rendering |
| `deferred.cpp` | Deferred rendering helpers |
| `edge_detection.cpp` | Edge detection for SMAA |
| `ssao_extras.cpp` | SSAO enhancements |
| `vegetation.cpp` | Vegetation rendering |
| `wind.cpp` | Wind animation |
| `yuv.cpp` | YCbCr color correction |

### Render (src/render/)
30 files covering pipeline implementations, shader management, and rendering passes.

### RenderWare (src/rw/)
14 files for RW engine integration and hooks.

## Shaders (shaders/)

### Merged Building PS
| Entry Points | Original Files |
|-------------|----------------|
| `buildingPipePS.hlsl` (5) | simplePS, simpleDetailPS, simpleFogPS, xboxBuildingPS, normMapBuildingPS |
| `stochasticBuildingPS.hlsl` (3) | simpleStochasticPS, simpleDetailStochasticPS, xboxBuildingStochasticPS |

### Vehicle PS (all in VehiclePBR_Modern.hlsl)
| Entry Points | Original Files |
|-------------|----------------|
| `main` | VehiclePBR_Modern (PBR) |
| `main_envCar` | envCarPS |
| `main_ps2EnvSpecFx` | ps2EnvSpecFxPS |
| `main_specCarFx` | specCarFxPS |
| `main_mobileVehicle` | mobileVehiclePS |
| `main_normMapVehicle` | normMapVehiclePS |

### Standalone Vehicle PS
| File | Purpose |
|------|---------|
| `Glass_Vehicle.hlsl` | Glass/light mesh rendering |
| `Rubber_Vehicle.hlsl` | Tire rendering |
| `CarPaint_Reflections.hlsl` | Car paint reflections |
| `VehiclePaint_GTAIV.hlsl` | GTA IV vehicle paint |

### PostFX PS
| File | Purpose |
|------|---------|
| `SSAO_ps20.hlsl` | SSAO with normal buffer |
| `SMAA_*.hlsl` (8 files) | SMAA passes |
| `MotionBlur_Burnout.hlsl` | Speed-based motion blur |
| `ColorFilter_CrossMix.hlsl` | Color filter mixing |
| `SkinEnhance.hlsl` | Skin SSS enhancement |
| `HairEnhance.hlsl` | Hair anisotropic highlights |
| `NormalBuffer.hlsl` | Stereo disparity normals |
| `PipeChain.hlsl` | 4-pass post-processing |
| `unifiedPipe.hlsl` | Forward+ unified pipeline (WIP) |

### Includes
| File | Purpose |
|------|---------|
| `PBR_Common.hlsl` | Shared GGX/Smith/Schlick functions |
| `StochasticSamplerPS.hlsl` | Hash-based stochastic sampling |
| `SubsurfaceScattering.hlsl` | SSS material IDs |
| `CarPaintNoise.hlsl` | Paint noise functions |

### Additional Shaders (80+ total)
80+ HLSL files covering building, vehicle, post-processing, and utility shaders.

## Installer
| File | Purpose |
|------|---------|
| `installer/sas_1987.py` | Python SAS 1987 installer |

## Resources
| File | Purpose |
|------|---------|
| `resources/resource.h` | IDR defines |
| `resources/Resource.rc` | CSO → IDR mappings |
| `resources/*.cso` | Pre-compiled shader objects |
| `resources/*.png` | Texture assets |

## Build
| File | Purpose |
|------|---------|
| `build/skygfx.vcxproj` | MSBuild project |
| `tools/fix_build.py` | Build automation |
| `premake5.lua` | Premake build (alternative) |

## See Also
- [[Shader Architecture]] — How shaders are organized
- [[Build System]] — How to build
- [[Roadmap to Ultimate Mod]] — Development roadmap

# File Inventory

#reference #files

## Source Files (src/)

### Core
| File | Lines | Purpose |
|------|-------|---------|
| `main.cpp` | ~2200 | Entry point, hooks, config, crash handler |
| `pipelinecommon.cpp` | ~475 | Matrix helpers, shader loading, light uploaders |
| `skygfx.h` | ~770 | Main header, Config struct, externs |
| `vehiclePipe.cpp` | ~1675 | All vehicle pipeline render callbacks |
| `buildingPipe.cpp` | ~950 | Building pipeline render callbacks |
| `postfx.cpp` | ~800 | SSAO, SMAA, color filters, SSS |
| `envmap.cpp` | ~500 | Reflection map rendering, normal camera |

### Configuration
| File | Purpose |
|------|---------|
| `config.cpp` | INI parsing, preset configs |
| `presets.cpp` | Game preset definitions |
| `veh_shaders.cpp` | Vehicle shader data bridge |
| `vehicles.cpp` | Vehicle registry |
| `texdb.cpp` | Texture database |

### Supporting
| File | Purpose |
|------|---------|
| `RenderPipeline.h/cpp` | Unified pipeline controller |
| `shaders.h` | Central shader hub header |

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

### Includes
| File | Purpose |
|------|---------|
| `PBR_Common.hlsl` | Shared GGX/Smith/Schlick functions |
| `StochasticSamplerPS.hlsl` | Hash-based stochastic sampling |
| `SubsurfaceScattering.hlsl` | SSS material IDs |
| `CarPaintNoise.hlsl` | Paint noise functions |

## Resources
| File | Purpose |
|------|---------|
| `resources/resource.h` | IDR defines |
| `resources/Resource.rc` | CSO → IDR mappings |

## Build
| File | Purpose |
|------|---------|
| `build/skygfx.vcxproj` | MSBuild project |
| `tools/fix_build.py` | Build automation |
| `premake5.lua` | Premake build (alternative) |

## See Also
- [[Shader Architecture]] — How shaders are organized
- [[Build System]] — How to build

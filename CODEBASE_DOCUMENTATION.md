# SkyGfx Plus expIV - Complete Codebase Documentation

## Project Overview
**Repository**: E:\dev(dave)\skygfx_plus_expIV
**Branch**: gta_IV_focus
**Base**: expIV fork (based on aap's skygfx, via junior → zeneric → plk4 lineage)
**Target**: GTA: San Andreas 1.0 US (0x94BF)
**Build**: Visual Studio 2022, Release x86 (Win32), Static Runtime

---

## Source Code Structure

### Core Headers

#### src/skygfx.h
Main configuration and type definitions. Key features:
- **CarPipeline enum**: PS2, PC, XBOX, SPEC, MOBILE, NEO, LCS, VCS, ENV, GTAIV
- **BuildingPipeline enum**: PS2, XBOX
- **DefinedVertexShader enum**: DEFAULT, WIND
- **Config struct**: 239 fields covering all rendering features:
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
- **TexInfo struct**: Texture database with detail maps, stochastic/dual-pass flags
- **CPostEffects**: 377 lines of static members and function declarations
- **Shader externs**: 60+ shader pointers for all pipelines

#### src/gta.h
Game structure definitions (CVector, CMatrix, CEntity, etc.)

---

### Core Implementation

#### src/main.cpp (2266 lines)
**Key Systems**:
1. **Crash Handler** (lines 15-60): SEH handler with auto-fixes for known crashes
2. **Debug Logging** (lines 62-88): Timestamped file logging to skygfx_dbg.log
3. **Hook Framework** (lines 110+): InjectHook, InterceptCall, Patch helpers
4. **Pipelines**:
   - Building pipeline hooks (CTagManager__SetupAtomic, CTagManager__RenderTag)
   - Vehicle pipeline hooks (CustomPipeAtomicSetup_Hook)
   - Dual-pass rendering (D3D9RenderDual, D3D9RenderBlack_DUAL)
   - Grass renderer (grassRenderCallback, CPlantMgr_Initialise)
5. **RenderScene Hook** (lines 740-760): Before/after frame callbacks
6. **Initialisation** (lines 770-800): InitialiseGame_hook with envmap, neo, texdb
7. **DllMain** (lines 1519+): Entry point, version check, hook installation
8. **Config System** (lines 859-1112): INI parsing, StrAssoc mapping, config reload
9. **Menu System** (lines 1177-1364): DebugMenu integration
10. **Delayed Patches** (lines 1366-1517): Applied at IsAlreadyRunning hook

**Important Fixes in expIV**:
- Lines 8-11: `RwEngineInstance` global for rpnormmap.lib
- Line 26-30: `_mm_loadu_si64` x86 compatibility macro

#### src/skygfx.h (lines 26-30)
```cpp
#ifdef _M_X86
#define _mm_loadu_si64 _mm_loadu_si32
#endif
```

---

### Pipeline Implementations

#### src/buildingPipe.cpp
- PS2, Xbox, GTAIV building pipelines
- Stochastic detail maps, wind shaders
- PS2 modulation, dual-pass support
- Tag rendering callbacks

#### src/vehiclePipe.cpp
- CAR_PS2, CAR_PC, CAR_XBOX, CAR_SPEC, CAR_MOBILE, CAR_NEO, CAR_LCS, CAR_VCS, CAR_ENV, CAR_GTAIV
- Environment mapping (Leeds, Neo)
- Specular, Fresnel, custom shaders
- Upgrade parts pipeline assignment

#### src/pipelinecommon.cpp
- Matrix helpers (RwToD3DMatrix, MakeProjectionMatrix)
- Shader constant uploaders (light, material, world/view/proj)
- Fixed transform multiplication order

---

### Post-Processing

#### src/postfx.cpp
**Effects Implemented**:
- **Radiosity**: PS2, Shader, VCS variants
- **ColourFilter**: PS2, PC, Mobile, III, VC, VCS, GTAIV, Generic
- **DarknessFilter**: Fixed alpha handling
- **Infrared/Night Vision**: PS2 variants with grain
- **Grain**: PS2 noise pattern
- **DrawFinalEffects**: Full post-processing chain
- **SSAO**: Screen-space ambient occlusion
- **SMAA**: Subpixel Morphological Anti-Aliasing

#### src/SMAA.cpp
- SMAA implementation with area/search textures
- Presets: LOW, MEDIUM, HIGH, ULTRA

---

### Environment Mapping

#### src/envmap.cpp
- Reflection camera/raster creation
- Vehicle reflection rendering (Leeds, Neo)
- Mirror fixes

#### src/neo.cpp / src/neoCarpipe.cpp / src/neoWaterdrops.cpp
- Neo vehicle pipeline
- Water drop effects
- Blood drop effects

---

### Texture Database

#### src/texdb.cpp
- TXD slot management
- Texture name associations
- Detail map system
- Stochastic/dual-pass per-texture settings

---

### Extended Features

#### src/extendedplg.cpp
- Wind vertex shader (WIND)
- Stochastic detail blending
- GTAIV forward rendering passes

#### src/PC_GrassRenderer.cpp / .h
- PC grass renderer with PS2 modulation
- Custom plant models

---

### Utilities

#### src/config.cpp
- INI parsing (linb::ini)
- Config field mapping via StrAssoc
- 10 config slots support

#### src/pipeplg.cpp
- Pipeline plugin attachment

#### src/defaultFuncs.cpp
- Atomic render callbacks

#### src/texdb.cpp
- Texture database management

---

## Shaders (resources/cso/)

**Vertex Shaders**:
- ps2BuildingVS, ps2BuildingFxVS, ps2BuildingWindVS
- xboxBuildingVS, xboxBuildingWindVS
- vehicleVS, ps2CarFxVS, xboxCarVS, leedsCarFxVS
- mobileVehicleVS, neoVehiclePass1VS, neoVehiclePass2VS
- sphereBuildingVS, gtaivVehicleVS, gtaivBuildingVS, gtaivFPVS
- postfxVS, envCarVS

**Pixel Shaders**:
- ps2BuildingPS, xboxBuildingPS, xboxBuildingStochasticPS
- simplePS, simpleDetailPS, simpleDetailStochasticPS, simpleFogPS
- specCarFxPS, envCarPS, ps2EnvSpecFxPS
- radiosityPS, blurPS, contrastPS, gradingPS
- iiiTrailsPS, vcTrailsPS, grassPS
- SMAA, SSAO, GTAIV_PS
- gtaivVehiclePS, gtaivBuildingPS

---

## Build Configuration

### premake5.lua
- Workspace: "skygfx"
- Configurations: Release, Debug
- Target: SharedLib (.dll)
- CharacterSet: MBCS
- Static Runtime
- Platform: Win32 (x86)
- **Pre-build**: Compiles all HLSL shaders via fxc.exe (ps_3_0/vs_3_0)
- **Post-build (Release)**: Copies skygfx.dll → E:\games\gtasa_skygfx_plus\skygfx.asi
- Defines: `_CRT_USE_MM_LOADU_SI64=0`, threadSafeInit disabled

### build\build.bat
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
| ImGui | E:\SDKs\imgui-master |
| plugin-sdk | E:\SDKs\plugin-sdk-master |

---

## Fixes Applied (This Session)

### 1. SSE2 Intrinsic Fix (skygfx.h:26-30)
```cpp
#ifdef _M_X86
#define _mm_loadu_si64 _mm_loadu_si32
#endif
```
Maps x64-only `_mm_loadu_si64` to x86-compatible `_mm_loadu_si32`

### 2. RwEngineInstance Global (main.cpp:1-11)
```cpp
#ifdef RwEngineInstance
#undef RwEngineInstance
#endif
#include <rpnormmap.h>
extern "C" void *RwEngineInstance = NULL;
```
Required by rpnormmap.lib for normal map plugin linkage

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
- **Build Output**: E:\dev(dave)\skygfx_plus_expIV\bin\Release\skygfx.dll (421,376 bytes)
- **Deploy To**: E:\games\gtasa_skygfx_plus\skygfx.asi
- **Plugins**: SilentPatch, CLEO, GInput, MoonLoader, modloader

---

## Next Steps
1. Run build: `cmd /c "cd E:\dev(dave)\skygfx_plus_expIV\build && build.bat"`
2. Verify skygfx.dll generated in bin\Release\
3. Copy to exp install, rename to skygfx.asi
4. Launch gta_sa.exe, check skygfx_dbg.log
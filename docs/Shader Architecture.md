# Shader Architecture

#shaders #architecture

## Overview
All shaders compile to CSO (Compiled Shader Object) via `fxc.exe` during build. Each HLSL file can contain multiple entry points — `fxc` compiles one entry point per invocation with `/E <entry>`. The C++ side loads CSOs from embedded resources via `makePS()`/`makeVS()`.

## Directory Layout
```
shaders/
├── include/                    # Shared includes (7 files)
│   ├── PBR_Common.hlsl         # GGX/Smith/Schlick, cloud FBM, Fresnel
│   ├── StochasticSamplerPS.hlsl # Hash-based stochastic texture sampling
│   ├── colorSpace.hlsl         # YCbCr, linear/gamma conversions
│   ├── SubsurfaceScattering.hlsl # SSS material IDs, wrap lighting
│   ├── CarPaintNoise.hlsl      # Paint noise functions
│   └── PerlinNoise.hlsl        # Procedural noise
├── vs/                         # Vertex shaders (16 files)
│   ├── vehicleVS.hlsl          # Basic vehicle vertex transform
│   ├── ps2BuildingVS.hlsl      # PS2 building vertex transform
│   ├── ps2BuildingFxVS.hlsl    # PS2 building effects VS
│   ├── ps2BuildingWindVS.hlsl  # PS2 building wind animation
│   ├── ps2BuildingSSSVS.hlsl   # PS2 building SSS
│   ├── xboxBuildingVS.hlsl     # Xbox building vertex transform
│   ├── xboxBuildingWindVS.hlsl # Xbox building wind animation
│   ├── sphereBuildingVS.hlsl   # Sphere-mapped building VS
│   ├── mobileBuildingVS.hlsl   # Mobile building VS
│   ├── mobileVehicleVS.hlsl    # Mobile vehicle VS
│   ├── neoVehiclePass1VS.hlsl  # Neo vehicle pass 1 VS
│   ├── neoVehiclePass2VS.hlsl  # Neo vehicle pass 2 VS
│   ├── postfxVS.hlsl           # Post-processing fullscreen quad
│   ├── Water_VS.hlsl           # Water vertex transform
│   └── EdgeTessellationVS.hlsl # Edge tessellation displacement
├── ps/                         # Pixel shaders (35 files)
│   ├── simplePS.hlsl           # Basic texture * color
│   ├── grassPS.hlsl            # Grass rendering
│   ├── VehiclePBR_Modern.hlsl  # Unified vehicle PBR (7 entry points)
│   ├── Glass_Vehicle.hlsl      # Vehicle glass rendering
│   ├── Rubber_Vehicle.hlsl     # Tire rendering (standalone)
│   ├── CarPaint_Reflections.hlsl # Car paint reflection
│   ├── VehiclePaint_GTAIV.hlsl # GTA IV vehicle paint
│   ├── normMapVehiclePS.hlsl   # Normal-mapped vehicle
│   ├── mobileVehiclePS.hlsl    # Mobile vehicle rendering
│   ├── GTAIV_ps20.hlsl         # GTA IV post-process
│   ├── iiiTrailsPS.hlsl        # GTA III trails
│   ├── vcTrailsPS.hlsl         # GTA VC trails
│   ├── radiosityPS.hlsl        # Shader-based radiosity
│   ├── blurPS.hlsl             # Blur filter
│   ├── gradingPS.hlsl          # Color grading
│   ├── contrastPS.hlsl         # Contrast adjustment
│   ├── SMAA_Edge.hlsl          # SMAA edge detection
│   ├── SMAA_BlendWeight.hlsl   # SMAA blend weight
│   ├── SMAA_BlendNeighbor.hlsl # SMAA neighborhood blend
│   ├── SMAA_Temporal.hlsl      # SMAA temporal
│   ├── SMAA_EdgeNormal.hlsl    # SMAA normal-based edge
│   ├── SMAA_EdgeDepth.hlsl     # SMAA depth-based edge
│   ├── SMAA_EdgeCombined.hlsl  # SMAA combined edge
│   ├── SMAA_EdgeMotionDepth.hlsl # SMAA motion+depth edge
│   ├── SSAO_ps20.hlsl          # Screen-space AO
│   ├── SSAO_ps20_simple.hlsl   # Simple SSAO
│   ├── SSAO_VertexDepth.hlsl   # SSAO vertex depth
│   ├── SSAO_ps20_depthonly.hlsl # SSAO depth only
│   ├── MotionBlur_Burnout.hlsl # Burnout-style motion blur
│   ├── ColorFilter_CrossMix.hlsl # Color filter cross-mixing
│   ├── SkinEnhance.hlsl        # Skin SSS enhancement
│   ├── HairEnhance.hlsl        # Hair anisotropic highlights
│   ├── NormalBuffer.hlsl       # Stereo disparity normals
│   ├── PipeChain.hlsl          # 4-pass post-processing
│   ├── SkinSSS.hlsl            # Skin subsurface scattering
│   ├── SSS_Blur.hlsl           # SSS blur pass
│   ├── vectorMotionBlur.hlsl   # Vector motion blur
│   ├── Water_Parallax.hlsl     # Parallax water
│   ├── DynamicSky.hlsl         # Dynamic sky rendering
│   ├── IBL_SkyCloud.hlsl       # IBL sky/cloud rendering
│   ├── PBR_Lighting.hlsl       # PBR lighting functions
│   ├── ModernColorFilterPS.hlsl # Modern color filter
│   ├── GTA_SA_ModernColor.hlsl # GTA SA modern color
│   └── Clamp.hlsl              # Color clamping
├── buildingPipePS.hlsl         # Consolidated building PS (5 entry points)
├── stochasticBuildingPS.hlsl   # Consolidated stochastic building PS (3 entry points)
├── vehiclePipeVS.hlsl          # Consolidated vehicle VS (10 entry points)
└── unifiedPipe.hlsl            # 4-pass forward+ post-processing chain
```

## Consolidation Model
Individual HLSL files are now **thin wrappers** that `#include` the merged source, or the merged source is compiled directly with multiple entry points. For example:

```hlsl
// buildingPipePS.hlsl — 5 entry points in one file
float4 main_simple(...)        { /* texture * color */ }
float4 main_simpleDetail(...)  { /* texture * detail * color */ }
float4 main_simpleFog(...)     { /* fog blending */ }
float4 main_xboxBuilding(...)  { /* dual-layer env */ }
float4 main_normMapBuilding(...) { /* normal-mapped */ }
```

Each entry point is compiled to a separate CSO:
```
fxc /E main_simple /T ps_2_0 /Fo cso/buildingPipePS_simple.cso buildingPipePS.hlsl
fxc /E main_simpleDetail /T ps_2_0 /Fo cso/buildingPipePS_simpleDetail.cso buildingPipePS.hlsl
```

This preserves backwards compatibility — `fix_build.py` compiles each wrapper to its own CSO with the correct entry point.

## Compilation
See [[Build System]]. Each HLSL compiles with:
```
fxc /E <entry> /T <profile> /Fo cso/<name>.cso <name>.hlsl
```
- Vehicle PBR shaders: `ps_3_0` / `vs_3_0`
- Building shaders: `ps_2_0` / `vs_2_0`
- PostFX shaders: `ps_2_0` / `ps_3_0`

## Loading
`CreateShaders()` in `pipelinecommon.cpp` loads all CSOs at startup via `makePS(IDR_*, &pointer)` and `makeVS(IDR_*, &pointer)`. Each IDR maps to a CSO in `Resource.rc`.

### IDR → Resource Mapping (resource.h / Resource.rc)

| IDR Define | ID | CSO File |
|---|---|---|
| IDR_GRASSPS | 100 | grassPS.cso |
| IDR_SIMPLEPS | 101 | simplePS.cso |
| IDR_VEHICLEVS | 102 | vehicleVS.cso |
| IDR_GTAIVPS | 110 | GTAIV_ps20.cso |
| IDR_SSAOPS | 120 | SSAO_ps20.cso |
| IDR_SMAAPS | 121 | SMAA.cso |
| IDR_SMAAEDGEPS | 122 | SMAA_Edge.cso |
| IDR_SMAABLENDWEIGHTPS | 123 | SMAA_BlendWeight.cso |
| IDR_SMAABLENDNEIGHBORPS | 124 | SMAA_BlendNeighbor.cso |
| IDR_SMAAEDGENORMALPS | 125 | SMAA_EdgeNormal.cso |
| IDR_SMAAEDGEDEPTHPS | 126 | SMAA_EdgeDepth.cso |
| IDR_SMAATEMPPS | 127 | SMAA_Temporal.cso |
| IDR_SMAAEDGECOMBINEDPS | 128 | SMAA_EdgeCombined.cso |
| IDR_SMAAEDGEMOTIONDEPTHPS | 129 | SMAA_EdgeMotionDepth.cso |
| IDR_SSAO_VERTEXDEPTH | 130 | SSAO_VertexDepth.cso |
| IDR_MOTIONBLUR_BURNOUT | 140 | MotionBlur_Burnout.cso |
| IDR_COLORFILTER_CROSSMIX | 141 | ColorFilter_CrossMix.cso |
| IDR_SSS_BLUR | 142 | SSS_Blur.cso |
| IDR_NORMALBUFFERPS | 143 | NormalBuffer.cso |
| IDR_PIPECHAINPS | 144 | PipeChain.cso |
| IDR_GRADINGPS | 160 | gradingPS.cso |
| IDR_CONTRASTPS | 161 | contrastPS.cso |
| IDR_BLURPS | 162 | blurPS.cso |
| IDR_RADIOSITYPS | 163 | radiosityPS.cso |
| IDR_VCTRAILSPS | 164 | vcTrailsPS.cso |
| IDR_MODERNCOLORFILTERPS | 165 | ModernColorFilterPS.cso |
| IDR_VEHICLEPAINT_GTAIV | 200 | VehiclePaint_GTAIV.cso |
| IDR_WATER_PARALLAX | 201 | Water_Parallax.cso |
| IDR_WATER_VS | 202 | Water_VS.cso |
| IDR_VEHICLEPBR_MODERN | 203 | VehiclePBR_Modern.cso |
| IDR_GLASS_VEHICLE | 204 | Glass_Vehicle.cso |
| IDR_RUBBER_VEHICLE | 205 | Rubber_Vehicle.cso |
| IDR_CARPAINT_REFL | 206 | CarPaint_Reflections.cso |
| IDR_PBR_LIGHTING | 207 | PBR_Lighting.cso |
| IDR_CLAMP | 208 | Clamp.cso |
| IDR_DYNAMICSKY | 209 | DynamicSky.cso |
| IDR_SKINPBR | 210 | SkinSSS.cso |
| IDR_RUBBER_VEHICLE_MODERN | 240 | Rubber_Vehicle_Modern.cso |
| IDR_VEHICLEPBRVS | 211 | vehiclePBRVS.cso |
| IDR_PS2CARFXVS | 213 | ps2CarFxVS.cso |
| IDR_SPECCARFXVS | 214 | specCarFxVS.cso |
| IDR_SPECCARFXPS | 215 | specCarFxPS.cso |
| IDR_XBOXCARVS | 216 | xboxCarVS.cso |
| IDR_LEEDSCARFXVS | 217 | leedsCarFxVS.cso |
| IDR_MOBILEVEHICLEVS | 218 | mobileVehicleVS.cso |
| IDR_MOBILEVEHICLEPS | 219 | mobileVehiclePS.cso |
| IDR_GTAIVVEHICLEVS | 220 | — (in GTAIV CSO) |
| IDR_GTAIVVEHICLEPS | 221 | — (in GTAIV CSO) |
| IDR_PS2BUILDINGVS | 222 | ps2BuildingVS.cso |
| IDR_PS2BUILDINGFXVS | 223 | ps2BuildingFxVS.cso |
| IDR_PS2BUILDINGWINDVS | 224 | ps2BuildingWindVS.cso |
| IDR_XBOXBUILDINGVS | 225 | xboxBuildingVS.cso |
| IDR_XBOXBUILDINGPS | 226 | xboxBuildingPS.cso |
| IDR_XBOXBUILDINGSTOCHASTICPS | 227 | xboxBuildingStochasticPS.cso |
| IDR_XBOXBUILDINGWINDVS | 228 | xboxBuildingWindVS.cso |
| IDR_SPHEREBUILDINGVS | 229 | sphereBuildingVS.cso |
| IDR_SIMPLEDETAILPS | 230 | simpleDetailPS.cso |
| IDR_SIMPLEDETAILSTOCHASTICPS | 231 | simpleDetailStochasticPS.cso |
| IDR_SIMPLEFOGPS | 232 | simpleFogPS.cso |
| IDR_GTAIVBUILDINGVS | 233 | — (in GTAIV CSO) |
| IDR_GTAIVBUILDINGPS | 234 | — (in GTAIV CSO) |
| IDR_CUSTOMBUILDINGVS | 235 | customBuildingVS.cso |
| IDR_NORMMAPBUILDINGPS | 236 | normMapBuildingPS.cso |
| IDR_NORMMAPVEHICLEPS | 237 | normMapVehiclePS.cso |

## Complete Entry Point Catalog

### Vertex Shaders

| HLSL File | Entry Point | Pipeline | Target |
|---|---|---|---|
| `vehiclePipeVS.hlsl` | `main_vehicle` | PS2/PC/Xbox basic | vs_2_0 |
| `vehiclePipeVS.hlsl` | `main_vehiclePBR` | Modern PBR | vs_3_0 |
| `vehiclePipeVS.hlsl` | `main_ps2CarFx` | PS2 car effects | vs_2_0 |
| `vehiclePipeVS.hlsl` | `main_specCarFx` | Specular car FX | vs_2_0 |
| `vehiclePipeVS.hlsl` | `main_xboxCar` | Xbox car | vs_2_0 |
| `vehiclePipeVS.hlsl` | `main_leedsCarFx` | Leeds/VCS car | vs_2_0 |
| `vehiclePipeVS.hlsl` | `main_mobileVehicle` | Mobile vehicle | vs_2_0 |
| `vehiclePipeVS.hlsl` | `main_neoPass1` | Neo pass 1 | vs_2_0 |
| `vehiclePipeVS.hlsl` | `main_neoPass2` | Neo pass 2 | vs_2_0 |
| `vehiclePipeVS.hlsl` | `main_edgeTessellation` | Edge tessellation | vs_3_0 |
| `vehicleVS.hlsl` | `main` | Basic vehicle | vs_2_0 |
| `ps2BuildingVS.hlsl` | `main` | PS2 building | vs_2_0 |
| `ps2BuildingFxVS.hlsl` | `main` | PS2 building FX | vs_2_0 |
| `ps2BuildingWindVS.hlsl` | `main` | PS2 building wind | vs_2_0 |
| `ps2BuildingSSSVS.hlsl` | `main` | PS2 building SSS | vs_2_0 |
| `xboxBuildingVS.hlsl` | `main` | Xbox building | vs_2_0 |
| `xboxBuildingWindVS.hlsl` | `main` | Xbox building wind | vs_2_0 |
| `sphereBuildingVS.hlsl` | `main` | Sphere-mapped building | vs_2_0 |
| `mobileBuildingVS.hlsl` | `main` | Mobile building | vs_2_0 |
| `mobileVehicleVS.hlsl` | `main` | Mobile vehicle | vs_2_0 |
| `neoVehiclePass1VS.hlsl` | `main` | Neo vehicle pass 1 | vs_2_0 |
| `neoVehiclePass2VS.hlsl` | `main` | Neo vehicle pass 2 | vs_2_0 |
| `postfxVS.hlsl` | `main` | Post-processing quad | vs_2_0 |
| `Water_VS.hlsl` | `main` | Water | vs_2_0 |
| `EdgeTessellationVS.hlsl` | `main` | Edge tessellation | vs_3_0 |

### Pixel Shaders — Vehicle

| HLSL File | Entry Point | Pipeline | Target |
|---|---|---|---|
| `VehiclePBR_Modern.hlsl` | `main` | Modern PBR | ps_3_0 |
| `VehiclePBR_Modern.hlsl` | `main_rubber` | PBR rubber/tire | ps_3_0 |
| `VehiclePBR_Modern.hlsl` | `main_ps2EnvSpecFx` | PS2 env+spec | ps_2_0 |
| `VehiclePBR_Modern.hlsl` | `main_specCarFx` | Specular car FX | ps_2_0 |
| `VehiclePBR_Modern.hlsl` | `main_mobileVehicle` | Mobile vehicle | ps_2_0 |
| `VehiclePBR_Modern.hlsl` | `main_normMapVehicle` | Normal-mapped vehicle | ps_3_0 |
| `VehiclePBR_Modern.hlsl` | `main_building` | PBR building | ps_3_0 |
| `Glass_Vehicle.hlsl` | `main` | Vehicle glass | ps_3_0 |
| `Rubber_Vehicle.hlsl` | `main` | Tire rendering (standalone) | ps_3_0 |
| `CarPaint_Reflections.hlsl` | `main` | Car paint reflections | ps_3_0 |
| `VehiclePaint_GTAIV.hlsl` | `main` | GTA IV vehicle paint | ps_3_0 |
| `GTAIV_ps20.hlsl` | `main` | GTA IV post-process | ps_2_0 |
| `simplePS.hlsl` | `main` | Basic texture * color | ps_2_0 |
| `mobileVehiclePS.hlsl` | `main` | Mobile vehicle PS | ps_2_0 |
| `normMapVehiclePS.hlsl` | `main` | Normal-mapped vehicle | ps_3_0 |

### Pixel Shaders — Building

| HLSL File | Entry Point | Pipeline | Target |
|---|---|---|---|
| `buildingPipePS.hlsl` | `main_simple` | PS2 simple building | ps_2_0 |
| `buildingPipePS.hlsl` | `main_simpleDetail` | Building + detail map | ps_2_0 |
| `buildingPipePS.hlsl` | `main_simpleFog` | Building + fog | ps_2_0 |
| `buildingPipePS.hlsl` | `main_xboxBuilding` | Xbox dual-layer env | ps_2_0 |
| `buildingPipePS.hlsl` | `main_normMapBuilding` | Normal-mapped building | ps_2_0 |
| `stochasticBuildingPS.hlsl` | `main_simpleStochastic` | Stochastic simple | ps_2_0 |
| `stochasticBuildingPS.hlsl` | `main_simpleDetailStochastic` | Stochastic + detail | ps_2_0 |
| `stochasticBuildingPS.hlsl` | `main_xboxBuildingStochastic` | Stochastic Xbox env | ps_2_0 |

### Pixel Shaders — PostFX

| HLSL File | Entry Point | Feature | Target |
|---|---|---|---|
| `SSAO_ps20.hlsl` | `main` | SSAO with normal buffer | ps_2_0 |
| `SSAO_ps20_simple.hlsl` | `main` | Simple SSAO | ps_2_0 |
| `SSAO_VertexDepth.hlsl` | `main` | SSAO vertex depth | ps_2_0 |
| `SSAO_ps20_depthonly.hlsl` | `main` | SSAO depth only | ps_2_0 |
| `SMAA_Edge.hlsl` | `main` | SMAA edge detection | ps_2_0 |
| `SMAA_BlendWeight.hlsl` | `main` | SMAA blend weight | ps_2_0 |
| `SMAA_BlendNeighbor.hlsl` | `main` | SMAA neighborhood blend | ps_2_0 |
| `SMAA_Temporal.hlsl` | `main` | SMAA temporal | ps_2_0 |
| `SMAA_EdgeNormal.hlsl` | `main` | SMAA normal-based edge | ps_2_0 |
| `SMAA_EdgeDepth.hlsl` | `main` | SMAA depth-based edge | ps_2_0 |
| `SMAA_EdgeCombined.hlsl` | `main` | SMAA combined edge | ps_2_0 |
| `SMAA_EdgeMotionDepth.hlsl` | `main` | SMAA motion+depth edge | ps_2_0 |
| `MotionBlur_Burnout.hlsl` | `main` | Burnout-style motion blur | ps_2_0 |
| `ColorFilter_CrossMix.hlsl` | `main` | Color filter cross-mixing | ps_2_0 |
| `SkinEnhance.hlsl` | `main` | Skin SSS enhancement | ps_3_0 |
| `HairEnhance.hlsl` | `main` | Hair anisotropic highlights | ps_3_0 |
| `NormalBuffer.hlsl` | `main` | Stereo disparity normals | ps_2_0 |
| `PipeChain.hlsl` | `main` | 4-pass post-processing | ps_2_0 |
| `SkinSSS.hlsl` | `main` | Skin subsurface scattering | ps_3_0 |
| `SSS_Blur.hlsl` | `main` | SSS blur pass | ps_2_0 |
| `vectorMotionBlur.hlsl` | `main` | Vector motion blur | ps_2_0 |
| `Water_Parallax.hlsl` | `main` | Parallax water | ps_3_0 |
| `DynamicSky.hlsl` | `main` | Dynamic sky rendering | ps_3_0 |
| `IBL_SkyCloud.hlsl` | `main` | IBL sky/cloud rendering | ps_3_0 |
| `PBR_Lighting.hlsl` | `main` | PBR lighting functions | ps_3_0 |
| `ModernColorFilterPS.hlsl` | `main` | Modern color filter | ps_2_0 |
| `GTA_SA_ModernColor.hlsl` | `main` | GTA SA modern color | ps_3_0 |
| `Clamp.hlsl` | `main` | Color clamping | ps_2_0 |

### Pixel Shaders — Effects / Grading

| HLSL File | Entry Point | Feature | Target |
|---|---|---|---|
| `radiosityPS.hlsl` | `main` | Shader-based radiosity | ps_2_0 |
| `blurPS.hlsl` | `main` | Blur filter | ps_2_0 |
| `gradingPS.hlsl` | `main` | Color grading | ps_2_0 |
| `contrastPS.hlsl` | `main` | Contrast adjustment | ps_2_0 |
| `iiiTrailsPS.hlsl` | `main` | GTA III trails | ps_2_0 |
| `vcTrailsPS.hlsl` | `main` | GTA VC trails | ps_2_0 |
| `grassPS.hlsl` | `main` | Grass rendering | ps_2_0 |

### Unified Pipeline (WIP — not in Resource.rc)

| HLSL File | Entry Point | Pass | Target |
|---|---|---|---|
| `unifiedPipe.hlsl` | `main` (= `main_pre`) | Pre-pass: gamma decode + edge detect | ps_3_0 |
| `unifiedPipe.hlsl` | `main_occlusion` | Occlusion pass | ps_3_0 |
| `unifiedPipe.hlsl` | `main_post` | Post-pass: lighting + grading | ps_3_0 |
| `unifiedPipe.hlsl` | `main_ibl` | IBL pass | ps_3_0 |

## Shared Libraries (shaders/include/)

| File | Purpose | Used By |
|---|---|---|
| `PBR_Common.hlsl` | GGX/Smith/Schlick, SchlickFresnelScalar, cloud FBM, sun contribution | VehiclePBR_Modern, Glass_Vehicle, CarPaint_Reflections, Rubber_Vehicle |
| `StochasticSamplerPS.hlsl` | Hash-based stochastic texture sampling | stochasticBuildingPS, buildingPipePS |
| `colorSpace.hlsl` | YCbCr conversion, linear/gamma, grading, SSAO, edge detect | unifiedPipe, GTA_SA_ModernColor, ColorFilter_CrossMix |
| `SubsurfaceScattering.hlsl` | SSS material IDs, wrap lighting, Crysis vegetation bending | SkinSSS, SkinEnhance, HairEnhance |
| `CarPaintNoise.hlsl` | Paint noise functions | CarPaint_Reflections, VehiclePBR_Modern |
| `PerlinNoise.hlsl` | Procedural Perlin noise | DynamicSky, IBL_SkyCloud, Water_Parallax |

## Pre-compiled CSOs

Pre-compiled CSOs from GTA IV exist in `resources/cso/`:
- `GTAIV_ps20.cso` — GTA IV post-process tonemapping
- `VehiclePaint_GTAIV.cso` — GTA IV vehicle paint shader
- `gtaivVehiclePS.cso` — GTA IV vehicle pixel shader
- `gtaivBuildingPS.cso` — GTA IV building pixel shader
- `gtaivBuildingVS.cso` — GTA IV building vertex shader
- `gtaivVehicleVS.cso` — GTA IV vehicle vertex shader
- `normMapBuildingPS.cso` — Normal-mapped building PS
- `normMapVehiclePS.cso` — Normal-mapped vehicle PS

## See Also
- [[VehiclePBR Modern]] — The unified vehicle PBR shader
- [[Building Pipeline]] — Building pixel shaders
- [[PBR Common]] — Shared PBR functions
- [[File Inventory]] — Complete file listing
- [[Three-Codebase Comparison]] — Shader comparison across all codebases

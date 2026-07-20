# SKYGFXPLUS_DOCS — Master Index

This is the central documentation hub for SKYGFXPLUS, a GTA San Andreas rendering enhancement project built on Rockstar's RenderWare 3.7 SDK but targeting modern shader pipelines.

---

## Overview

SKYGFXPLUS aims to:
- **Port GTA IV rendering techniques** (SMAA, SSAO, FXAA, Forward+ rendering) to GTA San Andreas
- **Integrate normal mapping** (one asi/dll, not separate plugin)
- **Emulate PS2 rendering** (PS2 GS register references, VU1 microprograms, 4x4 dither matrix)
- **Maintain compatibility** with existing plugins (SilentPatch, modloader, moonloader, CLEO, etc.)
- **Use a single DLL** (skygfx.asi) that includes: lighting, pipelines, post-processing, normal mapping, GTA IV shaders, shader system porting

---

## Key Documentation Topics

### 1. PS2 Emulation Infrastructure
| File | Purpose | Related |
|------|---------|----------|
| [[PS2 GS Register Reference (from source)]] | Complete GS register table from game code | [[PS2 DMA and VU1 System]], [[PS2 Screen Effects and Post-Processing]] |
| [[PS2 DMA and VU1 System]] | VIF1 commands, DMA channels, VU1 microprograms | [[PS2 GS Register Reference (from source)]], [[PS2 Screen Effects and Post-Processing]] |
| [[PS2 Screen Effects and Post-Processing]] | Post-processing GS setup, interlace handling, blur, dithering | GTA SA RE docs (interlace section), debugging techniques |
| [[GTASource Rendering Code Analysis]] | CarFXRenderer, BuildingRenderer, Plants/Grass, custom pipelines | PS2-specific render passes, modelinfo architecture |

### 2. Pipeline Architecture
| File | Platform | Purpose | Related |
|------|----------|---------|----------|
| [[Pipelines/Normal Mapping]] | Both | Normal mapping plugin reference | [[PS2 Xbox Feature Emulation Guide]] |
| [[PS2 Xbox Feature Emulation Guide]] | Both | Color clamping, dithering, dual-pass alpha, TFX | [[Pipelines/Normal Mapping]] |
| [[PS2 GS Register Reference (from source)]] | PS2 | GS register table, debug setup, GIF packet decoding | PS2 register reference, screen effects |
| [[PS2 DMA and VU1 System]] | Both | PS2 vs PC/Xbox rendering distinctions | Xbox NV2A docs, PS2/Xbox emulation guide |

### 3. Shader System & Pipeline Porting
| File | Target | Approach | Related |
|------|--------|----------|----------|
| [[ReVC RenderWare Shader System]] | ReVC | Register layout, pipeline architecture port | Shader sources |
| Shader sources (`shaders/*.hlsl`) | SKYGFX | Modified GTA IV shaders ported to SM2.0/3.0 | ReVC register layout, GTAIV shader porting reference |

### 4. Lighting & Normal Mapping
| File | Component | Focus | Related |
|------|----------|-------|----------|
| [[GTASource Rendering Code Analysis]] | GTA IV | GTA IV lighting pipeline, modelinfo architecture | PS2 lighting DMA, normal mapping |
| [[Timecycle Fix]] | PS2/Xbox | Color clamping, GPU-side DN interpolation | [[PS2 Xbox Feature Emulation Guide]] |
| [[Pipelines/Normal Mapping]] | Both | Plugin API, pipeline attachment, texture capture | GTASource code analysis, PS2/Xbox implementations |

### 5. Model & Atomic Data Structures
| File | Format | Purpose | Related |
|------|--------|---------|----------|
| [[GTASource Rendering Code Analysis#model-info-hierarchy]] | GTA V RAGE Engine | Model types, factory system | [[GTASource Rendering Code Analysis#model-info-accelerator-ps2-xbox-specific]] |
| [[GTASource Rendering Code Analysis#pipes-folder-structure]] | GTA SA Source | Custom pipelines, day/night, environment mapping | [[ReVC RenderWare Shader System]] |

### 6. Crash Analysis & Debugging
| File | Issue | Root Cause | Prevention |
|------|------|------------|------------|
| [[Crash Analysis - normmap_stubs symbol conflicts]] | 0x4C9FB5 / 0x40890A | normmap_stubs exports clash with game/SilentPatch | Remove extern "C" symbol exports |
| [[Debugging Guide]] | All | SkyGFX logging, crash handling, debug menu | PS2 register reference, VIF marks |

### 7. Technical References
| File | Source | Purpose | Tags |
|------|--------|---------|------|
| ShaderX1 Book (external) | Free book | Legacy shader techniques | shaderx1, legacy, techniques |
| [[ReVC RenderWare Shader System]] | ReVC | D3D9 register layout | revc, registers, shader |
| [[GTA SA Design Document - Rockstar Internal Style]] | GTASource/RE | Reconstructed engine design doc | design, architecture, gta-sa |
| [[OpenSA Architecture]] | OpenSA project | MTA64 + SKYGFXPlus multi-project architecture | opensa, mta, architecture |

---

## Technical Debt

- **Trampoline issue**: [[PS2 DMA and VU1 System]] need better hook library (MinHook)
- **Symbol conflicts**: [[Crash Analysis - normmap_stubs symbol conflicts]] solution implemented
- **RwEngineInstance**: [[PS2 DMA and VU1 System]] discusses RenderWare global state

---

## Additional Documentation

| File | Topic |
|------|-------|
| [[Build/Build Scripts]] | Build system architecture, premake5 config, build commands |
| [[Shaders/Shader Reference]] | HLSL register layout, sampler bindings, shader implementations |
| [[Performance Tuning]] | Performance impact of each feature, tuning recommendations |
| [[Forward Plus Rendering]] | GTA IV tiled forward renderer implementation details |
| [[Hardware/Overview]] | Platform hardware architecture deep-dives |
| [[Hardware/PS2 GS]] | PS2 GS pixel pipeline, register fields, context swapping |
| [[Hardware/Xbox NV2A]] | Xbox NV2A EDRAM, push buffer, register combiners |
| [[Hardware/Early PC GPUs]] | DX7-DX9 PC GPU capabilities and limitations |

## External References

The companion vault at GTA SA RE Documentation covers:
- GTA SA RE Documentation > PS2 Rendering Pipeline — Full PS2 GS and VU1 hardware reference
- GTA SA RE Documentation > Xbox NV2A Rendering Architecture — Xbox GPU register combiner reference
- GTA SA RE Documentation > RenderWare > RenderWare Shader System Reference — RW3 D3D9 register layouts
- GTA SA RE Documentation > RenderWare > RpGeometry and RpAtomic — Geometry, mesh header, atomic internals, vertex formats
- GTA SA RE Documentation > RenderWare > RpMaterial and MatFX — Material struct, surface properties, MatFX plugin
- GTA SA RE Documentation > RenderWare > Plugin System — Plugin registration, extension data, stream callbacks
- GTA SA RE Documentation > RenderWare > RwRaster and Textures — RwRaster, texture management, TXD format
- GTA SA RE Documentation > RenderWare > RwFrame and Coordinate Spaces — Frame hierarchy, matrix transforms, coordinate conventions
- GTA SA RE Documentation > RenderWare > RwCamera and RpWorld — Camera setup, projection, world sectors, BSP culling

The modding wiki focuses on SKYGFXPLUS at Modding > Wiki > README covering:
- PS2 emulation techniques for PS2 GS and VU1 hardware
- XBox NV2A rendering architecture
- Shader system and pipeline porting from GTA IV
- GTA IV shaders ported to SM2.0/3.0

Complete GTA SA game structures at GTA SA RE Documentation > Structures > Index:
- GTA SA RE Documentation > Structures > Entity System — CPlaceable, CEntity, CBuilding, CPhysical hierarchy
- GTA SA RE Documentation > Structures > Rendering — CVisibilityPlugins, CRenderer, CTimecycleInfo, CWeather
- GTA SA RE Documentation > Structures > Model System — CBaseModelInfo, CVehicleModelInfo, CPedModelInfo, LOD system
- GTA SA RE Documentation > Structures > Animation — CAnimBlendAssociation, RpHAnim, skin deformation
- GTA SA RE Documentation > Structures > World Management — CWorld sectors, CStreaming, CFileLoader

Technical porting at GTA SA RE Documentation > PS2 to PC Porting Techniques — Techniques for emulating GS features in shaders

## Project Status

**Current Commit**: Work in progress

**Key Issues**:
1. ✓ Fixed crash by removing `normmap_stubs.cpp` and `rpnormmap.lib`
2. ✗ Need to fix trampoline at 0x5DA610 for proper normal mapping support
3. ✗ Need to integrate GTA IV shader system and normal mapping into single DLL
4. ✗ Need PS2 emulation foundation

**Build Status**: ✅ Build succeeds, crashes on launch due to trampoline issue

---

*Last Updated: 2026-06-30*
*Maintained: SKYGFXPLUS Team*
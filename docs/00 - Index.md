# SkyGFX Plus — Map of Content

> **GTA San Andreas Enhanced Edition** — A modern rendering overhaul built on [[Project Lineage|aap's skygfx]].
> Hybrid deferred→forward pipeline moving toward forward+. All code in one ASI, all features toggleable via INI.

---

## Architecture
- [[Project Paths]] — All SDK/game/script paths in one place
- [[Project Lineage]] — aap → junior → expIV evolution
- [[Build System]] — fix_build.py, MSVC/SDK setup, shader compilation
- [[INI Configuration]] — All config fields, quality presets, INI blocks
- [[Debug Tools]] — Hotkeys, profiling, crash handler
- [[Feature Hook Pattern]] — How to add new features (template)

## Rendering Pipeline
- [[Vehicle Pipeline]] — All vehicle pipe modes (PS2 through Modern PBR)
- [[VehiclePBR Modern]] — Unified PBR vehicle shader (GGX/Smith/Schlick)
- [[Building Pipeline]] — PS2, PBR building pipes
- [[PostFX Pipeline]] — SSAO, SMAA, motion blur, color filters, SSS
- [[Weather Timecycle]] — Sky, sun, moon, clouds, lighting
- [[Water Rendering]] — Gerstner waves, PBR water, sky reflection
- [[Unified Pipeline]] — Forward+ approach

## Shaders
- [[Shader Architecture]] — How shaders are organized, compiled, and loaded
- [[PBR Common]] — Shared PBR functions include (GGX, Smith, Fresnel, cloud FBM)
- [[Glass Shader]] — Vehicle glass with Fresnel, tint system
- [[BRDF Reference]] — Disney BSDF, Callisto, O3DE reference
- [[RW SDK Reference]] — RenderWare 3.7 function signatures, structs

## Systems
- [[Vehicle Classification]] — Per-vehicle paint, glass, tire detection
- [[Normal Buffer]] — Stereo disparity faux normals
- [[Color Grading]] — Three-point grading system
- [[Lens Effects]] — Distortion, chromatic aberration, grain

## Status
- [[Implemented Features]] — What's done
- [[Future Features]] — What's deferred
- [[Backwards Compatibility]] — All INI settings still work

## Reference
- [[Credits]] — Contributors and academic references
- [[File Inventory]] — Every source/shader/resource file
- [[Project Paths]] — All SDK/game/script paths

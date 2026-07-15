# SkyGFX Plus — Map of Content

> **GTA San Andreas Enhanced Edition** — A modern rendering overhaul built on [[Project Lineage|aap's skygfx]].
> Hybrid deferred→forward pipeline moving toward forward+. All code in one ASI, all features toggleable via INI.

---

## Architecture
- [[Project Paths]] — All SDK/game/script paths in one place
- [[Project Lineage]] — aap → junior → expIV evolution, feature parity matrix, key decisions
- [[Build System]] — fix_build.py, MSVC/SDK setup, shader compilation
- [[INI Configuration]] — All 100+ config fields, quality presets, INI blocks, unified pipeline keys
- [[Debug Tools]] — Hotkeys, profiling, crash handler
- [[Feature Hook Pattern]] — How to add new features (template)
- [[Backwards Compatibility]] — Compatibility matrix, verified INI keys across all three codebases
- [[Three-Codebase Comparison]] — Complete aap / junior / skygfx_plus side-by-side reference
- [[Roadmap to Ultimate Mod]] — Six-phase development plan

## Rendering Pipeline
- [[Vehicle Pipeline]] — All 11 vehicle pipe modes (PS2 through Modern PBR)
- [[VehiclePBR Modern]] — Unified PBR vehicle shader (GGX/Smith/Schlick)
- [[Building Pipeline]] — PS2, PC/Xbox, GTAIV, PBR building pipes
- [[PostFX Pipeline]] — SSAO, SMAA, motion blur, color filters, SSS
- [[Weather Timecycle]] — Sky, sun, moon, clouds, lighting, GTA V-style expansion
- [[Water Rendering]] — Gerstner waves, PBR water, sky reflection, parallax
- [[Unified Pipeline]] — Forward+ approach with 4-pass pipeline
- [[Night and Infrared Vision]] — Special vision modes
- [[Radiosity]] — Fixed-function and shader-based radiosity
- [[GTA IV Mode]] — GTA IV filmic tonemapping, vignette, bloom

## Shaders
- [[Shader Architecture]] — Directory layout, compilation, loading, complete entry point catalog
- [[PBR Common]] — Shared GGX/Smith/Schlick functions include
- [[Glass Shader]] — Vehicle glass with Fresnel, tint system
- [[Color Grading]] — Three-point grading system
- [[Lens Effects]] — Distortion, chromatic aberration, grain
- [[BRDF Reference]] — Disney BSDF, Callisto, O3DE reference
- [[RW SDK Reference]] — RenderWare 3.7 function signatures, structs

## Systems
- [[Vehicle Classification]] — Per-vehicle paint, glass, tire detection
- [[Vehicle Pipeline]] — Neo, Leeds, Mobile, Env, GTAIV pipe details
- [[Neo Vehicle Pipe]] — Neo pipe implementation
- [[Leeds Vehicle Pipes]] — Leeds/VCS engine pipes
- [[Wheel Extender]] — Extended wheel system (wheel_lod_system, wheel_naming_convention, wheel_texture_atlas)

## Status
- [[Implemented Features]] — What's done across all three codebases
- [[Future Features]] — What's deferred and what fits in SM3.0
- [[SkyGFX_Living_Systems_Database]] — Living systems reference

## Reference
- [[Credits]] — Contributors and academic references
- [[File Inventory]] — Every source/shader/resource file across all three codebases
- [[SDK_Dependecies]] — SDK dependencies and versions

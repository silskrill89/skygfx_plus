---
tags: [index, moc, overview]
created: 2025-01-02
updated: 2026-07-15
---

# SkyGFX Plus — Technical Vault

> **GTA San Andreas Enhanced Edition** — A modern rendering overhaul built on [[Project Lineage|aap's skygfx]].
> Hybrid deferred→forward pipeline moving toward forward+. All code in one ASI, all features toggleable via INI.

---

## Map of Content

### Architecture & Design
- [[Decided Architecture]] — Architecture decisions (20-question quiz with answers)
- [[64-bit Bridge Architecture]] — 64-bit bridge for scripts/AI/assets
- [[Signal Splitter Architecture]] — "Bigger Bus" for heavy operations
- [[32_to_64_Bit_Transition_Reference]] — 32→64 bit porting reference

### Rendering Pipeline
- [[SkyGFX Pipeline Overview]] — Forward+ pipeline architecture, mesh classification, texture bindings, shader constants
- [[Weather System Architecture]] — Multi-timecyc weather with hash-based selection
- [[Wheel System Architecture]] — VC-style shared wheels + hash-based selection
- [[Wheel Extender Technical Plan]] — Extended wheel system phases

### Shaders
- [[VehiclePBR Modern]] — Unified vehicle pixel shader (7 entry points)

### Technical Decisions
- [[IBL Env Map Decision]] — IBL tints env map, not replaces
- [[Rubber Shader Merge]] — Merged rubber shader into VehiclePBR_Modern

### Build & Deployment
- [[Build System]] — fix_build.py, MSVC/SDK setup
- [[SDK Dependencies]] — All external SDKs and paths

### Reference Papers
- [[Non-Parametric Sparse BRDF]] — BRDF research paper summary
- [[SMAA Enhanced Subpixel Morphological AA]] — SMAA implementation reference
- [[Dynamic Temporal AA Call of Duty]] — COD temporal AA reference
- [[Conservative Morphological AA]] — CMAA alternative reference

### RenderWare & Engine
- [[RenderWare V2.1 API Reference]] — RW V2.1 core concepts
- [[GTA IV RAGE Engine Format Reference]] — RAGE format reference
- [[GTA IV RAGE Engine Complete Reference]] — Full RAGE reference

### Visual Reference
- [[GTA San Andreas EPK Reference]] — Rockstar EPK screenshots
- [[EPK Visual Reference - Quick Look]] — Quick visual reference

### Planning
- [[Three Codebase Comparison]] — aap → junior → expIV comparison
- [[Roadmap]] — Phases 1–5 roadmap to ultimate mod
- [[Workflow Quiz]] — 20-question architecture decision quiz

---

## Cross-References to Main docs/

The main `docs/` directory contains project-level documentation. Use these wikilinks to navigate:

- [[Project Lineage]] — aap → junior → expIV evolution
- [[Project Paths]] — All SDK/game/script paths
- [[Vehicle Pipeline]] — All vehicle pipe modes
- [[Building Pipeline]] — PS2, PBR building pipes
- [[PostFX Pipeline]] — SSAO, SMAA, motion blur, color filters
- [[Weather Timecycle]] — Sky, sun, moon, clouds, lighting
- [[Water Rendering]] — Gerstner waves, PBR water
- [[Shader Architecture]] — How shaders are organized, compiled, loaded
- [[PBR Common]] — Shared PBR functions
- [[Glass Shader]] — Vehicle glass with Fresnel
- [[BRDF Reference]] — Disney BSDF, Callisto, O3DE reference
- [[INI Configuration]] — All config fields, quality presets
- [[Vehicle Classification]] — Per-vehicle paint, glass, tire detection
- [[Color Grading]] — Three-point grading system
- [[Lens Effects]] — Distortion, chromatic aberration, grain
- [[Implemented Features]] — What's done
- [[Future Features]] — What's deferred
- [[Build System]] — fix_build.py, MSVC/SDK setup
- [[File Inventory]] — Every source/shader/resource file
- [[Credits]] — Contributors and academic references
- [[wheel_naming_convention]] — wheel_class_style_variant format
- [[wheel_lod_system]] — Distance-based LOD switching
- [[wheel_texture_atlas]] — Combined texture optimization

---

## Current Status

### Implemented
- Unified PBR vehicle shader (7 entry points)
- Parametric rubber/tire shader with dirt/wear
- IBL tinting for env map reflections
- Wheel extraction tools (Python)
- Wheel naming convention & LOD system
- Vehicle classification system
- Glass shader with Fresnel
- SMAA anti-aliasing (3-pass)
- SSAO screen-space ambient occlusion
- Motion blur
- Subsurface scattering
- Weather system with multi-timecyc

### In Progress
- Wheel extender render hook
- Mismatched wheels for beater cars
- Passenger class system
- Region-based vehicle spawning

### Planned
- Universal dirt/wear system
- Rust texture system
- TransFender integration
- MoonLoader Lua scripts
- Forward+ unified pipeline
- GTA V/IV-style settings menu

---

## Quick Links

### Code
- `src/vehiclePipe.cpp` — Vehicle rendering pipeline
- `src/buildingPipe.cpp` — Building rendering pipeline
- `src/veh_shaders.cpp` — Vehicle shader bridge
- `src/wheels_extender.cpp` — Wheel extender system
- `src/weather.cpp` — Weather system implementation

### Shaders
- `shaders/ps/VehiclePBR_Modern.hlsl` — Unified vehicle shader
- `shaders/ps/Rubber_Vehicle.hlsl` — Tire/rubber shader (legacy, merged)
- `shaders/include/PBR_Common.hlsl` — Shared PBR functions

### Tools
- `tools/fix_build.py` — Build automation
- `tools/wheel_extractor.py` — Wheel extraction
- `tools/wheel_pipeline.py` — Wheel processing

---

## How to Use This Vault

1. **Open in Obsidian**: File → Open Vault → `docs/obsidian-vault`
2. **Graph View**: See connections between topics
3. **Search**: Find specific materials, techniques, or decisions
4. **Tags**: Filter by `#pbr`, `#shaders`, `#tools`, etc.
5. **Backlinks**: See what references each note
6. **Project docs**: Navigate up to `docs/` for project-level documentation

## Learning Path

### For New Contributors
1. Start with [[Decided Architecture]] (architecture decisions)
2. Read [[SkyGFX Pipeline Overview]] (rendering pipeline)
3. Study [[VehiclePBR Modern]] (unified vehicle shader)
4. Understand [[Build System]] (build setup)

### For Shader Work
1. Read [[Non-Parametric Sparse BRDF]] (BRDF research)
2. Study [[VehiclePBR Modern]] (7 entry points)
3. Understand [[IBL Env Map Decision]] (IBL approach)
4. Review [[Rubber Shader Merge]] (merge pattern)

### For Tool Development
1. Read [[Build System]] (fix_build.py overview)
2. Study [[Wheel Extender Technical Plan]] (wheel system)
3. Understand [[Wheel System Architecture]] (hash-based selection)
4. Review [[SDK Dependencies]] (external SDKs)

---
tags: [index, moc, overview]
created: 2025-01-02
updated: 2025-01-02
---

# SkyGFX Plus - Technical Vault

> **GTA San Andreas Enhanced Edition** — A modern rendering overhaul built on aap's skygfx.
> Hybrid deferred→forward pipeline moving toward forward+. All code in one ASI, all features toggleable via INI.

## 🗺️ Map of Content

### 🏗️ Architecture & Design
- [[01-Architecture/Project Lineage]] — aap → junior → expIV evolution
- [[01-Architecture/Project Paths]] — All SDK/game/script paths
- [[01-Architecture/Feature Hook Pattern]] — How to add new features
- [[01-Architecture/Backwards Compatibility]] — INI settings preservation

### 🔧 Build & Deployment
- [[08-Build-Deploy/Build System]] — fix_build.py, MSVC/SDK setup
- [[08-Build-Deploy/SDK Dependencies]] — All external SDKs and paths
- [[08-Build-Deploy/Shader Compilation]] — HLSL → CSO pipeline
- [[08-Build-Deploy/Deployment]] — ASI + INI to game directory

### 🎨 Rendering Pipelines
- [[02-Pipelines/Vehicle Pipeline]] — All vehicle pipe modes (PS2 through Modern PBR)
- [[02-Pipelines/Building Pipeline]] — PS2, PBR building pipes
- [[02-Pipelines/PostFX Pipeline]] — SSAO, SMAA, motion blur, color filters
- [[02-Pipelines/Weather Timecycle]] — Sky, sun, moon, clouds, lighting
- [[02-Pipelines/Water Rendering]] — Gerstner waves, PBR water, sky reflection
- [[02-Pipelines/Unified Pipeline]] — Forward+ approach

### 💎 Shaders
- [[03-Shaders/Shader Architecture]] — How shaders are organized, compiled, loaded
- [[03-Shaders/VehiclePBR Modern]] — Unified PBR vehicle shader (6 entry points)
- [[03-Shaders/PBR Common]] — Shared PBR functions (GGX, Smith, Fresnel, cloud FBM)
- [[03-Shaders/Glass Shader]] — Vehicle glass with Fresnel, tint system
- [[03-Shaders/Rubber Vehicle]] — Parametric tire/rubber shader with dirt/wear
- [[03-Shaders/BRDF Reference]] — Disney BSDF, Callisto, O3DE reference

### 🎮 Systems
- [[04-Systems/Vehicle Classification]] — Per-vehicle paint, glass, tire detection
- [[04-Systems/Normal Buffer]] — Stereo disparity faux normals
- [[04-Systems/Color Grading]] — Three-point grading system
- [[04-Systems/Lens Effects]] — Distortion, chromatic aberration, grain
- [[04-Systems/INI Configuration]] — All config fields, quality presets

### 🛞 Wheel System
- [[07-Wheel-System/Wheel System Architecture]] — Overview of wheel extender
- [[07-Wheel-System/Wheel Naming Convention]] — wheel_class_style_variant format
- [[07-Wheel-System/Wheel LOD System]] — Distance-based LOD switching
- [[07-Wheel-System/Wheel Texture Atlas]] — Combined texture optimization
- [[07-Wheel-System/Wheel Extraction Tools]] — Python tools for wheel extraction

### 🛠️ Tools & Scripts
- [[05-Tools/Python Tools Overview]] — All Python automation scripts
- [[05-Tools/Wheel Extractor]] — Extract wheels from vehicle DFFs
- [[05-Tools/Wheel Pipeline]] — Dedup, pack, metadata generation
- [[05-Tools/Wheel Audit]] — Audit wheel extraction quality
- [[05-Tools/TXD Builder]] — Build texture dictionaries
- [[05-Tools/Release Packager]] — Create release zips

### 📝 Technical Decisions
- [[06-Technical-Decisions/IBL Env Map Decision]] — IBL tints env map, not replaces
- [[06-Technical-Decisions/Rubber Shader Merge]] — Merged into VehiclePBR_Modern
- [[06-Technical-Decisions/C22 C23 Layout]] — Unified PBR constant layout
- [[06-Technical-Decisions/Wheel Selection Hash]] — Deterministic per-vehicle selection

### 📚 References
- [[09-References/RW SDK Reference]] — RenderWare 3.7 function signatures
- [[09-References/File Inventory]] — Every source/shader/resource file
- [[09-References/Credits]] — Contributors and academic references

## 🎯 Current Status

### ✅ Implemented
- Unified PBR vehicle shader (6 entry points)
- Parametric rubber/tire shader with dirt/wear
- IBL tinting for env map reflections
- Wheel extraction tools (Python)
- Wheel naming convention & LOD system
- Vehicle classification system
- Glass shader with Fresnel

### 🚧 In Progress
- Wheel extender render hook (TODO stub)
- Mismatched wheels for beater cars
- Passenger class system
- Region-based vehicle spawning

### 📋 Planned
- Universal dirt/wear system
- Rust texture system
- TransFender integration
- MoonLoader Lua scripts

## 🔗 Quick Links

### Code
- `src/vehiclePipe.cpp` — Vehicle rendering pipeline
- `src/buildingPipe.cpp` — Building rendering pipeline
- `src/veh_shaders.cpp` — Vehicle shader bridge
- `src/wheels_extender.cpp` — Wheel extender system

### Shaders
- `shaders/ps/VehiclePBR_Modern.hlsl` — Unified vehicle shader
- `shaders/ps/Rubber_Vehicle.hlsl` — Tire/rubber shader
- `shaders/include/PBR_Common.hlsl` — Shared PBR functions

### Tools
- `tools/fix_build.py` — Build automation
- `tools/wheel_extractor.py` — Wheel extraction
- `tools/wheel_pipeline.py` — Wheel processing

## 📖 How to Use This Vault

1. **Open in Obsidian**: File → Open Vault → `docs/obsidian-vault`
2. **Graph View**: See connections between topics
3. **Search**: Find specific materials, techniques, or decisions
4. **Tags**: Filter by `#pbr`, `#shaders`, `#tools`, etc.
5. **Backlinks**: See what references each note

## 🎓 Learning Path

### For New Contributors
1. Start with [[01-Architecture/Project Lineage]]
2. Read [[08-Build-Deploy/Build System]]
3. Understand [[02-Pipelines/Vehicle Pipeline]]
4. Study [[03-Shaders/VehiclePBR Modern]]

### For Shader Work
1. Read [[03-Shaders/PBR Common]]
2. Study [[03-Shaders/VehiclePBR Modern]]
3. Understand [[03-Shaders/Rubber Vehicle]]
4. Review [[09-References/BRDF Reference]]

### For Tool Development
1. Read [[05-Tools/Python Tools Overview]]
2. Study [[05-Tools/Wheel Extractor]]
3. Understand [[05-Tools/Wheel Pipeline]]
4. Review [[07-Wheel-System/Wheel System Architecture]]

---
tags: [index, moc, overview]
created: 2025-01-02
updated: 2026-11-16
---

# SkyGFX Plus — Technical Vault

> **GTA San Andreas Enhanced Edition** — A modern rendering overhaul built on [[../docs/Project Lineage|aap's skygfx]].
> Hybrid deferred→forward pipeline moving toward forward+. All code in one ASI, all features toggleable via INI.

---

## Map of Content

### Architecture & Design
- [[Current Architecture (Wave 20)]] — post-extraction module map, motion_fx/wind, postfx split, address layer
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
- [[Status 2026-11-16]] — root-cause verdict (untextured world), fix ledger (e7bc752), instrumentation pending
- [[Status 2026-11-10]] — wave 20 status, fix-wave ledger, hard rule, watch-list
- [[Gate Chain]] — W0 → W0.5 binary identities and gate protocol
- [[Build System]] — fast_build.py / fix_build.py, MSVC/SDK setup
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

The main `docs/` directory contains project-level documentation. Use these links to navigate:

- [[../docs/Project Lineage]] — aap → junior → expIV evolution
- [[../docs/Project Paths]] — All SDK/game/script paths
- [[../docs/Vehicle Pipeline]] — All vehicle pipe modes
- [[../docs/Building Pipeline]] — PS2, PBR building pipes
- [[../docs/PostFX Pipeline]] — SSAO, SMAA, motion blur, color filters
- [[../docs/Weather Timecycle]] — Sky, sun, moon, clouds, lighting
- [[../docs/Water Rendering]] — Gerstner waves, PBR water
- [[../docs/Shader Architecture]] — How shaders are organized, compiled, loaded
- [[../docs/PBR Common]] — Shared PBR functions
- [[../docs/Glass Shader]] — Vehicle glass with Fresnel
- [[../docs/BRDF Reference]] — Disney BSDF, Callisto, O3DE reference
- [[../docs/INI Configuration]] — All config fields, quality presets
- [[../docs/Vehicle Classification]] — Per-vehicle paint, glass, tire detection
- [[../docs/Color Grading]] — Three-point grading system
- [[../docs/Lens Effects]] — Distortion, chromatic aberration, grain
- [[../docs/Implemented Features]] — What's done
- [[../docs/Future Features]] — What's deferred
- [[../docs/File Inventory]] — Every source/shader/resource file
- [[../docs/Credits]] — Contributors and academic references
- [[../docs/wheel_naming_convention]] — wheel_class_style_variant format
- [[../docs/wheel_lod_system]] — Distance-based LOD switching
- [[../docs/wheel_texture_atlas]] — Combined texture optimization

---

## Current Status

### As of 2026-11-16 (root cause found, fix landed)

- **Root cause of the ~3-month untextured-world regression:** the `TexDefsFindCB` miss path
  bound the shared `skygfx_missgrey` 1×1 grey sentinel permanently into `material->texture`
  (broken `dict->parent` walk in this ModLoader/ROSA install + LOD-DFF-first streaming) → every
  building/road/prop rendered grey × ambient (pale-blue slabs by day, near-black at dawn).
  Full verdict: [[Status 2026-11-16]].
- **Fix landed — commit e7bc752 (pushed):** per-name sentinel clones, `TexDefsResolveAtDraw`
  (1 s rate-limited draw-time retry with pinned CTxdStore slot validation),
  `TexDefsRemapToWorld`, consumers wired (`bldPolicyTexture`, `vehPolicyTexture`, ped sites).
- **Awaiting one user game-run** for three one-shot receipts: `[BPIPE] Switch cb ENTER`,
  `[BldTex] bldPolicyTexture first`, `[texdefs] resolve first-call` — to decide which cb draws
  the world and why zero resolve receipts fired.
- **Pipe verified AAP-identical:** hooks byte-identical (2026-11-15 RVA/PE-mapping theory
  disproven), pipe creation identical. Frame-order chart:
  `docs/plans/2026-11-15-rendering-pipeline-order.md` (committed; its "0x53C15E is NOT a CALL"
  claim is disproven — see [[SkyGFX Pipeline Overview]]).

### As of 2026-11-10 (wave 20, on disk — unbuilt)

- **Last verified binary:** W0.5 — `2,616,832 B`, md5 `bb5d1230aba68348cb7de28f0201f7a3`. Next
  gate is W0.6, pending the address-hygiene sweep. See [[Status 2026-11-10]] and [[Gate Chain]].
- **Architecture:** `main.cpp` reduced to ≈4.4k lines; `shadows.cpp`, `rw_fixes.cpp`,
  `config.cpp`, `render_callbacks.cpp` extracted; `postfx_*` split into nine units;
  `motion_fx.cpp` + `wind.cpp` own motion and wind. See [[Current Architecture (Wave 20)]].
- **Working fix wave:** CA contract (rainbow), far/fog restore + BldZW park guard, VCS RT-copy
  guards, softbody TOCTOU gates, luminance cache-desync, safety-lever floors, sentinel honors,
  black-quad white-shield, CClouds split restore.
- **Hard rule:** game/SDK addresses live only in `src/rw/gta.h`-style named declarations; call
  sites use names, never raw hex (2026-11-10, user m0269).
- **Goal line:** match `C:\Users\aaaaaaaaa\Documents\ShareX\Screenshots\2026-09` (Sep-23 ~07:09)
  with zero on-screen glitches.

### Earlier snapshot (2026-07-15)

#### Implemented
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

#### In Progress
- Wheel extender render hook
- Mismatched wheels for beater cars
- Passenger class system
- Region-based vehicle spawning

#### Planned
- Universal dirt/wear system
- Rust texture system
- TransFender integration
- MoonLoader Lua scripts
- Forward+ unified pipeline
- GTA V/IV-style settings menu

---

## Quick Links

### Code
- `src/render/vehiclePipe.cpp` — Vehicle rendering pipeline
- `src/render/buildingPipe.cpp` — Building rendering pipeline
- `src/extras/veh_shaders.cpp` — Vehicle shader bridge
- `archive/wheels_extender/wheels_extender.cpp` — Wheel extender system (archived)
- `src/extras/wheels.cpp` — Wheel system implementation

### Shaders
- `shaders/ps/VehiclePBR_Modern.hlsl` — Unified vehicle shader
- `shaders/ps/Rubber_Vehicle.hlsl` — Tire/rubber shader (legacy, merged)
- `shaders/include/PBR_Common.hlsl` — Shared PBR functions

### Tools
- `tools/fix_build.py` — Build automation
- `tools/_archive/wheel_extractor.py` — Wheel extraction (archived)
- `tools/_archive/wheel_pipeline.py` — Wheel processing (archived)

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

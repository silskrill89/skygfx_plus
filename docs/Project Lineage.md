# Project Lineage

## Version Evolution
```
aap (original) → junior_dr (fork) → skygfx_plus (expIV, current)
```

This document traces the complete lineage of the skygfx mod across three major codebases: the original by aap, the junior_dr fork that extended it, and the skygfx_plus rewrite that merged and expanded both into a modern architecture.

## aap Original (v4.2b)

- **Binary**: SkyGfx_SA_4.2b
- ~22 source files, flat directory layout
- 9 car pipelines: PS2, PC, Xbox, Spec, Mobile, Neo, LCS, VCS, Env
- 2 building pipelines: PS2, Xbox
- 30 HLSL shaders (15 PS, 15 VS)
- Features: colour filters (PS2/PC), radiosity (fixed-function), infrared/night vision, grain, Neo water/blood drops, env map rendering, texdb detail maps, INI cycling (F10/F11), SAMP compat
- Config: ~40 fields
- No SSAO, SMAA, PBR, normal maps, wind, stochastic

The original aap release established the core architecture: a hook-based DLL injection model with pipeline switching for vehicles and buildings, INI-driven configuration, and a shader-based post-processing chain. The codebase was compact and self-contained — a single flat directory with no subfolder structure.

## junior_dr Fork

- Backed up in this repo as `backup_original/` (33 files, ~10K LOC)
- Added CAR_ENV vehicle pipe with per-pixel Fresnel reflection
- Added stochastic texturing (StochasticSamplerPS.hlsl + ps/2_a/ variants)
- Added wind animation shaders (ps2BuildingWindVS, xboxBuildingWindVS)
- Added EDED plugin (extendedplg.cpp) for per-atomic shader assignment
- Added shader radiosity (radiosityPS.hlsl + blurPS.hlsl)
- Added VCS trails (Radiosity_VCS, Blur_VCS)
- Added III/VC/VCS colour filters
- Added Mobile colour grading (gradingPS.hlsl)
- Added contrast adjustment (contrastPS.hlsl)
- Added texdb system (texdb.cpp) with per-texture detail maps, stochastic flags, dual-pass, z-write thresholds
- Added sphere map buildings (sphereBuildingVS + simpleFogPS)
- Added blood drops, moon alpha fix, corona z-test, YCbCr correction
- Added per-texture z-write threshold, grass dual-ped dual-pass
- 38 HLSL shaders (19 PS including stochastic variants, 17 VS, 2 includes)
- Config: ~80 fields

The junior_dr fork tripled the codebase size and introduced several technologies that would become foundational: stochastic texturing for grain-free detail, wind animation for buildings, and a texdb system for per-texture material overrides. The EDED plugin allowed per-atomic shader assignment — a significant step toward per-object rendering control.

## skygfx_plus (current, expIV rewrite)

- 67 source files organized in `src/{Core,entities,extras,render,rw}/` + `src/skygfx.h` at root
- ~18K LOC (.cpp), ~36K total (including headers)
- 11 car pipelines: PS2, PC, Xbox, Spec, Mobile, Neo, LCS, VCS, Env, GTAIV, Modern (PBR)
- 4 building pipelines: PS2, Xbox, GTAIV, PBR
- 69 HLSL shaders across `shaders/{ps,vs,include}/` + root + 8 pre-compiled GTAIV CSOs in `resources/cso/`
- Added: GTA IV vehicle/building pipes, Modern PBR (GGX/Smith/Schlick)
- Added: SMAA (4 quality levels), SSAO, motion blur (Burnout style)
- Added: SSS (skin/hair/vegetation), parallax water, normal buffer
- Added: Per-vehicle classification (paint/glass/tire), glass tint system
- Added: Quality presets (LOW/MED/HIGH/ULTRA), 16 game presets
- Added: 239+ config fields, unified pipeline (forward+)
- Added: Weather/timecycle expansion (GTA V style sky, sun, moon, clouds)
- Added: Debug logging, performance timers, crash handler
- Config: 239+ fields

skygfx_plus is a ground-up rewrite that merges the aap and junior_dr foundations into a modern, modular architecture. The flat source tree was replaced with a subdirectory structure organized by responsibility. The rendering pipeline was unified into a forward+ model with material classification, and the post-processing chain was expanded with SMAA, SSAO, motion blur, and SSS. GTA IV support was added for both vehicles and buildings, and a full PBR pipeline (Modern) was introduced alongside the legacy paths.

## Feature Parity Matrix

| Feature | aap (v4.2b) | junior_dr | skygfx_plus |
|---|---|---|---|
| Car Pipelines | 9 | 10 | **11** |
| Building Pipelines | 2 | 2 | **4** |
| HLSL Shaders | 30 | 38 | **69 (+ 8 CSO)** |
| Post-FX Effects | 3 | 5 | **15+** |
| Config Fields | ~40 | ~80 | **239+** |
| Stochastic Texturing | — | Yes | Yes |
| Wind Animation | — | Yes | Yes |
| SSAO | — | — | **Yes** |
| SMAA | — | — | **Yes (4 levels)** |
| PBR (GGX/Smith) | — | — | **Yes** |
| Normal Maps | — | — | **Yes** |
| SSS (skin/hair/veg) | — | — | **Yes** |
| Motion Blur | — | — | **Yes (Burnout)** |
| Parallax Water | — | — | **Yes** |
| Glass Tint System | — | — | **Yes** |
| Quality Presets | — | — | **Yes (4 tiers)** |
| Per-Atomic Shading | — | Yes (EDED) | Yes (extended) |
| GTA IV Pipes | — | Building only | **Vehicle + Building** |
| Radiosity | Fixed-function | Shader-based | Shader-based |
| Colour Filters | PS2/PC | PS2/PC/III/VC/VCS | **8 filters** |
| Debug / Diagnostics | — | — | **Logging + timers + crash handler** |

## Key Technical Decisions

### Subdirectory Structure

The original aap and junior_dr codebases used a flat source layout — every `.cpp` and `.h` file sat in a single directory. skygfx_plus adopted a hierarchical structure (`src/Core/`, `src/entities/`, `src/extras/`, `src/render/`, `src/rw/`) to manage the 67-file codebase. This separation makes it straightforward to locate code by responsibility and prevents the "everything in one folder" problem that made the earlier codebases difficult to navigate as they grew.

### hooks.cpp and diagnostics.cpp Kept Separate

In expIV, hooks and diagnostics were inlined into other translation units. skygfx_plus keeps them as standalone files in `src/Core/`. The reason: hooks are the primary injection surface — keeping them visible and isolated makes it straightforward to audit what the mod intercepts from the game engine. diagnostics.cpp owns logging, performance timers, and the crash handler — separate concerns that would bloat any other file if merged. This separation also makes it easier to conditionally compile diagnostics for release builds without touching the hook logic.

### config.cpp vs Core.cpp

Core.cpp is the original aap configuration loader (~40 fields). config.cpp is the skygfx_plus evolution (239+ fields). Both exist because Core.cpp (in main.cpp) handles the legacy INI parsing path that some users still rely on, while config.cpp manages the full modern config surface including quality presets, per-vehicle classification rules, and weather/timecycle overrides. They share no code — config.cpp is a ground-up rewrite, not an extension. The dual existence allows backward compatibility with old INI files while supporting the expanded configuration that the modern pipeline requires.

## See Also
- [[Implemented Features]] — What's built on this foundation
- [[Credits]] — Who contributed
- [[Three-Codebase Comparison]] — Detailed file-by-file and feature-by-feature comparison

# Implemented Features

## Rendering
- **PBR Vehicle Shader** — GGX/Smith/Schlick, metallic energy conservation, clearcoat reflections, IBL
- **Vehicle Glass** — Schlick Fresnel, per-vehicle tint, sun lighting
- **Vehicle Rubber** — Tire-specific material
- **Car Paint Reflections** — Sphere env mapping, noise breakup
- **Environment Mapping** — PS2/PC/Xbox/Mobile/Neo/Leeds modes
- **Normal Buffer** — Stereo disparity faux normals (half-screen)
- **Cloud Shadow FBM** — Ground vehicle cloud shadows
- **Wind Animation** — Building wind shaders (inherited from junior)
- **Stochastic Texturing** — Grain-free detail maps (inherited from junior)
- **Adaptive Tonemap** — Hable filmic with timecycle-driven exposure, toe, and grade

## Vehicle System
- **All 11 Vehicle Pipes** — PS2 through Modern PBR, backwards compatible
- **Per-Vehicle Classification** — Paint, glass, tire, headlight/taillight detection
- **Glass Tint System** — Taxi/cop/gang/lowrider/casual tint profiles
- **Wheel Extender** — Extended wheel system with LOD management
- **Vehicle Registry** — Model ID mapping and shader assignment

## Building Pipeline
- **4 Building Pipes** — PS2, Xbox/PC, GTAIV, PBR (timecycle ambient, no hardcoded floor)
- **Wind Animation** — Building wind shaders (PS2 + Xbox)
- **Stochastic Building Shaders** — Grain-free detail for buildings
- **Sphere Map Buildings** — Sphere-mapped environment mapping

## Anti-Aliasing
- **SMAA** — 4-level quality (LOW/MED/HIGH/ULTRA)
- **SMAA Predication** — Depth-based edge detection

## Ambient Occlusion
- **SSAO** — Hemisphere-oriented with normal buffer
- **SSAO Quality** — Configurable radius, power, kernel, samples

## Post-Processing
- **Motion Blur** — Burnout Paradise-style speed blur
- **SSS Post-Process** — Screen-space skin translucency blur
- **Skin Enhancement** — Wrap lighting for SSS approximation
- **Hair Enhancement** — Anisotropic Kajiya-Kay highlights
- **Vegetation Enhancement** — SSS-like translucency
- **Pipe Chain** — 4-pass post-processing chain
- **Color Filters** — PS2, PC, Mobile, III, VC, VCS, GTAIV (8 filters)
- **GTA IV Filmic Tonemapping** — Hable curve, luminance-only
- **Adaptive Tonemap** — Hable/Uncharted2 with scene-adaptive exposure, toe, and grading
- **Grain Filter** — Film grain
- **Infrared / Night Vision** — Special vision modes
- **VCS Trails** — Light trail effects
- **III Trails** — GTA III trail effects
- **Color Grading** — Mobile-style grading
- **Contrast Adjustment** — Screen contrast control

## Weather / Timecycle
- **GTA V-style Sky** — Dynamic sky rendering
- **Weather Expansion** — Sun, moon, clouds, lighting overhaul
- **Weather Timecycle** — Configurable time-of-day lighting

## Water
- **Parallax Water** — Parallax-mapped water surface
- **Neo Water Drops** — Rain drop effects
- **Neo Blood Drops** — Blood drop effects

## Quality System
- **Quality Presets** — LOW/MED/HIGH/ULTRA with sensible defaults
- **Game Presets** — 16 pre-configured game presets
- **Config Profiles** — INI cycling (skygfx.1.ini - skygfx.9.ini)

## Build
- **fix_build.py** — Auto-detect SDK, compile shaders, MSBuild, deploy
- **Guard against multiple CreateShaders calls**
- **Bundled shader support** — Multiple entry points per HLSL

## Infrastructure
- **Debug Menu** — Dear ImGui-based debug overlay
- **Debug Logging** — File-based logging
- **Performance Timers** — QueryPerformanceCounter profiling
- **Crash Handler** — Exception handling with diagnostics
- **SAMP Compatibility** — Multiplayer fix
- **Shared Memory Bridge** — IPC bridge for external tools

## See Also
- [[Future Features]] — What's deferred
- [[Backwards Compatibility]] — All INI settings verified
- [[Roadmap to Ultimate Mod]] — Development roadmap

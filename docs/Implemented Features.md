# Implemented Features

#status #features

## Rendering
- ✅ **PBR Vehicle Shader** — GGX/Smith/Schlick, energy conservation, IBL
- ✅ **Vehicle Glass** — Schlick Fresnel, per-vehicle tint, sun lighting
- ✅ **Vehicle Rubber** — Tire-specific material
- ✅ **Car Paint Reflections** — Sphere env mapping, noise breakup
- ✅ **Environment Mapping** — PS2/PC/Xbox/Mobile/Neo/Leeds modes
- ✅ **Normal Buffer** — Stereo disparity faux normals (half-screen)
- ✅ **Cloud Shadow FBM** — Ground vehicle cloud shadows

## Anti-Aliasing
- ✅ **SMAA** — 4-level quality (LOW/MED/HIGH/ULTRA)
- ✅ **SMAA Predication** — Depth-based edge detection

## Ambient Occlusion
- ✅ **SSAO** — Hemisphere-oriented with normal buffer
- ✅ **SSAO Quality** — Configurable radius, power, kernel, samples

## Post-Processing
- ✅ **Motion Blur** — Burnout Paradise-style speed blur
- ✅ **SSS Post-Process** — Screen-space skin translucency blur
- ✅ **Skin Enhancement** — Wrap lighting for SSS approximation
- ✅ **Hair Enhancement** — Anisotropic Kajiya-Kay highlights
- ✅ **Vegetation Enhancement** — SSS-like translucency
- ✅ **Pipe Chain** — 4-pass post-processing chain
- ✅ **Color Filters** — PS2, PC, Mobile, III, VC, VCS, GTAIV
- ✅ **GTA IV Filmic Tonemapping** — Hable curve, luminance-only
- ✅ **Grain Filter** — Film grain
- ✅ **Infrared / Night Vision** — Special vision modes

## Vehicle System
- ✅ **Per-Vehicle Classification** — Paint, glass, tire, headlight/taillight detection
- ✅ **Glass Tint System** — Taxi/cop/gang/lowrider/casual tint profiles
- ✅ **Quality Presets** — LOW/MED/HIGH/ULTRA with sensible defaults
- ✅ **All 11 Vehicle Pipes** — PS2 through Modern PBR, backwards compatible

## Build
- ✅ **fix_build.py** — Auto-detect SDK, compile shaders, MSBuild, deploy
- ✅ **Guard against multiple CreateShaders calls**
- ✅ **Bundled shader support** — Multiple entry points per HLSL

## See Also
- [[Future Features]] — What's deferred
- [[Backwards Compatibility]] — All INI settings verified

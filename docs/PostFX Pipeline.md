# PostFX Pipeline

#postfx #pipeline

## Overview

Screen-space post-processing effects applied after the main scene render. Implemented in `postfx.cpp` with effects dispatched through `ColourFilter_switch`.

## Effects

### SSAO (Screen-Space Ambient Occlusion)
- Depth-based with configurable radius, power, kernel, samples
- Uses INTZ depth format when available (falls back to D24S8)
- Normal buffer integration for hemisphere-oriented sampling
- Runs before SMAA in the pipeline
- Config: `ssaoEnable`, `ssaoRadius`, `ssaoPower`, `ssaoKernelSize`, `ssaoSampleCount`

### SMAA (Subpixel Morphological Anti-Aliasing)
- 3-pass: Edge Detection → Blend Weight → Neighborhood Blending
- Optional temporal stabilization (camera movement tracking)
- 4 quality presets: LOW, MEDIUM, HIGH, ULTRA
- Predication support (depth-based edge detection)
- Must run AFTER hardware MSAA resolve
- Config: `smaaEnable`, `smaaPreset` (0=LOW, 1=MEDIUM, 2=HIGH, 3=ULTRA), `smaaPredication`, `smaaTemporal`

### Motion Blur
- Burnout Paradise-style speed-based blur
- Radial component from screen center
- Camera-aware (reduces blur when camera moves fast)
- Config: `motionBlurEnable`, `motionBlurStrength`, `motionBlurRadial`, `motionBlurSpeedFactor`, `motionBlurCameraAware`

### Color Filters

| Filter | Enum | Shader | Description |
|--------|------|--------|-------------|
| None | `COLORFILTER_NONE` | — | No filter applied |
| PS2 | `COLORFILTER_PS2` | D3DTOP_MODULATE2X | Classic PS2 two-color filter with blur |
| PC | `COLORFILTER_PC` | `ColourFilter` | PC-style with gamma 2.2 |
| Mobile | `COLORFILTER_MOBILE` | `gradingPS` | Mobile-style grading with Reinhard tonemapping |
| III | `COLORFILTER_III` | `iiiTrailsPS` | GTA III color filter |
| VC | `COLORFILTER_VC` | `vcTrailsPS` | GTA Vice City color filter |
| VCS | `COLORFILTER_VCS` | `vcTrailsPS` | GTA Vice City Stories |
| Modern | `COLORFILTER_MODERN` | `gradingPS` | Modern grading with timecycle modulation |
| GTAIV | `COLORFILTER_GTAIV` | — | Bypass mode — no filter applied (relies on SA timecycle) |

### Adaptive Tonemap (PBR Pipeline)
- **Adaptive Hable/Uncharted2** filmic tonemap with timecycle-driven parameters
- **SoftKnee** highlight compression before tonemap (prevents hard clipping)
- **PostGrade**: S-curve + brightness/contrast, adaptive to scene luminance
- **sRGB gamma encode**: Exact OETF (not pow 1/2.2)
- Interior/cutscene: same adaptive path, exposure ×0.85 dampening (cameras face sun more often)
- Exterior signals from timecycle (CColourSet):
  - `sceneLuma = tcAmbientLuma + tcDirLuma * 0.5`
  - `shadowNorm = tc.shadowStrength / 255.0` → [0, 1]
  - `fogFactor = tc.fogStart / 500.0` → [0, 1] (less fog = 1, heavy fog = 0)
  - `clouds = tc.cloudAlpha` → [0, 1]
  - `sunBright = tc.spriteBrightness` → [0, 2]
  - `streetLights = tc.lightsOnGroundBrightness` → [0, 1]
- Exposure: `baseExposure × sceneExposure × carcolsAdapt × sunDampen`
  - `sceneExposure = 1.0 / (0.70 + sceneLuma * 2.0)` clamped [0.80, 1.30]
  - `sunDampen = 1 - clamp(sunBright * 0.05, 0, 0.08)` — bright sun reduces exposure
- Toe: `clamp(0.20 - sceneLuma * 1.0 + shadowNorm * 0.08, 0.05, 0.25)`
- Grade params (fully timecycle-driven):
  - contrast = 1.15 + shadowNorm × 0.25 × fogFactor → [1.15, 1.40]
  - brightness = 0.03 + streetLights × 0.04 → [0.03, 0.07]
  - lift = clouds × 0.015 + (1-fogFactor) × 0.01 → [0.00, 0.025]
  - curveBlend = 0.25 + sceneLuma × 0.25 → [0.25, 0.50]
- Runs as part of `COLORFILTER_MODERN` path in ColourFilter_switch

### SSS (Subsurface Scattering)
- **Post-process blur**: Screen-space edge-preserving blur (`sssPostProcessEnable`)
- **Skin Enhancement**: Wrap lighting for SSS approximation (`skinEnhanceEnable`)
- **Hair Enhancement**: Anisotropic Kajiya-Kay highlights (`hairEnhanceEnable`)
- **Vegetation Enhancement**: SSS-like translucency for grass (`vegetationEnhanceEnable`)

### Other Effects
- **Grain Filter**: Film grain overlay (PS2-style VU RNG or standard)
- **Infrared / Night Vision**: Special vision modes with PS2-style implementations
- **Radiosity**: PS2, Shader, and VCS variants
- **VCS Blur**: Motion-blur-like effect for VCS color filter
- **Pipe Chain**: 4-pass post-processing chain (requires normal buffer)
- **YCbCr Filter**: YCbCr color space transformation

### Height Fog (Phase F)
- **Crytek exponential height fog** — world-space position reconstructed from depth buffer
- Depth linearization via `projInfo` pattern (same as SSAO): `viewPos.z = projInfo.z / (depth - projInfo.w)`
- World Z = `viewPos.z + camAxisZ * rayLength`
- Fog formula: `fogFactor = exp(-density * max(worldZ - startY, 0))` (single exp, no bilateral)
- Config: `heightFogEnable`, `heightFogDensity`, `heightFogHeightFalloff`, `heightFogStartHeight`, `heightFogR/G/B`
- Shader: `shaders/ps/HeightFog.hlsl` (ps_3_0, IDR=250)
- Registers: s0=scene, s1=depth, c0=fogParams, c1=fogColor, c2=projInfo, c3=screenSize, c4=camPos, c5=camAxisZ

### God Rays (Phase F)
- **Screen-space radial blur** toward projected sun position
- Sun screen pos = `worldToScreen(sunDirection × 1000 + cameraPos)` → NDC → UV
- Radial blur: 20-sample loop with exponential decay from sun center
- Config: `godRaysEnable`, `godRaysExposure`, `godRaysDecay`, `godRaysDensity`, `godRaysWeight`, `godRaysNumSamples`
- Shader: `shaders/ps/GodRays.hlsl` (ps_3_0, IDR=251)
- Registers: s0=scene, c0=sunScreenPos, c1=rayParams, c2=numSamples

## Pipeline Order

```
Scene Render → Normal Buffer → SSAO → Pipe Chain → ColourFilter_switch → SSS Blur → SMAA → Output
```

Within `ColourFilter_switch`:
1. Normal buffer generation (if `normalBufferEnable`)
2. SSAO
3. Pipe chain (if `pipeChainEnable`)
4. Config hotkey handling
5. Color filter (selected by `colorFilter` enum)
6. Motion Blur
7. **Height Fog** (if `heightFogEnable`)
8. **God Rays** (if `godRaysEnable`)
9. UpdateFrontBuffer
10. SSS Blur

## See Also

- [[GTA IV Mode]] — GTA IV filmic tonemapping details
- [[Color Grading]] — Grading system details
- [[Radiosity]] — Radiosity effect variants

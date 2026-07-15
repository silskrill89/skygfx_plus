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

## See Also

- [[GTA IV Mode]] — GTA IV filmic tonemapping details
- [[Color Grading]] — Grading system details
- [[Radiosity]] — Radiosity effect variants

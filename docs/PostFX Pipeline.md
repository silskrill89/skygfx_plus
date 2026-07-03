# PostFX Pipeline

#postfx #pipeline

## Overview
Screen-space post-processing effects applied after the main scene render.

## Effects

### SSAO (Screen-Space Ambient Occlusion)
- Depth-based with configurable radius, power, kernel, samples
- Uses INTZ depth format when available
- Normal buffer integration for hemisphere-oriented sampling
- Runs before SMAA in the pipeline

### SMAA (Subpixel Morphological Anti-Aliasing)
- 4-pass: Edge Detection → Blend Weight → Neighborhood Blending → (optional Temporal)
- 4 quality presets: LOW, MEDIUM, HIGH, ULTRA
- Predication support (depth-based edge detection)
- Must run BEFORE hardware MSAA resolve

### Motion Blur
- Burnout Paradise-style speed-based blur
- Radial component from screen center
- Camera-aware (reduces blur when camera moves fast)

### Color Filters
| Filter | Description |
|--------|-------------|
| PS2 | Classic PS2 color filter |
| PC | PC-style with gamma 2.2 |
| Mobile | Mobile-style filter |
| III | GTA III color filter |
| VC | GTA Vice City color filter |
| VCS | GTA Vice City Stories |
| GTAIV | [[GTA IV Mode\|Filmic tonemapping]] |

### SSS (Subsurface Scattering)
- **Post-process blur**: Screen-space edge-preserving blur
- **Skin Enhancement**: Wrap lighting for SSS approximation
- **Hair Enhancement**: Anisotropic Kajiya-Kay highlights
- **Vegetation Enhancement**: SSS-like translucency for grass

### Other
- **Grain Filter**: Film grain overlay
- **Infrared / Night Vision**: Special vision modes
- **Radiosity**: PS2, Shader, VCS variants
- **Pipe Chain**: 4-pass post-processing chain
- **SpeedFX**: Speed-based visual effects

## Pipeline Order
```
Scene Render → Normal Buffer → SSAO → SMAA → Color Filter → Vignette → Output
```

## See Also
- [[GTA IV Mode]] — GTA IV filmic tonemapping details
- [[Quality Presets]] — Which effects are enabled per preset
- [[Build System]] — How postfx shaders compile

# Radiosity

#radiosity #postfx

## Overview

PS2-style radiosity approximation — pre-pass that samples and bounces light for color bleeding. Implemented in `postfx.cpp`.

## Variants

| Variant | Config Value | Description |
|---------|-------------|-------------|
| PS2 (Fixed-function) | `radiosity=0` | Original PS2 approximation using fixed-function texture stage combiners |
| Shader | `radiosity=1` | Shader-based approximation using `blurPS` + `radiosityPS` |
| VCS | `vcsTrails=1` | Vice City Stories variant with multi-pass blur and trail effect |

### PS2 (Fixed-function)
- Downsamples front buffer in multiple passes
- Subtracts intensity limit using `D3DTOP_SUBTRACT` + `D3DTOP_ADD` combiners
- Adds result back to framebuffer with alpha blending
- Uses `D3D9Render` with fixed-function pipeline

### Shader
- Uses `blurPS` for Gaussian blur (vertical + horizontal passes)
- Uses `radiosityPS` for final compositing
- Configurable filter passes and render passes
- Better quality than fixed-function

### VCS
- Multi-pass blur with directional offsets (8 directions)
- Trail effect via `lastFrameBuffer` temporal blending
- Integrates with VCS color filter (`Blur_VCS`)
- Configurable resolution multiplier

## Config

```ini
radiosity=0              # 0=PS2 (fixed-function), 1=Shader
doRadiosity=1            # Master enable
radiosityFilterPasses=2  # Blur passes (shader mode)
radiosityRenderPasses=4  # Composite passes
radiosityIntensity=32    # Output intensity
radiosityIntensityLimit=128  # Clipping threshold
vcsTrails=1              # VCS trail mode
trailsLimit=128          # VCS trail limit
trailsIntensity=32       # VCS trail intensity
trailsResolution=1       # VCS resolution multiplier
```

## See Also

- [[PostFX Pipeline]] — Other effects

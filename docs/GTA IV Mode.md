# GTA IV Mode

#postfx #gtaiv #tonemapping

## Overview
GTA IV-style filmic post-processing: desaturation, gamma, saturation boost, curves, vignette, bloom, exposure.

## Parameters (c7-c12)
| Register | Content |
|----------|---------|
| c7 | {desaturation, gamma, saturation, curves} |
| c8-c12 | Filmic params (vignette, bloom, exposure) |

## Tonemapping
**Hable (Uncharted 2) luminance-only curve** — applied to luminance only, NOT RGB independently (which would desaturate).

## INI Fields
```ini
ivMode=1
ivDesaturation=1.0    # 1.0 = no desaturation (lerp factor)
ivGamma=1.0
ivSaturation=0.3
ivCurves=1.0
ivVignetteIntensity=0.5
ivVignetteRadius=0.5
ivVignetteContrast=2.0
ivBloomIntensity=0.1
ivExposure=2.5
```

## Key Design Decisions
- **ivDesaturation semantics**: 1.0 = no desaturation (not 0.0)
- **ivSaturation uses additive boost**: `color += (color - lum) * sat` avoids lerp clamping
- **Filmic params at c7+**: Safe from ColourFilter_Generic (c0-c1)
- **UI unbinding**: Shaders unbound after ColourFilter_Generic draw to prevent UI disappearing

## See Also
- [[PostFX Pipeline]] — Other post-processing effects
- [[Quality Presets]] — Preset defaults for IV mode

# GTA IV Mode

When `ivMode=1`, SkyGFX Plus applies a GTA IV-style post-processing pipeline inspired by RAGE engine's filmic tonemapping and colour grading.

## Features

- **Hable/Uncharted2 filmic tonemapping** — from RAGE `fullFilmicTonemap`
- **Luminance-dependent colour correction** — from RAGE `ApplyColorCorrection`
- **Desaturation** — GTA IV muted palette look
- **Bloom compositing** — configurable intensity
- **Vignette** — darkened screen edges (GTA IV signature)
- **Blue shift** — shadow tones shifted toward blue

## Tonemapping Curve

The Hable/Uncharted2 curve:
```
((x × (A × x + C × B) + D × E) / (x × (A × x + B) + D × F)) - E/F
```

Parameters are passed via shader constants from `filmicParams0` and `filmicParams1`.

## Configuration

```ini
[SkyGfx]
ivMode=0                ; Enable GTA IV postFX
ivDesaturation=0.3      ; Colour desaturation (0=none, 1=full)
ivGamma=1.0             ; Gamma correction
ivVignetteIntensity=0.5 ; Vignette strength
ivVignetteRadius=0.5    ; Vignette radius
ivVignetteContrast=2.0  ; Vignette edge contrast
ivBloomIntensity=0.15   ; Bloom composite intensity
ivExposure=1.0          ; Exposure multiplier
```

## Shader

The GTA IV postFX is implemented in `shaders/ps/GTAIV_ps20.hlsl`. It samples the scene and bloom textures, applies tonemapping, colour correction, vignette, and outputs the final colour.

## See Also

- [[PostEffects/Colour Filter]]
- [[Shader Architecture]]

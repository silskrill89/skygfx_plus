# Post Effects Overview

SkyGFX Plus replaces GTA SA's post-processing with platform-accurate implementations.

## Effects Summary

| Effect | Platforms | Description |
|--------|-----------|-------------|
| [[Colour Filter]] | PS2, PC, Mobile, III, VC, VCS, GTAIV | Screen colour overlay |
| [[Radiosity]] | PS2 (Shader fallback) | Highlight boost + blur (bloom) |
| [[GTA IV Mode]] | GTA IV style | Filmic tonemapping, vignette, bloom |
| Night Vision | PS2, PC | Green-tinted vision |
| Infrared Vision | PS2, PC | Thermal vision effect |
| Film Grain | PS2, PC | Noise overlay |
| SSAO | All | Screen-space ambient occlusion |
| SMAA | All | Subpixel morphological AA |
| SSS | All | Subsurface scattering (skin, cloth, vegetation) |

## Night Vision & Infrared Vision

Both have PS2 and PC implementations. The PS2 versions use different blend modes that can't be perfectly replicated on PC, so the "PS2" option uses the closest approximation.

## Film Grain

Adds a noise texture overlay. PS2 and PC versions differ in strength and generation.

## YCbCr Correction

Advanced colour correction using YCbCr colour space:

```ini
[SkyGfx]
YCbCrCorrection=0       ; Enable YCbCr correction
lumaScale=0.859         ; 219/255
lumaOffset=0.063        ; 16/255
CbScale=1.23            ; Blue-difference scale
CbOffset=0.0            ; Blue-difference offset
CrScale=1.23            ; Red-difference scale
CrOffset=0.0            ; Red-difference offset
```

## See Also

- [[PostEffects/Colour Filter]]
- [[PostEffects/Radiosity]]
- [[PostEffects/GTA IV Mode]]
- [[Shader Architecture]]

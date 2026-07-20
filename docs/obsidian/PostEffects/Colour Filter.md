# Colour Filter

The colour filter is a post-processing overlay applied after the scene is rendered. SkyGFX implements filters from multiple platforms.

## Filter Types

### PS2
```
out = in × rgb1 × 2 + in × rgb2 × 2 × alpha2 × 2
```
Uses [[Pipelines/Overview#PS2 Colour Modulation|PS2 texture modulation]] and [[Hardware Differences#PS2 Alpha Blending|blending]].

### PC / Xbox
```
out = in + in × rgb1 × alpha1 + in × rgb2 × alpha2
```
Simple additive blending.

### Mobile
More complex — see source code for details. Uses `colorcycle.dat` in addition to `timecyc.dat`.

### III / VC / VCS
Platform-specific filters from the respective games.

### GTA IV
Filmic tonemapping (Hable/Uncharted2 curve), desaturation, vignette, bloom — see [[PostEffects/GTA IV Mode]].

## Configuration

```ini
[SkyGfx]
colorFilter=PC            ; None, PS2, PC, Mobile, III, VC, VCS, GTAIV
rgb1Mult=1.0              ; RGB1 multiplier
rgb2Mult=1.0              ; RGB2 multiplier
blurLeft=4000             ; Blur U offset (4000=auto)
blurRight=4000            ; Blur U offset (4000=auto)
blurTop=4000              ; Blur V offset (4000=auto)
blurBottom=4000           ; Blur V offset (4000=auto)
```

## Colour Filter Values

The filter colours come from `timecyc.dat` (Alpha/RGB1/RGB2 columns).

## See Also

- [[PostEffects/Radiosity]]
- [[PostEffects/GTA IV Mode]]
- [[Timecycle Fix]]

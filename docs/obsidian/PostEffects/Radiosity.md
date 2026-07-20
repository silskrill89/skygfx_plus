# Radiosity

Radiosity is a bloom-like effect that boosts highlights and applies a strong blur. It is only fully implemented for the PS2 pipeline.

## How It Works

1. Current frame buffer is **downsampled** (both dimensions halved) for each filter pass
2. Using a PS2-only blend mode, highlights are separated: `tmp = in × 2 - highlightLimit`
3. This is added to the render buffer for each render pass: `out = out + tmp × intensity / 2`

> [!important]
> The PS2 blend mode `(dst - src) × 128 + dst` (equivalent to `dst × 2 - src`) **cannot** be replicated in standard D3D. This is why the Shader implementation is used.

## Configuration

```ini
[SkyGfx]
doRadiosity=1                    ; Enable radiosity (0/1)
radiosity=Shader                 ; PS2 or Shader implementation
radiosityFilterPasses=2          ; Downsample passes (default: 2)
radiosityRenderPasses=1          ; Effect render passes (default: 1)
radiosityIntensity=35            ; Effect intensity (default: 0x23 = 35)
```

## Additional stream.ini Settings

| Setting | Description |
|---------|-------------|
| `radiosityFilterPasses` | How often image is downsampled |
| `radiosityRenderPasses` | How often effect is rendered |
| `radiosityIntensity` | Intensity multiplier |
| `radiosityFilterUCorrection` | Left stretch (default: 2, don't touch) |
| `radiosityFilterVCorrection` | Top stretch (default: 2, don't touch) |

> [!note]
> The highlight limit is set from the timecycle. There's an override in the game but it's not exposed by SkyGFX.

## See Also

- [[PostEffects/Colour Filter]]
- [[Timecycle Fix]]

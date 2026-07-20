# Pipeline Overview

## Supported Pipelines

| Pipeline | Vehicle | Building | Notes |
|----------|---------|----------|-------|
| PS2 | ✅ | ✅ | Most accurate platform recreation |
| PC | ✅ | ❌ | Broken defaults, fixed by SkyGFX |
| Xbox | ✅ | ✅ | What PC should have been |
| Specular | ✅ | ❯ | PS2-style with specular lighting |
| Mobile | ✅ | ✅ | Sphere-mapped env maps |
| Neo | ✅ | ❌ | Xbox III/VC RAGE engine |
| LCS | ✅ | ❌ | PS2 LCS RSL engine |
| VCS | ✅ | ❌ | PS2 VCS RSL engine |
| Env | ✅ | ❌ | Custom env mapping |
| GTA IV | ✅ | ✅ | Forward+ rendering |

## PS2 Colour Modulation

The PS2 Graphics Synthesizer calculates texture modulation as `(A × B) / 128`, equivalent to D3D's `D3DTOP_MODULATE2X`. This means textures can be **brightened** (values modulated with 128 become 255).

On PC, `D3DTOP_MODULATE` is `(A × B) / 255` — no brightening. SkyGFX uses `MODULATE2X` to match PS2 behaviour where R\* wrote custom rendering code (building and grass pipelines).

> [!note]
> This difference is invisible where RenderWare abstracts the modulation, but visible in custom R\* code in the building and grass pipelines.

## PS2 Alpha Test (Dual Pass)

The PS2 alpha test writes colour but discards depth when a pixel fails. PC hardware can only control Z-write per drawcall, not per pixel. SkyGFX emulates PS2 behaviour with **dual pass rendering**:

1. **First pass**: Pixels *above* alpha reference, Z-write **enabled**
2. **Second pass**: Pixels *below* alpha reference, Z-write **disabled**

Most noticeable on vegetation and vehicle windscreens.

## See Also

- [[Pipelines/Vehicle Pipelines]]
- [[Pipelines/Building Pipelines]]
- [[Pipelines/Grass Rendering]]
- [[Hardware Differences]]

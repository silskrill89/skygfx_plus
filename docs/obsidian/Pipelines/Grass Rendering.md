# Grass Rendering

The grass has a custom render pipeline that differs from the default PC implementation in several ways.

## Differences from Default PC

| Feature | Default PC | SkyGFX |
|---------|-----------|--------|
| Grass placement | Broken (RNG bug) | Fixed |
| Backface culling | Enabled | Configurable |
| Ambient light | Not added | Added |
| Colour modulation | PC style | PS2 style (brighter) |

## Configuration

```ini
[SkyGfx]
grassFixPlacement=0       ; Fix broken random number generator
grassBackfaceCull=1       ; Enable/disable backface culling
grassAddAmbient=0         ; Add ambient colour to grass colour
ps2ModulateGrass=0        ; PS2 colour modulation (overrides ps2Modulate)
dualPassGrass=0           ; PS2 alpha test (overrides dualPass)
```

## Visual Comparison

Progressive additions from default to full PS2 style:

1. **Default PC** — Broken placement, culling, PC modulation
2. **+ Fixed placement** — Correct RNG
3. **+ No backface culling** — More visible grass
4. **+ PS2 colour modulation** — Brighter, more vibrant
5. **+ Add ambient** — Full PS2 look

## See Also

- [[Pipelines/Building Pipelines|Building Pipeline]]
- [[Configuration Reference]]

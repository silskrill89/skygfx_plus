# Building Pipelines

The building pipeline handles rendering of static world geometry (buildings, walls, terrain objects). Its main job is interpolating between day and night vertex prelighting, but depending on platform it also handles wet road effects, environment maps, and UV animation.

## Available Pipelines

### PS2
- All effects implemented: day/night prelighting, wet road alpha, env map, UV animation
- Ambient light added *after* material colour multiplication (brightens buildings — considered a bug)
- [[Pipelines/Overview#PS2 Colour Modulation|PS2 colour modulation]] applies

### PC/Xbox
- What the PC pipeline *should* have been
- Smooth prelight interpolation
- UV animation or env mapping
- No wet road alpha (Xbox files lack the alpha channel, but SkyGFX has no such limitation)
- Minimal differences from PS2 in lighting and env map texture mapping

### GTA IV
- Forward rendering with per-pixel lighting
- GGX/Smith/Schlick BRDF
- Up to 8 dynamic lights
- Wet road reflections via environment map
- Detail texture support

## Effects Matrix

| Effect | PS2 | PC/Xbox | GTA IV |
|--------|-----|---------|--------|
| Day/Night prelighting | Smooth (GPU) | Smooth (GPU) | Per-pixel |
| Wet road alpha | Yes | Yes | Via env map |
| Environment map | Yes | Yes | Yes |
| UV animation | Yes | Yes | N/A |
| Detail textures | No | No | Yes |

## PC Pipeline Issues

The default PC pipeline (before SkyGFX) has several problems:
- Day/night interpolation happens in *steps* on the CPU (not smooth)
- No wet road alpha (vertex alpha doesn't work well in SA)
- Env map is broken (objects flash randomly)
- No UV animation (done by MatFX, which doesn't do day/night cycle)

## INI Settings

```ini
[SkyGfx]
buildingPipe=PS2          ; PS2, PC/Xbox, GTAIV
tagsBuildingPipe=PS2      ; Pipeline for tagged objects
explicitBuildingPipe=-1   ; Force specific pipeline (-1=auto)
dualPassBuilding=1        ; PS2 alpha test emulation
ps2ModulateBuilding=0     ; PS2 colour modulation
detailMaps=1              ; Enable detail textures
stochasticTexturing=1     ; Stochastic sampling (reduces tiling)
```

## See Also

- [[Pipelines/Grass Rendering|Grass Pipeline]]
- [[Timecycle Fix]]
- [[Hardware Differences#PS2 Alpha Test (Dual Pass)|PS2 Alpha Test]]

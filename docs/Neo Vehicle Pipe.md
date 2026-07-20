# Neo Vehicle Pipe

#vehicles #neo #reflection

## Overview

Neo vehicle pipe — enhanced reflection mapping with Fresnel interpolation. Implemented in `neoCarpipe.cpp` via the `CarPipe` class.

## Features

- **Reflection Map Rendering**: Renders scene into reflection camera texture
- **Fresnel Interpolation**: View-dependent reflection intensity (configurable fresnel and power)
- **Diffuse + Specular Passes**: Two-pass rendering (diffuse with env blend, specular highlights)
- **Specular Map Support**: Per-material specularity via `CustomSpecMapPipeMaterialData`
- **Environment Mapping**: Dual environment map frames with rotation
- **Tweaking Table**: `neo\carTweakingTable.dat` for per-weather Fresnel/power/diffuse/specular overrides

## Rendering Pipeline

```
CarPipe::RenderCallback()
  ├── ShaderSetup()         — Upload world/eye/light matrices
  ├── DiffusePass()         — Base texture + env map blend (LERP stage)
  └── SpecularPass()        — Specular highlights (additive blend)
```

## Config

```ini
vehiclePipe=Neo
neoShininessMult=1.0
neoSpecularityMult=1.0
```

## Tweaking Table

The Neo pipe reads `neo\carTweakingTable.dat` for per-weather/time-of-day overrides of:
- Fresnel factor
- Specular power
- Diffuse color
- Specular color

## See Also

- [[Vehicle Pipeline]] — All pipe modes
- [[Backwards Compatibility]] — Verified settings

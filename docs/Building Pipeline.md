# Building Pipeline

#buildings #pipeline

## Overview
Building rendering supports PS2, Xbox, and GTAIV modes. Selected via `buildingPipe=` in INI.

## Pipeline Modes
| INI Value | Enum | Description |
|-----------|------|-------------|
| `PS2` | `BUILDING_PS2` | Classic PS2 building rendering |
| `PC` / `Xbox` | `BUILDING_XBOX` | Xbox/PC enhanced with env maps |
| `GTAIV` | `BUILDING_GTAIV` | GTA IV forward pass |

## Merged Building Shaders

### `buildingPipePS.hlsl` — 5 entry points
| Entry Point | Original File | Description |
|-------------|---------------|-------------|
| `main_simple` | simplePS | Basic texture * color * colorscale |
| `main_simpleDetail` | simpleDetailPS | Texture * color * detailmap * 2 |
| `main_simpleFog` | simpleFogPS | Distance fog blending |
| `main_xboxBuilding` | xboxBuildingPS | Dual-layer env + diffuse |
| `main_normMapBuilding` | normMapBuildingPS | Normal-mapped lighting |

### `stochasticBuildingPS.hlsl` — 3 entry points
| Entry Point | Original File | Description |
|-------------|---------------|-------------|
| `main_simpleStochastic` | simpleStochasticPS | Stochastic sampling variant |
| `main_simpleDetailStochastic` | simpleDetailStochasticPS | Stochastic + detail |
| `main_xboxBuildingStochastic` | xboxBuildingStochasticPS | Stochastic Xbox env |

**No external dependencies** — inline hash-based jitter, no StochasticSamplerPS.hlsl include.

## Render Flow
```
buildingPipe.cpp
├── PS2 path → ps2BuildingVS + simplePS/simpleDetailPS/simpleFogPS
├── Xbox path → xboxBuildingVS + xboxBuildingPS (or stochastic variant)
└── GTAIV path → gtaivBuildingVS + gtaivBuildingPS
```

## Dual-Pass Support
Buildings support alpha-tested dual-pass rendering via `dualPassBuilding` config. First pass writes depth with alpha test, second pass renders transparent.

## See Also
- [[Shader Architecture]] — How shaders compile and load
- [[Vehicle Pipeline]] — Vehicle rendering for comparison
- [[Backwards Compatibility]] — All INI settings verified

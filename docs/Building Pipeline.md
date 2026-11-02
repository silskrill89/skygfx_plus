# Building Pipeline

#buildings #pipeline

## Overview

Building rendering supports PS2, Xbox, GTA IV, and PBR modes. Selected via `buildingPipe=` in INI. All modes route through `CCustomBuildingDNPipeline__CustomPipeRenderCB_Switch`.

## Pipeline Modes

| INI Value | Enum | VS Used | PS Used | Description |
|-----------|------|---------|---------|-------------|
| `PS2` | `BUILDING_PS2` | `ps2BuildingVS` / `ps2BuildingWindVS` | `simplePS` / `simpleDetailPS` / `simpleNoiseDetailPS` / `simpleDetailNoiseDetailPS` | Classic PS2 building rendering with day/night blending |
| `Xbox` | `BUILDING_XBOX` | `xboxBuildingVS` / `xboxBuildingWindVS` | `xboxBuildingPS` / `xboxBuildingNoiseDetailPS` | Xbox/PC enhanced with env maps and noise-detail sampling |
| `GTAIV` | `BUILDING_GTAIV` | `gtaivBuildingVS` | `gtaivBuildingPS` | GTA IV forward pass (swapped via `ivMode` flag in PS2 path) |
| `PBR` | `BUILDING_PBR` | `xboxBuildingVS` / `xboxBuildingWindVS` | `VehiclePBR_Modern` | PBR rendering using Xbox vertex shaders + modern PBR pixel shader |

**Note**: `BUILDING_GTAIV` is a shader swap within the PS2 callback — when `config->ivMode` is set, the PS2 path swaps to `gtaivBuildingVS`/`gtaivBuildingPS`.

## Merged Building Shaders

### `buildingPipePS.hlsl` — 5 entry points

| Entry Point | Original File | Description |
|-------------|---------------|-------------|
| `main_simple` | simplePS | Basic texture × color × colorscale |
| `main_simpleDetail` | simpleDetailPS | Texture × color × detailmap × 2 |
| `main_simpleFog` | simpleFogPS | Distance fog blending |
| `main_xboxBuilding` | xboxBuildingPS | Dual-layer env + diffuse |
| `main_normMapBuilding` | normMapBuildingPS | Normal-mapped lighting |

### `noiseDetailBuildingPS.hlsl` — 3 entry points

| Entry Point | CSO (Resource ID) | Description |
|-------------|-------------------|-------------|
| `main_simpleNoiseDetail` | `simpleNoiseDetailPS.cso` (`IDR_SIMPLENOISEDETAILPS` 278) | Noise-detail jitter on a body material with no detail map; falls back to `simplePS` if the CSO is missing |
| `main_simpleDetailNoiseDetail` | `simpleDetailNoiseDetailPS.cso` (`IDR_SIMPLEDETAILNOISEDETAILPS` 231) | Detail map + noise-detail jitter |
| `main_xboxBuildingNoiseDetail` | `xboxBuildingNoiseDetailPS.cso` (`IDR_XBOXBUILDINGNOISEDETAILPS` 227) | Xbox env-map + noise-detail jitter |

Packed noise tile and knob block are fetched from sampler `s9` / `c49-c52` via `pipeBindNoise(9)`; the helpers live in `Noise.hlsl`.

## Render Flow

```
buildingPipe.cpp
├── PS2 path   → ps2BuildingVS + simplePS/simpleDetailPS/simpleNoiseDetailPS/simpleDetailNoiseDetailPS
├── Xbox path  → xboxBuildingVS + xboxBuildingPS/xboxBuildingNoiseDetailPS
├── PBR path   → xboxBuildingVS + VehiclePBR_Modern (PBR lighting)
└── GTAIV path → gtaivBuildingVS + gtaivBuildingPS (via ivMode flag)
```

All paths support:
- Day/night vertex color blending (`setDnParams`)
- Wind animation (`setWindParams`) for vegetation
- UV transform support (MatFX)
- Tag rendering (`TagRenderCB`)
- Noise-detail sampling when `config->noiseDetail` is enabled and the material's `TexInfo::noiseDetail` flag is set (`buildingPipe_selectPS`, buildingPipe.cpp:942-966)

## Dual-Pass Support

Buildings support alpha-tested dual-pass rendering via `dualPassBuilding` config. First pass writes depth with alpha test, second pass renders transparent.

## See Also

- [[Shader Architecture]] — How shaders compile and load
- [[Vehicle Pipeline]] — Vehicle rendering for comparison
- [[Backwards Compatibility]] — All INI settings verified

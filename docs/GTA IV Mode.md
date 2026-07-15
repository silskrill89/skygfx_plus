# GTA IV Mode

#postfx #gtaiv #tonemapping

## Overview

GTA IV-style rendering mode that combines building/vehicle pipeline swaps with filmic post-processing. Implemented across `iv_mode.cpp`, `vehiclePipe.cpp`, and `postfx.cpp`.

## Rendering Configuration

| Component | Value | Effect |
|-----------|-------|--------|
| `buildingPipe` | `BUILDING_GTAIV` | Swaps to `gtaivBuildingVS`/`gtaivBuildingPS` |
| `vehiclePipe` | `CAR_NEO` | Uses `CarPipe` class (Neo pipe) |
| `colorFilter` | `COLORFILTER_GTAIV` | Bypass mode — no filter applied |
| `ps2Modulate` | 0 | No PS2 color modulation |
| `dualPass` | 0 | No dual-pass rendering |
| `radiosity` | None | No radiosity (GTA IV doesn't use it) |
| `ivMode` | 1 | Enables GTA IV shader path |

## IV Post-Processing Parameters

| INI Field | Default | Description |
|-----------|---------|-------------|
| `ivMode` | 1 | Master enable for IV rendering path |
| `ivDesaturation` | 1.0 | Desaturation lerp factor (1.0 = no desaturation) |
| `ivGamma` | 1.0 | Gamma correction |
| `ivSaturation` | 0.0 | Saturation boost (additive) |
| `ivCurves` | 0.0 | Filmic curve strength |
| `ivVignetteIntensity` | 0.75 | Vignette strength |
| `ivVignetteRadius` | 0.75 | Vignette radius |
| `ivVignetteContrast` | 1.5 | Vignette edge contrast |
| `ivBloomIntensity` | 0.0 | Bloom strength |
| `ivExposure` | 1.0 | Exposure compensation |

## Tonemapping

**Hable (Uncharted 2) luminance-only curve** — applied to luminance only, NOT RGB independently (which would desaturate).

## Key Design Decisions

- **ivDesaturation semantics**: 1.0 = no desaturation (not 0.0)
- **ivSaturation uses additive boost**: `color += (color - lum) * sat` avoids lerp clamping
- **Filmic params at c7+**: Safe from `ColourFilter_Generic` (c0-c1)
- **UI unbinding**: Shaders unbound after `ColourFilter_Generic` draw to prevent UI disappearing
- **GTAIV color filter is bypass**: Relies on SA's own timecycle/carcols values

## See Also

- [[PostFX Pipeline]] — Other post-processing effects
- [[Vehicle Pipeline]] — Vehicle pipe modes
- [[Building Pipeline]] — Building pipe modes

# Backwards Compatibility

#backwards-compatibility #ini #testing

## Overview
Every `vehiclePipe=` and `buildingPipe=` INI value from the original skygfx and all forks must continue to work. Users upgrading from older versions get the same rendering by default, with new features opt-in.

## Verified Vehicle Pipe Modes

| INI Value | Status | Render Path | Shaders |
|-----------|--------|-------------|---------|
| `PS2` | ✅ OK | `_CB_PS2` | `simplePS` + `ps2EnvSpecFxPS` |
| `PC` | ✅ OK | `_CB_exe` | Original game function |
| `Xbox` | ✅ OK | `_CB_Xbox` | NULL PS (FFP) |
| `Spec` | ✅ OK | `_CB_Specular` | `simplePS` + `specCarFxPS` |
| `Neo` | ✅ OK | `CarPipe::RenderCallback` | Neo pipe |
| `LCS` / `VCS` | ✅ OK | `_CB_leeds` | `simplePS` + leeds VS |
| `Mobile` | ✅ OK | `_CB_mobile` | `mobileVehiclePipePS` + `simplePS` |
| `Env` | ✅ OK | `_CB_Env` | `envCarPS` / PBR fallback |
| `GTAIV` | ✅ OK | `_CB_PS2` (ivMode) | `gtaivVehiclePS` |
| `Modern` | ✅ OK | `_CB_Env` | `VehiclePBR_Modern` / `Glass_Vehicle` / `Rubber_Vehicle` |

## Verified Building Pipe Modes

| INI Value | Status | Notes |
|-----------|--------|-------|
| `PS2` | ✅ OK | PS2 building VS/PS |
| `PC` / `Xbox` | ✅ OK | Xbox building with env maps |
| `GTAIV` | ✅ OK | GTA IV forward pass |

## Quality Presets
| Preset | Vehicle | Building | PostFX |
|--------|---------|----------|--------|
| LOW (0) | PS2 | PS2 | None |
| MEDIUM (1) | PC | Xbox | SMAA LOW |
| HIGH (2) | Modern | Xbox | SMAA HIGH + SSAO + Motion Blur |
| ULTRA (3) | Modern | Xbox | SMAA ULTRA + SSAO + Motion Blur + SSS |

## Shader Loading Integrity
All `makePS()`/`makeVS()` calls in `CreateShaders()` verified:
- Every IDR has a matching `#define` in `resource.h`
- Every IDR has a matching `RCDATA` in `Resource.rc`
- Every CSO file exists on disk (either standalone or compiled from wrapper)
- No IDR collisions (stochastic IDs moved to 251-253 range)

## See Also
- [[Vehicle Pipeline]] — Full mode details
- [[Building Pipeline]] — Building mode details
- [[INI Configuration]] — All config fields

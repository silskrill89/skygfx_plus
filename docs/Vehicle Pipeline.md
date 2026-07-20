# Vehicle Pipeline

#vehicles #pipeline #backwards-compatible

## Overview

The vehicle pipeline handles rendering of all vehicle models. Selected via `vehiclePipe=` in INI. Each mode dispatches to a separate render callback in `vehiclePipe.cpp` through `CCustomCarEnvMapPipeline__CustomPipeRenderCB_Switch`.

## Pipeline Modes

| INI Value | Enum | Render Callback | VS Used | PS Used | Notes |
|-----------|------|-----------------|---------|---------|-------|
| `PS2` | `CAR_PS2` | `_CB_PS2` | `vehiclePipeVS` / `ps2CarFxVS` | `simplePS` | Classic PS2 env+spec FX |
| `PC` | `CAR_PC` | `_CB_exe` | Original game function | Original game | Unmodified GTA SA |
| `Xbox` | `CAR_XBOX` | `_CB_Xbox` | `xboxCarVS` | NULL (FFP) | Vertex-shader-only, fixed-function texture stages |
| `Spec` | `CAR_SPEC` | `_CB_Specular` | `vehiclePipeVS` / `specCarFxVS` | `simplePS` / `specCarFxPS` | Specular highlights |
| `Neo` | `CAR_NEO` | `CarPipe::RenderCallback` | Neo pipe shaders | NULL (FFP) | Enhanced reflections with Fresnel |
| `LCS` | `CAR_LCS` | `_CB_leeds` | `vehiclePipeVS` / `leedsCarFxVS` | `simplePS` | Leeds engine (Liberty City Stories) |
| `VCS` | `CAR_VCS` | `_CB_leeds` | `vehiclePipeVS` / `leedsCarFxVS` | `simplePS` | Leeds engine (Vice City Stories), different blend mode |
| `Mobile` | `CAR_MOBILE` | `_CB_mobile` | `mobileVehiclePipeVS` / `vehiclePipeVS` | `mobileVehiclePipePS` / `simplePS` | Mobile-style env map lerp |
| `Env` | `CAR_ENV` | `_CB_Env` | `vehiclePBRVS` | Per-mesh routing (see below) | Environment mapping with PBR fallback |
| `GTAIV` | `CAR_GTAIV` | — | `gtaivVehicleVS` | `gtaivVehiclePS` | GTA IV forward pass (via ivMode flag in PS2 path) |
| `Modern` | `CAR_MODERN` | `_CB_Env` | `vehiclePBRVS` | Per-mesh routing (see below) | Full PBR with glass, 4 color channels, GGX specular |

## CAR_ENV / CAR_MODERN Render Callback

Both use `CCustomCarEnvMapPipeline__CustomPipeRenderCB_Env` with mesh-type routing via `veh_shaders.cpp`:

```
if (isGlassMesh || isLightMesh)  → Glass_Vehicle shader
else if (isTireMesh)             → Rubber_Vehicle_Modern shader
else                             → VehiclePBR_Modern shader (full PBR path)
```

Mesh classification uses `VehShaders_IsGlassTexture`, `VehShaders_IsHeadlightTexture`, `VehShaders_IsTaillightTexture`, and `VehShaders_IsTireTexture`.

## GTA IV Vehicle Rendering

`CAR_GTAIV` activates the `ivMode` flag which causes the PS2 render callback to swap shaders to `gtaivVehicleVS`/`gtaivVehiclePS` instead of the standard PS2 path. No dedicated render callback exists for GTA IV.

## Render Callback Summary

| Callback | Modes | Key Shaders |
|----------|-------|-------------|
| `_CB_PS2` | PS2 | `vehiclePipeVS` + `simplePS`, `ps2CarFxVS` for FX pass |
| `_CB_exe` | PC | Original game function |
| `_CB_Xbox` | Xbox | `xboxCarVS`, no pixel shader |
| `_CB_Specular` | Spec | `vehiclePipeVS` + `specCarFxVS`/`specCarFxPS` |
| `CarPipe::RenderCallback` | Neo | Neo pipe shaders (Fresnel-based reflections) |
| `_CB_leeds` | LCS, VCS | `vehiclePipeVS` + `leedsCarFxVS`, VCS uses alpha blend |
| `_CB_mobile` | Mobile | `mobileVehiclePipeVS`/`mobileVehiclePipePS` for env, fallback to PS2 |
| `_CB_Env` | Env, Modern | Per-mesh: `Glass_Vehicle`, `Rubber_Vehicle_Modern`, `VehiclePBR_Modern` |

## Shader Consolidation

All legacy vehicle pixel shaders are entry points in [[VehiclePBR Modern]]:
- `main_envCar` — PS2/PC env-map car
- `main_ps2EnvSpecFx` — PS2 env+spec dual-layer
- `main_specCarFx` — Specular car FX
- `main_mobileVehicle` — Mobile vehicle
- `main_normMapVehicle` — Normal-mapped vehicle

Individual wrapper files (`envCarPS.hlsl`, etc.) redirect to the merged source.

## VS → PS Data Flow

| Register | Content | Purpose |
|----------|---------|---------|
| TEXCOORD0 | UV coords | Diffuse texture |
| TEXCOORD1 | WorldNormal | Env map + lighting |
| TEXCOORD2 | WorldPos | Position for reflections |
| TEXCOORD3 | ViewDir | Eye-to-surface (PBR only) |
| TEXCOORD4 | SunDir | Sun direction (PBR only) |
| COLOR0 | Vertex lighting | Ambient + diffuse |
| COLOR1 | EnvColor | Fresnel-based env intensity |

## See Also

- [[VehiclePBR Modern]] — The PBR shader details
- [[Glass Shader]] — Glass/light mesh rendering
- [[Vehicle Classification]] — How meshes are classified
- [[Backwards Compatibility]] — All INI settings verified

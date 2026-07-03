# Vehicle Pipeline

#vehicles #pipeline #backwards-compatible

## Overview
The vehicle pipeline handles rendering of all vehicle models. Selected via `vehiclePipe=` in INI. Each mode dispatches to a separate render callback function in `vehiclePipe.cpp`.

## Pipeline Modes

| INI Value | Enum | Render Callback | PS Used | Notes |
|-----------|------|-----------------|---------|-------|
| `PS2` | `CAR_PS2` | `_CB_PS2` | `simplePS` + `ps2EnvSpecFxPS` | Classic PS2 env+spec |
| `PC` | `CAR_PC` | `_CB_exe` | Original game function | Unmodified GTA SA |
| `Xbox` | `CAR_XBOX` | `_CB_Xbox` | NULL (FFP) | Vertex-shader-only |
| `Spec` | `CAR_SPEC` | `_CB_Specular` | `simplePS` + `specCarFxPS` | Specular highlights |
| `Neo` | `CAR_NEO` | `CarPipe::RenderCallback` | Neo pipe shaders | Enhanced reflections |
| `LCS` / `VCS` | `CAR_LCS`/`CAR_VCS` | `_CB_leeds` | `simplePS` + leeds VS | Leeds engine style |
| `Mobile` | `CAR_MOBILE` | `_CB_mobile` | `mobileVehiclePipePS` + `simplePS` | Mobile-style env lerp |
| `Env` | `CAR_ENV` | `_CB_Env` | `envCarPS` fallback / PBR | Environment mapping |
| `GTAIV` | `CAR_GTAIV` | — | `gtaivVehiclePS` | GTA IV forward pass |
| `Modern` | `CAR_MODERN` | `_CB_Env` | `VehiclePBR_Modern` / `Glass_Vehicle` / `Rubber_Vehicle` | Full PBR |

## CAR_ENV / CAR_MODERN Render Callback

Both use `CCustomCarEnvMapPipeline__CustomPipeRenderCB_Env` with mesh-type routing:

```
if (isGlassMesh || isLightMesh)  → Glass_Vehicle shader
else if (isTireMesh)             → Rubber_Vehicle shader
else if (VehiclePBR_Modern)      → VehiclePBR_Modern shader (PBR path)
else                             → envCarPS shader (fallback)
```

## Shader Consolidation

All legacy vehicle PS are now entry points in [[VehiclePBR Modern]]:
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

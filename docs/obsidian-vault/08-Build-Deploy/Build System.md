---
tags: [build, toolchain, msbuild, sdk]
created: 2025-01-02
updated: 2026-07-15
---

# Build System

> [!info] Full documentation
> See `docs/Build System.md` for the complete build system reference.

## Overview

`fix_build.py` handles everything: SDK path detection, shader compilation, MSBuild, and deployment.

## Requirements

- **MSVC** v140+ (Visual Studio 2015+)
- **Windows SDK** (10.0+)
- **fxc.exe** (DirectX SDK or Windows SDK)
- **Python 3** (for fix_build.py)

## Build Command

```bash
python tools/fix_build.py
```

## What It Does

1. Detects MSVC, Windows SDK, vcvarsall.bat
2. Compiles all HLSL → CSO via fxc (with correct `/E` entry points)
3. Runs MSBuild on `build/skygfx.vcxproj`
4. Deploys ASI + INI to game directory

## Subdirectory Structure

```
skygfx_plus/
├── src/                    # C++ source code
│   ├── Core/               # Entry point, hooks, crash handler
│   ├── render/             # Vehicle, building, postfx, weather pipes
│   ├── rw/                 # RenderWare wrappers, gta types
│   ├── extras/             # TexDB, debug menu, wheels, veh_shaders
│   └── entities/           # Character rendering, plant surfaces
├── shaders/
│   ├── ps/                 # Pixel shaders (VehiclePBR_Modern.hlsl, etc.)
│   ├── vs/                 # Vertex shaders
│   └── include/            # Shared includes (PBR_Common.hlsl, etc.)
├── tools/                  # Python tools (fix_build.py, wheel_extractor.py, etc.)
├── build/                  # MSBuild output
│   └── skygfx.vcxproj      # Main project file
├── resources/
│   └── cso/                # Compiled shader objects (embedded in ASI)
├── data/                   # Runtime data (timecyc, wheels, etc.)
└── docs/                   # Documentation
    ├── obsidian-vault/     # This vault
    └── *.md                # Project docs
```

## Shader Compilation

Each HLSL file compiles to one CSO per entry point:

```bash
fxc /E main /T ps_3_0 /Fo resources/cso/VehiclePBR_Modern.cso ps/VehiclePBR_Modern.hlsl
```

### Multi-Entry Shaders

VehiclePBR_Modern.hlsl has multiple entry points compiled separately:

```python
# From fix_build.py multi_entry list:
('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_specCarFx', 'specCarFxPS.cso', False),
('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_mobileVehicle', 'mobileVehiclePS.cso', False),
('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_rubber', 'Rubber_Vehicle_Modern.cso', False),
```

### Shader Entry Points Summary

| Entry Point | CSO Output | Purpose |
|------------|-----------|---------|
| `main` | VehiclePBR_Modern.cso | Full PBR vehicle |
| `main_rubber` | Rubber_Vehicle_Modern.cso | Tire/rubber |
| `main_ps2EnvSpecFx` | (compiled separately) | PS2 env+spec dual-layer |
| `main_specCarFx` | specCarFxPS.cso | Specular car FX |
| `main_mobileVehicle` | mobileVehiclePS.cso | Mobile vehicle |
| `main_normMapVehicle` | (compiled separately) | Normal-mapped vehicle |
| `main_building` | (compiled separately) | Building PBR |

> [!note]
> `main_glass`, `main_envCar`, and `main_leeds` do NOT exist in VehiclePBR_Modern.hlsl. Glass has its own file (`Glass_Vehicle.hlsl`). See `shaders/ps/VehiclePBR_Modern.hlsl` for the actual entry points.

## Deployment

Copies both `skygfx.asi` and `skygfx.ini` to:

```
E:\games\gtasa_skygfx_plus\
```

## Resource Embedding

Compiled CSOs are embedded as RCDATA in `Resource.rc`:

```rc
IDR_VEHICLEPBR_MODERN    RCDATA "resources/cso/VehiclePBR_Modern.cso"
IDR_RUBBER_VEHICLE_MODERN RCDATA "resources/cso/Rubber_Vehicle_Modern.cso"
IDR_GLASS_VEHICLE        RCDATA "resources/cso/Glass_Vehicle.cso"
```

Shader pointers are loaded at runtime in `pipelinecommon.cpp` via `makePS()`.

## Troubleshooting

- **"d3d9helper.h not found"** — Already fixed, uses `<d3d9types.h>`
- **SDK paths wrong** — fix_build.py auto-detects from vcvarsall.bat
- **CSO not found** — Check Resource.rc entries match IDR defines in `resource.h`
- **Shader compilation fails** — Check HLSL syntax, ensure all includes exist
- **Missing entry point** — Verify `/E` flag matches HLSL function name exactly

## See Also

- [[VehiclePBR Modern]] — Vehicle shader details and entry points
- [[SDK Dependencies]] — External SDK paths
- [[Rubber Shader Merge]] — How rubber shader was merged
- [[IBL Env Map Decision]] — IBL env map approach
- Full docs: `docs/Build System.md`, `docs/File Inventory.md`, `docs/Shader Architecture.md`

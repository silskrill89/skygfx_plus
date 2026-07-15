---
tags: [build, toolchain, msbuild, sdk]
created: 2025-01-02
updated: 2026-07-15
---

# Build System

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
│   ├── extras/             # TexDB, debug menu, extras
│   └── wheels/             # Wheel system
├── shaders/
│   ├── ps/                 # Pixel shaders (VehiclePBR_Modern.hlsl, etc.)
│   ├── vs/                 # Vertex shaders
│   └── include/            # Shared includes (PBR_Common.hlsl, etc.)
├── tools/                  # Python tools (fix_build.py, wheel_extractor.py, etc.)
├── build/                  # MSBuild output
│   ├── skygfx.vcxproj      # Main project file
│   └── cso/                # Compiled shader objects
├── data/                   # Runtime data (timecyc, wheels, etc.)
└── docs/                   # Documentation
    ├── obsidian-vault/     # This vault
    └── *.md                # Project docs
```

## Shader Compilation

Each HLSL file compiles to one CSO per entry point:

```bash
fxc /E main_envCar /T ps_2_0 /Fo cso/envCarPS.cso ps/envCarPS.hlsl
```

Wrapper HLSL files `#include` the merged source, so `envCarPS.hlsl` → `VehiclePBR_Modern.hlsl` → compiled with `/E main_envCar`.

### Multi-Entry Shaders

Some shaders have multiple entry points compiled separately:

```python
# From fix_build.py multi_entry list:
('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main', 'VehiclePBR_Modern.cso', False),
('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_envCar', 'envCarPS.cso', False),
('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_specCarFx', 'specCarFxPS.cso', False),
('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_mobileVehicle', 'mobileVehiclePS.cso', False),
('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_rubber', 'Rubber_Vehicle_Modern.cso', False),
('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_glass', 'Glass_Vehicle.cso', False),
('ps_3_0', 'VehiclePBR_Modern.hlsl', 'main_leeds', 'leedsPS.cso', False),
```

### Shader Entry Points Summary

| Entry Point | CSO Output | Purpose |
|------------|-----------|---------|
| `main` | VehiclePBR_Modern.cso | Full PBR vehicle |
| `main_envCar` | envCarPS.cso | Env map car |
| `main_specCarFx` | specCarFxPS.cso | Specular car FX |
| `main_mobileVehicle` | mobileVehiclePS.cso | Mobile vehicle |
| `main_rubber` | Rubber_Vehicle_Modern.cso | Tire/rubber |
| `main_glass` | Glass_Vehicle.cso | Vehicle glass |
| `main_leeds` | leedsPS.cso | Leeds pipe |

## Deployment

Copies both `skygfx.asi` and `skygfx.ini` to:

```
E:\games\gtasa_skygfx_plus\
```

## Resource Embedding

Compiled CSOs are embedded as RCDATA in `Resource.rc`:

```rc
IDR_VEHICLEPBR_MODERN    RCDATA "cso/VehiclePBR_Modern.cso"
IDR_RUBBER_VEHICLE_MODERN RCDATA "cso/Rubber_Vehicle_Modern.cso"
IDR_GLASS_VEHICLE        RCDATA "cso/Glass_Vehicle.cso"
```

Shader pointers are loaded at runtime in `pipelinecommon.cpp` via `makePS()`.

## Troubleshooting

- **"d3d9helper.h not found"** — Already fixed, uses `<d3d9types.h>`
- **SDK paths wrong** — fix_build.py auto-detects from vcvarsall.bat
- **CSO not found** — Check Resource.rc entries match IDR defines in `resource.h`
- **Shader compilation fails** — Check HLSL syntax, ensure all includes exist
- **Missing entry point** — Verify `/E` flag matches HLSL function name exactly

## See Also

- [[03-Shaders/VehiclePBR Modern]] — Vehicle shader details and entry points
- [[08-Build-Deploy/SDK Dependencies]] — External SDK paths
- [[06-Technical-Decisions/Rubber Shader Merge]] — How rubber shader was merged
- [[06-Technical-Decisions/IBL Env Map Decision]] — IBL env map approach
- Main docs: [[Build System]], [[File Inventory]], [[Shader Architecture]]

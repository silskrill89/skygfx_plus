# Build System

#build #toolchain

## Overview
`fix_build.py` handles everything: SDK path detection, shader compilation, MSBuild, and deployment.

## Requirements
- **MSVC** v140+ (Visual Studio 2015+)
- **Windows SDK** (10.0+)
- **fxc.exe** (DirectX SDK or Windows SDK)
- **Python 3** (for fix_build.py)

## Build Command
```
python tools/fix_build.py
```

## What It Does
1. Detects MSVC, Windows SDK, vcvarsall.bat
2. Compiles all HLSL → CSO via fxc (with correct `/E` entry points)
3. Runs MSBuild on `build/skygfx.vcxproj`
4. Deploys ASI + INI to game directory

## Shader Compilation
Each HLSL file compiles to one CSO per entry point:
```
fxc /E main_envCar /T ps_2_0 /Fo cso/envCarPS.cso ps/envCarPS.hlsl
```

Wrapper HLSL files `#include` the merged source, so `envCarPS.hlsl` → `VehiclePBR_Modern.hlsl` → compiled with `/E main_envCar`.

## Deployment
Copies both `skygfx.asi` and `skygfx.ini` to:
```
E:\games\gtasa_skygfx_plus\
```

## Troubleshooting
- **"d3d9helper.h not found"** — Already fixed, uses `<d3d9types.h>`
- **SDK paths wrong** — fix_build.py auto-detects from vcvarsall.bat
- **CSO not found** — Check Resource.rc entries match IDR defines

## See Also
- [[Shader Architecture]] — How shaders fit into the build
- [[File Inventory]] — All build-related files

# Shader Architecture

#shaders #architecture

## Overview
All shaders compile to CSO (Compiled Shader Object) via `fxc.exe` during build. Each HLSL file can contain multiple entry points — `fxc` compiles one entry point per invocation with `/E <entry>`. The C++ side loads CSOs from embedded resources via `makePS()`/`makeVS()`.

## Directory Layout
```
shaders/
├── include/           # Shared includes (PBR_Common, StochasticSampler, etc.)
├── vs/                # Vertex shaders (individual .hlsl files)
├── ps/                # Pixel shaders (individual .hlsl files)
│   └── 2_a/           # Stochastic building variants (redirect wrappers)
├── buildingPipePS.hlsl    # Merged building PS (5 entry points)
├── stochasticBuildingPS.hlsl  # Merged stochastic building PS (3 entry points)
└── unifiedPipe.hlsl       # 4-pass post-processing chain
```

## Consolidation Model
Individual HLSL files are now **thin wrappers** that `#include` the merged source:
```hlsl
// envCarPS.hlsl — wrapper
#include "VehiclePBR_Modern.hlsl"
// Entry point: main_envCar, compiled via /E main_envCar
```

This preserves backwards compatibility — `fix_build.py` compiles each wrapper to its own CSO with the correct entry point.

## Compilation
See [[Build System]]. Each HLSL compiles with:
```
fxc /E <entry> /T ps_2_0 /Fo cso/<name>.cso <name>.hlsl
```

## Loading
`CreateShaders()` in `pipelinecommon.cpp` loads all CSOs at startup via `makePS(IDR_*, &pointer)` and `makeVS(IDR_*, &pointer)`. Each IDR maps to a CSO in `Resource.rc`.

## See Also
- [[VehiclePBR Modern]] — The unified vehicle PBR shader
- [[Building Shaders]] — Merged building pixel shaders
- [[PBR Common]] — Shared PBR functions
- [[File Inventory]] — Complete file listing

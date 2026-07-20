# SkyGFX Plus

> **Extension of [SkyGFX](http://gta.rockstarvision.com/skygfx/skygfx.html) by aap** — PS2/Xbox/Mobile/GTA IV graphics pipelines for GTA San Andreas PC, with GTA IV Forward+ rendering, SSAO, SMAA, SSS, and a unified rendering pipeline.
>
> GTA SA uses **RW 3.6.0.3** ([GTAMods wiki](https://gtamods.com/wiki/RenderWare)). skygfx_plus uses RW 3.7 SDK headers and backports 3.7 features to the 3.6 game binary.

## What This Mod Does

SkyGFX Plus replaces GTA San Andreas PC's rendering with platform-accurate pipelines from PS2, Xbox, Mobile, and GTA IV. The name comes from RenderWare's internal codename for the PS2 platform port: **"Sky"** (after Sony's SKY PS2 simulator).

### Key Features

| Feature | Description |
|---------|-------------|
| [[Pipelines/Vehicle Pipelines\|Vehicle Pipelines]] | PS2, PC, Xbox, Specular, Mobile, Neo, LCS, VCS, Env, GTA IV |
| [[Pipelines/Building Pipelines\|Building Pipelines]] | PS2, Xbox, GTA IV with day/night interpolation |
| [[PostEffects/Colour Filter\|Colour Filters]] | PS2, PC, Mobile, III, VC, VCS, GTA IV |
| [[PostEffects/Radiosity\|Radiosity]] | PS2-style bloom/highlight boost |
| SSAO | Screen-Space Ambient Occlusion (depth-based) |
| SMAA | Subpixel Morphological Anti-Aliasing |
| SSS | Subsurface Scattering for skin, cloth, vegetation |
| [[Pipelines/Grass Rendering\|Grass]] | Fixed placement, PS2 modulation, ambient lighting |
| Unified Pipeline | GTA V-style combined lighting/occlusion/post |
| [[Normal Map Plugin]] | DK22Pac normal mapping integration |

## Quick Navigation

### Core Architecture
- [[RenderWare SDK Architecture]] — RW SDK concepts mapped to game addresses
- [[Hook Architecture]] — All hooks: installation order, addresses, conflicts
- [[D3D9 Shader Architecture]] — Shader models, HLSL, register layout
- [[Normal Map Plugin]] — DK22Pac integration and MatFX

### Rendering
- [[Pipelines/Overview]] — All rendering pipelines
- [[Pipelines/Vehicle Pipelines]] — PS2/PC/Xbox/Neo/GTAIV vehicle rendering
- [[Pipelines/Building Pipelines]] — PS2/Xbox/GTAIV building rendering
- [[Pipelines/Grass Rendering]] — Grass and vegetation
- [[PostEffects/Overview]] — All post-processing effects
- [[Forward Plus Rendering]] — GTA IV tiled forward renderer

### Reference
- [[Configuration Reference]] — Complete INI settings
- [[Debugging Guide]] — Logs, crash analysis, common issues
- [[Crash Analysis Reference]] — Detailed crash patterns and addresses
- [[Build Guide]] — How to compile from source
- [[Build/Build Scripts]] — Build system internals

### Hardware & Shaders
- [[Hardware Differences]] — Why platforms look different
- [[Hardware/Overview]] — Platform hardware architecture
- [[Hardware/PS2 GS]] — PS2 Graphics Synthesizer pixel pipeline
- [[Hardware/Xbox NV2A]] — Xbox NV2A register combiners
- [[Hardware/Early PC GPUs]] — GeForce 3/4/FX and Radeon 8500-9800
- [[Shaders/Shader Reference]] — Shader register layout and implementations

### Structures
- [[Structures/Index]] — GTA SA game data structures
- [[Structures/Entity System]] — CPlaceable, CEntity, CBuilding hierarchy
- [[OpenSA Architecture]] — OpenSA reverse engineering reference

## Source Code

```
skygfx_plus_expIV/
├── src/           # C++ plugin source
│   ├── main.cpp           # DllMain, hooks, INI reader
│   ├── buildingPipe.cpp   # Building pipeline hooks
│   ├── vehiclePipe.cpp    # Vehicle pipeline hooks
│   ├── normalmap.cpp      # Normal map plugin hooks
│   ├── postfx.cpp         # Post-processing effects
│   ├── chars.cpp          # Character SSS
│   ├── core/diagnostics.cpp  # VEH handler, watchdog
│   └── ...
├── shaders/       # HLSL pixel/vertex shaders
│   ├── vs/        # Vertex shaders
│   ├── ps/        # Pixel shaders
│   ├── ps/2_a/    # Stochastic variants
│   └── include/   # Shared includes (colorSpace, PerlinNoise, SSS)
├── resources/     # CSO binaries, RC resources
├── external/      # RW D3D9 headers, injector
└── build/         # VS solution, vcxproj
```

## Key Addresses

### Game Binary
| Address | Function |
|---------|----------|
| `0x00400000` | gta_sa.exe base |
| `0x007F39F0` | RwTexDictionaryFindNamedTexture (may be) |
| `0x005A5730` | CopyTexture |
| `0x005A6111` | Pool iteration caller |
| `0xC8800C` | CTxdPool* (TXD store pool) |

### RW SDK
| Address | Function |
|---------|----------|
| `0x7F0410` | RwFrameCreate |
| `0x7F0990` | RwFrameGetLTM |
| `0x804EF0` | RwObjectHasFrameSetFrame |
| `0x74C790` | RpGeometryForAllMaterials |
| `0x72FC40` | GetPipelineID |
| `0x811C30` | RpMatFXAtomicQueryEffects |

## Compatibility

SkyGFX Plus runs on top of **SilentPatch** (the universal GTA SA compatibility patch). SilentPatch has explicit skygfx detection via `GetProcAddress("GetConfig")` and yields moonphases hooks when skygfx is present (version >= 0x360). See [[Hook Architecture#SilentPatch Compatibility]] for full analysis.

## Credits

- **aap** — Original SkyGFX author
- **Silent** — SilentPatch compatibility
- **GTASAS 1987 Team** — Extended configuration and unified pipeline
- **DK22Pac** — Normal map plugin
- See [[Credits]] for full attribution

## Links

- [SkyGFX Original Thread](https://gtaforums.com/topic/750681-skygfx-ps2-and-xbox-graphics-for-pc/)
- [SilentPatch Thread](https://gtaforums.com/topic/669045-silentpatch/)
- [Original Documentation](http://gta.rockstarvision.com/skygfx/skygfx.html)
- [RenderWare SDK 3.7 Docs](E:\SDKs\Renderware 3.7 SDK (For Windows) Full\RWSDK37\Graphics\docs)

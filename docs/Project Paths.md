# Project Paths Reference

#paths #sdk #reference

## Key Directories

| Path | Purpose |
|------|---------|
| `E:\dev(dave)\skygfx_plus_expIV` | Project root |
| `E:\dev(dave)\skygfx_plus_expIV\src` | C++ source |
| `E:\dev(dave)\skygfx_plus_expIV\shaders\ps` | Pixel shaders |
| `E:\dev(dave)\skygfx_plus_expIV\shaders\vs` | Vertex shaders |
| `E:\dev(dave)\skygfx_plus_expIV\shaders\include` | Shared HLSL includes |
| `E:\dev(dave)\skygfx_plus_expIV\resources` | resource.h, Resource.rc |
| `E:\dev(dave)\skygfx_plus_expIV\tools\fix_build.py` | Build script |
| `E:\dev(dave)\skygfx_plus_expIV\docs` | Obsidian wiki notes |
| `E:\games\gtasa_skygfx_plus` | Game install (deploy target) |

## SDKs

| Path | Purpose |
|------|---------|
| `E:\SDKs\Renderware 3.7 SDK (For Windows) Full\RWSDK37` | **RenderWare 3.7 SDK** — rwcore.h, rpworld.h, rpnormmap.h, D3D9 plugin |
| `E:\SDKs\dk22pac_nm_plug` | DK22Pac normal map plugin — RW hook reference |
| `E:\SDKs\SilentPatch-dev\SilentPatchSA` | SilentPatch SA — water ripple fix, mirror fix |
| `E:\SDKs\shaders` | MTA:SA reference shaders (dynamic sky, water refract) |
| `E:\dev(dave)\skygfx_plus_expIV\external\plugin_sdk` | Plugin SDK (SA) |
| `E:\dev(dave)\skygfx_plus_expIV\external\gta_sa_re` | GTA SA RE headers |
| `E:\dev(dave)\skygfx_plus_expIV\external\injector` | Injector (hook library) |
| `E:\dev(dave)\skygfx_plus_expIV\external\d3d9` | RW 3.7 D3D9 headers (rwcore.h) |
| `E:\dev(dave)\skygfx_plus_expIV\external\d3dx9` | D3DX9 headers |

## RW SDK Key Files
- `RWSDK37\Graphics\rwsdk\include\d3d9\rwcore.h` — Core RW types, D3D9 functions
- `RWSDK37\Graphics\rwsdk\include\d3d9\rpworld.h` — World plugin, vertex shader effects
- `RWSDK37\Graphics\rwsdk\include\d3d9\rpnormmap.h` — Normal map plugin API
- `RWSDK37\Graphics\rwsdk\include\d3d9\rpmatfx.h` — Material FX plugin

## RW D3D9 Function Addresses (GTA SA 1.0)
```
RwD3D9SetPixelShader        = 0x7FA100
RwD3D9SetVertexShader       = 0x7FA0C0
RwD3D9SetTexture            = 0x7FDE70
RwD3D9SetRenderState        = 0x7FC2D0
RwD3D9SetTextureStageState  = 0x7FC340
RwD3D9SetSamplerState       = 0x7FC3C0
_rwD3D9RenderStateFlushCache = 0x7FC200
_rwD3D9VSGetComposedTransformMatrix = 0x7646E0
```

## Notes
- **RW SDK 3.6 and 3.7** live in `E:\SDKs\` — check there for function signatures
- **D3D9 device pointer** at game address `0xC97C28`
- **Camera** at `0xC170C4`
- **Sun direction** at `0xC812CC`
- Game uses `RwIm3DVertex` format for immediate geometry (water, grass)

## See Also
- [[RW SDK Reference]] — Function signatures, struct layouts
- [[BRDF Reference]] — PBR material system
- [[Feature Hook Pattern]] — How to hook rendering

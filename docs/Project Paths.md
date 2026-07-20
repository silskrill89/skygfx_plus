# Project Paths Reference

#paths #sdk #reference

## Key Directories

| Path | Purpose |
|------|---------|
| `C:\Dev\shaisse_hub\skygfx_plus` | Project root |
| `C:\Dev\shaisse_hub\skygfx_plus\src` | C++ source |
| `C:\Dev\shaisse_hub\skygfx_plus\shaders\ps` | Pixel shaders |
| `C:\Dev\shaisse_hub\skygfx_plus\shaders\vs` | Vertex shaders |
| `C:\Dev\shaisse_hub\skygfx_plus\shaders\include` | Shared HLSL includes |
| `C:\Dev\shaisse_hub\skygfx_plus\resources` | resource.h, Resource.rc |
| `C:\Dev\shaisse_hub\skygfx_plus\tools\fix_build.py` | Build script |
| `C:\Dev\shaisse_hub\skygfx_plus\docs` | Documentation wiki |

## SDKs

| Path | Purpose |
|------|---------|
| `E:\SDKs\Renderware 3.7 SDK (For Windows) Full\RWSDK37` | RenderWare 3.7 SDK — rwcore.h, rpworld.h, rpnormmap.h, D3D9 plugin |
| `E:\SDKs\plugin-sdk-master\plugin_sa` | Plugin SDK (SA structs, hooks, memory) |
| `external/d3d9/` | RW 3.7 D3D9 headers (rwcore.h) |
| `external/imgui/` | Debug menu UI |
| `external/injector/` | Hook injection library |

## RW SDK Key Files
- `RWSDK37/Graphics/rwsdk/include/d3d9/rwcore.h` — Core RW types, D3D9 functions
- `RWSDK37/Graphics/rwsdk/include/d3d9/rpworld.h` — World plugin, vertex shader effects
- `RWSDK37/Graphics/rwsdk/include/d3d9/rpnormmap.h` — Normal map plugin API
- `RWSDK37/Graphics/rwsdk/include/d3d9/rpmatfx.h` — Material FX plugin

## RW D3D9 Function Addresses (GTA SA 1.0)
```
RwD3D9SetPixelShader              = 0x7FA100
RwD3D9SetVertexShader             = 0x7FA0C0
RwD3D9SetTexture                  = 0x7FDE70
RwD3D9SetRenderState              = 0x7FC2D0
RwD3D9SetTextureStageState        = 0x7FC340
RwD3D9SetSamplerState             = 0x7FC3C0
_rwD3D9RenderStateFlushCache      = 0x7FC200
_rwD3D9VSGetComposedTransformMatrix = 0x7646E0
```

## Notes
- **D3D9 device pointer** at game address `0xC97C28`
- **Camera** at `0xC170C4`
- **Sun direction** at `0xC812CC`
- Game uses `RwIm3DVertex` format for immediate geometry (water, grass)

## See Also
- [[RW SDK Reference]] — Function signatures, struct layouts
- [[BRDF Reference]] — PBR material system
- [[Feature Hook Pattern]] — How to hook rendering

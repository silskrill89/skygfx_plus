# RW SDK Reference

#rw #sdk #d3d9

## Location
- `external/d3d9/` — RW SDK 3.7 (D3D9 plugin)
- Key headers: `rwcore.h`, `rpworld.h`, `rpmatfx.h`, `rpnormmap.h`

## Key Function Signatures

### Texture
```c
// Set texture on stage (note: texture first, stage second!)
RwBool RwD3D9SetTexture(RwTexture *texture, RwUInt32 stage);

// Get raster from texture
RwRaster* RwTextureGetRaster(RwTexture *texture);

// Read texture by name
RwTexture* RwTextureRead(const RwString *name, const RwString *maskName);
```

### Shaders
```c
void RwD3D9SetPixelShader(void *shader);
void RwD3D9SetVertexShader(void *shader);
void _rwD3D9SetPixelShaderConstant(RwUInt32 i, const void *data, RwUInt32 size);
void _rwD3D9SetVertexShaderConstant(RwUInt32 registerAddress, const void *constantData, RwUInt32 constantCount);
```

### Render State
```c
void RwRenderStateSet(RwRenderState state, void *value);
void RwRenderStateGet(RwRenderState state, void *value);
```

### Camera
```c
RwFrame* RwCameraGetFrame(RwCamera *camera);
RwMatrix* RwFrameGetLTM(RwFrame *frame);  // Local-to-World matrix
```

### D3D9 Device
```c
// Get from game memory at address 0xC97C28
IDirect3DDevice9 *device = *(IDirect3DDevice9**)0xC97C28;
```

### RwRaster Struct (rwcore.h:1365)
```c
struct RwRaster {
    RwRaster *parent;
    RwUInt8 *cpPixels;
    RwUInt8 *palette;
    RwInt32 width, height, depth;
    RwInt32 stride;
    RwInt16 nOffsetX, nOffsetY;
    RwUInt8 cType, cFlags, privateFlags, cFormat;
    RwUInt8 *originalPixels;
    RwInt32 originalWidth, originalHeight, originalStride;
};
```

## WRAPPER Pattern (MemoryMgr.h)
```c
#define WRAPPER __declspec(naked)
#define EAXJMP(a) { _asm mov eax, a _asm jmp eax }

// Example: function at address 0x7FA100
WRAPPER void MyFunction(args) { EAXJMP(0x7FA100); }
```

## GTA SA D3D9 Function Addresses
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

## See Also
- [[Feature Hook Pattern]] — How to use RW functions
- [[BRDF Reference]] — PBR implementation
- [[Project Paths]] — SDK locations and file paths

# Feature Hook Pattern

#hooks #pattern #reference

## How to Add a New Feature

Every feature follows this pattern. Use this as a template.

### 1. HLSL Shaders (shaders/ps/ and shaders/vs/)
- Write VS + PS in ps_3_0 (or vs_3_0)
- Entry point must be `main` (build script compiles with `/E main`)
- Use PBR_Common.hlsl for shared functions
- Use existing register layouts from other shaders as reference

### 2. Resource IDs (resources/resource.h)
```c
#define IDR_MYFEATURE_PS  260   // pick unused ID
#define IDR_MYFEATURE_VS  261
```

### 3. Resource Mapping (resources/Resource.rc)
```
IDR_MYFEATURE_PS  RCDATA "cso/MyFeature.cso"
IDR_MYFEATURE_VS  RCDATA "cso/MyFeature_VS.cso"
```

### 4. Shader Globals (src/pipelinecommon.cpp)
```c
void *MyFeature_PS = nullptr;
void *MyFeature_VS = nullptr;
```

### 5. Shader Loading (src/pipelinecommon.cpp CreateShaders)
```c
makePS(IDR_MYFEATURE_PS, &MyFeature_PS);
makeVS(IDR_MYFEATURE_VS, &MyFeature_VS);
```

### 6. Extern Declarations (src/skygfx.h)
```c
extern void *MyFeature_PS;
extern void *MyFeature_VS;
```

### 7. Config Fields (src/skygfx.h Config struct)
```c
// My Feature
RwBool myFeatureEnable;
float myFeatureParam1;
```

### 8. INI Parsing (src/config.cpp)
```c
c->myFeatureEnable = readint(cfg.get("SkyGfx", "myFeatureEnable", ""), 0);
c->myFeatureParam1 = readfloat(cfg.get("SkyGfx", "myFeatureParam1", ""), 1.0f);
```

### 9. Hook Point (src/xxxPipe.cpp)
```c
void myFeature_setRenderState(void)
{
    if(!config->myFeatureEnable || !MyFeature_PS) return;

    // Set shaders
    RwD3D9SetVertexShader(MyFeature_VS);
    RwD3D9SetPixelShader(MyFeature_PS);

    // Upload VS constants
    float wvp[16];
    _rwD3D9VSGetComposedTransformMatrix(wvp);
    RwD3D9SetVertexShaderConstant(0, wvp, 4);
    // ... more constants ...

    // Upload PS constants
    float params[4] = {config->myFeatureParam1, 0, 0, 0};
    RwD3D9SetPixelShaderConstant(0, params, 1);

    // Set textures
    RwD3D9SetTexture(0, someTexture);

    // Render states
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)true);
}

void myFeature_restoreRenderState(void)
{
    RwD3D9SetVertexShader(nullptr);
    RwD3D9SetPixelShader(nullptr);
    // Clear textures
    for(int i = 0; i < 8; i++) RwD3D9SetTexture(i, nullptr);
}
```

### 10. Build
```bash
python tools/fix_build.py
```

## Water Pipe Hook Points (for reference)
```
CWaterLevel::RenderAndEmptyRenderBuffer
  0x6E8790, 0x6E8EF1, 0x6E91E4, 0x6E9963
```

## Key GTA SA Addresses
| Address | What |
|---------|------|
| 0xC97C28 | D3D9 device pointer |
| 0xC812CC | Sun direction (float[3]) |
| 0xC170C4 | Current camera |
| 0xC8132C | UnderWaterness |
| 0x7FA100 | RwD3D9SetPixelShader |
| 0x7FA0C0 | RwD3D9SetVertexShader |
| 0x7F9880 | RwD3D9SetTexture |

## Debugging
- Set breakpoints in `waterPipe_setRenderState()` to verify shader/constants
- Check `g_waterParallaxActive` flag in debugger
- Use `dbglog()` for runtime logging
- F4 debug menu, F8 console (if enabled)

## See Also
- [[Shader Architecture]] — How shaders compile
- [[Build System]] — fix_build.py details

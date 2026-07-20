# Normal Map Plugin

> DK22Pac's Normal Map Plugin for GTA SA, integrated into skygfx_plus.
> Covers the plugin architecture, RW SDK normal map plugin, and integration points.
>
> **Note**: GTA SA uses RW 3.6.0.3. rpNORMMAP exists in both 3.6 and 3.7.

## Plugin Overview

The normal map plugin adds bump/environment mapping to GTA SA's building and vehicle pipelines. It's a **standalone RW plugin** (`rpNORMMAP`) that attaches normal map textures to materials and provides custom pipeline nodes for rendering.

**Version note**: rpNORMMAP exists in **both RW 3.6 and 3.7** — it was added during the 3.6 release cycle (confirmed by RW 3.6 changelog: "RpNormMap Updated makefiles to build on all platforms" dated 31/07/03, and "Normal Map Plugin D3D9 fixed fog on NVidia video cards" dated 17/09/03). GTA SA 3.6.0.3 has the rpNORMMAP plugin available.

### DK22Pac's Plugin (Reference)

**Source**: `E:\SDKs\normalmap_byDK_1.01\sources\normalmap\main.cpp` (1327 lines)

Key functions:
- `IsCBPCPipelineAttached` (line 1227): Pure query — checks NM pipelines, MatFX effects
- `IsCCPCPipelineAttached` (line 1246): Pure query — checks pipeline ID
- `CustomPipeAtomicSetup` (line 1207): Sets up atomic pipeline assignment
- `MaterialHasDefaultMatFXEffect` (line 1190): Returns `RpMaterial*` (correct signature)
- `CreateLight` (line 633): Creates directional light for normal mapping
- `SetupDirectionalLight` (line 676): Sets light direction from sun/moon

### Our Integration

**Source**: `src/normalmap.cpp` (322 lines)

Differences from DK22Pac:
1. `MaterialHasDefaultMatFXEffect_cb` returns `void` (DK22Pac returns `RpMaterial*`)
2. We add frame creation in query functions (DK22Pac does NOT)
3. `normalmap_init()` calls `RpNormMapPluginAttach()` which may fail

## RW SDK Normal Map Plugin

### Plugin Registration

```cpp
// From normalmap_plugin.cpp:782
#define CopyTexture(tex) \
    ((RwTexture *(__cdecl *)(RwTexture *))0x5A5730)(tex)
```

The plugin registers via:
```cpp
RpNormMapPluginAttach()  // Attaches rpNORMMAP plugin to RW
```

**Address**: The plugin functions are at hardcoded addresses in gta_sa.exe:
- `0x5A5730`: `CopyTexture(RwTexture* tex)`
- `0x5A57B0`: `PlaceTextureOnTopOfTexture`
- `0x5A5820`: `Blend2`
- `0x5A59C0`: `Blend`
- `0x5A5BC0`: `BlendTextures`
- `0x5A5F70`: `GetTextureFromTxdAndLoadNextTxd`
- `0x5A6040`: `ConstructTextures`

### Normal Map Pipeline Addresses

| Address | Function | Purpose |
|---------|----------|---------|
| `0x5DA610` | `CustomPipeAtomicSetup` | Atomic pipeline assignment |
| `0x5D7F40` | `IsCBPCPipelineAttached` | Building pipe check |
| `0x5D5B80` | `IsCCPCPipelineAttached` | Car pipe check |

### Normal Map Pipelines

```cpp
// From normalmap.cpp:282-313
gNormalMapAtomicPipelines[0] = 
    RpNormMapGetAtomicPipeline(rpNORMMAPATOMICSTATICPIPELINE);
gNormalMapAtomicPipelines[1] = 
    RpNormMapGetAtomicPipeline(rpNORMMAPATOMICSKINNEDPIPELINE);
```

These are the static and skinned normal map atomic pipelines provided by the `rpNORMMAP` plugin.

## Player Normal Maps

DK22Pac's plugin includes a full player normal map system (lines 754-1115):

1. **Loading**: Reads player body part normal maps from TXD files
2. **Compositing**: Combines torso/legs/head/feet/necklace/watch/glasses/hat normal textures
3. **Blending**: Supports fat/ripped body blending
4. **Hooks**: Installed at `0x5A6B24`, `0x5A6B77`, `0x5A6B30`, `0x5A6D30`

### CopyTexture Usage

```cpp
// Player normal map compositing (normalmap_plugin.cpp:872)
destTexture = CopyTexture(texture);

// Torso compositing (line 948)
destTexture = CopyTexture(texture);

// Face compositing (line 979)
destTexture = CopyTexture(texture);
```

**Crash risk**: `CopyTexture` at `0x5A5730` dereferences texture parameter without NULL check. If texture is NULL or invalid → crash.

## MatFX Integration

### Effect Types (rpworld.h)

```cpp
enum {
    rpMATFXEFFECTNULL = 0,          // No effect
    rpMATFXEFFECTBUMPMAP = 1,       // Bump mapping only
    rpMATFXEFFECTENVMAP = 2,        // Environment mapping (may include bump)
    rpMATFXEFFECTBUMPENVMAP = 3,    // Combined bump + environment
    rpMATFXEFFECTDUAL = 4,          // Dual texture
    rpMATFXEFFECTUVTRANSFORM = 5,   // UV transform animation
    rpMATFXEFFECTSPHERE = 6         // Sphere mapping
};
```

### MaterialHasDefaultMatFXEffect Logic

```cpp
// DK22Pac version (correct):
RpMaterial *MaterialHasDefaultMatFXEffect(RpMaterial *material, 
                                           unsigned int *hasDefaultEffect) {
    unsigned int effect = RpMatFXMaterialGetEffects(material);
    if(effect) {
        if(effect == 2) {  // BUMPENVMAP
            // Check if has envmap texture
            if(*(unsigned int *)((unsigned int)material + *(unsigned int *)0x8D12C4))
                return material;  // Continue iteration
        }
        *hasDefaultEffect = 1;
        return 0;  // Stop iteration
    }
    return material;  // Continue iteration
}

// Our version (simplified):
static RpMaterial *MaterialHasDefaultMatFXEffect_cb(RpMaterial *material, 
                                                     int *hasDefaultEffect) {
    int effect = game_RpMatFXMaterialGetEffects(material);
    if(effect) {
        if(effect == 2) {
            int matfx_offset = *(int *)0x8D12C4;
            if(*(int *)((int)material + matfx_offset))
                return material;
        }
        *hasDefaultEffect = 1;
        return NULL;
    }
    return material;
}
```

**Key difference**: DK22Pac's version returns `RpMaterial*` to continue iteration. Our version returns `NULL` to stop. Both work with `RpGeometryForAllMaterials` but the semantics differ.

## Build System

### Compiled Files

- `src/normalmap.cpp` (322 lines) — active normalmap hooks
- `src/normalmap_plugin.cpp` (1327 lines) — DK22Pac player normal maps
- `src/normmap_stubs.cpp` — stubs when normalmap disabled

### Build Commands

```bash
# Via MSBuild batch file
C:\Users\aaaaaaaaa\AppData\Local\Temp\build_skygfx.bat

# Or direct MSBuild
cmd //c "call vcvars32.bat >nul 2>&1 && msbuild build/skygfx.vcxproj /p:Configuration=Release /p:Platform=Win32"
```

## See Also

- [[RenderWare SDK Architecture]]
- [[Hook Architecture]]
- [[Pipelines/Building Pipelines]]
- [[Crash Analysis Reference]]

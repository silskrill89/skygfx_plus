# RenderWare SDK Architecture

> RenderWare Graphics is the rendering engine embedded in GTA San Andreas (gta_sa.exe).
> This document maps RW SDK concepts to actual game addresses and skygfx_plus code paths.

## SDK Version in GTA SA

GTA San Andreas is built on **RenderWare 3.6.0.3** ([GTAMods wiki](https://gtamods.com/wiki/RenderWare)):

| Game | PS2 | PC | Xbox | Android |
|------|-----|-----|------|---------|
| GTA III | 3.1.0.0 | 3.3.0.2 | 3.5.0.0 | 3.4.0.5 |
| Vice City | 3.3.0.2 | 3.4.0.3 | 3.5.0.0 | 3.4.0.5 |
| **San Andreas** | **3.6.0.3** | **3.6.0.3** | **3.6.0.3** | **3.6.0.3** |

The RW libraries are **statically linked** into gta_sa.exe — RW is NOT a DLL. All RW function addresses are hardcoded offsets.

### skygfx_plus Backporting Strategy

skygfx_plus uses the **RW 3.7 SDK headers** (`E:\SDKs\rwsdk`) to access function declarations and structures, but targets the **RW 3.6.0.3 game binary**. This means:

- **Header compatibility**: 3.7 headers are backwards-compatible with 3.6 binary. The `rwLIBRARYBASEVERSION = 0x35000` ensures binary compatibility back to RW 3.5.
- **New 3.7 features**: Some 3.7-only APIs may NOT exist in the 3.6 game binary. These must be checked at runtime or stubbed.
- **Normal map plugin (rpNORMMAP)**: EXISTS in both 3.6 and 3.7 — it was added during the 3.6 release cycle (confirmed by RW 3.6 changelog: "RpNormMap Updated makefiles" dated 31/07/03).
- **librw** targets RW 3.6.0.3 (`E:\SDKs\librw\src\base.cpp:26: int32 version = 0x36003`).

### SDK Headers

- `rwcore.lib` (includes d3d9.lib) — core rendering, frames, cameras, D3D9 wrapper
- `rpworld.lib` (includes d3dx9.lib) — geometry, atomics, materials, lighting
- `rphanim.lib` — skeletal animation (HAnim plugin)
- `rpmatfx.lib` — material effects (bump/env mapping)
- `rpnormmap.lib` — normal map plugin (exists in both 3.6 and 3.7)

## Core Object Hierarchy

```
RwObject                   (+0: type, +1: subtype, +2: flags, +4: parent ptr)
  └─ RwObjectHasFrame      (+0: RwObject, +8: lFrame LLLink, +10: sync func ptr)
       ├─ RpAtomic          (geometry + frame + pipeline + render callback)
       ├─ RpLight           (directional/point/ambient light)
       ├─ RpCamera          (view/projection + scene rendering)
       └─ RpWorldSector     (world partition cell)

RwFrame                    (+0: RwObject, +8: inDirtyListLink, +10: modelling matrix, +50: ltm matrix)
  ├─ child                 (frame hierarchy tree)
  ├─ next                  (sibling)
  └─ root                  (tree root)
```

### Key Structure Layouts (from RW SDK headers)

**RwFrame** (`rwcore.h:3924-3943`):
| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| +0x00 | 0x04 | object | RwObject header (type/subtype/flags/parent) |
| +0x08 | 0x08 | inDirtyListLink | Link into engine's dirtyFrameList |
| +0x10 | 0x40 | modelling | Local transform matrix (4x4, 16-byte aligned) |
| +0x50 | 0x40 | ltm | Local-to-world matrix |
| +0x90 | 0x08 | objectList | LinkList of objects attached to this frame |
| +0x98 | 0x04 | child | First child frame |
| +0x9C | 0x04 | next | Next sibling frame |
| +0xA0 | 0x04 | root | Root of the frame tree |

**RwObjectHasFrame** (`rwcore.h:4132-4141`):
| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| +0x00 | 0x04 | object | RwObject header |
| +0x04 | 0x08 | lFrame | LLLink for frame attachment |
| +0x0C | 0x04 | sync | Sync callback function pointer |

**RpAtomic** (starts with RwObjectHasFrame):
| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| +0x00 | 0x10 | object (hasFrame) | RwObject + lFrame + sync |
| +0x10 | 0x04 | frame | Pointer to parent RwFrame |
| +0x14 | 0x04 | geometry | Pointer to RpGeometry |
| +0x18 | 0x04 | pipeline | Pointer to RxPipeline |
| +0x1C | 0x04 | renderCallBack | User render callback |
| +0x20 | 0x04 | object.info | Atomic info/flags |

### Accessing the Frame

```cpp
// SDK macro (rpworld.h:2833)
#define RpAtomicGetFrame(_atomic) ((RwFrame *) rwObjectGetParent(_atomic))

// Direct struct access (equivalent)
RwFrame* frame = (RwFrame*)atomic->object.object.parent;
// Or: atomic->frame (at offset +0x10 from atomic start)
```

## Pipeline Architecture

### Object Pipelines vs Material Pipelines

RW uses a **two-level pipeline** system:

```
RpAtomicRender(atomic)
  → atomic->renderCallBack(atomic)     // User callback (skygfx hooks here)
    → RxPipelineExecute(pipeline)       // Object pipeline
      → Node: D3D9AtomicAllInOne        // Instancing, rendering
        → Per-material callback          // Material pipeline
          → Node: D3D9DefaultMaterial    // Texture, color, lighting
```

**Object Pipeline**: Processes an entire atomic (instancing, vertex buffer creation, state setup)
**Material Pipeline**: Processes individual materials/meshes within the atomic

### Pipeline Node Structure

Pipelines are **directed acyclic graphs (DAGs)** of nodes:
```
RxPipeline
  └─ RxPipelineNode[] (fragments)
       ├─ node->name              (string identifier)
       ├─ node->callback          (main processing function)
       ├─ node->input             (input specification)
       └─ node->output            (output specification)
```

Each node has:
- **Input**: How data enters (from previous node or atomic)
- **Processing**: The callback function
- **Output**: How data exits (to next node or to material pipeline)

### D3D9 Pipeline Node (game binary)

The game's D3D9 pipeline node at various addresses:

| Address | Function | Purpose |
|---------|----------|---------|
| `0x5A6111` | Frame sync iteration | Iterates dirty frame pool, calls sync functions |
| `0x7F39F0` | `RwTexDictionaryFindNamedTexture` | Texture lookup by name in TXD |
| `0x7F39C0` | Linked list removal | Removes frame from dirty list |
| `0x5A5730` | `CopyTexture` | Creates a texture copy |

### Custom Pipeline Assignment (skygfx)

SkyGFX replaces the default PC pipeline with custom implementations:

| Game Address | Hook | skygfx Function |
|-------------|------|-----------------|
| `0x5D7F40` | `IsCBPCPipelineAttached` | Checks if atomic should use custom building pipe |
| `0x5D5B80` | `IsCCPCPipelineAttached` | Checks if atomic should use custom car pipe |
| `0x5DA610` | `CustomPipeAtomicSetup` | Sets up atomic pipeline assignment |
| `0x5D7100` | `CreateCustomObjPipe_PS2` | Creates DN (day/night) building pipe |
| `0x5D7D90` | `CreateCustomObjPipe_PS2` | Creates standard building pipe |

## Frame System & Dirty Lists

### How Frames Get Dirty

When a frame's transform changes (e.g., object moved):
```cpp
// librw frame.cpp:307-315
void Frame::updateObjects(void) {
    if((this->root->object.privateFlags & HIERARCHYSYNC) == 0)
        engine->frameDirtyList.add(&this->root->inDirtyList);
    this->root->object.privateFlags |= HIERARCHYSYNC;
    this->object.privateFlags |= SUBTREESYNC;
}
```

### Frame Sync Dirty List

```cpp
// librw frame.cpp:244-269
void Frame::syncDirty(void) {
    FORLIST(lnk, engine->frameDirtyList) {
        frame = LLLinkGetData(lnk, Frame, inDirtyList);
        // Sync LTM from modelling matrix
        // Sync attached objects
        // Recurse to children
        frame->object.privateFlags &= ~(SYNCLTM | SYNCOBJ);
    }
    engine->frameDirtyList.init();  // Clear list
}
```

### The Crash at 0x7F39F0

The function at `0x7F39F0` in gta_sa.exe accesses `frame->inDirtyListLink` (offset +8 from frame start). When called with a NULL frame pointer:
- `NULL + 8 = 0x8` → reads from address `0x8` → ACCESS_VIOLATION (READ at 0x8)

**Root cause**: Pool at `0xC8800C` (CTxdPool*) has entries with NULL frame pointers during first render. The game's rendering code at `0x5A6111` iterates this pool and calls the function 3x with different sync function pointers.

## Material Effects (MatFX Plugin)

### MatFX Effect Types (rpworld.h)

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

### MatFX in Building Pipe

The building pipeline uses MatFX for environment mapping:
```
CustomBuildingEnvMapPipeline__SetupEnv (buildingPipe.cpp:74-103)
  → Gets atomic frame or clump frame
  → Calls RwFrameGetLTM(frame) — CRASHES if frame is NULL
  → Computes inverse view matrix
  → Sets environment map matrix
```

**Critical**: This function has NO null guard on the frame pointer. If `RpAtomicGetFrame(atomic)` returns NULL, `RwFrameGetLTM(NULL)` returns address `0x8` (NULL + offset) → crash.

## D3D9 Integration (from RW whitepaper)

### Lazy State Updates

RenderWare buffers all render state and texture state changes until the next draw call:
- `RwRenderStateSet()` → internally calls `RwD3D9SetRenderState()`
- `RwD3D9SetTextureStageState()` → buffered texture state
- Only the **last** state change before a draw is sent to D3D9

This means multiple state changes between draws are collapsed to one.

### RwCamera Rendering Cycle

```
RwCameraBeginUpdate(camera)    // Start rendering
  → Syncs all dirty frames (frameDirtyList)
  → Sets view/projection matrices
  → Processes world sectors and atomics
    → For each atomic:
      → atomic->renderCallBack(atomic)
        → Default callback: RxPipelineExecute(atomic->pipeline)
          → Pipeline nodes process the atomic
          → Per-material callbacks render meshes
RwCameraEndUpdate(camera)      // End rendering
  → Flushes D3D9 state
```

### State Management Pattern

```cpp
// Typical render callback pattern:
void renderCB(RpAtomic* atomic) {
    // 1. Set pipeline state
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    
    // 2. Set textures
    RwD3D9SetTexture(texture, 0);
    
    // 3. Set shaders
    RwD3D9SetVertexShader(vs);
    RwD3D9SetPixelShader(ps);
    
    // 4. Upload constants
    RwD3D9SetVertexShaderConstantF(0, (float*)&wvp, 4);
    
    // 5. Render
    RwD3D9DrawIndexedPrimitive(...);
    
    // 6. Cleanup (optional — RW buffers state)
    RwD3D9SetTexture(NULL, 0);
    RwD3D9SetVertexShader(NULL);
    RwD3D9SetPixelShader(NULL);
}
```

## DX9 Shader Model Reference (from ATI/NVIDIA docs)

### Vertex Shader Registers

| Model | Inputs | Temps | Constants | Max Instructions |
|-------|--------|-------|-----------|-----------------|
| vs_1_1 | 16 (v0-v15) | 12 (r0-r11) | 96 | 128 |
| vs_2_0 | 16 | 12 | 256+ | 256 slots / 1024 executed |
| vs_3_0 | 16 + samplers | 12 | 256+ | 512+ (dynamic flow) |

### Pixel Shader Registers

| Model | Temps | TC Iterators | Constants | Samplers | Max Instructions |
|-------|-------|-------------|-----------|----------|-----------------|
| ps_1_x | 2 | 2 (2D) | 8 | 4 | 8 |
| ps_2_0 | 12 | 8 (4D) | 32 | 16 | 64 ALU + 32 TEX |
| ps_2_x | 28 | 8 (4D) | 32 | 16 | 512 slots / 1024 executed |
| ps_3_0 | 32 | 10 (4D) | 32 | 16 | 512+ (dynamic flow) |

### Key Shader Instructions

| Instruction | Description | Shader Model |
|-------------|-------------|--------------|
| `tex2D(s, t)` | 2D texture sample | ps_1_1+ |
| `texCUBE(s, t)` | Cube texture sample | ps_1_1+ |
| `tex2Dbias(s, t)` | Texture sample with bias | ps_2_0+ |
| `tex2Dgrad(s, t, ddx, ddy)` | Texture sample with gradient | ps_2_x+ |
| `lrp(a, b, c)` | Linear interpolation: a*b + (1-a)*c | vs_2_0+ |
| `crs(a, b)` | Cross product | vs_2_0+ |
| `nrm(a)` | Normalize | vs_2_0+ |
| `pow(a, b)` | Power | vs_2_0+, ps_2_0+ |
| `sincos(s, out s, out c)` | Sine/cosine | vs_2_0+ |

## RW 3.7 Features Backported to 3.6 Game

skygfx_plus uses RW 3.7 SDK headers (`E:\SDKs\rwsdk`) to access declarations and structures, but the game binary is RW 3.6.0.3. The following 3.7 features are backported:

### Confirmed Available in 3.6.0.3

| Feature | Status | Notes |
|---------|--------|-------|
| rpNORMMAP (normal map plugin) | ✅ Available | Added during 3.6 cycle (changelog 31/07/03) |
| rpMATFX (material effects) | ✅ Available | Bump/env/dual mapping |
| rpSKIN (skinned mesh) | ✅ Available | Skeletal animation |
| rpHANIM (hierarchical anim) | ✅ Available | Bone hierarchy |
| D3D9 backend | ✅ Available | rwcore includes d3d9.lib |
| `rwRENDERSTATEALPHATESTFUNCTION` | ✅ Available | Added mid-3.6 |

### 3.7-Only Features (may need stubs)

| Feature | Status | Notes |
|---------|--------|-------|
| `RpSkinGetD3D9Pipeline()` | ⚠️ May not exist | D3D9 pipeline getter — check at runtime |
| `RpMatFXGetD3D9Pipeline()` | ⚠️ May not exist | D3D9 pipeline getter — check at runtime |
| `rwD3D9VERTEXSHADEREFFECT_NORMALMAP` | ⚠️ May not exist | Vertex shader effect type enum |
| MatFX cloning fix (BZ#3430) | ⚠️ May not exist | Fixed in 3.7, workaround in skygfx neo.cpp |

### Version Check in Game Binary

The game binary at `0x82457C`/`0x8245BC` contains value `0x94BF` — this is an **EXE fingerprint** (binary signature), NOT a RW library version. skygfx uses it to verify the correct gta_sa.exe v1.0 US is loaded.

### SDK References

- **RW 3.6 SDK**: `E:\SDKs\RW36_031126` (exporters/tools, dated November 2003)
- **RW 3.7 SDK headers**: `E:\SDKs\rwsdk` (full headers, used by skygfx_plus)
- **librw**: `E:\SDKs\librw` (targets RW 3.6.0.3)
- **GTAMods wiki**: [RenderWare version table](https://gtamods.com/wiki/RenderWare)

## See Also

- [[Pipelines/Building Pipelines]]
- [[Pipelines/Vehicle Pipelines]]
- [[Shaders/Shader Reference]]
- [[Structures/RwFrame and Dirty Lists]]
- [[Debugging Guide]]

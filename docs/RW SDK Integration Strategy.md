# RW SDK Integration Strategy

> **Goal**: Link RW 3.7 .libs into skygfx ASI, call RW SDK APIs directly (bone matrices, skin pipelines, env maps, lights) instead of reverse-engineering addresses.

## Compatibility Premise

- Game exe bakes **RW 3.6**; our SDK is **RW 3.7**
- APIs are "basically identical" per developer testing
- GTA SA already calls these APIs internally — we hook at the render callback level, not replacing the engine
- Linking .lib gives us **type-safe calls** + **linker-resolved addresses** rather than hardcoded 0xDEADBEEF WRAPPER macros
- Risk: RW 3.7 may have added new fields to structs. Only use **function call APIs**, never reinterpret raw struct layouts from SDK headers unless verified against SA memory

---

## RW SDK Lib Inventory (E:\SDKs\rwsdk\lib\d3d9\release\)

### Tier 1 — HIGH relevance (link and use)

| Lib | Size | Purpose | Relevance |
|-----|------|---------|-----------|
| **rpskin.lib** | 62 KB | Skinned mesh pipeline, bone access, D3D9 skin pipeline | **HIGH** — SkinPBR needs RpSkinGetSkinToBoneMatrices, bone weights/indices, RpSkinGetD3D9Pipeline |
| **rpworld.lib** | 4.7 MB | World/atomic rendering, render callbacks, pipeline get/set | **HIGH** — RpAtomicSetRenderCallBack already used via WRAPPER; link for type-safe calls |
| **rpmatfx.lib** | 72 KB | Material effects (env map, bump, dual texture) | **HIGH** — env map setup, MatFX atomic enable; GTA SA uses MatFX heavily |
| **rpnormmap.lib** | 37 KB | Normal map plugin | **HIGH** — Normal-map PBR buildings already use normalmap_plugin.cpp; link for RpNormMapMaterialSetNormMapTexture |
| **rphanim.lib** | 39 KB | HAnim hierarchy (ped bones) | **HIGH** — Character PBR needs bone matrices; RpHAnimHierarchyGetSkinHAnimHierarchy |
| **rwcore.lib** | 633 KB | Core RW: rasters, textures, cameras, lights, frames | **HIGH** — RwRasterCreate, RwTextureSetAddressing, camera/light manipulation |

### Tier 2 — MED relevance (link if needed)

| Lib | Size | Purpose | Relevance |
|-----|------|---------|-----------|
| **rpskinmatfx.lib** | 68 KB | Skin + MatFX combined pipeline | **MED** — GTA SA uses MatFX skinning for some vehicles |
| **rppatchskin.lib** | 71 KB | Patch mesh + skin (LOD system) | **MED** — may be needed for accurate skin pipeline queries |
| **rppatchskinmatfx.lib** | 74 KB | Patch skin + MatFX | **LOW-MED** — combines patch + skin + matfx |
| **rpnormmapskin.lib** | 45 KB | Normal map + skin combined | **MED** — normal-mapped skinned meshes |
| **rppatch.lib** | 64 KB | Patch meshes (mesh detail/LOD) | **LOW-MED** — patch mesh queries |
| **rppatchmatfx.lib** | 66 KB | Patch + MatFX combined | **LOW** — niche |
| **rtskinsp.lib** | 15 KB | Skin splitting toolkit | **MED** — _rpSkinSplitDataCreate/Destroy; may be needed if querying split data |
| **rpdmorph.lib** | 35 KB | Delta morph plugin | **MED** — facial animation on peds |
| **rpmorph.lib** | 11 KB | Morph plugin (basic) | **LOW** — superseded by dmorph |
| **rpltmap.lib** | 35 KB | Lightmap plugin | **LOW** — SA doesn't use RW lightmaps |

### Tier 3 — LOW relevance (skip unless specific need)

| Lib | Size | Purpose |
|-----|------|---------|
| rpcollis.lib | 67 KB | Collision detection (we don't need) |
| rppvs.lib | 54 KB | Potentially visible sets (engine handles) |
| rpspline.lib | 18 KB | Spline paths (camera paths) |
| rpusrdat.lib | 23 KB | User data plugin |
| rprandom.lib | 5 KB | Random number generator |
| rpenv.lib | 3 KB | Environment/sky plugin |
| rpadc.lib | 11 KB | ADC (analog-digital?) |
| rpanisot.lib | 5 KB | Anisotropic filtering |
| rpmipkl.lib | 7 KB | Mipmap kernel |
| rplogo.lib | 41 KB | Logo rendering (splash screens) |
| rplodatm.lib | 12 KB | LOD atomic |
| rpprtstd.lib | 81 KB | Particle standard |
| rpptank.lib | 38 KB | Particle tank |
| rperror.lib | (header only) | Error handling macros |

### Toolkit (rt* libs)

| Lib | Size | Purpose |
|-----|------|---------|
| rtworld.lib | 21 KB | World toolkit helpers |
| rtanim.lib | 16 KB | Animation interpolation |
| rtquat.lib | 2 KB | Quaternion math |
| rtslerp.lib | 6 KB | Slerp interpolation |
| rtdict.lib | 9 KB | Dictionary/hash table |
| rtcharse.lib | 12 KB | Character encoding |
| rtimport.lib | 113 KB | Import/export |
| rt2d.lib | 179 KB | 2D rendering toolkit |
| rt2danim.lib | 122 KB | 2D animation |
| rttoc.lib | 4 KB | Table of contents |
| rtgncpip.lib | 146 KB | Generic pipeline toolkit |
| rtfsyst.lib | 26 KB | Filesystem abstraction |
| rttiff.lib | 496 KB | TIFF image loading |
| rtpng.lib | 356 KB | PNG image loading |
| rtbmp.lib | 6 KB | BMP image loading |
| rtbezpat.lib | 50 KB | Bezier path |
| rtgcond.lib | 37 KB | Geometry condition |
| rtltmap.lib | 113 KB | Lightmap toolkit |
| rtltmapcnv.lib | 52 KB | Lightmap converter |
| rtintsec.lib | 10 KB | Intersection testing |
| rtray.lib | 4 KB | Ray casting |
| rtvcat.lib | 18 KB | Vertex category |
| rtwing.lib | 31 KB | Wing (wing mesh?) |
| rtbary.lib | 3 KB | Barycentric coords |
| rtcmpkey.lib | 17 KB | Comparison keys |
| rttilerd.lib | 6 KB | Tiler |
| rtmipk.lib | 9 KB | Mipmap kernel (toolkit) |
| rtpick.lib | 3 KB | Picking |
| rtpitexd.lib | 10 KB | Pixmap texture data |
| rtsplpvs.lib | 2 KB | Split PVS |

---

## Skin Pipeline API (rpskin.h) — Full Reference

This is the most critical API for character/ped PBR rendering.

### Plugin Lifecycle
```c
RpSkinPluginAttach(void);                    // Call once at init
```

### Atomic ↔ HAnim Binding
```c
RpSkinAtomicSetHAnimHierarchy(RpAtomic*, RpHAnimHierarchy*);
RpSkinAtomicGetHAnimHierarchy(const RpAtomic*);
RpSkinAtomicSetType(RpAtomic*, RpSkinType);  // rpSKINTYPEGENERIC=1, MATFX=2, TOON=3
RpSkinAtomicGetType(const RpAtomic*);
```

### Geometry ↔ Skin Binding
```c
RpSkinGeometrySetSkin(RpGeometry*, RpSkin*);
RpSkinGeometryGetSkin(RpGeometry*);
```

### Skin Data Access (critical for PBR bone matrices)
```c
RpSkinCreate(RwUInt32 numVertices);
RpSkinDestroy(RpSkin*);
RpSkinGetNumBones(RpSkin*);
RpSkinGetVertexBoneWeights(RpSkin*);     // Returns RwMatrixWeights*
RpSkinGetVertexBoneIndices(RpSkin*);     // Returns RwUInt32*
RpSkinGetSkinToBoneMatrices(RpSkin*);    // Returns RwMatrix* — the bind-pose-to-bone matrices
RpSkinIsSplit(RpSkin*);                  // Whether the skin has been split for HW limits
```

### D3D9 Pipeline Selection
```c
RpSkinGetD3D9Pipeline(RpSkinD3D9Pipeline);
// Enum values: rpSKIND3D9PIPELINEGENERIC(1), rpSKIND3D9PIPELINEMATFX(2),
//              rpSKIND3D9PIPELINETOON(3), rpSKIND3D9PIPELINEMATFXTOON(4)
```

### Skin Splitting
```c
_rpSkinSplitDataCreate(RpSkin*, RwUInt32 boneLimit, RxPipelineNode*);
_rpSkinSplitDataDestroy(RpSkin*);
```

### VS Callback Hooks (for custom vertex shader setup)
```c
_rxD3D9SkinVertexShaderSetBeginCallBack(RxPipelineNode*, ...);
_rxD3D9SkinVertexShaderSetLightingCallBack(RxPipelineNode*, ...);
_rxD3D9SkinVertexShaderSetGetMaterialShaderCallBack(RxPipelineNode*, ...);
_rxD3D9SkinVertexShaderSetMeshRenderCallBack(RxPipelineNode*, ...);
_rxD3D9SkinVertexShaderSetEndCallBack(RxPipelineNode*, ...);
```

---

## HAnim API (rphanim.h) — Bone Hierarchy

```c
RpHAnimHierarchyCreate(numNodes, numNodesInfoArray, flags, maxKeySize);
RpHAnimHierarchyCreateFromHierarchy(RpHAnimHierarchy*, RwInt32, RwInt32*);
RpHAnimHierarchyDestroy(RpHAnimHierarchy*);
RpHAnimHierarchyCreateSubHierarchy(RpHAnimHierarchy*, RwInt32, RwInt32, RwInt32*);
RpHAnimHierarchyAttach(RpHAnimHierarchy*);
RpHAnimHierarchySetFlags(hierarchy, flags);
RpHAnimHierarchyGetFlags(hierarchy);
```

### Integration with Skin
```c
// Get the HAnim hierarchy attached to an atomic's skin:
RpSkinAtomicGetHAnimHierarchy(atomic);
// Then access node matrices:
hierarchy->pMatrixArray[i];  // Current pose matrix for bone i
```

---

## MatFX API (rpmatfx.h) — Material Effects

```c
RpMatFXPluginAttach(void);
RpMatFXAtomicEnableEffects(RpAtomic*);
RpMatFXAtomicQueryEffects(RpAtomic*);
RpMatFXMaterialSetEffects(RpMaterial*, RpMatFXMaterialFlags);
RpMatFXMaterialGetEffects(const RpMaterial*);
RpMatFXMaterialSetupEnvMap(RpMaterial*);  // Enable env map on material
RpMatFXMaterialSetupBumpMap(RpMaterial*);
RpMatFXMaterialSetupDualTexture(RpMaterial*);
RpMatFXMaterialSetEnvMapTexture(RpMaterial*, RwTexture*);
RpMatFXMaterialSetEnvMapFrame(RpMaterial*, RwFrame*);
RpMatFXMaterialSetBumpMapTexture(RpMaterial*, RwTexture*);
RpMatFXMaterialSetBumpMapCoefficient(RpMaterial*, RwReal);
```

---

## Normal Map API (rpnormmap.h)

```c
RpNormMapPluginAttach(void);
RpNormMapAtomicInitialize(RpAtomic*, RpNormMapAtomicPipeline);
RpNormMapAtomicIsInitialized(const RpAtomic*);
RpNormMapGetAtomicPipeline(RpNormMapAtomicPipeline);
RpNormMapMaterialSetNormMapTexture(RpMaterial*, RwTexture*);
RpNormMapMaterialGetNormMapTexture(const RpMaterial*);
RpNormMapMaterialSetEnvMapTexture(RpMaterial*, RwTexture*);
RpNormMapMaterialSetEnvMapCoefficient(RpMaterial*, RwReal);
RpNormMapSetActiveLights(RpLight*, RpLight*);
RpNormMapWorldEnable(RpWorld*);
```

---

## World / Atomic API (rpworld.h) — Render Callbacks

```c
RpAtomicSetRenderCallBack(RpAtomic*, RpAtomicCallBackRender);
RpAtomicGetRenderCallBack(const RpAtomic*);
RpWorldGetBBox(_world);
RpWorldGetNumMaterials(_world);
RpWorldGetMaterial(_world, _num);
RpWorldGetFlags(_world);
```

This is how we chain our PBR callbacks — we store the original callback, then replace it.

---

## Integration Plan

### Phase 1: Link Core Libs (build system change)
**premake5.lua changes:**
```lua
libdirs { path.join(os.getenv("RWSDK36"), "../lib/d3d9/release") }  -- or hardcode path
links { "rpskin", "rpworld", "rpmatfx", "rphanim", "rwcore" }
```

### Phase 2: Replace WRAPPER Macros with SDK Calls
Gradually replace hardcoded address calls with type-safe SDK calls:
- `RpAtomicSetRenderCallBack` — already used via WRAPPER, can switch to direct
- `RpSkinGetSkinToBoneMatrices` — **new** — needed for custom skin VS bone matrix upload
- `RpSkinGetVertexBoneWeights` / `RpSkinGetVertexBoneIndices` — **new** — vertex bone assignment access
- `RpMatFXAtomicEnableEffects` — **new** — ensure MatFX atomics are properly set up for our env map pass

### Phase 3: SkinPBR Pipeline Integration
1. In `normalmap_plugin.cpp` `MyNormalMapSkinRenderCallBack`:
   - After setting up lights, query `RpSkinAtomicGetHAnimHierarchy(atomic)` to get bone matrices
   - Upload bone matrices to VS constants for PBR skinning
   - Use `RpSkinGetSkinToBoneMatrices` to get bind-pose inverse matrices
   - This replaces the vanilla skin VS that we can't customize without replacing the entire pipeline node

2. Custom vertex shader callback:
   - Override `_rxD3D9SkinVertexShaderSetBeginCallBack` to set our VS constants
   - Keep the vanilla skin VS but extend it with PBR data (eye pos, light dirs/cols, ambient)

### Phase 4: Env Map from RW SDK
Instead of reading game memory for env map texture, use:
- `RpMatFXMaterialGetEnvMapTexture` to query material env maps
- `RpNormMapMaterialGetEnvMapTexture` for normal-mapped materials
- `RwTextureGetRaster` → `RwD3D9TextureGetSurface` to access D3D9 surfaces

---

## What We Currently Use vs What's Available

| Feature | Current (hardcoded) | SDK Available | Gain |
|---------|-------------------|---------------|------|
| Render callback hook | WRAPPER at 0xC978D0 | `RpAtomicSetRenderCallBack` | Type safety |
| Skin bone matrices | NOT AVAILABLE | `RpSkinGetSkinToBoneMatrices` | **NEW** — custom skin VS |
| Bone weights/indices | NOT AVAILABLE | `RpSkinGetVertexBoneWeights/Indices` | **NEW** — GPU skinning |
| HAnim hierarchy | Game memory probing | `RpSkinAtomicGetHAnimHierarchy` | **NEW** — clean API |
| Env map texture | Memory reading | `RpMatFXMaterialGetEnvMapTexture` | **NEW** — clean access |
| Normal map texture | Memory reading | `RpNormMapMaterialGetNormMapTexture` | **NEW** — clean access |
| Material effects query | Manual offset probing | `RpMatFXAtomicQueryEffects` | **NEW** — robust |
| Light setup | Manual RW light struct | `RpNormMapSetActiveLights` | Cleaner API |

---

## Risk Assessment

| Risk | Mitigation |
|------|-----------|
| RW 3.7 structs differ from 3.6 in memory layout | Only use function call APIs, not raw struct reinterpret |
| Linker symbols not found at runtime | RW 3.6 already in process — 3.7 .lib should resolve to same addresses |
| Skin pipeline node override breaks vanilla rendering | Test incrementally — start with bone matrix query only |
| Multiple RW plugin attachment conflicts | Attach order matters — skin before normmap before matfx |

---

## Related Documents
- [[RW SDK Reference]] — Struct layouts, key sizes
- [[SDK Dependencies]] — Build system, linked libraries
- [[Building Pipeline]] — Current building render callback chain
- [[Shader Architecture]] — HLSL compilation, CSO embedding
- [[Feature Hook Pattern]] — How new features are added

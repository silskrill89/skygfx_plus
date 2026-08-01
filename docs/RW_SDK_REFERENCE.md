# RenderWare 3.7 SDK Reference

## Location (LOCAL)
```
E:\dev(dave)\skygfx_plus_expIV\external\d3d9
```

## Location (SDK)
```
E:\SDKs\Renderware 3.7 SDK (For Windows) Full\RWSDK37\Graphics\rwsdk\include\d3d9
```

## Key Structures

### RwObject (rwplcore.h:2082)
```
Offset  Size  Field
0       1     type
1       1     subType
2       1     flags
3       1     privateFlags
4       4     parent (void*)
```
Total: 8 bytes

### RwObjectHasFrame (rwcore.h:4136)
```
Offset  Size  Field
0       8     RwObject object
8       8     RwLLLink lFrame (next, prev)
16      4     sync (function pointer)
```
Total: 20 bytes

### RwCamera (rwcore.h:4651)
```
Offset  Size  Field
0       20    RwObjectHasFrame object
20      ...   (camera-specific fields)
```

### Crash at offset 8
When `RwCameraCreate()` returns NULL, `RwCameraSetFrame(NULL, frame)` accesses
`camera->object.lFrame.next` at offset 8 of the NULL pointer → CRASH.

Fix: Always null-check `RwCameraCreate()` and `RwFrameCreate()` results.

## Include Path
Use `external/d3d9` as the primary reference for all RW SDK headers.
The vcxproj already includes this path.

---

## SDK Locations

| Resource | Path |
|----------|------|
| **Headers** | `E:\SDKs\rwsdk\include\d3d9\` (primary), `E:\SDKs\rwsdk\include\` (root) |
| **Precompiled libs** | `E:\SDKs\rwsdk\lib\d3d9\release\` (67 .lib files) |
| **Local headers** | `E:\dev(dave)\skygfx_plus_expIV\external\d3d9\` (subset) |
| **SDK Docs** | `E:\dev(dave)\GTA SA Reverse Engineering Documentation\SDK Docs\` |

> **RW 3.6 vs 3.7**: Game exe bakes RW 3.6; our SDK is 3.7. APIs are near-identical. Link .libs for type-safe calls, but only use **function call APIs** — never reinterpret raw struct layouts unless verified against SA memory.

---

## Skin Pipeline API (rpskin.h)

Most critical API for character/ped PBR rendering.

### Plugin Lifecycle
```c
RpSkinPluginAttach(void);                    // Call once at init
```

### Atomic ↔ HAnim Binding
```c
RpSkinAtomicSetHAnimHierarchy(RpAtomic*, RpHAnimHierarchy*);
RpSkinAtomicGetHAnimHierarchy(const RpAtomic*);
RpSkinAtomicSetType(RpAtomic*, RpSkinType);
RpSkinAtomicGetType(const RpAtomic*);
// SkinType enum: rpSKINTYPEGENERIC=1, rpSKINTYPEMATFX=2, rpSKINTYPETOON=3
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
RpSkinGetSkinToBoneMatrices(RpSkin*);    // Returns RwMatrix* — bind-pose-to-bone matrices
RpSkinIsSplit(RpSkin*);                  // Whether split for HW limits
```

### D3D9 Pipeline Selection
```c
RpSkinGetD3D9Pipeline(RpSkinD3D9Pipeline);
// Enum: rpSKIND3D9PIPELINEGENERIC=1, rpSKIND3D9PIPELINEMATFX=2,
//       rpSKIND3D9PIPELINETOON=3, rpSKIND3D9PIPELINEMATFXTOON=4
```

### Skin Splitting
```c
_rpSkinSplitDataCreate(RpSkin*, RwUInt32 boneLimit, RxPipelineNode*);
_rpSkinSplitDataDestroy(RpSkin*);
```

### VS Callback Hooks (custom vertex shader setup)
```c
_rxD3D9SkinVertexShaderSetBeginCallBack(RxPipelineNode*, ...);
_rxD3D9SkinVertexShaderSetLightingCallBack(RxPipelineNode*, ...);
_rxD3D9SkinVertexShaderSetGetMaterialShaderCallBack(RxPipelineNode*, ...);
_rxD3D9SkinVertexShaderSetMeshRenderCallBack(RxPipelineNode*, ...);
_rxD3D9SkinVertexShaderSetEndCallBack(RxPipelineNode*, ...);
```

---

## HAnim API (rphanim.h)

Bone hierarchy animation — used for ped/character skeletons.

```c
RpHAnimHierarchyCreate(numNodes, numNodesInfoArray, flags, maxKeySize);
RpHAnimHierarchyCreateFromHierarchy(RpHAnimHierarchy*, RwInt32, RwInt32*);
RpHAnimHierarchyDestroy(RpHAnimHierarchy*);
RpHAnimHierarchyCreateSubHierarchy(RpHAnimHierarchy*, RwInt32, RwInt32, RwInt32*);
RpHAnimHierarchyAttach(RpHAnimHierarchy*);
RpHAnimHierarchySetFlags(hierarchy, flags);
RpHAnimHierarchyGetFlags(hierarchy);
// Hierarchy flag macros: RpHAnimHierarchySetFlagsMacro, RpHAnimHierarchyGetFlagsMacro
// Access: hierarchy->pMatrixArray[i] = current pose matrix for bone i
```

---

## MatFX API (rpmatfx.h)

Material effects (env map, bump map, dual texture).

```c
RpMatFXPluginAttach(void);
RpMatFXAtomicEnableEffects(RpAtomic*);
RpMatFXAtomicQueryEffects(RpAtomic*);
RpMatFXMaterialSetEffects(RpMaterial*, RpMatFXMaterialFlags);
RpMatFXMaterialGetEffects(const RpMaterial*);
RpMatFXMaterialSetupEnvMap(RpMaterial*);
RpMatFXMaterialSetupBumpMap(RpMaterial*);
RpMatFXMaterialSetupDualTexture(RpMaterial*);
RpMatFXMaterialSetEnvMapTexture(RpMaterial*, RwTexture*);
RpMatFXMaterialSetEnvMapFrame(RpMaterial*, RwFrame*);
RpMatFXMaterialSetBumpMapTexture(RpMaterial*, RwTexture*);
RpMatFXMaterialSetBumpMapCoefficient(RpMaterial*, RwReal);
RpMatFXMaterialGetBumpMapTexture(const RpMaterial*);
RpMatFXMaterialGetBumpMapBumpedTexture(const RpMaterial*);
RpMatFXMaterialGetBumpMapCoefficient(const RpMaterial*);
RpMatFXWorldSectorEnableEffects(RpWorldSector*);
RpMatFXWorldSectorQueryEffects(RpWorldSector*);
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
RpNormMapMaterialGetEnvMapTexture(const RpMaterial*);
RpNormMapMaterialSetEnvMapCoefficient(RpMaterial*, RwReal);
RpNormMapMaterialGetEnvMapCoefficient(const RpMaterial*);
RpNormMapMaterialSetEnvMapFrame(RpMaterial*, RwFrame*);
RpNormMapMaterialGetEnvMapFrame(const RpMaterial*);
RpNormMapMaterialModulateEnvMap(RpMaterial*, RwBool);
RpNormMapMaterialIsEnvMapModulated(const RpMaterial*);
RpNormMapSetActiveLights(RpLight*, RpLight*);
RpNormMapWorldEnable(RpWorld*);
RpNormMapWorldSectorInitialize(RpWorldSector*);
RpNormMapWorldSectorIsInitialized(RpWorldSector*);
RpNormMapWorldIsEnabled(const RpWorld*);
```

---

## World / Atomic API (rpworld.h)

Render callback chaining and atomic pipeline management.

```c
RpAtomicSetRenderCallBack(RpAtomic*, RpAtomicCallBackRender);
RpAtomicGetRenderCallBack(const RpAtomic*);
RpWorldGetBBox(_world);
RpWorldGetNumMaterials(_world);
RpWorldGetMaterial(_world, _num);
RpWorldGetNumClumps(_world);
RpWorldGetRenderOrder(_world);
RpWorldGetFlags(_world);
```

---

## RwRaster API (rwcore.h) — Selected

```c
RwRasterCreate(width, height, depth, format);
RwRasterDestroy(RwRaster*);
RwRasterGetWidth(const RwRaster*);
RwRasterGetHeight(const RwRaster*);
RwRasterGetStride(const RwRaster*);
RwRasterGetDepth(const RwRaster*);
RwRasterGetFormat(const RwRaster*);
RwRasterGetType(const RwRaster*);
RwRasterGetParent(const RwRaster*);
RwRasterRenderFast(RwRaster*, x, y);      // Used for front buffer copies
RwRasterPushContext(RwRaster*);
RwRasterPopContext(void);
RwRasterGetCurrentContext(void);
RwRasterClear(pixelValue);
```

---

## Complete .lib Inventory (67 files)

### Tier 1 — HIGH relevance (link and use)
| Lib | Size (KB) | Purpose |
|-----|-----------|---------|
| rpskin.lib | 62 | Skinned mesh pipeline, bone access, D3D9 skin pipeline |
| rpworld.lib | 4761 | World/atomic rendering, render callbacks, pipeline get/set |
| rpmatfx.lib | 72 | Material effects (env map, bump, dual texture) |
| rpnormmap.lib | 37 | Normal map plugin |
| rphanim.lib | 39 | HAnim hierarchy (ped bones) |
| rwcore.lib | 633 | Core RW: rasters, textures, cameras, lights, frames |

### Tier 2 — MED relevance
| Lib | Size (KB) | Purpose |
|-----|-----------|---------|
| rpskinmatfx.lib | 68 | Skin + MatFX combined pipeline |
| rppatchskin.lib | 71 | Patch mesh + skin (LOD system) |
| rppatchskinmatfx.lib | 74 | Patch skin + MatFX |
| rpnormmapskin.lib | 45 | Normal map + skin combined |
| rppatch.lib | 64 | Patch meshes |
| rppatchmatfx.lib | 66 | Patch + MatFX |
| rtskinsp.lib | 15 | Skin splitting toolkit |
| rpdmorph.lib | 35 | Delta morph (facial animation) |
| rpmorph.lib | 11 | Morph plugin |
| rpltmap.lib | 35 | Lightmap plugin |
| rtgncpip.lib | 146 | Generic pipeline toolkit |

### Tier 3 — LOW relevance
| Lib | Size (KB) | Purpose |
|-----|-----------|---------|
| rpcollis.lib | 67 | Collision detection |
| rppvs.lib | 54 | Potentially visible sets |
| rpspline.lib | 18 | Spline paths |
| rpusrdat.lib | 23 | User data plugin |
| rprandom.lib | 5 | Random numbers |
| rpenv.lib | 3 | Environment/sky |
| rpadc.lib | 11 | ADC |
| rpanisot.lib | 5 | Anisotropic filtering |
| rpmipkl.lib | 7 | Mipmap kernel |
| rplogo.lib | 41 | Logo/splash |
| rplodatm.lib | 12 | LOD atomic |
| rpprtstd.lib | 81 | Particles |
| rpptank.lib | 38 | Particle tank |
| rperror.h | (header) | Error macros |

### Toolkit (rt*) Libs
| Lib | Size (KB) | Purpose |
|-----|-----------|---------|
| rtworld.lib | 21 | World toolkit |
| rtanim.lib | 16 | Animation interpolation |
| rtquat.lib | 2 | Quaternion math |
| rtslerp.lib | 6 | Slerp interpolation |
| rtdict.lib | 9 | Dictionary/hash |
| rtcharse.lib | 12 | Character encoding |
| rtimport.lib | 113 | Import/export |
| rt2d.lib | 179 | 2D rendering |
| rt2danim.lib | 122 | 2D animation |
| rttoc.lib | 4 | Table of contents |
| rtfsyst.lib | 26 | Filesystem abstraction |
| rttiff.lib | 496 | TIFF loading |
| rtpng.lib | 356 | PNG loading |
| rtbmp.lib | 6 | BMP loading |
| rtbezpat.lib | 50 | Bezier path |
| rtgcond.lib | 37 | Geometry condition |
| rtltmap.lib | 113 | Lightmap toolkit |
| rtltmapcnv.lib | 52 | Lightmap converter |
| rtintsec.lib | 10 | Intersection testing |
| rtray.lib | 4 | Ray casting |
| rtvcat.lib | 18 | Vertex category |
| rtwing.lib | 31 | Wing mesh |
| rtbary.lib | 3 | Barycentric coords |
| rtcmpkey.lib | 17 | Comparison keys |
| rttilerd.lib | 6 | Tiler |
| rtmipk.lib | 9 | Mipmap kernel |
| rtpick.lib | 3 | Picking |
| rtpitexd.lib | 10 | Pixmap texture data |
| rtsplpvs.lib | 2 | Split PVS |

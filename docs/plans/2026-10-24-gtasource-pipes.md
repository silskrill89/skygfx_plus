# GTASource custom pipelines — authoritative decode (2026-10-24)

**Status:** ACTIVE (reference) · **Scope:** read-only audit of the leaked/reconstructed Rockstar SA tree
**Source root:** `E:\dev(dave)\releases\GTASource\gtasa_src-main\pipes\`
**Deliverable owner:** skygfx_plus_expIV rendering lane

All `file:line` citations are relative to the **pipes/** directory unless prefixed with `src/` (our mod)
or `modelinfo/` (SA source). Line numbers are from the files as they exist in the tree on 2026-10-24.

---

## 0. TL;DR — the five facts that change how we port

1. The "Xbox" custom pipes are **not** a single API family. Buildings on Xbox are a **bare RxPipeline
   wrapper** with *no* callbacks (`Xbox/CustomBuildingPipeline_Xbox.cpp:918-956`); DN and vehicles use
   **D3D8** (`d3d8.h`, `D3DVSD_*`, `D3DLIGHT8`); the PC branches in the *same files* are **D3D9**
   (`D3DVERTEXELEMENT9`, `D3DLIGHT9`). The compiler switch is `GTA_XBOX` vs `GTA_PC`.
2. **No shipped custom pipeline attaches a custom instance callback.** On PC, the building/DN/car
   pipes let the default RW `RxD3D9AtomicAllInOne` node own instancing, vertex buffers, Z/cull/fog and
   alpha-test, and install a **render callback** (plus a **reinstance callback** for building/car). On
   Xbox, the building pipe installs nothing, the DN pipe installs only a render wrapper, and the car
   pipe installs only a render callback. The `CustomPipeInstanceCB` code is either `#if 0`, gated on
   the disabled `GTA_PC_USE_DN_VS`, or MBR-only
   (`PC/CustomBuildingPipeline.cpp:942`, `PC/CustomBuildingDNPipeline.cpp:1395-1407`,
   `PC/CustomCarEnvMapPipeline.cpp:1627`, `Xbox/CustomBuildingPipeline_Xbox.cpp:922-926`,
   `Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1939-1941`).
3. Day/night (DN) has **two completely different implementations**:
   * PC: CPU interpolation of `preLitLum` from day/night tables, round-robin over 16 frames
     (`PC/CustomBuildingDNPipeline.cpp:275-318`, `1497-1575`) using the extra-vert-colour plugin
     (`1597-1765`).
   * Xbox: the **vertex normals carry the night colour**, the diffuse colour carries the day colour,
     and a vertex shader interpolates with `c4`/`c5` (`Xbox/CustomBuildingDNPipeline_Xbox.h:26-39`,
     `Xbox/CustomBuildingDNPipeline_Xbox.cpp:92-112`, `455-525`, `DNshader.vsh:1-28`).
4. The **Xbox vehicle render callback does not draw env-map passes at all** in the live path. The entire
   env1/env2/dual-pass block is inside `#if 0` (`Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1207-1474`); the
   live path only does alpha, material, specular light, base texture and a single draw (`1478-1578`).
   The PC callback *does* draw env1/env2/specular (`PC/CustomCarEnvMapPipeline.cpp:1348-1487`,
   `1567-1573`).
5. `xboxallinone.c` is a **RW 3.6 shader-constant fix**, not a custom pipeline: it reimplements the
   skin all-in-one node so that clip-space constants `-38/-37` and the previous vertex shader are
   restored after skinned draws (`Xbox/xboxallinone.c:1-5`, `398-424`). **Caveat:** those are D3D8/Xbox
   register numbers. Our mod runs the PC D3D9 build, where the register file is different; treat this
   file as the documented *contract* (save/restore around skinning), not a drop-in patch.

---

## 1. Architecture map

### 1.1 How a pipeline is declared

Every pipeline follows the same RenderWare 3.6 sequence. The canonical example is the PC building pipe:

```cpp
RxPipeline *pipe = RxPipelineCreate();                                   // PC/CustomBuildingPipeline.cpp:920
RxNodeDefinition *nodedef = RxNodeDefinitionGetD3D9AtomicAllInOne();     // :924  (Xbox: :923)
RxLockedPipe *lpipe = RxPipelineLock(pipe);                              // :929
RxLockedPipeAddFragment(lpipe, NULL, nodedef, NULL);                     // :932
RxLockedPipeUnlock(lpipe);                                               // :934
RxPipelineNode *allinone = RxPipelineFindNodeByName(pipe, nodedef->name, NULL, 0); // :936
RxD3D9AllInOneSetInstanceCallBack(allinone, ...);                        // :942
RxD3D9AllInOneSetReinstanceCallBack(allinone, ...);                      // :943
RxD3D9AllInOneSetRenderCallBack(allinone, ...);                          // :944
pipe->pluginId   = PDSPC_CBuildingEnvMap_AtmID;                          // :946
pipe->pluginData = PDSPC_CBuildingEnvMap_AtmID;                          // :947
```

Key structural facts:

* **One node per pipeline.** For these three pipelines there are no multi-node graphs and no custom
  node definitions. The only node is the driver's `…AtomicAllInOne` node. The pipeline exists solely
  to install callbacks and a plugin ID. (`xboxallinone.c` is the exception: it *does* define a custom
  skin node, outside this set.)
* **Node definition names** are the RW SDK names; `RxPipelineFindNodeByName` is always called with
  `nodedef->name` (`PC/CustomBuildingPipeline.cpp:936`).
* **Pipeline identity** is `pipe->pluginId`/`pluginData` set to a `PDSPC_*_AtmID` value
  (`PC/CustomBuildingPipeline.cpp:946-947`, `PC/CustomBuildingDNPipeline.cpp:1415-1416`,
  `PC/CustomCarEnvMapPipeline.cpp:1631-1632`). Atomic identity is the same value written by
  `SetPipelineID(pAtomic, …)` (`PC/CustomBuildingPipeline.cpp:155`,
  `PC/CustomBuildingDNPipeline.cpp:247`, `PC/CustomCarEnvMapPipeline.cpp:185`).
* **Node private data** is a per-node callback struct; the skin node shows the pattern
  (`Xbox/xboxallinone.c:57-67`) and its node definition struct (`447-476`).

### 1.2 Backend node definitions used

| Backend | Node definition | Where |
|---|---|---|
| PC D3D9 | `RxNodeDefinitionGetD3D9AtomicAllInOne()` | `PC/CustomBuildingPipeline.cpp:924`, `PC/CustomBuildingDNPipeline.cpp:1379`, `PC/CustomCarEnvMapPipeline.cpp:1613` |
| Xbox D3D8 | `RxNodeDefinitionGetXboxAtomicAllInOne()` | `Xbox/CustomBuildingPipeline_Xbox.cpp:923`, `Xbox/CustomBuildingDNPipeline_Xbox.cpp:1444`, `Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1928` |
| Xbox skin (patched) | `RxNodeDefinitionGetXboxSkinAtomicAllInOne()` | `Xbox/xboxallinone.c:447-476` |
| MBR OpenGL | `RxNodeDefinitionGetOpenGLAtomicAllInOne()` | `MBR/CustomBuildingPipeline.cpp:315`, `MBR/CustomBuildingDNPipeline.cpp:348`, `MBR/CustomCarEnvMapPipeline.cpp:1202` |

### 1.3 Per-pipeline create / attach / destroy / render chain

#### CustomBuildingPipeline (non-DN)

| Stage | PC | Xbox | MBR |
|---|---|---|---|
| Create | `CCustomBuildingPipeline::CreatePipe` `PC/CustomBuildingPipeline.cpp:70-84` → `CreateCustomObjPipe` `:917-957` | `Xbox/CustomBuildingPipeline_Xbox.cpp:71-85` → `:918-956` | `MBR/CustomBuildingPipeline.cpp:26-34` → `:313-338` |
| Create callbacks | default instance + `CustomPipeReinstanceCB` + `CustomPipeRenderCB` `:942-944` | **none** — Xbox branch adds the node but installs no callbacks `:922-926`, `:940-944` | render only `:325` (default instance) |
| Per-atomic attach | `CustomPipeAtomicSetup` `:146-157` | `:147-158` | `:77-87` |
| Per-material | `CustomPipeMaterialSetup` `:99-140` (GTA_PC only) | same body gated `#if defined(GTA_PC)` `:100-139` → **no-op on Xbox** | `:43-75` |
| Reinstance | `CustomPipeReinstanceCB` `:719-742` | `:720-743` | n/a (GL instance CB) |
| Render | `CustomPipeRenderCB` `:750-908` | **default RW Xbox callback** (not in file) | `CustomPipeRenderCB` `:89-312` |
| Destroy | `DestroyPipe` `:86-95` → `RxPipelineDestroy` + null | `:87-95` | `:36-41` |

#### CustomBuildingDNPipeline (day/night)

| Stage | PC | Xbox | MBR |
|---|---|---|---|
| Create | `CCustomBuildingDNPipeline::CreatePipe` `PC/CustomBuildingDNPipeline.cpp:142-154` → `CreateCustomObjPipe` `:1358-1489` | `Xbox/CustomBuildingDNPipeline_Xbox.cpp:143-154` → `:1359-1489` | `MBR/CustomBuildingDNPipeline.cpp:34-45` → `:346-372` |
| Create callbacks | default instance + default reinstance when VS11 absent; `CustomPipeRenderCB` always `:1395-1414` | default instance; **custom render wrapper** `VShaderRenderCB` chained to default `:1459-1473` | `RxOpenGLAllInOneSetInstanceCallBack` + render `:362-363` |
| Per-atomic attach | `:232-250` (+ `rpD3D9GEOMETRYUSAGE_DYNAMICPRELIT` `:240-244`) | `:233-250` | `:83-93` |
| Per-material | `:185-226` (GTA_PC only) | `:186-225` → no-op on Xbox | none (MBR DN has no per-material body; only the `521-650` accessors) |
| Instance | `CustomPipeInstanceCB` `:575-950` (only if VS11; otherwise default) | default | `CustomPipeInstanceCB` `:106-241` |
| Render | `CustomPipeRenderCB` `:1174-1347` | `VShaderRenderCB` `:501-525` → `DefaultRenderCallback` | `CustomPipeRenderCB` `:242-345` |
| Lighting | `CustomPipeLightingCB` exists but **not attached** `:990-1045`, `:1413` | n/a (VS lighting) | GL lighting in render CB `:286-295` |
| Destroy | `:160-180` (also deletes DN VS) | `:161-180` | `:47-51` |

#### CustomCarEnvMapPipeline

| Stage | PC | Xbox | MBR |
|---|---|---|---|
| Create | `CCarFXRenderer::Initialise` calls `CCustomCarEnvMapPipeline::CreatePipe` `pipes/CarFXRenderer.cpp:40-46` → `PC/CustomCarEnvMapPipeline.cpp:1648-1684` → `CreateCustomD3D9ObjPipe` `:1604-1640` | `CarFXRenderer.cpp:40-46` → `Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1960-1998` → `CreateCustomD3D8ObjPipe` `:1919-1952` | `CarFXRenderer.cpp:40-46` → `MBR/CustomCarEnvMapPipeline.cpp:1228-1245` → `CreateCustomOpenGLObjPipe` `:1196-1226` |
| Create callbacks | default instance, `CustomPipeReinstanceCB`, `CustomPipeRenderCB` `:1627-1629` | **default instance only**; `RxXboxAllInOneSetRenderCallBack` `:1941`; instance/reinstance lines commented `:1939-1940` | instance + render `:1214-1215` |
| Per-atomic attach | `:171-188` | `:174-191` | `:84-99` |
| Per-material | `:109-163` (not platform-gated) | `:112-166` | `:40-82` |
| Instance | `CustomPipeInstanceCB` `:429-737` (not attached in shipped PC; `:1627`) | default | `CustomPipeInstanceCB` `:101-762` |
| Reinstance | `:746-770` | `:522-545` (`#if 0`, unused) | n/a |
| Render | `CustomPipeRenderCB` `:1231-1594` | `CustomPipeRenderCB(RxXboxResEntryHeader*)` `:1087-1579` | `CustomPipeRenderCB` `:809-1227` |
| Per-frame | `PreRenderUpdate` `:1157-1222` | `:933-997` | `:763-807` |
| Caps check | `CheckDeviceCaps` `:1690-1724` (active) | `:2005-2041` (returns TRUE; D3D9 checks are `#if 0`) | n/a |
| Destroy | `:1731-1748` | `:2048-2065` | `:1248-1277` |

### 1.4 Attach layer (who assigns atomics to these pipes)

* `CCustomBuildingRenderer::Initialise` creates both building pipes `pipes/CustomBuildingRenderer.cpp:62-77`;
  `Shutdown` destroys them `:82-86`; `PluginAttach` registers the DN geometry plugin `:92-99`.
* `CCustomBuildingRenderer::AtomicSetup` chooses DN vs non-DN: on Xbox by pipeline ID
  `PDS_CustomXBoxColourNormal_AtmID`; on PC by `PDSPC_CBuilding*` ID or by the presence of the
  extra-vert-colour plugin + prelit colours (`pipes/CustomBuildingRenderer.cpp:105-139`, `144-190`).
* `CCarFXRenderer::Initialise` creates the vehicle pipe (`pipes/CarFXRenderer.cpp:40-46`);
  `RegisterPlugins` registers the material/atomic/spec plugins (`:72-80`);
  `PreRenderUpdate` updates the specular light (`:87-90`);
  `CustomCarPipeClumpSetup` walks atomics and calls `CustomPipeAtomicSetup` (`:120-124`).
* The per-frame DN balance is computed in `CCustomBuildingRenderer::UpdateDayNightBalanceParam`
  (`pipes/CustomBuildingRenderer.cpp:194-260`): night 1.0 before 06:00, fade to day 06:00-07:00,
  day until 20:00, fade to night 20:00-21:00, then night. `Update` calls it and
  `CCustomBuildingDNPipeline::Update(FALSE)` (`:265-273`).

### 1.5 Plugins (data attached to RpMaterial / RpAtomic / RpGeometry)

| Plugin | Size / payload | Registration | Accessor |
|---|---|---|---|
| EnvMap material | `CustomEnvMapPipeMaterialData*` (12 B payload) | `PC/CustomCarEnvMapPipeline.cpp:2084-2104`, chunk `rwCUSTENVMAPMAT_ID` | `GETENVMAPPIPEDATAPTRPTR` / `GETENVMAPPIPEDATAPTR` `Xbox/CustomCarEnvMapPipeline_Xbox.h:155-156` |
| EnvMap atomic | `CustomEnvMapPipeAtomicData*` | `PC/CustomCarEnvMapPipeline.cpp:2109-2119`, chunk `rwCUSTENVMAPATM_ID` | `GETENVMAPPIPEATMDATAPTR*` `Xbox/CustomCarEnvMapPipeline_Xbox.h:158-159` |
| SpecMap material | `CustomSpecMapPipeMaterialData*` | `PC/CustomCarEnvMapPipeline.cpp:2122-2140`, chunk `rwCUSTSPECMAPMAT_ID` | `GETSPECMAPPIPEDATAPTR*` `Xbox/CustomCarEnvMapPipeline_Xbox.h:161-162` |
| DN geometry | `CDNGeomExtension` = 2 table pointers + `m_fCurrentDNValue` | `PC/CustomBuildingDNPipeline.cpp:1597-1619`, chunk `rwEXTRAVERTCOL_ID` | `EXTRAVERT_*` macros `Xbox/CustomBuildingDNPipeline_Xbox.cpp:61-66` |

Pass-bit storage: the 3 least-significant bits of `RpMaterial::surfaceProps.specular` are reused as a
pass mask. Accessors are inline: `CARFX_GET/SET_PASS_CFG` (`Xbox/CustomPipelinesCommonXbox.h:54-66`) and
`CBMATFX_GET/SET_PASS_CFG` (`:78-90`). The bit values are
`CARFX_NOFX_PASS_BIT=0`, `CARFX_ENVMAP1_PASS_BIT=1`, `CARFX_ENVMAP2_PASS_BIT=2`,
`CARFX_SPECMAP_PASS_BIT=4`, `CARFX_ALLMASK_PASS_BIT=7` (`Xbox/CustomCarEnvMapPipeline_Xbox.h:43-47`).

The vehicle material setup decides env1 vs env2 by **texture name**: first character `'x'` means env2
(`Xbox/CustomCarEnvMapPipeline_Xbox.h:367-371`, used at `Xbox/CustomCarEnvMapPipeline_Xbox.cpp:134-142`).

### 1.6 What actually renders — the render-callback-only model

The default RW all-in-one node performs, before invoking our render callback:

* resource entry creation/reuse, vertex buffer lock/fill, `rxGEOMETRY_*` flag checks
  (`Xbox/xboxallinone.c:95-214` shows the pattern for the skin variant);
* stream source / index buffer / vertex declaration binding (`PC/CustomBuildingPipeline.cpp:794-803`);
* fog, Z test/write, cull, alpha-test state from the *current* RW state block — the custom callbacks
  **do not touch** `D3DRS_ZENABLE`, `D3DRS_ZWRITEENABLE`, `D3DRS_CULLMODE`, `D3DRS_FOGENABLE` or
  `D3DRS_ALPHATESTENABLE`.

This is why the original pipelines can be small: they only add material/effect work and the draw call.
The only exception is the **dead** Xbox dual-pass block, which saves/restores Z-write, fog colour and
blend modes around a second draw (`Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1422-1462`).

**Instance callbacks:** the only *active* custom instance callbacks in the tree are MBR's OpenGL ones
for DN and vehicles (`MBR/CustomBuildingDNPipeline.cpp:362`, `MBR/CustomCarEnvMapPipeline.cpp:1214`);
MBR's non-DN building pipe also uses the default OpenGL instancing (`MBR/CustomBuildingPipeline.cpp:325`
sets only the render callback). Every D3D9/D3D8 pipeline uses the driver default. The DN Xbox
`D3DVSD_*` array is used at **shader creation** (`D3DDevice_CreateVertexShader`,
`Xbox/CustomBuildingDNPipeline_Xbox.cpp:1466-1468`), not to instance a custom buffer; the default
instance callback writes the matching pre-baked `RpXboxVtxFmt`.

**Alpha-test:** no file in `pipes/` sets it (verified by search). The vehicle/building callbacks only
toggle `rwRENDERSTATEVERTEXALPHAENABLE` (`PC/CustomBuildingPipeline.cpp:869-877`,
`PC/CustomCarEnvMapPipeline.cpp:1494-1502`, `Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1220-1230`).
Cutout alpha is therefore inherited from the game's global state and/or the texture's raster flags.

---

## 2. Per-pipeline behaviour

Legend: **PC** = `PC/*.cpp`; **XB** = `Xbox/*_Xbox.cpp`; **MBR** = `MBR/*.cpp`.
"n/a (default)" means the behaviour is owned by the RW all-in-one node, not by these files.
For the render-state rows, layers are our mod's terminology: **L1** = RW state cache
(`RwRenderStateSet`), **L2** = RW D3D9 driver cache (`RwD3D9SetRenderState`), **L3** = raw device
(`d3d9device->SetRenderState` / `D3DDevice_SetRenderState`). Rockstar's code only knows L1/L2, except
where it calls the device directly (Xbox fixed-function path).

### 2.1 CustomBuildingPipeline (non-DN)

| Property | PC | Xbox | MBR |
|---|---|---|---|
| Entry function | `CreatePipe` `PC/CustomBuildingPipeline.cpp:70` | `Xbox/CustomBuildingPipeline_Xbox.cpp:71` | `MBR/CustomBuildingPipeline.cpp:26` |
| Per-atomic callback | `CustomPipeAtomicSetup` `:146` | `:147` | `:77` |
| Per-material callback | `CustomPipeMaterialSetup` `:99` (GTA_PC only) | `:100` (GTA_PC only → no-op) | `:43` |
| Vertex format | RW default D3D9 all-in-one (pos, UV0, normal, prelit as flags). Custom `D3DVERTEXELEMENT9` code exists at `:391-711` but is **not attached** (`:942` uses `RxD3D9AllInOneGetInstanceCallBack`). | RW default Xbox `RpXboxVtxFmt`; no declaration code in file | RW default OpenGL all-in-one (`MBR/CustomBuildingPipeline.cpp:325` sets render only); the manual stride code at `MBR/CustomBuildingDNPipeline.cpp:157-175` is the DN/car pattern, not this file |
| Shader handle source | `instancedData->vertexShader` (set by RW node); pixel shader forced `NULL` in `CustomD3D9Pipeline_AtomicDefaultRender` `PC/CustomPipelinesCommon.cpp:126-129` | `mesh->vertexShader` (RW node) | fixed-function GL, no shader |
| Texture stages | stage0 `MODULATE(TEXTURE,DIFFUSE)` `PC/CustomPipelinesCommon.cpp:106-112`; optional stage1 env `MULTIPLYADD` `PC/CustomBuildingPipeline.cpp:850-854`; stage2 disabled `:822-823` | n/a (default) | stage0 via `_rwOpenGLSetRenderStateNoExtras` + texture addressing/filter `MBR/CustomBuildingPipeline.cpp:157-176` |
| Render states (name = value; layer) | `D3DRS_TEXTUREFACTOR = 0xFF000000` L2 (forceBlack) `:782`; `TEXTUREFACTOR = ARGB(0xFF,shin,shin,shin)` L2 `:850`; `rwRENDERSTATEVERTEXALPHAENABLE` L1→L2 `:869-877`; surface states via `RwD3D9SetSurfaceProperties` L2 `:883-885`; all stage states L2 `:850-860` | none (default node) | `rwRENDERSTATEVERTEXALPHAENABLE` L1; texture address/filter L1 `:126-176` |
| Env-map handling | Pass bit set in material setup `:112-124`; stage1 spherical texgen `D3DTSS_TCI_CAMERASPACENORMAL` `:859`, transform flags `DISABLE` `:860`; env texture `s1` `:838`; `envTexMatrix` computed `:830-834` but **never uploaded** (no `D3DTS_TEXTURE1`) | no env pass in this file | `SetEnvMapTexture(...)` `MBR/CustomBuildingDNPipeline.cpp:301` (DN variant; non-DN MBR building file has no env stage) |
| Lighting path | FFP lighting if `D3DRS_LIGHTING`; `RwD3D9SetSurfaceProperties` `:883-885`; vertex colours when `rxGEOMETRY_PRELIT`; force-black path when unlit and no prelit `:774-787` | n/a (default) | `_rwOpenGLLightsSetMaterialProperties` `MBR/CustomBuildingPipeline.cpp:137-145`; `glColor4fv(opaqueBlack)` when unlit/no prelit `:147-152` |
| Alpha / alpha-test | `_rwD3D9RenderStateVertexAlphaEnable` from `vertexAlpha` or material alpha `:869-877`; no alpha-test | n/a | `rwRENDERSTATEVERTEXALPHAENABLE` + `emu_EnableAlphaModulate(alpha/255)` `:126-143`; skips material alpha 0 `:119-123` |
| Z-write / Z-test | not touched | not touched | not touched |
| Cull | not touched | not touched | not touched |
| Fog | not touched | not touched | not touched |
| Save/restore | stage1 `COLOROP/ALPHAOP -> DISABLE`, `TEXCOORDINDEX -> 1`, `TEXTURETRANSFORMFLAGS -> DISABLE` `:900-903` | n/a | per-mesh `emu_DisableAlphaModulate()` `MBR/CustomBuildingPipeline.cpp:192`; no explicit stage restore |

### 2.2 CustomBuildingDNPipeline (day/night)

| Property | PC | Xbox | MBR |
|---|---|---|---|
| Entry function | `CreatePipe` `PC/CustomBuildingDNPipeline.cpp:142` | `Xbox/CustomBuildingDNPipeline_Xbox.cpp:143` | `MBR/CustomBuildingDNPipeline.cpp:34` |
| Per-atomic callback | `CustomPipeAtomicSetup` `:232` | `:233` | `:83` |
| Per-material callback | `:185` (GTA_PC only) | `:186` (GTA_PC only) | n/a (no per-material function in MBR DN) |
| Vertex format | RW default + `rpD3D9GEOMETRYUSAGE_DYNAMICPRELIT` `:240-244`; alternate declaration adds `COLOR1` only when `m_bDeviceSupportsVS11` `:697-712` | `D3DVSD_STREAM(0)` + `v0 FLOAT3 position`, `v1 NORMPACKED3` (**night colour**), `v2 D3DCOLOR` (day), `v3 FLOAT2` UV `:103-111` | GL mesh: stride includes **2× `RwRGBA`** prelight `MBR/CustomBuildingDNPipeline.cpp:157-175`; `vertexDesc = geomFlags` `:118-119` |
| Shader handle source | `instancedData->vertexShader`; optional PC VS `DNVertexShaderPC` created from `g_vs11_main[]` `PC/CustomBuildingDNPipeline.cpp:1417-1427` (`DNshaderPC.h:9-18`) — **disabled by default** (`:48-51` `GTA_PC_USE_DN_VS` commented out) | `DNVertexShader` created with `D3DDevice_CreateVertexShader(DNVertexShaderDeclaration, dwDNshaderVertexShader, …)` `:1466-1468` from `DNshader.h:1-13`; assigned to every mesh `:516-521` | none (GL fixed function + `emu_SetSecondVertexColor`) |
| Texture stages | stage0 modulate; env stage1 `MULTIPLYADD` with `ARG2 = D3DTA_DIFFUSE` (not TFACTOR) `PC/CustomBuildingDNPipeline.cpp:1289-1293`; stage2 disabled `:1261-1262` | n/a (default) | GL texture bind + addressing `MBR/CustomBuildingDNPipeline.cpp:312-331` |
| Render states (name = value; layer) | `D3DRS_TEXTUREFACTOR = 0xFF000000` L2 (forceBlack) `:1206`; `rwRENDERSTATEVERTEXALPHAENABLE` L1→L2 `:1308-1316`; surface states L2 `:1322-1324`; all stage states L2 `:1289-1299` | n/a (VS) | `rwRENDERSTATEVERTEXALPHAENABLE` L1; GL texture states `:266-331` |
| Lighting path | FFP; `RwD3D9SetSurfaceProperties` `:1322-1324`; DN blend CPU-written into `preLitLum` (`:1512-1575`) | **vertex shader** interpolates day/night: `oD0.xyz = night*c4.x + day*c5.x` (`DNshader.vsh:21-28`); constants c4=DN, c5=1-DN (`Xbox/CustomBuildingDNPipeline_Xbox.cpp:466-478`) | `emu_SetSecondVertexColor(GL_TRUE, m_fDNBalanceParam)` `MBR/CustomBuildingDNPipeline.cpp:243`; GL lighting `:286-295` |
| Env-map handling | same as non-DN but `ARG2 = DIFFUSE` `:1265-1302` | no env pass in this file | `SetEnvMapTexture` `:296-304`, `ResetEnvMap` `:339` |
| Alpha / alpha-test | vertex-alpha enable `:1308-1316`; no alpha-test | inherited | `rwRENDERSTATEVERTEXALPHAENABLE` + `emu_EnableAlphaModulate` `:266-283`; skip alpha 0 `:259-264` |
| Z / cull / fog | not touched | not touched | not touched |
| Save/restore | stage1 disable + texcoord 1 + transform disable `:1338-1342` | n/a | `ResetEnvMap`, second-colour off `:339-343` |

#### DN scheduling and CPU interpolation (PC shipped path)

* `Update(bCameraChange)` advances `m_AtmDNWorkingIndex = (m_AtmDNWorkingIndex+1) % 16`
  (`PC/CustomBuildingDNPipeline.cpp:1497-1504`, `DN_NUM_FRAMES_FOR_PROCESS=16` at `:68`).
* `UpdateAtomicDN` skips atomics not using this pipe (`:256-268`, `:296-299`), computes
  `abs(currentDN - m_fDNBalanceParam)`, and updates when `> 0.01`, forced, camera changed, or `> 0.3`
  (`:275-278`, `:303-316`).
* `UpdateAtomicPrelitLum` clamps `interpVal` 0..1, locks `rpGEOMETRYLOCKPRELIGHT`, and writes
  `preLitLum = night*interp + day*(1-interp)` (`:1552-1573`). It stores the applied value in
  `m_fCurrentDNValue` (`:1528-1529`).
* `UpdateClumpDN` walks atomics (`:324-346`).

#### DN plugin layout

`CDNGeomExtension { RwRGBA *m_pNightColTable; RwRGBA *m_pDayColTable; float m_fCurrentDNValue; }`
(`Xbox/CustomBuildingDNPipeline_Xbox.h:33-39`). Stream read allocates both tables, reads only the
night table from disk, and copies the day table from the geometry's existing `preLitLum`
(`PC/CustomBuildingDNPipeline.cpp:1727-1749`). `GetExtraVertColourPtr` returns the **night** table
(`:1771-1775`); `GetExtraVertDNValue` returns `&m_fCurrentDNValue` (`:1780-1784`).

> **Tree bug (do not copy):** `pluginExtraVertColourStreamGetSizeCB` returns
> `4 + numVerts*4*2` (`:1697-1707`) while the write callback writes `4 + numVerts*4` on non-Xbox
> (`:1671-1689`); on Xbox the write body is compiled out entirely (`:1673`). The stream size and the
> written payload disagree by `4*numVerts`.

### 2.3 CustomCarEnvMapPipeline (vehicles)

| Property | PC | Xbox | MBR |
|---|---|---|---|
| Entry function | `CreatePipe` `PC/CustomCarEnvMapPipeline.cpp:1648` | `Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1960` | `MBR/CustomCarEnvMapPipeline.cpp:1228` |
| Per-atomic callback | `CustomPipeAtomicSetup` `:171` | `:174` | `:84` |
| Per-material callback | `CustomPipeMaterialSetup` `:109` | `:112` | `:40` |
| Vertex format | RW default D3D9 (UV0 + UV1 when `rpGEOMETRYTEXTURED2`); custom instance CB exists `:429-737` but is **not attached** (`:1627`) | RW default Xbox `RpXboxVtxFmt`; `RpXboxGeometryGetVtxFmt` is read into `format` `:1107` but **unused** | `RxOpenGLMeshInstanceData`; `vertexDesc = geomFlags`; custom stride `:101-762` |
| Shader handle source | `instancedData->vertexShader`; PS `NULL` `PC/CustomPipelinesCommon.cpp:126-129` | `mesh->vertexShader` `:1544` (`RwXboxSetCurrentVertexShader`); PS `0` `:1541` | none (GL FFP) |
| Texture stages | stage0 modulate `:1588-1593`; stage1 env1 `MULTIPLYADD(TEXTURE,TFACTOR)` `:1415-1419`; stage1 env2 `BLENDFACTORALPHA(TEXTURE,CURRENT)` `:1468-1471`; stage2 disabled `:1380-1381` | live: stage0 `MODULATE(DIFFUSE,TEXTURE)` `:1531-1537`; stage1 disabled `:1565-1572`; env passes are in `#if 0` `:1207-1474` | GL texture bind + `SetEnvMapTexture` (`:893`), `ResetEnvMap` (`:933`) |
| Render states (name = value; layer) | `D3DRS_SPECULARENABLE=TRUE` L2 `:1364`; `LOCALVIEWER=FALSE` L2 `:1365`; `SPECULARMATERIALSOURCE=MATERIAL` L2 `:1366`; `TEXTUREFACTOR=ARGB(...)` L2 `:1415`, `:1468`; `rwRENDERSTATEVERTEXALPHAENABLE` L1→L2 `:1494-1502`; `rwRENDERSTATETEXTUREADDRESS=WRAP` L1 `:1386`, `:1434`; surface states L2 `:1554-1556` | `COLORVERTEX`/`EMISSIVEMATERIALSOURCE`/`DIFFUSEMATERIALSOURCE` via `_rwXbRenderStateForce` (cached L2) `:1157-1175`; `AMBIENTMATERIALSOURCE` cached L2 `:1232-1234`; `SPECULARENABLE`/`LOCALVIEWER`/`SPECULARMATERIALSOURCE` via `D3DDevice_SetRenderState` (**L3 direct**) `:1494-1496`; `TEXTUREFACTOR` cached L2 only in the dead env block `:1340`; `rwRENDERSTATEVERTEXALPHAENABLE` L1 `:1230` | `rwRENDERSTATEVERTEXALPHAENABLE` L1; GL texture states `:856-924` |
| Env-map 1 | `_rpMatFXSyncEnvMap1Data` `:790-829` → `D3DTS_TEXTURE1` matrix `:1399`; `COUNT3` + `PROJECTED` `:1425`; shininess `254*shininess*mult` `:1405-1407` | dead block only (`:1288-1291`, `1300-1319`) | `_rpMatFXSyncEnvMap1Data` `:509-547`, `SetEnvMapTexture` `:893` |
| Env-map 2 | `_rpMatFXSyncEnvMap2Data` `:837-1076` → `D3DTS_TEXTURE1` `:1456`; `COUNT2` `:1484`; shininess fixed 24 `:1463`; U accumulates, V clamped 0..0.1, flips when upside down `:1047-1066` | dead block only | both env1/env2 call `_rpMatFXSyncEnvMap1Data` (`:889`, `:1060`; env2 sync at `:1098`) |
| Specular | D3D light index **1** `:42`; `D3DRS_SPECULARENABLE`, `LOCALVIEWER=FALSE`, `SPECULARMATERIALSOURCE=MATERIAL` `:1364-1366`; material specular `2*specularity*mult` clamped 1, power `100*specularity` `:1361-1363`; disabled after mesh `:1567-1573` | same formulas with `D3DLIGHT8`/`D3DDevice_*` `:1478-1505`, disable `:1554-1558` | GL lighting; spec light code is a **D3D9 leftover** (`RwD3D9SetLight` at `:1027-1028`) |
| Lighting path | FFP + specular light; `CustomD3D9Pipeline_RwSetSurfaceProperties` `:1554-1556`; vehicle remap-colour fix zeroes `MAT1..MAT4`/`LIGHT_*` `:1513-1534`; blown-up atomic detection `:1263-1275` | FFP material setup `Custom_rxXbDefaultRenderFFPMeshSetUp` `:1011-1080`; `D3DMATERIAL8` `:1014-1021`; material-source states `:1157-1175`; blown-up detection `:1104` | `_rwOpenGLLightsSetMaterialProperties` `:866-878`; `emu_EnableAlphaModulate` |
| Alpha / alpha-test | vertex alpha from `vertexAlpha` or material alpha `:1494-1502`; no alpha-test | `RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, alpha)` `:1230`; cached material-source states `:1232-1234` | `rwRENDERSTATEVERTEXALPHAENABLE` + `emu_EnableAlphaModulate` `:856-868`; skips alpha 0 `:849-855` |
| Z / cull / fog | not touched in live path | not touched in live path; dead dual-pass saves/restores `ZWRITEENABLE`, `FOGCOLOR`, `SRCBLEND/DESTBLEND` `:1422-1462` | not touched |
| Save/restore | `CustomD3D9Pipeline_RwSetSurfacePropertiesCacheResync()` `:1580-1581`; stage1 disable/texcoord 1/transform disable `:1584-1588` | stage0 transform/texcoord reset, stage1 disable, texture1 unbind `:1565-1572` | `ResetEnvMap` after each mesh `:933` |

#### Vehicle material → pass-bit mapping (both D3D backends)

```
effects == rpMATFXEFFECTENVMAP            -> SetFxEnvTexture(pMaterial, NULL)      // :124-128 (XB) / :121-125 (PC)
shininess != 0 && envTexture != NULL      -> ENVMAP1 or ENVMAP2 (first char 'x')  // :130-143 (XB)
specularity != 0 && specTexture != NULL   -> SPECMAP                              // :146-150 (XB)
passMask stored in 3 LSBs of surfaceProps.specular                               // :157-162 (XB)
```
No-extra-pass gating uses `CVisibilityPlugins::GetAtomicId` bits `1<<13|1<<14` (`:1104`; constants at
`modelinfo/VehicleModelInfo.h:220-221`).

#### Env-map math (PC authoritative)

`CALC_ENVMAP_OFFSETX/Y` macros `PC/CustomCarEnvMapPipeline.cpp:780-782`. Env1 uses the **clump** LTM
(chassis) if present, else the atomic LTM, and returns `(-fracX, -fracY)` (`:790-829`). Env2 accumulates
a U offset only when the render frame changes, using sector parity and forward/backward dot product
(`:930-1040`), then V from the matrix `at` vector clamped `[0, 0.1]`, flipped when `up.z < 0`
(`:1047-1066`), and outputs `(-finalU, finalV)` (`:1072-1073`).

---

## 3. Xbox vs PC vs MBR deltas

### 3.1 API / type matrix

| Concern | PC | Xbox | MBR |
|---|---|---|---|
| Device API | D3D9 (`d3d9.h`) | D3D8 (`d3d8.h`, `xtl.h`) | OpenGL via RW MBR (`stubbedgl.h`, `emu_*`) |
| Light type | `D3DLIGHT9` `PC/CustomCarEnvMapPipeline.cpp:1166` | `D3DLIGHT8` `Xbox/CustomCarEnvMapPipeline_Xbox.cpp:941` | n/a |
| Material type | `D3DMATERIAL9` `PC/CustomPipelinesCommon.cpp:158` | `D3DMATERIAL8` `Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1014` | n/a |
| Vertex declaration | `D3DVERTEXELEMENT9` + `RwD3D9CreateVertexDeclaration` `PC/CustomCarEnvMapPipeline.cpp:429-737`, call at `:562` | `D3DVSD_*` + `D3DDevice_CreateVertexShader` `Xbox/CustomBuildingDNPipeline_Xbox.cpp:103-111`, `:1466-1468`; otherwise pre-baked `RpXboxVtxFmt` | `RxOpenGLMeshInstanceData`, `vertexDesc = geomFlags` `MBR/CustomBuildingDNPipeline.cpp:118-119` |
| Draw | `RwD3D9DrawIndexedPrimitive` `PC/CustomPipelinesCommon.cpp:135` | `RwXboxDrawIndexedVertices` `Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1547` | `instanceData->DrawStored()` `MBR/CustomBuildingDNPipeline.cpp:333` |
| Set VS | `RwD3D9SetVertexShader(instancedData->vertexShader)` `PC/CustomPipelinesCommon.cpp:129` | `RwXboxSetCurrentVertexShader(mesh->vertexShader)` `Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1544` | none |
| Set PS | `RwD3D9SetPixelShader(NULL)` `PC/CustomPipelinesCommon.cpp:126` | `RwXboxSetCurrentPixelShader(0)` `Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1541` | none |
| Stage state | `RwD3D9SetTextureStageState` `PC/CustomBuildingPipeline.cpp:850` | `RwXboxSetCachedTextureStageState` `Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1531` | `_rwOpenGLSetRenderState` `MBR/CustomBuildingPipeline.cpp:170` |
| Render state | `RwD3D9SetRenderState` / `_rwD3D9RenderStateVertexAlphaEnable` `PC/CustomBuildingPipeline.cpp:872` | `_rwXbRenderStateForce` / `RwXboxSetCachedRenderState` `Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1157`, `1232` | `_rwOpenGLSetRenderState` `MBR/CustomBuildingPipeline.cpp:142` |
| Texture bind | `RwD3D9SetTexture` `PC/CustomBuildingPipeline.cpp:838` | `RwXboxRenderStateSetTexture` `Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1529` | `_rwOpenGLSetRenderStateNoExtras(…TEXTURERASTER)` `MBR/CustomBuildingPipeline.cpp:164` |

**RW driver API naming:** the PC driver is `RwD3D9*`; the Xbox driver is `RwXbox*` (not `RwD3D8*`), e.g.
`RwXboxSetCachedRenderState`, `RwXboxSetCachedTextureStageState`, `RwXboxDrawIndexedVertices`
(`Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1232`, `1531`, `1547`). The `D3DDevice_*` calls in the Xbox
files are the XDK's thin device wrappers (`D3DDevice_SetRenderState`, `D3DDevice_SetLight`, …), i.e.
layer-3 writes outside RW's cache (`:1494-1496`, `:1487-1488`).

### 3.2 Shader model and handles

* DN Xbox shader source is `xvs.1.1` (`PC/DNshader.vsh:11`), compiled to the DWORD array in
  `PC/DNshader.h:1-13`. DN PC shader is `vs_1_1` (`PC/DNshaderPC.vsh:21`), compiled to
  `PC/DNshaderPC.h:9-18`.
* General Xbox skin shader limits: 16 dir lights, 16 point lights, 192 constant slots
  (`PC/shaderdesc.h:4-6`); shader descriptor is a packed 32-bit key (weights, dir/point counts, matfx
  mode, BNT, prelit) (`PC/shaderdesc.h:20-33`).
* Constant register contract for the skin/all-in-one node (`PC/skindefs.h`):
  `SHADERCONSTS_PER_BONE=3` (`:53`), `PER_ATOMIC_OFFSET=-96` (`:57`), screen-space `-96` (`:60-61`),
  transform `-95..-92` (`:64-65`), ambient `-91` (`:69`), dir light `-90` (`:73`), point light follows
  (`:75`), material colour `+95` (`:86`), env `+93` (`:90`), bump world `+90` (`:94`), texture
  transforms `+79` (`:105`).
* DN Xbox constants: `c0-c3` = proj×view×world, `c4` = DN balance, `c5` = 1-DN, `c6..` debug only
  (`Xbox/CustomBuildingDNPipeline_Xbox.cpp:455-498`). DN PC packs `c4 = {1-DN, DN}` instead
  (`:1150-1164`; shader uses `c4.x`/`c4.y` at `PC/DNshaderPC.vsh:53-54`).
* Shader lifetime: DN Xbox creates one shader in `CreateCustomObjPipe` (`:1466-1468`) and deletes it in
  `DestroyPipe` (`:165-167`). PC optionally creates `DNVertexShaderPC` (`:1421`) and deletes it
  (`:169-175`).

### 3.3 Vertex input

* Building/car PC: **no custom declaration is active**. The driver's default instance callback builds
  the declaration from geometry flags; the code in the file is an alternate that is not attached
  (`PC/CustomBuildingPipeline.cpp:942`, `PC/CustomCarEnvMapPipeline.cpp:1627`).
* DN PC: same default + `rpD3D9GEOMETRYUSAGE_DYNAMICPRELIT` (`PC/CustomBuildingDNPipeline.cpp:240-244`);
  if the optional VS11 path is enabled, COLOR1 is added (`:697-712`).
* DN Xbox: explicit D3D8 declaration; **night colour lives in the normal stream**
  (`D3DVSDT_NORMPACKED3`) and day colour in the diffuse stream (`:103-111`). The declaration is bound
  at `D3DDevice_CreateVertexShader` (`:1466-1468`); instancing is still the RW default, so the
  geometry must carry a matching pre-baked `RpXboxVtxFmt`.
* Car Xbox: default RW Xbox format; `RpXboxGeometryGetVtxFmt` result is unused (`:1107`).
* MBR: manual stride assembly including 2× `RwRGBA` for DN (`MBR/CustomBuildingDNPipeline.cpp:157-175`),
  and `vertexDesc` packed from geom flags (`:118-119`).

### 3.4 Xbox-only fixed-function leftovers

The Xbox vehicle callback is a **fixed-function renderer** with a thin shader binding:

* `Custom_rxXbDefaultRenderFFPMeshSetUp` builds a `D3DMATERIAL8` from material colour/surface props and
  calls `D3DDevice_SetMaterial` (`Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1011-1080`).
* Material-source render states are forced with `_rwXbRenderStateForce`: `D3DRS_COLORVERTEX`,
  `D3DRS_EMISSIVEMATERIALSOURCE`, `D3DRS_DIFFUSEMATERIALSOURCE` (`:1157-1175`), plus cached
  `D3DRS_AMBIENTMATERIALSOURCE` (`:1232-1234`).
* Specular uses `D3DDevice_SetLight`/`D3DDevice_LightEnable` at index 1 (`:1487-1488`), not the shader.
* `RwXboxSetCachedTextureStageState` is used for all stage state, i.e. the Xbox driver keeps a shadow
  cache; the PC driver exposes the plain D3D9 calls.
* `xboxallinone.c` shows the low-level device API used in this era: `D3DDevice_SetStreamSource`
  (`:319-326`), `D3DDevice_SetVertexShaderConstantFast` (`:314-316`, `:354-356`),
  `D3DDevice_SetVertexShader` (`:421`), `D3DDevice_GetViewportOffsetAndScale` (`:266-268`),
  `_rwXBLightsEnable` (`:226`).

### 3.5 `xboxallinone.c` — what it replaces

* Header comment: it is a **fix for a RW 3.6 vertex-shader bug**, not needed in RW 3.7. The skin
  all-in-one node is overloaded so custom shaders are clipped correctly among skinned peds, because the
  skin shaders did not restore some constants (`Xbox/xboxallinone.c:1-5`).
* It defines `XBoxSkinAtomicAllInOneNode` (`:70-428`) and
  `RxNodeDefinitionGetXboxSkinAtomicAllInOne` (`:447-476`), replacing the RW SDK's skin node.
* Node private data: instance/reinstance/enumerate-lights/render callbacks (`:57-67`).
* It calls the RW internals `_rxXBInstance` (`:15-21`, used `:189-192`),
  `_rpSkinXboxTo192Constants`/`To96Constants` (`:23-30`, used `:260-261`, `:418`),
  `_rpSkinXboxPurgeShaderCache` (`:29-30`, used `:260`),
  `_rpSkinXboxComputeProjViewWorld` (`:32-40`, used `:272-277`),
  `_rpSkinXboxMatrixUpdate` (`:45-51`, used `:298`), and
  `_rpSkinXboxPerAtomicConstants[1024]` (`:55`).
* The actual fix saves the current shader, uploads per-atomic constants, and after all meshes restores
  the viewport offset/scale into **`-38` and `-37`** and the old shader
  (`:398-424`). Without it, subsequent custom shaders see stale clip-space constants.

### 3.6 MBR deltas (skim only, as requested)

* MBR is the **OpenGL** variant (`MBR/CustomEnvMapPipelineCSL.h:9-10` includes
  `../plugin/matfx/mbr/effectPipesMBR.h`); all three pipes use
  `RxNodeDefinitionGetOpenGLAtomicAllInOne` (`MBR/CustomBuildingPipeline.cpp:315`,
  `MBR/CustomBuildingDNPipeline.cpp:348`, `MBR/CustomCarEnvMapPipeline.cpp:1202`). DN and car install
  both instance and render callbacks; non-DN building installs render only.
* No shaders, no vertex declarations: GL mesh data with `DrawStored()` (`MBR/CustomBuildingDNPipeline.cpp:333`).
* DN uses `emu_SetSecondVertexColor` (`:243`, `:343`) instead of a prelit rewrite or a VS.
* Env map is a GL extension: `SetEnvMapTexture(...)` / `ResetEnvMap()` (`MBR/CustomBuildingDNPipeline.cpp:301`,
  `:339`; declared in `MBR/CustomEnvMapPipelineCSL.h:11-12`).
* Tree leftovers: `RwD3D9SetLight` / `RwD3D9EnableLight` in the MBR car callback
  (`MBR/CustomCarEnvMapPipeline.cpp:1027-1028`, `:1179`), and the MBR building header reuses the PC
  guard `__PCCUSTOMBUILDINGPIPELINE_H__` (`MBR/CustomBuildingPipeline.h:5-6`) — a guard collision with
  the PC header (`PC/CustomBuildingPipeline.h:10-11`).
* MBR `CustomPipelinesCommon.cpp` is the **PC D3D9** file (it compiles the D3D9 helpers under
  `GTA_PC`), i.e. the MBR directory is a partial copy, not a clean GL-only tree.

### 3.7 Dead code and contradictions in the tree (important for porting)

| Finding | Evidence |
|---|---|
| `CustomPipelinesCommonXbox.cpp` is entirely `#if 0` after the D3D8 state-stub block; no live D3D9 helpers | `Xbox/CustomPipelinesCommonXbox.cpp:51` `#if 0`, `:306` `#endif`; live code is only `:23-46` |
| The Xbox car instance/reinstance callbacks are `#if 0` and not attached | `Xbox/CustomCarEnvMapPipeline_Xbox.cpp:204-545` (`#endif` at `:546`); create only sets the render CB at `:1941` |
| The PC-style render callback in the Xbox car file is `#if 0` | `Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1583` … `:1911` |
| The env-map/dual-pass block in the Xbox car callback is `#if 0` | `Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1207` … `:1474` |
| Old instance callbacks in the PC building and car files are `#if 0` | `PC/CustomBuildingPipeline.cpp:164-384`, `PC/CustomCarEnvMapPipeline.cpp:200-421`; the DN file has no old instance CB (its only `#if 0` is an alternate env blend at `PC/CustomBuildingDNPipeline.cpp:1282-1288`) |
| PC car custom instance callback is not attached | `PC/CustomCarEnvMapPipeline.cpp:1627` |
| PC building material setup + old instance CB contain a stride bug (`sizeof(RwV3d)` for an RGBA element) | `PC/CustomBuildingPipeline.cpp:509` |
| PC DN VS path is compiled out by default | `PC/CustomBuildingDNPipeline.cpp:48-51` |
| `CheckDeviceCaps` on Xbox returns TRUE immediately; the D3D9 checks are dead | `Xbox/CustomCarEnvMapPipeline_Xbox.cpp:2005-2041` |
| `CustomPipeLightingCB` is defined but never attached | `PC/CustomBuildingDNPipeline.cpp:990-1045`, commented at `:1413` |
| DN stream size and written payload disagree; Xbox writes nothing | `PC/CustomBuildingDNPipeline.cpp:1671-1707` |
| MBR car callback calls D3D9 light functions | `MBR/CustomCarEnvMapPipeline.cpp:1027-1028` |

---

## 4. Naming and structure conventions to adopt

This is a concrete convention sheet extracted from the original files. It is deliberately mechanical so
it can be applied to `src/render/ps2_mode.cpp`, `xbox_mode.cpp`, `neoCarpipe.cpp`, `buildingPipe.cpp`,
`vehiclePipe.cpp` without redesigning them.

### 4.1 File naming

| Pattern | Examples | Rule |
|---|---|---|
| `Custom<Domain>Pipeline[_<Platform>].cpp/.h` | `CustomBuildingPipeline.cpp`, `CustomBuildingDNPipeline_Xbox.cpp`, `CustomCarEnvMapPipeline_Xbox.cpp` | Platform suffix only for the Xbox fork; PC and MBR use the bare name |
| `Custom<Domain>Renderer.cpp/.h` | `CustomBuildingRenderer.cpp`, `CarFXRenderer.cpp` | The attach/lifecycle layer is a separate `*Renderer` class |
| `CustomPipelinesCommon<Platform>.cpp/.h` | `PC/CustomPipelinesCommon.h`, `Xbox/CustomPipelinesCommonXbox.h` | Shared helpers get a platform suffix. **Tree quirk:** the PC file is named `CustomPipelinesCommon.h` but its guard is `__CUSTOMPIPELINESCOMMONPC_H__` and the Xbox fork includes it as `CustomPipelinesCommonPC.h` (`Xbox/CustomBuildingPipeline_Xbox.cpp:32`); the `…PC.h` filename does not exist under `PC/`. Use one consistent name in our tree. |
| Shader blobs `<ShaderName>.h` + `<ShaderName>.vsh` | `DNshader.h` + `DNshader.vsh`, `DNshaderPC.h` + `DNshaderPC.vsh` | Header is the compiled DWORD array; `.vsh` is the readable source |

### 4.2 Header guards

Pattern: `__<PLATFORM><FILENAME>_H__`, all caps, double leading/trailing underscore.

* `__XBOXCUSTOMBUILDINGPIPELINE_H__` (`Xbox/CustomBuildingPipeline_Xbox.h:11-12`)
* `__XBOXCUSTOMBUILDINGDNPIPELINE_H__` (`Xbox/CustomBuildingDNPipeline_Xbox.h:10-11`)
* `__XBOXCUSTOMCARENVMAPPIPELINE_H__` (`Xbox/CustomCarEnvMapPipeline_Xbox.h:11-12`)
* `__PCCUSTOMBUILDINGPIPELINE_H__` (`PC/CustomBuildingPipeline.h:10-11`)
* `__PCCUSTOMBUILDINGDNPIPELINE_H__` (`PC/CustomBuildingDNPipeline.h:10-11`)
* `__CUSTOMPIPELINESCOMMONPC_H__` (`PC/CustomPipelinesCommon.h:8-9`)
* `__CUSTOMPIPELINESCOMMONXBOX_H__` (`Xbox/CustomPipelinesCommonXbox.h:8-9`)
* `__CUSTOMENVMAPPIPELINECSL_H__` (`PC/CustomEnvMapPipelineCSL.h:10-11`)

**Avoid the tree's bug:** MBR reuses `__PCCUSTOMBUILDINGPIPELINE_H__`
(`MBR/CustomBuildingPipeline.h:5`) and uses `__CUSTOMBUILDINGDNPIPELINE_H__`
(`MBR/CustomBuildingDNPipeline.h:5`). In our code use a unique platform token in every guard.

Cross-platform build guards in the body:

```cpp
#ifdef GTA_PC
    #error "This sourcecode is NOT a part of GTA_PC build."
#endif
```
(`Xbox/CustomBuildingPipeline_Xbox.h:14-16`; mirrored for Xbox in PC headers `PC/CustomBuildingPipeline.h:13-15`).

### 4.3 Classes and methods

| Kind | Convention | Examples |
|---|---|---|
| Pipeline class | `CCustom<Domain>Pipeline` | `CCustomBuildingPipeline`, `CCustomBuildingDNPipeline`, `CCustomCarEnvMapPipeline` |
| Renderer/attach class | `CCustom<Domain>Renderer` or `C<Domain>FXRenderer` | `CCustomBuildingRenderer`, `CCarFXRenderer` |
| Lifecycle | `CreatePipe()`, `DestroyPipe()` | `PC/CustomBuildingPipeline.cpp:70`, `:86` |
| Pipe factory | `CreateCustomObjPipe()`, or backend-specific `CreateCustomD3D9ObjPipe()` / `CreateCustomD3D8ObjPipe()` / `CreateCustomOpenGLObjPipe()` | `PC/CustomBuildingPipeline.cpp:917`; `PC/CustomCarEnvMapPipeline.cpp:1604`; `Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1919`; `MBR/CustomCarEnvMapPipeline.cpp:1196` |
| Per-atomic | `CustomPipeAtomicSetup(RpAtomic*)` | `PC/CustomBuildingPipeline.cpp:146` |
| Per-material | `CustomPipeMaterialSetup(RpMaterial*, void*)` | `:99` |
| Driver callbacks | `CustomPipeInstanceCB`, `CustomPipeReinstanceCB`, `CustomPipeRenderCB`, `CustomPipeLightingCB` | `:391`, `:719`, `:750`; `PC/CustomBuildingDNPipeline.cpp:990` |
| DN control | `Update`, `UpdateAtomicDN`, `UpdateClumpDN`, `UpdateAtomicPrelitLum`, `UsesThisPipeline`, `ExtraVertColourPluginAttach`, `SetDNBalanceParam`, `GetDNBalanceParam` | `PC/CustomBuildingDNPipeline.cpp:1497-1619`; inline balance at `Xbox/CustomBuildingDNPipeline_Xbox.h:142-154` |
| Vehicle control | `PreRenderUpdate`, `CheckDeviceCaps`, `RegisterPlugin` | `PC/CustomCarEnvMapPipeline.cpp:1157`, `:1690`, `:2079` |
| Env accessors | `SetFxEnvTexture`, `GetFxEnvTexture`, `SetFxEnvShininess`, `GetFxEnvShininess`, `SetFxEnvScale`, `GetFxEnvScaleX/Y`, `SetFxEnvTransScl`, `GetFxEnvTransSclX/Y` | `PC/CustomBuildingPipeline.cpp:990-1178`; `Xbox/CustomCarEnvMapPipeline_Xbox.h:240-260` |
| Spec accessors | `SetFxSpecTexture`, `GetFxSpecTexture`, `SetFxSpecSpecularity`, `GetFxSpecSpecularity` | `Xbox/CustomCarEnvMapPipeline_Xbox.h:270-278` |
| World-space helpers | `_rpMatFXSyncEnvMap1Data`, `_rpMatFXSyncEnvMap2Data` (leading `_rp` = RenderWare-internal-style free function) | `PC/CustomCarEnvMapPipeline.cpp:790`, `:837` |

### 4.4 Structs

| Struct | Purpose | Definition |
|---|---|---|
| `CustomEnvMapPipeMaterialDiskData` | On-disk material payload (5 floats + texture pointer) | `Xbox/CustomCarEnvMapPipeline_Xbox.h:57-63` |
| `CustomEnvMapPipeMaterialData` | In-memory material payload; compressed 8-bit scales + shininess + frame + texture; accessor methods `Getf...`/`Setf...` | `:69-97` |
| `CustomEnvMapPipeAtomicData` | Per-atomic env2 scroll state: `fOffsetU`, `fPrevMapPosX`, `fPrevMapPosY` | `:104-109` |
| `CustomSpecMapPipeMaterialDiskData` | On-disk spec payload: `fSpecularity`, `cName[24]` | `:116-120` |
| `CustomSpecMapPipeMaterialData` | In-memory spec payload: `fSpecularity`, `pSpecularTexture` | `:126-130` |
| `CDNGeomExtension` | DN geometry plugin: two colour tables + current DN value | `Xbox/CustomBuildingDNPipeline_Xbox.h:33-39` |

Field naming: **Hungarian-ish prefixes**, lower-case type tag + PascalCase name:
`f` float, `p` pointer, `cf` compressed float, `n`/`ui` integer, `m_` member, `ms_` static member,
`g_`/`m_g` global. Examples: `fEnvScaleX`, `pEnvTexture`, `cfShininess`, `nCurrentRenderFrame`,
`m_fDNBalanceParam`, `ms_envMapPluginOffset`, `m_gSpecularLight`, `m_gEnvMapPipeMatDataPool`.

### 4.5 Enums, IDs, macros

| Kind | Convention | Examples |
|---|---|---|
| Pipe ID | `PDSPC_<Pipeline>_AtmID` built with `rpPDSPC_MAKEPIPEID(rwVENDORID_ROCKSTAR, rwCUSTPDSPC_..._PIPE_ID)` | `Xbox/CustomBuildingPipeline_Xbox.h:23`; `Xbox/CustomBuildingDNPipeline_Xbox.h:22`; `Xbox/CustomCarEnvMapPipeline_Xbox.h:27` |
| Plugin chunk ID | `MAKECHUNKID(rwVENDORID_ROCKSTAR, rwCUST..._ID)` | `PC/CustomCarEnvMapPipeline.cpp:2085`, `:2110`, `:2123`; `PC/CustomBuildingDNPipeline.cpp:1602` |
| Pass bits | `CARFX_<EFFECT>_PASS_BIT`, `CARFX_ALLMASK_PASS_BIT`; `CBMATFX_*` for buildings | `Xbox/CustomCarEnvMapPipeline_Xbox.h:43-47`; `Xbox/CustomPipelinesCommonXbox.h:54-90` |
| Accessors | `GET<THING>PTR`, `GET<THING>PTRPTR` | `GETENVMAPPIPEDATAPTRPTR`, `GETENVMAPPIPEDATAPTR`, `GETENVMAPPIPEATMDATAPTR`, `GETSPECMAPPIPEDATAPTR` (`Xbox/CustomCarEnvMapPipeline_Xbox.h:155-162`) |
| Geometry plugin fields | `<THING>FROMGEOM(GEOM)`, `<THING>_CURRENTDN`, `<THING>_DAYTABLE`, `<THING>_NIGHTTABLE` | `Xbox/CustomBuildingDNPipeline_Xbox.cpp:61-66` |
| Constants | `UPPER_SNAKE`, `(value)` parenthesised | `VERTELEM_NUMDECLARATIONS (4+1)`; `DN_NUM_FRAMES_FOR_PROCESS (16)`; `BIG_UPDATE_THRESHOLD (0.3f)`; `CARFX_ENVMAPDATAPOOL_SIZE (4096)` (`Xbox/CustomCarEnvMapPipeline_Xbox.h:138-148`) |
| Math helpers | `FLOATASINT(f)`, `COLORSCALAR` | `Xbox/CustomPipelinesCommonXbox.h:41-42` |

### 4.6 Comment style

* `//` only; no `/* */` except copied RW SDK headers.
* File banner: `//`, title line, `//`, then a **date-author changelog**:
  ```cpp
  //	15/10/2004	- JonhW:	- initial port to PC;
  //	04/02/2005	- Andrzej:	- initial;
  //	28/02/2005	- Andrzej:	- GTA_XBOX has its own sourcecode for pipeline;
  ```
  (`Xbox/CustomBuildingPipeline_Xbox.cpp:1-7`)
* Section separators: `//\n//\n//\n//` and long `///////...` rules
  (`Xbox/CustomBuildingPipeline_Xbox.cpp:960-964`).
* Trailing `// end of Foo()...` after long functions (`:909`, `:956`).
* Preprocessor trailing comments: `#endif //GTA_PC...`, `#endif //GTA_XBOX`,
  `#endif //DN_USE_VERTEX_SHADER....` (`PC/CustomBuildingDNPipeline.cpp:224`, `:526`).
* Inline justifications explain *why* (hardware quirk, driver bug), e.g.
  `// use standard instancing to avoid problems with onchip accelerators (Intel, S3, etc.)`
  (`PC/CustomBuildingPipeline.cpp:940-941`), `// this doesn't work good on nVidia cards`
  (`PC/CustomCarEnvMapPipeline.cpp:1353`).

### 4.7 Concrete rename map for our files

| Current (ours) | Proposed (Rockstar-conformant) | Evidence / why |
|---|---|---|
| `PS2Mode_ApplyDefaults`, `XboxMode_ApplyDefaults` (`src/render/ps2_mode.cpp:64-67`, `xbox_mode.cpp:60-63`) | `CPS2Mode::ApplyDefaults(Config*)`, `CXboxMode::ApplyDefaults(Config*)` | Original lifecycle methods are class statics (`CCustomBuildingRenderer::Initialise`, `pipes/CustomBuildingRenderer.cpp:62`) |
| `PS2Mode_GetPreset`, `XboxMode_GetPreset` | `CPS2Mode::GetPreset`, `CXboxMode::GetPreset` | same |
| `CCustomBuildingDNPipeline__CustomPipeRenderCB_PS2` (`src/render/buildingPipe.cpp:1023`) | `CCustomBuildingDNPipeline::CustomPipeRenderCB_PS2` (or `CustomBuildingDNPipeline_RenderCB_PS2` if kept free) | Original method name is `CustomPipeRenderCB` (`PC/CustomBuildingDNPipeline.cpp:1174`); the `__` free-function mangling is ours |
| `CCustomBuildingDNPipeline__CustomPipeRenderCB_Xbox` (`:1452`) | `CCustomBuildingDNPipeline::CustomPipeRenderCB_Xbox` | same |
| `CCustomBuildingDNPipeline__CustomPipeRenderCB_PBR` (`:1817`) | `CCustomBuildingDNPipeline::CustomPipeRenderCB_PBR` | same |
| `DNInstance_PS2` (`:2650`) | `CCustomBuildingDNPipeline::CustomPipeInstanceCB` (platform suffix only if multiple) | Original name `CustomPipeInstanceCB` (`PC/CustomBuildingDNPipeline.cpp:575`) |
| `CCustomBuildingPipeline__CreateCustomObjPipe_PS2` (`:2865`), `CCustomBuildingDNPipeline__CreateCustomObjPipe_PS2` (`:2884`) | `CCustomBuildingPipeline::CreateCustomObjPipe`, `CCustomBuildingDNPipeline::CreateCustomObjPipe` | Original factory is a private static method of the class (`PC/CustomBuildingPipeline.cpp:917`) |
| `CCustomCarEnvMapPipeline__CustomPipeRenderCB_Xbox` (`src/render/vehiclePipe.cpp:1390`) | `CCustomCarEnvMapPipeline::CustomPipeRenderCB_Xbox` | Original name `CustomPipeRenderCB` (`PC/CustomCarEnvMapPipeline.cpp:1231`) |
| `CCustomCarEnvMapPipeline__Env1Xform_PC` (`:404`) | `_rpMatFXSyncEnvMap1Data` (static free function) | Original name and linkage (`PC/CustomCarEnvMapPipeline.cpp:790`) |
| `CCustomCarEnvMapPipeline__Env2Xform_PC` (`:424`) | `_rpMatFXSyncEnvMap2Data` (static free function) | `PC/CustomCarEnvMapPipeline.cpp:837` |
| `CCustomCarEnvMapPipeline__AllocEnvMapPipeAtomicData` (`:165`) | `CCustomCarEnvMapPipeline::AllocEnvMapPipeAtomicData` | Declared as a public static in the original (`Xbox/CustomCarEnvMapPipeline_Xbox.h:223`) |
| `CarPipe` (`src/render/neoCarpipe.cpp:39-47`) | `CNeoCarPipeline` (or keep `CarPipe` but add the `C` prefix) | Family convention `C<Domain>Pipeline` (`CCustomCarEnvMapPipeline`) |
| `CarPipe::CreateShaders` (`:475`), `CarPipe::ShaderSetup` (`:523`) | `CNeoCarPipeline::CreateShaders`, `CNeoCarPipeline::ShaderSetup` | same |
| `CustomEnvMapPipeMaterialData { int8 scaleX; … texture; }` (`src/rw/gta.h:187-203`) | `{ CompressedFloat8 cfEnvScaleX, cfEnvScaleY, cfEnvTransSclX, cfEnvTransSclY; uint8 cfShininess; uint8 __pad0; uint16 nCurrentRenderFrame; RwTexture *pEnvTexture; }` + `GetfEnvScaleX()` etc. | Exact original layout (`Xbox/CustomCarEnvMapPipeline_Xbox.h:69-97`) |
| `CustomEnvMapPipeAtomicData { float trans; float posx; float posy; }` (`src/rw/gta.h:205-212`) | `{ float fOffsetU; float fPrevMapPosX; float fPrevMapPosY; }` | `Xbox/CustomCarEnvMapPipeline_Xbox.h:104-109` |
| `CustomSpecMapPipeMaterialData { float specularity; RwTexture *texture; }` (`src/rw/gta.h:215-219`) | `{ float fSpecularity; RwTexture *pSpecularTexture; }` | `Xbox/CustomCarEnvMapPipeline_Xbox.h:126-130` |
| Magic pass-bit tests `(materialFlags & 1)`, `& 2`, `& 4` (`src/render/vehiclePipe.cpp:1482-1484`) | `CARFX_ENVMAP1_PASS_BIT`, `CARFX_ENVMAP2_PASS_BIT`, `CARFX_SPECMAP_PASS_BIT`, `CARFX_ALLMASK_PASS_BIT` | Bit definitions `Xbox/CustomCarEnvMapPipeline_Xbox.h:43-47` |
| `#define GETENVMAP(m)` (`src/rw/gta.h:324`), `GETENVMAPATM`, `GETSPECMAP` | Keep as thin wrappers, but add the original `GETENVMAPPIPEDATAPTRPTR` / `GETENVMAPPIPEDATAPTR` / `GETENVMAPPIPEATMDATAPTR` names | Original macro family `Xbox/CustomCarEnvMapPipeline_Xbox.h:155-162` |

**Do not rename** our config enums (`CAR_PS2`, `BUILDING_XBOX`, …) — they are mod-level preset selectors,
not Rockstar pipeline IDs. Add `PDSPC_*` only for the actual game pipe IDs.

---

## 5. Port backlog (ordered)

Priorities: **P0** = blocks a correct PS2/Xbox mode; **P1** = visible parity gap; **P2** = robustness /
naming / cleanup.

| # | P | Item | Source to port | Destination in our mod |
|---|---|---|---|---|
| 1 | P0 | **DN Xbox vertex-shader contract + normal-packed night colour.** Xbox stores night in `v1` (NORMPACKED3), day in `v2` (D3DCOLOR), and interpolates with `c4/c5`; port that *contract* into our SM3.0 emulation, not the D3D8 binary. Our current DN instance path builds separate COLOR0+COLOR1 streams (`src/render/buildingPipe.cpp:2714-2725`), which is the PC-side layout. | `Xbox/CustomBuildingDNPipeline_Xbox.cpp:92-112`, `455-498`, `501-525`; `PC/DNshader.vsh:1-28`; `Xbox/CustomBuildingDNPipeline_Xbox.h:26-39` | `src/render/buildingPipe.cpp` DN instance + VS upload; `shaders/vs/` DN shader; keep a COLOR1 fallback for PC layouts |
| 2 | P0 (verify first) | **DN CPU interpolation scheduler.** Round-robin `m_AtmDNWorkingIndex` over 16 frames, thresholds 0.01/0.3, camera-change force, `preLitLum = night*t + day*(1-t)`. The vanilla game's `CCustomBuildingRenderer::Update` still calls this (`pipes/CustomBuildingRenderer.cpp:265-273`); port only if our 0x5D7100 replacement stops it. Ensure the `ObjPipeline` global at `0xC02C1C` points at our recreated pipe so `UsesThisPipeline` passes. | `PC/CustomBuildingDNPipeline.cpp:68`, `275-318`, `1497-1504`, `1512-1575`, `1597-1765`; `PC/CustomBuildingDNPipeline.cpp:256-268` | `src/render/buildingPipe.cpp` (or `pipelinecommon.cpp`); verify `src/render/buildingPipe.cpp:28` global |
| 3 | P0 | **Verify the extra-vert-colour plugin is still attached** after our 0x5D7100 hook. Our `GetInstExtraColors` reads offset `0x8D12BC` (`src/rw/gta.cpp:62`) but we never call `RpGeometryRegisterPlugin`; if vanilla `PluginAttach` is bypassed, night colours are NULL. | `PC/CustomBuildingDNPipeline.cpp:1597-1619`; our `src/render/buildingPipe.cpp:2613-2638` | `src/render/buildingPipe.cpp` init; add an assert/log on `ms_extraVertColourPluginOffset > 0` |
| 4 | P1 | **Env-map 2 scroll parity.** Sector parity, forward/backward direction dot, U accumulation with wrap, V clamp `[0,0.1]`, flip when `at.z < 0`, output `(-U, V)`. | `PC/CustomCarEnvMapPipeline.cpp:780-782`, `837-1076` | `src/render/vehiclePipe.cpp` `Env2Xform_PC` (`:424`) — replace with a literal port |
| 5 | P1 | **Env-map 1 building path uses texgen, not a texture matrix.** Original sets `D3DTSS_TCI_CAMERASPACENORMAL`, `TEXTURETRANSFORMFLAGS=DISABLE`, and never uploads `envTexMatrix`. | `PC/CustomBuildingPipeline.cpp:828-860` | `src/render/buildingPipe.cpp` PS2/Xbox env branch (`:1256-1290`, `:1624-1650`) |
| 6 | P1 | **Specular pass constants.** Light index 1, `LOCALVIEWER=FALSE`, `SPECULARMATERIALSOURCE=MATERIAL`, `specular = min(2*specularity*mult, 1)`, `power = 100*specularity`, disable after mesh. | `PC/CustomCarEnvMapPipeline.cpp:42`, `1348-1375`, `1567-1573` | `src/render/vehiclePipe.cpp` spec paths (`:1108-1140`, `:1268-1300`, `:1506-1511`) |
| 7 | P1 | **Vehicle remap-colour bug fix.** Zero `MAT1..MAT4` and `LIGHT_*` colours before surface properties. | `PC/CustomCarEnvMapPipeline.cpp:1513-1534`; constants `modelinfo/VehicleModelInfo.h:114-132` | `SetupMaterial_Xbox` / `SetupMaterial` in `src/render/vehiclePipe.cpp` |
| 8 | P1 | **Blown-up vehicle gating.** Detect `!(pDirect->flags & rpLIGHTLIGHTATOMICS)` plus atomic id bit `1<<14`, suppress env/spec, darken. | `PC/CustomCarEnvMapPipeline.cpp:1253-1275`, `1364-1375`, `1384`, `1432` | `src/render/vehiclePipe.cpp` Xbox/PS2/PC callbacks (partial at `:1423-1425`) |
| 9 | P1 | **Device caps gate.** Require multitexture, `BLENDFACTORALPHA`, `MULTIPLYADD`, specular shade caps before enabling the car pipe. | `PC/CustomCarEnvMapPipeline.cpp:1690-1724` | `src/render/pipelinecommon.cpp` `CreateShaders`/init, or `vehiclePipe.cpp` `CCustomCarEnvMapPipeline__Init` (`:182`) |
| 10 | P2 (conditional) | **Xbox skin constant restore — reference only for our PC D3D9 build.** The fix restores D3D8 registers `-38/-37` and the previous shader after skinned ped draws. Only port it if we add a D3D8/Xbox target; for PC D3D9, audit our own shader-constant saves/restores around skinned draws instead of copying the register numbers. | `Xbox/xboxallinone.c:1-5` (rationale), `:398-424` (fix), `:447-476` (node def) | Documentation in `src/render/pipelinecommon.cpp`; a D3D9-specific audit of VS constant uploads around `RpSkin` draws |
| 11 | P2 | **Pool allocators + default data.** 4096 material / 1024 atomic / 4096 spec entries; `fakeEnvMapPipeMatData` sentinel; duplicate-on-write. Ours allocates with `new` per atomic (`src/render/vehiclePipe.cpp:165-177`). | `Xbox/CustomCarEnvMapPipeline_Xbox.h:136-148`; `PC/CustomCarEnvMapPipeline.cpp:2156-2206`, `2485-2512` | `src/render/vehiclePipe.cpp` |
| 12 | P2 | **Plugin chunk IDs + disk payload.** `rwCUSTENVMAPMAT_ID`, `rwCUSTENVMAPATM_ID`, `rwCUSTSPECMAPMAT_ID`; disk struct is 5 floats + texture pointer. Needed only if we serialize/parse original assets. | `PC/CustomCarEnvMapPipeline.cpp:2079-2140`; `Xbox/CustomCarEnvMapPipeline_Xbox.h:57-63`, `116-120`; copy helpers `Xbox/CustomCarEnvMapPipeline_Xbox.cpp:2604-2647` | `src/rw/gta.h` + `src/render/vehiclePipe.cpp` |
| 13 | P2 | **Naming/structure refactor** per §4.7 (class statics, `_rpMatFXSyncEnvMap*`, struct fields, pass-bit macros). | §4 | `src/render/buildingPipe.cpp`, `vehiclePipe.cpp`, `neoCarpipe.cpp`, `src/rw/gta.h` |
| 14 | P2 | **Document the Xbox vehicle choice.** Original shipped Xbox callback does *not* draw env passes; our `CAR_XBOX` does (PC-style env1/env2 + shader `xboxCarVS`). Decide: emulate the shipped Xbox (shader/env in VS/PS) or keep the PC-style passes for visual richness. | Compare `Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1207-1474` (dead) vs `1478-1578` (live) and `PC/CustomCarEnvMapPipeline.cpp:1384-1487` | Decision record in this doc + `src/render/vehiclePipe.cpp` comment |

### 5.1 Explicit non-goals

* Do **not** port `MBR/*` — it is the OpenGL/mobile variant and is not on the PS2/Xbox path.
* Do **not** port the `#if 0` instance callbacks (`PC/CustomBuildingPipeline.cpp:164-384`,
  `PC/CustomCarEnvMapPipeline.cpp:200-421`) as if they were live; they are alternate/rejected paths.
* Do **not** copy the DN stream-size inconsistency (`PC/CustomBuildingDNPipeline.cpp:1671-1707`) or
  the RGBA stride bug (`PC/CustomBuildingPipeline.cpp:509`).

---

## 6. Verification checklist for the port

1. `grep` our tree for `ms_extraVertColourPluginOffset` and confirm it is non-zero at runtime
   (`src/rw/gta.cpp:62` is only a reference, not a registration).
2. Confirm `CCustomBuildingDNPipeline::UsesThisPipeline` sees our recreated pipe: the global at
   `0xC02C1C` (`src/render/buildingPipe.cpp:28`) must equal the pipe returned by
   `CCustomBuildingDNPipeline__CreateCustomObjPipe_PS2` (`:2884`).
3. Verify our DN Xbox-emulation path uploads `c4/c5` exactly once per atomic and that `c0-c3` are not
   clobbered by skinned draws (see §5 item 1 — the original D3D8 fix is reference-only for PC D3D9).
4. Compare our `Env2Xform_PC` output against the original for a vehicle crossing a sector boundary at
   `x = ±WIDTH_OF_SECTOR` (`PC/CustomCarEnvMapPipeline.cpp:780-782`, `962-1035`).
5. Check that our building env stage uses `D3DTSS_TCI_CAMERASPACENORMAL` and not a texture matrix
   (`PC/CustomBuildingPipeline.cpp:859-860`).
6. Check that `CARFX_ENVMAP2_PASS_BIT` is only honoured when `rpGEOMETRYTEXTURED2` is set
   (`PC/CustomCarEnvMapPipeline.cpp:1322-1324`).
7. Check that specular is disabled after each mesh and that light index 1 is disabled
   (`PC/CustomCarEnvMapPipeline.cpp:1567-1573`).

---

## 7. Appendix — source inventory

| File | Lines | Role |
|---|---:|---|
| `Xbox/CustomBuildingPipeline_Xbox.cpp` | 1178 | Xbox/PC building pipe; render CB is PC-only, Xbox is a bare wrapper |
| `Xbox/CustomBuildingDNPipeline_Xbox.cpp` | 2006 | DN pipe with Xbox D3D8 VS path and PC CPU path |
| `Xbox/CustomCarEnvMapPipeline_Xbox.cpp` | 3060 | Xbox D3D8 vehicle pipe; live callback is FF+shader, env passes dead |
| `Xbox/CustomPipelinesCommonXbox.cpp` | 310 | D3D8 state stubs live; all D3D9 helpers `#if 0` |
| `Xbox/xboxallinone.c` | 480 | RW 3.6 skin node fix (constant restore) |
| `Xbox/skinxbox.h` | 143 | RW Xbox skin platform data + API |
| `PC/CustomBuildingPipeline.cpp` | 1179 | PC D3D9 building pipe (default instancing + custom render) |
| `PC/CustomBuildingDNPipeline.cpp` | 2007 | PC D3D9 DN pipe (CPU interpolation shipped; optional VS11 path) |
| `PC/CustomCarEnvMapPipeline.cpp` | 2756 | PC D3D9 vehicle pipe (env1/env2/spec) |
| `PC/CustomEnvMapPipelineCSL.h` | 292 | MatFX structs/macros for Xbox/PC/PS2 |
| `PC/DNshader.h` / `.vsh` | 13 / 30 | Xbox DN VS binary/source |
| `PC/DNshaderPC.h` / `.vsh` | 18 / 71 | PC DN VS binary/source |
| `PC/skindefs.h` | 121 | Shader constant/matrix register contract |
| `PC/shaderdesc.h` | 39 | Shader descriptor key + limits |
| `PC/rwskin.h` | 216 | RW skin structs/enums |
| `PC/rpplugin.h` | 30 | RW plugin boilerplate |
| `MBR/*` | — | OpenGL variant; skim only |

---

*End of document. Every behavioural claim above is traceable to the cited line; where the tree contains
dead or contradictory code it is flagged explicitly rather than silently normalized.*

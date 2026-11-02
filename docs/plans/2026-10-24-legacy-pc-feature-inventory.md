# Legacy PC feature inventory — GTASource `pipes/PC` vs aap SkyGfx vs current mod

**Date:** 2026-10-24
**Type:** read-only audit; one Markdown deliverable, no source edits.
**Directive:** *"legacy pc mode should have all the functions the pc code found has so that we have full restore of beta features that weren't shipped."*

**Roots used**

| Root | Abbreviation |
|---|---|
| `E:\dev\dave\releases\GTASource\gtasa_src-main\` | `GS:/` (all `pipes/...`, `GtaPlugins.cpp` citations) |
| `E:\dev(dave)\skygfx_plus_expIV\backup_original\src\` | `aap:/` (aap SkyGfx legacy snapshot) |
| `E:\dev(dave)\skygfx_plus_expIV\src\` | repo-relative (`src/...`) |

**Scope note.** `pipes/MBR` is used only as a second witness that a behaviour belonged to the PC-era asset set (it shares the same MatFX pass-bit material setup, DN second-colour path and env-map stage, and has no detail/stochastic code). `pipes/Xbox` is cited only where it proves the PC branch is a port. Earlier decodes are not repeated: see `docs/plans/2026-10-24-gtasource-pipes.md` §§1.3–2.3 and `docs/plans/2026-10-24-aap-xbox-ps2-parity.md` §§2–3; this document extracts only the PC-specific deltas and the restore map.

**Status is relative to a hypothetical legacy PC mode** (`buildingPipe = BUILDING_PC`, `vehiclePipe = CAR_PC`, `colorFilter = COLORFILTER_PC`), not relative to the mod as a whole. A feature that exists only in the PBR/Xbox path and does not reproduce PC semantics is `PARTIAL`.

**Status legend**

| Status | Meaning |
|---|---|
| `PORTED` | shipped-PC behaviour is available in the current tree (native trampoline or faithful reimplementation) |
| `PORTED (aap)` | the aap shader reimplementation exists and works, but is not the shipped-PC code path |
| `PARTIAL` | something equivalent exists, but semantics/UV/math/branching differ from the PC source |
| `MISSING` | no live code path reproduces it |
| `DEAD CODE` | code exists but is never dispatched, or is only reachable from a disabled `#if` |
| `DROPPED` | deliberately removed; restoring it would fight a current invariant |

---

## 0. Five facts that frame the whole restore

1. **The vehicle half of PC is already live.** `vehiclePipe=PC` maps to `CAR_PC` (`src/core/main.cpp:5091`) and dispatches the original exe callback at `0x5D9900` (`src/rw/gta.cpp:163`, `src/render/vehiclePipe.cpp:3206-3208`). The same trampoline existed in aap (`aap:/gta.cpp:141`, `aap:/vehiclePipe.cpp:1406-1408`). Everything the PC car pipe does (env1/env2, FFP specular, remap-colour clear, blown-up gate) is therefore available, but opaque to our state guards.
2. **The building half is gone.** Both building create-pipe hooks return shader pipes: `0x5D7100` → `CCustomBuildingDNPipeline__CreateCustomObjPipe_PS2` and `0x5D7D90` → `CCustomBuildingPipeline__CreateCustomObjPipe_PS2` (`src/render/buildingPipe.cpp:2916-2917`). There is no `BUILDING_PC` enum (`src/skygfx.h:185-193`) and no `buildingPipe=PC` INI alias (`src/core/main.cpp:5078-5084`).
3. **aap's "PC" building pipe was never PC.** aap mapped `buildingPipe=PC` to `BUILDING_XBOX` (`aap:/config.cpp:145-148`, `aap:/main.cpp:1272-1277`). That path is the shader emulation at `aap:/buildingPipe.cpp:368-511` — it adds detail maps/stochastic, uses a shader env term, and has no FFP material/force-black/env-texgen. So "legacy PC mode" cannot be produced by selecting `BUILDING_XBOX`.
4. **A partial PC FFP port already exists but is dead and broken.** `src/rw/defaultFuncs.cpp:41-116` (PC building FFP callback) and `:300-466` (PC vehicle FFP callback) are compiled (`build/skygfx.vcxproj:171`) but have **no callers** (grep: only declarations at `src/skygfx.h:1112,1268-1269`). The building callback dereferences an uninitialised `envData = nullptr` (`:63`, used at `:82`) and has no day/night blend; `DNInstance_default` (`:126`) writes the night table directly to COLOR0 and ignores the day table. Treat it as a sketch, not a restore source.
5. **The shipped PC DN path is CPU interpolation, not a VS.** `GTA_PC_USE_DN_VS` is commented out (`GS:/pipes/PC/CustomBuildingDNPipeline.cpp:47-50`) because it failed on ATI; shipped PC blends the two colour tables into `preLitLum` on the CPU (`:1513-1576`) across a 16-frame scheduler (`:67, 283-345, 1498-1505`). The current mod patches that interpolation to `RET` (`src/render/buildingPipe.cpp:2918`, address `0x5D7200`) and uses aap's two-colour-stream shader DN instead (`:2650-2831`) — i.e. the current DN is the **beta PC DN-VS feature**, not the shipped CPU path.

---

## 1. Feature register

### 1.1 Mode / pipeline plumbing

| ID | Feature | What it does visually | Original PC implementation | aap legacy | Current mod | Status | Config knob | Notes / risks |
|---|---|---|---|---|---|---|---|---|
| PC-01 | Legacy PC mode selection | Selects the shipped PC look as a whole | PC builds default to the FFP building/vehicle pipes + PC colour filter; `CCustomBuildingRenderer::Initialise` creates both (`GS:/pipes/CustomBuildingRenderer.cpp:62-75`) | `buildingPipe=PC`→`BUILDING_XBOX` (`aap:/config.cpp:145-148`), `vehiclePipe=PC`→`CAR_PC` (`aap:/main.cpp:1284-1300`) | no `BUILDING_PC`/`PIPELINE_PC` (`src/skygfx.h:185-200`); `buildingPipe` map has only PS2/Xbox/GTAIV/PBR (`src/core/main.cpp:5078-5084`); `vehiclePipe=PC`→`CAR_PC` (`:5091`); "SA PC" preset uses `BUILDING_XBOX` (`src/core/presets.cpp:48`); "MEDIUM - PC classic" also uses Xbox (`src/core/main.cpp:4954-4957`); `pc_patched_mode.cpp:64-66` uses Xbox+Neo | **MISSING** (building half) / `PORTED` (vehicle half, native) | `pipeline`, `buildingPipe`, `vehiclePipe`, `colorFilter` | This is the top-level gap; every other PC row is blocked on it |
| PC-02 | PC building pipeline ID / atomic detection | Routes buildings to the PC pipe | `PDSPC_CBuildingEnvMap_AtmID` set at `GS:/pipes/PC/CustomBuildingPipeline.cpp:154,946-947`; DN `PDSPC_CBuildingDNEnvMap_AtmID` at `:246,1415-1416`; detection `GS:/pipes/CustomBuildingRenderer.cpp:163-179` | `RSPIPE_PC_CustomBuilding_PipeID`/`DN` (`aap:/buildingPipe.cpp:870-871,901-902`), `IsCBPCPipelineAttached` `:906-924` | same IDs (`src/render/buildingPipe.cpp:2859-2860,2890-2891`), detection `:2896-2910`; `explicitBuildingPipe` gates the `0x5D7F40` override (`:2920`) | `PORTED` | `explicitBuildingPipe` | IDs are reused for the shader pipes, so plugin data still arrives from the vanilla atomic setup |
| PC-03 | PC-only material setup (MatFX→pass bits) | Marks a material for env-map pass | `GS:/pipes/PC/CustomBuildingPipeline.cpp:99-139`, DN `:185-225`, car `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:109-163`; env2 chosen by texture name `x*` (`:131-139`, `CustomCarEnvMapPipeline.h:368-372`) | same behaviour in MBR (`GS:/pipes/MBR/CustomBuildingPipeline.cpp:43-75`, `MBR/CustomCarEnvMapPipeline.cpp:40-79`); aap replaces only `CreatePipe` (`aap:/buildingPipe.cpp:845-873`) and lets the vanilla `CustomPipeAtomicSetup` set the bits | not reimplemented; relies on vanilla `CCustomBuildingRenderer::AtomicSetup`/`CustomCarPipeAtomicSetup` still running (the game's own static functions) | `PORTED` (vanilla) | none | Risk: any future bypass of the vanilla atomic setup (e.g. `explicitBuildingPipe`) loses pass bits |

### 1.2 Building: base, DN, env

| ID | Feature | What it does visually | Original PC implementation | aap legacy | Current mod | Status | Config knob | Notes / risks |
|---|---|---|---|---|---|---|---|---|
| PC-04 | Base (non-DN) building pipeline, FFP | Draws ordinary buildings with texture × vertex colour, FFP lighting | `GS:/pipes/PC/CustomBuildingPipeline.cpp:750-908`; create `:917-957`; draw helper `CustomD3D9Pipeline_AtomicDefaultRender` `GS:/pipes/PC/CustomPipelinesCommon.cpp:58-125` | `BUILDING_XBOX` cb (`aap:/buildingPipe.cpp:368-511`) — shader, alias "PC" | `BUILDING_XBOX` cb (`src/render/buildingPipe.cpp:1452-1672`); `BUILDING_PS2` cb (`:1023-1363`); dead FFP sketch `src/rw/defaultFuncs.cpp:41-116` | **MISSING** | add `BUILDING_PC`; INI `buildingPipe=PC` | The shader cb is not PC: it uses `xboxBuildingVS/PS` + detail/stochastic and has no FFP material semantics |
| PC-05 | DN building pipeline, shader variant | Cross-fades day/night vertex colours | beta only: `GTA_PC_USE_DN_VS` (`GS:/pipes/PC/CustomBuildingDNPipeline.cpp:47-50,84-118`), VS source `GS:/pipes/PC/DNshaderPC.vsh:1-71`, constants `:1141-1165`, extra COLOR1 declaration `:696-711`, instance writes `:905-930` | `aap:/buildingPipe.cpp:103-127` (`setDnParams`), `:668-842` (`DNInstance_PS2`), VS `ps2BuildingVS.hlsl` | `setDnParams` `src/render/buildingPipe.cpp:228-289`, `DNInstance_PS2` `:2650-2831`, VS `shaders/vs/ps2/ps2BuildingVS.hlsl:35` | `PORTED (aap)` | `pcDnCpuBlend=0` (default) | This is effectively the "beta PC DN-VS" restored by aap; exact PC `DNshaderPC` packing and ×3 prelit boost are not used |
| PC-06 | DN CPU prelit blend (shipped PC) | Same visual cross-fade, no second vertex stream | `GS:/pipes/PC/CustomBuildingDNPipeline.cpp:1513-1576`; writes `preLitLum` under `rpGEOMETRYLOCKPRELIGHT` | none (shader path only) | **MISSING** — interpolation patched out at `0x5D7200` (`src/render/buildingPipe.cpp:2918`) | **MISSING** | `pcDnCpuBlend` (new) | Restoring requires unpatching `0x5D7200` and using the default RW instance CB |
| PC-07 | DN 16-frame scheduler | Spreads the CPU blend across frames to avoid spikes | `DN_NUM_FRAMES_FOR_PROCESS=16` (`GS:/pipes/PC/CustomBuildingDNPipeline.cpp:67`), `UpdateAtomicDN` `:283-317`, `Update` `:1498-1505`, `UpdateClumpDN` `:335-345` | none | only `0x5D7200` is patched to RET (`src/render/buildingPipe.cpp:2918`, aap comment "disable interpolation"); the vanilla `Update` entry itself is not hooked. Which function `0x5D7200` is must be confirmed before relying on the scheduler | `DEAD CODE` | with `pcDnCpuBlend` | Only meaningful with PC-06 |
| PC-08 | DN balance parameter + hour table | Decides day/night mix at dawn/dusk | `m_fDNBalanceParam` (`GS:/pipes/PC/CustomBuildingDNPipeline.cpp:75`); hour table `GS:/pipes/CustomBuildingRenderer.cpp:194-259` (night <6:00, ramp 6-7, day 7-20, ramp 20-21, night >21) | `aap:/buildingPipe.cpp:108` reads the same global | `CCustomBuildingDNPipeline__m_fDNBalanceParam` aliased to `0x8D12C0` (`src/rw/gta.cpp:63`); `setDnParams` `src/render/buildingPipe.cpp:233-289`; adds cutscene/vision-FX repair `:246-269` | `PORTED` + divergent repair | none | The repair is deliberate (`[dnbal_repair]`); keep unless exact PC parity is required |
| PC-09 | DN day/night tables + geometry plugin | Supplies the two colour tables used by either DN path | plugin attach `GS:/pipes/PC/CustomBuildingDNPipeline.cpp:1598-1620`; layout `GS:/pipes/PC/CustomBuildingDNPipeline.h:33-39`; accessors `:1771-1784` | `aap:/buildingPipe.cpp:611-656` reads the same offsets | reads game globals/offsets (`src/rw/gta.cpp:62`, `src/render/buildingPipe.cpp:2610-2638`); vanilla loader owns the data | `PORTED` (vanilla) | none | Do not port the stream callbacks (see §4.1) |
| PC-10 | Building env map (FFP camera-space-normal texgen) | Adds a static reflection to env-mapped buildings | `GS:/pipes/PC/CustomBuildingPipeline.cpp:824-862`; DN variant `:1264-1301`; stage1 `D3DTSS_TCI_CAMERASPACENORMAL` with index 1, `D3DTTFF_DISABLE` | PS2 cb FX pass `aap:/buildingPipe.cpp:326-355` (`ps2BuildingFxVS` + `ps2EnvSpecFxPS`); Xbox cb `:461-478` | PS2 cb `src/render/buildingPipe.cpp:1255-1350`; Xbox cb `:1623-1640`; PBR cb `:2322-2380` | `PARTIAL` | `bldEnvReflect` gates PBR only; add `pcBldEnvReflect` | Shader path computes UV from an inverse-view matrix + scale, not camera-space normal texgen; PBR caps env weight at 0.20 (`:2358-2368`) |
| PC-11 | Building env scale / tfactor / shininess | Controls reflection strength and tiling | `envTexMatrix` built from `GetfEnvScaleX/Y` but **never uploaded** (`GS:/pipes/PC/CustomBuildingPipeline.cpp:829-834`); TFACTOR `nShininess = shininess*254*1.0` (`:839-854`); DN uses `D3DTA_DIFFUSE` as ARG2 (`:1288-1293`) | shader `envXform.z/w = GetScaleX/Y` (`aap:/buildingPipe.cpp:335-338`); `ps2BuildingFxVS.hlsl:28-31` | `envXform.z/w` (`src/render/buildingPipe.cpp:1273-1278`, `:1629-1634`); `ps2BuildingFxVS` / `ps2EnvSpecFxPS` | `PARTIAL` | `envShininessMult` (shader-only) | PC's env texture matrix was inert; do not "fix" it in a PC-faithful mode (§4.7) |
| PC-12 | Force-black for unlit/non-prelit geometry | Unlit props render black instead of wrongly lit | `GS:/pipes/PC/CustomBuildingPipeline.cpp:773-786` + `CustomD3D9Pipeline_AtomicRenderBlack` `GS:/pipes/PC/CustomPipelinesCommon.cpp:30-48`; DN `:1197-1210,1328-1331`; vehicle `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1279-1292` | no equivalent; aap lights everything from its VS | **MISSING** in live cbs; dead copies `src/rw/defaultFuncs.cpp:52-66,109-112` and `:327-336,449-455` | **MISSING** | `pcForceBlackUnlit` (new) | Affects unlit props/debris; test against `D3DRS_LIGHTING` + `rxGEOMETRY_PRELIT` exactly as PC does |
| PC-13 | FFP material properties / material colour + alpha | Diffuse/ambient/emissive material response, specular params | `CustomD3D9Pipeline_RwSetSurfaceProperties` `GS:/pipes/PC/CustomPipelinesCommon.cpp:153-262`; per-mesh call `GS:/pipes/PC/CustomBuildingPipeline.cpp:880-887`; `COLORSCALAR` 1/255 `:44` | `pipeUploadMatCol` `aap:/pipelinecommon.cpp:195-204` + VS combine `ps2BuildingVS.hlsl:35-37` | `pipeUploadMatCol` `src/render/pipelinecommon.cpp:477-494` (alpha-16 fix), `buildingPipe_uploadSurfProps` `src/render/buildingPipe.cpp:129-139`, per-cb uploads `:1181-1184`, `:1609-1619` | `PARTIAL` | none | Shader path cannot reproduce D3D material emissive/ambient separation exactly; document the approximation |
| PC-14 | Prelit / vertex colours | Baked lighting from the mesh | default RW instance CB + FFP `D3DMCS_COLOR1` (first vertex colour) emissive when `rxGEOMETRY_PRELIT` (`GS:/pipes/PC/CustomPipelinesCommon.cpp:189-203,228-233`) | `DNInstance_PS2` + `ps2BuildingVS` | `DNInstance_PS2` (`src/render/buildingPipe.cpp:2650-2831`), VS `ps2BuildingVS.hlsl:35-37` | `PORTED (aap)` | none | Two colour streams only for DN; non-DN uses one |
| PC-15 | Alpha test / vertex-alpha enable | Cuts out foliage/fences, enables per-vertex translucency | `_rwD3D9RenderStateVertexAlphaEnable` (`GS:/pipes/PC/CustomBuildingPipeline.cpp:868-876`, car `:1494-1502`); game alpha-test state `GS:/GtaPlugins.cpp:534-540`; tag ref `GS:/GtaPlugins.cpp:1911-1947` | `aap:/buildingPipe.cpp:259`, `:345`, `:441-445`, `:529-540` | per-mesh `src/render/buildingPipe.cpp:1179`, `:1605`; alpha guards `:525-750`, `:2485-2590`; vehicle `src/render/vehiclePipe.cpp` Switch guards | `PORTED` + hardened | `zwriteThreshold`, `zwriteThreshold*` | `pipeEnterAlphaMode` preserves the caller's ref except degenerate 0 (`src/render/pipelinecommon.cpp:1506-1515`) |
| PC-16 | Z-write / `MODEL_DONT_WRITE_ZBUFFER` | Keeps decals/fences from occluding | vanilla `RenderEntity`: ZW off when `GetDontWriteZBuffer()`, on at exit (`GS:/GtaPlugins.cpp:1894,1957`); no per-mesh ZW changes in the PC cbs | inherits vanilla; aap never touches ZW at cb entry | **forces ZWRITE=TRUE at cb entry**: building `src/render/buildingPipe.cpp:2514`, vehicle `src/render/vehiclePipe.cpp:3188`; restored at exit | **PARTIAL (regression)** | fix unconditionally, or `preserveDontWriteZBuffer` | This defeats the PC-only `DONT_WRITE_ZBUFFER` feature for every building/vehicle atomic. Detect with `CVisibilityPlugins__GetModelInfo` (`src/render/buildingPipe.cpp:318`) + `CBaseModelInfo::GetDontWriteZ` (`src/rw/gta.h:78`) |
| PC-17 | Cull mode | Backface culling / double-sided decals | no explicit cull change in PC cbs | none | `pipeEnterCullMode` preserves NONE/FRONT/BACK, sanitises only unknown values (`src/render/pipelinecommon.cpp:1285-1319`), called at building switch `:2484`, vehicle switch `:3156` | `PORTED` | none | Correctly preserves vanilla `CULLNONE` for `bBackfaceCulled=false` (`GS:/GtaPlugins.cpp:1915-1929`) |
| PC-18 | Fog handling for env/FX legs | Prevents fog tinting the reflection add | FOGCOLOR=0 around the FX draw (`GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1304-1305,1344-1345`; building DN `:1337-1340`); aap/building `aap:/buildingPipe.cpp:348-350` | aap `aap:/buildingPipe.cpp:348-350,534-536`; vehicle `:534-536` | building PS2 `src/render/buildingPipe.cpp:1304-1321`; vehicle `vehiclePipe_fxAdditiveBlend` `src/render/vehiclePipe.cpp:674-760` | `PORTED` | none | Keep for any new PC FFP env leg |
| PC-19 | Texture-stage setup | stage0 modulate, stage1 env add, stage2 off | stage0 `MODULATE(TEX,DIFFUSE)` `GS:/pipes/PC/CustomPipelinesCommon.cpp:65-96`; stage1 `MULTIPLYADD` (`GS:/pipes/PC/CustomBuildingPipeline.cpp:849-861`, car `:1415-1427`) or `BLENDFACTORALPHA` for env2 (`:1467-1478`); stage2 disable `:822-823` | PS2/Xbox stages `aap:/vehiclePipe.cpp:921-935,543-550`; building relies on PS | shader paths only clean stages (`src/render/buildingPipe.cpp:376`, `src/render/vehiclePipe.cpp:644`); dead FFP code has the stage setup `src/rw/defaultFuncs.cpp:74-102,405-432` | `PARTIAL` | n/a | Only a PC FFP restore should own these stages |
| PC-20 | Cached-material resync after custom material | Prevents stale D3D material state | `CustomD3D9Pipeline_RwSetSurfacePropertiesCacheResync` `GS:/pipes/PC/CustomPipelinesCommon.cpp:272-278`, called `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1580-1581` | not present | not present | `MISSING` | none | Needed only with the FFP material path |
| PC-21 | Sphere-map / reflection pass for buildings | Renders buildings into the mobile/reflection capture | not in PC building pipes | `aap:/buildingPipe.cpp:515-589`, dispatch `:597-598` | `src/render/buildingPipe.cpp:1676-1814`, dispatch `:2559-2560` | `PORTED (aap)` | runtime `gRenderingSpheremap` (not INI) | Non-PC feature; keep out of PC mode |
| PC-22 | Wind vertex shader | Tree/leaf sway | not in PC pipes | `aap:/buildingPipe.cpp:130-142,271-294` | `src/render/buildingPipe.cpp:291-316,1172-1245` | `PORTED (aap)` | `treeFx` (profile) | Non-PC feature |

### 1.3 Vehicle

| ID | Feature | What it does visually | Original PC implementation | aap legacy | Current mod | Status | Config knob | Notes / risks |
|---|---|---|---|---|---|---|---|---|
| PC-23 | Base vehicle pipeline (FFP) | Draws cars with texture × material, FFP lighting, env/spec | `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1231-1594`; create `:1604-1640`, `:1648-1684`; no custom instance CB (render CB only at `:1627`) | `CAR_PC` trampoline `aap:/gta.cpp:141`, `aap:/vehiclePipe.cpp:1406-1408` | `CAR_PC` trampoline `src/rw/gta.cpp:163`, `src/render/vehiclePipe.cpp:3206-3208` | `PORTED` (native) | `vehiclePipe=PC` | Best fidelity; cannot be combined with IV/PBR shaders, and inner state is not guarded by our alpha/cull doctrine |
| PC-24 | Vehicle env1 (sector-snapped offset) | Static-ish reflection that snaps per 50 m sector | `_rpMatFXSyncEnvMap1Data` `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:789-829`; stage `:1384-1428` | smooth `aap:/vehiclePipe.cpp:143-156`; PC variant `:229-240` | `Env1Xform` `src/render/vehiclePipe.cpp:322-345`; `Env1Xform_PC` `:404-422`; PC variant used only in Xbox cb `:1529` | `PARTIAL` | add `pcEnv1Scroll`; native handles it under CAR_PC | Two variants exist; the PC one is not selectable outside Xbox |
| PC-25 | Vehicle env2 (scroll + flip) | Moving reflection that wraps and flips with car heading | `_rpMatFXSyncEnvMap2Data` `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:836-1048`; stage `:1431-1487` (`BLENDFACTORALPHA`, `D3DTTFF_COUNT2`, `at.z` flip) | `aap:/vehiclePipe.cpp:158-226` (smooth), `:243-303` (PC) | `Env2Xform` `src/render/vehiclePipe.cpp:347-402`; `Env2Xform_PC` `:424-487` | `PORTED` (math available; Xbox cb uses PC variant `:1559`) | `pcEnv2Scroll` (optional) | Native CAR_PC also does it |
| PC-26 | Env2 selection heuristic | Chooses env2 for textures named `x*` | `CustomPipeMaterialSetup` `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:131-139`; `IsTextureUsingEnvMap2FX` `CustomCarEnvMapPipeline.h:368-372` | same, via vanilla setup; `RwTextureRead_HACK` `aap:/vehiclePipe.cpp:1460-1465` maps `xvehicleenv128`→`vehicleenvmap128` | same; `RwTextureRead_HACK` `src/render/vehiclePipe.cpp:3330-3335` | `PORTED` (vanilla) | `betaEnvmaptest` (debug) | Any future texture starting with `x` silently becomes env2 |
| PC-27 | Vehicle remap-colour clearing (Bug #760) | Clears default paint/light remap colours so they do not render black/odd | `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1513-1534` (MAT1..MAT4, LIGHT_FRONT/REAR) | **missing** in aap shader cb (`aap:/vehiclePipe.cpp:444-449` only uploads mat colour) | native `CAR_PC` has it; shader PS2/Env cbs do **not**; dead FFP sketch has a hardcoded list `src/rw/defaultFuncs.cpp:441-448` | `PARTIAL` | `pcRemapColorClear` (new) | Needed only if a shader-based PC vehicle is introduced |
| PC-28 | Vehicle FFP specular (extra D3D light) | Sun-facing highlight on paint | `CARFX_D3D9_SPEC_LIGHT_INDEX=1` (`GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:42`); light built `PreRenderUpdate` `:1157-1222`; per-mesh `:1348-1375` (`materialSpecular=2*spec*1.85`, `power=100*spec`, `LOCALVIEWER=FALSE`, `SPECULARMATERIALSOURCE=MATERIAL`) | PS2 shader spec `aap:/vehiclePipe.cpp:516-520`; Xbox `SetupMaterial_Xbox` `:742-771` | PS2 cb `src/render/vehiclePipe.cpp:1142-1146`; Xbox `SetupMaterial_Xbox` `:1342-1388`; native PC | `PARTIAL` | `pcSpecLight` / `envSpecularityMult` | Shader path is a different BRDF; native PC has the exact FFP look |
| PC-29 | Specular mask texture (`s2`) | Modulates specular by a texture | **not used** by the PC callback — `pSpecularTexture` only exists in accessors `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1983-2055` | aap binds s2 and `ps2EnvSpecFxPS` samples it (`aap:/vehiclePipe.cpp:516-520`) | PS2 cb binds s2 (`src/render/vehiclePipe.cpp:1142-1150`); Xbox/Env varies | `PORTED (aap, non-PC)` | none | Do not call this PC fidelity; shipped PC ignored the mask |
| PC-30 | Specular quality/LOD gate | Keeps specular on small LODs only at high FX quality | `g_fx.GetFxQuality() >= FX_QUALITY_HIGH` bypasses `NO_EXTRA_PASSES` (`GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1329-1337`) | **missing** (`aap:/vehiclePipe.cpp:467-480` always drops) | **missing**; `noFx = GetAtomicId & 0x6000` only (`src/render/vehiclePipe.cpp:952,1480-1484`) | `MISSING` | none (respect in-game quality) | Low visual impact; exact-PC only |
| PC-31 | No-extra-passes atomic flags | Suppresses env/spec on LOD/damaged atomics | `VEHICLE_ATOMIC_PIPE_NO_EXTRA_PASSES(_LOD)` (`GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1253-1254,1322-1336`); flags defined `GS:/GtaPlugins.cpp:85-86` | `aap:/vehiclePipe.cpp:427,467-480` (`0x6000`) | `src/render/vehiclePipe.cpp:952,1480-1484`, `:1912`, `:2722` | `PORTED` | none | The quality nuance above is the only missing part |
| PC-32 | Blown-up / scorched vehicle gating | Removes reflection/specular and (optionally) darkens wrecked cars | detect `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1263-1275`; gates `:1384,1432,1507,1567`; dead darken `:1537-1551` | only `renderingWheel`; no blown-up gate | Xbox cb has `blownUp` (`src/render/vehiclePipe.cpp:1423-1425,1507-1514`); PS2/Env cbs do not; native PC does | `PARTIAL` | `pcBlownUpGate` | Shader PS2/Env path can leave scorched cars reflective |
| PC-33 | Vehicle env light multiplier | Global env/spec brightness | `m_EnvMapLightingMult` `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1242` (`*1.85` for spec, `*1.0` for env1/2) | `aap:/vehiclePipe.cpp:428, 504, 511` | aliased `0x8D12D0` (`src/rw/gta.cpp:61`), used `src/render/vehiclePipe.cpp:953,1227` | `PORTED` | `envShininessMult`, `envSpecularityMult` (shader) | Native PC uses the live global; shader paths scale it further |
| PC-34 | Vehicle pass-bit mapping | Decides env1/env2/spec per material | `CARFX_ENVMAP1_PASS_BIT=1`, `ENVMAP2=2`, `SPECMAP=4` (`GS:/pipes/PC/CustomCarEnvMapPipeline.h:43-47`), set at `CustomCarEnvMapPipeline.cpp:125-159` | same bits (`aap:/vehiclePipe.cpp:470-480`) | same bits (`src/render/vehiclePipe.cpp:1091,1254,1480`) | `PORTED` | none | Contract must not change |
| PC-35 | PC vehicle spec light save/restore | Avoids leaking light 1 / specular state | `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1348-1375,1567-1573` (light not restored on nVidia; comment) | not saved/restored (`aap:/vehiclePipe.cpp:516-541`) | not in shader cbs; native PC handles itself | `PARTIAL` | none | If restoring FFP, save/restore light 1 and `D3DRS_SPECULARENABLE` |
| PC-36 | PC car PreRenderUpdate | Builds the spec light from `pDirect` | `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1157-1222` | `aap:/vehiclePipe.cpp:128-140` computes view/light/env inverses then calls the original | `src/render/vehiclePipe.cpp:287-320` (same aap pattern) | `PORTED` (aap) | none | The original spec light still runs via `PreRenderUpdate_orig` |
| PC-37 | Vehicle dirt levels | Progressive dirt/grime texture swap (0-15) | `GS:/pipes/CarFXRenderer.cpp:202-328` (`InitialiseDirtTexture`, `RemapDirt`) | none | only PBR dirt material classification (`src/extras/veh_shaders.cpp:278-296`); no level remap | `MISSING` | `pcDirtLevels` (optional) | Texture-management feature, not a pipe; low priority |
| PC-38 | Dirt material remap callback | Swaps `vehicleGrunge256` per dirt level | `GS:/pipes/CarFXRenderer.cpp:278-305` | none | none | `MISSING` | `pcDirtLevels` | Same as above |
| PC-39 | Vehicle device-cap gate | Refuses env/spec if HW lacks multitexture/MULTIPLYADD/BLENDFACTORALPHA/specular | `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1690-1724` | none | none | `DROPPED` | none | Safe on modern HW; optional one-shot assert |

### 1.4 Shared: shaders, state, non-PC extensions

| ID | Feature | What it does visually | Original PC implementation | aap legacy | Current mod | Status | Config knob | Notes / risks |
|---|---|---|---|---|---|---|---|---|
| PC-40 | Shader creation paths (PS/VS) | Creates all programmable shader handles | PC building = no PS; optional DN VS `DNshaderPC.h:9-18`, created `GS:/pipes/PC/CustomBuildingDNPipeline.cpp:1418-1428`; car = no shaders | `CreateShaders` `aap:/pipelinecommon.cpp:366-433` (all PS2/Xbox/leeds/mobile/env + Neo in `neoCarpipe.cpp:206-223`) | `CreateShaders` `src/render/pipelinecommon.cpp:823-947`; **`simpleStochasticPS` not embedded**, `envCarVS/PS`/`iiiTrailsPS` dropped, `gtaivFPVS/PS` lazy (`ivFPReady` `src/render/buildingPipe.cpp:1007-1020`), `makeVSfromFile` removed (`pipelinecommon.cpp:802`) | `PARTIAL` | none | See §2 G13/G17 |
| PC-41 | Render-state save/restore discipline | Prevents state leaks between callbacks | PC cbs save only the env FX states (`GS:/pipes/PC/CustomBuildingPipeline.cpp:898-906`, car `:1580-1592`); no cull/ZW/alpha save | partial (`aap:/buildingPipe.cpp:218-227,357-363`; `vehiclePipe.cpp:419-425,543-550`) | heavy 3-layer guards: building `src/render/buildingPipe.cpp:410-451,525-750,2479-2591`; vehicle `src/render/vehiclePipe.cpp:605-760,3150-3300` | `PORTED` + hardened | `stateBaselineEnable`, `defaultCbShaderHygiene`, `texAlphaClassify` | The only known regression is the forced ZW (PC-16) |
| PC-42 | MatFX UV transform | Animates textures (UV scroll) | **not handled** (no `RpMatFXMaterialGetUVTransformMatrices` in PC cbs) | supported `aap:/buildingPipe.cpp:239-247` (`REG_texmat`) | supported `buildingPipe_setUVTransform` `src/render/buildingPipe.cpp:354-367`, VS `ps2BuildingVS.hlsl:33` | `PORTED (aap, non-PC)` | none | PC mode should arguably ignore UV anim for fidelity; aap modes keep it |
| PC-43 | PS2 modulate (colour scale 255/128) | Brightens PS2 textures | none | `aap:/buildingPipe.cpp:232-237` | `src/render/buildingPipe.cpp:1160-1165`, config `ps2Modulate*` | `PORTED (aap, non-PC)` | `ps2Modulate`, `ps2ModulateBuilding`, `ps2ModulateGrass` | Set `ps2Modulate=0` in PC mode |
| PC-44 | Detail maps | Adds a tiled detail texture | **not in PC pipes** (grep `detail`/`stochast` in `GS:/pipes/PC` = 0 hits; MBR also 0) | `aap:/buildingPipe.cpp:275-316,379-489` | `buildingPipe_selectPS` `src/render/buildingPipe.cpp:941-959`; config `detailMaps` | `PORTED (aap, non-PC)` | `detailMaps` | Must be **off** for a PC-faithful mode |
| PC-45 | Stochastic texturing | Randomises tiled UVs | **not in PC pipes** | plain `simpleStochasticPS` + detail variant (`aap:/buildingPipe.cpp:279-315,483-497`) | detail-stochastic and Xbox env-stochastic work (`src/render/buildingPipe.cpp:944-956`); plain `simpleStochasticPS` is **not bound** (CSO orphan `resources/cso/simpleStochasticPS.cso`, no `IDR_`/`.rc` entry) | `PARTIAL` | `stochasticTexturing` | aap parity gap; see §2 G13 |
| PC-46 | Dual pass (building/vehicle/ped/default/grass) | Two alpha-test legs for soft cutouts | **not in PC pipes** (single draw) | `aap:/main.cpp:219-253`; used at `aap:/buildingPipe.cpp:324,504`, `vehiclePipe.cpp:462,637,936,1035` | `src/core/main.cpp:208-258` (hardened, clamp 254), used building `:1252,1650,2399`, vehicle `:1083,1246,1601` | `PORTED (aap, non-PC)` | `dualPass`, `dualPassBuilding/Vehicle/Ped/Grass/Default` | Set all to 0 for PC mode |
| PC-47 | Additive FX legs | Second ONE/ONE pass for env/spec | **not in PC pipes** (env is a stage1 add in the same draw) | `aap:/vehiclePipe.cpp:522-541`, `aap:/buildingPipe.cpp:343-355` | `vehiclePipe_fxAdditiveBlend` `src/render/vehiclePipe.cpp:674-760`; building PS2 `src/render/buildingPipe.cpp:1293-1315` | `PORTED (aap, non-PC)` | none | A PC FFP restore should fold env into stage1 instead |
| PC-48 | Tags building pipe | Renders spray tags with per-object alpha | none | `aap:/buildingPipe.cpp:147-163`, `tagsBuildingPipe` (`aap:/skygfx.h:80`) | `TagRenderCB` `src/render/buildingPipe.cpp:321-339`; `tagsBuildingPipe` is overwritten (`src/core/main.cpp:5339-5340`) and its menu row is commented (`src/core/debugmenu_ui.cpp:235-236`) | `DEAD CODE` | `tagsBuildingPipe` | Either wire it or delete it; do not include in PC restore |
| PC-49 | Neo car pipe (aap PC-era look) | Two-pass car paint + fresnel/spec colour + screen reflection | none | `aap:/neoCarpipe.cpp:206-223` (shaders), `:296-369` diffuse, `:371-425` specular, `:427-453` callback | `CarPipe::CreateShaders` sets both VS to NULL (`src/render/neoCarpipe.cpp:475-485`); dispatch no longer calls `RenderCallback` (`src/render/vehiclePipe.cpp:3214-3231`); `RenderEnvTex` still runs (`neoCarpipe.cpp:295-450`) | `DEAD CODE` | `vehiclePipe=Neo` still selectable | Either restore Neo VS CSOs + RC entries or remove the option; currently it silently renders via Env/PS2 |
| PC-50 | Env reflection texture generation | Builds the live reflection map used by env/Neo/Modern vehicles | PC uses static env textures | `aap:/neoCarpipe.cpp:120-184` (`CarPipe::RenderEnvTex`) | `src/render/neoCarpipe.cpp:295-450`, `src/render/envmap.cpp:66-115` | `PORTED (aap)` | `envMapSize`, `envMapFarClipMult`, `reflectionMsaaLevel` | Independent of PC mode; keep |

### 1.5 `#if 0` / disabled / PC-only branches that count as "beta features not shipped"

| ID | Feature | Where | Why it matters | Current support |
|---|---|---|---|---|
| PC-51 | Old non-reinstancing instance CB (building) | `GS:/pipes/PC/CustomBuildingPipeline.cpp:164-383` (`#if 0`) | Dead alternative vertex-stream builder; contains the RGBA stride bug (§4.2) | not restored (correctly) |
| PC-52 | Old non-reinstancing instance CB (car) | `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:200-427` (`#if 0`) | Pre-reinstance path; no COLOR stream | not restored |
| PC-53 | Camera-direction specular light | `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1171-1181` (`#if 0`) | Would replace `pDirect` with the camera `at` vector | not restored (shipped used `pDirect`) |
| PC-54 | `BLENDFACTORALPHA` env1 variant | `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1409-1420` (`#if 0`) | Alternative env1 blend; shipped uses `MULTIPLYADD` | not restored (correct) |
| PC-55 | Blown-up darkening | `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1537-1551` (`#if 0`) | Shipped disabled it | not restored (correct) |
| PC-56 | Debug pass-bit validator | `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1050-1155` (`#if 0`) | Developer assertion of pass-bit combos | not restored (debug only) |
| PC-57 | DN dead `#if 0` env branch in render CB | `GS:/pipes/PC/CustomBuildingDNPipeline.cpp:1282-1293` (`#if 0`/`#else`) | Alternative env blend; shipped uses `MULTIPLYADD` with `D3DTA_DIFFUSE` | not restored (correct) |
| PC-58 | DN ×3 prelit colour helper | `GS:/pipes/PC/CustomBuildingDNPipeline.cpp:534-566` (`Custom_rpD3D9VertexDeclarationInstColor`, never called) | Would triple prelit RGB with clamp; intended for the DN VS streaming path | not restored |
| PC-59 | PC `CustomPipeLightingCB` | `GS:/pipes/PC/CustomBuildingDNPipeline.cpp:990-1044`, attach commented at `:1413` | Would replace RW's lighting callback with `pAmbient`/`pDirect` setup | not restored (shipped did not attach it) |
| PC-60 | PC `GTA_PC_USE_DN_VS` | `GS:/pipes/PC/CustomBuildingDNPipeline.cpp:47-50` (commented) | The actual "beta DN-VS" flag; disabled for ATI strictness | effectively restored by aap's DN VS (`src/render/buildingPipe.cpp:2650-2831`) |
| PC-61 | PC default RW instance CB (on-chip GPU workaround) | `GS:/pipes/PC/CustomBuildingPipeline.cpp:939-944`, DN `:1393-1411` | PC deliberately used the default instance CB for Intel/S3 | `DROPPED` (current always overrides with `DNInstance_PS2`) |
| PC-62 | DONT_WRITE_ZBUFFER entity branch | `GS:/GtaPlugins.cpp:1894,1941-1947,1957,2016,2048` | PC-only alpha-ref 100/0 + ZW off | vanilla code is untouched, but our forced ZW overrides it (PC-16) |
| PC-63 | Vertex-colour specular source (disabled) | `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1366-1368` (active `D3DMCS_MATERIAL`; `D3DMCS_COLOR1`/`D3DMCS_COLOR2` commented) | Would take the specular colour from vertex colour instead of the material; shipped PC uses the material | not restored (correct) |

---

## 2. Gap list

Only rows that need action for a legacy PC mode. "Config field" follows the current `Config` camelCase style; "INI key" is under `[SkyGfx]`.

| Gap | Feature(s) | Restore source (exact) | Destination (our tree) | Config field / INI key | Risk |
|---|---|---|---|---|---|
| **G01** | PC-01 mode selection | `GS:/pipes/CustomBuildingRenderer.cpp:62-75`; `GS:/pipes/PC/CustomBuildingPipeline.cpp:917-957`; aap alias map `aap:/config.cpp:145-148` | `src/skygfx.h:185-200` (append `BUILDING_PC` after `BUILDING_PBR`, `PIPELINE_PC` after `PIPELINE_GTAIV`); `src/core/main.cpp:4871-4910` (map+apply), `:5078-5084` (`{"PC", BUILDING_PC}`), `:600-640` (derive rule), `src/core/presets.cpp:48` ("SA PC" → `BUILDING_PC`), `src/render/pc_patched_mode.cpp:64` | existing `buildingPipe`, `vehiclePipe`, `pipeline`; INI `buildingPipe=PC`, `pipeline=PC` | Changing the meaning of `buildingPipe=PC` breaks existing configs that expected Xbox; document and provide `PCXbox` alias. `pipelineOverride` range must become `0..5` (`main.cpp:4886-4888`). |
| **G02** | PC-04 base non-DN building FFP cb + pipe | `GS:/pipes/PC/CustomBuildingPipeline.cpp:750-908` (callback), `:917-957` (create), `GS:/pipes/PC/CustomPipelinesCommon.cpp:58-125` (draw/default) | `src/render/buildingPipe.cpp`: add `CCustomBuildingPipeline__CustomPipeRenderCB_PC` and `CCustomBuildingPipeline__CreateCustomObjPipe_PC`; dispatch in `..._Switch` `:2479-2591` on `BUILDING_PC` | `buildingPipe` value only; no new field | FFP draw bypasses all shader register contracts; must add the same 3-layer alpha/cull/ZW guards. No detail/stochastic/dual-pass. Stale D3D material cache (PC-20). |
| **G03** | PC-10/11 building env FFP stage | `GS:/pipes/PC/CustomBuildingPipeline.cpp:824-862`; DN `GS:/pipes/PC/CustomBuildingDNPipeline.cpp:1264-1301` | same new PC callback (G02) | `bldEnvReflect` (reuse; currently PBR-only at `src/render/buildingPipe.cpp:2322`); optional `pcBldEnvTFactor` | Stage-1 leaks across meshes; env texture NULL deref (the dead sketch already does this at `src/rw/defaultFuncs.cpp:63-82`); DN must use `D3DTA_DIFFUSE`, non-DN `D3DTA_TFACTOR`. |
| **G04** | PC-12 force-black unlit | `GS:/pipes/PC/CustomBuildingPipeline.cpp:773-786`, `GS:/pipes/PC/CustomPipelinesCommon.cpp:30-48`; DN `GS:/pipes/PC/CustomBuildingDNPipeline.cpp:1197-1210`; car `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1279-1292` | `src/render/buildingPipe.cpp` (PC cb), `src/render/vehiclePipe.cpp` (PC FFP cb if added) | `pcForceBlackUnlit` | Wrong `D3DRS_LIGHTING` read (cache vs device) can black out the world; mirror PC's exact `lighting OR PRELIT` test and restore the stage-0 state after the black draw. |
| **G05** | PC-13/20 FFP material + cache resync | `GS:/pipes/PC/CustomPipelinesCommon.cpp:153-262,272-278` | new PC cbs | none | The RW wrapper `RwD3D9SetSurfaceProperties` exists (`src/rw/gta.cpp:109`), but custom specular/power needs the game's `D3D9SetRenderMaterialProperties` (`src/rw/gta.cpp:151`); must call the cache resync or later draws use a stale material. |
| **G06** | PC-06/07 CPU DN blend + scheduler | `GS:/pipes/PC/CustomBuildingDNPipeline.cpp:1513-1576` (blend), `:283-345` (schedule), `:1498-1505` (`Update`), `:239-244` (`rpD3D9GEOMETRYUSAGE_DYNAMICPRELIT`) | `src/render/buildingPipe.cpp`: new `PC_UpdateAtomicPrelitLum`/`PC_UpdateDN`; replace the unconditional `Patch<uint8>(0x5D7200, 0xC3)` `:2918` with a conditional detour; return a PC DN pipe from `CCustomBuildingDNPipeline__CreateCustomObjPipe_PS2` `:2865` when enabled | `int pcDnCpuBlend;` INI `pcDnCpuBlend` (0=shader DN default, 1=PC CPU blend) | First confirm what `0x5D7200` patches (interpolation body vs scheduler entry); if it is `UpdateAtomicPrelitLum`, the vanilla `Update` still cycles and no extra hook is needed. Locking `rpGEOMETRYLOCKPRELIGHT` every frame; must use the default RW instance CB for dynamic prelit; conflicts with the shader DN (two owners of `preLitLum`). |
| **G07** | PC-16 ZW / `DONT_WRITE_ZBUFFER` regression | vanilla `GS:/GtaPlugins.cpp:1894,1957`; flag accessor `src/rw/gta.h:78` | `src/render/buildingPipe.cpp:2504-2515`, `src/render/vehiclePipe.cpp:3178-3189`: when `CVisibilityPlugins__GetModelInfo(atomic)->GetDontWriteZ()` is true, force **FALSE** (or skip the force) instead of TRUE | none (bug fix); optional `preserveDontWriteZBuffer` | If the model-info pointer is null or the atomic is not an entity, fall back to the saved value. Test with fences, hedges, decals, and tags. |
| **G08** | PC-27 remap-colour clearing | `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1513-1534` | `src/render/vehiclePipe.cpp` PS2/Env/Xbox per-mesh material uploads (e.g. `:1110`, `:1265`, `:1584`) | `pcRemapColorClear` | Only for shader-based PC vehicle; native `CAR_PC` already does it. Clearing MAT1..MAT4 could change legitimately tinted lights if the constant table shifts. |
| **G09** | PC-28 FFP spec light (shader path) | `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:42,1157-1222,1348-1375` | `src/render/vehiclePipe.cpp` new PC FFP cb or a `pcSpecLight` branch in PS2 cb | `pcSpecLight` | Light index 1 collides with the game's vehicle/spec light; save/restore `m_gSpecularLight` and `D3DRS_SPECULARENABLE`; nVidia `GetLight` issue noted in the original (`:1353-1354`). |
| **G10** | PC-32 blown-up gate (shader cbs) | `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1263-1275,1384,1432,1507,1567` | `src/render/vehiclePipe.cpp` PS2 cb `:1088-1166`, Env cb `:2191+` | `pcBlownUpGate` | Incorrect detection (pDirect flags) can drop reflections from all traffic; the identical logic already works in the Xbox cb (`:1423-1425`). |
| **G11** | PC-30 specular quality gate | `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1329-1337` | `src/render/vehiclePipe.cpp:952,1480` | none (read `g_fx.GetFxQuality()`) | Low; only changes low-LOD cars at LOW/MEDIUM quality. |
| **G12** | PC-24 env1 PC scroll selector | `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:789-829`; aap `aap:/vehiclePipe.cpp:229-240` | `src/render/vehiclePipe.cpp` PS2/Env branches (`:1116`, `:1276`, `:2191+`) select `Env1Xform_PC` when enabled | `pcEnv1Scroll` | The PC variant snaps at sector boundaries by design (visible pop); only for shader PC modes. |
| **G13** | PC-45 plain stochastic PS missing | `shaders/stochasticBuildingPS.hlsl:24-28` (`main_simpleStochastic`, already compiled `tools/fast_build.py:440`); aap use `aap:/buildingPipe.cpp:287-292` | `resources/resource.h` add `IDR_SIMPLESTOCHASTICPS` at **278** (documented free at `resources/resource.h:134`; 279 is TREEALPHA, 280 staged), `resources/Resource.rc` add the line, `src/render/pipelinecommon.cpp:902-945` add `makePS`, `src/skygfx.h` declare handle, `src/render/buildingPipe.cpp:941-959` return it for stochastic-without-detail | reuse `stochasticTexturing` | Tiny; the CSO already exists (`resources/cso/simpleStochasticPS.cso`) but is not embedded. |
| **G14** | PC-48 tags pipe | aap `aap:/buildingPipe.cpp:147-163`, `aap:/main.cpp:1722` | `src/render/buildingPipe.cpp:321-339`, `src/core/main.cpp:5339-5340`, `src/core/debugmenu_ui.cpp:235-236` | `tagsBuildingPipe` | Either wire to a real selection or delete; do not let it silently mirror `buildingPipe`. |
| **G15** | PC-49 Neo pipe | aap `aap:/neoCarpipe.cpp:206-223`, `:296-453`; deleted `IDR_NEOVEHICLEONEVS/TWOVS` | `src/render/neoCarpipe.cpp:475-485`, `src/render/vehiclePipe.cpp:3214-3231`, `resources/resource.h`/`.rc` | `vehiclePipe=Neo` | High: needs two CSOs + RC entries that no longer exist; the current fallback must stay until assets are restored. |
| **G16** | PC-37/38 dirt levels | `GS:/pipes/CarFXRenderer.cpp:202-328` | `src/render/vehiclePipe.cpp` or a new `src/render/carDirt.cpp`; hook `CCarFXRenderer::RemapDirt`/texture load | `pcDirtLevels` | Conflicts with PBR dirt/grunge classification (`src/extras/veh_shaders.cpp:278-296`); texture lifetime owned by the game. |
| **G17** | PC-40 removed/lazy shaders | `aap:/pipelinecommon.cpp:366-433`; current build lists `tools/fast_build.py:430-442` | `src/render/pipelinecommon.cpp:823-947` | none | `envCarVS/PS` and `iiiTrailsPS` are unused legacy; `gtaivFPVS/PS` are intentionally lazy. Only `simpleStochasticPS` is a real gap (G13). |
| **G18** | PC-01 legacy INI aliases | aap `aap:/main.cpp:1272-1300` (`PC`, `Spec`, `LCS`) | `src/core/main.cpp:5078-5103` | existing keys | Low; but note `buildingPipe=PC` semantics change with G01. Add `Spec` and `LCS` aliases independently. |

**Deliberate divergences — no action needed**

| Item | Why no action |
|---|---|
| `CAR_PC` native (PC-23) | Already exact; do not replace it with the dead FFP sketch |
| Env1 smooth vs PC snap (PC-24) | Both variants exist; native PC already uses the snap |
| Env2 PC math (PC-25) | Already ported and used by the Xbox cb |
| PC pass-bit material setup (PC-03) | Vanilla still runs it |
| DN hour table (PC-08) | Vanilla still runs it, plus our repair |
| PC spec-mask texture (PC-29) | Not a PC feature |

---

## 3. Ranked restore plan (visual impact for a legacy PC mode)

| Rank | Phase | Work | Impact | Effort | Depends on |
|---|---|---|---|---|---|
| 1 | P0 — mode plumbing | G01 + G18: append `BUILDING_PC`/`PIPELINE_PC`, map `buildingPipe=PC`, add preset, fix `pipelineOverride` range | Makes the mode selectable; without it every later item is unreachable | 0.5 d | none |
| 2 | P1 — PC building FFP (non-DN) | G02 + G03 + G05: render callback, create pipe, stage0/1, FFP material, env texgen | The single largest visual delta: restores the actual shipped building look | 2–3 d | P0 |
| 3 | P1b — force-black + state | G04 + G07: unlit black path, ZW/DONT_WRITE fix, material cache resync | Fixes black-world/unlit props and the only PC-only regression in our hardening | 0.5–1 d | P1 |
| 4 | P2 — DN CPU blend | G06: `UpdateAtomicPrelitLum`, 16-frame scheduler, dynamic prelit hint, conditional unpatch | Restores the **shipped** DN path (vs aap's beta DN VS). Visual delta is small; fidelity is exact | 1–2 d | P1 (needs the PC DN pipe) |
| 5 | P3 — PC vehicle shader legs (optional) | G08 + G09 + G10 + G11 + G12 | Only needed if PC vehicles must run through our shader tree (IV/PBR combos). Native `CAR_PC` already covers the pure PC case | 1–2 d | P0 |
| 6 | P4 — resource/alias hygiene | G13 + G18 (`Spec`, `LCS`) | Restores plain stochastic for aap modes; prevents silent INI fallbacks | 0.5 d | none |
| 7 | P5 — tags | G14 | Removes a dead config knob; no visual delta | 0.25 d | none |
| 8 | P6 — Neo | G15 | Restores the aap PC-era car look; large effort, high asset risk | 2–3 d | assets/CSOs |
| 9 | P7 — dirt levels | G16 | Nice-to-have PC texture feature | 1 d | vehicle texture ownership |
| 10 | P8 — beta DN VS exactness | PC-60 + PC-58: `DNshaderPC` packing, ×3 prelit scaling | Almost invisible next to aap's DN; archival value only | 1 d | P2 |

**Recommended release gate for "legacy PC mode" = ranks 1–4 + 6.** Ranks 5, 7–10 are optional/second wave.

**Mode defaults for a PC-faithful preset** (derived from the PC source, not from aap):

| Setting | Value | Reason |
|---|---|---|
| `buildingPipe` | `PC` (`BUILDING_PC`) | shipped building FFP |
| `vehiclePipe` | `PC` (`CAR_PC`) | shipped car FFP |
| `colorFilter` | `PC` (`COLORFILTER_PC`) | PC colour filter |
| `detailMaps` | `0` | absent from `GS:/pipes/PC` and `GS:/pipes/MBR` |
| `stochasticTexturing` | `0` | same |
| `dualPass` / `dualPass*` | `0` | PC draws once; dual pass is aap-only |
| `ps2Modulate` / `ps2ModulateBuilding` | `0` | aap-only |
| `bldEnvReflect` | `1` | PC env stage exists for env-flagged materials |
| `envMapSize` | `256` | PC-era texture size (current MEDIUM preset uses 256) |
| `envMapFarClipMult` | `1.0` | PC env camera used the game default |
| `pcDnCpuBlend` | `1` | shipped PC DN path |
| `pcForceBlackUnlit` | `1` | PC-only branch |
| `pcRemapColorClear` | `1` | PC Bug #760 |
| `pcSpecLight` (if shader PC) | `1` | PC FFP specular |
| `pcBlownUpGate` | `1` | PC wrecked-car gate |

---

## 4. Do-not-restore list

### 4.1 DN stream size / payload mismatch
`GS:/pipes/PC/CustomBuildingDNPipeline.cpp:1672-1690` writes `sizeof(uint8*)` (a pointer value used as a present/absent flag) plus **one** table (`numVerts*4`), while `:1698-1708` reports **two** tables (`4 + numVerts*4*2`). `:1714-1765` reads the pointer flag, allocates two tables and reads only the night table. Every chunk is 4·numVerts bytes larger than its payload → Rw stream desync, and any following chunk is read from the wrong offset. If the PC DN tables ever need to be serialized, write both tables and make `GetSize` match, or write one table and halve `GetSize`. Do **not** copy this pair into our tree.

### 4.2 RGBA stride bug
`GS:/pipes/PC/CustomBuildingPipeline.cpp:496-510`: the RGBA branch advances `offset += sizeof(RwRGBA)` (`:507`) but `vertexStream->stride += sizeof(RwV3d)` (`:508`), adding 12 instead of 4. The vertex buffer is then 8 bytes per vertex too large and every attribute after COLOR uses a wrong byte offset. This is inside the disabled `#if 0` instance CB (`:164-383`), so shipped code is unaffected, but any "restore the old instance CB" would resurrect it.

### 4.3 Header-guard collision (PC vs MBR)
`GS:/pipes/PC/CustomBuildingPipeline.h:10-11` and `GS:/pipes/MBR/CustomBuildingPipeline.h:5-6` both use `__PCCUSTOMBUILDINGPIPELINE_H__`; `GS:/pipes/PC/CustomPipelinesCommon.h:8-9` and `GS:/pipes/MBR/CustomPipelinesCommon.h:8-9` both use `__CUSTOMPIPELINESCOMMONPC_H__`. Including both variants in one translation unit silently drops one header. Never import both trees into the same build/TU.

### 4.4 Dead `CustomPipelinesCommonXbox.cpp`
`GS:/pipes/Xbox/CustomPipelinesCommonXbox.cpp` exists (and is listed in a historical `gtafiles.txt` PC path), but it implements D3D8/Xbox state (`D3DDevice_*`, `_rwXbRenderStateForce`) and is not part of a PC build. It cannot compile against our D3D9/RW 3.6 tree; use `GS:/pipes/PC/CustomPipelinesCommon.cpp` instead.

### 4.5 `GTA_PC_USE_DN_VS` as-is
`GS:/pipes/PC/CustomBuildingDNPipeline.cpp:47-50` is disabled for ATI strictness; the VS (`DNshaderPC.vsh`) declares `dcl_color1` and only works with the matching extra COLOR stream (`:696-711,905-930`) and c4 packing (`:1141-1165`). Our aap DN VS already achieves the same visual with a cleaner register contract. Restoring the PC VS would add a second DN implementation without a visual win. If exactness is required, port the c4/c5 packing and the ×3 prelit helper (`:534-566`) behind a debug flag, not as a default.

### 4.6 `CustomPipeLightingCB`
`GS:/pipes/PC/CustomBuildingDNPipeline.cpp:990-1044` is defined but never attached (`:1413` commented). It would replace RW's lighting callback and change every light contribution. Shipped PC used RW's default lighting; do not attach it.

### 4.7 PC building env texture matrix
`GS:/pipes/PC/CustomBuildingPipeline.cpp:829-834` builds `envTexMatrix` from `GetfEnvScaleX/Y` but never calls `RwD3D9SetTransform(D3DTS_TEXTURE1, …)`; only the FFP `CAMERASPACENORMAL` texgen is active. "Fixing" the missing upload changes the reflection tiling and is not PC-faithful. Keep it inert in a PC mode.

### 4.8 Specular mask texture as a PC feature
`CustomSpecMapPipeMaterialData::pSpecularTexture` is loaded/accessed (`GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1983-2055`) but the PC render callback never binds `s2`; only the scalar `fSpecularity` is used. The aap `ps2EnvSpecFxPS` mask sampling (`aap:/vehiclePipe.cpp:516-520`) is an aap extension, not a PC restore. Do not treat missing s2 as a PC gap.

### 4.9 Camera-direction spec light
`GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1171-1181` (`#if 0`) would use the camera `at` vector; shipped code uses `pDirect` (`:1178-1181`). Switching changes the highlight direction on every car. The same applies to the commented `D3DMCS_COLOR1`/`D3DMCS_COLOR2` specular-source alternatives at `:1366-1368`: shipped PC reads specular from the material only.

### 4.10 Old non-reinstancing instance callbacks
`GS:/pipes/PC/CustomBuildingPipeline.cpp:164-383`, `GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:200-427`. They destroy/recreate buffers on every call, do not support re-instancing, and the building one contains §4.2. The shipped pipes leave instancing at the RW default (building `:942`; the car pipe sets only a render callback at `:1627`).

### 4.11 Blown-up darkening
`GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1537-1551` (`#if 0`). Shipped code disabled it; the live path only removes env/spec from wrecked cars (`:1384,1432,1507`). Do not darken—it double-darkens the already-scorched texture.

### 4.12 `BLENDFACTORALPHA` env1 variant
`GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1409-1420` (`#if 0`). Shipped env1 uses `MULTIPLYADD` (`:1415-1419`); env2 is the one that legitimately uses `BLENDFACTORALPHA` (`:1467-1478`). Mixing them changes env brightness.

### 4.13 PC dirt remap inside the pipe restore
`GS:/pipes/CarFXRenderer.cpp:202-328` is a texture-management feature (game-owned TXD and material pointers), not part of the render callback. It conflicts with the PBR grunge/dirt classification (`src/extras/veh_shaders.cpp:278-296`) and with our texture lifetime rules. If wanted, implement it as a separate opt-in `pcDirtLevels` lane, never inside the pipe callbacks.

### 4.14 `RpD3D9GeometrySetUsageFlags(DYNAMICPRELIT)` without the CPU blend
`GS:/pipes/PC/CustomBuildingDNPipeline.cpp:239-244` exists only because PC re-writes `preLitLum` every frame. Enabling it with the shader DN path allocates dynamic prelit buffers that nobody updates, wasting memory and causing stale prelight. Only enable it with `pcDnCpuBlend=1`.

### 4.15 PC device-cap hard failure
`GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1690-1724` returns FALSE (and `CreatePipe` asserts) if `MULTIPLYADD`/`BLENDFACTORALPHA`/specular caps are missing. Modern D3D9 hardware always has them; copying the hard failure would turn a driver quirk into a black-screen/no-vehicle failure. Keep it as a one-shot log if desired.

---

## 5. Verification checklist (for the eventual implementation)

1. `skygfx.ini` with `buildingPipe=PC`, `vehiclePipe=PC`, `pipeline=PC` resolves to `BUILDING_PC` + `CAR_PC` + `COLORFILTER_PC` in the log (`Config: pipeline=... buildingPipe=... vehiclePipe=...`).
2. Day scene: env-mapped building shows a static camera-space-normal reflection; non-env building is texture × prelight with no detail map.
3. Night scene: dawn 6:00–7:00 and dusk 20:00–21:00 cross-fade is smooth; `[dnbal_repair]` only fires in cutscenes with vision FX off.
4. Unlit non-prelit prop (e.g. debris) renders black, not ambient-lit.
5. Fence/hedge with `MODEL_DONT_WRITE_ZBUFFER` does not occlude what is behind it (PC-16 fix); log shows no "forcing TRUE" for those models.
6. Vehicle: env1 snaps at sector edges; env2 scrolls and flips; specular is a single sun-facing highlight; MAT1..MAT4 remaps clear; wrecked cars lose reflection.
7. `detailMaps=0`, `stochasticTexturing=0`, `dualPass=0`, `ps2Modulate=0` in the PC preset; flipping any of them visibly changes the look in the expected direction.
8. No state leak: after a building/vehicle frame, `D3DRS_ZWRITEENABLE`, `ALPHABLENDENABLE`, `SRCBLEND/DESTBLEND`, `ALPHAFUNC/ALPHAREF`, `CULLMODE`, `TEXTUREFACTOR`, and stage1/2 `COLOROP` equal the pre-frame values.
9. `vehiclePipe=Spec` / `vehiclePipe=LCS` (aap aliases) parse, not silently fall back to PBR.
10. Neo: either the two VS CSOs load and `CarPipe::RenderCallback` runs, or the menu entry is removed. No silent Env/PS2 fallback under the name "Neo".

---

## 6. Condensed 12-bullet summary

1. **Only the vehicle half of legacy PC is live**: `vehiclePipe=PC` already trampolines the shipped exe callback at `0x5D9900` (`src/rw/gta.cpp:163`, `src/render/vehiclePipe.cpp:3206-3208`).
2. **The building half is missing**: both building create-pipe hooks return shader pipes (`src/render/buildingPipe.cpp:2916-2917`); there is no `BUILDING_PC` and no `buildingPipe=PC` alias (`src/skygfx.h:185-193`, `src/core/main.cpp:5078-5084`).
3. **aap's "PC" building was never PC**: `buildingPipe=PC` mapped to `BUILDING_XBOX` (`aap:/config.cpp:145-148`), a shader emulation (`aap:/buildingPipe.cpp:368-511`) that adds non-PC detail/stochastic.
4. **A partial PC FFP port exists but is dead and broken**: `src/rw/defaultFuncs.cpp:41-116` / `:300-466` are compiled but never called; the building callback null-derefs `envData` and ignores the day table.
5. **Shipped PC DN is CPU interpolation** (`GS:/pipes/PC/CustomBuildingDNPipeline.cpp:1513-1576`) over a 16-frame scheduler (`:283-345`); our mod patches it to RET (`src/render/buildingPipe.cpp:2918`) and uses aap's two-colour shader DN, i.e. the beta PC DN-VS path.
6. **PC building env is FFP stage-1 texgen** (`CAMERASPACENORMAL`, `MULTIPLYADD`, TFACTOR non-DN / DIFFUSE DN); current shader paths approximate it with `ps2EnvSpecFxPS` and cap PBR env weight at 0.20 (`src/render/buildingPipe.cpp:2358-2368`).
7. **PC vehicle env math is split**: env1 sector-snap (`Env1Xform_PC` `src/render/vehiclePipe.cpp:404-422`) and env2 scroll/flip (`Env2Xform_PC` `:424-487`) both exist, but only the Xbox cb selects the PC variants.
8. **PC vehicle-only branches absent from shader cbs**: remap-colour clear (`GS:/pipes/PC/CustomCarEnvMapPipeline.cpp:1513-1534`), FFP spec light (`:1348-1375`), blown-up gate (`:1263-1275`), and the FX-quality specular gate (`:1329-1337`).
9. **Our ZW hardening defeats `MODEL_DONT_WRITE_ZBUFFER`**: unconditional `ZWRITE=TRUE` at `src/render/buildingPipe.cpp:2514` and `src/render/vehiclePipe.cpp:3188` overrides the vanilla PC branch (`GS:/GtaPlugins.cpp:1894,1957`).
10. **`simpleStochasticPS` is a real resource gap**: the CSO exists (`resources/cso/simpleStochasticPS.cso`) and is compiled (`tools/fast_build.py:440`), but is not embedded (`resources/resource.h`/`Resource.rc`), so plain stochastic silently uses `simplePS`.
11. **Do not restore** the DN stream size/payload mismatch (`:1672-1765`), RGBA stride bug (`CustomBuildingPipeline.cpp:508`), PC/MBR header-guard collision, dead Xbox common, dead lighting callback, or the `#if 0` spec/env variants.
12. **Recommended order**: mode plumbing → PC FFP building (non-DN + env) → force-black + ZW fix → CPU DN blend/scheduler → optional shader-PC vehicle legs → aliases/stochastic → Neo/dirt. Ranks 1–4 plus aliases are the minimum for a truthful legacy PC mode.

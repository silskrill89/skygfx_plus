# AAP → skygfx_plus_expIV: PS2 / Xbox mode parity audit + port plan

**Date:** 2026-10-24
**Repos audited:** `E:\dev(dave)\skygfx_plus_expIV` (current mod), `backup_original/` (aap SkyGfx legacy), `E:\dev(dave)\releases\GTASource\gtasa_src-main\pipes\` (Rockstar originals, reference only)
**Mode:** read-only audit. Only this file was written.
**Parity baseline:** *aap behaviour*, not Rockstar console behaviour. The Rockstar Xbox/PC pipes are fixed-function (FFP) renders (`pipes/Xbox/CustomBuildingPipeline_Xbox.cpp:751`, `pipes/PC/CustomBuildingPipeline.cpp:750`); aap's "Xbox" pipe is already a shader re-implementation of that look (`backup_original/src/buildingPipe.cpp:368`). "Exact Rockstar parity" is therefore not a meaningful target for the Xbox building/vehicle pipes — the target is aap's shader emulation.

**Look baseline used throughout** (this is the only place the target values are defined):

| Field | PS2 target | Xbox target | Source |
|---|---|---|---|
| buildingPipe / vehiclePipe | `BUILDING_PS2` / `CAR_PS2` | `BUILDING_XBOX` / `CAR_XBOX` | `src/core/presets.cpp:43-46` |
| colorFilter | `COLORFILTER_PS2` | `COLORFILTER_PC` | `src/core/presets.cpp:43-46` |
| ps2Modulate | 1 | 0 | `src/core/presets.cpp:43-46` |
| dualPass | 0 | 1 | `src/core/presets.cpp:43-46` |
| radiosity / doRadiosity | 0 (composite) / 1 | 1 (shader) / 1 | `src/core/presets.cpp:43-46` |
| grainFilter | 0 (PS2 grain) | 0 (preset) — aap INI default is 1 for non-PS2 filters | `src/core/presets.cpp:43-46`, `backup_original/src/main.cpp:1367-1372` |
| detailMaps / stochastic | 0 / 0 | 1 / 1 (aap first-run template) | `backup_original/src/main.cpp:1282-1284`, `1476-1477` |
| envMapSize | 256 (aap default) | 256 | `backup_original/src/main.cpp:1315` |
| coronaZtest | 0 (PS2) | 1 (PC) | `backup_original/src/main.cpp:330-346` |
| ped/stencil shadows | −1 (leave alone) | −1 (leave alone) | `src/core/presets.cpp:43-46` |
| SSAO/SMAA | 0/0 | 0/0 | `src/core/presets.cpp:43-46` |

---

## 0. Executive summary (what is actually broken)

1. **The PS2/Xbox *modes* are not implemented.** `src/render/ps2_mode.cpp` and `src/render/xbox_mode.cpp` are compiled but **never called** (no reference outside their own definitions and `src/skygfx.h:1428-1432`). The live mode manager profiles are explicit stubs: `profilePS2` / `profileXbox` only set `pipeline`, `buildingPipe`, `vehiclePipe` (`src/core/modeManager.cpp:125-141`, comments literally say "expand later").
2. Consequence: clicking **PS2** today changes the pipes but leaves `colorFilter`, `ps2Modulate`, `dualPass`, `detailMaps`, `stochastic`, `grainFilter`, `radiosity`, `coronaZtest`, `envMapSize` at whatever the user had. A user coming from PBR gets PS2 pipes under the **Modern** tone-map.
3. The PS2/Xbox **render callbacks themselves are ported** and are close to aap (`src/render/buildingPipe.cpp:1023` PS2, `:1452` Xbox; `src/render/vehiclePipe.cpp:896` PS2, `:1390` Xbox), with substantial render-state hardening.
4. One aap building material path is **missing outright**: `simpleStochasticPS` (stochastic tiling without a detail map). aap loads it (`backup_original/src/pipelinecommon.cpp:379`) and selects it (`backup_original/src/buildingPipe.cpp:310-315`, `491-496`); our tree has no global, no `IDR_SIMPLESTOCHASTICPS`, no `makePS`, and `buildingPipe_selectPS` falls through to plain `simplePS` (`src/render/buildingPipe.cpp:940-959`). The CSO still exists in `resources/cso/simpleStochasticPS.cso` and is still built (`tools/fast_build.py:440`), but nothing loads or binds it.
5. The **lighting source diverges from aap in both modes**: aap buildings use `CTimeCycle ambient * CCoronas::LightsMult` (`backup_original/src/buildingPipe.cpp:57-59`); aap PS2 vehicles use `pAmbient->color` (`backup_original/src/vehiclePipe.cpp:348`). Our `uploadLights` uses timecycle ambient with a sky-luminance clamp (`src/render/vehiclePipe.cpp:540`, `src/render/pipelinecommon.cpp:190-213`, clamp at `:97-188`), and our building ambient uses the same helper (`src/render/buildingPipe.cpp:182`). aap's Xbox building VS has **no direct-light term** (ambient + DN prelight + matCol only), so the lighting change is most visible there.
6. Our PS2 vehicle path also **force-uploads the sun** (ignores `rpLIGHTLIGHTATOMICS`) at `src/render/vehiclePipe.cpp:557`; aap gated it (`backup_original/src/vehiclePipe.cpp:348-349`).
7. Our Xbox building path **adds a direct-sun term aap never had** (`src/render/buildingPipe.cpp:1574-1575`; shader `shaders/vs/xboxBuildingVS.hlsl:13-14`, `65-76`). It is a deliberate "dawn black world" fix, but it is a parity divergence.
8. Legacy INI aliases were dropped: aap accepted `buildingPipe=PC` (`backup_original/src/main.cpp:1273-1274`) and `vehiclePipe=Spec` (`:1292`); our maps accept neither (`src/core/main.cpp:5076-5083`, `5087-5100`).
9. `tagsBuildingPipe` is now dead: aap's `TagRenderCB` branched on it (`backup_original/src/buildingPipe.cpp:151`), ours branches on `buildingPipe` (`src/render/buildingPipe.cpp:325`), so the field is written once (`src/core/main.cpp:5338`) and ignored.
10. Xbox's mode preset (if ever wired) turns stochastic texturing **off** (`src/render/xbox_mode.cpp:94`), contradicting the aap first-run default `stochastic=1` (`backup_original/src/main.cpp:1477`).

---

## 1. Inventory — every legacy PS2/Xbox-related file and function

### 1.1 Files that exist (and whether they compile in our tree)

`backup_original/` is **not in the build**: `premake5.lua:15-19` only globs `src/*.*`, `src/Core/*.*`, `src/render/*.*`, etc. No legacy file is compiled.

| File | Lines | PS2/Xbox role | Compiled in our tree? |
|---|---:|---|---|
| `backup_original/src/buildingPipe.cpp` | 940 | Building PS2 + Xbox + Sphere + PBR-less DN callbacks, DN instance, pipe creation, building hooks | **No** (`premake5.lua:15-19`) |
| `backup_original/src/vehiclePipe.cpp` | 1492 | Vehicle PS2, Specular, Xbox, Leeds/LCS/VCS, Mobile, Neo/Env, Switch, env-map setup, vehicle hooks | **No** |
| `backup_original/src/neoCarpipe.cpp` | 463 | Neo two-pass car pipe (`CarPipe`), env capture, lights | **No** |
| `backup_original/src/neo.cpp` / `neo.h` | 209 / — | Neo tweaking table, water/blood drops flags | **No** |
| `backup_original/src/pipelinecommon.cpp` | 434 | `CreateShaders` (loads every PS2/Xbox/Neo CSO), light upload helpers | **No** |
| `backup_original/src/envmap.cpp` | 483 | Env-map rasters/camera, `RenderReflectionScene`, `DrawEnvMapCoronas`, `envmaphooks` | **No** |
| `backup_original/src/main.cpp` | 2266 | `D3D9RenderDual`, `D3D9Render*_DUAL`, INI pipeline maps, corona Z-test patch, hook installation | **No** |
| `backup_original/src/postfx.cpp` | — | `COLORFILTER_PS2` / `COLORFILTER_PC` (`:1295-1300`), `Grain_PS2` (`:1171-1175`) | **No** |
| `backup_original/src/gta.cpp` / `gta.h` | — | Game wrappers: `CCustomBuildingDNPipeline__m_fDNBalanceParam` (`gta.cpp:44`), `CCustomCarEnvMapPipeline__CustomPipeRenderCB_exe` (`gta.cpp:141`) | **No** |
| `backup_original/src/config.cpp` | — | Second INI reader containing the same PS2 keys (`config.cpp:248` `usePCTimecyc`) | **No** |
| `backup_original/src/texdb.cpp` | — | `TexInfo` (detail/stochastic/dualPass/zwriteThreshold) consumed by building PS selection | **No** |
| `backup_original/src/skygfx.h` | — | Enums `CAR_PS2…NUMCARPIPES` (`:41-55`), `BUILDING_PS2/ XBOX/ GTAIV` (`:57-63`), CSO externs (`:471`) | **No** |

**Files that do NOT exist in `backup_original/`:** `ps2_mode.cpp`, `xbox_mode.cpp`, `xboxPipe.cpp`, `ps2Pipe.cpp`, `iv_mode.cpp`, `mobile.cpp`, `pc_patched_mode.cpp`. Legacy PS2/Xbox logic is entirely inside `buildingPipe.cpp` + `vehiclePipe.cpp` + `main.cpp` config/hooks; our per-mode `.cpp` files are a **new architecture** with no aap ancestor.

**Second aap snapshot warning:** `memory-bank/.cache/external-repos/aap-skygfx/src/` is an **older** copy (818 lines `buildingPipe.cpp`, 1481 `vehiclePipe.cpp`) and differs materially (no wind VS, no `xboxBuildingStochasticPS`, no `simpleDetailStochasticPS`, and `TagRenderCB` uses `buildingPipe`). Do not use it as the legacy reference; `backup_original/` is newer/canonical for this audit.

### 1.2 Function inventory — building

| Function | Location | What it does |
|---|---|---|
| `CustomBuildingPipeline__Update` | `backup_original/src/buildingPipe.cpp:47-66` | Recomputes `buildingAmbient` each frame: `CTimeCycle_GetAmbient*() * CCoronas__LightsMult` (`:57-59`); lightning override (`:61-62`) |
| `CustomBuildingEnvMapPipeline__SetupEnv` | `:71` | Builds the building env-map matrix from atomic/clump/camera |
| `setDnParams` | `:103-130` | Uploads `REG_dayparam` (c30) / `REG_nightparam` (c31); forces night=1 when the non-DN pipe is used |
| `TagRenderCB` | `:147-163` | Special blend for tag atomics; PS2 branch uses `tagsBuildingPipe` (`:151`) and SRC=ONE/DST=ZERO (`:152-153`) |
| `CCustomBuildingDNPipeline__CustomPipeRenderCB_PS2` | `:166-364` | PS2 building callback: WVP, DN, env matrix, 7-light PS upload, per-mesh `colorScale=255/128` for `ps2ModulateBuilding` (`:234`), wind VS selection (`:271-273`), PS2 detail/stochastic PS selection (`:275-316`), `D3D9RenderDual` (`:324`), additive env/spec pass with `ps2BuildingFxVS` + `ps2EnvSpecFxPS` (`:340-355`) |
| `CCustomBuildingDNPipeline__CustomPipeRenderCB_Xbox` | `:368-511` | Xbox callback: WVP, DN, env matrix, wind VS (`:419`), per-mesh vertex alpha (`:440-446`), matCol/surfProps only when `rpGEOMETRYLIGHT` (`:450-458`), env PS / detail PS / stochastic PS (`:461-497`), `D3D9RenderDual` (`:504`) |
| `CCustomBuildingDNPipeline__CustomPipeRenderCB_Sphere` | `:515-590` | Sphere-map building callback (`sphereBuildingVS` + `simpleFogPS`) |
| `CCustomBuildingDNPipeline__CustomPipeRenderCB_Switch` | `:592-608` | Dispatches `BUILDING_PS2` (`:601`), `BUILDING_XBOX` (`:605`); no cull/alpha/ZWRITE wrapper in aap |
| `reinstance` | `:611-616` | Thin wrapper over the D3D9 instance callback |
| `isNightColorZero` / `GetVertColourPtr` / `GetExtraVertColourPtr` / `GetInstVertexColors` / `GetInstExtraColors` | `:618-656` | Day/night vertex-colour plugin accessors |
| `instWhite` | `:658-665` | Fills missing prelight colours white |
| `DNInstance_PS2` | `:667-842` | Builds a custom vertex declaration with **two** COLOR streams, instances day (COLOR1) and night (COLOR0) sets, falls back day→night |
| `CCustomBuildingPipeline__CreateCustomObjPipe_PS2` | `:845-873` | Non-DN building pipe; pluginId `RSPIPE_PC_CustomBuilding_PipeID`; instance CB `DNInstance_PS2`; render CB `..._Switch` |
| `CCustomBuildingDNPipeline__CreateCustomObjPipe_PS2` | `:876-904` | DN building pipe; pluginId `RSPIPE_PC_CustomBuildingDN_PipeID` |
| `CCustomBuildingRenderer__IsCBPCPipelineAttached` | `:906-924` | Decides whether an atomic belongs to the custom building pipe |
| `hookBuildingPipe` | `:927-940` | Intercepts `0x53C15E`, injects `0x5D7100` / `0x5D7D90`, patches `0x5D7200` (disable interpolation), optional `0x5D7F40` |

### 1.3 Function inventory — vehicle

| Function | Location | What it does |
|---|---|---|
| `CustomEnvMapPipeAtomicData::operator new` / `AllocEnvMapPipeAtomicData` | `backup_original/src/vehiclePipe.cpp:44-70` | Pool-backed per-atomic env2 data |
| `CCustomCarEnvMapPipeline__Init` | `:73-125` | Creates `reflectionTex`, env camera/rasters, `CreateShaders`, env1/env2 frames, `envtexmat` |
| `CCustomCarEnvMapPipeline__PreRenderUpdate` | `:128-141` | Recomputes view/env inverses and light direction each frame |
| `Env1Xform` / `Env2Xform` | `:143-227` | Per-atomic env1/env2 UV transforms (cached by last object/frame) |
| `Env1Xform_PC` / `Env2Xform_PC` | `:229-305` | FFP-compatible variants used by the Xbox path |
| `SetupEnv` | `:306-329` | Produces 3x3 env matrix for the current atomic |
| `SetupSpec` | `:330-367` | Produces specular matrix + light dir |
| `uploadLights` | `:345-360` | Uploads `pAmbient->color` to `REG_ambient` (`:348`), `pDirect` gated by `rpLIGHTLIGHTATOMICS` (`:349`), plus 6 extras (`:351-359`) |
| `uploadNoLights` | `:362-366` | Zeros c4 + 2×7 light registers |
| `CCustomCarEnvMapPipeline__CustomPipeRenderCB_PS2` | `:369-551` | PS2 vehicle callback: WVP, 7-light local upload, matCol/surfProps, base VS/PS (`:458-459`), `D3D9RenderDual` (`:462`), MatFX env1/env2/spec pass with `ps2CarFxVS` + `ps2EnvSpecFxPS` (`:487-541`) |
| `CCustomCarEnvMapPipeline__CustomPipeRenderCB_Specular` | `:554-740` | "Spec" vehicle callback (legacy PC-ish) |
| `SetupMaterial_Xbox` | `:742-771` | FFP-style material setup; raw `RwSurfaceProperties` upload to `LOC_surfProps` (`:769-770`) |
| `CCustomCarEnvMapPipeline__CustomPipeRenderCB_Xbox` | `:774-944` | Xbox vehicle callback: world/view/proj + worldIT + eye + sunDir/sunDiff/**pAmbient** (`:811-816`), env switch (`:834-917`), FFP texture stages 0/1/2 (`:921-935`), `D3D9RenderDual` (`:936`) |
| `..._leeds` / `..._mobile` / `..._Env` | `:949-1398` | LCS/VCS, Mobile, and Env/PBR vehicle callbacks |
| `..._CustomPipeRenderCB_Switch` | `:1400-1434` | Dispatch: `CAR_PS2` (`:1403`), `CAR_PC` (`:1407`), `CAR_XBOX` (`:1410`), `CAR_SPEC` (`:1413`), `CAR_NEO` (`:1416-1418`), LCS/VCS (`:1419-1421`), Mobile (`:1423-1427`), Env (`:1429-1430`) |
| `setVehiclePipeCB` | `:1437-1441` | Installs `CCustomCarEnvMapPipeline__Init` + the Switch as the D3D9 render callback |
| `hookVehiclePipe` | `:1468-1491` | Injects `0x5D9FE9`, intercepts `0x5D5B10`, injects wheel callback `0x7323C0` |

### 1.4 Function inventory — Neo / Env / envmap / core support

| Function | Location | What it does |
|---|---|---|
| `CarPipe::CreateShaders` | `backup_original/src/neoCarpipe.cpp:206-223` | Loads `IDR_NEOVEHICLEONEVS` + `IDR_NEOVEHICLETWOVS` |
| `CarPipe::RenderEnvTex` | `:121-185` | Renders the Neo reflection cubemap/tex |
| `CarPipe::DiffusePass` / `SpecularPass` | `:296-369` / `:371-425` | Neo two-pass draw |
| `CarPipe::RenderCallback` | `:427-463` | Neo pipe entry point |
| `MakeEnvmapRasters` / `MakeEnvmapCam` | `backup_original/src/envmap.cpp:18-48` | Env-map rasters/camera |
| `RenderReflectionScene` | `:241-257` | Renders the mirror/env scene |
| `DrawEnvMapCoronas` | `:319-343` | Draws coronas into the env map |
| `RenderReflectionMap_leeds` | `:357-388` | Leeds/VCS reflection variant |
| `envmaphooks` | `:471-482` | Hooks `CEntity::GetIsOnScreen` (6 sites) and `CRenderer__ConstructRenderList` |
| light/material upload helpers | `backup_original/src/pipelinecommon.cpp:195-292` | `pipeUploadMatCol` (`:195`), `pipeUploadZero/PS` (`:207/214`), `pipeUploadLightColor/PS` (`:221/235`), `pipeUploadLightDirection/Local/PS` (`:249/279/264`) — the `rpLIGHTLIGHTATOMICS` gate that D4 is about lives at `:224` |
| `D3D9Render` | `backup_original/src/main.cpp:211-217` | Draw indexed/primitive |
| `D3D9RenderDual` | `:219-260` | Dual-pass alpha/Z-write split |
| `D3D9RenderBlack_DUAL` / `D3D9RenderDefault_DUAL` | `:255-302` | Dual-pass adapters for the game's default/black callbacks |
| corona Z-test patch | `:330-346` | `coronaZtest`: 0 = NOP the PC z-test (`0x6FB17C`), 1 = force PC call, −1 = leave |
| INI building map | `:1272-1277` | `PS2`, `PC` **and** `Xbox` all map to `BUILDING_XBOX`/`BUILDING_PS2` |
| INI vehicle map | `:1289-1296` | includes `Spec`, `Leeds`, `LCS`, `VCS`, `Mobile`, `Env` |
| Hook install | `:1890-1894` | `hookBuildingPipe()` / `hookVehiclePipe()` behind `iCanHas*` flags |

### 1.5 What our tree actually has (`src/render/`)

| File | Lines | Status |
|---|---:|---|
| `src/render/buildingPipe.cpp` | 2941 | PS2 cb `:1023`, Xbox cb `:1452`, Sphere `:1676`, PBR `:1817`, Switch `:2479`, DN instance `:2650`, pipe creation `:2834`/`:2865`, hooks `:2913` |
| `src/render/vehiclePipe.cpp` | 3398 | PS2 cb `:896`, Spec `:1176`, Xbox `:1390`, Leeds `:1641`, Mobile `:1820`, Env/PBR `:2012`, Switch `:3150`, init `:182`, hooks `:3338` |
| `src/render/pipelinecommon.cpp` | 1805 | `GetTimecycleAmbient` `:190`, `GetTimecycleAmbientPBR` `:215`, `CreateShaders` legacy handles `:902-945` |
| `src/render/envmap.cpp` | 975 | rasters `:67`, temporal blend `:117`, reflection scene `:392`, coronas `:472`, sphere reflections `:855`, hooks `:963` |
| `src/render/neoCarpipe.cpp` | 812 | `CreateShaders` **nulls both Neo VS** `:475-486`, `RenderCallback` `:758` |
| `src/render/ps2_mode.cpp` | 109 | **Dead** preset shim (`PS2Mode_ApplyDefaults` `:65`) |
| `src/render/xbox_mode.cpp` | 105 | **Dead** preset shim (`XboxMode_ApplyDefaults` `:61`) |
| `src/render/iv_mode.cpp` | 79 | Gutted (only raises `ivMode`, `:63-71`) |
| `src/render/mobile.cpp` / `pc_patched_mode.cpp` / `custom_mode.cpp` | 105 / 106 / 171 | Same dead-preset pattern |
| `src/core/modeManager.cpp` | 294 | Live profiles: `profileGTAIV` `:61`, `profilePS2` `:125-132`, `profileXbox` `:134-141` |

---

## 2. Feature parity matrix

Legend — **ported**: behaviourally equivalent path exists; **partial**: path exists but a sub-feature is missing/disabled; **divergent**: implemented differently than aap (may be intentional); **missing**: no equivalent.

| Feature | aap legacy implementation (file:line) | Rockstar original (file:line) | Current mod (file:line) | Status | Action |
|---|---|---|---|---|---|
| **Building PS2 pipe** | `backup_original/src/buildingPipe.cpp:166-364` (cb), `:601` (dispatch), `:876` (pipe) | No PS2 renderer in this source drop; nearest `pipes/PC/CustomBuildingPipeline.cpp:750-908` (FFP) | `src/render/buildingPipe.cpp:1023-1363`, dispatch `:2563-2568`, pipe `:2865` | **ported** (shader-for-shader + state hardening) | Keep; wire mode defaults |
| **Building Xbox pipe** | `backup_original/src/buildingPipe.cpp:368-511`, dispatch `:605` | `pipes/Xbox/CustomBuildingPipeline_Xbox.cpp:751-909`, env stage `:817-826`, vertex alpha `:872/876`, create `:918` | `src/render/buildingPipe.cpp:1452-1672` | **divergent** (aap has no direct-sun term; current adds one `:1574-1575`, shader `shaders/vs/xboxBuildingVS.hlsl:13-14,65-76`) | Gate the direct-sun term for parity mode |
| **Vehicle PS2 pipe** | `backup_original/src/vehiclePipe.cpp:369-551`, dispatch `:1403` | No PS2 renderer; nearest `pipes/PC/CustomCarEnvMapPipeline.cpp:1231-1594` (FFP) | `src/render/vehiclePipe.cpp:896-1173`, dispatch `:3203-3205` | **partial** (draw path ported; lighting source diverged) | Restore aap ambient/sun gating |
| **Vehicle Xbox pipe** | `backup_original/src/vehiclePipe.cpp:774-944`, dispatch `:1410` | Xbox1 cb `pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1087`; D3D9 cb `:1584-1918`; env passes `:1657-1810`; create `:1960` | `src/render/vehiclePipe.cpp:1390-1636`, dispatch `:3209-3211` | **partial** (draw path ported; ambient + interior dampening diverged) | Restore aap ambient; keep state fixes |
| **Neo / Env vehicle pipe** | `backup_original/src/neoCarpipe.cpp:427-463` (Neo cb), `:206-223` (VS load); Env cb `backup_original/src/vehiclePipe.cpp:1246-1398` | No Neo; nearest mobile env `pipes/MBR/CustomCarEnvMapPipeline.cpp:809` (OpenGL) | Neo: `src/render/neoCarpipe.cpp:475-486` (VS = NULL), `:758` (cb); dispatch reroutes `CAR_NEO` to Env/PS2 `src/render/vehiclePipe.cpp:3214-3223`; Env cb `:2012-3146` | **Neo missing; Env ported** | Decide: restore Neo resources, or formally deprecate Neo and relabel the mode/preset |
| **Dual pass** | `backup_original/src/main.cpp:219-260`; building `buildingPipe.cpp:324/504`; vehicle `vehiclePipe.cpp:462/637/936` | Xbox-only, and **disabled** (`pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1239-1455` is inside `#if 0`); PC has none | `src/core/main.cpp:208-259` (hardened, threshold clamp 254, two-layer alpha/Z helpers); building `1252`/`1650`; vehicle `1083`/`1246`/`1601` | **ported + hardened** | None; document 254 clamp |
| **DN (night vertex colours)** | `setDnParams` `buildingPipe.cpp:103-130`; `DNInstance_PS2` `:667-842`; `GetInstExtraColors` `:650-656` | `pipes/PC/CustomBuildingDNPipeline.cpp:283-331` (`UpdateAtomicDN`), `:575` (`CustomPipeInstanceCB`), `:1174-1347` (cb), `:1513-1576` (`UpdateAtomicPrelitLum`); Xbox `:284-354`, `:576`, `:1175-1348` | `setDnParams` `src/render/buildingPipe.cpp:228-286` (adds cutscene leak repair `:240-277`); `DNInstance_PS2` `:2650-2831` (bounds-checked COLOR1 scan `:2779-2811`) | **ported + divergent** (cutscene repair, same idea as Rockstar CPU blend) | Keep repair; optional parity flag |
| **Env map (building)** | `CustomBuildingEnvMapPipeline__SetupEnv` `buildingPipe.cpp:71-101`; FX pass `:327-355` | FFP stage 1 in `pipes/Xbox/CustomBuildingPipeline_Xbox.cpp:817-826` | Setup `src/render/buildingPipe.cpp:196`; FX pass `:1254-1350` (3-layer state restore) | **ported + hardened** | Keep |
| **Env map (vehicle)** | `SetupEnv` `vehiclePipe.cpp:306-329`; `SetupSpec` `:330-367`; `Env1Xform/Env2Xform` `:143-227`; PCs `:229-305` | `_rpMatFXSyncEnvMap1Data` `pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:566`; env stage `:1730-1760` | `SetupEnv` `src/render/vehiclePipe.cpp:489`, `SetupSpec` `:510`, `Env1/2` `:322/347`, PCs `:404/424`; PS2 branches `:1109-1146`; Xbox branches `:1513-1579` | **ported + hardened** (null-guards, pool-exhaustion skip) | Keep |
| **Specular** | PS2 `vehiclePipe.cpp:516-520` + `SetupSpec` `:330`; Xbox `SetupMaterial_Xbox` `:742-771` | Xbox D3D9 spec `pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1740-1760`; PC `pipes/PC/CustomCarEnvMapPipeline.cpp:1469-1486` | PS2 `src/render/vehiclePipe.cpp:1142-1146`; Xbox `SetupMaterial_Xbox` `:1342-1388` | **ported** | None (note `SetupMaterial_Xbox` by-name upload is equivalent to aap's raw struct — the code comment claiming a diffuse/spec swap is misleading) |
| **Vertex alpha** | Building PS2 `buildingPipe.cpp:252-259`; Xbox `:440-446`; vehicle `:441` | Xbox building `_rwD3D9RenderStateVertexAlphaEnable` `:872/876`; DN `:1311/1315` | Building `1172-1179` / `1599-1605`; vehicle `1486-1493` | **ported** | None |
| **Alpha test** | `TagRenderCB` `buildingPipe.cpp:147-163`; `D3D9RenderDual` `main.cpp:219-260` | Xbox building FFP alpha state `pipes/Xbox/CustomBuildingPipeline_Xbox.cpp:860-880` | `TagRenderCB` `src/render/buildingPipe.cpp:320-338` (branches on `buildingPipe`, not `tagsBuildingPipe`); `D3D9RenderDual` hardened `src/core/main.cpp:208-259` | **divergent** (tag pipe selection) | Restore `tagsBuildingPipe` or sync it on every mode change |
| **Cull** | Not touched in PS2/Xbox cbs (engine state) | Not touched in cbs | `pipeEnterCullMode`/`pipeExitCullMode` in both Switches (`buildingPipe.cpp:2484/2589`, `vehiclePipe.cpp:3158/…`); sanitizes saved mode, preserves FRONT/NONE | **hardening only** (behaviour preserved) | None; keep the FRONT/NONE exemption for mirror/shadow passes (`pipelinecommon.cpp:1292-1299`) |
| **Fog** | Building save/restore `buildingPipe.cpp:218-227`, `348-350`; vehicle `vehiclePipe.cpp:419-425`, `534-536` | Xbox dual-pass fog save/restore is dead code (`pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1431-1455` inside `#if 0`) | Building `src/render/buildingPipe.cpp:1146-1147`, `1304-1321`; vehicle FX helper `src/render/vehiclePipe.cpp:674+` | **ported + hardened** (all 3 layers) | None |
| **Z-write** | FX sets FALSE, restores building snapshot / vehicle hard TRUE (`buildingPipe.cpp:344-351`, `vehiclePipe.cpp:530-537`) | Dead dual-pass block toggles ZWRITE (`:1393-1450`) | `vehiclePipe_fxAdditiveBlend` `src/render/vehiclePipe.cpp:674+`; Switch forces TRUE on entry (`vehiclePipe.cpp:3171-3174`, `buildingPipe.cpp:2514-2517`) | **ported + new entry force** | Keep; log when a legitimate pass enters with ZWRITE off |
| **Texture stages** | Xbox vehicle stage 0/1/2 `vehiclePipe.cpp:921-935`; PS2 cleanup `:543-550` | Xbox `:1584-1918`; PC `:1380-1486` | Xbox `src/render/vehiclePipe.cpp:1586-1600`; PS2 cleanup `:632-665`; deterministic stage1/2 disable `:1496-1504` | **ported + hardened** | Keep |
| **Shader constants / registers** | Building `buildingPipe.cpp:20-40` (c0,c4,c5,c12,c19,c20,c21,c22,c29,c30,c31,c32,c36,c37,c38); vehicle `vehiclePipe.cpp:6-23` (…c30,c31,c32,c35,c38; `REG_eye=35` collides with `REG_specmat=35`) | FFP for both original pipes; only DN has a VS (`pipes/PC/DNshader.h`, `pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:463-521` uses c0/c4/c5/c6) | Building `src/render/buildingPipe.cpp:64-94` adds `REG_xbDirectDir=6`; vehicle `src/render/vehiclePipe.cpp:25-45` fixes `REG_eye` 35→36 (shader `shaders/vehiclePipeVS.hlsl:191` expects c36) and adds `REG_envtexmat=40` | **divergent/additive (legacy collision fixed)** | Keep c36 fix; document c6; do not rename registers |
| **Lighting path** | Building `CTimeCycle * CCoronas::LightsMult` `buildingPipe.cpp:57-59`; PS2 vehicle `pAmbient->color` `vehiclePipe.cpp:348`; Xbox vehicle `pAmbient->color` `:816`; `rpLIGHTLIGHTATOMICS` gate `:349` | FFP lights (`D3DRS_LIGHTING` etc.; Xbox vehicle `pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1584+`) | Shared timecycle helper with sky clamp `src/render/pipelinecommon.cpp:97-213`; building `src/render/buildingPipe.cpp:182`; vehicle `src/render/vehiclePipe.cpp:540`; force sun `:557`; Xbox interior dampening `:1457-1467` | **divergent** (largest look difference) | Add a per-pipe legacy ambient path (Plan C1) and restore the atomics gate (C2) |
| **Corona handling** | `coronaZtest` patch `backup_original/src/main.cpp:330-346`; env-map coronas `backup_original/src/envmap.cpp:319-343`; `gpCoronaTexture` `:259` | Not in pipes (game owns corona render) | Patch `src/core/main.cpp:334-346`; env coronas `src/render/envmap.cpp:472`; corona shield hooks `src/core/main.cpp:3540-3650`, `7012-7036` | **ported + extended**, but **modes never set `coronaZtest`** | Set 0 for PS2, 1 for Xbox; call `resetValues()` after mode apply |
| **Stochastic/detail PS selection** (extra row, carries a critical finding) | Detail + stochastic `buildingPipe.cpp:275-316`; stochastic **without detail** `:310-315` / `:491-496` | No stochastic concept (FFP) | `buildingPipe_selectPS` `src/render/buildingPipe.cpp:940-959`: detail/stochastic path yes, **no `simpleStochasticPS` fallback**; no global/ID/load (`grep simpleStochasticPS` = 0 hits in `src/`) | **missing** | Restore `simpleStochasticPS` (Plan B1) |
| **Wind VS** (extra row) | `setWindParams` `buildingPipe.cpp:129-142`; PS2 wind `:271-273`; Xbox wind `:416-420` | N/A | `setWindParams` `src/render/buildingPipe.cpp:292-315` (adds `TreeWind_UploadWindExt` c23); PS2 `:1235-1240`; Xbox `:1580-1590`; shaders `shaders/vs/ps2BuildingWindVS.hlsl:1-24`, `shaders/vs/xboxBuildingWindVS.hlsl:7-16` | **ported + extended** (c23 tree sway) | Keep; note c23 is a fork extension, not aap |

---

## 3. Divergence list (severity + the specific regression each causes)

### D1 — CRITICAL — PS2/Xbox modes are stubs; the real preset code is dead
- Live: `src/core/modeManager.cpp:125-132` (`profilePS2`) and `:134-141` (`profileXbox`) write only `pipeline`, `buildingPipe`, `vehiclePipe`.
- Dead: `src/render/ps2_mode.cpp:65-103`, `src/render/xbox_mode.cpp:61-99`, plus their static presets (`ps2_mode.cpp:42`, `xbox_mode.cpp:39`). There is **no call site**: `PS2Mode_ApplyDefaults` / `XboxMode_ApplyDefaults` appear only in their definitions and `src/skygfx.h:1428-1432`.
- **Regression:** switching to PS2 keeps the current `colorFilter` (e.g. Modern under PBR), keeps `dualPass=1`, `detailMaps=1`, `stochastic=1`, `ps2Modulate=0`; switching to Xbox keeps `ps2Modulate=1` if the user came from PS2. The mode label lies. `preset` is also not synced (compare `profileGTAIV` `:73`), so the Style dropdown shows the old preset.
- **The other preset path is also incomplete:** `ApplyPreset` writes only the `PresetConfig` fields (`src/core/presets.cpp:127-156`), and `PresetConfig` has no `detailMaps`, `stochastic`, `envMapSize`, `doglare`, `neoWaterDrops` or `coronaZtest` (`src/skygfx.h:255-275`). So even `Style = SA Xbox` leaves the material/lighting knobs at whatever the user had. There are effectively **three** preset sources (mode files, mode manager profiles, `presetConfigs`) and none is complete for PS2/Xbox.

### D2 — CRITICAL — `simpleStochasticPS` path is gone
- aap: loaded `backup_original/src/pipelinecommon.cpp:379`; selected for stochastic-without-detail at `backup_original/src/buildingPipe.cpp:310-315` (PS2) and `:491-496` (Xbox); declared `backup_original/src/skygfx.h:471`.
- Current: `buildingPipe_selectPS` returns `simplePS` when there is no detail map (`src/render/buildingPipe.cpp:949-958`); no `simpleStochasticPS` symbol exists in `src/`; no `IDR_SIMPLESTOCHASTICPS` in `resources/resource.h`.
- The CSO is still produced by `tools/fast_build.py:440` and still present at `resources/cso/simpleStochasticPS.cso`, so this is purely a wiring/selection regression.
- **Regression:** every stochastic building material without a detail map loses its noise/tiling in **both PS2 and Xbox** modes — flat, repeating textures.

### D3 — HIGH — ambient source changed (sky clamp + multiplier + timecycle instead of aap's sources)
- aap building: `CTimeCycle_GetAmbient*() * CCoronas__LightsMult` (`backup_original/src/buildingPipe.cpp:57-59`).
- aap PS2 vehicle: `pAmbient->color` (`backup_original/src/vehiclePipe.cpp:348`); aap Xbox vehicle: `pAmbient->color` (`:816`).
- Current: `GetTimecycleAmbientPBR` → `GetTimecycleAmbient` (`src/render/pipelinecommon.cpp:190-213`) applies `CCoronas__LightsMult * ambientMult` (`:200-210`) and then `ClampAmbientToSky` (`:97-188`, called `:211`); used at `src/render/buildingPipe.cpp:182` and `src/render/vehiclePipe.cpp:540`.
- **Regression:** at night/dusk, when ambient luminance exceeds sky luminance, the clamp darkens ambient below aap; aap's Xbox building VS has no direct-light term (ambient + DN prelight + matCol only), so the world goes darker/contrastier than aap. `ambientMult` also has no aap equivalent.

### D4 — HIGH — PS2 vehicles force-upload the sun (ignores `rpLIGHTLIGHTATOMICS`)
- aap: `pipeUploadLightColor(pDirect, …)` (`backup_original/src/vehiclePipe.cpp:349`) zeros c5 when the game deactivates directional lighting (`backup_original/src/pipelinecommon.cpp:224`).
- Current: `pipeUploadLightColorForce(pDirect, REG_directCol)` at `src/render/vehiclePipe.cpp:557`.
- **Regression:** cars stay lit in interiors, cutscenes and scripted passes where vanilla deliberately turns the sun off; they no longer match the ambient level of buildings or the pre-aap reference.

### D5 — HIGH — Xbox building direct-sun term is a fork, not aap parity
- Current: `pipeUploadLightColorForce` + `pipeUploadLightDirectionForce` at `src/render/buildingPipe.cpp:1574-1575`; compiled shader `shaders/vs/xboxBuildingVS.hlsl:13-14` declares c5/c6 and `:65-76` adds `directContrib`.
- aap: no such upload or term in the Xbox cb (`backup_original/src/buildingPipe.cpp:394-458`).
- **Regression:** Xbox buildings are brighter at dawn/day than aap and can double-count lighting with prelight; the wind VS does not declare c5/c6 (`shaders/vs/xboxBuildingWindVS.hlsl:1-24`), so the upload is dead there. Keep as a bug-fix, but expose it as a parity toggle rather than unconditional.

### D6 — HIGH — legacy INI aliases no longer parse
- aap building map accepts `"PC"` (`backup_original/src/main.cpp:1273-1274`); current building map does not (`src/core/main.cpp:5076-5083` only `PS2/Xbox/GTAIV/PBR`).
- aap vehicle map accepts `"Spec"` (`backup_original/src/main.cpp:1292`); current accepts only `"Specular"` (`src/core/main.cpp:5087-5100`).
- **Regression:** existing `skygfx.ini` with `buildingPipe=PC` / `vehiclePipe=Spec` is silently ignored (`StrAssoc::get` returns −1, `:5083-5085` / `:5101-5103`), then `derivePipelineFromPipes` (`src/core/main.cpp:607-630`) derives from whatever pipe was left — often PBR — so the user gets a completely different renderer.

### D7 — MEDIUM — `tagsBuildingPipe` is dead
- aap: `TagRenderCB` branches on `config->tagsBuildingPipe` (`backup_original/src/buildingPipe.cpp:151`).
- Current: branches on `config->buildingPipe` (`src/render/buildingPipe.cpp:325`); `tagsBuildingPipe` is written once at INI load (`src/core/main.cpp:5338`) and never again (mode profiles do not touch it).
- **Regression:** tag atomics cannot use a different pipe; if we restore `tagsBuildingPipe`, mode switching must sync it or PS2 tags will still use whatever stale value was loaded.

### D8 — MEDIUM — Xbox mode preset disables stochastic texturing
- `src/render/xbox_mode.cpp:94` sets `c->stochastic = 0` (detailMaps=1 at `:93`).
- aap first-run template: `detailMaps=1`, `stochasticTexturing=1` (`backup_original/src/main.cpp:1476-1477`).
- **Regression:** if the dead preset is wired as-is, all stochastic building surfaces render flat in Xbox mode. (`presets.cpp:43-46` does not carry a stochastic field, so this is the only Xbox lookup.)

### D9 — MEDIUM — mode changes don't run `resetValues()` / no listener
- `ModeManager_SetMode` applies the profile and calls `notifyListeners` (`src/core/modeManager.cpp:281-293`); the listener API exists (`:225`) but **no listener is registered** anywhere (`grep ModeManager_RegisterListener` = definition only).
- The ImGui mode rows call `resetValues()` manually (`src/core/debugmenu_ui.cpp:2703-2704`); any programmatic mode change (preset path, future keybind, MCP) does not.
- **Regression:** `doRadiosity`, `grainFilter`, `coronaZtest` and the other `resetValues` consumers (`src/core/main.cpp:294-346`) stay stale after a mode change until something else calls it.

### D10 — MEDIUM — `envMapSize` deltas are uncoordinated
- `src/render/ps2_mode.cpp:101` proposes 128; `src/render/xbox_mode.cpp:97` proposes 256; aap default is 256 (`backup_original/src/main.cpp:1315`).
- `MakeEnvmapRasters` re-creates rasters on size mismatch (`src/render/envmap.cpp:39-67`) and is called per reflection pass (`:561`), so a change is handled but may hitch one frame and can leave NULL rasters on failure (`:562-566` handles it).
- **Regression:** if wired, PS2 reflections are lower resolution than aap's default; not a correctness bug, but it breaks "same look" expectations.

### D11 — LOW — Xbox vehicle interior dampening is a fork
- Current: `src/render/vehiclePipe.cpp:1457-1467` multiplies ambient by 0.55 when sheltered and not interior.
- aap: uploads `pAmbient->color` untouched (`backup_original/src/vehiclePipe.cpp:816`).
- **Regression:** cars in garages/under cover are ~45% darker than aap. It may be desirable, but it is not parity.

### D12 — LOW — render-state hardening changes aap semantics
- Entry ZWRITE force: `src/render/vehiclePipe.cpp:3171-3174`, `src/render/buildingPipe.cpp:2514-2517`.
- Alpha force: `pipeForceAlphaBlock()` `src/render/vehiclePipe.cpp:3201`; cull sanitize `src/render/pipelinecommon.cpp:1285-1338`; TFACTOR/sampler restore `src/render/vehiclePipe.cpp:1621-1633`; texture-address restore `src/render/buildingPipe.cpp:1337-1349`.
- **Regression:** none observed; these fix postfx raw-state leaks. The only risk is a pass that legitimately enters with ZWRITE off (mirror/reflection) and is forced on for the cb duration. Keep, but log it (already done at `:3175-3179` / `:2510-2512`).

### D13 — LOW — stale duplicate shader sources
- Root `shaders/vs/xboxBuildingVS.hlsl` declares c5/c6; `shaders/vs/xbox/xboxBuildingVS.hlsl` does **not** (diff confirms only the root has them). Root `shaders/vs/xboxBuildingWindVS.hlsl` has c23 windExt; subdir copy does not. Same pattern for `shaders/vs/ps2/ps2Building*`.
- The build prefers root stems and skips colliding subdir files (`tools/fast_build.py:360-392`), so today the correct files compile.
- **Regression:** future edits to the subdir copies are silent no-ops; if the priority rule is ever changed, Xbox loses its direct-sun term and both wind shaders lose the tree-sway extension.

### D14 — LOW — legacy `REG_eye` register collision was fixed (keep)
- aap `REG_eye=35` (`backup_original/src/vehiclePipe.cpp:23`) collides with `REG_specmat=35` (`:19`), yet aap uploads eye into it at `:614`, `:1006`, `:1137`.
- Current moves `REG_eye` to c36 (`src/render/vehiclePipe.cpp:35`) and uploads it at `:1234`, `:1699`, `:1888`; the specular VS declares `eyePos_spec : register(c36)` (`shaders/vehiclePipeVS.hlsl:191`). `REG_envtexmat=40` is added at `src/render/vehiclePipe.cpp:38` for the Leeds VS.
- **Regression:** none; this is a correctness fix over aap. It is listed as a divergence only so it is not "fixed back" to aap's colliding c35.

### D15 — LOW — mode-file documentation points at the wrong lines
- `src/render/ps2_mode.cpp:28-31` cites `buildingPipe.cpp:188-386` / `vehiclePipe.cpp:444-632`; the aap callbacks start at `buildingPipe.cpp:166` and `vehiclePipe.cpp:369`.
- `src/render/xbox_mode.cpp:28-31` cites `buildingPipe.cpp:390-533` / `vehiclePipe.cpp:861-1037`; actual aap Xbox callbacks are `:368-511` and `:774-944`.
- `src/render/xbox_mode.cpp:30` cites `postfx.cpp:24` for `COLORFILTER_PC`; the aap PC case is `postfx.cpp:1298`.
- **Regression:** none at runtime; actively misleads the port work.

### D16 — LOW (out of PS2/Xbox scope) — Neo cannot render
- aap: `CarPipe::CreateShaders` loads two Neo VS (`backup_original/src/neoCarpipe.cpp:206-223`), dispatch calls `CarPipe::RenderCallback` (`backup_original/src/vehiclePipe.cpp:1416-1418`).
- Current: `CreateShaders` sets both VS to `nullptr` (`src/render/neoCarpipe.cpp:475-486`); dispatch reroutes `CAR_NEO` to Env or PS2 (`src/render/vehiclePipe.cpp:3214-3223`).
- **Regression:** "Neo" preset/mode no longer uses the Neo two-pass pipe; it silently renders as PBR Env. Relevant to the matrix because the Neo/Env row cannot be called "ported" until this is decided.

---

## 4. Port plan — ordered tasks to finish PS2 and Xbox modes

Each task is scoped to exact files. "Risk" = chance of visual/regression impact, not implementation difficulty.

### Phase A — make the modes real (do first; unblocks everything else)

**A1. Replace `profilePS2` / `profileXbox` stubs with the dead preset bodies.**
- Source: `src/render/ps2_mode.cpp:42-103`, `src/render/xbox_mode.cpp:39-99`.
- Destination: `src/core/modeManager.cpp:125-141` (the two static functions), **and** `src/core/presets.cpp:108-156` (`ApplyPreset`) if the Style combo is to stay a second entry point. Copy every field write; add `c->preset = PRESET_SA_PS2` / `PRESET_SA_XBOX` (enum lines `src/skygfx.h:231-232`; preset rows `src/core/presets.cpp:43-46`), `c->colorFilter`, `c->tagsBuildingPipe = c->buildingPipe`, and `c->coronaZtest`.
- Also delete (or convert to thin wrappers around the mode profile) `src/render/ps2_mode.cpp` / `xbox_mode.cpp` so there is **one** source of truth; leaving dead copies guarantees future drift.
- **Risk: medium.** `envMapSize` changes cause one-time raster recreation (`src/render/envmap.cpp:39-67`, `:561`); test a mode switch in the rain/mirror scene.

**A2. Register a mode listener that pushes config into the renderer.**
- Source: `ModeManager_RegisterListener` (`src/core/modeManager.cpp:225`).
- Destination: `installDeferredHooks` or `InjectDelayedPatches` in `src/core/main.cpp` (next to the existing `resetValues` usage at `:294-346` and the hook install at `:6731-6738`).
- Listener body: `resetValues();` (applies `doRadiosity`, `grainFilter`, `coronaZtest`), `config->tagsBuildingPipe = config->buildingPipe;`, and log the new pipes. Do **not** call `ReleaseEnvMapResources` unless `envMapSize` actually changed.
- **Risk: low–medium.** Re-entrancy: `ModeManager_SetMode` already calls listeners after writing `*config` (`:281-293`).

**A3. Mode-specific filter/corona values.**
- PS2: `COLORFILTER_PS2`, `coronaZtest=0`; Xbox: `COLORFILTER_PC`, `coronaZtest=1`.
- Destination: `src/core/modeManager.cpp:125-141`; the corona patch lives in `src/core/main.cpp:334-346` and is idempotent.
- **Risk: low.** Coronas are a visible, easy-to-compare signal; screenshot before/after.

### Phase B — restore missing aap material paths

**B1. Re-add `simpleStochasticPS`.**
- Source: `backup_original/src/pipelinecommon.cpp:379` (load), `backup_original/src/buildingPipe.cpp:310-315` / `:491-496` (selection), `backup_original/src/skygfx.h:471` (extern).
- Destination:
  - `src/render/buildingPipe.cpp:23` add `void *simpleStochasticPS;`
  - `src/render/pipelinecommon.cpp:902-945` add `makePS(IDR_SIMPLESTOCHASTICPS, &simpleStochasticPS);`
  - `resources/resource.h` add `#define IDR_SIMPLESTOCHASTICPS` (pick a free ID — 279 is the highest defined, 278 is documented free at `resources/resource.h:134`; legacy's numeric value is not recoverable because `backup_original/` contains no `.rc`/`resource.h`)
  - `resources/resource.rc` add `IDR_SIMPLESTOCHASTICPS RCDATA "cso/simpleStochasticPS.cso"`
  - `src/render/buildingPipe.cpp:940-959` add the no-detail fallback:
    `if(texinfo->stochastic && config->stochastic) return simpleStochasticPS;` before `return simplePS;`
- The CSO is already produced (`tools/fast_build.py:440`) and exists on disk.
- **Risk: low.** One PS swap; no hook/ABI impact.

**B2. Restore INI aliases.**
- `src/core/main.cpp:5076-5083`: add `{"PC", BUILDING_XBOX}` (aap alias, `backup_original/src/main.cpp:1273-1274`).
- `src/core/main.cpp:5087-5100`: add `{"Spec", CAR_SPEC}` (aap alias, `backup_original/src/main.cpp:1292`).
- **Risk: low.** Only affects previously-ignored strings; test `buildingPipe=PC` does not fight `derivePipelineFromPipes` (`src/core/main.cpp:607-630`).

**B3. Restore the `tagsBuildingPipe` contract (or delete the field).**
- Source: `backup_original/src/buildingPipe.cpp:147-163`.
- Destination: `src/render/buildingPipe.cpp:320-338` — branch on `config->tagsBuildingPipe` again; A2 keeps it synced with `buildingPipe`.
- **Risk: low** (tag atomics only), but visible in PS2 mode: aap uses `SRC=ONE/DST=ZERO` for PS2 tags, current always normal alpha (`:325-329`).

### Phase C — lighting parity (the visual core of the modes)

**C1. Add a legacy ambient source and use it for PS2/Xbox.**
- Source: `backup_original/src/buildingPipe.cpp:57-59`; `backup_original/src/vehiclePipe.cpp:348` / `:816`.
- Destination: `src/render/pipelinecommon.cpp` — add `RwRGBAReal GetLegacyAmbient(void)` (timecycle × `CCoronas__LightsMult`, **no** `ClampAmbientToSky`, **no** `ambientMult`), then:
  - `src/render/buildingPipe.cpp:182`: `buildingAmbient = (config->buildingPipe == BUILDING_PS2 || config->buildingPipe == BUILDING_XBOX) ? GetLegacyAmbient() : GetTimecycleAmbientPBR();`
  - `src/render/vehiclePipe.cpp:540`: same conditional on `config->vehiclePipe == CAR_PS2 || config->vehiclePipe == CAR_XBOX`.
- Keep the PBR/Env/Mobile/IV paths on `GetTimecycleAmbientPBR` untouched.
- **Risk: high** — shared lighting; but the branch is pipe-gated and testable against aap reference screenshots at 01:28 (night), 06:30 (dawn), 12:00, 20:30 (dusk).

**C2. Restore the `rpLIGHTLIGHTATOMICS` gate for PS2/Xbox vehicles.**
- Source: `backup_original/src/vehiclePipe.cpp:348-349` (`pipeUploadLightColor`, not Force).
- Destination: `src/render/vehiclePipe.cpp:556-558`:
  - parity modes: `pipeUploadLightColor(pDirect, REG_directCol)` + `pipeUploadLightDirectionLocal(...)`;
  - PBR/Env: keep `pipeUploadLightColorForce` + `pipeUploadLightDirectionForce`.
- **Risk: medium.** Cars darken in interiors/cutscenes; that is the correct aap behaviour, but it will look like a regression in a dark garage. Gate with the same pipe check as C1.

**C3. Make the Xbox direct-sun building term a parity toggle.**
- Source: current `src/render/buildingPipe.cpp:1574-1575`; shader `shaders/vs/xboxBuildingVS.hlsl:13-14,65-76`.
- Destination: add `RwBool xboxDirectLight` (default 0 for `MODE_XBOX`, 1 for the current "best look") in `src/skygfx.h` + INI (`src/core/main.cpp` reader/save) and gate both the C++ uploads and the shader term (compile-time `#define` is not enough if the mode is runtime; either use two CSOs or a PS/VS constant flag that multiplies `directContrib` by 0).
- **Risk: medium.** Touches the Xbox building VS/CSO; a bad edit makes every building black. Validate with the existing `[BPIPE] light` audit (`bpipeLightAuditLog` call `:1670`).

### Phase D — hygiene that protects the above

**D1. Delete or redirect the stale shader duplicates.**
- `shaders/vs/xbox/xboxBuildingVS.hlsl`, `shaders/vs/xbox/xboxBuildingWindVS.hlsl`, `shaders/vs/ps2/ps2BuildingVS.hlsl`, `shaders/vs/ps2/ps2BuildingWindVS.hlsl`, `shaders/vs/ps2/ps2BuildingFxVS.hlsl` (subdir copies are older; root wins at `tools/fast_build.py:360-392`).
- **Risk: low** today; prevents a future silent Xbox regression.

**D2. Fix mode-file documentation and remove dead code.**
- Replace the citations in `src/render/ps2_mode.cpp:27-31`, `xbox_mode.cpp:27-31`, `pc_patched_mode.cpp:29-31` with the real aap lines listed in §1.
- Remove `PS2Mode_GetPreset`/`XboxMode_GetPreset` if A1 deletes the files; otherwise keep them but mark them as data-only.
- **Risk: none** (comments/dead code).

**D3. Decide Neo.**
- Option 1 (parity): restore `IDR_NEOVEHICLEONEVS/TWOVS` resources + `CarPipe::CreateShaders` loading (legacy `backup_original/src/neoCarpipe.cpp:206-223`) and re-point `CAR_NEO` dispatch (`src/render/vehiclePipe.cpp:3214-3223`). Requires the deleted CSOs/RC entries — high risk.
- Option 2 (recommended): formally deprecate CarPipe, relabel the `Neo` row/preset to "Env (PBR)" and delete the dead shader members. No rendering change.
- **Risk: high (option 1) / low (option 2).**

### Phase E — verification gate (must pass before calling modes "done")

1. **Mode switch log:** with `skygfx_dbg.log`, confirm `[mode] ps2 -> xbox` and a new line listing `buildingPipe/vehiclePipe/colorFilter/ps2Modulate/dualPass/detailMaps/stochastic/envMapSize/coronaZtest`.
2. **Shader handles:** `CreateShaders` must log non-NULL `ps2BuildingVS`, `ps2BuildingFxVS`, `ps2BuildingWindVS`, `xboxBuildingVS`, `xboxBuildingPS`, `xboxBuildingStochasticPS`, `ps2CarFxVS`, `ps2EnvSpecFxPS`, `xboxCarVS` (`src/render/pipelinecommon.cpp:902-945`).
3. **Stochastic:** a stochastic building without detail map must bind `simpleStochasticPS` (add the pointer to the `[BPIPE]` shader-name map at `src/render/buildingPipe.cpp:767-786`).
4. **Lighting:** compare same spot/time/weather (01:28, 06:30, 12:00, 20:30) PS2 vs aap and Xbox vs aap. Record `buildingAmbient` and VS c4/c5 in the log.
5. **State:** `[BUILDING]`/`[VEHICLE]` ZWRITE/cull/alpha probes must not fire more than the usual throttle windows after 10 minutes of play (`bpipeBlendLeakProbe`, `pipeEnterCullMode` logs).
6. **INI back-compat:** a config with `buildingPipe=PC`, `vehiclePipe=Spec` must resolve to Xbox/Spec, not PBR.

---

## 5. Naming-convention alignment

Conventions to converge on:
- **Classes / files:** `C`-prefixed class names, PascalCase files; aap free functions use `CCustomBuildingDNPipeline__CustomPipeRenderCB_Xbox` (double underscore = free function emulating a member); Rockstar uses real members `class CCustomBuildingDNPipeline { … CustomPipeRenderCB(...) }` (`pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:1175`).
- **Bools:** `b` prefix (`bUseEnvMap1`, `bNoExtraPasses`, `bLightingEnabled` — `pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1199-1202`, `:1606`, `:1590`; `bInstanceUV` etc. — `pipes/Xbox/CustomBuildingPipeline_Xbox.cpp:191-193`).
- **Members:** `m_` / static members `ms_` (Rockstar `m_fDNBalanceParam`, `ms_extraVertColourPluginOffset` — `pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:59`, `:76`).
- **Xbox variants:** `_Xbox` suffix on classes/files/functions (`CustomBuildingPipeline_Xbox`, `CustomCarEnvMapPipeline_Xbox`, `CustomPipelinesCommonXbox`).
- **Building pipeline names:** `CCustomBuildingPipeline` / `CCustomBuildingDNPipeline` (not `buildingPipe_*` helpers).

### Rename table

Risk key: **safe** = internal/static, no address/name dependency; **medium** = extern across TUs or named in logs; **risky/forbidden** = exported ABI, INI keys, resource IDs, or game-wrapper semantics.

| Old | New | Rationale | Risk |
|---|---|---|---|
| `src/render/ps2_mode.cpp` | `src/render/PS2Mode.cpp` | PascalCase file, matches `CustomBuildingPipeline_Xbox.cpp` style | safe (build glob `premake5.lua:19`) |
| `src/render/xbox_mode.cpp` | `src/render/XboxMode.cpp` | same | safe |
| `PS2Mode_ApplyDefaults(Config*)` | `CPS2Mode::ApplyDefaults(Config*)` | C-prefixed class | safe (decl `src/skygfx.h:1428`; once A1 lands it is unused) |
| `XboxMode_ApplyDefaults(Config*)` | `CXboxMode::ApplyDefaults(Config*)` | same | safe |
| `PS2Mode_GetPreset()` | `CPS2Mode::GetPreset()` | same | safe |
| `ps2_preset` / `xbox_preset` | `CPS2Mode::ms_Preset` / `CXboxMode::ms_Preset` | `ms_` static member | safe |
| `profilePS2` / `profileXbox` (`src/core/modeManager.cpp:125/134`) | `CPS2Mode::ApplyProfile` / `CXboxMode::ApplyProfile` | profile is a mode behavior | safe (file-static) |
| `CCustomBuildingDNPipeline__CustomPipeRenderCB_PS2` | `CCustomBuildingDNPipeline::CustomPipeRenderCB_PS2` | real member, Rockstar naming | safe (file-local; dispatch `src/render/buildingPipe.cpp:2567`) |
| `...__CustomPipeRenderCB_Xbox` | `CCustomBuildingDNPipeline::CustomPipeRenderCB_Xbox` | same | safe |
| `...__CustomPipeRenderCB_Switch` | `CCustomBuildingDNPipeline::CustomPipeRenderCB_Switch` | same | safe (callback pointer only, `:2886`) |
| `DNInstance_PS2` (`:2650`) | `CCustomBuildingDNPipeline::CustomPipeInstanceCB_PS2` | Rockstar `CustomPipeInstanceCB` (`pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:576`) | safe (same TU) |
| `setDnParams` (`:228`) | `CCustomBuildingDNPipeline::SetDNParams` | member-style, `DN` acronym | safe (same TU) |
| `CCustomCarEnvMapPipeline__CustomPipeRenderCB_PS2` | `CCustomCarEnvMapPipeline::CustomPipeRenderCB_PS2` | Rockstar class naming | safe (file-local; dispatch `src/render/vehiclePipe.cpp:3204`) |
| `..._Xbox` | `CCustomCarEnvMapPipeline::CustomPipeRenderCB_Xbox` | same | safe |
| `..._Switch` | `CCustomCarEnvMapPipeline::CustomPipeRenderCB_Switch` | same | safe |
| `SetupMaterial_Xbox` (`:1342`) | `CCustomCarEnvMapPipeline::SetupMaterial_Xbox` | member-style | safe (same TU) |
| `buildingPipe_selectPS` (`:940`) | `CCustomBuildingPipeline::SelectPS` | Rockstar building pipeline class | safe (same TU) |
| `buildingPipe_saveRenderState` / `_restoreRenderState` / `_cleanup` | `CCustomBuildingPipeline::SaveRenderState` / `RestoreRenderState` / `Cleanup` | same | safe |
| `vehiclePipe_setupMaterial` / `_saveRenderState` / `_cleanup` / `_fxAdditiveBlend` | `CCustomCarEnvMapPipeline::SetupMaterial` / `SaveRenderState` / `Cleanup` / `FXAdditiveBlend` | same | safe (file-static) |
| locals `hasAlpha`, `hasEnv1`, `hasEnv2`, `hasSpec`, `noFx`, `lighting`, `blownUp`, `vertexAlphaIsAlpha`, `fxpass` | `bHasAlpha`, `bUseEnvMap1`, `bUseEnvMap2`, `bUseSpecular`, `bNoExtraPasses`, `bLightingEnabled`, `bBlownUp`, `bVertexAlphaIsAlpha`, `nFxPass` | Rockstar `b` convention (`pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1199-1202`) | safe (locals) |
| `buildingAmbient` (`src/render/buildingPipe.cpp:154`, extern in `src/render/treeWind.cpp:33`) | `CCustomBuildingPipeline::ms_BuildingAmbient` | `ms_` member | medium (extern across TUs) |
| `ps2BuildingVS`, `xboxBuildingVS`, `xboxCarVS`, `ps2EnvSpecFxPS` (`src/skygfx.h:1228`, `:1215-1227`) | `CCustomBuildingPipeline::ms_pPS2BuildingVS` … / `CCustomCarEnvMapPipeline::ms_pXboxCarVS` | static members | medium (extern in `pipelinecommon.cpp`, `buildingPipe.cpp`, `vehiclePipe.cpp`) |
| `renderingWheel` (`src/render/vehiclePipe.cpp`, extern `src/render/neoCarpipe.cpp:5`) | `CCustomCarEnvMapPipeline::ms_bRenderingWheel` | `ms_` + `b` | medium (shared extern) |
| `hookBuildingPipe` (`:2913`) | `CCustomBuildingRenderer::InstallHooks` | Rockstar renderer class | medium (called `src/core/main.cpp:6733`, named in logs) |
| `hookVehiclePipe` (`:3338`) | `CCustomCarEnvMapPipeline::InstallHooks` | same | medium |
| `CCustomCarEnvMapPipeline__CustomPipeRenderCB_exe` (`src/rw/gta.cpp:163`) | **do not rename** | It is a WRAPPER for the game's own function at `0x5D9900`; the name documents the original and is declared in `src/skygfx.h:1285` | **forbidden** |
| `CCustomBuildingDNPipeline__m_fDNBalanceParam` (`src/rw/gta.cpp:63`, `src/rw/gta.h:330`) | **do not rename** | Wrapper reference to game global `0x8D12C0`; renaming offers no benefit and touches every DN site | **risky/forbidden** |
| `Config::buildingPipe`, `vehiclePipe`, `colorFilter`, `ps2Modulate*`, `dualPass*`, `detailMaps`, `stochastic`, `envMapSize`, `grainFilter`, `radiosity`, `coronaZtest` | **do not rename** | INI keys + `X()` mirror list (`src/core/main.cpp:6512-6516`) + fixed-offset ABI comments (`src/skygfx.h:280-285`); `Config` is exported (`src/core/main.cpp:103`) | **forbidden** |
| Resource IDs / CSO filenames (`resources/resource.h`, `resources/resource.rc`, `resources/cso/*`) | **do not rename** | Embedded resources are referenced by numeric ID; renaming CSOs breaks `fast_build.py` outputs | **forbidden** |
| `CCustomBuildingPipeline__CreateCustomObjPipe_PS2` / `CCustomBuildingDNPipeline__CreateCustomObjPipe_PS2` | Optional: `CCustomBuildingPipeline::CreateCustomObjPipe` / `CCustomBuildingDNPipeline::CreateCustomObjPipe` | Rockstar has one `CreateCustomObjPipe` per class (`pipes/PC/CustomBuildingPipeline.cpp:917`, `pipes/PC/CustomBuildingDNPipeline.cpp:1358`); our `_PS2` suffix reflects aap's runtime-selected pipe, not a class difference | medium (hook targets are by address, so no name dependency; keep the `_PS2` suffix if the second non-DN pipe is kept) |

**Do-not-touch summary:** exported `Config` layout/fields, INI keys, `IDR_*`/CSO names, and the `_exe`/`m_fDNBalanceParam` game wrappers. Everything else in the pipe files is free to rename because hooks are installed by address (`InjectHook(0x5D7100, …)` etc. `src/render/buildingPipe.cpp:2916-2921`, `src/render/vehiclePipe.cpp:3340-3342`) and the callbacks are passed as pointers to `RxD3D9AllInOneSetRenderCallBack` (`:2886`, `:3309`).

---

## 6. Rockstar reference index (used only to confirm naming/behaviour)

| Purpose | File:line |
|---|---|
| Xbox building FFP render cb | `pipes/Xbox/CustomBuildingPipeline_Xbox.cpp:751-909` |
| Xbox building env stage | `pipes/Xbox/CustomBuildingPipeline_Xbox.cpp:817-826` |
| Xbox building vertex alpha | `pipes/Xbox/CustomBuildingPipeline_Xbox.cpp:872/876` |
| Xbox building instance cb | `pipes/Xbox/CustomBuildingPipeline_Xbox.cpp:166`, `:392` |
| Xbox building pipe creation | `pipes/Xbox/CustomBuildingPipeline_Xbox.cpp:918` |
| Xbox building pipeline ID | `pipes/Xbox/CustomBuildingPipeline_Xbox.cpp:155` (`PDSPC_CBuildingEnvMap_AtmID`) |
| Xbox DN render cb | `pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:1175-1348` |
| Xbox DN instance cb | `pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:576` |
| Xbox DN update / blend | `pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:284-331`, `:336-353` |
| Xbox DN VS constants (c0/c4/c5/c6) | `pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:463-521` |
| Xbox DN pipe ID | `pipes/Xbox/CustomBuildingDNPipeline_Xbox.cpp:247` (`PDSPC_CBuildingDNEnvMap_AtmID`) |
| PC building FFP cb / create | `pipes/PC/CustomBuildingPipeline.cpp:750-908`, `:917` |
| PC DN cb / create / prelit blend | `pipes/PC/CustomBuildingDNPipeline.cpp:1174-1347`, `:1358`, `:1513-1576` |
| Xbox vehicle Xbox1 cb | `pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1087-1582` |
| Xbox vehicle Xbox1 dual pass (**`#if 0`**) | `pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1239-1455` |
| Xbox vehicle D3D9 cb | `pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1584-1918` |
| Xbox vehicle env2 stage | `pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1730-1810` |
| Xbox vehicle original pre-render update | `pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:933` |
| Xbox vehicle pipe creation / ID | `pipes/Xbox/CustomCarEnvMapPipeline_Xbox.cpp:1960`, `:188` (`PDSPC_CarFXPipe_AtmID`) |
| PC vehicle cb | `pipes/PC/CustomCarEnvMapPipeline.cpp:1231-1594` |
| PC vehicle env stages | `pipes/PC/CustomCarEnvMapPipeline.cpp:1380-1486` |
| PC vehicle pipe creation | `pipes/PC/CustomCarEnvMapPipeline.cpp:1648` |
| Mobile (OpenGL) building / vehicle | `pipes/MBR/CustomBuildingPipeline.cpp:89`, `pipes/MBR/CustomCarEnvMapPipeline.cpp:809` |
| MBR DN pipeline | **absent** (no `CustomBuildingDNPipeline.cpp` in `pipes/MBR/`) |

---

## Appendix A — quick file:line index of current-mode code

- Mode plumbing: `src/core/modeManager.cpp:125-141` (stubs), `:225` (listener API), `:281-293` (apply/notify); `src/core/debugmenu_ui.cpp:2687-2704` (mode rows + manual `resetValues`); `src/render/ps2_mode.cpp:42-103`; `src/render/xbox_mode.cpp:39-99`; `src/skygfx.h:255-275` (`PresetConfig`), `:1428-1432` (mode decls).
- Building legacy path: `src/render/buildingPipe.cpp:150-186` (ambient update), `:228-286` (DN params), `:320-338` (tags), `:1023-1363` (PS2), `:1452-1672` (Xbox), `:1676-1814` (Sphere), `:2479-2591` (Switch), `:2650-2831` (DN instance), `:2834-2893` (pipe create), `:2913-2926` (hooks).
- Vehicle legacy path: `src/render/vehiclePipe.cpp:182-284` (init/pre-render/env frames), `:322-488` (env xform/setup), `:525-574` (lights), `:896-1173` (PS2), `:1342-1388` (`SetupMaterial_Xbox`), `:1390-1636` (Xbox), `:2012` (Env/PBR), `:3150-3300` (Switch), `:3338-3346` (hooks).
- Env map: `src/render/envmap.cpp:67-115` (rasters), `:117-165` (temporal blend), `:392-470` (reflection scene), `:472-512` (coronas), `:855-916` (sphere reflections), `:963-975` (hooks).
- Shader creation: `src/render/pipelinecommon.cpp:902-945` (legacy VS/PS handles), `:190-213` (ambient), `:97-188` (sky clamp).
- Resources/build: `resources/resource.h:5-89`, `resources/resource.rc:75-94`, `tools/fast_build.py:355-445`, `premake5.lua:12-20`.

# Code Inventory — skygfx_plus_expIV

**Date:** 2026-10-24 (snapshot 19:15–19:25 local)
**Repo:** `E:\dev(dave)\skygfx_plus_expIV`
**Scope:** every `.cpp`/`.h` under `src/`, every `.hlsl`/`.hlsli` under `shaders/`, `build/skygfx.vcxproj` + `.filters`, `resources/resource.h` + `Resource.rc`, `premake5.lua`, `tools/fast_build.py`.

> **LIVE-SNAPSHOT WARNING.** The tree was being edited while this audit ran. `src/core/debugmenu_theme.{cpp,h}` appeared at 19:13–19:18, `build/skygfx.vcxproj` was updated at 19:17 to add it, and `debugmenu_ui.cpp` changed again at 19:19. Totals below are for the 19:25 state unless a line says otherwise. Build artifacts predate the current tree (`skygfx.asi` = 19:08; `debugmenu_ui.obj` = 19:05), so **the current working tree has not been built**.

Method: full-tree line counts; per-file header/body reads; function/global extraction across all 111 source files (841 parsed definitions); approximate static call-graph by unique symbol name; shader-compile list imported from the real `tools/fast_build.py::get_shader_list()` (110 outputs); resource mapping parsed from `resource.h`/`Resource.rc`. Call-graph columns are approximate (common names excluded), marked `~`.

---

## 0. Totals

| Metric | Value |
|---|---|
| Source files under `src/` | **111** (64 `.cpp`, 47 `.h`) |
| Source LOC | **73,558** |
| Compiled TUs (vcxproj, not excluded) | **59** |
| ExcludedFromBuild TUs | **2** (`PC_PlantsMgr.cpp`, `PC_PlantsMgr_overlay.cpp`) |
| Commented-out TUs | **2** (`rw/normalmap_plugin.cpp`, `render/SMAA.cpp`) |
| TUs absent from vcxproj | **1** (`extras/skygfx_bridge.cpp`) |
| Headers | 47 (13 are `#include`-only / data blobs) |
| HLSL files | **120** (118 `.hlsl` + 2 `.hlsli`), 14,984 LOC |
| Compiled shader entry points | **110** |
| `.cso` files on disk | **121** |
| `IDR_*` resources defined + embedded | **99** |
| Orphan RC shader IDs (never bound) | **16** |
| RC IDs with no loader at all | **1** (`IDR_PEDPBRVS`) |
| CSOs on disk not embedded in RC | **21** |
| CSOs with no HLSL producer | **11** |
| HLSL-produced CSOs not embedded | **17** |
| Verified dead functions | **29** (~470 LOC) |
| Non-built/excluded source | **5,032 LOC** (5 TUs) |
| `#if 0` code | **174 LOC** (7 blocks) |
| Inert (compiled, zero call sites) modules | **2** (`vegImposter` 1,070 LOC; most of `PC_GrassInstancer` 574 LOC) |
| Raw D3D9 device writes (`d3d9device->` / `dev->` / `device->`) | **≈928** across 25 files |
| TODO/FIXME/HACK/XXX hits | 19 raw (10 actionable + 9 false positives) |
| Duplicated LOC (≥15-line clone spans) | **≈1,220** |
| `Config` fields | **436** (231 assigned by parser; **198 never referenced outside `skygfx.h`**) |

---

## 1. FILE TABLE

**Build legend:** `C` = compiled; `X` = `ExcludedFromBuild`; `M` = commented out in vcxproj; `N` = not in vcxproj; `H` = header-only / ClInclude.
**Flag legend:** `NEW` = untracked in git; `MOD` = modified in working tree; `DEAD` = zero callers; `UNUSED` = included by nothing; `DATA` = byte blob.

### 1.1 `src/core` — 17 files, 9,305 LOC

| File | LOC | Role | B | Key entry points | Key globals | Calls into (~) | Called by (~) | Flag |
|---|---|---|---|---|---|---|---|---|
| `core/crashhandler.cpp` | 304 | Self-installing unhandled-exception filter + minidump via dbghelp | C | `CrashHandlerInstaller@290` (static ctor) | `s_crashHandling`, `s_prevFilter`, `s_crashInstaller` | dbghelp, diagnostics | CRT static init | |
| `core/debugmenu_theme.cpp` | 493 | ImGui "Aero/Bento" palette, metrics, glass draw helpers | C | `DrawGlassHeader@249`, `DrawGlassCollapsingHeader@333`, `DrawAccentSeparator`, `DrawStatusBadge` | `g_dmPalette@15`, `g_dmMetrics@39` | imgui draw lists | `debugmenu_ui.cpp` | **NEW** (created 19:13, added to vcxproj 19:17) |
| `core/debugmenu_theme.h` | 112 | Theme API (palette structs, helper decls) | H | — | — | — | `debugmenu_ui.cpp:6` | **NEW** |
| `core/debugmenu_ui.cpp` | 3,673 | ImGui debug menu: ~315 setting rows, presets, mode tabs, profiler, config editor | C | `DrawUnifiedDebugMenu@3397`, `EnsureImGuiInit@392`, `UploadUnifiedConstants@416`, `ImGuiWndProc@379` | `menu`, `hasMenu`, `s_*` UI state (29) | main, presets, postfx, forwardplus, vegImposter, debugmenu_public | `postfx.cpp` (menu pause guard) | **MOD** (19:19) |
| `core/diagnostics.cpp` | 461 | `dbglog`, VEH crash handler, log ring buffer, watchdog thread, scope registry | C | `diag_getLogPath@24`, `diag_ringPut@36`, `diag_registerScope@72`, `diag_startWatchdog@347` | `s_logPath`, `g_scopeTags`, `g_inGuardedIm2DPass`, `g_renderPhase` | Win32, DbgHelp | nearly every TU via `dbglog` | **MOD** |
| `core/diagnostics.h` | 67 | Diagnostics API + `DIAGNOSTICS_DISABLED` stubs | H | — | — | — | diagnostics.cpp, main.cpp | |
| `core/hooks.h` | 27 | Declares `InstallAllHooks`/`InjectDelayedPatches` (never includes/defined here) | H | — | — | — | **nothing** | **UNUSED/DEAD** |
| `core/main.cpp` | 6,192 | DLL entry `DllMain@5901`, INI parse/save, all hook install, `RenderScene`, front-buffer sync, corona/roadsign/moon/skidmark/cutscene patches, mode/preset application | C | `DllMain`, `InjectDelayedPatches@5694`, `installDeferredHooks@3677`, `RenderScene@2081`, `readIniFile@3945`, `saveConfigTo@5162`, `refreshIni`, `ApplyPreset` caller | `dllModule`, `perfFreq`, `numConfigs`, `currentConfig`, 60+ feature flags; `MENUSETTINGS` macro @5583 | diagnostics, menu_inject, presets, weather, ragdoll, depthhook, forwardplus, normalmap, pipelinecommon, postfx, waterPipe, gta | debugmenu_ui, pipelinecommon, postfx, mirrorPass, PC_GrassInstancer | **MOD** (19:10) |
| `core/main_exports.h` | 87 | Externs from main.cpp for hook TUs | H | — | — | — | main.cpp | |
| `core/menu_inject.cpp` | 429 | Injects "SkyGFX" page into native CMenuManager (screen 44), navigation + input | C | `menu_inject_init@408`, `build_skygfx_screen@149`, `patch_screens_table@184` | `s_options`, `s_extendedScreens`, `s_aScreensPtr`, `original_*` | — | main.cpp | **MOD**; `draw_skygfx_options@279`, `handle_skygfx_input@351` are **DEAD** |
| `core/menu_inject.h` | 21 | Menu-injection API + action enums | H | — | — | — | menu_inject.cpp | |
| `core/modeManager.cpp` | 294 | Single-active-mode state machine + config profiles (GTAIV/PS2/Xbox/Mobile/GTAV) | C | `ModeManager_SetMode@242`, profiles @62–168, `ModeManager_*` | `g_preModeConfig`, `s_mode`, `s_listeners` | skygfx.h only | debugmenu_ui, main | **NEW** |
| `core/modeManager.h` | 68 | Mode IDs + manager API | H | — | — | — | modeManager.cpp, main, menu_inject | **NEW** |
| `core/presets.cpp` | 155 | Preset name table + `ApplyPreset` | C | `ApplyPreset@108`, `presetConfigs@25` | `presetNames`, `presetConfigs` | — | debugmenu_ui, main | **MOD** |
| `core/test.cpp` | 47 | Dead native-menu dumper (`dumpMenu`) | C | `dumpMenu@24` (0 callers) | `screens` | — | **nothing** | **DEAD** |
| `core/weather.cpp` | 466 | Multi-timecyc override (`weathers2.dat`), hash set selection, blend, sun config | C | `Weather_Init@229`, `Weather_Update@285`, `Weather_ApplySunConfig@249`, `ParseTimecycFile@77` | `g_altTimecyc`, `g_currentBlend`, `g_targetBlend` | skygfx.h | main, pipelinecommon, vehiclePipe | **MOD** |
| `core/weather.h` | 31 | Weather API + set enum | H | — | — | — | weather.cpp, main | |

### 1.2 `src/entities` — 5 files, 1,491 LOC

| File | LOC | Role | B | Key entry points | Key globals | Calls into (~) | Called by (~) | Flag |
|---|---|---|---|---|---|---|---|---|
| `entities/PlantSurfPropMgr.h` | 32 | Stub plant surface-property structs for the legacy plant port | H | — | — | — | `PC_PlantsMgr.h` | |
| `entities/Ragdoll.h` | 138 | Ped animation/bone types + ragdoll API | H | — | — | — | ragdoll.cpp | |
| `entities/chars.cpp` | 720 | Ped part classification (skin/hair/cloth/…) + SSS blur pass | C | `chars_drawSSSBlur@321`, `chars_init@648`, `chars_OnDeviceReset@703` | `g_partSSSParams`, `g_charsClassifyTex/Surf/Raster`, `sssBlurRasterA/B` | pipelinecommon, postfx, postfx_utils, gta | postfx.cpp (calls `chars_drawSSSBlur`) | **MOD**; 6 classify helpers **DEAD** |
| `entities/chars.h` | 67 | Part enums, params, API | H | — | — | — | chars.cpp | **MOD** |
| `entities/ragdoll.cpp` | 534 | Living-ped secondary-motion ragdoll (verlet, bone limits, blend) | C | `RagdollManager_c@396`, `Ragdoll_c::Update@185`, `ProcessAllPeds@452` | `g_ragdollMan`, `aBONETAG_ENUM_TAB` | gta | main, ragdoll_death | |

### 1.3 `src/extras` — 12 files, 4,423 LOC

| File | LOC | Role | B | Key entry points | Key globals | Calls into (~) | Called by (~) | Flag |
|---|---|---|---|---|---|---|---|---|
| `extras/brdfLibrary.h` | 627 | 270+ material BRDF database + surface-type lookup | H | `GetBRDF@487`, `GetSurfaceTypeFromMaterial@524` | `g_brdfTable@259` | string.h | veh_shaders, buildingPipe, vehiclePipe | `PackBRDFConstants@496` **DEAD** |
| `extras/debugmenu_public.h` | 152 | debugmenu.asi dynamic API typedefs/loader | H | 28 inline loaders | — | — | debugmenu_ui, vehiclePipe | |
| `extras/dismember.cpp` | 633 | Bullet gibs, limb severing, stump zero-scale, blood spurts | C | `Dismember_Init@126`, `BuildGibClump@197`, `Dismember_Update@526`, `Dismember_Render@544` | `g_dismemberEnable`, `g_dismemberGibs`, `s_gibCount` | ragdoll_death, gta, Bullet | ragdoll_death | |
| `extras/dismember.h` | 54 | Dismember API + GibInstance | H | — | — | — | dismember.cpp, ragdoll_death | |
| `extras/ragdoll_death.cpp` | 1,227 | Bullet 2.87 death-ragdoll solver (16-ped pool), joint configs, pose apply | C | `Ragdoll_Init@346`, `Ragdoll_Activate@411`, `Ragdoll_Simulate@963`, `Ragdoll_ApplyPose@1150` | `g_bulletWorld`, `g_ragdollDeathPool`, joint/body tables | dismember, ragdoll, gta, diagnostics, Bullet | main | **MOD**; `ReadIniInt@155` **DEAD** |
| `extras/ragdoll_death.h` | 143 | Death-ragdoll API/structs | H | — | — | — | ragdoll_death.cpp, skygfx.h | |
| `extras/skygfx_bridge.cpp` | 134 | Cross-process named-shared-memory command bridge + **second `DllMain`** | **N** | `Bridge_Init@27`, `Bridge_SendCommand@72`, `DllMain@124` | `g_sharedMem`, `g_mapFile`, `g_mutex` | Win32 | **nothing** | **DEAD** (would collide if premake globs it) |
| `extras/texdb.cpp` | 319 | Texture-db hook (patches game texture-find at `0x731FD5`), detail-texture lookup | C | `hooktexdb@287`, `TexDbFindCB@216`, `RwTextureRegisterPlugin@198` | `texdb`, `texdbOffset`, `detailTxd`, `detailTextures[100]` | neoWaterdrops, MemoryMgr | normalmap.cpp (chains CB) | |
| `extras/veh_shaders.cpp` | 539 | Vehicle→shader data bridge: paint type/weight, glass/light tints, tire/glass detection, BRDF surface | C | `VehShaders_Init@130`, `SelectPaintType@141`, `GetSurfaceType@252`, `ApplyDirtToPBR@304` | `paintTable`, `glassDefaults`, `tireTable`, `chromeFrameKeywords` | brdfLibrary, vehicles, gta | vehiclePipe | |
| `extras/vehicles.cpp` | 378 | Parses `vehicles.ide`/`carcols.dat`/`handling.cfg` into vehicle DB | C | `Vehicles_Init@312`, `GetVehicleEntry@342`, `GetVehicleGroup@349`, `IsVehicleCopByID@364` | `vehicleDB`, `vehicleDBCount` | stdio | veh_shaders, wheels, vehiclePipe | `GetVehicleDB[Count]@377-378` **DEAD** |
| `extras/wheels.cpp` | 169 | Wheel DFF registry + hash-based selection | C | `Wheels_Init@47`, `Wheels_SelectForVehicle@111`, `Wheels_GetEntry@157` | `g_wheels[512]`, `g_wheelCount` | vehicles | vehiclePipe | |
| `extras/wheels.h` | 44 | Wheel classes/API | H | — | — | — | wheels.cpp | |

### 1.4 `src/render` — 58 files, 51,244 LOC

| File | LOC | Role | B | Key entry points | Key globals | Calls into (~) | Called by (~) | Flag |
|---|---|---|---|---|---|---|---|---|
| `render/AreaTex.h` | 14,980 | SMAA area-texture byte blob | H | — | `areaTexBytes@43` | — | postfx_smaa.cpp:134, SMAA.cpp:42 | **DATA** (largest file in repo) |
| `render/EdgeDetection.h` | 146 | Unused SMAA `EdgeDetection` class declaration | H | — | — | — | **nothing** | **UNUSED** |
| `render/GrassSystem.h` | 101 | Unused grass/plant structs | H | `GenPointInTriangle@90` | — | — | **nothing** | **UNUSED** |
| `render/PC_GrassInstancer.cpp` | 478 | Batched Im3D plant instancer (concatenated vertex pool, one submit/texture) | C | `CGrassInstancer::SetEnabled@218`, `BeginBatch@248`, `AddInstance@265`, `ShaderReady@463` | `s_masterEnable`, `s_verts/indices`, `s_grassInstPS`, `s_rngState` | main, gta | main (gate only); draw calls only from excluded `PC_PlantsMgr` | **NEW**; drawing path effectively DEAD; `IDR_GRASSINSTANCED` undefined |
| `render/PC_GrassInstancer.h` | 96 | Instancer API/contract | H | — | — | — | main, PC_PlantsMgr | **NEW** |
| `render/PC_GrassRenderer.cpp` | 637 | Legacy `CGrassRenderer` tri-plant buffer/renderer (PC/Xbox port) | C | `CGrassRenderer::DrawTriPlants@257`, `Flush@491`, `ChangeCurrentPlantModelsSet@567` | `PPTriPlantBuffer`, `m_vecCameraPos`, `m_windBending` | gta | PC_PlantsMgr (excluded), main | **MOD**; `#if 0` block 509-532 |
| `render/PC_GrassRenderer.h` | 169 | Legacy grass renderer types | H | — | — | — | PC_GrassRenderer.cpp | |
| `render/PC_PlantsMgr.cpp` | 2,175 | Legacy `CPlantManager` port (collision cache, loc tris, plant render) | **X** | `CPlantMgr::Render@414`, `_ProcessEntryCollisionData@1266`, `_ColEntityCache_Update@715` | 30 globals (`m_LocTrisTab`, plant texture/model tabs, `s_instCamPos`) | PC_GrassRenderer, PC_GrassInstancer, gta, normmap_stubs | PC_PlantsMgr.h | **EXCL** (needs absent SA game source); duplicate body 1161↔1381 |
| `render/PC_PlantsMgr.h` | 386 | Legacy plant manager types | H | — | — | — | PC_GrassRenderer, overlay | |
| `render/PC_PlantsMgr_overlay.cpp` | 1,035 | Legacy plant init overlay (`LoadPlantsDat`, textures/models, surf props) | **X** | `CPlantMgr::Initialise@1028`, `LoadPlantsDat@869`, `CPlantSurfPropMgr::Initialise@681` | `models_fnames`, `_plantTexture` | PC_GrassRenderer, gta, postfx_colourfilter | PC_PlantsMgr.h | **EXCL**; `#if 0` 168-189 |
| `render/PostFXPipeline.h` | 103 | Unused PostFX pipeline class declaration | H | — | — | — | **nothing** | **UNUSED** |
| `render/SMAA.cpp` | 374 | Legacy standalone SMAA implementation | **M** | `loadAreaTex@234`, `edgesDetectionPass@270`, `neighborhoodBlendingPass@340` | — | SearchTex/AreaTex | **nothing** | **DEAD** (vcxproj line 249) |
| `render/SMAA.h` | 177 | Legacy SMAA class decl | H | — | — | — | only SMAA.cpp | **DEAD** |
| `render/SearchTex.h` | 132 | SMAA search-texture byte blob | H | — | `searchTexBytes@43` | — | postfx_smaa.cpp:135 | **DATA** |
| `render/buildingPipe.cpp` | 2,840 | Building render callbacks (PS2/Xbox/GTAIV/PBR), IV FP swap, tree alpha/wind, normal-map + texalpha material setup, shader-name trace | C | `buildingPipe_blendToD3D@453`, `ivFPReady@1008`, callbacks @614-2100, `ReleaseBuildingPipeCaches@2828` | 24 shader handles, `CWeather__WetRoads`, `CCoronas__LightsMult`, pipeline ptrs | pipelinecommon, postfx, treeWind, normalmap, texalpha, forwardplus, brdf | postfx.cpp | **MOD**; duplicates defaultFuncs 2581-2650 |
| `render/custom_mode.cpp` | 171 | Dead "Custom (PBR)" preset/defaults | C | `CustomMode_ApplyDefaults@68`, `GetPreset@166` (0 callers) | `custom_preset` | — | **nothing** | **DEAD** |
| `render/depthhook.cpp` | 577 | Vtable hooks `SetDepthStencilSurface`/`Reset`, INTZ substitution, suspend/restore, post-Reset shader invalidation | C | `hook_SetDS@167`, `hook_Reset@329`, `DepthHook_Install@410`, `DepthHook_Suspend@468` | `g_intzTex/Surf`, `g_gameDS`, `g_hookedDev`, `orig_*` | postfx, motionblur, ssao, texalpha, diagnostics | main, postfx*, forwardplus, underwater | **MOD**; duplicates screen-size logic with forwardplus |
| `render/depthhook.h` | 35 | Depth-hook API | H | — | — | — | postfx*, motionblur, ssao | |
| `render/envmap.cpp` | 963 | Environment-map render, reflection camera, visibility/cloud/corona/stadium-quad hooks | C | `RenderReflectionScene@?`, `CEntity::GetIsOnScreen@312`, `CClouds__RenderSkyPolys@400`, hooks @950-960 | `reflectionCam`, `envFB`, `reflectionTex`, `CMirrors__TypeOfMirror`, `bFudgeNow` | neoCarpipe, gta | buildingPipe, postfx, vegImposter | **MOD**; duplicates neoCarpipe 189-234 |
| `render/forwardplus.cpp` | 1,648 | Forward+ light collect/sort/dedup, tile+atlas upload, cull, debug cluster view | C | `ForwardPlus_AddLight@446`, `CollectLights@421`, `CullAndUpload@990`, `SetConstants@1226`, `DrawClusterDebug@1384` | 26 globals (`g_fp*Tex`, `g_fpLights`, `g_fpStats`) | depthhook, gta, diagnostics, main | main, buildingPipe, vehiclePipe, postfx, debugmenu | **MOD** |
| `render/forwardplus.h` | 95 | ClusterLight + FP API | H | — | — | — | forwardplus.cpp | **MOD** |
| `render/iv_mode.cpp` | 79 | Dead IV preset (only raises `ivMode`; real profile in modeManager) | C | `IVMode_ApplyDefaults@63`, `GetPreset@76` (0 callers) | `iv_preset` | — | **nothing** | **DEAD** |
| `render/mirrorPass.cpp` | 347 | W7-C mirror composite distortion fix; hooks `CMain::Render` @`0x53EABA`, always calls game first, overdrawns stadium quad | C | `mirrorPass_init`, `mirrorCompositePS@97` | `CMirrors__pBuffer`, `kMirrorStadiumQuad*`, `mirrorPSLoadTried` | main, gta, MemoryMgr | main:3700 | **NEW** |
| `render/mirrorPass.h` | 14 | Mirror-init decl | H | — | — | — | main | **NEW** |
| `render/mobile.cpp` | 105 | Dead Mobile preset | C | `MobileMode_ApplyDefaults@61`, `GetPreset@102` | `mobile_preset` | — | **nothing** | **DEAD** |
| `render/neo.cpp` | 209 | `neo.txd` loader, NEO pipeline creation, color interpolation types | C | `neoInit@?`, `CreateRwPipeline@170`, `Attach@186` | `neoTxd`, `iCanHasNeoCar` | neoWaterdrops, gta | neoCarpipe | `#if 0` 143-209 |
| `render/neo.h` | 206 | NEO color/interpolation classes | H | — | — | — | neo.cpp, neoCarpipe, neoWaterdrops | |
| `render/neoCarpipe.cpp` | 812 | GTA IV/Neo `CarPipe`: reflection env tex, diffuse/specular passes, render callback | C | `CarPipe::RenderCallback@758`, `CreateShaders@475`, `RenderEnvTex@295`, `Init@461` | shader handles, `reflectionMatrix`, `screenQuad` | neo, pipelinecommon, gta, neoWaterdrops | main, buildingPipe, envmap, iv_mode, depthhook, postfx, vehiclePipe | **MOD**; `vertexShaderPass1/2` permanently NULL |
| `render/neoWaterdrops.cpp` | 810 | Screen-space rain drops (move/spray/fade/render) | C | `Render@670`, `Process@357`, `PlaceNew@426` | `ms_drops`, `ms_maskTex`, `ms_vertexBuf` | postfx, gta | neo, neoCarpipe, vehiclePipe, texdb, postfx* | |
| `render/normalmap.cpp` | 280 | NM lane: chains texture-find CB to pair `<diffuse>_n`/`_norm`, RW texture plugin slot, gloss flag | C | `nmInitHooks@148`, `nmPluginAttach@133`, `nmGetNormalMap@188`, `nmVehicleAllowed@254` | `nm_pluginOffset`, `nm_hooksInstalled` | texdb, MemoryMgr | main:6179, buildingPipe, vehiclePipe | **NEW** |
| `render/normalmap.h` | 30 | NM API | H | — | — | — | buildingPipe, vehiclePipe, main | **NEW** |
| `render/pc_patched_mode.cpp` | 106 | Dead "PC Patched" preset | C | `PCPatchedMode_ApplyDefaults@?`, `GetPreset@?` | `pcpatched_preset` | — | **nothing** | **DEAD** |
| `render/pipelinecommon.cpp` | 1,751 | Shader creation (`makePS@665`/`makeVS@692`), matrix/projection, light uploads, PBR c[22]/c[23] (`pipeUploadPBR`), guarded Im2D render, world baseline | C | `CreateShaders@815`, `makePS`, `makeVS`, `Pipeline_InvalidateShaders@808`, `pipeUploadPBR`, `guardedIm2DRender@?` | 56 shader handles, `s_forceShaderRecreate`, light staging | main, weather, neoCarpipe, postfx, gta | main, chars, buildingPipe, neoCarpipe, postfx_smaa, vehiclePipe | **MOD**; 5 dead lazy getters |
| `render/postfx.cpp` | 4,379 | PostFX orchestrator: `ColourFilter_switch@1331`, `DrawFinalEffects@1972`, radiosity/VCS, IBL, normal buffer, PipeChain, motion blur, height fog, god rays, dumps, device-reset cleanup | C | `ColourFilter_switch@1331`, `DrawFinalEffects@1972`, `OnDeviceReset@2264`, `Initialise_skygfx@4344` | 99 globals (`pRasterFrontBuffer`, CPostEffects mirrors, `g_iblTex`, `g_pipeChain*`) | all postfx_* splits, debugmenu, chars, envmap, forwardplus, vehiclePipe, depthhook | main, postfx splits, waterPipe, chars, debugmenu | **MOD** |
| `render/postfx.h` | 193 | Shared postfx declarations/state | H | — | — | — | all postfx_* splits | **MOD** |
| `render/postfx_colourfilter.cpp` | 1,745 | Colour-filter chain: Modern/Mobile/PS2/IV grade, colour-cycle (timecycle), tonemap handshake | C | `ColourFilter_Modern@150`, `ColourFilter_Mobile@674`, `ColourFilter_PS2@766`, `Update@1612`, `Initialise@1675` | `overrideColorMod`, `grainRaster`, `s_ivGrade*`, timecycle mirrors | postfx, luminance, newfx, utils, gta | postfx.cpp, plant overlay | **NEW** (m0060 split); `#if 0` 853-893 |
| `render/postfx_luminance.cpp` | 250 | 8×8 luminance measure + 1×1 temporal eye adaptation targets | C | `EnsureLuminanceTargets@20`, `RunFrameExposure@168`, `ReleaseLuminanceTargets@240` | `g_lumaMeas*`, `g_lumaAdapt*`, `g_lumaAdaptFlip` | postfx, utils, gta | postfx, colourfilter | **NEW** |
| `render/postfx_motionblur.cpp` | 272 | Velocity buffer + camera-aware motion blur | C | `DrawVelocityBuffer@23`, `ReleaseVelocityBufferResources@15` | `g_velocityTex/Surf`, `g_prevVPMatrix` | depthhook, ssao, postfx, utils, gta | postfx/motionblur | **NEW** |
| `render/postfx_newfx.cpp` | 823 | Sun shafts, DOF chain, bloom pyramid, CA, grain, vignette, FXAA3 | C | `FxLoadShaders@87`, `DrawSunShafts@517`, `DrawDOF@552`, `DrawBloom@629`, `DrawFXAA3@789` | `s_fx`, `s_fxLoadTried`, `g_fx*Tex` | postfx, utils, gta | postfx, colourfilter | **NEW** |
| `render/postfx_smaa.cpp` | 1,328 | SMAA edge/blend/temporal passes, static resources, guard/bail, device state checks | C | `DrawSMAA@441`, `SMAA_DrawPass@338`, `smaaGuardBail@376`, `SMAATryInitRasters@193` | 18 globals (`g_smaa*`, `s_smaa*`) | depthhook, pipelinecommon, postfx, utils, gta | postfx | **NEW**; 3 loaded-but-unused variants |
| `render/postfx_ssao.cpp` | 1,078 | SSAO resources, depth copies, full-res fallback `DrawSSAO@518`, quarter-res `DrawSSAO_Overhaul@706` | C | `DrawSSAO@518`, `DrawSSAO_Overhaul@706`, `PostFX_CopyDepthToTexture@288`, `InitSSAOResources@43` | `g_ssao*`, `g_prevDepth*`, `g_normalBuffer*` | depthhook, postfx, utils, gta | postfx, motionblur, waterPipe | **NEW** |
| `render/postfx_underwater.cpp` | 539 | Underwater volume fog + SSS (W7-D), plane probe from water quads | C | `PostFX_UnderwaterTick@306`, `PostFX_UnderwaterActive` | `s_uwPlanes`, `s_uwPS`, `s_uwActive` | depthhook, postfx, utils, gta | postfx.cpp:1672 | **NEW** |
| `render/postfx_underwater.h` | 52 | Underwater API/wiring contract | H | — | — | — | postfx.cpp | **NEW** |
| `render/postfx_utils.cpp` | 329 | Shared postfx helpers: tracked rasters, screen-size cache, menu/pause guard, fullscreen quad, raw-geom save/restore | C | `TrackRaster@17`, `RasterEnsureSurfaceReady@44`, `GetScreenSize@109`, `IsGameInMenuOrPaused@121` | `colorfilterVerts/Indices`, `s_trackedRasters`, `g_screenSizeW/H`, `s_ffQuad` | depthhook, gta | all postfx_* splits, chars, buildingPipe | **NEW** |
| `render/profiler.cpp` | 662 | Pass/frame profiler: QPC CPU scopes, D3D9 timestamp GPU timers, draw counters, ImGui snapshot | C | `PfInit/PfFrame/PfScope@355`, `PfDraw`, `PfBindPS/VS` | `g_pfEnable`, `g_pfGpuTimers`, `s_slots`, `s_hist` | skygfx.h | profiler.h users (buildingPipe, vehiclePipe, postfx, waterPipe, forwardplus) | **NEW** |
| `render/profiler.h` | 36 | `PF_SCOPE`/`Pf*` API | H | — | — | — | many render TUs | **NEW** |
| `render/ps2_mode.cpp` | 109 | Dead PS2 preset | C | `PS2Mode_ApplyDefaults@65`, `GetPreset` | `ps2_preset` | — | **nothing** | **DEAD** |
| `render/texalpha.cpp` | 398 | Per-texture alpha classification (opaque/cutout/faded), DXT1/3/5+A8 scan, 8/frame budget | C | `TexAlpha_Classify@337`, `TexAlpha_OnDeviceReset@392` | `s_tacCache`, `s_tac*` counters | gta | buildingPipe:2111, vehiclePipe:2246, depthhook | **NEW** |
| `render/texalpha.h` | 40 | TAC classes + API | H | — | — | — | buildingPipe, vehiclePipe, depthhook | **NEW** |
| `render/treeWind.cpp` | 475 | Tree wind sway (CPU WVP tilt + VS c23 upload), console tree lighting/alpha PS | C | `TreeWind_ApplyToTransform`, `TreeWind_UploadWindExt`, `TreeAlpha_Upload`, `TreeAlpha_Active` | `s_windTable`, `s_windTime*`, `treeAlphaPS`, `TreeAlpha_*` | MemoryMgr, extendedplg, gta | buildingPipe | **NEW** |
| `render/treeWind.h` | 140 | Wind/alpha contract + API | H | — | — | — | buildingPipe, debugmenu | **NEW** |
| `render/utilitynoise.cpp` | 251 | Generates one channel-packed utility noise tile (Perlin/Worley/white/Bayer) | C | `GenerateUtilityTexture@170` | `g_pUtilityNoise`, `g_CurrentNoiseSize`, `s_bayer8` | gta, normmap_stubs | main:715 (refreshIni) | **no shader consumer yet** |
| `render/vegImposter.cpp` | 704 | Inert vegetation-imposter infrastructure (16-tile atlas, BeginTile/EndTile, fade) | C | `VegImposter_Ensure@239`, `BeginTile@428`, `EndTile@579`, `BindAtlas@675` | `s_cfg` | envmap, postfx, postfx_utils, gta | **nothing** | **NEW/DEAD (inert by design)** |
| `render/vegImposter.h` | 366 | Imposter API/doctrine | H | — | — | — | debugmenu (status row) | **NEW** |
| `render/vehiclePipe.cpp` | 3,295 | Vehicle render callbacks: PS2/Xbox/Neo/IV/PBR/Modern/Mobile, env-map matrix, texalpha/normalmap uploads, `ReleaseVehiclePipeCaches@3262` | C | callbacks @700-3000, `vehiclePipe_saveRenderState@605`, `vehiclePipe_cleanup@632` | 35 globals (shader handles, env tweaks, caches) | weather, brdf, veh_shaders, vehicles, wheels, forwardplus, neo, normalmap, pipelinecommon, postfx, texalpha, gta | buildingPipe, postfx | **MOD**; 3 intra-file clone blocks |
| `render/waterPipe.cpp` | 1,586 | Water shading-only waves: VS/PS bind, body classification (OCEAN…LAKE), scene/depth/reflection copies, detail normal, render-state save/restore | C | `waterPipe_setRenderState@890`, `waterPipe_restoreRenderState@1524`, `waterPipe_init@537`, `WbBuildTable@306` | `g_sceneTexture`, `g_depthTexture`, `g_reflTexture`, `s_saved*`, body tables | postfx_ssao, profiler, gta | main | **MOD**; 633-line state function |
| `render/waterPipe.h` | 17 | Water API | H | — | — | — | main | |
| `render/xbox_mode.cpp` | 105 | Dead Xbox preset | C | `XboxMode_ApplyDefaults@?`, `GetPreset` | `xbox_preset` | — | **nothing** | **DEAD** |

### 1.5 `src/rw` — 18 files, 5,694 LOC

| File | LOC | Role | B | Key entry points | Key globals | Calls into (~) | Called by (~) | Flag |
|---|---|---|---|---|---|---|---|---|
| `rw/ColData.h` | 17 | Stub collision triangle | H | — | — | — | PC_PlantsMgr.h | |
| `rw/HLSL_hook.cpp` | 35 | Stub HLSL compile helpers, all return null | C | `CompileVertexShader`, `CompilePixelShader`, `LoadShaderFromFile` | — | — | **nothing** | **DEAD** |
| `rw/HLSL_hook.h` | 12 | Stub API | H | — | — | — | HLSL_hook.cpp | **DEAD** |
| `rw/LinkList.h` | 148 | `CLink<T>` template | H | — | — | — | skygfx.h | |
| `rw/MemoryMgr.h` | 98 | `WRAPPER`/`EAXJMP`/patch helpers (`Patch`, `Nop`, `InjectHook`, `InterceptCall`) | H | — | — | — | main, texdb, mirrorPass, normalmap, buildingPipe… | |
| `rw/Pools.h` | 161 | `CPool<T>` template | H | — | — | — | skygfx.h, PC_GrassRenderer | |
| `rw/defaultFuncs.cpp` | 466 | Replacement D3D9 default render callbacks (not-lit/pre-lit), pipeline clone/destroy, vertex-declaration builders | C | `D3D9RenderNotLit@4`, `D3D9RenderPreLit@15`, `DNInstance_default@?` | — | gta | main (callback table) | duplicates buildingPipe 148-205 |
| `rw/extendedplg.cpp` | 52 | `RpAtomic` EDED plugin (DefinedVertexShader per atomic), wind default | C | `EDEDPluginAttach@45`, `ededConstruct/ededCopy/Read/GetSize`, `Get/SetDefinedShader` | `ededAtmOffset` | gta | buildingPipe, treeWind | |
| `rw/gta.cpp` | 232 | RW 3.6 globals + **122 `WRAPPER` thunks** to hardcoded exe addresses | C | all `WRAPPER` stubs | `RsGlobal`, `d3d9device`, `Scene`, `pAmbient`, `pDirect`, camera/weather/timecycle refs (44) | — | almost every TU | many wrappers have 0 callers (API surface) |
| `rw/gta.h` | 391 | `CVector`, RW/SA type declarations, externs | H | — | — | — | nearly every TU | |
| `rw/normalmap.cpp` | 368 | DK normal-map plugin integration: custom atomic pipes, 3 light callbacks, tangent fix, init/shutdown | C | `normalmap_init@352`, `normalmap_tryAttach@293`, `SetupDirectionalLight@194` | `gNormalMapAtomicPipelines`, `gDirectionalLight`, `gAmbClr`, multipliers | MemoryMgr, rpnormmap | main | |
| `rw/normalmap_plugin.cpp` | 1,314 | Full DK normal-map plugin port (TXD player textures, material/atomic setup) | **M** | `BuildPlayerNormalMapTextures@906`, `SetupAtomic@1160`, `DllMain@1280` | 19 globals | gta, normmap_stubs | **nothing** | **DEAD** (vcxproj line 248) |
| `rw/normmap_stubs.cpp` | 87 | Forwarding stubs for `rpnormmap.lib` RW imports | C | 39 stubs | — | game addresses | rpnormmap, plants, postfx | |
| `rw/pipeplg.cpp` | 74 | GTA pipeline-ID plugin accessor + PDS material/atomic offsets | C | `GetPipelineID@?`, `SetPipelineID`, `PDSPluginAttach` | `gtaPipeOffset`, `pdsMatOffset`, `pdsAtmOffset` | gta | buildingPipe, vehiclePipe | |
| `rw/rphanim.h` | 673 | RW HAnim SDK header | H | — | — | — | ragdoll, ragdoll_death, dismember | |
| `rw/rtanim.h` | 654 | RW anim SDK header | H | — | — | — | rphanim | |
| `rw/rtquat.h` | 646 | RW quaternion SDK | H | — | — | — | ragdoll, ragdoll_death | |
| `rw/rtslerp.h` | 262 | RW slerp SDK | H | — | — | — | ragdoll | |

### 1.6 Root

| File | LOC | Role | B | Key entry points | Key globals | Calls into (~) | Called by (~) | Flag |
|---|---|---|---|---|---|---|---|---|
| `src/skygfx.h` | 1,418 | Master header: all enums, `Config` (436 fields), `PresetConfig`, shader externs, `dbglog`/`PERF_SCOPE`/`pipeSetTexture` helpers | H | `dbglog_throttle@87`, `dbglog_first@99`, `perfNow@65` | — | — | essentially every TU | **MOD** |

**New/uncommitted source files in this session burst (Oct 24 or still untracked):** `debugmenu_theme.{cpp,h}`, `texalpha.{cpp,h}`, `modeManager.{cpp,h}`, `normalmap.{cpp,h}`, all 8 `postfx_*` split TUs + `postfx_underwater.h`, `profiler.{cpp,h}`, `treeWind.{cpp,h}`, `mirrorPass.{cpp,h}`, `PC_GrassInstancer.{cpp,h}`, `vegImposter.{cpp,h}` = **30 files** (plus 19 untracked shader files). `postfx_colourfilter/luminance/motionblur/newfx/smaa/ssao/utils` are `??` in git even though older (Oct 9–23) — the whole m0060 split was never committed.

---

## 2. SHADER TABLE

### 2.1 Compile path

- Single-entry pass: every `shaders/ps/**/*.hlsl` → `resources/cso/<stem>.cso` (`ps_3_0`, entry `main`), every `shaders/vs/**/*.hlsl` → `<stem>.cso` (`vs_3_0`, entry `main`), with skips for multi-entry producers and subdir/root stem collisions (`tools/fast_build.py:352-403`).
- Multi-entry pass (`fast_build.py:406-454`): 36 explicit (file, entry, cso) rows.
- Total expected outputs: **110**. All 110 exist on disk.
- Prebuilt binaries with no HLSL source: `GTAIVBuilding_ps`, `GTAIVForwardPlus_ps/vs`, `GTAIVVehicle_ps/vs`, `SMAA`, `customBuildingVS`, `buildingPipePS`, `vehiclePipeVS`, `neoVehiclePass1VS`, `neoVehiclePass2VS` (12 — last two are also no longer emitted).

### 2.2 Embedded resources (99 `IDR_*`)

`Load` = handle creation (resource→D3D object). `Draw` = first verified draw-path bind. `Status`: **live**, **orphan** (loaded, never drawn), **lazy** (only an uncalled getter), **dead** (never loaded), **prebuilt** (no HLSL source).

| # | IDR | CSO | HLSL : entry | Load | Draw | Status |
|---|---|---|---|---|---|---|
| 100 | `IDR_GRASSPS` | grassPS.cso | `ps/grassPS.hlsl:main` | main.cpp:1536 (manual) | main.cpp:750 (`gpCurrentPixelShaderForDefaultCallbacks`) | live |
| 101 | `IDR_SIMPLEPS` | simplePS.cso | `buildingPipePS.hlsl:main_simple` | pipelinecommon.cpp:894 | vehiclePipe.cpp:1079 / main.cpp:3828 | live |
| 102 | `IDR_VEHICLEVS` | vehicleVS.cso | `vs/vehicleVS.hlsl:main` | pipelinecommon.cpp:901 | vehiclePipe.cpp:1077 | live |
| 110 | `IDR_GTAIVPS` | GTAIV_ps20.cso | `ps/GTAIV_ps20.hlsl:main` | pipelinecommon.cpp:857 | — | **orphan** |
| 111 | `IDR_GTAIVFPVS` | GTAIVForwardPlus_vs.cso | — prebuilt | buildingPipe.cpp:1013 | buildingPipe.cpp:1208 | live |
| 112 | `IDR_GTAIVFPPS` | GTAIVForwardPlus_ps.cso | — prebuilt | buildingPipe.cpp:1014 | buildingPipe.cpp:1210 | live |
| 120 | `IDR_SSAOPS` | SSAO.cso | `ps/SSAO.hlsl:main` | pipelinecommon.cpp:836 | postfx_ssao.cpp:623 | live |
| 121 | `IDR_SMAAPS` | SMAA.cso | — prebuilt | pipelinecommon.cpp:837 | — | **orphan** (old monolithic pass replaced by Edge/Blend) |
| 122 | `IDR_SMAAEDGEPS` | SMAA_Edge.cso | `ps/SMAA_Edge.hlsl:main` | pipelinecommon.cpp:838 | postfx_smaa.cpp:321 (`DrawSMAA_EdgeDetect`) | live |
| 123 | `IDR_SMAABLENDWEIGHTPS` | SMAA_BlendWeight.cso | `ps/SMAA_BlendWeight.hlsl:main` | pipelinecommon.cpp:860 | postfx_smaa.cpp:985 | live |
| 124 | `IDR_SMAABLENDNEIGHBORPS` | SMAA_BlendNeighbor.cso | `ps/SMAA_BlendNeighbor.hlsl:main` | pipelinecommon.cpp:861 | postfx_smaa.cpp:1058 | live |
| 125 | `IDR_SMAAEDGENORMALPS` | SMAA_EdgeNormal.cso | `ps/SMAA_EdgeNormal.hlsl:main` | pipelinecommon.cpp:839 | — | **orphan** |
| 126 | `IDR_SMAAEDGEDEPTHPS` | SMAA_EdgeDepth.cso | `ps/SMAA_EdgeDepth.hlsl:main` | pipelinecommon.cpp:840 | — | **orphan** |
| 127 | `IDR_SMAATEMPPS` | SMAA_Temporal.cso | `ps/SMAA_Temporal.hlsl:main` | pipelinecommon.cpp:843 | postfx_smaa.cpp:1246 | live |
| 128 | `IDR_SMAAEDGECOMBINEDPS` | SMAA_EdgeCombined.cso | `ps/SMAA_EdgeCombined.hlsl:main` | pipelinecommon.cpp:841 | — | **orphan** |
| 129 | `IDR_SMAAEDGEMOTIONDEPTHPS` | SMAA_EdgeMotionDepth.cso | `ps/SMAA_EdgeMotionDepth.hlsl:main` | pipelinecommon.cpp:842 | postfx_smaa.cpp:902 | live |
| 130 | `IDR_SSAO_VERTEXDEPTH` | SSAO_VertexDepth.cso | `ps/SSAO_VertexDepth.hlsl:main` | pipelinecommon.cpp:844 | — | **orphan** |
| 131 | `IDR_SSAO_TEMPORAL_PS` | SSAO_Temporal.cso | `ps/SSAO_Temporal.hlsl:main` | pipelinecommon.cpp:845 | postfx_ssao.cpp:852 | live |
| 132 | `IDR_SSAO_BILATERALBLUR_PS` | SSAO_BilateralBlur.cso | `ps/SSAO_BilateralBlur.hlsl:main` | pipelinecommon.cpp:846 | postfx_ssao.cpp:894 | live |
| 133 | `IDR_SSAO_UPSAMPLE_PS` | SSAO_Upsample.cso | `ps/SSAO_Upsample.hlsl:main` | pipelinecommon.cpp:847 | postfx_ssao.cpp:986 | live |
| 140 | `IDR_MOTIONBLUR_BURNOUT` | MotionBlur_Burnout.cso | `ps/MotionBlur_Burnout.hlsl:main` | pipelinecommon.cpp:850 | postfx.cpp:3807 | live |
| 141 | `IDR_COLORFILTER_CROSSMIX` | ColorFilter_CrossMix.cso | `ps/ColorFilter_CrossMix.hlsl:main` | pipelinecommon.cpp:851 | — | **orphan** |
| 142 | `IDR_SSS_BLUR` | SSS_Blur.cso | `ps/SSS_Blur.hlsl:main` | pipelinecommon.cpp:852 | chars.cpp:504 | live |
| 143 | `IDR_NORMALBUFFERPS` | NormalBuffer.cso | `ps/NormalBuffer.hlsl:main` | pipelinecommon.cpp:853 | postfx.cpp:2807 | live |
| 144 | `IDR_PIPECHAINPS` | PipeChain.cso | `ps/PipeChain.hlsl:main` | pipelinecommon.cpp:854 | postfx.cpp:3344 | live |
| 160 | `IDR_GRADINGPS` | gradingPS.cso | `ps/gradingPS.hlsl:main` | pipelinecommon.cpp:827 | postfx.cpp:2092 / colourfilter.cpp:285 | live |
| 161 | `IDR_CONTRASTPS` | contrastPS.cso | `ps/contrastPS.hlsl:main` | pipelinecommon.cpp:831 | colourfilter.cpp:753 | live |
| 162 | `IDR_BLURPS` | blurPS.cso | `ps/blurPS.hlsl:main` | pipelinecommon.cpp:832 | postfx.cpp:1055 | live |
| 163 | `IDR_RADIOSITYPS` | radiosityPS.cso | `ps/radiosityPS.hlsl:main` | pipelinecommon.cpp:833 | postfx.cpp:1103 | live |
| 164 | `IDR_VCTRAILSPS` | vcTrailsPS.cso | `ps/vcTrailsPS.hlsl:main` | pipelinecommon.cpp:825 | postfx.cpp (VCS trails) | live |
| 165 | `IDR_MODERNCOLORFILTERPS` | ModernColorFilterPS.cso | `ps/ModernColorFilterPS.hlsl:main` | pipelinecommon.cpp:826 | postfx.cpp:481 | live |
| 166 | `IDR_TONEMAPPASS` | TonemapPass.cso | `ps/TonemapPass.hlsl:main` | pipelinecommon.cpp:828 | colourfilter.cpp:621 (`tmPS`) | live |
| 200 | `IDR_VEHICLEPAINT_GTAIV` | VehiclePaint_GTAIV.cso | `ps/VehiclePaint_GTAIV.hlsl:main` | pipelinecommon.cpp:864 | — | **orphan** |
| 201 | `IDR_WATER_PARALLAX` | Water_Parallax.cso | `ps/Water_Parallax.hlsl:main_xbox` | pipelinecommon.cpp:875 | waterPipe.cpp:1062 (`ps`) | live |
| 202 | `IDR_WATER_VS` | Water_VS.cso | `vs/Water_VS.hlsl:main` | pipelinecommon.cpp:878 | waterPipe.cpp:1061 | live |
| 203 | `IDR_VEHICLEPBR_MODERN` | VehiclePBR_Modern.cso | `ps/VehiclePBR_Modern.hlsl:main` | pipelinecommon.cpp:879 | vehiclePipe.cpp:2967 | live |
| 204 | `IDR_GLASS_VEHICLE` | Glass_Vehicle.cso | `ps/Glass_Vehicle.hlsl:main` | pipelinecommon.cpp:880 | vehiclePipe.cpp:2456 | live |
| 205 | `IDR_RUBBER_VEHICLE` | Rubber_Vehicle.cso | `ps/Rubber_Vehicle.hlsl:main` | pipelinecommon.cpp:881 | — (only boolean gate at vehiclePipe.cpp:2329) | **semi-live** |
| 206 | `IDR_CARPAINT_REFL` | CarPaint_Reflections.cso | `ps/CarPaint_Reflections.hlsl:main` | pipelinecommon.cpp:883 | — | **orphan** |
| 207 | `IDR_PBR_LIGHTING` | PBR_Lighting.cso | `ps/PBR_Lighting.hlsl:main` | pipelinecommon.cpp:884 | — | **orphan** |
| 208 | `IDR_CLAMP` | Clamp.cso | `ps/Clamp.hlsl:main` | pipelinecommon.cpp:885 | postfx_ssao.cpp:234 | live |
| 209 | `IDR_DYNAMICSKY` | DynamicSky.cso | `ps/DynamicSky.hlsl:main` | pipelinecommon.cpp:886 | postfx.cpp:2510 | live |
| 210 | `IDR_SKINPBR` | SkinSSS.cso | `ps/SkinSSS.hlsl:main` | pipelinecommon.cpp:789 (lazy) | — (getter never called) | **lazy/orphan** |
| 211 | `IDR_VEHICLEPBRVS` | vehiclePBRVS.cso | `vehiclePipeVS.hlsl:main_vehiclePBR` | pipelinecommon.cpp:902 | vehiclePipe.cpp:999 | live |
| 212 | `IDR_PS2ENVSPECFXPS` | ps2EnvSpecFxPS.cso | `ps/VehiclePBR_Modern.hlsl:main_ps2EnvSpecFx` | pipelinecommon.cpp:910 | buildingPipe.cpp:1287 / vehiclePipe.cpp:1160 | live |
| 213 | `IDR_PS2CARFXVS` | ps2CarFxVS.cso | `vehiclePipeVS.hlsl:main_ps2CarFx` | pipelinecommon.cpp:903 | vehiclePipe.cpp:1149 | live |
| 214 | `IDR_SPECCARFXVS` | specCarFxVS.cso | `vehiclePipeVS.hlsl:main_specCarFx` | pipelinecommon.cpp:904 | vehiclePipe.cpp:1304 | live |
| 215 | `IDR_SPECCARFXPS` | specCarFxPS.cso | `ps/VehiclePBR_Modern.hlsl:main_specCarFx` | pipelinecommon.cpp:905 | vehiclePipe.cpp:1306 | live |
| 216 | `IDR_XBOXCARVS` | xboxCarVS.cso | `vehiclePipeVS.hlsl:main_xboxCar` | pipelinecommon.cpp:913 | vehiclePipe.cpp:1428 | live |
| 217 | `IDR_LEEDSCARFXVS` | leedsCarFxVS.cso | `vehiclePipeVS.hlsl:main_leedsCarFx` | pipelinecommon.cpp:914 | vehiclePipe.cpp:1736 | live |
| 218 | `IDR_MOBILEVEHICLEVS` | mobileVehicleVS.cso | `vehiclePipeVS.hlsl:main_mobileVehicle` | pipelinecommon.cpp:915 | vehiclePipe.cpp:1974 | live |
| 219 | `IDR_MOBILEVEHICLEPS` | mobileVehiclePS.cso | `ps/VehiclePBR_Modern.hlsl:main_mobileVehicle` | pipelinecommon.cpp:916 | vehiclePipe.cpp:1976 | live |
| 220 | `IDR_GTAIVVEHICLEVS` | GTAIVVehicle_vs.cso | — prebuilt | pipelinecommon.cpp:917 | vehiclePipe.cpp:1059 (`ivVS`) | live |
| 221 | `IDR_GTAIVVEHICLEPS` | GTAIVVehicle_ps.cso | — prebuilt | pipelinecommon.cpp:918 | vehiclePipe.cpp:1001 | live |
| 222 | `IDR_PS2BUILDINGVS` | ps2BuildingVS.cso | `vs/ps2BuildingVS.hlsl:main` | pipelinecommon.cpp:926 | buildingPipe.cpp:1242 | live |
| 223 | `IDR_PS2BUILDINGFXVS` | ps2BuildingFxVS.cso | `vs/ps2BuildingFxVS.hlsl:main` | pipelinecommon.cpp:927 | buildingPipe.cpp:1280 | live |
| 224 | `IDR_PS2BUILDINGWINDVS` | ps2BuildingWindVS.cso | `vs/ps2BuildingWindVS.hlsl:main` | pipelinecommon.cpp:928 | buildingPipe.cpp:1237 | live |
| 225 | `IDR_XBOXBUILDINGVS` | xboxBuildingVS.cso | `vs/xboxBuildingVS.hlsl:main` | pipelinecommon.cpp:929 | buildingPipe.cpp:1588 | live |
| 226 | `IDR_XBOXBUILDINGPS` | xboxBuildingPS.cso | `buildingPipePS.hlsl:main_xboxBuilding` | pipelinecommon.cpp:932 | buildingPipe.cpp:1639 (`selPS`) | live |
| 227 | `IDR_XBOXBUILDINGSTOCHASTICPS` | xboxBuildingStochasticPS.cso | `stochasticBuildingPS.hlsl:main_xboxBuildingStochastic` | pipelinecommon.cpp:933 | buildingPipe.cpp:1639 (`selPS`) | live |
| 228 | `IDR_XBOXBUILDINGWINDVS` | xboxBuildingWindVS.cso | `vs/xboxBuildingWindVS.hlsl:main` | pipelinecommon.cpp:934 | buildingPipe.cpp:1584 | live |
| 229 | `IDR_SPHEREBUILDINGVS` | sphereBuildingVS.cso | `vs/sphereBuildingVS.hlsl:main` | pipelinecommon.cpp:935 | buildingPipe.cpp:1734 | live |
| 230 | `IDR_SIMPLEDETAILPS` | simpleDetailPS.cso | `buildingPipePS.hlsl:main_simpleDetail` | pipelinecommon.cpp:936 | buildingPipe.cpp:1639 (`selPS`) | live |
| 231 | `IDR_SIMPLEDETAILSTOCHASTICPS` | simpleDetailStochasticPS.cso | `stochasticBuildingPS.hlsl:main_simpleDetailStochastic` | pipelinecommon.cpp:937 | buildingPipe.cpp:1639 (`selPS`) | live |
| 232 | `IDR_SIMPLEFOGPS` | simpleFogPS.cso | `buildingPipePS.hlsl:main_simpleFog` | pipelinecommon.cpp:938 | buildingPipe.cpp:1736 | live |
| 233 | `IDR_GTAIVBUILDINGVS` | GTAIVBuilding_vs.cso | `vs/GTAIVBuilding_vs.hlsl:main` | pipelinecommon.cpp:922 | buildingPipe.cpp:1213 | live |
| 234 | `IDR_GTAIVBUILDINGPS` | GTAIVBuilding_ps.cso | — prebuilt | pipelinecommon.cpp:923 | buildingPipe.cpp:1215 | live |
| 235 | `IDR_CUSTOMBUILDINGVS` | customBuildingVS.cso | — prebuilt | pipelinecommon.cpp:732 (lazy) | — (getter never called) | **lazy/orphan** |
| 236 | `IDR_NORMMAPBUILDINGPS` | normMapBuildingPS.cso | `buildingPipePS.hlsl:main_normMapBuilding` | pipelinecommon.cpp:744 (lazy) | — (getter never called) | **lazy/orphan** |
| 237 | `IDR_NORMMAPVEHICLEPS` | normMapVehiclePS.cso | `ps/VehiclePBR_Modern.hlsl:main_normMapVehicle` | pipelinecommon.cpp:758 (lazy) | — (getter never called; PBR uses c48 flag instead) | **lazy/orphan** |
| 238 | `IDR_BUILDINGPBRVS` | buildingPBRVS.cso | `vs/buildingPBRVS.hlsl:main` | pipelinecommon.cpp:930 | buildingPipe.cpp:1967 | live |
| 239 | `IDR_BUILDINGPBRPS` | BuildingPBRPS.cso | `ps/VehiclePBR_Modern.hlsl:main_building` | pipelinecommon.cpp:931 | buildingPipe.cpp:1795 | live |
| 240 | `IDR_RUBBER_VEHICLE_MODERN` | Rubber_Vehicle_Modern.cso | `ps/VehiclePBR_Modern.hlsl:main_rubber` | pipelinecommon.cpp:882 | vehiclePipe.cpp:2583 | live |
| 241 | `IDR_WATER_IV` | Water_IV.cso | `ps/Water_Parallax.hlsl:main_iv` | pipelinecommon.cpp:876 | waterPipe.cpp:1062 via `ps` | live |
| 242 | `IDR_WATER_V` | Water_V.cso | `ps/Water_Parallax.hlsl:main_v` | pipelinecommon.cpp:877 | waterPipe.cpp:1062 via `ps` | live |
| 250 | `IDR_HEIGHTFOG` | HeightFog.cso | `ps/HeightFog.hlsl:main` | pipelinecommon.cpp:889 | postfx.cpp:4034 | live |
| 251 | `IDR_GODRAYS` | GodRays.cso | `ps/GodRays.hlsl:main` | pipelinecommon.cpp:890 | postfx.cpp:4268 | live |
| 252 | `IDR_VELOCITYRECONSTRUCT` | VelocityReconstruct.cso | `ps/VelocityReconstruct.hlsl:main` | pipelinecommon.cpp:891 | postfx_motionblur.cpp:220 | live |
| 253 | `IDR_LUMINANCEREDUCE` | LuminanceReduce.cso | `ps/LuminanceReduce.hlsl:main` | pipelinecommon.cpp:829 | postfx_luminance.cpp:175 | live |
| 254 | `IDR_LUMINANCEADAPT` | LuminanceAdapt.cso | `ps/LuminanceAdapt.hlsl:main` | pipelinecommon.cpp:830 | postfx_luminance.cpp:142 | live |
| 260 | `IDR_FXAA3` | FXAA3.cso | `ps/FXAA3.hlsl:main` | postfx_newfx.cpp:102 | postfx_newfx.cpp DrawFXAA3 | live |
| 261 | `IDR_GRAIN` | Grain.cso | `ps/Grain.hlsl:main` | postfx_newfx.cpp:103 | postfx_newfx.cpp DrawFilmGrain | live |
| 262 | `IDR_CA` | CA.cso | `ps/CA.hlsl:main` | postfx_newfx.cpp:104 | postfx_newfx.cpp DrawChromaticAberration | live |
| 263 | `IDR_VIGNETTE` | Vignette.cso | `ps/Vignette.hlsl:main` | postfx_newfx.cpp:105 | postfx_newfx.cpp DrawVignette | live |
| 264 | `IDR_VECTORMOTIONBLUR` | vectorMotionBlur.cso | `ps/vectorMotionBlur.hlsl:main` | pipelinecommon.cpp:773 (lazy) | — (getter never called) | **lazy/orphan** |
| 265 | `IDR_DOF_COC` | DOF_CoC.cso | `ps/DOF_CoC.hlsl:main` | postfx_newfx.cpp:92 | postfx_newfx.cpp DrawDOF | live |
| 266 | `IDR_DOF_BOKEH` | DOF_Bokeh.cso | `ps/DOF_Bokeh.hlsl:main` | postfx_newfx.cpp:93 | postfx_newfx.cpp DrawDOF | live |
| 267 | `IDR_DOF_TENT` | DOF_Tent.cso | `ps/DOF_Tent.hlsl:main` | postfx_newfx.cpp:94 | postfx_newfx.cpp DrawDOF | live |
| 268 | `IDR_DOF_COMBINE` | DOF_Combine.cso | `ps/DOF_Combine.hlsl:main` | postfx_newfx.cpp:95 | postfx_newfx.cpp DrawDOF | live |
| 269 | `IDR_TONEMAPLUT` | TonemapLUT.cso | `ps/TonemapLUT.hlsl:main` | postfx_newfx.cpp:96 | colourfilter.cpp tonemap branch | live |
| 270 | `IDR_BLOOM_EXTRACT` | Bloom_Extract.cso | `ps/Bloom_Extract.hlsl:main` | postfx_newfx.cpp:97 | postfx_newfx.cpp DrawBloom | live |
| 271 | `IDR_BLOOM_DOWNSAMPLE` | Bloom_Downsample.cso | `ps/Bloom_Downsample.hlsl:main` | postfx_newfx.cpp:98 | postfx_newfx.cpp DrawBloom | live |
| 272 | `IDR_BLOOM_UPSAMPLE` | Bloom_Upsample.cso | `ps/Bloom_Upsample.hlsl:main` | postfx_newfx.cpp:99 | postfx_newfx.cpp DrawBloom | live |
| 273 | `IDR_BLOOM_COMPOSITE` | Bloom_Composite.cso | `ps/Bloom_Composite.hlsl:main` | postfx_newfx.cpp:100 | postfx_newfx.cpp DrawBloom | live |
| 274 | `IDR_SUNSHAFTS` | SunShafts.cso | `ps/SunShafts.hlsl:main` | postfx_newfx.cpp:101 | postfx_newfx.cpp DrawSunShafts | live |
| 275 | `IDR_PEDPBRVS` | pedPBRVS.cso | `vs/pedPBRVS.hlsl:main` | **never** | — | **dead** |
| 276 | `IDR_WATER_UNDERWATER` | Water_Underwater.cso | `ps/Water_Underwater.hlsl:main` | postfx_underwater.cpp:284 | postfx_underwater.cpp:496 | live |
| 277 | `IDR_MIRROR_COMPOSITE` | Mirror_Composite.cso | `ps/Mirror_Composite.hlsl:main` | mirrorPass.cpp:117 | mirrorPass.cpp:203 | live |
| 279 | `IDR_TREEALPHA` | TreeAlpha.cso | `ps/TreeAlpha.hlsl:main` | pipelinecommon.cpp:898 | buildingPipe.cpp:993 | live |

**ID gaps:** 103–109, 113–119, 135–139, 145–159, 167–199, 243–249, 255–259, 278, 280+ are unallocated. `resource.h:134` comment reserves 278 for a future lane and claims 276/277/279 for W7-D/W7-C/W8-2.

### 2.3 Orphan RC shader IDs — 16 (+1 semi-live)

| Kind | IDs |
|---|---|
| Loaded, never drawn | `IDR_GTAIVPS` (110), `IDR_SMAAPS` (121), `IDR_SMAAEDGENORMALPS` (125), `IDR_SMAAEDGEDEPTHPS` (126), `IDR_SMAAEDGECOMBINEDPS` (128), `IDR_SSAO_VERTEXDEPTH` (130), `IDR_COLORFILTER_CROSSMIX` (141), `IDR_VEHICLEPAINT_GTAIV` (200), `IDR_CARPAINT_REFL` (206), `IDR_PBR_LIGHTING` (207) |
| Lazy getter, zero call sites | `IDR_SKINPBR` (210), `IDR_CUSTOMBUILDINGVS` (235), `IDR_NORMMAPBUILDINGPS` (236), `IDR_NORMMAPVEHICLEPS` (237), `IDR_VECTORMOTIONBLUR` (264) |
| Never loaded | `IDR_PEDPBRVS` (275) |
| Gate-only (boolean, never bound) | `IDR_RUBBER_VEHICLE` (205) |

### 2.4 CSOs on disk not embedded in RC — 21

`GTA_SA_ModernColor`, `GrassInstanced`, `HairEnhance`, `IBL_SkyCloud`, `SSAO_ps20_depthonly`, `SSAO_ps20_simple`, `SkinEnhance`, `buildingPipePS`, `iiiTrailsPS`, `mobileBuildingVS`, `neoVehiclePass1VS`, `neoVehiclePass2VS`, `postfxVS`, `ps2BuildingSSSVS`, `simpleStochasticPS`, `unifiedPipe`, `unifiedPipePre`, `unifiedPipeOcclusion`, `unifiedPipePost`, `unifiedPipeIBL`, `vehiclePipeVS`.

- `GrassInstanced.cso` is produced and referenced by `#ifdef IDR_GRASSINSTANCED` but `IDR_GRASSINSTANCED` is **not defined** → the instanced plant path silently falls back and `s_grassInstPSTried` latches failure forever (`PC_GrassInstancer.cpp:99-123`).
- `unifiedPipe*.cso` (5) are WIP Forward+ chain, no RC wiring, no C++ consumer.
- `neoVehiclePass1/2VS` are explicitly dead per `fast_build.py:390-395` and `neoCarpipe.cpp` (“VS passes removed”).
- `buildingPipePS.cso` / `vehiclePipeVS.cso` are stale pre-multi-entry artifacts (those filenames are no longer emitted by `get_shader_list()`).

### 2.5 CSOs with no HLSL producer — 11

`GTAIVBuilding_ps`, `GTAIVForwardPlus_ps`, `GTAIVForwardPlus_vs`, `GTAIVVehicle_ps`, `GTAIVVehicle_vs`, `SMAA`, `customBuildingVS`, `buildingPipePS`, `vehiclePipeVS`, `neoVehiclePass1VS`, `neoVehiclePass2VS`.

Of these, the five GTAIV binaries are intentional prebuilts documented at `fast_build.py:366-371` (`GTAIVBuilding_vs` is the one GTAIV VS with audited source and is rebuilt). `SMAA`, `customBuildingVS`, `buildingPipePS`, `vehiclePipeVS`, `neoVehiclePass1VS/2VS` are stale/legacy with no producer in the current build list.

### 2.6 HLSL entry points that never reach the DLL — 17 CSOs

| CSO | HLSL : entry | Why unreachable |
|---|---|---|
| `GTA_SA_ModernColor.cso` | `ps/GTA_SA_ModernColor.hlsl:main` | no `IDR_*`, no `makePS` |
| `GrassInstanced.cso` | `ps/GrassInstanced.hlsl:main` | `IDR_GRASSINSTANCED` undefined (deferred RC diff) |
| `HairEnhance.cso` | `ps/HairEnhance.hlsl:main` | no `IDR_*` |
| `IBL_SkyCloud.cso` | `ps/IBL_SkyCloud.hlsl:main` | no `IDR_*` |
| `SSAO_ps20_depthonly.cso` | `ps/SSAO_ps20_depthonly.hlsl:main` | legacy ps_2_0 variant, no `IDR_*` |
| `SSAO_ps20_simple.cso` | `ps/SSAO_ps20_simple.hlsl:main` | legacy ps_2_0 variant, no `IDR_*` |
| `SkinEnhance.cso` | `ps/SkinEnhance.hlsl:main` | no `IDR_*` |
| `iiiTrailsPS.cso` | `ps/iiiTrailsPS.hlsl:main` | no `IDR_*` |
| `mobileBuildingVS.cso` | `vs/mobileBuildingVS.hlsl:main` | no `IDR_*` (mobile uses Xbox building VS) |
| `postfxVS.cso` | `vs/postfxVS.hlsl:main` | no `IDR_*` (Im2D uses FFVS) |
| `ps2BuildingSSSVS.cso` | `vs/ps2BuildingSSSVS.hlsl:main` | no `IDR_*` |
| `simpleStochasticPS.cso` | `stochasticBuildingPS.hlsl:main_simpleStochastic` | no `IDR_*` (only detail-stochastic variant is embedded) |
| `unifiedPipe.cso` | `unifiedPipe.hlsl:main` | WIP Forward+ chain, no RC |
| `unifiedPipePre.cso` | `unifiedPipe.hlsl:main_pre` | WIP |
| `unifiedPipeOcclusion.cso` | `unifiedPipe.hlsl:main_occlusion` | WIP |
| `unifiedPipePost.cso` | `unifiedPipe.hlsl:main_post` | WIP |
| `unifiedPipeIBL.cso` | `unifiedPipe.hlsl:main_ibl` | WIP |

The 17 above + the 99 RC rows = every one of the 110 compiled entry points is accounted for (RC rows also include 11 prebuilt/no-source CSOs, so the partition is not 1:1 by count).

Additionally, these HLSL files are compiled or present but superseded: `ps/simplePS.hlsl`, `ps/simpleDetailPS.hlsl`, `ps/simpleFogPS.hlsl`, `ps/xboxBuildingPS.hlsl`, `ps/normMapBuildingPS.hlsl`, `ps/simpleDetailStochasticPS.hlsl`, `ps/xboxBuildingStochasticPS.hlsl`, `ps/normMapVehiclePS.hlsl` (all skipped by fast_build; the root consolidated files produce their CSOs). They are source duplicates of `buildingPipePS.hlsl` / `stochasticBuildingPS.hlsl` / `VehiclePBR_Modern.hlsl` and will drift.

---

## 3. DEAD CODE

### 3.1 Functions with zero callers (verified)

| File:line | Function | LOC | Note |
|---|---|---|---|
| `src/core/test.cpp:24` | `dumpMenu` | 28 | Whole file dead; global `screens` also dead |
| `src/core/main.cpp:1929` | `CreateRoadsignTexture_RwTextureSetName` | 6 | Never `InjectHook`ed; `_orig` wrapper at 1927 also unused |
| `src/core/menu_inject.cpp:279` | `draw_skygfx_options` | 67 | Never referenced by screen table |
| `src/core/menu_inject.cpp:351` | `handle_skygfx_input` | 45 | Same |
| `src/entities/chars.cpp:113` | `chars_classifyByTexture` | 32 | Only declared, never called |
| `src/entities/chars.cpp:267` | `classifyCB` | 7 | Only used by dead `chars_classifyClump` |
| `src/entities/chars.cpp:275` | `chars_classifyClump` | 12 | Never called |
| `src/entities/chars.cpp:653` | `chars_getPartParams` | 6 | Never called |
| `src/entities/chars.cpp:660` | `chars_buildClassificationBuffer` | 17 | Never called |
| `src/entities/chars.cpp:678` | `chars_shutdown` | 12 | Never called |
| `src/extras/brdfLibrary.h:496` | `PackBRDFConstants` | 10 | `static inline`, no users |
| `src/extras/ragdoll_death.cpp:155` | `ReadIniInt` | 3 | Duplicate of `ReadIniFloat` pattern |
| `src/extras/skygfx_bridge.cpp:27,72,98,111,124` | `Bridge_Init/SendCommand/ReceiveCommand/GetPendingCount/DllMain` | 93 | TU not built |
| `src/extras/vehicles.cpp:377-378` | `GetVehicleDBCount`, `GetVehicleDB` | 2 | No users |
| `src/render/buildingPipe.cpp:318` | `CVisibilityPlugins__GetModelInfo` | 1 | WRAPPER, never called |
| `src/render/pipelinecommon.cpp:727` | `Pipeline_GetCustomBuildingVS` | 9 | Getter never called |
| `src/render/pipelinecommon.cpp:739` | `Pipeline_GetNormMapBuildingPS` | 9 | Getter never called |
| `src/render/pipelinecommon.cpp:753` | `Pipeline_GetNormMapVehiclePS` | 9 | Getter never called |
| `src/render/pipelinecommon.cpp:767` | `Pipeline_GetVectorMotionBlurPS` | 11 | Getter never called |
| `src/render/pipelinecommon.cpp:784` | `Pipeline_GetSkinSSSPS` | 9 | Getter never called |
| `src/rw/HLSL_hook.cpp:5,12,17` | `CompileVertexShader`, `CompilePixelShader`, `LoadShaderFromFile` | 25 | All return null; no callers |
| `src/render/postfx_colourfilter.cpp:855` | `CPostEffects::ColourFilter_PC` | 38 | Inside `#if 0` “For reference only” |

**Total: 29 functions, ~470 LOC.** Excluded from this list: RW `WRAPPER` thunks installed/exported by address (they are API surface, and some are referenced only from the excluded `normalmap_plugin.cpp`), and vtable/inline-hook targets (`hook_SetDS@167`, `hook_Reset@329`, `Im2DColorModulationHook@426`, `Im2DAlphaModulationHook@433`, `CCarFXRenderer__IsCCPCPipelineAttached@3219`, `RwTextureRead_HACK@3227`, `custom pipe hooks`) — these have zero textual callers but are installed by address at `main.cpp`/`postfx.cpp`/`vehiclePipe.cpp`/`depthhook.cpp` and are **live**.

### 3.2 Dead globals / resources

| Global | File:line | Evidence |
|---|---|---|
| `GTAIV_PS` | `pipelinecommon.cpp:55` | Loaded at 857, never referenced |
| `VehiclePaint_GTAIV` | `pipelinecommon.cpp:37` | Loaded at 864, never referenced |
| `SkinPBR` | `pipelinecommon.cpp:50` | Only lazy getter, getter never called |
| `SMAA_EdgeNormal`, `SMAA_EdgeDepth`, `SMAA_EdgeCombined` | `pipelinecommon.cpp:21-23` | Loaded, zero uses |
| `CarPaint_Reflections` | `pipelinecommon.cpp:46` | Loaded, only logged |
| `PBR_Lighting` | `pipelinecommon.cpp:47` | Loaded, only logged |
| `s_customBuildingVS`, `s_normMapBuildingPS`, `s_normMapVehiclePS`, `s_vectorMotionBlurPS` | `pipelinecommon.cpp:64-67` | Getters never called |
| `screens` | `core/test.cpp:22` | No users |
| `g_pUtilityNoise` | `render/utilitynoise.cpp:26` | Generated, read by debugmenu status only; **no shader binds it** (comment says Wave 2) |
| `Rubber_Vehicle` | `pipelinecommon.cpp:44` | Only boolean gate at `vehiclePipe.cpp:2329`, never bound |
| `s_uwPS` if feature off | `postfx_underwater.cpp:135` | Loaded lazily; no default consumer until `PostFX_UnderwaterTick` runs |

### 3.3 Commented-out / excluded TUs

| File | LOC | vcxproj state | Reason |
|---|---|---|---|
| `src/rw/normalmap_plugin.cpp` | 1,314 | commented, line 248 | full DK plugin port superseded by `rw/normalmap.cpp` + `rpnormmap.lib` |
| `src/render/SMAA.cpp` | 374 | commented, line 249 | superseded by `postfx_smaa.cpp` |
| `src/render/PC_PlantsMgr.cpp` | 2,175 | `ExcludedFromBuild` lines 202-204 | needs absent SA PC game source (`general.h`/`handy.h`) |
| `src/render/PC_PlantsMgr_overlay.cpp` | 1,035 | `ExcludedFromBuild` lines 205-207 | same |
| `src/extras/skygfx_bridge.cpp` | 134 | **absent from project** | second `DllMain`; would collide under premake glob |
| **Total** | **5,032** | | |

Headers with zero includers: `core/hooks.h` (27), `render/EdgeDetection.h` (146), `render/PostFXPipeline.h` (103), `render/GrassSystem.h` (101), `render/SMAA.h` (177, only `SMAA.cpp`). = **554 LOC**.

### 3.4 `#if 0` blocks

| File:line | Lines | Content |
|---|---|---|
| `src/render/neo.cpp:143-209` | 67 | Old `CarPipe::Init` / pipeline setup |
| `src/render/postfx_colourfilter.cpp:853-893` | 41 | `CPostEffects::ColourFilter_PC` reference copy |
| `src/render/PC_GrassRenderer.cpp:509-532` | 24 | Old flush path |
| `src/render/PC_PlantsMgr_overlay.cpp:168-189` | 22 | Old plant init |
| `src/render/PC_PlantsMgr.cpp:673-685` | 13 | Old collision setup |
| `src/render/vehiclePipe.cpp:3242-3248` | 7 | Old hook |
| **Total** | **174** | (115 in compiled TUs; 59 in excluded TUs) |

### 3.5 Stale one-shot latches (device-reset hazards)

These latches permanently suppress a retry; none is cleared by `OnDeviceReset()`/`hook_Reset` (`depthhook.cpp:329`), so after a D3D9 Reset the cached shader handle is stale and never recreated. `CreateShaders()` is rebuilt (`pipelinecommon.cpp:808-812`, `depthhook.cpp:371-372`) but these lazy paths are not.

| Latch | File:line | Consequence |
|---|---|---|
| `s_fxLoadTried` | `postfx_newfx.cpp:85` | DOF/bloom/CA/grain/vignette/FXAA/LUT/sunshafts handles dead after Reset |
| `s_uwLoadTried` | `postfx_underwater.cpp:136` | underwater PS dead after Reset |
| `s_uwProbeTried` | `postfx_underwater.cpp:131` | water-plane probe never retried |
| `mirrorPSLoadTried` | `mirrorPass.cpp:98` | mirror composite PS dead after Reset |
| `s_grassInstPSTried` | `PC_GrassInstancer.cpp:72` | latches the missing-`IDR_GRASSINSTANCED` failure permanently |
| `ivFPReady` `static attempted` | `buildingPipe.cpp:1010` | GTAIVForwardPlus pair dead after Reset |
| 5 × `static attempted` | `pipelinecommon.cpp:729,741,755,770,786` | dead getters (unreachable) |
| `s_wbAttempted` | `waterPipe.cpp:241` | **OK** — cleared by `waterPipe_init/shutdown` (543/553) |

### 3.6 Unused / no-op `Config` fields (cross-check vs `docs/plans/2026-10-24-imgui-settings-coverage.md`)

Coverage doc snapshot said 436 fields, 233 parsed, 203 unparsed. Current tree: **436 fields, 231 assigned by `readIniFile`, 198 never referenced outside `skygfx.h`**. Doc deltas verified today:

| Field | Line | Verdict |
|---|---|---|
| `stateBaselineEnable` | `skygfx.h:615` | **Doc is stale** — now consumed at `main.cpp:2146, 2639, 2662, 5687` |
| `ssaoKernelSize` | `skygfx.h:381` | **No consumer** (parse/save/menu/preset only) — no-op knob |
| `ssaoSampleCount` | `skygfx.h:382` | **No consumer** — no-op knob |
| `vignetteRadius` | `skygfx.h:512` | **No consumer** (IV grade uses `ivVignetteRadius`); legacy field |
| `vignetteContrast` | `skygfx.h:512` | **No consumer** (uses `ivVignetteContrast`) |
| Reserved `_reserved_smaa*` (3) | `skygfx.h` | Intentionally reserved |

The remaining 193 zero-reference fields are the documented legacy weather/cloud/sky/sun blocks (e.g. `skyZenith*`, `sunMie*`, `cloudGen*`, `moon*`) — kept for future DynamicSky work, not wired to any shader.

---

## 4. DUPLICATION

Quantified with an 8-line normalized rolling-window clone detector; spans below are maximal ≥15-line matches.

| # | Duplicate pair (file:line ↔ file:line) | LOC | What | Suggested single owner |
|---|---|---|---|---|
| 1 | `render/PC_PlantsMgr.cpp:1161` ↔ `:1381` | 88 | Collision-section processing body duplicated verbatim in one file | (moot if TU archived) |
| 2 | `render/buildingPipe.cpp:2581` ↔ `rw/defaultFuncs.cpp:148` | 36 | `DNInstance` reinstance/vertex-declaration clone | `rw/defaultFuncs.cpp` |
| 3 | `render/buildingPipe.cpp:2623` ↔ `rw/defaultFuncs.cpp:182` | 58 | D3D9 vertex-declaration builder (`dcl[i++] = …`) | `rw/defaultFuncs.cpp` |
| 4 | `render/buildingPipe.cpp:2110` ↔ `render/vehiclePipe.cpp:2245` | 30 | Per-material normal-map + texalpha classification + c48/c8 upload | new `materialBind.cpp` (`pipeBindMaterial`) |
| 5 | `render/vehiclePipe.cpp:2536` ↔ `:2833` | 27 | Env-map/PBR parameter upload block | one helper in `vehiclePipe.cpp` |
| 6 | `render/vehiclePipe.cpp:366` ↔ `:451` | 20 | `scaledfract` env-mat hash | one `EnvMatHash()` |
| 7 | `render/vehiclePipe.cpp:900` ↔ `:1180` | 19 | Render-callback prologue (resEntryHeader/instanceData) | `pipeRenderPrologue()` |
| 8 | `render/vehiclePipe.cpp:1098` ↔ `:1258` | 21 | MatFX env-map detection branch | one `IsEnvMappedMat()` |
| 9 | `render/envmap.cpp:189` ↔ `render/neoCarpipe.cpp:69` | 18 | Screen-quad creation | `postfx_utils.cpp` (`s_ffQuad`) |
| 10 | `render/envmap.cpp:210` ↔ `render/neoCarpipe.cpp:91` | 25 | Env-map raster size/camera rebind | `envmap.cpp` |
| 11 | `render/depthhook.cpp:116` ↔ `render/forwardplus.cpp:907` | 27 | Trusted screen-size clamp (`DpGetTrustedScreenSize` vs `FpGetTrustedScreenSize`) | `postfx_utils.cpp` (`GetScreenSize`) |
| 12 | `render/postfx_ssao.cpp:658` ↔ `:1025` | 22 | Depth-bind + state cleanup between SSAO passes | `postfx_ssao.cpp` helper |
| 13 | `render/postfx_colourfilter.cpp:192` ↔ `:681` | 18 | Render-state block (filter/zwrite/blend/fog) | `postfx_utils.cpp` state helpers |
| 14 | `render/postfx.cpp:4044` ↔ `render/postfx_underwater.cpp:501` | 21 | PS-constant save/restore + `RwCameraEnd/BeginUpdate` | `postfx_utils.cpp` |
| 15 | `core/main.cpp:270` ↔ `rw/defaultFuncs.cpp:16` | 18 | Pipeline clone/instance boilerplate | `rw/defaultFuncs.cpp` |
| 16 | `core/main.cpp:5583-5646` ↔ `core/debugmenu_ui.cpp:77-158` | 74 | Two divergent `MENUSETTINGS` X-macros (72 vs ~100 entries) | one macro in `skygfx.h` |
| 17 | `render/{ps2,xbox,mobile,iv,pc_patched,custom}_mode.cpp` | **675** | Six near-identical preset + `ApplyDefaults` + `GetPreset` TUs, zero callers | `core/modeManager.cpp` (already owns profiles) |
| 18 | `render/postfx_newfx.cpp:87-110` ↔ `render/PC_GrassInstancer.cpp:104-123` ↔ `render/mirrorPass.cpp:92-129` ↔ `pipelinecommon.cpp:665-713` | ~100 | Four hand-rolled `FindResource`/`LoadResource`/`Create*Shader` loaders | `pipelinecommon.cpp` `makePS/makeVS` |
| 19 | `render/postfx_motionblur.cpp:39` ↔ `render/postfx.cpp:3615` | 16 | Velocity/screen-size guard | `postfx_utils.cpp` |
| 20 | `render/custom_mode.cpp:73` ↔ `render/mobile.cpp:66` ↔ `xbox_mode.cpp:78` ↔ `iv_mode.cpp:46` | ~80 | Identical pipe/ps2/dualpass/IV reset sequence | `modeManager.cpp` |

**Quantified total:** ≈**1,220 LOC** (≈1.7% of `src/`). The two biggest single wins are the six mode TUs (675) and the `buildingPipe`/`defaultFuncs` clone pair (94).

---

## 5. COMPLEXITY HOTSPOTS

Score = `LOC + 3×branches + 2×raw-device-calls`. All are state-desync candidates because they write D3D9 state directly and rely on manual restore.

| # | File:line | Function | LOC | Branches | Raw dev | Score |
|---|---|---|---|---|---|---|
| 1 | `postfx_smaa.cpp:441` | `DrawSMAA` | 888 | 127 | 60 | 1389 |
| 2 | `postfx.cpp:1331` | `ColourFilter_switch` | 625 | 82 | 0 | 871 |
| 3 | `waterPipe.cpp:890` | `waterPipe_setRenderState` | 633 | 50 | 0 | 783 |
| 4 | `postfx_colourfilter.cpp:150` | `ColourFilter_Modern` | 522 | 51 | 3 | 681 |
| 5 | `postfx.cpp:3177` | `DrawPipeChain` | 407 | 43 | 63 | 662 |
| 6 | `extras/ragdoll_death.cpp:411` | `Ragdoll_Activate` | 463 | 48 | 0 | 607 |
| 7 | `postfx_ssao.cpp:706` | `DrawSSAO_Overhaul` | 373 | 25 | 48 | 544 |
| 8 | `forwardplus.cpp:1384` | `ForwardPlus_DrawClusterDebug` | 265 | 36 | 66 | 505 |
| 9 | `entities/chars.cpp:321` | `chars_drawSSSBlur` | 323 | 45 | 13 | 484 |
| 10 | `debugmenu_ui.cpp:3397` | `DrawUnifiedDebugMenu` | 277 | 46 | 0 | 415 |
| 11 | `postfx_motionblur.cpp:23` | `DrawVelocityBuffer` | 250 | 30 | 27 | 394 |
| 12 | `postfx.cpp:3586` | `DrawMotionBlur` | 267 | 32 | 15 | 393 |
| 13 | `core/weather.cpp:285` | `Weather_Update` | 182 | 54 | 0 | 344 |
| 14 | `forwardplus.cpp:990` | `ForwardPlus_CullAndUpload` | 234 | 34 | 3 | 342 |
| 15 | `PC_PlantsMgr.cpp:1266` | `_ProcessEntryCollisionData` | 245 | 30 | 0 | 335 (excluded TU) |
| 16 | `PC_PlantsMgr.cpp:414` | `CPlantMgr::Render` | 241 | 30 | 0 | 331 (excluded TU) |
| 17 | `extras/dismember.cpp:197` | `BuildGibClump` | 208 | 38 | 0 | 322 |
| 18 | `rw/normalmap_plugin.cpp:906` | `BuildPlayerNormalMapTextures` | 197 | 36 | 0 | 305 (excluded TU) |
| 19 | `extras/ragdoll_death.cpp:963` | `Ragdoll_Simulate` | 181 | 41 | 0 | 304 |
| 20 | `postfx_ssao.cpp:518` | `DrawSSAO` | 187 | 24 | 14 | 287 |
| 21 | `vegImposter.cpp:239` | `VegImposter_Ensure` | 182 | 33 | 0 | 281 (inert) |
| 22 | `postfx.cpp:2354` | `RenderIBLBuffer` | 171 | 24 | 9 | 261 |
| 23 | `depthhook.cpp:167` | `hook_SetDS` | 150 | 35 | 2 | 259 |
| 24 | `PC_PlantsMgr_overlay.cpp:869` | `LoadPlantsDat` | 152 | 34 | 0 | 254 (excluded) |
| 25 | `postfx.cpp:1120` | `Radiosity` | 196 | 19 | 0 | 253 |
| 26 | `postfx.cpp:2708` | `DrawNormalBufferToTexture` | 153 | 20 | 20 | 253 |
| 27 | `PC_GrassRenderer.cpp:257` | `DrawTriPlants` | 193 | 17 | 0 | 244 |
| 28 | `PC_PlantsMgr.cpp:715` | `_ColEntityCache_Update` | 155 | 25 | 0 | 230 (excluded) |
| 29 | `brdfLibrary.h:524` | `GetSurfaceTypeFromMaterial` | 104 | 39 | 0 | 221 |
| 30 | `vegImposter.cpp:428` | `BeginTile` | 145 | 18 | 11 | 221 (inert) |

### Raw D3D9 device writes per file (25 files, ≈928 total)

| File | Writes |
|---|---|
| `render/postfx.cpp` | 150 |
| `core/main.cpp` | 111 |
| `render/postfx_ssao.cpp` | 104 |
| `render/forwardplus.cpp` | 86 |
| `render/vehiclePipe.cpp` | 82 |
| `render/postfx_smaa.cpp` | 81 |
| `render/buildingPipe.cpp` | 65 |
| `render/postfx_luminance.cpp` | 46 |
| `render/pipelinecommon.cpp` | 36 |
| `render/postfx_newfx.cpp` | 36 |
| `render/postfx_motionblur.cpp` | 27 |
| `render/vegImposter.cpp` | 16 |
| `entities/chars.cpp` | 14 |
| `render/SMAA.cpp` | 12 (not built) |
| `core/debugmenu_ui.cpp` | 11 |
| `render/depthhook.cpp` | 11 |
| `render/neoCarpipe.cpp` | 9 |
| `render/postfx_underwater.cpp` | 8 |
| `render/neoWaterdrops.cpp` | 6 |
| `render/postfx_colourfilter.cpp` | 4 |
| `render/profiler.cpp` | 4 |
| `render/postfx_utils.cpp` | 3 |
| `render/mirrorPass.cpp` | 2 |
| `render/treeWind.cpp` | 2 |
| `render/vegImposter.h` | 2 |

**Highest state-desync risk:** `DrawSMAA` (60 writes inside a SEH-guarded pass with manual bail), `DrawPipeChain` (63), `ForwardPlus_DrawClusterDebug` (66), `DrawSSAO_Overhaul` (48), and `waterPipe_setRenderState` (633 LOC, zero raw writes but a 1,500-line save/restore pair).

---

## 6. TODO / FIXME / HACK / XXX CENSUS

**Actionable (10):**

| File:line | Text |
|---|---|
| `core/debugmenu_ui.cpp:253` | `// TODO: allow III/VC somehow?` |
| `entities/chars.cpp:230` | `// see its tail and the TODO in chars_buildClassificationBuffer). The` |
| `entities/chars.cpp:671` | `// REMAINING TODO: wire chars_classifyClump into the ped render path —` |
| `render/postfx.cpp:652` | `// TODO: tex coords correct?` |
| `render/postfx_colourfilter.cpp:61` | `// TODO (INI plumbing): a real 'ivExposurePostCap' config knob still needs the` |
| `render/postfx_colourfilter.cpp:1498` | `// s_ivExpCap (file-static, default 1.5 = old literal; INI plumbing TODO).` |
| `render/vehiclePipe.cpp:3227` | `RwTextureRead_HACK` (name; live `InjectHook` at 3251) |
| `render/vehiclePipe.cpp:3249` | `// temp hack RwTextureRead to read other env map` |
| `rw/pipeplg.cpp:19` | `// PDS pipeline hack` |
| `shaders/ps/NormalBuffer.hlsl:86` | `// TODO: multiply by inverse view matrix for true world-space normals.` |

**False positives (9):** `shaders/include/LUTOps.hlsl:270,286,287,289,349` and `shaders/ps/TreeAlpha.hlsl:103` (`lum.xxx` swizzle matches “XXX”); `entities/chars.cpp` line 230/671 are real TODOs already counted; `extras/ragdoll_death.cpp:63,78` are “loaded from [BODY]/[JOINT]” (substring “load” does not match — grep artifact).

Only 4 files carry TODO markers; the codebase instead uses “binding later”, “deferred”, “inert until wired”, “WIP” prose (≈40 occurrences), which is why a plain TODO census under-reports debt.

---

## 7. BUILD RISK

### 7.1 Object/basename collisions

| Collision | Severity | Current handling |
|---|---|---|
| `src/rw/normalmap.cpp` vs `src/render/normalmap.cpp` → both `normalmap.obj` | **High (LNK4042/LNK2019 historically)** | Fixed only in `build/skygfx.vcxproj:186-188` via `<ObjectFileName>$(IntDir)normalmap_rw.obj</ObjectFileName>` |
| 11 duplicate HLSL stems across `shaders/ps|vs` and their `ps2/`, `xbox/`, `mobile/` subdirs (`mobileBuildingVS`, `mobileVehiclePS`, `mobileVehicleVS`, `neoVehiclePass1VS`, `neoVehiclePass2VS`, `ps2BuildingFxVS`, `ps2BuildingSSSVS`, `ps2BuildingVS`, `ps2BuildingWindVS`, `xboxBuildingVS`, `xboxBuildingWindVS`) | Medium | `fast_build.py:397-400` skips subdir copies; premake's `for /r ... %%~nf.cso` would overwrite the same CSO from both copies |
| CSO name collisions from multi-entry (`vehiclePipeVS.hlsl` emits 6 CSOs; `VehiclePBR_Modern.hlsl` emits 7; `Water_Parallax.hlsl` emits 3; `buildingPipePS.hlsl` emits 5; `stochasticBuildingPS.hlsl` emits 3; `unifiedPipe.hlsl` emits 5) | Medium | One producer per CSO enforced only in `fast_build.py`; premake prebuild does not know this |

### 7.2 Project membership

- Single project: `build/skygfx.vcxproj` (only `.vcxproj` for this codebase). No file is in two projects.
- External TUs compiled by absolute path: 6 × `E:\SDKs\imgui-master\*.cpp` (unversioned, machine-local).
- `src/extras/skygfx_bridge.cpp` is in the repo but **not in any project**.
- `src/core/debugmenu_theme.cpp` was added to the project at 19:17 during this audit; before that the tree would have failed to link (`g_dmPalette`, `g_dmMetrics`, `DrawGlass*` unresolved).

### 7.3 `premake5.lua` vs `skygfx.vcxproj` divergence

`fast_build.py` builds the vcxproj directly and never runs premake; `premake_log.txt` shows premake was last run separately. Regenerating would produce a project that **does not build** and would drop these hand-edits:

1. **Lost:** `<ObjectFileName>normalmap_rw.obj</ObjectFileName>` override → `normalmap.obj` collision returns (LNK4042, one TU dropped, LNK2019 `normalmap_init/normalmap_tryAttach`).
2. **Lost:** `ExcludedFromBuild` on `PC_PlantsMgr.cpp` and `PC_PlantsMgr_overlay.cpp` → both are globbed by `src/render/*.*`, fail immediately on missing `general.h`/`handy.h`.
3. **Lost:** exclusions of `src/rw/normalmap_plugin.cpp` and `src/render/SMAA.cpp` → premake glob would add them back (plugin-sdk/game_sa headers, duplicate SMAA code).
4. **Lost:** the deliberate absence of `src/extras/skygfx_bridge.cpp` → premake glob adds it, introducing a **second `DllMain`** (LNK2005) and exported bridge symbols.
5. **Lost:** `OutDir = E:\games\gtasa_skygfx_plus\`, `IntDir`, `ImportLibrary`, `MapFile`, `GenerateMapFile` (`skygfx.map`), and the post-build `copy → skygfx.asi && del skygfx.dll`. Premake uses `bin/Release`, copies only (does not delete the DLL) and has no map file.
6. **Lost:** link libs `rpnormmap.lib`, `dbghelp.lib`, `winmm.lib`, and `d3d9.lib` (premake links only `d3dx9`).
7. **Lost:** include dirs `E:\SDKs\normalmap_byDK_1.01\...\sources`, `E:\dev(dave)\Sa_dev\GTASource\GTA SA RE\source` (dead path, but referenced), `E:\SDKs\Renderware 3.7 SDK ...\include\d3d9`, Windows Kits UM; premake instead uses `os.getenv("RWSDK36")`.
8. **Lost:** explicit v143 toolset + `WindowsTargetPlatformVersion 10.0.19041.0`; premake emits `systemversion 10.0.26100.0`.
9. **Lost:** `<IgnoreWarnCompileDuplicatedFilename>true</IgnoreWarnCompileDuplicatedFilename>` and per-config `Full` optimization / `FunctionLevelLinking` / `IntrinsicFunctions` / `StringPooling` / `MultiThreaded` / EditAndContinue.
10. **Gained failures from premake prebuild:** `%DXSDK_DIR%` is not set on this machine (AGENTS.md uses `C:\Program Files (x86)\...`), and the commands reference `shaders/buildingPipeVS.hlsl` and `shaders/vehiclePipePS.hlsl`, which do not exist. Premake regeneration makes the shader build fail before C++ compile.
11. **Gained failures from premake globs:** `src/*.*`, `src/Core/*.*` (wrong case on a case-sensitive include path, harmless on Windows), and `third_party/bullet/src/**.*` would compile ~200 Bullet files vs the 115 pinned in vcxproj (GImpact/SoftBody/Extras not currently linked), changing link surface.

### 7.4 `skygfx.vcxproj.filters` is badly stale

- vcxproj entries missing from filters: **177** (all 115 Bullet files + 42 src entries + 6 imgui + 14 headers).
- Filter entries pointing at pre-restructure paths that no longer exist: **27**, e.g. `..\src\HLSL_hook.cpp`, `..\src\main.cpp`, `..\src\postfx.cpp`, `..\src\gta.cpp`, `..\src\SMAA.cpp`, `..\src\buildingPipe.cpp` (now under `src/rw/`, `src/core/`, `src/render/`).
- New files with no filter: `debugmenu_theme.*`, `modeManager.*`, `mirrorPass.*`, `treeWind.*`, `profiler.*`, `texalpha.*`, `postfx_*`, `normalmap.*`, `PC_GrassInstancer.*`, `vegImposter.*`.
- Impact: IDE only. Double-clicking many files opens nothing; new files appear under “Other Files”. Regenerating filters from vcxproj would fix all 177+27 entries.

### 7.5 Other risks

- **Active edit race:** `debugmenu_ui.cpp` changed at 19:19, after the last build (19:08). Any inventory line numbers in that file can drift by the time you read this.
- **`Config` ABI is pinned by raw offsets:** `main.cpp` asm reads `[ecx+8]`/`[ecx+12]` for `fixGrassPlacement`/`doglare` (`skygfx.h:262-265`). Reordering those two fields is a silent crash, not a compile error.
- **`IDR_GRASSINSTANCED` is referenced under `#ifdef` but never defined** — builds cleanly, feature silently dead, and the one-shot latch prevents recovery even if RC is fixed without a restart.
- **`debugmenu_theme.cpp` added mid-session** — if this doc is read from a checkout before 19:17, expect unresolved theme symbols.
- **12 CSOs have no source and 17 HLSL outputs are unreachable** (Section 2.4–2.6); stale CSOs are still embedded and shipped, growing the DLL for no runtime path.

---

## 8. QUICK WINS (20)

All are deletion/merge-only; none changes a live render path. LOC saved includes declarations/parse/menu rows where applicable.

| # | Action | File:line | LOC saved | Risk |
|---|---|---|---|---|
| 1 | Delete the six dead `*_mode.cpp` TUs (presets live in `modeManager.cpp`/`presets.cpp`) | `render/ps2_mode.cpp:1`, `xbox_mode.cpp:1`, `mobile.cpp:1`, `pc_patched_mode.cpp:1`, `iv_mode.cpp:1`, `custom_mode.cpp:1` (109+105+105+106+79+171) | **675** (+22 decls in `skygfx.h:1390-1410`) | none — 0 call sites |
| 2 | Delete/archive the excluded legacy plant island | `render/PC_PlantsMgr.cpp:1` (2,175), `PC_PlantsMgr_overlay.cpp:1` (1,035) | **3,210** | none for build/runtime; loses reference port |
| 3 | Delete commented-out DK plugin port | `rw/normalmap_plugin.cpp:1-1314` | **1,314** | none |
| 4 | Delete inert imposter module | `render/vegImposter.cpp:1-704`, `.h:1-366` | **1,070** | none — header says nothing calls it |
| 5 | Delete commented-out legacy SMAA | `render/SMAA.cpp:1-374`, `SMAA.h:1-177` | **551** | none (`postfx_smaa` owns SMAA) |
| 6 | Delete unused class/header stubs | `render/EdgeDetection.h:1-146`, `PostFXPipeline.h:1-103`, `GrassSystem.h:1-101`, `core/hooks.h:1-27` | **377** | none — zero includers |
| 7 | Delete `#if 0` reference blocks in compiled TUs | `neo.cpp:143-209`, `postfx_colourfilter.cpp:853-893`, `vehiclePipe.cpp:3242-3248` | **115** | none |
| 8 | Delete dead lazy-getter block (5 getters + 4 statics) | `pipelinecommon.cpp:724-792` | **69** | none — zero callers |
| 9 | Delete dead ped-classification helpers | `chars.cpp:113-144`, `267-286`, `653-676`, `678-701` | **~101** | none |
| 10 | Delete dead bridge TU | `extras/skygfx_bridge.cpp:1-134` | **134** | removes a duplicate `DllMain` landmine |
| 11 | Delete `test.cpp` + `screens` + `dumpMenu` | `core/test.cpp:1-47` (+ vcxproj line, + `skygfx.h` decl if any) | **47** | none |
| 12 | Delete `HLSL_hook.{cpp,h}` stubs | `rw/HLSL_hook.cpp:1-35`, `.h:1-12` | **47** | none |
| 13 | Remove 16 dead shader resources (makePS line + `#define` + RC row + CSO; optionally HLSL) | `pipelinecommon.cpp:837,839-841,844,851,857,864,883,884`; `resource.h`; `Resource.rc` | **48 code + 16 CSO blobs** (≈1,300 if HLSL sources also removed) | none — verified never drawn |
| 14 | Deduplicate `buildingPipe` ↔ `defaultFuncs` render/vertex-decl clones | `buildingPipe.cpp:2581-2650` ↔ `rw/defaultFuncs.cpp:148-205` | **94** | low — pure move, same code |
| 15 | Unify the two `MENUSETTINGS` X-macros | `main.cpp:5583-5646` ↔ `debugmenu_ui.cpp:77-158` | **74** | low — removes drift; must reconcile the ~28 entries that differ |
| 16 | Deduplicate env-map quad/raster rebind | `envmap.cpp:189-234` ↔ `neoCarpipe.cpp:69-114` | **46** | low |
| 17 | Deduplicate trusted-screen-size clamp (`Dp*` vs `Fp*`) | `depthhook.cpp:116-147` ↔ `forwardplus.cpp:907-938` | **32** | low — identical math |
| 18 | Remove no-op `Config` knobs + parse/save/menu rows | `skygfx.h:381-382,512`; `main.cpp:4129-4130,4487-4488,4877-4878,4981-4982,5306-5307`; `debugmenu_ui.cpp:1144-1146` | **~16** (logic surface + 4 lying UI rows) | none; note `vignetteRadius/Contrast` legacy |
| 19 | Delete dead menu-inject render/input pair | `menu_inject.cpp:279-345`, `351-396` | **112** | none — screen table never references them |
| 20 | Delete dead `CreateRoadsignTexture` + `_orig` | `main.cpp:1927-1935` | **9** | none — no `InjectHook` uses `0x7F38A0` |

**Top-15 LOC total:** ≈**6,700** if #13 HLSL sources are also removed; ≈**5,400** for code-only deletions.

---

## 9. Caveats

1. Call-graph edges are heuristic (unique symbol names, comments excluded only where obvious). The `Calls into`/`Called by` columns are sufficient for ownership questions, not for linker analysis.
2. “Dead” means zero textual references in `src/`; hook targets installed by address are explicitly excluded and listed in §3.1.
3. LOC uses newline counts from the current files; `debugmenu_ui.cpp` and `main.cpp` changed during the audit.
4. The shader table’s `Draw` column names the first verified draw bind, not every bind. PostFX passes bind through locals (`ps`, `shader`) and `overrideIm2dPixelShader`, so `Load` + status are the authoritative fields.
5. Section 3.6 cross-checks `docs/plans/2026-10-24-imgui-settings-coverage.md`; that doc’s `stateBaselineEnable` claim is stale as of today’s tree (it now has four consumers).

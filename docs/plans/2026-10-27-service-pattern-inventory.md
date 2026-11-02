# Service-pattern inventory — cross-cutting concepts and their one-service form

**Date:** 2026-10-27
**Status:** plan only. No source edits, no builds, no shader compiles were performed.
**Scope:** every cross-cutting concept in `src/` and `shaders/` that currently has 2+ implementations or scattered copies, plus the template that the recent consolidations (`texdefs`, `SkyNoise`, `texalpha`, `debug_handl`, `modeManager`, `neopipe`, `grasssys`, `sftbdyphsx`, `renderstate_monitor`, `profiler`) establish.

**Method:** read-only inspection of the working tree at `898e894` (plus uncommitted lane work already present). Line numbers are current-tree. The existing `docs/plans/2026-10-27-render-refactor-roadmap.md` already specifies `pipeParkSet3L` and the `utilitynoise` deletion; this document generalises those findings and does not contradict them.

---

## 0. File conventions (line endings)

The repo has two families. This document does not normalise either one; every stage keeps the file's existing convention.

| Convention | Files cited here |
|---|---|
| **LF** | `src/render/noise.cpp/.h`, `treeWind.cpp/.h`, `texalpha.cpp/.h`, `renderstate_monitor.cpp/.h`, `profiler.h`, `src/core/modeManager.cpp/.h`, `src/render/postfx_utils.cpp`, `postfx_newfx.cpp`, `postfx_colourfilter.cpp`, `postfx_underwater.cpp`, `mirrorPass.cpp`, `shaders/include/Noise.hlsl`, `CommonMath.hlsl`, `shaders/ps/CloudsPS.hlsl`, `Grain.hlsl`, `shaders/noiseDetailBuildingPS.hlsl`, `tools/gen_ini_ref.py` |
| **CRLF** | everything else cited here: `texdefs.cpp/.h`, `utilitynoise.cpp`, `debug_handl.cpp/.h`, `sftbdyphsx.cpp/.h`, `neopipe.cpp/.h`, `grasssys.cpp/.h`, `profiler.cpp`, `presets.cpp`, `main.cpp`, `skygfx.h`, `pipelinecommon.cpp`, `postfx.cpp`, `buildingPipe.cpp`, `vehiclePipe.cpp`, `envmap.cpp`, `depthhook.cpp`, `waterPipe.cpp`, `debugmenu_ui.cpp`, `weather.cpp`, `veh_shaders.cpp`, `chars.cpp`, `forwardplus.cpp`, `PerlinNoise.hlsl`, `CarPaintNoise.hlsl`, `DynamicSky.hlsl`, `IBL_SkyCloud.hlsl`, `MotionBlur_Burnout.hlsl`, `VehiclePBR_Modern.hlsl`, `tools/fix_build.py`, `premake5.lua`, `build/skygfx.vcxproj`, `build/skygfx.vcxproj.filters` |

Where a table row cites several files, the `[LF]`/`[CRLF]` marker on the first file of a family applies to the other files of that family in the same row; this legend is authoritative.

**Merge-hostile files (do not merge, only call into a service):** any TU with `__finally` (`src/render/envmap.cpp`, `src/render/mirrorPass.cpp`; census: 14 and 8 tokens) or >2,500 lines (`main.cpp` 8,701; `postfx.cpp` 4,818; `debugmenu_ui.cpp` 4,461; `vehiclePipe.cpp` 3,868; `buildingPipe.cpp` 3,563). `pipelinecommon.cpp` (2,112), `neopipe.cpp` (1,776), `modeManager.cpp` (1,357), `debug_handl.cpp` (1,207) are safe hosts.

---

## 1. Concept × implementation matrix

Each row is one concept. `A`/`B` are the two (or more) live implementations; further copies are listed in the same cell. "Divergent?" means observable behaviour or ownership differs, not merely style.

### 1.1 Noise and hash

| Concept | Implementation A | Implementation B (and further copies) | Divergent? | Verdict |
|---|---|---|---|---|
| CPU packed noise tile | `SkyNoise` `src/render/noise.cpp:59` [LF]: R=Perlin fBm, G=Worley F1, B=white, A=Mitchell blue; 32/64/128 (`qualitySize`, `:140`); seed 1337; D3D9 texture created lazily (`:876`, `:897`); CPU tile survives Reset (`:830`, `:841`) | `GenerateUtilityTexture` `src/render/utilitynoise.cpp:170` [CRLF]: R=Perlin, G=Worley, B=white, **A=Bayer 8×8** (`:151-160`, `:224`); 32..1024; eager `RwTexture` (`:194-245`); no seed/quality enum; globals `:26-27` | **Yes** — alpha channel (blue vs Bayer), size set, lifetime, quality model, determinism | **ABSORB** into `SkyNoise`; **DELETE** `utilitynoise.cpp` (see §4.5) |
| SSAO random-vector tile | `g_ssaoNoiseTex` 4×4 RGBA8, `src/render/postfx_ssao.cpp:8`, created `:95-116`, bound s1 at `:711`/`:984`; shaders `shaders/ps/SSAO.hlsl:19` [CRLF] and `SSAO_Temporal.hlsl:20` | `SkyNoise` packed scalar tile (`noise.h:6-12`) | **Yes** — 3-channel uniform random vectors vs scalar-packed noise; 4×4 vs 32/64/128; s1 vs s9 | **KEEP-SEPARATE** (different distribution/register). Optional: let `SkyNoise` own the generator to remove the duplicate D3D creation block |
| GPU noise helper | `shaders/include/Noise.hlsl:42-105` [LF] — s9 + c49-c52, `NoiseFbm3`, `NoiseCloudDensity`, `NoiseBlueJitter`, `NoiseDither` | `shaders/include/PerlinNoise.hlsl` (303 lines, **zero includes** — dead), `shaders/include/CarPaintNoise.hlsl:6` (unused; `CarPaint_Reflections.hlsl:50` re-defines the same `hash22`) | **Yes** — three different contracts; two files never compiled | **DELETE** dead includes; **TEMPLATE-APPLY** one include per consumer (already done by `noiseDetailBuildingPS.hlsl:26` [LF] and `CloudsPS.hlsl:87` [LF]) |
| Shader hash primitives | `CommonMath.hlsl:80` `Hash2D` / `:86` `Hash2DSeed` [LF] — explicitly "NOT yet included by any file" (`:9-11`) | `colorSpace.hlsl:233` `HashNoise`; `PBR_Common.hlsl:78` `PBR_chash`; `DynamicSky.hlsl:33` + `IBL_SkyCloud.hlsl:25` identical `hash(float)`; `MotionBlur_Burnout.hlsl:47`; `VehiclePBR_Modern.hlsl:112`; `CarPaintNoise.hlsl:6` + `CarPaint_Reflections.hlsl:50`; `ps2BuildingWindVS.hlsl:30` + `xboxBuildingWindVS.hlsl:38` | **Yes** — 5+ identical `frac(sin(dot(...)))` variants and two cloud hash copies; `CommonMath` unwired | **MERGE-INTO-SERVICE** (shader include): wire `CommonMath.hlsl` into PS consumers; keep VS wind hashes procedural (see next row) |
| Wind phase "noise" | `treeWind_computeLean` `src/render/treeWind.cpp:135-176` [LF] — 16-value `s_windTable` (`:107-110`), atomic-pointer seed (`:144`), ms clock (`:121-131`) | `SkyNoise::White2` (`noise.cpp:614`) / any spatial tile | **Yes** — temporal, game-parity table, not spatial; changing it changes tree sway | **KEEP-SEPARATE** (parity + no visual benefit from a texture fetch) |
| Procedural VS wind hash | `ps2BuildingWindVS.hlsl:30-46` [CRLF], `xboxBuildingWindVS.hlsl:38-51` [CRLF] — `hash(float)` + value-noise | `Noise.hlsl` s9 fetch | **Yes** — vs_3_0 vertex texture fetch is not guaranteed; wind must stay procedural | **KEEP-SEPARATE** |
| Film grain | `shaders/ps/Grain.hlsl:43-60` [LF] — IGN (Jimenez), 3 octaves, animated scroll | `Noise.hlsl` blue-noise dither (`:100-105`) | **Yes** — IGN is deliberately TAA-friendly; the old s9 mode 2 was removed because no caller bound s9 (`Grain.hlsl:1-20`) | **KEEP-SEPARATE**; if mode 2 returns, bind via the noise service |

### 1.2 Render state and blend

| Concept | Implementation A | Implementation B (and further copies) | Divergent? | Verdict |
|---|---|---|---|---|
| rw→D3D blend forward map | `pipeBlendHasRwTwin` `src/render/pipelinecommon.cpp:1439` + `pipeBlendToD3D` `:1447` [CRLF] — identity for rw 1..11, caller fallback outside | `carBlendClampDev` `src/render/neopipe.cpp:360` [CRLF] clamps D3D 1..17; `mirrorPass.cpp:185-206` [LF] maps per layer by hand; `envmap.cpp:688-734` [CRLF] domain-guards per layer | **Yes** — three different domain tests and fallbacks | **TEMPLATE-APPLY**: every forward map calls the pipelinecommon pair; keep `carBlendClampDev` only for device-only 14..17 |
| rw←D3D blend inverse | `coronaRwBlendFromD3D` `src/core/main.cpp:2938` [CRLF] — only ZERO/ONE/INVSRCALPHA/SRCALPHA, default SRCALPHA | `pipeBlendToD3D` (forward) | **Yes** — lossy inverse is intentional for corona capture, but it is not exported or documented as a service | **KEEP-SEPARATE** behaviour; **MERGE** into `pipelinecommon` as `pipeD3DToRwBlend` so the lossy contract is named once |
| Three-layer park-and-set | `pipeRwParkSet` `pipelinecommon.cpp:1135`, `pipeL2ParkSet` `:1142`, `pipeL3Set` `:1149`; composed by `RestoreOneRawGeomState` `postfx_utils.cpp:228` [LF] | `envParkSetState3L` `envmap.cpp:371`; `MirrorRestoreState3L` `mirrorPass.cpp:116`; `carBlendRestore3L` `neopipe.cpp:400`; main.cpp corona/sprite hand-rolled sequences `:3150-3210`, `:4015-4116` | **Yes** — four helper copies, different flush placement, different park choices; all end at the same three layers | **ABSORB**: add `pipeParkSet3L()` (park L1/L2, target L1/L2, one flush, raw L3) and `pipeSave3L()`; convert the four copies to wrappers |
| Canonical alpha block | `pipeEnterAlphaMode` `pipelinecommon.cpp:1527` / `pipeExitAlphaMode` (`skygfx.h:1455-1456`) | `pipeForceAlphaBlock` defined in **`buildingPipe.cpp:525`** (the void overload; the `PipeAlphaState` overload at `:531` is file-static), extern'd from `main.cpp:857`, `:3672`, `:3987` | **Yes** — owner is the wrong TU; four call-site families depend on it | **MERGE-INTO-SERVICE**: move `pipeForceAlphaBlock` beside `pipeEnterAlphaMode` in `pipelinecommon.cpp`; leave the call sites unchanged |
| Raw geometry save/restore | `SaveRawGeomStates` `postfx_utils.cpp:190`, `RestoreRawGeomStates` `:246` (9 states, `:176-185`) | Per-pass hand-rolled snapshots: `mirrorPass.cpp:328` + `:216`; `envmap.cpp:688-760`; `neopipe.cpp:372-434`; `main.cpp:2905-2925` + `:3040-3220`; `chars.cpp:505` (uses the helper, good); `forwardplus.cpp:1858/1913`; `depthhook.cpp:541/604` | **Yes** — some cover 3 states, some 9, some 15; several use their own park order | **TEMPLATE-APPLY**: every pass that writes ALPHATEST/CULL/Z*/BLEND* wraps with the helper; extra fields stay local |
| World baseline | `pipeApplyWorldBaseline` `pipelinecommon.cpp:1975` — canonical frame-boundary state, three layers, flush | Hand-rolled canonical pushes in `main.cpp:3398-3426` (corona close), `:4590-4600` (sprite resync), `buildingPipe.cpp:3151-3184` (Switch) | **Yes** — baseline is already the service but not the only writer | **TEMPLATE-APPLY**: route the frame-boundary resyncs through it; per-draw guards stay separate |
| PBR c22/c23 block | `pipeUploadPBR` `pipelinecommon.cpp:903` | `vehiclePipe.cpp:2925` / `:2939` call `pipeUploadPBR` then overwrite c23 with a light-only block; `:3344` uses the full helper | **No functional divergence** — the c23 overwrite is a documented per-material light override | **KEEP-SEPARATE**; optionally add `pipeUploadPBRLight()` to name the override |

The postfx family is the **good** side of the raw-geom pattern: `postfx.cpp` has 23 `SaveRawGeomStates`/`RestoreRawGeomStates` sites, `postfx_ssao.cpp` 10, `postfx_newfx.cpp` 6, `postfx_smaa.cpp` 4, `postfx_colourfilter.cpp` 4, `postfx_underwater.cpp` 4, `postfx_motionblur.cpp` 2. The hand-rolled snapshots listed above are the exception; convert them, do not weaken the postfx wraps.

### 1.3 Shader lifetime and device reset

| Concept | Implementation A | Implementation B (and further copies) | Divergent? | Verdict |
|---|---|---|---|---|
| Shader creation | `makePS`/`makeVS` `pipelinecommon.cpp:674`/`:702`; `CreateShaders` `:729`; `s_forceShaderRecreate` `:656`; `Pipeline_InvalidateShaders` `:723` | Lazy loaders: `ivFPReady` `buildingPipe.cpp:1044`; `FxLoadShaders` `postfx_newfx.cpp:87` [LF]; `MirrorPass_Composite` `mirrorPass.cpp:226`; `UWLoadShader` `postfx_underwater.cpp:283` [LF]; `EnsureIVGradePS` `postfx_colourfilter.cpp:1446` [LF] | **Yes** — only `CreateShaders` handles are rebuilt after Reset; four lazy handles are dead until the process restarts (IVGrade is the exception, with a manually wired reset) | **MERGE-INTO-SERVICE**: a shader-slot registry with `{void **slot, bool *tried, int res, bool vs}`; `Pipeline_InvalidateShaders` clears every `tried` flag |
| Lazy-load latch | `static bool attempted` `buildingPipe.cpp:1046`; `s_fxLoadTried` `postfx_newfx.cpp:85`; `mirrorPSLoadTried` `mirrorPass.cpp:98`; `s_uwLoadTried` `postfx_underwater.cpp:140` | `PostFX_IVGradeInvalidateShader` `postfx_colourfilter.cpp:1446` is the **only** latch with a reset path (and it is wired manually from `depthhook.cpp:425`) | **Yes** — 5 latches, 1 reset | **MERGE-INTO-SERVICE** (same registry as above) |
| Device-reset fan-out | Pre-Reset: `CPostEffects::OnDeviceReset` `postfx.cpp:2635` + `SkyNoise::ReleaseDeviceObjects` `depthhook.cpp:358` + `WaterPipe_ReleaseDeviceObjects` `:367` + ImGui invalidate `:347` | Post-Reset: hardcoded extern list in `depthhook.cpp:395-430` (`Pipeline_InvalidateShaders`, `TexAlpha_OnDeviceReset`, `SkyNoise::OnDeviceReset`, `PostFX_IVGradeInvalidateShader`, `RSMon_Reset` **not called**, `PfInit` not re-called) | **Yes** — two lists, pre/post split by hand; `renderstate_monitor.h:19` says to call `RSMon_Reset` after reset but no caller exists | **MERGE-INTO-SERVICE**: `Pipeline_RegisterDeviceReset(name, pre, post)`; `depthhook` calls `Pipeline_DeviceResetPre/Post`; services register at Init |
| Reset ordering | `depthhook.cpp:330-430` documents the order: pre-release → `orig_Reset` → post-recreate → RW caches → world baseline | `postfx.cpp:2635` pre-release list is separate | **Yes** — one order, two code paths | **MERGE-INTO-SERVICE** (registry preserves the documented order) |

### 1.4 Presets, modes, config/INI

| Concept | Implementation A | Implementation B (and further copies) | Divergent? | Verdict |
|---|---|---|---|---|
| Named rendering profile | `presetConfigs` `src/core/presets.cpp:25` + `ApplyPreset` `:108` [CRLF] — 17-field `PresetConfig` (`skygfx.h:255-277`); writes colour filter/features but **not** `buildingPipe`/`vehiclePipe` (`:126-141`); `smaaPreset` never applied | `kValues*` + `g_modeProfiles` `src/core/modeManager.cpp:95/206/316/418/545/557/708/809` [LF] — 50-104 `ModeValue` rows per profile; `ApplyMode` `:1149` is the one writer | **Yes** — presets still expose dead pipe fields and a dead `smaaPreset`; modes own the pipes; `ApplyPreset` clears the mode latch (`presets.cpp:120-121`) | **ABSORB** `presets.cpp` into `modeManager` as a compatibility adapter; delete dead fields/API |
| Quality preset | `qualityPreset` switch `main.cpp:6019-6150` [CRLF] — 0..3 writes features and, for 0..2, pipes; `g_qualityPreset` `:5899` | `kValuesModern` `modeManager.cpp:557` documents "qualityPreset=3 semantics" but duplicates the values; `presetConfigs` has no quality tiers | **Yes** — 0..2 can overwrite the `pipeline` key's pipes, then `derivePipelineFromPipes` (`main.cpp:6213`) re-derives; mode profiles duplicate the feature values | **MERGE-INTO-SERVICE**: qualityPreset becomes a mode profile (`MODE_MODERN` + tier flag) or is deleted after migration |
| `gamePreset` storage | `Config::preset` (`skygfx.h:282`), read `main.cpp:6742`, saved `:7594`; UI Style combo `debugmenu_ui.cpp:2863-2896` | Mode id (`ModeManager_GetMode`) + `[Modes] active=` (`modeManager.h:76-84`) | **Yes** — two independent "which profile is active" latches; `ApplyPreset` clears the mode latch to keep them from fighting | **ABSORB** into `modeManager`; keep `gamePreset` as a deprecated read alias for one release |
| INI read | `readIniFile` `main.cpp:5905` — one giant function, `StrAssoc` maps, clamps, mode apply | `GetPrivateProfileIntA` `forwardplus.cpp:168`/`:257`; `GetPrivateProfileStringA` `texdefs.cpp:981`; `ragdoll.ini` readers `sftbdyphsx.cpp:666-670`, `ragdoll_death.cpp:147-170` | **Yes** — three skygfx.ini keys bypass `Config`; `ragdoll.ini` is a separate file/domain | **MERGE** skygfx.ini keys into the config schema; **KEEP-SEPARATE** `ragdoll.ini` (different file, different owner) |
| INI write | `saveConfigTo` `main.cpp:7256` — hand-maintained `cfg.set` list | `MenuWriteMissingIniKeys` `debugmenu_ui.cpp:3561` — 12 `WritePrivateProfileStringA` keys; `ADD_IF_MISSING` `main.cpp:6909`; fresh-INI writer `:7037` | **Yes** — four writers, overlapping key sets; menu-only keys are invisible to `saveConfigTo` | **MERGE-INTO-SERVICE**: one schema table drives read, save, defaults and the fresh file; menu-only keys become `Config` fields or schema rows |
| INI documentation | `tools/gen_ini_ref.py:6-9` [LF] parses `readIniFile`, `Config`, `kValues*`, `ADD_IF_MISSING` | `docs/ini-reference.md` generated output; hand-written debug-menu tooltips `debugmenu_ui.cpp:975-1139` | **Yes** — generator cannot see `MenuWriteMissingIniKeys` or the external reads unless hand-listed (`gen_ini_ref.py:266-275`) | **TEMPLATE-APPLY**: generator consumes the schema, not three source parsers |

### 1.5 Texture metadata and classification

| Concept | Implementation A | Implementation B (and further copies) | Divergent? | Verdict |
|---|---|---|---|---|
| Texture metadata | `TexInfo` `skygfx.h:1117-1131`; loader `texdefs.cpp:421-535`; accessor `TexDefsGetInfo` `:573`; plugin slot `:301-303` | No live `src/extras/texdb.cpp` remains (deleted 2026-10-25); `vegImposter` deleted; `treeWind.cpp:26` uses `TexDefsGetDefinedShader` (correct). `grasssys.h:147` carries a raw `RwTexture *texture_ptr` per plant (domain data, not metadata) | **No live duplicate** — `texdefs` is already the one service | **KEEP**; **TEMPLATE-APPLY** all consumers through `TexDefsGetInfo` (already true for building/vehicle/chars) |
| RwTexture→IDirect3DTexture9 extraction | `TexDefsGetNormalMap` `texdefs.cpp:593-650` — `raster + *(DWORD*)0xB4E9E0`, `ext+0` ptr, `ext+8` flag | Same formula re-derived in **5 TUs / 6 sites**: `texalpha.cpp:592` [LF], `main.cpp:1524`/`:1734`, `chars.cpp:348`, `postfx_smaa.cpp:413`, `postfx_ssao.cpp:608` | **Yes** — each copy has slightly different guards; one bad pointer is a crash | **MERGE-INTO-SERVICE**: `TexDefsGetD3DTexture(RwTexture*, bool *valid)`; replace all six |
| Texture alpha semantics | `TexAlpha_Classify` `texalpha.cpp:793` [LF] — 4096-entry cache, 256/frame budget, `TAC_*` | Legacy `hasAlpha` decisions in pipes (`buildingPipe.cpp:1881`, `:2681`; `vehiclePipe.cpp:2736`); `TexAlpha_Classify` already called there | **Yes** — legacy fallback is intentional when `TAC_UNKNOWN`, but the decision is duplicated in each pipe | **TEMPLATE-APPLY**: keep the classifier; centralise the `TAC_UNKNOWN → legacy hasAlpha` policy in one helper |
| Vehicle surface classification | `VehShaders_GetSurfaceType` `veh_shaders.cpp:250-284` [CRLF] — name heuristics (`tire`, `glass`, `chrome`, ...) | `TexInfo::materialType` `skygfx.h:1130` — authored skin/cloth/vegetation; `TexAlpha_Classify` alpha class | **Yes** — three orthogonal classifiers, different inputs (name vs texdb vs pixels) | **KEEP-SEPARATE** (vehicle name heuristics are a different domain); optionally expose `VehShaders_GetSurfaceType` to the PBR building lane for shared vegetation/skin naming |
| Normal-map pairing | `NmPair` + `TexDefsGetNormalMap` `texdefs.cpp:157-220`, `:593` | `src/rw/normmap_stubs.cpp` (87 lines) — SDK symbol stubs only, no metadata | **No** — stubs are link-time, not a second implementation | **KEEP** |

### 1.6 Logging, profiling, camera, colour

| Concept | Implementation A | Implementation B (and further copies) | Divergent? | Verdict |
|---|---|---|---|---|
| Logging sink | `dbglog` `debug_handl.cpp:166` [CRLF]; `dbglog_loc` `:176`; `dbglog_warn/err` `skygfx.h:53-54` | `PfNote` `profiler.cpp:532` writes a separate timeline; `PerfTimer` `skygfx.h:76-80` writes `PERF [name]` through `dbglog`; `PostfxPerfGate` `postfx.cpp:21-30` writes its own `PERF` line | **Yes** — four output paths/line formats; no tag registry | **MERGE-INTO-SERVICE**: one sink (`dbglog_loc`) with a `[tag]` prefix; profiler notes are a separate structured channel, not text |
| Throttle / once guards | `dbglog_throttle` `skygfx.h:89` — FNV-1a, 256 buckets, 2 s | `dbglog_first` `:104` — x31, 64 buckets; `dbglog_once` `:116` — FNV-1a, 256 buckets | **Yes** — three tables, two hashes; layering check reports **352 tags / 404 sites / 104 colliding buckets involving 278 tags** (e.g. `noise_bind` collides with `mode_active`, `bpipe_elide`, `VehicleSkipped:NEO`) | **MERGE-INTO-SERVICE**: move all three to `debug_handl.cpp`, one 512-bucket table + collision check; keep the macro names |
| Profiling | `PF_SCOPE`/`PfScope` `profiler.h:14-36` [LF] — QPC, GPU queries, draw counters | `PERF_SCOPE`/`PerfTimer` `skygfx.h:76-80`; `PostfxPerfGate` `postfx.cpp:21-30` (gated by `profilerEnable`) | **Yes** — two APIs, two log formats, `postfx` deliberately gates to avoid overhead | **TEMPLATE-APPLY**: make `PerfTimer` a thin `PfScope` alias with a gate flag; keep `PF_SCOPE` as the only macro |
| Screen size | `GetScreenSize` `postfx_utils.cpp:109` [LF] — trusted cache, captured at `postfx.cpp:1568`/`:3667` | Direct `RwCameraGetRaster(Scene.camera)`/`Scene.camera->frameBuffer` reads: `neopipe.cpp:1326-1327`, `depthhook.cpp:215/238/267/500`, `buildingPipe.cpp:1651/2582/3142`, `forwardplus.cpp:1150/1275`, `postfx.cpp` pass entries | **Yes** — the live camera raster is a transient non-screen raster mid-frame; direct readers inherit 2048×1024 (documented `postfx_utils.cpp:63-94`) | **MERGE-INTO-SERVICE**: `CameraView_ScreenSize()`/`CameraView_CameraRaster()`; geometry-phase readers use the cache, only postfx entry captures |
| Front buffer | `CPostEffects::pRasterFrontBuffer` `postfx.cpp:43`; `UpdateFrontBuffer` `:536`; validity latch `:496-535` | `neopipe.cpp:1656` assigns `ms_tex->raster = pRasterFrontBuffer` then calls `UpdateFrontBuffer` (`:1657`); `chars.cpp:587/636` binds the raster directly; `buildingPipe.cpp:3131` documents a skipped update | **Yes** — the sync/validity contract is in `postfx.cpp` but consumers are spread across 4 TUs | **MERGE-INTO-SERVICE**: `CameraView_FrontBuffer()` + `CameraView_SyncFrontBuffer()`; one validity latch; consumers ask the service |
| View/proj cache | `_RwD3D9ActiveViewProjTransform` / `D3D9ActiveTransform` `pipelinecommon.cpp:270-298` — null triggers recompute | `waterPipe.cpp:1225-1231` nulls and restores both globals around its camera swap | **Yes** — only waterPipe knows the invalidation idiom; any future camera swap must copy it | **MERGE-INTO-SERVICE**: `pipeCameraInvalidateViewProj()` / `pipeCameraSaveRestoreViewProj()`; waterPipe calls it |
| Camera raster cache | `s_camRasGame` + `InvalidateCameraRasterCache` `main.cpp:3755`; called from `main.cpp:3893` and `postfx.cpp:2661` | `GetScreenSize` cache (`postfx_utils.cpp:96-119`) is a second screen-dimension cache | **Yes** — two caches with different invalidation triggers (reset/resolution vs postfx entry) | **MERGE-INTO-SERVICE** with screen-size service; one resolution-change invalidation |
| Timecycle ambient | `GetTimecycleAmbient` `pipelinecommon.cpp:175`, `GetTimecycleAmbientPBR` `:200`, `GetTimecycleAmbientRaw` `:240`, `UpdateTimecycleLighting` `:251`; `ClampAmbientToSky` `:79` | Direct `CTimeCycle_GetAmbientRed/Green/Blue` in `main.cpp:2484-2486` (grass ambient, bypasses `ambientMult` and the night clamp) | **Yes** — grass ambient ignores the global multiplier/clamp; all other pipes use the service | **TEMPLATE-APPLY**: `main.cpp:2484` calls `GetTimecycleAmbient()`; no new API needed |
| Timecycle colour readers | `CTimeCycle__m_CurrentColours` direct reads: `postfx.cpp:2791` (sky/cloud), `:4349` (height fog), `postfx_colourfilter.cpp:327` (grade), `weather.cpp:250`/`:343` (writer/blender) | `GetTimecycleAmbient*` for ambient only | **Yes** — colour consumers re-read the struct directly; ambient has a service, colour does not | **TEMPLATE-APPLY**: add `const CColourSet &Timecycle_GetColours()` in `pipelinecommon`; keep `weather.cpp` as the only writer |
| Carcols / charcol readers | `carcols.dat` parsed only in `vehicles.cpp:3`; paint derivation from carcols in `vehiclePipe.cpp:3180-3278` | `charcol` has **no live reader** (only comments); `postfx_colourfilter.cpp:380` names a `carcolsAdapt` scalar but does not read carcols | **No live duplicate** — vehicle paint is vehicle-local; ambient is the only shared colour concept and it is already consolidated | **KEEP-SEPARATE** (vehicle paint domain); do not invent a `charcol` service |
| Fullscreen quad | `s_ffQuad` `postfx_utils.cpp:142` + `SetupFullscreenQuad` `:146` [LF] | `screenQuad` `envmap.cpp:192`; `CarPipe::screenQuad` `neopipe.cpp:217` + `MakeScreenQuad` `:254`; `verts[4]` `mirrorPass.cpp:251`; game-owned `colorfilterVerts` `postfx_utils.cpp:8` | **Yes** — 4 project copies with different UV/NDC conventions; `colorfilterVerts` is game-owned and must not be replaced | **MERGE-INTO-SERVICE**: one `PostFX_MakeFullscreenQuad(w,h,uvSpace)`; `colorfilterVerts` stays as a compatibility path |

**Tag ownership today is convention only.** Each TU chooses a bracket prefix (`[Noise]`, `[Mode]`, `[texdefs]`, `[grassinst]`, ...) and a throttle-tag family (`noise_*`, `mode_*`, `pf_*`, `bld_*`), but there is no registry mapping tag→owner and no check that a tag is unique. The 104 colliding buckets above are the symptom; the template fix is one prefix per service and one `service_*` throttle family, enforced by a debug-build assertion or the layering check.

---

## 2. Template specification — what a shared subsystem looks like here

The successful services share five properties. These are the rules; deviations are bugs.

### 2.1 File layout

- One concept = one `.cpp` + one `.h`, siblings, in the domain directory: `src/render/` for render services, `src/core/` for core/config services, `src/entities/` for entity services. Root `src/` only for an engine-wide service that both render and core need (`texdefs.cpp/.h` is the precedent). A service may be a feature family (`neopipe`, `grasssys`) or a single concept (`noise`, `texalpha`); the rule is one owner and one API, not one file per function.
- The `.h` is self-contained: forward-declare D3D/RW types (`struct IDirect3DDevice9; struct RwTexture;`), include `skygfx.h` only when it needs `Config`/enums. Never include another service's `.cpp`.
- **Hard limits:** never add code to a TU >2,500 lines; never add code to a TU with `__finally` (`envmap.cpp`, `mirrorPass.cpp`). Those files may only become thin callers of the service.
- One log prefix, one INI section/prefix, one device-reset registration per service.

### 2.2 API surface

- All mutable state is `static` in the `.cpp`. **No cross-TU mutable globals.** The one sanctioned exception is `Config *config` / `configs[]`, owned by `main.cpp`; services read and write it through the `Config*` they are given, never through their own globals. The layering check currently reports 102 such globals (`src/core/main.cpp` 40, `postfx.cpp` 9, `utilitynoise.cpp` 1, ...). A service may expose a `const Stats&` or a `const` data pointer with a documented lifetime.
- One `Get*`/`Set*` pair per knob. No public data members, no `extern` arrays. Accessors return `const` data or copies; a mutable pointer (e.g. `TexDefsGetInfo` returning `TexInfo*`) is allowed only when the service documents that the consumer owns the mutation — `texdefs` currently does not, so treat that as debt.
- Lifecycle, in this order and with these exact semantics:
  - `Init(...)` — idempotent, **no D3D calls**, safe from `DllMain`.
  - `Shutdown()` — process detach, **no D3D/RW calls**, frees CPU data.
  - `ReleaseDeviceObjects()` — pre-`Reset`; releases `D3DPOOL_DEFAULT` only; safe standalone.
  - `OnDeviceReset()` — post-`Reset`; clears device-generation caches/stats; recreates lazily.
  - `OnFrameStart(unsigned frame)` — optional; only if the service needs a per-frame time/frame stamp.
- Device binding is one function (`Bind*`/`BindAndUpload`) and it is the only place in the service that creates/binds device objects; `ReleaseDeviceObjects` may touch the device only to release. Null-device is a clean no-op, never a crash.
- Errors: return `bool`/`int`; never throw; log once on failure, throttle per-frame failures.

### 2.3 INI ownership

- One section + one key prefix per service. The service owns the parse/save of its keys; `readIniFile` and `saveConfigTo` call `Foo_SetX`/`Foo_GetX`, not the other way around.
- Preferred storage is a `Config` field. `Config` has pinned offsets (`skygfx.h:280-285`) and static_asserts, so add new fields only at the end and update the asserts, or keep the knob service-owned with a schema row. If the value is not a `Config` field (a runtime-only knob), add a schema row with `{section, key, type, default, getter, setter}` so `readIniFile`, `saveConfigTo`, `ADD_IF_MISSING` and `gen_ini_ref.py` all see it.
- No `GetPrivateProfile*`/`WritePrivateProfileString*` inside a service unless the key is deliberately outside the schema (document why). The current `texdefs.cpp:981` read of `enableNormalMaps` is the counterexample: it is a skygfx.ini key, but it is invisible to the config schema.
- One default: the schema default is the only default. `ADD_IF_MISSING` and the fresh-INI writer must not spell a second literal.

### 2.4 Device-reset participation

- The service registers at `Init`:
  ```cpp
  Pipeline_RegisterDeviceReset("Foo", Foo_ReleaseDeviceObjects, Foo_OnDeviceReset);
  ```
  `depthhook.cpp` calls `Pipeline_DeviceResetPre()` / `Pipeline_DeviceResetPost()` once; it must not grow another `extern void Foo_OnDeviceReset(void)` line.
- Pre-reset callbacks release only `D3DPOOL_DEFAULT`; post-reset callbacks clear caches and stats. CPU data survives (the `SkyNoise` model).
- Lazy shader loaders additionally register `{slot, triedFlag, resourceId, isVS}`; `Pipeline_InvalidateShaders()` clears every `triedFlag`. This is the fix for the four dead lazy handles plus the IVGrade latch in §1.3.
- Reset order is the registry's insertion order; document any dependency (the hard contract is every pre-release before `orig_Reset`, every post-recreate after it).

### 2.5 How consumers bind

1. **Accessor (preferred):** `TexDefsGetInfo(tex)`, `TreeAlpha_SetProfile(fx)`, `ModeManager_GetMode()`. The consumer includes the header and calls the function.
2. **Callback registration (events):** `ModeManager_RegisterListener(fn)` — the service owns the list, the consumer never writes service state.
3. **Include for types/constants only:** `#include "noise.h"` for `SkyNoise::NQ_*`; never include a service's private header or `.cpp`.
4. **Forbidden:** writing a service global, calling raw `d3d9device->*` state/texture setters outside `pipelinecommon.cpp`/`postfx_utils.cpp` (or the service's documented `Bind*`), or adding a new `extern` to `depthhook.cpp`.

### 2.6 Skeleton header

```cpp
// src/render/foo.h — Foo service. One TU, one owner, one lifecycle.
// EOL: match the directory family (render pipes = CRLF; new LF TUs stay LF).
#pragma once
#include "skygfx.h"          // Config/enums only; forward-declare D3D/RW below

struct IDirect3DDevice9;
struct RwTexture;

namespace Foo {

enum Quality { FQ_LOW = 0, FQ_MEDIUM = 1, FQ_HIGH = 2 };

// lifecycle ----------------------------------------------------------------
void Init(unsigned seed = 1337u);      // idempotent, no D3D
void Shutdown(void);                   // detach, no D3D
void ReleaseDeviceObjects(void);       // pre-Reset: POOL_DEFAULT only
void OnDeviceReset(void);              // post-Reset: caches/stats only

// knobs — one pair per knob, no public data --------------------------------
int  GetQuality(void);
void SetQuality(int q);                // rebuilds; the only writer
void SetSeed(unsigned seed);

// device binding — the only function that touches d3d9device ---------------
bool Bind(int samplerStage);           // null-device safe, returns success

// stats/debug — const view only --------------------------------------------
struct Stats { unsigned builds, seed, size; int quality, textureLive; };
const Stats &GetStats(void);

} // namespace Foo
```

`.cpp` registration:

```cpp
static void FooPre(void)  { Foo::ReleaseDeviceObjects(); }
static void FooPost(void) { Foo::OnDeviceReset(); }
// in Foo::Init, after state is ready:
Pipeline_RegisterDeviceReset("Foo", FooPre, FooPost);
```

### 2.7 Scorecard — which successes to copy, which are incomplete

| Service | Copy this | Do not copy this |
|---|---|---|
| `texdefs` (1,123 ln) | one plugin registry, one find-CB, one metadata map, one accessor (`texdefs.h:12-30`) | the `GetPrivateProfileStringA("enableNormalMaps")` read (`:981`) — bypasses the schema |
| `SkyNoise` (959 ln) | CPU/D3D split, deterministic seed, pre/post reset, stats (`noise.h:43-93`) | 22 public functions, 17 with zero callers; trim to the used surface |
| `texalpha` (962 ln) | one cache, one classifier, `TexAlpha_OnDeviceReset` (`texalpha.h:53-58`) | the duplicate D3D-extraction formula (`:592`) |
| `debug_handl` (1,207 ln) | one sink, ring, VEH, watchdog, scope tags | the throttle guards still live in `skygfx.h:89-123`, not here |
| `modeManager` (1,357 ln) | one apply path, one member-pointer table, listener registration (`modeManager.h:52-125`) | `presets.cpp` and `qualityPreset` still duplicate the profile values |
| `neopipe`/`grasssys`/`sftbdyphsx` | one header per merged group, verbatim bodies, section banners | `neopipe.cpp:372-434` still hand-rolls its own three-layer save/restore |

---

## 3. Ranked candidate list

Ranked by expected payoff (low-risk items first where payoff is comparable). Each entry names the verdict, the payoff, the risk (1 = trivial, 5 = user-facing regression likely), and the exact work. "No build delta" means only existing TUs are edited.

### 3.1 `TexDefsGetD3DTexture` — **MERGE-INTO-SERVICE**
**Payoff:** 6 call sites unified (~70 lines removed), one guard policy; prevents the next null/stale `ext` pointer crash. **Risk: 2.**
**Work:** add `IDirect3DTexture9 *TexDefsGetD3DTexture(RwTexture*, bool *outValid)` and `IDirect3DTexture9 *TexDefsRasterToD3D(RwRaster*, bool *outValid)` to `src/texdefs.h`; implement beside `TexDefsGetNormalMap` (`src/texdefs.cpp:593-650`, formula at `:609-620`); replace `src/render/texalpha.cpp:592-625`, `src/core/main.cpp:1524-1540` (`shadowRasterToTex`, raster form) and `:1730-1745`, `src/entities/chars.cpp:343-360`, `src/render/postfx_smaa.cpp:405-425`, `src/render/postfx_ssao.cpp:600-620`. Keep each site's own plausibility guards until the helper's `outValid` is proven.

### 3.2 Delete `utilitynoise.cpp` — **DELETE**
**Payoff:** 251 lines + ~45 call-site lines; one noise quality knob; removes the Bayer-vs-blue divergence. **Risk: 2** (no render consumer; debug-UI behaviour change needs sign-off).
**Work:** delete `src/render/utilitynoise.cpp`; drop `src/skygfx.h:968-970`; remove the utility block inside `main.cpp:722-758` (`g_noiseQuality`, `utilityNoiseResForQuality`, and the `refreshIni` regen block at `:751-756`) and `:7755-7760` (`menuGet/SetNoiseQuality`), keeping only a small `g_noiseQuality`-equivalent holder that calls `SkyNoise::SetQuality` (0→NQ_LOW, 1/2→NQ_MEDIUM, 3→NQ_HIGH); remove `debugmenu_ui.cpp:865`, `:2062-2063`, `:2418-2419`, `:3642-3643`, `:4319-4325`; build deltas in §5 Stage 2.

### 3.3 `pipeParkSet3L` + `pipeSave3L` — **ABSORB**
**Payoff:** four helper copies (`envmap.cpp:371`, `mirrorPass.cpp:116`, `neopipe.cpp:400`, `main.cpp:3150-3210`) become wrappers; ~120 lines removed; one flush placement. **Risk: 2** (state semantics are sensitive but the end state is identical).
**Work:** add to `pipelinecommon.cpp` beside `pipeL3Set` (`:1149`):
`void pipeParkSet3L(RwRenderState rw, D3DRENDERSTATETYPE d3d, RwUInt32 rwPark, RwUInt32 rwTarget, DWORD d3dPark, DWORD d3dTarget);`
`void pipeSave3L(DWORD out[9]);` / `void pipeRestore3L(const DWORD in[9]);`
Convert `envParkSetState3L` and `MirrorRestoreState3L` bodies to call it; convert `carBlendRestore3L` and the main.cpp corona/sprite sequences. Do **not** merge `envmap.cpp`/`mirrorPass.cpp` (they carry `__finally`).

### 3.4 Unify log guards — **MERGE-INTO-SERVICE**
**Payoff:** one 512-bucket table, one hash, collision test; removes 104 colliding buckets (278 tags) and the `dbglog_first` 64-bucket table. **Risk: 2.**
**Work:** move `dbglog_throttle`/`dbglog_first`/`dbglog_once` from `src/skygfx.h:89-123` into `src/core/debug_handl.cpp` (declarations stay in `skygfx.h`); use one `s_tagBuckets[512]` + FNV-1a; add a debug-build collision scan (or extend `tools/layering_check.py` check 3 to fail on same-family collisions only). Keep `DBGLOG_THROTTLE_MS` 2000.

### 3.5 Shader hash/include cleanup — **DELETE + MERGE-INTO-SERVICE**
**Payoff:** ~120 shader lines; two dead include files; one `Hash2D` implementation. **Risk: 2.**
**Work:** delete `shaders/include/PerlinNoise.hlsl` and `shaders/include/CarPaintNoise.hlsl` (zero includes); make `shaders/ps/CarPaint_Reflections.hlsl:50-66` include a shared `CarPaintNoise.hlsl` or inline only the used `hash22`; make `DynamicSky.hlsl:33` and `IBL_SkyCloud.hlsl:25` include one `CloudNoise.hlsl`; wire `CommonMath.hlsl` into PS consumers that currently copy `Hash2D`/`SafeDivide` (it is explicitly unwired at `:9-11`). Do **not** touch the VS wind hashes.

### 3.6 Fullscreen quad — **MERGE-INTO-SERVICE**
**Payoff:** 4 project copies → 1 (`s_ffQuad` `postfx_utils.cpp:142`, `envmap.cpp:192`, `neopipe.cpp:217`, `mirrorPass.cpp:251`); ~80 lines removed; one UV/NDC convention. **Risk: 2.**
**Work:** extend `SetupFullscreenQuad` (`postfx_utils.cpp:146`) with a `bool uvSpace` argument; make `envmap.cpp:192-240`, `neopipe.cpp:217-280`, `mirrorPass.cpp:251-315` call it. Leave `colorfilterVerts` (`postfx_utils.cpp:8`, game-owned `0xC400D8`) alone.

### 3.7 Device-reset registry — **MERGE-INTO-SERVICE**
**Payoff:** removes the hardcoded extern list in `depthhook.cpp:395-430` and the separate pre-reset list in `postfx.cpp:2635`; fixes `RSMon_Reset` never being called (`renderstate_monitor.h:19`); one ordering. **Risk: 3** (a missed pre-release fails `Reset`).
**Work:** add `Pipeline_RegisterDeviceReset(const char *name, void (*pre)(void), void (*post)(void))` to `pipelinecommon.cpp`; implement `Pipeline_DeviceResetPre/Post`; register `SkyNoise`, `TexAlpha`, `RSMon`, `WaterPipe`, `PostFX_IVGrade`, ImGui, and the lazy shader loaders at their `Init` (or first use); replace `depthhook.cpp:340` (`OnDeviceReset()`), `:358`, `:367`, `:412-425` with the two registry calls. Keep the documented order in comments.

### 3.8 Lazy-shader reset — **MERGE-INTO-SERVICE**
**Payoff:** fixes the post-Reset dead-handle class: `ivFPReady` (`buildingPipe.cpp:1044-1056`), `FxLoadShaders` (`postfx_newfx.cpp:85-93`), `mirrorPSLoadTried` (`mirrorPass.cpp:98`), `s_uwLoadTried` (`postfx_underwater.cpp:140`), `EnsureIVGradePS` (`postfx_colourfilter.cpp:1446`). **Risk: 3** (shader creation on the render thread after reset).
**Work:** add `Pipeline_RegisterLazyShader(void **slot, bool *tried, int resourceId, bool isVS)`; call it once per loader; `Pipeline_InvalidateShaders` (`pipelinecommon.cpp:723`) sets `*tried = false` and `*slot = NULL` for every registration; the next loader call recreates through `makePS`/`makeVS` with `s_forceShaderRecreate`.

### 3.9 Camera/screen-size service — **MERGE-INTO-SERVICE**
**Payoff:** one screen-size cache + one camera-raster cache; ~15 direct reads unified; prevents transient 2048×1024 sizing (`postfx_utils.cpp:63-94`). **Risk: 3.**
**Work:** extend `postfx_utils.cpp` (or a new `src/render/cameraView.{cpp,h}` if the postfx-support merge lands first) with `CameraView_ScreenSize(int*,int*)`, `CameraView_CameraRaster()`, `CameraView_FrontBuffer()`, `CameraView_SyncFrontBuffer()`, `CameraView_Invalidate()`. Replace `neopipe.cpp:1326-1327`, `depthhook.cpp:215/238/267/500`, `buildingPipe.cpp:1651/2582/3142`, `forwardplus.cpp:1150/1275`; keep `main.cpp:3755` `InvalidateCameraRasterCache` as a thin alias.

### 3.10 Timecycle colour accessor — **TEMPLATE-APPLY**
**Payoff:** one `const CColourSet&` accessor; 4 direct readers unified; grass ambient stops bypassing `ambientMult`/night clamp. **Risk: 2.**
**Work:** add `const CColourSet &Timecycle_GetColours(void)` beside `GetTimecycleAmbient` (`pipelinecommon.cpp:175`); replace `postfx.cpp:2791`, `:4349`, `postfx_colourfilter.cpp:327`; change `main.cpp:2484-2486` to call `GetTimecycleAmbient()`; keep `weather.cpp:250`/`:343` as the only writer.

### 3.11 CloudsPS binding — **TEMPLATE-APPLY**
**Payoff:** activates `IDR_CLOUDPS`/`IDR_CLOUDSSKYPS` (`resources/resource.h:143-144`, `Resource.rc:142-143`), which are compiled/embedded (`fast_build.py:539-543`) but **never loaded or bound**; removes a latent undefined-sampler read. **Risk: 3** (visual change).
**Work:** load both CSOs in `CreateShaders` (`pipelinecommon.cpp:729`) or via the shader registry; bind s9 + c49-c52 in the cloud pass (currently `RenderIBLBuffer` uses `DynamicSky`, `postfx.cpp:2879`); add a cloud-specific `NoiseParams` block (see §4.6) because the default c51 is hardcoded — it is a usable cloud block, but a cloud consumer cannot override it through the current API.

### 3.12 World baseline ownership — **TEMPLATE-APPLY**
**Payoff:** the frame-boundary resyncs call `pipeApplyWorldBaseline` (`pipelinecommon.cpp:1975`) instead of hand-rolling canonical pushes; ~60 lines removed. **Risk: 3.**
**Work:** `main.cpp:3398-3426`, `:4590-4600`, `buildingPipe.cpp:3151-3184`; leave per-draw guards (`pipeForceCullMode`, `pipeEnterAlphaMode`) as they are.

### 3.13 Alpha-block ownership — **MERGE-INTO-SERVICE**
**Payoff:** `pipeForceAlphaBlock` moves from `buildingPipe.cpp:525` to `pipelinecommon.cpp` beside `pipeEnterAlphaMode`; removes the three `extern` declarations in `main.cpp:857/3672/3987`; one owner for the alpha half of the doctrine. **Risk: 3.**
**Work:** move the two functions (`:522-720`) verbatim; keep the `PipeAlphaState` overload; update `buildingPipe.cpp` includes and `main.cpp` declarations.

### 3.14 Preset/mode absorption — **ABSORB**
**Payoff:** `presets.cpp` 157 → ~40-line adapter; dead `PresetConfig` fields (`buildingPipe`, `vehiclePipe`, `smaaPreset`) and dead API (`skygfx.h:1568-1592`) removed; one profile table. **Risk: 4** (user-facing Style combo).
**Work:** replace `presetConfigs` (`presets.cpp:25`) with a `{PresetId → ModeId + legacy feature overrides}` adapter; `ApplyPreset` (`:108`) calls `ApplyMode` for the pipe/identity fields and keeps the legacy colour-filter/feature writes; delete the dead `buildingPipe`/`vehiclePipe`/`smaaPreset` fields and the `*Mode_GetPreset` declarations (`skygfx.h:1568-1592`); route `debugmenu_ui.cpp:2863-2896` through `ModeManager_SetMode`; keep `gamePreset` as a read alias for one release (`main.cpp:6742`, `:7594`).

### 3.15 INI schema — **MERGE-INTO-SERVICE**
**Payoff:** one schema drives `readIniFile` (`main.cpp:5905`), `saveConfigTo` (`:7256`), `ADD_IF_MISSING` (`:6909`), the fresh writer (`:7037`), `MenuWriteMissingIniKeys` (`debugmenu_ui.cpp:3561`) and `gen_ini_ref.py:6-9`; removes ~250 lines of duplicate key lists and the save/load drift class. **Risk: 4** (every key round-trip).
**Work:** add `src/core/config_schema.{cpp,h}` with rows `{section, key, type, default, offset/getter, setter, flags}`; keep `Config` as the storage; migrate `main.cpp` and `debugmenu_ui.cpp`; update `tools/gen_ini_ref.py` to read the schema; add build deltas in §5 Stage 8.

### 3.16 Texture semantics — **KEEP-SEPARATE** (with one optional merge)
**Payoff:** none immediate; avoids conflating vehicle name heuristics (`veh_shaders.cpp:250-284`) with authored metadata (`skygfx.h:1117`) and alpha pixels (`texalpha.cpp:793`). **Risk: 3** if forced.
**Work:** keep the three classifiers; expose `VehShaders_GetSurfaceType` to the PBR lane only if vegetation/skin naming needs it; do not move vehicle parsing into `texdefs`.

### 3.17 SSAO noise tile — **KEEP-SEPARATE**
**Payoff:** avoids a 3-channel → scalar rewrite of the SSAO TBN rotation. **Risk: 2.**
**Work:** leave `postfx_ssao.cpp:95-116`; optionally add `SkyNoise::RandomVectorTile()` if a second consumer appears; do not bind s9 in SSAO (s1 is its contract).

### 3.18 treeWind phase — **KEEP-SEPARATE**
**Payoff:** preserves game-parity sway (`treeWind.cpp:135-176`). **Risk: 1.**
**Work:** none. If a shared seed helper is wanted, use `SkyNoise::White2` only for the seed and prove no visual change.

### 3.19 Grain IGN — **KEEP-SEPARATE**
**Payoff:** preserves the TAA-friendly IGN (`Grain.hlsl:43-60`). **Risk: 1.**
**Work:** none; if the removed s9 mode 2 is restored, bind through the noise service and set POINT filtering.

### 3.20 PBR c23 override — **KEEP-SEPARATE**
**Payoff:** none; the override is a deliberate per-material light block (`vehiclePipe.cpp:2925-2939`). **Risk: 1.**
**Work:** optionally add `pipeUploadPBRLight()` so the override is named; do not force it through `pipeUploadPBR`.

### 3.21 Profiler unification — **TEMPLATE-APPLY**
**Payoff:** one macro/API; ~30 lines. **Risk: 2.**
**Work:** make `PerfTimer` (`skygfx.h:76-80`) a `PfScope` alias with a `g_pfEnable` gate; keep `PostfxPerfGate` (`postfx.cpp:21-30`) as a gated wrapper or delete it; `PF_SCOPE` remains the only macro.

---

## 4. Noise-specific deep dive

### 4.1 What `SkyNoise` exposes

`src/render/noise.h` [LF] declares 22 functions. Only 5 have live callers today (the last row is the unused knob pair):

| API | Caller today |
|---|---|
| `Init` (`noise.cpp:763`) | `pipeBindNoise` `pipelinecommon.cpp:2075` |
| `BindAndUpload` (`:897`) | `pipeBindNoise` `pipelinecommon.cpp:2099` |
| `ReleaseDeviceObjects` (`:830`) | `depthhook.cpp:358` |
| `OnDeviceReset` (`:841`) | `depthhook.cpp:419` |
| `GetStats` (`:943`) | `pipeBindNoise` log `pipelinecommon.cpp:2106` |
| `SetQuality`/`SetSeed` | **no caller** (the `[General] NoiseQuality` key drives the dead utility tile instead) |

Unused but exported: `Shutdown`, `Perlin2/3`, `PerlinTileable2`, `Fbm2`, `FbmTileable2`, `Worley2`, `WorleyFbm2`, `White2`, `BlueNoise2`, `UniversalSize/Data/Texture`, `BindUniversal`, `LogStats`. This is the YAGNI counterexample: the service is correct but over-built. The template says expose the used surface; move the scalar generators to `static` in the `.cpp` unless a CPU consumer lands.

### 4.2 Who binds the tile today

- `pipeBindNoise(int samplerStage)` — `src/render/pipelinecommon.cpp:2071`. It lazily `Init(1337)`, stamps `GetTickCount`/`renderFrame` into c50, and calls `BindAndUpload` with the default block `p49..p52` (`:2094-2097`). It is the **only** CPU binder.
- `buildingPipe_bindNoiseFor(void *ps)` — `src/render/buildingPipe.cpp:974`; called at `:1011` and `:2061`; latched per render callback (`s_noiseDetailBoundCB`, cleared at `:1190`/`:1696`) so at most one upload per callback.
- `SkyNoise::BindAndUpload` sets sampler stage `s9` WRAP/LINEAR/no-mips/aniso=1 (`noise.cpp:913-921`) and uploads c49-c52 to **both** the raw device and the RW cache (`:925-939`).
- Shader consumers today: `shaders/noiseDetailBuildingPS.hlsl:26` [LF] (building noise-detail family) and `shaders/ps/CloudsPS.hlsl:87` [LF] (clouds).
- `CloudsPS` is compiled (`fast_build.py:539-543`) and embedded (`resources/resource.h:143-144`, `Resource.rc:142-143`), but **no C++ TU references `IDR_CLOUDPS`/`IDR_CLOUDSSKYPS`**; it is a latent undefined-sampler read. `Grain.hlsl:1-20` documents the same class: its s9 mode 2 was removed because `DrawFilmGrain` never called `pipeBindNoise(9)`.

### 4.3 Who does not bind it, and whether they should

| Consumer | Binds? | Should bind? | Reason |
|---|---|---|---|
| Building noise-detail PS (`noiseDetailBuildingPS.hlsl`) | Yes, via `buildingPipe_bindNoiseFor` | Already correct | The building lane only reads c52.x (jitter); the bind is latched per callback |
| Clouds (`CloudsPS.hlsl`) | **No C++ loader/binder** | **Yes** | It already includes `Noise.hlsl` and reads c49-c52; without s9 it degrades to the documented "drive=0" fallback (`CloudsPS.hlsl:71-73`) |
| `DynamicSky` (`postfx.cpp:2879`) | No | No (for now) | It uses its own `hash/noise3d` and c0-c8; binding s9 would require a shader rewrite (no register collision, but no benefit without replacing the noise). `CloudsPS` is the intended replacement |
| `treeWind` (`treeWind.cpp:135-176`) | No | No | Temporal wind phase, not a spatial tile; game-parity table |
| Film grain (`Grain.hlsl`) | No | Only if s9 mode 2 returns | IGN is deliberate; the removed mode 2 would need `pipeBindNoise(9)` + POINT filtering |
| SSAO (`postfx_ssao.cpp:95-116`) | No (s1 4×4 random vectors) | No | Different distribution/register; s1 is its contract |
| SMAA (`postfx_smaa.cpp`) | No | No | SMAA uses area/search textures; no noise term |
| Dithering in `postfx` (`Grain`, analytic CA/Vignette patterns) | No | No | IGN/analytic patterns are cheaper than a texture fetch; blue-noise dither is only useful where banding is proven |

### 4.4 Divergences that must be fixed before binding clouds

1. **The block is cloud-shaped but hardcoded.** `pipeBindNoise` uploads `p49 = {8.0, 4.0, 0.45, 0.35}`, `p51 = {0.10, 0.32, 1.0, 0.6}`, `p52 = {0.012, 0.35, 0.5, 0.0}` (`pipelinecommon.cpp:2094-2097`). Those are the cloud knob set (`CloudsPS.hlsl:56-62`, `:125-130`); the building noise-detail PS only reads c52.x (`noiseDetailBuildingPS.hlsl:32-37`). So the block is effectively cloud-owned while the binder is named after the building lane, and a cloud consumer cannot tune it without editing `pipeBindNoise`.
2. **c50 time/frame is stamped by the binder.** That part is universal and correct (`noise.cpp:923-933`); do not move it into per-consumer blocks.
3. **One global block, two consumers.** If building and clouds both bind in the same frame, the last upload wins. The building lane only needs c52.x; clouds need the whole block. A split `BindTile`/`UploadParams` makes ownership explicit.
4. **Quality knob mismatch.** `[General] NoiseQuality` drives `utilitynoise` (32/128/512/1024), not `SkyNoise` (32/64/128). `SkyNoise::SetQuality` has no caller. After the utility deletion, repoint the key to `NQ_*` and document the size change.
5. **Alpha channel mismatch.** `utilitynoise` writes Bayer 8×8; `SkyNoise` writes Mitchell blue noise. Any shader that sampled the utility tile expecting Bayer would change; there are no such shaders today.

### 4.5 Should `utilitynoise.cpp` be deleted?

**Yes — delete, do not merge.** It has **zero render consumers**: outside its own TU, `g_pUtilityNoise` appears only in `main.cpp:754`, `debugmenu_ui.cpp:3643/4320` and the `skygfx.h:968` declaration; no `RwRenderStateSet(TEXTURERASTER, g_pUtilityNoise)` exists. Its only surfacing is the debug "Utility" tab (`debugmenu_ui.cpp:2062-2063`, `:2418-2419`, `:4319-4325`) plus the F11 regen path (`main.cpp:753-755`). It is a superseded duplicate with an incompatible lifetime (eager `RwTexture` vs lazy `IDirect3DTexture9`) and an incompatible alpha channel (Bayer vs blue). Merging it into `SkyNoise` would force one lifetime model on two different contracts; the render roadmap reached the same verdict (`docs/plans/2026-10-27-render-refactor-roadmap.md:276-316`). Keep the `[General] NoiseQuality` key alive by repointing it to `SkyNoise::SetQuality`; that plus the loss of the debug Utility-tab noise widgets is the only user-visible change.

### 4.6 The binding contract to adopt

Split the current one-call API into two layers:

```cpp
// noise.h — tile ownership is separate from parameter ownership
struct NoiseParams { float p49[4], p50[4], p51[4], p52[4]; };
bool BindTile(int stage);                       // s9 only, no constants
bool UploadParams(const NoiseParams &p, float timeSeconds, unsigned frame);
bool BindAndUpload(int stage, const NoiseParams&, float time, unsigned frame);
```

- `BindTile` is idempotent and cheap; callers may bind s9 once per pass.
- `UploadParams` is per-consumer; the building lane keeps the default block it has today (it only consumes c52.x), the cloud lane supplies its full cloud block, and `pipeBindNoise(stage)` remains the universal default for compatibility.
- `Noise.hlsl` keeps the register block for shaders that want the default; new consumers that need different values should take explicit parameters or upload their own block immediately before the draw.
- Sampler state: `s9` is the project-wide reservation; `s10` stays free (`pipelinecommon.cpp:2054`). Do not add a second noise sampler.

### 4.7 Register/sampler table (canonical)

| Slot | Contents | Owner |
|---|---|---|
| `s9` | packed tile, WRAP/LINEAR/no mips/aniso 1 | `SkyNoise::BindAndUpload` |
| `c49` | `{noiseScale, worleyScale, coverage, erosion}` | universal default; per-consumer override |
| `c50` | `{whiteScale, blueScale, timeSeconds, frameIndex}` | stamped by the binder |
| `c51` | `{cloudScaleA, cloudScaleB, cloudScaleC, cloudDensity}` | cloud default; per-consumer override |
| `c52` | `{jitterAmount, ditherAmount, detailStrength, unused}` | universal default; per-consumer override |

---

## 5. Ordered stage plan

Sequenced by risk and dependency: the first stages are the lowest-risk independent changes; later stages either touch user-facing config or are optional polish. Each stage is independently compilable; run the verification before the next stage. `python tools/fast_build.py --fastest` is the default build+deploy; `--check-shaders` validates HLSL without a DLL build. The runtime log path is the documented game log (`E:/games/gtasa_skygfx_plus/skygfx_dbg.log`).

### Stage 0 — Baseline (no edits)
```bash
python tools/fast_build.py --check-shaders
python tools/fast_build.py --fastest
python tools/layering_check.py            # read-only; record the counts
```
Record: shader census, `[INIT] shaders` summary, layering counts (802 raw sites / 790 outside doctrine / 102 cross-TU globals / 104 colliding buckets). Do not write a baseline yet; the tree is dirty.

### Stage 1 — Delete dead shader includes
**Files:** `shaders/include/PerlinNoise.hlsl` (delete), `shaders/include/CarPaintNoise.hlsl` (delete or make `CarPaint_Reflections.hlsl` include it).
**Build delta:** none. HLSL includes are resolved by FXC; `fast_build.py` fingerprints every `.hlsl` under `shaders/` (`:145-163`). `resources/resource.h`/`Resource.rc` do not list includes.
**Verify:** `python tools/fast_build.py --check-shaders`; `rg -n "PerlinNoise|CarPaintNoise" shaders` returns only the new include.

### Stage 2 — Noise service consolidation
**Files:** `src/render/utilitynoise.cpp` (delete), `src/core/main.cpp`, `src/core/debugmenu_ui.cpp`, `src/skygfx.h`, `src/render/noise.h`, `src/render/noise.cpp`.
**Build delta:**
- `build/skygfx.vcxproj:207` remove `<ClCompile Include="..\src\render\utilitynoise.cpp" />`.
- `build/skygfx.vcxproj.filters:163` remove the same `ClCompile` row.
- `premake5.lua:185` (`files { "src/render/*.cpp" }`) needs no edit; the glob stops seeing the file. `noise.h` is already in the explicit header list at `:246`.
- `tools/fix_build.py`: add a `FORBIDDEN_SOURCES` check for `render/utilitynoise.cpp` (optional; the integrity list only checks required files at `:232-242`).
**Verify:** `python tools/fast_build.py --fastest`; `rg -n "GenerateUtilityTexture|g_pUtilityNoise|g_CurrentNoiseSize|NoiseResForQuality|utilityNoiseResForQuality" src/` returns zero; `g_noiseQuality` may remain only as the `SkyNoise` quality holder; game log has `[Noise] rebuild` and `[Pipeline] pipeBindNoise` but no `[UtilityNoise]`.

### Stage 3 — Texture extraction helper
**Files:** `src/texdefs.h`, `src/texdefs.cpp`, `src/render/texalpha.cpp`, `src/core/main.cpp`, `src/entities/chars.cpp`, `src/render/postfx_smaa.cpp`, `src/render/postfx_ssao.cpp`.
**Build delta:** none (all existing TUs).
**Verify:** `python tools/fast_build.py --fastest`; `python tools/layering_check.py` (raw-site count must not rise); `rg -n "0xB4E9E0" src` returns only `texdefs.cpp` (plus comments).

### Stage 4 — State primitives (`pipeParkSet3L`, `pipeSave3L`, alpha block)
**Files:** `src/render/pipelinecommon.cpp`, `src/skygfx.h`, `src/render/envmap.cpp`, `src/render/mirrorPass.cpp`, `src/render/neopipe.cpp`, `src/core/main.cpp`, `src/render/buildingPipe.cpp`.
**Build delta:** none.
**Verify:** `python tools/fast_build.py --fastest`; run the game through a mirror/water/env-map frame; `rg "rs_mon|\[MirrorPass\]|\[EnvTex\]"` shows no new divergence; compare `pipeParkSet3L` end state with `RestoreRawGeomStates` (same captured values).

### Stage 5 — Device-reset + lazy-shader registry
**Files:** `src/render/pipelinecommon.cpp`, `src/skygfx.h`, `src/render/depthhook.cpp`, `src/render/postfx.cpp`, `src/render/buildingPipe.cpp`, `src/render/postfx_newfx.cpp`, `src/render/mirrorPass.cpp`, `src/render/postfx_underwater.cpp`, `src/render/postfx_colourfilter.cpp`, `src/render/renderstate_monitor.cpp`.
**Build delta:** none.
**Verify:** `python tools/fast_build.py --fastest`; launch, Alt-Tab / change resolution to force `Reset`; log must show `[Noise] OnDeviceReset`, no `DepthHook: orig_Reset FAILED`, and no `makePS(...): FindResource FAILED`; `TexAlpha_OnDeviceReset` and `RSMon_Reset` are silent, so verify the next `texalpha:` line re-probes and the first post-reset `rs_mon` report carries no pre-reset counts; IV Forward+ still draws (`[IV-FP]` not "keep the plain pair" after reset).

### Stage 6 — Camera and colour accessors
**Files:** `src/render/postfx_utils.cpp`, `src/render/postfx.cpp`, `src/render/neopipe.cpp`, `src/render/depthhook.cpp`, `src/render/buildingPipe.cpp`, `src/render/forwardplus.cpp`, `src/render/waterPipe.cpp`, `src/core/main.cpp`, `src/render/postfx_colourfilter.cpp`, `src/render/pipelinecommon.cpp`.
**Build delta:** none (if `CameraView` lives in `postfx_utils.cpp`). If a new `cameraView.{cpp,h}` is chosen instead, add it to `build/skygfx.vcxproj` (ClCompile near `:191`, ClInclude near `:146`), `.filters`, `premake5.lua` explicit list (`:204-228`) and `tools/fix_build.py` integrity list (`:232-242`).
**Verify:** `python tools/fast_build.py --fastest`; `rg "UpdateFrontBuffer SKIP|camRas .* != screen"` must not increase; water/mirror camera swap still restores the view-proj (`rg "D3D9ActiveTransform"` only in `pipelinecommon`/the camera service, not in new call sites).

### Stage 7 — Preset/mode absorption
**Files:** `src/core/presets.cpp`, `src/core/modeManager.cpp`, `src/core/debugmenu_ui.cpp`, `src/skygfx.h`, `src/core/main.cpp`.
**Build delta:** none if `presets.cpp` stays as a thin adapter. If deleted, remove `build/skygfx.vcxproj:205` and the `.filters` row; `premake5.lua` glob handles it.
**Verify:** `python tools/fast_build.py --fastest`; cycle the Style combo and the mode chips; `[Mode] apply`/`[Mode] disengage` receipts match the UI; `gamePreset` still round-trips on save/load.

### Stage 8 — INI schema
**Files:** new `src/core/config_schema.{cpp,h}`; `src/core/main.cpp`; `src/core/debugmenu_ui.cpp`; `src/render/forwardplus.cpp`; `src/texdefs.cpp`; `tools/gen_ini_ref.py`.
**Build delta:**
- `build/skygfx.vcxproj`: add `<ClCompile Include="..\src\core\config_schema.cpp" />` beside `main.cpp` (`:178`) and `<ClInclude Include="..\src\core\config_schema.h" />` beside `modeManager.h` (`:156`).
- `build/skygfx.vcxproj.filters`: add both rows under the `src\core` filter.
- `premake5.lua`: `files { "src/core/*.cpp" }` (`:180`) picks up the `.cpp`; add `"src/core/config_schema.h"` to the header list (`:230-253`).
- `tools/fix_build.py`: add `core\config_schema.cpp` to the required-source loop (`:232-242`).
**Verify:** `python tools/gen_ini_ref.py` (regenerate `docs/ini-reference.md`), then `python tools/gen_ini_ref.py --check` (must be clean); `python tools/fast_build.py --fastest`; save/reload an INI and diff the keys (no dropped/duplicated keys); `[General] NoiseQuality` and the 12 `MenuWriteMissingIniKeys` keys round-trip.

### Stage 9 — Optional cleanups (after the above)
- Shader hash consolidation (`CommonMath.hlsl` wiring, cloud hash include): no build delta; verify `--check-shaders`.
- Fullscreen quad service: no build delta; verify a mirror + env-map + postfx frame.
- Log guard unification: no build delta; verify `layering_check.py` check 3 reports zero collisions.
- Profiler alias: no build delta; verify `PF_SCOPE` lines still appear with `profilerEnable=1`.

---

## 6. Explicit non-goals (do not do)

- **Do not merge** `main.cpp`, `postfx.cpp`, `buildingPipe.cpp`, `vehiclePipe.cpp`, `debugmenu_ui.cpp` into any service; they are >2,500 lines and the first two are SEH-heavy.
- **Do not merge** `envmap.cpp` or `mirrorPass.cpp`; they carry `__finally` (C2702 risk). Make them call `pipeParkSet3L` instead.
- **Do not unify** VS wind hashes into `Noise.hlsl`; vs_3_0 vertex texture fetch is not guaranteed and the wind is game-parity.
- **Do not bind** s9 in SSAO/SMAA; their noise contracts are different and their registers are reserved. Grain binds s9 only if its removed mode 2 is deliberately restored.
- **Do not force** `ragdoll.ini` into `skygfx.ini` in this pass; it is a separate file with a separate owner (`sftbdyphsx.cpp:666`, `ragdoll_death.cpp:147`).
- **Do not add** a new `extern` to `depthhook.cpp`; register the service instead.
- **Do not normalise** line endings across a stage; edit each file in its existing convention (§0).

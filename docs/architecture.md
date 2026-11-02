# Architecture

Subsystem map, frame order, and the render-state rules that everything else depends on. Line numbers refer to the current tree and drift when files grow; symbols are stable, line numbers are hints.

Reading guide: start with the subsystem map, then the frame order, then the state doctrine. The doctrine explains most visual bugs; the contracts explain where to fix them.

## Subsystem map

| Subsystem | Files | Responsibility |
|---|---|---|
| Entry / config / hooks | `src/core/main.cpp` | `DllMain`, INI read/write (`readIniFile` :5106), pipeline mapping, hook installation (`InjectDelayedPatches` :6983, delayed block :7297-7600), frame hooks, profiler boundary |
| Mode manager | `src/core/modeManager.cpp/.h` | one-active-mode state machine, per-slot base snapshot, `ApplyMode`, canonical `kValues*` profiles, mode-owned introspection (`ModeManager_IsOwnedKey`/`GetOwnedValue`) |
| Presets | `src/core/presets.cpp` | 15 built-in `GamePreset` entries, `ApplyPreset` (writes colours/features, clears mode latch). The six per-mode writers (`ps2_mode`, `xbox_mode`, `pc_patched_mode`, `iv_mode`, `custom_mode`, `mobile`) are retired no-op stubs — `ModeManager` is authoritative |
| Debug menu | `src/core/debugmenu_ui.cpp`, `debugmenu_theme.cpp`, `debug_handl.cpp` (menu inject) | ImGui menu, Modes rows, live config edits |
| Building pipe | `src/render/buildingPipe.cpp` | RW building callbacks (PS2/Xbox/GTAIV/PBR), grass, tagged objects, dual pass, noise detail |
| Vehicle pipe | `src/render/vehiclePipe.cpp` | RW vehicle callbacks (PS2/Xbox/PC/GTAIV/Mobile/Modern), specular/env, paint classification |
| Pipe common | `src/render/pipelinecommon.cpp/.h` | matrices, light upload, shader creation, `pipeUploadPBR`, `pipeApplyWorldBaseline`, alpha/state helpers |
| PostFX | `src/render/postfx*.cpp` | colour filter chain, SSAO, SMAA/FXAA, motion blur, bloom, DoF, sun shafts, radiosity, SSS, grain/CA/vignette, underwater |
| Forward+ | `src/render/forwardplus.cpp` | tile cull + light atlases, `ForwardPlus_CullAndUpload` |
| Env map / mirror | `src/render/envmap.cpp`, `mirrorPass.cpp` | environment map render, mirror pass, reflection composite |
| State monitor | `src/render/renderstate_monitor.cpp` | `RSMon_Sample` receipts, drift reports |
| Water | `src/render/pipelinecommon.cpp` + water shaders | style selection (`waterStyle`), IV/V shaders, tiered quality |
| Normal maps / depth | `src/texdefs.cpp`, `depthhook.cpp` | RW normal-map plugin attach, INTZ depth hook |
| Misc renderers | `src/render/{neo,neoCarpipe}.cpp` | live per-era helpers; `{iv_mode,ps2_mode,mobile,custom_mode,pc_patched_mode}.cpp` are retired no-op stubs |
| RW SDK bridge | `src/rw/gta.h/.cpp` | globals (`pDirect`, `pAmbient`, `Scene`, `d3d9device`) + WRAPPER/EAXJMP thunks to the exe's RW 3.6 |
| Crash / diagnostics | `src/core/debug_handl.cpp`, `src/render/profiler.cpp` | minidump/SEH, log helpers, frame profiler |

## Per-frame hook order

`RenderScene_hook` (`src/core/main.cpp:4652`) is the frame's spine. It runs, in order:

| # | Stage | Entry point | Anchor |
|---|---|---|---|
| 1 | Ctrl+F4 menu toggle, ragdoll update, weather/timecyc blend | `RenderScene_hook` prologue | `main.cpp:4652-4691` |
| 2 | Clouds hygiene window opens at `CClouds::Render` | `CClouds__Render_hygiene` | `main.cpp:2460-2470` |
| 3 | Sprite flush shield | `CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect` hook | `main.cpp:2678-2730`, install :7530 |
| 4 | Corona reflections (`CCoronas::Render`) inside `RenderEffects` | `CCoronas__Render_blendprobe_hook` | `main.cpp:2436-2450`, install :7411 |
| 5 | Point lights | `CPointLights` tail hook `0x700BCD` | `main.cpp:2646` |
| 6 | Shadows (static/stored/extra player) | `CShadows__Render*_hook` → `shadowContractRun` | `main.cpp:1348-1416` |
| 7 | Env-map / mirror / reflection passes | `envmap.cpp`, `mirrorPass.cpp` | hook block :7318-7330 |
| 8 | World render | `RenderScene_before` → `RenderScene()` → `RenderScene_after` | `main.cpp:3069`, `:4696-4698`, `:4625` |
| 9 | Water | water render callbacks (`waterStyle` selects shader) | water hooks near :7318 |
| 10 | Skidmarks | `CSkidmarks__Render` | `main.cpp:2974-3006` |
| 11 | `RenderEffects` (coronas, particles, fx) | `RenderEffects_hook` → `effectsForceAlphaBlock` enter/exit | `main.cpp:3558-3573` |
| 12 | PostFX chain | `DrawFinalEffects` / `PostFX` chain | `postfx.cpp`, install via `0x53EBE9` :7527 |
| 13 | `DrawFinalEffects` tail | `DrawFinalEffects_pf`: frame end, corona window close, world baseline, `PfFrame`, `RSMon_Report` | `main.cpp:6941-6958` |

Install order is not frame order: immediate hooks go in `DllMain`, delayed patches in `InjectDelayedPatches`, and some hooks are installed from `InitialiseGame_hook` if the game path was missed. `RenderScene_before` is also registered as the UG `EVENT_BEFORE_RENDERSCENE` callback (`main.cpp:4734`), which is why per-frame setup that must always run lives there and not in `RenderScene_hook`.

Frame boundary is `DrawFinalEffects`, not present: the profiler and `RSMon_Report` treat it as the end of the frame (`main.cpp:35`).

## Render-state doctrine (L1/L2/L3)

RW's D3D9 driver keeps a **write cache**: `RwD3D9SetRenderState` parks values in `pending[]`, and `_rwD3D9RenderStateFlushCache` pushes them to the device pre-draw only when `applied[] != pending[]`. There are three layers: **L1** = the mod's canonical intent, **L2** = the RW cache (`RwRenderStateSet/Get`), **L3** = raw `d3d9device->SetRenderState`. Correct sequence is L1/L2 writes → one cache flush so `pending[] == applied[] == canonical` → **then** raw L3 writes, so the device equals the cache and a later flush cannot resurrect a stale value (`pipelinecommon.cpp:1444-1462`). A raw L3 writer that runs *without* the preceding L2 commit desyncs the cache permanently: the device holds X while `applied[]` holds Y, every later RW write of Y is a cache-hit skip, and X survives all flushes until a full reset. That is the class of bug behind stale ALPHAREF 140, wrong blends after env-map, and black shadow quads. Builders: `pipeEnterAlphaMode`, `pipeForceCullMode`, `pipeApplyWorldBaseline` (`pipelinecommon.cpp:1881-1915`) — use those, never bare `dev->SetRenderState` outside a documented pin.

Every hook that changes state runs an enter/exit pair (`effectsForceAlphaBlock("RenderEffects.enter"/"exit")`, `shadowContractRun` capture/force/restore) and logs drift through `renderstate_monitor`.

## Hook installation order

Three installation phases, in this order:

1. **DllMain / immediate** — module handle, log open, crash handler, early device/raster probes.
2. **`InjectDelayedPatches()`** (`main.cpp:6983`) — runs once from DllMain after the game modules are mapped. It intercepts `CGame::Initialise` (`0x748CFB`, `main.cpp:7092`), installs FX-quality hooks (`:7128-7134`), optional cloud/gamma/glare patches (`:7145-7159`), the delayed block at `:7297-7600` (render scene, building/vehicle pipes, postFX, sprites, RNG), and ends with `=== InjectDelayedPatches complete ===` (`:7216`).
3. **`InitialiseGame_hook()`** (`main.cpp:4758`) — legacy path; runs after the game's own `CGame::Initialise`, re-asserts INI-derived globals, and is skipped under MoonLoader (`:4715-4722`). `installDeferredHooks()` is idempotent and called from both phases.

Pipe install sites: building `0x5D7100` (`buildingPipe.cpp` install block), vehicle `0x5D9FE9` (`vehiclePipe.cpp:3375` region). PostFX install sites: `0x704D1E` colour filter, `0x53EBE9` DrawFinalEffects, `0x704D5D/0x704FB3` radiosity, `0x704F59/0x704EE8/0x705078/0x705091` grain/vision, sprite `0x700B6B` (`main.cpp:7511-7530`).

A hook that is installed but never fires produces no log line. When a subsystem looks dead, check the install receipt first: `InjectDelayedPatches entered` / `=== InjectDelayedPatches complete ===`.

## Config ownership and precedence

```
configs[] slots  ← per-slot Config, selected by setConfig()
   ↑ base snapshot (modeManager s_baseConfig[slot])
   ↑ mode profile (ApplyMode, owned fields only)
   ↑ top-level [SkyGfx] keys (readIniFile, fields not owned)
   ↑ live menu edits (debugmenu_ui, highest)
```

Writers that bypass `ApplyMode` and can desync the chain: `pipelineOverride` (`main.cpp:5174-5177`), quality tiers 0-2 pipe writes when no mode is active (`:5230-5233, 5261-5263, 5283-5285`), `ApplyPreset` clearing the latch (`presets.cpp:118-137`), and the debug-menu pipeline apply paths (source comment "pending M2", `main.cpp:5181-5184`). `derivePipelineFromPipes()` (`main.cpp:682-717`) is the derivation authority for the `pipeline` mirror; `tagsBuildingPipe` is mirrored unconditionally at `:5664`.

## Ped A/B diagnostics (`pedCullMode`, `pedAlphaTestEnable`)

Two global `[SkyGfx]` ints are kept as single-variable in-game A/B levers for the ped view-angle investigation. Both are read in `readIniFile()` and written by the INI save path (`src/core/main.cpp`), declared in `struct Config` (`src/skygfx.h`), and absent from every `kValues*` table in `src/core/modeManager.cpp` — a mode switch does not overwrite them. Defaults preserve the behaviour already described in this document.

| Key | Values (default first) | Behaviour | Receipt |
|---|---|---|---|
| `pedCullMode` | `-1`, `1`, `2`, `3` | `-1` keeps the game's sanitized raw cull for the ped span; `1` forces `CULLNONE` (two-sided), `2` `CULLBACK`, `3` `CULLFRONT` through L1/L2/L3. `0` and out-of-range behave as `-1`. The pre-entry cull is restored at ped exit via `pipeExitCullMode(savedCull)`. | `[PedCull] mode=%d saved=%u forced=%u`, emitted once and re-armed when the effective mode changes |
| `pedAlphaTestEnable` | `1`, `0` | `1` keeps the canonical `GREATEREQUAL@1` ped alpha test; `0` forces `ALPHATESTENABLE` FALSE and skips the `ALPHAFUNC`/`ALPHAREF` writes. Only active while `pedOpaqueContract != 0`; the pre-entry alpha state is restored at ped exit. | `[PedAlphaTest] enabled=%d`, emitted once and re-armed when the value changes |

Procedure: change exactly one key, reload the INI (F11) or restart, and compare the same camera with the receipt. `pedCullMode` tests the cull side; `pedAlphaTestEnable` tests the cutout test; the third lever is the mode disengage test (`[Modes] active=None`, or toggle the active chip off) to separate profile-owned state from renderer behaviour.

These are diagnostics, not look controls. Do not make a shipped preset depend on them, and do not treat either as mode-owned.

## Render graph by pipeline

| Pipeline | Building | Vehicle | Colour filter | PostFX owner |
|---|---|---|---|---|
| `PIPELINE_PBR` | `BUILDING_PBR` | `CAR_MODERN` | `COLORFILTER_MODERN` forced | Modern chain (`postfx.cpp`) |
| `PIPELINE_PS2` | `BUILDING_PS2` | `CAR_PS2` | `COLORFILTER_PS2` | legacy SA effects |
| `PIPELINE_XBOX` | `BUILDING_XBOX` | `CAR_XBOX` | inherited/top-level | legacy SA effects |
| `PIPELINE_GTAIV` | `BUILDING_GTAIV` | `CAR_GTAIV` | `COLORFILTER_GTAIV` (bypass) + `DrawIVGrade` | IV postFX stack |
| `PIPELINE_MOBILE` | `BUILDING_XBOX` | `CAR_MOBILE` | inherited | legacy |

Mode profiles override the pipeline row above (see [modes.md](modes.md)); the table is the legacy `pipeline=` mapping, which is ignored by pipe-owning modes.

## Threading and allocation

- Rendering hooks run on the game's render thread. `CClouds` state and the corona window are POD render-thread-only (`g_inCloudsRender`, `main.cpp:2465`).
- No heap allocation inside per-draw callbacks; mode snapshots are static arrays (`s_baseConfig`, `modeManager.cpp:31-34`).
- `dbglog` is not thread-safe by design; per-frame logging must be throttled.
- Weather/timecycle blending runs in `RenderScene_hook` after `CTimeCycle::Update()` and before any renderer read (`main.cpp:4683-4691`).

## State helper reference

Use these instead of raw device writes.

| Helper | File | Purpose |
|---|---|---|
| `pipeApplyWorldBaseline(tag)` | `pipelinecommon.cpp:1905` | canonical frame-boundary world state; called at `SceneStart` and `EffectsExit` |
| `pipeEnterAlphaMode()` | `pipelinecommon.cpp:1442` | capture + canonicalise alpha test/blend with L1/L2 → flush → L3 |
| `pipeForceCullMode()` | `pipelinecommon.cpp` | same doctrine for cull mode |
| `effectsForceAlphaBlock(tag)` | `main.cpp` (RenderEffects hooks) | enter/exit alpha pair around particle/corona draws |
| `shadowContractCapture/Force/Restore` | `main.cpp:1133-1330` | shadow-pass state contract with drift count |
| `CoronaWindow_Close()` | `main.cpp` | expires a deferred corona/cone sprite window |
| `RSMon_Sample(tag, frame)` | `renderstate_monitor.cpp` | records state receipt at scene/frame boundaries |
| `pipeUploadPBR(...)` | `pipelinecommon.cpp` | the only legal writer of PBR c22/c23 |
| `SMAATryInitRasters()` | `postfx_smaa.cpp` | deferred raster init outside the camera update |

Rule of thumb: if a state change is not expressible through one of these, it needs an enter/exit capture and a `[probe]` receipt.

## Pipe callback contracts

**Building pipe callback** (`buildingPipe.cpp`)
- Receives the RW atomic/sector info; must leave the world matrix, texture stages s0-s2, and alpha state as the next callback expects.
- Owns: building geometry, detail/noise detail, grass, tagged-object dispatch, dual pass.
- Must not: flip `pipeline`/`buildingPipe`, call `ApplyMode`, or leave a raw L3 state unreset.
- Shader uploads go through `pipeUploadPBR` for c22/c23; manual uploads produce flat output.

**Vehicle pipe callback** (`vehiclePipe.cpp`)
- Receives the vehicle atomic; selects the shader for the pipe (PS2/Xbox/PC/GTAIV/Mobile/Modern), uploads light/material constants, sets env map (s1), mask (s2), IBL (s3).
- Owns: paint classification, glass/rubber sub-shaders, reflection strength, `envMapSize` behaviour.
- Must not: assume `RwFrame`/`RwCamera` are non-null — guard every pointer.
- Hook install site is `0x5D9FE9` (`vehiclePipe.cpp:3375` region), not `0x5D9020`.

**Common rules for both**
- All `normalize()` guarded; GGX denominator clamped; `1e-7` epsilon floor.
- Save/restore any state you change; prefer the `pipe*` helpers.
- No allocation per draw; stack only.
- Shader model 3.0 only.

## Module invariants

1. **One mode writer.** `ApplyMode()` is the only writer of mode-owned fields; menu/preset paths must call it, not write profiles directly (`modeManager.h:9-23`). The six `*_mode.cpp` `*_ApplyDefaults` writers are retired no-ops (`skygfx.h:1488-1508`) and must not be revived as a second authority.
2. **`pipeline` is a mirror.** Pipes are authority; `derivePipelineFromPipes()` re-derives `pipeline` (`main.cpp:682-717`). `pipelineOverride` is the known exception that can desync it.
3. **`tagsBuildingPipe` is derived**, never read from INI (`main.cpp:5664`).
4. **PBR c22/c23 only via `pipeUploadPBR`**; the component meaning is per entry point, not global.
5. **`COLORFILTER_MODERN` is required for PBR**; `postfx.cpp ColourFilter_switch` re-derives it (`main.cpp:635`, `postfx.cpp`).
6. **Frame baseline.** `RenderScene_before` applies `pipeApplyWorldBaseline("SceneStart")`; `DrawFinalEffects` applies `pipeApplyWorldBaseline("EffectsExit")`. Skipping either leaks state across frames.
7. **Per-config-slot mode base.** Never assume a process-global snapshot; `setConfig()` can repoint `config`.
8. **RW 3.6 only.** No RW 3.7 API, no external RW SDK, no SM4/5. All RW calls through `src/rw`.
9. **SM3.0 only.** Every compile entry is `ps_3_0`/`vs_3_0` (30 entries in `tools/fast_build.py`, zero `ps_2_0`); no fallback profile exists.
10. **Log-gated debugging.** Per-frame logs use `dbglog_throttle(tag)`; init/errors use `dbglog()`. Every new subsystem needs a receipt line.

## Static layering gate

`tools/layering_check.py` is the enforceable form of the rules above and of `docs/plans/2026-10-24-refactor-blueprint.md` §2.4. It runs from any cwd and never edits source.

| # | Check | Blocking condition |
|---|---|---|
| 1 | raw `->SetRenderState`/`SetTexture`/`SetSamplerState`/shader/stream state writes | any write outside `src/render/pipelinecommon.cpp`, `src/render/postfx_utils.cpp`, or `tools/layering_allowlist.txt` |
| 2 | non-`static`, non-`const` file-scope variables in `.cpp` | a new external-linkage global |
| 3 | `dbglog_throttle`/`dbglog_once`/`dbglog_first` tags sharing an FNV-1a/256 bucket | any bucket with more than one tag |
| 4 | `InterceptCall`/`InjectHook`/`Patch<`/`Nop` sites in `main.cpp` | a site with no `0xE8`/`op=`/`MISMATCH`/`memcmp`/`s_*Expect` check in the preceding 40 lines |

```bash
python tools/layering_check.py                  # gate: FAIL on regression vs the baseline
python tools/layering_check.py --strict         # FAIL on any remaining debt
python tools/layering_check.py --json           # machine-readable report
python tools/layering_check.py --write-baseline # record current counts (ratchet)
```

Without `tools/layering_baseline.json`, every non-zero finding is FAIL. After `--write-baseline`, only counts above the recorded baseline fail (check 3 always fails on a collision). `tools/layering_allowlist.txt` is the sole place to record a legitimate raw-device exception; it is seeded empty.

# `src/core/main.cpp` split plan

**Date:** 2026-10-27
**Scope:** `src/core/main.cpp` (8,701 lines, CRLF, branch `very-experimental`, HEAD `b7396c4`,
~300 dirty/untracked files in the working tree).
**Deliverable of this doc:** exactly one plan. No source edits, no builds, no premake runs were
performed to produce it. Every line number below is the current working tree, not a post-edit
projection.

The split follows the project's established fortify/move pattern (see
`docs/plans/2026-10-27-render-refactor-roadmap.md:9-26` and the precedent in
`src/core/debug_handl.cpp`, `src/texdefs.cpp`, `src/entities/sftbdyphsx.cpp`):

1. Verbatim body moves; no wrapping, no rewriting, no reordering across function boundaries.
2. One prologue comment per new TU naming the source range and why order matters.
3. `// ---- Includes (deduped union) ----` block; strict union of the includes the moved body
   already compiled against. Never add a header the original body did not see.
4. `// ==== ... (was src/core/main.cpp:N-M) ====` banner per moved section.
5. Build-file swap in `build/skygfx.vcxproj`, `build/skygfx.vcxproj.filters`,
   `premake5.lua` (explicit TU tripwire + header list), `tools/fix_build.py` (integrity list).
6. Backup the touched files to `_backup/main_split_s<N>_2026-10-27/` before each stage.
   Never `git checkout` / `git restore`: ~300 unrelated dirty files are in this tree.
7. `cl /Zs` syntax gate before any link; full build + launch is the human acceptance step.

---

## 0. Invariants carried into every stage

| # | Invariant | Enforcement |
|---|---|---|
| I1 | **Config ABI is frozen.** `struct Config` stays in `src/skygfx.h:280-931`; the append-only EOF block (`shadowContractEnable` at `skygfx.h:913`, `spriteStateShield` at `skygfx.h:923`) and the offset static-asserts at `skygfx.h:925-927` are untouched. | Compile gate; no field is moved, added, removed, or reordered. `Config configs[10]` / `Config *config` definitions stay at `main.cpp:79-80`. |
| I2 | **Hook install order is load-bearing.** `InjectDelayedPatches` (`main.cpp:7810-8071`) and `DllMain` (`main.cpp:8108-8701`) remain in the entry TU. A module may own an `X_InstallHooks()` function, but the call must sit at the exact original line; no site may cross another site. | Byte-for-byte diff of the call sequence; install receipts in the log. |
| I3 | **No `__finally` in `main.cpp`** (verified count 0), **no `#pragma`** (verified count 0). 28 `__try` tokens (21 live fences; the rest are comments), 13 `__declspec(naked)` tokens (12 definitions), 17 asm tokens. C2702 is therefore structurally impossible in every stage; C2712 is prevented by moving bodies verbatim and never adding a class-with-destructor local to an existing `__try`. | `grep -c "__finally"` per stage must stay 0; `cl /Zs`. |
| I4 | **Every new TU basename is unique** (`build/skygfx.vcxproj` has 165 `ClCompile` rows; duplicate basenames = LNK4042). | Basename scan per stage. |
| I5 | **Line endings.** `src/core/*` is CRLF. New TUs extracted from `main.cpp` adopt CRLF; do not normalise the source file. | `file` + CRLF ratio. |
| I6 | **No new log tags, no renamed existing tags.** `dbglog_once`/`dbglog_throttle` keys are process-global strings; renaming one silently changes what the log proves. | Diff the string literals of every moved body. |
| I7 | **Backups, not reverts.** Each stage copies only its write set to `_backup/`; other lanes' files are never touched. | `git status` shows exactly the stage's declared files. |

---

## 1. Section inventory and per-section verdict

Verdict key:
**EXTRACT-NOW** = self-contained, no shared mutable state, clear API, ≤~600 lines, safe to move
now (SEH/naked allowed only when fully contained and verbatim-movable).
**EXTRACT-LATER** = needs an accessor/state handle or an install-order-preserving wrapper first.
**KEEP** = must stay in the entry TU (reason in the row).

### 1a. Bootstrap, globals, exports

| Lines | Section | Purpose | Verdict |
|---|---|---|---|
| 1-24 | Include block | Union of RW/game/debug/injector headers for the whole TU. | **KEEP** — entry TU's include surface. |
| 26-39 | Merge/status notes | Historical banners (debugmenu stubs, grass gate, profiler boundary, texdefs lane). | **KEEP** — doc only. |
| 41-80 | Core globals | `perfFreq`, `dllModule`, `asipath`, feature gates, `configs[10]`, `config`, `numConfigs`. | **KEEP** — single definition site for `skygfx.h:47-80,930` externs; Config ABI. |
| 82-119 | Mode INI helpers | `modeIniAlias`, `modeOwnsPipes`, `s_activeModeIni/Seen`. | **EXTRACT-LATER** → `configRuntime` (S6); state accessors first. |
| 121-140 | Render globals | `grassPixelShader`, `simplePS`, shader-once ptrs, `gRenderingSpheremap`, `reflectionCamPos`, colour offsets, TempBuffer refs, `windPos`. | **KEEP** except 135-138 → `waterHook` (S2). |
| 142-151 | Exported ABI | `GetConfig`, `SkyGfx_FollowsMovedArrays`. | **KEEP** — `__declspec(dllexport)` ABI. |
| 153-173 | `getpath` | ASI-relative file probe; no call sites in the tree. | **KEEP** — dead but behaviour-preserving; deleting is out of scope. |

### 1b. Shared render helpers

| Lines | Section | Purpose | Verdict |
|---|---|---|---|
| 175-214 | `_rwD3D9RenderStateFlushCache`, `fixSAMP`, `D3D9Render` | SAMP fog sync + single draw primitive used by every dual leg. | **EXTRACT-NOW** → `defaultCallbacks` (S1). |
| 216-332 | Dual-pass two-layer helpers | `setAlphaTestTwoLayer`, `setZWriteTwoLayer`, `D3D9RenderDual`, `D3D9RenderBlack_DUAL`, `D3D9RenderDefault_DUAL`. | **EXTRACT-NOW** → `defaultCallbacks` (S1). `D3D9Render*` declarations already in `skygfx.h:1175-1176`. |
| 334-384 | Timecycle wrappers + `resetValues` | CTimeCycle ambient getters; copies Config into `CPostEffects`. | **EXTRACT-LATER** → `configRuntime` (S6). `resetValues` is declared in `skygfx.h:934`. |
| 386-526 | Clamp wave | `clampEnvSliderRanges`, `clampTonemapExposureRanges`, `clampSsaoPower`, `clampChromeParams`. | **EXTRACT-NOW** → `configPolicy` (S1). |
| 528-596 | `colorFilterEnable` latch | `s_cfEnableUser/Seen`, `cfEnableSetUserIntent`, `cfEnableHeal`. | **EXTRACT-NOW** → `configPolicy` (S1). |
| 598-719 | `derivePipelineFromPipes` | Per-frame pipe→pipeline derivation + PBR lock + heal call. | **EXTRACT-LATER** → `configRuntime` (S6); needs mode state accessors. Declared `extern` by `debugmenu_ui.cpp:34`. |
| 721-737 | Noise quality mapping | `g_noiseQuality`, `utilityNoiseResForQuality`. | **EXTRACT-LATER** → `configRuntime` (S6). |
| 739-776 | `refreshIni` | Re-clamp, regenerate noise, derive, push profiler/tree/grass, reset, refresh menu. | **EXTRACT-LATER** → `configRuntime` (S6). Declared `skygfx.h:935`. |
| 778-910 | Plant globals, `grassRenderCallback`, `atomicHasAlpha` | PS2 grass modulate/cull + dual legs; atomic alpha probe for ped/default dual gate. | **EXTRACT-NOW** → `defaultCallbacks` (S1). |

### 1c. State contracts

| Lines | Section | Purpose | Verdict |
|---|---|---|---|
| 912-1140 | Ped opaque contract | `PedOpaqueState` POD + capture/diverged/enforce/restore; banner already defines the future API at 945-951. | **EXTRACT-NOW** → `pedContract` (S1). |
| 1142-1667 | Shadow render-state contract | Capture/drift/force/restore, naked `shadowCallThis`, run wrapper, 4 game-entry hooks; banner defines API at 1189-1199. | **EXTRACT-NOW** → `shadowContract` (S2); install block 8214-8383 moves with it as `ShadowContract_InstallHooks()` called at the same line. Naked thunk is contained (no SEH, no shared state). |
| 1669-1707 | `vs_probe` | Vertex-shader/declaration cache-vs-device drift probe, log only. | **EXTRACT-NOW** → `defaultCallbacks` (S1). |
| 1709-1747 | `pedStage0Resync` | Stage-0 texture park-and-set from the RW cache truth. | **EXTRACT-NOW** → `defaultCallbacks` (S1). |

### 1d. Default callback and engine shims

| Lines | Section | Purpose | Verdict |
|---|---|---|---|
| 1749-2354 | `myDefaultCallback` | Default-pipe/ped render path: entry contract, ped opaque, ambient override, dual legs, [PedRS] monitor, `vs_probe`. | **EXTRACT-NOW** → `atomicDefaultCallback` (S2, after `pedContract`). Function-local statics move with the body; no file-scope state. |
| 2356-2391 | Tag-manager hooks + `clamp` | Re-pipeline tag atomics; float clamp used by the light hook. | **EXTRACT-LATER** → optional `engineShims` (S7). |
| 2393-2499 | Light hooks | `SetLightsWithTimeOfDayColour`, `setTextureAndColor`, ambient/dir colour globals. | **EXTRACT-LATER** → optional `engineShims` (S7). |
| 2501-2504 | `CMessages__AddMessageJumpQWithNumber` | cdecl thunk to 0x69E4E0. | **EXTRACT-LATER** → optional `engineShims` (S7). |
| 2506-2530 | `fixSeed` | Naked grass-placement branch; reads `Config+8`. | **KEEP** — naked asm + Config offset. Optional S7 only as a verbatim block with the static-asserts. |
| 2532-2550 | `saveIntensity` | Naked colour/intensity saver for `setTextureAndColor`. | **KEEP** — naked asm + `tmpintensity` global. |
| 2552-2572 | `rxD3D9DefaultRenderCallback_Hook` | Naked PS bind cache bypass. | **KEEP** — naked asm. |
| 2574-2608 | Frustum sphere/box guards | Naked NULL-camera guards. | **KEEP** — naked asm. |
| 2610-2641 | `PSSetCameraConstantD_Guard` | Naked NULL-camera PS constant guard. | **KEEP** — naked asm. |
| 2643-2663 | `rxD3D9DefaultRenderCallback_VertexShaderHook` | Naked VS bind cache bypass. | **KEEP** — naked asm. |
| 2665-2685 | `CPlantMgr_Initialise` | Loads grass PS, installs `grassRenderCallback` on 8 plant atomics. | **EXTRACT-LATER** → optional `engineShims` (S7). |
| 2687-2701 | `FX::GetFxQuality_*` | Config-driven shadow quality overrides. | **EXTRACT-LATER** → optional `engineShims` (S7). |
| 2703-2734 | RNG shim | `ps2rand/srand`, `floatbitpattern`, `gtasrand`, `mysrand`. | **EXTRACT-LATER** → optional `engineShims` (S7); `ps2randnormalize` declared `main_exports.h:37`. |
| 2736-2752 | Sun glare | `CVehicle__DoSunGlare` wrapper + naked `doglare`; reads `Config+12`. | **KEEP** — naked asm + Config offset. |

### 1e. Corona / sprite-state window

| Lines | Section | Purpose | Verdict |
|---|---|---|---|
| 2754-2822 | Corona subsystem banner/API | Scope contract + planned module API. | **EXTRACT-LATER** → `coronaWindow` (S4). |
| 2823-2841 | `PointLight` + `pointLights` | 48-byte CPointLights entry; base resolved from the game's `lea` immediate. | **EXTRACT-LATER** → `coronaWindow` (S4). |
| 2843-2908 | Window flags + pre-window statics | `g_inCoronasRender`, `g_coronaBodyDone`, `g_inCloudsRender`, `g_coronaPre*`. | **EXTRACT-LATER** → `coronaWindow` (S4); shared with effects/cloud via accessors. |
| 2910-2945 | `coronaRwBlendFromD3D` | D3DBLEND→rwBlend mapping. | **EXTRACT-LATER** → `coronaWindow` (S4). |
| 2947-3021 | `coronaDrawRawShield` | Per-draw park-and-set additive shield + spriteStateShield four-state normalisation. | **EXTRACT-LATER** → `coronaWindow` (S4). |
| 3023-3043 | Flush snapshot | `SpriteFlushState`, `s_spriteFlushState`, `s_spriteForceValid`. | **EXTRACT-LATER** → `coronaWindow` (S4); `s_spriteForceValid` is read by `effectsForceAlphaBlock`. |
| 3045-3107 | `spriteFlushShieldCapture` | Nine-state pre-force snapshot across L1/L2/L3. | **EXTRACT-LATER** → `coronaWindow` (S4). |
| 3109-3218 | `spriteFlushShieldClose` | Park-and-set replay; REPLACE→canonical substitution. | **EXTRACT-LATER** → `coronaWindow` (S4). |
| 3220-3258 | CSprite wrapper | `_asm` captures `currentLight/stkp`; point-light colour fix; shield call. | **EXTRACT-LATER** → `coronaWindow` (S4); exported via `main_exports.h:60`. |
| 3260-3430 | `SpriteFlushIndexedDraw_hook` | Shared sprite-flush force/close + window close + canonical resync. | **EXTRACT-LATER** → `coronaWindow` (S4). |
| 3432-3472 | `CPointLights__Render_shield_hook` | Re-arms the window for street-lamp cones/glow. | **EXTRACT-LATER** → `coronaWindow` (S4). |
| 3474-3556 | `CoronaWindow_Close` | Frame-boundary safety close; canonical blend, captured alpha pair. | **EXTRACT-LATER** → `coronaWindow` (S4); declaration already `skygfx.h:1520`. |

### 1f. Effects, frame and probes

| Lines | Section | Purpose | Verdict |
|---|---|---|---|
| 3558-3581 | Roadsigh hooks | Texture-name + atomic-geometry re-pipeline. | **EXTRACT-LATER** → optional `entityHooks` (S7). |
| 3583-3593 | Commented water-wave hook | Dead commented block. | **KEEP** — no code; preserve history. |
| 3595-3671 | Moon mask | SEH-fenced `0x70D000` call with ebp/esp frame surgery + naked wrapper. | **KEEP** — highest-risk SEH/asm in the file; extraction buys ~77 lines and risks the frame layout. |
| 3673-3710 | `CSkidmarks__Render` | Alpha-forced skidmark draw via `pipeForceAlphaBlock`. | **EXTRACT-LATER** → optional `entityHooks` (S7). |
| 3712-3723 | `RenderScene_A` + reflection decls | Indirection var for the intercepted RenderScene call; `RenderReflectionMap_leeds`/`RenderReflectionScene` prototypes. The actual env-map hooks are `envmaphooks()` (envmap.cpp:1238), called from `installDeferredHooks` (main.cpp:5538-5540) and staying there. | **EXTRACT-LATER** → `sceneHooks` (S3) for the two prototypes; `envmaphooks()` stays in `installDeferredHooks`. |
| 3725-3766 | Camera-raster cache | `s_camRas*`, `InvalidateCameraRasterCache` (called by `postfx.cpp:2633/2661`). | **EXTRACT-LATER** → `sceneHooks` (S3). |
| 3768-3946 | `RenderScene_before` | Frame start: baseline, corona close, depth hook, SMAA init, Forward+, camera-raster repair, ragdoll pose, wind. | **EXTRACT-LATER** → `sceneHooks` (S3). |
| 3948 | `cloudAnimTimer` | Unused global (no references). | **KEEP** — dead; no value in moving. |
| 3950-4121 | `effectsForceAlphaBlock` | Producer gate, three-layer drift evidence, stale Im2D sweep, additive leak substitution, `pipeForceAlphaBlock`. | **EXTRACT-LATER** → `effectsHygiene` (S5); reads corona/cloud/sprite flags via accessors. |
| 4123-4262 | Corona→Forward+ feed | `CoronaFeedEntry` static_assert 0x3C; per-frame registry + corona walk. | **EXTRACT-NOW** → `coronaFeed` (S1). |
| 4264-4298 | `RenderEffects_hook` | Feed + enter/exit alpha sweeps + window close + baseline. | **EXTRACT-LATER** → `effectsHygiene` (S5). |
| 4300-4353 | `CPostEffects__Render_hook` | Postfx exit sweep + SEH-guarded after-repair receipt. | **EXTRACT-LATER** → `effectsHygiene` (S5). |
| 4355-4398 | Effects coverage audit | Call-site interleave analysis (comment only). | Moves with `effectsHygiene` (S5). |
| 4400-4418 | Corona reflection hooks | Feed + alpha sweep before wet-ground/sun reflection draws. | **EXTRACT-LATER** → `effectsHygiene` (S5). |
| 4420-4757 | `CCoronas__Render_blendprobe_hook` | Entry blend/ZTEST shield + pre-window capture + proxied body + exit restore + probe; two SEH fences. | **EXTRACT-LATER** → `coronaWindow` (S4). |
| 4759-5039 | FBWP hardware watchpoint | DR0 write watchpoint on `Scene.camera->frameBuffer`, VEH attribution, fire cap. | **EXTRACT-LATER** → `fbWatchpoint` (S3); SEH-only, debug-gated. |
| 5041-5410 | `CClouds__Render_hygiene` | Cloud state save/force/restore, PS/VS neutralise, stage-0 raster restore, SEH fence. | **EXTRACT-LATER** → `cloudHygiene` (S5); sets the cloud producer flag via accessor. |
| 5412-5488 | `RenderScene_after` / `RenderScene_hook` | Post-scene ragdoll/env-map, Ctrl+4 menu, weather, texdefs attach, before/render/after chain. | **EXTRACT-LATER** → `sceneHooks` (S3). |

### 1g. Startup / install and config

| Lines | Section | Purpose | Verdict |
|---|---|---|---|
| 5490-5509 | `PipelinePluginAttach` + `myPluginAttach` | Calls the game plugin attach first, then `TexDefsPluginAttach`. | **KEEP** — startup ordering contract. |
| 5511-5548 | `installDeferredHooks` | Scene hooks (or UG events), envmaphooks, mirrorPass init; idempotent. | **KEEP** — MoonLoader/UG branch + install ordering. |
| 5550-5554 | `installLCMV2Hooks` | Empty compatibility stub. | **KEEP** — no-op. |
| 5556-5600 | `InitialiseGame_hook` | First-init lane (neo, texdefs), SEH around the original. | **KEEP** — hook-order + crash-pass-through state. |
| 5602-5612 | RwIm3D wrappers | Thin cdecl forwarders. | **KEEP** — RW ABI shims. |
| 5614-5772 | Water draw hook | Gate, raw state park, water pipe draw, cache/device unpark. | **EXTRACT-NOW** → `waterHook` (S2). |
| 5774-5808 | Commented water hooks | Dead code + naked `selectVM` comment. | **KEEP** — comments only. |
| 5811-5892 | INI/file helpers | `readhex` ×2, `FileExists`, `StrAssoc`, `findInis`, `readint/readfloat`. | **EXTRACT-LATER** → `configIo` (S6). |
| 5894-7190 | `readIniFile` | Full parse + default emission + missing-key upgrade + `[Modes]` apply. | **EXTRACT-LATER** → `configIo` (S6); single 1.3k-line body, verbatim move. |
| 7191-7253 | `readIni`/`readInis`/`setConfig`/`reloadAllInis` | Path selection, multi-slot read, live config switch. | **EXTRACT-LATER** → `configIo` (S6). |
| 7255-7632 | `saveConfigTo` | 366-key merge-write; clamps/heal before write; mode alias. | **EXTRACT-LATER** → `configIo` (S6). |
| 7634-7651 | `saveConfig` | `.asi`→`.ini` path rewrite + `saveConfigTo`. | **EXTRACT-LATER** → `configIo` (S6). |
| 7653-7661 | `loadConfigFile` | Read into live config + `refreshIni`. | **EXTRACT-LATER** → `configIo` (S6). |
| 7663-7671 | `afterStreamIni` | Naked trampoline calling `readInis`. | **EXTRACT-LATER** → `configIo` (S6); moves with its only caller. |
| 7673-7746 | `MENUSETTINGS` macro | Duplicate of `debugmenu_ui.cpp:90`; unused in `main.cpp`. | **KEEP** — dead macro; moving buys nothing. |
| 7748-7760 | Menu noise accessors | `menuGet/SetNoiseQuality` over TU-static `g_noiseQuality`. | **EXTRACT-LATER** → `configRuntime` (S6). |
| 7762-7788 | `DrawFinalEffects_pf` | Frame-end profiler snap, corona close, baseline, `vs_probe`, RSMon. | **KEEP** — 27-line hook target at the frame boundary; extraction adds a TU without reducing coupling. |
| 7790-7808 | `ModeListener_OnApply` | Mode apply callback: reset values, tags mirror, mode latch. | **EXTRACT-LATER** → `configRuntime` (S6). |

### 1h. Entry / install

| Lines | Section | Purpose | Verdict |
|---|---|---|---|
| 7810-8071 | `InjectDelayedPatches` | Mode manager init, INI read, module detection, all delayed hooks/patches in fixed order (including tree-wind at 8052-8054, water at 8042-8046, FBWP at 8061-8062). | **KEEP** — hook order + startup gating. |
| 8073-8094 | `verifyBytes` | PATCH_JUMP prologue check with one-shot receipt. | **KEEP** — used only by `DllMain`. |
| 8096-8106 | Norm-map plugin externs | RpNormMap API block + note. | **KEEP** — startup note. |
| 8108-8701 | `DllMain` | Process attach hook installs; SEH-guarded detach cleanup. | **KEEP** — process entry; never extract. |

---

## 2. EXTRACT-NOW module specifications

### 2.1 `src/core/configPolicy.{cpp,h}` — stage 1

- **Moves:** `main.cpp:386-596` (four clamps + cfEnable latch). Prologue banner:
  `Config validation policy (was src/core/main.cpp:386-596)`.
- **Exports (original names, no call-site rename):**
  `clampEnvSliderRanges`, `clampTonemapExposureRanges`, `clampSsaoPower`,
  `clampChromeParams`, `cfEnableSetUserIntent`, `cfEnableHeal`.
- **Header includes:** `skygfx.h` only. TU adds `debug_handl.h` for `dbglog*`.
- **Cross-TU repoints:** `main.cpp:742-745` (`refreshIni`), `main.cpp:7263-7267`
  (`saveConfigTo`), `main.cpp:6282/6292/6306/6505/6590/6603` (`readIniFile`),
  `main.cpp:647` (`derivePipelineFromPipes` → `cfEnableHeal`). No other TU references these.
- **State:** `s_cfEnableUser/Seen` move with the latch; `configs[]` remains extern
  (`skygfx.h:930`). Zero-init arrays only; no dynamic initialiser.

### 2.2 `src/render/pedContract.{cpp,h}` — stage 1

- **Moves:** `main.cpp:912-1140` (banner + `PedOpaqueState` + 4 helpers). The documented
  API at `main.cpp:945-951` becomes the real header.
- **Exports:** `PedOpaque_Capture`, `PedOpaque_Diverged`, `PedOpaque_Enforce`,
  `PedOpaque_Restore`; `PedOpaqueState` moves verbatim (POD, scalar members only).
- **Cross-TU repoints:** `main.cpp:1880` (`PedOpaqueState savedOpaque = {}`),
  `main.cpp:1893` (`pedOpaqueCapture`), `main.cpp:1994` (`pedOpaqueDiverged`),
  `main.cpp:2006` (`pedOpaqueEnforce`), `main.cpp:2271` (`pedOpaqueRestore`).
  After stage 2 those five lines live in `atomicDefaultCallback.cpp`.
- **Not moved:** `myDefaultCallback`'s [PedRS] monitor block remains inline (see the banner's
  "MOVES TOGETHER" note at `main.cpp:953-962`); carving `PedRS_Monitor()` is an optional later follow-up, not part of this plan.

### 2.3 `src/render/coronaFeed.{cpp,h}` — stage 1

- **Moves:** `main.cpp:4123-4262` (layout comment, `CoronaFeedEntry`, `static_assert`,
  `coronaFeedForwardPlus` + its `s_fedFrame` static).
- **Exports:** `CoronaFeed_ForwardPlus()`.
- **Cross-TU repoints:** `main.cpp:4279`, `main.cpp:4405`, `main.cpp:4415`
  (after stage 5: `effectsHygiene.cpp`). `ForwardPlus_AddLight` /
  `ForwardPlus_FeedGameLights` are already declared in `forwardplus.h:46,64`; delete the
  local re-declarations at `main.cpp:4185-4186`.

### 2.4 `src/render/defaultCallbacks.{cpp,h}` — stage 1

- **Moves:** `main.cpp:175-332` (`fixSAMP`, `D3D9Render`, dual helpers),
  `main.cpp:778-910` (plant tables, `grassRenderCallback`, `pipeForceAlphaBlock` forward
  declaration, `atomicHasAlpha`), `main.cpp:1669-1747` (`vs_probe`, `pedStage0Resync`).
- **Exports:** `Grass_RenderCallback`, `AtomicHasAlpha`, `Vs_Probe`,
  `Ped_Stage0Resync`. `D3D9Render`, `D3D9RenderDual`, `D3D9RenderBlack_DUAL`,
  `D3D9RenderDefault_DUAL` keep their existing declarations (`skygfx.h:1175-1176`,
  `main_exports.h:55-56`).
- **Cross-TU repoints:** `buildingPipe.cpp:333/1446/1536/2083/2232/2270/2998` and
  `vehiclePipe.cpp:722/1106/1269/1702/1825` (no edit — same declarations);
  `main.cpp:2011` (`pedStage0Resync`), `main.cpp:2155/2217/2218` (`atomicHasAlpha`),
  `main.cpp:2352/7785` (`vs_probe`), `main.cpp:2680/2682` (`grassRenderCallback`),
  `main.cpp:8204-8205` (DUAL installs, declarations already in `main_exports.h`).
- **State:** none file-scope except `plantTab0/1`; function-local statics in `vs_probe`
  and `pedStage0Resync` move with their functions.

### 2.5 `src/render/shadowContract.{cpp,h}` — stage 2

- **Moves:** `main.cpp:1142-1667` + the install block `main.cpp:8214-8383`.
- **Exports:** `ShadowContract_InstallHooks()` only. `shadowContractRun`, the four hook
  bodies and the four `_orig` pointers become static in the new TU.
- **Install-order contract:** the call must replace `main.cpp:8214-8383` *in place*:
  after the skidmarks intercept (`main.cpp:8212`) and before the effects block
  (`main.cpp:8393`). The function internally performs the same order:
  `0x53E0BE` call-site verify → intercept; `0x708300` entry verify → trampoline → hook;
  `0x706AB0` entry verify → trampoline → hook.
- **Cross-TU repoints:** `main.cpp:1346-1350/1465-1469` use `pipeBlendToD3D` and
  `pipeAlphaFuncToD3D`; `pipeBlendToD3D` is declared `skygfx.h:1481`, but
  `pipeAlphaFuncToD3D` is **not** in `skygfx.h` today (local externs at `main.cpp:226`,
  `buildingPipe.cpp:47`, `vehiclePipe.cpp:111`). Add one declaration next to
  `skygfx.h:1481` in this stage; `main.cpp:226` disappears with S1, and the
  `buildingPipe.cpp:47` / `vehiclePipe.cpp:111` copies may be deleted in S2 or left as harmless duplicates.
- **Naked:** `shadowCallThis` (`main.cpp:1537-1546`) is fully contained; no SEH, no
  C++ objects, no shared state. It is safe in its own TU.

### 2.6 `src/render/waterHook.{cpp,h}` — stage 2

- **Moves:** TempBuffer refs `main.cpp:135-138` + hook `main.cpp:5614-5772`.
- **Exports:** `CWaterLevel__RenderAndEmptyRenderBuffer_hook` (already
  `main_exports.h:70`), `CWaterLevel__RenderAndEmptyRenderBuffer` pointer (already
  `main_exports.h:38`), `WaterHook_InstallHooks()`.
- **Cross-TU repoints:** `main.cpp:8043-8046` → `WaterHook_InstallHooks();`.
  The module uses `waterPipe.h:3-17` (`waterPipe_init`, `waterPipe_pixelShader`,
  `waterPipe_setRenderState/restoreRenderState`, `g_waterParallaxActive`) and `Water_VS`
  (`skygfx.h:1215`). `waterPipe.cpp:221-223` already defines its own private aliases to
  the same addresses; no duplicate definition is introduced because the four `main.cpp`
  globals move into this TU.
- **Install-order contract:** the call replaces lines 8043-8046 exactly (after `waterPipe_init()`
  at 8042, before the tree-wind block at 8048).

### 2.7 `src/render/atomicDefaultCallback.{cpp,h}` — stage 2

- **Moves:** `main.cpp:1749-2354` (`myDefaultCallback` comment + body).
- **Exports:** `myDefaultCallback` (already `main_exports.h:49`).
- **Cross-TU repoints:** `main.cpp:8174` install stays byte-identical. The body's five
  ped-contract calls are repointed to `pedContract.h`; `AtomicHasAlpha`, `Vs_Probe`,
  `Ped_Stage0Resync` come from `defaultCallbacks.h`. All pipeline APIs
  (`pipeEnterCullMode`, `pipeExitCullMode`, `pipeForceCullMode`, `pipeEnterAlphaMode`,
  `pipeExitAlphaMode`, `pipeAlphaFuncToD3D`, `pipeBlendToD3D`, `pipeForceAlphaBlock`)
  are declared in `skygfx.h` except `pipeForceAlphaBlock` and `pipeAlphaFuncToD3D`
  (add both in stage 2; `pipeForceAlphaBlock` is currently forward-declared twice:
  `main.cpp:857`, `main.cpp:3672`).
- **State:** every `static` in this function (`defpipeLogged`, `pedAlphaTestLogged`,
  `pedCullLoggedMode`, `defcbShaderLogged`, `defcbShaderProbeLogged`, `pedAmbCmpLogged`,
  `pedDualLegacyLogged`, `pedDualTexAlphaLogged`, `pedRsFirst`) is function-local; it
  moves with the body and stays process-global exactly as today.

---

## 3. EXTRACT-LATER module specifications

### 3.1 `src/core/fbWatchpoint.{cpp,h}` — stage 3
Moves `main.cpp:4759-5039`. Exports `FbWp_ArmOnce()`; all DR/VEH statics become private.
Repoints: `main.cpp:3816-3817`, `main.cpp:5136-5137`, `main.cpp:8061-8062`.
SEH only; every `__try` local is POD. No shared state. Gate stays `config->postfxDumpDebug`.

### 3.2 `src/core/sceneHooks.{cpp,h}` — stage 3
Moves `main.cpp:3712-3946` + `5412-5488` (not `cloudAnimTimer`, `main.cpp:3948`).
Exports `RenderScene_A`, `RenderScene_before`, `RenderScene_after`, `RenderScene_hook`,
`InvalidateCameraRasterCache`. Repoints: `main.cpp:5531-5535` (installDeferredHooks),
`postfx.cpp:2633/2661` (extern already resolves by symbol name). Uses `fbWatchpoint.h`
(3.1), `CoronaWindow_Close` (`skygfx.h:1520`), and the `postfx.h`/`forwardplus.h`/
`depthhook.h`/`weather.h`/`texdefs.h`/`chars.h`/`neo.h` APIs already used today.

### 3.3 `src/render/coronaWindow.{cpp,h}` — stage 4
Moves `main.cpp:2754-3556` + `4420-4757` + install `8405-8549`.
Exports: `CoronaWindow_Close` (already `skygfx.h:1520`), `CoronaWindow_ProducerActive()`,
`CoronaWindow_SpriteForceActive()`, `CoronaWindow_SetCloudProducer(bool)`,
`CoronaWindow_InstallHooks()`, and the `CSprite__RenderBufferedOneXLUSprite_Rotate_Aspect`
wrapper (already `main_exports.h:60`).
Repoints: `main.cpp:8405-8549` → one call in place; `depthhook.cpp:407-408` unchanged;
`effectsHygiene`/`cloudHygiene` use the accessors instead of the file-scope flags.
The four `spriteStateShield`/`coronaZtest` reads stay verbatim.

### 3.4 `src/render/effectsHygiene.{cpp,h}` — stage 5
Moves `main.cpp:3950-4121` + `main.cpp:4264-4418` (comment banner + `effectsForceAlphaBlock` +
`RenderEffects`/`CPostEffects`/reflection hooks + audit comment; the feed at 4123-4262 moved in S1). Exports
`Effects_ForceAlphaBlock(const char*)`, `EffectsHygiene_InstallReflectionHooks()`,
`EffectsHygiene_InstallPostFxHook()`. Repoints: `main.cpp:8393-8396` and `main.cpp:8552`
(two calls, each in place); `cloudHygiene` calls `Effects_ForceAlphaBlock`.
Uses `coronaWindow` accessors and the `overrideIm2dPixelShader`/`overrideColorMod`/
`overrideAlphaMod` externs currently at `main.cpp:3988-3990`.

### 3.5 `src/render/cloudHygiene.{cpp,h}` — stage 5
Moves `main.cpp:5041-5410`. Exports `CClouds__Render_hygiene`, `CClouds__Render_orig`,
`CloudHygiene_InstallHooks()`. `cloudRenderFix` can stay defined at `main.cpp:5080`
(the file is read by `debugmenu_ui.cpp:24`) and be externed by the module; moving its
definition is optional and must not precede the debugmenu extern. Repoints:
`main.cpp:7979-7980` → `CloudHygiene_InstallHooks();` in place.

### 3.6 `src/core/configRuntime.{cpp,h}` — stage 6
Moves `main.cpp:82-119` (mode helpers + `s_activeModeIni/Seen`), `334-384`
(`resetValues`), `598-719` (`derivePipelineFromPipes`), `721-776` (noise quality +
`refreshIni`), `7748-7760` (noise accessors), `7790-7808` (`ModeListener_OnApply`).
Exports: `refreshIni`, `resetValues`, `derivePipelineFromPipes`, `ModeIniAlias`,
`ModeIniActive`, `ModeIniSeen`, `ModeIniRecord`, `ModeListener_OnApply`,
`menuGetNoiseQuality`, `menuSetNoiseQuality`. Repoints: `main.cpp:7821`
(`ModeManager_RegisterListener(ModeListener_OnApply)`), `main.cpp:7834-7835`
(receipt via accessors), `main.cpp:7801` (moves with listener), `debugmenu_ui.cpp:34/4381`
unchanged symbol, `modeManager.cpp:1214/1224` (`resetValues` via `skygfx.h:934`).

### 3.7 `src/core/configIo.{cpp,h}` — stage 6
Moves `main.cpp:5811-5892`, `5894-7190`, `7191-7253`, `7255-7632`, `7634-7651`,
`7653-7661`, `7663-7671`. Exports the functions already declared at `skygfx.h:931-940`:
`findInis`, `readIni`, `readInis`, `setConfig`, `reloadAllInis`, `saveConfig`,
`saveConfigTo`, `loadConfigFile`; `readIniFile` stays static inside the new TU.
`explicitBuildingPipe_tmp` (definition `main.cpp:5894`, extern `main_exports.h:74`) and
`g_qualityPreset` (`main.cpp:5899`) move with it. Repoints: `main.cpp:8173`
(`afterStreamIni` install), `main.cpp:7935` (`explicitBuildingPipe = explicitBuildingPipe_tmp`),
`debugmenu_ui.cpp:639-640` (noise accessors now resolve to `configRuntime`).
`MENUSETTINGS` (`7673-7746`) stays in `main.cpp` (unused duplicate).

---

## 4. Blocker list — what dictates the ordering

### 4.1 SEH sites (28 `__try` tokens / 21 live fences, 0 `__finally`)

| Lines | Function | Fence count | Constraint |
|---|---|---|---|
| 3629-3656 | `renderMoonMaskBody` | 3 | Inline asm + ebp/esp frame surgery; **KEEP**. |
| 4332-4352 | `CPostEffects__Render_hook` | 1 | Read-only diagnostic; POD locals only. |
| 4496-4757 | `CCoronas__Render_blendprobe_hook` | 2 | Two independent fences: body+probe, then exit restore. Restore must never be merged into the first fence. |
| 4850-5038 | `fbWp_TargetAddr` / `fbWp_veh` / `fbWp_ArmOnce` | 7 | POD locals only; debug-gated. |
| 5309-5318 | `CClouds__Render_hygiene` | 1 | POD locals only; restore must run after the fence. |
| 5577-5598 | `InitialiseGame_hook` | 1 | POD + `EXCEPTION_POINTERS`; **KEEP**. |
| 8651-8695 | `DllMain` detach | 6 | **KEEP**. |

C2702 is impossible in this file today (0 `__finally`). C2712 can only appear if a moved
body gains a class-with-destructor local inside an existing `__try`; verbatim moves prevent
that. Do not introduce `std::string`/`linb::ini` into any SEH-bearing TU.

### 4.2 Naked functions and raw asm

| Lines | Symbol | Move policy |
|---|---|---|
| 1537-1546 | `shadowCallThis` | Moves with `shadowContract` (S2). Contained; no SEH. |
| 2506-2530 | `fixSeed` | **KEEP** — reads `Config+8`. |
| 2532-2550 | `saveIntensity` | **KEEP** — reads/writes `tmpintensity` + registers. |
| 2552-2572 | `rxD3D9DefaultRenderCallback_Hook` | **KEEP**. |
| 2574-2608 | `FrustumTestSphere_Guard`, `FrustumTestBox_Guard` | **KEEP**. |
| 2610-2641 | `PSSetCameraConstantD_Guard` | **KEEP**. |
| 2643-2663 | `rxD3D9DefaultRenderCallback_VertexShaderHook` | **KEEP**. |
| 2717-2725 | `floatbitpattern` | Optional S7 only. |
| 2739-2752 | `doglare` | **KEEP** — reads `Config+12`. |
| 3229-3230 | inline `_asm` in CSprite wrapper | Moves with `coronaWindow` (S4). |
| 3634-3651 | inline `_asm` in moon body | **KEEP**. |
| 3658-3665 | `renderMoonMask` | **KEEP**. |
| 7664-7671 | `afterStreamIni` | Moves with `configIo` (S6). |

Config-offset asm is pinned by `skygfx.h:925-927`:
`static_assert(offsetof(Config, version) == 0)`, `fixGrassPlacement == 8`,
`doglare == 12`. Any extraction that touches these must run after a clean `cl /Zs` of the
header and must not change `VERSION` (`skygfx.h:163`).

### 4.3 Shared file-scope mutable state

| State | Lines | Shared by | First safe owner |
|---|---|---|---|
| `s_activeModeIni/Seen` | 85-86 | `readIniFile` (5949), `saveConfigTo` (7278), `ModeListener` (7803), `modeOwnsPipes` (112) | `configRuntime` with `ModeIni*` accessors. |
| `s_cfEnableUser/Seen` | 545-546 | `cfEnableSetUserIntent`, `cfEnableHeal`, `derivePipelineFromPipes` | `configPolicy` (S1), no accessor needed. |
| `g_noiseQuality` | 728 | `refreshIni` (753), `readIniFile` (5958), `saveConfigTo` (7626), menu accessors (7759) | `configRuntime`; `configIo` uses `menuGet/SetNoiseQuality`. |
| `g_inCoronasRender`, `g_coronaBodyDone`, `g_inCloudsRender` | 2861-2875 | corona hooks, `effectsForceAlphaBlock` (4005), `CClouds` hygiene (5164/5166) | `coronaWindow` + `CoronaWindow_ProducerActive/SpriteForceActive/SetCloudProducer`. |
| `g_coronaPre*` | 2889-2908 | `CCoronas` hook + `CoronaWindow_Close` | `coronaWindow` only. |
| `s_spriteFlushState`, `s_spriteForceValid` | 3042-3043 | flush hook, `effectsForceAlphaBlock` (4005) | `coronaWindow`; export `CoronaWindow_SpriteForceActive()`. |
| `pointLights`, `currentLight`, `stkp` | 2841, 3223-3224 | CSprite wrapper + flush/pointlight hooks | `coronaWindow` only. |
| `s_camRas*` | 3748-3752 | `RenderScene_before`, `InvalidateCameraRasterCache`, `postfx.cpp:2661` | `sceneHooks` (S3) as one unit. |
| `s_fbWp*`, `s_fbWpEips` | 4820-4843 | fbWp VEH + `FbWp_ArmOnce` | `fbWatchpoint` only. |
| `RenderScene_A` | 3712 | `installDeferredHooks` (5531) | `sceneHooks`; export the var. |
| `cloudRenderFix` | 5080 | `readIniFile` (6379), `debugmenu_ui.cpp:24/1069` | Optional: keep definition in main, extern in `cloudHygiene`. |

### 4.4 C2712 / POD constraints
The only functions with `__try` in a moved block are `CPostEffects__Render_hook`,
`CCoronas__Render_blendprobe_hook`, `CClouds__Render_hygiene`, `fbWp_*`; all use POD
locals today. Do not add RAII (`std::string`, `linb::ini`, `std::vector`) to those TUs.
`CCoronas__Render_blendprobe_hook` additionally depends on its two-fence structure: the
exit restore is intentionally in a second `__try` so a body/trampoline fault cannot skip it.

---

## 5. Ordered stages

**Precondition.** Gate Stage 1 on the in-flight render-roadmap lane landing
(`postfx_support.cpp` merge + `blendToD3D`/`pipeBlendHasRwTwin` dedupe from
`docs/plans/2026-10-27-render-refactor-roadmap.md`). That lane writes the same
`build/skygfx.vcxproj`, `premake5.lua`, `src/core/main.cpp` and `src/skygfx.h`; if it has not
landed, re-anchor every line number by symbol before editing (the file is already dirty).

Each stage is independently compilable and linkable. Do not start the next stage until the
stage's `cl /Zs` and log gate pass. Back up the stage's write set to
`_backup/main_split_s<N>_2026-10-27/` **before** editing; never revert another lane's files.

**`cl /Zs` template** (run from a VS2022 x86 developer prompt; same include set as the
vcxproj `AdditionalIncludeDirectories` at `build/skygfx.vcxproj:67`):

```
cl /nologo /Zs /EHsc /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_WARNINGS /DNDEBUG
   /DWIN32 /D_WINDOWS /D_M_IX86 /Iexternal /Iexternal/injector/include /Iexternal/d3d9
   /Iresources /Ishaders /Isrc /Isrc/rw /Isrc/render /Isrc/core /I"E:\SDKs\imgui-master"
   /I"E:\SDKs\plugin-sdk-master\plugin_sa"
   /I"E:\SDKs\normalmap_byDK_1.01\normalmap_byDK_1.01\sources"
   <files touched by the stage>
```

**Basename gate** after every stage (must print nothing):

```
grep -o 'ClCompile Include="[^"]*"' build/skygfx.vcxproj \
  | sed 's/.*\\//;s/"//' | sort | uniq -d
```

**Build-file delta template.** For each new TU `<name>.cpp` / header `<name>.h`:

- `build/skygfx.vcxproj`: add `<ClCompile Include="..\src\<dir>\<name>.cpp" />` in the
  ItemGroup near `main.cpp` (`vcxproj:161-200`), and
  `<ClInclude Include="..\src\<dir>\<name>.h" />` near `vcxproj:155-158`. Comment each
  row with the source line range and "unique basename — no LNK4042 risk".
- `build/skygfx.vcxproj.filters`: add matching `<ClCompile>`/`<ClInclude>` blocks with
  `<Filter>src</Filter>` (pattern at `filters:110-113`); the file's existing paths are
  stale in places, so match the new rows to the live vcxproj path, not the stale rows.
- `premake5.lua`: add the TU to the explicit tripwire list at `premake5.lua:204-225` and
  the header to the exact header list at `premake5.lua:228-254`. The `src/core/*.cpp`,
  `src/render/*.cpp` globs (`premake5.lua:180-197`) pick the files up automatically; the
  explicit rows are the tripwire. Update the "glob is complete today" comment at
  `premake5.lua:176-179` in the same commit.
- `tools/fix_build.py`: add `_is_active_cpp('<dir>\\<name>.cpp')` checks in
  `check_pbr_build_integrity()` after the existing merged-TU checks
  (`tools/fix_build.py:204-245`). Add new headers to the required-file loop at
  `tools/fix_build.py:261-268` if they are load-bearing for compilation.
- All new files: CRLF, deduped include union, section banner, verbatim body.

### Stage 1 — pure leaves (4 TUs, main 8,701 → ≈7,751)

| TU | Source lines | Notes |
|---|---|---|
| `src/core/configPolicy.cpp/.h` | 386-596 | Clamps + cfEnable latch; original names. |
| `src/render/pedContract.cpp/.h` | 912-1140 | POD + 4 exports. |
| `src/render/coronaFeed.cpp/.h` | 4123-4262 | 1 export. |
| `src/render/defaultCallbacks.cpp/.h` | 175-332, 778-910, 1669-1747 | 4 exports + 4 existing `D3D9Render*`. |

Build deltas: +4 `ClCompile`, +4 `ClInclude`, +4 premake TU rows, +4 premake header rows,
+4 fix_build checks. Repoints are listed in §2.1-2.4. No hook site moves.
Verification: `cl /Zs` on `main.cpp` + the 4 TUs; basename gate; `wc -l` ≈7,751.
Runtime gate (after a normal `python tools/fast_build.py` launch): `[PedOpaque] pre-entry`
once; `[PointLights] base=` once; `[CoronaFeed]` once; `[probe] defcb_shader path taken`
once. No new/missing tags.

### Stage 2 — contracts + default callback (3 TUs, → ≈6,286)

| TU | Source lines | Notes |
|---|---|---|
| `src/render/shadowContract.cpp/.h` | 1142-1667 + 8214-8383 | `ShadowContract_InstallHooks()` called at 8214. |
| `src/render/waterHook.cpp/.h` | 135-138 + 5614-5772 | `WaterHook_InstallHooks()` called at 8043. |
| `src/render/atomicDefaultCallback.cpp/.h` | 1749-2354 | `myDefaultCallback` verbatim; 5 ped-call renames. |

Prerequisite header edits: add `pipeAlphaFuncToD3D` and `pipeForceAlphaBlock`
declarations to `skygfx.h` near `skygfx.h:1481`; `main.cpp:226` disappears in S1,
and `main.cpp:857` / `main.cpp:3672` are deleted when their last users move.
Build deltas: +3 `ClCompile`, +3 `ClInclude`, +3 premake TU rows, +3 header rows,
+3 fix_build checks.
Verification: `cl /Zs` on the 3 TUs + `main.cpp`; `[shadow_install]` order
(0x53E0BE → 0x708300 entry → 0x706AB0 entry) and all `OK`; `[water] water_state pre-draw`
once; `[PedOpaque] pre-entry` still once; `[ShadowHeal]` once.

### Stage 3 — probes + frame orchestrator (2 TUs, → ≈5,693)

| TU | Source lines | Notes |
|---|---|---|
| `src/core/fbWatchpoint.cpp/.h` | 4759-5039 | SEH-only, debug-gated. |
| `src/core/sceneHooks.cpp/.h` | 3712-3946, 5412-5488 | Camera-raster cache moves here. |

Build deltas: +2/+2/+2/+2/+2 (same template). Repoints: `main.cpp:3816-3817`,
`5136-5137`, `8061-8062` → `FbWp_ArmOnce()`; `main.cpp:5531-5535` → sceneHooks symbols.
Verification: `cl /Zs` on the 2 TUs + `main.cpp`; `[FBWP] frameBuffer watchpoint probe
active` only with `postfxDumpDebug=1`; `[probe] baseline scenestart called` once;
`[probe] baseline effects_exit_renderfx called` once; camera-raster invalidation line on
a resolution change (`sky_raster_inv`).

### Stage 4 — corona window (1 TU, → ≈4,407)

| TU | Source lines | Notes |
|---|---|---|
| `src/render/coronaWindow.cpp/.h` | 2754-3556 + 4420-4757 + 8405-8549 | One install call replaces the contiguous block. |

Exports/accessors: §3.3. Build deltas: +1 each. Verification: `cl /Zs` on the TU +
`main.cpp`; `[corona_entry_install] ... OK`; `[corona_install]` both sites OK;
`[pointlights_install]` both sites OK (0x53E21D and 0x53DE6B); `[CoronaShield]`,
`[CoronaExit]`, `[CoronaResync] window closed`, `[FlushShield] restore` each once;
`[PointLights] base=` still once.

### Stage 5 — effects + clouds (2 TUs, → ≈3,703)

| TU | Source lines | Notes |
|---|---|---|
| `src/render/effectsHygiene.cpp/.h` | 3950-4121 + 4264-4418 | Two install functions for the two non-contiguous site groups. |
| `src/render/cloudHygiene.cpp/.h` | 5041-5410 | `CloudHygiene_InstallHooks()` at 7979. |

Build deltas: +2 each. Verification: `cl /Zs` on the 2 TUs + `main.cpp`;
`[FxAlpha] producer owns additive state` / `[FxAlpha] ... layer drift` once;
`[CloudShield] flush batch=clouds` and `batch=sprites` once; `[CloudShield] restored
stage-0 raster=` once; `[skydome_install] ... OK` unchanged; `[corona_resync]` still once.

### Stage 6 — config runtime + INI I/O (2 TUs, → ≈1,558)

| TU | Source lines | Notes |
|---|---|---|
| `src/core/configRuntime.cpp/.h` | 82-119, 334-384, 598-719, 721-776, 7759-7760, 7790-7808 | Mode accessors + derive + refresh + noise + listener. |
| `src/core/configIo.cpp/.h` | 5811-5892, 5894-7190, 7191-7253, 7255-7632, 7634-7651, 7653-7661, 7663-7671 | readIniFile/saveConfigTo verbatim; `readIniFile` stays static. |

Build deltas: +2 each. Repoints: §3.6-3.7. Verification: `cl /Zs` on the 2 TUs +
`main.cpp`; full INI round-trip diff (load existing `skygfx.ini`, save, diff — only
formatting/key-order changes allowed, no value changes); `[Mode] active=` once;
`[INIT] ambientMult=` once; `[cfg_save] ok` and `[cfg_load] ok`;
`menuGet/SetNoiseQuality` still drive the Utility tab.

### Stage 7 (optional, risky) — engine shims / entity hooks (2 TUs, → ≈1,099)

Only if the team wants `main.cpp` below 1,000 lines. Moves `2356-2752` to
`src/core/engineShims.cpp` (tag manager, lights, messages, naked guards, plant init, FX
quality, RNG, glare) and `3558-3581` + `3673-3710` to `src/render/entityHooks.cpp`.
**Do not move the moon mask** (`3595-3671`). This stage is the only one allowed to move
naked functions; it must move them as one verbatim block and re-run the
`offsetof(Config, ...)` static-asserts. If any doubt, stop after Stage 6.

---

## 6. Target metrics

| Milestone | `main.cpp` lines (estimate) | New TUs | New headers |
|---|---|---|---|
| Today | 8,701 | — | — |
| After Stage 1 | ≈7,751 | 4 | 4 |
| After Stage 2 | ≈6,286 | 7 | 7 |
| After Stage 3 | ≈5,693 | 9 | 9 |
| After Stage 4 | ≈4,407 | 10 | 10 |
| After Stage 5 | ≈3,703 | 12 | 12 |
| After Stage 6 (planned end) | **≈1,558** (target ≤1,600) | **14** | **14** |
| After optional Stage 7 | ≈1,099 | 16 | 16 |

Expected `src/core/main.cpp` end state: includes + global definitions + ABI exports +
`getpath` + tag/lights/roadsign/moon/skidmarks + `DrawFinalEffects_pf` +
`verifyBytes` + the two install functions (`InjectDelayedPatches`, `DllMain`). The file
stops being the INI reader, the render callback owner, the corona state machine, and the
effects-hygiene owner.

**Never extracted (hard list):**
- `DllMain` (`8108-8701`) — process entry, attach/detach SEH, final hook installs.
- `InjectDelayedPatches` (`7810-8071`) — startup gating + literal hook/patch order.
- `verifyBytes` + the `s_exp_*` / `s_staticEntryExpect` / `s_rtEntryExpect` /
  `s_coronaEntryExpect` byte arrays (`8082-8094`, `8176-8180`, `8198`, `8284`, `8341`,
  `8422`) — verification is part of the install contract.
- `struct Config` (`skygfx.h:280-931`) and the `configs[10]` / `config` definitions
  (`main.cpp:77-80`) — APPEND-ONLY ABI, offset-pinned asm.
- `myPluginAttach` / `installDeferredHooks` / `InitialiseGame_hook` (`5490-5600`) —
  MoonLoader/UG branch and startup ordering.
- `renderMoonMaskBody` / `renderMoonMask` (`3595-3671`) — SEH + ebp/esp frame surgery.
- `fixSeed` / `saveIntensity` / `doglare` (`2506-2570`, `2739-2752`) unless Stage 7 is
  explicitly taken as one block.

---

## 7. Risks and the one probe that catches each

| Risk | Silent failure mode | Probe / log line |
|---|---|---|
| Hook install order changed | A later hook overwrites an earlier trampoline; one subsystem silently stops firing. | Existing receipt order in `skygfx_dbg.log`: `[shadow_install]` (8262/8297/8318/8354) → `[corona_entry_install]` (8439) → `[corona_install]` (8485) → `[pointlights_install]` (8517/8540) → `=== DllMain complete, all hooks applied ===` (8641). Order must match the pre-split log 1:1. |
| A verified site now mismatches | `InterceptCall` is silent on a bad site; `InjectHook PATCH_NOTHING` rewrites a foreign instruction. | `[corona_install] ... MISMATCH` (8485), `[shadow_install] ... SKIPPED` (8270), `[pointlights_install] ... MISMATCH` (8517/8540). Any MISMATCH means the new install call is at the wrong point or the exe differs. |
| Config ABI / offset drift | `fixSeed` reads `Config+8`, `doglare` reads `Config+12`; a moved field makes grass placement/sun glare read garbage. | Compile gate: `static_assert(offsetof(Config, fixGrassPlacement)==8 / doglare==12)` (`skygfx.h:925-927`). Runtime: `[INIT] ambientMult=` (7844) and `[Mode] listener ... pipes=%d/%d/%d cf=%d` (7805) must still print sane values. |
| Static initialisation order | A moved global with a dynamic initialiser runs after `DllMain` and reads zeroed config. All moved blobs are POD/zero-init; the only dynamic object is function-local `linb::ini` in `configIo`. | `numConfigs=` (7824) → `[INIT] ambientMult=` (7844) → `[BUILD] asipath=... built=...` (7882) → `=== InjectDelayedPatches complete ===` (8069). A missing/reordered `numConfigs` or `[INIT] ambientMult` means an initialiser moved. |
| SEH fence lost or merged | The game crashes on a fault path instead of restoring state (black screen / black quads) with no log. | First-hit receipts: `[PedOpaque] pre-entry` (1897), `[ShadowHeal]` (1499), `[CoronaExit]` (4728), `[FlushShield] restore` (3210), `[CloudShield]` (3369), `[FxAlpha]` (4006). A missing line means the path never ran or faulted. |
| Log tag renamed / duplicated | `dbglog_once` and throttle buckets are shared strings; a rename changes what the log proves and can starve a bucket. | `[probe] defcb_shader path taken` (2031), `[probe] sprite_flush shield active pre-dev` (3307), `[probe] baseline scenestart called` (3783), `[probe] baseline effects_exit_renderfx/postfx/final` (4295/4324/7781). Each must appear exactly once per run. |
| Water draw regression | The raw flush runs with the wrong VS/PS/FVF or leaves the RW caches stale; water renders as wireframe/garbage. | `[water] water_state pre-draw dev zenable=... zfunc=... zwrite=... ds=` (5709) plus `[water] gate failed` (5628) if it falls through. |
| Corona window regression | The shield window stays open/closed across a frame; black quads return next frame. | `[CoronaDraw] pre-force dev blend=...` (2963), `[CoronaResync] window closed` (3425), `[CloudShield] flush batch=clouds/sprites` (3369). |
| LNK4042 duplicate object | Link fails, no runtime symptom. | Basename gate in §5; the link itself. |
| Config I/O drift | INI keys/defaults change; a save round-trip drops a key or persists an out-of-range value. | `saveConfigTo` diff: `[cfg_save] ok` (7629) + `[cfg_load] ok` (7658); compare `skygfx.ini` before/after with all values unchanged. |
| Inlining/perf change (non-behavioural) | Cross-TU calls replace static inline calls in the hot default callback; frame time rises slightly. | No silent behaviour change. Optional check: `profilerEnable=1` and the `PfFrame` line; compare average frame time before/after Stage 2. |

---

## 8. Bottom line

`main.cpp` is splittable without behaviour change **because every large block is already
scope-isolated and the install order is a contiguous, enumerable list**. The safe order is:
pure leaves (S1) → contracts + default callback (S2) → probes/frame (S3) → corona window
(S4) → effects/clouds (S5) → config runtime/I/O (S6). That is 14 new TUs and a ~1,558-line
entry TU, with `DllMain`, `InjectDelayedPatches`, `Config`, and the moon mask untouched.
No stage needs a rewrite; every stage is a body move plus a build-row swap plus `cl /Zs`.

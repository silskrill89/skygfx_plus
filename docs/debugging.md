# Debugging

Order of operations for any visual bug: **prove the build → prove the mode → prove the gate → prove the pass**. Log first, screenshot second, hypothesis last.

## Log basics

- File: `skygfx_dbg.log` beside the ASI (`GetModuleFileNameA` at `src/core/main.cpp:7242-7244`). Overwritten per process.
- `dbglog(fmt, ...)` — init/error/one-shot lines.
- `dbglog_throttle("tag")` — per-frame lines, global 2 s interval per tag.
- `dbglog_once("tag")` / `dbglog_first("tag")` — once per process or first-N.
- Every line is plain text; no timestamps. Correlate by order and by the frame counter.

## Log-tag dictionary

`tag → meaning → healthy line → failure line`. Healthy/failure text is quoted as emitted; `...` marks variable tails.

| Tag | Meaning | Healthy | Failure |
|---|---|---|---|
| `[BUILD]` | identity of the ASI actually loaded | `[BUILD] asipath=E:\games\gtasa_skygfx_plus\skygfx.asi size=... mtime=YYYY-MM-DD HH:MM:SS built=<date> <time>` | missing line, or `asipath=?` / `size=0` / mtime older than your build |
| `[Mode]` | mode apply/load receipt | `[Mode] active=GTAIV (from INI)` then `[Mode] apply GTA IV pipes=4/2/9 cf=7 iv=1` | `[Mode] active=none` when INI says otherwise; apply line with unexpected pipes; no apply line |
| `[mode]` | manager lifecycle / warnings | `[mode] manager init` | `[mode] ApplyMode(%d) out of range - ignored`, `[mode] ApplyMode(%d) ignored - no config bound`, `[mode] listener table full`, `[mode] <X> latch cleared (direct Config write, base kept)` |
| `[Config]` | config healing / derivation | `[Config] PBR selected -> buildingPipe 1 -> 3` | `[Config] cfEnableHeal: ... early-return, heal is blind`, `[Config] colorFilterEnable=%d diverged from user intent` |
| `[Pipeline]` | pipe baseline / state helpers | `[Pipeline] pipeApplyWorldBaseline: SceneStart` and `: EffectsExit` once per frame | baseline missing at one end; repeated every frame warnings |
| `[RENDER]` | scene/camera raster | `[RENDER] frame=1 world=... camera=...` (first 3 frames) | `[RENDER] scene-entry camRas=... != screen ... repairing` every frame; `camera-raster cache invalidated` on device reset is normal |
| `[probe]` | one-shot baseline/evidence | `[probe] baseline scenestart called`, `[probe] baseline effects_exit_final called`, `[probe] sprite_flush shield active ...` | missing probe = the hook never ran; repeated probe = re-entry |
| `[PostFX]` | postFX chain stages (65 sites) | per-pass enable/skip lines in chain order | a pass logs "skip" while its INI key is on; chain stops before `DrawFinalEffects` |
| `[SMAA]` | SMAA raster/quality | init + quality lines | raster init failed / pass skipped |
| `[ForwardPlus]` | tile culling and light atlases | `[ForwardPlus] textures created %dx%d ...` | `CreateTexture index A/B failed`, `AddLight buffer full` (lights dropped), fallback kill-switch line |
| `[water]` | water style/tier/params (20 sites) | style + tier for the active mode | style mismatch (e.g. IV still on Xbox style), missing tier copy |
| `[MirrorPass]` / `[EnvMap]` / `[EnvTex]` | reflections, env map | pass start/end per frame | null raster, skipped pass, size mismatch |
| `[PipeChain]` | PipeChain reflections | chain build + pass lines | chain disabled/failed |
| `[VehiclePBR]` | vehicle shader constants | per-material upload | NaN/zero-length warnings, shader fallback |
| `[shadow_contract]` / `[ShadowHeal]` | shadow pass state capture | `[shadow_contract] static exit healed=<n>` | `[shadow_contract] <pass> pre-entry layer drift: drift=<n> ...`, `[ShadowHeal] L3 pinned to cache ...` |
| `[RSMon]` | render-state receipts | `[RSMon]` frame report with zero drift | drift counters non-zero at `scene_start` / `frame_end` |
| `[INIT]` / `[MenuInject]` / `[Menu]` | startup and menu injection | init complete, menu injected | injection failed, menu rows missing |
| `[GUARD]` / `[Cull]` | null/depth guards | guard armed once | guard firing per frame (null pointer path) |
| `noise_bind` | first tiled-noise bind (one-shot) | `[Pipeline] pipeBindNoise: first bind stage=... size=... tex=... seed=...` once | repeated binds or `tex=0`/zero-size noise |
| `shadow_heal` | shadow-state heal (one-shot) | `[ShadowHeal] L3 pinned to cache ...` | heal cannot pin; drift persists |
| `CloudShield` | cloud/sprite flush blend shield | `[CloudShield] flush batch=clouds forced ONE/ONE` | shield missing or wrong blend on a cloud batch |
| `cull_resync` | cull state restored after env/mirror (throttled) | `[CullResync] env pass restored=<n>` | missing restore after an offscreen pass |
| `BldXbox` | Xbox building material dispatch | `[BldXbox] tex='...' tac=... blendOn=... matA=... zw=...` | texture/tac mismatch or unwanted blend |
| `fp_path` | Forward+ end-to-end path audit | `[ForwardPlus] path: pushed=... drainRaw=... dedup=... collected=... gpu=... fallback=... replay=...` | `pushed>0` but `gpu=0`; fallback flap |
| `pipe_alpha_restore` | faithful alpha restore | `[PipeAlpha] faithful restore (parity 6): ...` | restore skipped; stale ALPHAREF/blend |
| `ped_rs` | ped render-state receipt (throttled) | throttled ped state line | ped state drift or contract missing |
| `rs_mon_report` | render-state monitor summary | `rs_mon_report: samples=... agree=... diverge=... pct=... bad=... sites=... top=...` | `diverge`/`bad` non-zero |
| `water_state` | water pre-draw device state | `[water] water_state pre-draw dev zenable=... zfunc=... zwrite=... ds=...` | missing line or unexpected depth state |
| `mode_active` | mode load receipt (one-shot) | `[Mode] active=<alias> (from INI)` | line repeats or active=none while INI names a mode |
| `bld_noisedetail` | per-mesh noise-detail path (one-shot: first real selection, throttle-gated) | `[BPIPE] bld_noisedetail: noise-detail body PS bound tex=... ps=...` | missing on a noise-detail material, or fired with a detail map |
| `pf_gate` | 18-pass PostFX gate mask (throttled) | `[PF] pf_gate: mask=0x... normalBuf=... vel=... ssao=... ... smaa=... fxaa=...` | pass bit set while its key is off, or needed bit clear |
| `bpipe_elide` | per-mesh render-state elision window (throttled) | `[BldElide] writes skipped since last window: PS2=... Xbox=... PBR=... Sphere=...` | skipped count grows while state visibly wrong |
| `water_rt` | water render-target lifecycle (event-driven, not throttled) | `[water_rt] release scene/depth/refl ...` | missing release / size mismatch (tag is **pending**: source emits it, not yet a promoted receipt) |
| `mode_click` | debug-menu mode chip click receipt (throttled) | `[ModeUI] click <label> -> mode=<id> menuOpen=<0/1>` | click with no following `[Mode] apply` line, or the mode id does not change |
| `ped_cull` | ped cull A/B receipt; source label `[PedCull]`, re-armed one-shot on change (not time-throttled) | `[PedCull] mode=<n> saved=<rw> forced=<rw>` | mode differs from the INI value, or saved/forced is not a valid rw cull mode |
| `ped_atest` | ped alpha-test A/B receipt; source label `[PedAlphaTest]`, re-armed one-shot on change (not time-throttled) | `[PedAlphaTest] enabled=<0/1>` | value differs from the INI value; line missing after a change |
| `ssao_alloc` | SSAO/velocity depth resource allocation receipt (throttled) | `[SSAO] alloc receipt: <res> <w>x<h> ssaoEnable=<0/1>` | wrong dimensions, or allocation churn visible every window |
| `cull_ood` | out-of-domain cull sanitized to BACK (one-shot) | `[Cull] out-of-domain value=<n> -> BACK` | cull still looks wrong after the sanitize, or the line repeats |
| `smaa_vel` | SMAA temporal velocity inputs (throttled) | `[SMAA] velocity: vel=... rot=... tanFov=(...) velTex=... velGate=<why>` | `velTex=00000000`, or `velGate` names a failed condition while motion blur is on |
| `veh_elide` | vehicle env-pass render-state elision window (throttled; sampled every 64th callback) | `[VehElide] cb=Env window_cbs=... meshes=... elided=... writes (...)` | elided count rises while vehicle state is visibly wrong |
| `bld_tex` | building material texture-bind receipt, first 24 meshes (throttled; sample-capped) | `[BldTex] tex='...' null=... bound=... blend=... zw=...` | `null=1` with a NULL/unresolved bind, or an unexpected blend/zwrite decision |
| `veh_tex` | vehicle material texture-bind receipt, first 24 meshes (throttled; sample-capped) | `[VehTex] tex='...' null=... bound=... env=... spec=... matA=...` | `null=1` with a NULL/unresolved bind (`bound=0`), or the same wrong texture bound |
| `cf_exp` | colour-filter exposure receipt after the exposure-floor clamp (throttled ~2 s) | `[CF] exposure=... sceneLuma=... knee=... minExp=... maxExp=... autoExp=...` | exposure at/near the floor while the frame is black; minExp/maxExp zero while auto-exposure should be on |
| `water_sun` | bounded water sun/reflection inputs, first successful setup (one-shot; re-armed by `waterPipe_init`) | `[water_sun] style=... tier=... specPow=... specInt=... reflStr=...` | line missing (water setup never ran), or any value pinned at a clamp bound |

Rule: one unknown tag in the log is a documentation gap — add it here when you find it.

**Renamed:** `bld_stoch` is the old throttle tag; the live tag is `bld_noisedetail`. Searching for `bld_stoch` in a current log will find nothing. The same rename applies to the feature name: use **noise detail**, not "stochastic"; `stochastic`/`stochasticTexturing` survive only as deprecated INI aliases.

## Build identity check

Before reading any visual evidence, confirm the running binary:

```
[BUILD] asipath=<full path> size=<bytes> mtime=YYYY-MM-DD HH:MM:SS built=<__DATE__> <__TIME__>
```

Emitted at `src/core/main.cpp:7054` from `GetFileAttributesExA` on the loaded module. Three checks:

1. `asipath` is `E:\games\gtasa_skygfx_plus\skygfx.asi` (or your `SKYGFX_GAME` target), not a stale copy elsewhere.
2. `size` matches `build\Release\skygfx.dll` from the last build.
3. `mtime` is **after** the source edit you are testing; `built=` is the compile timestamp, `mtime` is the copy time. If `mtime` is older than `built`, the deploy did not happen.

If the line is absent, the loaded ASI is not this build (or logging failed before init).

## INI bisect levers

Change one lever at a time; each is a single key or section.

| Lever | Effect | Use when |
|---|---|---|
| `[Modes] active=None` | disables the profile delta; top-level keys rule | isolate "mode profile vs renderer bug" |
| Remove `[Modes]` entirely | same as `None`, no mode read at all | rule out mode persistence |
| `stateBaselineEnable=0` | skips frame baseline only; corona window close still runs | isolate L1/L2/L3 drift |
| `texAlphaClassify=0`, `pedOpaqueContract=0` | disables alpha contracts | isolate ped/vehicle opacity |
| `pipelineOverride=-1` | stops the mirror rewrite | pipeline desync during a mode |
| `pipeline=PBR|PS2|Xbox|Mobile|GTAIV` | forces the legacy mapping (ignored by pipe-owning modes) | pipe selection without a mode |
| `qualityPreset=3` | ULTRA base, feature-only (no pipe writes) | clean base before a mode |
| `envMapSize`, `envFresnel` | env-map cost/look | reflection bugs |
| `waterStyle`/`waterQuality` | water shader/tier | water-only regressions (GTA V mode) |
| `smaaEnable=0`, `ssaoEnable=0`, `motionBlurEnable=0`, `bloomEnable=0`, `dofEnable=0`, `sunShaftsEnable=0`, `heightFogEnable=0` | per-pass kill switches | bisect PostFX chain |
| `normalBufferEnable=0`, `velocityBufferEnable=0`, `depthHookEnable=0` | buffer producers | pass goes inert / depth crashes |
| `forwardPlusEnable=0` | legacy 7-light path | lighting/tile bugs |
| `[General] NoiseQuality=0` | low-res utility noise | noise/detail artefacts |

Dead keys — changing them proves nothing: `smaaPreset`, `smaaPredication`, `smaaTemporal`, `normalMapBuilding`, `normalMapVehicle`, `normalMapDebug`, `normalMapDebugMode`, `tagsBuildingPipe`, `presetFile`.

## Annotated mode switch

```
[mode] manager init                          ← process start, latch = None
[Mode] active=Modern (from INI)              ← read from [Modes] active
[Mode] apply Modern pipes=0/3/10 cf=8 iv=0   ← one profile apply
```

Now the user picks GTA IV in the menu:

```
[Mode] apply GTA IV pipes=4/2/9 cf=7 iv=1    ← A->B on the ORIGINAL base
```

Read it as `pipeline/buildingPipe/vehiclePipe`: IV = `PIPELINE_GTAIV(4)` / `BUILDING_GTAIV(2)` / `CAR_GTAIV(9)`, filter 7. If the building/vehicle numbers look like another mode's, a legacy writer ran after the profile. Toggle the row again:

```
[Mode] apply None pipes=0/3/10 cf=8 iv=0     ← base restored verbatim
```

The toggle-off line prints the restored config, not the profile that was left. Compare it to the config you had before entering the mode; a mismatch means the base was mutated by a live edit.

## Crash classes

| Class | Signature in log | First check |
|---|---|---|
| Null frame/raster in a pipe callback | last line is a pipe/vehicle tag; crash at RW address | guard the `RwFrame`/`RwCamera`/`RwRaster` pointer; no per-draw assumptions |
| State desync | `[RSMon]` drift, `[shadow_contract] ... drift=`, `[ShadowHeal]` | find the last raw L3 writer before the pass; route through `pipe*` helpers |
| Depth hook / device | crash right after `DepthHook_Install`, `0x7FBD4A` referenced in comments | `depthHookEnable=0` bisect; check SMAA scratch RT restore |
| Shader constant upload | flat/black PBR frame, no shader error | confirm c22/c23 via `pipeUploadPBR`; check NaN guards |
| Mode desync | pipes in `[Mode] apply` differ from profile | `pipelineOverride`, quality tiers 0-2, `ApplyPreset` latch clear |
| Stack/hook timing | crash inside `InitialiseGame_hook` with SEH code | read the `CRASH in original! code=...` line; check MoonLoader skip path |

The crash handler (`src/core/debug_handl.cpp`) and the SEH guards around `InitialiseGame_hook` (`main.cpp:4790-4798`) log the fault before the process dies; the last tag is the pass to inspect.

## Crash recovery

Session history is in the OpenCode SQLite DB. **Always open read-only** — the DB is live and multi-GB:

```bash
sqlite3 -readonly "file:C:/Users/aaaaaaaaa/.local/share/opencode/opencode.db?mode=ro"
```

Exact query for this project's recent sessions:

```sql
SELECT id,
       datetime(time_updated/1000,'unixepoch','localtime'),
       title
FROM session
WHERE directory='E:/dev(dave)/skygfx_plus_expIV'
ORDER BY time_updated DESC
LIMIT 40;
```

Python one-liner (same query):

```bash
python -c "import sqlite3; c=sqlite3.connect('file:C:/Users/aaaaaaaaa/.local/share/opencode/opencode.db?mode=ro',uri=True); [print(r) for r in c.execute(\"SELECT id,datetime(time_updated/1000,'unixepoch','localtime'),title FROM session WHERE directory='E:/dev(dave)/skygfx_plus_expIV' ORDER BY time_updated DESC LIMIT 40\")]"
```

Recovery procedure:

1. Do not delete the DB or WAL while OpenCode is running. If it will not open, close OpenCode and remove only `opencode.db-wal` / `opencode.db-shm`, then retest.
2. Pull the last session title/timestamp; the last assistant message is the checkpoint.
3. Re-read the on-disk docs produced by that session (they are the intended durable state).
4. Rebuild identity, then re-run the last failing test rather than trusting the summary.
5. Never reopen with `mode=rw` unless you intend to write.

## Evidence loop

```
screenshot → RGB signature → log correlation → hypothesis → ONE build → retest
```

1. **Screenshot**: same camera position, same weather/time, same mode. Record the mode alias, INI diff, and build identity line with it.
2. **RGB signature**: sample 3-5 pixels (sky, shadowed wall, vehicle paint, water). A flat/dark PBR frame reads near-uniform; a wrong colour filter shifts all channels together; SSAO failure shows as unoccluded corners. Numbers, not adjectives.
3. **Log correlation**: find `[Mode] apply`, `[BUILD]`, the pass tag, and `[RSMon]` for the same frame window. Absence of a pass line is evidence.
4. **Hypothesis**: one sentence naming the layer (L1 intent / L2 cache / L3 raw / shader / gate).
5. **ONE build**: change one thing, build once (`python tools/fast_build.py`), confirm `[BUILD]` changed.
6. **Retest** the same scene. If the RGB signature moves in the predicted direction, keep; otherwise revert before the next hypothesis.

Anti-patterns: two changes per build; judging from memory of the previous screenshot; trusting a doc over the log; running `--fastest` while unsure whether shaders recompiled (use `--shaders` to rule it out); reading `skygfx_dbg.log` from a previous run without checking `mtime`.

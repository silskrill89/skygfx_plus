# MASTER PLAN — consolidated issue registry + gated attack waves (2026-11-08)

Date: 2026-11-08 (recon window ran into 2026-11-09 ~03:00-04:00) · Lane: planner (READ-ONLY; this doc is the only write)
Absorbs: the planned "issue registry wave2" doc (there is no second doc — this is it).
Branch `very-experimental`, HEAD `5cc5d25`, working tree dirty (36 modified src/shader paths + untracked `src/extras/water_swell.h`).

**⚠ Moving-tree caveat (verified live).** `src/core/main.cpp` measured **10516 lines at recon start, 10492 at recon end** — sibling lanes are editing RIGHT NOW: `src/render/buildingPipe.cpp` (mtime 2026-11-09 03:05, N5 buildingPipe+PBR-VS lane) and `src/core/weather.cpp` (mtime 03:06, sun-colour lane). `src/render/pipelinecommon.cpp` last written 2026-11-08 12:10 (after the 09:48 gate build — its contents are UNBUILT). All line numbers below are pinned **as-of this recon**; durable anchors are function names + receipt tokens (`[PedZW]`, `[zfix2]`, `[scene_repair]`, `[shadow_install]`, …). Re-verify line numbers before quoting in any fix lane (same rule as `docs/plans/2026-11-07-n-registry-rootcause.md` header).

**Method.** Full reads: `docs/plans/2026-11-07-n-registry-rootcause.md`, `2026-11-07-build-gate-checklist.md`, `2026-11-07-m6-ripple-sim-plan.md`, `2026-10-30-water-overhaul-spec.md`, `effects-menu-overhaul.md`, `2026-11-02-systematic-roadmap.md`, `2026-11-02-recon-pass.md`, `docs/baseline/2026-11-03-test12/findings.md`, `docs/baseline/2026-11-02-stable-config/README.md`, `.serena/memories/wave-14-record.md`, `wave-13-record.md` (§4-5), `standing-directives-2026-11-02.md`. Source spot-verification: **38 file:line anchors re-checked against live source** (§6 drift table). Targeted reads of `src/core/{main.cpp,presets.cpp,debugmenu_ui.cpp,debug_handl.cpp,debug_handl.h,weather.h,modeManager.h}`, `src/render/{pipelinecommon.cpp,renderstate_monitor.cpp,postfx.cpp,postfx_motionblur.cpp}`, `src/peds/{skin.h,peds_pipe.h}`, `src/entities/softbody.cpp` (grep), `src/extras/water_swell.h`, `shaders/ps/MotionBlur_Burnout.hlsl`, `premake5.lua`, live INI `E:\games\gtasa_skygfx_plus\skygfx.ini`.

---

## §0 Executive summary + attack order

### 0.1 Where the project stands (verified)

1. **The 2026-11-08 09:48 gate build shipped** (`skygfx.asi` 2,594,816 B, md5 `83b8ede7…`, `docs/plans/2026-11-07-build-gate-checklist.md:13`, §2-§4 all ticked; all 16 binary tokens verified on the deployed file). It contains: water toggles, MotionBlur Tier-1 HLSL, the **N2 canonical-contract comparator** (`[zfix2] CONTRACT`, `renderstate_monitor.cpp:701` `RSMon_ContractSample` — file mtime 09:46, pre-build), peds Phase-3b, M9 swell law (`src/extras/water_swell.h`, untracked), menu close-out.
2. **The user retest of that build produced the 2026-11-08 forensics** (dispatch §1b): the master fault **H1** — `scene_start` device render-state divergence recurring every 2 s tick (device ALPHABLEND=1, ZWRITE=0, ZTEST=0, ZFUNC=ALWAYS while both caches believe blend-off/z-on/LESSEQUAL) — the exact signature quoted verbatim in the NEW `pipeDeviceStateSet` header comment (`pipelinecommon.cpp:1212-1215`), plus **H5** — `pRasFB=2048×2048` vs `camRas=1920×1080` front-buffer mismatch (prior evidence: `docs/plans/2026-10-26-pale-slab-audit.md:37,:150`; `docs/plans/2026-10-27-log-forensics-1809.md:413`; probe receipt `main.cpp:5269` region).
3. **The H1 response already landed ON DISK, UNBUILT** (all post-09:48): `pipeDeviceStateSet` cache-coherent chokepoint (`pipelinecommon.cpp:1198-1234+`, header: "N1 master-fault kill, 2026-11-08") + routed writers in `forwardplus.cpp:1891-1953`, `particles_gpu.cpp`, `postfx.cpp`; `pipeRepairSceneStartSync` (`pipelinecommon.cpp:1585`, `[scene_repair]` receipt :1685-1686, called from `main.cpp:4975`); main.cpp state-contract cluster (font fast-path depth probe `main.cpp:4514-4530`, `spriteEnforceDepthContract` :3681 with 4 call sites :3807/:4123/:4573/:5942, PedOpaque blend-factor capture :1105-1164); `[PipeAlpha]` receipt semantics split (sticky-resolved :2401 vs clean-replay :2415-2440); INI safety-lever flips in the live INI (`E:\games\gtasa_skygfx_plus\skygfx.ini`: `pedOpaqueContract=1` :142, `spriteStateShield=1` :177, `texAlphaClassify=1` :209, `[Modes] active=None` :5); `MotionBlur_Burnout.hlsl` Tier-1 + rotor velocity-variance rejection (shader header :11-15, :107-109).
4. **Two sibling lanes are IN FLIGHT this minute**: buildingPipe+PBR-VS (N5 lighting + `[BldBind]` contract; `buildingPipe.cpp` + `buildingPBRVS.hlsl`) and `weather.cpp` sun colour. Per dispatch: **assume DONE-in-flight; do not schedule their files until they land** (§3 serialization).
5. **Checklist §5 (runtime receipts + test matrix) and §6 (retest→commit) are still unticked** — the tree has never been re-measured with the H1 response built in. Every N-item status below is therefore "landed-unbuilt / awaiting W0 measurement" where applicable.
6. **The core-audit backlog (dispatch §1c) is real but drifted**: 19 of 21 items re-verified against live source with new line numbers (§1.3, §6); 1 item (empty-ImGui-frame) is **already mitigated at the call sites** in current source; 1 item (mode-lever clobber) is **downgraded** after verification (only 2 of the 3 named levers are mode-owned, both profiles write the SAME safe value the INI flip just set, and no mode is active).

### 0.2 The one-paragraph strategy

H1 is the trunk: a foreign raw L3-only write in the effects/postfx/sprite phase moves the device behind agreeing caches; the applied[]-gated pre-draw flush value-compares against the stale cache and skips; the wrong device state survives the whole scene and is only caught (and partially re-created) by downstream "faithful restore" replays — one mechanism plausibly behind the milky sheets (N3), draw-over-CJ/blotches (family B/N2), ped depth drift and the head blob. The chokepoint (`pipeDeviceStateSet`) + scene-start repair + sprite/font depth-contract enforcement are the kill, **but they are unbuilt and their coverage is unproven**: ~138 raw `d3d9device->SetRenderState` sites remain in main.cpp alone (most are legitimate 3-layer-paired contracts; the census in W1-L2 separates paired-contract from foreign). So: **W0 builds and measures the kill; W1 closes the residual (census + attribution + contract-completeness) and the high-value menu/preset/debug clusters; W2 takes the INI/config main.cpp leaf + H5; W3 takes the measurement-gated N-tails + lever retirement; W4+ starts the structural extraction (shadows.cpp first) and the M6/M9 water endgame** — one integrated build per wave, main.cpp single-writer, extraction strictly after the system's behavioral fixes.

### 0.3 Attack order (each wave gated by §5)

| Wave | Name | Headline content | Gate to next wave |
|---|---|---|---|
| **W0** | Build + measure (orchestrator only) | absorb 2 in-flight lanes → ONE integrated build → deploy → binary tokens → user retest (test12 4/5/7/11/12 + N3 8/9/10 + stable-config) → log forensics: H1 verdict, N1 receipt split, N3/N4/N6 measurement, H5 evidence | build green + tokens + receipts present + retest shots captured |
| **W1** | H1 residual + contract completeness + menu/debug cluster | 6 lanes: main.cpp★ state-contracts, pipelinecommon census/attribution, presets+menu cluster, debug_handl UB fix, modeManager lever receipt, postfx_utils promotion rule | `[zfix2] scene_start DIVERGE → 0`; `rs_mon DIVERGE site=scene_start → 0`; no menu regressions |
| **W2** | INI/config main.cpp leaf + H5 + conditional N-fixes | 6 lanes: main.cpp★ INI cluster, postfx/motionblur H5 leaf, weather tail, conditional texalpha/grasssys/texdefs leaves (open only where W0/W1 measurement shows survival) | H5 verdict documented; save/load round-trip provable; conditional leaves measured |
| **W3** | N-tails + lever retirement + comparator follow-through | ≤5 lanes: main.cpp★ legacy-arm retirement (decision-gated), buildingPipe/vehiclePipe exit sticky-Z (attribution-gated), treeWind, renderstate_monitor verdicts | palms/grass re-shoot clean OR residual re-registered with fresh evidence |
| **W4** | EXTRACTION E1 — `src/core/shadows.cpp` | 1 mechanical lane + 1 verify lane; byte-identical bodies; build gate after | binary tokens identical; shadow A/B retest |
| **W5** | M6 ripple-sim leaves 1-4 + M9 completion | waterSim module, shader pair, waterPipe wiring, Water_Parallax consumption; softbody rebind to water_swell.h + single-clock collapse | tier<3 bit-identical; sim RT lifecycle survives Reset |
| **W6** | M6 leaf 5 (main.cpp★ disturbance sources) | rain/boat/player injection wired from main.cpp wake hooks | ripples visible; no disturbance → flat → identical surface |
| **W7** | EXTRACTION E2 — corona/sprite window | main.cpp★ ~1560-line region → own .cpp | token parity; corona retest |
| **W8+** | EXTRACTIONS E3-E5 + optional splits | pedContract (after retirement), config.cpp (INI), hooks_install.cpp; then camraster/clouds/wake/coronaLights splits | one system per wave, build gate each |

Budget: ≤6-8 lanes per wave (dispatch budget 10-20 overall; ≤2 lanes per model tier per wave — model tags in §3). main.cpp is **single-writer**: at most ONE ★ leaf per wave, never shared.

---

## §1 CONSOLIDATED ISSUE REGISTRY

Canonical numbering: **N1-N7** (wave-14 registry, `.serena/memories/wave-14-record.md:38-45`) stay canonical. Retest-forensics hypotheses keep their **H** names and are mapped onto N-items. Core-audit findings get **C** numbers. Done-today items get **D** numbers (never re-schedule). In-flight sibling work gets **F** numbers (assume done; verify at W0).

### 1.1 N-registry (canonical) — current status

| # | Item | Specimens / evidence | Status at this recon | Owner files (current anchors) |
|---|---|---|---|---|
| **N1** | `[PipeAlpha]` L2/L3 faithful-restore replay/split (81× `enable L2=1 L3=0` in build 11) | test12 findings:101-104, :172; wave-14:38 | **MITIGATION LANDED-UNBUILT**: sticky no-replay rule + receipt split (`pipelinecommon.cpp:2401` "sticky exit: split capture NOT REPLAYED … resolved=sticky", :2415-2440 clean-replay receipt, :2259 `pipeExitAlphaMode`) + source-kill chokepoint (D2). OPEN: split-source census (W1-L2), `CoronaWindow_Close` completeness (`main.cpp:4578`), `SaveRawGeomStates/RestoreRawGeomStates` drift promotion (`postfx_utils.cpp:190/:246`) | pipelinecommon.cpp, main.cpp (corona window), postfx_utils.cpp, postfx.cpp (gated), buildingPipe/vehiclePipe exits (gated) |
| **N2** | zfunc=8 dead-depth (106/127 repairs, 2 s throttle ⇒ ~120 frames dead-depth between repairs) | test12 findings:114-127, :170; roadmap:166 | **COMPARATOR BUILT (09:48)** (`[zfix2] CONTRACT`, `RSMon_ContractSample` renderstate_monitor.cpp:701; 4 sample sites pipelinecommon.cpp:1494/:1543/:1740/:1792) + **REPAIRS LANDED-UNBUILT**: `spriteEnforceDepthContract` (main.cpp:3681; callers :3807 flush-shield close, :4123 corona_resync, :4573 font exit, :5942 CCoronas_exit), font fast-path depth probe (main.cpp:4514-4530), scene-start repair (D3). OPEN: recurrence→0 verdict (W0), PostFX quad-bracket depth restore (gated), non-pipe consumers (skidmarks, buildingPipe.cpp:528 comment) | pipelinecommon.cpp, renderstate_monitor.cpp, main.cpp, postfx.cpp (gated) |
| **N3** | FAM-Q giant milky sprites/dome (shots 1,2,3,4,8,9,10) | test12 findings:79-81, :155 | **MEASURE FIRST**: texalpha.cpp landed +340 lines today (deterministic classification, D6) — re-shoot 8/9/10 at W0 before any new fix. H5 haze-scale is a confounder (§1.2). Conditional leaves: soft-glow class admission (texalpha), spriteStateShield soft-falloff policy (main.cpp★, serialized) | texalpha.{cpp,h}, main.cpp (sprite shields :3377-4869 region) |
| **N4** | FAM-G grass corruption (shard slabs, yellow streaks, grass-over-water; shots 4,5,12) | test12 findings:75-76, :156 | **MEASURE FIRST**: grasssys.{cpp,h} +40 lines today (D6); grass-over-water is family-B depth carve — expected clear post-W0. `grassRenderCallback` unconditional CULLBACK restore is a registered defect (C18). Conditional leaves: grasssys latch/class policy, texalpha grass band (collides with N3 leaf — serialize) | grasssys.{cpp,h}, texalpha.cpp, main.cpp (`grassRenderCallback` :820) |
| **N5** | FAM-N near-black palms + washed-white buildings (shots 4,5,12) | test12 findings:75-76, :157; n-registry §4.3 | **IN FLIGHT (F1)**: sibling lane landing buildingPipe+PBR-VS lighting + `[BldBind]` contract right now (buildingPipe.cpp mtime 11-09 03:05; buildingPBRVS.hlsl 12:29). Do not schedule. Residual (only if palms survive W0 re-shoot): pipelinecommon ambient/foliage consumption, treeWind `TreeAlpha_RestoreAlphaRef` policy | buildingPipe.cpp (F1), pipelinecommon.cpp (light uploads :174/:199 region per n-registry), treeWind.cpp |
| **N6** | FAM-T missing textures / red square (shots 1,2) | test12 findings:72-73, :159 | **MEASURE FIRST**: texdefs.cpp +107 lines today (D6, lookup-failure receipts). **H5 is a candidate mechanism for the fixed-screen-pos red square** (§1.2) — the W0 log analysis must decide texture-lookup vs raster-dims before opening a texdefs leaf | texdefs.cpp (:657 `TexDefsGetD3DTexture` per n-registry — re-verify line), postfx.cpp (H5 side) |
| **N7** | FAM-W bounded whiteout — **KNOWN/ACCEPTED** + rotor smear | test12 findings:158; roadmap:171 | Bounded level holds (accepted, tracked, not a ship gate). **Rotor smear fix LANDED-UNBUILT**: `MotionBlur_Burnout.hlsl` Tier-1 + per-pixel velocity sanity + per-tap velocity-variance rejection (shader :11-15, :107-109; git M + CSO M). Weather sun-colour sibling lane IN FLIGHT (F2). Acceptance: no full-white collapse; no giant rotor fan in straight flight; `camera CUT reset` spam → 0 (roadmap:94) | shaders/ps/MotionBlur_Burnout.hlsl (done), weather.cpp (F2), postfx.cpp (timecycle/fog bracket — item E height-fog gate still IN FLIGHT per roadmap:70-82) |

### 1.2 Retest forensics (2026-11-08) — ranked hypotheses, mapped

**H1 — MASTER FAULT: `scene_start` device render-state divergence, every 2 s tick.** Device {ALPHABLEND=1, ZWRITE=0, ZTEST=0, ZFUNC=ALWAYS} while L1 rw cache + L2 driver cache both believe {blend off, z on, LESSEQUAL}. ONE mechanism plausibly explains: milky sheets (N3 — additive/blend state wrong at sprite draw), draw-over-CJ/blotches (family B / N2 — dead depth), ped depth drift ([PedZW] class), head blob. Mechanism of persistence: the L2 pre-draw flush is applied[]-gated and value-compares pending[] against a cache that already holds world values → skips the device write → wrong device value survives the scene; cache-parity-keyed repairs never fire (verbatim: `pipelinecommon.cpp:1208-1219`; `pipeRepairSceneStartSync` header :1560-1567; roadmap:29-44 item B).
**Maps onto:** N1 (the split-capture SOURCE class) + N2 (the dead-depth survivor class). Historical counts: 123× `rs_mon DIVERGE site=scene_start` in the build-11 session (test12 findings:173).
**Status:** three-layer response landed-unbuilt (D2 chokepoint + D3 scene-start repair + D4 sprite/font depth enforcement). **Kill confirmation is W0's primary probe**: `[zfix2] CONTRACT site=scene_start` violations → 0 and `rs_mon DIVERGE site=scene_start` → 0 over ≥5 min. If they persist, the residual writer is unrouted — W1-L2 census + attribution names it.

**H5 — front-buffer mismatch: `pRasFB=2048×2048` vs `camRas=1920×1080`.** Candidate for the fixed-screen-position red square (N6 side) and haze/sheet scale errors (N3 side): fullscreen-quad geometry/UV built against the wrong raster dims. Evidence chain: 526 occurrences in the 10-26 run (`pale-slab-audit.md:37`, "front buffer ≠ camera raster" :150); `colorfilterVerts` spanning 0..2048 against a 1920×1080 camera (`log-forensics-1809.md:413` — "whether the resulting UV mapping is correct is not decidable from the log"); live probe receipts `[FxAlpha] … camRas=0488EFC0 pRasFB=04890A38` (`main.cpp:5265-5270` comment); existing partial repair `camRasterRepair` (INI default 1; `main.cpp:4869-5130` region, receipt `[RENDER] scene-entry camRas=%p %dx%d != screen %dx%d — camera-raster swap active, repairing…` :5102-5104). **OPEN — no fix landed.** W2-L2 leaf: dims-consistency assertion + attribution receipt at the UpdateFrontBuffer/colorfilterVerts path (`postfx.cpp`), plus the motionblur-lane flag "s2 INTZ camRas assertion" (`postfx_motionblur.cpp:176-180` camRas dims read; :433/:503-506 INTZ unbind/rebind window) — assert camRas dims == INTZ depth dims before s0/s2 sampling.

### 1.3 Core-audit registry (dispatch §1c, 2026-11-08 explorer full-read) — verified against live source

Verdicts: **CONFIRMED** = defect present in current source · **FIXED-TODAY** = already resolved by today's landed lanes · **MITIGATED** = call-site gates now exist; re-verify at runtime · **CORRECTED** = dispatch claim refined by verification. Current anchors in §6.

| # | Finding | Verdict | Current anchor (as-of recon) |
|---|---|---|---|
| **C1** | `ApplyPreset` never writes `buildingPipe`/`vehiclePipe` while the menu promises it ("PS2 -> building=PS2 vehicle=PS2…"); PBR derive rule-3 stomps filter choice; IV preset sets `ivMode` over non-IV pipes | **CONFIRMED** | presets.cpp:126-141 (skip comment "pipes owned by ApplyMode… must never be overwritten" :126-129; `c->ivMode = p->ivMode` :141 with no pipe write); debugmenu_ui.cpp:3006-3007 tooltip "overwrites the pipe/filter/effect keys the preset carries" + promise text :3038-3042; derive rule-3 lock `main.cpp:729-736` (forces `colorFilter=COLORFILTER_MODERN` whenever buildingPipe==PBR and no mode owns pipes — stable-config runs buildingPipe=PBR, so any preset filter choice is stomped every frame via ColourFilter_switch); Modern button already partially fixed via `SyncIvModeFromPipeFamily` (debugmenu_ui.cpp:3019-3030) |
| **C2** | `presetNames` NULL@14 vs `NUM_PRESETS=15` → preset combo reads a NULL item | **CONFIRMED** | presets.cpp:5-21 (14 names + NULL; missing "skygfxplusultramaxdeluxe" which EXISTS as presetConfigs[14] :86-104); `NUM_PRESETS`=15 (skygfx.h enum, 15 PRESET_* entries ending PRESET_SKYGFXPLUSULTRAMAXDELUXE, :285-307); consumer `ImGui::Combo(…, presetNames, NUM_PRESETS)` debugmenu_ui.cpp:3000 — index 14 = NULL → UB/crash-on-open |
| **C3** | Save INI always writes `skygfx.ini`, which is never read back when numbered configs exist; `CurrentIniPath()` off-by-one | **CONFIRMED** | save: `saveConfig()` main.cpp:9332-9348 (rewrites .asi→.ini → always `skygfx.ini`); load: `InjectDelayedPatches` main.cpp:9453-9459 (`if(numConfigs==0) readIni(0); else readIni(1)` → skygfx1.ini; skygfx.ini skipped); `readIni(n)` n>0 loads `skygfxN.ini` into `configs[n-1]` (:8885-8901); off-by-one: `currentConfig` is a 0-based configs[] index (`setConfig` main.cpp:8917-8922) but `CurrentIniPath` prints `skygfx%d.ini, currentConfig` (debugmenu_ui.cpp:641-642) → slot 0 (skygfx1.ini) reports "skygfx.ini", slot 1 (skygfx2.ini) reports "skygfx1.ini" |
| **C4** | Settings filter `ContainsCI` NULL-hay no-op | **CONFIRMED** | debugmenu_ui.cpp:2804-2807 (`if(!hay || !needle || !needle[0]) return true;`) + call sites :2861-2866 — any row whose `note` (or other field) is NULL matches EVERY filter → filter defeated for NULL-noted rows |
| **C5** | Stale coronaZtest tooltip | **CONFIRMED (minor)** | debugmenu_ui.cpp:934-937 — tooltip text ("Patches game code at 0x6FB17C through resetValues()…readIniFile clamps this key to [-1..1]") vs actual parse+clamp at main.cpp:8059-8069; patch-site wording must be re-verified against resetValues() before rewriting |
| **C6** | `va_start(ap, level)` UB in `dbglog_loc` | **CONFIRMED** | debug_handl.cpp:343-350 — `dbglog_loc(LogLevel level, const char *file, int line, const char *func, const char *fmt, ...)` does `va_start(ap, level)`; C requires the LAST named parameter (`fmt`). `dbglog` at :334-341 is correct (`va_start(ap, fmt)`) |
| **C7** | `PedOpaqueState` blend-factor half dead + lying comments | **CONFIRMED (post-fix residual)** | main.cpp:1074-1086 struct now captures rwSrc/rwDst/drvSrc/drvDst/devSrc/devDst (today's D5 work); comment :1077-1078 claims "sticky rule at exit compares these against the L2/L3 captures below and the canonical pair" and :1157-1162 claims "an out-of-pair L2/L3 only ever latches the exit sticky rule" — but NO exit path consumes them: `pedOpaqueDiverged` (:1167-1180) ignores src/dst; `pedOpaqueRestore` (:1246-1274) restores everything EXCEPT src/dst; the exit blocks (:2729-2803) never read savedOpaque.rwSrc/drvSrc/devSrc. The [PedRS] monitor re-reads its own locals (:2830-2890). Blend-factor capture is write-only; comments lie |
| **C8** | Dead legacy-arm remnants | **CONFIRMED** | main.cpp:2401-2407 — `else{ pedStage0Resync(atomic, pedsPipeClassified ? pedsPipeState : NULL); }` inside the `pedsPipeState==NULL` branch: the ternary can only evaluate to NULL (dead logic); legacy-arm end markers :2386, :2804, :2892; the whole legacy inline arm is the `pedsPipeEnable=0` rollback lever (peds_pipe.h:34-35, skin.h:5-12) — retirement decision in §3 W3 |
| **C9** | `readhex(char*)` unused duplicate | **CONFIRMED** | main.cpp:7455-7462 `int readhex(char *str)` vs :7523-7530 `int readhex(const char *str)` — identical bodies; both call sites (:7616/:7619) pass `kval.c_str()` → const overload; char* version dead |
| **C10** | First-parse default divergence: self-fallback parse vs EMIT literal list | **CONFIRMED** | self-fallback parses: `sssPostProcessEnable/skinEnhanceEnable/hairEnhanceEnable/vegetationEnhanceEnable` main.cpp:8353-8356 (`readint(…, c->x)` — inherits struct default), `ssaoRadius` :8081, `motionBlurStrength` :8093, `neoWaterDrops` :8036; EMIT list writes literals: :8560-8588 (`SKYGFX_EMIT_DEFAULT_KEYS`, e.g. sssPostProcessEnable "1" :8571, skin/hair/veg "1" :8575-8577, ssaoRadius "1.0" :8564, motionBlurStrength "0.4" :8570) + first-parse cfg.set mirror :8731-8743 — the value the engine runs with (struct default) can differ from the value written into a fresh INI (EMIT literal) for the same key |
| **C11** | Water-toggle clamp direction: negative → 1 | **CONFIRMED** | main.cpp:8417-8422 — `if(c->waterShoreFoam) c->waterShoreFoam = 1;` (same ×6: waterBodyClassify, waterRefractionValid, waterCaustics, waterSss, waterVolume): a negative INI value is truthy → clamped to **1 (ON)**, not 0; same idiom at :8453 (treeWind), :8458 (grassInstancing), :8464 (gpuParticlesEnable), :8474-8475 (profiler) |
| **C12** | sunCorona/Core/StreakIntensity no clamp | **CONFIRMED** | main.cpp:8376-8379 — `sunCoronaIntensity` (0.4f), `sunCoreIntensity` (0.6f), `sunStreakIntensity` (0.5f), `sunStreakSize` (0.6f) raw `readfloat`, no range clamp (contrast: coronaZtest clamps with receipt :8059-8069, treeFx clamps :8441-8443) |
| **C13** | zwriteThreshold lane-section inconsistency | **CONFIRMED** | main.cpp:8047 `readint2(cfg, "Core", "zwriteThreshold", 128)` vs grass/ped variants from **"SkyGfx"** :8051/:8055; save writes all three to **"SkyGfx"** :9052-9054 → the main threshold is read from [Core] but persisted to [SkyGfx]: a save→reload round-trip silently changes the parse source (unless readint2's lane fallback covers it — verify readint2 semantics in the fix lane) |
| **C14** | `SetLightsWithTimeOfDayColour` unconditional 184-byte memset of 3 game globals + F5 backdoor | **CONFIRMED** | main.cpp:2983-2999 — `ambRed/ambGreen/ambBlue` = `(uint8*)0xB7C3C8/0xB7C310/0xB7C258`; every call memsets all three 184 B to 0 (or 0xFF while F5 held); inherited verbatim from `backup_original/src/main.cpp:523-536` (verified identical); no INI gate, no receipt; interacts with N5-class lighting (per-weather ambient tables zeroed every call — the 184 B span covers the ambient arrays the game interpolates) |
| **C15** | `strncpy(asipath, …)` no NUL guarantee | **CONFIRMED (bounded)** | main.cpp:8868 `strncpy(asipath, modulePath, MAX_PATH);` — if the source were exactly MAX_PATH without NUL, asipath would be unterminated; practically bounded because `modulePath` comes from `GetModuleFileName` into MAX_PATH (itself truncation-guarded downstream at :8875-8881 for the n>0 path, but the strncpy at :8868 runs BEFORE that guard and asipath is consumed at :166/debugmenu_ui.cpp:639). One-line fix: `asipath[MAX_PATH-1]='\0'` |
| **C16** | `CTagManager__RenderTag` rw-only fallback | **CONFIRMED** | main.cpp:2915-2936 — the `!iCanHasbuildingPipe` fallback sets/restores ALPHATESTFUNCTIONREF (and the ref=64 tag value) through **L1 `RwRenderStateSet` only**; on this build the device frequently disagrees with the caches (the whole H1/N2 problem class) → tags can draw with a stale ref; the guarded side (building pipe) is fine |
| **C17** | Empty ImGui frame every game frame | **MITIGATED — re-verify at runtime** | the frame scaffolding exists as claimed (debugmenu_ui.cpp:4393-4395 NewFrame, :4669-4670 Render/RenderDrawData — exact lines) BUT current call sites gate it: `postfx.cpp:2677-2682` (`if(dev && config->debugMenuOpen) DrawUnifiedDebugMenu(dev)`); standalone overlay frame gated by `!s_ovEnabled → return` (debugmenu_ui.cpp:4803) and `debugMenuOpen → return` (:4811), with `debugOverlay` INI default 0 (roadmap PERF R2 :191-193). Residual: with debugOverlay=1 a full ImGui frame runs per game frame by design. W0 action: perf receipt check, then close or re-register |
| **C18** | `grassRenderCallback` restores CULLBACK unconditionally (L1-only) | **CONFIRMED** | main.cpp:820-932 — `if(!config->backfaceCull){ RwRenderStateSet(CULLMODE, CULLNONE); cullPatched=1; }` (:862-866) then restores **hardcoded CULLBACK** via L1 only at early-out (:895-896) and normal exit (:929-930): (a) clobbers a foreign cull scope (mirror/shadow spans run their own live mode — the ped path fixed exactly this with `pipeEnterCullMode`/`pipeForceCullMode`, main.cpp:2233-2267); (b) L1-only write can no-op against a drifted device (the C16 class) |
| **C19** | Stale header comments | **CONFIRMED ×3** | debug_handl.h:139-156 ("NOT yet wired to config parsing — reported hunk…do not apply": diagLevel IS wired — skygfx.h:733 Config member, main.cpp:7679 parse, skygfx.h:127-129 throttle consumer); weather.h:17-23 ("blended values from both sets"/"in BOTH modes" — `Weather_SelectSet` ALWAYS returns WEATHER_SET_2, weather.cpp:63-65, blend always 1.0 :70-71); modeManager.h:41-42 ("MODE_MOBILE…hidden from the 6+Custom picker" — the picker lists Mobile with a full hint, debugmenu_ui.cpp:2927-2932, and effects-menu-overhaul.md:166-168 records all nine ModeIds now covered) |
| **C20** | Entry-shield late foreign patch not re-detected | **CONFIRMED (design gap)** | main.cpp:10157-10217 — corona entry shield 0x6FAEC0: 20-byte prologue verified ONCE at install (`[corona_entry_install]` receipt :10191-10195, MISMATCH→SKIP :10196-10200); a foreign patcher (other ASI/SilentPatch-class) writing the entry AFTER install is never re-detected — the trampoline would execute stale copied bytes; same one-shot pattern across the shadow install block (:9939-9999). Low-cost fix: re-verify expected bytes at a throttled frame hook and log `[shield_integrity]` on divergence (do NOT auto-repatch — receipt-first) |
| **C21** | Mode-owned lever clobber risk (dispatch §1d) | **CORRECTED — downgraded** | Only `texAlphaClassify` + `pedOpaqueContract` are mode-owned (6 profiles each: modeManager.cpp:217-218, :326-327, :411/:415, :555-556, :712-713, :821-822; docs/ini-reference.md:163/:168 tagged "mode-owned (6 profiles)"). **`spriteStateShield` is NOT mode-owned** (ini-reference.md:165 carries no mode-owned tag; no MV_I entry) — dispatch claim refined. Both mode-owned levers are written **=1 by every profile = the same value today's INI safety flip set** (skygfx.ini:142/:209), and the live INI runs `[Modes] active=None` (:5) so ApplyMode never fires. Residual risk (keep as W1-L5 leaf, LOW): a user who deliberately sets either lever to 0 in INI **and** activates a mode gets it silently re-flipped to 1 — add an ApplyMode receipt naming every lever rewrite (modeManager.cpp `ApplyMode` :1393+) |

### 1.4 DONE today — landed on disk, never re-schedule (all UNBUILT except where noted)

| # | Item | Evidence |
|---|---|---|
| **D1** | main.cpp state-contracts cluster (dispatch: "5/6") — ped depth-trio unconditional entry force + sticky exit, [PedZW]/[PedOpaque] receipt chain, font fast-path depth probe, `spriteEnforceDepthContract` + 4 call sites, PedOpaque blend-factor 3-layer capture + rw-twin sanitize | main.cpp:2290-2321 (depth contract), :2783-2803 (sticky depth), :4504-4539 (fast path), :3681-3712+ (enforce), :1105-1164 (blend capture); git M main.cpp +623 lines |
| **D2** | `pipeDeviceStateSet` foreign-writer cache-sync chokepoint + routed writers | pipelinecommon.cpp:1198-1234+ ("N1 master-fault kill, 2026-11-08"); routed: forwardplus.cpp:1891-1953, particles_gpu.cpp, postfx.cpp (grep: 3 external caller files + pipelinecommon internals); NOTE: silent by design — no own receipt; kill is measured via [zfix2]/rs_mon silence |
| **D3** | scene_start repair-to-cache (wave-12 lineage) wired after RSMon_Sample | pipelinecommon.cpp:1547-1620+ `pipeRepairSceneStartSync`, `[scene_repair]` receipt :1685-1686; main.cpp:4964-4975 (shared 1-in-30 frame divider :4912-4926, frame_end pairing :9395-9401) |
| **D4** | N2 canonical-contract comparator (**BUILT in the 09:48 gate**) | renderstate_monitor.cpp:366, :507-522 (`[zfix2] CONTRACT site=%s pipe=%d rw=%02X drv=%02X dev=%02X`), :701-730; sample sites pipelinecommon.cpp:1494/:1543/:1740/:1792; checklist §3 token-verified in binary |
| **D5** | `[PipeAlpha]` receipt semantics split (sticky-resolved vs replayed vs clean) — closes n-registry flag #3 probe gap | pipelinecommon.cpp:2401 ("resolved=sticky"), :2415 ("Fires ONLY for a CLEAN capture that the faithful restore replayed"), :2440 (parity-6) |
| **D6** | texalpha (+340), texdefs (+107), grasssys (+40), particles_gpu (+85), peds skin/peds_pipe (+1171/+10, Phase-3b absorb), chars.cpp fixes | git status M + diffstat (recon-verified); skin.h:19-36 absorption table maps main.cpp blocks → SkinEntryContract/SkinExitContract/SkinExitMonitor/SkinStage0Resync |
| **D7** | Live INI 3 safety levers flipped ON | `E:\games\gtasa_skygfx_plus\skygfx.ini:142/:177/:209` (pedOpaqueContract/spriteStateShield/texAlphaClassify =1); `[Modes] active=None` :5 |
| **D8** | MotionBlur_Burnout.hlsl Tier-1 + rotor velocity-variance rejection (N7 rotor smear) | shader :11-15 ("N7 (2026-11-08 family-F rotor smear)"), :107-109 (2-texel tolerance rejection); git M hlsl + M resources/cso/MotionBlur_Burnout.cso (CSO regenerated — rebuild anyway per §5) |
| **D9** | M9 swell canonical law header shipped (**BUILT in the 09:48 gate** per checklist §1 "M9 swell law") | src/extras/water_swell.h (untracked, 111 ln): namespace WaterSwell :43, consumer list :8-12 (softbody lane, ragdoll, M9 LUT later), dual-clock note :32; ragdoll_death.cpp:9 includes it |
| **D10** | Menu close-out lane (P1-5, P2-2/3/4, P2-15, P3-1/3/8, water M4/M5/M7/M8 toggle rows) | effects-menu-overhaul.md:138-197 §8 |

### 1.5 IN FLIGHT — assume done, do not schedule (verify landing at W0)

| # | Lane | Files | Content |
|---|---|---|---|
| **F1** | buildingPipe + PBR-VS | src/render/buildingPipe.cpp, shaders/vs/buildingPBRVS.hlsl (+CSO) | N5 lighting fix + `[BldBind]` contract (dispatch); buildingPipe.cpp mtime 2026-11-09 03:05 — **actively being written during this recon** |
| **F2** | weather sun colour | src/core/weather.cpp (+weather.h M) | sun-colour fix adjacent to N7 (dispatch); weather.cpp mtime 2026-11-09 03:06 |

**pipelinecommon.cpp serialization**: last written 12:10 Nov-8; no mtime movement during this recon, but the n-registry moving-tree finding names it + renderstate_monitor.cpp as the comparator lane's contended scope. Rule: any W1 leaf writing pipelinecommon.cpp starts only after the orchestrator confirms the comparator/measurement lanes are quiescent (git status M-flag stable across two checks ≥30 min apart).

---

## §2 Per-issue analysis (root cause · owners · fix design · LEVERAGE · tier · risk · regression surface)

Priority ordering principle from dispatch: **fixes that kill many issues at once go first** — H1 master fault, mode-lever protection, classifier/texture correctness.

### P0 — H1 master-fault residual kill (N1+N2 trunk)

**Root cause (verified):** foreign raw L3-only writes in the effects/postfx/sprite phase + applied[]-gated flush skip + faithful-restore replay (pipelinecommon.cpp:1208-1219, :1560-1567; test12 findings:58-61 "one shared architectural root").
**Owners:** pipelinecommon.cpp (chokepoint + attribution), renderstate_monitor.cpp (verdicts), main.cpp (corona window completeness), postfx_utils.cpp (promotion engine), postfx.cpp (quad brackets, gated).
**Fix design:**
1. *(landed D2/D3/D4/D5 — measure at W0)*.
2. **Raw-writer census + attribution** (W1-L2, pipelinecommon.cpp): a throttled `[rawcensus]` receipt (or RSMon extension) that, on each `scene_start` DIVERGE, dumps the last-N state-transition attributions so the residual writer is NAMED, not hypothesized. Classify the remaining raw sites: main.cpp ×138, vehiclePipe ×40, buildingPipe ×39, skin.cpp ×30, neopipe ×17, chars.cpp ×6, depthhook ×4, envmap ×3, treeWind/grasssys ×2, postfx_utils/postfx_smaa/postfx/particles_gpu/forwardplus ×1 (recon grep counts) — most are legitimate 3-layer-paired contract pins; the census separates "paired" (fine) from "L3-only foreign" (route through `pipeDeviceStateSet`).
3. **`CoronaWindow_Close` completeness** (W1-L1★, main.cpp:4578): restore everything `coronaDrawRawShield` parks (n-registry S1-L3c: the close restored only ALPHAFUNC/ALPHAREF/ALPHABLENDENABLE per log-divergence-timeline:262-265 — re-verify current body first; today's `spriteEnforceDepthContract("CCoronas_exit")` at :5942 already covers the depth trio, so this leaf may reduce to blend-factor/BLENDOP completeness).
4. **`SaveRawGeomStates`/`RestoreRawGeomStates` promotion rule** (W1-L6, postfx_utils.cpp:190/:246 per n-registry — re-verify lines): never promote a divergent device capture into L1/L2 — canonical-or-verbatim + receipt (n-registry S1-L4).
5. **PostFX quad-bracket depth restore** (W2-L2, gated on W1 attribution naming postfx): Begin saves the depth trio, End restores it (n-registry S1-L5).
**LEVERAGE: HIGHEST.** Kills/advances: N1 (source), N2 (recurrence), family-A residual (A-2 discriminator, test12 findings:107-110), family-B residual, N3 (if milky sheets are blend-state victims), C7 (blend-half semantics ride the same sticky-rule machinery).
**Tier:** high (state surgery in the hottest shared files). **Risk:** a wrong canonical value now mis-repairs EVERY draw — mitigate by keeping repairs receipted and measurement-first. **Regression surface:** all draw paths; stable-config no-regression is the gate.

### P0 — W0 build + measurement gate (no code)

**Root cause of necessity:** 36 modified files unbuilt since 09:48; every status above is unverifiable without one integrated build + retest. **Design:** checklist §0-§6 run top-to-bottom (§5 below), plus capture: `[zfix2] scene_start` violation count, `rs_mon DIVERGE site=scene_start` count, `[PipeAlpha]` sticky-vs-replayed split counts, `[PedZW]`/`[BldZW] parked` presence, `[font_flush] fast path` vs pinned ratio, `[scene_repair]` count, texalpha summary (no `budget=99`), test12 shots 4/5/7/11/12 + 8/9/10 re-shoots, H5 evidence (`pRasFB`/`camRas` dims lines, red-square presence). **LEVERAGE:** decides which W2 conditional leaves open at all (measurement-gates N3/N4/N5/N6 per n-registry flags #4). **Tier:** orchestrator-only. **Risk:** none (no code).

### P1 — C1/C2/C3/C4 preset+menu+save cluster (one lane, two files)

**Root causes:** (C1) preset/menu contract drift after the mode-ownership refactor — ApplyPreset deliberately refuses pipe writes (presets.cpp:126-129 cites "doc E1-E4") but the UI still promises them (debugmenu_ui.cpp:3006-3007, :3038-3042) and derive rule-3 (main.cpp:729-736) stomps any non-MODERN filter while buildingPipe=PBR; (C2) 15th preset added to configs but not to names; (C3) save/load path asymmetry + 0-based/1-based confusion; (C4) NULL-hay filter no-op.
**Fix design (W1-L3, presets.cpp + debugmenu_ui.cpp only — no main.cpp needed):**
- C2: add `"skygfxplusultramaxdeluxe"` to presetNames at index 14 (one line; crash-class bug).
- C1: make the promise true OR true-sounding: when `ModeManager_GetMode()==MODE_NONE`, let ApplyPreset write buildingPipe/vehiclePipe from the PresetConfig (the struct still carries both fields, presets.cpp:24-105 — they are currently write-never-read); when a mode is active, keep the skip and change the tooltip/labels to "pipes are mode-owned while a mode is active". Add the same `SyncIvModeFromPipeFamily` call the Modern button uses (debugmenu_ui.cpp:3028) so the IV preset stops setting ivMode over non-IV pipes (presets.cpp:141). Rule-3 filter stomp: document in the preset pane ("PBR building pipe locks filter=Modern every frame — main.cpp derive rule 3"); do NOT weaken the lock (it is a never-revert fix, main.cpp:722-723).
- C3 (menu half): `CurrentIniPath` → `skygfx%d.ini, currentConfig+1` when `numConfigs>0`, else `skygfx.ini` (debugmenu_ui.cpp:641-644).
- C4: `ContainsCI(!hay) → return false` (empty needle still returns true) (debugmenu_ui.cpp:2804-2807).
- C3 (main half, W2-L1★): `saveConfig()` saves to the ACTIVE ini (respect currentConfig/numConfigs — reuse the readIni(n) path-building), keeping `skygfx.ini` only for the numConfigs==0 case.
**LEVERAGE:** high for user trust/workflow (the menu is the operator surface for every retest); C2 is a crash-class one-liner; C3 kills the "my saves vanish" class and protects the W1-W3 retest loop itself.
**Tier:** low-medium. **Risk:** preset writes touching pipes could fight ApplyMode — mitigated by the mode-active guard; rule-3 lock untouched. **Regression surface:** menu/preset/config-file behavior only; no draw paths.

### P1 — C7 blend-factor half: consume or prune (main.cpp★, W1-L1)

**Root cause:** today's D5 capture work landed the fields but not the exit-side consumer; comments (:1077-1078, :1157-1162) describe a sticky rule that does not exist.
**Fix design:** EITHER (a) extend the exit sticky rule (main.cpp:2750-2803 region) to compare `savedOpaque.{rw,drv,dev}{Src,Dst}` against the canonical pair and end canonical on divergence (same idiom as the depth sticky — and this is the ped-span twin of the N1 alpha-split rule), OR (b) if `pipeExitAlphaMode`'s PipeAlphaState sticky block (pipelinecommon.cpp:2259+, savedAlpha pre-seed main.cpp:2171-2175) already owns blend-factor restoration for the span, delete the dead capture fields + fix both comments to say so. Decide by reading pipeExitAlphaMode's current src/dst handling FIRST (unify directive §2: no second copy of an existing fix). Option (b) is the likely outcome — verify before coding.
**LEVERAGE:** medium — closes the ped-span blend-split class (family-A residual) or removes 6 dead capture reads per ped draw + stops comments misleading the next lane.
**Tier:** medium. **Risk:** (a) adds a state writer at ped exit — must end canonical, never re-emit; (b) is comment+dead-code only. **Regression surface:** ped draw span.

### P1 — C14 SetLights memset + F5 backdoor (main.cpp★, W1-L1)

**Root cause:** inherited verbatim from backup_original (verified :523-536 identical): zeroes the game's per-weather 184-byte ambient tables every call so the mod's own lighting owns ambient — but it is unconditional, ungated, unreceipted, and F5 (an in-game keybind the player can hit accidentally) whites them out.
**Fix design:** gate behind an INI lever (default 1 = current behavior, stable-config rule "no behavior change by default"), one-shot receipt, and move F5 to a debug-menu action or a dedicated rarely-colliding key with an on-screen dbglog note. Coordinate with F1/F2 (buildingPipe lighting + weather sun colour lanes are touching the same lighting semantics — land AFTER them; W1 timing is fine since they land at W0).
**LEVERAGE:** medium-high — the memset is a plausible contributor to N5-class lighting weirdness (zeroed ambient tables vs PBR ambient floor receipts at test12 #12, findings:83) and any future "ambient is wrong" hunt starts here.
**Tier:** low (gate+receipt) / medium (F5 relocation). **Risk:** changing ambient-zeroing behavior changes EVERY lit surface — hence default-preserving gate only. **Regression surface:** global lighting.

### P1 — C16 + C18 L1-only state writers (main.cpp★, W1-L1)

**Root cause:** both write render state through the rw cache only, which no-ops against a drifted device (the exact mechanism grasssys.cpp:143 documents and H1 generalizes).
**Fix design:** C16: run the tag fallback's ref=64 set/restore through the 3-layer idiom (`pipeForceAlphaBlock`-style or direct L2+L3 pairing, matching the "same convention as D3D9RenderDual / grassRenderCallback" comment at main.cpp:2925-2927 — which is itself L1-only, so fix both sites in one idiom). C18: capture the pre-entry cull mode (`pipeEnterCullMode` already exists — the ped path uses it at :2233) and restore THAT (span-aware) through `pipeExitCullMode`, instead of hardcoded CULLBACK L1-only (:895-896, :929-930); keep `backfaceCull` semantics.
**LEVERAGE:** medium — kills two members of the "L1-only write" defect class (the class H1 proves is load-bearing); C18 also removes a foreign-scope clobber (mirror/shadow grass).
**Tier:** medium. **Risk:** grass draws hundreds of atomics/frame — keep the frame-latch discipline (main.cpp:836-841 pattern) and don't add per-atomic Get/Set round-trips beyond the existing ones. **Regression surface:** tags, grass, mirror/shadow spans.

### P2 — C6 va_start UB (debug_handl.cpp, W1-L4)

**Root cause:** `va_start(ap, level)` with 4 more named params after `level` — undefined behavior; works on x86 cdecl by accident, breaks under any ABI/optimization change and is a latent log-corruption source.
**Fix:** `va_start(ap, fmt)` (one token). Add `static_assert`-style comment. **LEVERAGE:** low-direct, but the logging system is the evidence base for EVERY other issue — a corrupted va_list silently poisons forensics. **Tier:** trivial. **Risk:** none. **Regression surface:** dbglog_loc callers only.

### P2 — C21 mode-lever receipt (modeManager.cpp, W1-L5)

**Fix design:** ApplyMode logs one line per mode-owned lever it rewrites with old→new (`[mode] lever pedOpaqueContract 0->1 (profile X)`), so a silently re-flipped safety lever is visible in the same log the retest forensics read. Optionally honor an explicit-INI-wins rule later — receipt first (project doctrine: measure, then repair). Fix modeManager.h:41-42 stale comment in the same lane.
**LEVERAGE:** protects the D7 safety flips from being silently undone the first time a user picks a mode — cheap insurance on the whole N-registry program. **Tier:** low. **Risk:** none (logging). **Regression surface:** mode apply path.

### P2 — H5 front-buffer mismatch (postfx.cpp + postfx_motionblur.cpp, W2-L2)

**Root cause hypothesis:** some path adopts/creates a 2048×2048 front-buffer raster (pRasFB) while the camera raster is 1920×1080; fullscreen-quad verts/UVs built against pRasFB then map fixed-screen-position content (red square) and scale haze/sheets wrongly. `camRasterRepair` (main.cpp:4869-5130) repairs the CAMERA raster swap but not the pRasFB side.
**Fix design:** W0 log analysis first (is pRasFB still 2048² in the new build? does the red square correlate with a dims-mismatch receipt window?). W2-L2: (1) dims-consistency assertion + one-shot attribution receipt at the UpdateFrontBuffer/colorfilterVerts build path (postfx.cpp — locate via `[PostFX-REPORT]` / `colorfilterVerts` emit sites, cf. regression diff docs/plans/regression/cpp-c270294-to-head.diff:2138/:2644); (2) the motionblur lane's flag: assert camRas dims == INTZ depth dims before the s2 sample window (postfx_motionblur.cpp:176-180, :433, :503-506) with a throttled receipt; (3) only if attribution names a wrong-dims creator: repair/resize at the source.
**LEVERAGE:** high-if-confirmed — candidate single mechanism for N6's fixed-screen-pos red square AND N3 haze-scale AND the historical pale-slab family (pale-slab-audit.md:150). **Tier:** medium (investigation-heavy). **Risk:** touching front-buffer adoption can blank the screen — receipt-only leaves first, repair leaf gated on attribution. **Regression surface:** postfx composite, motion blur, anything screen-quad.

### P2 — INI/config cluster C9-C13, C15, C8-prune, C10 (main.cpp★, W2-L1)

**Fix designs (one serialized main.cpp leaf):**
- C9: delete `readhex(char*)` (:7455-7462), keep the const overload.
- C10: make first-parse defaults single-source: where EMIT writes a literal (:8560-8588, :8731-8743), the parse must use the SAME literal as its self-fallback default (or vice versa) — align the 6 identified keys first (sssPostProcessEnable, skin/hair/vegetationEnhanceEnable, ssaoRadius, motionBlurStrength), then sweep the EMIT list against parse defaults mechanically. Respect stable-config: keys the user runs at 0 via explicit INI stay 0 (explicit value beats default); only ABSENT-key behavior changes — verify each alignment against `docs/baseline/2026-11-02-stable-config/README.md` before landing (e.g. skinEnhance EMIT "1" vs stable-config OFF — the stable INI has the explicit key, so no regression, but triple-check).
- C11: clamp direction — `if(c->x) c->x = 1;` → `c->x = (c->x > 0) ? 1 : 0;` at :8417-8422 and the four sibling sites (:8453/:8458/:8464/:8474-8475).
- C12: clamp sun intensities to a sane range (suggest [0..4] with one-time receipt, matching the coronaZtest clamp idiom :8059-8069) at :8376-8379.
- C13: read `zwriteThreshold` from [SkyGfx] with [Core] fallback (keep legacy INIs working), save to [SkyGfx] as today (:8047 vs :9052-9054); verify readint2's existing lane-fallback semantics first.
- C15: `asipath[MAX_PATH-1] = '\0';` after :8868.
- C8-prune: `pedStage0Resync(atomic, pedsPipeClassified ? pedsPipeState : NULL)` → `pedStage0Resync(atomic, NULL)` (behavior-identical; the full legacy-arm retirement is W3, gated).
- C3-main: saveConfig active-INI fix (see P1 cluster above).
**LEVERAGE:** medium — config correctness is the substrate of every retest (a save that silently lands in the wrong file invalidates A/B runs); C10 kills the "fresh install behaves differently from saved install" class. **Tier:** low-medium, breadth over depth. **Risk:** default changes can flip behavior on absent keys — each change must be diffed against stable-config expectations. **Regression surface:** INI parse/save, fresh-install path.

### P3 — Measurement-gated N-tails (W2/W3 conditional leaves)

- **N3 residual** (texalpha.cpp, W2-L4, strong): soft-glow class admission + soft-decal "painted solid" fallback policy (n-registry S2-L1/S2-L2; texalpha.h class enum extension rides the same lane). Opens ONLY if W0 re-shoot shows shots 8/9/10-class specimens surviving D6's deterministic classification. Collides with N4's texalpha leaf — one texalpha writer per wave.
- **N4 residual** (grasssys.{cpp,h}, W2-L5, mid): draw-time latch set + cutout/faded grass policy (n-registry S3-L1/L2). Opens only if shard-slab/streak specimens survive. grass-over-water expected clear via family-B (n-registry H4) — verify, don't re-fix.
- **N5 residual** (pipelinecommon.cpp light uploads / treeWind.cpp, W3, gated): foliage ambient-floor consumption audit (n-registry S4-L1: trees/foliage not among the enumerated consumers) + TreeAlpha ref policy (S4-L3). Opens only if palms still near-black after F1 lands and re-shoots. pipelinecommon leaf serializes behind W1-L2.
- **N6 residual** (texdefs.cpp, W2-L6, fast): lookup-failure `outWhy` receipt surfacing + name/demotion path — opens only if pink/magenta specimens survive AND H5 doesn't claim the red square.
- **N1/N2 gated tails** (postfx.cpp W2-L2 gated; buildingPipe.cpp/vehiclePipe.cpp exit sticky-Z W3-L2/L3 gated): open only where W1-L2 attribution names the writer (n-registry S1-L5/L6a/L6b). buildingPipe waits for F1 to land.
**LEVERAGE:** per-item medium; collectively they close the visible-artifact registry. **Tier:** mid-high (texalpha) to low (texdefs). **Risk:** classifier changes affect every textured surface — one TexAlpha_Classify change set per wave, never per-consumer copies (unify directive §2; n-registry §5 unify note 2). **Regression surface:** all alpha-tested/blended geometry.

### P3 — C5/C17/C19/C20 hygiene (folded into adjacent lanes)

- C5 tooltip refresh → W1-L3 (menu lane). C17 runtime re-verify → W0 log/perf check, close or re-register. C19 weather.h comment → W2-L3 (after F2 lands); debug_handl.h comment → W1-L4; modeManager.h comment → W1-L5. C20 shield-integrity re-verify receipt → W3 main.cpp★ leaf (retirement wave already owns main.cpp) OR fold into W2-L1 if that leaf is light — decision at wave dispatch; design: throttled re-read of the verified prologue bytes at each install site, `[shield_integrity]` receipt on divergence, no auto-repatch.
**LEVERAGE:** low-direct; keeps the evidence base (comments/receipts) trustworthy — this project's forensics culture makes stale comments a real defect class. **Tier:** trivial. **Risk:** none.

### P3 — Lever retirement: pedsPipeEnable=0 legacy inline arm (decision leaf W3-L1★)

**Context:** Phase-3 FLIPPED the peds pipe (skin.h:5-12, peds_pipe.h:34-35): the module (SkinEntryContract/SkinExitContract/SkinExitMonitor/SkinStage0Resync) owns the span when pedsPipeState≠NULL; the legacy inline blocks (main.cpp:2208-2386, :2401-2407, :2728-2804, :2816-2892) are the byte-for-byte rollback lever.
**Decision rule:** retire (delete the inline arms + the lever) only after **two consecutive clean retests** with the module path live (W0 + W1 gates) showing [PedRS] receipt parity and no ped-family regression; otherwise keep the lever one more wave. The decision itself is a doc note in the wave record; the code leaf is mechanical deletion (large main.cpp diff — its own wave slot, and it shrinks the later pedContract extraction).
**LEVERAGE:** structural — removes ~400 duplicated lines from main.cpp before extraction E3, ending the dual-maintenance hazard (unify directive §2: two copies of the same contract IS wrong). **Tier:** medium (deletion-only but hot path). **Risk:** loses the rollback lever — mitigated by git history + the two-clean-retest gate. **Regression surface:** ped draw span.

---

## §3 Target-fix waves — lane matrix

Rules honored (standing-directives-2026-11-02 + n-registry §5): lanes never build — ONE integrated build per wave by the orchestrator; no overlapping write scopes within a wave; **main.cpp single-writer (★, at most one leaf per wave)**; pipelinecommon.cpp serialized behind its current lane (quiescence check §1.5); clustered multi-file passes allowed (directive §3); one shared implementation, never per-site clones (directive §2); ≤2 lanes per model tier per wave (strong/mid/fast tags below — if the orchestrator fields more model choices the budget scales to the dispatch's 10-20, keeping ≤2 per distinct model).

### W0 — build + measure (orchestrator only, 0 lanes)

Precondition: F1 (buildingPipe+PBR-VS) and F2 (weather) lanes report landed; pipelinecommon quiescent.
Run §5 in full. Capture the W0 evidence sheet: H1 counters ([zfix2] scene_start violations, rs_mon scene_start DIVERGE count over ≥5 min), N1 split counts (sticky vs replayed receipts), [PedZW]/[BldZW] parked presence, [scene_repair] count, [font_flush] fast/pinned ratio, texalpha summary (no budget=99), pRasFB/camRas dims lines (H5), red-square presence, test12 4/5/7/11/12 + 8/9/10 re-shoots, stable-config run, rotor/straight-flight `camera CUT reset` count, C17 perf note.
**Gate → W1:** build exits 0; all §5 tokens verified; receipts present; evidence sheet written to the wave record.
**Decision outputs:** which W2 conditional leaves open (N3/N4/N6); H1 verdict (killed / residual-writer-named); family-A discriminator (shot 7: TAC-only fix ⇒ A-1 closed, else N1 rule confirmed — wave-14:45).

### W1 — H1 residual + contract completeness + menu/debug cluster (6 lanes)

| Lane | Files (write scope) | Model | Content (§2 refs) |
|---|---|---|---|
| W1-L1★ | src/core/main.cpp | strong | C7 consume-or-prune (read pipeExitAlphaMode src/dst handling FIRST); C14 gate+receipt+F5 relocation; C16+C18 3-layer/span-aware fixes; CoronaWindow_Close completeness (verify current body first) |
| W1-L2 | src/render/pipelinecommon.cpp (+renderstate_monitor.h read-only) | strong | raw-writer census + scene_start divergence attribution receipt; quiescence-gated start |
| W1-L3 | src/core/presets.cpp + src/core/debugmenu_ui.cpp | mid | C2 one-liner; C1 promise/guard/sync; C3 menu off-by-one; C4 ContainsCI; C5 tooltip |
| W1-L4 | src/core/debug_handl.cpp + debug_handl.h | fast | C6 va_start(ap, fmt); C19 diagLevel comment truth |
| W1-L5 | src/core/modeManager.cpp + modeManager.h | mid | C21 ApplyMode lever-rewrite receipt; C19 mobile comment |
| W1-L6 | src/render/postfx_utils.cpp | mid | SaveRawGeomStates/RestoreRawGeomStates canonical-or-verbatim promotion rule (n-registry S1-L4) |

Disjointness: {main} {pipelinecommon} {presets,debugmenu_ui} {debug_handl×2} {modeManager×2} {postfx_utils} ✓. renderstate_monitor.cpp deliberately NOT written (D4 comparator is built and stable — extend only if W0 shows a verdict gap; then it replaces W1-L6 in a re-cut wave, keeping ≤6 lanes).
**Gate → W2:** [zfix2]/rs_mon scene_start → 0 (or attribution receipt names the residual writer → routed in W2); menu preset/save behaviors manually verified; stable-config no-regression; ONE build.

### W2 — INI/config leaf + H5 + conditional N-fixes (≤6 lanes, conditionals open per W0/W1)

| Lane | Files | Model | Content |
|---|---|---|---|
| W2-L1★ | src/core/main.cpp | strong | C3-save active-INI; C9; C10 defaults single-source (stable-config diff per key); C11 clamp direction ×10 sites; C12 sun clamps; C13 section fix; C15; C8 dead-ternary prune |
| W2-L2 | src/render/postfx.cpp + postfx_motionblur.cpp | mid | H5: dims assertion + attribution receipts; s2 INTZ camRas assertion (motionblur flag); S1-L5 quad-bracket depth restore ONLY if W1-L2 attribution named postfx |
| W2-L3 | src/core/weather.h (+weather.cpp comment-only if F2 landed) | fast | C19 stale comments; N7 close-out notes |
| W2-L4 (cond.) | src/render/texalpha.{cpp,h} | strong | N3 soft-glow class admission (opens only if W0 shots 8/9/10-class survive) |
| W2-L5 (cond.) | src/render/grasssys.{cpp,h} | mid | N4 latch/class policy (opens only if shard/streak specimens survive) |
| W2-L6 (cond.) | src/texdefs.cpp | fast | N6 outWhy receipt surfacing (opens only if pink/magenta survives and H5 doesn't claim the red square) |

Disjointness ✓ (texalpha vs grasssys vs texdefs pairwise disjoint; W2-L4 and any N4 texalpha band NEVER share a wave — n-registry S3-L3 collision rule).
**Gate → W3:** H5 verdict documented; save→reload round-trip provable (save under numbered config, reload, diff); conditional-leaf measurements captured; ONE build.

### W3 — N-tails + retirement + gated pipes (≤5 lanes)

| Lane | Files | Model | Content |
|---|---|---|---|
| W3-L1★ | src/core/main.cpp | strong | legacy-arm retirement (decision rule §2 P3; deletion-only) + C20 shield-integrity receipt if not folded earlier |
| W3-L2 (gated) | src/render/buildingPipe.cpp | strong | exit perpetuator sticky-Z (n-registry S1-L6a) ONLY if W1-L2 attribution named it; requires F1 landed |
| W3-L3 (gated) | src/render/vehiclePipe.cpp | mid | mirror of L2 (S1-L6b; cf. clobber-scope audit region per n-registry :4209-4233 — re-verify lines) |
| W3-L4 (gated) | src/render/treeWind.cpp | fast | TreeAlpha ref policy (S4-L3) only if palms survive W0 |
| W3-L5 (gated) | src/render/pipelinecommon.cpp | strong | foliage ambient-floor consumption (S4-L1) only if palms survive; serializes behind W1-L2 |

**Gate → W4:** two consecutive clean retests on record (W0+W1 or W2+W3) for the retirement decision; palms/grass verdicts registered; ONE build.

### W4 — EXTRACTION E1: `src/core/shadows.cpp` (2 lanes; details §4)

W4-L1★ main.cpp + NEW src/core/shadows.cpp/.h (strong, mechanical). W4-L2 verify (fast, read-only + receipt diff). **Gate:** token parity incl. `[shadow_install]`; shadow A/B retest (shadowContractEnable=1 run + stable-config run); ONE build.

### W5 — M6 ripple-sim leaves 1-4 + M9 completion (≤6 lanes; plan doc: 2026-11-07-m6-ripple-sim-plan.md)

| Lane | Files | Model | Content (m6 plan §4) |
|---|---|---|---|
| W5-L1 | NEW src/render/waterSim.{cpp,h} | strong | Leaf 1: ping-pong RTs + API standalone |
| W5-L2 | NEW shaders/vs/WaterSim_VS.hlsl + shaders/ps/WaterSim_PS.hlsl + resources/Resource.rc + resources/resource.h | mid | Leaf 2: kernel shader pair + resource ids (register budget per plan §3: s5 free, c19+ free) |
| W5-L3 | src/render/waterPipe.cpp | strong | Leaf 3: step+bind wiring, OnDeviceReset/SyncDevice forget-list (plan §1.5) |
| W5-L4 | shaders/ps/Water_Parallax.hlsl | mid | Leaf 4: s5 consumption + tier-3 gate + c17.w "M6"→"M3" label fixes (plan §0.1, Water_Parallax.hlsl:173) |
| W5-L5 | src/entities/softbody.cpp + src/extras/ragdoll_death.cpp | strong | M9: softbody rebind to water_swell.h (delete local WaterSwellMakeWave duplication softbody.cpp:201-238 — unify directive) + clock mirror check |
| W5-L6 | (folded into L3) waterPipe.cpp clock | — | M9 single-clock collapse: always-on accumulator ticked in the main loop shared by c11.w upload (waterPipe.cpp:2018) and WaterClockSeconds consumers (softbody.cpp:170-181 KNOWN LIMITATION comment; water-overhaul-spec §4 contract #1) — same file as L3, one lane owns both |

Disjointness ✓; main.cpp NOT touched (leaf 5 deferred to W6). **Gate:** tier<3 bit-identical (plan §5); Reset lifecycle clean; physics still on the analytic law (contract #2); ONE build (+CSO compile for the new pair).

### W6 — M6 leaf 5 (main.cpp★, 1-2 lanes)

W6-L1★ main.cpp: disturbance sources — rain (CWeather__Rain gta.h:178), player (FindPlayerPed + SoftbodyWaterQuery softbody.h:96), boat wakes from `WakeDirectFlush1_hook`/`WakeDirectFlush2_hook` (m6 plan §2.2 anchors main.cpp:7059/:7066/:9559/:9560 — re-verify lines; the wake region lives at :6912-7552 today). **Gate:** rain/boat/player ripples visible; no disturbance → flat → identical surface; ONE build.

### W7 — EXTRACTION E2: corona/sprite window (details §4). W8+ — E3 pedContract, E4 config.cpp, E5 hooks_install.cpp, optional splits. One system per wave, build gate each (§4).

Serialization notes (global): main.cpp★ appears in W1, W2, W3, W4(E1), W6, W7(E2), W8+(E3-E5) — never twice in one wave, never concurrent with an extraction of the same region. pipelinecommon.cpp: W1-L2 then W3-L5 only, each after quiescence checks. texalpha.cpp: W2-L4 only (N4's texalpha band, if ever needed, takes a later wave). buildingPipe.cpp: F1 → W3-L2 → E-waves untouched. weather.*: F2 → W2-L3 comment-only.

---

## §4 Extraction map — `shadows.cpp` first, per-system .cpp end state

**User directive (verbatim):** "we need to write out own shadows.cpp to trim down main.cpp any major system liek that shoudl ahve its own cpp to make it easier to work on the diff system sintepdenetly". Standing directive §4 (standing-directives-2026-11-02.md): "the end state is one file per system".

**Hard rules (every extraction wave):** mechanical moves ONLY — byte-identical bodies (no cleanup, no renames, no behavior edits ride along); ONE system per wave; extraction strictly AFTER that system's behavioral fixes have landed and retested (never during); build gate + binary-token parity after each; premake needs NO edit — per-dir globs already cover every target dir (`premake5.lua:182` `files { "src/core/*.cpp" }`, :190 render, :202 peds, :197 extras); main.cpp is the single-writer ★ lane of its extraction wave.

**Naming reconciliation:** main.cpp's own subsystem banners pre-declare extraction targets — `render/pedContract.cpp` (main.cpp:995), `render/shadowContract.cpp` (:1277), `weather/corona.cpp` (:3310). The user directive says **shadows.cpp**. Decision: follow the USER for the shadow system → **`src/core/shadows.cpp`** (it owns the CShadows hooks; "shadows" matches the game subsystem name); update the banner comment in the extraction commit. For the other systems keep the in-code target names unless the user redirects (they were authored under the same standing directive).

### E1 — `src/core/shadows.cpp` (+ shadows.h) — W4

MOVES TOGETHER (the banner's own list, main.cpp:1331-1340, verified):
1. Region main.cpp:1276-1810: `ShadowContractState` (:1379-1390), `shadowContractCapture` (:1392+), `shadowContractDrift`, `shadowContractForce`, `shadowContractRestore`, `shadowContractRun`, the four hooks `CShadows__UpdateStaticShadows_hook` / `RenderStaticShadows_hook` / `RenderStoredShadows_hook` / `CRealTimeShadowManager__Update_hook` + `*_orig` pointers. Public API per banner :1327-1329: `void ShadowContract_InstallHooks(void);` (rest become static).
2. DllMain shadow install block: call-site intercept 0x53E0BE (expects 0x707F40) + verified entry shields 0x708300 (RenderStaticShadows), **0x70A960 (RenderStoredShadows — the 0x53E0C8 substitute; 0x53E0C8 is SilentPatch-owned and NOT intercepted)**, 0x706AB0 (CRealTimeShadowManager::Update) — receipt `[shadow_install]` (main.cpp:9939-9999 region).
3. Stays put: `shadowContractEnable` Config member (skygfx.h) + its parse line (main.cpp:8168) + `pipeAlphaFuncToD3D` (pipelinecommon.cpp — shared helper, banner :1340).
Address anchors: 0x53E0BE / 0x53E0C8(subst. 0x70A960) / 0x708300 / 0x706AB0 / 0x707F40; labels verified in banner :1345-1352. Verify gate: `[shadow_install]` receipt parity + shadowContractEnable=1 A/B retest.

### E2 — corona/sprite window → per banner `weather/corona.cpp` (suggest `src/render/coronaWindow.cpp`; user call) — W7

Region main.cpp:3309-4869 (~1560 lines, the largest single cluster): corona entry shield + blendprobe hook (0x6FAEC0 install :10149-10217 moves with it), `coronaDrawRawShield`, `CoronaWindow_Close` (:4578), `SpriteFlushState` + capture/close (:3617-3690 region), `spriteEnforceDepthContract` (:3681+ — shared with font/HUD exits; if the census shows non-corona consumers, it moves to pipelinecommon instead — decide at dispatch), font glyph flush hook `CloudRegionFlush_7273D0_hook` (PERF R5 fast path :4504-4539), CPointLights re-arm (:4137 region), producer discrimination + wave-6b flush sites (:4223/:4326 regions), flush-site intercepts 0x70CF62/0x70CF9B install. Banner MOVES list at :3364-3368. **Precondition:** N1/N3 residuals closed (W1-W2) — the CoronaWindow_Close completeness fix (W1-L1) must be IN before the move.

### E3 — ped contract → per banner `render/pedContract.cpp` — after W3 retirement

Region main.cpp:994-1276 (`PedOpaqueState`, capture/diverged/enforce/restore) + `PedRS_Monitor` carve (banner :1041) from the exit block (:2806-2892). The banner's MOVES list (:1044-1053) also names the myDefaultCallback entry/exit blocks — those stay in main.cpp (the callback stays, banner :998-999) UNLESS the W3 retirement deleted the legacy arms, after which the module path (skin.cpp SkinEntryContract et al.) is the only consumer and the pedContract helpers can move to src/peds/ and merge with skin.cpp's contract code (unify directive — decide at dispatch; do not pre-merge in this plan). **Sequencing note:** extracting pedContract BEFORE retiring the legacy arm would freeze the duplication in two files — retirement first.

### E4 — INI parse/save → `src/core/config.cpp` — after W2

Region main.cpp:7552-9440 (~1900 lines): `findInis` (:7493-7521), `readhex`/`readint`/`readfloat` helpers (:7523-7551), section-aware reads (:7552+), `readIniFile` (parse body incl. EMIT default machinery :8560-8743), `readIni` (:8863-8901), `readInis`/`setConfig`/`reloadAllInis` (:8904-8935), `saveConfigTo` (:8938+), `saveConfig` (:9332-9348), `loadConfigFile` (:9350-9358), `MenuWriteMissingIniKeys` adjacency (debugmenu_ui.cpp:3759-3902 stays in the menu file). **Precondition:** W2-L1 config cluster landed (C3/C9-C13/C15) — extraction after behavior, always.

### E5 — hooks install + diagnostics → `src/core/hooks_install.cpp` — after W3/W4

Region main.cpp:9729-10507: PATCH_JUMP byte-verification helpers (:9733+), the InterceptCall battery, shadow install (moves in E1 — coordinate: E1 takes the shadow block, E5 takes the rest), corona install (moves in E2), `InjectDelayedPatches` (:9440-9470 region), `DllMain` (:9765-10452+). DllMain itself may stay in main.cpp with a single `InstallAllHooks()` call — decide at dispatch (banner-less region; keep the move minimal-risk).

### Later optional splits (post-E5, one per wave, only if main.cpp is still >4k lines)

- camera-raster repair + RSMon frame divider (main.cpp:4869-5187) → `src/render/camraster.cpp`
- corona→Forward+ light feed (:5331-5593) → `src/render/coronaLights.cpp`
- CClouds hygiene (:6286-6912) → `src/render/clouds.cpp`
- Im3D/wake flush fixes (:6912-7552) → `src/render/wakeflush.cpp`
- vs_probe vertex-stage diagnostic (:1810-2160) → `src/core/vsprobe.cpp`
- myDefaultCallback itself (the ~1200-line callback, :2160-2898 + default-cb hygiene :2410-2470) stays LAST and only after pedContract/retirement settle — it is the file's structural core.

End state: main.cpp ≈ DllMain + myDefaultCallback + RenderScene glue + install battery ≈ 2.5-3.5k lines, every major system independently diffable — the standing directive's per-system .cpp end state.

---

## §5 Build gate + acceptance (every wave)

Reuse `docs/plans/2026-11-07-build-gate-checklist.md` verbatim as the operator checklist; deltas per wave:

1. **Pre-build:** all wave lanes report landed; dirty-file census (`git status --porcelain`); never-stage list re-checked: `n1`, `qc`, `skygfx_plus_expIV.7z`, `_backup/`, `PLAN.md`, `tools/*tabshell*.ps1`, `tools/*cloudstore*.ps1` (checklist §1; wave-13 §commit-hygiene).
2. **ONE integrated build:** `python tools/fast_build.py` (orchestrator only; lanes never build). Shader validation must pass 100% (was 113/113); **CSO recompile check: `MotionBlur_Burnout.cso` (D8 HLSL changed after the 09:48 build), `Water_Parallax` ×3, `buildingPBRVS` (F1), + any new wave shaders (W5 WaterSim pair → resource.h/Resource.rc ids per m6 plan §3)**. Build exits 0; record bytes + md5 in the checklist §0 table (new row per wave).
3. **Binary token verify (ASCII substrings, `grep -a -o -F`):** `[PedZW]` (main.cpp:2306/:2802), `[BldZW]` (buildingPipe.cpp:2369/:2525/:3316/:3599 — parked/loop-ended forms), `[shadow_install]` (main.cpp:9939+), `[PipeAlpha]` (pipelinecommon.cpp:2241/:2401/:2440), `[zfix]` (pipelinecommon.cpp:1535), `[zfix2]` + `CONTRACT` + `depthct_in` + `cullmode_in` (renderstate_monitor.cpp:522); `[scene_repair]` (pipelinecommon.cpp:1686), `[corona_entry_install]` (main.cpp:10191), `[BldBind]` (F1 lane's token — verify after it lands), `[font_flush]` (main.cpp:4533). Cut/resume: `camera %s reset` + literals `CUT`/`RESUME` present but NOT the contiguous phrases `camera CUT reset`/`camera RESUME reset` (composed at runtime, postfx_motionblur.cpp:337-338 per checklist §3) — confirm composed forms in `skygfx_dbg.log` at runtime, not in the binary.
4. **Deploy:** `E:\games\gtasa_skygfx_plus\skygfx.asi`; confirm size/mtime/md5 on target.
5. **Runtime receipts + test matrix:** `skygfx_dbg.log` shows `[PedZW]` + `[BldZW] parked` (success criterion test12 findings:184); **H1 probes: `[zfix2] CONTRACT site=scene_start` violations → 0 and `rs_mon DIVERGE site=scene_start` → 0 over ≥5 min**; `[PipeAlpha]` sticky-vs-replayed counts recorded; test12 shots **4/5/7/11/12** re-shoot (Glen Park pond + Tahoma + swimming) **+ 8/9/10** for N3; stable-config no-regression run (`docs/baseline/2026-11-02-stable-config/README.md`: pipeline=PBR, buildingPipe=PBR, vehiclePipe=Modern, waterStyle=V, motionBlurEnable=1 — note the live INI now additionally carries the three flipped safety levers, D7); no `camera CUT reset` spam in straight flight (roadmap:94); rotor hover check (N7/D8).
6. **Retest → commit:** user retest passes → stage lanes + wave docs + wave record; commit message carries compliance flags (checklist §7: SADestruction NO-LICENSE must not ship; Bullet zlib vendored). **Never commit:** the §5.1 list. Wave record appended to `.serena/memories/` (NEVER-REVERT append-only rule).

Acceptance vocabulary (unchanged from roadmap:8-9): LANDED = merged, probe pending re-shoot · IN FLIGHT = lane active · OPEN = not started. "Receipts absent = bugs live" remains the standing rule (test12 findings:40-41).

---

## §6 Anchor-drift verification table (duty: ≥15 spot-checks; 38 performed)

Claimed = dispatch/audit/n-registry/checklist line · Actual = live source at this recon (2026-11-09 ~03:00-04:00). ⇄ = file actively moving.

| # | Claimed anchor | Actual | Δ |
|---|---|---|---|
| 1 | presets.cpp:126-141 ApplyPreset skip | :126-141 (comment :126-129) | ✓ exact |
| 2 | debugmenu_ui.cpp:3009-3042 preset pane | :3009-3048 (promise text :3038-3042) | ✓ exact |
| 3 | presets.cpp:5-21 names NULL@14; NUM_PRESETS=15 | :5-21; skygfx.h enum ends :306 | ✓ exact |
| 4 | main.cpp:9288-9302 save | saveConfig() :9332-9348 | +44 ⇄ |
| 5 | main.cpp:8864-8868 / :9411-9415 load | :8866-8868 / :9453-9459 | +2 / +42 ⇄ |
| 6 | debugmenu_ui.cpp:637-650 CurrentIniPath | :637-651 | ✓ exact |
| 7 | debugmenu_ui.cpp:2806-2807/:2861-2863 ContainsCI | :2804-2807 / :2861-2866 | −2 / +3 |
| 8 | debugmenu_ui.cpp:935 coronaZtest tooltip | :934-937 | −1 |
| 9 | debug_handl.cpp:347 va_start(ap,level) | :347 | ✓ exact |
| 10 | main.cpp:1079-1164 PedOpaque blend half | struct :1074-1086, capture :1105-1118, sanitize :1157-1164 | ≈ exact |
| 11 | main.cpp:2628-2632/:2658/:2664/:2401 legacy remnants | :2386/:2401-2407 (dead ternary :2405)/:2804/:2892 | −220..−240 ⇄ |
| 12 | main.cpp:7417 readhex(char*) | :7455-7462 (const twin :7523-7530) | +38 ⇄ |
| 13 | main.cpp:7979-7996 vs :8507-8640 defaults | :8081/:8093/:8353-8356 vs :8560-8588/:8731-8743 | +80..+110 ⇄ |
| 14 | main.cpp:8369-8378 water clamps | :8417-8422 | +48 ⇄ |
| 15 | main.cpp:8332-8335 sun intensities | :8376-8379 | +44 ⇄ |
| 16 | main.cpp:8003-8011 zwriteThreshold | :8047-8057; save :9052-9054 | +44 ⇄ |
| 17 | main.cpp:2987-2995 SetLights memset | :2983-2999; backup_original/src/main.cpp:523-536 identical | ✓ ≈exact |
| 18 | main.cpp:~8827 strncpy asipath | :8868 | +41 ⇄ |
| 19 | main.cpp:2919-2930 CTagManager fallback | :2915-2936 | −4 ⇄ |
| 20 | debugmenu_ui.cpp:4393-4395/:4669-4670 ImGui frame | exact lines — BUT gated at call sites (postfx.cpp:2677-2682; debugmenu_ui.cpp:4803/:4811) | ✓ lines / **verdict MITIGATED** |
| 21 | main.cpp:894-929 grass cull restore | :862-866 patch / :895-896 + :929-930 restores (fn :820) | ≈ exact |
| 22 | debug_handl.h:132-156 stale | :139-156 ("NOT yet wired" — wired at skygfx.h:733, main.cpp:7679) | ✓ |
| 23 | weather.h:21 stale | :17-23 ("both sets"/"BOTH modes" vs weather.cpp:63-65) | ✓ |
| 24 | modeManager.h:41 stale | :41-42 (vs debugmenu_ui.cpp:2927-2932 picker) | ✓ exact |
| 25 | pipelinecommon.cpp:1234 pipeDeviceStateSet | :1234 (header :1198) | ✓ exact |
| 26 | n-registry: pipeExitAlphaMode :2113 | :2259 | +146 ⇄ |
| 27 | n-registry/checklist: [zfix] :1385/:1390 | :1535 | +145 ⇄ |
| 28 | n-registry: pipeEnforceDepthContract :1349 | :1486 | +137 ⇄ |
| 29 | n-registry: RSMon_Sample :598 | :598 | ✓ exact |
| 30 | checklist: [shadow_install] main.cpp:9792 | :9939-9999 | +147 ⇄ |
| 31 | checklist: [PipeAlpha] pipelinecommon:2077 | :2241/:2401/:2440 | +164..+363 ⇄ |
| 32 | checklist: [PedZW] main.cpp:2302 | :2306/:2802 | +4 ⇄ |
| 33 | checklist: [BldZW] buildingPipe:2298 | :2369/:2525/:3316/:3599 | +71.. ⇄(F1) |
| 34 | n-registry: CoronaWindow_Close :4468 | :4578 | +110 ⇄ |
| 35 | ini-reference.md:163/:165/:168 mode-owned rows | exact; spriteStateShield row NOT mode-owned (correction C21) | ✓ |
| 36 | live INI levers | skygfx.ini:142/:177/:209 = 1; [Modes] active=None :5 | ✓ |
| 37 | main.cpp line count "~10516+" | 10516 → 10492 during recon | ⇄ actively moving |
| 38 | m6 plan anchors (waterPipe.cpp:1067/:1075/:2434, Water_Parallax.hlsl:173, main.cpp:7059/:7066) | not re-verified this pass (plan is future-wave; re-verify at W5 dispatch) | deferred |

Pattern: main.cpp anchors drift **+40..+150 lines** vs the 2026-11-08 morning audit (today's landings), pipelinecommon **+137..+363** vs the n-registry recon. Function names + receipt tokens held in every case — the n-registry's durable-anchor rule is confirmed correct and mandatory.

---

## §7 Flags

1. **CONTENDED NOW:** `buildingPipe.cpp`, `weather.cpp` (F1/F2 in flight, mtimes moving during recon). `pipelinecommon.cpp`/`renderstate_monitor.cpp` require the quiescence check (§1.5) before W1-L2. **main.cpp itself moved 10516→10492 mid-recon** — W1-L1★ must re-verify every anchor before editing.
2. **Everything since 09:48 is UNBUILT** — no claim in §1.4 (D1-D3, D5-D8) is runtime-proven until W0. The single highest-value action in this plan is the W0 build+retest, not any new code.
3. **H1 kill is unproven, not unimplemented** — `pipeDeviceStateSet` is silent by design (no own receipt); confirmation rides `[zfix2]`/`rs_mon` scene_start counters → 0. If they persist, W1-L2's attribution receipt names the residual writer before any further code.
4. **C2 is the only crash-class item** (NULL presetNames[14] read by a 15-item ImGui::Combo) and is a one-line fix — it rides W1-L3 and should not wait on anything.
5. **C7 verdict may flip to "prune"** — do not build the exit blend sticky rule before reading `pipeExitAlphaMode`'s current src/dst ownership (unify directive: no second copy of an existing fix).
6. **C14 changes global lighting semantics if un-gated** — the fix is gate+receipt only (default preserves current behavior); any actual memset-policy change needs its own measured wave after F1/F2 land.
7. **Extraction waves are behavior-free by rule** — if a lane "notices" a bug mid-extraction, it registers the finding and moves the bytes anyway; fixes happen in later behavioral waves.
8. **Stable-config paradox to watch:** the baseline README (:34) says the optional repair layers (texAlphaClassify, pedOpaqueContract) "add state churn… do not re-enable by default", yet today's D7 flipped them ON as safety levers and the mode profiles force them to 1. W0's stable-config run must use the LIVE INI (levers ON) and the README must be amended in the W0 wave record if the retest confirms ON is the new stable — do not silently let the baseline doc contradict the deployed config.
9. **Never-commit list** (every wave): `n1`, `qc`, `skygfx_plus_expIV.7z`, `_backup/`, `PLAN.md`, `tools/*tabshell*.ps1`, `tools/*cloudstore*.ps1`; SADestruction must not ship (no license); Bullet zlib license confirmed at packaging.

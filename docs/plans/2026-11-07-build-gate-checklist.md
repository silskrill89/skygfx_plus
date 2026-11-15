# Integrated Build Gate — Operator Checklist (2026-11-07)

One build, run by the orchestrator only. Lanes never build. Run top-to-bottom; tick each box.
Covers this wave (water toggles, MotionBlur Tier 1 HLSL, N2 comparator, peds 3b, M9 swell law,
compliance) plus the designer menu overhaul.

## 0. Baselines

| Artifact | Bytes | md5 | Source |
|---|---|---|---|
| build 11 (2026-11-02) | 2,562,560 | cfe007e7 | `docs/baseline/2026-11-03-test12/findings.md:11` |
| deployed Nov-2 artifact | 2,565,120 | b0c0b050 | wave record |
| this gate build (2026-11-08) | 2,594,816 | 83b8ede723dac9fa856c7585538d5a0f | built 2026-11-08 09:48, orchestrator gate |
| **W0 gate build (2026-11-09)** | **2,602,496** | **5854020218cf222b32670c08aaca4541** | built 2026-11-09, orchestrator gate (N-registry wave 2 + main.cpp campaign) |

## 1. Pre-build

- [x] All lanes landed: water toggles, MotionBlur Tier 1 HLSL, N2 comparator, peds 3b, M9 swell law, compliance.
- [x] Designer menu overhaul merged into the tree.
- [x] Dirty-file census (`git status --porcelain`) — observed 51 paths (50 + this checklist fill); none of these staged:
  - [x] `n1` — gitignored
  - [x] `qc` — gitignored
  - [x] `skygfx_plus_expIV.7z` — gitignored
  - [x] `_backup/` — gitignored
  - [x] `PLAN.md` — gitignored
  - [x] `tools/*tabshell*.ps1`, `tools/*cloudstore*.ps1` — wave-rule never-commit (`docs/plans/effects-menu-overhaul.md:14`, `docs/plans/2026-10-17-systematic-dig.md:25`); tracked & clean here, leave untouched.
- [x] `git check-ignore` per-path on `n1 qc skygfx_plus_expIV.7z _backup PLAN.md` → all 5 ignored (note: `-q` takes a single path).

## 2. ONE integrated build

- [x] Orchestrator ran `python tools/fast_build.py --no-deploy` (shaders + C++). 113/113 shader validation PASS; MotionBlur_Burnout + Water_Parallax ×3 CSOs recompiled; MSBuild 50 pre-existing warnings (C4005/C4805), **0 errors**; 80.24s total.
- [x] Build exits 0; ASI = `E:\games\gtasa_skygfx_plus\skygfx.asi` (post-build targetdir is the game dir — `--no-deploy` only skipped the redundant copy).

## 3. Binary string-verify (ASCII substrings must appear verbatim in the ASI)

- [x] `[PedZW]` — `src/core/main.cpp:2302`, `src/peds/skin.cpp:459`
- [x] `[BldZW]` — `src/render/buildingPipe.cpp:2298` (+ PBR/Switch variants)
- [x] `[shadow_install]` — `src/core/main.cpp:9792`
- [x] `[PipeAlpha]` — `src/render/pipelinecommon.cpp:2077`
- [x] `[zfix]` — `src/render/pipelinecommon.cpp:1385`
- [x] `[zfix2]` — N2 canonical-contract comparator token (renderstate_monitor.cpp; also `CONTRACT`, `depthct_in`, `cullmode_in` verified in binary)
- [x] Cut/resume: binary carries `camera %s reset` + literals `CUT` and `RESUME`
      (`src/render/postfx_motionblur.cpp:337–338`). The full strings
      `camera CUT reset` / `camera RESUME reset` are composed at runtime — confirm them in
      `skygfx_dbg.log` in step 5, not by binary substring.
- [x] Recorded bytes + md5 into the "this gate build" row above.

> Binary check one-liner (git-bash): `grep -a -o -F "[PedZW]" skygfx.asi`

## 4. Deploy

- [x] Copy produced ASI → `E:\games\gtasa_skygfx_plus\skygfx.asi`. (Done by post-build step.)
- [x] Confirm size/mtime on the target: 2,594,816 B, 2026-11-08 09:48, md5 83b8ede723dac9fa856c7585538d5a0f (all 16 binary tokens verified on the deployed file).

## 5. Runtime receipts + test matrix

- [ ] `skygfx_dbg.log` shows `[PedZW]` and `[BldZW]` receipts (success criterion: `docs/baseline/2026-11-03-test12/findings.md:184`).
- [ ] test12 shots 4 / 5 / 7 / 11 / 12 clear (baseline: `docs/baseline/2026-11-03-test12/findings.md`).
- [ ] Stable-config no-regression (`docs/baseline/2026-11-02-stable-config/README.md`): `pipeline=PBR`, `buildingPipe=PBR`, `vehiclePipe=Modern`, `waterStyle=V`, `motionBlurEnable=1`.
- [ ] No new `camera CUT reset` spam during straight flight (`docs/plans/2026-11-02-systematic-roadmap.md:94`).

## 6. User retest → commit

- [ ] User retest passes.
- [ ] Stage lanes + CJ A/B/C wave + 8 root MD docs + this wave; never the census list in step 1.
- [ ] Commit only after retest; commit message carries the step 7 flags.

## 7. Compliance flags (commit message and/or docs)

- [ ] SADestruction — NO LICENSE: must not ship.
- [x] Bullet — zlib LICENSE.txt vendored from upstream (bulletphysics/bullet3), gitignore carve-out added; confirm included in release staging at packaging.

## W0 gate run (2026-11-09) — N-registry wave 2 + main.cpp campaign

- [x] All lanes landed (40+ files): N2 sprite/font depth enforcement, N1 source-kill + ZWRITE repair + pipeDeviceStateSet foreign-writer sync, N3+N6 texalpha/texdefs/particles, N4 grass contract, N5 foliage prelight factor + weather sun, peds head veto fix + dual-pass gate, rotor smear shader, INI levers flipped (texAlphaClassify/spriteStateShield/pedOpaqueContract = 1, mode-profile-protected), 19/20 core-audit fixes, master plan.
- [x] Pre-build census: dirty ~61 paths; never-commit set (n1/qc/7z/_backup/PLAN.md) all gitignored, zero staged.
- [x] `python tools/fast_build.py --no-deploy` — 113/113 shader validation PASS; 2 CSOs recompiled (buildingPBRVS + MotionBlur_Burnout); MSBuild 49.76s, link clean, **0 errors** (C4005 macro-redefinition warnings only); 61.93s total.
- [x] Deployed ASI = `E:\games\gtasa_skygfx_plus\skygfx.asi` 2,602,496 B, md5 5854020218cf222b32670c08aaca4541 (post-build targetdir = game dir).
- [x] Binary verify 18/18 tokens: [PedZW] [BldZW] [shadow_install] [PipeAlpha] [zfix] [zfix2] [SpriteDepthFix] [N5bld] [BldBind] [PedHeadPath] [PedHead] [texdefs] `camera %s reset` CUT RESUME resolved=sticky resolved=replayed FIRST_DIVERGE (`pipeDeviceStateSet` receipt-silent by design — H1 confirmation = `[zfix2]`/rs_mon scene_start counters at runtime).
- [ ] Runtime receipts + test matrix (step 5) — user retest pending. W0 acceptance additions: `[zfix2]`/rs_mon scene_start ≈ 0 (H1 kill proof); `[texdefs] miss` names the red square; `[N5bld]`/`[BldBind]` discriminators fire as expected; sun white at noon; levers stay ON (no `texalpha: DISABLED`, no `contract skipped (INI off)`).
- Note: stable-config baseline README documents the 3 levers as OFF — live config intentionally diverges (wave-15-record.md §2).


## W0.1 gate run (2026-11-09 04:54) â€” main.cpp flush-state perfection

- Build: `python tools/fast_build.py --no-deploy` â€” 0 shaders recompiled (all 113 up-to-date), MSBuild rc=0 (main.cpp recompiled; only pre-existing C4005), 33.16s.
- Identity: `E:\games\gtasa_skygfx_plus\skygfx.asi` **2,602,496 bytes, md5 58b2190be48c7e5de298bc7e3ccc5c0c** (W0 = 2,602,496 / 58540202â€¦ â€” same size = link alignment; md5 changed).
- Backup: `.cache/skygfx.w01gate.asi` (md5-verified identical).
- Binary tokens: 18/18 OK (sole miss `pipeDeviceStateSet` = silent helper by design).
- Landed in this gate: SpriteFlushState ZENABLE/ZFUNC 3-layer capture+replay (covers SpriteFlush_70CF20 + PostFxFlush_701FE9), CloudRegionFlush_7273D0 fast-path enforce (was the 44Ã— zfunc=8 leaker), spriteEnforceDepthContract routed through pipeDeviceStateSet.
- Expected retest deltas: `[SpriteDepthFix]` 50Ã— â†’ ~0; `[PedZW]` 7Ã— â†’ ~0; `scene_repair` ZFUNC/ZTEST terms silent (ALPHABLEND/ZWRITE terms may persist â€” corona-exit alpha block is L1+L2-only by design, flagged residual); vehicle-over-peds verdict = the direct question.

## W0.2 gate run (2026-11-09 14:11) - W0.2 pre-build wave (8 writers) + gate-blocking compile fixes

- Build: `python tools/fast_build.py --no-deploy` - 0 shaders recompiled (all 113 up-to-date), MSBuild incremental rc=0, link clean, 3.98s total after obj cache warm. (First attempt ran a full ~60-TU compile and was killed at the 300s tool timeout mid-compile = no msbuild log write; second surfaced a real compile break.)
- **Gate-blocking fixes landed by orchestrator (both were lane-verification gaps - lanes never compile):**
  1. `src/core/debug_handl.cpp` crashSnapshotModules: SDK 10.0.19041 um/tlhelp32.h declares NO MODULEENTRY32A/Module32FirstA/Module32NextA (C2065/C3861) - switched to MODULEENTRY32W/Module32FirstW/Module32NextW + wchar-to-char ASCII-safe copy.
  2. `build/skygfx.vcxproj`: `..\src\core\shadows.cpp` was MISSING from ClCompile (E1 extraction assumed premake glob auto-pick, but the build reads the pre-generated vcxproj) -> LNK2019 shadows_installHooks. Added the entry after main.cpp.
- Identity: `E:\games\gtasa_skygfx_plus\skygfx.asi` **2,608,640 bytes, md5 80ebc9a9972910fb7e0c78907d31acaa** (post-build targetdir = game dir; this build also RESTORED the ASI after it was found renamed to `skygfx (3).asi_` and missing from the game dir).
- Backup: `.cache/skygfx.w02gate.asi` (md5-verified identical).
- Binary tokens: **29/29 OK** - 18 prior + W0.2 additions: [frame_repair] (frame-end StateBlockEnforceCanonical), [depthdim] (INTZ vs camRas assert), dictfallback/found-in= (texdefs cross-dict sweep), [PedHeadBind]/[PedHeadScan] (chars head discriminator), "boneidx oob" (ragdoll bounds), SESSION-START (debug fortification banner), plus [ShadowHeal]/[shadow_contract]/[PedRS] shadow-receipt persistence after the shadows.cpp move.
- Landed in this gate (8 writers): frame-end StateBlockEnforceCanonical + corona-exit L3 blend pins + diag_dumpStats/diag_removeVEH at DETACH (pipelinecommon/main.cpp); texdefs cross-dict pool sweep with inverted-byteMap bug caught (texdefs); chars head-part diagnostics (chars); DepthHook_AssertDepthDims (depthhook/postfx_motionblur); ragdoll pool-churn liveness gates + BuildGibClump leak-safe order + softbody/water_swell law+clock collapse (sftbdyphsx/softbody/ragdoll_death); debug_handl forensics fortification (module+offset, stack window, instr bytes, crash-safe flush, session banner).
- Expected retest deltas: [scene_repair]/[SpriteDepthFix]/[PedZW] -> ~0 (frame-end assert is the curative lever - H1 kill proof); [texdefs] dictfallback recovers ~31/32 LOD-parent names (slabs/sheets/marker family); floating props/elevated peds gone (ragdoll churn gate); crash free (fortified CRASH line carries mod=+instr= if it recurs); [depthdim] silence = no depth-UV stretch.
- Note: `skygfx (3).asi_` (2,602,496 B, mtime 04:54 = W0.1 binary) remains in game dir as the pre-rename artifact; superseded by this gate build.

## W0.3 gate run (2026-11-09) - junior-rewrite wave (6 lanes): noise-based-variation + motion_fx/wind + main.cpp campaign

- Build: `python tools/fast_build.py --no-deploy` - 113/113 shader validation PASS; 3 `noiseDetailBuildingPS` CSOs recompiled (per-material phase change); MSBuild rc=0 after **1 gate-fix**: `motion_fx.cpp:91` called `NoiseMaterialVariation` unqualified vs `noise.h`'s `namespace SkyNoise` -> `SkyNoise::` qualification (lesson: pin the namespace when pinning cross-lane APIs).
- Identity: `E:\games\gtasa_skygfx_plus\skygfx.asi` **2,614,784 bytes, md5 e463506abd4fe2e5d81479fa02d21366**.
- Backup: `.cache/skygfx.w03gate.asi` (md5-verified identical).
- Binary tokens: **35/35 OK** (`.cache/w03_verify.py`) - 29 prior + [rwfix] / "install OK (6 machines)" (rw_fixes kill-switch install receipt) / "[ragdoll] stale-write skip" / "temp-buffer re-render disabled" / "alpha-ref thresholds" / bld_noisedetail.
- Landed in this gate (6 lanes): SkyNoise::NoiseMaterialVariation + per-material noiseDetail jitter (noise/buildingPipe/shader); motion_fx.cpp parent + wind.cpp child + treeWind slim-down (treeWind 521->229) + MotionFxClockSeconds; main.cpp comment reconciliation + S1 junior water temp-buffer re-render killed (`waterTempBufferHook` default 0) + S2 zwriteThreshold-trio EMIT symmetry; ragdoll `ownsClump` identity-gated writes (S3 TOCTOU closed; verdict: zero-scale cannot make a waist cut); docs sync (CREDITS lineage chain aap->junior->Zeneric->us fixed per user m0210) + project-wide "stochastic"->"noise based variation" rename sweep.
- Expected retest deltas: `[water] temp-buffer re-render disabled` 1x + water still visible (S1 kill proof; if hole persists -> depth/stencil, not S1); `[ragdoll] stale-write skip`/`boneidx oob` = legs discriminator; `[rwfix] install OK (6 machines)` 1x with **`rwFixesEnable=0` bisection lever** for the corona/sprite/font family; dictfallback recovery rate vs 2/32; hole+legs verdict vs docs/plans/2026-11-09-visual-triage.md families. Full watch-list: `.serena/memories/wave-17-record.md`.

## W0.4 gate run (2026-11-09)

- [x] Pre-build census: never-commit set clean (n1, qc, skygfx_plus_expIV.7z, _backup/, PLAN.md all ignored; tabshell/cloudstore ps1 untouched)
- [x] ONE integrated build: `python tools/fast_build.py --no-deploy` â€” 113/113 shaders up-to-date (no shader changes), MSBuild 48.5s, **0 errors** (clean first pass)
- [x] Binary token verify: **39/39** (35 prior via `.cache/w03_verify.py` + NEW `miss-policy`, `skygfx_missgrey`, `skygfx_missclear`, `stateBaselineEnable`)
- [x] Deploy identity: `E:\games\gtasa_skygfx_plus\skygfx.asi` **2,616,320 bytes, md5 516d904c3baac849f7bf298967fe98f8**; backup `.cache/skygfx.w04gate.asi` (md5-verified)
- [ ] Runtime receipts + user retest: see wave-18-record.md watch-list (stateBaselineEnable drop, miss-policy naming, grey/magenta sentinels, VCS parity, hole/legs verdict)

Baselines table row: W0.4 (2026-11-09) | 2,616,320 | 516d904câ€¦ | L1 lever floor + J1/J2, L2 CoronaWindow depth net, L4 texdefs fail-closed, L5 VCS pipeDeviceStateSet, crash Â§8 (not ours)

## W0.5 gate run (2026-11-09, late)

- **Identity**: `E:\games\gtasa_skygfx_plus\skygfx.asi` = **2,616,832 B, md5 bb5d1230aba68348cb7de28f0201f7a3** (backup `.cache/skygfx.w05gate.asi`, md5-verified). 0 MSBuild errors first pass, 113/113 shaders up-to-date, 31.4s.
- **Carries**: config.cpp + render_callbacks.cpp extraction (user order: main.scm/main.cpp cleanup) + R1-R9 redundancy fixes (audit `2026-11-09-main-redundancy-audit.md`) + R4 dead-branch retirement.
- [x] Step 1 pre-build census (never-commit set untouched)
- [x] Step 2 one integrated build (`python tools/fast_build.py --no-deploy`; premake targetdir = game dir)
- [x] Step 3 binary token verify â€” **43/43** (39 prior + `cloud_texraster_passthru` + `cloud_texraster_normal` + 2 receipt-text spot checks)
- [x] Step 4 deploy record (build artifact IS the deploy)
- [ ] Step 5 runtime receipts â€” **user retest pending** (watch-list in wave-19 record)
- [ ] Step 6 commit (gated on retest verdict)
- **LOG RECIPE NOTE**: `dbglog_once("cloud_texraster")` split into `cloud_texraster_passthru` / `cloud_texraster_normal` (R5); recipes matching the old token now hit both â€” distinguish by suffix.

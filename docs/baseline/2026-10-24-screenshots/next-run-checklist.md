# Next-run checklist — after the 2026-10-24 baseline

Use this for the first build after `corona-flush-shield`. The goal is to decide, issue by issue, whether the pending fixes landed and which open issues remain. Reference: [README.md](README.md), [images.md](images.md), [issues.md](issues.md).

## 0. Before launching

- [ ] Record the new `[BUILD]` line from `skygfx_dbg.log`: `asipath`, `size`, `mtime`, `built`. The baseline is **2,317,824 B / built 2026-10-24 21:02:54 / deployed 21:03:40**; if the new numbers are identical, you are not testing a new binary.
- [ ] Note the build tag/label for the run.
- [ ] Rotate or clear `E:\games\gtasa_skygfx_plus\skygfx_dbg.log` so the run is isolated.
- [ ] Confirm `stateBaselineEnable=1`. The `state_baseline` tag is throttled, so a short run with zero lines is not proof the baseline never ran.
- [ ] Record the starting INI values: `waterStyle`, `waterQuality`, `waterParallaxEnable`, `waterBodyClassify`, `waterFresnelSplit`, `texAlphaClassify`, `pedOpaqueContract`, `defaultCbShaderHygiene`, `dualPassPed`, `pipeChainEnable`, `pipeChainReflEnable`, `radiosityEnable`.
- [ ] Keep a copy of the run's `skygfx.ini` next to the log.

## 1. First pass — no INI changes

Capture the same route (or at least the anchor shots):

- **Ocean Docks:** #01–#07 — water veil, CJ absent, flat water.
- **Ganton:** #09–#17 — vehicle corruption, CJ/ped state, shadows.
- **Los Santos:** #18–#23 — shadow blobs, washout, dark horizontal band.
- **Interior:** #24 — partial CJ, debug banner.

Check in this order:

1. **BASE-04 water veil.** Stand on the dock/beach. Does any translucent sheet cover dry land or rise above the horizon? If yes, the clip-space fix did not take; capture `[water] entry`, `[water] chain`, `[water] water_state pre-draw`.
2. **BASE-02 ped opacity.** Close-up CJ/ped in daylight. Are holes/transparency gone? Capture `[PedOpaque] pre-entry`; expect `ALPHAREF=1`, no repeated `contract repaired divergent entry`.
3. **BASE-06 vehicle opacity.** Parked car in daylight. Is the body solid, with no mirror-chrome/dissolve? Capture `[VehOpacity]` and the `texalpha:` summary.
4. **BASE-01 / BASE-03 CJ absent / black peds.** Third-person daylight. Is CJ's mesh + shadow present? Are near peds coloured rather than black silhouettes?
5. **BASE-07 / BASE-08 vehicles.** Panels attached, wheels on the ground, interiors visible through glass?
6. **BASE-09 shadows.** Any giant black pools? If yes, run the `pipeChainEnable=0` A/B.
7. **BASE-16 washout.** Contrast/sky bands. A/B `pipeChainEnable=0`.
8. **BASE-10 / BASE-11 / BASE-14 / BASE-15.** Floating props, double ground, sky streaks, red cubes.
9. **BASE-12 / BASE-13.** Debug banner and minimap labels — record only; cosmetic.

## 2. INI bisect levers

Only after the no-change pass, and only for issues that remain. Change **one key per run**, capture the same shot, then revert. Do not stack levers.

| Lever | Value | What it isolates |
|---|---|---|
| `waterParallaxEnable` | `0` | **Water kill switch.** The hook calls the vanilla `CWaterLevel::RenderAndEmptyRenderBuffer`. If the veil disappears, the water pipe owns it (BASE-04). |
| `waterBodyClassify` | `0` | Per-vertex water body write / c14 sentinel; tests the classifier (BASE-04/05). |
| `waterFresnelSplit` | `0` | Legacy composite; lower alpha / less veil visibility, but geometry stays wrong. Diagnostic only. |
| `texAlphaClassify` | `0` | Disables texture-alpha classification; reverts to the legacy CPU alpha path. Tests BASE-06 and building/vegetation alpha. |
| `pedOpaqueContract` | `0` | Disables the ped opaque contract. Tests BASE-02; if the holes return, the contract is active. |
| `defaultCbShaderHygiene` | `0` | Disables stale VS/PS unbinding in `myDefaultCallback`; tests default-pipeline state leaks (BASE-02/03). |
| `dualPassPed` | `0` | Disables the ped dual-pass path; tests whether the ped body draw owns BASE-01/02. |
| `pipeChainEnable` | `0` | Disables PipeChain; tests black shadow pools (BASE-09) and washout/sky banding (BASE-16). If the live INI already has `0`, use `pipeChainReflEnable=1` instead and compare. |
| `radiosityEnable` | `0` | Disables radiosity; tests the full-screen blocky overlay candidate (BASE-04/16). |

Do **not** raise `waterQuality` to 2/3 to "fix" the veil: that enables the depth/scene copies and changes the path (water-veil diagnosis §6.F).

## 3. Log tags to capture

Grep the run log for these; record counts and the first/last occurrence.

| Tag | Why |
|---|---|
| `[BUILD]` | Exact binary identity. |
| `[PedOpaque] pre-entry` | Ped entry state (`ALPHAREF`, `ALPHAFUNC`, blend, ZWRITE). |
| `[PedOpaque] contract repaired divergent entry` | Contract had to repair a divergent entry; spam means a state leak. |
| `texalpha:` | Per-texture classification and the 2 s summary (opaque/cutout/faded/unknown/faults). |
| `[water] entry` / `[water] chain` / `[water] restore` | Water flush counts, tier, RTs, WVP gate. |
| `[water] water_state pre-draw` | One-shot pre-draw depth state (`zenable`, `zfunc`, `zwrite`, `ds`). |
| `[VehOpacity]` | Vehicle per-mesh `tac` / `blendOn` / `c48w`. |
| `[BldOpacity]` | Building/vegetation per-mesh `tac` / `blendOn`. |
| `CoronaDraw` | Corona/point-light sprite flush blend state. |
| `[CoronaShield]` / `[FlushShield]` | Sprite-flush shield hits (if the corona fix is in the build). |
| `[FxAlpha]` | Cache↔device blend/alpha split; compare `rw(...)` vs `dev(...)`. |
| `state_baseline` | Frame-boundary baseline receipts (throttled; count, not per-frame). |
| `[PipeChain]` | Pass 3 / reflection / classify state. |
| `[BUILDING]/[FxAlpha]` | Non-screen raster / alpha drift lines. |

## 4. Record the result

For each issue checked, add a row to the verification log in [issues.md](issues.md): date, build size/stamp, issue ID, `PASS`/`FAIL`, evidence filename, tester. Then update the issue status. If a new artifact appears, add a new `BASE-xx` ID in the new baseline folder; do not overwrite this baseline.

## 5. Stop conditions

- **Crash before the first shot:** capture the crash address and the last 50 log lines; do not compare visuals.
- **Build identity mismatch:** stop and fix the deploy; a different binary is a different baseline.
- **`waterParallaxEnable=0` removes the veil but the fix build still shows it:** the fix did not land; re-check `src/render/waterPipe.cpp:1117-1121` and the binary stamp.
- **Ped holes disappear but peds are still black:** that is BASE-03 (lighting), not BASE-02 (opacity); record both separately.

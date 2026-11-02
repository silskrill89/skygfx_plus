# Next-Scope Questionnaire — 2026-10-27

Answer inline (short answers are fine). Anything left blank I will decide conservatively and flag it.

## 1. Order of work
The tree currently has **6 landed lanes that have never been built** (neopipe/grasssys merges, blend-mapper dedupe, docs), on top of a deployed ASI that is 2,438,656 B from 18:06.

| Option | Meaning |
|---|---|
| A | Build gate FIRST (validate the 6 lanes), then outdoor fixes, then main.cpp split |
| B | Build gate, then main.cpp split, then outdoor fixes |
| C | Build gate, then run outdoor fixes and main.cpp split in parallel lanes |

**Your answer:** ____

## 2. Outdoor defects — rank what to kill first
From the 18:09 screenshots (shots 3–5 only; shots 1–2 were interiors):

- (a) giant opaque **black quads**, fill exactly `(2,2,2)`, covering 15 → 19 → 24% of frame as the camera pans
- (d) translucent **stepped quads** (hard-edged luminance plateaus) over ground/geometry
- (h) ped **split-exposure** (blown torso `172,165,160` + crushed legs `22,21,19` on the same ped)
- (j-1) stray **red/orange corona dots in daylight** (70–150 px/frame)
- (c) pond/water flat pale blue-grey `(103,119,135)`
- (b) sky clean but **zero clouds** visible
- (i) interiors still show the **yellow marker triangle** + black quads

Put 1–7 in your priority order (or say "as listed"):

**Your answer:** ____

## 3. Mode for the next test round
The 18:09 run was **Xbox** mode (`pipeline=2 bld=1 veh=2 cf=2`) with the colour filter **off** (`colorFilterEnable` pinned 0 by a heal path).

- Keep Xbox as the test target?
- Or test PBR/Modern in parallel so we can tell whether a defect is pipe-specific or global?

**Your answer:** ____

## 4. Colour filter
`cfEnableHeal` currently reports "no recorded user intent — heal is blind", and something silently writes `colorFilterEnable=1` which the heal then reverts to 0. Do you want the filter **on** (grading/tonemap active) for the next test, or stay off while we chase geometry?

**Your answer:** ____

## 5. Shadow contract
`shadowContractEnable` defaults to 0 (vanilla shadow passes) and that gate is what removed the baked-lightmap-on-floor artifact. Stored-shadow hooks are currently unhooked entirely.

- Leave the contract permanently off and rely on vanilla?
- Or re-enable it later as an opt-in diagnostic only?

**Your answer:** ____

## 6. main.cpp split — how aggressive
main.cpp is **8,701 lines**. Candidate extractions: hook/patch install block, corona sprite-state window, ped opaque contract, shadow contract, water hook, config/INI reader, plugin-attach + init chain.

| Option | Risk | Effect |
|---|---|---|
| A | Lowest | Extract **only** the install/patch block + contracts that own no shared state (≈1,500–2,500 lines out) |
| B | Medium | A + split config/INI reader into its own TU (≈1,200 more lines out) |
| C | Highest | Full module carve down to a thin entry shell (≤1,200 lines), matching the refactor blueprint target |

**Your answer:** ____

## 7. Refactor tolerance
A multi-file extraction will produce at least one intermediate state where the tree does not compile until all lanes in that stage land.

- OK to have a short-lived non-compiling window if it buys a cleaner result?
- Or must every lane land compiling on its own?

**Your answer:** ____

## 8. Dead code disposal
Verified dead / superseded:

- `src/render/utilitynoise.cpp` (superseded by `SkyNoise`; shown in the debug Utility tab but never bound to a sampler)
- 6 retired `*_mode.cpp` stubs — already deleted this wave
- stale `RETIRED` decls in `skygfx.h:1571-1592`
- `docs/obsidian/**` still names `diagnostics.cpp` / `normalmap.cpp`

May I delete `utilitynoise.cpp` + its debug-tab wiring (it is the only source of that tab's values)?

**Your answer:** ____

## 9. Further merges
The render refactor review concluded **only one more merge survives**: `postfx_luminance` + `postfx_motionblur` + `postfx_utils` → `postfx_support.cpp` (~970 lines). Everything else was rejected for real reasons (C2702 SEH, `AreaTex.h` 14,980 lines, false families).

- Do that one merge?
- Or stop merging and move to the main.cpp split?

**Your answer:** ____

## 10. Halloween deadline (Oct 31 — 4 days)
What is the **minimum acceptable** end state by then?

- (i) outdoor visual defects gone, refactor partial
- (ii) refactor clean, some visual defects remain
- (iii) both "good enough" with a written known-issues list
- (iv) something else: ____

**Your answer:** ____

## 11. Anything you are seeing that the screenshots/log do NOT capture
(e.g. specific locations, times of day, actions that trigger it, menu settings you changed mid-run)

**Your answer:** ____

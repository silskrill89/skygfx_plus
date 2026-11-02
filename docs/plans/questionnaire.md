# Questionnaire — resolved answers (user deep-dive, consolidated)

Date finalized: 2026-10-01. User answered across several replies; this is the consolidated record.
Items marked **[RESOLVED]** have a root cause and/or landed fix. Items marked **[OPEN]** still need
user confirmation — see the Open Items section at the end.

## A. Water wireframe bug — **[RESOLVED]**

Root cause was a primitive-enum / transform bug, not a water-material bug.

- Primitive enum passed the wrong D3D primitive type → wireframe-looking water. Fixed to
  `D3DPT_TRIANGLELIST` at `src/core/main.cpp:1500`.
- WVP handling: the null/no-transform case plus screen-UV remap produced the streaked look; corrected.
- Guards added: `waterCamUsable` check + SEH around the draw so an unusable camera degrades gracefully
  instead of emitting garbage geometry.

## B. Night black splatter pools — **[RESOLVED]**

Root cause: three-layer alpha/blend desync (pipe layer, effects layer, decal layer each disagreed).

- Fixes: `pipeForceAlphaBlock` + `effectsForceAlphaBlock` re-establish alpha/blend state per layer.
- Decals (the visible pools) are the ground-decals layer of the same alpha path; now consistent.

## C. Vehicle paint corruption (daytime taxi dark patches) — **[RESOLVED]**

Two compounding causes, not a mask/carcols issue.

- `PRELIT=0` caused a shading collapse (unlit surfaces read as dark patches). Fixed via
  `vehPrelightFallback` upload to c28.
- Environment intensity was applied twice ("double-darken"); replaced with an energy-consistent env
  model so reflectance is preserved instead of multiplied down.

## D. Pipe groups & presets — **[RESOLVED]** (preset effect-set detail **[OPEN]**)

- **3 pipe groups:** building / vehicle / pedestrian.
- **5 presets:** PS2, Xbox, PC, IV, Modern.
- **Reference combo** (the current stable target): GTAIV building + Mobile vehicle + GTA IV water.
- Water, grass, shadows, clouds are **separate effect toggles**, not part of the pipe groups.
- **Pedestrian:** repair the vanilla-skin look first; no new PBR ped shader yet.
- **Modern:** the reference combo *upgraded*; must be flawless before anything else ships as final.
- **Legacy:** restore functionality from earlier versions (reference build **[OPEN]** — see below).

## E. Effects toggle inventory — **[RESOLVED]**

- Every effect must be individually toggleable.
- Live toggles via the **Ctrl+4 debug menu (8 tabs)**, plus INI keys for persistence.
- **[OPEN]** whether any effect is engine-level and cannot be toggled — not enumerated.

## F. Config + menu layout — **[RESOLVED]**

- INI is **re3/revc-style**: sections mirror menu pages ([Building], [Vehicle], [Pedestrian],
  [Effects], [PostFX], [Presets]).
- **MenuExtender port** (aScreens `0x8CE008` hooks) is **queued after the pipe restructure**.

## G. Config oddities — **[RESOLVED]**

- `envPower=380.36` (default ≈20) — flagged as user experiment; **needs a clamp** (ceiling **[OPEN]**).
- Negative tonemap values (`MaxExposure=-0.284`, `Min=-0.5`, `CurveLows=-0.548`) — intentional user
  experiment.
- `pipeChainEnable=1` + `intensity=1.0` — noted (code default 0 / 0.3); intentional experiment.
- Out-of-parser-range enums `coronaZtest=2`, `pedShadows=2`, `stencilShadows=2` (parser only understood
  0/1/-1) — now **clamped** by the parser.

## H. User-discovered reasons (goal spec) — **[RESOLVED]**

Water must satisfy all of these simultaneously:
- **Above water:** grazing-angle reflection + steep-angle see-through.
- **Below water:** refraction emphasis up + underwater reflection.
- **Swimmer:** refraction correct on both sides (in/out).

Day transparency:
- Shallow water = see the bottom.
- Deep water = depth-tinted.

Clouds priority:
- Ground visibility first (do not occlude the ground).

Broken alphas observed = ground decals + particles/smoke (routed to the alpha/blend layer, section B).

## Open Items (need user confirmation)

1. **D3 preset detail** — exact pipe ID per group and exact effect set per preset (only the reference
   combo and the Modern super-set are defined).
2. **D5 legacy reference** — which earlier version/build is the legacy baseline and what specifically
   regressed.
3. **E1 engine-level effects** — any effect that structurally cannot be toggled.
4. **G1 envPower clamp** — the target clamp ceiling.

# 2026-10-30 — Sep-3 correlation archaeology: alpha / blend / corona handling

Lane: READ-ONLY git archaeology, no source edits, no build. Written as the report-back of the
Sep-3 → 898e894 + working-tree diff pass requested by the corona-black regression lane.
Read docs/plans/2026-10-21-alpha-reversal-findings.md first (known alpha-reversal family).

## 1. Baseline calibration (what "Sep 3" means in git)

No commit lands between **c270294 (2026-08-30, config reader consolidation)** and
18f5303 (2026-09-07, "checkpoint: PBR restore + dismemberment/ragdoll/depthhook/SMAA").
The visuals deployed on Sep 3 therefore correspond to the **c270294 tree (+ trivial deltas)**;
18f5303 is the first commit AFTER the reference was captured.

Verified immobile at c270294:
- `src/render/pipelinecommon.cpp` contains **zero** alpha/blend/zwrite render-state logic
  (only SMAA PS handles + c23 layout). The entire alpha doctrine is post-Sep-3.
- `src/core/main.cpp` corona surface = `coronaZtest` dispatch (main.cpp:268) — config-only,
  default `-1` "don't touch", bytes only written when explicitly configured. `coronaZtest`
  introduced 4f34628 (Aug 27) / d1f0761 (Aug 8) era — pre-baseline, NOT a suspect.
- No corona shields, no forced alpha-test canonicalization, no `effectsForceAlphaBlock`,
  no `0x70CF20` work, no `texalpha.cpp`.

## 2. Commit touch-map for the four tracked files (c270294..898e894)

| commit | date | files in range | alpha/blend/corona relevance |
|---|---|---|---|
| 18f5303 | 09-07 | main.cpp | only a "+// Sun corona" comment — inert |
| 94dcd37 | 09-09 | main/pipelinecommon/postfx/skygfx.h | PS light constants + glossy/glass lux |
| e39a6f8 | 09-10 | main/skygfx.h | modular PBR vehicle layers |
| 191dbe7 | 09-10 | main/pipelinecommon/postfx/skygfx.h | tonemap + env — shader-side |
| ca922ef | 09-11 | main/skygfx.h | hemisphere ambient — lighting |
| 671bcd7 | 09-11 | postfx | tonemap toe fix — shader-side |
| 59bd64f | 09-15 | all 4 | envmap cleanup + `DrawEnvMapCoronas` TEXTURERASTER leak fix (raster, not blend) |
| aac8478 / 95cf544 | 09-15/22 | postfx | SMAA Im2D SEH guards (state-leak **window** class) |
| **b7396c4** | **10-02** | **all 4** | **"render-state integrity" swarm rounds 1–3 — THE pivot** |
| docs ×3 | 10-25 | — | docs only |

`git log -S` (pickaxe) results:
- `pipeEnterAlphaMode`, `setAlphaTestTwoLayer`, `effectsForceAlphaBlock`, `RestoreRawGeomStates`,
  raw `RwD3D9SetRenderState(D3DRS_ZWRITEENABLE …)` expansion in postfx.cpp → **all b7396c4 only**.
- `coronaDrawRawShield`, `0x70CF20`, the CCoronas::Render blend-probe hook, `CoronaWindow_Close` →
  **never committed; uncommitted working tree only**.
- `hasAlpha` / `instData->vertexAlpha` whole-mesh logic → b680e31 (08-03), **pre-baseline, constant
  across Sep 3 → HEAD (not the regression differentiator)**.

Transient raw-ZWRITE note: postfx.cpp already used
`RwD3D9SetRenderState(D3DRS_ZWRITEENABLE…)` in **64adba9 (08-08, SMAA)** — present in the good
Sep-3 build, so raw ZWRITE writes per se are not the differentiator. b7396c4 multiplied them and
added the restore bundle (898e894 postfx.cpp:2660–2718).

## 3. Mechanism-by-mechanism history

### 3.1 RWALPHATEST / alpha-test function
- **c270294 (Sep 3 look):** per-pipes legacy writes only — main.cpp grass dual pass
  (`zwriteThresholdGrass`) and vehicle dual pass set
  `rwRENDERSTATEALPHATESTFUNCTION` = GREATEREQUAL / LESS / preserved *directly via L1*; device
  (L2/L3) untouched; no game-wide forcing. Alpha-test state was essentially the game's own.
- **b7396c4 (Oct 2):** introduces `pipeEnterAlphaMode` (pipelinecommon.cpp, now :1527) —
  game-wide entry contract: blend trio canonical L1+L2+flush+L3, `ALPHATESTENABLE=TRUE`
  forced, `ALPHAFUNC` = preserved rw func (game parks GREATER), **ref==0 → 1 bump**; adds
  `pipeAlphaFuncToD3D` (:1331-1334 area, single conversion point) and `pipeForceAlphaBlock`
  (buildingPipe.cpp:406 area) for standalone callers.
- **Uncommitted:** `setAlphaTestTwoLayer` exported to main.cpp call sites (grass/vehicle
  dual passes re-routed); postfx.cpp:5048 `RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE)` and
  analogous L3 pins in ~a dozen pass blocks (see Objective findings — posttx ALPHATEST=FALSE sites
  pre-date, the TRUE pins are new).

### 3.2 Blend-mode enums / factors
- **c270294:** no L2/L3 blend writes in pipelinecommon at all. postfx legacy blend writes
  (BLENDOP=REVSUBTRACT postfx.cpp:814-era, replace-multiply SRCBLEND=ZERO/DESTBLEND=SRCCOLOR at
  :2833-2835 etc.) existed pre-baseline (they are `-` context in the range diff).
- **b7396c4:** adds `pipeBlendToD3D` (rw 1..11 ↔ D3D mapping, note rwBLENDNABLEND alias fix),
  the 3-layer canonical trio + flush, and `effectsForceAlphaBlock` (main.cpp:4018) — which at
  b7396c4 time **rewrote any observed ONE/ONE (2/2) pair to SRCALPHA/INVSRCALPHA**; that sweep is
  exactly the additive-pair inconsistency documented Oct-21 (shield forces SRCALPHA/ONE while the
  vanilla corona contract — gta-reversed Coronas.cpp:195/382/536, Clouds.cpp:706 — is ONE/ONE).
- **Uncommitted:** producer gate `g_inCoronasRender || g_inCloudsRender || s_spriteForceValid`
  added BEFORE any body write (skips the whole sweep during a live additive batch);
  substitution now only cleans genuine non-sprite additive leaks (`src/core/main.cpp:4116-4137`);
  `pipeRwParkSet`/`pipeL2ParkSet`/`pipeL3Set` (pipelinecommon.cpp:1135-1150) park-AND-SET idiom:
  opposite value through L1+L2 first, then target — used at pipelinecommon.cpp:1265-1287 with a
  **reversed pair** (SRCBLEND INVSRCALPHA / DESTBLEND SRCALPHA, BLENDOP REVSUBTRACT) — these
  reversed restores are deliberate "cache-transition guarantee" writes, not inversions, but they
  are new-idiom surface area.

### 3.3 Z-WRITE for translucent batches
- **c270294:** ~35 `rwRENDERSTATEZWRITEENABLE` toggles in postfx.cpp via **L1 (rw cache)** —
  the RW driver itself flushes on the next transition, so a device/rw-cache desync could not persist:
  cache and device were driven by the same state machine.
- **b7396c4:** adds raw bypass-cache writes (84 `RwD3D9SetRenderState(D3DRS_ZWRITEENABLE…)` in the
  range diff; 52 at 898e894), `RestoreRawGeomStates` (L2 bundle postfx.cpp:2660-2718), and the
  three-layer entry ZWRITE guard mirrored to vehiclePipe.cpp:2570. This is the machinery behind
  the "ZWRITE off/desynced on entry" repairs (19/43/26 per run, Oct-21 log).
- **Uncommitted:** `coronaDrawRawShield` (main.cpp:2928-2981) now **forces device ZWRITEENABLE=FALSE
  on 3 layers per corona draw** (park TRUE → set FALSE), sprung at every draw inside the corona
  window including the SilentPatch-deferred 0x70CF20 flush (:3352/:3356); the pre-window value is
  only re-pinned at the hook exit / `CoronaWindow_Close` (main.cpp:3483, called from RenderEffects
  and postfx exit :4321/:4350). Anything drawn between a corona draw and the close inherits
  device ZWRITE=FALSE + rw-cache TRUE.

### 3.4 Corona shield (TempBuffer batch flush 0x70CF20)
- **b7396c4 (committed v1):** entry "normalize" block (Oct-21 findings: main.cpp:2541-2544) ran
  **once per window, never restored**: `ALPHAFUNC=GREATEREQUAL / ALPHAREF=0 /
  SEPARATEALPHABLENDENABLE=0 / COLORWRITEENABLE=0xF`; exit restore **hard-set
  ALPHABLENDENABLE=FALSE (REPLACE)**. Identified Oct-21 as candidate #1 (no alpha cut + broken
  blend signature `src=2 dst=2 blend=0` = ONE/ONE-disabled → opaque black square) and Oct-29
  working tree's own comment now labels that exit "what manufactured the next frame's blend-off
  black quads" (src/core/main.cpp:2879-2883).
- **Uncommitted (v2, live):** `g_coronaPre*` POD captures at the CCoronas::Render probe entry
  (:2768, 0x6FAEC0), per-draw `coronaDrawRawShield` (park-and-set blend→ONE/ONE default,
  ZWRITE→FALSE, per-draw GE@0 + mask + separate-alpha normalizes :2993-3003), flush-hijack of
  0x70CF20 (main.cpp:3255-3356: 0x6FB621 tail-jmp cut to plain RET so SilentPatch's deferred flush
  routes through the patched helper), `CoronaWindow_Close` restore, `effectsForceAlphaBlock`
  producer gate. Flag surface (skygfx.h:923/:932/:939 + main.cpp:6687-6705): `spriteStateShield`,
  `coronaBlendShield`, `effectsAlphaBlock`, `stateBaselineEnable`, `defaultCbShaderHygiene` —
  all default 1 (bisection levers exist for every one of them).

### 3.5 texAlphaClassify
- **Wholely uncommitted** (`src/render/texalpha.cpp`, 954 ln, + `texdefs.cpp`). Confirmed input
  to the PBR building callback (buildingPipe.cpp:1902, :2702) and vehicle Env cb
  (vehiclePipe.cpp:2712-2736). It documents the see-through mechanism explicitly: "PBR pixel
  shaders alpha-test with GREATER-EQUAL ref 1 … DXT3 empty alpha blocks decode tex.a ~ 0 …
  discards the whole surface". All its thresholds (FADED 60%, CUTOUT mid <2%, zero-share window
  1–85%) are tunings of previous REJECTED classifiers — i.e. this file is a *treatment*, layered
  on top of the disease; every classifier iteration predating it is also uncommitted.

## 4. What could black coronas AND hollow solids in ONE change

Shared flaw family — "forced alpha semantics landing on top of texture-authored intent":
1. **Force alpha-test ON with a ref/fun the texture doesn't author** → solid meshes with unused
   alpha channels are alpha-tested away (see-through faces, holes in walls) — texalpha.cpp:4-16
   documents it as the CURRENT failure being treated; PipeChain.hlsl classify echo (:237-247
   comment) proves the same mechanism on the classify re-draw.
2. **Force an additive-window into OTHER pipes' blend expectations (or the inverse)** → coronas
   (black-RGB-surround + opaque-alpha textures) drawn under a wrong or inherited pair light up as
   black squares (Oct-21 log: `src=2 dst=2 blend=0` ×2 caught by the shield) — b7396c4's
   entry canonicalization + effects sweeps are the first machinery to ever write blend state into
   the corona/participants' shared device state.

Both symptoms point at the same switch: **somewhere the pipeline began WRITING canonical
alpha/blend device state instead of observing the game's**, i.e. b7396c4 + its uncommitted
continuation. The shield artifacts (per-draw GE@0, mask normalizes, park-and-set ZWRITE=FALSE)
are v2 repairs whose leaks are each just as capable of blackening the NEXT consumer as the
original v1 exit REPLACE was.

## 5. Ranked suspect list (commit + file:lines + mechanism)

**S1 — b7396c4 (2026-10-02) "render-state integrity" — FIRST-ONSET commit, explains BOTH families.**
- What it changed: introduced every "canonical" force in existence —
  `pipeEnterAlphaMode` (src/render/pipelinecommon.cpp, introduced here; forced
  ALPHATESTENABLE=TRUE + blend trio + ref==0→1 bump), `setAlphaTestTwoLayer`, 3-layer
  save/restore mirrors in buildingPipe/vehiclePipe/neoWaterdrops/neoCarpipe, raw
  ZWRITE/ALPHATEST/CULL restore bundles postfx.cpp (`RestoreRawGeomStates` :2660-2718),
  `effectsForceAlphaBlock` (src/core/main.cpp) with the ONE/ONE→5/6 additive substitution sweep,
  corona entry normalize (GREATEREQUAL/ALPHAREF=0/mask, once per window, never restored) and
  corona exit hard ALPHABLENDENABLE=FALSE.
- Corona mechanism (committed v1, later self-admitted in uncommitted comments
  main.cpp:2879-2883): exit REPLACE + per-window GE@0 leaking into the rest of frame ⇒
  blend-off black quads on the NEXT frame's corona draws — exactly the `blend=0` first-hit
  observations in the 18:19 log.
- Solids mechanism: entry contract forces alpha-test semantics + blend baseline over pipes whose
  shader output alpha is not authored for it (empty DXT3/DXT5 blocks ≈ 0) ⇒ whole-surface
  discards / whole-mesh blending (Oct-21 finding candidates 3+4) — the exact hole texalpha.cpp
  and the vehicle paint-marker fix (vehiclePipe.cpp:2706-2734) are still patching around.
- Verdict: **b7396c4 is the single committed change after Sep 3 whose switch can produce both
  `corona-black` and `solid-see-through`, because it is the only commit that changed WHO OWNS the
  alpha/blend device state.** Everything after it (incl. the entire uncommitted tree) is
  remediation/rework of b7396c4's ownership flip.

**S2 — uncommitted corona shield v2 (Oct 24-29) — CURRENT-STATE defect surface.**
- src/core/main.cpp:2928-2981 (`coronaDrawRawShield`): forces ALPHABLENDENABLE=TRUE on 3 layers
  per corona draw — if the corona/pointlight texture is authored premultiplied
  (SRCALPHA-intent, the translucent-yellow caps look), forcing default ONE/ONE
  (main.cpp:2907-2915 claim) double-multiplies and can re-derive the black-quad look; per-draw
  GE@0 (:2994-2997) removes any authored cut-out.
- src/core/main.cpp:2976-2980: device ZWRITEENABLE=FALSE forced at every corona draw and only
  restored at the flush/hook exit (`CoronaWindow_Close` :3483, call sites :4321/:4350) — a
  protective repair chain, not a contract: the SEH/fault or a missed close leaves device ZWRITE
  off while the rw cache claims TRUE (same desync genus as Oct-21 "ROOT EVIDENCE"), and
  octree/tree-batch drawing after coronas inherits no z-write → stack-through translucent solids.
- The three shield flags (`coronaBlendShield`, `spriteStateShield`, `effectsAlphaBlock`) are
  the clean INI bisectors to prove S1-vs-S2 membership: switch them off one at a time.

**S3 — 95cf544 (2026-09-22) + aac8478/d0886dd (09-15/19) — SMAA SEH containment windows.**
- Guarded Im2D dispatches leave whatever device state the fault window had; d0886dd added
  VEH pass-through precisely because entities inside the window got a torn state; documented as
  the "stale-Im2D override" class that `effectsForceAlphaBlock` sweeps. Stationary; primary
  fault-inducing for the per-frame intermittent blend signatures but not the ownership flip.

**S4 — 59bd64f (2026-09-15) — envmap/DrawEnvMapCoronas TEXTURERASTER leak fix.**
- Raster leak class, not blend; mobile only for monitoring matrix completeness.

**S5 — shader-only commits 94dcd37/e39a6f8/191dbe7/ca922ef/671bcd7 (Sep 9-11).**
- Changed PS output alpha only indirectly (`main()` returns raw tex.a, Oct-21 audit item "main()
  :776 returns RAW tex.a"; `main_building` = saturate(diff.a*IN.color.a)). They do not touch
  blend state; they modulate which alpha VALUE reaches the forced-alpha machinery introduced in S1. Secondary.

## 6. Verdict on "one single commit?"

**Yes — b7396c4 (Oct 2).** It is the only commit in the Sep-3 → HEAD window that changed the
alpha/blend OWNERSHIP contract (game-rw-ledger arbitrates to *canonical forced device writes*),
and both observed symptoms are the canonical forced-write faults documented on Oct 21:
- corona black = additive window entered with a device state the contract dictated vs. the
  authored ONE/ONE pair (entry normalize GE@0 + exit REPLACE, src=2 dst=2 blend=0 in the log);
- solids see-through = alpha-test/blend canonicalization applied to textures with unused alpha
  channels (GREATER@1-style discards) + whole-mesh `vertexAlpha` blend riders that lane only
  processes with the new alpha-test/baseline blocking.
Every post-898e894 file state (the Oct 29 build) is uncommitted re-mitigation of that single
flip, and its scaffolding (texalpha.cpp classifier, paint-marker alpha==16 carve-out, corona
shield park-and-set) exists precisely because the underlying ownership model still disagrees
with the textures' authored alpha intent.

Open follow-ups (NOT in this lane's scope): INI bisect run with
`coronaBlendShield=0` → `spriteStateShield=0` → `effectsAlphaBlock=0` → `stateBaselineEnable=0`
(modeManager profiles all pin 1) to attribute S1/S2 empirically per mode.

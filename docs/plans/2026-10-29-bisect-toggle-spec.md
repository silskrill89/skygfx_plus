# 2026-10-29 - Renderer-wide corruption bisect toggle spec (m0970)

Status: READ-ONLY diagnosis. No source edited. Deliverable = master toggle table, bisect order,
and the fixer's missing-row / missing-key list.

Live INI: `E:/games/gtasa_skygfx_plus/skygfx.ini` (`[Modes] active = Xbox`).
Sources as-read: `src/core/main.cpp`, `src/core/modeManager.cpp` (kValuesXBOX :206-308, 81 rows),
`src/core/debugmenu_ui.cpp`, `src/render/{pipelinecommon,buildingPipe,vehiclePipe,waterPipe,postfx,depthhook,forwardplus,texalpha,mirrorPass}.cpp`, `src/skygfx.h`.
Cross-check: `docs/plans/2026-10-29-object-corruption-diagnosis.md` (R1..R6),
`docs/plans/2026-10-25-state-leak-audit.md` (leak census), `2026-10-27-stages-0-3-checklist.md`.

---

## 0. The two facts that invalidate naive INI bisecting

1. **ApplyMode overrides the 81 profile-owned fields at every startup.** `main.cpp:7052-7059`:
   every top-level key is parsed first, then `ApplyMode(iniMode)` runs - "profile-owned fields
   now win over the INI". Editing any of the 81 `kValuesXBOX` keys in the INI is a no-op while
   `active=Xbox`. The only live lever for owned keys is the **in-game menu** (writes `config->`
   directly; consumers read it next frame). Session-only: relaunch re-overrides.
2. **The live INI does NOT describe the effective config.** Profile overrides vs INI (the deltas
   that matter most, all forced at startup):

   | Key | INI value (what the user sees) | LIVE under Xbox (profile) | Effect |
   |---|---|---|---|
   | **waterParallaxEnable** | **0** | **1** | **Custom water pipe ON - user believes it is OFF** |
   | dualPass | 0 | 1 | Two-layer alpha ON globally |
   | dualPassBuilding / Grass / Default | 0 | 1 | ON for buildings/grass/default pipe |
   | dualPassPed | 1 | 1 | ON (agrees) |
   | dualPassVehicle | 0 | 0 | OFF (agrees) |
   | doRadiosity | 0 | 1 | Game flag `m_bRadiosity` set (main.cpp:338); SkyGFX pass still blocked by non-owned `radiosityEnable=0` |
   | usePCTimecyc | 0 | 1 | PC timecycle |
   | colorFilterEnable | 0 | 1 | Colour filter chain runs (PC filter) |
   | forwardPlusEnable | **1** | **0** | Forward+ OFF despite INI 1 |
   | normalBufferEnable | **1** | **0** | Normal buffer OFF despite INI 1 |
   | pipeChainEnable | 0 | 0 | OFF (agrees) |
   | stateBaseline / pedOpaqueContract / texAlphaClassify / defaultCbShaderHygiene | 1 | 1 | All four safety systems ON |
   | coronaZtest | 1 | 1 | Z-test forced ON per corona draw |

   Non-owned keys keep INI values: velocityBufferEnable=1, cloudRenderFix=1, spriteStateShield=1,
   waterBodyClassify=1, pedAlphaTestEnable=1, pedCullMode=-1, ambientFloor=0.03, pbrAmbientFloor=0.05,
   pedAmbientFloor=0.20, shadowContractEnable=0, mirrorDistortionFix=0, bldEnvReflect=0,
   pipeChainReflEnable=0, forwardPlusFallback=1 (read once, forwardplus.cpp:248), fixShadows=0.

**Consequence:** "loading Xbox turned it on and messed it up" = the profile force-enables
waterParallaxEnable + the dualPass family + doRadiosity/usePCTimecyc/colorFilterEnable, and force-
disables forwardPlus/normalBuffer, on top of an INI the user believes reflects the run.

---

## 1. Master toggle table (active=Xbox)

Legend Menu: Main = Main pane row; SI = Main>State integrity group; W = Water tab;
**MISSING** = no writable row anywhere (dedicated or All-Settings). Bisect priority: 1 = flip first.
Evidence: `29-OCD` = 2026-10-29 object-corruption-diagnosis, `25-SLA` = 2026-10-25 state-leak-audit.

### 1A. ACTIVE gates that write raw render state / run extra passes (bisect targets)

| # | INI key | Default (reader) | Live under Xbox | Menu | What it gates (render-state effect) | Pri | Suspect evidence |
|---|---|---|---|---|---|---|---|
| 1 | waterParallaxEnable | 1 (main.cpp:6648); XBOX owns 1 | **1 (forced vs INI 0)** | W master + Main | Custom water pipe master (waterPipe.cpp:1111); every bail path falls into vanilla Im3D draw **without VS/PS unbind** in the deployed ASI | **1** | **29-OCD R1 (78%)**: stale-VS/PS fall-through = giant teal/grey quads over all geometry; 29-OCD R2: every mid-setup bail shares the hole; 25-SLA #10: water hardcoded blend/fog restore |
| 2 | dualPass (global) | 0 (main.cpp:6060); XBOX owns 1 | **1 (forced vs INI 0)** | Main + detail 805-809 | Two-layer alpha: `setAlphaTestTwoLayer` writes ALPHAFUNC/ALPHAREF/ZWRITE per dual draw (main.cpp:270-286); amplifies every layer-2 leak across the default pipe | **2** | 25-SLA root class: ALPHAFUNC/REF covered by neither shared restore helper; runs on nearly every draw under Xbox |
| 3 | dualPassBuilding / Grass / Default / Ped | default->global; XBOX owns 1/1/1/1 | all 1 | Main detail 805-809 | Per-surface two-layer legs: buildings (buildingPipe.cpp:1467/2104/3019), grass (main.cpp:795), default pipe (main.cpp:1757), peds (main.cpp:1808 + :2120-2186) | **2** | Same as #2; ped leg is unconditional whenever dualPassPed && zwriteThresholdPed>0 |
| 4 | stateBaselineEnable | 1 (main.cpp:6428); XBOX owns 1 | 1 | SI | `pipeApplyWorldBaseline` canonical world state at 4 frame boundaries (main.cpp:3756/4272/4301/7578; pipelinecommon.cpp:2038) | 3 | 25-SLA finding #1: baseline is overwritten by stale corona capture every frame - turning it OFF isolates baseline-vs-stale-capture race |
| 5 | texAlphaClassify | 1 (main.cpp:6425); XBOX owns 1 | 1 | SI | Per-material texture-alpha classification feeding building (buildingPipe.cpp:1902/2702), vehicle (:2736), ped (texalpha.cpp:792) alpha paths - affects ALL THREE pipes' blending/cutout | 4 | Matches "all three pipes weird" signature; texalpha-unknown doc 2026-10-25 |
| 6 | cloudRenderFix | 1 (main.cpp:6259, global bool) | 1 | SI | CClouds::Render hygiene hook (main.cpp:5062-5139): forces filter/alpha-test/fog on cloud+corona draws; skips whole body at 0 | 5 | 25-SLA #7: hygiene missing entry forces CULLMODE/COLORWRITEENABLE/ALPHATESTENABLE + clobbers s5 (Forward+ tile) - first FFP consumer of the frame |
| 7 | spriteStateShield | 1 (main.cpp:6431) | 1 | **MISSING** | Per-corona-draw 4-state normalisation ALPHAFUNC/ALPHAREF/SEPARATEALPHA/COLORWRITE (main.cpp:2982-2984, inside coronaDrawRawShield) | 6 | 29-OCD R3 cleared as slab source but "extend shield" still open; 25-SLA 3.1: corona shield restores 3 of 9 states on the deferred path |
| 8 | velocityBufferEnable | 1 (main.cpp:6397) | 1 (non-owned, INI) | Main | Per-frame velocity RT pass + INTZ prev-depth sampling (postfx_motionblur.cpp:34/193; postfx.cpp:4225/4319) even with motionBlur off (VelocityReconstruct consumers) | 7 | 25-SLA: DrawVelocityBuffer RESTORED, but adds a raw INTZ pass every frame - cheap bisectable cut |
| 9 | depthHookEnable | 1 (main.cpp:6400); XBOX owns 1 | 1 | Main | D3D depth-hook (INTZ surface swap, depthhook.cpp:475) - substrate for velocity/normal buffers, SSAO, underwater depth | 8 | 29-OCD R6 underwater white blowout uses depthRT translucency; INTZ bind/unbind is a cache/device split (25-SLA 2.4) |
| 10 | pedOpaqueContract | 1 (main.cpp:6447); XBOX owns 1 | 1 | **MISSING** | Ped opaque state contract: capture/enforce/restore of 7 states every ped span (main.cpp:1862-1984; repaired 86x/frame in 29-OCD log) | 9 | 29-OCD section 4 called the 86 repairs "normal", but it is a per-frame state writer - bisect it |
| 11 | defaultCbShaderHygiene | 1 (main.cpp:6442); XBOX owns 1 | 1 | SI | Unbinds stale VS/PS in myDefaultCallback (main.cpp:2002; 25-SLA: UNRESTORED by design) | 10 | Same failure CLASS as 29-OCD R1 (stale shader projection); flip last - disabling hygiene can ADD slabs |
| 12 | coronaZtest | -1 (main.cpp:6357); XBOX owns 1 | 1 | Main (enum) | Forces D3D Z-test enable on corona draws (main.cpp:4530: ztTarget = coronaZtest==0 ? FALSE : TRUE) | 11 | Render-state write per corona; 29-OCD counted 312 blend receipts/frame - high-frequency writer |
| 13 | pedAlphaTestEnable | 1 (main.cpp:6457) | 1 (non-owned) | **MISSING** | Ped canonical cutout test GREATEREQUAL@1 vs off (main.cpp:1863/1984) | 12 | Ped-pipe alpha class; purpose-built A/B lever |
| 14 | pedCullMode | -1 (main.cpp:6453) | -1 (non-owned) | **MISSING** | Forced CULLMODE on peds when != -1 (main.cpp:1888-1916) | 13 | Blast-A class if it leaks (cull); default is pass-through - only flip 1->2->3 to test |
| 15 | waterBodyClassify | 1 (main.cpp:6660) | 1 (non-owned) | W tab | Water flush classify gate `WbClassifyFlush` (waterPipe.cpp:1195); 29-OCD Patch A guard lives beside it | 14 | 29-OCD: the unsane 621v/3432i flush is exactly what this gate must catch |
| 16 | colorFilterEnable | 1; XBOX owns 1 | 1 | Main/PostFX | Colour-filter pass runs (postfx.cpp:1906; filter paths flagged PARTIAL in 25-SLA 2.13) | 15 | Look-class, but postfx passes leak TEXTURERASTER (25-SLA root class 3) |
| 17 | usePCTimecyc | 0; XBOX owns 1 | 1 (forced vs INI 0) | Main | PC timecycle data path (postfx.cpp:1764 pipeline branch) | 16 | Lighting-look only; cheap negative test |
| 18 | ambientFloor / pbrAmbientFloor / pedAmbientFloor | 0.03 / INI 0.05 / 0.20 | as INI | Main x2; **pedAmbientFloor MISSING** | Ambient floors at light upload (pipelinecommon.cpp:162/209; main.cpp:2082) - constants only, no state writes | 17 | Look-only; test only if corruption is "wrong shading", not geometry |

### 1B. Gates already OFF under the live config (no bisect step needed; listed for completeness)

| Key | Live | Why off | Menu |
|---|---|---|---|
| pipeChainEnable / pipeChainReflEnable | 0 / 0 | XBOX owns 0; INI 0 | Main / reflection row |
| forwardPlusEnable (+ forwardPlusFallback) | 0 (INI 1 ignored) | XBOX owns 0; fallback inert | Main; fallback read-only row 1332 |
| normalBufferEnable | 0 (INI 1 ignored) | XBOX owns 0 | Main |
| shadowContractEnable | 0 | non-owned, INI 0 | **MISSING** |
| mirrorDistortionFix | 0 | non-owned, INI 0 (29-OCD R4 cleared) | Water tab |
| bldEnvReflect | 0 | non-owned, INI 0 | Building tab |
| ssao/smaa/motionBlur/bloom/dof/sunShafts/fxaa/sssPost | 0 | XBOX owns 0 | Main/PostFX |
| radiosityEnable | 0 | non-owned, INI 0 - blocks SkyGFX radiosity pass (postfx.cpp:1243) despite doRadiosity=1. doRadiosity also sets game flag `m_bRadiosity` (main.cpp:338); menu rows exist for both - only relevant if radiosityEnable ever seen =1 | Radiosity tab |
| fixShadows | 0 | global int, INI 0 | debug row |

### 1C. Other render-state-relevant config-> gates found in the four files (not suspects, inventoried)

- main.cpp: zwriteThreshold{,Grass,Ped} (128; two-layer alpha threshold), pedShadows/stencilShadows
  (-1 sentinel), ps2Modulate{,Building,Grass} (0 under Xbox), grassBackfaceCull (1), fixSAMP fog
  resync, explicitBuildingPipe/pipelineOverride/gamePreset (pipe-identity writers - never flip mid-bisect).
- buildingPipe.cpp: lightningIlluminatesWorld (0), detailMaps/noiseDetail (1; texture binds), ivMode,
  bldSkyReflect, normalMapEnable (0), pbrIblAmbientWeight (0.8; constants only).
- vehiclePipe.cpp: ivMode, vehEnv* look constants, dualPassVehicle (0).
- pipelinecommon.cpp: ambientMult, chromeGloss/F0 (constants); buildingPipe/vehiclePipe in receipts only.

---

## 2. Xbox profile's 81 owned fields - classification + bisect lever

Source: `modeManager.cpp:206-308`. Class: **PIPE** = pipe identity (never flip while staying on
Xbox), **SAFE** = state-integrity safety system, **GATE** = feature/render gate, **LOOK** = appearance,
**PERF** = performance/debug. Lever: "menu" = live row exists (session toggle; NOT persistent across
relaunch); "NONE" = no lever at all.

| Group (count) | Fields | Class | Bisect lever |
|---|---|---|---|
| Identity (7) | pipeline, buildingPipe, vehiclePipe, colorFilter, tagsBuildingPipe, preset(gamePreset), ivMode | PIPE | menu (mode chip / pipes) - do NOT flip during bisect |
| Building (5) | ps2ModulateBuilding, detailMaps, noiseDetail, **dualPassBuilding**, usePCTimecyc | LOOK + GATE (dualPassBuilding, usePCTimecyc) | menu (Main/detail rows) |
| Vehicle/env (5) | **dualPassVehicle** (=0), envMapSize, envMapFarClipMult, envMapUseLODs, reflectionMsaaLevel | GATE(0)/LOOK/PERF | menu |
| Water (5) | waterStyle, waterQuality(2=High), **waterParallaxEnable**, sunGlare(0), neoWaterDrops(0) | GATE (waterParallax = #1 suspect) | menu (Water tab) |
| PostFX (28) | radiosity, doRadiosity, grainFilter, infrared/night, ssao/smaa/motionBlur/sssPost/skin/hair/veg/normalMap/bloom/dof/sunShafts/fxaa (all 0), grainIntensity, caIntensity, vcsTrails, **colorFilterEnable**, rgb1/2Mult, YCbCrCorrection, ivDesaturation/Gamma/VignetteIntensity/BloomIntensity | LOOK(0) + GATE colorFilterEnable | menu rows |
| Coronas (5) | **coronaZtest**, sunCorona/Core/StreakIntensity, sunStreakSize (all 1) | GATE + LOOK | menu (enum row 796) |
| Alpha/state (18) | ps2Modulate, ps2ModulateGrass, **dualPass, dualPassPed, dualPassGrass, dualPassDefault**, grassAddAmbient, grassBackfaceCull, grassFixPlacement, pedShadows, stencilShadows, zwriteThreshold{,Grass,Ped}, **stateBaselineEnable, pedOpaqueContract, texAlphaClassify, defaultCbShaderHygiene** | GATE x5 dualPass, **SAFE x4**, LOOK rest | menu for all EXCEPT **pedOpaqueContract (NO row - fixer item F1)** |
| FPS/perf (8) | grassInstancing, treeWind, treeFx, **forwardPlusEnable(0), pipeChainEnable(0), normalBufferEnable(0)**, **depthHookEnable**, profilerEnable | GATE x4, PERF rest | menu rows (all) |

Total: 7+5+5+5+28+5+18+8 = **81** (matches the L12-measured count).
**Bisect-lever gap among owned fields: exactly one - pedOpaqueContract (owned, no menu row).**

---

## 3. Recommended bisect procedure (user instructions)

Ground rules:
- One toggle at a time. Use the **in-game menu** for every owned key (INI edits are overridden -
  section 0.1). Non-owned keys (velocityBufferEnable, cloudRenderFix, spriteStateShield,
  waterBodyClassify, pedAlphaTestEnable, pedCullMode, ambient floors) can be bisected via INI +
  relaunch OR menu.
- After each flip: reload area, watch the pier/road/ped route from 29-OCD, screenshot. Corruption
  gone = culprit found; revert it and stop. Corruption persists = continue.
- Before starting: confirm binary generation. The 29-OCD run's ASI (00:39:57) lacked water Patches
  A/B. If the current ASI still lacks them (`grep -a -c water_vanilla_fallthrough skygfx.asi` == 0),
  step 1 (waterParallaxEnable=0) is expected to clear the teal/grey slabs outright - R1 confirmed.

### Top-10 order (flip OFF one at a time, live)

1. **waterParallaxEnable -> 0** (Water tab "Water pipe (master)"). Expect teal/grey quads gone if
   29-OCD R1 is the emitter (stale-VS/PS vanilla water fall-through).
2. **dualPass -> 0, then dualPassBuilding/Ped/Grass/Default -> 0** (Main + Dual-pass detail group;
   flip the family together first, split later only if it clears). Kills the two-layer alpha
   ALPHAFUNC/ALPHAREF/ZWRITE writers on most draws.
3. **stateBaselineEnable -> 0** (Main > State integrity). Isolates the frame-boundary canonical
   writes from the stale-capture replay (25-SLA finding #1).
4. **texAlphaClassify -> 0** (Main > State integrity). One switch feeding all three pipes' alpha
   paths - directly tests the "all pipes weird" common cause.
5. **cloudRenderFix -> 0** (Main > State integrity). Disables CClouds hygiene including its s5
   clobber and missing entry forces (25-SLA #7).
6. **spriteStateShield -> 0** (INI edit - non-owned; or temporary menu row after fixer F2). Drops
   the per-corona-draw 4-state normalisation.
7. **velocityBufferEnable -> 0** (Main). Removes the per-frame velocity RT pass + INTZ sampling.
8. **depthHookEnable -> 0** (Main). Removes the INTZ depth hook (disables step 7's substrate too -
   if 7 alone did nothing but 8 clears it, blame depthhook/INTZ cache-split, 25-SLA 2.4).
9. **pedOpaqueContract -> 0** - REQUIRES fixer item F1 (no menu row; not INI-persistent). Ped-only
   contract; tests the per-frame ped state repair.
10. **defaultCbShaderHygiene -> 0** (Main > State integrity). Same failure class as R1 (stale
    shader); flip only after water is cleared - disabling hygiene can ADD stale-shader slabs.

Runner-up (only if 1-10 all clean): coronaZtest -> -1, pedAlphaTestEnable -> 0, waterBodyClassify -> 0,
colorFilterEnable -> 0, doRadiosity -> 0, usePCTimecyc -> 0, ambient floors -> 0.

---

## 4. Fixer worklist - MISSING menu rows + missing INI keys

**Missing INI keys: NONE.** All 18 active-gate keys (section 1A) parse in `readIniFile` and are
present in the live INI (including the six that lack menu rows). No key needs adding for a bisect
to be possible via INI - except that owned keys are startup-overridden (section 0.1), so an
INI-only bisect of owned keys requires disengaging the mode chip (which also reverts pipe-identity
look fields) and is therefore NOT recommended; use menu rows.

**Missing menu rows (writable) - add to `src/core/debugmenu_ui.cpp`, Main > State integrity (or ped) group:**

| ID | Key | Suggested row | Priority |
|---|---|---|---|
| F1 | pedOpaqueContract | `rWn("pedOpaqueContract", "Ped opaque contract", &config->pedOpaqueContract, ...)` - the ONLY owned field with no lever | **Must for step 9** |
| F2 | spriteStateShield | `rWn(...)` - non-owned but INI-only today | High (step 6) |
| F3 | shadowContractEnable | `rWn(...)` - diagnostic shadow contract, INI-only today | Medium |
| F4 | pedAlphaTestEnable | `rWn(...)` - ped A/B lever | Medium (runner-up) |
| F5 | pedCullMode | `rIn(..., -1, 3, ...)` - ped A/B lever | Medium (runner-up) |
| F6 | pedAmbientFloor | `rF(..., 0.0f, 1.0f, ...)` | Low (look-only) |

Optional hygiene: add the six keys to the All-Settings defaults table
(`debugmenu_ui.cpp:1425-1504`) so coverage reports stop missing them; forwardPlusFallback already
has a read-only A-row (:1332) and needs no change.

**Persistent-bisect escape hatch (no action required now):** the 81 owned keys are un-bisectable via
INI by design (modeManager ownership). If a cross-relaunch bisect of an owned key is ever needed:
a `[Mode.<alias>]` sparse-override section - currently TODO `main.cpp:7056` (M1b-overrides) - or a
temporary local profile edit in `modeManager.cpp`.

---

## 5. Caveats

- Menu live-toggles of owned keys are authoritative for the running session only; F11 reload with
  the mode latched keeps them (same-id apply skipped, main.cpp:7054); relaunch does not.
- `doRadiosity=1` is forced by the profile but the SkyGFX radiosity pass stays blocked by non-owned
  `radiosityEnable=0`; only the game-side `m_bRadiosity` flag follows. Do not spend an early step
  on radiosity unless `radiosityEnable` was also seen =1 in a run.
- The deployed-ASI generation check (section 3 preamble) is a precondition for trusting step 1.
- Line numbers refer to the working tree as read 2026-10-29; re-grep symbols before editing.

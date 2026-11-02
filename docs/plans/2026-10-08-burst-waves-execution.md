# Execution Plan — Ship Roadmap with Burst Waves (2026-10-08)
# Supersedes sequencing of docs/plans/2026-10-08-iv-v-graphics-physics-port.md (that doc = feature matrix; this = schedule).
# Workflow: bursts of 4-8 parallel agents, ≤15 min/lane (target 5 min as we condense).
# Loop discipline: burst → reconcile → clash/regression check → next burst. Build ONLY when
# orchestrator judges game not broken. User tests. Ship target: Halloween (Oct 31).

## TIME ESTIMATES (per lane / per wave, wall-clock incl. reconcile)
| Wave | Scope | Lanes | Est. |
|---|---|---|---|
| W1 | P0 functional bugs (effects broken today) | 6 | ~25 min |
| W2 | Forward+ lights overhaul (collection, atlas, all shader entries) | 5 | ~40 min |
| W3 | IV/GTAIV wiring + orphan shader triage | 5 | ~40 min |
| W4 | DOF port (CoC→bokeh→tent→combine) | 4 | ~1 h |
| W5 | LUT tonemap (ACES/AgX/Frostbite/GT7) + bloom chain | 4 | ~1 h |
| W6 | Sun shafts + FXAA + grain/CA/vignette | 6 | ~1 h |
| W7 | Water family (shorelines/foam/mirror fix) | 4 | ~1 h |
| W8 | Vegetation (intensity win + imposter infra + Blender DFF) | 4 | ~2 h |
| W9 | Physics (ragdoll terrain/hit/Phase4, buoyancy) | 4 | ~2 h |
| W10 | Regression sweep + build + deploy + user test prep | orchestrator | ~1 h |
| W11+ | Stabilisation loop reserve (crash/INI/menu fallout) | bursts | 2-3 days |
| P4/P5 | Physics↔graphics motion vectors, boolean damage | later | post-ship |
| TOTAL ship core (W1-W10) | | | ~3-4 sessions-days |

## HALLOWEEN VIABILITY (honest): FULL matrix NOT viable by Oct 31 (23 days). SHIPPABLE CORE IS:
Stability (all P0/P1 bugs), Mode Manager, Forward+ lighting (2DFX-lit world), DOF, LUT tonemap,
bloom, sun shafts, FXAA, CA/vignette/grain, water polish. → ~23 days is workable if bursts hold.
POST-HALLOWEEN: vegetation imposters (W8 full), physics completion (W9 full), cloth/ropes,
decals, ped PBR, boolean damage.

## W1 — P0 BUGS BLOCKING EXISTING EFFECTS (dispatched 2026-10-08)
1. vcxproj: register src/core/modeManager.cpp/.h (link-blocks everything). [micro]
2. main.cpp: delete ivMode re-derivation :679-689 (mode manager owns it); wire ModeManager_Init.
3. postfx.cpp: DrawIVGrade() call in bypass path before chars_drawSSSBlur :3198 (IV grade invisible today when filter bypassed).
4. iv_mode.cpp: gut IVMode_ApplyDefaults :63-111 → just ivMode=1 (preset-creator bug).
5. forwardplus.cpp: stale view/proj (GetTransform before BeginUpdate) → derive from Scene.camera; frame-skew buffer flip (binds N-1 tiles with N lights).
6. buildingPipe.cpp+vehiclePipe.cpp: ForwardPlus_SetConstants call sites ungated (callee self-guards; today toggling OFF freezes stale lights).

## W2 — FORWARD+ OVERHAUL (O3DE 16px tile algo, SM3.0 adaptation; source exp-1 report)
1. VehiclePBR_Modern.hlsl: add cluster reads (c45/s5) to main_specCarFx/main_mobileVehicle/main_normMapVehicle/main_ps2EnvSpecFx (toggle invisible on those variants today).
2. forwardplus.cpp: light atlas texture (2x RGBA8 64x64: pos+radius / col+type) replaces c48-c111 constants → 256+ lights (needed for 2DFX).
3. forwardplus.cpp: collect corona/2DFX lights (CCoronas::Render hook data + static IPL 2dfx array) + brightest/distance sort (fix first-4-wins), raise to 16/tile.
4. GTAIVForwardPlus_ps/vs.cso wiring into GTAIV pipe draws (prebuilt, unwired — buildingPipe.cpp:639-647).
5. Per-tile zNear/zFar depth gate (optional refinement).

## W3 — IV/MODE + ORPHAN SHADER TRIAGE (gap audit items 3,4,5,6,9)
1. Wire IDR_CUSTOMBUILDINGVS/IDR_NORMMAPBUILDINGPS/IDR_NORMMAPVEHICLEPS (3 RC-embedded, never loaded → main_normMapVehicle unreachable).
2. vectorMotionBlur: add IDR to Resource.rc or delete dead source (decide: it's wanted for physics-velocity motion vectors P4 → wire it).
3. Ped/skin PBR island: decide wire-or-cut (SkinSSS load commented pipelinecommon.cpp:655; pedPBRVS no PS). → wire minimal (P1).
4. neoCarpipe dead entries cleanup (neoVehiclePass1/2VS never used).
5. Root-level HLSL (buildingPipePS 5 entries, stochasticBuildingPS, unifiedPipe) not scanned by fast_build.py — decide: move to ps/ or add scan.

## W4 DOF / W5 TONEMAP+BLOOM / W6 FXAA+SHAFTS+GRAIN / W7 WATER — source specs in
## docs/plans/2026-10-08-iv-v-graphics-physics-port.md §1A/C + LibertyRecomp algorithm table.
## W8 VEGETATION — exp-2 imposter report phases 0-5 (Blender 3.6 LTS + DragonFF, user to set up).
## W9 PHYSICS — GTA V leak port order (CTaskRageRagdoll writhe/relax → CBuoyancy → CWheel).
## NEVER-REVERT lists .serena/memories/swarm-round* + round5-consolidated MUST be honored in all lanes.

## BUILD GATE RULES
- Lanes NEVER build. Orchestrator builds (`python tools/fast_build.py --rebuild`) only when:
  (a) all W1-W3 P0/P1 items attempted, (b) no known crash-loop, (c) git status reviewed for
  clash. User runs game and reports. No commits before user pass.

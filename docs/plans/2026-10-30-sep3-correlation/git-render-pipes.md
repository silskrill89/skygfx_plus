# Git Archaeology: Render-Pipe Files - Sep 3 -> Oct 29 Regression Correlation

**Lane:** read-only research | **Date:** 2026-10-30 | **Repo:** E:\dev(dave)\skygfx_plus_expIV
**Scope files:** src/render/waterPipe.cpp, buildingPipe.cpp, vehiclePipe.cpp, pipelinecommon.cpp
**Symptoms targeted (ref report 4.1):** see-through solids, meshes drawing inside-out, vegetation over everything, occluded objects visible (culling/depth-order broken).

---

## 1. Repo state used for this analysis

- HEAD = `898e894` (2026-10-25, docs-only; last code-touching commit = `b7396c4` 2026-10-02).
- Working tree is hugely uncommitted (388 paths; 103 under src/): the "Oct waves" (stages 0-3, L1-L16, texdefs/debug_handl/sftbdyphsx merges) are in NO commit. Net uncommitted diff for the four scope files: **+4,923 / -505 lines**.
- Many neighbor files are deleted-but-uncommitted (SMAA.cpp/h, GrassSystem.h, PC_PlantsMgr*.cpp, neoCarpipe.cpp, mobile.cpp, iv_mode.cpp, xbox_mode.cpp, ps2_mode.cpp, custom_mode.cpp, ...). Those are outside four-file scope but alter dispatch globally (see 6).

## 2. Sep-3-era baseline reconstruction

Most recent commit at/before 2026-09-03 per file:

| File | Sep-3-era commit | Date |
|---|---|---|
| src/render/waterPipe.cpp | **b78a72a** | 2026-08-27 |
| src/render/buildingPipe.cpp | **b78a72a** | 2026-08-27 |
| src/render/vehiclePipe.cpp | **c270294** | 2026-08-30 |
| src/render/pipelinecommon.cpp | **c270294** | 2026-08-30 |

- No commit between 2026-08-30 and 2026-09-07 (next: 18f5303) touched any of the four files. c270294 (2026-08-30) is the closest single ancestor snapshot of the Sep-3 look; water/building pipes were untouched by c270294 and remain at b78a72a content.
- Commits since Sep 3 touching the scope files:

| Commit | Date | Files touched | Theme |
|---|---|---|---|
| 18f5303 | 09-07 | all 4 | checkpoint: PBR restore + depthhook intro (bldg +65, veh +106, common 63, water 14) |
| 94dcd37..b5d36a9 (6) | 09-09 | vehicle/common | vehicle PBR PS bisect/revert cluster |
| e39a6f8 / 9e93dc2 / 6be76d4 / 191dbe7 | 09-10 | vehicle/common | PBR layers, env fixes, CryEngine exposure |
| 01204d7 / ca922ef | 09-11 | building | lighting/ambient unification |
| 59bd64f | 09-15 | building/vehicle/common | crash-4, SSAO depth ordering, envmap cleanup |
| fcca238 | 09-19 | vehicle | rubber c22/c23 via pipeUploadPBR |
| **b7396c4** | **10-02** | **all 4 (+6,801/-795 across render)** | "render-state integrity, SMAA temporal fix, occluder hunt" - first-gen three-layer doctrine |
| (uncommitted Oct waves) | Oct 04-25 | all 4 | second-gen park-and-set, world baseline, draw elision, Z-trio, alpha-capture rewrite |

## 3. Classification of every state-handling change since Sep 3

### 3.1 Committed: 18f5303 (Sep 7)

- Light on the pipes: buildingPipe +65 (ambient/tonemap knobs), vehiclePipe +106 (PBR restore scaffolding), pipelinecommon 63 (shader-load plumbing), waterPipe 14.
- Render-state content marginal to cull/depth; ambient uploads only. New depthhook.cpp (+289) introduced the detach-SSAO-depth actor both Oct waves then fight. Low direct risk.

### 3.2 Committed: Sep 9-19 cluster (14 commits)

- Almost entirely shader/constant-param work (pipeUploadPBR c22/c23, env transforms, fxParams, ambient). 59bd64f also touches SSAO depth ordering (postfx side, out of scope) and envmap resets.
- First L2 mirrors (RwD3D9SetRenderState(D3DRS_ALPHABLENDENABLE,...)) appear in Sep 10 vehicle work. Low-to-moderate risk; does not itself break culling.

### 3.3 Committed: b7396c4 (Oct 2) - "render-state integrity, SMAA temporal fix, occluder hunt"

**Pivotal committed change.** Installs a three-layer render-state doctrine (L1 rw cache via RwRenderStateSet / L2 RW D3D9 driver cache via RwD3D9SetRenderState / L3 raw d3d9device->SetRenderState) and rewrites every render callback:

- **buildingPipe** (+882): dual-write blocks in CustomPipeRenderCB_PS2 (diff hunks @@ -311..-427); pipeForceAlphaBlock added (three-layer alpha/blend repair); ZWRITE three-layer guard in CustomPipeRenderCB_Switch (@@ -776..-791): entry snapshots ZWRITE from all three layers (RwRenderStateGet + RwD3D9GetRenderState + d3d9device->GetRenderState), forces ZWRITE=TRUE L1+L2+L3, restores on exit.
- **vehiclePipe** (+974): dual-write versions of FX/leeds/xbox/Env blocks; pipeForceCullMode(rwCULLMODECULLBACK) added in CustomPipeRenderCB_Env (@@ -1416); ZWRITE three-layer guard in CustomPipeRenderCB_Switch (~:2570 in committed file).
- **pipelinecommon** (+550): pipeForceCullMode / pipeEnterCullMode / pipeExitCullMode / pipeEnterAlphaMode / pipeExitAlphaMode born. pipeEnterCullMode at this commit **forces CULLBACK** (its own comment: "Enter always forces CULLBACK"). pipeExitAlphaMode restores L1 faithfully but pins D3DRS_ALPHATESTENABLE=TRUE unconditionally.
- **waterPipe** (+639): cull save/seed (s_savedCull) + sampler reset block + RwRenderStateSet(rwCULLMODECULLNONE) during plane draw with restore.

**Architectural hazard baked in on Oct 2** (what the later waves keep patching): with dual writes, restoration is frequently L1-only. RW's cached-set is a no-op when the cache already equals the target, but L3 raw values held by foreign writers (game vegetation immediate-mode CPostEffects::ImmediateModeRenderStatesSet, mirror pass, postfx raw D3DRS writes) make device and cache disagree. Failure classes:

1. Device keeps a stale value after restore ("double no-op"): foreign raw cull=NONE desyncs D3DRS while cache already claims BACK -> callbacks trusting cache-only restore draw double-sided / inside-out.
2. Restore mirrors an entry force, not the true pre-entry state: leeds/PS2 FX legs restore ZWRITE=TRUE unconditionally even when pre-entry ZWRITE was off -> later draws overwrite depth -> occluded objects visible.
3. ALPHATESTENABLE=TRUE pin at alpha-exit -> punch-through holes in passes needing alphatest off (foliage decal see-through).

### 3.4 Uncommitted Oct waves (working tree, Oct 04-25) - the active suspect layer

Second-generation idiom, ~+4.9k lines across the four files.

#### (a) pipelinecommon.cpp doctrine rewrite

- New helpers: pipeRwParkSet (:1135), pipeL2ParkSet (:1142), pipeL3Set = unconditional raw pin (:1149). Replaces b7396c4's conditional L3 write (gated on a possibly stale L2 read).
- pipeForceCullMode (:1206) rebuilt park-first (park vs BACK/FRONT/NONE domain includes CULLFRONT now).
- pipeEnterCullMode (:1250) semantics changed: forced-BACK (Oct 2) -> force the sanitized L1-cache value; CULLNONE and CULLFRONT now survive into pipe callbacks ("the mirror pass's CULLNONE reaches the assert intact"). Any callback span entered while the stale L1 cache says NONE now renders the whole span double-sided.
- pipeCullScopeBegin/End stack (:1351/:1369) RECORDS a cull mode but has no default call site (NO-OP in practice; mirror repair un-begun).
- pipeApplyWorldBaseline (:1975): canonical frame-boundary state, wired by default (INI stateBaselineEnable default 1, main.cpp:6687-6690) at four boundaries: RenderScene entry (main.cpp:3791), EffectsExit x2 (main.cpp:4323, 4352, 7778), depthhook DeviceReset (depthhook.cpp:421). Parks+pins on all three layers: ZWRITE=TRUE, ZENABLE=TRUE, **ZFUNC=LESSEQUAL**, SRC/DESTBLEND=SRCALPHA/INVSRCALPHA + blend OFF, cull BACK, SEPARATEALPHABLEND=FALSE, **ALPHATESTENABLE=TRUE**. New invariant that did not exist on Sep 3; the EffectsExit variants write state AFTER the postfx tail (last-state-writer inversion).
- pipeEnterAlphaMode (:1527) / pipeExitAlphaMode (:1751) rewritten: capture ALPHABLENDENABLE/ALPHATESTENABLE/BLENDOP from L2 AND L3 at entry, restore from those captures at exit (before: L1-faithful restore + unconditional pins). If capture occurs after a foreign actor already mutated the device (vegetation immediate-mode), the exit restores that foreign value as pre-entry truth - a foreign-state laundering path Sep-3 did not have.

#### (b) vehiclePipe.cpp (+1301 net)

- s_vehSwitchSavedCull (:1427): Switch-captured cull now carried across Xbox/leeds/Env callbacks and enforced via pipeForceCullMode(s_vehSwitchSavedCull) at :1542, :1993, :2507 - preserves a cache-derived NONE/FRONT across callbacks (same stale-cache laundering as #1).
- vehiclePipe_fxAdditiveBlend (:685-780): L3 ALPHATESTENABLE save/restore mirror added (save at entry, restore L2+L3).
- Xbox SetupMaterial dual-write expansion, D3DRS_TEXTUREFACTOR L3 mirrors, sampler 6-7 resets in vehiclePipe_cleanup (diff @@ -646..-660).
- ZWRITE three-layer guard in Switch (~:2570 committed, refined uncommitted): forces ZWRITE TRUE at entry on all three layers.

#### (c) buildingPipe.cpp (+1881 net)

- New draw-elision system: bpipeElideChanged4 (:1137) compares a 4-float state signature and SKIPS per-instance work; skip counters across four slots (PS2/XBOX/PBR/SPHERE) at :1382, :1391, :1949-2007, :2241-2245, :2780-2885. Skips include vertex-shader-constant uploads AND state sets (VERTEXALPHAENABLE/blend set skipped when signature unchanged, diff @@ -672..-699 and @@ -1039..-1596). A signature collision (different instance, equal floats) silently inherits the previous draw's blend/ZWRITE/fog -> localized x-ray/see-through.
- Decal-boundary state: new "next mesh is not a decal -> hand the callback baseline (ZWRITE TRUE)" block (diff :2145) + class-NAME contract comments; evidence the elide window previously let ZWRITE=FALSE leak into opaque geometry.
- pipeForceAlphaBlock(preEnter) overload (:218 region) + blend_leak repair block (diff :498-568): detects a RAW device with ALPHABLENDENABLE=FALSE after the mod pass and re-asserts. Proof of active blend leaks under the committed doctrine.
- Switch cb refinements: pipeEnterCullMode/pipeEnterAlphaMode + ZWRITE trio guard with L3-mirror restore (:3122-3201), pipeForceAlphaBlock(&savedAlpha) pre-dispatch.

#### (d) waterPipe.cpp (+1179 net)

- New Z-state trio capture (L1/L2/L3 for ZTEST/ZWRITE/ZFUNC, :940-1130) + "point of no return" contract (ZENABLE=TRUE/ZFUNC=LESSEQUAL/ZWRITE=FALSE all layers in setRenderState, :1531-1645; restore :1753-1783).
- Scene-RT machinery: EnsureDepthTexture / CopySceneToTexture / SyncDevice; the parallax path detaches the device depth-stencil surface (comment block) and handback relies on the same snapshot gate. Restore-ordering bug class here = "everything draws without depth" (occluded objects visible) until next Clear.
- New WaterPipe_ReleaseDeviceObjects / waterPipe_OnDeviceReset lifecycle contract wired to the Reset lane (depthhook.cpp / postfx.cpp OnDeviceReset).

## 4. Ranked suspect list (most -> least likely 4.1 culprit)

| # | Suspect | Where | What changed | Why it can produce the symptoms |
|---|---|---|---|---|
| 1 | Uncommitted pipeEnterCullMode semantics flip: preserve L1 CULLNONE/FRONT instead of forcing BACK | pipelinecommon.cpp:1250-1284; callers vehiclePipe.cpp:1542/1993/2507, buildingPipe.cpp:3106/3231, main.cpp:1886/2245 | b7396c4 forced CULLBACK at every pipe entry; Oct waves force the cache's saved value, letting NONE/FRONT cross the whole callback span. | Pipe callbacks are entered constantly while the stale L1 cache says CULLNONE (game vegetation PC_PlantsMgr immediate-mode + CPostEffects::ImmediateModeRenderStatesSet park cull NONE with RAW D3DRS writes; the rw cache is never told). Whole buildings/vehicles render double-sided -> inside-out meshes, see-through solids; vegetation drawn with device cull NONE sits at the far end of that span -> vegetation over everything. Sep-3 code always forced BACK at entry. |
| 2 | Uncommitted pipeApplyWorldBaseline default-ON at 4 frame boundaries | pipelinecommon.cpp:1975; wired main.cpp:3791/4323/4352/7778, depthhook.cpp:421; default stateBaselineEnable=1 (main.cpp:6687) | New boundary invariant: pins ZWRITE TRUE / ZFUNC LESSEQUAL / blend OFF / ALPHATESTENABLE TRUE / cull BACK on all three layers, including AFTER the postfx tail. | The EffectsExit runs overwrite the last state writer (postfx). A next-frame consumer inheriting baseline state (e.g. a pass that legitimately expects inherited ZFUNC or blend from the previous mode) now gets forced LESSEQUAL/blend-off instead. New invariant vs Sep 3; toggling stateBaselineEnable=0 in skygfx.ini is the 1-minute A/B test. |
| 3 | Uncommitted buildingPipe elision system | buildingPipe.cpp:1108-1200 helpers; skip sites :1382/:1391/:1949-2007/:2241-2245/:2780-2885 | bpipeElideChanged4 skips per-instance state sets + constant uploads on a 4-float signature match; classes PS2/XBOX/PBR/SPHERE. Decal <-> opaque boundary ZWRITE handback added (:2145 diff). | Signature collision between different instances inherits blend/ZWRITE/fog/VERTEXALPHAENABLE state from the previous draw -> decal getting opaque handling or vice versa: localized see-through / x-ray, decals off; the code itself carries a "next mesh is not a decal" repair comment proving leaks already observed. |
| 4 | b7396c4 (Oct 2) three-layer doctrine: L1-only restores after L2/L3 raw dual writes | buildingPipe.cpp:1638+ (ZWRITE trio), vehiclePipe.cpp:2570+ (Switch), pipelinecommon.cpp (first-gen helpers), every dual-write block in 3.3 | Dual-write entry + mixed restore paths; conditional L3 write gated on possibly-stale L2 reads; unconditional ALPHATESTENABLE=TRUE pin in pipeExitAlphaMode. | Device retains stale ZWRITE=FALSE / ALPHABLEND=TRUE / cull=NONE after cache-restore no-ops (documented failure classes in own comments: "double no-op", "the double no-op in pipelinecommon pipeForceCullMode"). Primarily affects the Oct-2..Oct-25 window; the uncommitted waves partially re-pair it, but any callback still lacking a force/restore (e.g. PS2/Xbox legs beyond the patched set) inherits the stale device state. |
| 5 | Uncommitted waterPipe Z-trio + depth-stencil detach | waterPipe.cpp:940-1130 (capture), :1531-1645 (pin), :1753-1783 (restore), CopySceneToTexture/EnsureDepthTexture | Parallax path detaches device depth-stencil; entire depth trio saved/pinned/restored through three layers. | Order bug between detach/restore or skipping restore on an early-out path -> transparent-water pass leaves the device without a depth surface or with ZWRITE=FALSE -> everything drawn after water renders without depth: occluded objects visible, vegetation-over-everything style artifacts immediately after any water draw. |
| 6 | Uncommitted alpha capture/restore rewrite (foreign-state laundering) | pipelinecommon.cpp:1527 (enter), :1751 (exit); buildingPipe.cpp Switch pre-dispatch pipeForceAlphaBlock(&savedAlpha) | pipeEnterAlphaMode now captures ALPHABLENDENABLE/ALPHATESTENABLE/BLENDOP from L2 AND L3 and restores those captures; vehicle fxAdditiveBlend mirrors ALPHATESTENABLE save/restore. | If the device was mid-mutation by a foreign immediate-mode actor at capture time, exit re-applies foreign alpha state to the whole post-callback frame (blend ON where OFF is expected = soft see-through). Mitigated by "captures present" gating, but only when pipeEnterAlphaMode itself ran. |
| 7 | Committed Sep 9-19 vehicle PBR cluster | 94dcd37..fcca238 (14 commits), vehiclePipe.cpp + common | Shader/parameter work; first L2 blend mirrors. | Low risk to culling/depth; top suspect only for Sep-early look drift (paint/env), not for the Oct 29 culling regression. |
| 8 | Committed 18f5303 (Sep 7) checkpoint | all 4, depthhook.cpp +289 | Depthhook intro, PBR restore scaffolding, no-op tuning knobs. | Low direct state risk; but depthhook (SSAO depth detach actor) born here - watch it when chasing #5. |

## 5. Sep-3-era commit hashes (answers)

- closest single ancestor snapshot overall: **c270294** (2026-08-30)
- waterPipe.cpp: **b78a72a** (2026-08-27)
- buildingPipe.cpp: **b78a72a** (2026-08-27)
- vehiclePipe.cpp: **c270294** (2026-08-30)
- pipelinecommon.cpp: **c270294** (2026-08-30)

## 6. Out-of-scope changed actors referenced by the in-scope code (context only)

- postfx.cpp (modified, uncommitted; ~+2,846/-795 committed in b7396c4): raw D3DRS_ZWRITEENABLE writes at its own gates (:3451, 3630, 3703, 3741, 3791 per in-file comments), Save/RestoreRawGeomStates, ImmediateModeRenderStatesSet call sites (0x700D70) x15.
- depthhook.cpp (modified, uncommitted; +336 in b7396c4): hook_Reset, SSAO depth detach, DeviceReset baseline call.
- main.cpp (modified, uncommitted): pipeApplyWorldBaseline wiring (:3791, 4323, 4352, 7778), pedCullMode span (:1886-2245).
- Deleted subsystems (uncommitted D): PC_PlantsMgr*/GrassSystem.h (vegetation), SMAA, neoCarpipe, mobile, iv/xbox/ps2/custom modes - these change the set of render-state actors present in the frame, so a Sep-3..Oct-29 A/B is NOT achievable by reverting only the four pipe files.

## 7. Recommended verification order (for the fix lane)

1. Set stateBaselineEnable=0 in skygfx.ini, rebuild, test (isolates suspect 2 without code changes).
2. Temporarily re-force CULLBACK in pipeEnterCullMode (pipelinecommon.cpp:1262-1282) - restoring the Oct-2 committed behavior isolates suspect 1.
3. Disable the elision skip path (gate bpipeElideChanged4 to always report change) - isolates suspect 3.
4. In a session with heavy water, toggle only the water Z-trio detach/restore path - isolates suspect 5.

*(comma-scope note: suspects 1-3 are uncommitted-wave only; suspect 4 includes the last committed code change b7396c4 (Oct 2), which post-dates the Sep-3 reference by one month.)*


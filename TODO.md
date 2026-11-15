# SkyGFX Plus — Project TODO & Plan

> Persistent working plan. Recovered from OpenCode session `ses_09b16e578ffesKBRwznkIrFhFn`
> ("Fix request") + subagent `ses_09ae74fa9ffejxd0Mw1thynb6i` ("Study SilentPatch hooking pattern").
> Working repo: `E:\dev(dave)\skygfx_plus_expIV` (the `C:\Dev\shaisse_hub\skygfx_plus` copy is STALE/divergent — ignore it).
> Game dir: `E:\games\gtasa_skygfx_plus\` (gta_sa.exe, skygfx.asi/dll).
>
> **Reconciled 2026-11-07 against wave-14-record.md + activeContext.md** — the ✅ DONE
> list and §G (current active queue) carry the present truth; all older sections below
> are retained history.

---

## Golden rules (from user)
- **Always use the hardest / most thorough solution.** Proper C++, no fragile SEH when a correct C++ path exists.
- **Follow RW SDK 3.7 coding conventions.**
- **Stick to SilentPatch's standard** for skygfx compat: SilentPatch already accommodates skygfx in source
  (`ModCompat::SkyGfx` namespace, reads `GetConfig()` export → `Config.version`). Keep that export intact.
- **skygfx must ALWAYS load after SilentPatch** (see Load-order note below).
- **Workflow:** assistant builds + deploys; user runs game and reports; we iterate (bounce).
  If no word from user within **3 minutes of a build**, assistant force-kills `gta_sa.exe`
  (PowerShell `Stop-Process -Name gta_sa -Force`; plain `taskkill /F` mangles the arg here).
- **Normal map + txd `_n` feature = LAST.** Everything else below is good to proceed now.
- **Standing directives (2026-11-02, in force):** ASAP pacing (no idle gaps; no deadline
  active — stable-config is the goal line); unify impls (one shared implementation,
  registry-driven addresses); cluster lanes by subsystem (never parallelize overlapping
  writes); per-system .cpp end state (skin system .cpp, ONE noise.cpp, physics engines
  connecting systems); write summaries to memory (memory-bank + serena), never chat-only.
  Source: `.serena/memories/standing-directives-2026-11-02.md` (extends, does not replace,
  older gates: lanes never build; one integrated build by orchestrator; NEVER-REVERT wave
  records).

---

## ✅ DONE
1. **0x5DA610 recursion crash fixed** (committed 89be8b7).
   - Root cause: InterceptCall on function entry → bogus original → stack overflow.
   - Fix: InjectHook trampoline. Build 0 errors/0 warnings. DLL+ASI deployed.
2. **Build system fixes** — /FS for PDB contention, dbghelp.lib, crash capture (VEH+minidump).
3. **GetConfig() export confirmed** — version=0x370 ≥ SilentPatch 0x360.
4. **Game boots and runs clean** — skygfx_dbg.log shows 2035+ frames, PBR active, ragdoll init.
5. **Hooking discipline audit complete** (PLAN.md P3) — all hooks documented, no incorrect trampolines.
6. **Dawn brightness fix** — ambient 0.50, direct 0.35, env 0.30 in vehicle shader; IBL 0.15 in building shader; AMBIENT_FLOOR 0.20.
7. **Adaptive tonemap v2** — timecycle-driven exposure+toe, interior/cutscene detection, carcols integration.
8. **CJ render fixes families A/B/C — ALL FIXED ON DISK (unbuilt)** (2026-11-02/03; next gate
   + roadmap table: §G below):
   - **A (angle transparency)** — texalpha deterministic classification (first-sight scans always,
     per-raster stable class, cyclic-LRU, `budget=` telemetry-only) + pedOpaqueEnforce
     UNCONDITIONAL (ALPHATEST GE@1 + CULLBACK 3L at ped entry).
   - **B (buildings-over-CJ)** — M2 carve (`decalBlend == blendOn`, blended-mesh ZWRITE park 3L
     + mesh-loop-end restore) + ZTEST/ZFUNC contracts (ped-entry depth trio
     ZTEST=1/ZENABLE=1/ZFUNC=LESSEQUAL forced 3L) + sticky depth (canonical state left at exit).
   - **C (inside-out player)** — renderCallBack at **+0x48** (old +0x10 clobbered
     `RwObjectHasFrame.sync`) + **0x804EF0 clump-splice REMOVED** + Dismember offsets (frame at
     atomic+0x04, modelling matrix embedded at frame+0x10). Also closes the gibs render fix.
   - Next gate: integrated build → deploy → user retest → commit (see §G).
9. **Also landed (waves 12-14 / build 11, per activeContext 2026-11-03)** — peds_pipe Phase 1-2
   (src/peds/ + `Pdp1` clump plugin + `[Ped]` config mirror); water M1-M3 + wind sea + shore gate
   (fix-36); particles module (`src/render/particles_gpu.*`, `gpuParticlesEnable=0` default);
   menu overhaul (30-section master-detail); fonts glyph-flush pin (0x7273D0); RSMon
   classification fix (entry/exit verdict pairing); scene-start repair (wave-12); test12
   forensics baseline recorded (`docs/baseline/2026-11-03-test12/findings.md` — pre-fix A-side
   evidence set, 12 shots + 19,371-line log, build 11 = 0 `[PedZW]`/`[BldZW]` receipts); perf
   wave shipped (PERF R2 / R5 / R6).

---

## 🔲 TODO (in priority order)

### A. Stability / verification ✅ DONE
- [x] **Confirm game loads clean past `CGame::Initialise`** — verified, 2035+ frames, PBR active.
- [x] **Verify crash capture armed** (VEH + minidump) — working.

### B. Hooking discipline ✅ DONE
- [x] **Audit ALL hooks** — documented in PLAN.md P3. No incorrect trampolines found.
- [x] **Document load-order guarantee** — skygfx.asi sorts after silentpatch.asi alphabetically.
- [x] **Verify no duplicate hook installations** — src/core/main.cpp is the active file; src/core/hooks.cpp is dead code.

### C. Normal map integration (DK22Pac rwnormal) — DEFERRED, see rule
- [ ] (LAST) Wire the txd `_n` normal-map feature: read normal maps from `_n`-suffixed textures
      and feed them through the rwnormal plugin. This is the final feature step.

### D. Visual quality refinement ✅ DONE
- [x] Vehicle PBR specularity/metallic/env reflections improvement — clearcoat Fresnel, metallic energy conservation, ambientObj
- [x] Ground/street brightness at dawn — ambientObj timecycle, removed hardcoded floor
- [x] Sun flare fix — sunCoronaIntensity (0.4), sunCoreIntensity (0.6) INI controls, unified tonemap
- [x] CJ character material depth — SSS depth-based character mask, Fresnel highlight, timecycle ambient matching
- [x] Glass shader transparency — env intensity boost, view-angle-dependent alpha, Schlick Fresnel
- [x] Vehicle env reflections — view-space transform, sky contribution, carcols-driven envMask, glossiness workflow

### E. Housekeeping
- [x] **Full roadmap plan** — `docs/plans/2026-08-01-remaining-roadmap.md` (Phases 0-5, 25 safety issues, all TODOs consolidated)
- [x] **Update PLAN.md** — mark P2/P3 done, add visual refinement tracking.

---

### F. Atmospheric & Shadow Features (research complete, core features implemented)
- [x] Stencil shadow research — Z-Fail Carmack's Reverse, D3D9 states documented, addresses mapped
- [x] Cloud research — GTAV geometry dome (not raymarching), VolumetricCloudsExtended.SA (no conflicts)
- [x] FusionShaders initialized — SunShafts_PS.hlsl, DeferredShadow*.asm available for reference
- [x] **Commit vehicle shader work** (sRGB, vertex color base, rubber darkening, interior→matte, chrome BRDF) — committed 535cde7
- [x] **Crytek height fog** — HeightFog.hlsl + DrawHeightFog() in postfx.cpp, INI 7 fields, exponential fog with depth reconstruction
- [x] **Screen-space god rays** — GodRays.hlsl + DrawGodRays() in postfx.cpp, INI 6 fields, radial blur toward sun
- [x] **PostFX chain wired** — ColourFilter→MotionBlur→HeightFog→GodRays→UpdateFrontBuffer→SSS
- [x] **INI config** — 13 new fields parsed in config.cpp with sensible defaults
- [x] **Code review** — completed, SHIP verdict (2 warnings: misleading comment, scratch cam leak)
- [x] **debugmenu_ui.cpp** — Add "Atmospheric" collapsing header with 13 ImGui sliders (done: lines 651+)
- [x] **Stencil shadow port** — game already has stencil shadows; skygfx hooks GetFxQuality_stencil (0x7113B8, 0x711D95, 0x70F9B8) + z-offset patches
- [x] **Volumetric clouds** — DynamicSky.hlsl integrated via RenderIBLBuffer() in buildingPipe, procedural cloud clumps + layered clouds
- [x] **Cloud ground shadows** — ComputeCloudShadow() in colorSpace.hlsl + PBR_cloudFBM() in PBR_Common.hlsl for vehicles
- [x] Update docs/plans with atmospheric roadmap

---

### G. Current active queue — **Reconciled 2026-11-07 against wave-14-record.md + activeContext.md**

> Sources: `docs/plans/2026-11-02-systematic-roadmap.md` (CJ bug wave, NEW open items,
> Remaining backlog), `.opencode/memory-bank/activeContext.md` (2026-11-03 state + Remaining
> work), `.serena/memories/wave-14-record.md`. **Goal line = stable-config acceptance**
> (`docs/baseline/2026-11-02-stable-config/README.md`) — the Halloween deadline passed Oct 31,
> no carry-forward deadline active. **test12 baseline** =
> `docs/baseline/2026-11-03-test12/findings.md` — pre-fix A-side evidence set (build 11 ran
> with 0 `[PedZW]`/`[BldZW] parked` receipts = the CJ bugs live). Standing directives in force:
> ASAP pacing, unify impls, cluster-by-subsystem lanes, per-system .cpp end state, write
> summaries to memory.

**Next gate — families A/B/C (roadmap section on the CJ bugs: all three FIXED ON DISK, unbuilt):**
- [ ] **Integrated build → deploy → user retest → commit+push (w/ shot_sync).** Acceptance: the
  `[PedZW]` / `[BldZW] … ZWRITE parked` receipts appear (receipt-gate rule); test12 shots
  4/5/7/11/12 clear on re-shoot (Glen Park pond + Tahoma + swimming); stable-config
  no-regression. Family-A discriminator: if shot 7 survives A's fixes ⇒ A-2 ⇒ next-lane rule below.
  Fix details + done record: ✅ DONE #8; roadmap table for owner files.

**Roadmap NEW open items (test12 registry) — N1…N7:**
- [ ] **N1** `[PipeAlpha]` faithful-restore L2/L3 replay, 81× `enable L2=1 L3=0` (parity-6 exit
  replays a divergent alpha capture into later draws) — `src/render/pipelinecommon.cpp`
  `pipeExitAlphaMode`; extend sticky rule to ALPHABLENDENABLE + cull exits; probe: replay count → 0.
- [ ] **N2** zfunc=8 dead-depth test, 106/127 repairs (~2 s throttle ⇒ ~120 frames run with dead
  depth) — drift **source** unidentified; `pipelinecommon.cpp` `pipeEnforceDepthContract` +
  `renderstate_monitor.cpp`; probe: `[zfix]` full-drift recurrence → 0.
- [ ] **N3** FAM-Q giant milky sprites — `texalpha.{cpp,h}` soft-decal class + `main.cpp` sprite
  shields; probe: test12 #9/#10 class gone. Measure after family A ships before adding fixes.
- [ ] **N4** FAM-G grass corruption — `grasssys.{cpp,h}` + `texalpha.{cpp,h}`; probe: Glen Park
  re-shoot, crisp billboards, no grass-over-water depth artifact.
- [ ] **N5** FAM-N near-black palms — needs its own sweep; `buildingPipe.cpp` normal/IBL lane +
  `pipelinecommon.cpp` light uploads; probe: palm trunks lit at ambient level.
- [ ] **N6** FAM-T missing textures (minor) — `texdefs.cpp` + `texalpha.{cpp,h}`; probe: no
  pink/magenta untextured surfaces on same routes.
- [ ] **N7** FAM-W bounded whiteout — **KNOWN/ACCEPTED** (bounded; tracked, not a ship gate) —
  `postfx.cpp` timecycle/fog bracket + `weather.cpp`; probe: bounded level holds; measurement
  un-confounded once height-fog gate (below) lands.

**Roadmap A–G + cross-cutting — open/acceptance portions:**
- [ ] **Roadmap A** white blowout — GodRays bounded-contribution clamp lane **IN FLIGHT**
  (exp-2 fix step 3) + aerial acceptance probe (18:00→22:00 route, no full-white frame,
  `[TC] fogStart` clamp receipt, scene_start DIVERGE → 0).
- [ ] **Roadmap B** scene_start repair / see-through world — **LANDED** (wave-12 lane, `84ef232`);
  acceptance probe pending: `rs_mon` scene_start DIVERGE → 0 over 5 min, `[PED] ZWRITE
  off/desynced` → 0, shot-13 re-shoot.
- [ ] **Roadmap C** sea edge / ocean — **LANDED (fix-36)**; acceptance: re-shoot shots 7/9/14
  (foam transition at waterline, far-water fades to horizon colour).
- [ ] **Roadmap D** cloud lane — **LANDED (fix-37)**; H1 additive-accumulation clamp **IN
  FLIGHT**; acceptance: soft-edged deck 18:00–21:00, no slab/puffs, `[FxAlpha] Clouds` → 0.
- [ ] **Roadmap E** height-fog veil / inert pass — **IN FLIGHT**; honour `heightFogDensity = 0`
  as OFF, clamp+log negative fogStart; probe: `[PF] HeightFog gate -> SKIP`, veil gone in
  shots 5–10.
- [ ] **Roadmap F** rotor / fast-geometry motion smear — **IN FLIGHT**; cut test requires BOTH
  large posDelta AND low fwdDot; clamp rotor-class blur; probe: `camera CUT reset` → 0 in
  straight flight.
- [ ] **Roadmap G** untextured white ground planes — **LANDED (fix-34)**; acceptance: shot-13
  beach re-shoot (sand textured at all distances, `[bind-elision] dev_mismatch` live).
- [ ] **Cross-cutting** TC input validation — **LANDED (fix-33 CfSanitize)**; acceptance: clamp
  receipt prints on every bad transition, no consumer sees negative fogStart.

**Roadmap Remaining backlog + activeContext Remaining work:**
- [ ] **peds_pipe Phase 3 (skin absorb)** — rides the B bracket for entry-state repair; design
  `docs/plans/2026-10-31-peds-pipe-design.md` §4. (Phase 1/2 LANDED — DONE #9.)
- [ ] **Water M4-M9** — per `docs/plans/2026-10-30-water-overhaul-spec.md`; fix-36 covers the
  sea-edge slice; stable config = acceptance gate. (M1-M3 LANDED — DONE #9.)
- [ ] **G1 vignette round-trip** — INI ↔ menu ↔ shader constants, follow-up to G-family work.
- [ ] **Address migration P1** — hardcoded exe addresses → registry; settle SilentPatch-owned
  sites 0x6FB17C / 0x748AA8 / 0x53E0C8; `docs/plans/2026-11-02-wrapper-migration.md`;
  prerequisite for non-1.0-US exe support.
- [ ] **GPU particles lane** — module shipped in build 11 (`particles_gpu.*`,
  `gpuParticlesEnable=0`); further emitter work rides on it.
- [ ] **Menu overhaul — remaining plan items** — verify/close against
  `docs/plans/effects-menu-overhaul.md` (30-section wave shipped in build 11).
- [ ] **RpAtomic layout resolution** — device geometry/layout contract.
- [ ] **RSMon metric recalibration** — `rs_mon_report` 100% diverge at scene_start
  (samples=13885, bad=0) is a monitor-metric defect, not a live desync; one-off field trip to
  recalibrate the comparison gate.
- [ ] **Motion-blur Tier 0/1** — `docs/plans/2026-10-29-motionblur-physics-research.md`:
  Tier 0 dt-normalisation + cut-reset; Tier 1 shader upgrade (25 → 13-17 fetches, 0 new
  registers).
- [ ] Also open (activeContext 2026-11-03 OPEN line): FNV `s_lastTick` 256→1024;
  PS2/Sphere M2 carve.
- [ ] Secondary (kept unresolved): unified* dead-code cleanup; CopySceneToTexture HRESULT
  decision; ps2ModulateGlobal cross-slot decision; **readme.md rewrite commit still pending**
  (checkpoint note; attribution + README rewrite landed `5cc5d25`).

---

## Full Roadmap

> Status note (Reconciled 2026-11-07): legacy plan below is historical — live status is
> §G above + `docs/plans/2026-11-02-systematic-roadmap.md` (authoritative open-item
> registry per `docs/FEATURE_LIST.md` §12).

See **`docs/plans/2026-08-01-remaining-roadmap.md`** for the complete implementation plan covering:

| Phase | Description | Status |
|-------|-------------|--------|
| 0 | Safety hardening (normalize guards, div-by-zero, strcat, sprintf, fxParams init) | ✅ DONE (52bf910) |
| 1 | Restore full compatibility (dead stubs annotated, Motion Blur wired, SSS wired) | ✅ DONE (7f85408, 05dc968) |
| 2 | Visual quality (CJ depth, glass, sun streaks, CryEngine PBR, glossiness, env reflections) | ✅ DONE (a600105, c671ce3, f09e50a) |
| 3 | Architecture cleanup (vehiclePipe dedup, buildingPipe dedup, FOGENABLE fix) | ✅ DONE (aa8693b, 4ef8ce3, b680e31) |
| Phase F | Atmospheric features (height fog ✓, god rays ✓, stencil shadows ✓, clouds ✓) | ✅ DONE |
| 4 | Platform selection UI (presets ✓, pipe dropdowns ✓, hot-reload) | ✅ DONE (hot-reload deferred) |
| 5 | Advanced features (Forward+ ✓, skin/SSS ✓, normal buffer ✓) | ✅ DONE |
| C | Normal map integration (DK22Pac) — **LAST per user rule** | 🔒 DEFERRED |

## Key addresses / facts
- `0x5DA610` = CustomPipeAtomicSetup (vehicle pipe) — MUST use trampoline, never InterceptCall.
- `0x4C88F0` → `0x5DA610` redirect (upgrade parts vehicle pipe) — fine as-is.
- `0x74872D` = IsAlreadyRunning — SilentPatch hooks this; skygfx must NOT (defer instead).
- `CCustomCarEnvMapPipeline__CustomPipeAtomicSetup` = WRAPPER `EAXJMP(0x5DA610)` (gta.cpp:150) —
  never call from a hook at 0x5DA610 (recursion).
- SilentPatch SkyGfx compat: `ModCompat::SkyGfx::GetConfig(module)` → `GetProcAddress("GetConfig")`
  → reads `Config.version`. Keep `GetConfig` exported and `version` first member.

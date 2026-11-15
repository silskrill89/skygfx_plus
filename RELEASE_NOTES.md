
## Wave 12 — Whiteout bounds, scene repair, wind sea, GPU particles (2026-11-02)

- **GodRays additive bound**: worst-case energy cap (grWorst ≤ 0.30, uniform scale-down, shape preserved); blend SET now 3-layer park-and-set; NightVision unbounded `while(n--)` loop capped at 64; InfraredVision radius sanitized; underwater composite audited clean (opaque REPLACE, not additive).
- **Scene-start repair**: new `pipeRepairSceneStartSync()` runs every frame in `RenderScene_before` — compares device vs rw cache on the blend/Z family and repairs to cache truth BEFORE the first draw. Kills the standing see-through divergence (device entering the scene blend-on/depth-off).
- **Height fog**: `heightFogDensity=0` is now a true no-op skip (was force-substituting 0.0015 + skyBot tint = the grey veil).
- **Motion blur**: velocity-cut detector now gated on camera identity (`PoseIsMainCamera`) — leaked secondary-camera matrices (mirror/vehicle cam) no longer false-fire cuts (rotor smear fixed).
- **Ped/marker contracts**: marker NULL-pipe entry contract now sets ALPHAFUNC=GREATEREQUAL@1; always-on cheap NULL-shader safety net under the defaultCbShaderHygiene lever; ped contract skip path resets to canonical GE@1 (leaked alpharef 140 can no longer hole ped skins).
- **Clouds**: weather-driven coverage wired (`CWeather__CloudCoverage` → noise constants); PBRBuilding frame spam throttled (was 31% of the log).
- **GPU particles module (new, experimental)**: `particles_gpu.{cpp,h}` + WakeParticlePS.hlsl — effectsPC.txd texture harvest, wind-scaled wake particles spawned from the live wake flushes, water-plane billboards, full state capture/restore. INI `gpuParticlesEnable` (default 0 = inert).
- **Perf**: font pin entry==canonical fast path; Forward+ skips full rebuild when zero tiles lit; debug overlay default OFF.

---

## 2026-11-07 reconciliation — release lineage, CJ wave, attribution (append-only; timeline above unchanged)

Sources: `.serena/memories/wave-13-record.md` (§1–§2), `.serena/memories/wave-14-record.md`,
`docs/plans/2026-11-02-systematic-roadmap.md`, `docs/FEATURE_LIST.md` (2026-11-03).
Nothing below is user-verified unless explicitly stated — and nothing is.

### Build identity / release lineage
- **Build 11** (wave-12 8-lane release; committed `84ef232` + docs `5cc5d25`): `skygfx.asi`
  **2,562,560 B**, md5 `cfe007e7f5f72b2aeb4ff0311e6d7911`, deployed 2026-11-02 16:57
  (wave-13 §1 identity). Lanes in it: fix-33 whiteout bounds + CfSanitize, scene-start repair,
  fix-36 wind sea + shore gate, fix-37 clouds, fix-34 TAC_UNKNOWN ground fallback, GPU particles
  module (`gpuParticlesEnable=0` default), 30-section menu overhaul, perf R2/R5/R6 (debug overlay
  default OFF, font glyph-flush pin fast path, Forward+ rebuild reorder), RSMon entry/exit
  classification fix, peds_pipe Phase 1–2 module (`src/peds/` + `Pdp1` + `[Ped]` config mirror),
  water M2/M3 foam base.
- **Last verified deployed ASI**: `E:\games\gtasa_skygfx_plus\skygfx.asi` **2,565,120 B**,
  mtime **2026-11-02 21:40**, md5 `b0c0b050c24bc0935dbdc33c61d2156c` (re-hashed 2026-11-07 during
  this reconciliation). It is 2,560 B off the wave-13 build-11 identity (delta not documented in
  the memory records; test12's 2026-11-03 log recorded the 2,562,560 B / 16:57 identity as its
  baseline binary instead). **Both deployed observations pre-date the CJ fixes**: no integrated
  build has been cut since, so no deployed ASI contains them.

### CJ bug wave (families A/B/C) — FIXED ON DISK, unbuilt, untested
- Families per test12 findings numbering: **A** angle transparency · **B** buildings-over-CJ ·
  **C** inside-out player. All three root-caused and fixed in the working tree during the
  2026-11-02/03 CJ wave: A = texalpha deterministic classification + unconditional
  `pedOpaqueEnforce`; B = M2 carve + ZTEST/ZFUNC contracts + sticky depth; C = renderCallBack
  +0x48 + 0x804EF0 clump-splice removal + Dismember frame/matrix offsets (owner files per the
  roadmap "CJ bug wave" table; gibs render fix = family C).
- Status: **FIXED ON DISK (unbuilt), NOT user-verified.** They await one integrated build, then
  the acceptance gate — `[PedZW]` / `[BldZW] … ZWRITE parked` receipts appear, test12 shots
  4/5/7/11/12 clear on re-shoot, stable-config no-regression. test12 (2026-11-03: 12 shots +
  19,371-line log) ran against build 11 **pre-fix** and is the A-side baseline for the next
  build's A/B.

### Attribution — skrillex message-pool rewrite (291 commits, 3 branches)
- Commit/message pool lineage rewritten message-side to the **skrillex** pool across **291
  commits** on all 3 branches; heads at rewrite: `very-experimental` **cd19e3f** · `master`
  **d0cface** · `experimental` **25040bb** (remote heads re-checked 2026-11-07 with
  `git ls-remote origin` — exact match; the local clone's remote-tracking refs lag them).
- Branch tip messages still look normal and author fields were not the rewrite target — expected
  per wave-13 §2. Do **not** flag normal tip messages as corruption or as a failed rewrite.

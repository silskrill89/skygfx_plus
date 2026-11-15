# Systematic Roadmap — consolidated status (2026-11-02)

Consolidates the exp-2 systematic fix roadmap
(`docs/baseline/2026-11-02-test11/findings.md` §4) with CURRENT status per item.
Global acceptance gate for every item: **no regression at the
2026-11-02-stable-config** (`docs/baseline/2026-11-02-stable-config/README.md`).

Status vocabulary: **LANDED** (fix merged, acceptance probe pending re-shoot) ·
**IN FLIGHT** (specialist lane active) · **OPEN** (not started).

---

## A — White blowout (S1) — **LANDED (fix-33) + GodRays lane IN FLIGHT**

- exp-2 root-cause ranking: H-A1 additive accumulation across the ~16×/frame
  multi-render into an unrepainted raster (CClouds::Render first draw of every
  scene pass, ONE/ONE additive over the chronic scene_start diverge); H-A2
  weather 9→2 max cloud coverage at cloud altitude; H-A3 negative TC fogStart.
- **fix-33 landed** — TC input sanitization (see "Cross-cutting" below) removed
  the unvalidated-input confounder feeding the blowout window.
- **GodRays lane in flight** — the godRays/shaft additive path is the remaining
  additive-accumulation suspect on the aerial route; lane owns the
  bounded-contribution clamp (exp-2 fix step 3).
- Acceptance probe (unchanged): fly the 18:00→22:00 route through the weather
  9→2 transition at altitude — (a) no full-white frame; (b) `[TC] fogStart`
  clamp receipt prints once; (c) scene_start DIVERGE count → 0; (d) night city,
  not white, in the 21:36/21:50/22:11 shot set.

## B — scene_start repair / see-through world (S1) — **LANDED (wave-12 scene-repair lane) + acceptance probe pending**

- exp-2 root cause: previous frame's postfx composite tail leaves the raw
  device at {ALPHABLEND=1, ZWRITE=0, ZTEST=0, ZFUNC=ALWAYS} while the rw cache
  holds the world baseline; cache-parity-keyed repairs never fire before the
  first building-pipe entry → sky/clouds/coronas draw with depth off.
- Fix shape (from exp-2): promote the rs_mon `scene_start` *detection* into a
  *repair* — canonical entry block {blend OFF, ZWRITE ON, ZTEST ON, ZFUNC
  LESSEQUAL} through all three layers, once per presented frame, before
  CClouds; fold the ped lane's ad-hoc ZWRITE force into the same bracket.
- **Landed in build 11** (wave-12 scene-repair lane, commit `84ef232`; see
  `.serena/memories/wave-13-record.md` §1 lane 2). Acceptance probe below is
  the remaining close-out step.
- Acceptance: `rs_mon: DIVERGE site=scene_start` → 0 over 5 min;
  `[PED] ZWRITE off/desynced` → 0; shot-13 scene re-shoot: no void wedge,
  markers occluded by buildings.

## C — Sea edge / ocean surface (S2) — **LANDED (fix-36)**

- exp-2 root cause: water routed correctly through the mod pipe (zero faults);
  the flat/razor-shore output was the shader's own sampling — broken tier-3
  parallax inputs, the fixed-function fog bracket fighting the shader horizon
  term, and shore-foam loaded but never blended.
- **fix-36 landed**: swell 0.54 m + shore gate + wind sea — ocean shows
  displacement/texture detail near shore, the waterline is gated (no
  razor-straight boundary), and the wind-sea term breaks the flat far plane.
- Remaining acceptance: re-shoot shots 7/9/14 scenes — foam transition at the
  waterline, far-water fades smoothly into the horizon colour.

## D — Cloud lane (S2) — **LANDED (fix-37) + H1 lane IN FLIGHT**

- exp-2 root cause: vanilla additive cloud sprites oversized/overbright at
  cloud=1.00 (slab = sprite quad at close range; ground puffs = same deck from
  below) plus the `[FxAlpha] Clouds` vertex-alpha drift burst.
- **fix-37 landed** — cloud lane geometry/coverage repair.
- **H1 lane in flight** — H-A1's additive-accumulation clamp for the cloud
  deck (per-sprite screen-coverage fade so inside-the-deck cannot exceed a
  bounded additive term); also closes the Clouds-layer FxAlpha drift.
- Acceptance: fly through the deck 18:00–21:00 — deck with soft edges, no
  full-screen slab, no ground-level puffs; `[FxAlpha] Clouds` drift count → 0.

## E — Height-fog veil / inert pass (S2) — **IN FLIGHT**

- exp-2 root cause (code-confirmed): `postfx.cpp` `DrawHeightFog` substitutes a
  forced 0.0015 density when INI `heightFogDensity = 0` (documented "zero
  density = inert"), so the pass is never inert; colour = skyBot → the sepia
  veil at aerial distance (53 % @500 m, 78 % @1 km, 95 % @2 km). Negative
  `tc.fogStart = -29` silently skips the TC-scale term.
- Fix shape: honour `heightFogDensity = 0` as OFF (bit-exact no-op; move the
  0.0015 default to the INI parser default), one-shot clamp+log for negative
  fogStart, optional aerial distance-ramp cap (~35 % max without explicit INI).
- Acceptance: with `heightFogDensity = 0` → `[PF] HeightFog gate -> SKIP` (or
  zero DrawHeightFog lines) and shots 5–10's veil gone; explicit small density
  returns the veil proportionally. Un-confounds every future aerial shot.

## F — Rotor / fast-geometry motion smear (S3) — **IN FLIGHT**

- exp-2 root cause: velocity buffer sees camera-relative motion only; rotor
  rotation is per-object motion it never sees → `MotionBlur_Burnout` smears
  along wrong/giant vectors. The cut classifier (`posDelta` 15–24 with
  fwdDot≈0.98) misreads fast straight flight as a cut, clearing velocity every
  frame → flickering smear.
- Fix shape: cut test requires BOTH large posDelta AND low fwdDot (or a
  teleport-scale threshold); clamp blur for rotor-class motion magnitudes.
- Acceptance: hover + fast straight flight in the Hunter — no giant fan beyond
  the rotor disc; `camera CUT reset` count during straight flight → 0.

## G — Untextured white ground planes (S2) — **LANDED (fix-34)**

- exp-2 root cause: stage-0 diffuse bind failure on the ground path (11-01 D9);
  the bind-elision cache claimed parity (`dev_mismatch=0`) while rs_mon showed
  23k transient state changes — the elision cache and the monitor disagreed
  about "same state" across the 3 layers.
- **fix-34 landed**: TAC_UNKNOWN safe default — the texture-classify path no
  longer trusts an unknown classification for the ground/terrain material
  class; unclassified binds fall back to the safe (bound) path instead of the
  elided (white) one.
- Remaining acceptance: re-shoot shot-13's beach — sand textured at all
  distances, no white sheets; `[bind-elision] dev_mismatch` reporting live.

## Cross-cutting — TC input validation (small) — **LANDED (fix-33 CfSanitize)**

- exp-2 finding: `fogStart = -29` reached every consumer unvalidated.
- **fix-33 CfSanitize landed**: one-shot per-transition sanity receipt + clamp
  in the TC read path (colour-filter print site) for `fogStart`,
  `shadowStrength`, cloud coverage outside [0,1].
- Acceptance: clamp receipt prints on every bad transition; no consumer ever
  sees a negative fogStart.

---

## CJ bug wave — families A/B/C (2026-11-02/03) — **ALL THREE FIXED ON DISK (unbuilt)**

User-reported on build 11 (stable-config lineage). All three families root-caused
and fixed in the working tree by the CJ wave (2026-11-02/03). The fixes are **NOT in
the deployed ASI**: test12 forensics (`docs/baseline/2026-11-03-test12/findings.md`)
proves build 11 ran with 0 `[PedZW]` / `[BldZW] parked` receipts ("receipts absent =
bugs live"), so the test12 log + 12 shots are the **pre-fix A-side baseline** for the
next build's A/B.

Family-letter note: this section now uses the **test12 findings numbering** —
A = angle transparency, B = buildings-over-CJ, C = inside-out player. (The original
dispatch table below swapped B/C; the fix lanes and findings §2 use this numbering.)

| Family | Symptom (test12 specimens) | Fix on disk | Owner files | Status |
|---|---|---|---|---|
| **A** | Angle-dependent partial transparency — see-through car (shot 4), CJ torso/head gone (shot 7), ghost ped/cars (shot 3) | (1) **texalpha deterministic classification** — first-sight scans always run, per-raster stable class, cyclic-LRU promotion, `budget=` demoted to scan-rate telemetry (kills the budget-skip `TAC_UNKNOWN` flip-flop that made the same texture class depend on draw order/angle); (2) **pedOpaqueEnforce unconditional** — canonical opaque signature (ALPHATEST GREATEREQUAL@1 + CULLBACK) forced through all 3 layers at ped entry, no longer gated on `pedOpaqueContract` | `src/render/texalpha.{cpp,h}`, `src/core/main.cpp` (`pedOpaqueEnforce`) | **FIXED ON DISK** (unbuilt) |
| **B** | Buildings/props draw over CJ — grass billboards paint over Glen Park pond (shot 5), translucent band across Tahoma scene (shot 3) | (1) **M2 carve** — `decalBlend == blendOn` structural coverage (any blended mesh = carve risk), blended building meshes ZWRITE-parked 3L + mesh-loop-end restore so nothing carves a plane that later rejects CJ/peds (`[BldZW] … ZWRITE parked` / `loop-ended` receipts); (2) **ZTEST/ZFUNC contracts** — ped-entry depth trio ZTEST=1 / ZENABLE=1 / **ZFUNC=LESSEQUAL** forced unconditionally 3L-paired, closing the deployed build's ZWRITE-only gap (`[PedZW] depth drift on entry`); (3) **sticky depth** — exit leaves canonical ZTEST=1 ZFUNC=LESSEQUAL behind instead of faithfully replaying a divergent capture (`[PedZW] sticky depth repair at exit`) | `src/render/buildingPipe.cpp`, `src/core/main.cpp` (ped entry/exit) | **FIXED ON DISK** (unbuilt) |
| **C** | Inside-out player — white faceted interior mesh at 0 m (shot 11), whole-player flat-white inverted shading (shot 12) | (1) **renderCallBack at +0x48** (byte-verified via RpAtomicCreate 0x749C50 / clump walk 0x749B20; the old +0x10 write clobbered `RwObjectHasFrame.sync`); (2) **0x804EF0 splice removed** — frame-setting the CLUMP spliced clump+8 atomicList into frame->objectList → `RpClumpForAllAtomics` returned garbage atomics, Dismember wrote physics transforms through random pointers and poisoned the engine dirty-list (the build-11 inside-out/transform corruption); (3) **Dismember frame/matrix offsets** — frame read at atomic+**0x04** (object.parent; +0x08 is lFrame.next) and modelling matrix EMBEDDED at frame+**0x10** (the old `(RwMatrix**)(frame+0x14)` read right.y/right.z floats as a pointer) | `src/entities/sftbdyphsx.cpp` (BuildGibClump / Dismember_Render), `src/extras/ragdoll_death.cpp` | **FIXED ON DISK** (unbuilt) |

Acceptance (all three families): integrated build + deploy → `[PedZW]` /
`[BldZW] … ZWRITE parked` receipts appear (receipt-gate rule); test12 shots
4/5/7/11/12 clear on re-shoot of the same routes (Glen Park pond + Tahoma +
swimming); stable-config no-regression
(`docs/baseline/2026-11-02-stable-config/README.md`).

Family-A discriminator still open: if shot 7 (missing torso/head) survives A's two
fixes, the residual is hypothesis A-2 (L3 alpha-enable replay) → item N1 below
(distinguish test: TAC-only fix clears it ⇒ A-1, else N1 rule needed).

Context for pickup (unchanged): ped/skin path = `2026-10-31-peds-pipe-design.md`; ped
alpha contract + `TAC_UNKNOWN` opacity history = `.opencode/memory-bank/activeContext.md`
Wave 5 section (PedOpaqueContract / pedOpaqueContract); L1/L2/L3 park-and-set doctrine
= activeContext "Known root causes"; CJ ghost lineage = `chars_classifyClump` never
called → SSS composite gated (same list). Wave records:
`.serena/memories/wave-13-record.md` §4 (NEVER-REVERT) +
`.serena/memories/wave-14-record.md` (CJ-wave outcomes + test12 baseline).

## NEW open items — test12 baseline (2026-11-03, `docs/baseline/2026-11-03-test12/findings.md`)

Not covered by the A/B/C fixes above. Each row: owner file + acceptance probe + status.
N1/N2 share one architectural root with the CJ wave ("faithful restore replays the
capture that IS the leak") and are the natural next lane after the CJ fixes ship.

| # | Item | Mechanism / evidence | Owner file | Acceptance probe | Status |
|---|---|---|---|---|---|
| **N1** | `[PipeAlpha]` faithful-restore L2/L3 replay (81×) | Exit faithfully replays a divergent alpha capture (`enable L2=1 L3=0` — device ALPHABLENDENABLE OFF while rw+drv say ON; 81 of 188 parity-6 replays) into every later draw — the alpha twin of the sticky-depth problem F1/ZTEST-ZFUNC contracts fixed for Z state | `src/render/pipelinecommon.cpp` `pipeExitAlphaMode` (faithful-restore, parity 6) | `[PipeAlpha] faithful restore (parity 6): … enable L2=1 L3=0` count → 0 (divergent ALPHABLENDENABLE captures never replayed — sticky rule extended to alpha + cull exits) | **OPEN** |
| **N2** | zfunc=8 dead-depth-test (106/127) | `[zfix]` full drift `rw ztest=0 drv zen=0 zfunc=8 dev zen=0 zfunc=8` (= D3DCMP_ALWAYS) at pipe entry, 106 of 127 repairs, every ~2 s. Repairs are throttled ⇒ ~120 frames between repairs run with dead depth testing (screenshot sampling windows). Drift **source** unidentified — family B forces the trio at ped entry but the leak is still re-created between entries | `src/render/pipelinecommon.cpp` (`pipeEnforceDepthContract` / `[zfix]` receipts), `src/render/renderstate_monitor.cpp` (persistent-vs-transient verdict) | `[zfix]` full-drift (`ztest=0 zfunc=8`) recurrence → 0 — drift never re-created, not merely repaired inside the 2 s throttle window | **OPEN** |
| **N3** | FAM-Q giant milky sprites | Soft glow/sheet/quad painted as huge milky solids (shots 1,2,3,4,8,9,10; dome = shot 10 ⅔-frame) — soft-sprite misclassification (TAC soft-decal fallback "painted solid") | `src/render/texalpha.{cpp,h}` (soft-decal class), `src/core/main.cpp` (sprite state/flush shields) | Re-shoot: no giant milky quad/dome specimen (test12 #9/#10 class gone); soft decals render soft-edged | **OPEN** (family A's deterministic TAC likely shrinks it — measure before adding fixes) |
| **N4** | FAM-G grass corruption | Yellow streak haze over lawns, billboards as jagged shard slabs (shots 4,5,12) — cutout/faded misclassification + streaked sampling; grass-over-water (shot 5) is also family B carve side | `src/render/grasssys.{cpp,h}`, `src/render/texalpha.{cpp,h}` | Re-shoot Glen Park: grass billboards crisp (no shard slabs / streak haze), no grass-over-water depth artifact | **OPEN** |
| **N5** | FAM-N near-black palms | Palm trunks near-black w/ yellow banding; washed-white buildings (shots 4,5,12) — plausibly normal-buffer/IBL side, or the C-family cull inversion hitting foliage | `src/render/buildingPipe.cpp` (normal/IBL lane), `src/render/pipelinecommon.cpp` (light uploads) | Re-shoot: palm trunks lit at ambient level (no near-black / yellow banding); pale buildings read as albedo, not washout | **OPEN** (needs its own sweep) |
| **N6** | FAM-T missing textures | Pink untextured pillar (#1), magenta wall scribble (#2) — missing/misnamed texture or TAC demotion | `src/texdefs.cpp` (texture load/classify), `src/render/texalpha.{cpp,h}` | Re-shoot same routes: no pink/magenta untextured surface | **OPEN** (minor) |
| **N7** | FAM-W bounded whiteout | Scene globally milky/low-contrast at daylight 11:23–13:53 (shots 1–7, 12) — continues the test11 whiteout thread (timecycle/fog) but **bounded**: HUD + colours survive, no 100 % white collapse | `src/render/postfx.cpp` (timecycle/fog bracket), `src/core/weather.cpp` | Bounded level holds on re-shoot (never a full-white collapse); un-confounded measurement once item E (height-fog gate) lands | **KNOWN / ACCEPTED** (bounded; tracked, not a ship gate) |

## Remaining backlog (not in the A–G families)

| Item | Scope | Notes |
|---|---|---|
| **peds_pipe Phase 3** | `src/render` ped lane | Phase 3 of the peds pipe design (`docs/plans/2026-10-31-peds-pipe-design.md`); rides the B bracket for entry-state repair. |
| **Water M4–M9** | water overhaul milestones 4–9 | Per `docs/plans/2026-10-30-water-overhaul-spec.md`; fix-36 covers the sea-edge slice, M4–M9 are the remaining milestones. |
| **G1 vignette round-trip** | postfx vignette | Vignette params round-trip (INI ↔ menu ↔ shader constants) follow-up to the G-family ground work. |
| **Address migration P1** | RW address table | P1 wave of the hardcoded-address migration (`docs/plans/2026-11-02-wrapper-migration.md`); prerequisite for non-1.0-US exe support. |
| **GPU particles lane** | particles | **Module shipped in build 11** (`src/render/particles_gpu.*`, `WakeParticlePS.hlsl`); further emitter work rides on it. |

## Perf lane (this session)

- **PERF R5** — font pin fast path (`src/core/main.cpp` CloudRegionFlush_7273D0
  hook): entry==canonical device blend state skips the park-and-set + restore.
- **PERF R6** — Forward+ rebuild-before-fallback reorder
  (`src/render/forwardplus.cpp`): cheap light-count check first; the full
  sort/merge/view-transform/cull is skipped on empty-drain frames (fallback
  fired 361/368 windows with all-zero counts).
- **PERF R2** — debug overlay default OFF (`debugOverlay` INI default 0; F9
  still toggles): removes the full ImGui frame (3–7 % CPU) from every game
  frame unless the overlay is explicitly enabled.

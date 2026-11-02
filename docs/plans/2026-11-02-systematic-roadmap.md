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

## B — scene_start repair / see-through world (S1) — **IN FLIGHT**

- exp-2 root cause: previous frame's postfx composite tail leaves the raw
  device at {ALPHABLEND=1, ZWRITE=0, ZTEST=0, ZFUNC=ALWAYS} while the rw cache
  holds the world baseline; cache-parity-keyed repairs never fire before the
  first building-pipe entry → sky/clouds/coronas draw with depth off.
- Fix shape (from exp-2): promote the rs_mon `scene_start` *detection* into a
  *repair* — canonical entry block {blend OFF, ZWRITE ON, ZTEST ON, ZFUNC
  LESSEQUAL} through all three layers, once per presented frame, before
  CClouds; fold the ped lane's ad-hoc ZWRITE force into the same bracket.
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

## Remaining backlog (not in the A–G families)

| Item | Scope | Notes |
|---|---|---|
| **peds_pipe Phase 3** | `src/render` ped lane | Phase 3 of the peds pipe design (`docs/plans/2026-10-31-peds-pipe-design.md`); rides the B bracket for entry-state repair. |
| **Water M4–M9** | water overhaul milestones 4–9 | Per `docs/plans/2026-10-30-water-overhaul-spec.md`; fix-36 covers the sea-edge slice, M4–M9 are the remaining milestones. |
| **G1 vignette round-trip** | postfx vignette | Vignette params round-trip (INI ↔ menu ↔ shader constants) follow-up to the G-family ground work. |
| **Address migration P1** | RW address table | P1 wave of the hardcoded-address migration (`docs/plans/2026-11-02-wrapper-migration.md`); prerequisite for non-1.0-US exe support. |
| **GPU particles lane** | particles | GPU-resident particle lane (emitters → compute-style atlas update within SM3.0 constraints). |

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

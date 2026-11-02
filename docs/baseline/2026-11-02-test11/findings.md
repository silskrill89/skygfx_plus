# Screenshot Forensics — 2026-11-02 "build 10" test (15:12–15:29 session, 14 shots)

READ-ONLY forensics. No source file was modified; no build was run.

**Evidence base**
- Screenshots: `C:\Users\aaaaaaaaa\Documents\ShareX\Screenshots\2026-11\gta_sa_*`, newest 14 of the
  15:13–15:29 batch (the 12:38–12:42 batch belongs to the earlier stable-config test and was skipped
  per directive; shots before 20:00 user-local excluded).
- Run log: `E:\games\gtasa_skygfx_plus\skygfx_dbg.log` — single run 15:12:46.830 → 15:29:41.098,
  clean `DLL_DETACH`, 81,246 lines.
- Binary: `skygfx.asi` 2,536,448 bytes, mtime 2026-11-02 15:12:23 (≠ Nov 1 build 2,528,768 — new
  binary). Boot banner: `skygfx build corona-flush-shield (CoronaBlend entry hook + flush-window
  close at SpriteFlushIndexedDraw_hook + corona_install verification)`.
- Live INI: `E:\games\gtasa_skygfx_plus\skygfx.ini`.
- Prior baselines: `docs/baseline/2026-11-02-stable-config/README.md` (config),
  `docs/baseline/2026-10-30-test2-0438/findings.md`, `docs/baseline/2026-11-01-fonts/findings.md`.

**CONFIG DRIFT vs the stable baseline (2026-11-02-stable-config) — this test did NOT run at the
declared stable config:**

| Key | stable-config README | this test |
|---|---|---|
| `colorFilterEnable` | 0 ("deliberate — do not fix back on") | **1** (CF_Modern grading ran all session) |
| `dofEnable` | 1 | **0** |

Everything else checked matches the baseline (pipeline PBR / buildingPBR / vehicle Modern /
waterStyle V q3 / bloom 1 / motionBlur 1 (strength 0.028) / velocityBuffer 1 / sunShafts 1 /
godRays 1 / tonemapAutoExposure 1 / cloudRenderFix 1 / pedAlphaTest 1 / forwardPlus 1 /
debugOverlay 1). Future A/B tests must re-pin the baseline or record the delta up front.

**Correlation method.** The F9 overlay prints `F <frameId> | C 0x<code>`; every log line carries the
same pair as `[#<frameId>:<code>]` (`code = (frameId & 0x0FFFFFFF) | (pipeline<<28)`; top nibble 0 =
PBR in all 14 shots). ShareX mtimes are second-precision, so each shot was bracketed by the first
log line at/after its mtime (sub-second, ≤ ~300 RW ticks). Overlay-read digits where legible agreed
with the log bracket to within that offset; **the log-bracket values below are authoritative**.
Note: the RW renderFrame tick advances ~16× per presented frame (water reflection + env sphere +
IBL + mirror sub-renders), so F deltas between shots are not frame counts.

---

## 0. TL;DR

1. **The white blowout is real, new, and time-anchored**: the world went from "sepia-veiled but
     readable" (F 972435, 15:27:13) to **100 % white** (F 1006177, 15:28:12) and stayed white to
     session end. Onset sits exactly on the completed **weather 9→2 timecycle transition**
     (`fog=-29` — a NEGATIVE fogStart — and `cloud=1.00`) while the camera flew fast at altitude.
     The white is hue-preserving with soft ghosts (not flat untextured silhouettes), the zenith sky
     stays dark (thin dark strip, shot 1) and the HUD is immune → the white is a bright
     translucent/accumulated layer in the scene raster, carried through the (bounded) tonemap.
2. **The see-through family is a depth/blend state leak with a standing log receipt**: every
     monitored sample of the whole session reports
     `rs_mon: DIVERGE site=scene_start | ALPHABLEND rw=0 drv=0 dev=1 | ZWRITE rw=1 dev=0 |
     ZTEST rw=1 dev=0 | ZFUNC drv=4 dev=8` — the raw device enters the scene with blend ON, depth
     test/write OFF, ZFUNC=ALWAYS while the rw cache disagrees (so cache-parity checks pass and
     nothing repairs until the first building-pipe entry — *after* sky/clouds/early draws).
     `[PED] ZWRITE off/desynced on entry` fires in the blowout window.
3. **Fonts are FIXED this build** (Nov 1 D1/D2/D6 closed): the repurposed 0x7273D0 hook pins the
     glyph blend (`[font_flush] gate pinned=245912 empty=0`); the Vigilante help box and the
     "HUNTER" vehicle name are fully legible (shot 13). Do-not-regress item.
4. The ocean renders as a **flat untextured plane with a razor-straight shoreline** in 6/14 shots
     (water routes through the mod pipe correctly — `flatpath → Water_VS+parallax PS, foam live`,
     zero water faults — so this is shader output, not routing).
5. The **height-fog pass is never inert**: INI `heightFogDensity = 0` is documented "zero density =
     inert" in the stable-config baseline, but `postfx.cpp` substitutes a forced 0.0015 default and
     paints `skyBot` colour — 53 % veil at 500 m, 78 % at 1 km, 95 % at 2 km — which is the
     sepia/pale veil over shots 5–10 and a major confounder for every aerial screenshot.
6. Vehicle PBR, coronas, sun flare, HUD, exposure CPU clamp (exp 1.13–1.16 within [0.15, 2.512])
     are healthy all session.

---

## 1. Per-shot defect table (newest first)

Severity: S1 unusable / S2 major regression / S3 noticeable artifact / S4 cosmetic.

| # | File (mtime) | F \| C (log bracket) | In-game | Scene | Defects (sev) |
|---|---|---|---|---|---|
| 1 | gta_sa_uMLeIJ6nhB.png 15:29:25 | 1027480 \| 0xFAD98 | 22:11 | Aerial, night | **COMPLETE WHITE BLOWOUT** — 100 % white frame, only a thin dark zenith strip at the very top edge; HUD/radar perfect (S1) |
| 2 | gta_sa_jENKG75i6c.png 15:29:04 | 1013498 \| 0xF76FA | 21:50 | Aerial, night | Near-total white wash; faint hue-preserving ghosts (red/white plane centre, buildings+trees top, yellow marker sliver) (S1) |
| 3 | gta_sa_pWMxm86B2O.png 15:28:53 | 1009304 \| 0xF6698 | 21:39 | Aerial, night | Full white; faint vegetation ghosts only (S1) |
| 4 | gta_sa_cLneuBtUKm.png 15:28:12 | 1006177 \| 0xF5A61 | 21:36 | Aerial, night | Full white; faint red plane ghost (S1) — **first full-white frame** |
| 5 | gta_sa_t0idNpsNoI.jpg 15:27:13 | 972435 \| 0xED693 | 20:37 | Hunter over terrain | Heavy sepia/tan veil over everything; flat untextured terrain patches with hard polygon edges; dark tree silhouettes; white dot coronas on road (S2) |
| 6 | gta_sa_arM5aX5MrE.jpg 15:27:04 | 964587 \| 0xEB7EB | 20:28 | Hunter, Verona B. | **Giant translucent white/blue slab quad** (hard straight edges) covering upper-centre frame; sepia ground veil; washed terrain (S2) |
| 7 | gta_sa_d4sj764zat.jpg 15:26:54 | 955695 \| 0xE952F | 20:18 | Hunter, beach | **Sea edge broken**: ocean = flat grey-green untextured plane, hard shoreline; beach buildings washed white; red coronas OK (S2) |
| 8 | gta_sa_ZPcpLoffyc.jpg 15:25:26 | 857868 \| 0xD170C | 18:50 | Hunter, SF naval base | Flat pale ocean, razor-straight shoreline; damage smoke = flat white blob; heavy grey-blue haze (S2) |
| 9 | gta_sa_wgLt4OWyWa.png 15:25:15 | 852700 \| 0xD02DC | 18:39 | Hunter dive, ocean | Flat ocean, razor horizon line; **sky completely cloudless**; sun flare/lens OK (S2/S3) |
| 10 | gta_sa_xnjRrn7f1L.jpg 15:24:55 | 835740 \| 0xCC09C | 18:19 | Apache, SF coast | **Giant translucent rotor-blur smear** extending far beyond the rotor disc; flat washed ocean; hard shoreline (S3) |
| 11 | gta_sa_zrfUJXcb50.jpg 15:24:33 | 798238 \| 0xC2E1E | 17:58 | Hunter, EB docks | **Giant white cloud puffs at dock level** + hard-edged white quad bottom-centre; terrain under clouds see-through (S2) |
| 12 | gta_sa_hxPVZy2mOI.jpg 15:23:05 | 755165 \| 0xB85DD | 17:24 | Hunter, SF beach | Rotor smear arcs (both sides + below); streaky broken water texture near shore; hard-edged dark slab on terrain (S3) |
| 13 | gta_sa_quWNARQe5J.png 15:22:55 | 732911 \| 0xB2EEF | 17:15 | Hunter parked, beach | Ground = **untextured white sheet**; **black void wedge bottom-left (see-through terrain)**; sea indistinguishable from sand; **help box + "HUNTER" text fully legible (fonts OK)** (S2) |
| 14 | gta_sa_xrSnEjKIWC.jpg 15:21:30 | 697344 \| 0xAA400 | 16:57 | On foot, SF beach | Hard razor-straight waterline, zero foam transition; blotchy/streaky water texture; sand correct close-up (S2) |

---

## 2. Defect taxonomy (ranked by severity)

| Fam | Defect | Shots | Sev | Status vs prior baselines |
|---|---|---|---|---|
| **A** | **Complete white blowout** (whole scene white, hue-preserving ghosts, zenith strip dark, HUD immune) | 1,2,3,4 (+11 partial) | **S1** | **NEW** — absent from 10-30 and 11-01 sets |
| **B** | **See-through world / depth-state leak** — terrain void wedges, markers/vehicles/peds ghosting, draw-order violations | 11,13 (+ ghosts in 1–4) | **S1** | KNOWN family (10-30 checklist #1 FAIL, 11-01 D3) — **now has a standing per-sample log receipt** (scene_start DIVERGE) |
| **C** | **Sea edge / ocean surface broken** — flat untextured ocean, razor-straight shoreline, no foam transition, streaky near-shore texture | 7,8,9,10,12,14 | **S2** | KNOWN (10-30 F2 water family) — persists at waterStyle V q3 |
| **D** | **Cloud lane broken** — giant translucent slab quad (6), giant white puffs at ground level (11), otherwise empty sky (9) | 6,9,11 | **S2** | NEW scope (10-30 F7 was a horizon glow band; this is wrong cloud *geometry/coverage*) |
| **E** | **Height-fog veil** — sepia/pale wash over everything at aerial distances; forced density despite INI 0 | 5,6,7,8,9,10 | **S2** | KNOWN (10-30 D14 haze wall) — **root cause now identified in code** (forced 0.0015 substitute) |
| **F** | **Rotor / fast-geometry motion smear** — translucent fan far beyond the rotor disc | 10,12 | S3 | NEW scope (motion blur cameraAware existed; this magnitude is new) |
| **G** | Untextured white ground planes | 13 | S2 | KNOWN (11-01 D9, stage-0 diffuse bind family) — still open |
| — | Fonts (menu/HUD/vehicle-name text) | 13 (legible) | — | **FIXED this build** (11-01 D1/D2/D6 closed by the `[font_flush]` pin) |
| — | HUD/2D layer, vehicle PBR, coronas, sun flare, exposure clamp | all | — | Healthy (do-not-regress) |

Frequency: A 4/14 full + 1 partial · B 2/14 explicit + ghosting in all A shots · C 6/14 · D 3/14 ·
E 6/14 · F 2/14 · G 1/14.

---

## 3. Key log receipts (correlation backbone)

```
[15:24:59.099] [#838873] [Tonemap] TC ... shadow=104 fog=52  cloud=1.00 hour=18 wOld=3 wNew=15 wInterp=0.39   ← weather change #1 (veil worsens)
[15:27:11.210] [#970656] [Tonemap] TC ... shadow=93  fog=-29 cloud=1.00 hour=20 wOld=9 wNew=2  wInterp=0.60   ← weather change #2, NEGATIVE fogStart
[15:27:13]     [#972435]  last frame with readable world (shot 5)
[15:28:12]     [#1006177] first full-white frame (shot 4)
every 2s sample, whole session:
    rs_mon: DIVERGE site=scene_start pipe=-1 | ALPHABLEND rw=0 drv=0 dev=1 | ZWRITE rw=1 drv=1 dev=0
            | ZTEST rw=1 drv=1 dev=0 | ZFUNC rw=- drv=4 dev=8
[15:28:53]     [#1009375] [zfix] pipe entry depth drift repaired -> ZENABLE=TRUE ZFUNC=LESSEQUAL (rw ztest=0 dev zen=0)
[15:28:53]     [#1009376] [SKY] entry: DEV ps=00000000 vs=00000000 alphaTest=1 fn=5 ref=2 blend=1   ← probe DISPROVES the leftover-shader theory; blend=1 at sky entry
[15:28:53]     [#1009378] [CoronaShield] forced additive ONE/ONE ... [CoronaBlend] dev blend=1 src=2 dst=2
[15:25:35–15:26:34] [#860717–#914885] [FxAlpha] Clouds: layer drift BEFORE repair ×11 (dev vtx=1 vs rw/d3d vtx=0)
[15:12:46]     [#0] [cloud_flush_install] 0x719B2B op=E8 orig=007273D0 OK   ← intercept INSTALLED (the "MISMATCH = SKIPPED" text is the template tail, not the verdict)
[whole run]    [font_flush] gate pinned=245912 empty=0                      ← font pin working; [cloud_flush] gate 0 lines (hook repurposed)
[whole run]    [PF] HeightFog gate -> RUN; DrawHeightFog: density=0.0015 (tc=0.0000) colour=skyBot
               (0.42,0.57,0.71)@18:19 … (0.30,0.25,0.19)@20:28 … (0.05,0.12,0.15)@22:00   ← veil colour tracks skyBot, never white
[15:28:1x]     TonemapPass lumaTex 1D8131B0 → 1D8143C8 (adapt texel handle swap across the blowout onset)
[15:28:53]     [PipeChain] c1chk c1=(0.549,0.650,0.700) vs (0.0039,0.150,0.040) minutes apart   ← PipeChain c1 (fog-family colour) swings with weather
[15:28:53]     [water] flatpath site=2 → pipe route (Water_VS + parallax PS, foam live); zero water faults all run
[15:28:5x]     [PostFX] DrawVelocityBuffer: camera CUT reset (posDelta=15–24/9.00 fwdDot≈0.98–0.99) — repeated while flying fast
```

---

## 4. SYSTEMATIC FIX ROADMAP — "fix all of them systematically once and for all"

Order chosen by (a) blocks-testing-first, (b) cheapest-confounder-removal, (c) dependency.
Each family: root-cause hypothesis → owner file → fix → acceptance probe. **No regression at the
2026-11-02-stable-config is the global acceptance gate for every item.**

### Family A — WHITE BLOWOUT (NEW, S1) — fix order **1**

- **Root-cause hypothesis (ranked).**
  - **H-A1 (primary) — additive accumulation across the multi-render frame into an unrepainted
    raster.** The RW renderFrame tick advances ~16× per presented frame (water reflection sceneRT,
    env sphere, IBL, mirrors all re-render the scene). `CClouds::Render` is the FIRST draw of
    *every* scene pass (two call sites 0x53DCA2/0x53DFA0) and draws **ONE/ONE additive**; the
    chronic scene_start device diverge (blend=1, ZTEST=0, ZWRITE=0 — Family B) means those first
    draws blend onto whatever the raster already holds instead of a clean surface. If any sub-render
    leaves the main camera raster holding the *previous frame's postfx output* (PipeChain literally
    "repaints camera raster from pRasterFrontBuffer" at intensity 0.81 each frame), each scene pass
    adds another additive sky/cloud layer on top → ratchet to white. The bounded tonemap
    (Hable ≤ ~0.8 + sRGB) then faithfully carries the white with hue-preserving ghosts — exactly
    what shots 1–4 show.
  - **H-A2 — weather 9→2 maximises cloud coverage while the camera sits at cloud altitude.**
    `cloud=1.00`; main.cpp:5879-5882 itself documents that at cloud altitude each additive cloud
    sprite covers hundreds of pixels. Shot 6's giant slab and shot 11's ground-level puffs are the
    same deck seen from below/at level. Onset timing (transition completed between 15:27:13 and
    15:28:12) matches.
  - **H-A3 — negative timecycle fogStart (-29) escaping a consumer.** DynamicSky c7 saturates
    fogD→1.0 for fogStart≤1 (by design), heightFog skips its TC term — but any consumer using
    fogStart directly (PipeChain c1 swing 0.549→0.0039 across the onset is suspicious) may invert.
- **Owner files:** `src/core/main.cpp` (`CClouds__Render_hygiene`, scene-start bracket,
  RenderScene_before), `src/render/postfx.cpp` (PipeChain repaint / front-buffer resync),
  `src/render/pipelinecommon.cpp` (cloudsPS binding).
- **Fix (systematic, not spot):**
  1. Make the scene start **self-clean**: at the rs_mon `scene_start` site, *repair* (not just
     report) the canonical entry block {blend OFF, ZWRITE ON, ZTEST ON, ZFUNC LESSEQUAL} through
     all three layers — this is Family B's fix and it is also A's precondition.
  2. Guarantee the camera raster is **cleared or fully repainted** before the first scene pass of
     each presented frame (assert raster identity at CClouds entry: if camRas content is stale from
     postfx, clear once). Log a one-shot receipt when the clear fires.
  3. Clamp cloud sprite contribution for aerial cameras: scale additive cloud RGB by a
     distance/altitude factor so inside-the-deck cannot exceed a bounded additive term.
  4. Validate timecycle inputs once per weather transition: log + clamp `fogStart < 0` (one-shot
     `[TC] fogStart=-29 clamped` receipt).
- **Acceptance probe:** fly the same 18:00→22:00 route through the weather 9→2 transition at
  altitude. (a) No full-white frame; (b) `[TC] fogStart` clamp receipt prints once; (c) the
  scene_start DIVERGE receipt count → 0; (d) shot set re-taken at 21:36/21:50/22:11 shows the night
  city, not white.

### Family B — SEE-THROUGH WORLD / depth-state leak (pressing, S1) — fix order **2**

- **Root-cause hypothesis.** The previous frame's last postfx pass leaves the raw device in
  {ALPHABLEND=1, ZWRITE=0, ZTEST=0, ZFUNC=ALWAYS} (the composite/restore tail), while the rw cache
  still holds {blend=0, zwrite=1, ztest=1}. Because repairs are keyed on cache-parity, nothing
  repairs until the first building-pipe entry — but sky, clouds, coronas and early 2D-ish draws
  happen *before* that with depth test/write off → draw-order violations: terrain void wedges
  (shot 13), markers/vehicles/peds drawing through each other, clouds over terrain (shot 11).
  `[PED] ZWRITE off/desynced on entry (rw=0 dev=0)` is the same leak reaching the ped lane.
- **Owner files:** `src/core/main.cpp` (RenderScene_before / scene-start bracket; ped lane),
  `src/render/pipelinecommon.cpp` (shared entry-state repair).
- **Fix:** promote the existing rs_mon scene_start *detection* into a *repair* (canonical block,
  all 3 layers, once per presented frame, before CClouds). Add the ped lane to the same bracket
  instead of its ad-hoc ZWRITE force.
- **Acceptance probe:** (a) `rs_mon: DIVERGE site=scene_start` count → 0 over a 5-min run;
  (b) `[PED] ZWRITE off/desynced` → 0; (c) re-shoot shot-13's scene: no black void wedge, terrain
  solid to the horizon; (d) markers occluded by buildings.

### Family E — HEIGHT-FOG VEIL (S2, cheap high-leverage) — fix order **3**

- **Root-cause hypothesis (code-confirmed).** `postfx.cpp:4857-4859`: INI `heightFogDensity = 0`
  (documented "zero density = inert" in the stable-config baseline) is **substituted with a forced
  0.0015 default**, so the pass is never inert. Colour = `skyBot` (bright blue 0.42/0.57/0.71 at
  18:19, sepia 0.30/0.25/0.19 at 20:28). At aerial distances 1-exp(-0.0015·d) ≈ 53 % @500 m,
  78 % @1 km, 95 % @2 km → the veil dominating shots 5–10. Additionally `tc.fogStart = -29`
  (negative) silently skips the TC-scale term (`fogStart > 1.0` gate) with no receipt.
- **Owner files:** `src/render/postfx.cpp` (`DrawHeightFog` ~:4844-4900), `docs` for the INI key.
- **Fix:** (1) honour `heightFogDensity = 0` as OFF (skip the pass entirely — bit-exact no-op) and
  move the 0.0015 default to the INI parser default instead; (2) one-shot clamp+log for negative
  `tc.fogStart`; (3) optional: distance-ramp cap for aerial cameras so the veil cannot exceed ~35 %
  at any range without an explicit INI value.
- **Acceptance probe:** with `heightFogDensity = 0`: `[PF] HeightFog gate -> SKIP` (or zero
  DrawHeightFog lines) and shots 5–10's veil gone; with an explicit small density the veil returns
  proportionally. This single change un-confounds every future aerial screenshot.

### Family C — SEA EDGE / ocean surface (S2) — fix order **4**

- **Root-cause hypothesis.** Water routes correctly through the mod pipe
  (`flatpath → Water_VS + Water_Parallax PS, foam live`, zero faults), so the flat untextured
  output is the shader's own sampling: (H-C1) the tier-3 parallax path reading a broken/empty
  reflection RT (sceneRT/depthRT) or falling back to a constant tint; (H-C2) the water fog bracket
  (`tableMode=3 vertexMode=0 start=100 end=1600 density=1.0 color=0xFF000000`) fighting the
  shader's own horizon colour → the razor-straight boundary and banded/streaky near-shore texture;
  (H-C3) shore-foam loaded (256×256, mips=1) but never blended at the waterline.
- **Owner files:** `src/render/` water pipe lane, `shaders/ps/Water_Parallax.hlsl` (+ `Water_VS`).
- **Fix:** audit the tier-3 parallax inputs (reflection RT readiness + fallback), reconcile the
  fixed-function fog bracket with the shader's horizon term (single owner for the waterline
  colour), and wire the shore-foam blend to the shoreline depth delta.
- **Acceptance probe:** re-shoot shots 7/9/14 scenes: ocean shows normal/texture detail near shore,
  a foam transition at the waterline, no razor-straight boundary; far-water fades into the horizon
  colour smoothly.

### Family D — CLOUD LANE (S2) — fix order **5**

- **Root-cause hypothesis.** (H-D1) At `cloud=1.00` the vanilla cloud sprites (additive ONE/ONE,
  drawn first in the scene) render oversized/overbright — the slab (shot 6) is a sprite quad at
  close range, the puffs (shot 11) the same deck from below. (H-D2) The `[FxAlpha] Clouds` drift
  burst (dev vtx=1 vs rw/d3d vtx=0, 11× in the veil window) shows the cloud batches rasterising
  with vertex-alpha state the cache does not model → opacity wrong exactly when the weather deck
  thickens. (H-D3) `cloudsSkyPS` was removed ("created but never bound") — there is deliberately no
  full-screen sky entry; the slab must therefore come from the *sprite* path.
- **Owner files:** `src/core/main.cpp` (`CClouds__Render_hygiene`, cloud noise upload c49-c52),
  `shaders/ps/CloudsPS.hlsl` (sprite entry), `src/render/pipelinecommon.cpp`.
- **Fix:** (1) clamp per-sprite screen coverage (RHW sprites beyond N hundred px get faded —
  prevents the full-screen slab); (2) close the Clouds-layer FxAlpha drift (repair dev vtx to match
  the batch's intent *inside* the cloud window); (3) derive c49.z coverage from
  `CWeather__CloudCoverage` so weather drives the field (the header's own "reported hunk" note).
- **Acceptance probe:** fly through the deck at 18:00–21:00: clouds read as a deck with soft edges,
  no full-screen slab, no ground-level puffs; `[FxAlpha] Clouds` drift count → 0.

### Family F — ROTOR / fast-geometry motion smear (S3) — fix order **6**

- **Root-cause hypothesis.** The velocity buffer captures camera-relative motion only; rotor
  rotation is per-object motion it never sees, so `MotionBlur_Burnout` smears along wrong/giant
  vectors around fast-rotating geometry. The repeated `camera CUT reset (posDelta=15–24, fwdDot
  ≈0.98)` while flying fast clears velocity every frame (fwdDot≈1 is NOT a cut — the posDelta
  threshold misclassifies fast straight flight as a cut), making the smear flicker.
- **Owner files:** `src/render/postfx.cpp` (`DrawVelocityBuffer`, cut classifier),
  `shaders/ps/MotionBlur_Burnout.hlsl`.
- **Fix:** (1) cut test must require *both* large posDelta AND low fwdDot (or a teleport-scale
  threshold — 15–24 units/frame is fast flight, not a cut); (2) clamp blur for pixels whose motion
  magnitude exceeds a physical bound (rotor-class) instead of smearing full-strength.
- **Acceptance probe:** hover + fast straight flight in the Hunter: no giant fan beyond the rotor
  disc; `camera CUT reset` count during straight flight → 0.

### Family G — untextured white ground planes (KNOWN, S2) — fix order **7**

- **Root-cause hypothesis.** Stage-0 diffuse bind failure on the ground path (11-01 D9, still
  open). `[bind-elision] bld hits=43 writes=36245 dev_mismatch=0` claims parity while
  `rs_mon_report` shows 23k *transient* (entry-only) state changes — the elision cache and the
  monitor disagree about what "same state" means across the 3 layers.
- **Owner files:** `src/render/buildingPipe.cpp` (bind-elision + 3-layer stage-0 bind).
- **Fix:** make the elision cache verify against L3 (raw device) at window boundaries (the
  `dev_mismatch` counter must be able to go non-zero), or disable elision for the ground/terrain
  material class until the drift is understood.
- **Acceptance probe:** re-shoot shot-13's beach: sand textured at all distances; no white sheets;
  `[bind-elision] dev_mismatch` reporting live.

### Cross-cutting: timecycle input validation (fix order **8**, small)

`fog=-29` reached every consumer unvalidated. Add a one-shot per-transition sanity receipt +
clamp for `fogStart`, `shadowStrength`, cloud coverage outside [0,1] in the TC read path
(`src/render/postfx_colourfilter.cpp:531` print site is the natural home). Acceptance: the clamp
receipt prints on every bad transition; no consumer ever sees a negative fogStart.

---

## 5. Do-not-regress (verified correct in this set — protect all of it)

- **Fonts**: help box + "HUNTER" name legible (shot 13); `[font_flush] gate pinned=245912 empty=0`.
  The repurposed 0x7273D0 hook is the fix — do not re-force corona ONE/ONE there.
- HUD/2D layer (icons, clock, money, health, radar, F/C overlay) — perfect in all 14.
- Vehicle PBR (Hunter/Apache textured + shaded in every shot; `pipeUploadPBR` c22/c23 receipts
  healthy; ambient (0.08,0.10,0.13) at night).
- Coronas (red/white dots render, placement sane), sun flare/lens (shot 9).
- Exposure CPU clamp (exp 1.13–1.16 within [0.15, 2.512] all run).
- Clean session teardown (`DLL_DETACH complete`, no crash dump this run).

---

## Appendix — shot index with correlation

| # | File | mtime | Overlay read | Log bracket [#F:C] | Δ |
|---|---|---|---|---|---|
| 1 | gta_sa_uMLeIJ6nhB.png | 15:29:25 | F 1027749 C 0xFAEA5 | [#1027480:000FAD98] | ~269 ticks (sub-second) |
| 2 | gta_sa_jENKG75i6c.png | 15:29:04 | F 1013706 C 0xF77CA | [#1013498:000F76FA] | ~208 |
| 3 | gta_sa_pWMxm86B2O.png | 15:28:53 | F 1009621 C 0xF67D5 | [#1009304:000F6698] | ~317 |
| 4 | gta_sa_cLneuBtUKm.png | 15:28:12 | F 1006346 C 0xF5AEC | [#1006177:000F5A61] | ~169 |
| 5 | gta_sa_t0idNpsNoI.jpg | 15:27:13 | F 972325 C 0xED625 | [#972435:000ED693] | ~110 |
| 6 | gta_sa_arM5aX5MrE.jpg | 15:27:04 | F 964833 C 0xEB8ED | [#964587:000EB7EB] | ~246 |
| 7 | gta_sa_d4sj764zat.jpg | 15:26:54 | F 955801 C 0xE9599 | [#955695:000E952F] | ~106 |
| 8 | gta_sa_ZPcpLoffyc.jpg | 15:25:26 | F 937829 C 0xD1E55 | [#857868:000D170C] | overlay digits misread (tiny font) — log bracket authoritative |
| 9 | gta_sa_wgLt4OWyWa.png | 15:25:15 | F 932325 C 0xD0165 | [#852700:000D02DC] | " |
| 10 | gta_sa_xnjRrn7f1L.jpg | 15:24:55 | F 935810 C 0xC0DE2 | [#835740:000CC09C] | " |
| 11 | gta_sa_zrfUJXcb50.jpg | 15:24:33 | F 798717 C 0xC2FFD | [#798238:000C2E1E] | ~479 |
| 12 | gta_sa_hxPVZy2mOI.jpg | 15:23:05 | F 755365 C 0xB8CB9 | [#755165:000B85DD] | ~200 |
| 13 | gta_sa_quWNARQe5J.png | 15:22:55 | F 734513 C 0xB3531 | [#732911:000B2EEF] | ~1602 (2 s mtime granularity) |
| 14 | gta_sa_xrSnEjKIWC.jpg | 15:21:30 | F 690093 C 0xAA6F2 | [#697344:000AA400] | ~7251 (mtime lags capture) |

All C top nibbles = 0 → pipeline 0 (PBR) in every shot. Where overlay-read and log-bracket differ,
the delta is the mtime/second-granularity offset plus the ~16 sub-renders per presented frame; the
log bracket is the reproducible reference.

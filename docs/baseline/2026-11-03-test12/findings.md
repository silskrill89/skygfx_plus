# Screenshot Forensics — 2026-11-02 "build 11" session (19:01–19:04, 12 shots)

READ-ONLY forensics. No source file was modified; no build was run. (This file is the only
deliverable written, per directive.)

**Evidence base**
- Screenshots: `C:\Users\aaaaaaaaa\Documents\ShareX\Screenshots\2026-11\gta_sa_*`, newest 12 by
  mtime (19:01:30 → 19:04:00 on 2026-11-02). `…\Screenshots\2026-12` does not exist.
- Run log: `E:\games\gtasa_skygfx_plus\skygfx_dbg.log` — single run 18:59:19.764 → 19:04:32.122,
  clean `DLL_DETACH`, 19,371 lines. Covers the whole screenshot window.
- Binary: `skygfx.asi` 2,562,560 bytes, mtime 2026-11-02 16:57:20, `built=Nov  2 2026 16:54:40`
  (build 11). Boot banner: `skygfx build corona-flush-shield`.
- Correlation method: F9 overlay `F <frameId> | C 0x<code>` ≈ log `[#<frameId>:<code>]`; ShareX
  mtimes bracketed to the first log line at/after capture. Overlay digits are best-effort reads;
  **mtime brackets are authoritative** (two overlay F values were inconsistent with their C codes —
  marked ≈ in the table).

**Deployed-fix status (the key gate)** — two fixes are committed-to-working-tree but NOT in the
deployed ASI (`git status`: modified `src/core/main.cpp`, `src/render/buildingPipe.cpp`,
`src/render/texalpha.{cpp,h}`):

| Fix | Files | Targets | Receipt it should emit | Status in this log |
|---|---|---|---|---|
| **F1 — ped depth-trio contract** (unconditional ZTEST/ZENABLE/**ZFUNC** at ped entry + sticky-depth exit repair; all-3-layer ZWRITE desync gate) | `src/core/main.cpp` | Bug B (draw-over-CJ) | `[PedZW] depth drift on entry` / `[PedZW] sticky depth repair at exit` | **0 receipts — NOT DEPLOYED → bug B live** |
| **F2 — TAC stability + blended-mesh ZWRITE-park** (first-sight scans always run; per-raster stable class; `[BldZW] parked/loop-ended/Switch-drift` receipts) | `src/render/texalpha.{cpp,h}`, `src/render/buildingPipe.cpp` | Bug A (angle-dependent alpha) + Bug B carve path | `[BldZW] PBR/Xbox: blended mesh … ZWRITE parked` | **0 receipts of the new formats — NOT DEPLOYED → bug A + carve path live** |

What *is* in the log (deployed code only): 128× `[BldZW] entry rw=… -> restore=…` (committed
format), 128× `[PedRS] ok …` exit monitor, 127× `[zfix]`, 128× `[cullfix]`, 188×
`[PipeAlpha] faithful restore (parity 6)`, 123× `rs_mon DIVERGE site=scene_start`,
50× `non-screen raster LIVE during building cb`, one `[DefPipe]` once-receipt (old wording — no
`zfunc=LE`, confirming the deployed build predates F1). texalpha summary still shows the OLD
budget-gate telemetry (`unknown_budget=99 … budget=99`, final: 1300 opaque / 58 cutout / 4 faded).

---

## 0. TL;DR

1. **All three reported bugs are live in these shots and both landed fixes are absent from the
   binary** — `[PedZW]` receipts: **0**; new-format `[BldZW]` receipts: **0**. Per the standing
   rule ("receipts absent = bugs live"), this session is a valid *pre-fix* baseline to A/B the
   next build against.
2. **(A) Angle-dependent partial transparency** shows in 4+ shots (see-through car 19:03:10, CJ
   torso+head gone 19:03:39, ghost ped/cars 19:02:50). Mechanism evidence in log: the old TAC
   budget-gate flip-flop (F2 root) **plus** a second, *un-fixed* layer: `[PipeAlpha] faithful
   restore` replays a **divergent alpha capture** 81× (`enable L2=1 L3=0` — device ALPHABLENDENABLE
   off while rw+drv say on) straight back into later draws.
3. **(B) Props/buildings draw over CJ** — strongest direct evidence is grass billboards painting
   over the Glen Park pond (19:03:17) and a translucent band across the Tahoma scene (19:02:50).
   Mechanism evidence in log: **106× full depth drift** `rw ztest=0 drv zen=0 zfunc=8 dev zen=0
   zfunc=8` — `zfunc=8` = **D3DCMP_ALWAYS** (depth test dead) at pipe entry, every ~2 s. The ped
   contract in the deployed build forces ZWRITE only, never ZFUNC — exactly the gap F1 closes.
4. **(C) Inside-out player/bandana** — the 19:03:54 shot shows a point-blank white faceted mesh
   mass (player interior facets) and 19:04:00 shows the player with flat white "wrong-side"
   shading. Mechanism evidence in log: persistent **cull tug-of-war** — 77× `rw=1 drv=1 dev=2`
   (rw cache wants CULLNONE, device culls BACK) and 44× `rw=2 drv=2 dev=1` (mirror). A double-
   sided ped part (bandana/hair cards) whose cull-none intent is overridden by a stale device cull
   loses its far side → see-through + interior visible → "inside-out", camera-angle dependent.
5. One shared architectural root for all three: **"faithful restore" replays captures that ARE the
   leak**, and the repairs run at *building/vehicle pipe entry* only — ped draws happen between
   repairs with no entry contract in the deployed build. F1/F2 close the ped + TAC halves; the
   PipeAlpha L3-replay and the cull-replay halves are **not yet covered by any landed fix**.

---

## 1. Per-shot catalogue (chronological)

Severity: H = breaks the frame / player unreadable · M = clearly wrong, playable · L = cosmetic.
"FAM" = other bug families (§3).

| # | File (mtime) | Overlay | Scene | A? | B? | C? | Other | Sev | Notes |
|---|---|---|---|---|---|---|---|---|---|
| 1 | `gta_sa_uYqP3dvpGv.jpg` 19:01:30 | F≈148275 | Aerial, gas-station intersection (cinematic cam) | possible (tiny CJ reads dark/ghostly) | possible (translucent green sheet over canopy) | — | FAM-Q (sheet), FAM-T (pink untextured pillar), FAM-W | L | CJ 20 px; low diagnostic value for A/B/C, but the giant translucent sheet is a clean FAM-Q specimen |
| 2 | `gta_sa_aluGathCd3.jpg` 19:01:35 | F≈152452 | Aerial intersection; quad + rider airborne, ped on sidewalk | rider limbs merge/detach | — | — | FAM-Q (floating grey quad right), FAM-T (magenta scribble on wall) | L–M | Rider/quad silhouette ambiguity is consistent with A but distance-limited |
| 3 | `gta_sa_dT0KF4sZVW.jpg` 19:02:50 | F≈227579 | Tahoma street, cinematic cam, traffic + ped down in road | cars slightly see-through; ghost ped | translucent band draws across whole scene (over geometry) | — | FAM-Q (mid-screen band), FAM-W | M | Log at capture: `PipeAlpha enable L2=1 L3=0` (19:02:50.369) + `zfix` full drift (19:02:50.809) + `cullfix rw=1 drv=1 dev=2` (19:02:51) — all three leak families active in-bracket |
| 4 | `gta_sa_lJUtY3IErn.jpg` 19:03:10 | F≈240673 | Glen Park pond, car on grass | **H: car body see-through (grass visible through metal)** | — | — | FAM-N (palms near-black w/ yellow banding), FAM-Q (white quad bottom-center), FAM-G (yellow streak haze), FAM-W | **H (A)** | Log at capture (19:03:10.382): `[PipeAlpha] faithful restore (parity 6): … enable L2=1 L3=0 (derived 1)` — L3 alpha-enable desync replayed at exit. Best A specimen in the set |
| 5 | `gta_sa_U2xyR6MPB1.jpg` 19:03:17 | F≈245603 | Glen Park pond edge, low cam | — | **H: grass billboards paint OVER pond water (depth order)** | — | FAM-G (grass = jagged shard slabs, streaked), FAM-N | **H (B)** | Log at capture: `cullfix rw=1 drv=1 dev=2` (19:03:17.007), `[BldZW] entry rw=1 drv=1 dev=1 -> restore=verbatim` (19:03:17.038), `non-screen raster LIVE` 512×512 building cb. Grass-over-water is the same ZWRITE/ZTEST carve family as draw-over-CJ |
| 6 | `gta_sa_o1KJ00y1jv.jpg` 19:03:34 | F≈269610 | Underwater, looking up at surface | — | — | — | FAM-W (heavy smear) | L | Refraction effect plausible; no clean A/B/C evidence |
| 7 | `gta_sa_pwMAngkvHf.jpg` 19:03:39 | F≈274700 (C-code ≈0x46414; read conflict) | CJ swimming at surface | **H: CJ torso + head completely missing — only pelvis/legs render** | water plane reads as solid slab over lower scene | — | FAM-W | **H (A)** | Log at capture (19:03:39.298): `[zfix] … rw ztest=0 drv zen=0 zfunc=8 dev zen=0 zfunc=8` — full dead-depth drift in the same 100 ms window. Partial-body invisibility = prime A specimen (angle/cull-dependent) |
| 8 | `gta_sa_LSOK4L4w3S.jpg` 19:03:44 | F≈279907 | Underwater, oyster pickup, bubbles | — | — | — | FAM-Q (giant flat white glow ellipse) | L | Bubble sprites oversized/milky; glow quad family |
| 9 | `gta_sa_ntoRoaT5Rj.png` 19:03:46 | F≈283287 (C 0x45297; overlay F read 286207 — conflict w/ #10) | Underwater, oyster text | — | — | — | **FAM-Q H: giant white haze blob fills upper half** | M | Soft glow/decals painted as one huge solid milky sheet — same soft-sprite misclassification family F2's TAC fix targets |
| 10 | `gta_sa_d4kTojbPWL.png` 19:03:49 | F=286207 (C 0x45DFF consistent) | Under surface, looking up | — | possible (dome edge occludes world) | — | **FAM-Q H: milky-white dome fills ⅔ of frame** | M | Same-frame neighborhood as #9; sharp curved edge = giant quad/sphere, not fog |
| 11 | `gta_sa_NOJOle4wDL.jpg` 19:03:54 | F≈291381 | Surfacing at pond, point-blank | — | — | **H: giant white faceted mesh mass at camera — interior facets of player mesh (inside-out at ~0 m)** | FAM-Q (bubbles), FAM-W | **H (C)** | Log window 19:03:55–57: `cullfix rw=1 drv=1 dev=2` ×2, `zfix` dev `zen=0 zfunc=8`, `PipeAlpha entry resync` — cull+depth+alpha all divergent in the exact window. Triangular facets = mesh geometry, not a sprite (distinguishes C from FAM-Q) |
| 12 | `gta_sa_r7sIrehZHM.jpg` 19:04:00 | F≈299366 | CJ running away on sidewalk, Glen Park | arms read thin/translucent | — | **M–H: whole player flat white/washed shading (inverted-normal signature); dark bandana/cap reads solid-dark vs pale body** | FAM-N (pale buildings), FAM-G (yellow streaks) | **M–H (C)** | Whole-player shading wrong from a *normal* third-person angle — matches "bandana + whole player inside-out". Log at 19:03:59.9: `[PBR-Ambient] absolute floor` only; the cullfix/zfix cadence is throttled 2 s so in-frame state is the bracketing divergence |

Shots 9/10 are two captures of the same game-frame neighborhood (same C-window; the two overlay F
reads disagree — one is an OCR misread; per test11 practice the mtime bracket is authoritative).

---

## 2. Bug-family evidence → mechanism mapping

### (A) Angle-dependent partial transparency
- **Shots:** 4 (see-through car, H), 7 (CJ torso/head gone, H), 3 (ghost ped/cars, M), 2/12 (weak).
- **Two mechanisms, both live:**
  1. **TAC class flip-flop (F2 covers this — un-built):** the deployed texalpha still gates
     first-sight scans on the per-frame budget; a budget-skipped draw takes the `TAC_UNKNOWN` safe
     default while a later draw of the *same* texture gets the real class. WHICH textures miss the
     scan depends on camera-angle draw order ⇒ exactly "transparent from different angles".
     Log signature: `budget=99` telemetry present in summary lines (the fix converts this to
     rate-only telemetry).
  2. **Alpha-enable L3 desync (NOT covered by any landed fix):** 81× `[PipeAlpha] faithful restore
     (parity 6): … enable L2=1 L3=0 (derived 1)` — at exit the per-layer capture (device blend
     enable = OFF) is faithfully replayed, handing the desync to every subsequent draw. This is
     the alpha twin of the ped sticky-depth problem F1 fixes for Z state.
- **Hypothesis A-1:** shot 4's see-through car = a vehicle mesh whose diffuse texture missed the
  TAC scan that frame (draw-order/angle dependent) and fell back to blend-on-UNKNOWN.
- **Hypothesis A-2:** shot 7's missing torso/head = ped atomic drawn while the device layer had
  ALPHABLENDENABLE/Z-func drifted (the 19:03:39 zfix bracket) or alpha-tested out via the
  `TAC_UNKNOWN` empty-alpha-solid fallback ("empty-alpha solids alpha-tested to invisible" —
  texalpha.h). Distinguish: if F2 alone fixes shot 7 → TAC; if it needs the L3 alpha rule → A-2.

### (B) Buildings/props draw over CJ (depth order)
- **Shots:** 5 (grass over pond, H — same carve family), 3 (band over scene, M), 1 (weak).
- **Mechanism (F1 covers the ped half — un-built):** `zfunc=8` = **D3DCMP_ALWAYS** observed at
  pipe entry in **106/127** zfix repairs, usually in all three layers (`rw ztest=0`). While that
  state is live, no surface carves depth: anything drawn later at ANY depth paints over it.
  The deployed ped contract forces `ZWRITEENABLE` only (grep: `[PED] ZWRITE` path); ZTEST/ZENABLE/
  ZFUNC are never forced at ped entry and a divergent exit capture is faithfully replayed — the
  leak is *re-created* after every ped draw and only repaired at the next building/vehicle pipe
  entry. Peds drawn in between are exactly the "props draw over CJ" victims.
- **Hypothesis B-1:** the recurring 2 s zfix cadence is throttling, not resolution — between two
  repairs ~120 frames run with dead depth testing; the screenshots sample those windows.
- **Hypothesis B-2 (carve side):** blended building meshes (fences/glass/foliage) write depth in
  the deployed build — F2's `[BldZW] … ZWRITE parked` receipts are the instrumentation and the
  repair; their absence here means carved-CJ-under-fence artifacts are also expected. Shot 5's
  grass-over-water is consistent (grass = faded/cutout blended draw with ZWRITE on).

### (C) Bandana + whole player inside-out (inverted cull/normals)
- **Shots:** 11 (interior facets, H), 12 (whole-player flat-white shading, M–H).
- **Mechanism (NOT covered by any landed fix):** the cull tug-of-war is bidirectional and
  constant: 77× `rw=1 drv=1 dev=2` + 44× `rw=2 drv=2 dev=1` + 7× all-1-forced-to-2 (128 total).
  Interpretation (RW: 1=CULLNONE 2=CULLBACK; D3D9: 1=NONE 2=CW 3=CCW): the rw cache wants
  CULLNONE for double-sided ped parts (bandana cloth, hair cards, tank-top edges) while the stale
  device still culls a side — the far side vanishes and the head/torso INTERIOR shows through at
  the angles that face the culled side. The mirror drift (dev=1 while intent=2) renders interior
  back-faces of normally solid geometry → whole-player "inside-out".
  The `[PedRS]` exit monitor reads `cull rw=2 drv=2 dev=2` "ok" 128/128 — it samples the ped
  *exit* state and cannot see mid-draw divergence or entry drift, so its "ok" does NOT exonerate
  cull (it only proves the exit restore is symmetric).
- **Hypothesis C-1 (cull state, preferred):** stale device cull at ped draw time; the repair
  pattern is the same 3L-force + sticky-don't-replay rule F1 applies to depth, applied to CULL.
  Building pipe already re-asserts cull after Im2D fullscreen passes (buildingPipe comment
  "Im2D fullscreen pass may leave CULLNONE") — the ped callback has no such re-assert.
- **Hypothesis C-2 (winding/normals in shader):** negative-determinant bone matrices flip triangle
  winding; with any fixed cull mode the player is inside-out at mirrored poses. Test: force
  CULLNONE for one build — if shot 12's inversion persists, it's C-2 (shader/winding), if it
  clears, it's C-1 (state). This A/B is the cheapest discriminator.

---

## 3. Other families seen (not among the three reported bugs)

| FAM | Evidence | Shots | Note |
|---|---|---|---|
| **FAM-Q giant translucent quads/sprites** | soft glow/sheet/quad painted as huge milky solids | 1, 2, 3, 4, 8, 9, 10 | Same soft-sprite misclassification root as A-1 (TAC soft-decal fallback "painted solid"); expect F2 to shrink this family |
| **FAM-G grass corruption** | yellow streak haze over lawns; billboards as jagged shard slabs | 4, 5, 12 | Cutout/faded misclassification + streaked sampling; grass-over-water in #5 is also a B-family depth artifact |
| **FAM-N shading/color** | palm trunks near-black w/ yellow banding; washed-white buildings | 4, 5, 12 | Plausibly normal-buffer/IBL side, or the C-family cull inversion hitting foliage; needs its own sweep |
| **FAM-W whiteout/washout** | scene globally milky/low-contrast (daylight 11:23–13:53) | 1–7, 12 | Continues the test11 whiteout thread (timecycle/fog); bounded here — HUD + colors survive, unlike test11's 100 % white collapse |
| **FAM-T texture artifacts** | pink untextured pillar (#1); magenta wall scribble (#2) | 1, 2 | Missing/misnamed texture or TAC demotion; minor |

---

## 4. Log correlation summary (session 18:59:19 → 19:04:32)

| Receipt | Count | Reading |
|---|---|---|
| `[PedZW] …` (F1) | **0** | F1 not deployed → ped depth contract absent → **B live** ✔ rule satisfied |
| `[BldZW] entry rw=…` (committed) | 128 | Deployed baseline receipt only |
| `[BldZW] parked / loop-ended / Switch-drift` (F2) | **0** | F2 not deployed → TAC + carve fixes absent → **A + B-carve live** ✔ |
| `[zfix] pipe entry depth drift repaired` | 127 (106 full-drift `ztest=0 zfunc=8`; 16 dev-only `zen=0 zfunc=8`; 5 dev-only `zfunc=8`) | Dead-depth windows recur all session; repairs are throttled 2 s — **B mechanism confirmed** |
| `[cullfix] pipe entry cull reconciled` | 128 (77× `rw=1 drv=1 dev=2`; 44× `rw=2 drv=2 dev=1`; 7× all-1→2) | Bidirectional cull desync — **C mechanism confirmed** |
| `[PipeAlpha] faithful restore (parity 6)` | 188 (81× `enable L2=1 L3=0`; 34× `L2=0 L3=0`; 11× `vtx=0`; 1× clean) | Exit replays divergent alpha captures — **second A mechanism, unfixed** |
| `rs_mon DIVERGE site=scene_start` | 123 (ALPHABLEND dev=1 vs rw=0 etc.) | Scene-start baseline itself drifts; repaired each frame by `[scene_repair]` |
| `[PedRS] ok …` | 128/128 (cull 2/2/2, zw 1/1/1, afunc 7/7/7) | Ped EXIT symmetric only; cannot exonerate entry/mid-draw state |
| `non-screen raster LIVE during building cb` | 50 | Building cb runs into 512×512 rasters (reflection/shadow) during these shots — extra state churn source |
| texalpha summary (final) | `budget=99` present, 1300 opaque / 58 cutout / 4 faded, 0 unknown, 0 faults | Old budget-gate telemetry live (pre-F2); no extraction failures |

---

## 5. Recommended next actions (for the orchestrator)

1. **Build + deploy the two landed fixes** (F1 ped depth-trio + F2 texalpha stability) and
   re-shoot the same routes (Glen Park pond + Tahoma + swimming) — this baseline is the A side.
   Success = `[PedZW]`/`[BldZW] parked` receipts appear and shots 4/5/7/11 clear.
2. **Extend the F1 pattern to CULL and ALPHA exits** (both are unfixed halves of the same
   "faithful restore replays the leak" root): 3L-force cull at ped entry; never replay a
   divergent ALPHABLENDENABLE capture (`enable L2=1 L3=0` class).
3. **Run the C discriminator A/B** (C-1 vs C-2): one build with ped cull forced CULLNONE;
   shot-12-style whole-player inversion that persists ⇒ shader/winding (C-2), clears ⇒ state
   (C-1).
4. Re-run with `[PedRS]` upgraded to an ENTRY monitor (it currently only samples exits and
   reported "ok" through every incident in this set).

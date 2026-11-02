# Regression baseline — 2026-10-25 22:37–22:42 (23 screenshots, Xbox mode)

**Baseline ID:** `2026-10-25-screenshots-2200`
**Run date:** 2026-10-25
**Capture window:** 22:37:39.609 → 22:42:45.963 (local file mtimes)
**Images:** **23** (14 JPEG, 9 PNG), all **1768×992**
**In-game clock span:** 12:05 → 17:08 (HUD clock)
**Scene span:** Ganton canal/residential → LS downtown / Idlewood / El Corona / Willowfield → Ocean Docks underwater / dock / beach
**Previous baseline:** [`docs/baseline/2026-10-24-screenshots/`](../2026-10-24-screenshots/)
**Report status:** evidence + triage. No source files were modified.

> **Count correction.** The request said “newest 25 files” in `22:37:39 → 22:42:45`. That window contains **23** GTA screenshots: 22 from `22:37:39.609` to `22:42:34.708`, plus `gta_sa_ZX7AJo83Io.jpg` at `22:42:45.963`. The next files are unrelated (`vesktop_*` at 22:49, `OpenCode_*` at 22:51). There is no 24th/25th GTA frame in the window. All 23 are covered below.

---

## 1. Build and run under test

Exact binary identity from the live log (`E:\games\gtasa_skygfx_plus\skygfx_dbg.log`:27):

```text
[BUILD] asipath=E:\games\gtasa_skygfx_plus\skygfx.asi
        size=2360832 mtime=2026-10-25 21:57:27 built=Oct 25 2026 21:56:44
```

| Field | Value |
|---|---|
| Deployed ASI | `E:\games\gtasa_skygfx_plus\skygfx.asi` |
| Size | **2,360,832 bytes** |
| Compile stamp | **2026-10-25 21:56:44** |
| Deploy mtime | **2026-10-25 21:57:27** |
| Version string | `corona-flush-shield` — **stale tag**; identify the binary by size/stamp, not the string (`src/core/main.cpp:7466` loads a tag that was not bumped) |
| Log | 6,152,664 B, mtime 22:42; run starts `22:35:22.682` |
| Shaders | `compiled OK (ps=58 vs=17), FAILED (ps=0 vs=0)` |
| Mode | **Xbox** — `[Mode] apply Xbox pipes=2/1/2 cf=2`, `[Mode] active=Xbox` |
| Pipeline enum | `pipeline=Xbox` (2), `buildingPipe=Xbox` (1), `vehiclePipe=Xbox` (2), `colorFilter=PC` (2) |
| Render size | camera 1768×992; front buffer 2048×1024; building callback sees 2048×1024 (non-screen raster) |
| Log tag receipts | `[SKY]`, `[CoronaBlend]`, `[PedOpaque]`, `[shadow_contract]`, `[water] gate failed`, `[texalpha]`, `[UW]` |

### Confirmed live INI values (from `E:\games\gtasa_skygfx_plus\skygfx.ini`)

| Key | Value | Relevance |
|---|---|---|
| `[Modes] active` | `Xbox` | selects building/vehicle Xbox paths; PBR paths are not exercised |
| `buildingPipe` / `vehiclePipe` | `Xbox` / `Xbox` | `BUILDING_XBOX` / `CAR_XBOX` |
| `colorFilter` / `colorFilterEnable` | `PC` / `0` | grading bypassed |
| `cloudRenderFix` | `1` | CClouds state-hygiene hook is active |
| `disableClouds` | `0` | vanilla cloud layer drawn |
| `waterParallaxEnable` | **`0`** | custom water pipe **not running**; log confirms `[water] gate failed (enable=0 …) -> original draw` on every flush |
| `waterStyle` / `waterQuality` | `Xbox` / `0` | only meaningful if the custom water pipe runs |
| `pipeChainEnable` | `0` | post chain pass 3 off |
| `ssaoEnable` | `0` | SSAO off |
| `pedOpaqueContract` | `1` | ped opaque contract active |
| `stateBaselineEnable` | `1` | state baseline active |
| `fixShadows` | `0` | vanilla shadow offsets kept (the state contract still installs) |
| `pedShadows` / `stencilShadows` | `-1` / `-1` | vanilla defaults |
| `sunGlare` | `0` | vehicle sun-glare hook off |
| `godRaysEnable` | `0` | mod god-rays off |
| `sunShaftsEnable` | `0` | mod sun-shafts off |
| `waterUnderwaterFog` | **`1.0`** | underwater volume fog + SSS **on** |
| `sunCoronaIntensity` | `0.000000` | sun corona intensity zero |

### Key log evidence used in this report

| Line | Meaning |
|---|---|
| `[PED] ZWRITE off/desynced on entry (rw=1 dev=0) — forcing TRUE for cb` | ped depth-write state still drifts every frame |
| `[PedOpaque] contract repaired divergent entry: dev(ref=100 fn=5 blend=1 zw=0 …)` | ped contract fires repeatedly; pre-entry device is still polluted |
| `[PedRS] ok cull … blend rw=0 drv=0 dev=0 … aref rw=0 dev=0` | ped exit state monitor: blend/alpha states do not stay repaired |
| `[shadow_install] 0x53E0C3 … MISMATCH zeroarg=1 … SKIPPED — vanilla path kept` | `CShadows::RenderStoredShadows` is **not hooked** on this exe |
| `[shadow_contract] … pre-entry layer drift: drift=1/4 …` | shadow state still drifts on every pass |
| `[water] gate failed (enable=0 vs=… ps=…) -> original draw` | custom water path disabled by INI; vanilla draw used |
| `[texalpha] … 129276 unknown … unknown_texnull=129276` | per-mesh texture classification cannot resolve a texture for most calls |
| `[SKY] entry … DEV ps=00000000 vs=00000000 alphaTest=1 fn=7 ref=1 blend=1` | clouds/sky are fixed-function; no mod shader bound |
| `[CoronaBlend] dev blend=1 src=2 dst=2` | additive ONE/ONE at the sprite/cloud flush |
| `[VPIPE] cb=Xbox … VS=IDR_XBOXCARVS PS=-` | vehicle path is the Xbox pipe, not PBR |
| `[UW] underwater ON … depth=0.27 … str=1.00` | underwater fog/SSS active during shots 12–14 |

### Caveats that affect interpretation

1. **Mode change vs the previous baseline.** The 2026-10-24 registry cites PBR paths (`VehiclePBR_Modern`, `pipeUploadPBR`, `texalpha`, PBR ambient). This run is **pure Xbox** (`[Mode] active=Xbox`). A previous issue that is invisible here may be *bypassed*, not fixed; a new issue may belong to the Xbox implementation. Each comparison row below states this explicitly.
2. **Source line numbers drift.** The cited lines are from the working tree on 2026-10-25. The tested binary stamp is 21:56:44; the `docs/plans/2026-10-25-clouds-ffp-audit.md` audit is newer than the binary in places. Where a log receipt is absent, treat the source line as a *candidate*, not proof it is in this binary.
3. **Hover may be hydraulics or external plugins.** The player vehicle is a lowrider; `VehDeform.SA.asi` and `VehIK.asi` are installed in the game directory. A hydraulics jump and those plugins can move panels/wheels. Do not attribute every hover/detach to SkyGfx without an A/B.
4. **23 hand-taken shots, not a sweep.** Absence of an artifact is not proof it is fixed; presence is evidence it was live in this build.
5. **JPEG shots** (14 of 23) have ringing around hard edges. PNG shots were used for exact colour where possible; the RGB values below are 5×5 medians, which suppress ringing.

---

## 2. Method

- Python 3.10 (32-bit), Pillow 12.2.0, NumPy 2.2.6.
- Every image opened at native 1768×992; per-image mean RGB, per-channel stddev, and top/mid/bottom regional means recorded.
- ~120 targeted 5×5 median patches taken at artifact locations; the table in §10.2 records the exact bbox, median, mean and stddev.
- Artifact masks (strict, on a 4× downscale, then bbox scaled back):
  - **pure black:** `R,G,B < 6` per channel (component area ≥ 8,000 px full-res);
  - **blown white:** `R,G,B > 252` per channel;
  - **pink cloud:** `R > 120`, `R > G+12`, `G > B+2`, `B > 70`;
  - **pink/beige cloud coverage** counted against the 1,753,856-pixel frame.
- Ambiguous regions cropped and re-read at 2–4× (`C:\Users\AAAAAA~1\AppData\Local\Temp\opencode\crops2200\`).
- The cloud texture format was checked directly in `models/particle.txd` (TXD header + first mip pixels) to distinguish a state bug from a content bug. No game/source files were modified.

---

## 3. Per-image table

Source directory: `C:\Users\aaaaaaaaa\Documents\ShareX\Screenshots\2026-10\`.

**CJ legend:** `TEXTURED` = skin/clothes textures present; `WHITE/GREY` = present but untextured, material-colour mannequin; `GHOST` = white/grey **and** semi-transparent; `IN-CAR` = not visible through the cabin; `UNDERWATER` = first-person/underwater, not visible; `ABSENT` = no CJ body in frame.

| # | File | mtime | Clock | Scene / location | CJ | Vehicle | Water / cloud | Issues |
|---|------|-------|-------|------------------|----|---------|---------------|--------|
| 01 | `gta_sa_cfU28ixIoG.jpg` | 22:37:39 | 12:05 | Ganton canal / road junction | **WHITE/GREY** — only head visible above a screen-covering black quad | black car on far bank appears to float | flat blue canal band; clear deep-blue sky | R2-01, R2-03, R2-05, R2-06 |
| 02 | `gta_sa_1O58acZ43r.jpg` | 22:37:41 | 12:07 | Ganton canal | **WHITE/GREY** — full body, head [163,158,152] | black car on far bank floating | flat blue canal; clear | R2-01, R2-03, R2-05, R2-06 |
| 03 | `gta_sa_sQr6TfbGla.jpg` | 22:37:42 | 12:08 | Ganton canal | **ABSENT/GHOST** — CJ hidden; 3 white translucent peds around car | black car floating over canal bank | flat blue canal; clear | R2-01, R2-03, R2-05, R2-06 |
| 04 | `gta_sa_sQdkFD1CoX.jpg` | 22:37:46 | 12:12 | Ganton street (top-down) | **TEXTURED** — green ragdoll on road | black lowrider, peds around; body [2,2,2] | none; clear | R2-04 |
| 05 | `gta_sa_BxjrbhtP6S.jpg` | 22:37:50 | 12:16 | Ganton residential | **TEXTURED** — green bandana, white tank | black lowrider hovering ~1.5 m; body [2,2,2] | none; clear | R2-04, R2-05, R2-09 |
| 06 | `gta_sa_PQB6tqlTsZ.jpg` | 22:37:54 | 12:20 | LS downtown / overpass | **GHOST** — white translucent at centre | black cars; pure-black rect [476,468,1768,992] | canal band; clear | R2-01, R2-03, R2-04, R2-06, R2-09 |
| 07 | `gta_sa_8oROCcx6JC.jpg` | 22:38:01 | 12:27 | Idlewood | **GHOST** — white crouched under car | black lowrider floating; rear wheel/trim detached; background cars float | flat canal; clear | R2-01, R2-04, R2-05, R2-06, R2-09 |
| 08 | `gta_sa_hbQy9fqDOw.jpg` | 22:38:13 | 12:39 | LS downtown / Market | **IN-CAR** | black player car; rear trim detached; spark fan | canal line; clear | R2-01, R2-04, R2-06, R2-09 |
| 09 | `gta_sa_lwexiJ9aOy.jpg` | 22:38:28 | 12:54 | LS under bridge / wall | **IN-CAR** | black car; green grass-texture ghost on roof; grey opaque rear window | none; clear | R2-04 |
| 10 | `gta_sa_5fP153lT5h.jpg` | 22:38:37 | 13:03 | El Corona | **IN-CAR** | black car; spark fan; body [3,2,2] | canal band; clear | R2-04, R2-06, R2-09 |
| 11 | `gta_sa_bEILIemJyp.jpg` | 22:38:47 | 13:19 | Willowfield | **IN-CAR** | black car in blue-grey smoke; queue of black cars | canal band; clear | R2-04, R2-06, R2-09 |
| 12 | `gta_sa_zbi6b5jTaB.jpg` | 22:39:51 | 14:17 | Ocean Docks — just below surface | **UNDERWATER** | none | rippled teal water + white sun cone | R2-06 |
| 13 | `gta_sa_rS8VtWPPM5.png` | 22:39:52 | 14:18 | Ocean Docks — underwater | **UNDERWATER** | none | rippled water + white cone [189,217,217] | R2-06 |
| 14 | `gta_sa_hSMWNNxWVV.png` | 22:39:56 | 14:21 | Ocean Docks — underwater | **UNDERWATER** | none | **pure-white cone** [255,255,255], bbox [468,492,1404,992] | R2-06 |
| 15 | `gta_sa_lDha6vnq2J.png` | 22:40:21 | 14:47 | Ocean Docks — at surface | **UNDERWATER** | none | **pure-white water band** [255,255,255], bbox [0,584,1768,992]; ship | R2-06 |
| 16 | `gta_sa_q2zyFkbrc9.png` | 22:41:10 | 15:36 | Ocean Docks — dock path | **ABSENT** | none; ship/cranes | flat teal ocean; stepped/detached hedge cards | R2-06, R2-07 |
| 17 | `gta_sa_C9myMFzVUI.jpg` | 22:41:20 | 15:45 | Ocean Docks — chain-link fence / beach | **WHITE/GREY** — head [207,200,194], torso [80,76,75]; white peds behind fence | none; black bus/truck on road | flat teal ocean; clear | R2-01, R2-06, R2-07 |
| 18 | `gta_sa_0i8bISC8pO.png` | 22:41:53 | 16:18 | Ocean Docks — beach, looking up | **ABSENT** | none | **opaque pink cloud quads cover 69%** of frame; sunset | R2-02 |
| 19 | `gta_sa_Vx7i27JUFE.png` | 22:41:58 | 16:22 | Ocean Docks — beach, looking up | **GHOST/SILHOUETTE** — giant flat ped shape [116,104,95], std < 2.1 | none | pink clouds cover 59%; sun | R2-02, R2-08 |
| 20 | `gta_sa_5aa1QDwnGI.png` | 22:42:01 | 16:25 | Ocean Docks — beach, looking up | **ABSENT** | none | **pink clouds cover 83%**; sun | R2-02 |
| 21 | `gta_sa_4PGDUDzsUu.jpg` | 22:42:11 | 16:34 | Ocean Docks — beach | **TEXTURED** — green bandana, white tank, bat | none | clear; black foliage cards at left | R2-07, R2-09 |
| 22 | `gta_sa_v8mTbb1pOB.jpg` | 22:42:34 | 16:57 | Ocean Docks — beach | **TEXTURED** — kneeling with bat; peds textured | none | clear; semi-transparent diamond shadows (plausible) | R2-09 |
| 23 | `gta_sa_ZX7AJo83Io.jpg` | 22:42:45 | 17:08 | LS palm-lined road (Ganton/Idlewood) | **WHITE/GREY** — running with bat; head/torso [92,89,86] | none | clear; floating light-blue panel at right [88,110,124] | R2-01, R2-07, R2-09 |

**Frequency sanity check (of 23):** R2-01 **8**, R2-02 **3**, R2-03 **4**, R2-04 **8**, R2-05 **5**, R2-06 **13**, R2-07 **4**, R2-08 **1**, R2-09 **21**. R2-09 is present in essentially every outdoor HUD frame; it is listed in the issue column only where it is the only issue or where the crowding is worst, to keep the table readable.

---

## 4. Per-image notes

### 01 — `gta_sa_cfU28ixIoG.jpg` — 22:37:39 — 12:05 — Ganton canal

Low camera at the canal. A **screen-covering black quad** fills the lower half; the strict-black component is bbox `[876,632,1416,832]`, median `[5,8,4]`, and the looser dark mask is 713,024 px (40.6% of frame), median `[9,8,8]`. Only a white head/ball is visible at the centre; CJ's body is swallowed by the quad. A black car on the far bank appears suspended above the canal, with no contact shadow. The canal band is flat blue (mixed tree/water sample `[44,53,64]`; the clean canal value from shot 07 is `[42,97,147]`). Sky is clear deep blue. **Issues: R2-01, R2-03, R2-05, R2-06.**

### 02 — `gta_sa_1O58acZ43r.jpg` — 22:37:41 — 12:07 — Ganton canal

CJ is fully visible but **untextured**: head 5×5 median `[163,158,152]` at (895,485), torso `[109,102,96]` at (910,570), legs fall into the shadow at `[13,14,12]`. No face, no clothing pattern, no skin tone — a smooth material-colour mannequin. The black shadow/decal quad starts at y≈584 and covers the lower third; dark-mask bbox `[0,584,1768,992]`, median `[7,8,5]`. The black car on the far bank still floats. Canal flat; clear sky. **Issues: R2-01, R2-03, R2-05, R2-06.**

### 03 — `gta_sa_sQr6TfbGla.jpg` — 22:37:42 — 12:08 — Ganton canal

Three peds around the floating black car are **white/grey and semi-transparent** — the road and car body are visible through them. Grey-ped mask median `[46,44,42]` over 3,111 px. This is not a missing mesh: the geometry, pose and shading are present; the texture/alpha is wrong. A second black quad is under/left of the car: strict-black core bbox `[304,504,948,992]`, 111,744 px, median `[5,6,4]`. Car still floats; canal flat; clear. **Issues: R2-01, R2-03, R2-05, R2-06.**

### 04 — `gta_sa_sQdkFD1CoX.jpg` — 22:37:46 — 12:12 — Ganton street (top-down)

CJ is a **textured green ragdoll** on the road — proof that the ped texture path can render correctly in this run. The black lowrider's body is `[2,2,2]` with only small chrome/detail areas at `[22,22,21]`; no paint colour or environment reflection. A large diagonal shadow across the lower-left is semi-transparent and is not the pure-black R2-03 class (the sampled patch `[400,700,700,850]` is road `[64,57,52]`, not the band itself). No water or clouds. **Issue: R2-04.**

### 05 — `gta_sa_BxjrbhtP6S.jpg` — 22:37:50 — 12:16 — Ganton residential

A **control shot**: CJ is fully textured (green bandana, white tank, jeans) and the peds behind him are textured too. The black lowrider is **hovering ~1.5 m** above the road, front wheels clear of the ground; body `[2,2,2]`, no contact shadow. This may be a hydraulics jump; the clearer wheel/trim separation is in shot 07. Minimap labels (`BS`, `Cv`, `R`, `N`) crowd the radar. **Issues: R2-04, R2-05, R2-09.** This shot is the strongest argument that R2-01 is an intermittent **state** fault, not a missing ped asset.

### 06 — `gta_sa_PQB6tqlTsZ.jpg` — 22:37:54 — 12:20 — LS downtown / overpass

The clearest **screen-space black rectangle**: strict-black component bbox `[476,468,1768,992]`, 495,552 px (28.3% of frame), median `[2,2,2]`, with a hard vertical left edge and a near-horizontal top edge. A faint car shape is visible inside the black near its top — consistent with a shadow/decal quad or a missing render region, not terrain. CJ is a **white translucent ghost** at centre (`[86,81,76]`), with the road visible through him. Several black cars; canal/water band; clear sky. **Issues: R2-01, R2-03, R2-04, R2-06, R2-09.**

### 07 — `gta_sa_8oROCcx6JC.jpg` — 22:38:01 — 12:27 — Idlewood

The player's black lowrider is **airborne** above the road; a rear wheel and some trim are visibly displaced from the body. Body/roof are `[2,2,2]`, with no reflection. CJ is a **white ghost** crouched under the car (`[101,97,90]`); background cars float above the flat blue canal band (`[42,97,147]`). The scene suggests the lowrider hydraulics may be active, but the detached wheel/trim is not a hydraulics pose. **Issues: R2-01, R2-04, R2-05, R2-06, R2-09.**

### 08 — `gta_sa_hbQy9fqDOw.jpg` — 22:38:13 — 12:39 — LS downtown / Market

Chase view. The black player car has a detached rear trim/bumper and a huge fan of orange sparks from the left-rear wheel; body `[3,2,2]`. White/grey peds stand on the left pavement. The canal/water line is a thin band at the horizon. CJ is **in-car** and not visible through the cabin. **Issues: R2-01, R2-04, R2-06, R2-09.** The sparks are a particle effect; they are not the vehicle dissolving.

### 09 — `gta_sa_lwexiJ9aOy.jpg` — 22:38:28 — 12:54 — LS under bridge / wall

Close parked view. The car roof carries a **green/olive grass-texture ghost**: the pattern matches the grass on the wall behind, with green-excess pixels median `[31,30,14]`. The body is `[2,2,2]`; the rear window is an opaque grey slab (`[11,11,10]`) with no interior. This is the strongest **per-mesh texture-desync** candidate on the vehicle: the roof appears to sample the vegetation atlas while the body samples a black texture. A real-time reflection of the local grass cannot be ruled out from one frame, but GTA SA's car env map is static, so a stale texture bind is the more likely explanation. No water/clouds. **Issue: R2-04.**

### 10 — `gta_sa_5fP153lT5h.jpg` — 22:38:37 — 13:03 — El Corona

Black player car with the same orange spark fan from the rear wheel; body `[3,2,2]`. Background cars; a flat blue canal band `[42,97,145]` cuts across the middle at water level. Sky is a clean deep blue `[43,107,174]` — no washout. **Issues: R2-04, R2-06, R2-09.**

### 11 — `gta_sa_bEILIemJyp.jpg` — 22:38:47 — 13:19 — Willowfield

The black player car is surrounded by a large blue-grey **tyre-smoke plume**; the body is opaque at `[34,34,38]` (lighter than the pure-black shots because of the smoke lighting). A queue of black cars follows; the canal band is at the top. The smoke is **not** the old vehicle translucency — the body remains solid. **Issues: R2-04, R2-06, R2-09.**

### 12 — `gta_sa_zbi6b5jTaB.jpg` — 22:39:51 — 14:17 — Ocean Docks, just below the surface

Camera at the surface. The water has **ripple texture** (dark teal `[17,33,36]`), so it is not flat in this view. A white sun cone rises from the bottom centre (`[200,211,213]` in the sampled band, soft edge); the above-water dock is visible at the top. No clouds. **Issue: R2-06.**

### 13 — `gta_sa_rS8VtWPPM5.png` — 22:39:52 — 14:18 — Ocean Docks, underwater

Same setup, slightly deeper. Water `[46,79,78]`; white cone `[189,217,217]`; the shore is visible above the surface. The cone is brighter and more geometric than in shot 12. **Issue: R2-06.**

### 14 — `gta_sa_hSMWNNxWVV.png` — 22:39:56 — 14:21 — Ocean Docks, underwater

The white cone is now **pure white**: bbox `[468,492,1404,992]`, 278,256 px (15.9% of frame), median `[255,255,255]`, std 0. The cone has a sharp triangular apex and a soft curved base — a blown sun reflection / underwater in-scatter, not a textured surface. Water around it is `[21,36,39]`. **Issue: R2-06.**

### 15 — `gta_sa_lDha6vnq2J.png` — 22:40:21 — 14:47 — Ocean Docks, at the surface

The bottom half of the frame is **pure white**: bbox `[0,584,1768,992]`, 649,440 px (37.0%), median `[255,255,255]`, std 0 — a flat, featureless overexposed water surface. The teal water band above is `[57,81,83]`; the ship is visible; sky is clear `[85,114,150]`. **Issue: R2-06.**

### 16 — `gta_sa_q2zyFkbrc9.png` — 22:41:10 — 15:36 — Ocean Docks, dock path

A row of **oversized hedge leaf cards** with stepped diagonal edges and detached cards on the right (`[22,28,14]`); a flat teal ocean `[83,101,94]`; clear sky `[100,132,169]`. No CJ. The cards are hard-edged and geometrically detached, not a soft hedge. **Issues: R2-06, R2-07.**

### 17 — `gta_sa_C9myMFzVUI.jpg` — 22:41:20 — 15:45 — Ocean Docks, fence / beach

CJ is **white/grey and untextured**: head `[207,200,194]`, torso `[80,76,75]`, legs `[55,52,48]`. The peds behind the chain-link fence are also white/grey mannequins. A large semi-transparent shadow quad lies under the fence; sand `[46,43,36]` vs shadow `[47,42,36]` — only slightly darker and sand still visible, so this is **not** the pure-black R2-03 class. A grey/green slab floats at the far left `[113,100,84]`. Flat teal water. **Issues: R2-01, R2-06, R2-07.**

### 18 — `gta_sa_0i8bISC8pO.png` — 22:41:53 — 16:18 — Ocean Docks, beach, looking up

The sky is filled with **opaque pink/beige cloud quads**: 1,210,048 px = **69.0%** of the frame matches the pink-cloud mask, median `[139,114,109]`. Each quad is a huge rotated rectangle with hard edges; the cloud texture detail is visible only as darker grey patches. The cloud texture (`cloudmasked`/`cloudhigh` in `models/particle.txd`) is 1024×1024 A8R8G8B8 with first BGRA pixel `FF FF FF 00` — **white RGB, alpha 0**. The cloud batch is drawn additively ONE/ONE, so the white background is added and the alpha is ignored. Sunset. **Issue: R2-02.**

### 19 — `gta_sa_Vx7i27JUFE.png` — 22:41:58 — 16:22 — Ocean Docks, beach, looking up

The pink clouds cover 1,034,304 px = **59.0%** of the frame, median `[166,131,122]`, with the sun at `[255,219,129]`. A **giant flat ped silhouette** (head, arms, torso) sits at centre-bottom: uniform beige `[116,104,95]`, per-channel std `[2.1,0.8,0.5]`. The interior is too uniform to be the textured ped model; it reads as a shadow/reflection decal or a ped seen through the cloud quads. **Issues: R2-02, R2-08.**

### 20 — `gta_sa_5aa1QDwnGI.png` — 22:42:01 — 16:25 — Ocean Docks, beach, looking up

The pink cloud quads cover 1,463,136 px = **83.4%** of the frame, median `[155,126,114]` — almost the whole sky is a wall of hard-edged pink rectangles. The sun is visible through the layers. **Issue: R2-02.**

### 21 — `gta_sa_4PGDUDzsUu.jpg` — 22:42:11 — 16:34 — Ocean Docks, beach

CJ is fully textured (green bandana, white tank, jeans) running with a bat. At the left, a cluster of **black foliage cards** with hard triangular edges and a white gap/triangle (`[33,32,31]`); sand `[101,91,79]`. No clouds. This is the leaf/vegetation-quad defect, but it is not ped-bound (no bush over CJ's head). **Issues: R2-07, R2-09.**

### 22 — `gta_sa_v8mTbb1pOB.jpg` — 22:42:34 — 16:57 — Ocean Docks, beach

CJ is textured and kneeling with the bat; the surrounding peds are textured (skin, swimsuits, shorts). Large **semi-transparent diamond shadows** lie on the sand: shadow `[94,85,71]` vs sand `[122,111,97]`. Unlike shots 01–03/06, the sand is visible through them and they are roughly object-sized; they look like normal GTA shadow decals, so they are **not** counted as R2-03. Water at right is dark blue. Minimap labels crowd. **Issue: R2-09.**

### 23 — `gta_sa_ZX7AJo83Io.jpg` — 22:42:45 — 17:08 — LS palm-lined road (Ganton/Idlewood)

Final frame. CJ is **white/grey and untextured** running with a bat: head/torso `[92,89,86]`, legs `[39,35,34]`; no skin or clothing texture. A **light-blue translucent panel** floats at the right edge `[88,110,124]`, with a hard vertical edge and a dark outline, above the road — a water/building/vegetation plane drawn in the wrong place. Trees, road and sky are otherwise normal (no clouds, no global colour cast). **Issues: R2-01, R2-07, R2-09.**

---

## 5. Ranked issue registry (round 2)

**Severity:** 3 = player/vehicle/water rendering failure; 2 = clearly visible, affects readability or immersion; 1 = cosmetic.
**Class:** `STATE` = render-state / cache / hook ordering; `SHADER` = shader math, blend or exposure; `CONTENT` = asset, INI, plugin or HUD.

| ID | Title | Sev | Freq (n/23) | Class | One-line root-cause lead |
|---|---|---|---|---|---|
| R2-01 | Untextured white/grey peds (intermittent) | 3 | 8 | STATE | cloud texture-raster leak into default/ped callback |
| R2-02 | Opaque pink/beige cloud quads | 3 | 3 | SHADER + CONTENT | additive ONE/ONE over white-RGB / alpha-0 cloud textures |
| R2-03 | Pure-black shadow/decal quads | 3 | 4 | STATE | `RenderStoredShadows` hook skipped (0x53E0C3 mismatch) |
| R2-04 | Vehicle material corruption: flat black, green roof ghost, grey window | 3 | 8 | STATE + CONTENT | Xbox cb binds NULL paint texture as if textured |
| R2-05 | Floating vehicles / detached panels | 3 | 5 | CONTENT/PLUGIN | hydraulics or `VehDeform`/`VehIK`, not proven SkyGfx |
| R2-06 | Flat/blown water; white sun cone / white water band | 2 | 13 | SHADER/EXPOSURE + INI | vanilla water + `waterUnderwaterFog` in-scatter; white 255 |
| R2-07 | Oversized/detached vegetation and floating panels | 2 | 4 | CONTENT/STATE | leaf-card impostors + stale texture/transform |
| R2-08 | Giant flat ped silhouette in the sky | 2 | 1 | STATE (low confidence) | shadow/reflection decal projected at wrong scale |
| R2-09 | Minimap label crowding | 1 | 21 | CONTENT/HUD | radar labels, not the 3D renderer |

---

### R2-01 — Untextured white/grey peds (intermittent)

**Severity:** 3 · **Frequency:** 8/23 — shots 01, 02, 03, 06, 07, 08, 17, 23 · **Class:** STATE

**Visual.** CJ and nearby pedestrians render as smooth **grey/white mannequins**: the head is white, the torso/legs grey, with no face, skin tone, clothing pattern or hair. In shots 03, 06 and 07 they are also **semi-transparent** (road/car visible through them). In shot 19 the related giant silhouette is a flat beige fill. The same CJ is **fully textured** in shot 05 (green bandana, white tank, jeans) and the beach peds are textured in shot 22, so the assets are present and this is intermittent.

**Exact evidence.**

| Shot | Region | bbox | Median RGB | Note |
|---|---|---|---|---|
| 02 | CJ head | (895,485) 5×5 | **163,158,152** | white, no face |
| 02 | CJ torso | (910,570) 5×5 | **109,102,96** | grey, no clothing pattern |
| 17 | CJ head | (875,450) 5×5 | **207,200,194** | white |
| 17 | CJ torso | (890,530) 5×5 | **80,76,75** | grey |
| 17 | CJ legs | (890,705) 5×5 | **55,52,48** | grey |
| 03 | three peds | mask over (780,440,1160,620) | **46,44,42** (n=3,111) | semi-transparent |
| 23 | CJ head/torso | (900,480)/(900,550) | **92,89,86** | still untextured at 17:08 |
| 23 | CJ legs | (900,700) | **39,35,34** | dark grey |

**Suspected subsystem.** Default/ped callback after the cloud pass. `CClouds__Render_hygiene` saves/restores filter, alpha test, fog, Z test/write, blend factors, stage-0 ops and shaders — but **not the texture raster** (`rwRENDERSTATETEXTURERASTER`, state 1). CClouds pushes the cloud/sun raster as rw state 1; the RW texture-binding cache is not told, so the device ends the cloud pass holding the **cloud/particle atlas** while the cache still names the ped texture. The next `RwD3D9SetTexture(pedTex, 0)` no-ops (cache already says pedTex) and the ped samples the cloud/particle atlas: white/grey RGB, soft varying alpha. That yields both the opaque grey mannequin (alpha≈1 region) and the ghost peds (alpha<1 region).

**Source map.**

- `src/core/main.cpp:4544-4820` — `CClouds__Render_hygiene`; the restore block at 4758-4815 has no `rwRENDERSTATETEXTURERASTER` capture/restore.
- `docs/plans/2026-10-25-clouds-ffp-audit.md` §1.5, row “**Texture raster (state 1)** never saved or restored” — the audit explicitly names this leak and says “the first world draw samples a cloud/sun raster”.
- `src/core/main.cpp:1564-1818` — `myDefaultCallback`; `defaultCbShaderHygiene` unbinds PS/VS at 1795-1818 but there is **no texture resync**.
- `src/render/neoCarpipe.cpp:245-252` — `carBlendRestore3L` is the correct L1 texture-raster restore pattern (`RwRenderStateSet(rwRENDERSTATETEXTURERASTER, …)`).
- `src/render/vehiclePipe.cpp:1620-1635` — the Xbox vehicle callback has the same NULL-texture branch problem (see R2-04).

**Classification.** **State bug**, not a shader-math bug and not content. The proof is the control shots 05/21/22: the same ped textures render correctly, so the asset path works; only the bind/cache state is wrong.

**Probe / verify.** Log `GetTexture(0)` plus `rwRENDERSTATETEXTURERASTER` at ped entry; then add the texture-raster save/restore to the cloud hook and a `pipeSetTexture`-style resync at `myDefaultCallback` entry. Success test: CJ head shows a face/skin tone in shot 02/17/23, and the ghost peds in 03/06/07 become opaque and textured.

---

### R2-02 — Opaque pink/beige cloud quads

**Severity:** 3 · **Frequency:** 3/23 — shots 18, 19, 20 · **Class:** SHADER + CONTENT

**Visual.** When the camera looks up at the sunset sky, the cloud layer renders as **huge rotated rectangles with hard edges**, filled with a flat pink/beige background and darker grey cloud-texture patches. The quads overlap, cover the sun and most of the sky, and have no soft alpha fringe. At ground level the daytime sky is clear, so this is only visible when the camera points at the cloud layer.

**Exact evidence.**

| Shot | Pink-cloud mask | Coverage | Median RGB | Note |
|---|---|---|---|---|
| 18 | 1,210,048 px | **69.0%** of 1,753,856 | **139,114,109** | cloud quad sample (450,120,750,300) = **168,132,125** |
| 19 | 1,034,304 px | **59.0%** | **166,131,122** | sun **255,219,129** |
| 20 | 1,463,136 px | **83.4%** | **155,126,114** | almost the whole frame |

**Texture evidence.** Direct TXD parse of `E:\games\gtasa_skygfx_plus\models\particle.txd` (and the launcher DLC copy, which is identical):

| Texture | Format | Flags | First BGRA pixel | Meaning |
|---|---|---|---|---|
| `cloudmasked` | A8R8G8B8 (0x15) | has alpha | `FF FF FF 00` | white RGB, **alpha 0** |
| `cloudhigh` | A8R8G8B8 (0x15) | has alpha | `FF FF FF 00` | white RGB, **alpha 0** |
| `cloud1` | X8R8G8B8 (0x16) | no alpha | `00 00 00 00` | black background (correct for additive) |

The cloud batch is drawn **additively ONE/ONE** (`[CoronaBlend] dev blend=1 src=2 dst=2`; `[SKY] entry … blend=1`, `DEV ps=00000000 vs=00000000`). Additive blending **ignores the alpha channel**: an alpha-authored white-background cloud texture contributes its white RGB across the whole quad, tinted pink by the sunset vertex colour. The result is an opaque pink rectangle. `cloud1` (black background) would be correct; the low/high cloud textures are not.

**Suspected subsystem.** `CClouds::Render` / sky cloud layer. The cloud texture raster is bound at `0x713DC1`/`0x714246`; the batch is flushed at `0x714019`/`0x71422A`/`0x714387`; the hook is installed at `src/core/main.cpp:7341-7345`. The current hook forces stage-0 `COLOROP/ALPHAOP` only on **exit** (restore at 4788-4815), not on entry; the audit's §5.2 cause #1. The sky-dome/cloud-layer path (`CClouds__RenderSkyPolys`, 0x714650, wrapper `src/render/envmap.cpp:569`) is drawn before the cloud hook's repair; the audit's §5.2 cause #2. **Counter-claim:** the source comment at `src/core/main.cpp:7346-7363` says the dome self-sets blend/raster and deliberately does not hook it. A draw-order/state capture is needed to decide whether the pink quads are the sprite batch or the dome; the content/blend mismatch is established either way. The audit's §6.2 proposes a premultiplied `CloudSpritePS` as the robust fix.

**Classification.** **Shader/blend + content.** The engine's additive blend is incompatible with the installed cloud textures (white RGB/alpha 0). Fix options, in order:

1. Premultiplied cloud sprite shader (`float4(rgb*opacity, opacity)` with ONE/ONE), as in the audit §6.2 — robust, keeps the additive contract.
2. Convert the `cloudmasked`/`cloudhigh` RGB backgrounds to black (premultiply the texture) in `particle.txd` — content fix, cheap, but requires a TXD edit.
3. Force `SRCALPHA/ONE` for the cloud batch instead of ONE/ONE — a state experiment; may wash the clouds (the audit §5.3 warns about this).

`disableClouds=1` will hide the symptom but is not a fix.

**Probe / verify.** A/B `disableClouds=1`; dump the bound texture at the cloud flush (`GetTexture(0)` and its format); run the audit §6.3 stage-op A/B (force `MODULATE(TEXTURE,DIFFUSE)` on entry, `MIPFILTER=NONE`). Success test: the sunset sky shows soft-edged clouds, no hard pink rectangles, and the cloud coverage no longer occupies >50% of the frame as flat fill.

---

### R2-03 — Pure-black shadow/decal quads

**Severity:** 3 · **Frequency:** 4/23 — shots 01, 02, 03, 06 · **Class:** STATE

**Visual.** Uniform **pure-black** (`[2,2,2]`) hard-edged quads cover 22–41% of the frame. The edges are straight/geometric; there is no texture or gradient; the quads occlude the road, CJ and the car. In shot 06 a faint car shape is visible inside the black near the top. These are distinct from the **semi-transparent** beach shadows in shots 17/22, where the sand is still visible and the shadow is only slightly darker (shadow `[47,42,36]` vs sand `[46,43,36]` in 17; `[94,85,71]` vs `[122,111,97]` in 22) — those are **not** counted here.

**Exact evidence.**

| Shot | Black mask bbox | Area / coverage | Median RGB | Note |
|---|---|---|---|---|
| 01 | `[0,480,1768,992]` | 713,024 px (40.6%) | **9,8,8** | strict core `[876,632,1416,832]` = **5,8,4** |
| 02 | `[0,584,1768,992]` | 605,456 px (34.5%) | **7,8,5** | covers CJ's legs |
| 03 | `[304,504,948,992]` | 111,744 px strict | **5,6,4** | under/left of the floating car |
| 06 | `[476,468,1768,992]` | 495,552 px (28.3%) | **2,2,2** | std 0 core; faint car inside |

**Suspected subsystem.** Stored-shadow pass. The install loop is at `src/core/main.cpp:7542-7590`; the shadow state contract is at `src/core/main.cpp:1181-1513`. The log proves the stored pass is **not hooked**:

```text
[shadow_install] 0x53E0BE ... OK ... CShadows::RenderStaticShadows
[shadow_install] 0x53E0C3 ... MISMATCH zeroarg=1 ... SKIPPED — vanilla path kept
[shadow_install] 0x53E0C8 ... OK ... CShadows::RenderExtraPlayerShadows
```

The contract fires (`[shadow_contract] … drift=1/4 …`) but only for the static and extra passes. The stored pass keeps the vanilla state, which has drifted (blend off / alpha ref 100 / ZWRITE off) from the postfx raw writes. The shadow sprite then draws with **blend off and alpha ignored** → a black quad instead of a soft blob. The pure-black area is the sprite's geometry; the size may be normal for a vehicle/ped shadow, but the opacity is not.

**Classification.** **State bug** (un-healed stored-shadow render state), not a shader-math bug and not content. The `pedShadows=-1`, `stencilShadows=-1`, `fixShadows=0` settings mean the shadow *types/offsets* are vanilla; the contract is the only state repair, and it is missing this pass.

**Probe / verify.** Update the `0x53E0C3` signature/pointer for the live `gta_sa.exe` (the log's `orig=68E8DD00` vs expected `00708300` means the call target is a thunk/other build), or wrap the three call sites at a level that does not require the exact callee. Log `ALPHABLENDENABLE/SRCBLEND/DESTBLEND/ALPHAREF/ZWRITE` at the stored pass. Success test: shots 01/02/03/06 show a soft, semi-transparent shadow (road/CJ visible through it), not a `[2,2,2]` fill.

---

### R2-04 — Vehicle material corruption: flat black, green roof ghost, grey window

**Severity:** 3 · **Frequency:** 8/23 — shots 04–11 (player car visible in 04, 05, 07–11; in 06 the player car is not visible, but the background cars are black) · **Class:** STATE + CONTENT

**Visual.** The player's black lowrider reads as **flat matte black** with only small chrome/detail highlights; there is no paint colour, environment reflection or clearcoat. In shot 09 the **roof samples a green grass texture** (the pattern matches the wall behind) and the **rear window is an opaque grey slab** with no interior. In shots 05/07 the body is black but the wheels/trim are displaced. A red sedan in shot 07 does keep a red/brown diffuse (visually), so the defect is **material-specific**, not a global “all cars black” shader.

**Exact evidence.**

| Shot | Region | bbox | Median RGB | Note |
|---|---|---|---|---|
| 04 | car body | `[900,150,1100,250]` | **2,2,2** | chrome sample `[22,22,21]` |
| 05 | car body | `[1150,200,1400,330]` | **2,2,2** | no reflection |
| 07 | car body/roof | `[450,500,650,650]` / `[650,420,800,470]` | **2,2,2** | detached wheel/trim |
| 08 | car body | `[800,600,950,720]` | **3,2,2** | rear trim detached |
| 09 | roof green ghost | green-excess mask over `[450,380,950,600]` | **31,30,14** | pattern = wall grass |
| 09 | rear window | `[380,800,560,900]` | **11,11,10** | opaque grey, no interior |
| 10 | car body | `[800,550,950,700]` | **3,2,2** | spark fan |
| 11 | car body | `[800,600,1050,850]` | **34,34,38** | smoke lighting; still no paint |

**Suspected subsystem.** Xbox vehicle callback `CCustomCarEnvMapPipeline__CustomPipeRenderCB_Xbox` at `src/render/vehiclePipe.cpp:1419-1670`. The critical branch is at `vehiclePipe.cpp:1620-1635`:

```cpp
if(flags & (rxGEOMETRY_TEXTURED2 | rxGEOMETRY_TEXTURED)){
    RwD3D9SetTexture(material->texture, 0);
    ...
}else{
    RwD3D9SetTexture(NULL, 0);
    ...
}
```

Many car **paint** meshes have UVs (geometry is flagged textured) but **no diffuse texture** — the paint is material colour + env map. If this car's paint mesh is one, `material->texture == NULL` while the branch is “textured”: the NULL bind is a cache no-op and the mesh samples the **previous texture** (the grass/wall from the vegetation pass, or a cloud/particle raster). That is the green grass ghost. The body's black material then multiplies the stale texture to black. The env/spec bits are read at `vehiclePipe.cpp:1470-1600` (`hasEnv1/hasEnv2/hasSpec`); if the paint material's env bits are not set, the body has no reflection and reads flat black.

The correct pattern already exists: `pipeSetTexture(material->texture, 0)` in `src/skygfx.h:1259` resolves NULL to `gpWhiteTexture` and forces a real cache transition. The building pipe uses it at `src/render/buildingPipe.cpp:1289`/`1776`/`2040`/`2435`; the Xbox vehicle cb does not. The `[texalpha] … unknown_texnull=129276` log is consistent with many null material textures.

**Classification.** **State/logic bug** (NULL texture bind branch) + possible **content** (paint/env flags, texture streaming). It is not a PBR shader-math bug; the run does not use PBR. It is not an INI issue except that `vehiclePipe=Xbox` selects this callback.

**Probe / verify.** Log `material->texture`, `flags`, `materialFlags`, `hasEnv1/hasEnv2/hasSpec` and `envSwitch` for the player lowrider's paint meshes. A/B `vehiclePipe=PC` or a PBR preset to see whether the paint colour/reflection returns. Success test: the lowrider body shows a dark paint colour with a sky/building reflection; the roof no longer shows grass; the rear window is transparent or dark-tinted, not a grey slab.

---

### R2-05 — Floating vehicles / detached panels

**Severity:** 3 · **Frequency:** 5/23 — shots 01, 02, 03, 05, 07 · **Class:** CONTENT/PLUGIN (unproven SkyGfx)

**Visual.** The player's black lowrider hovers ~1–1.5 m above the road with no contact shadow (shots 05, 07); in shot 07 a rear wheel and some trim are visibly displaced from the body. Background cars also float over the canal bank in shots 01–03. The player vehicle is a lowrider, so a **hydraulics jump** is a plausible benign cause for the hover — but not for the wheel/trim separation.

**Evidence.** Shot 05: car body `[2,2,2]`, wheels clear of the road, no contact shadow. Shot 07: car body `[2,2,2]`, rear wheel/trim gap; background cars over the canal band `[42,97,147]`. Shot 03: black car over the canal bank. (No RGB signature of its own; the issue is geometric.)

**Suspected subsystem.** Vehicle atomic/frame transform or external vehicle plugins. The Xbox callback reads `RwD3D9GetTransform(D3DTS_WORLD)` and calls `_rwD3D9VSSetActiveWorldMatrix` at `src/render/vehiclePipe.cpp:1455-1465`; per-mesh panels use the atomic frame. The game directory contains `VehDeform.SA.asi` and `VehIK.asi` — both can deform/pose vehicles and are not part of SkyGfx. The `VehDeform`/`VehIK` configs should be disabled for an A/B before blaming the renderer.

**Classification.** **Content/external plugin/transform**, not proven SkyGfx. This is the least certain top issue. (The previous baseline's BASE-07 was also “OPEN” with no root cause; this run does not improve the certainty.)

**Probe / verify.** Disable `VehDeform.SA.asi` and `VehIK.asi`; test with hydraulics inactive; compare the same car parked; log the atomic frame positions for the body/panels. Success test: all panels attached, wheels on the ground, contact shadow present.

---

### R2-06 — Flat/blown water; white sun cone / white water band

**Severity:** 2 · **Frequency:** 13/23 — shots 01, 02, 03, 07, 08, 10, 11, 12, 13, 14, 15, 16, 17 · **Class:** SHADER/EXPOSURE + INI

**Visual.** The canal/ocean surface is a flat blue/teal plane with little or no foam, reflection or wave detail. In the underwater shots a **huge white cone** grows from the bottom centre, with a sharp triangular apex and a soft base; at the surface the bottom half of the frame becomes a featureless **pure-white** area. These are distinct: the canal bands are legitimate water (not the old full-frame veil), while the white cone/band is a blown highlight.

**Exact evidence.**

| Shot | Region | bbox | Median RGB | Note |
|---|---|---|---|---|
| 07 | canal band | `[650,285,850,320]` | **42,97,147** | legitimate blue water |
| 10 | canal band | `[600,295,900,340]` | **42,97,145** | legitimate blue water |
| 12 | water | `[1100,300,1400,450]` | **17,33,36** | ripples present |
| 13 | white cone | `[800,780,950,930]` | **189,217,217** | bright, soft |
| 14 | white cone | `[468,492,1404,992]` | **255,255,255** | 278,256 px = 15.9%; std 0 |
| 15 | white band | `[0,584,1768,992]` | **255,255,255** | 649,440 px = 37.0%; std 0 |
| 15 | teal band | `[700,500,1100,580]` | **57,81,83** | water body |

**Suspected subsystem.** Vanilla water + underwater path. The custom water pipe is **not running**:

```text
[water] gate failed (enable=0 vs=23D5E3A8 ps=23D5E790) -> original draw
```

`CWaterLevel__RenderAndEmptyRenderBuffer_hook` at `src/core/main.cpp:5003-5025`; live `waterParallaxEnable=0` (parse at `main.cpp:6125`). `waterUnderwaterFog=1.0` (`main.cpp:6131`; `src/render/postfx_underwater.cpp:314+`; shader `shaders/ps/Water_Underwater.hlsl:111-121`). The white cone is **not** the mod's god rays (`godRaysEnable=0`) or vehicle sun glare (`sunGlare=0`); it is the vanilla sun glare/shaft, the water sun reflection, or the `Water_Underwater` in-scatter — pure 255 and hard-edged. The likely cause is exposure/tonemap or a missing clamp in the water/underwater shader, not a texture leak (the white area is uniform 255, not a stale atlas).

**Classification.** **Shader/exposure** + **INI workaround**. The old BASE-04 full-frame water veil is absent because the water pipe is bypassed, not because the WVP fix was verified. The flat canal is vanilla water at a grazing angle; the ripple in shot 12 proves the surface can texture.

**Probe / verify.** A/B `waterUnderwaterFog=0` (isolates the underwater SSS), then `waterParallaxEnable=1` with the WVP fix (isolates the custom water). Log the water surface shader/tonemap exposure and clamp the sun reflection to <1.0 before tonemap. Success test: no `[255,255,255]` water areas; the surface keeps wave detail and a controlled sun highlight.

---

### R2-07 — Oversized/detached vegetation and floating panels

**Severity:** 2 · **Frequency:** 4/23 — shots 16, 17, 21, 23 · **Class:** CONTENT/STATE

**Visual.** A row of hedge leaf cards has **stepped diagonal edges** and detached cards at the right (16); black triangular foliage cards hang at the left with a white gap (21); a grey/green slab floats at the far left (17); a light-blue translucent panel floats at the right edge above the road (23). The cards are hard-edged and much larger than a leaf; they do not read as foliage.

**Exact evidence.** Shot 16 hedge `[22,28,14]`, detached card `[18,25,13]`, water `[83,101,94]`; shot 21 black foliage `[33,32,31]`, sand `[101,91,79]`; shot 17 slab `[113,100,84]`; shot 23 panel `[88,110,124]`.

**Suspected subsystem.** Vegetation impostor/leaf-card path (`treeAlphaPS` / `buildingPipe_selectPS` at `src/render/buildingPipe.cpp:765-1033`; the log prints `treeAlpha(act=0 en=1 prof=0 mul=1.000 …)`, so the tree-alpha shader is **not active**). The black cards may also be the R2-01 texture desync sampling a dark atlas region. The light-blue panel in 23 may be a water/building plane, not vegetation. The geometry/placement is likely **content** (vegetation mod in `modloader`/`models`) or a transform/culling fault; it is not a single SkyGfx shader.

**Classification.** **Content/vegetation + possible state.** Do not over-attribute to SkyGfx; A/B the vegetation mods and check `treeAlpha` activation.

**Probe / verify.** Disable vegetation mods in `modloader`/`models`; log the atomic frame/scale of the hedge cards; check whether `treeAlpha` activates. Success test: hedge cards have soft leaf edges and stay attached; the floating slab/panel disappears.

---

### R2-08 — Giant flat ped silhouette in the sky

**Severity:** 2 · **Frequency:** 1/23 — shot 19 · **Class:** STATE (low confidence)

**Visual.** A giant humanoid silhouette (head, arms, torso) sits at centre-bottom of shot 19, filled with a **uniform beige** `[116,104,95]`; per-channel std `[2.1,0.8,0.5]`. The interior is too uniform to be the textured ped model, and the pink cloud quads overlay the sky around it. It reads as a **shadow/reflection decal** of a ped projected at the wrong scale/position, or a ped seen through the cloud layer.

**Suspected subsystem.** The stored-shadow/reflection path (same family as R2-03) — a ped-shaped decal with a flat fill. It could also be a **ped-bound leaf/shadow quad** from the previous baseline's BASE-10 class. Low confidence: one frame, no repro.

**Classification.** **State bug** (shadow/reflection projection), low confidence. Not a shader-math bug; not content.

**Probe / verify.** Reproduce at the same camera; toggle `pedShadows=0`, `stencilShadows=0`, `disableClouds=1`; capture the draw order. If the silhouette disappears with the stored-shadow fix (R2-03), merge it into R2-03.

---

### R2-09 — Minimap label crowding

**Severity:** 1 · **Frequency:** 21/23 · **Class:** CONTENT/HUD

**Visual.** White radar labels (`BS`, `R`, `Cv`, `N`) overlap each other and the radar rim; worst in the canal/driving shots. The radar blips/roads remain readable underneath. This is a HUD/zone-label issue, not the 3D renderer; no SkyGfx source line is involved.

**Classification.** **Content/INI (HUD)**, cosmetic. Not counted against the renderer.

**Probe / verify.** Check the HUD/radar label settings or discovered-zone state; no SkyGfx change required.

---

## 6. Comparison against the 2026-10-24 baseline (BASE-01 … BASE-16)

### 6.1 The comparison is mode-confounded — read this first

The 2026-10-24 registry was written against a **PBR** run: its root-cause notes cite `VehiclePBR_Modern`, `pipeUploadPBR`, `texalpha`, PBR ambient and the water-pipe WVP. This run is **pure Xbox**:

```text
[Mode] apply Xbox pipes=2/1/2 cf=2
[Mode] active=Xbox (from INI)
[VPIPE] cb=Xbox ... pipe=XBOX(2) ... VS=IDR_XBOXCARVS PS=-
```

Therefore:

- a previous issue that is invisible here may be **bypassed**, not fixed;
- a previous PBR fix (texalpha, PBR ambient, PBR vehicle layers) is **not exercised** by these screenshots;
- a “new” issue may be a defect of the **Xbox implementation** that the PBR run never showed.

The visual comparison is still valid as evidence of what the tested binary actually rendered. The status column below distinguishes **visual status in this run** from **code-level proof**.

### 6.2 Status table — BASE-01 … BASE-16

| BASE | Previous title (2026-10-24) | Previous freq | Status in this run | New evidence | Caveat |
|---|---|---|---|---|---|
| **01** | CJ body fully absent | 12/24 | **FIXED (visual)** | CJ mesh/head visible in 01, 02, 04, 05, 07, 17, 21, 22, 23; no third-person shot where the player position has no CJ mesh/head (shot 03 is hidden by the black quad, not proven absent) | CJ is often white/untextured (R2-01) or hidden by a black quad (R2-03), but the mesh is submitted. The ped contract is active (`[PedOpaque]`). |
| **02** | CJ/ped partial, clipped or transparent | 5/24 | **PERSISTS (transparency only)** | No partial/clipped body; but white **translucent** peds in 03, 06, 07 and a giant silhouette in 19 | The old mesh clipping is gone; the remaining transparency is a **texture/alpha state** (R2-01), not a geometry hole. |
| **03** | Near peds flat black silhouettes | 5/24 | **PERSISTS — signature changed black → white/grey** | Head `[207,200,194]`, torso `[80,76,75]`, legs `[55,52,48]` in 17; head `[163,158,152]` in 02; ghost peds in 03 | Same class (ped material/texture), different colour. The same CJ is textured in 05/21/22, so it is intermittent/state, not a missing asset. |
| **04** | Stepped translucent water veil over land/sky | 14/24 | **FIXED IN THIS RUN — config workaround** | No full-frame veil; only canal/horizon bands with ripples (`[42,97,147]` in 07/10); log `[water] gate failed (enable=0 …)` on every flush | The custom water pipe is **disabled** (`waterParallaxEnable=0`), so the working-tree WVP fix is **not verified**. Re-test with `waterParallaxEnable=1` before claiming a code fix. |
| **05** | Water flat/untextured | 7/24 | **PERSISTS (partial improvement)** | Canal flat `[42,97,145]`; underwater ripple visible in 12 (`[17,33,36]`) but the sun cone/white band is blown to 255 | The near surface can texture; there is still no foam/reflection, and the white cone/band is worse than “flat”. See R2-06. |
| **06** | Vehicle translucency, dissolve, mirror-chrome shells | 7/24 | **FIXED VISUALLY (Xbox mode); PBR path unverified** | Cars are opaque; no dissolve; no mirror-chrome; body `[2,2,2]`–`[3,2,2]` | The previous texalpha fix is PBR-specific and is not exercised. The new **R2-04** (flat black / green roof ghost / grey window) is a different vehicle-material defect. The shot-11 smoke is a particle effect, not body translucency. |
| **07** | Vehicle panels detached/floating; cars hover | 8/24 | **PERSISTS** | Car floats in 03/05/07; rear wheel/trim displaced in 07; background cars float in 01–03 | Some hover is plausibly **hydraulics**; the detached wheel/trim is not. `VehDeform.SA.asi`/`VehIK.asi` are installed and must be A/B’d. |
| **08** | Black car interiors / opaque glass voids | 3/24 | **PERSISTS / CHANGED** | Grey opaque rear window `[11,11,10]` in 09; dark cabin in 07 | No clear side-glass chase shot in this set; the interior is still not visible. |
| **09** | Black shadow-blob sprites / shadow acne | 12/24 | **PERSISTS (subset, 4/23)** | Pure-black quads in 01 `[0,480,1768,992]` 40.6%, 02 `[0,584,1768,992]` 34.5%, 03 `[304,504,948,992]`, 06 `[476,468,1768,992]` **2,2,2** | The previous count included semi-transparent blobs; this run’s clear defect is the **pure-black stored-shadow** quads. Beach shadows in 17/22 are semi-transparent and plausible, so they are **not** counted. The stored-shadow hook is verifiably skipped. |
| **10** | Floating world geometry, props and vegetation | 6/24 | **PERSISTS** | Hedge cards 16; black foliage 21; floating slab 17 `[113,100,84]`; light-blue panel 23 `[88,110,124]` | Less widespread than the previous run; vegetation/content is the main suspect. |
| **11** | Z-fighting / “double ground” | 3/24 | **NOT OBSERVED** | Ground single-layer in 04/05/09/22; no duplicated terrain | Not proven fixed: the route and the water workaround differ. Keep OPEN/unverified. |
| **12** | Earshot debug banner | 1/24 | **NOT OBSERVED** | No interior shot; no banner | Unverified. |
| **13** | Minimap label crowding | 23/24 | **PERSISTS** | `BS`/`R`/`Cv`/`N` overlap in ~21/23 | HUD/content, not the 3D renderer. |
| **14** | Sky streaks / vertical dark columns | 1/24 | **NOT OBSERVED** | No vertical streaks; sky clear or cloud quads | Unverified. |
| **15** | Untextured bright red cube sprites | 2/24 | **NOT OBSERVED** | No saturated red cube component; the only per-frame red is the HUD health bar `[1512,152,1672,164]` `[175,32,36]`; other reds are the billboard/swimsuit | Not proven fixed: route/scene differs. |
| **16** | Washout / haze / pastel grading + stepped sky | 23/24 | **FIXED (grading) / NEW sky failure** | Deep blue sky `[43,107,174]` in 10; natural sand/grass in 05/21/22; no pastel haze | The colour filter is `PC` with `colorFilterEnable=0`; the PBR MODERN grading is not exercised. The new **R2-02** opaque cloud quads are a different sky failure. |

### 6.3 FIXED / PERSIST / NEW — explicit buckets

**FIXED or BYPASSED in this run (4):**

| ID | Why it counts | Honest qualification |
|---|---|---|
| **BASE-01** | CJ mesh/head is submitted and visible in the on-foot shots; no BASE-01-type absent body | CJ is white/untextured or shadowed; this is a different defect, not a clean pass |
| **BASE-04** | No full-frame water veil over land/sky | **Config workaround**: `waterParallaxEnable=0`; the code fix is not exercised |
| **BASE-06** | No vehicle dissolve/translucency/mirror-chrome | **Xbox mode**: the PBR/texalpha path is not exercised; the new R2-04 replaces it |
| **BASE-16** | No washout/pastel haze; sky is deep blue, sand/grass natural | **PC colour filter / grading off**; the PBR grading path is not exercised; the new R2-02 cloud quads are separate |

**PERSIST (8):**

| ID | What persists | New form / evidence |
|---|---|---|
| **BASE-02** | ped transparency | White translucent peds in 03/06/07; giant silhouette 19 |
| **BASE-03** | untextured near peds | Black → **white/grey** mannequins (R2-01); same class, new signature |
| **BASE-05** | flat/untextured water | Flat canal `[42,97,145]`; white cone/band 255 (R2-06) |
| **BASE-07** | floating/detached vehicles | Car hover 03/05/07; wheel/trim detached 07 (R2-05) |
| **BASE-08** | opaque glass / black interior | Grey opaque rear window `[11,11,10]` in 09 |
| **BASE-09** | shadow defects | Pure-black quads 01/02/03/06; stored-shadow hook skipped (R2-03) |
| **BASE-10** | floating world geometry/vegetation | Hedge 16; black foliage 21; slab 17; panel 23 (R2-07) |
| **BASE-13** | minimap label crowding | ~21/23; HUD/content |

**NEW in this run (4):**

| ID | New issue | Why it is new / not just a BASE rename |
|---|---|---|
| **R2-02** | Opaque pink/beige cloud quads (59–83% coverage) | The previous baseline’s sky problem was washout/banding, not huge opaque billboards. This is a **content/blend mismatch** (white-RGB/alpha-0 cloud textures + additive ONE/ONE). |
| **R2-04** | Vehicle material corruption: flat black, green grass ghost on roof, grey opaque rear window | The previous vehicle issue was translucency/chrome; this is **opaque but wrongly textured** (NULL paint texture bind). Distinct from BASE-06. |
| **R2-06** | Blown white water cone/band (`[255,255,255]`, 16–37% of frame) | The previous flat water was not pure white; this is an **exposure/underwater in-scatter** failure. Related to BASE-05 but a new signature. |
| **R2-08** | Giant flat ped silhouette in the sky (uniform beige, std <2.1) | Not present in the previous baseline; likely a shadow/reflection projection, one frame. |

**NOT OBSERVED / UNVERIFIED (4):** BASE-11 (Z-fighting), BASE-12 (Earshot banner), BASE-14 (sky streaks), BASE-15 (red cubes). Do **not** mark these fixed from this run; the route/scene differs and the artifacts may simply not have been triggered.

### 6.4 What this comparison changes for the next wave

1. **The previous PBR fixes cannot be credited from this run.** The Xbox mode bypasses the PBR vehicle/building pipeline. If the PBR mode is the target, run the same route once with `pipeline=PBR` and compare; otherwise the “fixed” rows are only visual, not code-level.
2. **The ped problem did not disappear; it changed colour.** BASE-01/02/03 were three symptoms of the ped path. This run shows the mesh is submitted (BASE-01 fixed) but the **texture/alpha** is still wrong (white/grey/ghost). That points at the **texture bind/cache**, not the ped contract. The ped contract logs prove the alpha/depth repair runs; the remaining defect is RGB/UV/texture-raster.
3. **The cloud problem is worse and better understood.** The audit predicted “faint/flattened” clouds; the live run shows **opaque 59–83% coverage**. The direct TXD parse explains why: `cloudmasked`/`cloudhigh` are white-RGB/alpha-0, and the additive cloud path ignores alpha. This is now an evidence-backed content/blend issue, not a guess.
4. **The shadow problem has a concrete, verifiable gap.** `CShadows::RenderStoredShadows` is skipped on this exe (`0x53E0C3` mismatch). The pure-black quads are consistent with an un-healed stored pass. This is the highest-confidence code fix in the list.
5. **The water veil is gone only because the water pipe is off.** Do not close BASE-04. The WVP fix in the working tree is still unverified; `waterParallaxEnable=1` must be tested with the same canal/beach route.

---

## 7. Direct answers

### (a) Are clouds present and geometrically correct?

**Present only in the sunset shots (18–20); not geometrically correct.** Shots 01–17 and 21–23 have a clear sky (or an underwater view) with no cloud layer. In 18–20 the cloud layer is present but renders as **huge, hard-edged, rotated pink/beige quads** that cover **59.0%, 59.0% and 83.4%** of the frame respectively (pink-cloud mask; medians `[139,114,109]`, `[166,131,122]`, `[155,126,114]`). There is no soft alpha fringe; the cloud texture detail is visible only as darker grey patches inside the rectangles. The direct TXD parse explains the mechanism: `cloudmasked`/`cloudhigh` are 1024×1024 A8R8G8B8 with first BGRA pixel `FF FF FF 00` (white RGB, alpha 0), while the cloud batch is additive ONE/ONE, which ignores alpha. **Verdict: clouds are present but wrong (content/blend mismatch, R2-02).**

### (b) Are shadow/decal quads still oversized/opaque?

**Yes, but only the pure-black class is clearly a defect.** Four shots (01, 02, 03, 06) contain uniform **pure-black** quads:

- 01: bbox `[0,480,1768,992]`, 713,024 px = **40.6%** of frame, median `[9,8,8]`; strict core `[876,632,1416,832]` = `[5,8,4]`.
- 02: bbox `[0,584,1768,992]`, 605,456 px = **34.5%**, median `[7,8,5]`.
- 03: bbox `[304,504,948,992]`, 111,744 px strict, median `[5,6,4]`.
- 06: bbox `[476,468,1768,992]`, 495,552 px = **28.3%**, median `[2,2,2]`, std 0 in the core.

They have straight geometric edges and no texture/gradient; they occlude CJ, the road and the car. The **beach shadows in 17/22 are different**: semi-transparent (sand visible), only slightly darker than the sand (`[47,42,36]` vs `[46,43,36]` in 17; `[94,85,71]` vs `[122,111,97]` in 22), and roughly object-sized. Those look like normal GTA shadow decals and are **not** counted. The defect is the **stored-shadow pass** (hook skipped at `0x53E0C3`), not a global “all shadows are oversized”.

### (c) Are cars still flat black (no paint reflection)?

**Yes for the player's lowrider — and it is worse than “black”: the material is wrongly textured.**

- Body medians: shot 04 `[2,2,2]`, shot 05 `[2,2,2]`, shot 07 `[2,2,2]`, shot 08 `[3,2,2]`, shot 10 `[3,2,2]`; shot 11 is `[34,34,38]` because of the smoke lighting.
- Shot 09: the roof samples the **green grass texture** (green-excess median `[31,30,14]`; pattern matches the wall behind) and the rear window is an **opaque grey slab** `[11,11,10]`.
- The chrome/detail bits are still visible at `[22,22,21]`, so it is not a fully unlit mesh; the **paint/environment layer** is missing and the NULL paint texture is sampling the previous atlas.

Not every car is black: a red sedan in shot 07 keeps a red/brown diffuse (visually), so the defect is **material/env-specific** on the Xbox path, not a global “black car shader”. **Verdict: yes for the player car, with an additional green-texture corruption (R2-04).**

### (d) Is there any remaining see-through / missing geometry?

**No large world holes in this set.** Roads, pavements, buildings, terrain, the bridge and the ship are solid; CJ’s mesh is submitted (BASE-01 fixed); the ground resolves to one layer (BASE-11 not observed). The remaining transparency is **object-level**, not world-level:

- peds are white/grey and **semi-transparent** in shots 03, 06 and 07 (you can see the road/car through them);
- the car rear window in shot 09 is an **opaque grey** slab, not see-through — the interior is simply not visible;
- a **light-blue translucent panel** floats at the right edge of shot 23 (`[88,110,124]`), above the road;
- the water surface is present (not a hole), though overexposed to white in 14/15.

The old “CJ body fully absent” hole (BASE-01) is gone. **Verdict: no major world see-through; peds and one floating panel are the remaining transparency defects.**

### (e) Is the green colour cast still present?

**No global green cast in the daylight shots.** The deep-blue sky in shot 10 is `[43,107,174]`; the sand in 21 is `[101,91,79]`; the grass is green because it is grass; the beach peds have natural skin/swimsuit colours. The only green/teal cast is the **underwater** water (`[17,33,36]`–`[57,81,83]`), which is expected. The previous washout/pastel grading (BASE-16) is not present. The sunset shots 18–20 have a **warm pink** cast, which is the expected sunset tint — but the cloud quads amplify it into flat pink rectangles. **Verdict: no remaining global green cast; the earlier washout/green look is fixed in this mode.**

### (f) Red untextured cubes / ped-bound leaf quad — still present?

**Red untextured cubes: no.** Across all 23 frames, the only per-frame saturated red component is the **HUD health bar** at bbox `[1512,152,1672,164]`, median `[175,32,36]`. Two other red blobs are contextual: the “ADULT BOOKS & VIDEOS” billboard in shot 08 (`[1704,364,1768,396]`, `[120,63,50]`) and the red swimsuit ped in shot 22 (`[1128,272,1172,344]`, `[124,65,40]`). No solid untextured red cube was found. The previous BASE-15 class is **not observed** (not proven fixed; the route/props differ).

**Leaf/vegetation quad: yes, in a different form.** Shot 16 has a row of hedge leaf cards with stepped diagonal edges and detached cards (`[22,28,14]`; detached `[18,25,13]`). Shot 21 has black triangular foliage cards at the left with a white gap/triangle (`[33,32,31]`). Shot 17 has a grey/green slab floating at the far left (`[113,100,84]`). Shot 23 has the light-blue floating panel (`[88,110,124]`). There is **no ped-bound bush over CJ’s head** in this set; the previous “ped-bound leaf quad” is not reproduced. **Verdict: red cubes no; oversized/detached leaf and panel quads yes (R2-07).**

---

## 8. Next five highest-value fixes

### Fix 1 — Make texture binds survive the cloud pass (highest value)

**Targets:** R2-01, R2-04 (green ghost), likely parts of R2-03/R2-07/R2-08.
**Why first:** One root cause explains several unrelated-looking failures: the peds are grey/white/ghostly, the car roof samples grass, and the shadow quads can sample the wrong atlas. The cloud hook saves/restores 12 render states but **not the texture raster**; the RW cache and the device disagree after CClouds, so the next `RwD3D9SetTexture` can no-op.

**What to change (candidate):**

1. In `CClouds__Render_hygiene` (`src/core/main.cpp:4544-4820`), capture `rwRENDERSTATETEXTURERASTER` before the original call and restore it **through L1** after the pass (`RwRenderStateSet(rwRENDERSTATETEXTURERASTER, saved)`), exactly like `carBlendRestore3L` in `src/render/neoCarpipe.cpp:245-252`. The L1 set is a real transition when the cache differs, and RW’s own flush re-binds the driver/device stage.
2. Add a texture resync at the start of `myDefaultCallback` (the ped/default path, `src/core/main.cpp:1564-1818`) and at the start of the Xbox vehicle callback (`src/render/vehiclePipe.cpp:1419+`). A `pipeSetTexture(currentTexture, 0)`-style re-bind, or a small helper that reads the L1 raster and writes it back through L1+L2+L3, is enough.
3. Replace the direct `RwD3D9SetTexture(material->texture, 0)` in the Xbox cb with `pipeSetTexture(material->texture, 0)` (`src/skygfx.h:1259`), which resolves NULL to `gpWhiteTexture` and forces a transition.
4. Log a one-shot receipt: `GetTexture(0)`, `rwRENDERSTATETEXTURERASTER`, and the intended material texture at ped/vehicle entry. The log should show them equal.

**Verify:** CJ head shows a face/skin tone in shots 02/17/23; the ghost peds in 03/06/07 become opaque and textured; the car roof in 09 no longer shows grass; the black quads shrink or become soft.
**Risk:** COM AddRef/Release discipline. Prefer RW state setters over borrowed raw `IDirect3DBaseTexture9*` pointers; do not leak a raster reference.

### Fix 2 — Fix the Xbox vehicle NULL-paint-texture branch and env bind

**Targets:** R2-04 (flat black, green roof ghost, grey rear window).
**Why:** The Xbox callback chooses the textured branch from **geometry flags**, then binds `material->texture` directly. Paint meshes have UVs but no diffuse texture; the NULL bind no-ops and samples the previous atlas (grass/cloud). The body then multiplies that to black; the roof shows the grass. The env/spec bits decide whether a reflection exists at all.

**What to change (candidate):**

- At `src/render/vehiclePipe.cpp:1620-1635`, branch on `material->texture` (and the geometry flags), not geometry flags alone. For a NULL paint texture, use the `SELECTARG2`/diffuse path or `pipeSetTexture` with the white fallback; do not leave the previous stage-0 texture bound.
- Log `material->texture`, `flags`, `materialFlags`, `hasEnv1`, `hasEnv2`, `hasSpec`, `envSwitch` for the player lowrider. If `hasEnv1/hasEnv2` are false on the paint, find where the material's env bits are lost (texalpha/material setup, `src/render/texalpha.cpp`, `vehiclePipe.cpp:1470-1600`).
- A/B `vehiclePipe=PC` or a PBR preset on the same parked car to determine whether the flat black is Xbox-only.

**Verify:** body shows a dark paint colour with a controlled sky/building reflection; no grass on the roof; rear window is tinted/dark, not a flat grey slab.
**Risk:** Do not make every car white by forcing `gpWhiteTexture` on genuinely textured meshes; guard on `material->texture == NULL` and keep the env branch intact.

### Fix 3 — Hook the stored-shadow pass (concrete, high-confidence)

**Targets:** R2-03; possibly R2-08.
**Why:** The log proves `CShadows::RenderStoredShadows` is **not** under the shadow state contract:

```text
[shadow_install] 0x53E0C3 ... orig=68E8DD00 MISMATCH ... SKIPPED — vanilla path kept
```

The static and extra-player passes are healed; the stored pass keeps the drifted blend/alpha state and paints opaque black quads. This is the clearest code-level gap in the run.

**What to change (candidate):**

- Re-verify the `0x53E0C3` call target in the live `gta_sa.exe` (the log says `orig=68E8DD00`, not the expected `00708300`; the site may point to a launcher/thunk or a different build). Update the expected target/signature in `src/core/main.cpp:7542-7590`, or install a **call-site trampoline** that calls the real target and wraps it with `shadowContractRun`.
- Alternatively, move the shadow contract up to `CScene::Render` around the three shadow calls (the three zero-arg calls are adjacent), so it does not depend on the exact callee address.
- Add a one-shot log of `ALPHABLENDENABLE/SRCBLEND/DESTBLEND/ALPHAREF/ZWRITE/ZFUNC` at the stored pass.

**Verify:** shots 01/02/03/06 show a soft, semi-transparent shadow (road/CJ visible through it), not `[2,2,2]`. If the quads remain, the next suspect is the cloud texture-raster leak into the shadow draw (Fix 1).
**Risk:** The stored pass may be used by multiple shadow types; force only the canonical shadow state, not a blanket blend change.

### Fix 4 — Premultiply the cloud batch and repair the sky-dome state

**Targets:** R2-02.
**Why:** The installed `cloudmasked`/`cloudhigh` textures are **white RGB / alpha 0**, while the cloud batch is **additive ONE/ONE**, which ignores alpha. The result is an opaque pink rectangle. This is not the audit’s “faint/flattened” case; it is a content/blend mismatch.

**What to change (candidate), in order of robustness:**

1. Wire a premultiplied cloud sprite shader (`float4(rgb*opacity, opacity)` with ONE/ONE) per `docs/plans/2026-10-25-clouds-ffp-audit.md` §6.2. This preserves the additive contract and removes the white background.
2. Or convert the `cloudmasked`/`cloudhigh` RGB backgrounds to black (premultiply the texture) in `models/particle.txd`; keep `cloud1` as-is. This is a content fix and does not need a build.
3. Force stage-0 `COLOROP/ALPHAOP` + args on **entry** (audit §6.3) and repair the state before `CClouds::RenderSkyPolys` (`0x53D81C`, wrapper `src/render/envmap.cpp:569`), not only inside `CClouds::Render` (`src/core/main.cpp:4544+`). The audit §5.2 cause #2 explains why the dome is outside the current repair.

**Verify:** sunset sky (shots 18–20) shows soft-edged clouds; no hard pink rectangles; cloud coverage no longer occupies >50% of the frame as flat fill.
**Risk:** A shader bound for the whole cloud window can shade deferred corona quads (audit §4.5); arm a `g_batchIsClouds` flag or use the exact producer tag.

### Fix 5 — Clamp the water sun reflection / underwater in-scatter

**Targets:** R2-06 (white cone/band).
**Why:** The cone/band is pure `[255,255,255]` over 16–37% of the frame, with hard geometric edges. It is not the mod’s god rays (`godRaysEnable=0`) or vehicle sun glare (`sunGlare=0`); it is the water sun reflection / underwater in-scatter (`Water_Underwater.hlsl:111-121`, `postfx_underwater.cpp:314+`), over-exposed before tonemap.

**What to change (candidate):**

- A/B `waterUnderwaterFog=0` in the INI (isolates the underwater SSS) while keeping `waterParallaxEnable=0`. If the white cone disappears, the SSS/in-scatter is the source; clamp `sss`/`sssColour` and the fog output to <1 before tonemap.
- A/B `waterParallaxEnable=1` with the WVP fix (isolates the custom water path). If the white band is the vanilla water sun reflection, clamp the specular term in the water shader/tonemap.
- Log the water shader constants and the pre-tonemap value at the cone/band pixels; a `[255]` after tonemap with a low pre-tonemap value points at exposure, not the water shader.

**Verify:** no `[255,255,255]` water areas; the surface keeps ripple detail and a controlled sun highlight.
**Risk:** Do not lower the global exposure just to fix the water; the daylight shots are correctly exposed.

### Runner-up fixes (not in the top five)

- **R2-05 vehicle hover/detach:** A/B `VehDeform.SA.asi` and `VehIK.asi`, test with hydraulics inactive, then log the atomic frame matrices. Do this before changing SkyGfx; the game directory contains both plugins.
- **R2-07 vegetation/panel:** A/B the vegetation mods in `modloader`/`models`; check `treeAlpha` activation (`buildingPipe.cpp:765-1033`); the floating panel in shot 23 may be a water/building plane, not vegetation.
- **R2-09 minimap:** HUD/radar settings, no SkyGfx change.
- **BASE-04 re-test:** run the same canal/beach route with `waterParallaxEnable=1` to verify the working-tree WVP fix; the current run only proves the workaround.

---

## 9. Angle-series follow-up

The angle-series run repeats the same route with the camera swept across the ped/vehicle view angles on the same build. Its evidence lives in [`docs/baseline/2026-10-25-angle-series/README.md`](../2026-10-25-angle-series/README.md); that file is written by the angle-series lane, so this section is a pointer only.

Run these three in-game A/B levers next, one at a time, from the same camera positions:

1. **`pedCullMode`** — `[SkyGfx] pedCullMode`: `-1` preserves the game's own ped-span cull (current behaviour); `1` forces CULLNONE (two-sided), `2` CULLBACK, `3` CULLFRONT. Receipt: `[PedCull] mode=... saved=... forced=...`. This tests whether the missing or ghost ped faces are a cull-side issue.
2. **`pedAlphaTestEnable`** — `[SkyGfx] pedAlphaTestEnable`: `1` keeps the canonical GREATEREQUAL@1 ped alpha test (current behaviour); `0` forces ALPHATESTENABLE off and skips the ALPHAFUNC/ALPHAREF writes. Receipt: `[PedAlphaTest] enabled=...`. It only has an effect while `pedOpaqueContract=1`.
3. **Mode disengage test** — set `[Modes] active=None` (or toggle the lit mode chip off) and repeat the route. Toggling off restores the per-slot pre-mode base verbatim, so a defect that disappears is mode-profile/state ownership, not the renderer itself.

Both ped keys are diagnostics, not look controls; their defaults (`-1`, `1`) preserve the behaviour captured in this baseline. Wiring is documented in [`architecture.md`](../../architecture.md).

## 10. Appendix — exact patch evidence, masks, and source map

### 10.1 Artifact-mask metrics (full frame = 1,753,856 px)

| Shot | Mask | bbox | Area (px) | Coverage | Median RGB |
|---|---|---|---|---|---|
| 01 | black, loose | `[0,480,1768,992]` | 713,024 | 40.6% | **9,8,8** |
| 01 | black, strict core | `[876,632,1416,832]` | 43,328 | 2.5% | **5,8,4** |
| 02 | black, loose | `[0,584,1768,992]` | 605,456 | 34.5% | **7,8,5** |
| 03 | black, strict | `[304,504,948,992]` | 111,744 | 6.4% | **5,6,4** |
| 06 | black, strict | `[476,468,1768,992]` | 495,552 | 28.3% | **2,2,2** |
| 14 | white, strict | `[468,492,1404,992]` | 278,256 | 15.9% | **255,255,255** |
| 15 | white, strict | `[0,584,1768,992]` | 649,440 | 37.0% | **255,255,255** |
| 18 | pink cloud | `[0,0,1768,892]` | 1,210,048 | 69.0% | **139,114,109** |
| 19 | pink cloud | `[0,0,1768,804]` | 1,034,304 | 59.0% | **166,131,122** |
| 20 | pink cloud | `[0,0,1768,992]` | 1,463,136 | 83.4% | **155,126,114** |

The pink-cloud mask is intentionally broad (`R > 120`, `R > G+12`, `G > B+2`, `B > 70`); the coverage numbers are a **lower bound** on how much of the sky is cloud-quad fill. The black mask is `R,G,B < 6`; the loose row includes dark scene content and is shown only to size the screen-space quad.

### 10.2 Key 5×5 median patches (targeted regions)

| Shot | Label | bbox | Median | Mean | Std |
|---|---|---|---|---|---|
| 01 | black quad | `[650,750,750,850]` | **5,8,4** | 6.7,8.7,5.8 | 4.6,2.6,3.7 |
| 02 | CJ torso | `[840,520,910,600]` | **99,93,87** | 96.3,91.0,85.7 | 35.0,33.6,33.0 |
| 02 | shadow | `[700,760,900,900]` | **8,7,7** | 7.8,7.7,6.9 | 2.3,1.9,2.3 |
| 03 | black quad | `[400,760,600,900]` | **2,2,2** | 3.5,3.5,3.3 | 1.7,1.7,1.7 |
| 04 | car body | `[900,150,1100,250]` | **2,2,2** | 3.1,2.8,2.2 | 1.9,1.9,0.8 |
| 04 | chrome | `[650,180,750,240]` | **22,22,21** | 22.2,21.6,21.0 | 15.6,15.5,15.4 |
| 05 | car body | `[1150,200,1400,330]` | **2,2,2** | 5.6,5.8,5.4 | 8.9,10.4,8.5 |
| 06 | black rect | `[900,780,1200,950]` | **2,2,2** | 2.0,2.0,2.0 | 0.0,0.0,0.0 |
| 07 | car body | `[450,500,650,650]` | **2,2,2** | 5.9,5.4,5.2 | 11.1,10.9,10.6 |
| 07 | CJ white | `[830,780,920,900]` | **101,97,90** | 107.9,101.9,97.1 | 19.5,19.5,18.5 |
| 07 | canal | `[650,285,850,320]` | **42,97,147** | 41.4,89.3,135.4 | 11.8,23.9,37.5 |
| 08 | car body | `[800,600,950,720]` | **3,2,2** | 3.4,2.4,2.9 | 2.6,2.6,2.4 |
| 09 | roof green ghost | green-excess mask `[450,380,950,600]` | **31,30,14** | 31.0,29.6,13.5 | — |
| 09 | rear window | `[380,800,560,900]` | **18,18,15** | 14.7,14.6,13.1 | 10.8,10.9,10.1 |
| 10 | car body | `[800,550,950,700]` | **3,2,2** | 3.9,3.7,2.9 | 3.1,3.1,2.4 |
| 10 | canal | `[600,295,900,340]` | **42,97,145** | 43.7,73.4,102.6 | 15.8,32.7,58.4 |
| 11 | car body | `[800,600,1050,850]` | **34,34,38** | 36.1,36.2,39.2 | 11.8,11.4,13.2 |
| 12 | water | `[1100,300,1400,450]` | **17,33,36** | 17.8,33.9,36.4 | 3.7,5.2,6.2 |
| 12 | white cone | `[800,750,950,900]` | **200,211,213** | 198.5,209.5,212.0 | 16.2,16.5,16.7 |
| 13 | white cone | `[800,780,950,930]` | **189,217,217** | 187.3,216.1,215.7 | 7.8,7.6,7.8 |
| 14 | white cone | `[750,700,1000,900]` | **255,255,255** | 255.0,255.0,255.0 | 0.0,0.0,0.0 |
| 15 | white band | `[700,800,1100,950]` | **255,255,255** | 255.0,255.0,255.0 | 0.0,0.0,0.0 |
| 15 | teal band | `[700,500,1100,580]` | **57,81,83** | 58.4,83.1,85.2 | 9.2,8.9,8.8 |
| 16 | hedge | `[700,350,900,500]` | **22,28,14** | 32.6,42.4,30.3 | 20.5,25.8,28.9 |
| 16 | detached hedge | `[1400,300,1600,400]` | **18,25,13** | 24.1,33.9,22.5 | 11.2,19.2,23.0 |
| 17 | CJ white | `[850,480,950,620]` | **75,71,68** | 91.8,86.4,81.3 | 47.1,45.4,43.8 |
| 17 | shadow quad | `[600,650,900,800]` | **47,42,36** | 49.5,44.9,39.5 | 8.4,8.8,10.1 |
| 17 | floating slab | `[20,540,180,680]` | **113,100,84** | 106.6,94.9,80.6 | 21.2,17.6,14.8 |
| 18 | pink quad | `[450,120,750,300]` | **168,132,125** | 171.7,139.4,131.9 | 26.7,22.2,20.0 |
| 19 | giant ped | `[780,700,950,900]` | **116,104,95** | 116.9,103.8,95.0 | **2.1,0.8,0.5** |
| 19 | pink quad | `[400,120,700,300]` | **166,130,122** | 176.7,139.6,130.7 | 17.8,16.3,14.7 |
| 19 | sun | `[1200,220,1350,330]` | **255,219,129** | 252.8,219.1,129.4 | 6.8,32.0,3.2 |
| 20 | pink quad | `[450,120,750,300]` | **143,114,107** | 141.5,113.0,106.4 | 9.6,6.7,5.6 |
| 21 | black foliage | `[100,200,300,400]` | **33,32,31** | 46.9,43.7,38.6 | 28.7,25.2,21.6 |
| 21 | sand | `[650,350,850,500]` | **101,91,79** | 96.5,87.4,75.4 | 20.9,18.3,18.3 |
| 22 | shadow diamond | `[650,400,900,550]` | **96,86,74** | 94.8,85.0,73.4 | 15.5,14.6,14.5 |
| 22 | sand | `[850,650,1100,800]` | **122,111,97** | 102.1,95.6,87.9 | 43.0,36.6,27.7 |

**Point samples (5×5) used in the issue text:**

| Shot | Point | Median | Interpretation |
|---|---|---|---|
| 02 | (895,485) | **163,158,152** | CJ head, white, no face |
| 02 | (910,570) | **109,102,96** | CJ torso, grey |
| 17 | (875,450) | **207,200,194** | CJ head, white |
| 17 | (890,530) | **80,76,75** | CJ torso, grey |
| 17 | (890,705) | **55,52,48** | CJ legs, grey |
| 23 | (900,480)/(900,550) | **92,89,86** | CJ head/torso, untextured |
| 23 | (900,700) | **39,35,34** | CJ legs |
| 23 | (1600,400) | **88,110,124** | floating light-blue panel |
| 09 | (450,850) | **11,11,10** | opaque grey rear window |
| 09 | (650,480) | **30,25,16** | green/olive roof ghost |
| 19 | (850,800) | **116,104,95** | giant flat ped silhouette |

### 10.3 Log excerpts (log time, not in-game clock)

```text
[22:35:22.701] [INFO] - [Mode] apply Xbox pipes=2/1/2 cf=2
[22:35:22.705] [INFO] - [Mode] active=Xbox (from INI)
[22:35:22.707] [INFO] - [BUILD] asipath=E:\games\gtasa_skygfx_plus\skygfx.asi size=2360832 mtime=2026-10-25 21:57:27 built=Oct 25 2026 21:56:44
[22:35:25.697] [INFO] - [INIT] shaders: compiled OK (ps=58 vs=17), FAILED (ps=0 vs=0)
[22:35:22.734] [INFO] - [shadow_install] 0x53E0BE op=E8 orig=00707F40 OK zeroarg=1 site=CShadows::RenderStaticShadows
[22:35:22.735] [INFO] - [shadow_install] 0x53E0C3 op=E8 orig=68E8DD00 MISMATCH zeroarg=1 site=CShadows::RenderStoredShadows
[22:35:22.735] [INFO] - [shadow_install] 0x53E0C3 SKIPPED - vanilla path kept
[22:37:38.068] [INFO] - [PED] ZWRITE off/desynced on entry (rw=1 dev=0) - forcing TRUE for cb
[22:37:38.486] [INFO] - [PedOpaque] contract repaired divergent entry: dev(ref=0 fn=7 blend=0 zw=1 zen=1 zfunc=4 atest=1) rw(ref=0 ...)
[22:37:39.106] [INFO] - [PedRS] ok cull rw=2 drv=2 dev=2 | blend rw=0 drv=0 dev=0 | src rw=5 drv=5 dev=5 | dst rw=6 drv=6 dev=6 | zw rw=1 drv=1 dev=1 | afunc rw=7 drv=7 dev=7 | aref rw=0 drv=0 dev=0
[22:37:38.856] [INFO] - [CoronaBlend] dev blend=1 src=2 dst=2 (D3DBLEND: 2=ONE ...)
[22:37:38.980] [INFO] - [SKY] entry: camRas=048BEFC0 1768x992 DEV ps=00000000 vs=00000000 alphaTest=1 fn=7 ref=1 blend=1
[22:37:57.185] [INFO] - [water] gate failed (enable=0 vs=23D5E3A8 ps=23D5E790) -> original draw
[22:41:50.234] [INFO] - texalpha: 17076 opaque, 2500 cutout, 5 faded, 129276 unknown, 0 faults (cached 2330/4096) unknown_texnull=129276 ...
[22:41:50.289] [INFO] - [VPIPE] cb=Xbox obj=267B8C74 pipe=XBOX(2) ivMode=0 fpCfg=0 fpUp=0 VS=IDR_XBOXCARVS PS=-
[22:41:53.281] [INFO] - [shadow_contract] extra pre-entry layer drift: drift=4 rw(blend=0 src=5 dst=6 ref=0 ...) drv(...) dev(blend=1 src=5 dst=6 ref=100 ztest=1 zfunc=4 zw=1 fog=1)
[22:40:12.418] [INFO] - [UW] underwater ON: cam=(2751.4,-2250.8,-0.3) level=0.00 depth=0.27 str=1.00 density=0.0850 fog=(0.012,0.036,0.086) light=(0.07,0.06,0.05)
```

**Interpretation of the log:** the ped contract and shadow contract are active, but the **stored-shadow pass is not hooked**; the water pipe is disabled; the sky/clouds are fixed-function; the ped alpha/depth state still diverges every frame; the texalpha resolver is failing on most calls (`texnull`). The cloud hook's `[SKY]` receipt shows no shader bound, so the cloud quads are FFP, not a missing `CloudsPS` binding.

### 10.4 Source map (current working tree)

| Issue | File:line | What it owns / why it is a suspect |
|---|---|---|
| R2-01 | `src/core/main.cpp:4544-4820` | `CClouds__Render_hygiene`; texture raster (state 1) is not saved/restored |
| R2-01 | `src/core/main.cpp:1564-1818` | `myDefaultCallback`; shader hygiene at 1795-1818 but no texture resync |
| R2-01 | `src/render/neoCarpipe.cpp:245-252` | correct L1 `rwRENDERSTATETEXTURERASTER` restore pattern |
| R2-02 | `src/core/main.cpp:7341-7345` | CClouds hook install at 0x53DCA2/0x53DFA0 |
| R2-02 | `src/render/envmap.cpp:569` | `CClouds__RenderSkyPolys` wrapper (0x714650) |
| R2-02 | `docs/plans/2026-10-25-clouds-ffp-audit.md` | §1.5 texture-raster leak; §5.2 causes #1/#2; §6.2 premultiplied shader |
| R2-02 | `models/particle.txd` | `cloudmasked`/`cloudhigh` white RGB/alpha 0 |
| R2-03 | `src/core/main.cpp:1181-1513` | shadow state contract |
| R2-03 | `src/core/main.cpp:7542-7590` | shadow install; 0x53E0C3 mismatch |
| R2-04 | `src/render/vehiclePipe.cpp:1419-1670` | Xbox vehicle callback |
| R2-04 | `src/render/vehiclePipe.cpp:1620-1635` | geometry-flag textured branch with NULL paint texture |
| R2-04 | `src/render/vehiclePipe.cpp:1470-1600` | env/spec bits (`hasEnv1/hasEnv2/hasSpec`) |
| R2-04 | `src/skygfx.h:1259` | `pipeSetTexture` white fallback |
| R2-05 | `src/render/vehiclePipe.cpp:1455-1465` | world matrix upload; check per-atomic transforms |
| R2-05 | game dir `VehDeform.SA.asi`, `VehIK.asi` | external vehicle deformation/IK plugins; A/B first |
| R2-06 | `src/core/main.cpp:5003-5025` | water hook gate; fallback to original |
| R2-06 | `src/core/main.cpp:6125` | `waterParallaxEnable` parse; live value 0 |
| R2-06 | `src/core/main.cpp:6131` | `waterUnderwaterFog` parse; live value 1.0 |
| R2-06 | `src/render/postfx_underwater.cpp:314+` | underwater fog/SSS tick |
| R2-06 | `shaders/ps/Water_Underwater.hlsl:111-121` | SSS in-scatter term |
| R2-07 | `src/render/buildingPipe.cpp:765-1033` | `treeAlphaPS` / tree-alpha selection |
| R2-07 | game dir `modloader`, `models` | vegetation content; A/B |
| R2-08 | shadow/reflection path; low confidence | one-frame silhouette; reproduce with `pedShadows=0` |
| R2-09 | HUD/radar; no SkyGfx line | cosmetic/content |

### 10.5 Cloud-texture parse (content evidence)

`models/particle.txd` and the launcher DLC copy are byte-identical (34,036,264 B). The TXD texture-native structs for the three cloud textures:

| Name | Section offset | rasterFormat | d3dFormat | Size | Levels | Flags | First BGRA pixel |
|---|---|---|---|---|---|---|---|
| `cloudmasked` | `0x10b529c` (name at `0x10b52bc`) | 0x500 | **0x15 (A8R8G8B8)** | 1024×1024 | 1 | **0x1 (has alpha)** | `FF FF FF 00` |
| `cloudhigh` | `0x14b531c` (name at `0x14b533c`) | 0x500 | **0x15 (A8R8G8B8)** | 1024×1024 | 1 | **0x1 (has alpha)** | `FF FF FF 00` |
| `cloud1` | `0x18b539c` (name at `0x18b53bc`) | 0x600 | **0x16 (X8R8G8B8)** | 1024×1024 | 1 | 0x0 (no alpha) | `00 00 00 00` |

The first stored pixel is `FF FF FF 00` in BGRA order — **white RGB, alpha 0**. With additive ONE/ONE the alpha is ignored, so the white background is added. `cloud1` (black background, no alpha) is the texture that is correct for the additive path. This is the content half of R2-02; the blend half is the ONE/ONE cloud batch. Caveat: the header declares A8R8G8B8, but the section size (262 KB) is smaller than an uncompressed 4 MB mip; the file may store a compressed variant. The white/alpha-0 authoring is supported by the first bytes and by the visual result, but a full decode was not performed.

### 10.6 What was not proven (do not overclaim)

- **The exact path of the texture-raster leak into peds.** The cloud hook is missing a texture-raster restore (audit §1.5), and the peds are consistent with sampling a white/soft-alpha atlas, but no `GetTexture(0)` probe was captured at ped entry in this run. Fix 1 includes that probe; do not mark R2-01 closed from the code read alone.
- **The exact cloud sub-pass (sprite batch vs sky dome).** The content/blend mismatch is established; which draw emits the pink rectangles (CClouds sprite batches vs `CClouds::RenderSkyPolys`) still needs a draw-order capture. The audit’s §5.2 cause #2 (dome before the repair) is the leading candidate for the hard-edged sky quads.
- **Stored-shadow ownership of the black quads.** The hook is verifiably skipped, and the black is consistent with an un-healed blend state, but an A/B with the stored pass fixed is required to prove it.
- **The exact stale texture on the car roof.** The material/texture log was not captured; the grass pattern is strong visual evidence, but the specific stage/atlas is not proven.
- **Vehicle hover cause.** Hydraulics vs `VehDeform`/`VehIK` vs an atomic transform is unresolved; disable the external plugins first.
- **Giant silhouette cause.** One frame; likely a shadow/reflection projection, but no repro.
- **BASE-11/12/14/15.** Not observed in this run; not proven fixed.

### 10.7 Reproducibility

- Full per-image stats: `C:\Users\AAAAAA~1\AppData\Local\Temp\opencode\crops2200\stats.json` (22-image version; the 23rd was added manually).
- Full targeted patch list: `C:\Users\AAAAAA~1\AppData\Local\Temp\opencode\crops2200\patches.json`.
- Crops: `C:\Users\AAAAAA~1\AppData\Local\Temp\opencode\crops2200\` (`c2_cj.jpg`, `c3_peds.jpg`, `c17_cj.jpg`, `c9_car.jpg`, `c9_roof.jpg`, `c21_foliage.jpg`, `sheet1.jpg`, `sheet2.jpg`).
- The temp files are diagnostic artifacts; the evidence that matters is reproduced inline above so the report remains usable if the temp directory is cleared.

---

## 11. Bottom line

This run is **not a regression of the same build/mode** as 2026-10-24: it is an **Xbox-mode** run of a newer binary (2,360,832 / 21:56:44), while the previous registry was PBR-oriented. The big visual wins are real but partly **bypasses**: CJ is no longer absent (BASE-01), the full-frame water veil is gone because the custom water pipe is off (BASE-04 workaround), the vehicle dissolve/mirror-chrome is gone in Xbox mode (BASE-06), and the pastel washout is gone (BASE-16). The remaining top defects are **state/texture hygiene** (white ghost peds, pure-black stored-shadow quads, green-texture car roofs) plus **two content/blend failures** (opaque white-backed cloud textures under additive blending, and blown-white water in-scatter). Fixing the **texture raster/cache resync** is the single highest-value change because it can clear R2-01, the green ghost in R2-04, and parts of R2-03/R2-07/R2-08 at once; the **stored-shadow hook** and the **cloud premultiply** are the next most concrete, evidence-backed fixes.

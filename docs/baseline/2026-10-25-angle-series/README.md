# Camera-angle series — 2026-10-25 22:37:39 → 22:42:45

**User report (verbatim):** “basicaly at random angles screen goes balck in initeriors and outside the peds are onibly visible udner specific angle,s you can see how the camera is at diff angles refgering to cj”

**Report status:** evidence + triage only. No source file was modified.
**Tested binary:** `E:\games\gtasa_skygfx_plus\skygfx.asi`, 2,360,832 bytes, built **2026-10-25 21:56:44**, deployed 21:57:27.
**Source tree read for this report:** working tree at **2026-10-26 00:24** (newer than the tested binary — see §1.3).
**Images:** 23 GTA frames, all 1768×992, in `C:\Users\aaaaaaaaa\Documents\ShareX\Screenshots\2026-10\`.
**Run window:** 22:37:39.609 → 22:42:45.963 (file mtimes). The next captures (`vesktop_*` 22:49, `OpenCode_*` 22:51) are not game frames.
**Previous baseline:** [`docs/baseline/2026-10-25-screenshots-2200/`](../2026-10-25-screenshots-2200/README.md).

---

## 0. TL;DR — the four answers

### 0.1 What the angle series actually shows

The newest series is **not an interior test**. It is a third-person orbit around CJ at Ganton canal (01–07), a chase cam (08–11), first-person underwater (12–15), and an on-foot docks/beach sequence (16–22) plus a palm-lined LS road (23). The closest enclosed frame is **09** (under a bridge), and it is a dark *outdoor* underpass, not an interior cell. There is **no house/shop/interior frame in the 23**; the interior black cannot be confirmed or refuted from pixels here (§1.4, §11).

### 0.2 Peds — the angle correlation is real, but it is not the ped's own surface angle

On every frame where a CJ/ped is judgeable, the state splits **12/12** on one variable:

* **sky/cloud layer in frame** → ped is **white/grey, untextured, or translucent**;
* **no sky/cloud in frame** (camera aimed down) → ped is **fully textured**.

| Group | Shots | Sky/cloud proxy | Ped state |
|---|---|---|---|
| Sky/cloud visible | 01, 02, 03, 06, 07, 17, 19, 23 | 12.6–46.1% blue, or 59% pink cloud (19) | untextured / ghost / flat silhouette |
| Sky/cloud absent | 04, 05, 21, 22 | 0.0–0.2% blue | fully textured |

This is a **state/texture-bind leak from the sky/sprite pass**, not backface culling and not a grazing-angle lighting term. The strongest source-backed gap is that `CClouds__Render_hygiene` saves/restores filter, alpha test, fog, Z, blend and stage-0 ops but **not the texture raster**, so the device can hold the cloud/sun atlas while the RW cache still names the ped texture; the next ped texture bind then no-ops. Confidence **85%** (§7, §8). Shot 19 is the clean visual proof: a huge ped-shaped silhouette of **uniform beige with luma std 0.3 (per-channel ≤0.5)** — a ped sampling a flat sprite atlas, not a lit mannequin.

### 0.3 Black — it is a world-space opaque overlay, not a screen-space full-frame pass

The captured black regions are **world-space polygons**:

* shot 01: full-width dark mass from y=482, top edge **slopes +0.11 px/px** (y=490 at x=800 → y=593 at x=1700), with a hard step **61,56,53 → 6,8,5 in 10 px**; CJ's body is inside it, only the head is above;
* shot 02: same class, top edge slope **+0.17 px/px**, hard step **70,62,59 → 5,6,1**;
* shot 03: faceted, kinked top edge, hard step **88,77,71 → 0,4,3**;
* shot 06: lower-right quad, hard left step **56,45,41 → 2,2,2** at x≈476, sloped top edge, and it cuts a car in half.

None of these is a screen-aligned four-sided rectangle anchored to the raster. The shape is what a projected shadow/decal polygon looks like. The tested binary **does not hook `CShadows::RenderStoredShadows`** — the log has the call-site `MISMATCH` / `SKIPPED` line and **no** function-entry-hook line. That is the leading cause: the stored-shadow pass draws with drifted state (blend off / stale ALPHAREF), so the blob is opaque black instead of a soft darkening. Confidence **80%** (§6, §9 R3-01). Current source already contains an entry shield at `src/core/main.cpp:7673-7722`; the 21:56 binary predates it.

### 0.4 Body parts do not "vanish" by culling — a moving world-space edge crosses them

CJ's missing parts are exactly the parts **behind the moving black polygon**: 01 chest-down, 02 knees-down, 06 feet, 07 legs behind the car/shadow. In every case the same body part is visible in another frame at another camera position, and the ped silhouette itself is intact (no holes, no missing limb). That is mechanism **(ii) overlay geometry occluding at a certain depth**, not **(i) backface culling of single-sided geometry**. See §8 for probabilities.

### 0.5 Interior black — direct answer (details in §11)

* **Room position / world geometry: 65%.** The black that is actually measured here is anchored to world-space surfaces and shadows, and it moves across the body as the camera moves, not as a fixed screen layer.
* **Camera yaw/pitch: 25%.** Yaw/pitch changes *whether* the world-space occluder or an unlit face is between the lens and the scene. That looks like an angle correlation but is a visibility selection, not a screen-space shader.
* **Full-screen shader / stale postfx: 10%.** The source has a previous report with the opposite signature (“outdoors-only full-screen black, interiors perfect”, `src/core/main.cpp:4620-4645`); a stale fullscreen shader indoors would be a new, different failure.

If the interior black is a **full viewport**, the discriminating test is in §10: stand still, yaw 360°, then move 1 m and repeat. If it flips with yaw at a fixed position → view/normal/shader class. If it follows the room → position/geometry class.

---

## 1. Scope, count, and three corrections

### 1.1 Count and window

The newest GTA burst in `2026-10` is **23 files**, `gta_sa_cfU28ixIoG.jpg` (22:37:39) through `gta_sa_ZX7AJo83Io.jpg` (22:42:45). There is no newer GTA screenshot after 22:42:45 in the folder (the 22:49/22:51/22:55 files are Discord/OpenCode windows). All 23 are covered below. Older files (< 22:30) are not part of this window and are excluded.

### 1.2 Correction: the run is **PS2 for shots 01–17, Xbox for 18–23**, not “pure Xbox”

The previous baseline labels the whole run Xbox. The live log contradicts that:

```text
[22:35:22.701] [Mode] apply Xbox      pipes=2/1/2 cf=2
[22:37:04.730] [Mode] apply PS2       pipes=1/0/0 cf=1
[22:37:07.934] [Mode] apply Xbox      pipes=2/1/2 cf=2
[22:37:09.054] [Mode] apply Legacy PC pipes=2/1/1 cf=2
[22:37:10.179] [Mode] apply GTA IV    pipes=4/2/9 cf=7
[22:37:14.271] [Mode] apply GTA V     pipes=1/0/0 cf=0
[22:37:22.900] [Mode] apply Modern    pipes=0/3/10 cf=8
[22:37:26.634] [Mode] apply Custom    pipes=1/0/0 cf=0
[22:37:29.442] [Mode] apply PS2       pipes=1/0/0 cf=1
[22:41:42.749] [Mode] apply None      pipes=1/0/0 cf=0
[22:41:45.855] [Mode] apply Xbox      pipes=2/1/2 cf=2
```

The log tag confirms the pipe in use: `PS2Building: frame ...` runs through 22:41:45, then `XboxBuilding: frame ...` from 22:41:45 onward. Therefore:

| Shots | Time | Mode | Building / vehicle pipe |
|---|---|---|---|
| 01–17 | 22:37:39–22:41:20 | **PS2** | `BUILDING_PS2` / `CAR_PS2` |
| 18–23 | 22:41:53–22:42:45 | **Xbox** | `BUILDING_XBOX` / `CAR_XBOX` |

Consequences: the untextured-ped fault appears in **both** modes (01–03/06/07/17 PS2, 19/23 Xbox) → mode-independent. The opaque black quad is proven in **PS2 only** in this series; whether it is PS2-specific is untested here and is an A/B item (§10).

### 1.3 Correction: the source tree is newer than the tested binary

The pinpoint build stamp and the post-run edits:

| Item | Value |
|---|---|
| Log build receipt | `size=2360832 mtime=2026-10-25 21:57:27 built=Oct 25 2026 21:56:44` (`skygfx_dbg.log:27`) |
| `src/core/main.cpp` mtime | 2026-10-26 00:20 |
| `src/render/buildingPipe.cpp` mtime | 2026-10-26 00:24 |
| `src/render/pipelinecommon.cpp` mtime | 2026-10-26 00:11 |
| Working-tree delta vs HEAD | 6,693 insertions / 571 deletions across the 4 relevant source files |

Two specific consequences for this report:

1. **`CShadows::RenderStoredShadows` entry shield** exists in the current source (`src/core/main.cpp:7673-7722`) but **not** in the tested binary: the 22:35 log has the 0x53E0C3 call-site `MISMATCH ... SKIPPED` and **no** `entry hook installed` line. A rebuild is required before concluding R3-01 is fixed or unfixed.
2. **The cloud texture-raster gap** exists in both the tested binary and the current source: the save block at `src/core/main.cpp:4674-4682` and the restore block at `src/core/main.cpp:4809-4872` have no `rwRENDERSTATETEXTURERASTER` / `GetTexture(0)` entry. Rebuilding alone will not fix R3-02.

All file:line citations below are to the **current working tree**; treat them as the place to verify against the shipping binary.

### 1.4 There is no interior frame in the 23

The closest candidates:

* **09** — under a bridge/overpass, camera close to a parked car. This is outdoor geometry in deep shade, not an interior cell. The car cabin is the only enclosed volume: the rear window is an opaque grey slab (`[36,31,27]` at 450,430), so even the car interior is not a usable “room black” sample.
* **01–03, 06** — outdoor streets with the giant black decal/shadow.
* **12–15** — first-person underwater (CJ absent).
* **18–20** — beach looking up at the cloud layer.

So the interior claim is answered mechanistically (§11) and with a test plan (§10), not with interior pixels.

### 1.5 Measurement corrections vs the previous baseline

* The previous baseline’s shot-23 CJ coordinate is off: the head is at ~(840,565), not (900,480) (~110 px). Shot 02’s cited head point (895,485) is the **bright side** of the same head (163,158,152); the shadowed side at (865,485) is 112,105,99. Shot 17 is close. This report’s CJ boxes were re-picked from 2× grid crops (§2), and the R3-02 table notes both sides where the gradient is large.
* The previous baseline’s “pure Xbox” label is corrected in §1.2.
* The previous baseline’s “pure-black quad” for 01 is correct in shape but not uniformly pure: the mask is 51.5% below max-channel 24, with a strict <8 core of 8.3%. The hard step at the boundary is real; the interior is a dark plateau, not a mathematically uniform fill (JPEG).

---

## 2. Method

* Python 3.10 (32-bit), Pillow 12.2.0, NumPy 2.2.6.
* Every image opened at native 1768×992; no resampling for measurement.
* **5×5 median** RGB at each cited point (JPEG-ringing resistant).
* **Dark mask** = `max(R,G,B) < 24`; **strict black** = `max(R,G,B) < 8`. Largest connected components via a row-run union-find at full resolution. Bboxes are `[x0,y0,x1,y1]` inclusive.
* **Boundary shape:** extracted the per-column/per-row first dark pixel of the largest component, then fitted `y = m·x + b` per 442-px band. A screen-space rectangle has m≈0 and axis-aligned edges; a projected world quad has non-zero slope and/or kinks. Hard steps were confirmed on 10-px scanlines.
* **Sky/cloud proxy:** blue-sky mask `B>105 and B>R+18 and B>G+6` over the top 30% of the frame; pink-cloud mask from the previous baseline (`R>120, R>G+12, G>B+2, B>70`). The proxy measures “sky/cloud layer in frame”, not a cloud-pass trace.
* **CJ box stats:** median RGB plus per-channel std and mean saturation over a 40×40 box. Textured skin/cloth has saturation and multi-channel variance; a grey mannequin has low saturation and low variance; a ghost ped retains background variance through its alpha.
* Crops/grids used for visual verification are in `C:\Users\AAAAAA~1\AppData\Local\Temp\opencode\angles\` (scratch, not part of the repo).

---

## 3. Per-image table

CJ legend: **FULL** = whole body in frame; **PARTIAL** = some parts outside frame/occluded; **ABSENT** = no CJ in frame; **WHITE/GREY** = present but untextured; **GHOST** = untextured and semi-transparent; **TEXTURED** = skin/clothing texture present.

| # | File | mtime | Clock | Mode | Scene | Camera vs CJ | CJ state | Vehicle | Water / cloud | Angle-relevant note |
|---|---|---|---|---|---|---|---|---|---|---|
| 01 | `gta_sa_cfU28ixIoG.jpg` | 22:37:39 | 12:05 | PS2 | Ganton canal / road junction | behind, low (~1.5 m), pitch ≈ −12° | **PARTIAL / WHITE** — head only above black; body inside black | black car far bank, appears to float | flat blue canal; clear sky (22.6%) | world-space black edge crosses CJ at chest (y≈515 at x=600) |
| 02 | `gta_sa_1O58acZ43r.jpg` | 22:37:41 | 12:07 | PS2 | Ganton canal | right-side profile (~3 m), low, pitch ≈ −10° | **FULL / WHITE/GREY** — head 95,89,84; torso 102,96,90; legs in black 7,8,5 | same black car floating | flat canal; clear (20.8%) | black edge at knees (y≈585 at x=1000) |
| 03 | `gta_sa_sQr6TfbGla.jpg` | 22:37:42 | 12:08 | PS2 | Ganton canal | behind-left of car/group (~6 m), level | **ABSENT** (CJ not identifiable); **3 peds GHOST** | black car floating | flat canal; clear (19.5%) | ghost peds: 41,36,33 / 40,35,32 / 40,35,32, std 1.9–4.3 |
| 04 | `gta_sa_sQdkFD1CoX.jpg` | 22:37:46 | 12:12 | PS2 | Ganton street | overhead/top-down, pitch ≈ −65° | **FULL / TEXTURED** — green ragdoll 35,58,35 | black lowrider parked | none; clear (0.2%) | control: sky out of frame → texture present |
| 05 | `gta_sa_BxjrbhtP6S.jpg` | 22:37:50 | 12:16 | PS2 | Ganton residential | behind/above (~6 m), pitch ≈ −30° | **FULL / TEXTURED** — green bandana 19,32,15; white tank 67,59,59; jeans 42,35,31 | black lowrider airborne (hydraulics) | none; clear (0.1%) | control: high angle, no sky → texture present |
| 06 | `gta_sa_PQB6tqlTsZ.jpg` | 22:37:54 | 12:20 | PS2 | LS downtown / overpass | behind (~5 m), level, pitch ≈ −3° | **FULL / GHOST/WHITE** — head 67,59,54; torso 42,37,33 (std 1.8) | black cars on road | canal band; clear (46.1%) | black quad at right cuts a car; CJ feet at y>665 inside it |
| 07 | `gta_sa_8oROCcx6JC.jpg` | 22:38:01 | 12:27 | PS2 | Idlewood | left/front side (~4 m), low, pitch ≈ 0° | **PARTIAL / WHITE** — head 81,73,65; torso 101,97,90; legs behind car/shadow | black lowrider airborne, wheel/trim detached | flat canal; clear (16.6%) | legs occluded by car + black; torso still ghost |
| 08 | `gta_sa_hbQy9fqDOw.jpg` | 22:38:13 | 12:39 | PS2 | LS downtown / Market | chase behind player car | **ABSENT (in car)** | black car; spark fan | canal line; clear (22.5%) | n/a |
| 09 | `gta_sa_lwexiJ9aOy.jpg` | 22:38:28 | 12:54 | PS2 | under bridge / wall | close right-rear quarter of car, low | **ABSENT (in car)** | black car; green roof ghost; opaque rear window 36,31,27 | none; clear (0.1%) | closest thing to an “interior”: car cabin, not a room; no viewport black |
| 10 | `gta_sa_5fP153lT5h.jpg` | 22:38:37 | 13:03 | PS2 | El Corona | chase behind car | **ABSENT (in car)** | black car; spark fan | canal band; clear (42.1%) | n/a |
| 11 | `gta_sa_bEILIemJyp.jpg` | 22:38:47 | 13:19 | PS2 | Willowfield | chase behind car | **ABSENT (in car)** | black car in smoke | canal band; clear (15.6%) | n/a |
| 12 | `gta_sa_zbi6b5jTaB.jpg` | 22:39:51 | 14:17 | PS2 | Ocean Docks, surface | first person | **ABSENT** | none | rippled water; sun cone; no clouds | n/a |
| 13 | `gta_sa_rS8VtWPPM5.png` | 22:39:52 | 14:18 | PS2 | Ocean Docks, underwater | first person | **ABSENT** | none | water; white cone 189,217,217 | n/a |
| 14 | `gta_sa_hSMWNNxWVV.png` | 22:39:56 | 14:21 | PS2 | Ocean Docks, underwater | first person | **ABSENT** | none | pure-white cone [468,492,1404,992] | n/a |
| 15 | `gta_sa_lDha6vnq2J.png` | 22:40:21 | 14:47 | PS2 | Ocean Docks, at surface | first person | **ABSENT** | none | pure-white water band [0,584,1768,992] | n/a |
| 16 | `gta_sa_q2zyFkbrc9.png` | 22:41:10 | 15:36 | PS2 | Ocean Docks, dock path | level, no CJ | **ABSENT** | none | teal ocean; clear | n/a |
| 17 | `gta_sa_C9myMFzVUI.jpg` | 22:41:20 | 15:45 | PS2 | Ocean Docks, fence / beach | behind (~4 m), level, pitch ≈ −5° | **FULL / WHITE/GREY** — head 198,191,184; torso 77,73,71 (std 4.5); legs 57,53,50 | none | teal ocean; clear (17.2%) | soft shadow darkens body; no plain black quad |
| 18 | `gta_sa_0i8bISC8pO.png` | 22:41:53 | 16:18 | Xbox | Ocean Docks, beach | looking up +40° | **ABSENT** | none | pink clouds 69.0%; sunset | n/a |
| 19 | `gta_sa_Vx7i27JUFE.png` | 22:41:58 | 16:22 | Xbox | Ocean Docks, beach | looking up, ped <1 m from lens | **PARTIAL / FLAT SILHOUETTE** — torso 116,103,95 std 0.3; head cut by cloud | none | pink clouds 59.0% | ped-shaped beige silhouette with **zero texture variance** |
| 20 | `gta_sa_5aa1QDwnGI.png` | 22:42:01 | 16:25 | Xbox | Ocean Docks, beach | looking up +45° | **ABSENT** | none | pink clouds 83.4% | n/a |
| 21 | `gta_sa_4PGDUDzsUu.jpg` | 22:42:11 | 16:34 | Xbox | Ocean Docks, beach | behind/above (~6 m), pitch ≈ −35° | **FULL / TEXTURED** — green bandana, white tank, jeans, skin | none | none; clear (0.0%) | control: sky out of frame → texture present |
| 22 | `gta_sa_v8mTbb1pOB.jpg` | 22:42:34 | 16:57 | Xbox | Ocean Docks, beach | front/above (~5 m), pitch ≈ −30° | **FULL / TEXTURED** — green bandana, white tank, jeans | none | sea edge; no sky (0.1%) | control: sky out of frame → texture present; nearby peds textured |
| 23 | `gta_sa_ZX7AJo83Io.jpg` | 22:42:45 | 17:08 | Xbox | LS palm-lined road | behind (~5 m), pitch ≈ −10° | **FULL / WHITE/GREY + TRANSLUCENT** — head 61,57,54; torso 72,68,65 (std 25.4 = road through body); leg 70,66,63 | none | none; clear (12.6%) | body visible but grey; background variance through torso |

**Clock source:** HUD clock (top-right), read at native resolution; ±1 minute.

---

## 4. The angle series: what the camera is actually doing

The 23 frames are three different rigs, not one orbit:

* **01–07 — third-person orbit around CJ** at Ganton canal/Idlewood. The camera moves from behind-low (01) to right-profile (02) to a group shot (03), then jumps to top-down (04), back to high-behind (05), level-behind (06), and front-left-low (07). This is the actual “camera at different angles referring to CJ” series.
* **08–11 — chase cam behind the player car.** CJ is in the cabin; no body visibility data.
* **12–15 — first-person underwater.** CJ is absent by design.
* **16–22 — on foot at Ocean Docks.** High-behind (21), high-front (22); 18–20 are sky-only. **23** is a palm-lined LS road (Ganton/Idlewood), not the docks.
* **09** is the only enclosed-looking view (under-bridge). It is not an interior and its dark areas are legitimate shade + a black car body, not the flat `[2,2,2]` overlay class.

The orbit has **no controlled yaw/pitch telemetry**. The angle classes below are visual estimates from the horizon height and the CJ facing direction; treat them as ±10°.

| Shot | Rig | Relative to CJ | Estimated pitch | CJ facing |
|---|---|---|---|---|
| 01 | orbit | behind, low | −12° | away (toward canal) |
| 02 | orbit | right side, low | −10° | left |
| 03 | orbit/group | behind-left of car | −5° | n/a (CJ out of frame) |
| 04 | orbit | overhead | −65° | down/ragdoll |
| 05 | orbit | behind/above | −30° | away/right |
| 06 | orbit | behind | −3° | away |
| 07 | orbit | left/front side, low | 0° | toward camera/under car |
| 17 | on foot | behind | −5° | away |
| 19 | on foot | under lens | +35° (looking up) | n/a |
| 21 | on foot | behind/above | −35° | away |
| 22 | on foot | front/above | −30° | toward camera |
| 23 | on foot | behind | −10° | away |

The key axis is not “azimuth around CJ”; it is **whether the sky/cloud layer is inside the frustum**. Shots 04/05/21/22 all aim down and exclude the sky; shots 01/02/03/06/07/17/23 include the sky at 12.6–46.1%; shot 19 is filled with the cloud layer.

---

## 5. Angle → visibility correlation table

This is the table the report exists for. “Sky/cloud” is the measured proxy from §2; “Mech” is the mechanism that fits the evidence, not a guess from the angle alone. Confidence is for the mechanism, not for the visual description.

| Shot | Camera vs CJ (pitch/azimuth) | Sky/cloud in frame | CJ / ped state | Body parts visible | Parts that vanish / are occluded | Mech | Conf |
|---|---|---|---|---|---|---|---|
| 01 | behind, low, −12° | 22.6% blue | PARTIAL, WHITE | head (75,67,61) | torso, arms, legs — behind world-space black edge at chest | (ii) overlay | 85% |
| 02 | right side, low, −10° | 20.8% blue | FULL, WHITE/GREY | head, torso, arms, thighs | lower legs/feet — black below y≈585 | (ii) overlay | 85% |
| 03 | behind-left of group, −5° | 19.5% blue | CJ ABSENT; 3 peds GHOST | all 3 peds' full silhouettes | texture/opacity (whole body) | state/texture + (iv) | 80% |
| 04 | overhead, −65° | 0.2% blue | FULL, TEXTURED | all | none | control | 95% |
| 05 | behind/above, −30° | 0.1% blue | FULL, TEXTURED | all | none | control | 95% |
| 06 | behind, −3° | 46.1% blue | FULL, GHOST/WHITE | head, torso, arms | feet/lower shins below y≈665 (black quad) | state/texture + (ii) | 85% |
| 07 | left/front side, low, 0° | 16.6% blue | PARTIAL, WHITE | head, torso, one arm | legs — behind car + black shadow | (ii) overlay + state | 85% |
| 17 | behind, −5° | 17.2% blue | FULL, WHITE/GREY | all | none (body darkened by soft shadow, not absent) | state/texture | 85% |
| 19 | under lens, looking up +35° | 59% pink cloud | PARTIAL, FLAT SILHOUETTE | torso, arms | head — cloud quad in front; whole body untextured/flat | state/texture + (ii) | 80% |
| 21 | behind/above, −35° | 0.0% blue | FULL, TEXTURED | all | none | control | 95% |
| 22 | front/above, −30° | 0.1% blue | FULL, TEXTURED | all | none | control | 95% |
| 23 | behind, −10° | 12.6% blue | FULL, WHITE/GREY + translucent | all (silhouette intact) | none; texture gone, road visible through torso | state/texture + (iv) | 85% |

**Body-part verdict.** There is **no limb-specific, one-sided disappearance pattern**. The same arm/torso/leg is present in 05/21/22 and present-but-grey in 02/23. The only hard “missing” parts are the ones behind the black polygon (01, 02, 06, 07) or behind the car/cloud (07, 19). A single-sided geometry cull would produce *holes in the silhouette* (one side of the torso, the inside of a leg) that change with azimuth, not a whole-body colour/opacity swap. That is not what the frames show.

**Ped-texture verdict (12/12).** Every judgeable CJ/ped in the series follows the sky/cloud split in §0.2. The four high-angle/no-sky controls (04, 05, 21, 22) are fully textured; the eight sky/cloud frames are white, grey, ghost or flat. This is the strongest correlation in the data set and it is the one that explains “only visible at specific angles”.

---

## 6. Black regions: bbox, boundary shape, inside/outside medians

### 6.1 Measurements

| Shot | Dark mask (max<24) | Largest dark component | Strict-black core | Inside 5×5 median | Outside 5×5 median | Boundary shape |
|---|---|---|---|---|---|---|
| 01 | 51.5% | 716,405 px (40.8%), bbox `[0,482,1767,991]`, touches L/R/B | 8.3% (<8) | (600,700) = **3,8,4** | (600,490) = **60,56,57** | world-space: sloped top edge +0.11 px/px from x=800→1700 (y 490→593); hard step 61,56,53 → 6,8,5 between y=510 and 520 at x=600; faceted kinks |
| 02 | 47.1% | 608,666 px (34.7%), bbox `[0,584,1767,991]`, touches L/R/B | 11.4% | (1000,690) = **6,8,5** | (1000,580) = **70,62,59** | world-space: slope +0.17 px/px at x=1300→1700 (626→693); hard step 70,62,59 → 5,6,1 between y=580 and 590 |
| 03 | 40.9% | 447,357 px (25.5%), bbox `[0,356,1233,991]`, touches L/B | 20.7% | (300,700) = **4,6,3** | (600,450) = **77,73,70** | world-space: faceted/kinked top edge; hard step 88,77,71 → 0,4,3 between y=540 and 550 at x=600 |
| 06 | 41.7% | 647,632 px (36.9%), bbox `[0,374,1767,991]` | 28.3%; strict core `[476,468,1767,991]` | (1200,800) = **2,2,2** (std 0) | (470,700) = **56,45,41**; (1200,400) = **40,33,25** | world-space: hard left step 56,45,41 → 2,2,2 at x≈476 (10 px); sloped top edge; quad cuts a car; only the left edge is near-vertical, the rest is not screen-aligned |
| 09 | 39.9% | 503,359 px (28.7%), bbox `[12,400,1526,991]` | distributed, not a plateau | car body (500,600) = **7,5,6**; rear window (450,430) = **36,31,27**; wall (200,300) = **26,23,18** | road (900,700) = **2,2,2** | under-bridge shade + black car; no single projected quad covering the screen; not the R3-01 class |
| 23 | 12.3% | 48,492 px (2.8%), bbox `[83,0,621,324]` | — | canopy (300,150) = **3,3,2** | road (700,800) = **34,30,29** | tree canopy at frame edge; not a ground overlay |

### 6.2 Shape verdict: world-space, not a screen-space rect

A scissor/viewport or fullscreen postfx leak produces an **axis-aligned rectangle anchored to the raster** with constant x/y edges and a uniform fill. The measured shapes are not that:

* 01/02/03 top edges are **sloped and kinked**; the slope changes with the scene, not with the resolution.
* 06’s left edge is near-vertical at x≈476, but the top edge slopes and the region is not anchored to the top/left of the raster; it cuts through a car at world depth. A screen-space rect would have all four edges axis-aligned; this has one.
* The transition at the edge is a **hard 10-px step** from lit road (50–70) to `[2,2,2]`, with no soft alpha gradient. That is an opaque polygon / failed alpha blend, not a stochastic post-process.

**Verdict:** the black is an opaque **world-space overlay** (shadow/decal quad or an unlit black surface) at a depth in front of CJ. Mechanism **(ii)**.

### 6.3 What it does to CJ

* **01:** the edge crosses CJ at chest height. Head above (75,67,61), torso/arms/legs below are swallowed (10,8,9 at 810,520). The body is still there, just behind the quad.
* **02:** the edge crosses at the knees. Head/torso/arms above are white/grey; legs below are `[7,8,5]` and invisible against the quad.
* **06:** CJ is at x≈790; the quad’s top at that column is y≈657. Head/torso above are ghost-white; feet below are inside the black.
* **07:** the car itself plus the quad hide the legs; the head/torso remain ghost.
* **17:** this is a **soft** shadow (sand/road still visible), not the opaque quad class: head bright (198,191,184), torso 77,73,71, legs 57,53,50, with the background still readable. Do not lump 17 into R3-01.
* **23:** no quad; the ped is grey because of the texture/alpha state, and the road is visible through the torso (std 25.4).

---

## 7. The ped texture loss: the sky-visibility correlation

### 7.1 The 12/12 split

| Shot | Sky/cloud proxy | CJ/ped state | Textured? |
|---|---|---|---|
| 01 | 22.6% blue | PARTIAL, white head | no |
| 02 | 20.8% blue | FULL, white/grey | no |
| 03 | 19.5% blue | 3 peds ghost | no |
| 06 | 46.1% blue | FULL, ghost | no |
| 07 | 16.6% blue | PARTIAL, white | no |
| 17 | 17.2% blue | FULL, white/grey | no |
| 19 | 59.0% pink cloud | flat silhouette | no |
| 23 | 12.6% blue | FULL, grey/translucent | no |
| 04 | 0.2% blue | FULL | **yes** |
| 05 | 0.1% blue | FULL | **yes** |
| 21 | 0.0% blue | FULL | **yes** |
| 22 | 0.1% blue | FULL | **yes** |

No counterexample in the series. Shot 08/10/11 are in-car (CJ not visible) and are excluded; they have 15–42% sky but no ped to judge. Shot 19 is the only “clouds fill the frame” case and its ped is the **most** clearly wrong (flat beige, std 0.3), reinforcing the link.

### 7.2 Why this is a texture bind, not lighting or geometry

* The ped **geometry is intact**: head, arms, legs, clothing silhouette and animation all present. Only the diffuse detail is gone.
* The state is **whole-model**, not per-face or per-limb. A grazing N·V term would shade the chest (facing the camera) differently from the silhouette edge; instead the entire model, including the camera-facing chest, is uniform.
* The ghost peds in 03 have **per-pixel std 1.9–4.3** at 40×40 — essentially flat RGB. A lit untextured mannequin would still show shading variation from the normals; a constant sample from a sprite atlas does not.
* Shot 19’s silhouette is the smoking gun: **116,103,95 and 117,103,95 at two different body points, luma std 0.3 (per-channel ≤0.5)**. That is not a lit human; that is a constant texel from a flat sprite atlas.
* Shot 23’s torso has **std 25.4 with the road texture variance**, i.e. the ped is translucent and the background shows through. That is an alpha channel from the wrong source (the atlas alpha), not a lighting term.

### 7.3 Source gap

`CClouds__Render_hygiene` (`src/core/main.cpp:4606`) saves and restores:

* `rwRENDERSTATETEXTUREFILTER` (4679), alpha test func/ref (4680–4681), fog (4682);
* sampler min/mag/mip (4690–4692), raw `D3DRS_FOGENABLE`, ZENABLE, ZFUNC (4693–4695);
* ZWRITE / VERTEXALPHA / SRCBLEND / DESTBLEND (4710–4728);
* stage-0 `D3DTSS_COLOROP` / `ALPHAOP` (4730–4737, restored 4869–4870).

It does **not** capture or restore `rwRENDERSTATETEXTURERASTER` (state 1), nor the raw device `GetTexture(0)` / `SetTexture(0)`. The comment at 4700–4705 explicitly says the cloud pass “sets stage-0 COLOROP/ALPHAOP for its sprite texture; vanilla restores none of it” — the ops were added to the restore list, the **raster was not**.

The ped/default callback (`myDefaultCallback`, `src/core/main.cpp:1585`) does a shader unbind (`defaultCbShaderHygiene`, 1833–1860), a cull/alpha/Z contract, the opaque contract and the dual pass — but **no texture-raster resync** and **no stage-0 resync**. So the divergence can survive into the ped draw: RW’s texture cache believes the ped texture is bound, the device holds the cloud/sun atlas, and `RwD3D9SetTexture(pedTex, 0)` no-ops. The result is the cloud atlas RGB (white/grey) and its alpha (ghost), which is exactly what 02/03/06/07/17/19/23 show.

The correct pattern already exists: `pipeSetTexture()` (`src/skygfx.h:1272`) resolves NULL to `gpWhiteTexture` and forces a real transition; the building pipe uses it at `src/render/buildingPipe.cpp:1173/1442/1480/1991`, and the current vehicle route uses it at `src/render/vehiclePipe.cpp:1660/1672`. The ped path does not.

**Caveat:** the exact atlas is not proven. The same class of leak could come from the sun/corona/sprite flush (`CoronaBlend`, `[SKY]`), which also binds a texture. The fix should be atlas-agnostic: force a real texture transition at the default/ped callback entry and save/restore the raster around the sky/cloud hook.

**Probe before coding (this decides whether the cloud hook is even the right target).** At `myDefaultCallback` entry when `isPedPipe`, log three values for the first 5 ped atomics per frame: `d3d9device->GetTexture(0)` (raw L3), `rwRENDERSTATETEXTURERASTER` (RW cache L1), and the current material's `texture` pointer. If **device != cache** at entry, the raster leak is proven and the cloud-hook restore is the fix. If **device == cache == NULL**, the ped material itself is untextured (streaming / TXD / content), and the sky correlation is a proxy for ped distance/LOD, not for the cloud bind. The 12/12 split makes the first case much more likely, but the probe is the only way to be sure.

---

## 8. Mechanism judgement: (i) vs (ii) vs (iii) vs (iv)

| Case | Evidence | (i) backface cull | (ii) overlay/depth | (iii) grazing shader | (iv) alpha test/fade | Best fit |
|---|---|---|---|---|---|---|
| **A. CJ body swallowed by black** (01, 02, 06, 07) | straight world-space edge moves across the body; same part visible from other angles; silhouette intact | **5%** — a cull would remove one side/hole, not a clean horizontal cut across the whole body at a world-space line | **85%** — opaque projected polygon in front; hard 10-px RGB step | **5%** — black is not view-dependent per surface; it is a polygon edge | **5%** — alpha=1, not faded | **(ii)** |
| **B. Whole-body white/grey** (02, 06, 17, 23) | whole model uniform; sky/cloud 12/12 split; flat std; textures return when sky out of frame | **2%** — no missing faces | **10%** — a wrong texture is a kind of bind/overlay, but not geometry | **3%** — angle is a proxy for sky visibility, not surface N·V | **85%** — a stale texture raster from the sky/sprite pass is a bind/alpha state | **state/texture (X), closest to (iv)** |
| **C. Ghost/translucent peds** (03, 23) | background visible through torso; std = background; flat RGB where not transparent | **0%** | **5%** | **5%** | **90%** — the alpha comes from the wrong atlas; blending is on | **(iv) with (X) source** |
| **D. Giant flat silhouette** (19) | ped-shaped, flat 116,103,95 std 0.3, near lens, clouds fill frame | **0%** | **85%** — wrong texture/overlay; geometry correct | **5%** | **10%** | **(ii)/(X)** |
| **E. Interior full-viewport black** (reported, not captured) | no interior frame; source has an interior-ambient fix and a previous opposite-signature shader fix | **5%** | **55%** — opaque quad/surface in front | **30%** — interior ambient/normal crush | **10%** | **(ii), then (iii)** |

Case B is the important one for the user’s “peds only visible under specific angles”: **none of (i)–(iv) is a perfect fit** because the root is a state/texture bind, which the four-way list does not include. I label it **(X)** and map it to the closest listed class, **(iv)**, for the alpha/ghost half. If forced to choose only among the four for the *white* body, (iv) is the least wrong; for the *missing* body, (ii) is correct.

Why (iii) is rejected as the primary for the ped texture: a grazing-angle/Fresnel term would be **per-pixel and surface-dependent**; the camera-facing chest would retain texture and the rim would brighten. The frames show the opposite: the entire model, chest included, is uniform, and the controlling variable is whether the sky is in the frustum, not the normal-to-view angle. The 12/12 split is a **render-order/state correlation**, not a BRDF correlation.

Why (i) is rejected: the missing parts are not a consistent set of backfaces. In 01 the cut is at the chest; in 02 at the knees; in 06 at the feet; in 07 the legs are behind the car. The cut line follows the black polygon, not the body's surface orientation. The ped silhouette itself is closed.

---

## 9. Ranked issues

### R3-01 — Opaque black shadow/decal quad (Sev 3, 80%)

**Visual.** A hard-edged, flat `[2,2,2]` polygon covers 26–41% of the frame and swallows the road/CJ/car. Boundary is a world-space projected edge with a 10-px hard step; not a screen rect.

**Evidence.**

| Shot | bbox | Area | Inside | Outside | Boundary |
|---|---|---|---|---|---|
| 01 | `[0,482,1767,991]` | 716,405 px / 40.8% | (600,700) = **3,8,4** | (600,490) = **60,56,57** | slope +0.11 px/px; step 61→6 |
| 02 | `[0,584,1767,991]` | 608,666 px / 34.7% | (1000,690) = **6,8,5** | (1000,580) = **70,62,59** | slope +0.17 px/px; step 70→5 |
| 03 | `[0,356,1233,991]` | 447,357 px / 25.5% | (300,700) = **4,6,3** | (600,450) = **77,73,70** | kinked facets; step 88→0 |
| 06 | strict `[476,468,1767,991]` | 493,312 px / 28.3% (strict) | (1200,800) = **2,2,2** std 0 | (470,700) = **56,45,41** | hard vertical step at x≈476; sloped top; cuts a car |

**Suspected subsystem.** The game’s stored-shadow pass. The current source has a function-entry shield for `CShadows::RenderStoredShadows` at `src/core/main.cpp:7673-7722`, and the contract/comment naming this exact symptom at `src/core/main.cpp:1203-1215` (“stale ALPHAREF (140/1), a stale DESTBLEND/ZTEST or blend OFF — hard black blobs”). The **tested binary does not have the entry shield**: the log shows

```text
[shadow_install] 0x53E0C3 op=E8 orig=68E8DD00 MISMATCH zeroarg=1 site=CShadows::RenderStoredShadows (expect op=E8 orig=00708300)
[shadow_install] 0x53E0C3 SKIPPED — vanilla path kept
```

and there is no `entry hook installed` line anywhere in the 6 MB log. The two call-site hooks that do install (`RenderStaticShadows`, `RenderExtraPlayerShadows`) are not enough for the stored blobs.

**Alternative mechanism (15%).** If the black is not a stored shadow, it is a **tagged PS2 building decal** drawn through `TagRenderCB` (`src/render/buildingPipe.cpp:322-340`), which forces `SRCBLEND=ONE` / `DSTBLEND=ZERO` for `BUILDING_PS2` — an opaque replace. The same file documents the alpha-cutout road-decal failure at `src/render/buildingPipe.cpp:505-522` (“the cutout region’s RGB=0 lands unmodulated: a solid black pool … in EVERY building pipe”). The 06 hard left edge and the 01 faceted triangles are consistent with a large decal quad; the 80% stored-shadow lead is stronger because the log proves the pass is unshielded.

**A/B / fix order.** (1) Rebuild with the entry shield and repeat the 01/02/06 sweep. (2) If the black persists, test `TagRenderCB`’s PS2 opaque pair and the decal alpha cut. Do **not** start by changing `fixShadows` (it is 0 and controls offsets, not the state).

---

### R3-02 — Untextured/ghost peds from a sky/sprite texture-raster leak (Sev 3, 85%)

**Visual.** CJ and nearby peds lose diffuse texture (white/grey mannequin) and sometimes render semi-transparent (road visible through the torso). The state flips with whether the sky/cloud layer is in frame; 12/12.

**Evidence.**

| Shot | Region | 5×5 median | 40×40 std / saturation | Note |
|---|---|---|---|---|
| 02 | CJ head (865,485) | **95,89,84** (bright side 163,158,152 at 895,485) | 23.4 / 11.7 | no face, no skin; shading gradient, no texture |
| 02 | CJ torso (870,555) | **102,96,90** | 15.3 / 11.5 | no shirt pattern |
| 03 | ped torso (1450,555) | **41,36,33** | 1.9 / 7.8 | flat; ghost |
| 06 | CJ torso (792,610) | **42,37,33** | 1.8 / 9.0 | flat |
| 17 | CJ head (875,455) | **198,191,184** | 42.8 / 18.4 | bright, untextured |
| 17 | CJ torso (873,540) | **77,73,71** | 4.5 / 5.7 | flat |
| 19 | silhouette (870,780) | **116,103,95** | **0.3** / 21.4 | constant texel |
| 23 | CJ torso (835,612) | **72,68,65** | 25.4 / 6.7 | road visible through body |
| 05 | CJ head (880,480) | **19,32,15** | 11.7 / 14.3 | **textured control** (green bandana) |
| 04 | ragdoll (1210,870) | **35,58,35** | 8.6 / 22.7 | **textured control** |

**Suspected subsystem.** `CClouds__Render_hygiene` (`src/core/main.cpp:4606`) does not save/restore `rwRENDERSTATETEXTURERASTER` or the raw device texture. The save list is `src/core/main.cpp:4674-4682`; the restore list is `src/core/main.cpp:4809-4872`; both stop at texture filter/sampler/stage ops. The ped/default callback (`src/core/main.cpp:1585`, shader hygiene `1833-1860`) does not resync the texture bind either. The correct forced-transition helper exists (`pipeSetTexture`, `src/skygfx.h:1272`) and is used by the building route (`src/render/buildingPipe.cpp:1173/1442/1480/1991`) and the current vehicle route (`src/render/vehiclePipe.cpp:1660/1672`), not by the ped route.

**Confidence.** 85% for the texture-raster/state class. The 12/12 sky split, the flat 0.3 std in 19 and the 4/5/21/22 controls make a lighting/shader explanation very unlikely. The remaining 15% is the exact atlas (cloud vs corona/sun sprite) and the exact layer that diverges (RW cache vs raw device).

**Fix candidates.** (a) In `CClouds__Render_hygiene`, capture `rwRENDERSTATETEXTURERASTER` + `dev->GetTexture(0)` and restore both through all layers. (b) At `myDefaultCallback` entry (ped branch), force a real texture transition: `RwD3D9SetTexture(NULL,0); RwD3D9SetTexture(material->texture,0)` or the `pipeSetTexture` pattern. (c) Add the same stage-0 `COLOROP/ALPHAOP` resync. Do not rely on `cloudRenderFix=0`; the hook is not the binder.

---

### R3-03 — Interior black at random angles (Sev 3, untested by pixels; 65% world-space, 45% exact cause)

**Visual (reported, not in this series).** The user reports the screen goes black in interiors at random camera angles. No interior frame exists in the newest 23; the closest enclosed view is 09 (under a bridge), which is dark but not a full-viewport black and not a room.

**Suspected subsystems, ranked.** (1) **Opaque world-space quad/surface** — the same stored-shadow/decal class as R3-01; in a tight room a large shadow quad or an unlit wall can fill the viewport, and yaw changes whether it is in front. (2) **Interior ambient crush** — `ClampAmbientToSky` (`src/render/pipelinecommon.cpp:81-95`) has an explicit interior early-return to prevent “view/light-dependent black patches”; if the tested binary lacks that hunk or the interior flag (`CGame__currArea`) is not set for the room, the outdoor night law can crush interior ambient to black. The IBL capture is also suppressed indoors (`src/render/pipelinecommon.cpp:1049-1080`), so an interior shell has no sky IBL; with a low `ambientFloor` and no direct sun it can go black face-by-face. (3) **Stale fullscreen shader** — the previous opposite-signature report (“outdoors-only full-screen black, interiors perfect”, `src/core/main.cpp:4620-4645`) was fixed by unbinding PS/VS at sky entry; a new stale-shader path indoors is possible but not evidenced here.

**Why 65% world-space.** The only black measured in this series has world-space sloped/faceted edges and hard steps; nothing resembles a screen-space full-frame pass. The user’s “random angles” is exactly how a world-space quad behaves when the camera sweeps it across the body/viewport.

**Why 45% exact cause (not 65%).** There is no interior pixel evidence. The ambient-crush path is real in the source and is explicitly described as view/light-dependent; it could produce the reported symptom without any quad. The A/B in §10 separates them in one session.

---

### R3-04 — Black/translucent vehicles (Sev 2, 70%, out of scope but visible)

The player lowrider is flat black `[2,2,2]` in 04/05/07–11 (07 dark-mask bbox `[234,380,1345,850]`, 5×5 body sample (500,600) = **2,2,2**), with a green grass-texture ghost on the roof in 09 and an opaque grey rear window. The **current source already contains the NULL-paint-texture fix** at `src/render/vehiclePipe.cpp:1660/1672` (`pipeSetTexture(material->texture, 0)` / `pipeSetTexture(NULL, 0)` instead of a cache-no-op `RwD3D9SetTexture(NULL, 0)`); the 21:56 binary may predate it. Green ghost on the roof is the symptom to re-test after a rebuild. This is not the ped bug; it is listed so the black car in 01–07 is not mistaken for the ped-texture fault. The previous baseline’s R2-04 covers the history.

---

### R3-05 — Mode attribution / binary drift (Sev 2, 100% fact, documentation)

The previous baseline labels the run “pure Xbox”; the log proves PS2 for 01–17 and Xbox for 18–23 (§1.2). The tested binary is 21:56:44; the source is 00:24 next day. Any conclusion that a source-level fix is “already in” must be checked against the binary, not the tree. This is not a renderer bug, but it changes the interpretation of every previous A/B.

---

### R3-06 — Sunset cloud quads / white water (Sev 2, 90%, out of scope)

Shots 18–20 show opaque pink cloud quads covering 59–83% of the frame; shots 14–15 show a pure-white `[255,255,255]` water cone/band. These are the known additive-cloud and underwater in-scatter issues, not the angle bug. Mentioned only so the bright/pink frames are not confused with the ped/black evidence.

---

## 10. What to A/B in game — exact INI keys

All keys go under `[SkyGfx]` unless a section is named. Values are the ones to set, not the current defaults. Change one at a time, keep the same camera position/time/weather, and capture the same 01/02/06/17/23-style angles. **P0:** before any key, add the three-value texture probe from §7.3; it is cheaper than a blind A/B and it falsifies or confirms the texture-raster lead in one run.

**Mode-owned keys — read this before testing.** `buildingPipe`, `vehiclePipe`, `colorFilterEnable`, `dualPassPed`, `pedOpaqueContract`, `pedShadows` and `stencilShadows` are written by `ApplyMode()` from the `[Modes] active` profile on every apply (`src/core/modeManager.cpp:98-99,146,166,172-173,180`). Editing them in the INI and reloading will be silently overwritten (the shipped `dualPassPed=1` is already forced to 0 by the PS2 profile). For these keys, use the **debug menu** (which writes the live Config after the mode apply) or change `[Modes] active`; verify the live value in the log before trusting the result. `ambientFloor`, `pbrAmbientFloor`, `ambientMult` and `tonemapBlackLift` are not mode-owned.

### 10.1 Ped texture / ghost

| # | Key | Value | What it isolates | Expected if confirmed |
|---|---|---|---|---|
| P1 | `disableClouds` | `1` | removes the cloud/sky sprite draw entirely | peds become textured at **all** angles; if yes, the sky/sprite pass is the binder (R3-02) |
| P2 | `dualPassPed` | `0` | disables the ped dual pass | ghost/alpha fringe changes → the dual/alpha leg is involved |
| P3 | `pedOpaqueContract` | `0` | disables the ped state contract | if the ghost appears/disappears, the contract is masking or causing the state |
| P4 | `pedAlphaTestEnable` | `0` (add key) | turns the ped alpha test off | white body persists → not the alpha test; ghost changes → alpha-test leg |
| P5 | `pedCullMode` | `2` / `3` / `1` (add key) | forces CULLBACK / CULLFRONT / CULLNONE | if **CULLNONE** restores the body, a single-sided cull is involved (unlikely per §8); if nothing changes, culling is exonerated |
| P6 | `stateBaselineEnable` | `0` | disables the state baseline repair | if the ghost gets worse/better, the baseline contract is in the path |
| P7 | `depthHookEnable` | `0` | disables the raw depth hook | if the ghost changes, raw depth drift is in the path (should not affect texture) |

`pedAlphaTestEnable` and `pedCullMode` are parsed by the code (`src/core/main.cpp:6006-6012`, `1744-1756`) but are **not present in the shipped 22:35 INI**; add them manually under `[SkyGfx]`.

**Do not use `cloudRenderFix=0` as the ped test.** The hook is not the texture binder; setting it to 0 only skips the hygiene, and the cloud pass still binds its atlas. `disableClouds=1` is the clean switch.

### 10.2 Black quads / shadow blobs

| # | Key | Value | What it isolates | Expected if confirmed |
|---|---|---|---|---|
| B1 | `pedShadows` | `0` | disables **ped** shadow blobs only | if the black under/over CJ disappears → ped stored-shadow; if the big 06 quad under a car remains → vehicle/building stored-shadow, still R3-01 |
| B2 | `stencilShadows` | `0` | disables stencil shadows | if the big road/building shadow disappears → shadow-volume class |
| B3 | `fixShadows` | `1` | shadow offset fix (not state) | should **not** remove a hard black quad; if it does, the offset is placing the quad in the camera |
| B4 | `[Modes] active` | `PS2` vs `Xbox` | pipe switch at the same spot | if the black is PS2-only, the PS2 building/tag path (`buildingPipe.cpp:322-340`) is the lead |
| B5 | `buildingPipe` | `PC` / `PS2` / `Xbox` | building callback | if `PC` removes the black, the custom building/tag path is involved |
| B6 | rebuilt binary | include `main.cpp:7673-7722` | stored-shadow entry shield | this is the **first** test: the 21:56 binary lacks it |

For B1/B2, note these keys are `-1` (vanilla) in the shipped INI. Setting them to 0 is a *disable*, not a fix; use them as diagnostics.

### 10.3 Interior black

| # | Key | Value | What it isolates | Expected if confirmed |
|---|---|---|---|---|
| I1 | `ambientFloor` | `0.10` (from 0.03) | outdoor ambient floor | if the interior lifts → ambient crush (R3-03 cause 2) |
| I2 | `pbrAmbientFloor` | `0.15` (from 0.05) | PBR ambient floor | same, PBR path only |
| I3 | `ambientMult` | `1.5` | overall ambient multiplier | a quick “is it just too dark?” test |
| I4 | `tonemapBlackLift` | `0.05` (from 0.0) | black-level lift in the colour filter | if the interior lifts, the tonemap/colour-filter toe is crushing |
| I5 | `colorFilterEnable` | `0` | bypasses grading/tonemap | if the interior lifts, the postfx grade is the cause |
| I6 | `[Modes] active` | `PC` | vanilla-ish pipe | if `PC` fixes the interior, the custom building/PS2 path is the cause |

**The decisive in-game test (no INI change):** stand in the interior, do **not** move, and yaw 360° at constant pitch; then pitch ±60° at constant yaw; then walk 1 m and repeat the yaw. Record which of the three changes the black. This separates:

* **yaw-only at a fixed position** → view/normal/shader class (R3-03 cause 2/3);
* **position-only** (black follows the room, not the yaw) → world-space geometry/occluder (R3-03 cause 1);
* **both** → a world-space occluder that is only in the frustum at some yaws (still cause 1, with a yaw modulation).

### 10.4 One-session capture recipe

1. Rebuild (so `main.cpp:7673-7722` is in the binary), deploy, and verify the log line `entry hook installed` for `0x708300`.
2. At Ganton canal, reproduce 01/02/06/17/23 exactly (same time/weather, same camera orbit) with defaults. Confirm the 12/12 sky split reproduces.
3. `disableClouds=1` at the same spot → if peds become textured, R3-02 is confirmed.
4. Re-enable clouds; `pedShadows=0` + `stencilShadows=0` → if the black quad disappears, R3-01 is confirmed.
5. Enter an interior; run the stand-still yaw/pitch/move test; then `ambientFloor=0.10` and `pbrAmbientFloor=0.15`; then `colorFilterEnable=0`.
6. Repeat the black test in `[Modes] active=PS2` and `Xbox` at the same spot.

---

## 11. Direct answer: interior black — yaw/pitch or room position?

**Short answer: room position / world geometry first (65%), camera yaw/pitch second (25%), a full-screen shader third (10%).** The captured black is not a screen-space layer; it is a world-space polygon whose visibility is selected by the camera. The user’s “random angles” is the signature of sweeping a world-space occluder or an unlit face into the frustum, not of a screen-space shader that turns on at a yaw.

Breakdown:

1. **World-space occluder / shadow quad (65%).** The measured black has sloped, kinked, hard-edged boundaries and a 10-px step from lit road to `[2,2,2]`. In a room, a large stored-shadow blob or a black-material interior surface in front of the camera produces exactly a full-viewport black at some positions and not others. The tested binary’s `RenderStoredShadows` pass is unshielded (log), which is the same mechanism that produces the outdoor quad.
2. **Interior ambient/lighting crush (25%).** The source explicitly deals with “view/light-dependent black patches” indoors in `ClampAmbientToSky` (`src/render/pipelinecommon.cpp:81-95`). If the deployed binary lacks that hunk, or the room’s `CGame__currArea` is not what the code expects, the outdoor night law can crush interior ambient to black; which faces you see then depends on yaw/pitch. This is the only mechanism in the source that is genuinely view/normal-dependent.
3. **Stale fullscreen shader (10%).** The previous opposite-signature bug (“outdoors-only full-screen black, interiors perfect”) was a stale postfx shader over the sky (`src/core/main.cpp:4620-4645`), fixed by unbinding PS/VS at sky entry. A new stale-shader path indoors is possible but there is no pixel evidence in this series.

**Interior full viewport vs region — explicit answer.** There is no interior frame, so the interior case is not classified directly. The rule the frames *do* establish: a **region** has a straight geometric boundary and interior scene content is replaced; a **full-viewport pass** covers the whole raster and typically leaves a ~1-px lit margin at the edge. The captured black is a **region with a geometric boundary** (01/02/03/06), not a full viewport with a 1-px lit margin. For the reported interior: if the black covers the entire raster to the edge, it is a full-viewport class (screen-space pass or an ambient/normal crush), and the 10% stale-shader / 25% ambient branches move up; if it has a straight moving boundary, it is the world-space occluder class (65%).

**What is *not* the answer.** It is not primarily backface culling of single-sided interior geometry: a cull produces see-through walls/holes, not a uniform black fill, and the measured black is a front-facing opaque overlay, not a missing face. It is also not the ped texture leak: that is a separate, pitch/sky-gated fault (R3-02).

---

## 12. Caveats and falsification

1. **No interior frames.** §11 is a mechanism ranking, not an interior pixel measurement. The decisive test is in §10.3.
2. **No camera telemetry.** The pitch/azimuth values are visual estimates (±10°). The sky proxy is a frame-content measure, not a hook trace.
3. **23 hand-taken shots, not a sweep.** The 12/12 split is strong but is still an observational correlation; a controlled A/B (`disableClouds=1`) is required for causation.
4. **Two modes in one run.** The CJ orbit (01–07) is PS2; the Xbox shots (18–23) are a different building/vehicle pipe. The ped fault crosses modes, the black quad is proven only in PS2 here.
5. **Binary ≠ source.** The stored-shadow entry shield is in the tree but not in the tested binary. The cloud texture-raster gap is in both. Verify the binary stamp before shipping a fix.
6. **JPEG.** 14 of 23 are JPEG; 5×5 medians suppress ringing, but the “std 0.3” for shot 19 is a PNG and is exact.
7. **Confounders.** The black lowrider hovers/detaches panels in 05/07; `VehDeform.SA.asi`/`VehIK.asi` and hydraulics are installed. That is not the ped/black bug and should not be attributed to it.
8. **Falsifiers.** If `disableClouds=1` does not restore ped textures, R3-02’s cloud-atlas lead is wrong (the state leak could still be real, but from the corona/sprite flush). If a rebuilt binary with the stored-shadow entry shield still shows the 01/02/06 black, R3-01’s stored-shadow lead is wrong and the tag/decal route (`buildingPipe.cpp:322-340`, `505-522`) becomes primary. If the interior black follows yaw at a fixed position and is a full viewport with a 1-px margin, R3-03’s world-space lead is wrong and the stale-shader/ambient class moves up.

---

## Appendix A — raw numbers used above

### A.1 Per-image global / dark mask

| # | Mean RGB | Dark <24 | Strict <8 | Largest dark bbox |
|---|---|---|---|---|
| 01 | 32.3,37.0,38.8 | 51.5% | 8.3% | `[0,482,1767,991]` |
| 02 | 33.4,37.5,38.7 | 47.1% | 11.4% | `[0,584,1767,991]` |
| 03 | 34.1,37.1,38.3 | 40.9% | 20.7% | `[0,356,1233,991]` |
| 04 | 52.4,47.4,41.8 | 21.0% | 16.0% | `[542,35,1679,670]` (car) |
| 05 | 63.9,56.7,47.6 | 8.3% | 5.7% | `[1023,54,1679,390]` (car) |
| 06 | 34.6,40.2,45.5 | 41.7% | 33.8% | `[0,374,1767,991]` |
| 07 | 50.8,50.7,50.2 | 31.6% | 17.6% | `[234,380,1345,850]` |
| 08 | 59.7,57.8,57.2 | 9.6% | 4.7% | `[651,482,1053,823]` (car) |
| 09 | 34.5,30.8,28.0 | 39.9% | 18.1% | `[12,400,1526,991]` |
| 10 | 67.7,71.0,75.4 | 8.0% | 5.2% | `[671,491,1057,824]` (car) |
| 11 | 74.9,71.1,68.7 | 5.1% | 1.5% | `[800,762,1106,909]` |
| 12 | 56.7,68.6,70.1 | 4.9% | 1.6% | `[48,47,647,159]` |
| 13 | 83.8,106.7,104.6 | 6.3% | 2.2% | `[71,43,647,163]` |
| 14 | 82.3,93.4,94.8 | 5.9% | 1.4% | `[50,53,647,203]` |
| 15 | 137.4,147.8,156.7 | 5.0% | 1.3% | `[1115,341,1767,455]` |
| 16 | 67.3,75.2,74.8 | 10.1% | 1.3% | `[1293,54,1767,512]` |
| 17 | 75.3,75.4,70.6 | 3.0% | 1.7% | `[225,110,424,228]` |
| 18 | 129.6,111.7,110.6 | 1.7% | 1.3% | `[95,755,378,938]` |
| 19 | 145.1,128.6,129.8 | 3.2% | 2.0% | `[103,755,391,944]` |
| 20 | 150.5,123.2,116.4 | 1.5% | 1.3% | `[103,755,381,941]` |
| 21 | 97.5,89.5,78.5 | 2.4% | 1.1% | `[103,746,378,937]` |
| 22 | 94.1,87.5,78.1 | 2.8% | 1.2% | `[89,748,378,937]` |
| 23 | 49.6,50.7,49.8 | 12.3% | 3.3% | `[83,0,621,324]` (canopy) |

### A.2 Sky/cloud proxy

| # | Blue sky, top 30% | Pink cloud | # | Blue sky, top 30% |
|---|---|---|---|---|
| 01 | 22.6% | — | 13 | 0.8% |
| 02 | 20.8% | — | 14 | 0.4% |
| 03 | 19.5% | — | 15 | 52.1% |
| 04 | 0.2% | — | 16 | 62.1% |
| 05 | 0.1% | — | 17 | 17.2% |
| 06 | 46.1% | — | 18 | 0.1% (69.0% pink) |
| 07 | 16.6% | — | 19 | 0.1% (59.0% pink) |
| 08 | 22.5% | — | 20 | 0.1% (83.4% pink) |
| 09 | 0.1% | — | 21 | 0.0% |
| 10 | 42.1% | — | 22 | 0.1% |
| 11 | 15.6% | — | 23 | 12.6% |
| 12 | 0.4% | — | | |

### A.3 Log receipts

```text
[22:35:22.707] [BUILD] asipath=E:\games\gtasa_skygfx_plus\skygfx.asi size=2360832 mtime=2026-10-25 21:57:27 built=Oct 25 2026 21:56:44
[22:35:22.735] [shadow_install] 0x53E0C3 op=E8 orig=68E8DD00 MISMATCH zeroarg=1 site=CShadows::RenderStoredShadows (expect op=E8 orig=00708300)
[22:35:22.735] [shadow_install] 0x53E0C3 SKIPPED — vanilla path kept
(no "0x708300 entry hook installed" line anywhere in the 6.1 MB log)
[22:35:40.951] [Ped] ped_amb_cmp pre=(0.000,0.000,0.000) tc=(0.050,0.050,0.050) post=(0.200,0.200,0.200) floor=0.200
[22:35:40.941] [PedOpaque] pre-entry dev ALPHAREF=2 ALPHAFUNC=5 ALPHABLENDENABLE=0 ZWRITE=0 ZENABLE=0 ZFUNC=8 ALPHATEST=0
[22:35:40.955] [PedDual] legacy dual pass restored (zwriteThresholdPed=128)
[22:35:40.964] [PedDual] first ped atomic alpha probe: material/prelit=0 texture=0
[22:37:04.730] [Mode] apply PS2 pipes=1/0/0 cf=1
[22:37:29.442] [Mode] apply PS2 pipes=1/0/0 cf=1
[22:37:42.001] PS2Building: frame 1717201 object=25F2128C type=1 flags=0x6F
[22:37:42.061] [PED] ZWRITE off/desynced on entry (rw=1 dev=0) — forcing TRUE for cb
[22:41:45.855] [Mode] apply Xbox pipes=2/1/2 cf=2
[22:42:45.004] XboxBuilding: frame 5923201 object=228F01E0 type=1 flags=0x2F
[22:42:45.124] [PBR-Ambient] absolute floor +0.000 (lum 0.030 -> 0.030, skyL=0.522)
[22:42:46.339] texalpha: ... unknown_texnull=152749 ...
```

---

## Appendix B — where the report’s numbers came from

* Dark/strict masks and components: full-resolution 1768×992, `max RGB < 24` and `< 8`, row-run union-find.
* Medians: 5×5 at the cited native coordinates.
* CJ boxes: 2× grid crops; head/torso/leg points listed in the tables.
* Shot 19 flatness: PNG, 40×40 box around (870,780), per-channel std `[0.5,0.4,0.0]`, median `116,103,95`.
* The scratch crops/grids are outside the repo; no repo asset was added or changed.

**End of report.**

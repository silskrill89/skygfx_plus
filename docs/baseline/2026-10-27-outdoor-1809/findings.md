# Outdoor Playtest Findings — 2026-10-27 18:09 (build 2,438,656 B)

## 1. Build / config identity

- **Build under test**: 2,438,656 B (per report); single playtest session, 5 ShareX captures in 8 s real time (2026-10-27 18:09:50–18:09:58).
- All captures 1920x1080 JPEG. In-game clock advances 10:12 → 10:20 (daytime, sun reasonably high — light comes from upper right in outdoor frames).
- HUD identical across all 5: weapon slot `14-29`, money `$00001099`, full red health bar, camera icon → same session, no config changes mid-run. Zone text `Ganton` visible in shots 3–5 (minimap: Ganton/Idlewood/Glen Park area).
- **Scope note (adversarial)**: shots 1–2 (18:09:50/52) are **interior** frames (Johnson house), NOT outdoor. The outdoor evidence proper is shots 3–5 (18:09:56–58). Interiors are "almost all fixed" per user — confirmed for lighting/materials, but shots 1–2 still carry two residual defects (black quads, yellow marker triangle), documented below because they survive into the "fixed" regime.

## 2. Per-image overview table

| # | file | mtime (real) | in-game | scene | ground/roads | buildings | vehicles | peds | sky/clouds | water | shadows/quads | coronas/lights | exposure/colour |
|---|------|--------------|---------|-------|--------------|-----------|----------|------|------------|-------|---------------|----------------|-----------------|
| 1 | gta_sa_Mx4tvPG713.jpg | 18:09:50.156 | 10:12 | Johnson house kitchen/dining (interior) | tile floor OK, warm olive | interior walls/props OK | none | CJ lit sanely, not blown | n/a | n/a | **black quad over table/bench** (hard edges) + black quads blocking back-room view | none | dim interior, warm olive (wall 41,38,29); no green cast |
| 2 | gta_sa_1nnPtMUTyu.jpg | 18:09:52.023 | 10:14 | Johnson house living room (interior) | wood floor OK | interior OK | none | CJ mid-stride, sane | n/a | n/a | **2 hard-edged black rects** (lower-left rug + doorway); grey-white translucent smear inside LL rect | none | wall (113,109,108) neutral; window white (expected) |
| 3 | gta_sa_2wk9kKL5sL.jpg | 18:09:56.357 | 10:18 | OUTDOOR Ganton park/skate plaza, low angle | plaza ~15% of frame swallowed by black quads; grey islands L≈60 | distant buildings OK | none visible | CJ large mid-grey desaturated (93,89,82); pale fence figures | clean gradient, **no clouds at all** | flat pale sheet right edge (102,115,129) | **giant flat (2,2,2) blobs**, splatter + hard poly edges; stepped translucent band mid-frame | stray red/orange dots ~70 px (day!) | crushed foreground (L 2–60) vs normal sky |
| 4 | gta_sa_0q5Lu8gCvw.jpg | 18:09:57.518 | 10:19 | OUTDOOR same plaza, panned right/down | black 19.3% of frame; blob interior 65% pure (2,2,2) | distant buildings OK | none | CJ washed grey (92,88,82, sat-spread 11) | clean patch (99,141,189) stdev≤11, no clouds | flat pale sheet (102,118,134) | stepped quads: 4 luminance plateaus w/ hard ≤5px edges at x1100 profile | red/orange dots ~110 px | same crush; no green cast |
| 5 | gta_sa_UkkWscOSBu.jpg | 18:09:58.102 | 10:20 | OUTDOOR same plaza, low angle | black 23.9% — worst; ground detail only in narrow islands | distant buildings OK | none | **CJ torso/head blown (172,165,160)**, legs crushed (22,21,19) | gradient OK, no clouds | flat pale sheet (103,119,135) | giant (2,2,2) masses w/ hard poly edges lower-right/left | red/orange dots ~150 px (densest) | split exposure: ped blown, ground crushed |

## 3. Per-image detail notes

All RGB values measured with Pillow on the 1920x1080 JPEGs (3–5 px box averages unless stated). "L" = Rec.601 luminance. HUD regions excluded from all frame statistics.

### 3.1 gta_sa_Mx4tvPG713.jpg

Interior, 10:12, Johnson house kitchen/dining; CJ centred, doorway to living room behind.

- **Defect (i) stray yellow marker triangle**: solid saturated yellow inverted triangle over CJ's head. Measured bbox **x[920..1022] y[412..566]**, core fill **(221,221,3) → (254,254,3)** — pure #FFxx03-class yellow, constant fill, hard 3-edge polygon silhouette. Geometry class (real quad/cone), not postfx. Reappears identically in 3.2, absent outdoors (3–5).
- **Black quad over table/bench (left)**: sample (350,700) = **(9,7,5)** near-black mass with hard straight polygon edges where it cuts the bench and floor. Covers dining table + bench + floor patch. Geometry class — hard edges + flat fill rule out tonemap.
- **Back-room doorway**: quads occlude the living-room view through the doorway (interior near-black; furniture edges barely visible).
- **Exposure/colour**: wall (400,300) = **(41,38,29)**, floor (1400,950) = **(45,41,30)**, ceiling (700,120) = **(44,41,32)**. R≥G>B everywhere → warm olive interior cast; **no green cast** (green cast would show G>R). Dim but plausibly interior-lit.
- CJ shading sane: white tank reads light grey, jeans blue, skin warm. No corona/light defects indoors.

### 3.2 gta_sa_1nnPtMUTyu.jpg

Interior, 10:14, Johnson house living room (camera rotated from 3.1, same session state).

- **Defect (i) again**: yellow triangle at (975,420) = **(254,254,3)** — same marker, same offset relative to CJ. Two-for-two indoors.
- **Hard-edged black rectangles (defect (a)/(d) family)**:
  - Lower-left rug area: measured rect sample (300,850) = **(2,2,2)**, straight rectangular borders → real quad. **Inside** it, a soft neutral grey-white smear cluster, bbox **x[0..448] y[700..934]**, avgRGB **(127,125,123)** (R≈G≈B, mean spread ≈2/255) — a broken translucent light/gradient overlay sitting on the black quad. This is exactly the "translucent stepped quads" signature of defect (d): soft fill but hard quad boundary.
  - Doorway/corridor centre: dark rect block occluding the back room (same black-quad family).
- Wall (250,300) = **(113,109,108)** neutral blue-grey — normal. Window blown white = expected window light, not scored as defect.
- Verdict for interiors: materials/lighting indeed fixed; **black quads + marker triangle are the residual interior defects**.

### 3.3 gta_sa_2wk9kKL5sL.jpg

OUTDOOR, 10:18, Ganton park/skate plaza (skate ramps, palms, chain-link fence, hills; pond at right edge; low camera angle).

- **Defect (a) — giant black quads (dominant)**: near-pure-black masses cover the plaza. Blob interiors: (400,620) = **(2,2,2)**, (250,700) = **(2,2,2)**. Black-fraction of frame (HUD-excluded, every-3px sample): **14.9 %**. Edge morphology: splatter/alpha-test organic boundaries in places AND hard straight polygon segments where blobs cut the plaza (right of centre) — i.e. mis-projected decal/shadow quads with alpha-tested edges. Constant flat fill (2,2,2) over huge areas rules out any soft postfx gradient.
  - **Refinement vs known list**: known (a) says opaque **#20281E** (32,40,30 greenish); measured fill here is **(2,2,2)** — same family, darker unlit variant. Worth knowing for shader-side triage: the quad is rendering with effectively zero light response.
- **Defect (d) — translucent stepped quads**: translucent grey band across mid-frame behind the skate ramp. Vertical luminance profile at **x=1100**: 69 → 15 (y≈422), 15 → 67 (y≈458), 55 → 26 (y≈480), 26 → 67 (y≈535). Each transition ≤5 px = hard quad edge; each plateau a different translucency level. NOT a smooth postfx effect (would be continuous, whole-frame).
- **Peds**: foreground CJ is a large mid-grey desaturated mass — ped-blob avgRGB **(93,89,82)**, mean saturation spread **14.9/255** (near-neutral). In THIS frame he is not fully blown (brightest ped-class pixels below y524 reach only L≈60); he reads "washed" because he sits between crushed-black quads (L=2) and grey plaza (L≈60) plus genuine desaturation. Distant fence-line figure avgRGB **(106,107,102)** — pale neutral, mild defect (h) (real skin/clothes would show spread ≫5).
- **Sky/clouds**: smooth blue gradient (neighbouring frame's clean patch measured 99,141,189, stdev ≤11). **No cloud sprites/layer visible at all** — sky "looks good" holds, but there are literally zero clouds to be half-broken (see §4 note on (b)).
- **Water**: right-edge pond = flat pale blue-grey (1650,520) = **(69,80,92)**, brighter sheets (102,115,129); no wave/specular variation → defect (c) flat water. Hue is pale blue-grey rather than "teal" in these frames.
- **Coronas/lights (NEW)**: scattered saturated red/orange glow dots across the mid-ground at y≈240–480, on hedges/grass/fence (clusters at (600,360) n=15, (60,360) n=14, (780,360) n=13, (300,240) n=9, (1200,300) n=7, …; ~70 sampled px at step 2). At **10:18 AM** streetlamp corona sprites should not be visible → broken corona timing/blending. Distinct from known (f) (which is black rectangles at night) — scored as new (j) below.
- **Exposure/colour**: foreground crushed (L 2–60) against normal sky; no green cast (sky blue channels dominant, ground warm-grey).

### 3.4 gta_sa_0q5Lu8gCvw.jpg

OUTDOOR, 10:19, same plaza, camera panned right/down one second later.

- **Defect (a)**: black-fraction **19.3 %**. Blob-interior census (x300–900, y560–820, every-3px): **(2,2,2) = 11381/17400 (65 %)**; next most common values ~(22,21,19)/(21,20,18) = grey plaza/props. So the quads are flat constant black, not dark shading. Hard straight seam where blob meets grey plaza (plaza L≈64–76) on the right of centre.
- **Defect (d)** — the clearest stepped-quad evidence of the set. Profile at **x=1100**: y400–420 ≈ (58–69) → y425–430 = (15–17) → y435–495 ≈ (30–39) → y500–515 = (78–96) → y520+ = **(2,2,2)**. Four distinct plateaus, all boundaries ≤5 px: stacked translucent quads + black quad with ground see-through between them. Classification: geometry/alpha-quad, not postfx (edges too hard, values too flat).
- **Peds**: CJ washed grey, ped-blob avgRGB **(92,88,82)**, saturation spread **11.4/255** — most desaturated ped of the set. Fence-line figures: 122 px ≥L95 at x[1005..1307] y[420..456], avgRGB **(108,105,100)** — pale neutral mannequins (defect (h) mild at distance).
- **Sky**: clean patch x250–420 y30–120 = **(99,141,189)**, stdev **(5.5, 7.6, 10.7)** — smooth gradient. Zero clouds, zero cloud artifacts in the visible band.
- **Water**: (1830,520) = **(102,118,134)**, (1900,545) = **(97,109,123)** — flat pale sheet, B ≈ R+30, unshaded; silhouette cuts oddly against plants (see-through edge, defect (d) adjacency).
- **Coronas/lights (NEW)**: red/orange dot clusters (~110 sampled px): (360,480) n=15, (360,240) n=14, (540,360) n=13, (780,360) n=13, (0,360) n=11, (420,480) n=9, (1020,360) n=8 …
- **Yellow marker**: **0** yellow px in frame — marker triangle is interior-only in this set.

### 3.5 gta_sa_UkkWscOSBu.jpg

OUTDOOR, 10:20, same plaza, low angle, CJ walking away from camera.

- **Defect (a) — worst frame**: black-fraction **23.9 %**; giant flat masses (samples (600,700) = (2,2,2), (1300,620) = (2,2,2)) cover essentially the entire ground plane. Hard polygon edges visible lower-right and left; plaza texture survives only in narrow grey islands (L≈50–70).
- **Defect (h) — blown ped (clearest evidence of the set)**: CJ head/shirt cluster **x[900..1000] y[500..600], avgRGB (172,165,160)** (~4300 real px), face (945,515) = **(175,168,162)**, shirt (945,565) = **(191,185,179)** — bright and near-neutral (channel spread 12–13; real skin would show spread ≫30). Meanwhile his legs (950,880) = **(22,21,19)** — crushed. **One ped, two extremes**: torso/head blown to near-white, legs crushed to near-black. Classic high-sun ped shading failure.
- **Defect (d)**: profile at x=1100 again stepped — y435–455 ≈ (48–68) → y460–490 ≈ (28–34) → y500–510 = (55–91) → y515+ = **(2,2,2)**.
- **Water**: (1870,520) = **(103,119,135)** — same flat pale sheet (c).
- **Sky**: gradient fine (matches 3.4's measured smoothness), no clouds.
- **Coronas/lights (NEW)**: densest of the set (~150 sampled px); clusters (600,480) n=32, (360,240) n=29, (780,360) n=19, (540,480) n=17, (540,360) n=15, (420,480) n=13 …
- **Yellow marker**: none.

## 4. Consolidated defect table (severity × frequency → known-list mapping)

Ranked by severity × frequency. Class discipline: **geometry** = hard polygon edges, flat constant fill, localized; **postfx/tonemap** = soft, whole-frame, gradient-like. All ranked defects below are geometry/shading unless stated.

| Rank | Defect | Class | Known-list map | Sev (1–5) | Freq | Key evidence |
|------|--------|-------|----------------|-----------|------|--------------|
| 1 | Giant mis-projected black shadow/decal quads covering the ground | Geometry (alpha-tested quads, flat fill) | **(a)** — variant: measured fill **(2,2,2)** not #20281E; same family, unlit-black constant | 5 | 3/3 outdoor (14.9 / 19.3 / 23.9 % of frame); indoor variants 2/2 | §3.3–3.5 blob samples all (2,2,2); img4 census 65 % of blob interior = (2,2,2) |
| 2 | Translucent stepped quads + ground see-through (stacked luminance plateaus, ≤5 px hard edges) | Geometry (stacked alpha quads) | **(d)** | 4 | 3/3 outdoor + 1 indoor smear (3.2) | x=1100 profiles: img4 four plateaus (58–69 / 15–17 / 30–39 / 78–96 / 2,2,2); img3 & img5 same signature |
| 3 | White/blown peds at high sun + ped desaturation (torso blown, legs crushed in one ped) | Shading (ped-local light response) | **(h)** | 4 | 3/3 outdoor (worst img5) | img5 head/shirt (172,165,160) spread 12; legs (22,21,19); img3/4 ped sat-spread 11–15/255 |
| 4 | Stray red/orange corona dots in DAYLIGHT + white sparkle dots on vegetation | Corona/sprite blending | **NEW (j)** — adjacent to (f) but distinct: (f) is black rects at night; these are red dots by day | 3 | 3/3 outdoor (70–150 sampled px/frame, 10:18–10:20 AM) | clusters e.g. img5 (600,480) n=32, (360,240) n=29 |
| 5 | Flat unshaded water sheet (right-edge pond) | Shading (water material) | **(c)** — hue here pale blue-grey (103,119,135), not teal; flat/unshaded matches | 3 | 3/3 outdoor (right edge every frame) | flat sheets (102,118,134)/(103,119,135), no wave/specular variation |
| 6 | Interior black quads (hard-edged rects; one carries a grey translucent smear) | Geometry | **(a)/(d)** indoor residue | 3 | 2/2 interior | img2 rect (2,2,2) + smear (127,125,123); img1 table quad (9,7,5) |
| 7 | Stray yellow marker triangle over CJ's head | Geometry (opaque constant-fill polygon) | **(i)** | 2 | 2/5 (interiors only; 0 yellow px outdoors) | bbox x[920..1022] y[412..566], fill (221–254,221–254,3) |
| 8 | Crushed dark outdoor foreground (L 2–60) vs normal sky | Postfx/tonemap (soft, whole-frame) | **(e) inverted** — NOT washout, NOT green cast; exposure crush instead | 2 | 3/3 outdoor | sky (99,141,189) normal; ground islands L 50–70, black quads L=2 |
| 9 | Cloud layer absent (sky itself fine) | Environment layer | **(b) partial** — "sky looks good" holds, but ZERO clouds observable (stronger than "half-broken"; could be clear-weather state) | 1 | 3/3 outdoor | clean-sky stdev ≤11 (img4); no cloud sprites in any visible sky band |
| — | Night corona-light blend (black rectangles) | — | **(f)** | — | **0/5** (all day shots) | not assessable in this set — needs night capture |
| — | Low-angle face-order / x-ray geometry | — | **(g)** | — | **0/5 not conclusively seen** | low angles present, but no wall/face x-ray demonstrable; translucent band showing vegetation through it is scored under (d), not (g) |
| — | Washout / green cast | — | **(e) as written** | — | **0/5** | measured walls (41,38,29) R≥G, sky (99,141,189) blue-dominant → no green cast anywhere |

**New findings not on the known list** (matter most, per brief):
1. **(j-1) Daylight red/orange corona dots** (rank 4) — red glow points on hedges/grass/fence at 10:18–10:20 AM; not in known list ((f) is the night variant).
2. **(j-2) Ped intra-model split exposure** (part of rank 3, but sharper than "white peds"): in img5 the SAME ped has blown torso/head (172,165,160) and crushed legs (22,21,19) — shading breaks per-part/per-light, not a global tonemap.
3. **(j-3) Interior black-quad + grey translucent smear combo** (rank 6): interiors claimed "almost fixed" still carry the (a)/(d) family — the fix wave that cleared interior lighting did not clear these quads.
4. **(j-4) Black-quad fill constant is (2,2,2), not the documented #20281E** — if a fix targets a #20281E-tinted shader path, it may miss these unlit-black quads.

## 5. Verdict: which shots prove "outdoor still broken" and first fix targets

**"Outdoor still broken" = shots 3, 4, 5** (2wk9kKL5sL / 0q5Lu8gCvw / UkkWscOSBu, 18:09:56–58). All three show the same signature at 10:18–10:20 in direct sunlight: giant flat-black projected quads eating 15–24 % of the frame (ground almost fully covered in 5), stacked translucent quads with hard plateau edges, flat unshaded water at the pond edge, desaturated/blown peds, and stray daytime corona dots. There is no frame among 3–5 where the ground renders correctly for its whole visible extent.

**Shots 1–2 (interiors)** back the "almost all fixed" claim for lighting/materials, but are NOT clean: both carry the stray yellow marker triangle (i) and hard-edged black quads (one with a grey translucent smear). Treat interiors as "lit correctly, quad defects pending".

**Fix-wave order (by severity × frequency, geometry-first):**
1. **(a) black quads** — kills every outdoor frame; the flat (2,2,2) unlit constant is the smoking gun (§3.4 census). Handle the (2,2,2) variant explicitly (see j-4).
2. **(d) stepped translucent quads + ground see-through** — present in all 3 outdoor frames with hard multi-plateau profiles; same projection family as (a) is plausible from the evidence (both are flat-fill alpha quads), so one wave may clear both.
3. **(h) ped shading blowout/crush split** (j-2) — img5's (172,165,160) torso vs (22,21,19) legs is the acceptance test.
4. **(j-1) daylight red corona dots** — cheap, high visibility, all outdoor frames.
5. **(c) flat water** — right-edge pond in all 3 outdoor frames; lower visual mass than (a)/(d) but persistent.
6. **(i) + interior black rects** — small, but they violate the "interiors fixed" claim and are quick wins.

**Not assessable from this set** (do NOT close): **(f)** night corona black rectangles (needs a night capture) and **(g)** x-ray/face-order (needs close-range ped/face shots at low angle with correct geometry around). **(e)** as written (washout/green cast) is contradicted by measurement — no green cast; the actual whole-frame postfx symptom here is exposure crush (rank 8).

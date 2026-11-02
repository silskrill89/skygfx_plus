# Screenshot Forensics — 2026-10-25 16:44 Round (6 images)

User complaint (verbatim): *"very messed up clouds and the ground issue, the decals and code pertaining to those, and then ground rendering, and order of things rendered, needs to be properly rearranged, the meshes shouldn't be drawing inside out, with the back of them being somehow displayed on front."*

Context: user says he selected **original Xbox mode**; suspected the deployed build was PBR. **Config check (deployed INI `E:\games\gtasa_skygfx_plus\skygfx.ini`): `pipeline = Xbox`, `buildingPipe = Xbox`, `vehiclePipe = Xbox`, `waterStyle = Xbox`, `cloudRenderFix = 1`, `disableClouds = 0`, `fixShadows = 0`, `forwardPlusEnable = 1`, `colorFilter = PC`, `colorFilterEnable = 0`.** So "mode did not apply" is *not* supported by the config file — Xbox is what is written. Any PBR behaviour is therefore an internal routing/fallback issue, not a missing setting. (See §5.)

Sampling: Python 3.10.11 + Pillow 12.2.0, 5x5 median patches. PNG = lossless. JPG = lossy (chroma subsampling smears 1-2 px edges; sub-3 px colour differences in JPGs are unreliable). Candidate coordinates were picked visually then verified with row scans (e.g. blue-fraction per row); where a sample landed on the wrong material it is either corrected or not reported. Crops: `C:\Users\AAAAAA~1\AppData\Local\Temp\opencode\crops1644\`.

## 1. Per-image table

| # | File | Time | In-game clock | Scene | Artifacts (region, sampled RGB, description) |
|---|------|------|---------------|-------|----------------------------------------------|
| 1 | gta_sa_Vei2TpWYeK.png | 16:44:23 | 12:42 | Grove St / park pond, running N; clear noon sky | **A1 water/ground overlay band** y≈418-660 across x≈260-1850: sky-blue (30,102,163) and (48,106,179) interleaved with **hard-edged pure-black blobs (2,2,2)**, 58% of band pixels <25 luma, 0% >200; black shapes sit directly under far-bank plants. **A2 pavement** foreground y≈660-1080 mean (75,70,66) sd=14 — flat, near-featureless asphalt (no lane marks). **A3 vegetation very dark**: palm trunk/canopy (2,6,11), building wall (22,21,13) at noon. Sky 48,106,179 top; **no clouds anywhere** (sky-patch sd is high only because trees occupy the box). Bright specular sparkles inside water band (e.g. ~1050,450). |
| 2 | gta_sa_2IR3aKF8wt.jpg | 16:44:27 | 12:45 | Street T-junction, Grove St area (minimap BS/CV), elevated rail + CUSTOMER PARKING sign, pond behind fence, JPG | **B1 opaque black ground decal/shadow** x≈120-1300 y≈770-1060: interior (2,2,2), 61% of box <25 luma; edges are **axis-aligned texel stair-steps** (4-6 px) with thin grey speckle gaps inside => alpha-cutout mask accepted, RGB=0 unmodulated. Matches documented road-decal artifact (buildingPipe.cpp:490-500). **B2 sky**: (58,106,180) centre, **no clouds**; top-left flat light-grey polygon (203,191,203) = untextured surface (rail/roof underside) clipping frame. **B3 vegetation black**: hedge (12,12,2), palm fronds (21,54,95 over sky). **B4 pond** behind fence y≈430-560 shows inverted treeline reflection, smeared/dark — plausible water reflection but low quality; same site as IMG 1. |
| 3 | gta_sa_7ClkqxnXxz.png | 16:44:29 | 12:48 | Under freeway interchange, looking up; palm + viaduct, PNG | **C1 CLOUDS = huge overlapping flat quads** filling top ~35% (y=0-330): mottled grey mean (108,116,126) sd≈11 over patch (600,0)-(1400,120), soft "cumulus" cores, **straight polygonal edges / visible quad seams**, thin near-white translucent band (140,146,155) with blue (65,106,179) showing through; quads sit only ~30-60 m overhead and the scene below is sunlit pavement (83,74,75), so this is not a solid bridge deck. Cloud geometry/altitude wrong, not missing. **C2** clear blue away from quads (65,106,179). **C3** vegetation near-black (palm trunk 11,2,2; building 30,21,11). |
| 4 | gta_sa_NrOxfdYkUy.png | 16:44:43 | 13:02 | Downtown LS boulevard, running S; palms, skyscrapers, power lines, PNG | **D1 stretched black shadow decal** x≈820-1850 y≈650-800: long thin horizontal streaks, interior (2,2,2), band mean (21.8,21.4,18.8) sd≈22, 58% <25 luma; edges hard/texel-stepped; passes through/around player's feet but is not his soft shadow -> shadow/decal projection smeared along ground. Scattered repeats bottom-right (1700-1900, 880-1010). **D2 no clouds**: sky (57,106,179), faint contrail only. **D3** sunlit palm (40,62,68) vs silhouette palm (2,6,11); utility-pole band (30,34,30). Road otherwise clean (40,34,30). |
| 5 | gta_sa_OQxDAeqVGo.jpg | 16:44:56 | 13:15 | Same park pond seen over the roof of a black car (camera above/behind), JPG | **E1 car renders flat black** trunk (7,7,7), roof (3,5,2), quarter (2,2,2); body patch (450,300)-(950,700) mean (4.3,4.6,3.9) sd≈6, **96.8% pixels <25 luma** — no paint highlight/reflection at noon, only chrome trim + grey rear-window smear (52,53,57). **E2 pond = flat blue band** (40,90,139) with **hard-edged black blobs at far edge** (same 2,2,2 decal class), water shows no wave/spec detail. **E3** pond band mean (24.4,40.5,48.6) sd=(12,32,57) = blue/black split. **E4** grass top-left (5,10,3) near-black. No sky/clouds in frame. |
| 6 | gta_sa_0MVqHI1k2T.jpg | 16:45:07 | 13:25 | Boulevard with pond behind chain-link fence; player standing; three parked black cars, JPG | **F1 pond flat blue band** rows y≈320-345, median (44,89,129); black hard-edged blobs at its far edge (rows 260-280 median (19,47,68) -> black/blue boundary), same 2,2,2 class. **F2 three cars flat black**: left-car patch (350,400)-(1000,550) mean (17.6,17.5,15.5), 77% <25; mid-car patch (950,330)-(1360,480) mean (22.3,23.8,22.9), 63% <25. Left car shows no wheel/arch gap -> possibly sunk/floating (perspective unresolved; INFERENCE). **F3 sky** (40,106,182) at row 100, **no clouds**. **F4** scene reads dusk-dark despite 13:25; palms/foliage (2,6,11)-(12,12,2). |

## 2. Consolidated ranked issue list

Legend: **CONFIRMED-FROM-PIXELS** = the *appearance* is measured; **INFERENCE** = cause/owner. File:line = suspected subsystem.

| Rank | Severity | Issue | Frequency | Subsystem / file:line | Status |
|---|---|---|---|---|---|
| 1 | **Critical** | **Opaque black shadow/road decals** with RGB=0 written unmodulated and hard texel-stepped alpha edges; drawn on pavement **and on water**. Core (2,2,2), 58-77% of affected boxes <25 luma, 0% bright; long horizontal streaking in IMG 4. | 6/6 (A1, B1, C-bottom edge, D1, E2, F1) | `src/render/buildingPipe.cpp:490-500` (comment names this exactly: "solid black pool that follows the decal texture's splatter shape — the road-decal artifact"), repair path `pipeForceAlphaBlock` `buildingPipe.cpp:531-603`; shadow offset patch gate `src/core/main.cpp:7162` (`fixShadows`, currently **0** in INI). | Appearance **CONFIRMED**; whether the mesh is the shadow decal vs a road splatter decal is **INFERENCE** (shape = palm-frond/splatter, obeys ground perspective). |
| 2 | **High** | **Cloud layer is geometrically wrong**: huge overlapping flat quads with visible straight edges/seams at very low altitude, grey/opaque, semitransparent white edge bands. Absent entirely from the sky in the other 5 frames. | 6/6 (present-but-wrong in IMG 3; absent in 1,2,4,5,6 views) | `CClouds::Render` hook `src/core/main.cpp:4321-4435` (`cloudRenderFix`, ON), cloud draw is first in scene pass (`main.cpp:4327`); Xbox cloud path via `pipeline = Xbox`. | Appearance **CONFIRMED**; altitude/scale cause **INFERENCE**. |
| 3 | **High** | **Pond/water is a flat blue plane** (44,89,129)/(40,90,139) with no wave, foam or specular detail; hard black blobs at its far edge. | 4/6 (IMG 1,2,5,6) | `waterStyle = Xbox` (`skygfx.ini`), water params `waterFresnelPower/Split/waterSpecularPower=512/waterQuality=0`; water enum parse `src/core/main.cpp:6006-6012`. | Appearance **CONFIRMED**; cause **INFERENCE** (Xbox water path or missing reflection/foam textures). |
| 4 | **High** | **Vehicles render flat black, no paint reflection**: car body means (4.3,4.6,3.9) / (17.6,17.5,15.5) / (22.3,23.8,22.9), 63-97% <25 luma. Only chrome trim and window glass read. | 2/6 frames but 4/4 cars (IMG 5 car, IMG 6 three cars) | `vehiclePipe = Xbox`; `forwardPlusEnable = 1` (`src/render/forwardplus.cpp`); PBR vehicle path only if routing ignores Xbox. | Appearance **CONFIRMED**; system **INFERENCE** (Xbox car lighting vs Forward+ black fallback). |
| 5 | Medium | **Too-dark scene / crushed vegetation** at midday: foliage (2,6,11), (5,10,3), (12,12,2), building (22,21,13); pavement bright (75,70,66) -> not global exposure, vegetation/building lighting specifically. | 6/6 | Building/vehicle ambient+direct upload, `src/render/buildingPipe.cpp`, `pipelinecommon.cpp`; `colorFilterEnable=0`, `colorFilter=PC`. | Appearance **CONFIRMED**; subsystem **INFERENCE**. |
| 6 | Medium | **Pond "water" edge is a perfectly straight cut** across the bank; black blobs sit at the cut line. Reads as a flat plane drawn/clipped rather than a shoreline. | 4/6 | Water plane + shore fade `waterShoreFade=2`, `waterShoreFoam=1`; draw order (water vs bank decals). | Appearance **CONFIRMED**; cause **INFERENCE**. |
| 7 | Low/Unproven | **Inside-out / backface** complaint: no closed mesh in the 6 frames shows an inverted-winding interior. The flat grey planes (IMG 2 top-left; IMG 3 overhead) are cloud/structure quads; the black artifacts are decals. | 0/6 confirmed | Cloud plane + water plane culling; `pipeForceCullMode`. | **INFERENCE ONLY — not confirmed from pixels.** |

## 3. Direct answers

**(a) Clouds — geometrically wrong, blend-wrong, or missing?**
**Geometrically wrong and present.** IMG 3 proves the cloud layer exists: multiple flat textured quads with straight polygonal boundaries and visible seams fill the top of the frame at low altitude, mottled grey (108,116,126), with a translucent bright edge band (140,146,155) over blue sky. In IMG 1/2/4/5/6 the sky is pure blue (48-58,106-180) with **zero cloud pixels in view**, so presence is also view-dependent/inconsistent. This is not simply "clouds missing" and not a colour/blend problem: it is a **projection/altitude/scale** problem with hard quad edges. CONFIRMED-FROM-PIXELS for the look; cause INFERENCE.

**(b) Ground — see-through (alpha holes) or covered by an overlay?**
**Covered by an opaque overlay, not see-through.** There is no sky-blue or background colour punching through the pavement in any frame; the plane remains continuous. The black regions are **opaque** (RGB 2,2,2, ~95-100% alpha-equivalent coverage), lie *on top of* the surface, and follow a decal mask with texel-quantised edges. Separately, the pond is a **flat blue plane** with a hard straight edge, not a hole. So: decal/shadow overlay on ground -> CONFIRMED; any ground-geometry hole -> not found.

**(c) Decals — blending correctly and in the right order?**
**No.** Decals are drawn with the mask "accepted" but the RGB=0 texels are not modulated away, so transparent/dark parts land as **solid black** with hard staircase edges. That is an alpha-test/blend path failure, not a colour-space effect. Order is also wrong in one specific way: the black decals are drawn over the **pond water** (IMG 1, 5, 6) — a ground-projected shadow/decal should not composite onto the water surface. CONFIRMED-FROM-PIXELS for appearance; the exact decal source (road splatter vs object shadow) is INFERENCE.

**(d) Anything inside-out (closed-mesh backfaces visible)?**
**No confirmed case.** I checked every frame for a closed object showing its interior (car cabin seen through body, building interior over facade). The car in IMG 5 is black, but its panels/window aperture are consistent with an exterior view; the rear window shows a grey glass smear, not the cabin interior. The large flat light-grey surfaces (IMG 2 top-left; IMG 3 overhead) are planar cloud/roof quads, not inverted closed meshes. Verdict: the user's "back displayed on front" impression is most likely caused by the **cloud quads and flat water plane being drawn in front of/through geometry**, not by reversed winding. This is INFERENCE and needs a targeted A/B (cull-mode dump).

**(e) Draw order — what draws over what?**
Measured order failures:
1. **Black shadow/decal over road AND over water** (IMG 1, 2, 4, 5, 6). Expected: decals only on opaque ground or depth/water-masked; observed: they composite onto the pond surface and onto the road with full opacity.
2. **Cloud layer drawn first in the scene pass** (`main.cpp:4327`) but its quads occupy near-camera depth, so they cover sky and intersect the near palm silhouette's airspace; the palm is still drawn in front, so this is depth-scale, not a sort inversion.
3. The player **is** correctly drawn over the ground decals (his legs occlude the black streaks), so scene-object-vs-decal order is not globally reversed.
No evidence of the whole scene being drawn back-to-front.

## 4. RGB signatures (for mechanical comparison in future runs)

| Artifact class | Characteristic pixels | Notes |
|---|---|---|
| Opaque shadow/road decal | core **(2,2,2)** (range 2-7), **>58% of box <25 luma**, 0% >200, hard texel-step edges | Constancy of (2,2,2) across PNG and JPG is the strongest signature; JPG preserves it because it is flat black. |
| Cloud quad layer | **(99,110,116) / (108,116,126) / (124,129,140)**, patch sd ≈ 11-12, 0% <25, 0% >200 | Low-contrast blue-grey mottle; bright translucent edge band **(140,146,155)**. |
| Clear sky | **(48,106,179)** high, **(57-58,106,180)** mid, **(40,106,182)** IMG 6 | Near-constant blue; useful to prove "no cloud pixels". |
| Pond / Xbox water | **(30,102,163)**, **(40,90,139)**, **(44,89,129)** | Saturated blue; sd is high only where black blobs/reflections are included. |
| Pavement / asphalt | **(75,70,66)** sd 14 (IMG 1); **(40,34,30)** road (IMG 4/6) | Ground itself is not black; black pools are an overlay. |
| Vegetation (too dark) | **(2,6,11)**, **(5,10,3)**, **(12,12,2)**, **(11,2,2)** | Near-black foliage at noon/13:25. |
| Black car body | mean **(4.3,4.6,3.9)** sd 6, **96.8% <25**; other cars mean 17-23 with 63-77% <25 | Chrome trim/window glass remain the only non-black pixels. |
| Grey untextured polygon (IMG 2 top-left) | **(203,191,203)** | Flat, no texture detail; likely rail/roof underside, not confirmed as an artifact. |

## 5. A/B test list (which INI keys isolate each class)

Deployed INI: `E:\games\gtasa_skygfx_plus\skygfx.ini`. Restart required for most; use `skygfx_dbg.log` + same 6 camera poses.

| Test | Key(s) | Isolation goal |
|---|---|---|
| T1 | `fixShadows = 0 -> 1` | Shadow offset patch (`main.cpp:7162`). Tests whether the displaced/stretched black decal blobs (IMG 2/4) are the vanilla shadow-offset bug. Highest-value single test for Issue 1. |
| T2 | `stencilShadows = -1 -> 0/1`, `pedShadows = -1 -> 0/1` | Separates shadow *geometry* sources if T1 is inconclusive. |
| T3 | `defaultCbShaderHygiene = 1 -> 0` | Confirms/denies the building decal alpha-block repair path (`buildingPipe.cpp:531`); if the black pools change, Issue 1 is in that path. |
| T4 | `cloudRenderFix = 1 -> 0` | Cloud render-state hygiene (`main.cpp:4321`). Tests cloud blend/state vs cloud geometry. |
| T5 | `disableClouds = 0 -> 1` | Removes the cloud layer entirely -> proves whether IMG 3's giant quads are clouds and whether they affect the near-field. |
| T6 | `pipeline = Xbox -> PBR`; also `buildingPipe`, `vehiclePipe`, `waterStyle` individually | Tests the user's hypothesis directly. INI currently says Xbox; if PBR changes the black cars/decals, the Xbox sub-pipes are the broken ones (not a mode-apply failure). |
| T7 | `waterStyle = Xbox -> IV/GTAIV/V` (allowed values per `main.cpp:6172`) | Isolates the flat-blue pond band + black blobs at the shoreline. |
| T8 | `forwardPlusEnable = 1 -> 0` | Isolates black vehicles / dark vegetation if lighting is Forward+ driven. |
| T9 | `colorFilterEnable = 0 -> 1` with `colorFilter = PC` | Rules out postfx colour crush as the cause of near-black foliage/cars. |
| T10 | `depthHookEnable = 1 -> 0` | Tests draw-order/depth interaction of decals vs water (Issue 1's "decal over water" half). |

Cross-check with the debug menu live toggles (`src/core/debugmenu_ui.cpp:1055`, `:1624`, `:1633`) — `cloudRenderFix`, `disableClouds`, `fixShadows` are exposed there.

---

**Bottom line:** the pixels support a **decal alpha/blend failure** (solid (2,2,2) black pools with texel-stepped cutout edges, over both road and water) plus a **cloud-quad projection/scale failure** (huge flat grey quads at near-camera altitude, absent elsewhere). The ground is **not** transparent. No closed mesh is proven inside-out. The INI proves Xbox mode *is* selected, so a "mode did not apply" story needs a runtime routing proof (T6), not a config explanation. `fixShadows=0` is the cheapest first A/B for the shadow half of the decal problem.

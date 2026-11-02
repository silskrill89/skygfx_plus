# Issue registry — 2026-10-24 baseline

Stable IDs for the 24-shot run of 2026-10-24. Each entry is tied to the tested binary in [README.md](README.md) and to the images in [images.md](images.md). Do not renumber IDs; add new ones for new artifact classes.

**Status vocabulary**

| Status | Meaning |
|---|---|
| `OPEN` | No fix in the tested build; the artifact is live. |
| `FIX-PENDING-BUILD` | A fix exists in the working tree but is not in a tested binary. |
| `FIXED-UNVERIFIED` | A build contains the fix, but the visual check has not passed. |
| `VERIFIED-FIXED` | The verify instruction passes on a named build. |

**Severity:** 3 = major (player/vehicle/water rendering failure); 2 = clearly visible, affects readability or immersion; 1 = cosmetic.

## Summary

| ID | Title | Sev | Frequency | Status |
|---|---|---|---|---|
| BASE-01 | CJ body fully absent | 3 | 12/24 — #02–#07, #09–#14 | `OPEN` |
| BASE-02 | CJ/ped partial, clipped or transparent | 3 | 5/24 — #15, #16, #21, #23, #24 | `FIX-PENDING-BUILD` |
| BASE-03 | Near-camera peds render as flat black silhouettes | 2 | 5/24 — #09, #15, #16, #17, #23 | `OPEN` |
| BASE-04 | Stepped translucent water veil over land/sky | 3 | 14/24 — #01–#10, #13, #14, #19, #20 | `FIX-PENDING-BUILD` |
| BASE-05 | Water flat/untextured | 2 | 7/24 — #01–#04, #06–#08 | `OPEN` |
| BASE-06 | Vehicle translucency, dissolve and mirror-chrome shells | 3 | 7/24 — #09, #11–#16 | `FIX-PENDING-BUILD` |
| BASE-07 | Vehicle panels detached/floating; cars hover | 3 | 8/24 — #10–#17 | `OPEN` |
| BASE-08 | Black car interiors / opaque glass voids | 2 | 3/24 — #10, #11, #13 | `OPEN` |
| BASE-09 | Black shadow-blob sprites / shadow acne | 2 | 12/24 — #09, #11–#14, #17–#23 | `OPEN` |
| BASE-10 | Floating world geometry, props and vegetation | 2 | 6/24 — #10, #13, #14, #16, #17, #21 | `OPEN` |
| BASE-11 | Z-fighting / "double ground" | 2 | 3/24 — #04, #05, #06 | `OPEN` |
| BASE-12 | Debug banner "Earshot miniaudio v0.1.0-alpha2 loaded" (external plugin) | 1 | 1/24 — #24 | `OPEN` |
| BASE-13 | Minimap label crowding | 1 | 23/24 — #01–#23 | `OPEN` |
| BASE-14 | Sky streaks / vertical dark columns | 1 | 1/24 — #06 | `OPEN` |
| BASE-15 | Untextured bright red cube sprites | 2 | 2/24 — #12, #21 | `OPEN` |
| BASE-16 | Washout / haze / pastel grading + stepped sky banding | 2 | 23/24 — #01–#23 | `OPEN` |

---

## BASE-01 — CJ body fully absent

**Severity:** 3 · **Frequency:** 12/24 — #02, #03, #04, #05, #06, #07, #09, #10, #11, #12, #13, #14 · **Status:** `OPEN`

**Visual:** No CJ mesh, no CJ shadow, no CJ-shaped hole, no hands/weapon. The failure splits into six on-foot Ocean Docks shots (#02–#07) and six in-vehicle Ganton shots (#09–#14). In #05 a small black blob is clipped by the bottom edge and is most likely the top of the head directly under the camera, so the camera is at player position but the body is not rasterised. The in-vehicle shots show a chase/top-down camera with no driver visible through the glass; because the cabin is dark or opaque in most of them, "absent" there means "no driver visible", while #13 is the clearest case (see-through roof, empty interior).

**Evidence:** `gta_sa_YGKAtsKnOl.png`, `gta_sa_lUcuyObJQq.png`, `gta_sa_WAvyJqbueG.png`, `gta_sa_q0CvX0KDfy.png`, `gta_sa_EvO5AcpKI4.png`, `gta_sa_dKHtwC2RN7.png`, `gta_sa_6zMDt8AZMR.png`, `gta_sa_rjh4B7XZej.png`, `gta_sa_gMYI2HSyL3.png`, `gta_sa_1MQ9wiuh3D.png`, `gta_sa_1uDM0MkKXP.png`, `gta_sa_ruPImT2z13.jpg`.

**Suspected subsystem:** Ped/default-pipeline draw (`myDefaultCallback`, `src/core/main.cpp:854-1015`) and/or the game skin-atomic submission. The cited diagnosis docs do not establish a root cause for a *fully missing* mesh. The ped opacity contract targets blend/alpha state, not a mesh that is never submitted; treat BASE-01 as a separate question from BASE-02. The ped-contract build is still the first one to test: if CJ appears there, reclassify this issue as fixed by that change, but do not assume it beforehand.

**Verify by:** Stand in third-person daylight; CJ's mesh **and** shadow must both appear (not just a camera at his position). Capture `[PedOpaque] pre-entry` and confirm the skin atomic is submitted; if the log shows the ped draw running but nothing is rasterised, escalate as a transform/culling fault.

---

## BASE-02 — CJ/ped partial, clipped or transparent

**Severity:** 3 · **Frequency:** 5/24 — #15, #16, #21, #23, #24 · **Status:** `FIX-PENDING-BUILD`

**Visual:** Only part of the body is visible: a large unlit black foreground silhouette (#15), a cropped dark figure at the frame edge (#16), head/upper body behind a floating bush (#21), torso swallowed by a shadow smear (#23), legs only below a translucent interior plane (#24). In #21 the body reads overlapped/translucent rather than simply occluded.

**Evidence:** `gta_sa_kUXHm8iMMp.png`, `gta_sa_KQHNZiyjU6.jpg`, `gta_sa_DaFLAtbaWa.png`, `gta_sa_lEIspQcqHJ.png`, `gta_sa_MjwjRPTdUD.png`.

**Suspected subsystem:** Ped alpha/blend contract plus the `ALPHAREF` leak:

- Ped opaque contract: `src/core/main.cpp:854-1015` — capture `pedOpaqueCapture` at 883, divergence check at 942, enforcement at 957, restore at 988; gated by `pedOpaqueContract` (`main.cpp:4885`, default 1).
- Outdoor-world `ALPHAREF` leak: `src/render/envmap.cpp:349-375` — the reflection/world pass parks `ALPHAREF=140` and restores the pre-entry value at 373-375. The comment at 349-354 states the exact failure: a leaked 140 cuts ped skin/clothing into holes.

**Fix reference:** working-tree fixes at the lines above; neither is in the tested 21:02:54 binary. Marked `FIX-PENDING-BUILD`.

**Verify by:** At close range in daylight, CJ's torso/head/clothing must be fully opaque with no holes. Log must show `[PedOpaque] pre-entry ... ALPHAREF=1` (not 140) and no repeated `[PedOpaque] contract repaired divergent entry`.

---

## BASE-03 — Near-camera peds render as flat black silhouettes

**Severity:** 2 · **Frequency:** 5/24 — #09, #15, #16, #17, #23 · **Status:** `OPEN`

**Visual:** Peds within a few metres lose all skin/clothing colour and read as solid black cut-outs: CJ's black foreground silhouette (#15), two black peds on the left pavement (#17), a black head at the left edge (#09), a cropped dark figure (#16), CJ's torso inside the shadow smear (#23). Distant peds (#11, #19, #20, #22) are lit and coloured. #21 has CJ partly shadowed, but the red-trouser ped on the right is lit.

**Evidence:** `gta_sa_6zMDt8AZMR.png`, `gta_sa_kUXHm8iMMp.png`, `gta_sa_KQHNZiyjU6.jpg`, `gta_sa_FuQ3QJWYvJ.png`, `gta_sa_lEIspQcqHJ.png`.

**Suspected subsystem:** Ped default-pipeline lighting/ambient, not the alpha contract. The ped contract fixes blend/alpha holes, not unlit shading; the cited diagnosis docs do not give a root cause.

**Verify by:** Stand within ~10 m of a ped in daylight; skin and clothing must show colour, not a flat black silhouette. Re-check after the ped-contract build; if the silhouette remains, treat it as a lighting/ambient issue, not opacity.

---

## BASE-04 — Stepped translucent water veil over land and sky

**Severity:** 3 · **Frequency:** 14/24 — #01–#10, #13, #14, #19, #20 · **Status:** `FIX-PENDING-BUILD`

**Visual:** Hard-edged, stepped translucent blue-grey sheets tile the frame. In the dock shots they cover water, sand and the sky band; the sheet's top edge is a near-horizontal line well above the ship's waterline. Inland (#09, #10, #13, #14) the same pale/dark sheet lies over dry roads with no water present. In #19/#20 it appears as a thin dark horizontal band at fence height rather than a full-frame sheet. The edges are straight/quantised, not terrain LOD rings or coastline contours. #01 also has two black horizontal bar artifacts at the left waterline.

**Evidence:** `gta_sa_pOnqy1KG59.png`, `gta_sa_YGKAtsKnOl.png`, `gta_sa_lUcuyObJQq.png`, `gta_sa_WAvyJqbueG.png`, `gta_sa_q0CvX0KDfy.png`, `gta_sa_EvO5AcpKI4.png`, `gta_sa_dKHtwC2RN7.png`, `gta_sa_bddgNSYQlD.png`, `gta_sa_6zMDt8AZMR.png`, `gta_sa_rjh4B7XZej.png`, `gta_sa_1uDM0MkKXP.png`, `gta_sa_ruPImT2z13.jpg`, `gta_sa_nX69IBmwFU.png`, `gta_sa_ALvawpNUu8.png`.

**Suspected subsystem:** Water pipe WVP from the RW cached view-projection. The water draw nulls `D3D9ActiveTransform` but not `_RwD3D9ActiveViewProjTransform` (`waterPipe.cpp:1069-1083` in the diagnosis), so `_rwD3D9VSGetComposedTransformMatrix` can return a reflection-camera matrix; correct world-space water quads are projected to the wrong clip-space position. Z-test is pinned on and tier 0 never runs the depth copy (water-veil diagnosis §3–§4). The sky band is the far boundary of the same misplaced mesh, not a separate atmosphere effect.

**Fix reference:** `src/render/waterPipe.cpp:1117-1121` forces `_RwD3D9ActiveViewProjTransform = NULL` before the recompute (the diagnosis §6.A fix, now in the working tree). Not in the tested 21:02:54 binary. Marked `FIX-PENDING-BUILD`.

**Verify by:** First A/B `waterParallaxEnable=0` (the hook then calls the vanilla `CWaterLevel::RenderAndEmptyRenderBuffer`). On the fix build, stand on the dock/beach and confirm no sheet over dry land or above the horizon; water must stay below the horizon and behind nearer geometry. Capture `[water] entry`, `[water] chain`, and the one-shot `[water] water_state pre-draw`.

---

## BASE-05 — Water flat/untextured

**Severity:** 2 · **Frequency:** 7/24 — #01, #02, #03, #04, #06, #07, #08 · **Status:** `OPEN`

**Visual:** All visible water is a flat blue/teal polygon with no wave normals, foam, sparkle or readable reflection. Long tan streaks and translucent angular shards sit above it (#08); the dock water is a grey-blue plane under the veil (#02–#04, #07). #05 is excluded because it has no distinct water surface.

**Evidence:** `gta_sa_pOnqy1KG59.png`, `gta_sa_YGKAtsKnOl.png`, `gta_sa_lUcuyObJQq.png`, `gta_sa_WAvyJqbueG.png`, `gta_sa_EvO5AcpKI4.png`, `gta_sa_dKHtwC2RN7.png`, `gta_sa_bddgNSYQlD.png`.

**Suspected subsystem:** Water_Parallax tier 0. The run log shows `waterStyle=0`, `tier=0`, `needRT=0`, `reflRT=none`, `reflFlag=0` on all 62 flushes (water-veil diagnosis §2), so the reflection/scene RT path is not active and the surface is the flat tier-0 composite. This may improve after BASE-04 but is a separate visual target.

**Verify by:** The sea should show wave/detail variation and a horizon/reflection gradient, not one flat colour. Use `waterStyle`/`waterQuality` only as diagnostics; do not raise `waterQuality` to 2/3 as a "fix" because that enables the depth/scene copies and changes the path (water-veil diagnosis §6.F).

---

## BASE-06 — Vehicle translucency, dissolve and mirror-chrome shells

**Severity:** 3 · **Frequency:** 7/24 — #09, #11, #12, #13, #14, #15, #16 · **Status:** `FIX-PENDING-BUILD`

**Visual:** Player-near cars lose opacity or material identity: the green sedan dissolves into a dust cloud with angular chunks (#09), body panels read as mirror-chrome slabs (#11, #12, #14, #15, #16), the roof/glass is see-through (#13), and the rear panels expose inner geometry. Distant traffic (#19, #20, #22) is opaque and correct.

**Evidence:** `gta_sa_6zMDt8AZMR.png`, `gta_sa_gMYI2HSyL3.png`, `gta_sa_1MQ9wiuh3D.png`, `gta_sa_1uDM0MkKXP.png`, `gta_sa_ruPImT2z13.jpg`, `gta_sa_kUXHm8iMMp.png`, `gta_sa_KQHNZiyjU6.jpg`.

**Suspected subsystem:** Per-mesh texture-alpha classification in the vehicle PBR callback (`src/render/vehiclePipe.cpp:2229-2302`; glass classification at 2312-2329). `TAC_FADED` false positives and `TAC_UNKNOWN` + `vertexAlpha` can move solid body panels onto the transparent pass; glass promotion can take a body texture down the glass path.

**Fix reference:** Working-tree texalpha fix:

- `src/render/texalpha.cpp:71` — `#define TAC_FADED_PERCENT 60`; classification at `texalpha.cpp:242-251` (FADED only when ≥60% of sampled texels are intermediate).
- Cache/starvation fix — `src/render/texalpha.cpp:25-37` (1024 sets × 4 ways, 32-new-textures-per-frame budget, no per-frame re-probe) and the constants at `texalpha.cpp:49-54`.

Not in the tested 21:02:54 binary. Marked `FIX-PENDING-BUILD`.

**Verify by:** A parked car in daylight must keep solid paint and panels; only glass/decals may blend. Capture `[VehOpacity] tex=... tac=... blendOn=...` and the `texalpha:` summary; body textures should report `tac=1 (OPAQUE)` / `blendOn=0`.

---

## BASE-07 — Vehicle panels detached/floating; cars hover

**Severity:** 3 · **Frequency:** 8/24 — #10, #11, #12, #13, #14, #15, #16, #17 · **Status:** `OPEN`

**Visual:** Fenders, bumpers, trunk lids, door panels and wheels detach from the body and float beside/behind it; the car may hover above the road with no contact shadow (#11–#17). #17's green car floats at head height and a motorcycle floats over the street. This is geometry/transform, not just opacity. The #09 dissolve is tracked separately as BASE-06.

**Evidence:** `gta_sa_rjh4B7XZej.png`, `gta_sa_gMYI2HSyL3.png`, `gta_sa_1MQ9wiuh3D.png`, `gta_sa_1uDM0MkKXP.png`, `gta_sa_ruPImT2z13.jpg`, `gta_sa_kUXHm8iMMp.png`, `gta_sa_KQHNZiyjU6.jpg`, `gta_sa_FuQ3QJWYvJ.png`.

**Suspected subsystem:** Vehicle atomic transform / per-mesh draw. No root cause in the cited diagnosis docs. The same build has a proven clip-space transform fault in the water pipe (BASE-04), so a shared transform/culling fault is plausible but unproven.

**Verify by:** A parked car must have all panels attached and wheels on the ground; no parts floating beside the body. Compare near vs distant traffic in the same shot.

---

## BASE-08 — Black car interiors / opaque glass voids

**Severity:** 2 · **Frequency:** 3/24 — #10, #11, #13 · **Status:** `OPEN`

**Visual:** Through the rear/side glass the cabin is a pure black void; in #13 the roof is see-through and the interior is mirrored/black, with no driver. The exterior may be intact around the void. Distant cars are not affected in the same way.

**Evidence:** `gta_sa_rjh4B7XZej.png`, `gta_sa_gMYI2HSyL3.png`, `gta_sa_1uDM0MkKXP.png`.

**Suspected subsystem:** Vehicle glass/interior path (`VehShaders_IsGlassTexture` / glass classification at `src/render/vehiclePipe.cpp:2312-2329`). No root cause in the cited diagnosis docs.

**Verify by:** Look through the side/rear glass in daylight; seats and the driver silhouette should be visible, not a black rectangle.

---

## BASE-09 — Black shadow-blob sprites / shadow acne

**Severity:** 2 · **Frequency:** 12/24 — #09, #11, #12, #13, #14, #17, #18, #19, #20, #21, #22, #23 · **Status:** `OPEN`

**Visual:** Huge black, irregular blobs lie on roads, grass and pavements; they do not follow the object that casts them and sometimes cover CJ (#23). In #11/#12 they appear as long black rectangular streaks; in #18–#22 as large soft-edged pools. They are distinct from normal contact shadows.

**Evidence:** `gta_sa_6zMDt8AZMR.png`, `gta_sa_gMYI2HSyL3.png`, `gta_sa_1MQ9wiuh3D.png`, `gta_sa_1uDM0MkKXP.png`, `gta_sa_ruPImT2z13.jpg`, `gta_sa_FuQ3QJWYvJ.png`, `gta_sa_6ZA3ObVBe8.png`, `gta_sa_nX69IBmwFU.png`, `gta_sa_ALvawpNUu8.png`, `gta_sa_DaFLAtbaWa.png`, `gta_sa_27C5PP7ouy.png`, `gta_sa_lEIspQcqHJ.png`.

**Suspected subsystem:** Shadow sprite / PipeChain pass 3. The corona-blackquad diagnosis §4 names PipeChain pass 3 as the prime suspect for black ink-blot pools: `src/render/postfx.cpp:3212-3216` documents the failure mode, and the pass itself is at `postfx.cpp:3462-3525`. The doc prescribes the A/B `pipeChainEnable=0`.

**Verify by:** Shadows must be attached and soft; no giant black pools on the ground. Run once with `pipeChainEnable=0` and compare; if the blobs vanish, the pass owns them. If they remain, the next suspect is the shadow-sprite path, not the post chain.

---

## BASE-10 — Floating world geometry, props and vegetation

**Severity:** 2 · **Frequency:** 6/24 — #10, #13, #14, #16, #17, #21 · **Status:** `OPEN`

**Visual:** Two chain-link fence panels hang in mid-air (#10); grey slabs/plates float beside the car (#13, #14); black vegetation sprites float near the centre (#16); a green car hovers above CJ and a motorcycle is suspended over the street (#17); a bush sprite sits over CJ's head (#21). #23 has a possible black disc above CJ's head; #07 has a ped standing on the ship's top deck (placement unconfirmed).

**Evidence:** `gta_sa_rjh4B7XZej.png`, `gta_sa_1uDM0MkKXP.png`, `gta_sa_ruPImT2z13.jpg`, `gta_sa_KQHNZiyjU6.jpg`, `gta_sa_FuQ3QJWYvJ.png`, `gta_sa_DaFLAtbaWa.png`.

**Suspected subsystem:** Per-atomic world transform/culling. No root cause in the cited diagnosis docs. The water clip-space fault (BASE-04) proves one transform fault in this build; a shared cause is plausible but not established.

**Verify by:** Fence panels must stay connected to posts; vehicles/props must sit on the ground; vegetation sprites must be anchored to their plants. Check whether the same atomic moves with the camera (screen-space fault) or stays in the world (transform fault).

---

## BASE-11 — Z-fighting / "double ground"

**Severity:** 2 · **Frequency:** 3/24 — #04, #05, #06 · **Status:** `OPEN`

**Visual:** Grass/rock textures appear duplicated and offset under the translucent sheet (#04, #06); in #05 an opaque brown-grey terrain slab with hard edges is drawn in front of the camera, covering the lower half of the frame. The "double ground" may be the misplaced sheet exposing a second layer rather than classic depth fighting.

**Evidence:** `gta_sa_WAvyJqbueG.png`, `gta_sa_q0CvX0KDfy.png`, `gta_sa_EvO5AcpKI4.png`.

**Suspected subsystem:** Misplaced water/terrain sheet (BASE-04) plus terrain LOD; no separate root cause established.

**Verify by:** Ground must resolve to one layer with no offset duplicate textures; the camera must not end up inside an opaque terrain slab. Re-check after the BASE-04 fix before opening a separate depth issue.

---

## BASE-12 — Debug banner "Earshot miniaudio v0.1.0-alpha2 loaded" (external plugin)

**Severity:** 1 · **Frequency:** 1/24 — #24 · **Status:** `OPEN`

**Visual:** A white debug string is centred across the bottom of the interior shot: `Earshot miniaudio v0.1.0-alpha2 loaded`. It is a third-party audio-plugin banner, not a SkyGfx render artifact.

**Evidence:** `gta_sa_MjwjRPTdUD.png`.

**Suspected subsystem:** Earshot miniaudio ASI/plugin (external). Not in the SkyGfx source; no file:line.

**Verify by:** Banner gone after disabling/updating the Earshot plugin. No SkyGfx change is required; do not count this against the renderer.

---

## BASE-13 — Minimap label crowding

**Severity:** 1 · **Frequency:** 23/24 — #01–#23 (#24 shows only `N`) · **Status:** `OPEN`

**Visual:** Several white map labels (`CJ`, `BS`, `R`, `Cv`, `N`) overlap each other and the radar edge; worst in the Ocean Docks shots where 3–4 labels stack at the left edge. The radar blips/roads remain readable underneath.

**Evidence:** all images #01–#23; worst: `gta_sa_pOnqy1KG59.png`, `gta_sa_YGKAtsKnOl.png`, `gta_sa_lUcuyObJQq.png`, `gta_sa_WAvyJqbueG.png`.

**Suspected subsystem:** HUD/radar label rendering (zone/legend text). Not diagnosed; no file:line established.

**Verify by:** Labels should not overlap each other or the radar rim. Check whether the crowding follows the player's discovered-zone state or a HUD scale setting; record both.

---

## BASE-14 — Sky streaks / vertical dark columns

**Severity:** 1 · **Frequency:** 1/24 — #06 · **Status:** `OPEN`

**Visual:** Faint dark vertical smears/columns appear in the upper-left sky of the hillside shot, like static or corrupted impostor cards. They are separate from the diagonal translucent sheet.

**Evidence:** `gta_sa_EvO5AcpKI4.png`.

**Suspected subsystem:** Corrupted vegetation/impostor cards (catalogue hypothesis); not confirmed. Could also be distant geometry seen through the sheet.

**Verify by:** Sky should be clean above the horizon. If the streaks persist, toggle vegetation/impostor rendering or capture the same angle with `waterParallaxEnable=0` to separate water from vegetation.

---

## BASE-15 — Untextured bright red cube sprites

**Severity:** 2 · **Frequency:** 2/24 — #12, #21 · **Status:** `OPEN`

**Visual:** A solid, untextured red cube/box floats in the world: one at the right edge in #12, one full cube plus a clipped fragment in #21. The faces are flat red with a dark edge; no texture or lighting gradient.

**Evidence:** `gta_sa_1MQ9wiuh3D.png`, `gta_sa_DaFLAtbaWa.png`.

**Suspected subsystem:** Marker/effect sprite or untextured Im2D/3D geometry. The yellow marker path (`0x60BA80`, corona-blackquad diagnosis Q4) is a known separate path, but the red cubes are not the yellow marker; no root cause established.

**Verify by:** No solid red boxes in the world. If present, capture `CoronaDraw`/`[CoronaShield]` and note whether they persist with `pipeChainEnable=0`; that separates the sprite batch from the post chain.

---

## BASE-16 — Washout / haze / pastel grading + stepped sky banding

**Severity:** 2 · **Frequency:** 23/24 — #01–#23 (#24 interior excluded) · **Status:** `OPEN`

**Visual:** Outdoor shots are pale, low-contrast and pastel, with blacks lifted and colours desaturated; the sky shows hard horizontal bands and stepped rectangles rather than a smooth gradient. In the dock shots the sky band may be the far edge of the misplaced water mesh (BASE-04); inland shots show the same banding with no water, so a separate grading/sky cause is likely.

**Evidence:** all outdoor images #01–#23; representative: `gta_sa_6ZA3ObVBe8.png`, `gta_sa_ALvawpNUu8.png`, `gta_sa_27C5PP7ouy.png`, `gta_sa_pOnqy1KG59.png`.

**Suspected subsystem:** Colour filter / tonemap / PipeChain pass 3 grading. Candidate: the pass-3 re-presentation (`src/render/postfx.cpp:3462-3525`; the corona-blackquad diagnosis §4 describes the pass). The cited diagnosis docs do not give a root cause for the washout specifically.

**Verify by:** Compare a daylight shot before/after `pipeChainEnable=0`; contrast and sky gradient should be smooth, with no hard bands. If the banding remains, split it from the water/sky path and open a separate sky issue.

---

## Verification log

Add one row per check. Do not overwrite old rows. Build identity must include size and compile stamp.

| Date | Build (size / stamp) | Issue | Result | Evidence | Tester |
|------|----------------------|-------|--------|----------|--------|
|  |  |  |  |  |  |

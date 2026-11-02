# Interior vs Exterior Forensics — 2026-10-26 04:10–04:30 window

**Source directory:** `C:\Users\aaaaaaaaa\Documents\ShareX\Screenshots\2026-10`
**Window:** 2026-10-26 04:10:00 – 04:30:59 (mtime, local)
**Tester report (verbatim):** "game still looks liek this when you load save, can you comprhendisvely dfigure out whats causing it ... you figure out what could be on the intertior side thats not on the exterior pirpe thast causing it ... as you see mirror reflections work coreclty, byut the other cartiants aint."

**Reference (load-save spawn, CJ's house, in-game 10:11, ~1920x1080):** room almost black; walls/floor/furniture barely lit; large pale grey/white slab spans room mid-height in front of camera, occluding furniture and CJ torso; bright yellow-green triangle in doorway; black potted plant and black stove visible only as silhouettes; CJ shows only jeans + shoes; wooden table edge bottom-left; HUD/radar normal.

## Corpus in the 20-minute window

mtime filter (python `os.listdir` + `getmtime`) returns **19 files**:

- **18 game frames, all `gta_sa_*`, all 1920x1080, all interiors of the Johnson house:** `gta_sa_Mm5FXg2vji.png` 04:22:55, `gta_sa_hbmWfDJwXp.png` 04:23:36, `gta_sa_fQxAMyTz3y.jpg` 04:23:38, `gta_sa_Ew1nMAorAf.png` 04:23:39, `gta_sa_0EG5A2e6mm.png` 04:23:40, `gta_sa_9AgfsAmmCq.png` 04:23:42, `gta_sa_h9gDMdEoYd.png` 04:23:43, `gta_sa_2mcjUQlr2Q.png` 04:23:44, `gta_sa_d7FYN1Y5MW.png` 04:23:51, `gta_sa_EPLAU8lt57.png` 04:24:26, `gta_sa_AInxBNbBlE.jpg` 04:24:27, `gta_sa_tf7Hd8cI9b.png` 04:24:38, `gta_sa_qM8zToGQ00.png` 04:24:40, `gta_sa_CorRe3YiQF.png` 04:24:44, `gta_sa_HoceCzem28.png` 04:24:49, `gta_sa_onlUOyJMCt.jpg` 04:24:53, `gta_sa_VTp89unGKj.jpg` 04:25:00, `gta_sa_zCcOBQVORv.png` 04:25:05.
- **1 app capture:** `waterfox_CDkbRknQv1.png` 04:14:20 — a browser/YouTube thumbnail ("DOOM CITY — Can we turn DOOM into GTA 6 with AI?"), not a game frame.

**No exterior/outdoor frames exist in this window.** Every game frame is inside the Johnson house: bedroom (1–8, 10, 11, 14), living room (9), stairwell (12), doorway (15), kitchen (16, 17), upstairs hall/floor (18). Consequence: the earlier outdoor defects (face inversion, see-through ground, CJ ring) cannot be confirmed or killed from this set. The reference frame is **#1** (clock 10:11).

Luma = `0.2126R + 0.7152G + 0.0722B`; 5x5 statistics are median RGB and σ of the 25 luma values; all coords are native 1920x1080.

## Per-image table

| # | file (mtime) | clk | I/E | pitch | exposure mean/p50/%<8 | slab = bed sheet (bbox; 5x5; darkest) | marker (bbox; core 5x5) | CJ drawn / darkest part | (d) NPC/veh; (e) outdoor |
|---|--------------|-----|-----|-------|----------------------|-----------------------------------------|--------------------------|--------------------------|--------------------------|
| 1 | `gta_sa_Mm5FXg2vji.png` 04:22:55 | 10:11 | Int bedroom | −8° | **10.3 / 9.9 / 30.5%** | YES (0,540)-(1600,810); (29,27,20) σ2.0 @708,557; (2,2,2) σ0 @1019,557; 55% of ROI <8; plain sheet, **no UV stretch** | (919,371)-(1023,531); (58,58,2) σ0 @970,450; mask σ3.5 | **jeans+shoes only; torso/head NOT drawn** (×12 lift: bed continuous, no silhouette); jeans (8,8,5) σ0.1 @880,900 | none; HUD max **55** = fade ×0.23; no outdoor |
| 2 | `gta_sa_hbmWfDJwXp.png` 04:23:36 | 10:14 | Int (bedroom) | n/a | 4.2 / 2.0 / **98.4%** | world black; no slab | (956,351)-(1070,526); (255,255,2) σ0 @1012,409; mask σ14.9 | none drawn | none; HUD 240, **radar empty** (N/arrow only) → world/radar dropout |
| 3 | `gta_sa_fQxAMyTz3y.jpg` 04:23:38 | 10:16 | Int bedroom | −15° | 53.9 / 43.9 / 11.1% | YES (150,720)-(1920,1080); (98,94,94) σ2.7 @607,795; (3,1,2) σ0.5 @970,978; 38% <8 | (1391,362)-(1571,617); (226,227,3) σ0.4 @1477,447; mask σ14.8 | torso/head visible above bed; shirt (150,153,168) σ0.9 @960,640; skin (72,48,36) σ2.5 @1040,620 | none; JPG; no outdoor |
| 4 | `gta_sa_Ew1nMAorAf.png` 04:23:39 | 10:17 | Int bedroom | −10° | 44.6 / 27.5 / 11.2% | YES (0,570)-(1130,1080); (98,95,94) σ0.5 @82,662; (2,2,2) σ0 @94,652; 7% <8 | (800,300)-(961,548); (255,255,2) σ0 @881,383; mask σ14.3 | head/shoulders above bed; head (62,62,64) σ1.0 @840,520; bandana (62,61,64) σ0.4 @860,560 | none; no outdoor |
| 5 | `gta_sa_0EG5A2e6mm.png` 04:23:40 | 10:18 | Int bedroom | −5° | 44.4 / 24.1 / 10.2% | YES (0,600)-(1050,1080); (155,157,160) σ1.0 @873,925; (2,2,2) σ0 @82,703; 6% <8 | (696,321)-(862,574); (255,255,2) σ0 @780,405; mask σ14.0 | head/shoulder close-up; skin (39,28,21) σ0.6 @1180,700; bandana (11,24,11) σ0.4 @1120,530 | none; no outdoor |
| 6 | `gta_sa_9AgfsAmmCq.png` 04:23:42 | 10:20 | Int bedroom | 0° | 34.6 / 27.2 / 3.4% | bed corner (0,790)-(470,1010); (67,65,65) σ1.4 @200,900; (2,2,2) σ0 @263,816; radar overlaps; **no slab** | (375,440)-(557,700); (255,255,2) σ0 @470,528; mask σ12.8 | upper body; tank (140,143,148) σ0 @950,1050 / (39,40,43) σ0 @1050,900; skin (27,18,15) σ5.7 @1250,810; bandana (42,57,39) σ15.2 @1075,670 | none; wall (31,28,23) σ0 @1632,576 |
| 7 | `gta_sa_h9gDMdEoYd.png` 04:23:43 | 10:21 | Int bedroom | 0° | 43.4 / 36.5 / 1.5% | bed corner (0,470)-(290,720); sheet (24,21,18) σ0.4 @100,700; wood (184,158,131) σ4.0 @49,523; **no pale slab** | (535,627)-(676,829); (255,255,2) σ0 @608,700; mask σ13.7 | full upper body; tank (173,176,192) σ0 @900,950; skin (72,49,36) σ0.3 @900,800 | none; wall (28,25,23) σ0 @400,300 |
| 8 | `gta_sa_2mcjUQlr2Q.png` 04:23:44 | 10:22 | Int bedroom | **−35°** | 48.3 / 49.6 / 2.6% | YES (250,780)-(1400,1080); (100,98,96) σ0.8 @595,867; (2,2,2) σ0 @542,822; 11% <8 | (961,624)-(1084,807); (255,255,2) σ0 @1022,715; mask σ19.0 | camera inside/behind CJ: hair (18,18,11) σ0.4 @300,200; bandana (28,24,17) σ0.4 @150,500; body clipped | none; **dollhouse view**: near wall/ceiling culled |
| 9 | `gta_sa_d7FYN1Y5MW.png` 04:23:51 | 10:29 | Int living room | 0° | 62.9 / 69.5 / 1.5% | none; wall (77,75,72) σ0.3 @300,200; floor (56,49,47) σ1.2 @700,900 | no yellow marker; red cylinder base ~(90,640)-(520,1080); (212,2,2) σ0.3 @300,850; top (225,2,2) | **direct** CJ dark: shirt (38,39,43) σ0 @955,600; skin (5,11,5) σ0.8 @955,500. **mirror** CJ: shirt (89,90,98) σ2.0 @1045,430; skin (16,22,15) σ3.0 @1040,390; leg (17,23,31) σ1.6 @1045,550 | none; mirror shows CJ + hall; mirror = only "correct" control |
| 10 | `gta_sa_EPLAU8lt57.png` 04:24:26 | 10:39 | Int bedroom | −8° | 50.4 / 40.3 / 3.3% | none; bed/wood (103,84,61) σ20.9 @738,882; floor (28,31,36) σ35.2 @900,950 | (1233,686)-(1362,877); (173,173,2) σ0 @1296,757; mask σ26.6 | CJ back; tank (72,75,87) σ2.0 @850,650; wall (10,20,9) σ0 @950,600 | none; zone text "The Johnson House" |
| 11 | `gta_sa_AInxBNbBlE.jpg` 04:24:27 | 10:41 | Int bedroom | −3° | 70.2 / 53.0 / 3.2% | YES (640,740)-(1140,1020); (219,220,206) σ0.3 @1093,810; (2,2,0) σ0.8 @851,983; 2% <8 | (1317,790)-(1463,997); (213,216,0) σ0.7 @1370,812; mask σ24.4 | full body; tank (147,150,165) @950,600; jeans (45,52,62) @900,750; skin (71,47,35) @1070,700 | none; far wall (184,176,154) σ1.9 @1200,300; bed sheet brightest 219 |
| 12 | `gta_sa_tf7Hd8cI9b.png` 04:24:38 | 10:52 | Int stairwell | 0° | 29.9 / 23.0 / 4.7% | none | none | close-up CJ; tank (142,147,156) @1100,550; skin (90,64,52) σ0.5 @1000,550; bandana (24,49,24) σ0.5 @1000,450; fully drawn | none; wall (38,34,30) σ0.7 @1500,500 |
| 13 | `gta_sa_qM8zToGQ00.png` 04:24:40 | 10:54 | Int (transition) | n/a | **3.1 / 2.0 / 98.8%** | world black; no slab; scattered red/white/blue specks | none | none drawn | none; HUD 240, **radar empty** → transition/streaming |
| 14 | `gta_sa_CorRe3YiQF.png` 04:24:44 | 10:58 | Int bedroom (camera outside) | −20° | 45.2 / 31.2 / 5.1% | bed/sofa (420,900)-(980,1075); (92,90,89) σ1.2 @966,952; (2,2,2) σ0 @461,1016; 29% <8 | (618,694)-(694,809); (255,255,2) σ0 @655,732; mask σ13.3 | CJ dark silhouette (41,42,45) σ1.8 @760,600 | none; **floating bed/table in ceiling** top-left, lamp below; window (166,140,118) σ4.3 @200,100 |
| 15 | `gta_sa_HoceCzem28.png` 04:24:49 | 11:03 | Int door | 0° | 30.9 / 24.0 / 2.4% | none | (1124,369)-(1214,505); (255,255,2) σ0 @1168,414; mask σ12.9 | none | none; door (61,61,61) σ2.4 @1500,700; wall (25,21,15) σ0.8 @800,400 |
| 16 | `gta_sa_onlUOyJMCt.jpg` 04:24:53 | 11:07 | Int kitchen | 0° | 63.9 / 61.5 / 1.7% | none | none | none | none; counter (50,42,24) σ7.7 @300,700; wall (69,66,51) σ1.3 @700,400; plates (65,62,50) σ1.1 @700,480 |
| 17 | `gta_sa_VTp89unGKj.jpg` 04:25:00 | 11:13 | Int kitchen | −20° | 55.0 / 43.1 / 1.2% | none | none | none | none; sink (147,148,143) σ20.7 @1000,800; wall (100,94,70) σ1.1 @1200,300; floor (59,56,47) σ0.8 @700,950 |
| 18 | `gta_sa_zCcOBQVORv.png` 04:25:05 | 11:19 | Int hallway | **−40°** | 42.1 / 34.1 / **18.5%** | none; floor (76,72,72) σ0.5 @1600,300; giant shadow (500,250)-(1500,1000): (2,2,2) σ0 @800,700; (9,9,9) σ0 @1200,900; carpet (70,78,91) σ30.5 @50,1000 | (1761,0)-(1919,160) clipped; (255,255,2) σ0 @1827,56 | none visible (camera down) | none; shadow shapes ~1000 px wide |

## The pale slab

**It is the bed, not a stray quad.** The "slab" is the white bedspread in the bedroom. Evidence from the set:

- It carries bed structure: wooden footboard/headboard in F4 (wood `141,113,72 σ27` @500,300), F8 (bed frame), F11 (frame `161,134,112 σ1.7` @650,850; dresser `161,133,104 σ4.4` @500,700). In F1 the ×12 lift shows a mattress, a pink-trimmed border, and a fold line.
- It behaves as world geometry: bbox changes with camera (F1 `(0,540)-(1600,810)` → F3 `(150,720)-(1920,1080)` → F8 `(250,780)-(1400,1080)` → F11 `(640,740)-(1140,1020)`); it is **occluded by CJ** when he stands behind it (F3/F4/F11), and it is **absent in every room without a bed** (F9 living room, F12 stairwell, F15 door, F16/F17 kitchen, F18 hall). A stray screen-space quad would not do all four.
- **It does not occlude CJ's torso in F1.** CJ's jeans are lower on screen (y 850–1010) than the bed's near edge (y≈810), i.e. CJ is between the camera and the bed. A ×12 linear lift of F1 shows the bed sheet uninterrupted where the torso/head should be — no silhouette, no black cutout. The torso is **not drawn**, not hidden by the slab. In F3/F4/F11 the bed *does* occlude CJ's **lower body** because he is on the far side. The tester conflated "bed behind CJ" with "slab in front of CJ".
- **Texture is not magnified / UV-collapsed.** The bedspread is a plain light fabric with a thin pink border; it has no repeating texel pattern whose scale could be judged. The black shapes on it are **shadows**: soft penumbra, they move with the light/camera, and their interior is `2,2,2`–`6,6,6` while the lit sheet is `60`–`219`. The "stretched" impression comes from shadow blotches 200–1000 px wide (F3, F18), not from texels. By contrast, wallpaper (F3 `96,97,109 σ4.2` @700,380), the fruit painting (F11 `183,177,153 σ3.9` @1240,300), and wood grain are at plausible scale. **No UV magnification evidence.**
- In F1 the slab looks "pale" only because the frame is globally faded: raw slab is `(29,27,20) σ2.0`; ×4.4 (the fade factor) gives `128,119,88` — a normal lit bed. See below.

**Verdict:** the slab is a real bed mesh with an over-dark shadow map. It is **not** a screen-aligned quad, **not** a missing-texture sheet, and **not** the occluder of CJ's torso.

## Interior darkness

### The reference frame is a global fade, not an interior lighting state

F1 is the only 10:11 frame and matches the tester's reference. It is uniformly multiplied by **≈0.23**:

| measurement | F1 raw | F2/F3 normal | ratio |
|---|---|---|---|
| HUD max (top-right block) | **55** | **240** | 0.229 |
| clock text p99 | **45** | **203** | 0.222 |
| money text p99 | **44** | **193** | 0.228 |
| marker core | **58,58,2** | **255,255,2** | 0.227 |
| whole-frame mean | **10.3** | 44.6–53.9 | 0.19–0.23 |

Multiplying F1 by 4.4 gives mean ≈45, p50 ≈44 — matching F4/F5 (`44.6/44.4`). A ×12 linear lift reproduces the tester's description exactly (pale bed, yellow triangle, dark plant/stove silhouettes, jeans-only CJ). The fade dims **HUD+marker+world together**, so it is a full-screen fade/overlay applied after the HUD, not an interior-only ambient collapse. The tester's "HUD/radar normal" is the one inaccurate detail: every HUD pixel in F1 is ×0.23 of the later frames.

F2 and F13 are a **different failure**: world `(2,2,2)`, **HUD 240**, **radar empty** (only N + arrow), marker still drawn. These are world/radar dropouts during a transition (load/teleport/streaming stall or a render-pipe early-out), not fades and not lighting. F2 is 3 in-game minutes after the spawn fade; F13 sits between the stairwell (F12, 10:52) and the bedroom (F14, 10:58), i.e. a scene transition.

### The real dark interiors: low fill + pitch-black shadows

Excluding F1/F2/F13, interior median luma is 23–69 and lit surfaces are 50–100. It is **not** a uniform black room. The defect is selective:

- **Shadowed surfaces go to zero ambient.** The white bedspread: lit `98,94,94` (F3 @607,795) vs shadow `3,1,2` (F3 @970,978); F4 lit `98,95,94` vs `2,2,2`; F5 lit `155,157,160` vs `2,2,2`; F8 lit `100,98,96` vs `2,2,2`; F11 lit `219,220,206` vs `2,2,0`; F18 floor `76,72,72` vs shadow `2,2,2` / `9,9,9`. The shadowed fraction of the bed ROI is 38% (F3), 29% (F14), 11% (F8), 7% (F4), 6% (F5), 2% (F11); F18 is 18.5% of the whole frame. A 50–100:1 lit:shadow ratio on a white sheet is the signature of **ambient being multiplied by the shadow term**, not of a dark albedo.
- **Shadowed walls/rooms are dim but not black.** F6 wall `31,28,23` @1632,576; F7 wall `28,25,23` @400,300; F12 wall `38,34,30` @1500,500; F14 wall `54,48,43` @300,400. Lit walls reach `77,81,95` (F4), `96,97,109` (F3), `184,176,154` (F11). The fill is roughly halved or worse on the shadow side.
- **CJ shows the same split.** F7 CJ is in sun: tank `173,176,192 σ0` @900,950 against a 28-luma wall. F6 CJ is in shade: tank `39,40,43 σ0` @1050,900 (lit edge `140,143,148` @950,1050) against a 31-luma wall. F9 direct CJ (back to camera) shirt `38,39,43 σ0` @955,600, while the **mirror** front shirt is `89,90,98 σ2.0` @1045,430. The ped texture is present; the shadowed side lacks fill.

**Conclusion for the reference:** the room is not "almost black" because the interior pipe has no ambient. The raw F1 is a fade; the lifted F1 is a normally lit room. Persisting after the fade are (a) pitch-black shadow blobs on lit white surfaces and (b) low fill on shadowed walls/backs. Both are real, but neither is "the whole room is black".

## Interior vs exterior

- **No exterior frames exist in the window.** The old outdoor defects (face inversion, see-through ground, CJ ring) are **not observable** here. Nothing in these frames shows a ground hole, an inverted facade, or a world-space ring; the black circle in the corner is the HUD radar (F3/F4/F5/F9/F15/F16), not the CJ ring. This set can neither confirm nor kill the outdoor fixes.
- **The interior-only differences that are visible:**
  1. **Shadow-ambient crush** on interior surfaces (F3/F4/F5/F8/F18). Exteriors have strong direct sun, so the same shader bug is masked outdoors; interiors are mostly shadowed/indirect and collapse.
  2. **Camera/portal culling** lets the view leave the room: F8 looks down into the bedroom with the near wall/ceiling culled (dollhouse), camera inside CJ's head/hair; F14 shows the upstairs bed/table embedded in the ceiling and the lamp below (an impossible composite) while the walls/floor are still drawn. This is an interior-visibility/portal defect, not lighting, and it is a strong candidate for "what the interior pipe does that the exterior pipe doesn't".
  3. **Spawn-time atomics**: F1 draws CJ's jeans+shoes but not torso/head (×12 lift proves the bed is not occluding them). By F3 the torso is present. This is a per-atomic draw/material/streaming state at spawn, independent of the fade.
  4. **F2/F13 world+radar dropout** while HUD/marker still draw — the only "black screen" behavior that is not a fade.
- **Mirror control (F9):** the mirror shows CJ's sunlit front (`89,90,98`) and a bright hallway; the direct view shows his shadowed back (`38,39,43`) and a 77-luma wall. Directionality explains part of the 2.3× difference, so the mirror is **not conclusive proof** of a separate reflection pipeline. It is, however, the best available control: if the mirror pass uses the non-PBR/exterior constants, logging it against the direct pass will show whether the interior path is missing ambient/shadow handling.
- **Marker:** the yellow triangle is **world-anchored**, not screen-anchored: its bbox moves with the camera and scales with distance (F2 115x176 → F14 77x116 → F15 91x137), it is clipped by the frame edge in F18 `(1761,0)-(1919,160)`, and it disappears when out of view (F9/F12/F16/F17). The red cylinder in F9 (`212,2,2 @300,850`) is the same marker's base at close range. Core yellow is `255,255,2 σ0` in most frames; F1 is `58,58,2` only because of the fade; F10 is `173,173,2` in a dim room; the interior is flat/untextured, which matches the vanilla save/mission marker rather than a PBR bug. **Not evidence of a stray overlay** (its bbox does not stay screen-fixed).
- **NPCs/vehicles:** none in any frame. The two human figures in F9 are CJ and his mirror image; F10's "second figure" is CJ's reflection/zone text. No pedestrians, no traffic, no vehicles.

## Ranked defects

1. **Shadow term zeroes ambient — pitch-black blobs on lit interior surfaces.** F3 (38% of bed ROI <8; `3,1,2` vs `98,94,94`), F4 (`2,2,2` vs `98,95,94`), F5 (`2,2,2` vs `155,157,160`), F8 (`2,2,2` vs `100,98,96`), F11 (`2,2,0` vs `219,220,206`), F18 floor (`2,2,2`/`9,9,9` vs `76,72,72`). **Confirm:** disable shadows / force the shadow term to 1 and re-shoot F4/F18; if the blobs lift to ambient (≈30–60) the shadow is multiplying ambient. Also check the shader: ambient must be added *after* (or independently of) the sun shadow. **Kill:** if the blobs stay black with shadows off, they are a decal/texture, not the shadow term.
2. **Interior camera/portal culling — view leaves the room.** F8 (dollhouse; camera in CJ's head/hair `18,18,11 @300,200`), F14 (bed/table embedded in ceiling, lamp below, `34,31,27 @500,100`). F2/F13 may be the same class (world culled to black) or a load/streaming stall; the empty radar favors streaming. **Confirm:** log camera position vs room bounds and portal visibility at F8/F14; disable the interior culling hook and repeat. **Kill:** if the camera is inside the room and the geometry still floats, it is a mesh-transform bug, not culling.
3. **Low interior fill/ambient (separate from the hard shadow).** F6 wall `31,28,23`, F7 wall `28,25,23`, F9 direct CJ back `38,39,43` vs mirror front `89,90,98`, F12 wall `38,34,30`, F14 wall `54,48,43`. **Confirm:** raise the interior ambient/fill by a known factor; if walls lift from 20–30 to 50–80 while the sunlit bed stays ~98, it is fill. Check the interior vs exterior ambient upload (`pAmbient`, c4/c24). **Kill:** if raising ambient has no effect, the walls are vertex-coloured/tex-dark, not under-lit.
4. **CJ torso/head atomics not drawn at spawn.** F1: jeans+shoes only; ×12 lift shows the bed sheet continuous behind the missing torso (no black silhouette); F3 has the full torso. **Confirm:** log ped atomic draw calls and the shader variant for the first 5 s after load; if the torso/head draw is skipped or writes alpha 0, confirmed. **Kill:** if the torso draw runs and the pixels are black (occluding the bed), it is lighting, not a missing draw.
5. **Full-screen fade dims HUD+world (F1).** HUD 55 vs 240; marker 58 vs 255; ×4.4 restores the frame. This explains the tester's "almost black load-save" reference. **Confirm:** capture the same spawn 2 s later; F2's HUD is already 240 while the world is black, so fade and world dropout are separate. **Kill:** if the HUD stays at 55 while the world brightens, it is a post-HUD overlay, not the game fade.
6. **Marker flat/untextured colours.** Yellow `255,255,2 σ0`; red base `212,2,2 σ0.3`. Likely vanilla marker geometry, not a defect. **Confirm:** compare a vanilla save at the same spot. **Kill:** if vanilla shows the same solid red cylinder/yellow arrow, remove from the defect list.
7. **Minor highlight clipping.** F11 bed `219,220,206`; F3 window `192,190,177`; F17 sink `147,148,143`; F5 bed `155,157,160`. Low severity; could be the PBR exposure. **Confirm:** histogram the tonemap output; if values cluster at 255 in the full-res PNG, clamp/tonemap. **Kill:** if the source textures are already near-white, it is albedo, not exposure.
8. **"Pale slab" — not a defect.** It is the bed (bbox/stats above), with no UV magnification and no screen anchoring. Drop this from the bug list.

## Answer to (i)/(ii)/(iii)/(iv)

- **(ii) is killed.** The pale slab is the bed mesh, not a stray large quad. It has a frame/headboard, moves as 3D geometry, is occluded by CJ in F3/F4/F11, and is absent in every room without a bed (F9/F12/F15/F16/F17/F18). It does not occlude CJ's torso in F1 — his jeans are in front of the bed and the torso is simply not drawn.
- **(i) is partly true but not "plain".** There is a real interior fill/ambient deficit (F6/F7/F9/F12/F14) and, more specifically, the shadow term is zeroing ambient on lit surfaces (F3/F4/F5/F8/F18). The reference's near-black room is **not** this: F1 is a ×0.23 global fade (HUD+marker+world together; ×4.4 recovers mean 45, matching F4/F5).
- **(iv) is the best single answer.** The "interior looks wrong on load" is a composite: a load fade (F1), a world/radar dropout during transition (F2/F13), a spawn-time missing CJ torso (F1), interior camera/portal culling (F8/F14), and shadow-ambient crush plus low fill (F3–F8, F14, F18). The slab is not the cause of any of them.
- If forced to choose between (i) and (iv) for the *lighting* alone: **(i) with the qualification that ambient is being killed by the shadow term**, not a uniform ambient collapse.

**What would confirm/kill the answer in one run:** log `pAmbient`/`pDirect` and the shader's ambient/shadow terms in the direct interior pass, the mirror pass, and an exterior control at the same clock; shoot F4/F18 once with shadows off; and capture the same spawn at +2 s and +5 s. If shadows-off lifts the `2,2,2` blobs and the +2 s spawn keeps the HUD at 240 while the world is black, the diagnosis is confirmed: fade + world dropout + shadow-ambient, with the slab exonerated.

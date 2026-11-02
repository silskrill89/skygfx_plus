# Screenshot Forensics — 2026-10-30 test2 build (04:38–04:41 session, 22 shots)

READ-ONLY forensics lane. Companion to the 8 already-reviewed shots (grey mannequin CJ, black
quadrant right-half, water seam, white polygon blob, white wedge at swim, grey wash bottom
third, horizon band, black unlit sedan). This file covers the remaining 22 screenshots from the
same session (`C:\Users\aaaaaaaaa\Documents\ShareX\Screenshots\2026-10\gta_sa_*`).

All shots are Vigilante-mission free-roam in LS (Ganton → Idlewood → beach → marina), 10:12→13:01
in-game. HUD renders correctly in every single shot.

## Per-shot table

| # | File (time) | Scene | Defects | Implicated subsystem |
|---|-------------|-------|---------|----------------------|
| 1 | zejtHZbaHu.png (13:01) | Police cruiser on open ocean | Cruiser pitch-black/unlit; water caustics blocky/low-res with hard pixel edges; flat pale band at horizon (distant water untextured, hard seam vs near textured water) | Vehicle pipe (unlit), water pipe (caustic LOD + far-water plane) |
| 2 | Eh4fPcTzC1.png (12:58) | Flooded beach street, grey sedan | Water sheet extends over land/street (semi-transparent milky slab); black blotchy decals on asphalt (corrupted alpha); hard fog band mid-frame; sedan flat grey unlit | Water pipe (plane over land), decals/alpha (dirt splats), vehicle pipe |
| 3 | y9KyixbsrI.png (13:01) | Car with wing on open ocean | Same as #1: black unlit vehicle, blocky caustics, pale flat horizon band, hard grey strip at bottom edge | Vehicle pipe, water pipe |
| 4 | cNgPWxMpxx.jpg (12:19) | Street, water plane ahead | Giant opaque milky water slab covering street/intersection, zero texture/caustics; tree canopy renders *behind/below* water (draw-order violation); white streaky particle smears on asphalt; dark unlit player car | Water pipe, vegetation draw order, particles, vehicle pipe |
| 5 | JS37tlPSrf.jpg (12:19) | Same, 1s later | Same four defects as #4 | Same |
| 6 | Yu79u1ZKdV.jpg (12:18) | Same drive | Milky water slab to horizon; white streak smoke behind car; smoke sheet drawn *over* windshield (particle/car z-order); unlit black car | Water pipe, particles, vehicle pipe |
| 7 | O1Texx3iG6.jpg (12:17) | Same drive, closer | Water slab shows a **vertical quad seam** (left half grey/pale, right half saturated blue) + razor-straight plane boundary; variable opacity (road visible through left half only); white streaks; unlit car | Water pipe (per-quad shading inconsistency), particles, vehicle pipe |
| 8 | 98ELXckf71.jpg (12:17) | Intersection, green light | Flat pale water slab with hard boundary; hedge row floats at water edge (submerged look); unlit black car; red corona smears left fence line | Water pipe, vegetation placement vs water, vehicle pipe, coronas |
| 9 | tylTwyzBS7.jpg (12:15) | Brick building + wreck | Flat water slab mid-ground with straight edge; **black unlit sedan right (repeat of known symptom)**; flatbed truck dark grey unlit; red coronas on wall | Vehicle pipe, water pipe |
| 10 | ohrWbVxOAh.jpg (12:12) | Burning wagon at intersection | Pitch-black unlit wagon; flat pale-blue water band across background with sharp top edge; HVAC roof boxes render pure black (unlit building props); smoke renders as flat grey sheets | Vehicle pipe, water pipe, building pipe (props), particles |
| 11 | 8FMJirI9vV.jpg (12:05) | Bridge climb, flooded background | **2 grey featureless mannequin peds on left street (repeat of known ped symptom)**; background world flooded under flat pale water; unlit black player car; distant cars dark on water | Ped FFP skin, water pipe, vehicle pipe |
| 12 | FtSdy81OW1.jpg (11:42) | Idlewood rails, smoking towtruck (aerial) | Towtruck near pitch-black; **pure black rectangular chunk** at truck underside; dark-red train car end unlit | Vehicle pipe, black-unlit geometry |
| 13 | cinWBQTKQG.jpg (11:40) | Beach approach | **Neon cyan/white glowing horizon band** spanning top of frame (bright additive streak above buildings/water); maroon car dim/unlit with odd red-tinted rear window | PostFX/atmosphere horizon glow (NEW), vehicle pipe |
| 14 | veTW46ywHl.jpg (11:37) | Downtown LS street | Unlit black player car; red dot coronas at road level; **small black square patch on asphalt**; washed grey flat sidewalk slabs right | Vehicle pipe, decals, pavement material |
| 15 | pGRQwCvDxb.jpg (11:35) | Street with NPC + corpse | Wrecked sedan + black car pitch black; player minivan black. **NPC ped renders CORRECT (textured, shaded)**; blood decals correct | Vehicle pipe (peds OK here — exterior FFP ped works at least once) |
| 16 | 0nd9XtvDhz.jpg (10:47) | Gun ped center, wrecks | **Grey featureless mannequin ped, central and unambiguous (repeat of known symptom)**; wrecked flatbed shows **white/grey faceted polygons** (repeat of white-blob family); black unlit sedans both sides; flat pale water band behind | Ped FFP skin, vehicle pipe (facets + unlit), water pipe |
| 17 | 0phsRcEkVL.png (10:43) | Canal walkway | **Palms/bushes render as pure black jagged silhouettes** (unlit alpha vegetation, left third of frame); row of oversized bright-white coronas along horizon; flat pale canal water; dark unlit road | Vegetation (unlit black — NEW scope), coronas, water pipe |
| 18 | uog8AzB7mS.jpg (10:33) | Marina park | **Giant black wedge/quadrant polygons floating mid-air over scene (repeat of known black-quadrant family, most dramatic instance)**; several palms pure black while neighbours textured (intermittent); city towers dark grey unlit; CJ flat-coloured/partially untextured; white corona rows | Black-unlit polys (shadow/decals or building pipe), vegetation, building pipe, ped skin |
| 19 | b4PcXYc7Va.png (10:28) | Ganton | **FULL-SCREEN BLACKOUT** — entire world black, only HUD + (dim) area text visible. Most severe failure in the set | Global (exterior world render/lighting or present) |
| 20 | lOMemgOPny.jpg (10:27) | CJ house interior | Interior renders CORRECT (walls/floor/furniture textured + lit; CJ body textured: white top, jeans, brown hair) except **green triangle facet over face** (head skin corruption) and dark/black door-jamb slabs by closet (borderline — may be legit dark closet) | Ped skin material (head only) |
| 21 | TrVkpLev7F.png (10:26) | CJ house interior | **CJ's head pure black** (skin/head texture loss; clothes fine); same black door-jamb slabs; interior otherwise correct | Ped skin material (head) |
| 22 | 0jAS4vQrRv.jpg (10:12) | CJ house interior/kitchen | Head occluded by yellow save marker (ambiguous); kitchen/interior fully correct: textures + lighting | None confirmed (interior healthy) |

## Consolidated taxonomy

| Defect family | Shots | Freq | Status |
|---|---|---|---|
| **F1. Unlit black/dark vehicles** (cars, trucks, props on vehicles) | zejtHZbaHu, y9KyixbsrI, Eh4fPcTzC1, cNgPWxMpxx, JS37tlPSrf, Yu79u1ZKdV, O1Texx3iG6, 98ELXckf71, tylTwyzBS7, ohrWbVxOAh, FtSdy81OW1, 8FMJirI9vV, veTW46ywHl, pGRQwCvDxb, 0nd9XtvDhz | **15/22** | Known family (black unlit sedan) — massively dominant |
| **F2. Flat milky water plane** — no texture/caustics, covers land/streets, hard quad boundaries, vertical quad seam, pale horizon band | Eh4fPcTzC1, cNgPWxMpxx, JS37tlPSrf, Yu79u1ZKdV, O1Texx3iG6, 98ELXckf71, tylTwyzBS7, ohrWbVxOAh, 8FMJirI9vV, 0nd9XtvDhz, 0phsRcEkVL, zejtHZbaHu, y9KyixbsrI | **13/22** | Extends known water-seam family; *over-land flooding + vertical quad seam is new scope* |
| **F3. Black unlit polygons/wedges** (mid-air shards, black chunks, black square decals, black vegetation, full blackout) | uog8AzB7mS, 0phsRcEkVL, FtSdy81OW1, veTW46ywHl, b4PcXYc7Va | **5/22** | Known black-quadrant family + NEW sub-symptoms (black palms, full-screen blackout) |
| **F4. White streaky particle smears on asphalt** (exhaust/skid as hard additive streaks; smoke sheets flat) | cNgPWxMpxx, JS37tlPSrf, Yu79u1ZKdV, O1Texx3iG6, 98ELXckf71, ohrWbVxOAh | **6/22** | **NEW family** |
| **F5. Grey featureless mannequin peds (exterior)** | 8FMJirI9vV, 0nd9XtvDhz, uog8AzB7mS (CJ partial) | **3/22** | Known family — repeats; *interiors and one street NPC render fine → exterior FFP ped path only* |
| **F6. Ped head corruption** (green face facet / pure black head) | lOMemgOPny, TrVkpLev7F | **2/22** | **NEW family** (head skin material only; body + interior world healthy) |
| **F7. Neon cyan/white glowing horizon band + oversized white coronas** | cinWBQTKQG, 0phsRcEkVL, uog8AzB7mS | **3/22** | **NEW family** |
| **F8. White faceted polygons on vehicles** | 0nd9XtvDhz (truck facets) | 1/22 | Known white-blob family — repeats |
| **F9. Black blotch decals on asphalt** (corrupted alpha) | Eh4fPcTzC1, veTW46ywHl | 2/22 | **NEW family** (subsumable into F3) |
| **F10. Draw-order: foliage below/behind water plane; particles over car body** | cNgPWxMpxx, JS37tlPSrf, Yu79u1ZKdV | 3/22 | NEW (likely consequence of F2 plane misposition) |

### The 3 most frequent
1. **F1 — unlit black vehicles** (15/22, ~68%)
2. **F2 — flat milky water plane / flood-over-land with quad seams** (13/22, ~59%)
3. **F4 — white streaky particle smears** (6/22) — narrowly ahead of F3 black wedges (5/22)

### NEW families beyond the 8 known symptoms
- F4 white streaky particle smears on road
- F6 ped head corruption (green face facet / black head) in interiors
- F7 neon cyan horizon glow band + oversized white coronas
- F9 black blotch decals (alpha-corrupted dirt/shadow decals)
- F2 scope expansion: water plane flooding *land/streets* with a vertical per-quad shading seam (known seam was ocean-only)
- F3 scope expansion: pure-black palm/bush silhouettes and one **full-screen blackout** (b4PcXYc7Va)

## Do-not-regress (verified correct in these shots)
- HUD/2D layer: icons, clocks, money, wanted stars, tooltips, minimap — perfect in all 22
- Interior world pipeline (CJ house ×3): textures + lighting near-perfect
- Ped rendering in interiors and at least one street NPC (11:35): fully textured, shaded, animated
- Road/asphalt/curb/sidewalk textures and yellow lane lines everywhere
- Mid-distance building facades with window detail (downtown skyline, Idlewood)
- Vegetation in most shots: palm trunks/fronds, agaves, hedges, grass slopes (when not black — see F3)
- Coronas: red traffic/mission dots render (placement suspicious in places, but they render)
- Ocean water *near camera*: caustic texture present (though low-res/blocky)
- Sky gradient + fog; traffic light sprites (green/red lit states); monorail/bridge geometry; blood pool decals; tree canopies; power-line towers; smoke plumes exist (shape wrong — F4)
- Wake/foam particles behind the ocean cars (white streaks may be *this* path degraded — see F4)

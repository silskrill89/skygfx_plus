# Screenshot Baseline — 2026-10-29 00:42–00:43 (FINAL GATE build test)

**Build**: skygfx.asi 2,451,456 B, md5 e7405bb26f0e1f741e9e359aee5e0d06, built 2026-10-29 00:39:57, branch very-experimental HEAD b7396c4+dirty.
**Log correlation**: skygfx_dbg.log run 00:41:10–00:43:08. `[water] entry waterStyle=0 (Xbox) tier=2`, `[water_rt] scene 1920x1080 + depth R32F ok`, `chain: router=IDR_WATER_PARALLAX VS=IDR_WATER_VS cullPre=0x01 cullSet=NONE restore=paired`, `verts=2044 indices=3066` → WbBufferCountsSane **PASSED** this run (the verts=621/indices=3432 failure from the earlier run did NOT recur).
**Source folder**: `C:\Users\aaaaaaaaa\Documents\ShareX\Screenshots\2026-10` (filenames carry `gta_sa_` prefix).

## Shot-by-shot (newest 8, chronological)

| # | File | In-game t | Findings |
|---|------|-----------|----------|
| 1 | gta_sa_Fa3HyQjE78.png | 11:19 | **FLAT GREY UTEXTURED ROAD SLAB** under car — hard rectangular edge vs brown dirt, zero texture/detail. Car renders OK (dark, spec visible). Water behind OK with caustics. → ground-texture path bug (§2 'ground textures see-through' family). |
| 2 | gta_sa_fwOki5g45c.jpg | 11:32 | **TEAL SLAB** left side: flat untextured teal water region, hard seam against textured caustic water. White statue-ped on dock. Otherwise good. |
| 3 | gta_sa_33FAwmesLO.jpg | 11:37 | Best shot. Water caustics + light reflections good. Minor: white foam puffs floating slightly above surface mid-distance. |
| 4 | gta_sa_8nn00HyfHt.jpg | 11:47 | **TEAL SLAB** left (flat untextured) + **TRANSLUCENT GREY-BLUE SMEAR QUAD** floating over railing/water centre — soft gradient rectangle, mis-blended veil-type quad. |
| 5 | gta_sa_l56CmrMkQc.png | 12:16 | **TEAL SLAB** bottom-right foreground with soft shadow blob ON it. **PEDS IN WATER RENDER PURE WHITE** (unlit/overbright, statue-like) — 3 peds mid-water. Seawall/railing OK. |
| 6 | gta_sa_qSqaV8zUs5.png | 12:22 | Car floating on water. **LARGE TEAL SLABS** foreground right + bottom — flat quads at different height than textured water, overlapping it. Car mostly solid but flat/dark-shaded. |
| 7 | gta_sa_Lk80k43oMu.png | 12:48 | Underwater view looking up. Surface caustics OK from below. **WHITE BLOWOUT DOME** bottom half (splash foam seen from below — renders through/below surface) + **VERTICAL FLAT TEAL WALL** right side (water plane edge-on, untextured). Foam speckles at crest. |
| 8 | gta_sa_Bn1MVuQikY.png | 12:56 | Surface swim. **WHITE BLOWOUT DOME** foreground (splash foam overbright, no blend) + **TEAL WEDGE** right side. Horizon band OK. |

## Issue taxonomy (priority order)

### W1 — Flat untextured TEAL SLABS (5/8 shots) — TOP
Water grid planes render as flat-shaded teal quads WITHOUT the parallax/caustic texture, adjacent to and overlapping correctly-textured water. **WbBufferCountsSane PASSED this run** → this is NOT (only) exp-1's bad-buffer bail path; a subset of planes renders untextured INSIDE or beside the pipe path. Suspects: (a) texture binding lost for planes taking a secondary path (flat-water quads drawn before/after pipe without pipe textures); (b) parallax PS sampling unbound stage for those quads → teal = base colour constant w/o texture multiply; (c) planes rendered via vanilla fall-through with stale/NULL VS-PS (exp-1 Patch B territory). Teal colour ≈ water base tint → shader outputs constant when texture fetch fails/zero.
**Fix wave**: instrument which planes skip the textured path (per-plane tag in [water] log), then bind/seed textures on that path. exp-1 Patch A+B still apply as hygiene.

### W2 — Splash-foam WHITE BLOWOUT (2/8, worst at swim-up + from below) — §4.6 confirmed
CJ splash-anim foam renders as giant white dome: overbright, no blending, drawn over/through water surface (visible from underwater = draw-order/Z issue too). Fix: foam blending (additive→proper alpha or clamp), brightness (saturate — cf. fix-13 Water_Underwater.hlsl:127 saturate pattern), draw order vs water plane.

### W3 — Peds in water render PURE WHITE (1/8, 3 peds)
Unlit/overbright peds standing in water. Likely ambient/lighting upload path for peds in water zone, or SSS composite no-op (chars_classifyClump never called — known). Distinct from §4.7 corona-black but same ambient family.

### W4 — Flat grey untextured road slab (1/8)
Ground surface with NO texture under car (shot 1). Hard rect edge. Ground-texture binding or texdefs classification bug on that surface type.

### W5 — Translucent smear quad (1/8)
Soft grey-blue gradient rectangle floating over pier railing. Mis-blended veil/quad — same family as §4.7 alpha inversion.

### GOOD (do not regress)
Water caustics/parallax where textured; reflections of shore lights; seawall/railing/pier geometry; car solid-body rendering; sky; HUD; no giant peds; no verts/indices buffer failure this run.

## Next actions
1. W1: locate the untextured-plane path in waterPipe.cpp (which planes skip pipe textures) → fix binding.
2. W2: foam blend/brightness/order fix (§4.6).
3. W3: ped-in-water ambient fix.
4. W4: grey-road texture binding.
5. W5: folds into §4.7 alpha-inversion root-cause pass.

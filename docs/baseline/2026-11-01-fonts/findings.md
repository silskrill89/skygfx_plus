# Findings - 2026-11-01 "fonts are completely broken" (+ "lot of broken crap")

**Type:** READ-ONLY forensics. No source file was modified.

**Evidence base:**
- Screenshots: `C:\Users\aaaaaaaaa\Documents\ShareX\Screenshots\2026-11\` (newest 15 `gta_sa_*` shots read; three `mercury_*` 20:16/21:56 shots are not GTA-SA frames and were excluded).
- Run log: `E:\games\gtasa_skygfx_plus\skygfx_dbg.log` (single run: 21:51:41.929 -> 21:53:34.744, clean `DLL_DETACH`).
- Live INI: `E:\games\gtasa_skygfx_plus\SkyGfx.ini` (`spriteFlushShield = 1` :173, `coronaBlendShield = 1` :37, `spriteStateShield = 1` :174).
- Build under test: `skygfx.asi size=2528768 mtime=2026-11-01 21:32:34 built=Nov 1 2026 21:31:09`.
- Prior research: `docs/plans/2026-11-01-sa-font-path.md` (SA font path disassembly), `docs/baseline/2026-09-03-reference-caps/findings.md` (target state), `docs/baseline/2026-10-31-test4/findings.md` (previous run).
- Git: `src/core/main.cpp` is **modified in the working tree**; `git show HEAD:src/core/main.cpp | grep -c spriteFlushShield` = **0**. The entire sprite-flush/corona shield family - and its gate - is *uncommitted* code, introduced today.

---

## 0. TL;DR

1. **Fonts are not "missing" - their glyph coverage (alpha) is being discarded.** Text rasterises as **solid rectangles at exact per-glyph advance widths**: variable-width filled bars, ~101 px tall in the frontend, ~10 px gaps, with 1-2 px brighter seams where adjacent glyph quads overlap. HUD/menu **colour** survives (pale blue `(178,208,243)`, slate `(83,99,116)`, white) because glyph colour comes from the vertex diffuse.
2. The **frontend/menu atlas (bold "font2/font*m"-family)** loses coverage 100% -> unreadable bars. The **HUD atlas (font1-family)** survives *visually* only because its transparent texels are RGB 0, so an additive blend adds nothing where there is no glyph - the digits stay legible but lose antialiasing/outline and glow.
3. **Proven root cause:** `CloudRegionFlush_7273D0_hook` (0x719B2B - the *font glyph flush*, one caller: `CFont::RenderFontBuffer` 0x719840) runs `coronaDrawRawShield(force=true)` -> `ALPHABLENDENABLE=TRUE, SRCBLEND=DESTBLEND=ONE, ZWRITE=FALSE, ALPHAFUNC=GREATEREQUAL/ALPHAREF=0`. The gate meant to prevent this (`flushBatchIdentity()`, src/core/main.cpp:3889) **never rejected a single batch in the whole run**: `[cloud_flush] gate shielded=23116 skipped=0`. Its `family` verdict ORs a stale raw-device stage-0 (L3) with the L1 raster; the sprite flush at 0x719B26 runs ~5 instructions earlier and leaves the **corona glow texture** on device stage 0 -> 100% false positives.
4. Everything else in the shots is the known "black / missing texture + leaked REPLACE blend" family, now much worse: whole world black or untextured, giant black quads/wedges, giant 2-D UI rectangles drawn at world scale, intermittent additive blow-out of HUD tags, plus a crash dump right after the 19:0x session.
5. Nothing in these shots reaches the 2026-09-03 target state. **No visual dimension improved**; HUD *composition* (icons, bars, radar, clock, money layout) is the only subsystem still intact.

---

## 1. Exactly how the fonts are broken

### 1.1 Pixel forensics (objective measurements)

Measured on `gta_sa_hDND0gjVT2.png` (frontend/menu frame):

| Property | Measurement |
|---|---|
| Distinct colours in whole frame | **4 dominant**: bg `(2,2,2)`, pale blue `(178,208,243)`, slate `(83,99,116)`, white `(255,255,255)` (+tiny edge shades) |
| Antialiased glyph shades | **none** - there is no colour ramp between background and glyph fill |
| Main menu row vertical extent | y = 68 ... 168 -> **101 px uniform height for every character** (cap height, x-height and descender are indistinguishable) |
| Glyph run widths on one row (y=430/440) | 58, 48, 54, 54, 56, 60, 58, 131 px - **varies per glyph = advance width preserved** |
| Inter-glyph gaps | 10-13 px, plus 1-2 px **brighter seams** at glyph boundaries (double blend of adjacent quads) |
| Small status line bottom-left (y~1026-1049) | same structure at ~6-10 px per cell, patterns like `++WW+++WW+++WW` = filled cells with seams |
| Glyph-shaped pixels | only the mouse cursor (the only non-rectangular 2-D object in the frame) |

Interpretation: the font atlas is **still bound and sampled** (otherwise the advance/gap structure and the seams would not exist), but the per-texel **alpha is ignored** and the whole glyph cell is written as opaque/additive colour. That is the signature of `SRCBLEND = ONE` (no `SRCALPHA`) - the doc's mechanism #1 - **not** of a missing/stretched atlas, not of a wrong atlas, not of invisible text.

What the user sees, precisely:
- **Garbled glyph soup / bars**: menu items, menu title, version line at bottom-left, i.e. the whole **frontend text**.
- **Not** "black boxes" (colour is right), **not** "invisible", **not** stretched - advances and baselines are geometrically correct. Only **coverage** is gone.
- The **ImGui F/C readout is unaffected** (ProggyClean bitmap atlas, drawn by the mod's own ImGui pass). It is crisp in every shot where it appears -> the defect is in the *game's* FFP font lane, not in ImGui.

### 1.2 Which text is broken vs which survives (lane split)

| Text | Shots | State |
|---|---|---|
| Frontend/menu labels, title, version line | hDND0gjVT2 (21:51, frontend window) | **BROKEN - solid bars** |
| Mobile-HUD weapon-name tag under the weapon icon | M33fQYKsbZ (F 33578), WTqcuy67CD (F 47732), 5Yj9PfEpON (F 11517) | **BROKEN - white-on-blue blocks ("N O B")** |
| HUD clock / money (`10:21`, `$00001099`) | M33f, WTqc, sja2, 38pu, 7tKY, Kk7W | readable (hard-edged, no AA) |
| Mission help box ("Press 2 to toggle Vigilante...", "Cheat activated") | EA0R, vZQT, Xh9l (19:0x build) | correct in the **19:0x build**, absent from all 21:5x frames |
| Radar blip letters (`BS`, `CV`, `R`, `N`) | all | correct - drawn by a *different* (CSprite2d/blip) lane |

Build split: the 19:03-19:05 batch is a *different binary* from the 21:5x batch (its `C 0x...` code field is 32-bit - `0x2004EA7A`, `0x280C6E2E`, `0x280534C7`, `0x21F43` - while the 21:5x build prints a 16-bit code - `0x1E517`, `0x2CFD`, `0xB32A`). The 19:0x build still had *correct in-game text*; the 21:31 build (the one now deployed) has lost it. Font breakage is a **regression inside today's uncommitted work**, not a long-standing defect.

### 1.3 Root cause (code + log receipts)

`src/core/main.cpp` (working tree, uncommitted):

- `SpriteFlushIndexedDraw_hook` @ 0x70CF62/0x70CF9B - producer-gated force, comment at :3552 ("perf fix 2026-11-01 ... gated on spriteBatch > 0"). Gate reads the shared sprite count `0xC6A158`.
- `CloudRegionFlush_7273D0_hook` @ 0x719B2B - **the font glyph flush**. `0x7273D0` has exactly one caller (0x719B2B, inside `CFont::RenderFontBuffer`). The hook's gate is `0xC8045C > 0` = the **font index count** -> true on every non-empty text draw (see `docs/plans/2026-11-01-sa-font-path.md` section 2A / 4#1).
- Forced block on that path: `ALPHABLENDENABLE=TRUE`, `SRCBLEND=DESTBLEND=ONE`, `ZWRITEENABLE=FALSE`, `BLENDOP=ADD`, `ALPHAFUNC=GREATEREQUAL`, `ALPHAREF=0`, `SEPARATEALPHABLENDENABLE=0`, `COLORWRITEENABLE=0xF` (`coronaDrawRawShield(..., force=true)`).
- `flushBatchIdentity()` (:3889) is the only thing that should reject a font batch. It returns `family` from **L1 raster OR raw-device stage 0**:

```
RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &rawRas);   // L1
d3d9device->GetTexture(0, &id.devTex);                   // L3, LAGS one draw behind
... if(devTex == family glow texture) family = true;
```

`CFont::RenderFontBuffer` calls **0x70CF20 (sprite flush) at 0x719B26**, then binds its atlas, then calls the glyph draw at 0x719B2B. The sprite flush leaves the last glow raster on device stage 0, so `devTex` matches the corona family **on every glyph batch**.

Log receipts (this run, 21:51:41 -> 21:53:34):

```
[21:53:34.120] [#129896] [cloud_flush] gate shielded=23116 skipped=0   <- font flush: 0 rejects in 23116 invocations
[21:53:32.171] [#124199] [sprite_flush] gate shielded=4156 skipped=0   <- sprite flush: same, 0 rejects
[21:53:32.124] [#124136] [postfx_flush] gate shielded=582 skipped=0
[21:52:23.811] [#31798]  [probe] sprite_flush force=1 pre blend=1 src=5 dst=6 zw=0   <- 5/6 replaced by 2/2
[21:51:49.070] [#305]    [CoronaDraw] pre-force dev blend=0 src=5 dst=6 at CloudFlush_7273D0 (black-quad=blend 0)
[21:53:33.699] [#128401] [CoronaDraw] pre-force dev blend=0 ... at CloudFlush_7273D0
```

`skipped=0` on all three gates is the proof: **no hook ever declined to force.** Combined with 1.1 (coverage lost, advances intact, colour preserved) this is mechanism #1 of `docs/plans/2026-11-01-sa-font-path.md` firing on 100% of glyph batches, through the residual gate hole that doc predicted (L3 OR is a false-positive source).

Secondary, unproven-but-live contributors (same file, :3638-:3667):
- `spriteFlushShieldClose()` replays the captured 9-state block *and rewrites a captured REPLACE to canonical 5/6*; the glyph draw 5 bytes later inherits whatever that close left. Text output therefore also depends on the preceding glow flush's capture (doc 4#2).
- the `g_coronaBodyDone` window-close path can push a canonical state block **between 0x719B26 and 0x719B2B**, i.e. mid-text (doc 4#2 B3).

### 1.4 Why HUD digits still read

`font1` texels are white-RGB with real alpha and RGB 0 under transparent texels (`docs/plans/2026-11-01-sa-font-path.md` 1.5). Additive `ONE/ONE` ignores alpha but *adds RGB*: where the atlas is RGB 0 it adds nothing, so glyph shapes still appear (white/bright, aliased, no outline). `font2/font2m` (frontend / masked variants) carry non-black RGB under transparent texels -> the whole cell adds -> **solid bars**. This explains the bimodal appearance in the same frame: menu = bars, HUD numbers = readable-but-hard.

---

## 2. Defect catalogue (every visible defect in the 15 shots)

Severity: **S1** = unusable/crash, **S2** = major visual regression, **S3** = noticeable artifact, **S4** = cosmetic.

| # | Defect | Sev | Evidence (shot -> overlay code) | Notes |
|---|---|---|---|---|
| D1 | Frontend/menu text = solid colour bars (fonts) | **S1** | hDND0gjVT2 (frontend, no overlay) | section 1; 4-colour palette, 101 px cells |
| D2 | HUD weapon tag text = blocks ("N O B") | **S1** | M33f `F 33578 C 0xB32A`, WTqc `F 47732 C 0xBA74`, 5Yj9 `F 11517 C 0x2CFD` | same mechanism, smaller cell |
| D3 | World geometry black / untextured over most of the frame | **S1** | M33f, WTqc, HzYo `F 59404 C 0xE88C`, sja2 `F 63130 C 0xF69A`, 5Yj9, 4uAe `F 814638`, Kk7W `F 341191`, 7tKY `F 322170`, Xh9l `F 120004`, qgbg `F 76555` | flat black silhouettes where buildings/road/grass should be |
| D4 | Giant black quads / wedges crossing the frame (glow sprites with `blend=0`) | **S1** | EA0R `F 149381 C 0x24785`, OTPW `F 167005 C 0x28C5D`, tPzc `F 171683`, qgbg, HzYo, M33f | log: `[CoronaDraw] pre-force dev blend=0 ... (black-quad=blend 0)` several times per run |
| D5 | 2-D UI rectangles drawn at world scale (huge slate/white blocks covering the view) | **S1** | FRimUi5Z0n `F 124183 C 0x1E517` | mosaic colours = **exact** menu palette (slate 41273 px, white 41105 px); ~15 hard edge rows at 100-300 px spacing -> same quads as D1/D2, transformed/magnified |
| D6 | Intermittent additive blow-out of 2-D text/sprite batches | **S1** | all 21:5x frames | `sprite_flush gate shielded=4156 skipped=0`, `cloud_flush 23116/0` |
| D7 | Night city = flat black + blown additive corona smear bands | **S2** | 4uAeIHR8pW `F 814638 C 0x280C6E2E`; also 7tKY (dark banded water/port) | opposite of baseline (soft translucent discs) |
| D8 | Daylight blackout (11:42 / 12:0x, world almost black, grain visible) | **S2** | Kk7WfZwMia `F 341191 C 0x280534C7` | exposure/grading blackout family (same as 10-31 findings) |
| D9 | Giant flat grey/white untextured ground planes | **S2** | 7tKY (lower half), 38pu `F 222030 C 0x3634E` partial | stage-0 diffuse bind failure |
| D10 | Radar drawn twice / mis-framed (round radar + a square copy in the screen corner; white plate under the radar) | **S3** | 4uAeIHR8pW (top-left square + bottom-left disc), 7tKY (white plate behind round radar) | 2-D sprite / RT restore leak |
| D11 | Yellow triangle artifact in interior doorways | **S3** | 5Yj9 (centre), OTPW (left of doorway) | both interiors -> FX/shadow quad leaking |
| D12 | Black-edged foliage / pot-plant cards, black doorway quads | **S3** | 5Yj9 (plant + pot, doorway), HzYo (plant ferns), EA0R (doorway) | alpha-test edge + null-texture family |
| D13 | Interiors far too dark (CJ's house barely visible) | **S3** | 5Yj9, HzYo | baseline wants form-readable dark |
| D14 | Distant fog/haze wall too heavy and too blue (port scene); ships/objects vanish | **S3** | 38pulmSHEs `F 222030` | mild-haze target |
| D15 | Grain/noise visible in dark frames despite `grain=0` | **S4** | Kk7Wf, 7tKY | `[PF] TonemapPass` |
| D16 | Crash after the 19:0x session | **S1** | `EarshotCrash_2026-11-01_190525.dmp` (19:05:25), right after the 19:04 batch | also 10-31 00:51 / 02:17 dumps; the 21:51 run exited clean |

Not observable in this set (do not claim either way): smoke, fire/sparks, water reflection correctness, mirrors, brake lights, sun shafts.

---

## 3. Regression vs the last known-good reference

Reference: `docs/baseline/2026-09-03-reference-caps/findings.md` (checklist items 1-14).

| Baseline checklist item | 2026-09-03 target | 2026-10-31-test4 | **2026-11-01 (these shots)** |
|---|---|---|---|
| 1 No see-through geometry | solid everywhere | black road/blotches (S2) | **FAIL - world black/untextured (D3)** |
| 2 Corona shape (translucent disc + streak) | soft warm discs | mirrored band + black squares | **FAIL - black quads from REPLACE, plus additive smear bands (D4, D7)** |
| 3 Night ambient readable | blue-grey lift | dark-at-noon blackout | **FAIL - flat black (D7, D8)** |
| 4 Day grade pale/neutral | pale milky | black at noon | **FAIL - 11:42 blackout (D8)** |
| 5 Shadow colour bluish-grey | soft | hard black blotches | **FAIL - pitch-black wedges (D4)** |
| 6 Vehicle specular soft band | present | some sheen | **N/A - no vehicle readable in any frame** |
| 7 Smoke blending | translucent | not observed | **N/A** |
| 8 Fire/sparks additive | correct | not observed | **N/A** |
| 9 Alpha-tested vegetation clean | clean | black plant silhouettes | **FAIL (unchanged) - D12** |
| 10 Distance haze mild | mild | - | **FAIL - heavy blue wall (D14)** |
| 11 Road lines & ground texture | crisp | black road | **FAIL - no ground texture at all (D9)** |
| 12 Sky gradients | pink/orange banding | - | **PARTIAL - port sky flat, no banding** |
| 13 Interior darkness preserves form | yes | interiors mostly good | **FAIL - too dark + black quads (D13, D12)** |
| 14 Colour filter MODERN | yes | yes | runs (`[PostFX] ColourFilter_Modern ENTER`) but sits on a black frame |
| - Text / HUD legibility | not in checklist | "HUD, map" listed **GOOD** | **NEW FAILURE - D1, D2, D6** |

**What improved:** only HUD *composition* (icons, health/armour bars, radar disc, clock, money layout), the existence of the SMAA/DOF/velocity/tonemap passes (log shows them executing), and clean process teardown in the 21:5x run. No rendering dimension the 09-03 baseline measures is better.

**What regressed new today:** text (D1/D2/D6) - explicitly "GOOD" on 10-31 - and 2-D geometry drawn at world scale (D5) or duplicated (D10).

---

## 4. Ranked actionable list

### TOP 5

1. **Close the `flushBatchIdentity()` false positive (0x719B2B font flush).**
   `src/core/main.cpp:3889-3919`. Stop OR-ing the raw-device stage-0 (`d3d9device->GetTexture(0)`) into `family`; require the **L1** raster (`RwRenderStateGet(rwRENDERSTATETEXTURERASTER)`) to match the corona family and use the device value only as corroboration, never as an OR. Expected effect: `[cloud_flush] gate shielded` falls to ~0 while text is on screen; D1/D2/D6 disappear.
   *Verify:* rerun; `[cloud_flush] gate shielded=<n> skipped=<m>` must show `skipped > 0` during the frontend, and `[cloud_flush 0x7273D0 force=1]` must not print on the menu.

2. **Never run `coronaDrawRawShield(force=true)` on a glyph batch - pin the font contract instead.**
   Add an explicit font-lane guard at 0x719B2B: if the flush carries font indices (`0xC8045C > 0` at entry, or caller == 0x719B26), either skip the force entirely or force `ALPHABLENDENABLE=TRUE, SRCALPHA/INVSRCALPHA, ALPHATEST on, ref>=1, ZWRITE=FALSE, BLENDOP ADD` - never `ONE/ONE`. `CFont::RenderFontBuffer` sets only `VERTEXALPHAENABLE` (doc 1.4), so this one guard fixes text deterministically even if the gate false-positives again.
   *Verify:* frontend menu readable at 1920x1080; `[CoronaDraw] pre-force ... at CloudFlush_7273D0` count -> 0.

3. **Make `spriteFlushShieldClose()` stop authoring the glyph draw's blend pair.**
   At 0x719B26/0x719B2B the close path replays (and can rewrite) the captured 9-state block ~5 instructions before the glyphs, and the `g_coronaBodyDone` branch can push a canonical block *mid-text*. Suppress the close-side replay when the next draw is the font flush, or re-pin the font contract right after the close. Re-evaluate the "captured REPLACE -> canonical 5/6" rewrite, which makes text depend on the previous glow flush.
   *Verify:* `[CoronaResync] window closed ...` must not appear between 0x719B26 and 0x719B2B (one-shot log of the caller return address).

4. **Kill the black-quad family at its source (D3, D4, D9).**
   `blend=0` receipts still print at `CloudFlush_7273D0` and `SpriteFlush_70CF20`; glow/cloud batches still rasterise with `ALPHABLENDENABLE=FALSE` (REPLACE) at least once per run, and the stage-0 diffuse binds for world/ground still fail. Convert the capture/repair into a *pre-draw* assertion ("blend must be TRUE when a glow batch flushes") and audit the 3-layer stage-0 diffuse bind on the ground path.
   *Verify:* `[CoronaDraw] pre-force dev blend=0` count == 0 over a 2-minute run; no flat grey/white ground planes.

5. **Fix the "2-D quads transformed like world geometry" class (D5, D10).**
   FRim shows menu-coloured quads magnified to hundreds of pixels over the 3-D view; 4uAe shows the radar drawn twice in two places. This is a viewport/transform or RT-restore leak on the Im2D path (the mod already hooks 0x7FB810/0x7FB824). Audit every `SetRenderState`/`SetViewport`/`SetRenderTarget` restore around Im2D dispatches (`guardedIm2DRender`, SMAA, DOF, colour filter) and assert Im2D VS + viewport identity at every `0x734EA0` dispatch.
   *Verify:* no frame where 2-D UI rectangles exceed ~200 px; radar appears exactly once.

### Additional (ranked 6-9)

6. **Blackout exposure (D8) / flat-black night (D7)** - `[PF] TonemapPass` + `ColourFilter_Modern` at `r=0.950 g=0.888 b=0.735 luma=0.389` still yields black at 11:42; re-check the frame-adaptive exposure path (same defect as 10-31, still open).
7. **Foliage/alpha-test black edges + black doorway quads (D12)** - alpha-test ref inheritance family (doc S2); `pipeForceAlphaBlock` bumps ref 0->1 only at frame boundaries.
8. **Radar plate/mis-framing (D10)** - white plate behind the disc + corner copy point to a 2-D render-target restore, same family as item 5.
9. **Crash dumps (D16)** - three in two days, latest `EarshotCrash_2026-11-01_190525.dmp` seconds after the 19:04 batch. Triage before more visual work; a crash invalidates every visual conclusion.

### Do-not-do
Do **not** chase the font atlas (TXD, DXT, palette, mip). It is bound, sampled and geometrically correct - 1.1 proves advance widths and line heights are exact. "Regenerate fonts.txd" or "fix font UVs" would be wasted work.

---

## 5. Probe plan (no source edits needed)

1. **Bisect with existing INI levers, one at a time, screenshots at the frontend only:**
   - `spriteFlushShield = 0` -> expect menu text to return. If it does, the font breakage is 100% the shield family (items 1-3).
   - `coronaBlendShield = 0` -> blend/ZWRITE force only.
   - `spriteStateShield = 0` -> ALPHAFUNC/ALPHAREF/CWR only (isolates the `ALPHAREF=0` always-true alpha-test part).
2. **Log the lane identity once per font style:** at 0x719B2B print `[0xC8045C]` (font index count), `[0xC71ACC]` (font style), the L1 raster, and `*(RwTexture**)0xC71AD0/0xC71AD4 -> raster`. That names which atlas is the frontend one and proves `family` is a false positive.
3. **Re-shoot the same 6 frontend/in-game scenes** after each change so the next `findings.md` can diff like-for-like against 1.1's measurements (101 px cells, 10-13 px gaps, 4-colour palette). A fixed build must show an antialiased colour ramp: if the text band still has <10 distinct colours, coverage is still being discarded.
4. **Correlation:** the overlay `F <id>` equals the log's `[#<id>]` field exactly (e.g. `F 124183` => `[#124183]` at 21:53:32). Keep `debugOverlay=1` in every capture; it is the only free timestamp.

---

## 6. Appendix - shot index (newest first)

| # | File | mtime | Overlay | Correlated log time |
|---|---|---|---|---|
| 1 | gta_sa_FRimUi5Z0n.jpg | 21:53 | `F 124183 \| C 0x1E517` | [#124183] 21:53:32 |
| 2 | gta_sa_5Yj9PfEpON.jpg | 21:52 | `F 11517 \| C 0x2CFD` | [#11517] 21:52:16 |
| 3 | gta_sa_HzYoh97MZ9.png | 21:52 | `F 59404 \| C 0xE88C` | [#59404] 21:52:34 |
| 4 | gta_sa_M33fQYKsbZ.png | 21:52 | `F 33578 \| C 0xB32A` | [#33578] 21:52:25 |
| 5 | gta_sa_WTqcuy67CD.png | 21:52 | `F 47732 \| C 0xBA74` | [#47732] 21:52:32 |
| 6 | gta_sa_hDND0gjVT2.png | 21:51 | **none** (frontend) | frames 0-~1020, 21:51:42-21:52:15 |
| 7 | gta_sa_sja28HWVXF.png | 19:04 | `F 63130 \| C 0xF69A` | prev binary, log not retained |
| 8 | gta_sa_EA0RvO8i0K.png | 19:04 | `F 149381 \| C 0x24785` | " |
| 9 | gta_sa_OTPWE1FOzX.png | 19:04 | `F 167005 \| C 0x28C5D` | " |
| 10 | gta_sa_nBNP4Kbpnn.jpg | 19:04 | `F 139075 \| C 0x21F43` | " |
| 11 | gta_sa_qgbgxSpwzT.jpg | 19:04 | `F 76555 \| C 0x1280B` | " |
| 12 | gta_sa_tPzctMWjEa.jpg | 19:04 | `F 171683 \| C 0x29F39` | " |
| 13 | gta_sa_vZQTIDhMEu.jpg | 19:04 | `F 145777 \| C 0x23971` | " |
| 14 | gta_sa_Xh9lzoQWlh.jpg | 19:04 | `F 120004 \| C 0x104C4` | " |
| 15 | gta_sa_38pulmSHEs.jpg | 19:05 | `F 222030 \| C 0x3634E` | crash dump 19:05:25 follows |
| 16 | gta_sa_4uAeIHR8pW.png | 18:56 | `F 814638 \| C 0x280C6E2E` | earliest binary |
| 17 | gta_sa_Kk7WfZwMia.png | 18:54 | `F 341191 \| C 0x280534C7` | " |
| 18 | gta_sa_7tKYpl1CZV.png | 18:54 | `F 322170 \| C 0x2004EA7A` | " |

Excluded: `mercury_JXotf9QwgK.png`, `mercury_Src3m7i3Nv.png`, `mercury_aLissLusKa.png` (20:16-21:56) - not GTA-SA frames.

Build identity note: `C 0x...` is 16-bit in the 21:5x binary (`0x1E517`, `0xF69A`) and 32-bit in the 18:5x/19:0x binaries (`0x2004EA7A`, `0x280C6E2E`) - `DebugFrameCode()` itself changed between today's uncommitted revisions; do not compare `C` values across batches.

# Git archaeology — ped/char lighting, Sep 3 reference → HEAD (898e894) + uncommitted tree

**Lane:** read-only archaeology for W3 (peds standing in water render PURE WHITE, statue-like).
**Baseline:** Sep 3 build = `c270294` (2026-08-30, last commit before Sep 3) plus whatever working-tree state existed that day. `18f5303` (Sep 7) self-describes as *restoring* the good look ("revert of regressive lighting change"), which corroborates a Sep-3 working-tree ≈ good state. The Sep 3 built look therefore corresponds to: `pAmbient` override with plain `GetTimecycleAmbient()`, hash-gated weather override, no ped ambient floor, SSS composite active-but-gated.

## 0. Ped render path (unchanged family, verify first)

Peds never enter vehiclePipe callbacks. `myDefaultCallback` (src/core/main.cpp:1749, `isPedPipe = pipe == skinPipe`) runs the **vanilla fixed-function skin pipe**: final ped color ≈ `pAmbient` + `DirectionalLightColourForFrame` (`RpLightSetColor(pDirect, …)` at main.cpp:2444) + prelit vertex colors. Prior forensics already pinned this: docs/plans/2026-10-26-image-forensics-0119-0140.md — white-ped frames log `pDirect(rgb)≈0.996`, textured/dark frames `0.35–0.50`; the white is **direct-light over-exposure on the vanilla skin pipe**, not a texture loss. Everything below feeds exactly those two RW globals and the override in main.cpp:940-1008 (HEAD numbering) / :2038-2118 (worktree).

## 1. Sep 3 → HEAD diffs by file

| File | Scope | Effect |
|---|---|---|
| `src/entities/chars.cpp` (161ln committed) | in 18f5303 (+b7396c4 state work) | SSS composite: `DESTBLEND=ONE` additive → BLENDFACTOR/INVBLENDFACTOR bounded lerp; `sssEnable` master gate added; `c21` ambient source `ambientObj` → `GetTimecycleAmbientPBR()` × `sssAmbientBoost`; `guardedIm2DRender` around all SSS Im2D draws; raw-geom state save/restore. **None of this can overbright peds today** — see §3. |
| `src/core/main.cpp` (ped block) | 18f5303 | `tcAmbient = GetTimecycleAmbient()` → **`GetTimecycleAmbientPBR()`** (main.cpp:947 at HEAD). Equal-by-default (`pbrAmbientFloor` opt-in, default 0.0) — semantics shift only when INI floors are set. |
| `src/core/weather.cpp` (27ln) | **94dcd37 (Sep 9)** | `Weather_SelectSet()` hash (70% vanilla / 30% weathers2.dat) → **always `WEATHER_SET_2`** + `g_currentBlend = 1.0` instantly. See §2.1. |
| `src/render/vehiclePipe.cpp` / `pipelinecommon.cpp` | Sep 9-11 lighting family (94dcd37, 01204d7, ca922ef, a6d56f8, 671bcd7, 191dbe7) | Vehicles/buildings only: PS c24 object ambient source `ambientObj` → shared `GetTimecycleAmbientPBR()` (vehiclePipe.cpp:529, :3128, :3446-350); PS c5-c18 light constants added; hemisphere/sky-ambient chain with `ClampAmbientToSky` (pipelinecommon.cpp:78-173), night-lerp law, `pbrAmbientFloor`. **Peds don't consume these PS registers** (FFP), so the register-layout drift (REG_eye 35→36, day/night c30-c31 relocation, REG_fxParams, c44/46) is irrelevant to W3. |
| `src/render/waterPipe.cpp` (+651 committed) | custom water pipe R2/G1 | Reflection is a **StretchRect scene copy** (`CopySceneFlippedToTexture`, :780), not a re-render: peds are not drawn TWICE and the water path never touches `pAmbient`/lights (grep: only c12/c13 timecycle constants for its own shader). Not a ped-white vector. |

### Uncommitted tree (2026-10-24+ "PED AMBIENT" checkpoint)

- **main.cpp:2038-2118** — ped override reworked: `base = max(game pAmbient, tcAmbient)` + **`pedAmbientFloor` hue-preserving floor** (`float floorV = …config->pedAmbientFloor`, default **0.20**) + **cutscene floor 0.15** (`CCutsceneMgr__ms_running`). None of this exists at HEAD (`grep floorV/maxCh` in HEAD: zero — uncommitted only).
- **pipelinecommon.cpp** — `GetTimecycleAmbient()`: hardcoded `×0.85` → INI `ambientMult` (default 1.0, +18% ambient); `ClampAmbientToSky` gets a **`SkyGfx.ambientFloor` absolute floor** (default 0.03, additive lift) — hits peds via the override.
- **weather.cpp** — full 52-field weathers2.dat parser (old one truncated at ~22 fields) + **`altTimecycEnable` gate** (default 0 → vanilla timecycle).
- **chars.cpp** — SSS composite hard early-out via `s_charsClassifyFresh` latch (chars.cpp:27-44, :175-195): still no mask producer, composite never composites. Checked *around* it: only state restorations and constant-source swaps; the Pass-2 white blowout path is unreachable.

## 2. Water-specificity audit (what could tie "in/near water" to peds)

1. **No `UnderWaterness` hook** — zero repo hits; underwater look is the game's own + the separate postfx_underwater lane (H6, all-screen, not ped-specific).
2. **Water reflection** = flipped scene copy; peds not re-rendered; no ambient/light mutation in waterPipe/mirrorPass.
3. **SSS composite** covered "the whole depth-masked lower half of the frame (water surface included)" by design, and pre-18f5303 it was an *additive* `DESTBLEND=ONE` blend — i.e. the historical shape of "stuff near the waterline gets white-washed". But the shipped build's Pass 2 is gated to a no-state no-op, and Sep 3 already had the same recipes → dead end for *today's* regression; the ambient-match added on `c21` only matters if the pass ever runs again.
4. **W3's capture frames co-occur with the teal-slab defect** (docs/baseline/2026-10-29-finalgate-0042/findings.md shots 2/5: white statue-ped on the *dock* + slab; white peds mid-water + slab). A sliver of W3 may be the mis-projected water-quad overlay covering peds (separate lane, W-teal), but theën lighting-family findings below stand independently (equal lights).

## 3. Ranked suspects

| # | Commit / location | Mechanism | Confidence |
|---|---|---|---|
| **1** | **Uncommitted worktree — main.cpp:2081-2100 (`pedAmbientFloor`)** | Peds are FFP: `final ≈ pAmbient + pDirect·NdotL`. The floor lifts `pAmbient` to ≥ 0.20/channel (default INI **ships 0.20**, skygfx.default.ini). At full sun (noon, W3 shots) + streetlight pools (night forensics) peds saturate to flat white → "statue". Awärthe Sep 3 had NO floor. | **~65%** primary white source in *current* default config |
| **2** | **94dcd37 (Sep 9) — weather.cpp:55-59, :250 (`always WEATHER_SET_2`)** | weathers2.dat "pins ambient near 0 and ships **Dir=255 in EVERY slot**" (waterPipe.cpp:1326 note). Always-on → `m_CurrentColours.directional ≈ 1.0` every frame/hour (vanilla night Direc ≈ 0.03-0.09) → `RpLightSetColor(pDirect)` at main.cpp:2444 → white peds while buildings (baked prelight) and vehicles (own constants) stay correct — the observed **ped-only** signature. Matched by the `pDirect 0.996` night evidence. At Sep 3 the 70/30 hash + slow ramp made this rare. | **~55%** (master regression lineage; today default-gated by worktree `altTimecycEnable=0`, but any INI with 1 restores it) |
| **3** | **Worktree — pipelinecommon.cpp clamp/floor tail (:147-173) + ambientMult 1.0 vs 0.85 (:187-196)** | Ambient lift ≥0.03 additive + +18% multiplier feeding the ped override; small uplift, compounds suspect 1. | ~30% contributor |
| **4** | **18f5303 (Sep 7) — main.cpp:947 swap `GetTimecycleAmbient*` → `GetTimecycleAmbientPBR()`** | Peds now inherit the PBR ambient chain (not the game's plain ambient); neutral while both INI floors are 0.0, becomes a lever iff floors engaged. | ~20% (contract drift, not standalone white) |
| — | Teal-slab / water-veil overlay (untracked water/mirror lane) | White/teal mis-projected quads drawn over mid-water peds in the capture frames — overlaps the lighting family in shots 2/5 only. | track separately |

**Ruled out:** SSS composite (hard early-out, and Sep-3 equivalent also produced correct caps); CWeather::UnderWaterness gating (absent); water reflection pass (copy-only, no light mutation); pipelinecommon PS c0/c4/c19/c30-c31 register drift (peds are FFP); re-render of peds anywhere near water.

## 4. Recommended fix approach for W3

1. **Bisect by INI (both are document keys, no rebuild):**
   - `pedAmbientFloor = 0.0` — proves/kills suspect 1 in one screenshot (same protocol as regression-attribution §2.3).
   - `altTimecycEnable = 0` explicitly in the tester's live INI — neutralizes suspect 2's weathers2 line for good.
2. **Restore Sep-3 semantics surgically (keep the modern plumbing):**
   - ped override: `pAmbient->color = GetTimecycleAmbient()` with **no `max()` floor** (keep the override — it predates Sep 3 — drop only the floor/max/cutscene 0.15 additions, or default `pedAmbientFloor=0.0` and document).
   - weather: keep worktree `Weather_Update` early-out (`altTimecycEnable=0` default); if weathers2 is ever wanted for garages/water colors, scope the Dir/ambient columns separately instead of whole-row overwrite.
3. **Leave the lighting family commits (vehicle/building PS constants, hemisphere ambient) untouched** — they don't reach the FFP ped path.
4. **Verification:** replay the Sep-3 caps conditions (pier/mid-water, noon) with `pedAmbientFloor=0, altTimecycEnable=0`; peds must return to the 2026-09-03 lit look; night streetlight-pool frames (01:56/02:03 forensics) double-check direct-exposure normalization. If noon remains white after both keys, escalate to the teal-slab lane before touching shaders.

*Report generated 2026-10-30 — read-only archaeology lane; no source edited, nothing built.*

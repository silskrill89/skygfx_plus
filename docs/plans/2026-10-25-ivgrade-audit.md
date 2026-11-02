# GTA IV Grade — Implementation Audit and Fix List

**Date:** 2026-10-25
**Scope:** read-only audit. No source, shader, resource, or config file was modified.
**Only artifact written:** this file.

**Subject:** `DrawIVGrade()` — `src/render/postfx_colourfilter.cpp:1354-1547`, plus the embedded
pixel-shader blob `g_ivGradePSBytes` at `:1084-1316`.
**Reference:** `docs/plans/2026-10-25-gtaiv-shader-overrides.md` (shorthand **OV**, which in turn
quotes `E:/SDKs/LibertyRecomp-main/LibertyRecompLib/shader_overrides/`).

## 0. Method and headline numbers

The shader exists **only** as a 1380-byte `BYTE[]` literal. There is no `IVGrade.hlsl` anywhere in
the tree (`find . -iname "IVGrade*"` → 0 files; no `grade0` in `shaders/`; no `IVGrade` entry in
`resources/resource.h`, `resources/resource.rc`, or the `multi_entry` list in
`tools/fast_build.py`). It is the only PS in `src/render/` created with raw
`IDirect3DDevice9::CreatePixelShader` (`:1342`) instead of `RwD3D9CreatePixelShader` — every other
shader (e.g. `pipelinecommon.cpp:694`, `PC_GrassInstancer.cpp:120`) goes through the RW wrapper.

To audit the math, the byte array was extracted to a scratch file **outside the repository** and
disassembled with `D3DDisassemble` (`d3dcompiler_47.dll`). Verbatim header:

```
//   float4 grade0;      c0
//   float4 grade1;      c1
//   float4 grade2;      c2
//   sampler2D sceneTex; s0
// approximately 67 instruction slots used (5 texture, 62 arithmetic)
```

| Metric | Value |
|---|---|
| PS profile | `ps_3_0` |
| Instruction slots | **67** (5 `texld`, 62 arithmetic) |
| Registers consumed | `c0`, `c1`, `c2`, `s0` — nothing else |
| Internal `def` constants | 4 (`c3`–`c6`, compiler-generated) |
| Draws per frame when live | 1 (`RwIm2DRenderIndexedPrimitive`, `:1525`) |
| Full-screen blits per frame | **2** (entry `RwRasterRenderFast` copy `:1455`, exit `UpdateFrontBuffer` `:1544`) |
| Gate | `config->ivMode` alone (`:1378`) |

---

## 1. Stage-by-stage table

### 1.1 Host stages (execution order)

| # | Stage | Lines | Consumes | Effect |
|---|---|---|---|---|
| H1 | `PF_SCOPE` + gate breadcrumb | 1357-1377 | `config->ivMode/pipeline/buildingPipe/vehiclePipe/colorFilterEnable` | Throttled log only. Explicitly documented as *not* evidence the pass ran. |
| H2 | **master gate** | 1378-1379 | `config->ivMode` | `return` if 0. The only gate on the whole pass. |
| H3 | shader create (lazy) | 1380-1382 → 1332-1352 | `g_ivGradePSBytes` | Raw `CreatePixelShader`. Sticky failure flag `s_ivGradePSFailed`. |
| H4 | device/camera/raster guards | 1383-1389 | `d3d9device`, `Scene.camera` | Bail. |
| H5 | screen-size resolve | 1393-1405 | `GetScreenSize` cache, `camRas->width/height` | Bail if `camRas` ≠ screen (transient envmap/reflection raster). |
| H6 | front-buffer guard | 1406-1407 | `CPostEffects::pRasterFrontBuffer` | **Guard only — the pointer is never read again.** Dead check (§3). |
| H7 | menu/pause gate | 1414-1415 | `CMenuManager__m_bMenuActive`, `m_bDisableAllPostEffect` | Cutscenes deliberately pass (expIV gate fix). |
| H8 | cutscene one-shot log | 1416-1418 | `CCutsceneMgr__ms_running` | Logs once/session. |
| H9 | param log | 1420-1424 | all `iv*` sliders | Throttled. |
| H10 | raster (re)alloc | 1438-1447 | `w`, `h`, `camRas->depth` | `RwRasterCreate(w,h,depth,rwRASTERTYPECAMERATEXTURE)` → `s_ivGradeRas`. 32-bit, so the grade's input and output are **8-bit LDR**. |
| H11 | **frame copy** | 1448-1462 | `camRas` → `s_ivGradeRas` | `RwCameraEndUpdate` / `RwRasterPushContext` / `RwRasterRenderFast(camRas,0,0)` / `Pop` / `RwCameraBeginUpdate`. Workaround for `pRasterFrontBuffer` being a padded 2048² raster with the frame at its top-left. Full-screen blit #1. |
| H12 | quad + raw-geom snapshot | 1464-1467 | `w`,`h`; 9 raw D3D9 states | `s_ffQuad` UV 0..1, positions 0..w/0..h. |
| H13 | IM state store/set | 1468-1476 | — | ALPHATEST off, filter LINEAR, fog off, Z/ZW off, vertex-alpha off, texture = `s_ivGradeRas`. **No blend state, no texture address mode** (§3/§4). |
| H14 | **exposure-cap decision** | 1478-1503 | `s_cfTonemapRanFrame`, `s_ivExpCap`, `config->ivExposure` | `ex = ivExposure > 0.01 ? ivExposure : 1`; then `if(tonemapRanThisFrame && ex > s_ivExpCap) ex = s_ivExpCap`. |
| H15 | constant upload | 1504-1517 | config | `c0 = (ex, desat, gamma, 0)`, `c1 = (vigInt, vigRadius, vigContrast, bloomInt)`, `c2 = (1/w, 1/h, 0, 0)`. |
| H16 | bind + draw | 1519-1526 | `s_ivGradePS` | `overrideIm2dPixelShader` + `PfBindPS` + `PfDraw(4,2)` + raw `RwIm2DRenderIndexedPrimitive`. Note: raw, not `guardedIm2DRender` (which `ColourFilter_Generic:139` uses). |
| H17 | state restore | 1528-1540 | 9 raw states | filter/fog/Z/ZW/raster/vertex-alpha + ALPHATEST. |
| H18 | **front-buffer resync** | 1544 | — | `CPostEffects::UpdateFrontBuffer()`. Full-screen blit #2. |
| H19 | done log | 1545-1546 | — | Throttled. |

### 1.2 Pixel-shader stages (single draw, in instruction order)

`c0 = grade0 = (exposure, desaturation, gamma, unused)`
`c1 = grade1 = (vignetteIntensity, vignetteRadius, vignetteContrast, bloomIntensity)`
`c2 = grade2 = (1/screenW, 1/screenH, unused, unused)`
`c3..c6` = compiler immediates: `(0.01,1,0.5,0.05)`, `(0.299,0.587,0.114,0.001)`, `(0,4,-0,100)`, `(0.25,-0.75,-2,3)`

| # | asm | Math | Regs | Live? |
|---|---|---|---|---|
| P1 | `add r0.xy,-c3.z,v0` / `dp2add` / `rsq` / `rcp` | `d = ‖uv − 0.5‖₂` (UV-space, **aspect-following**, not pixel-circular) | `v0` | live |
| P2 | `add/rcp/cmp` vs `c3.x`=0.01, `c5.w`=100 | `k = (radius ≥ 0.01) ? 1/radius : 100` | `c1.y` | live |
| P3 | `mul_sat` | `t = saturate(k·d)` | — | live |
| P4 | `mad r0.y,t,-2,3`; `mul t,t`; `mad -r0.y·t²+1` | `f = 1 − t²(3−2t) = 1 − smoothstep(t)` | — | live |
| P5 | `max f,0`; `max c1.z,0.01`; `pow` | `v = max(f,0)^max(vigContrast, 0.01)` | `c1.z` | live |
| P6 | `add v,-1`; `mad c1.x,…+1`; `max 0` | `V = max(0, 1 + vigIntensity·(v−1))` | `c1.x` | live |
| P7 | 4× `texld` at `±4·c2.x`, `±4·c2.y` | cardinal taps at **±4 px** (uniform, not Gaussian, not bilateral) | `c2.xy`, `s0` | **effectively dead** (see below) |
| P8 | `mad r0,r0,0.25,-0.75` | `residual = mean4 − 0.75` (hard-coded pedestal in display space) | — | " |
| P9 | `mul residual,c1.w`; `cmp` | `B = (residual ≥ 0) ? residual·bloomIntensity : 0` | `c1.w` | " |
| P10 | `add 0.01,-c0.x`; `cmp r0.w,·,1,c0.x` | `E = (exposure ≤ 0.01) ? 1 : exposure` | `c0.x` | live |
| P11 | `texld r3,v0,s0`; `mul E,r3` | `c = E·rgb` — **display-space multiply** | — | live |
| P12 | `dp3 r2.yzw,c4.xyz` | `L = 0.299R + 0.587G + 0.114B` (**Rec.601**, on `c`) | `c4` | live |
| P13 | `mad r3,-E,+L` | `h = L − c` | — | live |
| P14 | `mul_sat r0.w,0.5,c0.y` | `s = saturate(0.5·desaturation)` ← **half the slider** | `c0.y` | live |
| P15 | `mad s·h + c`; `cmp` guard | `g = c + s(L−c) = lerp(c, L, s)` | — | live |
| P16 | `max 0`; `log`×3 | `log10(g)` (negatives hard-clamped to 0) | — | live |
| P17 | `max c0.z,0.05`; `rcp` | `γ = max(gamma, 0.05)`, `1/γ` | `c0.z` | live |
| P18 | `mul`; `exp`×3 | `g' = g^(1/γ)` | — | live |
| P19 | `add γ,-1`; `add -\|·\|,0.001`; `cmp` | if `abs(γ−1) ≤ 0.001` skip the pow (identity) | — | live (guard) |
| P20 | `add r0,r0,r1` | `o = B + g'` — **bloom added after gamma** | — | live |
| P21 | `add 0.001,-c1.x`; `cmp r0.xyz,·,r1,r0` | **if `vigIntensity ≤ 0.001` discard `B`** | `c1.x` | live — **cross-gate bug** |
| P22 | `mul r1.xyz,r2.x,r0` | `V·o` | — | live |
| P23 | `add 0.001,-c1.w`; `cmp oC0,·,r0,r1` | **if `bloomIntensity ≤ 0.001` discard `V`** | `c1.w` | live — **cross-gate bug** |
| P24 | `mov oC0.w,c3.y` | alpha = 1.0 | — | live |

Effective chain: **exposure → Rec.601 luma split → desat (½ strength) → per-channel pow(1/γ) → + 4-tap high-pass → × vignette-gain**.

### 1.3 What P7-P9 actually produces at the shipped defaults

IV-Mode profile (`src/core/modeManager.cpp:408-432`) and the INI defaults (`src/core/main.cpp:5357-5365`) supply
`ivDesaturation=0.15, ivGamma=1.0, ivVignetteIntensity=0.15, ivVignetteRadius=0.70, ivVignetteContrast=1.5, ivBloomIntensity=0.05, ivExposure=2.5`.

- **P7-P9 bloom:** a 4-neighbour mean of a *display-encoded* frame minus 0.75 only survives on content
  brighter than sRGB 0.75 (≈0.52 linear). For a white 4-neighbourhood around a dark pixel the added
  value is `(1.0 − 0.75)·0.05 = +0.0125` — **1.25 % of white, invisible.** At the slider maximum
  (`ivBloomIntensity=2.0`, `debugmenu_ui.cpp:1269`) it is `+0.5`, but as an *unblurred* per-pixel
  high-pass, i.e. local-contrast sharpening with halos, not a glow.
- **P1-P6 vignette** at `radius=0.70` (corner distance is `√0.5²+0.5² = 0.7071`, i.e. the radius is
  the corner): `V(centre)=1.000`, `V(d=0.25)=0.940`, `V(d=0.35)=0.903`, `V(mid-edge d=0.5)=0.863`,
  `V(corner)=0.850`. A **flat 14–15 % darkening across the whole frame**, not a vignette.
- **P11/P14:** `ivDesaturation=0.15` acts as a 7.5 % lerp to luma.

---

## 2. Divergence list vs the real IV look

Reference for "real IV": `OV §2.9` (composite `ps11`/`ps13`), `OV §2.10`/`§4` (TLAD grain),
`OV §2.3` (scalable bloom), `OV §2.4` (light volume). The real composite's tail
(`motion_blur/gta_composite_mb.hlsl:1500-1543`) is:
`bloom/MB blend → × Exposure → luma/chroma split → chroma·deSatContrastGamma.x + luma →
ColorShift (luma-scaled tint) → log2 · deSatContrastGamma.y · exp2 (per-channel contrast-gamma) →
× ColorCorrect.xyz (per-channel gain)`.

| # | Divergence | Bug or simplification? | Cost to fix |
|---|---|---|---|
| D1 | **Exposure is applied in display-referred space.** P11 multiplies a frame that is already tonemapped (MODERN branch) or already display-referred with no encode (GTAIV branch — `ColourFilter_switch` case `COLORFILTER_GTAIV` at `postfx.cpp:1842-1850` is a pure bypass and the only `LinearToSRGB` in the chain is `ColourFilter_Modern`'s tonemap, `postfx_colourfilter.cpp:40-41`). IV multiplies **linear HDR** before tone mapping. | **Bug** | 1 line, no shader rebuild: upload `pow(ex, 1/2.2)` instead of `ex` (`:1505`). Correct fix (move the grade pre-tonemap) is ~40 LOC of reordering. |
| D2 | **Bloom is a 4-tap high-pass, not a bloom.** IV = 25-tap unsharp-shaped separable blur of an already-bright-passed buffer, **no threshold**, weights sum 1.1002, radius `h/45 px` (`OV §2.3`). Ours = 4 cardinal taps at 4 px, threshold 0.75, **no blur at all**, single pass. | **Bug** (wrong algorithm family) + deliberate cheapness | New pass `Bloom_IVBlur.hlsl`, **124 ps_3_0 slots measured**, 2 draws (H+V — a separable kernel cannot be folded into one pass), 1 constant. `OV §2.3`. |
| D3 | **Bloom is added after the grade** (P20, after desat and gamma) — IV adds it pre-exposure in linear space. | **Bug** | Falls out of D1's reorder; in-pass it's a 1-instruction move. |
| D4 | **No film grain.** IV = 1 tap of a 256² tile `.b` at 1.5625 px with a whole-texel temporal slide, amplitude `(n−0.5)·intensity·1.1`, applied **pre-exposure** (`OV §4.1`). We run a separate post-tonemap IGN pass (`DrawFilmGrain`, `postfx.cpp:1876`, `grainIntensity=0.03`). | Deliberate substitution, **not parity** | ~14 slots + 1 sampler, `s9` (`OV §4.4`). Needs POINT filtering — `noise.cpp:897-920` binds `s9` **LINEAR**, so the grain draw must override and restore. |
| D5 | **No contrast stage.** IV's `deSatContrastGamma.y` is a per-channel power applied in log space (`2^(k·log2 c)`) together with `.z`. Ours is a pure `pow(c, 1/gamma)` encode. | **Bug** (missing stage) | ~3 slots. Best combined with D6. |
| D6 | **No `ColorShift` and no `ColorCorrect`.** IV adds a luma-scaled tint and three per-channel gains (`ColorShift.xyz/.w`, `ColorCorrect.xyz`). We have no equivalent. | **Bug** (missing stages) | ~9 slots (1 `dp3`, 2 `mad` for shift, 3 `mul` for correct). |
| D7 | **`ivSaturation` and `ivCurves` are fully plumbed and never consumed.** Present in `skygfx.h:464-465`, INI read/save/write-back (`main.cpp:5359-5360, 5790-5791, 6136-6137, 6244-6245, 6614-6615`), debug menu (`debugmenu_ui.cpp:1261-1262, 1751-1752, 1905`), mode profiles (`modeManager.cpp:547-548`, `custom_mode.cpp:134-135`) — and read by **nothing**. They are exactly the two knobs D5/D6 need. | **Bug** (dead feature surface) | 0 shader cost if D5/D6 land; otherwise delete the fields. |
| D8 | **Desaturation is halved.** P14: `saturate(0.5·ivDesaturation)`. The UI says "0 = full colour, 1 = heavily desaturated" (`debugmenu_ui.cpp:1257-1258`) but 1.0 gives 50 %. Provenance: `main.cpp:5357` comment "was 1.0 = 50 % grey wash". | **Bug** (UI lies) | 0 slots (delete the `0.5`); retune the default to 0.075→0.15 range. |
| D9 | **Luma coefficients.** We use Rec.601 `(0.299,0.587,0.114)`; IV's composite uses `dot(…, c254.xyz)` with `c254 = (0.0721,0.2125,0.7154)` = **Rec.709** (`OV §2.9` stage 1). | Minor bug | 0 slots (change 3 `def` immediates). |
| D10 | **Vignette default radius makes it a flat darkening.** `0.70` ≈ the corner distance `0.7071`, so `t` never leaves `[0,1]` until the very corner (§1.3). | Tuning bug | 0 slots, config only: `ivVignetteRadius` → 0.35–0.45. |
| D11 | **Bloom ↔ vignette are cross-gated** (P21/P23): `ivVignetteIntensity=0` silently disables bloom; `ivBloomIntensity=0` silently disables the vignette. | **Bug** | −2 `cmp` (67 → 65 slots). |
| D12 | **Order in the frame is wrong.** The grade runs *after* CA, grain and the standalone vignette (`postfx.cpp:1865-1882` then `:1895-1908`), so `ivExposure` re-exposes the grain and CA, and partly cancels the vignette. IV's grade is inside the composite, before the lens effects. | **Bug** | C++ only: move the `DrawIVGrade()` block above the lens group in `ColourFilter_switch` (`postfx.cpp:1859`). |
| D13 | **No bloom pre-blur, no DoF, no volumetric light add in this pass.** We cover DoF (`DOF_Bokeh`, 22 taps), shafts (`SunShafts`) and MB (`MotionBlur_Burnout`) with separate, better passes. The IV light-volume integral (`light_volume/deferred_lighting_ps20_smooth.hlsl`, 34 slots) has no equivalent — we have **no volumetric light contribution at all**. | Deliberate simplification (DoF/MB/shafts) / genuine gap (light volume) | Light volume: PS+VS pair, needs per-light proxy geometry from `forwardplus.cpp`; `OV §2.4/§2.5`. |
| D14 | **"Bloom" has no bright-pass source.** It high-passes the *same* display frame it is grading, so it cannot glow from HDR values >1 — the target is 8-bit (`s_ivGradeRas`, `RwRasterCreate(w,h,camRas->depth,…)`, `:1440`). | Structural | Requires the grade to run on an HDR/linear intermediate, or to consume `Bloom_Extract`+`Bloom_IVBlur` output (D2). |
| D15 | **No player/stencil masking, no jitter, no CoC quantiser, no motion-blur matrix reprojection.** | Deliberate (out of scope for a grade) | n/a |

---

## 3. Dead / broken paths

| # | Item | Evidence | Status |
|---|---|---|---|
| B1 | **`s_ivExpCap` is a frozen literal.** | Declared `static float s_ivExpCap = 1.5f` at `:67`. Grep across `src/`: only the declaration (`:67`), the comment (`:1485/1498`) and the two reads (`:1499/1501/1502`). No writer, no INI key, no menu row. | Dead knob. The comment says "INI plumbing TODO" — the plumbing never landed. `s_ivExpCap` is a constant. |
| B2 | **`ivExposure` is capped at 1.5 and unreachable above that — but only sometimes.** | `:1499` fires only when `s_cfTonemapRanFrame` is true. `MODE_GTAIV` sets `colorFilter = COLORFILTER_GTAIV` (`modeManager.cpp:383`); `ColourFilter_switch` case `GTAIV` never calls `ColourFilter_Modern` (`postfx.cpp:1842-1850`), so `s_cfTonemapRanFrame` stays false (reset each frame at `postfx.cpp:1358`). | **The cap never applies on the shipped IV preset**, so `ivExposure=2.5` multiplies a display frame by 2.5 raw → anything above sRGB 0.4 hard-clips. The cap only fires in mixed configs (`ivMode=1` with `colorFilter=MODERN`/PBR). |
| B3 | **`tonemapRanThisFrame` still gates exactly one thing** | `:1492-1493` (consume) → `:1499` (clamp). | Yes, still live, but only as the B2 cap. Nothing else. Lifetime is sound: the entry reset at `postfx.cpp:1358` (`PostfxResetTonemapHandshake`) prevents a stale-true from a frame where `DrawIVGrade` bailed at H2/H5/H11. |
| B4 | **`s_cfTonemapSceneLuma` is log-only.** | Written `:631`, read `:1501` inside a `dbglog_throttle`. | Pure diagnostics. |
| B5 | **`ivSaturation` / `ivCurves` never reach a shader** | see D7 | Dead config, 9 write sites plumbing a value nobody reads. |
| B6 | **The `pRasterFrontBuffer` guard is dead.** | `:1406-1407` bails, and the pointer is never referenced again — the pass reads `camRas` (H11) and writes the camera RT (H16). | At worst it silently skips a frame for a reason that cannot affect the pass. |
| B7 | **`s_ivGradePS` is never released on device reset.** | Created with raw `dev->CreatePixelShader` (`:1342`), cached in a file-static (`:1319`), never freed. `ReleaseDefaultPoolResources` frees `s_ivGradeRas` (`postfx.cpp:2201-2203`) but has no case for `s_ivGradePS`; `EnsureIVGradePS` early-returns on a non-NULL pointer (`:1335`). | **Broken.** A `D3DPOOL_DEFAULT` shader is lost on `Reset` (alt-tab, resolution change). After a reset the grade binds a stale pointer; `s_ivGradePSFailed` is not consulted either (it is only set on create failure). Contrast with every other shader, which uses `RwD3D9CreatePixelShader` and is recreated by the RW device-lost handler. |
| B8 | **Sticky-failure latch.** | `:1335-1336, 1346-1347` | One transient `CreatePixelShader` failure (e.g. mid-reset) disables the grade for the rest of the session with no recovery path. |
| B9 | **No texture address mode set.** | `:1468-1476` sets filter but not `rwRENDERSTATETEXTUREADDRESS`. | The ±4 px bloom taps at the frame border inherit whatever the previous pass left (commonly WRAP), so the left edge can sample the right edge. At `ivBloomIntensity=0.05` this is invisible; at higher values it becomes an edge bleed. |
| B10 | **No blend state set.** | H13 sets alpha-test/Z/fog/filter only. | The pass relies on `ImmediateModeRenderStatesSet` (game wrapper, `postfx.cpp:49`) for `SRCBLEND/DESTBLEND/ALPHABLENDENABLE`. It happens to be safe today (`RwIm2D` re-emits the RW-cached blend, and the quad alpha is 255 → replace), but it is the only full-screen pass in the tree that does not state its own blend. A preceding additive pass would turn the grade into an `ONE/ONE` add. |
| B11 | **`rawGeomSaved = false` at `:1539` is dead.** | local `bool` leaving scope. | Cosmetic. |
| B12 | **The shader has no source of truth.** | §0 | Any math change means hand-patching a 1380-byte array. Blocks D2/D3/D5/D6/D8/D9/D11. |
| B13 | **Two redundant full-screen blits per frame.** | `:1455` and `:1544`. | A 67-slot grade over an 8-bit frame costs 2 blits. Could be 0 by sampling `pRasterFrontBuffer` with scaled UVs (needs `uv *= c2.zw` in the shader). |

---

## 4. Register budget

### 4.1 What the grade uses now

| Slot | Contents | Note |
|---|---|---|
| `c0` | `(exposure, desaturation, gamma, 0)` | per-draw |
| `c1` | `(vigIntensity, vigRadius, vigContrast, bloomIntensity)` | per-draw |
| `c2` | `(1/w, 1/h, 0, 0)` | per-draw; only `.xy` used |
| `s0` | scene copy | — |

Nothing else. `c3`–`c6` in the disassembly are compiler `def` immediates, not host uploads.

### 4.2 What is free

`DrawIVGrade` is a **standalone full-screen pass**: it uploads its constants immediately before its own
draw, exactly like `postfx_newfx.cpp`'s passes. It therefore owns `c0..cN` for the duration of that draw
and collides with nothing. Concretely free **in this pass**: `c3`–`c223` (221 vectors) and `s1`–`s15`.

Two project-wide reservations still apply and must be honoured:

| Reservation | Source | Consequence for this pass |
|---|---|---|
| **`c22`/`c23` are FROZEN to `pipeUploadPBR()`** | project rule (`AGENTS.md`, PBR register layout) | Applies to shaders that *share* the PBR constant block (vehicle/building `main()`). The grade does **not** share it — but do not put grade params at `c22/c23` anyway: it trains the wrong habit and breaks immediately if the grade is ever folded into `VehiclePBR_Modern.hlsl`'s constant file. Use `c3`–`c9`. |
| **`s9` + `c49`–`c52` are the universal noise system** | `noise.h:76-82`, `noise.cpp:897-935` | `s9` = utility tile `R=Perlin G=Worley B=white A=blue`; `c49 = {noiseScale, worleyScale, coverage, erosion}`, `c50 = {whiteScale, blueScale, time, frame}` (time/frame stamped by `BindAndUpload`), `c51`, `c52`. **Do not upload `c49`–`c52` by hand** — `BindAndUpload` writes raw *and* RW so the RW cache can't go stale. `s9` is bound **LINEAR** (`noise.cpp:913-917` → `D3DTEXF_LINEAR`); the IV grain needs **POINT** (`OV §4.2`), so a grain consumer must override the filter and restore it. |

### 4.3 Budget for the missing stages

| Stage | Slot cost | Where it belongs | Samplers | Notes |
|---|---|---|---|---|
| IV grain | **~14** (1 `texld`) | `Grain.hlsl` as `grainMode=2`, or in-grade | `s9` | `(s9.b − 0.5)·c49.z·1.1`, `c50.xy` temporal, `c49.xy = 1/1.5625`. Free-rides the reserved block. |
| IV bloom (25-tap separable) | **124** per axis pass | new `Bloom_IVBlur.hlsl`, CSO **286** (281-284 are claimed by `OV §5.1`, 285 is taken by F9) | `s0` | **Cannot** go in-grade: separable needs 2 draws. Runs on mip0 of the existing pyramid. |
| DoF | 5 taps (IV) vs our 22 | already covered by `DOF_Bokeh` | — | Ours is better; no work. |
| Sun shafts | — | already covered by `SunShafts` | — | — |
| Light volume | **34** + a VS | new PS+VS pair (`OV §2.4/§2.5`), additive | `s0` = depth | Needs per-light proxy geometry; not a grade concern. |

If grain + colour stages are folded into the grade PS: **67 + 14 + 9 ≈ 90 slots, 2 samplers** — far
under the `ps_3_0` ceiling (512 slots, 16 samplers). There is no budget pressure to fold anything;
the constraint is architectural, not numeric.

---

## 5. Fix list (ordered)

Phases are ordered so that every no-rebuild fix lands before the shader source has to be restored.

### Phase 0 — no shader rebuild (C++/config only)

**F1. Convert `ivExposure` into the correct (gamma) space.** `postfx_colourfilter.cpp:1505`
```c
float c0[4] = { ex, config->ivDesaturation, gm, 0.0f };
```
→
```c
float exDisplay = powf(ex, 1.0f/2.2f);   // display-space equivalent of a linear exposure multiply
float c0[4] = { exDisplay, config->ivDesaturation, gm, 0.0f };
```
*Effect:* `ivExposure=2.5` becomes 1.52× on screen instead of 2.5× — removes the hard clip of everything
above sRGB 0.40 and the "grey wash". *Risk:* low; **changes the look of every tuned IV preset**, so
re-tune `ivExposure` (2.5 → 2.2–2.6 keeps the intended brightness) and note it in the changelog.
*Rebuild:* no.

**F2. Delete the exposure cap (or gate it on the linear value).** `:1492-1503`
After F1 the cap's premise is gone. Remove `s_ivExpCap` (`:67`) and the `tonemapRanThisFrame` block at
`:1492-1503`, or keep it with `ex > s_ivExpCap` replaced by `exDisplay > s_ivExpCap`. If kept, expose
`ivExposurePostCap` through the four config sites listed in `OV §5.3`.
*Effect:* the `ivExposure` slider becomes usable over its full 0–3 range (`debugmenu_ui.cpp:1271`).
*Risk:* low. *Rebuild:* no.

**F3. Retune the vignette radius.** `main.cpp:5362` and `:6139`/`:6246`
`ivVignetteRadius` `0.70 → 0.40` (and `ivVignetteContrast` `1.5 → 1.2` if the edge is too soft).
*Effect:* replaces the flat 14 % darkening with an actual radial falloff — `V(centre)=1.00`,
`V(mid-edge)=0.93`, `V(corner)=0.85` at the same intensity.
*Risk:* low, look change only. *Rebuild:* no.

**F4. Move the IV grade ahead of the lens group.** `postfx.cpp:1895-1908` → before `:1865`.
*Effect:* `ivExposure` no longer re-exposes film grain and CA, and no longer cancels the standalone
vignette. Matches IV's ordering (grade inside the composite, lens effects after).
*Risk:* medium — the pass ends with `UpdateFrontBuffer()` (`:1544`), so the lens group will see the
graded frame; verify `DrawFilmGrain`/`DrawVignette` still self-sync. *Rebuild:* no.

**F5. Fix the device-reset lifetime.** `:1319-1352` + `postfx.cpp:2200-2203`
Either replace `dev->CreatePixelShader` with `RwD3D9CreatePixelShader(g_ivGradePSBytes, &s_ivGradePS)`
(preferred — it registers the shader for automatic recreation), or add
`if(s_ivGradePS){ ((IDirect3DPixelShader9*)s_ivGradePS)->Release(); s_ivGradePS=NULL; s_ivGradePSFailed=false; }`
to `ReleaseDefaultPoolResources` next to the `s_ivGradeRas` destroy.
*Effect:* the grade survives alt-tab / resolution changes instead of binding a dangling pointer.
*Risk:* low. *Rebuild:* no.

**F6. Set the texture address mode and an explicit blend.** `:1471` / `:1473-1475`
```c
RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSCLAMP);
RwRenderStateSet(rwRENDERSTATESRCBLEND,  (void*)rwBLENDONE);
RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
```
*Effect:* stops the ±4 px bloom taps from wrapping across the frame edge; makes the pass immune to an
inherited additive blend. *Risk:* very low. *Rebuild:* no.

**F7. Drop the dead `pRasterFrontBuffer` guard.** `:1406-1407`. *Effect:* none visible; removes a
spurious skip path. *Risk:* none. *Rebuild:* no.

**F8. Delete the dead `rawGeomSaved = false`.** `:1539`. Cosmetic.

### Phase 1 — restore the shader source (unblocks everything else)

**F9. Re-author `shaders/ps/IVGrade.hlsl` and build it as a CSO.** New file; CSO id **285**
(280 reserved for W8-3 per `OV §5.1`, 281-284 claimed by the other IV plan).
Copy the disassembly in §1.2 verbatim, then delete `g_ivGradePSBytes` (`:1084-1316`) and
`EnsureIVGradePS`'s raw `CreatePixelShader`, reusing the standard CSO loader (`pipelinecommon.cpp:694`).
*Effect:* none visually (byte-for-byte round trip — verify with `fxc /Fc` that it compiles to 67 slots).
*Risk:* low-medium; the only real risk is a transcription error, which the `/Fc` diff catches.
*Rebuild:* **yes** (first time the grade enters the build).
*Note:* without this, every remaining phase is a hand-patch on a 1380-byte array.

### Phase 2 — shader-correctness fixes

**F10. Remove the bloom↔vignette cross-gate.** Delete the `cmp` at P21 and P23 and the two
`add 0.001,-c1.x/c1.w`. *Effect:* `ivBloomIntensity` and `ivVignetteIntensity` stop disabling each other.
*Cost:* −2 slots (67 → 65). *Risk:* low. *Rebuild:* yes.

**F11. Stop halving the desaturation.** P14 `mul_sat r0.w, c3.z(0.5), c0.y` → `mul_sat r0.w, 1.0, c0.y`.
*Effect:* the slider matches its label; halve the default (`ivDesaturation 0.15 → 0.075`) to preserve the
current look at the default. *Cost:* 0 slots. *Risk:* low. *Rebuild:* yes.

**F12. Switch luma to Rec.709.** P12 `c4.xyz 0.299/0.587/0.114` → `0.2126/0.7152/0.0722`.
*Effect:* matches IV's `c254`; slightly greener neutral axis. *Cost:* 0 slots. *Risk:* low. *Rebuild:* yes.

**F13. Add the missing colour stages and wire the dead knobs.** After P19 (before the bloom add):
```hlsl
// contrast (IV deSatContrastGamma.y) in log space, per channel
c = pow(max(c, 0), ivContrast);                       // 3 slots (already have pow via log/exp)
// ColorShift: luma-scaled tint
float L2 = dot(c, float3(0.2126,0.7152,0.0722));
c += ivShiftW * L2 * ivShiftRGB;                      // 1 dp3 + 2 mad
// ColorCorrect: per-channel gain
c *= ivCorrectRGB;                                    // 3 mul
```
Bind `ivContrast`/`ivShiftRGB`/`ivCorrectRGB` to `c3`/`c4` and **reuse the existing
`config->ivSaturation` and `config->ivCurves` fields** for two of them instead of adding new keys.
*Effect:* the actual IV colour signature (contrast + tint + per-channel gains) — the single largest
"looks like IV" delta after the bloom. *Cost:* **+9 slots** (67 → ~73; 65 after F10). *Risk:*
low-medium (needs default values that don't break the current tuning — start with identity
`ivCorrectRGB=(1,1,1)`, `ivShiftW=0`, `ivContrast=1.0`). *Rebuild:* yes.

### Reconstruction status (added 2026-10-25)

`shaders/ps/IVGrade.hlsl` now exists as a source-of-truth reconstruction of
`g_ivGradePSBytes` (F9's first half).

- Compiles clean with the project FXC (`9.29.952.3111`): `-T ps_3_0 -E main` →
  **67 slots (5 texture, 62 arithmetic)**, matching §1.2 exactly.
- Contract names: `gradeParams0/1/2 : register(c0/c1/c2)`, `sceneTex : register(s0)`.
  The CPU-side `pow(ex, 1/2.2)` (F1) is documented in the file header — the shader
  multiplies display-referred values and does not re-encode.
- Both cross-gates (P21/P23) and the half-strength desaturation (P14) are preserved
  and marked as vanilla quirks; F10/F11 remain separate changes.
- Deliberate non-differences: (a) the final store is `saturate`d (`cmp_sat` instead
  of `cmp`; identical on the shipped UNORM8 target), and (b) the redundant P15 `cmp`
  guard (`s <= 0` selects the ungraded colour; math-equivalent) is not reproduced.
- Build auto-discovery picks the file up (`python tools/fast_build.py --shaders` →
  `OK: IVGrade.hlsl`; `IVGrade.cso` lands in the gitignored `resources/cso/`). It is
  **not** in `Resource.rc`/`resource.h`: the runtime still binds `g_ivGradePSBytes`,
  and the CSO wiring/removal remains a follow-up.

### Phase 3 — the missing stages

**F14. IV scalable bloom.** New `shaders/ps/Bloom_IVBlur.hlsl` (CSO **286**), one entry,
axis chosen by the uploaded constant; **124 ps_3_0 slots measured** (`OV App. A`). Insert into the
existing bloom pyramid at mip0, replacing `Bloom_Upsample`'s tent when `bloomIVScalable=1`.
Then delete P7-P9 from the grade (or default `ivBloomIntensity=0`).
*Effect:* real IV glow — no threshold, radius `h/45 px`, energy ×1.10.
*Risk:* medium — new pass, new rasters, needs a bright-pass in front of it (keep `Bloom_Extract`,
`bloomThreshold≈1.0` per `OV §2.3`). *Rebuild:* yes (new shader + `Resource.rc`).

**F15. IV film grain.** Add `grainMode=2` to `shaders/ps/Grain.hlsl` (14 slots, `s9`):
```hlsl
float2 dims = 256.0;
float2 tshift = floor(frac(c50.xy) * dims);
float2 gpos = (vpos.xy - 0.5) * c49.xy;   // c49.xy = 1/1.5625
float2 guv = frac((gpos + tshift + 0.5) / dims);
grain += (tex2Dlod(noiseTex, float4(guv,0,0)).b - 0.5) * c49.z * 1.1;
```
Call `SkyNoise::BindAndUpload(9, dev, …)` before the draw, and **override `s9` to `D3DTEXF_POINT`**
(`noise.cpp:913-917` binds LINEAR) then restore. Move the call into the pre-tonemap FX group
(`postfx.cpp:1670-1687`) so the grain lands in linear pre-exposure space like IV.
*Effect:* the authored Rockstar grain, correct space, 1 tap instead of ~40 ALU.
*Risk:* medium (sampler-state ownership; TAA interaction — white noise + integer slide crawls under TAA,
see `OV §4.5`). *Rebuild:* yes.

**F16. Remove the two full-screen blits.** Extend `c2` to `(1/w, 1/h, w/fbW, h/fbH)`, sample
`pRasterFrontBuffer` directly with `uv * c2.zw`, drop H10/H11 and the `UpdateFrontBuffer` at H18.
*Effect:* −2 full-screen blits and −1 `w×h` raster (≈8 MB at 1080p).
*Risk:* medium — the vignette centre and the `±4 px` bloom offsets must be recomputed in the scaled UV
space (offset becomes `4/fbW`, `4/fbH`); test the padded-raster band on a 2048² front buffer.
*Rebuild:* yes.

**F17. (Optional) Wire the IV light-volume add.** PS 34 slots + the paired VS — see `OV §2.4/§2.5`.
Out of scope for the grade; listed for completeness.

---

## 6. One-paragraph summary for the orchestrator

`DrawIVGrade` is one 67-slot `ps_3_0` draw (5 taps: 4 bloom + 1 scene) fed by a camera-sized copy of the
frame, gated on `ivMode` alone, costing 2 full-screen blits. It is **not** an IV grade: it is
exposure × desat(½) × pow(1/γ) × weak vignette + a 4-tap high-pass. The three highest-value fixes need
**no shader rebuild at all** — put the exposure multiply into gamma space (`pow(ex,1/2.2)`, F1), delete
the cap that never fires on the shipped preset (F2), and retune the vignette radius off the corner
(F3). The genuinely missing IV stages are bloom (124 slots, separate pass, separable so it cannot be
in-grade), grain (~14 slots on the reserved `s9`/`c49`-`c52`, must override the LINEAR bind to POINT),
and the colour-correct/shift/contrast triple (~9 slots, finally giving `ivSaturation`/`ivCurves` a
consumer). Two latent breakages are worth fixing regardless: the shader is created with a raw
`CreatePixelShader` and never released, so it goes stale after a device reset (F5), and it has no HLSL
source in the tree at all (F9).

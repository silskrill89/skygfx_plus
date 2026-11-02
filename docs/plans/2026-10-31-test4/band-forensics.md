# Band Forensics — Mirrored-World Band (run 01:24–01:27, Xbox pipeline, water OFF)

Run: `E:\games\gtasa_skygfx_plus\skygfx_dbg.log` (01:24–01:27, pipeline=Xbox,
pipeChainEnable=0, **waterParallaxEnable=0** → all water draws FFP).
Symptom: horizontal band of vertically-reflected world content (buildings/trees/road)
over land, hard horizontal seams, 3 exterior screenshots. Not our reflection RT
(reflRT: 0 log hits).

---

## 1. Raster identity verdict — s0=1035AF80 / 10355B10

**These are D3D9 base-texture pointers, not RwRaster pointers.** They come from
`d3d9device->GetTexture(0, &staleS0)` at `src/core/main.cpp:5873` — a different
object family than the RwRaster pointers the log prints elsewhere
(pRasterFrontBuffer=04C20A38, camRas=04C1EFC0, CMirrors::pBuffer=1C6D2848).
Pointer inequality with those is therefore *not* identity proof in either
direction.

No other receipt in this log names either pointer. The one-shot identity receipt
that would settle it (dims/format/pool — `src/render/waterPipe.cpp:2050-2080`,
"[water] core disabled by config ... s0=%p (%ux%u fmt=%u pool=%u)") is **not in
the deployed binary**: 0 log hits, as are the `fogfix` receipts. The run's ASI
predates the current source's test-4 hardening.

Circumstantial identity (strong):

| Receipt | staleVS | stalePS | s0 | Inference |
|---|---|---|---|---|
| site1 (log:5681) | 0CF94508 | 0CF94CD8 | 1035AF80 | Last shader-bound draw = Xbox building atomic. PS 0CF94CD8 = `makePS(101)` = **IDR_SIMPLEPS** (log:1196); VS 0CF94508 = same Xbox-building family (BPIPE receipt handles 0CF94xxx/0CF95xxx). s0 = that atomic's diffuse D3D texture — world-texture family (`veg_largefurs05/06` classified+drawn immediately prior, log:5676-5680). |
| site2 (log:5683) | NULL | NULL | 10355B10 | site1's exit forced FFP (VS/PS NULLed). Between the sites, vanilla RenderWater binds waterclear256 (exe 0x6EFB78) → s0 = **waterclear256's D3D texture**. |

**Verdict:** site1's s0 = last Xbox-building atomic's diffuse (world texture);
site2's s0 = waterclear256. Neither is proven to be a screen/reflection-family
raster, and neither needs to be — see §2. Next run, the already-in-source
one-shot receipt (waterPipe.cpp:2050-2080) will name dims/format/pool on sight.

## 2. Draw path / rebind timing — the water fallback is EXONERATED

**The flatpath s0 receipt is captured BEFORE the rebind — benign capture, not
the draw-time binding.**

Order inside `WaterDirectFlush_Draw` (src/core/main.cpp):

1. `:5870-5874` — diagnostic capture (GetVertexShader/GetPixelShader/GetTexture 0/3) ← **receipts print this**
2. `:5894` — `waterPipe_directFlushRoute()` → with waterParallaxEnable=0 returns
   false at the FIRST gate (waterPipe.cpp:2044) **before touching any device state**
   (the deployed binary has no `waterPipe_BindVanillaRaster3L()` call there yet).
3. `:5932-5935` — rebind `*(RwRaster**)0xC228A8` (waterclear256Raster, non-NULL
   vanilla global) via `RwRenderStateSet(rwRENDERSTATETEXTURERASTER, ...)`.
   RW cache at that moment holds the last building/veg raster (those atomics bind
   through RW) → **real cache transition** → device s0 = waterclear256 at draw.
4. `:5954` — the Im3D draw.

Site2: vanilla already bound waterclear256 between the sites (cache AND device
agree) → the rebind value-compares to a no-op, device already correct.

All other water draws (hooked RenderIfDoesntFit flushes,
`CWaterLevel__RenderAndEmptyRenderBuffer_hook`, main.cpp:6120-6147) go vanilla
fall-through: raw VS/PS NULL + the vanilla draw, which binds waterclear256
through vanilla RW code. No raw `SetTexture(0, …)` writer exists in the
water→site1 window (forwardplus.cpp:1879 raw-NULLs s0 but the next RW bind of a
*different* raster is a real transition; the veg atomics rebind through RW right
before site1).

**Conclusion: every water FFP draw in this run sampled waterclear256. The
mirrored-world band is NOT drawn by the water fallback.** The receipts are a
pre-rebind snapshot of the stale state the fallback *inherited* — useful as a
leak probe (they prove the Xbox building VS/PS survives across vanilla FFP
consumers — see §4), not as the band's texture.

## 3. PostFX scene-copy leak check — postfx EXONERATED

- The only scene-family raster alive in Xbox mode is `pRasterFrontBuffer`
  (0xC402D8 → 04C20A38, 2048x2048; log:1073). SSAO=0, smaa=0, normalBuf=0,
  pipeChain=0 (PostFX-REPORT log:5646) → no SSAO/SMAA/IV-grade rasters exist.
- postfx binds it via `RwRenderStateSet(rwRENDERSTATETEXTURERASTER, …)`
  (postfx_colourfilter.cpp:120/199/319/759/846/939, postfx_newfx.cpp:519) — the
  RW setter keeps cache+device in sync. Its raw `SetTexture(0,…)` sites
  (postfx.cpp:3372/3392, postfx_ssao.cpp:*, postfx_luminance.cpp:*) are paired
  with restores/NULLs and most are in disabled passes.
- postfx runs at **frame end, after water**; the next frame's geometry rebinds s0
  through RW long before water site1. s3=00000000 in every flatpath receipt
  (g_iblTex unset — IBL off). No postfx-owned s0 leak reaches the water draws.

## 4. The actual band source: the vanilla MIRROR pass (CMirrors::pBuffer)

The only thing in the frame that **contains vertically-reflected world content**
is `CMirrors::pBuffer` (0xC7C71C → **1C6D2848**, 2048x1024 CAMERATEXTURE —
mirrorPass.cpp:64-67). The log proves the whole mirror chain is live every frame:

- `[MirrorPass] hook: TypeOfMirror=2 … pBuffer=1C6D2848` every 2s window
  (log:1008 et seq.) — mirror active the whole session; hook installed at
  0x53EABA (log:729), `mirrorDistortionFix=0` → **the vanilla fullscreen blit
  stands** (log:1009, mirrorPass.cpp:477-489).
- `[BUILDING] cb camRas=1C6D2848 2048x1024 != screen 1920x1080 — non-screen
  raster LIVE during building cb` (log:977/1316/1401…, probe at
  buildingPipe.cpp:3310-3318) and `[BLIT] cb=Xbox camRas=1C6D2848` (log:987 et
  seq.) — **the reflected world renders through our Xbox building pipe with the
  camera on the mirror buffer**, binding Xbox building VS + IDR_SIMPLEPS.
- `[FxAlpha] Coronas.Reflections camRas=1C6D2848` (log:976 et seq.) — the
  non-screen raster is live into the effects phase too.

**Leak path (file:line):**

1. Inside the vanilla `CMirrors::RenderMirrorBuffer` span (wrapped by
   `MirrorPass_RenderMirrorBuffer`, src/render/mirrorPass.cpp:443-489), the
   reflected-world render runs through our Xbox building pipe → leaves
   **Xbox building VS (0CF94508) + IDR_SIMPLEPS (0CF94CD8) device-bound**.
   This exact survival across vanilla FFP consumers is independently proven by
   the water flatpath receipts (staleVS/stalePS at site1, §1) — the W1/W5 leak
   family documented at main.cpp:5830-5835/5999-6008 ("vanilla Im3D sets FVF/decl
   through RW but never unbinds shaders").
2. The vanilla **fullscreen blit** of pBuffer (the "plain fullscreen
   else-branch", mirrorPass.cpp:7-11/440) then draws its screen-space
   Im2D/Im3D quad **with those stale shaders still bound**. A screen-space
   (RHW-pretransformed) quad processed by the building VS — which expects
   object-space verts + its own WVP constants, and reads them through the
   building pipe's vertex declaration, not the Im2D layout — projects to an
   arbitrary screen region.
3. Result: the pBuffer content — **vertically-reflected buildings/trees/road** —
   smeared as a horizontal band with hard seams (per-strip/per-quad projection
   discontinuities), drawn at frame end (mirror pass sits immediately before
   DrawFinalEffects, 0x53EABA vs 0x53EBE9) → **on top of the world, over land**.

Every observed property of the band matches: content (reflected world = pBuffer),
orientation (vertical reflection = mirror camera), shape (horizontal band =
misprojected blit quad/strips), seams (hard horizontal edges), layering (drawn
last). The water fallback cannot produce reflected-world content because its
draw-time s0 is waterclear256 (§2).

Secondary (less likely) variant to keep in mind: if the vanilla blit is actually
state-clean in some frames, the alternative drawer is the mirror pass's
world-space "mirror-textured geometry" (stadium-quad family,
mirrorPass.cpp:517-518) misprojecting through the same leaked shader state —
same owner, same fix.

## 5. Recommended fix owner: **mirrorPass** (not waterPipe, not postfx)

- **Owner: `src/render/mirrorPass.cpp`** — `MirrorPass_RenderMirrorBuffer`
  already wraps the exact call site (0x53EABA).
- **Wrong fix:** park/unpark shaders around the whole
  `CMirrors__RenderMirrorBuffer_orig()` call — the reflected-world render inside
  the span legitimately needs the building pipe.
- **Right fix (either):**
  1. **Enable/verify `mirrorDistortionFix`** — our `MirrorPass_Composite`
     (mirrorPass.cpp:231-395) replaces the vanilla fullscreen blit with a PS
     composite that does its own park/restore (`MirrorPass_CompositeRestore`),
     bypassing the stale-shader blit entirely. Fastest confirmation: set
     `mirrorDistortionFix=1`, re-run, band gone ⇒ verdict proven.
  2. Or add a **finer FFP-force at the vanilla blit site** inside
     RenderMirrorBuffer (NULL VS/PS on both layers + `_rwD3D9Last*Used` seeds at
     0x8E2448/0x8E244C — the main.cpp:5923-5930 idiom) so the blit quad runs
     fixed-function like vanilla.
- **Add a one-shot state receipt** at MirrorPass entry (device VS/PS + s0 via
  GetTexture, same pattern as main.cpp:5867-5884) to confirm the blit-time
  stale pair on the next run.
- **Keep** the already-in-source `waterPipe_BindVanillaRaster3L()`
  (waterPipe.cpp:1962-1994, wired at :2085/2089/2094) — correct 3-layer
  hardening for the water fallback, but it is *not* the band fix; build+deploy
  it anyway so the next run's one-shot identity receipt (§1) names the stale
  s0 rasters definitively.

## Receipt index

| Evidence | Location |
|---|---|
| flatpath first hits s0=1035AF80/10355B10 | log:5681-5683 |
| stalePS 0CF94CD8 = makePS(101) = IDR_SIMPLEPS | log:1196; resources/resource.h:5 |
| pRasterFrontBuffer=04C20A38 (2048x2048), camRas=04C1EFC0 (1920x1080) | log:1073 |
| MirrorPass hook/gate, pBuffer=1C6D2848, fix off | log:1008-1009, 729 |
| non-screen raster live during building cb / coronas | log:976-977, 1316, 1401 |
| capture-before-rebind order | main.cpp:5873 vs 5933-5935 |
| water gate returns before device writes | waterPipe.cpp:2043-2087 |
| vanilla fall-through gate | main.cpp:6120-6147 |
| mirror hook install / wrap | mirrorPass.cpp:595, 443-489 |
| camRas probe | buildingPipe.cpp:3310-3318 |

# 2026-10-29 - Giant teal quads diagnosis (00:42-00:43 run, ASI 2,451,456)

Status: READ-ONLY diagnosis, no source edited.
Evidence base: log E:/games/gtasa_skygfx_plus/skygfx_dbg.log (run [BUILD] ... size=2451456 mtime=2026-10-29 00:39:57, logged 00:40:50, 13878 lines to 00:44); 8 ShareX captures in
C:/Users/aaaaaaaaa/Documents/ShareX/Screenshots/2026-10/ (3 JPG + 5 PNG, mtimes 00:42:22.064-00:43:58.259, all 1920x1080); source tree src/render/waterPipe.cpp (1762 lines),
src/core/main.cpp (8447 lines), shaders/vs/Water_VS.hlsl, shaders/ps/Water_Parallax.hlsl, src/render/pipelinecommon.cpp. Prior-art: docs/baseline/2026-10-27-outdoor-1809/findings.md,
docs/plans/2026-10-24-corona-blackquad-diagnosis.md.

---

## 0. Verdict (one paragraph)

The giant dark-teal slabs are **the SA water grid itself, mis-projected** - not coronas, not shadows, not sprites. Every one of the 8 captures sits at the Santa Maria/Verona waterline
(pier, swim, underwater); the slabs are flat, hard-edged, water-body-coloured (#3E5C55-family; measured fills (72,120,120)/(80,128,128) = the timecycle deep-water colour after the
x*0.65+0.27 quirk), and they sit exactly where water planes are. The water **pipe path is healthy** - the same frames render the near/mid-field water with correct parallax waves
(textured, spec, shore fade), and all its log receipts are clean. The defect is the **second, separate water flush**: SA water is emitted partly as a **shared-vertex grid mesh**
(live sample 621 verts / 3432 indices, ratio 5.53) which fails WbBufferCountsSane (nIdx <= nVerts*3 quad-ratio check), so waterPipe_setRenderState bails and main.cpp
else-branch hands the buffer to the **vanilla CWaterLevel__RenderAndEmptyRenderBuffer** - which rasterises through the **stale vertex/pixel shader still bound from the previous
pipe draw** (the code own comment at main.cpp:5595-5599 names this exact failure class, but the defensive guard only covers the *initial* canUseWaterPipe gate, not the
*mid-setup* sane-check bail). A water grid projected by a building/vehicle object WVP lands as giant screen-covering slabs with garbage depth; the flat teal fill is the water
texture/body colour shaded through the wrong constants. The old (2,2,2) black quads were the **sprite-flush blend=0 REPLACE** family - that lane is now clean in this build
(see section 3, corona receipts) and is NOT the teal emitter.

---

## 1. Per-frame correlation table

Screenshot mtimes are ShareX wall-time; log timestamps are the game wall-time - same clock, direct match. Water verts = [water] entry receipt (verts= = TempBufferVerticesStored
for that flush). Log sampling is 2 s-throttled (dbglog_throttle, 256-bucket FNV, skygfx.h:89-101), so between two receipts the game ran ~60 frames; the receipts bracket each capture.

| # | Capture (mtime) | In-game | Nearest log receipts | Water flush sample | Corona/sprite/mirror receipts | Visible artifact |
|---|----------------|---------|----------------------|--------------------|-------------------------------|------------------|
| 1 | gta_sa_Fa3HyQjE78.png 00:42:22.06 | 11:19, beach ramp, car | 00:42:20.817 entry v=2044 i=3066; 00:42:22.820 entry v=40 i=60; [SKY] camRas=1920x1080 DEV ps=0/vs=0 | 40 v (sane, pipe ran) | CoronaDraw blend=1; MirrorPass gate off (no composite overdraw); CullResync mirror exit rw=1 | **Giant flat GREY slab** across mid-frame behind car (stale-PS colour variant, see H1); water above it textured/correct |
| 2 | gta_sa_fwOki5g45c.jpg 00:42:34.95 | 11:32, pier | 00:42:32.816 v=312; 00:42:34.808 v=344 i=516 chain ocean=298 lake=42 | 344 v sane | CoronaDraw blend=1; CoronaShield forced ONE/ONE (was blend=0 -> shield worked); PF gate mask=0x005F5 | **Giant flat TEAL quad, left ~35% of frame**, razor edge cutting through pier geometry; right-field water correctly parallax-shaded |
| 3 | gta_sa_33FAwmesLO.jpg 00:42:39.94 | 11:37, pier, looking along bay | 00:42:38.812 v=344; 00:42:40.815 v=344 | 344 v sane | CoronaDraw blend=1; FxAlpha Coronas.SunReflection layer-drift repaired | Teal slab clipped to **left frame edge only** (same slab, mostly off-screen); bay water correct |
| 4 | gta_sa_8nn00HyfHt.jpg 00:42:49.93 | 11:47, pier low angle | 00:42:48.812 v=120 i=180 ocean=84 lake=32; 00:42:50.810 v=120 | 120 v sane | CoronaDraw blend=1; EnvMap RenderSphereReflections done 256x256 | **Giant teal polygon over the bay**, hard polygon edges overlapping the correctly-rendered water surface behind it |
| 5 | gta_sa_l56CmrMkQc.png 00:43:19.11 | 12:16, boardwalk at water level | 00:43:18.825 v=248 i=372 ocean=120 lake=124; 00:43:20.842 v=248 | 248 v sane | CoronaDraw blend=1; MirrorPass TypeOfMirror=0 gate off | **Teal slab covers bottom ~45% incl. the boardwalk in FRONT of it** (garbage depth passes Z) - the drawn-OVER-geometry signature |
| 6 | gta_sa_qSqaV8zUs5.png 00:43:24.86 | 12:22, beached car | 00:43:24.841 v=56 i=84 ocean=52 | 56 v sane | CoronaDraw blend=1 | Teal quads bottom-right overlapping car + sea; road slab grey-teal |
| - | (between captures) | - | **00:43:12.808 entry v=621 i=3432 - ratio 5.53, FAILS WbBufferCountsSane -> vanilla bail. No [water] bad buffer line: throttle starved (section 4).** | 621 v **UNSANE** | - | (same artifact class; the one logged proof the bail path ran in this window) |
| 7 | gta_sa_Lk80k43oMu.png 00:43:50.90 | 12:48, UNDERWATER | 00:43:50.840 v=8 i=12 ocean=8 nearClip=0.300 | 8 v sane (tiny local body via pipe) | CoronaDraw blend=1 | **Giant dark-teal near-screen-rect slab, right 35%**, axis-aligned hard edges (mis-projection signature); bottom half white underwater blowout (separate defect) |
| 8 | gta_sa_Bn1MVuQikY.png 00:43:58.26 | 12:56, surfacing | 00:43:56.844 v=8 i=12 | 8 v sane | CoronaDraw blend=1 | Teal band with **hard diagonal clip edge** at the waterline (near-plane clip of mis-projected grid) + white blowout below |

Anomalies noted in passing (not the teal emitter):
- 00:43:44.839 entry v=1644 **nearClip=0.100** (all other frames 0.300) - underwater camera near-clip switch, expected.
- 00:43:12.808 is the **only** ratio-FAIL in the window; the artifact is continuous across all 8 captures, so the grid flush must run every frame and the throttle surfaced it
  exactly once. Treat the observed FAIL as a per-frame event.
- [CoronaFeed] pool= is garbage (negative and drifting) - the known uint16_t read bug from the 10-24 audit; cosmetic, unrelated.

---

## 2. Ranked emitter hypotheses

### H1 - PRIMARY: shared-vertex ocean-grid flush bails to vanilla draw with stale pipe VS/PS bound

**Mechanism.** SA CWaterLevel emits water in (at least) two flush shapes through CWaterLevel::RenderAndEmptyRenderBuffer:
1. **Individual quads** (rivers/lakes/ponds/near ocean tiles): 4 v / 6 i each -> combined ratio exactly 1.5. Every sane sample in this run matches (2044/3066, 344/516, 120/180, 56/84, 8/12 ...).
2. **The ocean grid**: a shared-vertex mesh. Live sample **621 v / 3432 i** = 23x27 verts, 22x26 quads = 1144 tris x 3 = 3432 indices. Ratio 5.53.

WbBufferCountsSane (src/render/waterPipe.cpp:513-517) enforces nIdx <= nVerts * 3 - valid for non-shared quads, **impossible for a shared-vertex grid**. The grid flush therefore
takes the bail at waterPipe.cpp:1057-1063 (g_waterParallaxActive stays false, early return before any shader/state is touched), and main.cpp:5739-5747 (else-branch of
if(g_waterParallaxActive)) calls the vanilla CWaterLevel__RenderAndEmptyRenderBuffer().

The vanilla Im3D draw goes through RW fixed-function path, which sets FVF/declaration but **does not unbind vertex/pixel shaders** - and the skygfx pipes bind theirs via raw
device->SetVertexShader/SetPixelShader (main.cpp:5655-5656, and every pipe in src/render/). At the moment CWaterLevel renders (per-frame order in this log:
... [BPIPE] cb=Sphere PS=simpleFogPS -> [CoronaDraw] -> [EnvMap] -> [SKY] -> [water] -> [Cull]/[BPIPE] cb=PS2 ...), a **stale pipe shader from the previous draw** is plausibly still
device-live - simpleFogPS (sphere buildings, logged every frame), IDR_GTAIVBUILDINGPS, or a vehicle/PBR VS carrying the last object WVP in c0-c3. The water grid
world-space vertices (ocean spans +/-3000, [UW] water.dat bbox -3000..3000) transformed by a **single building object WVP** project to giant screen-covering slabs with
unpredictable depth (hence drawn over the boardwalk/car - Z-test passes with garbage z), and the pixel output is the water texture shaded by the wrong constants -> flat
dark-teal (or grey, capture 1). The code documents this class verbatim at main.cpp:5595-5599 ("rendering the water TempBuffer with stale PBR VS/PS left bound from a previous
pipe was producing garbage water") - but the guard there only fires when the pipe is *disabled/missing*; the sane-check bail re-creates the same hazard with the pipe enabled.

**Why it looks like the old black-quad family but is not.** The 10-27 (2,2,2) quads were the sprite batch drawn in blend=0 REPLACE (10-24 audit Q1). This build is the
corona-flush-shield build and that lane is repaired in-window (section 3). The shared trait is only hard-edged flat quads over the frame; the emitter, colour and trigger are different.

**File:line anchors.**
- src/render/waterPipe.cpp:513-517 - WbBufferCountsSane (nIdx <= nVerts*3 rejects grids).
- src/render/waterPipe.cpp:1057-1063 - bail -> g_waterParallaxActive=false.
- src/core/main.cpp:5739-5747 - else-branch vanilla fall-through, **no shader unbind**.
- src/core/main.cpp:5604-5612 - initial-gate fall-through, same missing unbind (defensive).
- Log proof: 00:43:12.808 [water] entry ... verts=621 indices=3432 (ratio 5.53).

### H2 - SECONDARY: every early-out bail path has the same stale-shader exposure

waterPipe_setRenderState has three more early-outs after entry (water_nort at :1121, device-lost at :1093, shader-missing at :1091). Any of them also reaches the same
else-branch vanilla draw with whatever the previous pipe left bound. Not observed firing in this window (no water_nort/water_lost receipts), but it is the same one-line hole
as H1 fall-through and should be closed by the same patch.

### H3 - CLEARED: corona/sprite blend=0 black quads

All 57 [CoronaDraw] receipts in the window report pre-force dev blend=1 src=5 dst=6 - additive before the force, healthy. The only two blend=0 events of the whole run
(00:41:14.515 / .524, ~28 s before the first capture) were immediately shielded: [CloudShield] flush batch=sprites forced ONE/ONE + [FlushShield] restore ... captured REPLACE ->
canonical normal-alpha. The 10-24 fix (corona-flush-shield build) is working; this lane does not emit the teal slabs.

### H4 - CLEARED: mirror pass

[MirrorPass] every frame: mirrorDistortionFix=0 ... gate: ... no composite overdraw, TypeOfMirror=0 most frames, [CullResync] mirror exit rw=1. No mirror composite ran.

### H5 - CLEARED as primary: shadows / CPointLights fog cones / clouds

Zero shadow_contract/ShadowHeal receipts in the window (hooks installed at init, no per-frame shadow draw logged). Fog cones (CPointLights 0x700817) are additive
gradients - with the sprite lane now additive-forced they cannot paint hard-edged flat slabs, and the slab geometry (screen-space rectangles + waterline-hugging bands) matches
water planes, not cones. Cloud sprites: no cloud-layer receipts; sky renders clean gradient in all captures.

### H6 - ADJACENT (log separately, do not conflate): underwater white blowout

Captures 7/8 show the bottom ~40% blown to pure white (248,248,248) with a hard curved edge. That is the underwater chain ([UW] Water_Underwater PS res=276, compiled at
00:41:10/00:41:11) or the depth-based translucency blowing out when depthRT is invalid below the surface - a **different defect** from the teal slabs. Track separately.

---

## 3. Minimal fix spec (per hypothesis - no source edited here)

### Fix for H1 (core, 2 changes)

**Patch A - accept shared-vertex grids in the sanity check.**
src/render/waterPipe.cpp:513-517, replace the ratio bound:

    static bool WbBufferCountsSane(DWORD nVerts, DWORD nIdx)
    {
        // Non-shared quads: 1.5 idx/vert. Shared-vertex OCEAN GRID (live sample
        // 621v/3432i = 23x27 verts, 22x26 quads): up to ~6 idx/vert as rows share
        // vertices. The old nVerts*3 bound rejected the grid and bounced it to the
        // vanilla draw with stale pipe shaders bound (giant teal slabs, 2026-10-29).
        return nVerts > 0 && nVerts <= 8192 && nVerts <= nIdx &&
               nIdx <= nVerts * 6 && (nIdx % 3) == 0;
    }

Rationale: the pipe draw is index-buffer driven end-to-end (RwD3D9DrawIndexedPrimitiveUP at main.cpp:5701 with TempBufferRenderIndexList; WbClassifyFlush iterates verts
only) - shared vertices are handled correctly. The 4096 cap is raised to 8192 so a denser grid cannot re-bail. The 232v/990i implausible flush the old guard was written for
(4.27 ratio) would now pass - if that flush recurs and is genuinely foreign, it must be caught by a *different* signal (e.g. vertex-position bbox vs the water.dat bbox
-3000..3000), not by ratio.

**Patch B - unbind stale pipe shaders before EVERY vanilla fall-through.**
src/core/main.cpp:5739-5747 (else-branch) and :5604-5612 (initial-gate branch), before calling CWaterLevel__RenderAndEmptyRenderBuffer():

    // Vanilla Im3D draw: a stale pipe VS/PS still device-bound projects the
    // water buffer through the last object WVP (giant slabs). Force FFP.
    if(d3d9device){
        d3d9device->SetVertexShader(NULL);
        d3d9device->SetPixelShader(NULL);
    }
    *(void**)0x8E2448 = NULL;   // _rwD3D9LastVertexShaderUsed
    *(void**)0x8E244C = NULL;   // _rwD3D9LastPixelShaderUsed

(Place inside both branches, after the prevVS/prevPS/prevDecl->Release() calls, before the vanilla call. Same park-and-seed discipline as the water pin block at
main.cpp:5648-5671.)

### Diagnostic patch (required to prove H1 on the next run)

src/render/waterPipe.cpp:1057-1063 - the existing dbglog_throttle("water_badbuf") line was **starved in this run** (0 lines despite a proven FAIL sample). Replace with a
first-hit-unconditional receipt plus a counter:

    static int s_bailCount = 0, s_gridBailCount = 0;
    s_bailCount++;
    double ratio = nVerts ? (double)TempBufferIndicesStored / nVerts : 0.0;
    if(ratio > 3.0) s_gridBailCount++;
    if(s_bailCount <= 3 || (s_bailCount % 120) == 0)
        dbglog("[water] bail#%d v=%u i=%u ratio=%.2f grid=%d -> vanilla draw",
               s_bailCount, (unsigned)TempBufferVerticesStored,
               (unsigned)TempBufferIndicesStored, ratio, s_gridBailCount);

Also add a one-shot device-shader receipt in the same bail path (GetVertexShader/GetPixelShader handles) so the stale-bind identity is captured, not inferred.

**Acceptance:** after Patch A, grid= stops incrementing and the teal slabs disappear while the sane-flush water (textured waves) is unchanged. If slabs persist with grid=0,
H1 is falsified and the next suspect is the underwater chain (H6) - re-open with a per-flush vertex-bbox dump.

---

## 4. Caveats

- Log sampling is 2 s-throttled (dbglog_throttle, 256-bucket FNV, skygfx.h:89-101); per-frame flush *counts* between receipts are inferred, not measured. The single
  621v/3432i FAIL is treated as representative because the artifact is continuous across all 8 captures spanning 96 s.
- The exact identity of the stale shader at the vanilla draw instant was not captured (no per-flush VS/PS receipt on the bail path). H1 mechanism is inferred from the documented
  failure class (main.cpp:5595-5599) + the frame-order receipts (cb=Sphere PS=simpleFogPS runs immediately before the next sky/water pair every frame), not from a direct bind dump.
- src/core/main.cpp and src/render/waterPipe.cpp were read at the working-tree state of this diagnosis (main.cpp 8447 lines, waterPipe.cpp 1762 lines); line numbers are as-read.

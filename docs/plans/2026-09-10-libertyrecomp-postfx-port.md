# LibertyRecomp PostFX Port — Reconciliation & Ordered Implementation Plan

- **Date:** 2026-09-10
- **Branch:** `very-experimental`
- **Goal:** Reimplement/repair GTA SA post-processing by porting selected effects from the LibertyRecomp SDK (GTA IV recomp) into our RW 3.6 / D3D9 / SM3.0 ASI plugin.
- **Reference (READ-ONLY):** `E:\SDKs\LibertyRecomp-main\LibertyRecomp\gpu\` (HLSL in `gpu\shader\hlsl\`, C++ in `gpu\postprocess_renderer.cpp|.h`, `gpu\postprocess_aa.cpp`, `gpu\gfx_state.cpp`, `gpu\camera_extract.cpp`, `gpu\upscaler.cpp`).
- **Our code:** `src\render\postfx.cpp` (~4701 lines), `src\render\pipelinecommon.cpp`, `src\skygfx.h`, `shaders\ps\*`, `resources\resource.h`, `resources\Resource.rc`, `tools\fast_build.py`.

> Reconciled from: exp-1 (LibertyRecomp inventory), exp-2 (our postfx.cpp map), plus direct reads of `postfx.cpp` (chain 1810–2112, HeightFog template 4199–4346, UpdateFrontBuffer 437–466, RT creation 2918/3075/3098/3913) and shader hook greps.

---

## 0. HARD CONSTRAINTS (RW 3.6 / SM3.0) — read before any edit

1. **No RW CAMERATEXTURE raster creation mid-frame.** The SMAA crash (0x007FBD4A, NULL vtable in `RwCameraEndUpdate`) came from creating RW camera textures lazily. Use the deferred pattern (see §3).
2. **New colour/depth render targets: use plain D3D9 `IDirect3DDevice9::CreateTexture(..., D3DUSAGE_RENDERTARGET, ..., D3DPOOL_DEFAULT)` + `GetSurfaceLevel`.** This is what our existing RTs already do (`g_normalBufferTex` 3075, `g_pipeChainTexA/B` 3098, `g_velocityTex` 3913, `g_iblTex` 2918) and it does **not** require the RW camera trick. Guard with `null` checks; create lazily; release in `ReleaseDefaultPoolResources()` (postfx.cpp:2257+); recreate after device loss / resolution change.
3. **Never read and write the same surface in one pass.** If a pass samples surface A, it must render to surface B (or the camera raster), then swap. Our chain relies on this.
4. **Save/restore all D3D9 + RW render state** via the existing helpers: `CPostEffects::ImmediateModeRenderStatesStore()` / `ImmediateModeRenderStatesSet()` / `ImmediateModeRenderStatesReStore()` (see HeightFog 4235/4337).
5. **Depth sampling requires `DepthHook_Suspend()` / `DepthHook_Restore()`** (HeightFog 4233/4335). Always restore in the `__except` bail path too.
6. **SM3.0 only** (`ps_3_0`). No compute, no atomics, no `RWTexture`, no SM4 intrinsics. All postfx shaders use `tex2D`/`tex2Dlod` + register constants.
7. **Division safety:** every `normalize`, `1/x`, `texel/…` needs `max(x, 1e-7f)`. NaN → black in SM3.0.
8. **Append-only config:** new fields go at the **end** of `Config` (`src\skygfx.h`, ~411+); never insert mid-struct. Wire INI read/write + debug menu.
9. **Resource IDs:** next free after 252 (verify current max in `resources\resource.h` before assigning; proposed block 260+).
10. **Environment map renders mid-frame are forbidden**; `UpdateTimecycleLighting()` stays uncalled.

---

## 1. Our current postfx chain (`src\render\postfx.cpp`)

`CPostEffects::ColourFilter_switch` — **line 1810**:
```
1829  DrawNormalBufferToTexture()        (if normalBufferEnable)
1836  DrawVelocityBuffer()               (if enabled)
1839  DrawSSAO_Overhaul() / DrawSSAO()   (if ssaoEnable)
1849  DrawPipeChain()                     (if pipeChainEnable && normalBufferEnable)
1880  colour conversion + switch(colorFilter) -> ColourFilter*()
2073  DrawMotionBlur()                    PERF_SCOPE("MotionBlur")
2079  DrawHeightFog()                     PERF_SCOPE("HeightFog")
2083  DrawGodRays()                       PERF_SCOPE("GodRays")
2088  UpdateFrontBuffer()                 (cam raster -> pRasterFrontBuffer)
2091  chars_drawSSSBlur()
2097  DumpCurrentRT("after_filter")
```
`CPostEffects::DrawFinalEffects` — **line 2129**:
```
2135  if(smaaEnable && SMAA_Edge){ UpdateFrontBuffer(); DrawSMAA(); }
2142  DrawUnifiedDebugMenu()              (ImGui, on top)
2150  YCbCr filter block                  (if m_bYCbCrFilter)
```

**Insertion points**
- **Scene-reading colour passes** → inside `ColourFilter_switch`, between `DrawGodRays()` (2086) and `UpdateFrontBuffer()` (2088). Source = `pRasterFrontBuffer` (s0); depth = `g_ssaoDepthTex` (s1); normals = `g_normalBufferTex` (s2).
- **Final overlay passes** (vignette/grain/CA if you want them after AA) → `DrawFinalEffects`, between `DrawSMAA()` (2140) and the ImGui block (2142).

---

## 2. CANONICAL FULL-SCREEN PASS TEMPLATE

Copy `DrawHeightFog` (postfx.cpp:4199–4346). Skeleton:

```cpp
static void DrawMyEffect(void)
{
    if(!config->myEnable || !MyEffectPS) return;
    if(!CPostEffects::pRasterFrontBuffer) return;
    if(IsGameInMenuOrPaused()) return;
    IDirect3DDevice9 *dev = d3d9device; if(!dev) return;
    if(!Scene.camera) return;
    RwRaster *camRas = RwCameraGetRaster(Scene.camera); if(!camRas) return;

    __try {
        // DepthHook_Suspend();                       // only if sampling g_ssaoDepthTex
        CPostEffects::ImmediateModeRenderStatesStore();
        CPostEffects::ImmediateModeRenderStatesSet();
        RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
        RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer); // s0
        // dev->SetTexture(1, g_ssaoDepthTex);       // s1 if needed
        // dev->SetTexture(2, g_normalBufferTex);     // s2 if needed

        float c0[4] = { /* params */ };
        RwD3D9SetPixelShaderConstant(0, c0, 1);

        overrideIm2dPixelShader = MyEffectPS;
        RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);
        overrideIm2dPixelShader = nil;

        // dev->SetTexture(1, NULL);  // clean up extra stages

        RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
        RwRenderStateSet(rwRENDERSTATEFOGENABLE,      (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE,    (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE,   (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER,  (void*)NULL);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE,(void*)TRUE);
        RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
        // DepthHook_Restore();
        CPostEffects::ImmediateModeRenderStatesReStore();
    } __except(EXCEPTION_EXECUTE_HANDLER){
        dbglog("[PostFX] DrawMyEffect CRASHED 0x%08X", GetExceptionCode());
        // DepthHook_Restore();
        CPostEffects::ImmediateModeRenderStatesReStore();
    }
    CPostEffects::UpdateFrontBuffer(); // so the next pass reads the result
}
```

`colorfilterVerts` / `colorfilterIndices` are the shared full-screen quad. Output goes to the **current render target** (camera raster), then `UpdateFrontBuffer()` copies cam→FB for the next pass.

---

## 3. RT allocation rule for multi-pass effects

For effects needing intermediate surfaces (bloom mips, DOF half buffers, SSR buffer, TAA history):

```cpp
IDirect3DTexture9 *g_myTex = NULL;
IDirect3DSurface9 *g_mySurf = NULL;
static bool s_myBroken = false;   // permanent skip on hard failure
static int  s_myW = 0, s_myH = 0;

static bool EnsureMyRTs(int w, int h) {          // call from draw, once/frame max
    if(s_myBroken) return false;
    if(g_myTex && s_myW==w && s_myH==h) return true;
    if(g_myTex){ g_myTex->Release(); g_myTex=NULL; g_mySurf=NULL; }
    // Float RT: verify support first
    HRESULT hr = dev->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,
                 D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &g_myTex, NULL);
    if(FAILED(hr)){  // fallback to 8-bit
        hr = dev->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,
             D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_myTex, NULL);
    }
    if(FAILED(hr) || FAILED(g_myTex->GetSurfaceLevel(0,&g_mySurf))){
        if(g_myTex){ g_myTex->Release(); g_myTex=NULL; }
        s_myBroken = true; dbglog("[MyEffect] RT create failed -> disabled"); return false;
    }
    s_myW=w; s_myH=h; return true;
}
```
- **No RW camera Begin/EndUpdate needed** for these (plain D3D9 textures are safe to create lazily; this is how g_normalBufferTex/g_pipeChainTex already work).
- Release in `ReleaseDefaultPoolResources()` (postfx.cpp:2257) and reset `s_myBroken`/dims on device reset & resolution change.
- Float-first with `A8R8G8B8` fallback keeps bloom/DOF from silently clipping HDR; HDR is not strictly required for the first milestone.

---

## 4. RECONCILIATION TABLE

| Effect | Our status | LibertyRecomp reference file(s) | Divergence / effort | Risk |
|---|---|---|---|---|
| **Bloom** | **INERT** — `ivBloomIntensity` config exists (skygfx.h:411, main.cpp:1665/1816/1854), `GTA_SA_ModernColor.hlsl:19` + `ColorFilter_CrossMix.hlsl` sample `bloomTex` s1, but nothing renders a bloom source and no `bloom_*` shaders exist | `bloom_extract/downsample/upsample/composite_ps.hlsl`; `ApplyBloom` postprocess_renderer.cpp:1863 | 9 passes (extract ½ + 6×down 13-tap + 6×up 9-tap tent + composite). Needs 6-level ½-res RT chain = biggest plumbing. Config hook already present. | Med |
| **Vignette** | Baked only inside color-filter shaders (`GTA_SA_ModernColor.hlsl:90`, `ColorFilter_CrossMix.hlsl:135`, `GTAIV_ps20.hlsl:33`); no dedicated pass | `vignette_ps.hlsl`; `ApplyVignette` :1038 | Trivial: 1 pass, no RT. | Low |
| **Chromatic aberration** | Absent (only hinted inside `MotionBlur_Burnout.hlsl:100`) | `chromatic_aberration_ps.hlsl`; `ApplyChromaticAberration` :1686 | Trivial: 1 pass, 3 taps. | Low |
| **Film grain** | Absent | `film_grain_ps.hlsl`; `ApplyFilmGrain` :1607 | Trivial: 1 pass, procedural IGN, no RT. | Low |
| **FSR1 upscale** | Absent | `fsr1_easu_ps.hlsl`, `fsr1_rcas_ps.hlsl`; `ApplyFSR1` :947 | 2 passes, purely spatial (no history). Needs low-res source + output-size RT. | Low–Med |
| **DOF** | Absent | `dof_prefilter/bokeh/postfilter/combine_ps.hlsl`; `ApplyDoF` :1274 | 4 passes, 2 half-res RTs. Needs depth (have `g_ssaoDepthTex`) + camera near/far. | Med |
| **SSR** | Absent (`CarPaint_Reflections` is a cubemap/environment reflection, not screen-space) | `ssr_raytrace/composite_ps.hlsl`; `ApplySSR` :1461 | 2 passes; needs depth (have) + normals (`g_normalBufferTex`, have) + reconstruct + 64-step raymarch + 4-iter binary search. Cost/quality tradeoff on SM3.0. | High |
| **TAA** | Absent | `taa_ps.hlsl`; `ApplyTAA` :752, `ApplyAA` postprocess_aa.cpp:169 | Needs per-pixel motion (`g_velocityTex`, have!) + history RT + jittered projection + YCoCg clip. Interacts with the whole chain & SMAA. | **Highest** |
| **Motion blur (camera)** | We have `MotionBlur_Burnout` + `vectorMotionBlur` + `VelocityReconstruct` + `g_velocityTex` (depth/velocity based) | `motion_blur_camera_ps.hlsl`; `ApplyMotionBlur` :1776 | We already have a camera motion blur; LibertyRecomp's is prevViewProj-based. **Skip, or port as an alternative** — do not duplicate. | — |
| **SSAO** | Already implemented (`SSAO.hlsl` + overhaul/blur/upsample) | `ssao_gtao/blur/composite_ps.hlsl`; `ApplySSAO` :1099 | Optional quality upgrade (GTAO), not a gap. Low priority. | — |
| **Sun shafts** | Already have `GodRays.hlsl` | `sunshafts_prepass/radial/composite_ps.hlsl`; `ApplySunShafts` :2046 | Optional alternative. Low priority. | — |
| **SMAA** | Already implemented (`src\render\SMAA.cpp` + `SMAA_*.hlsl` + LUTs) | `smaa_edge_detect/blend/neighborhood_blend_ps.hlsl` + `smaa.hlsli` | Parity check only. Low priority. | — |

---

## 5. RECOMMENDED IMPLEMENTATION ORDER

Chosen for (a) visual impact, (b) low regression risk, (c) reuse of existing config/plumbing, and (d) de-risking the shared plumbing before the hard effects.

| Phase | Effect(s) | Why here |
|---|---|---|
| **P1** | **Vignette + Chromatic Aberration + Film Grain** | Single-pass, zero new RTs (write to camera raster). Establishes the new-shader + wiring template with the least risk. Vignette also fixes that vignette is currently only baked in colour-filter shaders. |
| **P2** | **Bloom** | Highest visual impact of the gaps, and the `bloomTex` s1 + `ivBloomIntensity` hooks already exist — wiring bloom into s1 lights up the existing modern-colour path. Introduces the multi-level RT chain (the risky plumbing) in a contained scope. |
| **P3** | **DOF** | High impact, builds on the half-res RT pattern proven in P2. Needs depth we already have. |
| **P4** | **FSR1** | Cheap-ish, purely spatial; useful if we ever render at a reduced internal resolution. |
| **P5** | **SSR** | High impact but expensive; depends on normals + depth already produced; highest GPU cost → last of the "real" effects. |
| **P6** | **TAA** | Highest risk (history + jitter + motion). Only after everything above is stable. |

**Parallelisation:** P1's three shaders are independent HLSL files, but all three touch the shared files (`postfx.cpp`, `resource.h`, `Resource.rc`, `pipelinecommon.cpp`, `skygfx.h`, `fast_build.py`). Implement P1 as **one** integrated lane (not three parallel writers) to avoid overlapping-write conflicts. Same for every subsequent phase.

---

## 6. CONCRETE INTEGRATION DESIGN — FIRST THREE (P1) + BLOOM (P2)

Shared for all P1 effects:
- **Insertion:** `ColourFilter_switch`, after `DrawGodRays()` (postfx.cpp:2086), before `UpdateFrontBuffer()` (2088). Each writes to the camera raster then calls `UpdateFrontBuffer()`.
- **Quad:** `overrideIm2dPixelShader = XxxPS; RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, colorfilterVerts, 4, colorfilterIndices, 6);`
- **Resource IDs:** propose `IDR_VIGNETTE_PS 260`, `IDR_CHROMATIC_ABERRATION_PS 261`, `IDR_FILM_GRAIN_PS 262` (verify ≥ current max+1).
- **New shaders:** `shaders\ps\Vignette.hlsl`, `shaders\ps\ChromaticAberration.hlsl`, `shaders\ps\FilmGrain.hlsl` (each `float4 main(...) : COLOR` `ps_3_0`).
- **Register/constant layout (proposed):**

### 6.1 Vignette
- Config (append to `Config`): `vignetteEnable`, `vignetteIntensity` (0–1), `vignetteRadius` (0–1), `vignetteSoftness` (0–1), `vignetteRoundness` (0–1).
- Constants: `c0 = {intensity, radius, softness, roundness}`.
- Algorithm (from `vignette_ps.hlsl`): `uv = tex - 0.5`; aspect-correct via `lerp((aspect,1),(1,1),roundness)`; `dist = length(uv)*2`; `v = 1 - smoothstep(radius, radius+softness, dist)`; `v = lerp(1, v, intensity)`; `color.rgb *= v`.
- Source s0 = `pRasterFrontBuffer`; no depth.

### 6.2 Chromatic Aberration
- Config: `caEnable`, `caIntensity`, `caRedOffset`, `caBlueOffset`, `caRadialFalloff`, `caMaxOffset` (pixels), `caSoftKnee`.
- Constants: `c0 = {intensity, redOffset, blueOffset, radialFalloff}`; `c1 = {maxOffset, softKnee, aspect, texelW}`; `c2 = {texelH, 0,0,0}`.
- Algorithm (`chromatic_aberration_ps.hlsl`): radial falloff `pow(smoothDist, radialFalloff)`; sample R at `+offset`, G centered, B at `-offset`; clamp offset to `maxOffset/∅`; aspect correction.
- Source s0 = `pRasterFrontBuffer`.

### 6.3 Film Grain
- Config: `filmGrainEnable`, `filmGrainIntensity` (0.03–0.12), `filmGrainColored` (0–1), `filmGrainLuminanceScale`.
- Constants: `c0 = {intensity, frameIndex, luminanceScale, coloredGrain}`.
- Algorithm (`film_grain_ps.hlsl`): IGN `frac(52.9829189 * frac(dot(pixelCoord, float2(0.06711056,0.00583715))))` with `pixelCoord += 5.588238 * frameIndex`; base + 2 octaves (0.5, 0.25)/0.571; shadow/highlight falloff via `smoothstep`; `color.rgb += grain * intensity * response`.
- `frameIndex` from a global frame counter (increment per frame). Purely procedural → no RT, no texture.
- **Cheapest high-value win overall.**

### 6.4 Bloom (P2)
- Config (append): `bloomEnable`, `bloomThreshold` (0.8–1.5), `bloomSoftThreshold` (0–0.5), `bloomIntensity`, `bloomSaturation`, `bloomBlendMode` (0 add / 1 screen), `bloomRadius`, `bloomUseKaris`. Keep `ivBloomIntensity` for the GTA IV path; use a separate generic `bloomIntensity`.
- Shaders: `Bloom_Extract.hlsl`, `Bloom_Downsample.hlsl`, `Bloom_Upsample.hlsl`, `Bloom_Composite.hlsl` (IDs 263–266).
- RTs: 6 levels at `w/2, w/4, w/8, w/16, w/32, w/64` (`D3DFMT_A16B16G16R16F` → fallback `A8R8G8B8`), created via §3. Validate with `CheckDeviceFormat` once (depthhook.cpp:59 is the reference pattern).
- Pass structure: extract (full→mip0 ½, 4 taps + soft knee + Karis weight) → 6× downsample (13-tap) → 6× upsample (9-tap tent, additive blend) → composite into scene (`scene + bloom`, or screen).
- **Wiring into existing hook:** after computing the composite, bind the top bloom level to **s1** and let `GTA_SA_ModernColor.hlsl` / `ColorFilter_CrossMix.hlsl` add it — OR do bloom as its own composite pass and set `ivBloomIntensity=0` in the filters to avoid double-add. **Choose one** (recommend the dedicated composite pass; zero the filter hook) to prevent double count.
- Insert: after `DrawGodRays()` (2086), before `UpdateFrontBuffer()` (2088).

---

## 7. NOT PORTABLE / DROP OR APPROXIMATE

| Item | Verdict |
|---|---|
| Any `Texture2D`/`SamplerState`/`SampleLevel` usage | **Mechanical rewrite** to `sampler2D` + `tex2D`/`tex2Dlod`. Semantics identical. |
| Compute / atomics / `RWTexture` | **None present** in the listed effects — nothing to drop. |
| TAA jitter + motion-vector requirement | Portable *because* we already have `g_velocityTex`; but requires jittered projection matrix and a history RT → treat as P6, not P1. |
| SSR 64-step dynamic raymarch | Portable but expensive; consider reducing `maxSteps` to 24–32 and lowering `stride` for SM3.0-era GPUs. Approximate, don't fight it. |
| R16G16B16A16_FLOAT intermediates | D3D9 supports `D3DFMT_A16B16G16R16F`; **must** `CheckDeviceFormat` and fall back to `A8R8G8B8`. Do not assume. |
| `SampleLevel(...,0)` explicit-LOD calls | Collapse to `tex2D` where LOD isn't semantically needed. |
| LibertyRecomp native-DoF/AA/Bloom "disable" logic | **Drop.** It exists to stop double-processing in GTA IV; our pipeline has no such natives. |
| Motion-blur camera (prevViewProj) | **Drop/optional.** We already have velocity-based motion blur; porting it would duplicate. |
| Multiple simultaneous RTs (`OMSetRenderTargets`) | Prefer single-RT passes; MRT is avoidable across all listed effects. |

---

## 8. RISK / VALUE CALL

- **Highest-risk item: TAA (P6).** It needs a persistent history buffer, jittered projection, motion vectors, and interacts with SMAA/colour filter ordering; a mistake produces ghosting or a black frame, and the failure mode is hard to debug in-game.
- **Cheapest high-value win: Film Grain (P1)** — one pass, procedural, no RT, no depth, ~40 lines of HLSL + ~60 lines of C++, and it is completely isolated from the existing chain. **Vignette** is a near-tie and additionally removes the vignette dependency from the colour-filter shaders.

---

## 9. ADD-A-SHADER CHECKLIST (per effect)

1. `resources\resource.h` — `#define IDR_X_PS <next free id>`.
2. `resources\Resource.rc` — `IDR_X_PS RCDATA "cso/X.cso"`.
3. `shaders\ps\X.hlsl` — `ps_3_0` entry (`main`).
4. `tools\fast_build.py` — add to the `multi_entry` list (~line 380): `('ps_3_0','X.hlsl','main','X.cso')`.
5. `src\pipelinecommon.cpp` — global `void *XPS = nullptr;` + `makePS(IDR_X_PS, &XPS);` in `CreateShaders()` (line 414; registration block ~544–664, guarded by `shadersCreated`).
6. `src\skygfx.h` — `extern void *XPS;` and append config fields.
7. `src\render\postfx.h` — `void DrawX(void);` (or `static void`).
8. `src\render\postfx.cpp` — implement `DrawX()` from §2; register in `Initialise_skygfx()` (line 4488) with `diag_registerScope("DrawX", (void*)DrawX);`; wire into the chain (§1); release RTs in `ReleaseDefaultPoolResources()` (2257).
9. INI: read/write + `ADD_IF_MISSING` in `main.cpp`; debug-menu slider in `debugmenu_ui.cpp`.
10. Build+deploy: `python tools/fast_build.py --fastest`.

---

## 10. WORK BREAKDOWN (lanes)

| Lane | Scope | Owner | Depends |
|---|---|---|---|
| L1 | P1: Vignette + ChromaticAberration + FilmGrain (shaders + C++ + config + wiring) | fixer-senior | — |
| L2 | Verify L1 build (`fast_build.py --fastest`) + in-game smoke test | fixer-build / user | L1 |
| L3 | P2: Bloom (4 shaders + mip RT chain + composite + config) | fixer-senior | L1 template proven |
| L4 | P3: DOF | fixer-senior | L3 RT pattern |
| L5 | P4: FSR1 | fixer-coder | — |
| L6 | P5: SSR | fixer-principal | L4 |
| L7 | P6: TAA | fixer-principal + oracle review | L6, needs jitter/history design |

**Acceptance per phase:** builds clean via FXC (`--shaders`) + full build, deploys, and the effect is visible in-game with a debug-menu toggle, with no new crash in `skygfx_dbg.log`.

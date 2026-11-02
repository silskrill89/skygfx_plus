# Motion Blur + Physics Synergy Research (2026-10-29)

**Status:** READ-ONLY research deliverable. No source edits were made.
**Scope:** Part 1 — motion blur (ours / GTA IV / GTA V / recommendation). Part 2 — physics-cloth-vertex
synergy, 1 page (deep RE is another lane's job).
**Sources:** repo (relative paths), `E:\SDKs\LibertyRecomp-main` (GTA IV recomp — readable HLSL),
`E:\dev(dave)\releases\GTASource\rage\GTAV Source\src\dev_ng` (GTA V leaked source).

---

## 0. TL;DR

| Question | Answer |
|---|---|
| Keep ours? | **Yes.** The velocity-buffer lane is load-bearing for 3 consumers (MB, SMAA temporal, SSAO temporal) — do not replace it with an IV-style matrix-reproject MB that deletes it. |
| Upgrade ours? | **Yes, tiered.** Tier 0 (framerate normalisation + cut reset) is ~40 LoC and fixes two real bugs. Tier 1 (bidirectional taps, per-tap depth rejection, texel snap, velocity normalise) is one shader edit. |
| Steal from IV? | **Staggered jitter ladder + stencil/player predicate.** We already carry IV's jitter constants (via V — see §1b.2 provenance). Skip IV's `motionBlurMatrix` lane as the *primary* path — it duplicates what `VelocityReconstruct` already gives three consumers. |
| Steal from V? | **`(1/30)/dt` length normalisation, cut→reset-prev-matrix (instead of skipping the pass), per-tap texel snapping, velocity normalise/clamp, small-length discard.** All SM3.0-safe, all free of new registers. |
| Register budget | Tier 1 needs **zero** new registers (s0-s2 / c0-c3 already sufficient). Optional matrix lane → **c53+** per ledger. New CSO/resource ID → **286**. |
| Cost | Tier 0: ~40 LoC, 0 GPU. Tier 1: ~90 LoC + ~70 shader lines, 25 → 13-17 fetches (net **faster**). Tier 2: ~200 LoC, new pass, optional. |

---

# PART 1 — MOTION BLUR

## 1a. Our current motion blur (inventory)

### 1a.1 Two passes, two gates

There are **two separate passes** with **independent gates**:

| Pass | File | Gate | Called from |
|---|---|---|---|
| `DrawVelocityBuffer` (producer) | `src/render/postfx_motionblur.cpp:23` | `velocityBufferEnable && VelocityReconstruct && pRasterFrontBuffer && !IsGameInMenuOrPaused()` (`:34-39`) | `src/render/postfx.cpp:1593-1598` (early in `ColourFilter_switch`, **before SSAO**) |
| `CPostEffects::DrawMotionBlur` (consumer) | `src/render/postfx.cpp:4019` | `motionBlurEnable && MotionBlur_Burnout && pRasterFrontBuffer && !menu/pause && !cutscene` (`:4027-4048`) | `src/render/postfx.cpp:2243-2244` (late, after colour filter / lens group / IV grade) |

Skips are recorded for the frame summary at `src/render/postfx.cpp:2299`
(`if(!(config->motionBlurEnable && MotionBlur_Burnout)) pfSkip |= PFG_MOTIONBLUR`).

**Chain order inside `ColourFilter_switch`** (evidence):

```
normal buffer (:1581) → velocity buffer (:1598) → SSAO (:1622/:1624)
  → CopyDepthToPrev (:1650, only if velocity or SSAO-temporal want it, :1647-1648)
  → pre-tonemap FX group SunShafts→DOF→Bloom (:1857-1876)
  → colour filter / tonemap (:1934-1937)
  → post-tonemap lens CA→grain→vignette (:2137-2166)
  → IV grade (:2210-2232)
  → MOTION BLUR (:2236-2249)
  → height fog (:2251-2264) → god rays (:2265-2281) → UpdateFrontBuffer (:2283) → SSS (:2286)
```
…then `DrawFinalEffects` runs **later** and does SMAA (`src/render/postfx.cpp:2373-2378`,
called "the last per-frame call" at `:2349-2350`).

> **Doc defect (read-only finding):** the comment at `src/render/postfx.cpp:2235` says motion blur
> runs "after colour filter + SMAA". SMAA actually runs **after** motion blur. Stale comment.

### 1a.2 Velocity buffer contract

| Property | Value | Evidence |
|---|---|---|
| Resource | `IDirect3DTexture9 *g_velocityTex`, camera-sized, `D3DUSAGE_RENDERTARGET`, **`D3DFMT_A8R8G8B8`**, `D3DPOOL_DEFAULT`, recreated on resolution change | `postfx_motionblur.cpp:9-11`, `:55-92` |
| Format | **R** = packed vel.x (`v*0.5+0.5`), **G** = packed vel.y, **B** = `length(velocity)` (UV units), **A** = `saturate(|depth−prevDepth|*10)` | `shaders/ps/VelocityReconstruct.hlsl:48-63` |
| Zero value | `D3DCOLOR_ARGB(0,128,128,0)` (clear + first-frame latch) | `postfx_motionblur.cpp:77`, `:209` |
| Producer algorithm | NDC from UV+depth → `clipPos * invCurrentVP` → world → `* prevVP` → prev UV → `velocity = curUV − prevUV` (GPU Gems 3 ch.27) | `VelocityReconstruct.hlsl:21-45` |
| Inputs | `s0 = g_ssaoDepthTex` (INTZ, depth hook **suspended**), `s1 = g_prevDepthTex` (may be NULL) | `postfx_motionblur.cpp:213-224` |
| PS constants | `c0-c3` invCurVP, `c4-c7` prevVP, `c8` screenParams, `c9` {prevDepthValid, farPlane} | `:227-233`; `VelocityReconstruct.hlsl:8-11` |
| Prev VP latch | `g_prevVPMatrix` written at end of every successful pass; first frame returns after latching | `postfx_motionblur.cpp:101-129`, `:294-295` |
| First-frame guard | fresh RT cleared to zero velocity; `g_prevVPValid` reset on recreate/release | `:15-21`, `:67-81`, `:119-129` |
| Consumers | ① `MotionBlur_Burnout` **s1** (`postfx.cpp:4171-4177`) ② SMAA temporal **s2** (`postfx_smaa.cpp:1209-1229`) and SMAA edge **s3** (`:320-321`) ③ SSAO temporal **s3** (`postfx_ssao.cpp:982-986`) ④ `SMAA_EdgeMotionDepth.hlsl:21,73-77` (uses `.b` magnitude for edge motion) | see files |
| History dependency | `CopyDepthToPrev()` runs only when `velocityBufferEnable || ssaoTemporalEnable` | `postfx.cpp:1647-1654`; moved out of the MB pass per `postfx_motionblur.cpp:300-301` |
| Known fragility | menu/pause gates the producer but **not** symmetrically with all consumers; `g_velocityTex` is lazy-created so early-frame logs legitimately show NULL (`postfx_smaa.cpp:707-719`, `postfx_motionblur.cpp:84-91`) | see files |

### 1a.3 `DrawMotionBlur` behaviour

- Re-syncs the front buffer before reading (`postfx.cpp:4063-4069` → `UpdateFrontBuffer()`), because
  `ColourFilter_Modern` can leave `pRasterFrontBuffer` on a pre-tonemap intermediate
  (`postfx.cpp:2144-2146`).
- **Camera-only motion estimate** from `RwFrameGetLTM`: `cameraVelocity` = per-frame position delta,
  `cameraRotation` = `1 − dot(prevAt, at)` (`postfx.cpp:4071-4104`).
- `cameraMovement = min(1, vel*0.15 + rot*1.0)`; hard skip below `0.002` (`:4111-4115`).
- Speed ramps (smoothstep): `speedRamp` 0@0.05→1@0.12, `fullFrameW` 0@0.09→1@0.14
  (`:4126-4134`) — tuned from telemetry (walking ≈0.018, driving 0.135-0.487).
- `effectiveStrength = motionBlurStrength * speedRamp`, camera-aware dim above `cameraMovement>0.8`
  (`:4139-4143`), early-out `< 1e-4` (`:4152-4153`).
- Binds **s0** = front buffer (UV 0..1), **s1** = velocity (POINT), **s2** = `g_ssaoDepthTex`
  (POINT/CLAMP, depth hook suspended) (`:4167-4189`).
- Constants: `c0 = {strength, radial, 8.0, speedFactor}`, `c1 = screen`, `c2 = {camTerm, fullFrameW, fbU, fbV}`,
  `c3 = {near, far, tanFovX, maxBlurPx=32}` (`:4191-4231`). `maxBlurPx` was halved from 64 (`:4230`).
- Draws with `PfBindPS(MotionBlur_Burnout, "IDR_MOTIONBLUR_BURNOUT(140)")` (`:4243`; loaded at
  `pipelinecommon.cpp:35,765`; resource `resources/resource.h:140`).
- Ends with `UpdateFrontBuffer()` (`:4286`).

### 1a.4 `MotionBlur_Burnout.hlsl` (SM3.0, `ps_3_0`)

`s0` front buffer, `s1` motion, `s2` depth; `c0-c3` as above (`shaders/ps/MotionBlur_Burnout.hlsl:21-28`).

1. Centre sample at `tex * fbScale` (`:58`).
2. Unpack motion `rg*2−1`, magnitude `.b` (`:61-63`).
3. **Radial blend**: `blurDir = lerp(motionVec, radialDir, saturate(dist*2) * motionBlurRadial)` (`:66-74`).
4. `blurAmount = mag*strength + cameraTerm*speedFactor`, × `lensW = lerp(radialDist*1.414, 1, fullFrameW)` (`:80-92`).
5. Depth-aware fade: linear eye depth from INTZ, near geometry fades to 0 (`:98-101`).
6. Clamp to `maxBlurPx` in UV units (`:104`), early-out `<0.001` (`:107-108`).
7. **Jitter** via `frac(sin(dot(...)))` with IV's magic offset `{58.164, 47.13}` (`:44-48`, `:115-116`).
8. **8 directional samples × 3 RGB-separated fetches** = 25 fetches, quadratic weight falloff
   (`:128-153`).

### 1a.5 Current quality issues (evidence, not opinion)

| # | Issue | Evidence | Severity |
|---|---|---|---|
| Q1 | **8-bit velocity quantisation.** RG packed in `[-1,1]` → step `2/255 = 0.00784` UV (≈15 px on the U axis @1920); B magnitude step `1/255 = 0.0039` UV (≈7.5 px U / ≈4.2 px V @1080p). Slow camera pans produce per-frame velocities below one LSB → direction snaps to axis, blur length stair-steps. | `postfx_motionblur.cpp:61` (`D3DFMT_A8R8G8B8`), `VelocityReconstruct.hlsl:48-49,63`, consumer `MotionBlur_Burnout.hlsl:61-63,80` | **High** — the most visible artifact |
| Q2 | **Camera-only motion vectors.** Reprojection uses only `invCurrentVP`/`prevVP`; moving vehicles, peds, wheels, doors never produce vectors — they only blur because the camera term smears the whole frame. | `VelocityReconstruct.hlsl:30-45`; corroborated by `docs/plans/2026-10-25-gtaiv-parity-audit.md:400` and `2026-10-25-cryengine-parity-matrix.md:28` ("PARTIAL (no per-object vectors)") | **High** for gameplay moments |
| Q3 | **Post-tonemap domain.** MB runs on the graded/tonemapped front buffer (`:2244`), after CA/grain/vignette (`:2137-2166`). IV and V both blur **before** tonemap inside the composite → bright-object halos and no HDR energy bleed for us. | `postfx.cpp:2137-2146`, `:2244`; IV `gta_composite_mb.hlsl:1456-1522` (MB then `Exposure`/tonemap); V `postfx.fx:3103-3110` then `:3169 filmicToneMap` | **Medium** |
| Q4 | **Framerate-dependent gating.** `cameraVelocity` is a *per-frame* delta with no `dt` division, but the ramps use absolute per-frame thresholds (0.05/0.12/0.14) and a `0.002` gate. At 144 fps those values are ~4.8× smaller than at 30 fps → blur silently disappears / deadzone shifts. | `postfx.cpp:4084-4087`, `:4111-4115`, `:4126-4134`; no `dt` anywhere in the function | **High** (correctness) |
| Q5 | **Cutscene/menu pop.** Both passes are fully skipped during cutscenes; the producer also skips, so on re-entry the first MB frame can read a **stale** velocity texture (the code itself flags this at `:4039-4044`). | `postfx.cpp:4039-4048`; `postfx_motionblur.cpp:38-39` | **Medium** |
| Q6 | **No per-tap disocclusion rejection.** All 8 taps are taken along `blurDir` regardless of depth — foreground/background smear into each other. LR and V both reject per tap. | `MotionBlur_Burnout.hlsl:128-153` vs LR `motion_blur_camera_ps.hlsl:120-138` (`g_depthThreshold`), V `postfx.fx:2166-2183` (stencil per tap) | **Medium** |
| Q7 | **Unidirectional sampling.** Blur is a forward-only ladder (`i/8` from the centre), so streaks are asymmetric about the moving object. LR/V sample **both** directions. | `MotionBlur_Burnout.hlsl:128-131` vs `motion_blur_camera_ps.hlsl:110-139` | **Medium** |
| Q8 | **Fake radial term.** `motionBlurRadial` blends a screen-centre radial vector (`:66-74`) which is only correct for forward motion — wrong on strafe/lateral camera moves. | `MotionBlur_Burnout.hlsl:65-74`, config `src/skygfx.h:423` | **Low** (it's a documented knob) |
| Q9 | **Dead/legacy code.** `shaders/ps/vectorMotionBlur.hlsl` (46 lines, `IDR_VECTORMOTIONBLUR` 264, `resources/Resource.rc:118`) is compiled but has **zero** references in `src/`. | grep across `src/` = 0 hits | **Low** (cleanup) |
| Q10 | **Stale comment** at `postfx.cpp:2235` ("after colour filter + SMAA") — SMAA runs later in `DrawFinalEffects`. | `postfx.cpp:2235` vs `:2373-2378` | **Low** (doc) |

---

## 1b. How GTA IV does motion blur (LibertyRecomp)

### 1b.1 What the tree actually is

`E:\SDKs\LibertyRecomp-main\LibertyRecompLib\shader_overrides\manifest.json` maps **shader hashes →
readable HLSL** for the recompiled Xbox 360 post-FX binaries. Four of them are motion-blur composites:

| Hash | Source | Binary |
|---|---|---|
| `0xEE75C9F6AA1AB16A` | `motion_blur/gta_composite_mb.hlsl` | `rage_postfx_ps11.bin` |
| `0xE51D9DD95A333D92` | `motion_blur/gta_composite_mb_high_dof.hlsl` | `rage_postfx_ps13.bin` |
| `0xD3B2B2125BC24911` | `motion_blur/gta_composite_mb_dof_debug.hlsl` | (debug) |
| `0x94CC5AF0E2FF5B08` | `motion_blur/gta_composite_mb_high_dof_debug.hlsl` | (debug) |

(`manifest.json:6-43`; file headers `gta_composite_mb.hlsl:1`, `gta_composite_mb_high_dof.hlsl:1`.)

**Key structural fact:** IV has **no separate motion-blur pass.** MB is a *branch inside the single
composite post-FX pixel shader*, alongside DOF, bloom, tonemap and colour correction. The
`high_dof` variant differs from the base **only in three inline constant literals** (`c251/c252/c253`)
and register allocation — the algorithm is identical.

### 1b.2 Constants (readable, `gta_composite_mb.hlsl:847-903`)

| Symbol | Slot | Meaning |
|---|---|---|
| `gDirectionalMotionBlurLength` | `c212` | artist / timecycle MB length |
| `motionBlurMatrix[4]` | `c213-c216` | 4×4 previous-frame relative transform, rows consumed as `.zxy` swizzles |
| `dofProj / dofDist / dofBlur` | `c209 / c210 / c211` | DOF params shared with the MB block |
| `Exposure` | `c208` | applied **after** MB |
| `ToneMapParams`, `deSatContrastGamma`, `ColorCorrect`, `ColorShift` | `c217-c220` | grade, after MB |
| `PLAYER_MASK` | `c221` | stencil threshold for player rejection |
| `globalScreenSize` | `c44` | pixel snapping |
| `LibertyMotionBlurTimeScale` | shared `c37.x` | **LibertyRecomp host addition** — frame-time correction (header: "the sole behavioral change scales the stock directional reprojection vector by a host-supplied frame-time correction") |

Inline literal pool (`:1007-1034`, decoded from the hex):

| Literal | base (`ps11`) | `high_dof` (`ps13`) | role |
|---|---|---|---|
| `c250` | `(0.25, 0.125, 0, 0)` | same | ramp factors (`:1211,1220`) |
| `c251` | `(256, 0.5, 0.875, -1)` | `(**47.13, 58.164**, 0.875, -1)` | ladder scale + tap-sign helper |
| `c252` | `(0, 1, 0.375, 0.5)` | `(0, 1, 256, 0.5)` | one-s / half / scale constants |
| `c253` | `(0.625, 0.75, **47.13, 58.164**)` | `(0.625, 0.75, 0.375, 0.5)` | **jitter lookup constants** (`:1240-1244` uses `.wz`) |
| `c254` | `(0.0721, 0.2125, 0.7154, -0.5)` | same | BGR luma weights (`:1130-1134`) |
| `c255` | `(8, 4, 2, 256)` | same | tap-offset multipliers (`:1128`, `:1139`) |

> The `high_dof` variant is **the same algorithm with the literal pool shuffled** — the two jitter
> constants move from `c253.zw` into `c251.xy`, and the `256` scale moves from `c251.x` into `c252.z`.
> Nothing behavioural differs; only which literal sits where.

> **Cross-game provenance (new finding):** `c253.zw = (47.13, 58.164)` are **exactly** the "magic"
> jitter constants GTA V hardcodes as `JitterOffset = {58.164f, 47.13f}` (`postfx.fx:2156`) and that we
> copied into `MotionBlur_Burnout.hlsl:44`. The lineage is IV → V → us.

### 1b.3 Algorithm (`gta_composite_mb.hlsl`)

1. **Predicate:** `r3.w = (r0.w >= PLAYER_MASK.x)` where `r0.w` is the centre stencil value fetched at
   `:1074`. The whole MB block is guarded by `p0 = (r3.w == 0.0)` (`:1236-1238`) → **MB is skipped for
   player pixels**, applied to everything else.
2. **Velocity:** view/eye-space position built from GBuffer depth (`:1080-1091`), then
   `motionBlurMatrix` rows applied (`:1179-1185`), then ×
   `gDirectionalMotionBlurLength × LibertyMotionBlurTimeScale` (`:1207`). Delta is a **screen-space
   UV offset** — no velocity buffer, no depth history.
3. **Jitter:** `JitterSampler` fetched with `tex * c253.wz + …` (`:1240-1253`) — the 47.13/58.164 lookup.
4. **Taps:** 1 centre (`:1230`) + **7 HDR taps** (`:1290-1349`) + **7 stencil taps**
   (`:1353-1413`) = 8+8 fetches. Tap offsets are built from the literal pool `c251/c252/c253/c255`
   into `r13/r14/r9` (`:1262-1286`) — a **staggered, per-axis** ladder (U and V step by different
   constants) rather than a single scalar step; the exact step values are decoded in the table above
   (the prior audit reads it as a `0.5 / 0.75 / 1 / 2`-delta ladder,
   `docs/plans/2026-10-25-gtaiv-shader-overrides.md:991`).
5. **Weighting:** per-tap stencil mask → `playerMask` (`:1424-1444`), weighted accumulation
   (`:1456-1477`), normalise, then `lerp` back to the unblurred centre with a strength ramp
   (`:1486-1502`).
6. **Then** exposure / tonemap / grade (`:1504-1539`).

Tap count from the prior audit: "8 HDR taps + 8 stencil taps … 17 taps" total
(`docs/plans/2026-10-25-gtaiv-shader-overrides.md:991`).

### 1b.4 Also in the tree (not the composite)

`LibertyRecomp/gpu/shader/hlsl/motion_blur_camera_ps.hlsl` (144 lines) and
`…/postprocess_reference/motion_blur_camera_ps.hlsl` are **LibertyRecomp's own** D3D11 lane (not stock IV):
depth→world→prev-VP reprojection, **bidirectional** sampling with adaptive half-count
`clamp(len*0.5, 2, 16)` (`:99-100`), and **per-tap depth-threshold rejection**
(`:120-138`). This is the most directly portable reference for our Tier 1 work.

---

## 1c. How GTA V does motion blur

### 1c.1 Placement: a technique bit inside the composite pass

V has **no motion-blur render phase either**. It is one bit of the post-FX composite technique index:

- `#define BM_MotionBlur (0x00001)` — `renderer/PostProcessFX.cpp:1517`
- pass table `pp_composite`, `pp_composite_mb`, `pp_composite_highdof`, `pp_composite_mb_highdof`, …
  (`PostProcessFX.cpp:1087-1123`) — a full cross-product of {mb} × {dof} × {noise} × {nightvision} × {heathaze}.
- `if (hasMotionBlur) techniqueIdx |= BM_MotionBlur;` → `PassLookupTable[techniqueIdx]`
  (`PostProcessFX.cpp:10046`, `:10122`).
- Shader: `shader_source/PostFX/postfx.fx:3103-3110` calls `ApplyMotionBlur(...)` **before**
  `filmicToneMap(...)` at `:3169`.

### 1c.2 Gating & parameters

| Item | Value | Evidence |
|---|---|---|
| Enable test | `m_directionalBlurLength > 0` && !pause-frozen && !screenBlurFade && (**not inside vehicle** unless forced) && PC `PostFX >= Medium` | `PostProcessFX.cpp:7355-7377` |
| Length source | `pow(camFrame::GetMotionBlurStrength(), 2) * 10`, else `g_cachedDefaultMotionBlur` (look-around speed based), else timecycle `GetMotionBlurLength()` | `:1838-1869`, `:1790-1834` |
| **Framerate normalisation** | `lengthReduction = Clamp((1/30)/settings.m_timeStep, 0, 1)` → blur length is always expressed in **30 fps-equivalent** screen distance | `:9358-9364` |
| **Sample count** | 6 default, **16 on PC Ultra** | `:9366-9370` |
| Ghosting fade / max velocity mult | `m_directionalBlurGhostingFade = 0.2`, `m_directionalBlurMaxVelocityMult` | `:1866-1867` |
| Prev-matrix management | `MotionBlurPrevViewProjMat = Cur` each frame; **reset to current** on `cosAng < 0` (camera flipped) or cut flags (`Flag_HasCutPosition/Orientation`) — so **no smear on cuts** instead of skipping the pass | `:7485-7506` |
| Render-thread params | `MBPrevViewProjMatrix` transposed (rows dot worldPos), shear-proj params, `DirectionalMotionBlurParams`, `DirectionalMotionBlurIterParams` | `:9341-9370` |
| Player mask | `pp_DownSampleStencil` pass runs when MB is on; `StencilCopyTexture` bound when MB on | `:9526`, `:10159-10167` |
| MB source texture | `PostFxMotionBlurTexture` = `backBuffer` on PC (DOF_COMPUTE_GAUSSIAN is on for D3D11, `PostFX_shared.h:9`), else `HalfScreen0` (½×½ RT, `:2411`) — the "1/4 res" console path | `:10143-10156` |
| FPV-only lane | separate `pp_motion_blur_fpv*` techniques with own half-res targets, weights, velocity smoothing | `:1008-1030`, `:10631-10647` |

### 1c.3 Shader (`shader_source/PostFX/postfx.fx`)

```hlsl
// :2099-2132  GetMotionBlurVelocity
curNDCPos  = float4(tex*float2(2,-2)+float2(-1,1), depth.y, 1);
eyeRay     = dot(transformVec, MBPerspectiveShearParams0/1/2);   // 3 shear params
worldPos   = gViewInverse[3].xyz + eyeRay * depth.x;             // linear depth
prevNDC    = dot(worldPos, MBPrevViewProjMatrixX/Y/W);           // transposed rows
velocity   = (curNDC - prevNDC).xy * float2(0.5,-0.5);
// normalise to a fixed screen-space magnitude (MBVelMax = 64/w, 64/h reciprocals)
pixelVelocity = normalize(pixelVelocity) * MBVelMax.xy * lengthScale;
```

```hlsl
// :2135-2192  ApplyMotionBlur
sampleMask      = ReadStencilMask(tex);   // SM3 fallback = tex2Dlod(...).b (postfx.fx:2092);
                                          // SM4+ = getStencilValueScreen (:2090); V also uses .g/.r
                                          // channel reads per platform (:4215-4223) and GatherRed (:4250)
playerMaskCenter= CalculatePlayerMask(sampleMask);   // PLAYER_MASK = 127.9/255 (:476)
velocity        = GetMotionBlurVelocity(...);
mbStep          = velocity * MB_INV_NUM_SAMPLES;
jitterScalar    = h1tex2D(JitterSampler, tex*float2(58.164,47.13) + curSample.rg*8) - 0.5;
tex            += mbStep * jitterScalar * 0.5;
for (i = 1; i < MB_NUM_SAMPLES; ++i) {               // PC: [loop], console: [unroll]
    curTCr  = (round(curTC * globalScreenSize) + 0.5) / globalScreenSize;   // TEXEL SNAP
    sampleMask = ReadStencilMask(curTCr);
    mbSamplesSum += tex2Dlod(screen, float4(curTCr,0,0)) * CalculatePlayerMask(sampleMask);
}
blurred = mbSamplesSum / max(mbWeights, 0.1);
discardSmallLength = saturate(dot(velocity,velocity) * 100000);   // avoid 1/4-res softness
return lerp(curSample, blurred, playerMaskCenter * discardSmallLength);
```

**Distinctive V traits:** (1) 30 fps normalisation, (2) cut→reset prev matrix rather than pass skip,
(3) texel-snapped tap coordinates, (4) **stencil player mask per tap** (player never smears),
(5) velocity normalised to a bounded screen magnitude then scaled, (6) small-length discard,
(7) 6/16 samples, (8) pre-tonemap HDR domain.

### 1c.4 Where V's motion data also flows (relevant to us)

`m_bAddToMotionBlurMask` is a **per-entity flag** propagated through the draw lists and wrapped around
draws with `CShaderLib::BeginMotionBlurMask()/EndMotionBlurMask()` —
`renderer/DrawLists/DrawableDataStructs.h:364,425,480,526,716`,
`DrawableDataStructs.cpp:1212,1457,1961,2103-2112`,
`Entities/ObjectDrawHandler.cpp:200-204`, `Entities/VehicleDrawHandler.cpp:197-201`.
This is how V gets **per-object** participation: objects opt into the stencil mask (and, in the
deferred path, into velocity). SA has neither a G-buffer velocity nor a stencil-copy pass, so this
lane is not portable without a new RT.

---

## 1d. Comparison + recommendation

### 1d.1 Side-by-side

| Axis | **Ours** | **GTA IV** | **GTA V** |
|---|---|---|---|
| Architecture | Separate fullscreen pass + separate velocity producer | Inline branch of the composite PS | Inline branch of the composite PS (technique bit) |
| Velocity source | Depth reprojection → **velocity RT** (8-bit) | 4×4 `motionBlurMatrix` + GBuffer depth, in-shader | Eye-ray + linear depth → prev view-proj, in-shader |
| Object motion | **None** (camera only) | **None** (camera only, matrix is camera-relative) | **None** in the MB term itself; per-object influence via stencil mask + deferred velocity |
| Domain | **Post-tonemap LDR** (`postfx.cpp:2244`) | Pre-tonemap HDR (`:1504-1522`) | Pre-tonemap HDR (`postfx.fx:3103`→`:3169`) |
| Samples | 8 fwd × 3 RGB = **25 fetches** | 1 centre + 7 HDR + 7 stencil = **15** (prior audit: 17 taps) | adds ~5 screen + 6 stencil reads (default N=6); **15 + 16** on PC Ultra (N=16) |
| Direction | Unidirectional, radial-blended | Unidirectional, anisotropic ladder | Unidirectional |
| Jitter | Procedural sin-hash, IV constants | `JitterTexture` sampler, IV constants | `JitterTexture` sampler, same constants |
| Rejection | Depth **fade only** (near→0) | Stencil player mask, per tap | Stencil player mask, per tap |
| Framerate norm | **None** (Q4) | `LibertyMotionBlurTimeScale` (host) | `Clamp((1/30)/dt,0,1)` |
| Cut handling | Pass **skipped** (pop, stale velocity) | n/a (composite always runs) | Prev matrix **reset** to current |
| Cost drivers | 2 fullscreen passes (vel + MB) | 1 pass, shared with DOF/grade | 1 pass, shared with DOF/grade + stencil downsample |
| SM3.0 fit | Yes (current) | Yes (Xenos `tfetch2D` maps to `tex2D`) | Yes for the algorithm; stencil copy needs an RT |

### 1d.2 Recommendation: **keep the lane, upgrade the shader** (do not port IV's matrix lane as primary)

**Why keep the velocity buffer.** It has three consumers (`MotionBlur_Burnout` s1, SMAA temporal s2/s3,
SSAO temporal s3). IV's `motionBlurMatrix` approach would save the producer pass *only* when
`ssaoTemporalEnable=0 && smaaEnable=0` — it is an optimisation for a configuration we do not ship, and
it would regress SMAA/SSAO temporal quality if we removed the RT.

**Why not a V-style per-object mask.** Requires a stencil-copy RT (`pp_DownSampleStencil`,
`PostProcessFX.cpp:9526`) and a per-entity draw-flag system (`DrawableDataStructs.h:364`). Cost ≈ a new
RT + a full pass + touching every draw path. Not worth it for SA; **defer**.

**Register budget check** (ledger: `docs/plans/2026-10-25-cryengine-parity-matrix.md:156-158` —
"Next free sampler `s10`, next free constant `c53`; post-process passes may use `c0-c8` freely, but a
second new post pass wanting more than c0-c8 must also start at c53"):

| Tier | Samplers | Constants | New resource ID |
|---|---|---|---|
| Tier 0 (C++ only) | — | — | — |
| Tier 1 (shader upgrade) | `s0-s2` unchanged (no jitter texture — keep the procedural hash) | `c0-c3` unchanged | none |
| Tier 2 (optional IV matrix lane) | `s0`=front buffer, `s1`=depth (INTZ); velocity RT no longer bound **by the MB pass** (RT itself stays for SMAA/SSAO) | `c0-c3` unchanged + **`c53-c56` invVP, `c57-c60` prevVP, `c61` params** (c9+ is past the free window) | **286** |
| (noise reservation) | `s9` + `c49-c52` frozen — do not touch | | |

`resources/resource.h` tops out at **285** (`IDR_IVGRADEPS`); 280 is reserved for W8-3, 281/282 free →
**286** is the next safe ID for a new CSO.

### 1d.3 Implementation sketch

**Tier 0 — correctness (recommended first, ~40 LoC, no shader change)**

1. **Framerate normalisation.** In `DrawMotionBlur` (`src/render/postfx.cpp:4071-4134`) divide the
   per-frame deltas by a measured `dt` (or read the game's timestep) *before* the ramps, and scale the
   ramps' thresholds to per-second units. Mirrors V's `lengthReduction`
   (`PostProcessFX.cpp:9358-9364`). Fixes Q4.
2. **Cut → reset instead of skip.** Replace the cutscene early-return (`postfx.cpp:4047-4048`) with
   "if cutscene this frame, latch `g_prevVPMatrix = curVP` (and `camInitialized=false`), then return".
   Same for `IsGameInMenuOrPaused()` in `postfx_motionblur.cpp:38-39`. Eliminates the stale-velocity
   window and the pop (Q5), matching V's `:7494-7497`.
3. Fix the stale comment at `postfx.cpp:2235` (Q10) — **doc-only**.

**Tier 1 — shader upgrade (recommended, one file + small C++)**

Rewrite `shaders/ps/MotionBlur_Burnout.hlsl` (keep the name/resource 140 so no resource churn):

| Change | Stolen from | Lines | Registers |
|---|---|---|---|
| (a) Bidirectional taps: `for i in [-N..N]` with `|i|/N` weight | LR `motion_blur_camera_ps.hlsl:110-139` | ~15 | none |
| (b) **Per-tap depth rejection**: `if(abs(tapZ − centerZ) < threshold)` weight 0 | LR `:120-138` | ~8 | `s2` already bound; reuse `c3.x/y` near/far for the threshold scale |
| (c) Texel-snap tap UVs `(round(uv*res)+0.5)/res` | V `postfx.fx:2169` | ~2 | none |
| (d) Normalise+clamp `blurDir` to a bounded screen magnitude before stepping | V `postfx.fx:2126-2130` | ~5 | none |
| (e) Small-length discard already present (`:107`) — keep | V `postfx.fx:2189` | 0 | none |
| (f) Drop the 3× RGB split to a single fetch (or keep behind `motionBlurCA`-style knob) | IV's 1-fetch taps | −16 fetches | none |
| (g) Replace the screen-centre radial blend with a **depth-scaled** radial term, or gate `motionBlurRadial` to only apply when `fullFrameW` is low | ours, Q8 | ~5 | none |

Expected result: 25 → **13-17 fetches**, symmetric streaks, no background bleed, texel-stable.
No new samplers, no new constants, no new resource ID, no CPU changes beyond Tier 0.

**Tier 2 — optional IV lane (`motionBlurIVReproject`)** — only if we ever want MB with
`velocityBufferEnable=0` (e.g. a "cheap" quality tier). New entry `MotionBlur_IVReproject.hlsl`,
resource **286**, constants at **c53+** per the ledger, gated by a new INI key. Roughly the plan already
written in `docs/plans/2026-10-25-gtaiv-shader-overrides.md:917-931` (that doc proposes
`motionBlurIVReproject` / `motionBlurIVSamples` / `motionBlurIVPlayerMask`) — **recommendation: do not do
this until Tier 0+1 are shipped and verified**; it duplicates existing functionality.

**Explicitly out of scope:** per-object velocity (Q2) and the V stencil-mask lane — both need a
velocity/G-buffer RT SA's RW 3.6 pipeline does not have.

### 1d.4 Cost estimate

| Tier | Code | GPU cost @1080p | Risk |
|---|---|---|---|
| 0 | ~40 LoC C++ (`postfx_motionblur.cpp`, `postfx.cpp`) | **0** (pure CPU gates) | Low — but re-tuning ramp thresholds needs a telemetry pass (`skygfx_dbg.log` `mb_draw` tag) |
| 1 | ~70 shader lines + ~20 LoC C++ (param upload only) | **−0.2…−0.6 ms** (25→13-17 fetches); zero added | Medium — depth-rejection threshold is the one knob that can cause flicker; ship behind the existing `motionBlurEnable` first, A/B against `motionBlurStrength` |
| 2 | ~200 LoC + new CSO + resource plumbing | removes the velocity pass **only** when SSAO-temporal and SMAA are both off | High — new register block at c53+, new state-restore path |

Baseline for reference: our MB pass alone is 1 fullscreen quad at 25 fetches ≈ 52 M texel fetches;
the velocity producer is 2 fetches + CPU 4×4 inverse + cofactor work per frame
(`postfx_motionblur.cpp:131-168`) — the CPU inverse is ~60 multiply-adds, negligible.

---

# PART 2 — PHYSICS SYNERGY (1 page)

**Question:** anything in the same two trees relevant to softbody / cloth / vertex animation?
**Answer:** IV tree = nothing (engine recomp only). V tree = a lot, mostly *not* portable to SM3.0/SA —
one piece **is** portable.

## 2.1 LibertyRecomp (`E:\SDKs\LibertyRecomp-main`) — negative result

The tree is the **RAGE engine recompilation** (`LibertyRecomp/{cpu,gpu,kernel,patches,runtime,install,…}`)
plus `LibertyRecompLib` (shader overrides), `glue`, `libserver`, `os`, `third_party`. There is **no game
code**: no ped cloth, no ragdoll, no animation system.

- `grep -ril cloth` over `LibertyRecomp* glue libserver os` → only `libserver/src/main.cpp` (incidental).
- `CPed`/`CAnim`/`ragdoll` appear only as API surface (`LibertyRecomp/api/RAGE/RAGE.h`) or a patch
  (`LibertyRecomp/patches/gta4_fps_pathfinding_fix.cpp`).

**Conclusion:** GTA IV cloth/softbody RE must go to the **game binary / GTAMods wiki**, not this tree.
Track it under the existing deferred item `docs/plans/2026-07-30-ragdoll-restoration.md` (ARCHIVE) and the
master matrix `docs/plans/2026-10-08-iv-v-graphics-physics-port.md` (ACTIVE).

## 2.2 GTAV source (`…\src\dev_ng`) — findings

### A. Cloth simulation lives *outside* `game\renderer`

`game\cloth\`: `ClothMgr`, `ClothArchetype`, `ClothConstraintGraph`, `ClothConstraints`, `ClothContacts`,
`ClothParticles`, `ClothPendants`, `ClothRageInterface`, `ClothCapsuleBounds` — a full constraint-graph
softbody solver. The renderer only holds references:

| Site | What |
|---|---|
| `renderer/DrawLists/drawListMgr.cpp:23-24` | `#include "fwpheffects/clothmanager.h"`, `#include "cloth/environmentcloth.h"` |
| `renderer/DrawLists/drawListMgr.cpp:425` `ClothCleanup()` | refcount-driven release of `m_Cloths` |
| `renderer/DrawLists/DrawableDataStructs.cpp:1439-1441` | `clothVariationData*` → `gDrawListMgr->AddCharClothReference()` |
| `renderer/DrawLists/drawListCommands_Entities.cpp:1300` | `dlCmdDrawTwoSidedDrawable::UpdateVehicleCloth()` (vehicle cloth, SD-CSE only, `:1308`) |

### B. GPU-side cloth skinning — **two paths, neither SM3.0-portable as-is**

`shader_source/Peds/ped_common.fxh`:

| Path | Mechanism | Slot budget | Verdict |
|---|---|---|---|
| **Xenon** (`ClothTransform` `:1036-1073`, guarded `#endif // __XENON`) | `vfetch` 3 indices from a **secondary vertex stream** (`position1`), weighted blend + cross-product normal, `clothParentMatrix` at **vs c208** | needs an extra geometry stream | **Not portable** — RW 3.6 SA skin atomics have no second position stream |
| **PC/next-gen** (`DX11_CLOTH`, `:1640-1688`) | `clothVertices[DX11_CLOTH_MAX_VERTICES]` in a **paged constant buffer** (`b11`), indexed 3-at-a-time | `DX11_CLOTH_MAX_VERTICES = 254` (`ped_common_values.h:17`) → 254 `float3` ≈ 254 of 256 `vs_3_0` constants | **Not portable** — `vs_3_0` caps at c0-c255 and we need those for skinning/WVP |

`DX11_CLOTH` itself is defined as `__WIN32PC && __SHADERMODEL >= 40 || …` (`:34`) — GTA V explicitly
requires **SM4+** for cloth. Cloth wind (`PED_CPV_WIND`, `:52-55`) rides the same path.

### C. The one portable piece: **damage-texture vertex deformation**

`shader_source/Vehicles/vehicle_damage.fxh` (593 lines) — `DamageSampleValue()` does **`tex2Dlod` in the
vertex shader** (`:178-181`), unpacks a packed radial damage field (`:185-200`) and displaces the vertex;
file header notes *"texture samples in vertex shaders only allowed for shader >= 3.0"* (`:17`).
It is called from every vehicle VS variant via `ApplyVehicleDamage(inputPos, inputNormal,
VEHICLE_DAMAGE_SCALE, …)` (`shader_source/Megashader/megashader.fxh:2577,2709,2951,3130`), driven from
`renderer/ApplyDamage.cpp:372-380` (`GetDeformation()->GetDamageRenderTarget()`, GPU→CPU copy on PC) with
impact→UV mapping at `ApplyDamage.cpp:192,196`.

**SM3.0-legal** (`vs_3_0` supports `tex2Dlod`) and the classic GTA V "softbody" trick: CPU records
impacts → GPU renders a damage/displacement texture → VS samples it to bend the mesh. A SA port needs an
RT + one **world** VS sampler, which must start at `s10` per
`docs/plans/2026-10-25-cryengine-parity-matrix.md:156-158` (noise keeps `s9`+`c49-c52` frozen).
**Candidate for a separate lane, not this doc.**

### D. Blend shapes / vertex-blend deformation

- `renderer/DrawLists/DrawableDataStructs.cpp:1450-1453` — `grbTargetManager*` present →
  `m_bUseBlendShapes = true` (bit at `.h:424`, forwarded to `RenderPed(...)` at `.cpp:1548`);
  **objects** also force it at `.cpp:1463` (damage-offset path).
- `shader_source/Peds/ped_common.fxh:6059-6074` — `#ifdef RAGE_ENABLE_OFFSET` → `technique bs_draw`
  (`:6062`) → `VS_PedTransformBS`.
- `renderer/MeshBlendManager.h:32-60` — `MAX_OVERLAYS 13`, `MAX_NUM_BLENDS 64`,
  `enum eMicroMorphType` (`:46`): 14 face micro-morphs for character creation.

### E. Motion-blur ↔ physics crossover (bonus)

`m_bAddToMotionBlurMask` (§1c.4) is how V decides which entities contribute to the motion-blur stencil
mask — **physics-driven entities opt in/out of the blur**. Cited at `DrawableDataStructs.h:364` /
`.cpp:1212`; the flag to mirror if we ever add per-object blur.

**Bottom line:** the only immediately portable softbody idea here is **damage-texture vertex
deformation**. GTA V cloth is SM4+/Xenon-only in the shader and lives mostly outside `renderer`; GTA IV
cloth is not in the LibertyRecomp tree at all — defer to the binary-RE lane.

**Cross-lane pointers (same-day docs, do not duplicate):** `docs/plans/2026-10-29-rw-physics-ragdoll-re.md`,
`docs/plans/2026-10-29-destruction-mod-inventory.md`, and our own consolidated softbody/ragdoll code in
`src/entities/sftbdyphsx.{h,cpp}` (1,390 LoC, merged 2026-10-26 from `Ragdoll.h` + `dismember.h`).
The `m_bAddToMotionBlurMask` / damage-RT ideas above are the hooks those lanes would plug into.

---

## Sources

**Ours:** `src/render/postfx_motionblur.cpp`, `src/render/postfx.cpp` (:1575-1654, :2137-2309,
:2340-2394, :4019-4288), `src/render/postfx_smaa.cpp`, `src/render/postfx_ssao.cpp`,
`src/render/pipelinecommon.cpp`, `src/skygfx.h`, `shaders/ps/MotionBlur_Burnout.hlsl`,
`shaders/ps/VelocityReconstruct.hlsl`, `shaders/ps/SMAA_EdgeMotionDepth.hlsl`,
`shaders/ps/vectorMotionBlur.hlsl`, `resources/resource.h`, `resources/Resource.rc`.

**Prior in-repo analysis:** `docs/plans/2026-10-25-gtaiv-shader-overrides.md` (§ Motion blur),
`docs/plans/2026-10-25-cryengine-parity-matrix.md` (§4 register ledger),
`docs/plans/2026-10-25-gtaiv-parity-audit.md:400,559`,
`docs/plans/2026-10-25-gtav-parity-audit.md:72,111`,
`docs/plans/2026-10-25-cryengine-parity-matrix.md:28`,
`docs/plans/2026-09-10-libertyrecomp-postfx-port.md:163`.

**GTA IV (LibertyRecomp):** `LibertyRecompLib/shader_overrides/manifest.json`,
`LibertyRecompLib/shader_overrides/motion_blur/gta_composite_mb.hlsl` (+ `_high_dof`, `_dof_debug`,
`_high_dof_debug`), `LibertyRecomp/gpu/shader/hlsl/motion_blur_camera_ps.hlsl`,
`LibertyRecomp/gpu/shader/hlsl/postprocess_reference/motion_blur_{camera,enhanced}_ps.hlsl`.

**GTA V:** `game/renderer/PostProcessFX.cpp`, `PostProcessFX.h`, `PostFX_shared.h`,
`game/renderer/DrawLists/{DrawableDataStructs.cpp,.h,drawListCommands_Entities.cpp,drawListMgr.cpp}`,
`game/renderer/Entities/{ObjectDrawHandler,VehicleDrawHandler}.cpp`, `game/renderer/ApplyDamage.cpp`,
`game/renderer/MeshBlendManager.h`, `game/shader_source/PostFX/postfx.fx`,
`game/shader_source/Peds/{ped_common.fxh,ped_cloth.fx}`,
`game/shader_source/Peds/ped_common_values.h`, `game/shader_source/Vehicles/vehicle_damage.fxh`,
`game/shader_source/Megashader/megashader.fxh`, `game/cloth/*`.

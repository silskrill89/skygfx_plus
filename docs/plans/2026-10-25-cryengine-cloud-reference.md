# CryEngine cloud / atmosphere reference → SM3.0 port plan

Source: `E:\dev(dave)\releases\GTASource\CRYENGINE-release\Engine\Shaders` (+ `EngineAssets`).
All line numbers below are from that tree. Our tree: `E:\dev(dave)\skygfx_plus_expIV`.

**TL;DR**
1. The whole cloud family is **one file**: `CryFX/Clouds.cfx` (2373 lines). Everything else is sky/fog/shafts.
2. CryEngine does **not** raymarch clouds per pixel in the shipped path — it injects density+shadow into a **frustum-aligned 3D texture** (`InjectCloudDensityAndShadowCS`) and consumes it (`RenderCloudPS`). Both are DX10 compute. SM3.0 must use `CloudsLayerPS` (inline density) instead.
3. Real cloud shadow is a **precomputed 3D optical-thickness volume** (`CloudShadowGenCS`, 3 rays/voxel). Portable substitute: a 256² PS-to-RT pass → 1 fetch/step. This is the single biggest design decision (see §5.3).
4. The cloud path consumes **`noise3d.dds` (64³ A8)**, never the `perlinNoise*.tif` family. Our universal tile is a valid replacement for the *density* function only, with a 3D→2D shear mapping (§3.3).
5. One extra 256² RGBA8 aux texture (s10) removes **all** remaining asset dependencies: R = base density, G/B/A = sun/sky/ground optical depth. Nothing gets repacked inside the universal tile.

---

## 1. Inventory

| File | Lines | Purpose |
|---|---|---|
| `CryFX/Clouds.cfx` | 2373 | Everything volumetric-cloud: density gen, FBM, atmospheric inscatter, cloud-shadow volume gen, layer render, temporal reproject, upsample/composite, density injection, final render. |
| `CryFX/DistanceClouds.cfx` | 412 | Camera-facing quad "distance clouds": ray-marches a **height texture** toward the sun (8-tap simple / 16-48-tap AABB slab). |
| `CryFX/Sky.cfx` | 154 | Flat sky: white texture × `TFactor`, pushed to far plane. No scattering. |
| `CryFX/SkyHDR.cfx` | 282 | Sky-dome composite: 2 precomputed dome LUTs + analytic Rayleigh/Mie **phase only** + night gradient + moon + corona + fog. |
| `CryFX/Stars.cfx` | 147 | Night star field quad (`SV_VertexID`, star shimmer LUT, moon occlusion). Sky family, not atmosphere. |
| `CryFX/Sunshafts.cfx` | 286 | Screen-space god rays: mask gen (depth × HDR luma), 8-tap radial decay blur, soft-light display. |
| `CryFX/VolumetricFog.cfi` | 762 | Analytic exponential height fog (`ComputeVolumetricFogInternal`), phase functions (Schlick/HG), 4D smooth noise via `rotrandomcm.dds`, 3D-volume sampling helpers, jitter. Included by `DeferredShading.cfx:2192`. |
| `CryFX/VolumeLighting.cfi` | 2469 | Volumetric-fog **voxel** pipeline: fog-volume cull/accumulate, `GetExponentialHeightFogDensity`, sun shadow accumulation, blur, `RaymarchVolumetricFogCS`, reprojection. |
| `CryFX/FogVolume.cfx` | 364 | Analytic box/ellipsoid fog volume with closed-form height integral. |
| `CryFX/HDRPostProcess.cfx` | ~1100 | `FogPassPS` (887), `MultiGSMShadowedFogPS` (628), vol-shadow interleave/gather (700/780), LDR sunshaft composite (382), fog-pass techniques (1030-1085). |
| `CryFX/Common.cfi` | 798 | `ComputeGlobalFogPS` (47) — linear distance fog used by everything else. |
| `CryFX/Common.cfx` | 449 | Fog application on particles/beams via `GetVolumetricFogColor` (237, 381, 424). |
| `CryFX/WaterFogVolume.cfx` | — | Underwater box fog; same `FogVolShadowJitter.dds` jitter as HDRPostProcess. Out of scope. |

Dead branch, do not plan around it: `Clouds.cfx:12` has `//#define ENABLE_CIRRUS_CLOUD_LAYER` **off**, and `EngineAssets/Textures/cloud/` (stratus mask/diff/ddn) **does not exist in this export**. `GetAltoAndCirrusCloud` (868-914) is unreachable code.

---

## 2. Algorithm summaries

### 2.1 Volumetric clouds — `Clouds.cfx`

**Geometry** — fullscreen quad. `CloudsLayerVS` (494) takes a unit quad, `HPosition = (vPos.xy*2-1, vPos.z, 1)`, interpolates `CamVec`. Script declares `ShaderDrawType = Sky` (40-41), so it is a sky-pass draw, not a post pass.

**Ray / layer intersection** — `vcGetIntersectionWithCloudLayer` (156): two horizontal planes, `altitude` and `altitude+thickness`; flat planes in the default permutation, spherical shells under `%_RT_SAMPLE2`. Clamps in order (1035-1056): `max(tb,tt)` → `MaxIntersectionDistance` → `MaxRaymarchDistance` → scene depth → **horizon plane** (`vcGetIntersectionPlane(…, float4(0,0,1,horizonHeight))`, 1046) so nothing is marched below sea level.

**Step distribution** — `vcGetLinearDepth` (120): `d(i) = (end-start)·pow(i/(N-1), 2) + start`. **Quadratic**, dense near camera. `N = vcCloudRaymarchParams.z`, dynamic; no hardcoded cap in the file.

**Jitter** — interleaved-gradient noise, `GetJitterInternal` (`VolumetricFog.cfi:308`): `frac(52.9829189 · frac(dot(p, (0.06711056,0.00583715))))`, called as `GetJitterInternal(pixel, frameCount.xx)` (1073, 1861, 2109). Jitter ∈[0,1) is **added to the step index** (`vcGetLinearDepth(index + jitter, …)`, 1153), so it randomises the first-step offset, not the step count. Van der Corput (`GetVdC`, 129) exists but `ENABLE_LOW_DISCREPANCY_SEQUENCE` is `#undef` (31).

**Density** — `GetCloudDensity` (314):
- `cpos = (posWS + baseOffset) * noiseSizeScale`
- FBM: 2 octaves (340), `frequency = 2.71285739`, each octave pre-rotated by a fixed 3×3 (`0,0.8,0.6 / -0.8,0.36,-0.48 / -0.6,-0.48,0.64`, 226), amplitude ×0.5, normaliser `1.66` (224), sampled as `tex3Dlod(noise3dsampler, …).w * 2 - 1` (243). Optional 3D base-density texture path under `%_RT_SAMPLE3` (330-338).
- Height ramp (348-354): 0→1 over `[0, 0.3·thickness]`, plateau, 1→0 over `[0.7·thickness, thickness]`. **The top fade is in the density, not only the plane clip.**
- Edge erosion (357-361): one extra 1-octave FBM at `edgeNoiseSizeScale`, optionally `abs()` (turbulence), added or subtracted by `edgeTurbulence × (edgeFactor or -(1-edgeFactor))`.
- `× cloudiness²`, `× cloudDensityScale` (365-367).

**Lighting** — `AccumulateRadiance` (795). Four terms, not one:
`S = σs·ρ·(shadow·sunLow + highOrderShadow·sunHigh + skyShadow·sky + groundShadow·ground) · powder`,
`powder = 1 - exp(-Στ·powderFactor)` (833), in-voxel integration `Sint = (S - S·trns)/muE`, `muE = max(1e-9, σe·ρ)` (855-857), then `inscatter += T·Sint; T *= trns`.
- Phase: Mie single scattering from a **2048×1 LUT** `cloud_mie_phase_function.dds` (90) indexed `acos_fast(dot(camDir,sun))/π` (980, 2000). `acos_fast` = sqrt polynomial (115).
- Low-order (2-30 bounces): **Schlick**, not HG — `GetVolumetricFogInscatteringSchlickPhaseFunction` (2008). The HG call is present but commented out (987, 2007).
- High-order (31+): isotropic `1/4π` (995, 2015).
- Multi-scatter shadow remap: `lerp(preservation, 0, exp(-τ·attenuation))` (826-828).

**Shadow** — two paths:
- Precomputed: `Texture3D<float4> VolCloudShadow` (545), 3 channels = optical depth toward sun / zenith / nadir, trilinear, WRAP×WRAP, CLAMP in W (66-75).
- Inline: `GetOpticalThicknessCombined` (572): **6 steps** toward the sun when inside the shadow tex, **3** when outside (620-622), plus two analytic vertical terms with a 3-step coarse march. `GetOpticalThickness` (377) is the 6-step version used by the shadow-volume generator.

**Composite** — `OUT.Color = float4(inscatter, transmittance)` (1331, 2360) — **reversed alpha** (`// use reversed alpha to reduce composition banding`, 1329). Correct state is `SRCBLEND=ONE, DESTBLEND=SRCALPHA`. The technique blocks (1336, 2365) set **no** blend state; the engine owns it. Also writes `cloudDepthOutput` (front-cloud depth, 0 = empty, 1206-1208) and `cloudMinDepthColorOutput` (1299).

**Temporal** — `ReprojectClouds` (1358): `blendRatio = 0.95` (1416), neighbourhood clamp against the 4 diagonal taps (1439-1441), depth-similarity `exp2(-40·|Δdepth|)` (1448), range compression `exp2(-rgb)` / `-log2` (1454/1467).

**Upsample** — `CloudsBlend` (1536): repaints a manual 4-tap bilinear from the scaled buffers with **depth-aware weights** `1/(|depth|+1e-5)` applied separately to min- and max-depth colour (1601-1604), avoiding silhouette halos. Full-size fog is applied at the cloud's **front depth**, not the pixel depth (1632-1660).

### 2.2 Cloud layers / stratiform clouds
- `DistanceClouds.cfx` `ADVANCED` (289): density = height texture `.x`; ray traced through the slab AABB in (uv, height·CloudHeight) space using the slab method (303-311); `numSamples` 16/24/32/48 by quality (276-287), `[unroll]` (315); `density *= 64/N` (323); `sky = exp(-h·H·DensitySky)`, `sun = exp(-DensitySun·density)` (326-329); `alpha = pow(saturate(h·AlphaMultiplier), AlphaSaturation)` (335).
- Simple path (231): 8 forward taps along `toSun.xy·StepSize`, `c = exp2(-Attenuation·density)`, lerp sky↔sun (262-264).
- Blend: `ZEnable=true, ZWrite=false, AlphaBlendEnable=true, SrcBlend=SrcAlpha, DestBlend=InvSrcAlpha, CullMode=None` (395-401).
- Cirrus/alto: see §1 — dead in this tree.

### 2.3 Sky / atmosphere
- `Sky.cfx`: no atmosphere. `OUT.Position.z = w` (57), `ZEnable, ZWrite=false, Cull None`, colour = white × `TFactor` (85-86).
- `SkyHDR.cfx`: **the optical depth integral is precomputed**, only the phase is per pixel. Two dome LUTs `$SkyDomeMie` (s0) / `$SkyDomeRayleigh` (s1), 19-22; `miePhase = (1+cos²)·pow( a + b·cos, -1.5 )` where `a = miePart^(-2/3)(1+g²)`, `b = miePart^(-2/3)(-2g)` (169-176); `rayleighPhase = 0.75(1+cos²)` (177); `Color = MieLUT·partialMieConst·miePhase + RayleighLUT·partialRayleighConst·rayleighPhase` (179). Night sky gradient + moon + two corona lobes are additive (183-199). Clamped to 16384 (204).
- Atmosphere as a **volumetric** term also exists inside the cloud march: `GetAtmosphericParam` (710) = Rayleigh `3/(16π)(1+µ²)` and Mie with `g=0.8`, `3/(8π)·(1-g²)(1+µ²)/((1+g²-2gµ)^1.5·(2+g²))`; `GetAtmosphericInscattering` (728) = `exp(-h/8000)` and `exp(-h/1200)`, Mie coeff `21e-6`, 4 airlight steps before the cloud march (1120-1125) and 1 per cloud step (1185).
- `HDRPostProcess.cfx` fog passes: `FogPassPS` (887) applies `GetVolumetricFogColor` (analytic) and **un-premultiplies then re-premultiplies alpha** (912-918); `MultiGSMShadowedFogPS` (628) is a 16-sample × up-to-4-cascade shadowed-fog pass writing `float4(refDepth,1,1,occlusion)`.

### 2.4 Height fog
- Closed form, `FogVolume.cfx` (173-193) and duplicated in the box path (327-339):
  `fogInt = |dist| · exp(-dot(front - base, heightFallOffDirScaled))`
  `heightDiff = dot(dist, heightFallOffDirScaled)`; if `|heightDiff| > 1e-3`: `fogInt *= (1-exp(-heightDiff))/heightDiff`
  `fog = 1 - exp2(-globalDensity.y · max(fogInt - densityOffset.x, 0))`
  Output `float4(fogColor, fog)`, `SrcAlpha/InvSrcAlpha`, `ZEnable=false, CullMode=Front, ColorWriteEnable=RGB` (200-217).
- Per-pixel analytic (used by *every* material), `VolumetricFog.cfi:ComputeVolumetricFogInternal` (17): `t = heightScale·z`; `fogInt = (exp(t+h0) - exp(h0))/t` with a Taylor branch at `|t|≤0.01` (28-37); `× |camToWorld| · densityAtBase` (40-42); `f = saturate(exp2(-fogInt))`; distance ramp `r = saturate(l·ramp.x+ramp.y); r=r(2-r); r=r·ramp.z+ramp.w` (46-48); `f = (1-f)·r`; return `max(1-f, densityClamp)`. **No raymarch, no noise, ~25 ALU.**
- Voxel fog, `VolumeLighting.cfi`: `GetExponentialHeightFogDensity` (563) = `saturate(densityAtCamera · exp(heightScale·z)) · ramp(linearDepth)`. Injected into a 3D texture (809), blurred (138/246/386/465), composited per depth slice in `RaymarchVolumetricFogCS` (1917) with the same `Sint = (S - S·trns)/muE` integration, then reprojected (2014).

### 2.5 Sun shafts — `Sunshafts.cfx`
- `SunShaftsMaskGenPS` (126): `depth = GetDownscaledDepth(_tex0)`, `OUT = (depth,depth,depth, 1-depth)`, then `xyz *= dot(downscaledHDR, 0.33*4)` (140) — i.e. brightness-weighted depth mask.
- `SunShaftsGenPS` (145): 8 taps, `accum += depth·(1 - i/8)`, `/8`, `×2`, `× sunDist.xxx`, where `sunDist = saturate(fSign)·saturate(1 - |sunVec·(1,aspect)|·params.y)` (157); `w += 1 - saturate(fSign·0.1+0.9)` (192).
- Display (216): `cScreen + shafts·params.y·sunColor·(1-cScreen)` then `blendSoftLight` (12-slot approximation, 199-214). `CullMode=None`, no Z.
- `OcclCheckTechnique` (50) draws a constant red quad — hardware occlusion query only, not a visual pass.

---

## 3. Noise assets

### 3.1 What is actually in `EngineAssets/Textures`
Measured with PIL/DDS-header parse (dims from the file, not from docs):

| File | Format | Dims | Content | Consumed by |
|---|---|---|---|---|
| `noise3d.dds` | 1 byte/voxel, 7 mips | **64×64×64** | 3D FBM (read as `.w`, i.e. alpha volume) | `Clouds.cfx:79` |
| `cloud_mie_phase_function.dds` | RGBA32F (`2048·16` bytes) | **2048×1** | Mie phase vs `acos(cosθ)/π` | `Clouds.cfx:90` |
| `rotrandomcm.dds` | RGBA8, all 4 ch uniform random | 64×64 | 4D hash LUT (`uv = i.xy + 7·i.z + 89·i.w`) | `VolumetricFog.cfi:128`, `FogVolume.cfx:32` |
| `FogVolShadowJitter.tif` | 8-bit | 64×64 | interleave offset + jitter (`.x`,`.w`) | `HDRPostProcess.cfx:444`, `WaterFogVolume.cfx:53` |
| `perlinNoise2d.tif` | RGB, 1 ch replicated | 256² | Perlin, mean 104/255 | `CommonViewsPass.cfi:101`, `WaterVolume.cfx:452`, `PostEffectsGame.cfx:1379` |
| `perlinNoise_sum.tif` | RGB replicated | 256² | fBm sum, mean 203/255 | `CloakLayer.cfx:179`, `PostEffectsGame.cfx:30` |
| `perlinNoise_sum_small.tif` | RGB replicated | 128² | same, half res | **no consumer in this tree** |
| `perlinNoiseDerivatives.tif` | R/G derivative, B≡0 | 256² | dPerlin | `DeferredSnow.cfx:123` |
| `perlinNoiseNormal_ddn.tif` | RGB normal | 256² | normal map of Perlin | `Monitor.cfx:33`, `PostEffectsGame.cfx:248` |
| `noise.tif` | RGBA, 4 *distinct* channels | 64² | R/G/B different distributions, A≡255 | **no consumer** |
| `vector_noise.dds` | RGBA8, R/G bipolar | 64² | flow vectors | `PostEffectsGame.cfx:972,1921` |
| `jumpnoisehighfrequency_x27y19.dds` | RGB replicated, A≡255 | 64² | high-freq grey noise | **no consumer** |

**Key cross-reference finding:** the cloud path uses **only** `noise3d.dds` + the Mie LUT. The `perlinNoise*` 2D family is used by cloak/snow/monitor/water/post — *not* by clouds. So our universal tile is not replacing anything the cloud shader currently reads; it is a substitute for a **3D** texture.

### 3.2 Sampling details
- `noise3d`: `sampler3D`, LINEAR/LINEAR/LINEAR, WRAP/WRAP/WRAP (77-86); fetched as `tex3Dlod(…, float4(pos,0)).w` at **mip 0** (238, 243). 2 fetches for the body FBM + 1 for erosion = **3 volume fetches per density sample**; the shadow march multiplies that by 6 (or 3), so worst case ≈ 21 fetches/step before the shadow texture lookup.
- `cloud_mie_phase`: Clamp/Linear, one tap per pixel **outside** the loop (980, 2000) and once in `GetAltoAndCirrusCloud` (892).
- `rotrandomcm`: 8 taps per `vfGetSmoothNoise` (157-172), 2 octaves in `vfGetDensityNoise` (201-203) → **16 taps per fog-density sample**; `worldPos %= 1000` to keep tiling (195).
- `FogVolShadowJitter`: `tex2D(interleave, WPos.xy/64).xw` → one shared 64² tile gives both the sample offset and the per-pixel jitter (`HDRPostProcess.cfx:635-636`).

### 3.3 Mapping to our universal tile (`shaders/include/Noise.hlsl`)

| CryEngine need | Our channel | Verdict |
|---|---|---|
| `GetFBM` body (noise3d `.w`) | **R** (Perlin fBm) | direct; use `NoiseFbm3()` (3 taps) instead of 2 rotated octaves |
| edge erosion / turbulence | **G** (Worley F1) | direct, and better shaped than abs-FBM |
| `FogVolShadowJitter` / step jitter | **A** (blue) | direct — but prefer analytic IGN for the step jitter (0 fetches, `VolumetricFog.cfi:308`) |
| integer hash / per-pixel random | **B** (white) | direct |
| 3rd spatial axis (noise3d is a volume) | **—** | **missing.** See below |
| Mie phase LUT (2048×1) | **—** | do **not** pack: analytic Schlick is ~6 ALU (`Clouds.cfx:988` formula) |
| cloud shadow volume (3 ch) | **—** | do **not** pack into R/G/B/A: needs a writable 2D RT (§5.3) |
| large-scale base density | **R at low scale** | partial: R at `scale·0.25` is a valid low-frequency mask, but band-limited by the tile |

**3D→2D substitution.** `GetCloudDensity` indexes noise by `cpos = (posWS+baseOffset)·noiseSizeScale`, using z as the *height* axis. Our tile has no z. Use a **shear**, not a slice:

```
float2 cloudNoiseUV(float3 cpos, float s) { return cpos.xy * s + cpos.z * float2(0.37, -0.21) * s; }
```

The shear keeps vertical coherence (a column of cloud moves laterally with height, like wind shear) and cannot produce the "cheese slice" popping you get from `frac(z)` slicing. Two taps at `z` and `z + 0.5` are enough for a visible 3D feel; three is the practical max.

**Answer to the "one noise generator" question.**
- **Can the cloud path be driven entirely by the 4-channel universal tile?** For density, phase and jitter — **yes**. For the full CryEngine path — **no**, because two things are not noise: the shadow volume and the base-density map.
- **Do we need extra channels inside the tile?** **No. Do not touch R/G/B/A** — `Noise.hlsl` is the shared contract with stochastic detail texturing, and the cloud path needs exactly the same four primitives.
- **What to add:** one **256² A8R8G8B8** texture bound at **s10**, LINEAR, WRAP, no mips, written by our own fullscreen PS pass:
  - `R` = base/large-scale cloud density (replaces `CloudBaseDensityTexture`, `Clouds.cfx:332`)
  - `G` = sun optical depth (replaces `VolShadowRW.x`, `Clouds.cfx:479`)
  - `H`/`B` = sky (zenith) optical depth (replaces `.y`)
  - `A` = ground (nadir) optical depth (replaces `.z`)
  That is an exact 3-of-3 match to `VolShadowRW[pixelCoord] = float3(sunOD, skyOD, groundOD)` plus the base map in R, produced for free inside the same pass.

---

## 4. SM3.0 feasibility

**ps_3_0 facts used below:** static instruction limit 512 (device may report more via `MaxPixelShader30InstructionSlots`, but 512 is the floor and the safe target); dynamic `rep` loops are legal but **FXC unrolls loops with compile-time-constant bounds** — force `[loop]` and a bound read from a constant; `texld` (implicit derivative) is illegal inside a dynamic loop/divergent branch → use `texldl`; 16 samplers; no integers, no bitwise, no UAV, no compute, no `Texture3D` with mip chains we can write.

Slot counts below are **planning estimates counted by hand from the CryEngine source**, not measured FXC output. Method: 1 scalar op ≈ 1 slot, `texld` = 1 slot, `exp/pow/sqrt` ≈ 4 slots (transcendental block), `smoothstep` = 6.

| Technique | Ports to ps_3_0? | Loops | Estimated static body | Verdict / max steps |
|---|---|---|---|---|
| Volumetric clouds (`CloudsLayerPS`) | **partially** | yes, must stay dynamic | ~48 slots + 3 tex per step | **24 steps @ half-res, 16 @ full-res.** Static total ≈ 168 slots (120 prologue + 48 body) — fits. Unrolled 16× would be ~890 → **compile failure**, so `[loop]` is mandatory. |
| Cloud render from volume (`RenderCloudPS`) | **no** | — | — | needs `VolCloudDensity`/`VolCloudShadow` 3D textures that only a CS can fill. |
| `DistanceClouds` advanced | **yes** | `[unroll]` by design | ~8 slots/tap | **cap 16 taps.** 48 taps × 8 = 384 + prologue ≈ 480 → borderline, and 48 unrolled iterations will hit 512 on most drivers. |
| Sky dome (`SkyHDR`) | **yes** | no | 3 tex, ~45 slots | full port; drop the two LUTs for analytic phase if asset budget matters. |
| Height fog (`FogVolume` closed form) | **yes** | no | 2 `exp`, ~30 slots | full port, zero risk. |
| Height fog (`ComputeVolumetricFogInternal`) | **yes** | no | ~25 slots | full port; this is the cheap per-pixel variant. |
| Volumetric fog (voxel) | **no** | — | — | 3D texture + CS injection + reprojection. |
| Sun shafts | **yes** | `[unroll]` 8 | ~6 slots/tap | full port, 8 taps fixed. |
| Cloud-shadow volume gen | **no** as CS | — | — | port as **PS→RT**, 256², 8 steps (§5.3). |

**Per-step slot budget for our cloud port (the number to design against):**

| Work | Slots |
|---|---|
| uv reconstruct + shear + height normalise | 4 |
| R fBm, 2 taps (`texldl` + lerp) | 2 tex + 5 |
| G Worley carve (`texldl` + smoothstep) | 1 tex + 6 |
| coverage smoothstep + density scale | 10 |
| height ramp (one `smoothstep`) | 6 |
| extinction · density · stepSize | 4 |
| shadow fetch (RGBA aux, 1 `texldl`) + Beer | 1 tex + 6 |
| powder `exp` + 4-term accumulate | 12 |
| **Total** | **≈ 48 slots, 3 fetches** |

At 1920×1080, 16 steps × 3 fetches = 100 M fetches/frame. At 960×540, 24 steps = 37 M. Both are fine on a modern GPU; neither is fine at 4K. Ship half-res first.

**Hard SM3.0 constraint found in our own code:** `shaders/include/Noise.hlsl` helpers all use `tex2D` and the header states "no `tex2D` inside a dynamic branch". A raymarch loop *is* such a context. The cloud shader therefore needs `NoiseUniversalLod(uv)` = `tex2Dlod(uniNoiseTex, float4(uv,0,0))` added to `Noise.hlsl` (or local to the cloud file) — and because the tile has **no mips** (`noise.cpp` sets `MIPFILTER = NONE`, `MAXANISOTROPY = 1`), `texldl(…,0)` is exact and free of the usual mip-selection cost.

---

## 5. Port plan

### 5.1 Entry points, registers, resources

| Item | Value |
|---|---|
| New shader file | `shaders/ps/CloudsVolumetric.hlsl` — entry `main`, so `/E main` in `tools/fast_build.py` needs **no** multi-entry edit |
| Existing file kept | `shaders/ps/CloudsPS.hlsl` (`main_clouds` / `main_cloudsSky`) — the additive layer, unchanged |
| Resource ID | `IDR_CLOUDVOLUMETRIC 255` (`resources/resource.h`; 250-254 taken, 255-259 free) |
| **Reserved (do not touch)** | **s9**, **c49-c52** (`Noise.hlsl`) |
| **Next free constant** | **c53** (and c0-c48 are entirely free inside this entry — each shader has its own c file) |
| **Next free sampler** | **s10** |

Concrete slot map (fills c0-c12 only; c13-c48 + c53-c223 spare):

| Reg | Contents |
|---|---|
| c0 | `{ stepCount, maxRayDistance, layerAltitude, layerThickness }` |
| c1 | `{ coverage, erosion, densityScale, heightFalloff }` |
| c2 | `{ sunIntensity, scatterCoeff, extinctionCoeff, powderFactor }` |
| c3 | `{ sunDir.xyz, sunDirIntensity }` |
| c4 | `{ skyColor.rgb, ambientScale }` |
| c5 | `{ projInfo.x, projInfo.y, projInfo.z, projInfo.w }` — reuse `HeightFog.hlsl` c2 convention |
| c6 | `{ frontBufferU, frontBufferV, 0, 0 }` — reuse `HeightFog.hlsl` c3 |
| c7 | `{ camPos.xyz, horizonHeight }` |
| c8 | `{ camRight.z, camUp.z, camAt.z, 0 }` — reuse `HeightFog.hlsl` c5 |
| c9 | `{ shadowTiling.xy, shadowOffset.xy }` (aux texture UV transform) |
| c10 | `{ groundAlbedo.rgb, groundLightScale }` |
| c11 | `{ fogDensity, fogFalloff, fogStartHeight, fogMax }` — forward to the height-fog integral |
| s0 | scene colour, padded 2048² (same as `HeightFog.hlsl`) |
| s1 | INTZ depth, screen-sized |
| s9 | universal noise tile |
| s10 | 256² cloud aux (base density + 3-ch shadow), LINEAR/WRAP/no-mips |
| c50.z / c50.w | time / frame index — already uploaded, drives animation + jitter |

Everything is drawn through the existing `CPostEffects::Im2D` path (`CPostEffects::ImmediateModeRenderStatesStore/Set`, `guardedIm2DRender`, see `src/render/postfx.cpp:2520+` and `DrawHeightFog` at 3874). Insert the cloud pass **immediately before** `DrawHeightFog` in the `HeightFog`/`GodRays` block (`postfx.cpp:1925-1947`), so height fog and shafts composite over clouds the way CryEngine does (`Clouds.cfx:1217-1285` applies fog to the cloud front depth).

### 5.2 Per-technique keep / cut / approximate

**Volumetric cloud layer** (`CloudsLayerPS`)
- **Keep:** quadratic step distribution (`vcGetLinearDepth`), index jitter, reversed-alpha output, 4-term lighting with powder, precise in-voxel `Sint`, the horizon clamp.
- **Cut:** convergence `%_RT_SAMPLE1` stereo reprojection, `ENABLE_CLOUD_ATMOSPHERIC_*` in-loop atmosphere (4+1 extra steps), cloud blockers (`vcCloudBlockerPos[4]`), `CloudBaseDensityTexture` 3D path.
- **Approximate:** 6-step sun shadow march → **1 aux-texture fetch** (or **2-tap probe** if the aux texture is not ready: `shadow = exp(-σe·ρ·200·2)`); `noise3d` 3D FBM → R fBm with shear (§3.3); Mie LUT → Schlick; sky/ground optical depth from aux `.b`/`.a` instead of two extra marches.
- **Steps:** 24 half-res default, 16 full-res, clamp `stepCount` in c0 from the INI.

**Distance clouds / cirrus** — port only if a height-map quad is wanted later. If so: 16 taps, `[unroll]`, `SRCALPHA/INVSRCALPHA`, `ZEnable/ZWrite=false`.

**Sky** — do **not** add `SkyHDR.cfx`'s LUT pipeline. Instead extend the existing `shaders/ps/DynamicSky.hlsl` (already uploads c0-c8 from `postfx.cpp:2432-2520`) with the analytic phase block from `GetAtmosphericParam` (710) topped with `rayleighPhase = 0.75(1+cos²)` and `miePhase` with `g=0.8`. ~25 slots, no new assets, and it reuses the timecycle sun direction already in `c3`.

**Height fog** — replace the current `HeightFog.hlsl` body (`1 - exp(-fogDensity·linearDepth)`, lines 62-64) with the `FogVolume.cfx` closed form:
`fogInt = |dist|·exp(-dot(front-base, falloffDir))`, `·(1-exp(-heightDiff))/heightDiff`, `fog = 1-exp2(-density·max(fogInt-offset,0))`.
Cost: 2 `exp` + 1 `exp2` ≈ 15 extra slots. This is the single highest-value change in the whole plan: it fixes distance-integration error on tilted/aerial cameras that the current linear-depth formula has.

**Volumetric fog** — do not build the voxel pipeline. If more than height fog is wanted, do an **8-step per-pixel analytic march** of `GetExponentialHeightFogDensity` (`VolumeLighting.cfi:563`) with IGN jitter, accumulating with `Sint = (S-S·trns)/muE`. 8 steps × ~20 slots = 160 slots, 0 fetches, trivially affordable. One extra tap into aux `.g` gives cloud shadows inside the fog.

**Sun shafts** — keep `GodRays.hlsl`; add the two missing terms from `SunShaftsGenPS`: radial sun-distance falloff `saturate(1-|sunVec·(1,aspect)|·params.y)` (157) and the soft-light display (199-214, 238). 8 taps fixed.

### 5.3 Cloud shadow map as a PS→RT pass (replaces `CloudShadowGenCS`)
`Clouds.cfx:440-488` is 16×16 compute; SM3.0 cannot run it. Reproduce it as one fullscreen PS into a 256² A8R8G8B8 RT:
- One texel = one ground column at the cloud layer. Reconstruct world xy from uv via a `c9` tiling transform (same maths as 456-459).
- Per texel: 3 rays (sun, +z, -z), 6 quadratic steps each (`GetOpticalThickness`, 377), 2 noise fetches/step (R body + G erosion at the shear UV) → ~12–18 fetches/texel.
- Output `float4(baseDensity, sunOD, skyOD, groundOD)` (= `VolShadowRW` semantics, 479 + base density from 332).
- Refresh every N frames or when `cloudAnimTimer` moves > threshold; 65k texels is noise-level cost.
- Bind as **s10**, LINEAR/WRAP, no mips. Sample in the cloud march as `exp(-aux.g·σe)`.

This is the one place where the port is *structurally* different from CryEngine: they amortise the shadow over frames in 3D; we amortise it in a 2D map. Visual difference is negligible for a single cloud layer, and it removes the 6× fetch multiplier, which is what makes 24 steps affordable at all.

### 5.4 Order of work
1. `Noise.hlsl`: add `NoiseUniversalLod()` + `NoiseFbm3Lod()`. (No behaviour change for existing users.)
2. `HeightFog.hlsl`: swap in the closed-form integral. Independent, testable alone.
3. New `CloudsVolumetric.hlsl` with stub aux (constant shadow), 16-step full-res. Verify reversed-alpha composite + depth skip (reuse `HeightFog.hlsl:38` sky test).
4. Add the 256² shadow/base pass + s10; flip the march to 1 aux fetch/step; raise to 24 steps.
5. Half-res + upsample only if the profile demands it (needs `CloudsBlend`-style depth-aware upsample, 1601-1604).
6. `DynamicSky` analytic phase, `GodRays` soft-light — cosmetic, do last.

---

## 6. What NOT to port, and why

| Cut | Why |
|---|---|
| `CloudShadowGenCS` (440), `InjectCloudDensityAndShadowCS` (1697) | `[numthreads]` compute + `RWTexture3D` u0/u1. D3D9 has no compute and no UAV. Superseded by §5.3. |
| `RenderCloudPS` (1949) + `CloudsLayer` volume path | Depends on the two CS-filled 3D textures. Dead without compute. |
| `ReprojectClouds` (1358) | Needs a persistent cloud history RT + previous-frame VP matrix. We own neither; our postfx chain ping-pongs a 2048² front buffer (`postfx.cpp:1924`). Ghosting risk exceeds the benefit at 60 fps. |
| `CloudsBlend` min/max-depth bilateral upsample (1536) | Two extra `RWTexture2D` outputs (u1/u2, 547-548). Doable with plain RTs, but only worth it after the base pass is profiled. |
| `ENABLE_FULL_SIZE_FOG` + NaN masking (1288-1295) | NaN-as-mask (`0.0/0.0`) only makes sense with the half-res pipeline it guards. On SM3.0 NaN writes black; never ship this idiom. |
| Cloud blockers (`vcCloudBlockerPos[4]`, 260-296) | Dynamic-index const array + 4 `[branch]`es. ps_3_0 can do it, but it costs prologue slots for a gameplay feature we do not have. |
| `ENABLE_CLOUD_ATMOSPHERIC_ATTENUATION` (743-761) | 4 extra sun-ward steps + 2 `exp` per step for a tint. Use the sky colour (c4) instead. |
| Van der Corput `GetVdC` (129) | Needs integer bit-reversal; ps_3_0 has no integer ops. Already `#undef`ed. |
| `noise3d.dds` 64³ with 7 mips | Contract violation for our noise service (2D tile, no mips) and 262 KB of A8 with mip-selection cost inside a loop. |
| `cloud_mie_phase_function.dds` 2048×1 RGBA32F | 32 KB + 1 sampler for a function that is 6 ALU (Schlick, `Clouds.cfx:988`). |
| `Textures/cloud/stratus_*` cirrus path | Directory absent from the export; `ENABLE_CIRRUS_CLOUD_LAYER` is off (`Clouds.cfx:12`). |
| `MultiGSMShadowedFogPS` (628) | 16 samples × up to 4 cascades, and it needs CryEngine's GSM cascade layout + `TexGen0..3`. We do not own SA's shadow cascades. |
| VolumetricFog voxel pipeline (`VolumeLighting.cfi` 809-1975) | 3D texture + CS injection + blur + reprojection. Replaced by the 8-step analytic march (§5.2). |
| `Stars.cfx` | `SV_VertexID` (DX10 only) and a per-star vertex buffer we do not generate. |
| `rotrandomcm` 16-tap 4D noise (`VolumetricFog.cfi:147-215`) | 16 fetches per density sample for a low-frequency break-up. Our analytic height fog needs none; if texture is wanted, 1 blue-noise tap is enough. |
| `DistanceClouds` 48-tap quality tier | `[unroll]` 48 × ~8 slots ≈ 384, plus prologue, against a 512 static budget. Cap at 16. |

---

## 7. Risks / open questions

- **Static slot ceiling is the real gate**, not runtime cost. Any `for` with a literal bound will unroll and blow 512. Enforce `[loop]` + constant-driven bound in review, and check `premake_log.txt`/FXC output for the actual instruction count on every change.
- `ps_3_0` `texldl` inside the loop requires the sampler to have no mips — true for s9 (`noise.cpp:893`) but **not** guaranteed for s10 if the aux texture gets a mip chain; create it with `D3DUSAGE`/no mip levels.
- Reversed-alpha output (`SRC=ONE, DST=SRCALPHA`) is non-standard for our Im2D helpers, which assume straight alpha. Must be set/restored explicitly, exactly like `DrawGodRays` does (`postfx.cpp:1945` comment on paired set/restore logging).
- The sky/fog colour source is `CTimeCycle__m_CurrentColours` (`postfx.cpp:2439-2445`); cloud ambient must come from `skyTop/skyBot`, **not** a hardcoded constant, or clouds will not darken at night.
- `vcCloudRaymarchParams.z` has no compile-time cap in CryEngine. We must add one (c0.x clamp) or a bad INI value becomes a GPU hang.

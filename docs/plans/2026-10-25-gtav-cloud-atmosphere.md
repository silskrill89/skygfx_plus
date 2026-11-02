# GTA V cloud / sky / atmosphere → portable SM3.0 plan

Date: 2026-10-25. Source read-only. No repo source modified.

Trees consulted:
- V renderer: `rage/GTAV Source/src/dev_ng/game/renderer/` (276 files)
- V sky/cloud shaders: `.../game/shader_source/Sky/` (`sky_system.fx`, `Clouds/Clouds.fxh` + 8 `.fx`)
- V cloud C++: `.../game/vfx/clouds/` (`Clouds.cpp`, `CloudHat.cpp`, `CloudHatSettings.h`, `CloudSettingsMap.*`, `CloudListController.h`)
- V sky C++: `.../game/vfx/sky/` (`Sky.cpp`, `SkySettings.cpp/.h`)
- V fog: `.../game/shader_source/common.fxh`, `.../game/vfx/misc/FogVolume.*`
- V TOD: `.../game/timecycle/TimeCycle.h`, `TimeCycle.cpp`
- SA: `gtasa_src-main/Main.cpp`

Headline finding: **V has no volumetric cloud raymarcher and no atmospheric-scattering LUT.** The clouds are (a) a 2D noise field composited into the skydome and (b) textured, additively-blended dome meshes ("cloud hats"). The sky is a 6-colour analytic hemisphere blend. Both fit `ps_3_0` with room to spare; the binding constraint is *fill rate and interpolator count*, not instruction count.

---

## 1. Frame order

### 1.1 V (deferred, `CRenderPhaseDrawScene::BuildDrawList`)

| # | Step | Cite |
|---|------|------|
| 1 | Lock scene RTs, `CTimeCycle::SetShaderParams`, HDR mode | `RenderPhases/RenderPhaseStd.cpp:1678`, `1689` |
| 2 | Opaque drawlist → GBuffer → deferred lighting | built `RenderPhaseStd.cpp:1650-1663`, executed by `DRAWLISTMGR` (`DRAW_LIST_MGR_SPLIT_DRAWSCENE`) |
| 3 | `CApplyDamage::DrawDamage` | `RenderPhaseStd.cpp:1729` |
| 4 | `CVisualEffects::RenderBeforeWater` (PTFX) | `RenderPhaseStd.cpp:1789` |
| 5 | If underwater: sky first | `RenderPhaseStd.cpp:1818-1822` |
| 6 | `Water::ProcessWaterPass` | `RenderPhaseStd.cpp:1830` |
| 7 | `Static_PostFxDepthPass` (depth-prepass FX) | `RenderPhaseStd.cpp:1844` |
| 8 | `CHorizonObjects::Render` — "render this **first** in order to cover sky dome pixels" | `RenderPhaseStd.cpp:1852-1856` |
| 9 | **`CVisualEffects::RenderSky`** = skydome → sky plane → moon → sun → **clouds** | `RenderPhaseStd.cpp:1858`, chain in `vfx/VisualEffects.cpp:1088-1116` |
| 10 | Lens-flare occlusion queries | `RenderPhaseStd.cpp:1859` |
| 11 | Alpha drawlist | `RenderPhaseStd.cpp:1873` |

Sky is a *deferred-pass filler*: it runs after opaque+lighting, before alpha, with stencil `DEFERRED_MATERIAL_CLEAR` (`Sky.cpp:560`) so it only fills unlit pixels. Clouds run inside the sky step with `ZWrite=false`, `ZFunc=FixedupLESSEQUAL`, stencil-test-only (`Clouds.fxh:823-832`).

### 1.2 SA (`gtasa_src-main/Main.cpp`)

| # | Step | Line |
|---|------|------|
| 1 | `DoRWStuffStartOfFrame_Horizon` → `RwCameraClear(..., rwCAMERACLEARZ)` — **colour is NOT cleared** | `Main.cpp:1148-1152` |
| 2 | `CClouds::RenderSkyPolys()` — sky gradient polys become the background | `Main.cpp:1163` |
| 3 | Frame calls that helper (flash-white / timecycle sky top) | `Main.cpp:2947`, `2951` |
| 4 | `RenderScene()` | `Main.cpp:2973`, body `3194-3439` |
| 5 | ztest/zwrite **off**, alpha off — background setup | `Main.cpp:3222-3225` |
| 6 | `CMovingThings::Render_BeforeClouds()`; **`CClouds::Render()`** | `Main.cpp:3229-3232` |
| 7 | Restore ztest/zwrite/gouraud | `Main.cpp:3237-3239` |
| 8 | `SeamRemoverBegin`; `RenderRoads`; `Coronas::RenderReflections`; `RenderEverythingBarRoads`; `SeamRemoverEnd` | `Main.cpp:3258-3279` |
| 9 | Breaks, underwater fading entities, water, fading entities | `Main.cpp:3288-3312` |
| 10 | Static + stored shadows | `Main.cpp:3324-3349` |
| 11 | `CPlantMgr::Render` | `Main.cpp:3377` |
| 12 | `CClouds::RenderBottomFromHeight()`; rain streaks; sun reflection | `Main.cpp:3389-3392` |
| 13 | PS2 only: `RenderWaterFog`; **`MovingFogRender`**; **`VolumetricCloudsRender`** | `Main.cpp:3418-3428` |
| 14 | PC/Xbox: same two cloud calls live in `RenderEffects()` | `Main.cpp:3632`, `3637` |

SA order is *inverted* vs V: **sky polys at camera-clear time, clouds immediately after, then all geometry on top** — clouds rely on being drawn first with depth test off. V draws them after geometry with a stencil test.

**Consequence for us:** `CClouds::Render` is already intercepted at `0x53DCA2` / `0x53DFA0` with `CClouds__Render_orig` kept (`src/core/main.cpp:7151-7152`). That is the natural replacement seam — it already has the correct semantics (background, depth-off, before geometry). `disableClouds` proves the internal layer draws are individually patchable (`main.cpp:7145`, `0x714145→0x71422A`).

---

## 2. V cloud system

### 2.1 Two independent systems

| System | Geometry | Where |
|---|---|---|
| **Sky-plane clouds** | none — 2D field composited into the skydome pixel | `shader_source/Sky/sky_system.fx:587-633`, `817-940` |
| **Cloud hats** | streamed textured dome meshes, additive | `vfx/clouds/CloudHat.cpp:331-543` |

Both are driven by the same weather/TOD keyframe blocks but are otherwise independent. The hats are the "close" clouds; the sky-plane field is the far deck + horizon.

### 2.2 Generator vs renderer split

The "generator" is a **512×512 RGBA8 render target regenerated every frame**, not a mesh builder:

- `CSky::GeneratePerlin()` locks `PERLIN_NOISE_MAP` and draws a fullscreen quad — `vfx/sky/Sky.cpp:816-853`. Called unconditionally from `CSky::PreRender()` every frame (`Sky.cpp:122-134`).
- RT is 512×512, `A8R8G8B8`, `MipLevels=1` — `vfx/sky/SkySettings.cpp:178-196`.
- Pixel shader `ps_perlin_main` — `sky_system.fx:1452-1463`. Output:
  - `.r` = `smoothstep(threshold-softness, threshold+softness, fbm)` — **coverage body**
  - `.g` = same with `threshold + noiseDensityOffset` — **density (shifted threshold)**
  - `.b` = raw `fbm` in [0,1] — detail/shadow term
- `fbm` = 3-octave hand-written loop over `basePerlinNoise3Channel` with `p *= noiseFrequency`, `amp *= 0.5` — `sky_system.fx:709-730`; base texture bound at `SkySettings.cpp:198`; `noiseScale/frequency/phase` from `SetPerlinParameters()` (`SkySettings.cpp:329-345`).
- Extra high-frequency noise = `NOISE16_p.dds` (`SkySettings.cpp:201`), sampled at 2 scales as `-0.5` signed (`sky_system.fx:695-698`).

The renderer side has **two consumers of the same field**:
1. `ps_sky_main` → cloud colour + alpha in the sky (`sky_system.fx:817-940`).
2. `CSky::GetCloudShaderConstants()` → 6 `float4` fed to ground lighting for **cloud shadows** (`Sky.cpp:890-905`; shader side `shader_source/Lighting/forward_lighting.fxh:293-306`).

### 2.3 Cloud hats — layer definitions

- Weather type → `m_CloudSettingsName` → `CloudSettingsMap` (`commoncrc:/data/clouds`, `Clouds.cpp:55`) → `CloudHatSettings` with 9 `ptxKeyframe` tracks (`CloudHatSettings.h:89-99`).
- Per-weather cloud-hat selection is probability + bitset: `CloudListController` (`CloudListController.h:25-46`), entry point `SelectNextSkyhat()` (`Clouds.cpp:94-111`).
- TOD keyframes: `common:/data/CloudKeyframes` (`Clouds.cpp:56`), times force-synced to timecycle (`SYNC_TO_TIMECYCLE_TIMES`, `CloudHatSettings.h:17`).
- `CloudFrame` payload = 10 colours/scalars + `m_SunDirection` (`CloudHatSettings.h:21-43`). Light colour is overridden from timecycle unless `g_UseKeyFrameCloudLight` (`Clouds.cpp:196-215`), incl. `moonPhaseAdjust`.
- Per hat (`CloudHatFragContainer`): position/rotation/scale, `atArray<CloudHatFragLayer>`, `mTransitionAlphaRange`, `mTransitionMidPoint`, angular velocity, **3** anim blend weights, **3** UV velocities, **3** anim modes (`MODE_COMBINE` / `MODE_SCULPT`) (`CloudHat.h:226`, `289-320`).
- Per layer (`CloudHatFragLayer`): filename, `mCostFactor`, `mRotationScale`, `mCamPositionScalerAdjust`, in/out transition %+delay, `mHeightTigger`/`mHeightFadeRange`, `mHeightTrigger2`/`mHeightFadeRange2` (`CloudHat.h:177-211`). Layer skipped by altitude window at draw time (`CloudHat.cpp:424-428`).
- Draw: camera-relative (camera matrix zeroed, cloud placed at `CLOUDHATMGR->GetCameraPos()`, `Clouds.cpp:442-448`, `CloudHat.cpp:516-523`), far clip pushed to 10000, `ModelNeverCullCallBack` (`CloudHat.cpp:409-415`).

### 2.4 Density / coverage authoring (this is the whole model)

`ComputeCloudDensityAndNormal` — `Clouds.fxh:665-706`:

```
d_i      = 1 - tex(DensitySampler_i).g        // texture G = openness, not density
anim_i   = 1 - d_i*d_i                        // per layer
combine  = max(anim_i * w_i)                  // MODE_COMBINE
sculpt   = sum(anim_i * s_i)                  // MODE_SCULPT
density  = adj.x*combine - (1 - adj.x*combine) * (adj.y + sculpt)
```
then per-pixel `density = saturate((density - shift) * scale)` (`Clouds.fxh:1108`), and `alpha = density * vertexAlpha` (`1109`). `shift`/`scale` are the runtime coverage knobs (`CloudHat.cpp:436-449`), also modulated by layer transition alpha. Per-vertex CPV packs `r = haze, g = density-combine scale, b = sculpt adjust, a = alpha` (`Clouds.fxh:752`). Normal = `normalize(Σ normal_i * combine_i)`, tangent-space → world by VS basis (`Clouds.fxh:675-697`, `1116-1117`).

Coverage in the *sky-plane* system is different and purely a threshold on noise (`sky_system.fx:592-613`):
```
cloudBase = offsetPerlin.x + detailOverall * overallColorStrength * overallDetailStrength
cloudBase *= cloudBaseStrength * perlin.x
detailAmt  = (detailOverall, 1-perlin.y) * (overallDetailStrength, smallCloudDetailStrength)
           + detailEdge * (edgeStrength, smallStrength) * saturate(1 - 2*cloudBase) * perlin.y
cloud      = (perlin.x, 0) + detailAmt
density2   = saturate((cloudDensityMult, smallCloudDensityMult) * cloud - (bias, smallBias))
amount     = density2 * density2 * saturate(viewDir.z*5 - cloudFadeOut)
```

### 2.5 Lighting model

Per-pixel, `PSCloudCommon` — `Clouds.fxh:1097-1177`:

1. **Hemispheric fill** `HemisphericLightingParams` (`Clouds.fxh:476-498`), fed `Normal.xzy`:
   `west + saturate(N.x*0.5+0.5)*(east-west) + bounce*saturate(-N.y/1.25 + 0.2) + sky*saturate(N.y/1.75 + 0.4286)`
2. **Wrap directional** `saturate(dot(N,L)*wrapMult + wrap*wrapMult) * sunColor * scaleDiffuseFillAmbient.x` (`Clouds.fxh:510-521`).
3. **HG/Mie phase**, g = `m_ScatteringConst` clamped to ±0.999999 (`CloudHat.cpp:355-359`):
   `phase = (3(1-g²))/(2(2+g²)) * (1+cos²θ)/(1+g²-2g·cosθ)^1.5` (`Clouds.fxh:536-546`)
   `Scattering = phase * sunColor * scatteringScale * thickness`
4. **Piercing light** (silver lining) `ComputePiercingLight` (`Clouds.fxh:551-582`):
   `A = pow(saturate(dot(view,L)), piercingPower)`; `B = saturate(dot(N, normalize(-v - dot(-v,L)L))) * normalStrength`;
   `lerp(B*A, A, A) * thickness * sunColor * strength`.
5. **Thickness** = `1-density`, remapped by `piercingThickness` (`Clouds.fxh:1136-1137`).
6. **Ambient** `ambientColor * scaleDiffuseFillAmbient.z` (`Clouds.fxh:1158`).
7. **Dynamic point lights**: SM3 path uses *one* light (`gLight0*`, `Clouds.fxh:460-470`); 4-light `FastLight4` is `__SHADERMODEL >= 40` only (`Clouds.fxh:400-418`).
8. **Soft-particle vs depth** (optional): depth fetch + `Q = far/(far-near)` inverse, alpha *= `saturate(depthDiff / softParticleRange)` (`Clouds.fxh:587-653`, `614-653`).

Sun/moon glow through clouds is a separate *sky-plane* term: `calcEffectsDataSunMoon` (`sky_system.fx:667-683`), `dimming⁴` falloff with `sunInfluenceRadius`.

### 2.6 Temporal reprojection

**None.** There is no cloud reprojection, no TAA on the cloud pass, no history buffer. What exists is CPU-side **double buffering** so the render thread never reads a half-updated frame:
- `m_CurrentFrame[2]` + `m_UpdateBufferIndex` toggled in `Synchronise()` — `Clouds.cpp:40-41`, `425-432`; read as `m_CurrentFrame[1^idx]` at `Clouds.cpp:469`.
- Per-layer `mRenderActive[2]` / `mRenderDrawableSlot[2]` / `mRenderTransitionAlpha[2]` — `CloudHat.h:206-209`.
- UV animation is *accumulated* on the update thread (`mAnimUVAccumBuffered[2][3]`, `CloudHat.h:319-320`).

The closest thing to "reprojection" is the opposite: `USE_CAMERA_UV_SCROLL` cancels camera motion so the altitude deck stays world-anchored (`Clouds.fxh:959-967`; scroll built at `CloudHat.cpp:395-396`, `504-511`).

---

## 3. V sky / atmosphere

**Model: analytic 6-colour hemisphere blend + HG sun phase. No Rayleigh/Mie integration, no LUT, no transmittance table.**

`calcAtmosphereColor` — `sky_system.fx:556-583`, evaluated **per-vertex** in `vs_sky_main` (`sky_system.fx:775-777`):

```
azimuthBlend   = sqrt(-viewDir.x*0.5 + 0.5)                        // east↔west
azimuthColor   = lerp(east, transition, t) / lerp(transition, west, t')  at azimuthTransitionPosition
zenithBlend    = abs(viewDir.z)
zenithTransCol = lerp(azimuthColor, zenithTransitionColor,
                      lerp(zenithTransEastBlend, zenithTransWestBlend, azimuthBlend))
skyColor       = 2-segment lerp(azimuthColor → zenithTransCol → zenithColor)
                 segmented at zenithTransitionPosition, remapped by oneMinusZenithBlendStart
```
× `hdrIntensity` (default **7.0**, `SkySettings.cpp:79`).

- **Sun phase only**: `calcSunData` (`sky_system.fx:537-551`) — `(1+cos²θ)/(miePhaseSqrPlusOne - miePhaseTimesTwo·cosθ)^1.5 * mieConstantTimesScatter`; `invAlpha = saturate(1-phase)` is used to **absorb** (darken) the sky toward the sun (`sky_system.fx:876`). Sun colour is squared in C++ and scaled by `sunHdrIntensity` (`SkySettings.cpp:260-261`).
- **Dome**: `platform:/models/skydome` (`Sky.cpp:71`, `280`), scaled to 20000 (`DOME_SCALE`, `Sky.cpp:513`), camera Z is forced to `horizonLevel` (= water level) so the sun does not bob with altitude (`Sky.cpp:508-512`, `sky_system.fx:770`). Far clip temporarily 25000 (`Sky.cpp:465-468`).
- **Sky plane**: a 15000-radius triangle fan at `z = -skyPlaneOffset` (`Sky.cpp:67`, `700-741`), flat colour `skyPlaneColor.rgb² * alpha` (`sky_system.fx:1568`) fogged by `CalcFogData` with a camera-height fade `saturate((camZ - fadeStart)/diff)` (`sky_system.fx:1565-1566`, `1582-1587`). This is the horizon haze band that hides the dome/ground seam.
- **Sun / moon discs**: geometry fans, not shader sprites (`Sky.cpp:764`, `778`); moon is a fake sphere lit by `lunarCycle` (`sky_system.fx:1187-1206`).
- **Stars**: `starfield` texture, UV ×12, suppressed by cloud dimming and moon disc (`sky_system.fx:916-922`).
- **Technique permutation**: pass key is a bitmask `PF_CLOUDS|PF_SUN|PF_MOON|PF_STARS`, one technique per combination — 16 baked permutations (`sky_system.fx:944-961`, key built `Sky.cpp:530-537`).

### Time-of-day driving parameters

Everything is a **TimeCycle keyframe var**, ~50 of them: `TCVAR_SKY_AZIMUTH_EAST/WEST/TRANSITION_COL(_INTEN)`, `..._POSITION`, `TCVAR_SKY_ZENITH_COL(_INTEN)`, `TCVAR_SKY_ZENITH_TRANSITION_COL(_INTEN)`, `..._POSITION`, `..._EAST_BLEND`, `..._WEST_BLEND`, `TCVAR_SKY_ZENITH_BLEND_START`, `TCVAR_SKY_HDR`, `TCVAR_SKY_PLANE_COL/INTEN`, `TCVAR_SKY_SUN_*`, `TCVAR_SKY_MOON_*`, `TCVAR_SKY_STARS_ITEN`, and the whole cloud block `TCVAR_SKY_CLOUD_GEN_*` … `TCVAR_CLOUD_SHADOW_OPACITY` (`TimeCycle.h:340-450`).

Mapped 1:1 into `CSkySettings` by `CTimeCycle::GetSkySettings` (`TimeCycle.cpp:4381-4470`); pushed with `SendToShaderLinearMult` × intensity (`SkySettings.cpp:255-325`). Sun/moon direction comes from `GetSunSpriteDirection()` / `GetMoonSpriteDirection()` (`TimeCycle.h:897-898`), consumed by clouds at `Clouds.cpp:225-229`.

---

## 4. V fog

Single analytic function, no raymarch: `__CalcFogData(eyeRayToPoint, hazeScale)` — `common.fxh:2908-2943`.

1. **Height fog** (Crytek SIGGRAPH 2006 analytic integral) `common.fxh:2869-2885`:
   `dist = max(0, |ray| - fogNearDist)`; `deltaZ = ray.z * dist/|ray|`; `t = falloff*deltaZ`;
   `fogInt = |deltaZ|>0.01 ? (1-exp(-t))/t : 1`; `res = 1 - saturate(exp(min(1, densityAtViewer*dist*fogInt)))`.
   Note `densityAtViewer` is pre-multiplied by `-density` in C++ (`common.fxh:2881` comment).
2. **Distance haze** `common.fxh:2924-2927`:
   `hazeBlend = hazeScale*hazeAlpha*(1-groundFog)`; `hazeAmt = hazeBlend*(1-exp(hazeDensity*max(0, dist-hazeStart)))`.
   `hazeScale = 0` for sky/clouds (`CalcFogDataNoHaze`, `common.fxh:2963-2966`) so the sky never eats its own haze, except clouds inject it manually (below).
3. **Colour** = 4-way lerp: `ground → lerp(atmosphere, moonColor, moonAmount) → sunColor`, mixed by `atmosphereBlend = 1-exp(-horizonTintScale*dist)`, then toward `fogColorHaze` by `hazeBlend` (`common.fxh:2936-2942`). Sun/moon amounts are `pow(saturate(dot(ray, spriteDir)), lightingCalcPower)` (`common.fxh:2919-2920`).
4. **Cloud haze injection**: `CalcCloudFogData` (`Clouds.fxh:838-856`) folds the CPV red channel into fog alpha and pre-multiplies the fog blend into `(rgb, 1-a)` so clouds can be faded with 2 mads.
5. **Registers**: `globalFogParams[5]` = `c44..c48` (`common.fxh:1013`), decode macros at `1057-1081`.
6. **Fog ray**: optional per-pixel modulation from a `FogRay` texture (`common.fxh:2946-2955`) — the god-ray-ish term.
7. **Volumetric contributions**: *geometry*, not integral — `CFogVolumeMgr` meshes (`vfx/misc/FogVolume.h:145-196`, shader `shader_source/VFX/vfx_fogvolume.fx`), deferred-lit, plus `LINEAR_PIECEWISE_FOG` alternative (`common.fxh:2685-2866`).

---

## 5. What is portable to ps_3_0

`ps_3_0` D3D9 ceilings: **512 instruction slots**, 16 samplers (`s0..s15`), 224 `float4` constants (`c0..c223`), 32 temps, **10 interpolators (`v0..v9`)**, no compute, no UAV, loops OK, `tex2Dlod`/`ddx` OK.

Estimates below are analytic (op counts from source); no `.fxc` ships in the V tree, so treat as ±20%.

| Technique | Fetches | ALU (est.) | Portability |
|---|---|---|---|
| `calcAtmosphereColor` only (6-colour sky) | 0 | ~40 | Trivial. Vertex-shader cost in V; move to PS or keep in VS. |
| Full V `ps_sky_main` (gradient+clouds+sun+stars) | 7 | ~85 | Fits. 3 of the fetches are dither/stars/detail. |
| `ps_perlin_main` (coverage generator) | 3 | ~30 | Fits, but it is a *pass*, not a PS feature — see §6. |
| Cloud hat, static (`PSClouds_Cheap`) | 2 | ~60 | Fits. |
| Cloud hat, anim+soft+vertScatter+Pierce (`PSCloudsVertScatterPiercing_AnimSoft`) | 7 (3 density + 3 normal + 1 depth) | ~170–220 | Fits in 512 but **hits the 10-interpolator cap exactly** (V uses `TEXCOORD0..9`, `Clouds.fxh:771-785`). |
| + dynamic point light (SM3 = Light0 only) | +0 | +30 | Fits. |
| 4-light `FastLight4` | +0 | +100+ | **Not portable** — SM3 branch already collapses to one light. |
| `__CalcFogData` | 1 (FogRay, optional) | ~55 | Trivial. |
| `ComputeGlobalVolumetricFogValue_Crytek` | 0 | ~18 (incl. 2 `exp`) | Trivial. |

**LUT feasibility.** SM3.0 has no compute, so a LUT must be baked on the CPU or in one RT pass and sampled as a 2D texture. That is entirely viable (a 64×32 RGB `A8R8G8B8` = 8 KB, one upload per timecycle keyframe, `s`+1, 1 fetch, `tex2D` bilinear). **But V does not need one** — §3 is 6 colours. A LUT is only warranted if we replace V's model with a physical Rayleigh/Mie integral; that would be a *deviation*, not a port.

**Register/sampler cost of the whole thing:**

| Item | Registers |
|---|---|
| Sky gradient | `c0..c5` (5× vec4 colour+scalars, packed) |
| Sky clouds | `c6..c9` cloud1/2/detail/small |
| Sun | `c10..c12` |
| Fog | reuse `c44..c48` (already V's layout) |
| Samplers | sky: `s0` density field, `s1` detail, `s2` starfield, `s3` dither (4). Clouds: `s0/s1` density+normal ×3 layers = 6, or 2 with a merged tile. Fog: `s0/s1`. |

Our existing consumers already hold `s9` (`Noise.hlsl:42`) and PBR holds `s0..s4`; sky/cloud passes are their own draw so there is no conflict.

**Real constraint is fill rate.** The cloud hat shader is ~200 ALU over large screen coverage at 1080p with 3 density fetches plus 3 normal fetches. That is why V composes it additively over the dome instead of raymarching. Any raymarch at even 32 steps × 4 fetches = 128 fetches/pixel is ~2 orders of magnitude over budget on period hardware and will not hold 30 fps on SM3-class GPUs.

---

## 6. Concrete plan — impact per unit of work

Ordered. Each step is independently shippable.

### P0 — Replace the hardcoded sky gradient with V's 6-colour model  ★ highest impact / lowest work
Our `main_cloudsSky` currently lerps `float3(0.58,0.56,0.52) → float3(0.10,0.20,0.42)` (`shaders/ps/CloudsPS.hlsl:185-186`) — zero time-of-day. We already have **unused** config fields for exactly V's parameter set: `skyZenith*`, `skyZenithTrans*`, `skyAzimuthEast*`, `skyAzimuthTrans*`, `skyAzimuthWest*`, `skyPlane*` (`src/skygfx.h:473-479`) — and `src/core/debugmenu_ui.cpp:1697-1706` already records them as "INTENTIONALLY HIDDEN … Grep finds no reader outside skygfx.h; readIniFile has no keys for them".
- Port `calcAtmosphereColor` verbatim (~30 lines) — `sky_system.fx:556-583`.
- Drive from timecycle, not new keyframes: read the game's existing sky colours (SA's `CTimeCycle` already has top/bottom sky, sun, fog colours) and map into the 6 slots.
- **Work: 1 shader + ~10 INI fields + 1 upload block. Impact: the single biggest "this is not SA anymore" read.**

### P1 — Sky-plane cloud field on the dome  ★ high impact / low-medium work
Port `calcCloudData` (`sky_system.fx:587-633`) — 2 taps of the same field, threshold → alpha², with the small-cloud second channel and the `cloudFadeOut` horizon fade.
- Use our tile: body = `NoiseFbm3` (R), erosion = `NoiseWorley` (G) — `Noise.hlsl:67-88`.
- Keep V's `sphericalWarpOffset` sun-directional warp (`sky_system.fx:526-530`, `786-789`) — 4 ALU, and it is what makes clouds trail away from the sun.
- Feed `dimming` into the sun/moon glow (P5) and into cloud shadows (P2).
- **Work: 1 shader, ~60 lines, 2 fetches. Impact: the sky stops being a clean gradient.**

### P2 — Cloud shadows on the ground  ★ high impact / ~20 lines
V: `CSky::GetCloudShaderConstants` → `USE_FORWARD_CLOUD_SHADOWS` (`Sky.cpp:890-905`, `forward_lighting.fxh:293-306`). For us: sample the same coverage field by world XZ + time scroll, multiply the directional term in `pipeUploadPBR()` / the sun colour. Do **not** add a new sampler — reuse `s9` and the c49..c52 knob block.
- **Work: ~20 lines in `pipelinecommon.cpp` + a 2-line hook in the PBR shaders. Impact: ground stops looking like a uniform-lit heightmap.**

### P3 — Proper cloud deck (replaces SA sprite clouds)  ★ high impact / high work
Add 2–3 altitude shells projected from the view ray, each: `NoiseCloudDensity` → `saturate((d-shift)*scale)` → HG phase + wrap + hemispheric fill (port `Clouds.fxh:1097-1177` minus soft-particle and minus dynamic lights).
- Draw at `CClouds__Render_orig`'s call site so depth semantics are inherited (`main.cpp:7151-7152`).
- 2–3 fetches per layer (merged density, no normal map; add a 2-tap finite-difference normal only if it pays).
- **Work: ~200 lines, 2–3 draws. Impact: the thing people actually notice — but only after P0/P1, or it will sit on a wrong sky.**

### P4 — Fog upgrade  ★ medium impact / low work
Add to `shaders/ps/HeightFog.hlsl` (70 lines, already Crytek height fog at `HeightFog.hlsl:1-40`):
- the **haze** term (`common.fxh:2924-2927`) with `hazeStart`,
- the **4-way directional tint** (`common.fxh:2936-2942`) using the sun direction we already have.
- **Work: ~25 lines. Impact: kills the flat fog wall; makes sunsets directional.**

### P5 — Sun/Mie glow in-sky  ★ high impact at dawn/dusk / ~15 lines
Port `calcSunData` (`sky_system.fx:537-551`) + the `invAlpha` absorb (`876`) + `calcEffectsDataSunMoon` (`667-683`). This is the entire reason V sunsets read as sunsets.
- **Work: ~15 lines in the sky shader. Impact: disproportionate at low sun angles.**

### Do NOT attempt

| Rejected | Why |
|---|---|
| **Raymarched volumetric clouds with self-shadowing** | V does not have them (§2.1). 32 steps × 3–4 fetches = 96–128 fetches/pixel *before* any shading — 20–50% of the `ps_3_0` 512-slot budget consumed by fetch alone, every one a coherent-but-divergent cache miss at 1080p. The limit is bandwidth, not instruction count. |
| **Per-frame 512² Perlin RT regeneration** | V's generator is a whole extra render pass + a permanent 512² RGBA8 RT (`SkySettings.cpp:178-196`, `Sky.cpp:816-853`). We have a pre-baked 4-channel tile at `s9` that covers it for free; and allocating into the game's RT pool risks the front-buffer aliasing already documented for `pRasterFrontBuffer`. |
| **Physical Rayleigh/Mie LUT (Preetham/Hosek)** | Compute-free baking is possible (§5) but V's model is 6 colours. It would be *more* work than P0 and would not look like V — V's look is art-directed, not physical. |
| **4 dynamic cloud light sources** | SM3 branch of `Clouds.fxh` is single-light already (`Clouds.fxh:460-470`). |
| **Temporal reprojection / TAA on clouds** | V has none (§2.6). We have no velocity buffer for sky pixels; history would ghost on every camera cut. |
| **Cloud-hat streaming with probability-weighted transitions** | `CloudListController` (`CloudListController.h:25-46`) + per-layer transition cost accounting (`CloudHat.cpp:911-1092`) is an asset-pipeline problem, not a shader problem. One deck + P1 covers the read. |
| **`USE_CAMERA_UV_SCROLL` parallax anchoring** | Only pays off for a low-altitude deck you fly through. With a high deck it is invisible. |

---

## 7. Noise requirements vs our universal tile

Our tile (`shaders/include/Noise.hlsl:5-9`, `s9`, WRAP/LINEAR/**MIPFILTER NONE**): `.r` Perlin fBm, `.g` Worley F1, `.b` white, `.a` blue.

| V requirement | Cite | Covered? |
|---|---|---|
| Macro cloud body — fBm | `sky_system.fx:721-727` (3-octave loop) | **Yes** — `.r`, and `NoiseFbm3` already synthesises a deeper stack from 3 taps at 1×/3.17×/9.41× (`Noise.hlsl:67-74`). |
| Second "small cloud" coverage channel | `sky_system.fx:600-606` (uses `perlin.y` of the same field) | **Yes** — offset tap of `.r`. V uses a second channel of the same texture; an offset tap is equivalent and cheaper than a second sampler. |
| Erosion / cauliflower edge | `sky_system.fx:601`, `695-698` (`NOISE16_p.dds` signed `-0.5`) | **Yes, and better** — `.g` Worley F1 gives cellular cauliflower; V's is isotropic Perlin and needs the `saturate(1-2*cloudBase)` masking trick to fake structure. |
| Domain warp away from sun | `sky_system.fx:786-789`, `526-530` | **Yes** — geometric warp, no new channel. |
| Per-pixel dither (banding) | `sky_system.fx:935`, `kDitherScale=1e-4` (`sky_system.fx:43-45`) | **Yes** — `.a` blue, rank-permuted, already used by `NoiseDither` (`Noise.hlsl:100-105`). |
| Shadow/second-offset field | `sky_system.fx:592`, `625` (`offsetPerlin.xy`) | **Yes** — second tap of `.r` at an offset; same cost as V's. |
| **Gradient / normal of the density field** | `Clouds.fxh:658-662`, `675-677` (V ships a tangent-space normal map per density layer, and its own comment says it is *not* DXT5 yet ("we really should be using dxt5 normalmaps for clouds", `Clouds.fxh:656-657`)) | **No.** All four channels are committed. Options: (a) finite-difference `NoisePerlin` at ±ε → 2 extra taps, ~8 ALU; (b) derive the normal from the Worley cell id analytically; (c) drop normal-mapped shading and use V's *hemispheric* term with a geometric normal only — **this is what P1/P3 recommend**, because V's `HemisphericLightingParams` does most of the visible work (`Clouds.fxh:476-498`) and the normal map mostly adds mid-frequency sparkle. |
| **Mip / minification** | V: `MIPFILTER = LINEAR` (`sky_system.fx:219-228`) on a `MipLevels=1` RT (`SkySettings.cpp:181`) | **Gap.** Our tile is `MIPFILTER NONE` (`Noise.hlsl:5`). At the horizon a projected plane compresses hard and will alias into shimmer. V's own mitigation is the `cloudFadeOut` term (`sky_system.fx:611`) and the dither. **Recommendation: add 2–3 mips to the tile, or fade cloud density to its mean with distance — do not rely on dither.** |

**Verdict:** the 4-channel tile covers **every** noise shape V's cloud path actually uses (body, secondary coverage, edge detail, warp, dither, shadow offset) with *fewer* fetches than V (V spends 7 in `ps_sky_main`; a port needs 3–4). The only genuine gap is **normal-map data**, and the recommended plan (P1/P3) does not need it — hemispheric + wrap + HG phase reproduces V's cloud shading without a normal map. If a later pass wants normal-mapped clouds, finite-difference the density (2 taps) rather than repacking the tile.

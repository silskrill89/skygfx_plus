# Water Overhaul — SM3.0 Port Plan (2026-10-30)

Research lane doc. READ-ONLY survey of reference sources + current system; drives the
post-deadline water overhaul. Directive m0068: "if we're missing any parts of the water use
[B]ullet for the physics end… reference GTA V and RAGE and all the sources… including the
CryEngine source code… wire the shaders we have into a proper water overhaul."
Nothing here is implemented — this is the plan the implementation lanes consume.

Sources read for this doc (all local, verified):
- RAGE/GTAV: `E:\dev(dave)\releases\GTASource\rage\GTAV Source\src\dev_ng\game\shader_source\Water\`
  (water.fx, water_common.fxh, waterTex.fx, water_foam.fx, water_terrainfoam.fx, water_shallow.fx,
  water_river*.fx) + `game/renderer/{Water.cpp,newWater.cpp,RenderPhases/RenderPhaseWater*}` +
  `game/physics/{WaterTestHelper.h,Floater.cpp}` + `game/scene/world/GameWorldWaterHeight.h`
- CryEngine: `E:\dev(dave)\releases\GTASource\CRYENGINE-release\Engine\Shaders\HWScripts\CryFX\`
  (Water.cfx, WaterVolume.cfx, WaterCausticsPass.cfi, WaterReflectionsPass.cfi, WaterFogVolume.cfx,
  WaterOceanBottom.cfx) + `Engine\Shaders\{Water,WaterVolume}.ext`
- GTA IV: `E:\SDKs\LibertyRecomp-main\LibertyRecompLib\shader_overrides\water\water_ps1_smooth_shorelines.hlsl`
  (FusionShaders SmoothShorelines port) + `E:\SDKs\GTAIV.EFLC.FusionShaders\win32_30_nv8\water\*.asm`
  (constants already distilled in `docs/plans/water-refs.md`)
- aap skygfx legacy: `E:\dev(dave)\releases\skygfx-master\src\{main.cpp,postfx.cpp}`
- Current system: `shaders/vs/Water_VS.hlsl`, `shaders/ps/Water_Parallax.hlsl`,
  `shaders/ps/Water_Underwater.hlsl`, `src/render/waterPipe.cpp`, `src/entities/softbody.cpp`,
  `src/extras/ragdoll_death.cpp`, `docs/baseline/2026-10-30-test2-0438/findings.md`

Prior recon with full constant sets: `docs/plans/water-refs.md` (3 recon passes, all CONFIRMED).
This doc adds the feature matrix, defect-ranked priorities, per-feature port specs and the
physics coupling contract.

---

## 1. Inventory — what we HAVE vs what the references do

### 1a. Current system (shipped)

| Component | Detail |
|---|---|
| `Water_VS.hlsl` | Flat plane + **gated 2-wave vertical swell** (c11 = swellAmp/timeScale; 45 m/0.10 m + 23 m/0.06 m, vertical-only, position-pure). Body-class channel TEXCOORD5 (bodyType 0-5: OCEAN/SHALLOW/INTERIOR/RIVER/POND/LAKE + edgeBlend + signed aspect) from CPU via NORMAL. VS c0-c11. |
| `Water_Parallax.hlsl` | 3 style entry points (fast_build.py multi_entry): `main_xbox`→Water_Parallax.cso, `main_iv`→Water_IV.cso, `main_v`→Water_V.cso. Shared core: 2-layer scrolling mipmapped normals, fresnel sky reflection (timecycle tint), depth shallow→deep ramp, `WaterShoreCoverage` exp2 shore alpha (LR SmoothShorelines port, k=2 / IV k=3), `WaterJunctionEase` seam ramp, `WaterFoamOctaves` (3 spatial octaves 2x/4x/8x, 0.5/0.25/0.125), `WaterGlitter` 2-lobe sun glitter, `WaterBodyContract` (W7-B per-body scales via c14/c15), G1 screen-space reflection (s3 flipped-scene, IV/V cores, tier≥2), sun pierce (tier 3). Samplers s0-s3 (scene/detail/depth/refl), constants c0-c15. |
| `Water_Underwater.hlsl` | Underwater volume fog + SSS (CryEngine exp2 recipe), full-screen pass, s0/s1 + c0-c7. |
| `waterPipe.cpp` (1914 ln) | Pipe + `waterPipe_directFlushRoute` (RenderIfDoesntFit flushes get full bind). Scene/depth/INTZ/refl RT management, procedural detail-normal texture, W7-B body classification (table from WaterQuads 0xC21C90 / 0xC22854 / 0xC22910, per-vertex into NORMAL), c0-c15 + VS c0-c11 uploads, W7-D logging. INI: waterStyle, waterQuality 0-3 (tier≥2 = RT copies), waterParallaxEnable, waterUseTimecycle, waterShoreFade, waterTileScale, waterTranslucency, waterReflectionStrength, waterFresnelSplit. |
| Physics (Bullet lane, in flight) | `softbody.cpp`: `SoftbodyWaterQuery` (game core 0x6E8580), `SoftbodyWaterSwell`/`SoftbodyWaterSwellVel` = **verbatim c11 swell law** (`WaterSwellMakeWave`). `ragdoll_death.cpp:951` `RagdollWaterForces` (buoyancy = g·(submerged·densityRatio−1), drag/coupling/spring/drift clamps). |

### 1b. Feature matrix

| Feature | Ours | RAGE/GTAV | CryEngine | GTA IV | aap skygfx |
|---|---|---|---|---|---|
| Scrolling detail normals | ✅ 2-layer | ✅ `GetOceanBump` 4-tap + cellMask pow32 (water_common.fxh:586) | ✅ 2 base + detail + rain (Water.cfx:700-715) | ✅ 2-layer 0.002/0.01 | ❌ (vanilla SA) |
| Fresnel reflection | ✅ style curves | ✅ `lerp(dot(-V,N),1,.3)` + anti-chrome N damp 5/6 | ✅ `GetEnvmapFresnel` (shadeLib.cfi:223) | ✅ 0.2+0.6·w⁵ | ❌ |
| Screen-space reflection | ⚠️ G1 flipped scene only | ✅ planar RT + paraboloid + `reflectionPos` proj | ✅ `WaterReflectionsPass.cfi` | ✅ ReflectTexture via viewProj c74 | ❌ |
| Screen-space refraction | ⚠️ plain s0 capture | ✅ **depth-validated 2-tap**: 4-tap depthMax, `cannotRefract` gate, RGB-split sum (water_common.fxh:877-900) | ✅ refract offset + bump | ✅ opacity=depthDiff·a | ❌ |
| Shore alpha ramp | ✅ exp2 coverage | ✅ refractionColor.a=saturate(depth) | ✅ softIsec | ✅ ^0.25+0.125 | ❌ |
| Shore foam band | ✅ WaterFoamOctaves (procedural) | ✅ **texture foam**: FoamMask·(len(flatN.xy)·0.27+0.44) → BlendSampler 1D gradient (water_common.fxh:936-943) | ✅ fFoam texture + softIsec shaping (Water.cfx:1222-1250) | ⚠️ smooth octaves only | ❌ |
| Foam near geometry (terrain) | ❌ | ✅ `water_terrainfoam.fx` `PSFoamDeferred`: foamIntensity=max(height−WaterHeight,0)·HeightOpacity | ✅ soft-intersection foam | ❌ | ❌ |
| Interactive ripples / dynamic wave normals | ❌ | ✅ `waterTex.fx` GPU spring sim (`PS_WaterUpdateVelocity` + `technique waterupdate` passes updatedisturb/updatecalming/**updateshorewave**) | ✅ rain ripples | ❌ | ❌ |
| Wave displacement | ⚠️ c11 swell 0.16 m | ✅ dynamic grid + heightmap | ✅ FFT VTF + projected grid (Water.cfx:598-620) | ❌ flat | ❌ |
| Caustics | ⚠️ blocky/low-res (findings #1/#3) | ✅ `waterTex.fx technique caustics` | ✅ `WaterCausticsPass.cfi` sharpen 5.65c−4.66→c² + RGB dispersion | ❌ | ❌ |
| Sun pierce / SSS | ⚠️ pierce tier 3, no SSS in surface PS | ✅ pierce pow²·sat(depth/10) | ✅ SSS scale 2.0 EdotL²·height (Water.cfx:1202-1210) | ❌ | ❌ |
| Water volume (submerged) | ⚠️ Water_Underwater fog only | ✅ `WaterFogCommon` MRT fog prepass | ✅ `WaterVolume.cfx` + `WaterFogVolume.cfx` | ⚠️ depth fade | ⚠️ `CWeather__UnderWaterness` colourset only (postfx.cpp:1503) |
| Physics buoyancy coupling | ⚠️ c11 law shared, clock divergence | ✅ `CWaterTestHelper::GetWaterHeightAtPositionIncludingRivers(bConsiderWaves)` → `CBuoyancy` (Floater.cpp: buoyancy/lift/keel/flow/drag/surface-stick) | n/a | n/a | ❌ |
| aap legacy baseline | — | — | — | — | colour/alpha only: WaterRGBA alpha 127 vs 255, HD-colour quirk Nop 0x6E716B/0x6E7176 (`×0.65+0.27`), timecyc underwater grade. **No water shader.** |

### 1c. Current defect baseline (findings.md, 22 shots)

- **F2 flat milky water flooding over land** (13/22, 59%): semi-transparent slab over streets, hard
  quad boundaries, **vertical per-quad shading seam** (left grey / right blue), pale untextured
  horizon band (far general-ocean plane), tree canopies behind/below water (draw order).
- Caustics blocky/low-res with hard pixel edges (shots #1/#3).
- Missing foam on FFP/vanilla paths; wake foam particles degraded into white streaks (F4).
- White wedges/faceted polys at swim (known family) — water-adjacent, not water-shaded.

---

## 2. Missing features — ranked by visual impact

| # | Feature | Why | Defect/§ |
|---|---|---|---|
| M1 | **Far-water LOD + over-land flood guard** (correctness) | 59% of shots; flat pale horizon band + water on streets. Nothing to port — bug fix: distance normal/colour fade into the general-ocean plane + clamp the sheet to classified water polys (W7-B already gives per-vertex type/edgeBlend) | F2 |
| M2 | **Shoreline foam texture blending** | Procedural octaves alone read as noise; RAGE/CryEngine blend a foam TEXTURE through a 1D gradient with normal-length modulation — the single biggest "looks like real water" win | §1b |
| M3 | **Foam masks near geometry** (soft intersection) | Foam collects where water meets ANY geometry (piers, seawalls); RAGE `water_terrainfoam.fx` + Cry `softIsec` both key off sceneDepth−surfaceDepth | §1b |
| M4 | **Depth-validated wave refraction** | Our s0 capture is undistorted; RAGE's 4-tap depthMax + cannotRefract + RGB-split double sample kills refraction bleeding onto foreground geometry | §1b |
| M5 | **Caustics upgrade** (forward 2-tap, sharpened, dispersed) | Blocky caustics called out in findings; Cry recipe is cheap and SM3.0-native | F2 note |
| M6 | **Dynamic wave normals / ripple sim** | Interactive rain/boat ripples; RAGE `waterTex.fx` spring sim is RT ping-pong, portable but the biggest new system | §1b |
| M7 | **SSS + sun-pierce polish** | Cry `SubSurfaceScatteringScale=2.0, EdotL²·height` on wave crest — cheap, sells crest lighting | §1b |
| M8 | **Water-volume vs surface split** | INTERIOR body type (pools, z>950) should use a volume-fog shading path (Cry `WaterVolume.cfx`) instead of the ocean sheet | §1b |
| M9 | **FFT-lite swell upgrade** (VTF LUT) | Beyond the c11 2-wave swell; only if M1-M5 land | §1b |

---

## 3. Per-feature port specs

Register headroom (all specs assume it): PS uses c0-c15 + s0-s3 today → **c16-c31 and s4+ free**;
VS uses c0-c11 → c12+ free. ps_3_0/vs_3_0 caps (512 instr, 32 temps, VTF via `tex2Dlod` in VS)
are the hard walls. Every fetch chain keeps ≤4 levels of dependency where possible.

### M1 — Far-water LOD + flood guard (bug fix, no port)
- **Reference**: RAGE fades normals by depth (`depthScale = saturate(5−ScreenDepth/50)` in
  `GetOceanBump`, water_common.fxh:593 — commented for cells but the pattern is the intent);
  IV `1 − dist²·4e-4` normal fade (water-refs.md IV).
- **SM3.0**: free — a saturate/mad in `WaterSetup`.
- **Integration**: `Water_Parallax.hlsl` `WaterSetup` (distance factor already computed) — extend
  the far blend to also flatten detail normals and lerp colour toward the horizon tint so the
  general-ocean plane matches near water; in `waterPipe.cpp` W7-B classify pass, write a
  **flood alpha**: vertices of quads whose poly test says "outside any water poly" get alpha 0
  (kill the over-land slab) — the classification table already exists (`WbClassifyPoint`).
- **Risk**: LOW (shading-only + existing CPU table). Seam fix = ensure the far-plane colour equals
  the near-water colour at the fade distance.

### M2 — Shoreline foam texture blending
- **Reference (port exactly)**: RAGE `water_common.fxh` WaterShadingCommon foam block (~:936):
  `foam = FoamMask·(len(flatNormal.xy)·0.27 + 0.44); foam = tex1D(BlendSampler, foam + FoamIntensity).g
  · refractionColor.a; waterColor += (dirCol·sat(NdotL·.7+.3)·shadow + ambient)·foam` with
  `FOAMSCALE=1.0, FOAMWEIGHT=0.65` (water_common.fxh:24). CryEngine shaping
  (`Water.cfx:1228-1232`): `softIsec = saturate(0.75·(sceneDepth − surfaceW)); fFoam ·=
  saturate(−softIsec·(1+thresh)+fFoam) · saturate(softIsec − softIsec²)`.
- **SM3.0**: needs 1 new sampler (foam texture → **s4**) + 1 gradient LUT (can bake into the
  detail texture alpha channel or a 64×1 1D in s5). Cost ~4 taps. `len(flatN.xy)` is free.
- **Integration**: `Water_Parallax.hlsl` — replace/augment `WaterFoamOctaves` band in the
  `WaterShoreCoverage` callers (all 3 cores, tier≥2 like today's band); reuse `WaterFoamGate`
  day-gating. Texture source: author a foam tiling texture (white noise + cellular) or extract
  from RAGE assets (`maps_other/WaterFoamMap`).
- **Risk**: MED — texture authoring; alpha-channel packing in detailTex must not break the
  existing `.w` height fetch in `WaterFoamOctaves`.

### M3 — Foam masks near geometry (soft intersection)
- **Reference**: RAGE `water_terrainfoam.fx` `PSFoamDeferred` (`foamIntensity =
  max(height − WaterHeight, 0)·HeightOpacity`, animated×static×opacity multiply, :130-151) and
  `water_foam.fx` `PS_Foam` (:168-180). Cry `softIsec` (above). RAGE `waterTex.fx`
  `technique waterupdate`'s `updateshorewave` pass (VS_UpdateWave, :1001-1030) drives the
  shoreline wave band along the coast.
- **SM3.0**: we already copy INTZ depth (s2) → `softIsec = saturate(depthDiff·k)` is one subtract.
  Terrain foam per se needs mesh data we don't have → approximate with the **shore band from
  W7-B edgeBlend** + depth softness (depthDiff near 0 → foam), which is exactly Cry's model.
- **Integration**: `Water_Parallax.hlsl` composite block; feed `WaterShoreCoverage` thickness.
- **Risk**: LOW-MED.

### M4 — Depth-validated wave refraction
- **Reference (port exactly)**: RAGE `water_common.fxh:877-900` + `:735-766`:
  `refractionPos = (WorldPos + float3(−N.xy,0)+V)·refractionDepth·k` through
  `gRefractionWorldViewProj`; 4-tap `depths = tex2D(RefractionDepthSampler, ScreenPos ± 1px)`,
  `depth = max4 − ScreenDepth`; `cannotRefract` gate falls back to `ScreenPos`; double sample
  blended `(1,.5,0)+(0,.5,1)`; final `color = lerp(waterColor, color, refractionColor.a)` where
  `refractionColor.a = saturate(depth)`.
- **SM3.0**: 4 depth taps + 2 colour taps + 1 projected multiply — fine. We lack
  `gRefractionWorldViewProj` (a separate refraction camera matrix) → approximate the offset in
  **screen space**: `screenUV += N.xy·distortK·depthFade` (IV-style), then the same depth
  validation. Add per-style `refractionBlend` (V: RefractionBlend 0.7, Exponent 0.25).
- **Integration**: `Water_Parallax.hlsl` all 3 cores' s0 fetch; clamp with
  `screenSize`/clipPlanes. Must keep the hardware-SRCALPHA tier<2 fallback untouched.
- **Risk**: MED — foreground-geometry bleeding is the classic failure; the cannotRefract gate is
  the mitigation and must be ported, not simplified away.

### M5 — Caustics upgrade
- **Reference**: Cry `WaterCausticsPass.cfi` + `DeferredCaustics.cfx` (water-refs.md §Caustics):
  2 scrolling taps, sharpen `c = saturate(5.65·c − 4.66); c·=c`, colour dispersion
  `refract(vSunTS, normal, 0.9)` RGB offset 0/0.05/0.1, height atten `(1−exp(−2h))·step(0,h)`,
  dist atten `·p.x/(0.075·camDistSq)`, anim PB_time 1/0.5/0.25/0.125. RAGE
  `waterTex.fx technique caustics` is the alternative (bump-derived).
- **SM3.0**: forward 2-tap scroll — the current blocky look is a resolution/filter problem:
  force mipmapped sampler + world-space (not screen-space) UVs + the sharpen curve.
- **Integration**: `Water_Parallax.hlsl` near-water colour term; params into spare c16.
- **Risk**: LOW.

### M6 — Dynamic ripple normals (big system)
- **Reference**: RAGE `waterTex.fx` — `PS_WaterUpdateVelocity` (:1075+: spring sim
  SPRING=40, FALLBACK=0.5, FRICTION=0.7^t, 8-tap neighbourhood average, noise disturb
  `gDisturbAmount`), `PS_WaterUpdateHeight`-style integration, `technique waterupdate` passes
  `updatedisturb/updatecalming/updateshorewave`, `VS_UpdateWave` shore wave along flow dir;
  surface shader reads `HeightMapSampler/VelocityMapSampler` and derives normals by finite
  difference (cf. PS3 4-tap normals in IV VS, water-refs.md IV).
- **SM3.0**: RT ping-pong on A16B16G16R16F (or packed 8-bit) — ps_3_0 can render to float RTs on
  D3D9 but old Intel/ATI parts choke → gate at tier 3 and fall back to scrolling normals.
  Grid 256² world-anchored around the camera (like RAGE `gWorldBase`/DYNAMICGRIDSIZE).
- **Integration**: new `waterSim.cpp` sibling of waterPipe RT management; surface normal fetch
  becomes s5 in `Water_Parallax.hlsl` (uniform-tier gated like everything else).
- **Risk**: HIGH — new RT lifecycle, device-reset handling (`waterPipe_OnDeviceReset` pattern),
  cost. Post-deadline only.

### M7 — SSS + sun pierce polish
- **Reference**: Cry `Water.cfx:1202-1210` (`refract += SSS·(EdotL²)·(fog·Sun)·waveHeight·softIsec`)
  + RAGE pierce (`pierceScale = pow(sat(dot(refract(V,lerp(N,N0,.5),1/1.5),−Sun)),2)·sat(depth/10)`).
- **SM3.0**: free (saturate/pow); SSS tint rides `timeLight1` directional.
- **Integration**: `main_v`/`main_iv` composite; keep `WaterFoamGate` night gating.
- **Risk**: LOW.

### M8 — Water-volume vs surface split
- **Reference**: Cry `WaterVolume.cfx` (volumeDepth = `max(fogDepth − dot(vView, CameraFrontVector), 0)`,
  `waterVolumeFog = exp2(−density·volumeDepth/dot(V,Front))`, bias-Schlick `GetFresnel(0.05, 5)`,
  edge blend `:1194`) + `WaterFogVolume.cfx`. RAGE `WaterFogCommon` MRT fog prepass
  (water_common.fxh:664+).
- **SM3.0**: INTERIOR bodyType (z>950 pools) is already classified (W7-B). Interior water wants
  opaque-tinted volume shading, not the ocean sheet: a 4th entry point `main_volume` or a hard
  branch on `BodyContract` type==2 with volume params in c16.
- **Integration**: `Water_Parallax.hlsl` + `Water_Underwater.hlsl` reuse (same exp2 fog law).
- **Risk**: MED (interior water is rare in SA; low payoff relative to M2-M5).

### M9 — FFT-lite swell (defer)
- Cry FFT VTF (`Water.cfx:598-620`: `tcFFT = pos.xy·0.0125·OceanParams0.w·1.25`,
  `pos += atten·vtxDispl·0.06·OceanParams1.x·(1.5,1.5,1)`) → vs_3_0 `tex2Dlod` on a ≤64² LUT,
  CPU-baked from `OceanParams` (windSpeed 4.0, wavesAmount 1.5, wavesSize 0.75). Only after M1-M5.
- **Risk**: MED (VTF in VS is the one true SM3.0 grey zone; verify on target GPU first).

---

## 4. Water physics integration (Bullet lane contract)

**How the references couple buoyancy to the rendered surface:**
- RAGE: physics never touches the render mesh. `CWaterTestHelper::GetWaterHeightAtPositionIncludingRivers
  (vPos, …, bConsiderWaves=true, …)` (WaterTestHelper.h:50) queries `Water::GetWaterLevel()` —
  the SAME height field that composes the render geometry — and `CBuoyancy` (Floater.cpp) applies
  a force set against it: buoyancy + lift + keel + flow-induced + drag + surface-stick
  (ms_bDisable* toggles, Floater.cpp:65-71). Rivers are merged via cached river-bound probes.
- SA: `CWaterLevel` core 0x6E8580 (static level) + `CalculateWavesForCoordinate` 0x6E6EF0
  (shading waves: z, colorMult, normal). The wave-composing sibling 0x6EB690 exists but our
  render surface is the c11 swell, not the game wave field.

**Our shared surface-height function (must stay one law, three consumers):**
- `SoftbodyWaterQuery(x,y,z,&level)` — static level via 0x6E8580 (softbody.cpp:158).
- `SoftbodyWaterSwell(x,y)` / `SoftbodyWaterSwellVel(x,y)` — **verbatim port of Water_VS c11**
  (dirs (1,0.35)/(-0.45,1), λ 45/23 m, amp 0.10/0.06, `WaterSwellMakeWave` derives
  normalize + sqrt(9.81/k) with the shader's guards). Surface Z = query level + swell.
- Consumers: (1) shader VS (c11 upload, waterPipe.cpp:1313), (2) `RagdollWaterForces`
  (ragdoll_death.cpp:951 — buoyancy `g·(submergedFraction·densityRatio − 1)`, densityRatio
  0.6-1.0, 1.0 = sink-proof), (3) `SoftRagdoll_c::WaterResponse` (sftbdyphsx.cpp).

**Contract for the overhaul (post-deadline, physics lane owns #1):**
1. **Single clock**: today `g_waterTime` (the shader's `camTime.w`) only advances while water
   flushes run, while `WaterClockSeconds()` is wall-clock → phase divergence up to half a
   wavelength (~0.16 m) after interiors. Fix: one always-on accumulator ticked in the main loop,
   shared by waterPipe's c11.w and softbody's swell. Any M9 displacement LUT must use the same
   clock and the same (x,y)→height function so ragdolls track the visible surface.
2. If M6 (ripple sim) lands: the sim's height field is GPU-side → physics keeps the analytic
   c11 swell (CPU-cheap) and treats ripples as visual-only (amp ≪ buoyancy tolerance), same as
   RAGE where the sim grid is cosmetic beyond `GetWaterLevel`.
3. Bullet tunables already mirror across softbody.cpp / ragdoll_death.cpp (SOFTBODY_WATER_*
   constants) — any new wave-force term must be added in both with the same law.

---

## 5. Phased rollout

**Phase 0 — pre-Halloween quick wins (by Oct 31):**
1. M1 flood guard + far-water fade (kills the 59% F2 family: slab-on-street, horizon band,
   quad seam) — shading + existing W7-B table only.
2. M5 caustics sharpen/resolution fix (blocky pixels called out in findings).
3. Foam band tuning on the FFP fallback path (F4 white streaks may be wake foam degrading —
   verify the vanilla passthrough keeps its vertex-colour alpha ramp).
4. No new samplers, no new RTs, no build-system changes — edits confined to
   `Water_Parallax.hlsl` composite + `waterPipe.cpp` classify alpha.

**Phase 1 — foam + refraction (first post-deadline sprint):** M2 (foam texture blending, +s4/s5),
M3 (soft-intersection foam), M4 (depth-validated refraction). Biggest visual return per risk.

**Phase 2 — lighting polish:** M7 (SSS/pierce), M5 dispersion variant, per-body tuning
(river anisotropic scroll using W7-B aspect channel — the spare TEXCOORD5.z is already plumbed).

**Phase 3 — deep systems:** M6 ripple sim (new waterSim.cpp, tier-3 gated), M8 interior volumes,
M9 FFT-lite LUT (target-GPU VTF verification first), physics-clock unification (§4 contract #1).

**Do-not-regress list** (findings.md): near-camera caustic presence, HUD, interior pipeline,
tier<2 SRCALPHA fallback path, vanilla FFP water when waterParallaxEnable=0.

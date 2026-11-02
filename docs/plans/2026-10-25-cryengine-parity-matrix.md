# CryEngine 3.8.6 → skygfx_plus_expIV effect parity matrix

Reference engine: `E:\dev(dave)\releases\GTASource\CRYENGINE-release\Engine\Shaders`
(`HWScripts/CryFX/*.cfx` + `.cfi`, ~118 shader sources) and `EngineAssets` (noise/texture assets).
Our side: `src/render/*` + `shaders/*` read directly — **no claim below is taken from `docs/`**.
Companion doc: `docs/plans/2026-10-25-cryengine-cloud-reference.md` (clouds / sky / atmosphere /
height fog / sunshafts in depth). Rows marked *XREF* defer algorithm detail to that doc.

CryEngine 3.8.6 is an SM4/SM5 deferred renderer (DX11): G-buffer MRT, compute shaders, hardware
tessellation, shadow-map cascades. We are a forward RW 3.6/D3D9 SM3.0 plugin with two PBR pipes and
an Im2D post chain. Parity is therefore *effect-level*, never pipeline-level.

## 0. Matrix

| CryEngine effect | CryEngine file(s) | What it does (1-2 sentences) | Our implementation | Our file:line | Status | SM3.0 feasible | Port priority |
|---|---|---|---|---|---|---|---|
| SSAO (SSDO deferred) | `DeferredShading.cfx:1182` `SSDOFetchDepths`, `:1217-1302` SSDO taps; `ShadowBlur.cfx:442` `SSDO_Blur` | Screen-space directional occlusion computed inside the deferred light loop from reconstructed view-space depth+normal; blurred separately. | Quarter-res SSAO overhaul: 16-tap hemisphere kernel + temporal reprojection + depth-aware bilateral blur + upsample; full-res 16-tap fallback. | `src/render/postfx_ssao.cpp:706` (overhaul), `:518` (fallback), `shaders/ps/SSAO.hlsl:124` | PARTIAL | yes | P1 |
| HMAO / horizon-based AO | `ShadowBlur.cfx:451` `HMAO_Blur` (+ horizon search in deferred AO path) | Horizon-based occlusion: marches depth along screen axes, integrates max horizon angle — cheaper and less halo-prone than kernel AO. | Not implemented; our kernel AO is the only AO. | — | MISSING | yes | P1 |
| Terrain AO | `AmbientOcclusion.cfx:87` `Deferred_TerrainAO_Pass` | Heightmap/vegetation-elevation based sky-occlusion for terrain (not screen-space); samples terrain info textures. | None. | — | MISSING | yes (needs heightmap access) | P3 |
| CSM shadows (4 cascades) | `ShadowMaskGen.cfx:709` `DeferredShadowPass`, `:696` `DebugShadowCascades`; `ShadowCommon.cfi:406-441` cascade select | Four cascaded sun shadow maps + per-cascade blend, normal-offset bias, cloud-shadow tap; sampled in deferred lighting and forward passes. | None of our own. The game's stencil shadow volumes are left intact; we only fix their blend/state contract. | `src/core/main.cpp:1348` (`CShadows__RenderStaticShadows` hook) | MISSING | no (see §1) | — |
| Contact shadows | — | **Not present in 3.8.6** (added in CE 5.x `ContactShadows.cfx`). | — | — | N-A | yes (depth-ray) | P2 |
| Shadow map blur / soft shadows | `ShadowBlur.cfx:400` `ShadowBlurGen`, `:414` `ShadowBlurScreen`, `:460` `ShadowGaussBlur5x5` | Screen-space blur of the shadow mask to soften PCF edges and hide cascade seams. | No shadow maps exist to blur; only a depth-based contact-shadow approximation is possible. | — | MISSING | yes (as contact shadow) | P2 |
| TAA | `PostAA.cfx:23` `mReprojection`, `:555` prev-position reproject; TAA composite in `PostAAComposites` | Full temporal AA: velocity/depth reprojection, neighborhood clamp, history blend; grain moved out of HDR PP because TAA eats pixel noise. | SMAA **temporal** only (1-frame neighbourhood blend, no reprojection). Velocity buffer exists but is not fed to AA. | `src/render/postfx_smaa.cpp:441`; velocity at `postfx_motionblur.cpp:23` | PARTIAL (temporal SMAA ≠ TAA) | yes | P0 |
| SMAA 1x | `PostAA.cfx:1146` `LumaEdgeDetectionSMAA`, `:1156` `BlendWeightSMAA`, `:1166` `NeighborhoodBlendingSMAA` | Edge-detect + blend-weight + neighbourhood blend using Area/Search LUTs. | Full SMAA with generated Area/Search textures, temporal pass 3, raw-geometry state guard, history invalidation. | `src/render/postfx_smaa.cpp:441` | SHIPPED | yes | — |
| FXAA | (CE uses SMAA/TAA; FXAA not a CE 3.8 technique) | — | FXAA3 late-slot AA; SMAA wins when both enabled. | `src/render/postfx_newfx.cpp:789` | SHIPPED (extra) | yes | — |
| Bloom | `HDRPostProcess.cfx:1000` `HDRBloomGaussian` (+ down/up chain in `PostEffects.cfx`) | Bright-pass, Gaussian pyramid down/up, additive composite in HDR before tonemap. | Bright-pass with soft knee + Karis anti-firefly, 2-6 mip downsample/upsample chain, additive composite. | `src/render/postfx_newfx.cpp:629` | SHIPPED | yes | — |
| Depth of field | `DepthOfField.cfx:893` `DepthOfField`, `:937` `DofHiQuality`; `MotionBlur.cfx:502` `TileMinCoC`, `:626` `Dof` | CoC from depth, tile min/max CoC, half-res bokeh gather, near/far fields, composite. | CoC → half-res bokeh → tent → full-res combine, driven by `g_ssaoDepthTex`; no tile min-CoC, no near-field layer. | `src/render/postfx_newfx.cpp:552` | PARTIAL | yes | P1 |
| Motion blur | `MotionBlur.cfx:755` `MotionBlur`, `:805` `PackVelocities`, `:834` `VelocityTileGen` | Per-object velocity reconstruction from depth + prev matrices, tile min/max, camera+object blur with DofMb normalisation. | Burnout-style screen-space radial/velocity blur blended by speed ramp and camera rotation; separate velocity buffer pass. | `src/render/postfx.cpp:3605` (gate at `:3614`, `motionBlurEnable`) | PARTIAL (no per-object vectors) | yes | P2 |
| Sun shafts | `Sunshafts.cfx:256` `SunShaftsMaskGen`, `:266` `SunShaftsGen`, `:276` `SunShaftsDisplay` | Occlusion mask → radial blur from sun screen pos → additive display with edge AA. **XREF cloud doc §2.5.** | `DrawSunShafts`: single-pass radial scatter, sun position from sky, additive to camera RT, pre-tonemap. No occlusion mask, no edge-AA display pass. | `src/render/postfx_newfx.cpp:517` | PARTIAL | yes | P2 |
| Height fog | `HDRPostProcess.cfx:1043` `FogPass`, `:1069-1082` vol-shadow fog passes. **XREF cloud doc §2.4.** | Analytic exponential height fog with sun/ambient in-scatter and optional volumetric-shadow taps. | `DrawHeightFog`: exponential height fog with timecycle colour, density/falloff/start-height, depth-aware, pre-tonemap. | `src/render/postfx.cpp:3874` | PARTIAL | yes | P2 |
| Volumetric fog / fog volumes | `VolumetricFog.cfi:56` `GetVolumetricFogColorInternal`, `FogVolume.cfx`, `VolumeObject.cfx` | Ray-marched froxel/volume fog with radial lobe, shadowed in-scatter, per-volume analytic shapes. | None. Height fog is analytic only. **XREF cloud doc §2.4.** | — | MISSING | yes (raymarch, no froxel/UAV) | P1 |
| Water / ocean | `Water.cfx:1268` `Water`, `WaterOceanBottom.cfx`, `WaterVolume.cfx`, `WaterReflectionsPass.cfi` | Tessellated displaced ocean, per-pixel normals from wave layers, SSR + planar reflection, foam by depth/crest, refraction, caustics. | Shading-only water (no vertex displacement): 3 tiers — fresnel composite, refraction/depth copy + foam/shore blend, sun glints; planar reflection RT + screen-space reflection copy; timecycle colour. | `src/render/waterPipe.cpp:905` (`setRenderState`), `:1081` (flush draw) | PARTIAL | yes (no tess) | P1 |
| Underwater / water fog volume | `WaterFogVolume.cfx`, `WaterVolume.cfx` | Per-volume underwater absorption + in-scatter when the camera enters a water body. | Screen-space Beer-Lambert extinction + tint when submerged, gated by `waterUnderwaterFog` (0..4). | `src/render/postfx_underwater.cpp:310` (`PostFX_UnderwaterTick`) | PARTIAL (no volume shapes) | yes | P3 |
| Terrain shading | `Terrain.cfx:676` `General`, `:696` `Layer` (POM, 4-layer blend, tessellation), `SketchTerrain.cfx` | Multi-layer terrain with POM displacement, detail normal blend, tessellation and terrain-specific shadow/AO. | Not touched: the game's own terrain pipeline renders terrain. No terrain shader in our tree. | — | N-A (out of pipe scope) | partial (POM yes, tess no) | — |
| Vegetation alpha cards | `Vegetation.cfx:837` `General` (alpha-test, bending, touch-bend, tessellation) | Instanced vegetation with alpha-tested cards, vertex bending (wind + touch), tessellated near-field. | Tree-alpha pass + tree wind hook; batched plant-island grass instancer; alpha handling in `treeWind.cpp`. No touch bending, no tessellation. | `src/render/treeWind.cpp:256`, `PC_GrassRenderer.cpp`, `PC_GrassInstancer.cpp` | PARTIAL | yes (bending), no (tess) | P2 |
| Vegetation imposters / far trees | `FarTreeSprites.cfx` | Billboard imposters for distant trees with alpha and lighting normalisation. | Imposter atlas infrastructure exists but defaults OFF (builds atlas from atomic renders). | `src/render/vegImposter.cpp:416` | PARTIAL (default off) | yes | P3 |
| Decals | (no `Decal.cfx` in 3.8.6; decals go through `Illum.cfx` / deferred forward passes) | Deferred/projected decals blended into the G-buffer. | Only translucent/decal blend-state handling inside the building pipe; no decal projection system. | `src/render/buildingPipe.cpp:1720` | N-A | yes (projected quad) | — |
| Particles | `Particles.cfx`, `GpuParticles.cfx:53-651` (compute sort/update), `ParticleImposter.cfx` | CPU/GPU particle systems; GPU path uses compute sort, feature update, bounds reduction. | No particle system of our own; only a hook that forwards water-drop spawns to the game's particle manager. | `src/render/neoWaterdrops.cpp:253` | N-A | no (GPU path) / yes (CPU) | — |
| PBR / GGX | `DeferredShading.cfx:850` BRDF + `shadeLib.cfi` | GGX NDF + Smith visibility + Schlick Fresnel, energy conservation, roughness→alpha remap. | GGX + Smith-correlated + Schlick (incl. LdotH variant), roughness remap matching CE's `alpha=(1-smoothness)^2`, shared by building + vehicle + rubber PBR entry points. | `shaders/include/PBR_Common.hlsl:27`, `shaders/ps/VehiclePBR_Modern.hlsl` | SHIPPED | yes | — |
| IBL / env probes | `DeferredShading.cfx:850` `GetDeferredCubemap`, `:709` probe OBB mapping; `LightPropagationVolumes.cfx` | Cubemap probe lookups per pixel (diffuse + specular) plus probe-box parallax correction; LPV GI as a separate system. | One dynamic-sky IBL cubemap captured once per frame, bound raw on `s3`; no probe grid, no probe-box parallax, no local probes. | `src/render/pipelinecommon.cpp:1141` (`pipeEnsureIBLBuffer`) | PARTIAL | yes (single probe) | P2 |
| SVOGI / voxel GI | `Total_Illumination.cfx:2171` `ConeTracePass`, `:2161` `SvoDebugDraw` | Voxelized scene + cone-traced diffuse/specular GI with multi-bounce injection. | None. | — | MISSING | no | — |
| LPV GI | `LightPropagationVolumes.cfx:290` `LPVPropagate`, `:108-212` inject, `:526` `LPVApply` | Light propagation volumes: RSM inject → 3D volume propagation → per-pixel apply. | None. | — | MISSING | no | — |
| Tonemap | `HDRPostProcess.cfx:1009` `HDRFinalPass` (+ `PostEffects.cfx` grading) | Exposure scale, filmic curve, colour grading, sRGB encode. | `TonemapPass` with 6 selectable operators (filmic, ACES 1.3/2.0, AgX, Frostbite, GT7), LUT path, black-level lift, grade handshake. | `src/render/postfx_colourfilter.cpp:612`, `shaders/ps/TonemapLUT.hlsl` | SHIPPED | yes | — |
| Eye adaptation | `HDRPostProcess.cfx:973` `HDRSampleLumInitial`, `:982` `HDRSampleLumIterative`, `:991` `HDRCalculateAdaptedLum` | 8x8 luminance measure, iterative reduction, temporal adaptation of exposure. | 8x8 measure + 1x1 double-buffered adaptation with `tonemapAutoExposure` gate and adapt speed. | `src/render/postfx_luminance.cpp:166`, `:229` | SHIPPED | yes | — |
| Lens flare / lens optics | `LensOptics.cfx:507-630` (Ghost, Glow, ChromaticRing, IrisShafts, Streaks, CameraOrbs, ImageSpaceShafts) | Multi-element lens flare: ghosts, halo ring, streaks, orbs, image-space shafts. | The game's own sun glare / corona / streaks are kept and only state-hygiene'd; weather scales streak intensity/size. No shader-side flare system. | `src/core/main.cpp:4365` (`CClouds::Render` hooks), `src/core/weather.cpp:271` | PARTIAL (game-owned) | yes | P3 |
| Film grain | `PostAA.cfx:1210` `ApplyFilmGrain`, `:1217` 3D noise tap, `:1219` exposure fade | Animated 3D grain modulated by exposure, applied post-AA. | `DrawFilmGrain` with runtime noise, `grainIntensity` 0..1, exposure-ish scaling. | `src/render/postfx_newfx.cpp:737` | SHIPPED | yes | — |
| Chromatic aberration | `PostEffects.cfx:1180` `CA_SharpeningPS`, `PostEffectsGame.cfx:2032` `ApplyRadialBlurAndChromaShift` | Radial CA on the post chain; combined with a sharpening kernel in `CA_Sharpening`. | `DrawChromaticAberration`, radial RG/B split, `caIntensity` 0..1. | `src/render/postfx_newfx.cpp:703` | SHIPPED | yes | — |
| Vignette | `PostEffectsGame.cfx:1941-1961` (UberPost vignette alpha) | Radial vignette mask, optionally noise-modulated, applied in the uber post shader. | `DrawVignette` using the legacy `vignetteIntensity`/`radius`/`contrast` triple. | `src/render/postfx_newfx.cpp:767` | SHIPPED | yes | — |
| Sharpening | `PostEffects.cfx:1180` `CA_SharpeningPS` (CA + unsharp in one pass) | Unsharp/luma sharpening combined with CA; single full-screen pass. | None as a post pass (only in-shader surface sharpening term in the building PBR path). | `shaders/ps/VehiclePBR_Modern.hlsl:510` (comment only) | MISSING | yes | P3 |
| Screen-space reflections | `DeferredShading.cfx:1088` `SSR_Raytrace`, `:1123` `SSReflection_Comp`; `WaterReflectionsPass.cfi` | Depth-ray-marched reflections in screen space with blur/fallback, used for water and smooth surfaces. | `PipeChain`: screen-space reflection chain for water/vehicle surfaces (classify + reflection copy + composite); water also has a flipped planar reflection RT. No general opaque-surface SSR. | `src/render/postfx.cpp:3196`, `src/render/waterPipe.cpp:1017` | PARTIAL | yes | P0 |
| Subsurface scattering | `HumanSkin.cfx:433` `General` (thickness/translucency, tess), `:589` `ZPass` | Skin translucency from thickness + wrap diffuse, with tessellated displacement. | Screen-space SSS blur post pass + wrap-lighting skin enhance; per-material SSS strengths for skin/hair/vegetation; no thickness map. | `src/render/chars.cpp:321` (`chars_drawSSSBlur`) | PARTIAL | yes (no thickness) | P2 |
| Clouds / atmosphere | `Clouds.cfx`, `Sky.cfx`, `SkyHDR.cfx:250`, `DistanceClouds.cfx`, `Stars.cfx` | Volumetric cloud raymarch, HDR sky, distant cloud layers, stars. **XREF cloud doc §2.1-2.3.** | `CloudsPS.hlsl`, `DynamicSky.hlsl`, `IBL_SkyCloud.hlsl` + sky capture feeding IBL; clouds are the cloud lane's territory. | `shaders/ps/CloudsPS.hlsl`, `postfx.cpp` (sky/IBL path) | PARTIAL | yes (cross-ref) | XREF |
| Caustics | `DeferredCaustics.cfx`, `WaterCausticsPass.cfi` | Animated projected caustic texture on underwater/water-adjacent geometry. | None. | — | MISSING | yes (cheap projected texture) | P3 |
| Rain / snow (deferred) | `DeferredRain.cfx`, `DeferredSnow.cfx` | Screen-space rain streaks / snow using deferred normals + depth. | N-A — the game owns weather particles; we only hook water-drop spawns. | `src/render/neoWaterdrops.cpp:253` | N-A | yes | — |

## 1. SM3.0 blockers

Hard limits that decide every row above (D3D9 SM3.0, `ps_3_0` / `vs_3_0`; `tools/fast_build.py:357`
compiles the whole tree at these profiles):

1. **Instruction ceiling.** `ps_3_0` = 512 static instruction slots (min spec) and 512 texture
   slots; `vs_3_0` = 256 slots. `VehiclePBR_Modern.hlsl` already spends roughly 300-380 slots in
   `main`/`main_building` (GGX + Smith + 7 lights + FP cluster + IBL + normal map). Consequence:
   **no new per-pixel effect may be added to the PBR PS.** Every port below lands as its own
   full-screen pass with its own (small) shader.
2. **No compute / UAV / atomics.** `GpuParticles.cfx` (bitonic sort, bounds reduction, feature
   update), `DXTCompress.cfx`, `GpuMergeSort.cfx`, `CloudShadowGenCS`-style passes are impossible.
   Any scatter/gather must be a ping-pong RT chain.
3. **No geometry shader, no tessellation.** `CommonTessellation.cfi`, `Water.cfx` HW tessellation,
   `Vegetation.cfx` tessellation, `Terrain.cfx` POM+tess have no D3D9 equivalent. Vertex-texture
   fetch (`vs_3_0` supports 4 fetches/vertex) can fake height displacement, but not patch
   subdivision. All `.cfi` "Tess" variants are excluded by construction.
4. **No texture arrays.** CE binds cascade/shadow slices via arrays; D3D9 needs N separate samplers
   (we have 16 total, 9 already used — see §4). 4-cascade CSM + G-buffer + IBL would exhaust them.
5. **MRT is 4 RTs with matching bit depths, no per-RT blend, and depth cannot be sampled while it is
   bound.** `DeferredShading.cfx` writes albedo/normals/spec/velocity in one pass; we cannot. Depth
   must be copied to an INTZ/colour target first — that is exactly what
   `postfx_ssao.cpp:177` (`CopyDepthToPrev`) / `:288` (`PostFX_CopyDepthToTexture`) already do.
6. **No integer/bit ops, no texture gather, no sRGB read control, no hardware PCF.** Shadow sampling
   with PCF would be 4-9 manual taps + compare per light; at 7 direct lights that alone blows the
   PS budget. This is the structural reason CSM is a no-go, not the shadow math.
7. **Derivatives and `discard` are restricted inside divergent flow.** `ddx/ddy` and dynamic
   branching loops are legal but gradients inside divergent branches are undefined — rules out
   CE's "adaptive sample count" tricks in AO/SSR that branch on depth delta.
8. **Dynamic branching is real but the compiler must flatten static loops** (255/rep cap). A 16-step
   SSR march must be written as a bounded `for` with a constant limit, not a `while` on hit.
9. **Constant budget.** `ps_3_0` = 224 float4 constants. Our world shaders already use c0-c52
   (§4); post passes use their own c0-c8 namespace, which is why the current Fx passes are free.
10. **No MSAA + depth-texture resolve path.** CE's `PostAAComposites`/`MsaaDebug` chain assumes
    MSAA depth resolve; our device path is non-MSAA with an INTZ copy, so all MSAA-dependent AA
    code is dead on arrival.

## 2. Highest-value ports (ranked top 8)

Ranked by visual delta per instruction slot, given what already ships. All are *new full-screen
passes*, never edits to the PBR PS. Kill-switch names are proposed INI keys under `[SkyGfx]`,
default 0, parse site `src/core/main.cpp` read block (~:5490-6060) + `ADD_IF_MISSING` + save list.

| # | Port | Target file | Register slots | Est. PS slots | INI kill-switch |
|---|---|---|---|---|---|
| 1 | **General SSR** — generalise `PipeChain` (`DeferredShading.cfx:1088` `SSR_Raytrace`) from water/vehicle to opaque smooth surfaces: depth ray-march, hit refinement, edge fade, fallback to `g_iblTex`. Half-res. | `src/render/postfx.cpp:3196` + `shaders/ps/PipeChain.hlsl` | `s10`=depth (INTZ copy), `s11`=normal buffer (or reuse existing `s4` binding), `c53`=SSR params (maxDist, thickness, fade, intensity), `c54`=proj info | ~110-150 (24-step march + resolve) | `ssrEnable`, `ssrIntensity` |
| 2 | **TAA-lite** — reproject with the existing velocity buffer + depth, neighbourhood clamp, history blend ordered *before* SMAA edge-detect (CE `PostAA.cfx:23/555`). Kills SSAO/SMAA shimmer. | `src/render/postfx_smaa.cpp` (new pass) or `postfx.cpp` pre-SMAA | `s10`=velocity, `s11`=history, `c53`=blend/clamp, `c54`=jitter; no change to SMAA's own regs | ~45-60 | `taaEnable` |
| 3 | **HBAO/HMAO** — replace 16-tap kernel SSAO (`SSAO.hlsl:124`) with horizon search (CE `ShadowBlur.cfx:451` `HMAO_Blur`): 4-8 directions × 4 steps, max-horizon integration. Same inputs, far less halo. | `src/render/postfx_ssao.cpp:706` + `shaders/ps/SSAO.hlsl` | none new (reuses c0-c4: radius/power/noiseScale/blend, quarter dims, proj info, history flags) | ~90-120 at quarter res | `ssaoHorizon` (master stays `ssaoEnable`) |
| 4 | **SSDO** — directional occlusion + one-bounce colour bleed from the normal buffer (CE `DeferredShading.cfx:1244-1302`). Reuses `g_normalBufferTex` already populated when `normalBufferEnable=1`. | `src/render/postfx_ssao.cpp` (composite stage) + new `shaders/ps/SSDO.hlsl` | `s10`=normal buffer, `s9` already noise; `c53`=SSDO params (bounce strength, occlusion dir scale) | ~60-80 added to AO resolve | `ssdoEnable` |
| 5 | **Volumetric fog raymarch** — 12-16 step in-scatter using height-fog params + depth (CE `VolumetricFog.cfi:56`). **XREF cloud doc §2.4** for the parameter mapping; do not duplicate its noise work. | `src/render/postfx.cpp:3874` + `shaders/ps/HeightFog.hlsl` | `s10`=depth, `s9`=universal noise (already contractual); `c53`=raymarch params (steps, jitter, density scale) | ~70-100 | `volumetricFogEnable` |
| 6 | **DoF tile min-CoC + near field** — CE `MotionBlur.cfx:502` `TileMinCoC` / `DepthOfField.cfx:893`: add a max-CoC tile pass so the bokeh gather can shrink its tap radius, plus a near-field layer. Cuts current banding and cost. | `src/render/postfx_newfx.cpp:552` + `shaders/ps/DOF_*.hlsl` | none new (c0-c2 already DOF's own namespace) | ~30-45 extra | `dofNearEnable` (master `dofEnable`) |
| 7 | **Contact shadow approximation** — depth-ray march toward the sun in screen space, 8 taps, tint by ambient (CE has no contact shadows in 3.8.6; this is the `ShadowBlur.cfx` *idea* without shadow maps). Only AO-adjacent gap that is reachable. | `src/render/postfx.cpp` (before height fog) + new `shaders/ps/ContactShadow.hlsl` | `s10`=depth, `s9`=noise (dither); `c53`=ray params (length, thickness, strength) | ~40-50 | `contactShadowEnable` |
| 8 | **Water crest foam + soft intersection** — CE `Water.cfx:202-235` (`FoamSoftIntersectionFactor`, `FoamCrestAmount`): depth-based soft intersection at geometry, crest-weighted foam from wave height, additive to the tier-2/3 water path. | `src/render/waterPipe.cpp:905` + `shaders/ps/Water_Parallax.hlsl` | existing water PS block (`c2`, `c14`); add `c15` for crest/soft params (water PS has its own namespace) | ~25-35 | `waterFoamCrest`, `waterSoftIntersection` |

Deliberately *not* in the top 8: lens-flare ghosts (game owns the flare, high risk/low delta),
caustics (invisible in SA's shallow water), vegetation touch-bending (cheap but cosmetic, P3),
sharpening (one-pass but the CA pass already touches every pixel and CE fuses them — fold into CA
later rather than spending a slot).

## 3. Do-not-port list

| Effect | Why not |
|---|---|
| CSM shadow maps (`ShadowMaskGen.cfx`, `ShadowCommon.cfi`) | Would replace GTA SA's stencil shadow-volume system wholesale: no shadow-map depth exists, no hardware PCF, 4 cascades × manual taps at 7 lights is over the PS budget, and the game's shadow renderer (`CShadows__RenderStaticShadows`, hooked at `main.cpp:1348`) is entangled with stencil state the rest of the frame depends on. Effect-level substitute = contact shadows (#7 above). |
| SVOGI (`Total_Illumination.cfx`) | Requires 3D-texture UAV writes, voxelization of the whole scene per frame, cone tracing with 32+ taps. No compute, no UAV, no 3D RT in D3D9. |
| LPV GI (`LightPropagationVolumes.cfx`) | Needs MRT RSM inject + 3D volume propagation + additive volume blending; D3D9 has no 3D render targets with blending. |
| GPU particles (`GpuParticles.cfx`) | Bitonic sort / bounds / kill-list passes are compute. CPU-side reimplementation is a different project, and SA already has a particle system. |
| Tessellation variants (`.cfi` `CommonTessellation`, `Water.cfx`, `Vegetation.cfx`, `Terrain.cfx` tess) | No HS/DS in D3D9. VTF displacement is a visual approximation, not parity; do not label it "ported". |
| Deferred shading / G-buffer (`DeferredShading.cfx`) | SA is forward-rendered through RW 3.6; converting is a renderer rewrite, not an effect port, and MRT(4)+depth-copy+7-light deferred would not fit the SM3 budget. |
| Shadow mask / cube shadow gen (`ShadowMaskGen.cfx`, `:668-696` debug) | Depends on the CSM pipeline above. |
| `Hair.cfx`, `CloakLayer.cfx`, `Eye.cfx`, `Monitor.cfx`, `Glass.cfx`, `Scopes.cfx`, `Hud3D.cfx`, `MonoFrameworkUI.cfx`, `Scaleform.cfx` | Asset/material-specific CE shaders with no counterpart in SA's model set; porting them would add dead code. |
| `Stereo.cfx` | VR/stereo, out of scope. |
| `DXTCompress.cfx`, `MeshBaker*.cfx`, `ReferenceImage.cfx` | Offline/editor tooling, not runtime effects. |
| MSAA post composites (`PostAA.cfx:1262` `PostAAComposites`, `:1190` `MsaaDebug`, `UpscaleImage`) | We render non-MSAA at native resolution; nothing to resolve or upscale. |
| `DeferredRain.cfx` / `DeferredSnow.cfx` | Require deferred normals; SA weather is particle-driven and already owned by the game. |
| Cloud shadow maps as CS (`CloudShadowGenCS`) | Compute; the cloud lane already replaces it with a PS→RT pass (cloud doc §5.3). |

## 4. Budget / slot ledger (authoritative for new passes)

Frozen contracts — do not move, do not hand-upload:

- `c22` / `c23` — **must** go through `pipeUploadPBR()` (`src/render/pipelinecommon.cpp:998`).
- `s9` + `c49-c52` — universal noise (`src/render/noise.cpp:895` `BindAndUpload`; `shaders/include/Noise.hlsl:42-46`).

World-shader registers currently taken (PS):

| Slots | Owner |
|---|---|
| `s0-s4` | diffuse / env / mask / IBL cubemap / screen-space normal |
| `s5-s7` | Forward+ tile index / light-pos atlas / light-colour atlas |
| `s8`, `c48` | normal-map texture + nm params |
| `s9`, `c49-c52` | universal noise (frozen) |
| `c0-c4` | surfProps / fxParams / eyePos / iblParams / cloudShadow |
| `c5-c18` | 7 direct light colours + directions |
| `c19`, `c24`, `c29` | matCol / ambient / shader params |
| `c22-c23` | PBR (frozen) |
| `c30-c36` | day-night, texture transform, eye position |
| `c37-c43` | `colorSpace.hlsl` grading constants |
| `c44-c47` | chrome params / FP cluster / layer mask / building env |

**Next free sampler: `s10`. Next free constant: `c53`.** Post-process passes are a separate shader
namespace and may use `c0-c8` freely, but any register reaching a *world* shader must start at
`s10` / `c53`. A second new post pass wanting more than c0-c8 must also start at c53.

Cross-check / open questions:
- `c45` is Forward+ cluster params and `c44` chrome params — both live in `VehiclePBR_Modern.hlsl`;
  do not repurpose for post passes.
- SM3 instruction estimates above assume FXC `-O3`; re-measure with `fxc /Fc` on the first build of
  each port, because a 24-tap SSR march can silently cross 512 slots and fail as black.
- All eight ports must restore sampler/render state per the `DrawHeightFog` doctrine
  (`postfx.cpp:3874` cleanup block) — the post chain shares state with the whole frame.

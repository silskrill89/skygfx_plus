# GTA V Renderer — Parity Audit for skygfx_plus_expIV "Modern+" (2026-10-25)

Reference: `E:\dev(dave)\releases\GTASource\rage\GTAV Source\src\dev_ng\game\renderer` (276 files)
and `...\game\shader_source` (530 files). Target: GTA SA ASI, D3D9, SM3.0 only
(`ps_3_0`/`vs_3_0`), no compute/UAV/typed buffers, ≤4 MRT, PS consts to `c223`,
samplers `s0-s8` in use, `s9/s10` free.

All citations are `file:line`. "not found" means the construct does not exist in the
reference tree. V's renderer is a **deferred/tiled** renderer, ours is **forward+ with a
tiled light list** — every port below is expressed as a forward-pass shader change.

---

## §1 Inventory of the V renderer

### 1.1 Subsystem directories (162 of 276 files)

| Dir | Files (count) | What it does | Citations |
|---|---|---|---|
| `Deferred/` | `DeferredConfig.h`, `GBuffer.cpp/.h`, `DeferredLighting.cpp/.h` (5) | G-buffer creation/locking, deferred light pass permutation table, directional/tiled directional shadow pass | `Deferred/DeferredConfig.h:23-39` (GBufferRT enum), `Deferred/GBuffer.cpp:206` (`CreateTargets`), `Deferred/DeferredLighting.cpp:1765` (`RenderTiledDirectionalLight`), `:1844` (`RenderTiledShadow`) |
| `DrawLists/` | `CopyOffMatrixSet.cpp`, `CopyOffMatrixSetSPU.frag`, `CopyOffMatrixSetSPU_frag.cpp`, `DrawListNY.cpp/.h`, `DrawListProfileStats.cpp/.h`, `DrawableDataStructs.cpp/.h`, `drawList.cpp/.h`, `drawListCommands_Entities.cpp`, `drawListMgr.cpp/.h`, `drawListMgr_config.h`, `drawList_CopyOffEntityVirtual.h` (16) | Per-render-phase command lists (`DLC`) and GPU draw-list caching; manager owns alpha list count | `DrawLists/drawListMgr.cpp:63` (`ms_nNumAlphaEntityLists=4`), `:116` (`Initialise`) |
| `Entities/` | `CutSceneDrawHandler`, `DynamicEntityDrawHandler`, `EntityBatchDrawHandler`, `EntityDrawHandler`, `InstancedEntityRenderer(+_inline)`, `ObjectDrawHandler`, `PedDrawHandler`, `PtFxDrawHandler`, `VehicleDrawHandler`, `VehicleGlassDrawHandler` (21) | Per-entity-type draw handlers that emit DLC commands (vehicles, peds, objects, particles, vehicle glass) | `Entities/VehicleDrawHandler.cpp:108` (`AddToDrawList`), `Entities/PtFxDrawHandler.cpp:20`, `Entities/VehicleGlassDrawHandler.cpp:20` |
| `fw/` | `RenderPhaseList.cpp/.h` (2) | Effectively a stub: the `.cpp` is 8 lines of header comment and the `.h` only forward-declares `CRenderPhase`/`CViewport` | `fw/RenderPhaseList.cpp:1-8` (stub), `fw/RenderPhaseList.h:25-26` (forward decls) |
| `Lights/` | `AmbientLights`, `AsyncLightOcclusionMgr`, `LODLights`, `LightClassification.inl`, `LightCommon.h`, `LightCulling.h`, `LightEntity`, `LightGroup`, `LightInstance.h`, `LightSource.h`, `LightsVisibilitySort(.frag/_frag.cpp)`, `TiledLighting(.h/.cpp)`, `TiledLightingSettings.h`, `lights(.h/.cpp)` (24) | Light types/culling, tiled light classification texture, per-light volume stencil passes, light shafts list | `Lights/LightCommon.h:15-23` (point/spot/capsule/directional/AO volume), `Lights/TiledLighting.cpp:63` (`m_tileSize=16`), `:355` (`Init`), `Lights/LightCulling.h:18` (`fwBoxFromCone`) |
| `RenderPhases/` | 19 pairs: `RenderPhase`, `CascadeShadows`, `DebugNY`, `DebugOverlay`, `DefLight`, `EntitySelect`, `FX`, `HeightMap`, `LensDistortion`, `MirrorReflection`, `ParaboloidShadows`, `PedDamage`, `Radar`, `Reflection`, `Std`, `StdNY`, `TreeImposters`, `Water`, `WaterReflection` (38) | Phase implementations: G-buffer scene pass, deferred lighting, water surface/reflection, mirrors, CSM, paraboloid shadow/reflection, post FX, HUD/radar | `RenderPhases/RenderPhaseDefLight.cpp:721` (G-buffer draw list), `RenderPhases/RenderPhaseWater.cpp:50`, `RenderPhases/RenderPhaseMirrorReflection.cpp:927`, `RenderPhases/RenderPhaseCascadeShadows.h:22` (`CSM_TYPE_CASCADES, // 4 cascades`) |
| `Shadows/` | `CascadeShadowsDebug.cpp/.h`, `CascadeShadowsDepthHistogram.inl`, `ParaboloidShadows.cpp/.h`, `ParaboloidShadows_shared.h`, `ShadowTypes.h`, `Shadows.h`, `ShadowsPrivate.cpp/.h` (10) | CSM debug/statistics, paraboloid shadow (8 phases), shadow type enums | `Shadows/ParaboloidShadows.h:17` (`MAX_PARABOLOID_SHADOW_RENDERPHASES 8`), `ShadowTypes.h:9` (`SHADOWTYPE_CASCADE_SHADOWS`) |
| `SPU/` | `LightClassificationSPU.cpp/.pmjob`, `ProcessSPU.cpp/.pmjob`, `RainUpdateSPU.cpp/.pmjob`, `SSAOSPU.cpp/.h/.pmjob` (9) | PS3 Cell jobs: SSAO at 640×360 tiles, light classification, rain update | `SPU/SSAOSPU.cpp:54-59` (`BUF_SSAO_WIDTH 640`, `BUF_SSAO_HEIGHT 360`), `:171` (`InternalProcessSSAO`) |
| `SpuPM/` | build/link bat + `SpuPmMgr.cpp/.h`, `SpuPmMgrSPU.cpp/.h`, `SpuPmMgr_pm.h`, `SpuPmMgr_pm_spursentry.cpp`, `spupmmgr_pm.cpp` (17) | SPU program manager host/SPU bridge (PS3 only) | `SpuPM/SpuPmMgr.cpp:238` (`CSpuPmMgr::Init`), `:415` (`PostProcessTexture`) |
| `tesselation/` | `barycentric.h`, `linear.h`, `linearDualEdges.h`, `progressiveLinear.h`, `progressiveLinear2.h`, `progressiveLinearDualEdges.h` (6) | CPU-side progressive tessellation index generators used by terrain/imposters | `tesselation/progressiveLinear.h:9` (`progressiveLinearTesselator`), `barycentric.h:4` |
| `Util/` | `RenderDocManager.cpp/.h`, `ShaderUtil.cpp/.h`, `ShmooFile.cpp/.h`, `Util.cpp/.h`, `renderdoc_app.h` (10) | Shader state/PIX tagging, bilateral upsample helper, shmoo binary shader cache | `Util/ShaderUtil.cpp:39` (`StartShader`), `:188` (`PerformUpsample`) |
| `Debug/` | `EntitySelect.cpp/.h`, `Raster.cpp/.h` (4) | Debug entity picking and software occlusion rasterizer | `Debug/Raster.cpp:750` (`CRaster::Init`) |

### 1.2 Top-level files (114) — grouped by subsystem

- **Frame/orchestration**: `Renderer.cpp/.h` (`Renderer.cpp:484 Init`, `:923 PreRender`), `renderThread.cpp` (`:338 CParaboloidShadow::Synchronise`), `RenderThread.h`, `renderListGroup.cpp` (`:42 RPTYPE_REFLECTION = 3`), `RenderListBuilder.cpp` (`:800 AddToDrawList`, `:2054 AddToDrawListShadowParticles`), `RenderSettings.cpp/.h`, `RenderTargetMgr.cpp/.h`, `rendertargets.cpp` (`:738` RT pool table), `occlusion.cpp`, `occlusionAsync.cpp`, `OcclusionQueries.cpp`, `zonecull.cpp`, `PostScan.cpp`, `SortPVSAsync.cpp/.frag`, `computeShader.cpp` (`:210 BlurCS`), `clip_stat.h`, `color.h`, `render_channel.h`.
- **Post FX**: `PostProcessFX.cpp/.h` (bloom/motion-blur/DOF variable block at `:421-438`), `PostProcessFXHelper.cpp/.h/.psc`, `PostFX_shared.h`, `AdaptiveDOF.cpp/.h` (`:231 Init`, `:460 ProcessAdaptiveDofGPU`), `MLAA.cpp/.h` (`:204 AntiAlias`), `LensArtefacts.cpp/.h/.psc` (`:375 Render`), `SSAO.cpp/.h/.psc`, `SSAO_shared.h`.
- **Water**: `Water.cpp/.h/.psc` (`:7969 UsePlanarReflection`, `:8042 SetReflectionRT`), `WaterSPU.cpp/.h/.job`, `WaterSPU_job.cpp`, `River.cpp/.h` (`:163 SetWaterPassGlobalVars`), `newWater.cpp` (2-D constrained triangulation for water polys, `:147 triangulate_single_polygon`), `waterdefines.h`, `WaterUpdateDynamicCommon.h`.
- **Sky/world/vegetation**: `HorizonObjects.cpp/.h` (`:173 Render`), `TreeImposters.cpp/.h` (`:131 Init`, `:652 RenderTreeImposter`), `UseTreeImposters.h`, `PlantsGrassRenderer.cpp/.h/.psc` (`:257 DrawTriPlants`), `PlantsGrassRendererSPU.cpp/.h/.job/_job.cpp`, `PlantsGrassRendererSwitches.h`, `PlantsMgr.cpp/.h`, `PlantsMgrUpdateCommon.h`, `PlantsMgrUpdateSPU.cpp/.job/_job.cpp`, `TerrainEdgeInfo.cpp/.h`, `TerrainTessellation.psc` (`:6 TerrainTessellationParameters`).
- **Reflection/decals/sprites/UI**: `Mirrors.cpp/.h` (`:91 ms_pRenderTarget`, `:746 BuildRenderList` in phase), `sprite2d.cpp/.h` (`:460 CS_CORONA_PARABOLOID_REFLECTION`), `UI3DDrawManager.cpp/.h/.psc`, `GtaDrawable.cpp/.h`, `MeshBlendManager.cpp/.h`, `ApplyDamage.cpp/.h`, `ScreenshotManager.cpp/.h`, `ProcessLightOcclusionAsync.cpp/.frag/_frag.cpp`, `ProcessLodLightVisibility.cpp/.frag/_frag.cpp`, `HierarchyIds.h`, `psnvramrendertargettotal.h`, `makefile.txt`.

---

## §2 Effects catalogue

Legend for verdict: **HAVE** (ours ≥ V) / **PORT** (feasible on SM3.0) / **APPROX** (only an approximation is possible) / **SKIP-N/A** (N/A or not worth it) / **IMPOSSIBLE** (hardware/API).

| # | V feature | V impl (file:line) | Algorithm | Inputs | SM3.0 verdict |
|---|---|---|---|---|---|
| 1 | **Deferred G-buffer layout** | `Deferred/DeferredConfig.h:23-39` | 4 MRT: RT0 albedo+SSA, RT1 normal+twiddle, RT2 diffuse/spec-mix + spec exponent + fresnel + shadow, RT3 natural/artificial ambient + emissive + skin; depth; stencil; EQAA fragment masks | material textures, normals, ambient terms | **APPROX**: ≤4 MRT is D3D9-legal, but our RW 3.6 callbacks never bind multiple RTs (no MRT path is wired) → keep forward+, no G-buffer. |
| 2 | **Light types** | `Lights/LightCommon.h:15-23` | point(1), spot(2), capsule(4), directional(8), AO volume(16); `LIGHT_PARAM_COUNT 14` (`:27`) | light entity data | HAVE (ours: point/spot atlas, `forwardplus.cpp:14` 256 lights). Capsule is PORT. |
| 3 | **Deferred light pass permutation** | `Deferred/DeferredConfig.h:148-174` (`eLightPass`), `Deferred/DeferredLighting.cpp:1462 CalculatePass` | ~20 technique permutations: shadow/interior/exterior/texture/filler/stencil | pass key from light + material | N/A (not deferred); ours evaluates BRDF per light in forward. |
| 4 | **Tiled light assignment (V's "clustered")** | `Lights/TiledLighting.cpp:63,355,512,627`; `Lights/LightCulling.h:18`; `Deferred/DeferredLighting.cpp:1765` | 16×16-pixel tiles; CPU/SPU builds a **classification texture** of per-tile light indices, light lists in a buffer; no z-slices → **tiled, not clustered** | depth min/max, light AABBs/cones | HAVE-equivalent. Ours: `src/render/forwardplus.cpp:11-16` (16 px, 128×128 tiles, 256 lights, 16/tile), tile index in `s5`, atlas `s6/s7`. |
| 5 | **Clustered / forward+ (3-D clusters)** | not found | V is 2-D tiled deferred | — | N/A. Our forward+ already exceeds V's 2-D tiling. |
| 6 | **Cascade shadow maps (CSM)** | `RenderPhases/RenderPhaseCascadeShadows.h:22` (4 cascades); `shader_source/Lighting/Shadows/cascadeshadows_common.fxh:34-42` (1024²; VS 512²; mini 512²); `:54` HW-PCF only on SM4+; `:55` soft filtering; `cascadeshadows.fxh:108` slice packing | 4 cascades in one atlas, cascade bounds in shared consts `c50+` (`cascadeshadows_common.fxh:82-93`), depth-pass then PCF/soft filter in receive | sun matrix, split distances, depth | **PORT** (see §4.1). No `c223` problem; 9 taps. |
| 7 | **Local/spot shadows** | `Shadows/ParaboloidShadows.h:17,54`; `RenderPhases/RenderPhaseParaboloidShadows.cpp:43` | Up to 8 dual-paraboloid shadow maps for spot/point lights, cached static entries | light cone, occluders | **APPROX**: port as a single cube/dual-paraboloid depth map for the 2 strongest local lights, or skip. |
| 8 | **Contact shadows** | not found | — | — | **PORT as improvement** (SSAO-derived, §4.4). |
| 9 | **SSAO / HDAO / HBAO** | `SSAO.h:46-64` (`ssaotechnique_qsssao/cpssao/pmssao/cpqsmix/hdao/hdao2/hbao`), `SSAO.cpp`, `shader_source/Lighting/AO/SSAO.fx` | Depth-only horizon/hemisphere AO at reduced res, bilateral upsample (`Util/ShaderUtil.cpp:188`) | depth, normals | **HAVE**. Ours: `shaders/ps/SSAO.hlsl:124-171` (16-sample hemisphere), quarter-res + temporal + bilateral (`src/render/postfx_ssao.cpp:706`). |
| 10 | **Material model / "PBR"** | `Lighting/lighting_common.fxh:361` (`calculateBlinnPhong`), `:696-699` (Blinn-Phong + `specNormalisation`), `Lighting/lighting.fxh:1609-1613`; `Lighting/importance_sampling.fxh:229` | **Not GGX, not PBR**: Blinn-Phong `pow(NdotH, specExp)` + Schlick fresnel `fresnelSlick` (`lighting.fxh:760-764`), spec exponent remapped at `lighting_common.fxh:197-198` (500→558 knee). No split-sum. | spec map (exp/fresnel packed), fresnel, spec intensity | **N/A / HAVE**. Ours is true GGX+Smith+Schlick (`shaders/include/PBR_Common.hlsl:15,27,37`). Do **not** port V's BRDF. |
| 11 | **IBL / probes** | `RenderPhases/RenderPhaseReflection.cpp:800-802` (`LightProbe::Capture`), `:353` (`CreateCubeMapRenderTarget`); `Lighting/importance_sampling.fxh:190,229,332` | Cubemap probe capture + **importance-sampled Phong/Blinn-Phong** prefilter; dual-paraboloid env maps for everything else | cubemap, spec exponent, fresnel | **PORT** as prefiltered mip chain + gloss→LOD (no split-sum textures needed). |
| 12 | **Reflection probes + SSR** | probes: above; paraboloid: `RenderPhaseReflection.cpp:756,882,997`; SSR: not found | Dual-paraboloid + cube probe; **no screen-space reflections in V** | probe capture, reflection vector | Probes **PORT**; SSR **N/A** (V itself has none; ours has an SSR-ish water copy `src/render/waterPipe.cpp:86-89`). |
| 13 | **Planar reflections** | `RenderPhases/RenderPhaseMirrorReflection.cpp:746,927`; `Water.cpp:7969,8042`; `RenderPhases/RenderPhaseWaterReflection.cpp` | Mirror camera renders scene to a planar RT; water reuses mirror RT or its own (`River.cpp:163-187`) | mirror plane, clip plane | **PORT** (§4.3); ours only has SA's mirror (`src/render/mirrorPass.cpp:2-6`) and no water planar RT. |
| 14 | **Water — waves** | `Water/water_river_wave.fxh:110-124` | 2 deep-water waves: `waveLength = 9.81*T²/(2π)`, `phase = k*dot(p,dir) - ωt`, `p.z -= r*cos(phase)`, derivative `k*r*sin(phase)` + noise (`DWaveNoise*` `:67-103`) | period, amplitude, direction, wind | **PORT** (VS only; 2 waves ≈ 30 VS instr). Ours never displaces geometry (`src/render/waterPipe.cpp:22-24`). |
| 15 | **Water — refraction** | `Water/water_common.fxh:735-759` | `refract(V,N,RefractionIndex)`, project offset, two screen taps with depth reject (`:753-754`), blend with paraboloid refraction (`:762-766`) | refraction RT + low-res depth (`s11/s12`) | **PORT**; ours already has refraction depth RT (`src/render/waterPipe.cpp:628-643`) and `Water_Parallax.hlsl:114-134`. |
| 16 | **Water — foam** | `Water/water.fx:500-510` | `WetSampler` ripple foam × `StaticFoamSampler` detail, weighted (`FOAMWEIGHT 0.65`, `FOAMSCALE 1.0`, `water_common.fxh:24-25`), screen-depth fade | wet/foam textures, depth | **PARTIAL/HAVE** (waterPipe has foam scale/shore params, `:148-162`). |
| 17 | **Water — caustics** | `Water/waterTex.fx:484-566` | Caustics texture sampled through bump-offset (`CAUSTICSDEPTH 7`), 5-tap cross, additive `screenColor * causticIntensity` (`:695-698`) | caustics texture, bump | **PORT** as a projected caustic texture on shallow water (4-5 taps). |
| 18 | **Water — lighting** | `Water.cpp:139` (`WATER_TILED_LIGHTING 1`), `Water.cpp:9591-9666` (paraboloid water rect) | Water participates in tiled lighting + planar/paraboloid reflection | light list | **PARTIAL** (ours: timecycle only, `Water_Parallax.hlsl` c12/c13). |
| 19 | **Sky / atmosphere** | `Sky/sky_system.fx:556` (`calcAtmosphereColor`), `:542` Mie phase; `:869-938` composite | **Not Preetham/Hosek**: artist gradient `azimuthColor → zenithTransitionColor → zenithColor` by `abs(viewDir.z)`, plus sun/Mie phase and cloud layers | timecycle sky colours, sun dir, Mie constants | **HAVE-equivalent** (ours: analytic gradient `shaders/ps/IBL_SkyCloud.hlsl:74-83`, DynamicSky). No need to port scattering. |
| 20 | **Clouds / volumetric clouds** | `Sky/Clouds/Clouds.fxh:536` (`CalculateMieScattering`), `:665` density/normal, `:1097` `PSCloudCommon`; shaders `clouds_billboard_*`, `clouds_anim*`, `clouds_fog` | **Billboard/2-D density-map impostors** with Henyey-Greenstein/Mie phase + vertical scattering; no raymarch | density map, sun colour, camera ray | **HAVE/APPROX** (ours: fbm `shaders/ps/DynamicSky.hlsl:60-93`, IBL_SkyCloud). |
| 21 | **Volumetric fog / light shafts** | `VFX/vfx_fogvolume.fx:182` (`1-exp(-density*dist)`), `:299`; `Lighting/deferred_volume.fx:83` shaft-quad expansion; `Lights/LightCommon.h:45-56` (8 density types); `Deferred/DeferredConfig.h:58` HQ volumetric | Analytic fog volume + screen-aligned shaft quads expanded in VS, shadow-receiving (`SHADOW_RECEIVING 1`) | volume transform, depth, shadow map | **PORT** shaft quads (VS expansion, no GS needed); ours has screen-space godrays only (`src/render/postfx.cpp:4129`). |
| 22 | **Bloom** | `PostFX/postfx.fx:314-319` (`BloomParams`: threshold/intensity/thresholdOO/intensity-mult), `:73` (`BLOOM_ADJUST 0.13`); `PostProcessFX.cpp:426-438` (large/med/small blend), `:909-913` (half/quarter RTs) | Threshold + multi-level blur pyramid, 3 blur sizes composited | HDR colour | **HAVE** (ours: extract/down/up/composite `src/render/postfx_newfx.cpp:629`, `shaders/ps/Bloom_*.hlsl`). |
| 23 | **Tonemap curve** | `PostFX/postfx.fx:2881-2891` (`fullFilmicTonemap`, Hable/Uncharted-2 rational A..F), `:2894-2913` (`getFilmicParams` lerps Bright↔Dark curve pairs by exposure), `:3574`, `:3578` (`pow(...,0.4545)`) | Hable/Uncharted-2 filmic, per-exposure curve blend, then gamma | HDR colour, exposure | **HAVE**. Ours is the same Hable curve with adaptive toe (`shaders/ps/TonemapPass.hlsl:55-64`). IV: **not found** in this tree; our `DrawIVGrade` (`src/render/postfx_colourfilter.cpp:1355`) is an LDR grade, **not** the V/IV filmic curve — so "IV same curve?" cannot be confirmed from source; V's curve == ours. |
| 24 | **Motion blur** | `PostFX/postfx.fx:340-345` (`DirectionalMotionBlurParams`, `MB_NUM_SAMPLES`), `:742-744` sampler | Directional blur along per-pixel velocity, N iterations | velocity, depth | **HAVE/PARTIAL** (ours: `src/render/postfx_motionblur.cpp`, burnout-style `shaders/ps/MotionBlur_Burnout.hlsl`). |
| 25 | **Depth of Field** | `AdaptiveDOF.cpp:231,282,302,460`; `PostFX/postfx.fx:285-312`; `PostFX/adaptiveDof.fx`, `dofCompute.fx`, `dofDiffusion.fx` | CPU-readback adaptive CoC + bokeh + diffusion | depth, exposure, focus grid | **HAVE** (ours: CoC/bokeh/tent/combine `src/render/postfx_newfx.cpp:552`). |
| 26 | **TAA** | not found | V ships MSAA + FXAA3/MLAA, **no TAA** | — | **HAVE-better**: ours has SMAA 1x + temporal history + velocity (`src/render/postfx_smaa.cpp:286-313,446`). |
| 27 | **MSAA / SSAA** | `Deferred/GBuffer.h:25` (`GBUFFER_MSAA`), `Deferred/DeferredConfig.h:11` (`MSAA_EDGE_PASS`); `PostFX/postfx.fx:45-47` FXAA descoping | MSAA on G-buffer + edge pass; FXAA3 for SM<4 | — | **N/A** on RW 3.6 D3D9 (no MRT MSAA path); keep SMAA. |
| 28 | **Lens flare / artefacts** | `PostProcessFX.cpp:127` (`LensFlare.h`), `LensArtefacts.cpp:375,435,498,543` | Textured lens-artefact layers (ghosts/streaks) with occlusion + directional blur | flare textures, depth occlusion | **PORT** (§4.6), 6-8 taps. |
| 29 | **Particle lighting / shadows** | `Entities/PtFxDrawHandler.cpp:20`; `RenderPhaseCascadeShadows.h:85` (`GetShadowMap` for `RMPTFX_USE_PARTICLE_SHADOWS`); `VFX/rmptfx_litsprite.fx` | Lit sprites sample sun + CSM; shaded particles cast via CSM | light, shadow map | **APPROX** (ours has no lit-particle path; SA coronas only). |
| 30 | **Decals** | `Deferred/DeferredLighting.cpp:114-119` (`DECAL_BLEND_COLOUR/SPEC/AMB`, `DECAL_WRITE_DEPTH/NORM`), `Deferred/GBuffer.h:29` (`CREATE_TARGETS_FOR_DYNAMIC_DECALS`), `VFX/vfx_decal*.fx` | Deferred decals write albedo/normal/spec channels; dynamic decal RT | decal texture, projection | **MISSING**; forward decals are possible (§4.5). |
| 31 | **Vehicle paint / dirt / damage** | `Vehicles/vehicle_paint1.fx:9,17-25,38` (`USE_DIRT_LAYER`, `USE_DIRT_UV_LAYER`, `USE_SECOND_SPECULAR_LAYER`, `USE_VEHICLE_DAMAGE`, `USE_PAINT_RAMP`), `Vehicles/vehicle_damage.fxh:1-13`, `vehicle_common.fxh:343-387` | Paint ramp + 2 specular layers + separate dirt UV channel + damage normal/blend | 3-4 textures + vertex CPV | **PARTIAL** (ours: clearcoat/GGX/wet `VehiclePBR_Modern.hlsl:509-670`; no dirt/damage layers). |
| 32 | **Glass** | `Entities/VehicleGlassDrawHandler.cpp:20`; `Custom/Glass/*` (`glass_pv`, `glass_pv_env`, `glass_emissive(night)`, `glass_normal_spec_reflect`, `glass_breakable`) | Env-reflective glass with PV (per-vertex) env, emissive night, cracks | env map, tint, break state | **PARTIAL/HAVE** (`shaders/ps/Glass_Vehicle.hlsl:24-33`; lacks breakable/crack layers). |
| 33 | **Emissive / night lights** | `Megashader/emissivenight.fx`, `emissive*.fx`; `DeferredConfig.h:28` (RT3.B = emissive) | Emissive written to G-buffer and added after lighting | emissive texture/timecycle | **PARTIAL** (ours: SA extra night colours `buildingPipe.cpp:2734-2933`; no emissive buffer). |
| 34 | **Rain / wetness** | `shader_source/common.fxh:1124-1127` (`gDynamicBakesAndWetness`, `gWetnessBlend` c80), `IsWettable` `:1753`; `SPU/RainUpdateSPU.cpp`; `Megashader/Puddle.fxh`; `Water/water.fx:502` wet sampler | Global wetness blend lowers diffuse/raises gloss, puddle mask, rain collision heightmap, water ripple bump RT (`rendertargets.cpp:752`) | wetness, puddle mask, rain texture | **PARTIAL** (ours: `iblAmbient.z` wet-road term `VehiclePBR_Modern.hlsl:509-519`, waterdrops `neoWaterdrops.cpp`; no puddles/ripples). |
| 35 | **Wind / vegetation** | `common.fxh:686` (`SfxWindSampler3D s12`); `PlantsGrassRenderer.cpp:257,396-411` (wind bend matrix); `trees_windfuncs.fxh` | Vertex wind sway with gust table, per-instance bend | wind vector/table | **HAVE** (`src/render/treeWind.cpp:6-7,101-115`, `PC_GrassRenderer.cpp:396-411`). |
| 36 | **Tessellation** | `TerrainTessellation.psc:6`; `tesselation/progressiveLinear.h:9`; `RenderPhases/RenderPhaseDefLight.cpp:352` (terrain tessellation switch) | HW hull/domain tessellation + progressive CPU LOD | control points, edge lengths | **IMPOSSIBLE on SM3.0** (no HS/DS). **SKIP**; use normal maps + parallax. |
| 37 | **Async light occlusion/culling** | `ProcessLightOcclusionAsync.cpp/.frag`, `ProcessLodLightVisibility.cpp/.frag`, `Lights/LightsVisibilitySort.frag` | Compute/async occlusion and light visibility sorting | depth, light bounds | **N/A** (compute); ours CPU-culls (`forwardplus.cpp:110`). |
| 38 | **Height fog** | `VFX/vfx_fogvolume.fx` + `RenderPhaseHeightMap.cpp`; `PostFX/postfx.fx` | Height-based analytic fog | depth, height | **HAVE** (`shaders/ps/HeightFog.hlsl`, `postfx.cpp:3874`). |

---

## §3 Gap matrix (ranked by visual impact in "Modern")

| Rank | V feature | V impl | Ours (file:line) | Status |
|---|---|---|---|---|
| 1 | CSM / sun shadows | `RenderPhaseCascadeShadows.h:22`; `cascadeshadows_common.fxh:34-42` | no shadow sampler in PBR shaders; only cloud shadow `c4` (`VehiclePBR_Modern.hlsl:33`) and SSAO (`postfx_ssao.cpp:706`) | **MISSING** |
| 2 | Planar water reflection (mirror-camera RT) | `RenderPhaseMirrorReflection.cpp:927`; `Water.cpp:7969` | mirror-only `src/render/mirrorPass.cpp:2-6`; water uses flipped-scene copy `src/render/waterPipe.cpp:86-89` | **PARTIAL** |
| 3 | Local reflection probes / prefiltered IBL | `RenderPhaseReflection.cpp:353,800`; `importance_sampling.fxh:190-382` | single spherical env map `src/render/envmap.cpp:14-22,92`; global IBL texture `s3` `buildingPipe.cpp:2137` | **PARTIAL** |
| 4 | Water geometry waves + caustics | `water_river_wave.fxh:110-124`; `waterTex.fx:484-566` | no displacement `waterPipe.cpp:22-24`; no caustics | **PARTIAL** |
| 5 | Deferred decals | `DeferredLighting.cpp:114-119`; `GBuffer.h:29` | only game alpha decals `buildingPipe.cpp:1697` | **MISSING** |
| 6 | Volumetric light shafts | `Lighting/deferred_volume.fx:83`; `LightCommon.h:45-56` | screen-space godrays `postfx.cpp:4129` | **PARTIAL** |
| 7 | Particle lighting/shadows | `PtFxDrawHandler.cpp:20`; CSM particle shadows `RenderPhaseCascadeShadows.h:85` | none (coronas only) | **MISSING** |
| 8 | Vehicle dirt/damage layers | `vehicle_paint1.fx:21-25`; `vehicle_damage.fxh` | wet/clearcoat only `VehiclePBR_Modern.hlsl:509`; rubber dirt `main_rubber` | **PARTIAL** |
| 9 | Emissive/night-light buffer | `DeferredConfig.h:28`; `emissivenight.fx` | SA night vertex colours `buildingPipe.cpp:2734-2933` | **PARTIAL** |
| 10 | Puddles / rain ripples | `common.fxh:1124-1127`; `Megashader/Puddle.fxh` | wet-road term `VehiclePBR_Modern.hlsl:509-519` | **PARTIAL** |
| 11 | Lens flare/artefacts | `LensArtefacts.cpp:375` | none | **MISSING** |
| 12 | Contact shadows | not found (V has none) | none | **MISSING** (opportunity) |
| 13 | Tiled light density | `TiledLighting.cpp:63` 16 px | 16 px / 16 lights/tile `forwardplus.cpp:11-15` | **HAVE** |
| 14 | SSAO/HBAO | `SSAO.h:46-64` | quarter-res + temporal `postfx_ssao.cpp:706` | **HAVE** |
| 15 | Bloom | `PostFX/postfx.fx:314-319` | pyramid `postfx_newfx.cpp:629` | **HAVE** |
| 16 | Tonemap | `postfx.fx:2881-2913` | Hable `TonemapPass.hlsl:55-64` | **HAVE** |
| 17 | DoF | `AdaptiveDOF.cpp:460` | CoC/bokeh `postfx_newfx.cpp:552` | **HAVE** |
| 18 | Motion blur | `postfx.fx:340-345` | burnout blur `postfx_motionblur.cpp` | **HAVE/PARTIAL** |
| 19 | AA | MSAA+FXAA3, no TAA | SMAA temporal `postfx_smaa.cpp:286-313` | **HAVE-better** |
| 20 | Sky | `sky_system.fx:556` gradient+Mie | gradient+fbm `IBL_SkyCloud.hlsl:74-83` | **HAVE** |
| 21 | Grass/tree wind | `PlantsGrassRenderer.cpp:396-411` | `treeWind.cpp:6-7`, `PC_GrassRenderer.cpp:396-411` | **HAVE** |
| 22 | Tessellation | `TerrainTessellation.psc:6` | none | **IMPOSSIBLE / SKIP** |
| 23 | G-buffer / deferred | `DeferredConfig.h:23-39` | forward+ (`forwardplus.cpp`) | **N/A** (by design) |

---

## §4 Port plans (only MISSING/PARTIAL worth doing)

Constraints used below: `ps_3_0`/`vs_3_0` = 512 arithmetic slots, 32 temp registers,
16 samplers, no integer ops, no UAV, no GS/HS/DS, dynamic branching allowed but
keep warp-uniform. Existing PS consts used by `VehiclePBR_Modern`: `c0-c29`,
`c44-c48`; `c30-c43` and `c49+` are free (`VehiclePBR_Modern.hlsl:29-83`). VS used
through `c36` (AGENTS contract); `c37+` free.

### 4.1 Cascaded shadow maps — highest value
- **Algorithm**: 4 cascades, depth-only render (front-face cull), 1 atlas
  `2048×2048` = 4 × `1024×512` (V: `1024²` per cascade, `cascadeshadows_common.fxh:34-42`;
  we halve height to fit D3D9 texture limits and keep 1024 px horizontal).
  Sample one cascade (branch on `cascadeSplits`), 3×3 PCF with a 4-tap rotated
  pattern when `modernCsmQuality<3`, `z` slope bias + normal-offset bias.
- **Target**: `VehiclePBR_Modern.hlsl` `main`/`main_building`/`main_rubber` (all
  entry points read the same `c49+` block); C++: new `src/render/csmPass.cpp`
  (create atlas, render casters via existing entity list, set matrices) called
  from `RenderScene_before` next to `src/core/main.cpp:3140`.
- **Budget**: 9 taps, ~45-60 instructions (2 branch blocks, 1 matrix multiply).
- **Registers**: `c49` `csmParams {enable, strength, texel, depthBias}`;
  `c50` `cascadeSplits.xyzw` (view-space far distances); `c51` `csmFilter {radius,
  normalBias, pcfTaps, contactEnable}`; `c64-c79` `cascadeMatrix[4]` (4 consts
  each). Sampler **`s9`** (atlas, POINT/CLAMP). No collisions: `c64+` unused,
  `s9` free.
- **CSO/IDR**: recompile `IDR_VEHICLEPBR_MODERN 203` and `IDR_BUILDINGPBRPS 239`
  (same CSOs, new constants/sampler); no new IDR.
- **INI**: `modernCsmEnable` (default 1), `modernCsmQuality` 0-3, `modernCsmDistance`.
- **Risk**: HIGH (new caster pass + state restore + stencil ownership; SA's own
  stencil shadows must not be disturbed — `buildingPipe.cpp:513` documents the
  ownership rule). Fallback: `modernCsmEnable=0` → shader compiles the no-sample
  branch (uniform branch, no cost).

### 4.2 Prefiltered reflection probes / IBL
- **Algorithm**: keep V's importance-sampled env idea but offline: C++ builds a
  64² cubemap + 7 mips from the scene capture (`src/render/envmap.cpp:92`), then
  `D3DXFilterTexture`-style box prefilter per mip; shader picks LOD =
  `roughness * (mips-1)` (V approximates with `ImportanceSampleBlinnPhong`,
  `importance_sampling.fxh:229-382`). Blend probe with existing `s3` by
  `GetEnvmapFresnel` (`PBR_Common.hlsl:110`) and `envLodBias`.
- **Target**: `shaders/ps/VehiclePBR_Modern.hlsl:564-670` (env block),
  `src/render/envmap.cpp`.
- **Budget**: 1 cubemap tap + LOD math ≈ 12-18 instr; 0 extra taps.
- **Registers**: `c52` `probeParams {mode, envLodBias, planarStrength, probeWeight}`.
  Sampler **`s10`** (`samplerCUBE`, LINEAR/CLAMP) — `s3` stays the 2-D IBL sky
  (`VehiclePBR_Modern.hlsl:21`), so one new sampler is required.
- **CSO/IDR**: recompile 203/239 only.
- **INI**: `modernProbeStrength`, `modernProbeMips`.
- **Risk**: LOW (visual only, no new pass). D3D9 supports cubemap mip LOD sampling.

### 4.3 Planar water reflection
- **Algorithm**: V renders the scene once from a mirrored camera into
  `m_ReflectionRT` (`Water.cpp:8042`, `RenderPhaseMirrorReflection.cpp:927`), then
  water samples it projectively (`water_common.fxh:770-797`). On SM3.0: render at
  half res, oblique near-plane clip, skip translucent/particle buckets; water PS
  does one `tex2Dproj` and Fresnel-lerps to the analytic sky tint when the sample
  is invalid (depth compare against `s4`).
- **Target**: new `src/render/planarRefl.cpp` (mirror camera + RT + clip plane),
  `shaders/ps/Water_Parallax.hlsl` (existing `s3` is the flipped-scene copy;
  repurpose it or add `s5`), `src/render/waterPipe.cpp`.
- **Budget**: 2 taps (projective + depth reject) ≈ 15 instr.
- **Registers**: reuse `c11.w` (`qualityParams`, `Water_Parallax.hlsl:130`) as
  planar strength; `c9/c10` sky tint already present. Sampler **`s5`**
  (reflection RT, LINEAR/CLAMP) — the water PS only uses `s0-s3`
  (`Water_Parallax.hlsl:114-117`), so this is collision-free *within the water
  shader namespace* (sampler slots are per-draw state).
- **CSO/IDR**: recompile `IDR_WATER_PARALLAX 201`, `IDR_WATER_V 242`; no new IDR.
- **INI**: `modernPlanarRefl` (default 1), `modernPlanarReflRes` 0.25-1.0.
- **Risk**: MED-HIGH (recursion guard, clip plane, state restore).

### 4.4 Contact shadows (SSAO-derived, not in V)
- **Algorithm**: 6-8 step depth ray-march in screen space toward the sun, in the
  SSAO resolve; multiply into AO with distance fade. V has no contact shadows
  ("not found"), so this is a Modern+ upgrade, not parity.
- **Target**: `shaders/ps/SSAO.hlsl` (after `:171` occlusion loop) and
  `src/render/postfx_ssao.cpp:706`.
- **Budget**: 8 taps, ~40 instr.
- **Registers**: `c57` `contactParams {radius, thickness, strength, steps}`
  (SSAO currently only uses `c0`, `SSAO.hlsl:22`).
- **CSO/IDR**: recompile `IDR_SSAOPS 120`.
- **INI**: `modernContactShadow` (default 0 until CSM lands), `modernContactStrength`.
- **Risk**: LOW.

### 4.5 Forward decals (V is deferred)
- **Algorithm**: no G-buffer, so project decals in a small forward pass immediately
  after opaque: pitch-aligned box, `tex2Dproj` albedo/normal, alpha test, write
  nothing to depth; PBR shaders receive the decal through a half-res "decal
  albedo/normal" RT (add at `s11` in a dedicated decal-composite shader, or skip
  the RT and draw decals as regular translucent meshes).
- **Target**: `src/render/buildingPipe.cpp` callback (after opaque list),
  new `shaders/ps/Decal_Forward.hlsl`, `IDR_DECAL_FORWARD 280`.
- **Budget**: 25 instr, 2 taps.
- **Registers**: `c53` `decalParams {mode, normalStrength, fadeStart, fadeEnd}`.
- **Risk**: MED (sorting/blend). **Recommendation: defer** — V's deferred decals
  cannot be replicated faithfully without a G-buffer; low visual delta vs CSM.

### 4.6 Lens flare / artefacts
- **Algorithm**: V draws textured artefact layers with depth occlusion
  (`LensArtefacts.cpp:375-591`). Ours: screen-space ghost chain along the
  sun-centre vector, 5 ghosts + 1 streak, depth-test against `s2`, exposure fade.
- **Target**: `shaders/ps/LensFlare.hlsl`, `src/render/postfx_newfx.cpp` (new
  `DrawLensFlare`, after `DrawGodRays` `postfx.cpp:1946`).
- **Budget**: 6 taps, ~30 instr.
- **Registers**: `c0` in the flare shader (own namespace; the `c56` in §5.1 is
  only a reservation if it is folded into TonemapPass). Sample scene `s0` + depth `s1`.
- **CSO/IDR**: `IDR_LENSFLARE 280`.
- **INI**: `modernLensFlare` (default 0), `modernLensFlareIntensity`.
- **Risk**: LOW.

### 4.7 Water waves + caustics
- **Algorithm**: 2 Gerstner/cosine waves in VS (V formula,
  `water_river_wave.fxh:110-124`), normal from analytic derivative, no CPU
  displacement; caustics = projected caustic texture × depth fade (V:
  `waterTex.fx:484-566`) sampled in the PS for shallow bodies only.
- **Target**: `shaders/vs/Water_VS.hlsl`, `shaders/ps/Water_Parallax.hlsl`.
- **Budget**: VS +30, PS +10 (2 extra taps).
- **Registers (VS)**: `c37` `wave0 {amp, period, dirX, dirZ}`, `c38` `wave1
  {amp, period, dirX, dirZ}`, `c39` `waveTime {t, windScale, fade, enable}`.
  No VS collision (VS c0-c36 per AGENTS; `c37+` free). PS caustics: `c16`
  (free in water PS; `c0-c15` used, `Water_Parallax.hlsl:119-134`), sampler `s4`
  (free in the water namespace — water PS only uses `s0-s3`).
- **CSO/IDR**: recompile `IDR_WATER_V 242`, `IDR_WATER_PARALLAX 201`.
- **INI**: `modernWaveDisp` (default 1), `modernCaustics` (default 1).
- **Risk**: LOW-MED (waves change silhouette; keep amplitude ≤0.5 m).

### 4.8 Vehicle dirt/damage + glass cracks
- **Algorithm**: V samples a second UV dirt layer and a damage normal/blend
  (`vehicle_paint1.fx:21-25`). Reuse the existing mask texture **`s2`**
  (`maskTex`, `VehiclePBR_Modern.hlsl:20`) sampled with the dirt UV set: `.b` =
  dirt mask, `.a` = damage mask; multiply albedo by dirt, lerp normal by damage
  mask; glass: crack mask + broken UV offset.
- **Target**: `shaders/ps/VehiclePBR_Modern.hlsl` (`:509` wet block) and
  `shaders/ps/Glass_Vehicle.hlsl:27-33`.
- **Budget**: 3-4 taps, ~20 instr.
- **Registers**: `c54` `dirtParams {dirtAmount, dirtUVScale, damageAmount, damageBias}`.
  No new sampler (uses `s2` mask channels).
- **CSO/IDR**: recompile 203/239 + `IDR_GLASS_VEHICLE 204`.
- **INI**: `modernVehDirt`, `modernVehDamage`.
- **Risk**: LOW.

### Features explicitly **IMPOSSIBLE** on SM3.0 and the approximation we must use

| Impossible | Why | Our approximation |
|---|---|---|
| HW tessellation (terrain/vehicle) | `ps_3_0`/`vs_3_0` have no hull/domain stages; V uses `TerrainTessellation.psc:6` + `tesselation/*` | **SKIP**. Normal maps + parallax (we already have `Water_Parallax`-style POM for buildings); no geometry LOD change. |
| Compute light classification / async occlusion | D3D9 has no compute/UAV/typed buffers; V's `tiled_lighting.fx:135` uses `RWTexture2D` | CPU tile build like ours (`forwardplus.cpp:291-370`); keep 16 px tiles. |
| Deferred G-buffer + EQAA/stencil target | >4 MRT, stencil+MSAA resolve, integer fragment masks (`DeferredConfig.h:33-38`) | Forward+; SSAO in post; no deferred decals. |
| MSAA on MRT / alpha-to-coverage | RW 3.6 raster API has no MSAA (`src/render/envmap.cpp:32-38`) | SMAA temporal (already) + env-map supersampling (`reflectionMsaaLevel`). |
| Screen-space reflections | V has none (not found) | N/A — do not port. Our water flipped-scene copy is enough. |
| Full volumetric cloud raymarch | V does not raymarch (billboards, `Clouds.fxh:1097`) | fbm billboards (`DynamicSky.hlsl:60-93`). |

---

## §5 Register / constant contract for "Modern+"

### 5.1 Pixel shader (VehiclePBR_Modern + buildingPBR + Glass + water)

| Range | Owner | Contents |
|---|---|---|
| `c0-c4` | existing | `surfProps`, `fxParams`, `eyePos`, `iblParams`, `cloudShadow` (`VehiclePBR_Modern.hlsl:29-33`) |
| `c5-c18` | existing | `directCol` + `lightCol[6]` / `directDir` + `lightDir[6]` (`:34-37`) |
| `c19-c21` | existing | `matCol`, `iblAmbient`, (c20 shared with building) |
| `c22-c23` | **frozen** | `pipeUploadPBR` only (`pipelinecommon.cpp:997,1029-1030`) — never hand-upload |
| `c24` | existing | ambient + normalBuf flag |
| `c25-c29` | existing | view rows, skyParams, screenSize |
| `c44-c48` | existing | chrome, cluster, layers, bldEnv, nmParams |
| **`c49`** | **NEW** | `csmParams {enable, strength, texel, depthBias}` |
| **`c50`** | **NEW** | `cascadeSplits.xyzw` |
| **`c51`** | **NEW** | `csmFilter {pcfRadius, normalBias, taps, contactEnable}` |
| **`c52`** | **NEW** | `probeParams {mode, envLodBias, planarStrength, probeWeight}` |
| **`c53`** | **NEW** | `decalParams` (only if decals land; otherwise free) |
| **`c54`** | **NEW** | `dirtParams {dirtAmount, dirtUVScale, damageAmount, damageBias}` |
| **`c55`** | **NEW** | `contactParams {radius, thickness, strength, steps}` (SSAO only; SSAO uses `c0` today) |
| **`c56`** | **NEW** | `flareParams` (post pass; separate shader, can live at `c0`) |
| **`c57`** | **NEW** | `waveTime` shared PS-side wave normal modifier |
| **`c64-c79`** | **NEW** | `cascadeMatrix[4]` (4×`float4` each) |
| `c80-c223` | unused | reserve; do not hand out piecemeal |

### 5.2 Vertex shader (buildingPBRVS / vehiclePBRVS / Water_VS)

| Range | Owner | Contents |
|---|---|---|
| `c0-c3` | existing | WVP |
| `c4` | existing | ambient |
| `c5-c18` | existing | 7 lights (VS) |
| `c19-c20` | existing | material / surfProps |
| `c21-c23` | existing | wind (`treeWind.cpp:4-7`), c22/c23 wind intensity/ext |
| `c24-c27` | existing | world matrix |
| `c29` | existing | shader params (colour scale) |
| `c30-c31` | existing | day/night |
| `c32-c35` | existing | texture transform |
| `c36` | existing | eye position |
| **`c37-c39`** | **NEW** | `wave0/1` + `waveTime` (Water_VS) |
| **`c40-c43`** | **NEW** | reserved: CSM vertex-side shadow caster offset (not needed for receiving) |
| `c44+` | unused | free |

### 5.3 Samplers

| Sampler | Owner | Notes |
|---|---|---|
| `s0` | diffuse | existing |
| `s1` | env / reflection (2-D sphere map) | existing |
| `s2` | mask / depth in post | existing; also dirt+damage channels in `main_rubber`/paint (`VehiclePBR_Modern.hlsl:20`) |
| `s3` | IBL sky / water flipped scene | existing |
| `s4` | normal buffer / water depth | existing |
| `s5` | Forward+ tile index | existing (`forwardplus.cpp:27`) |
| `s6` | Forward+ light pos atlas | existing |
| `s7` | Forward+ light colour atlas | existing |
| `s8` | normal-map texture | existing (`VehiclePBR_Modern.hlsl:27`) |
| **`s9`** | **CSM atlas** | NEW, POINT/CLAMP |
| **`s10`** | **probe cubemap** (`samplerCUBE`) | NEW, LINEAR/CLAMP |
| **`s5`** (water PS) | **planar water reflection RT** | NEW in the water shader namespace; water PS only uses `s0-s3` (`Water_Parallax.hlsl:114-117`) |

### 5.4 Collision rules
1. `c22/c23` are write-only through `pipeUploadPBR()` (`pipelinecommon.cpp:992-1030`);
   any new feature must use `c49+`.
2. PS and VS constant spaces are distinct; VS `c37-c43` cannot collide with PS
   `c44-c48` even though the numbers overlap.
3. `Glass_Vehicle.hlsl` redefines `c22/c23` as `glassParams`/`lightParams`
   (`:29-30`) — the CSM block (`c49+`) is the only safe addition there.
4. `s9/s10` must be unbound on every non-Modern path (`buildingPipe.cpp:2571`
   unbinds `s3`; extend that teardown to `s9/s10` and to the water `s5`).
5. `IDR` budget: `280` decal-forward, `281` lens flare, `282` planar composite
   (`resources/resource.h:135` shows `279` = `IDR_TREEALPHA`; `:131` shows `277`
   = `IDR_MIRROR_COMPOSITE`).

### 5.5 Modern+ INI kill switches (all default off except where noted)
`modernCsmEnable=1`, `modernCsmQuality=2`, `modernCsmDistance=250`,
`modernProbeStrength=1.0`, `modernPlanarRefl=1`, `modernPlanarReflRes=0.5`,
`modernContactShadow=0`, `modernContactStrength=0.5`, `modernWaveDisp=1`,
`modernCaustics=1`, `modernVehDirt=1`, `modernVehDamage=1`, `modernLensFlare=0`.
Each toggle must short-circuit to the current uniform branch so
`modernCsmEnable=0` reproduces today's image bit-for-bit (no `if` in the inner
light loop; gate at the entry of each block).

---

## Appendix A — V things we deliberately do NOT copy

- `GBUFFER_REDUCED_COVERAGE`/EQAA fragment masks (`GBuffer.h:25-26,33-38`) — D3D9/RW
  has no fragment-mask resolve.
- 20-way `eLightPass` stencil permutation (`DeferredConfig.h:148-174`) — our
  forward+ BRDF is evaluated per light, no stencil volumes.
- HW-PCF (`CASCADE_SHADOWS_USE_HW_PCF_DX10`, `cascadeshadows_common.fxh:54`) —
  SM4+ only; use manual 3×3 PCF.
- `LIGHT_VOLUME_USE_LOW_RESOLUTION` (`LightCommon.h:29`) — no separate low-res
  volume RT in our path.
- `ImportanceSamplePhong` (`importance_sampling.fxh:190`) — replace with offline
  mip prefilter (SM3.0 can sample cubemap LODs but not integrate in PS).

## Appendix B — Evidence gaps ("not found")
- No TAA, no SSR, no contact shadows, no Preetham/Hosek atmosphere, no ocean
  FFT/spectrum (V uses 2 dispersion waves), no GTA IV tonemap source in this tree,
  no `GGX` anywhere in `shader_source/Lighting/*`; the only Smith reference is the
  **disabled** `FIS_BRDF_USE_SMITH_SHLICK_VIS 0` (`importance_sampling.fxh:40`).

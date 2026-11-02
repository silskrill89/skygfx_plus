# SkyGFX Plus expIV — Master Feature List

Consolidated from ALL project MDs (PLAN.md, TODO.md, WORKFLOW_PLAN.md, FUTURE_FEATURES.md,
CODEBASE_DOCUMENTATION.md, docs/plans/*, docs/baseline/*, memory-bank, .serena memories).
This is the single checklist future waves generate from.
Last consolidated: 2026-11-01. Repo: `E:\dev(dave)\skygfx_plus_expIV`. ASI target: `E:\games\gtasa_skygfx_plus\skygfx.asi`.

---

## 1. Status Legend

| Symbol | Meaning |
|---|---|
| ✅ | Implemented / landed (built + verified or in deployed tree) |
| 🟡 | In-progress / partially landed (design, code exists, not fully wired or verified) |
| 📋 | Planned (spec exists, not started or not built) |
| ⛔ | Blocked (gated by a decision or predecessor) |
| ✂ | Deferred explicitly (not scheduled; see §11) |

Source freshness hierarchy: `docs/plans/README.md` index → newest dated plan →
`.opencode/memory-bank/activeContext.md` → `.serena/memories/*` wave records. Older root MDs
(PLAN.md, CODEBASE_DOCUMENTATION.md, FUTURE_FEATURES.md) are stale where they conflict (§12).

---

## 2. Pipeline Modes

Mode architecture: `ModeManager` (src/core/modeManager.cpp) — one active mode, snapshot
`g_preModeConfig`, per-mode profile delta, listener registry. The six legacy `*_mode.cpp`
preset writers are retired no-ops; ModeManager is the sole authority. Runtime selector:
ini `[Modes] active` + 8-chip ImGui picker (PS2 · Xbox · PC · Mobile · GTA IV · GTA V ·
Modern · Custom).

| Mode | buildingPipe | vehiclePipe | colorFilter | Status |
|---|---|---|---|---|
| PIPELINE_PBR(0) / Modern | BUILDING_PBR(3) | CAR_MODERN(10) | MODERN(8) | ✅ (forced pairing in postfx.cpp ColourFilter_switch) |
| PIPELINE_PS2(1) | BUILDING_PS2(0) | CAR_PS2(0) | PS2 | ✅ |
| PIPELINE_XBOX(2) | BUILDING_XBOX(1) | CAR_XBOX(2) | — | ✅ (default live mode, 81-field profile) |
| PIPELINE_MOBILE(3) | mobileBuilding | CAR_MOBILE(4) | Mobile | ✅ (first-class 70-field profile) |
| PIPELINE_GTAIV(4) | BUILDING_GTAIV(2) | CAR_GTAIV(9) | GTAIV | ✅ (mode gut landed in ModeManager) |
| GTA V | (partial profile) | — | — | 🟡 (only 3 fields landed; full GTAV port = §3 water/sky/tone items) |
| Legacy PC vintage | CAR_PC + PC colour filter | — | PC/III/VC/VCS | ✅ |
| Preset framework (GTA III/VC/LCS/VCS/VI presets per PLAN.md P1.3) | — | — | — | ⛔ deferred to 64-bit build |

Owner files: src/core/modeManager.cpp, src/core/main.cpp (pipeline switch, readIni),
src/core/presets.cpp, src/render/buildingPipe.cpp, src/render/vehiclePipe.cpp, postfx.cpp.
Source MDs: AGENTS.md; docs/plans/2026-10-08-iv-v-graphics-physics-port.md (master matrix,
supersedes 2026-08-01-remaining-roadmap.md); .serena/memories/wave-12lane.md.

---

## 3. Rendering Features

### 3.1 Buildings
| Feature | Status | Owner files | Source MD |
|---|---|---|---|
| PS2 / Xbox / PBR / GTA IV building render callbacks | ✅ | src/render/buildingPipe.cpp | CODEBASE_DOCUMENTATION.md; source-inventory §1 |
| Stochastic detail maps + dual-pass (PS2/Xbox) | ✅ | buildingPipe.cpp, shaders/stochasticBuildingPS.hlsl | noise-system-audit §3 |
| Unified texture-def layer (normal-map extraction, TexDefsGetD3DTexture, plugin registry) | ✅ (merge landed) | src/texdefs.cpp/.h | 2026-10-29-merge-rinse §0 |
| Texture alpha classifier (TAC_OPAQUE/CUTOUT/FADED) + cache 4096 | ✅ | src/render/texalpha.cpp/.h (consumers buildingPipe.cpp:2111) | activeContext Wave 5 |
| Building dual-pass road-decals / classify bracket | ✅ | buildingPipe.cpp:2253-2280, PipeChain.hlsl | alpha-reversal-findings (alpha echo fix landed) |
| Building env reflect (bldEnvReflect/bldSkyReflect) | ✅ | buildingPipe.cpp | source-inventory §4 |
| World-state baseline at frame boundaries (pipeApplyWorldBaseline) | ✅ | pipelinecommon.cpp (call sites main.cpp:2144/2636/2658/5639, depthhook.cpp:378) | activeContext Wave 5 |
| stateBaselineEnable A/B lever | 🟡 (no-op today — call sites unconditional) | main.cpp:3935/4497/4526 | activeContext 2026-10-24 "INI bisect levers" |

### 3.2 Vehicles
| Feature | Status | Owner files | Source MD |
|---|---|---|---|
| Vehicle pipes PS2/PC/XBOX/SPEC/MOBILE/NEO/LCS/VCS/ENV/GTAIV | ✅ | src/render/vehiclePipe.cpp | CODEBASE_DOCUMENTATION.md |
| PBR Modern (VehiclePBR_Modern.hlsl, 7 entry points incl. rubber, specCarFx, mobile, ps2EnvSpec) | ✅ | vehiclePipe.cpp, src/render/pipelinecommon.cpp (pipeUploadPBR) | AGENTS.md |
| Vehicle opacity/glass contract (alpha-16 paint marker, glass SURFACE_CAR_GLASS fallback) | ✅ | vehiclePipe.cpp, pipelinecommon.cpp, VehiclePBR_Modern.hlsl | activeContext Wave 5 |
| Clearcoat Fresnel, metallic energy conservation, carcols envMask, glossiness | ✅ | vehiclePipe.cpp, veh_shaders | TODO.md §D |
| Chrome pipeline + vehAutoChrome family | ✅ | vehiclePipe.cpp | source-inventory §4 |
| Env mapping / screen capture (envmaphooks) | ✅ | src/render/envmap.cpp | source-inventory §1 |
| Neo water/blood drops on windscreen | ✅ | src/render/neopipe.cpp (gated neoWaterDrops/neoBloodDrops) | source-inventory §4 |
| Ground/street dawn brightness (ambientObj timecycle) | ✅ | vehiclePipe.cpp | TODO.md §D |
| Per-vehicle paint/BRDF/light-tint params (VehShaders_*) + vehicles.ide DB | ✅ | src/extras/veh_shaders.cpp, src/extras/vehicles.cpp, src/extras/brdfLibrary.h | source-inventory §1 |
| Dead API prune (dirt block, colour-area system) | 📋 verdict DELETE (rinse D2) | vh_shaders.cpp:288-539 | 2026-10-29-merge-rinse §3.3-E |

### 3.3 Peds
| Feature | Status | Owner files | Source MD |
|---|---|---|---|
| Ped default-callback contract (entry/exit, cull A/B, alpha A/B, opaque contract, pAmbient override) | ✅ | src/core/main.cpp:1881-2440 → extraction target | source-inventory §6; 2026-10-31-peds-pipe-design §3.1 |
| Ped dual-pass legs (ZWRITE two-layer, VERTEXALPHA blend F6 fix) | ✅ | main.cpp:2260-2388 | peds-pipe-design §3.1 |
| Ped opaque contract default ON (pedOpaqueContract, wave 5) | ✅ | main.cpp + envmap.cpp restore | activeContext Wave 5 |
| Ped ambient max(timecycle) + pedAmbientFloor (default 0.20) | ✅ ped ambient max() landed per wave 5; pedAmbientFloor is DEAD-BUT-PARSED (W3 revert removed consumers; field kept because debugmenu_ui.cpp:750/1506 references it) — RESOLVED in docs/plans/2026-10-31-md-reconciliation.md §3 | main.cpp:2185-2225, 7102-7104, 7284-7288; debugmenu_ui.cpp:750/1506 | md-reconciliation §3; source-inventory §2-D4 + sep3/git-chars-lighting.md |
| Stage-0 texture resync (pedStage0Resync, NULL-cache recovery) | ✅ | main.cpp:1760-1872 | source-inventory §5/P6 |
| Head-part receipt diagnostics (bandana/hair) | ✅ | src/entities/chars.cpp (chars_logHeadPartReceipt) | source-inventory §6 P9 |
| SSS skin blur post-pass (chars_drawSSSBlur, live) | ✅ | chars.cpp:150-640 (called from postfx.cpp chain) | merge-rinse §3.3-D; TODO.md §D (CJ material depth) |
| Ped shader-hygiene in default callback (defaultCbShaderHygiene) | ✅ | main.cpp:2142-2183 | activeContext Wave 5 |
| Skin PBR shader (SkinSSS.cso reserved, PartSSSParams tuned) | 🟡 Phase 2 — reserved, not bound (pipelinecommon.cpp:810-811; IDR_SKINPBR embedded-not-loaded) | pipelinecommon.cpp, shaders/ps/SkinSSS.hlsl | peds-pipe-design §3.2 |
| pedPBRVS.cso embedded, never loaded (IDR_PEDPBRVS) | 🟡 Phase 2 staging slot | resources/resource.rc:129 | source-inventory §2-D8 |
| Full status → §5 (peds_pipe + hair + cloth) | | | |

### 3.4 Water
| Feature | Status | Owner files | Source MD |
|---|---|---|---|
| Water pipe (3 styles: Xbox → Water_Parallax.cso, IV → Water_IV.cso, V → Water_V.cso) | ✅ | src/render/waterPipe.cpp, shaders/ps/Water_Parallax.hlsl, shaders/vs/Water_VS.hlsl | water-overhaul-spec §1a |
| 2-wave vertical swell (c11 law) + body-class TEXCOORD5 (OCEAN/SHALLOW/INTERIOR/RIVER/POND/LAKE) | ✅ | Water_VS.hlsl, waterPipe.cpp W7-B classify | water-overhaul-spec §1a |
| Fresnel sky reflection, depth shallow→deep ramp, shore alpha exp2, foam octaves, sun glitter | ✅ | Water_Parallax.hlsl | water-overhaul-spec §1a |
| G1 screen-space reflection (flipped scene, tier≥2), sun pierce tier 3 | ✅ | waterPipe.cpp:670 (CopySceneFlippedToTexture) | water-overhaul-spec §1a |
| Underwater volume fog + SSS (W7-D) | ✅ | src/render/postfx_underwater.cpp, shaders/ps/Water_Underwater.hlsl | source-inventory §1 |
| Water clip-space fix + WbBufferCountsSane + park/pin/restore (water_state/water_badbuf) | ✅ | waterPipe.cpp, main.cpp:5739-5747 | activeContext Wave 5 / 2026-10-29 teal-quads diagnosis |
| Water line: vanilla fallback NULL+cache-seed VS/PS (Patch A/B from exp-1) | 🟡 spec landed; patch status to confirm | waterPipe.cpp:513-517, main.cpp | .opencode (teal-quads fix spec) |
| 9 missing features M1-M9 (far-water LOD/flood guard, foam texture, geom foam, refraction, caustics, ripple sim, SSS pierce polish, volume split, FFT swell) | 📋 phased (spec Phase 0-3) | Water_Parallax.hlsl, waterPipe.cpp, new waterSim.cpp | 2026-10-30-water-overhaul-spec §2-§5 — full status in §6 |

### 3.5 Sky / clouds
| Feature | Status | Owner files | Source MD |
|---|---|---|---|
| Volumetric clouds (DynamicSky via RenderIBLBuffer: procedural clumps + layered) | ✅ | shaders/ps/DynamicSky.hlsl, buildingPipe.cpp | TODO.md §F |
| Cloud ground shadows (ComputeCloudShadow + PBR_cloudFBM) | ✅ | shaders/include/colorSpace.hlsl, PBR_Common.hlsl | TODO.md §F |
| CloudsPS IDs 283/284 wired | ✅ (excluded from merge-rinse deletion) | pipelinecommon.cpp, resources | activeContext Wave 8 |
| CClouds::Render FFP state hygiene hook (save/restore, shield) | ✅ | main.cpp:4363-4622 CClouds__Render_hygiene | 2026-10-25-clouds-ffp-audit §1.4 |
| Corona/sprite deferred-batch bleed fixes (flush shield, CPointLights window) | ✅ (residual ghost/bleed classes remain) | main.cpp corona hooks | clouds-ffp-audit §3; alpha-reversal-findings |
| Alt weathers2 timecycle blending (Weather_Update / ApplySunConfig) | ✅ | src/core/weather.cpp | source-inventory §1 |
| Sky dome (RenderSkyPolys) untouched (drawn before alpha block repair) | 🟡 known gap (audit §1.5) | game exe | clouds-ffp-audit §1.5 |
| GTA V-style weather expansion (sky/sun/moon/clouds fields) | 🟡 | weather.cpp, skygfx.h | CODEBASE_DOCUMENTATION.md config list; iv-v-port matrix |

### 3.6 Vegetation / grass
| Feature | Status | Owner files | Source MD |
|---|---|---|---|
| Plant island TU + depth contract (grasssys) | ✅ (support surface) | src/render/grasssys.cpp/.h | source-inventory §1 |
| Grass instancing draw path | ⛔ DEAD unfed — SetEnabled wired, nothing calls DrawTriPlant; force-decision: delete 1,771 ln or wire CPlantMgr::Render thunk | grasssys.cpp/.h | 2026-10-29-merge-rinse §8 (Z1); source-inventory §2-D1 |
| PC grass renderer legacy callback + plant surface props | ✅ | grasssys.cpp, grassRenderCallback in main.cpp | source-inventory §1 |
| CPU tree wind tilt (TreeWind_ApplyToTransform) | ✅ | src/render/treeWind.cpp (hook 0x534E90 main.cpp:7805) | rw-physics-ragdoll-re §4.1 |
| Tree angular spring (bend rewrite) | 🟡 Phase 2a spec, not landed | treeWind.cpp | rw-physics-ragdoll-re §5.4 |
| Tree crack (sub-hierarchy gibs) | 📋 Phase 2b opt-in | treeWind.cpp, softbody | rw-physics-ragdoll-re §5.4 |
| Grass colour data + plant-surf-prop header absorb | 📋 rinse M-E (absorb ColData.h + PlantSurfPropMgr.h into grasssys.h) | src/rw/ColData.h, src/entities/PlantSurfPropMgr.h | merge-rinse §7-E |

### 3.7 Decals / coronas / sprite / shadows
| Feature | Status | Owner files | Source MD |
|---|---|---|---|
| Corona draw shield + flush shield + blend shield + CPointLights window | ✅ | main.cpp (coronaRwBlendFromD3D, effectsForceAlphaBlock, flush 0x70CF20 hooks) | activeContext waves 3-5; clouds-ffp-audit |
| Sprite state/flush shields (spriteStateShield, spriteFlushShield), moon mask alpha, skidmark alpha fix, transparent lockon | ✅ | main.cpp hook sites | source-inventory §4 Effects lane |
| Corona z-test force | 🟡 D11 flag — comment "parse-only" contradicts active read (main.cpp:374-380 comment vs active `config->coronaZtest` reads in the CCoronas::Render shield); documented in docs/plans/2026-10-31-md-reconciliation.md §2, flagged for the owning lane before touching | main.cpp:374-380, 4876, 4950-4955 | source-inventory §2-D11; md-reconciliation |
| Stencil shadows (Carmack's Reverse research → game-native stencil passthrough via GetFxQuality hooks 0x7113B8/0x711D95/0x70F9B8 + z-offset patches) | ✅ | main.cpp | TODO.md §F |
| Shadow state contract (byte-verified zero-arg sites 0x53E0BE/C3/C8, canonical L1+L2+L3) | ✅ | main.cpp (shadow_contract) | activeContext Wave 5 |
| Cascaded shadow maps / CHSS / PCSS / night shadow atlas | 📋 | new shadow files | 2026-10-08-iv-v-graphics-physics-port §B |
| Tag rendering (CTagManager hooks) + mirror fixes hooks | ✅ | main.cpp, src/render/mirrorPass.cpp | PLAN.md P3 hook map |

### 3.8 PostFX chain
Chain order (verified): normal buffer → velocity buffer → SSAO → CopyDepthToPrev →
SunShafts→DOF→Bloom → colour filter/tonemap → CA→grain→vignette → IV grade → motion blur →
height fog → god rays → UpdateFrontBuffer → SSS; SMAA last (DrawFinalEffects).
Owner: src/render/postfx.cpp + postfx_*.cpp split.

| Feature | Status | Owner files | Source MD |
|---|---|---|---|
| ColourFilter passes PS2/PC/Mobile/III/VC/VCS/IV/Modern/Generic | ✅ | src/render/postfx_colourfilter.cpp | CODEBASE_DOCUMENTATION.md |
| SSAO chain (16-sample ps_3_0, temporal/bilateral/upsample + upsample fix) | ✅ | src/render/postfx_ssao.cpp, SSAO.hlsl | TODO.md full roadmap Phase 0; PLAN.md P1.1 |
| SMAA (embedded AreaTex/SearchTex, presets, temporal; velocity wiring) | ✅ | src/render/postfx_smaa.cpp | WORKFLOW_PLAN success criteria |
| Motion blur / burnout (velocity buffer 3 consumers: MB, SMAA temporal, SSAO temporal) | ✅ | src/render/postfx_motionblur.cpp, VelocityReconstruct.hlsl | motionblur research §1a |
| Motion blur Tier 0 (dt normalisation + cut reset, ~40 LoC) / Tier 1 (bidirectional taps, depth rejection, texel snap — 25→13-17 fetches) / Tier 2 (per-object velocity via physics — §9 synergy) | 📋 (10 quality issues Q1-Q10 listed) | postfx_motionblur.cpp, MotionBlur_Burnout.hlsl | 2026-10-29-motionblur-physics-research §0/§2 |
| Adaptive tonemap v2 (timecycle exposure+toe, interior/cutscene, carcols) | ✅ | postfx.cpp, postfx_colourfilter.cpp | TODO.md ✅#7 |
| LUT tonemaps (ACES 1.3/2.0, AgX, Frostbite, GT7) + LUT baker | 📋 | new shaders | iv-v-port §A |
| Auto-exposure (GPU avg luminance) | ✅ (RunFrameExposure consumer landed); cf_gain residual washout investigation | postfx_luminance.cpp, postfx_colourfilter.cpp:307 | source-inventory §1; alpha-reversal Q2 |
| Radiosity (PS2/Shader/VCS) | ✅ | postfx.cpp | CODEBASE_DOCUMENTATION.md |
| Crytek height fog (HeightFog.hlsl, INI 7 fields) | ✅ | postfx.cpp DrawHeightFog | TODO.md §F |
| Screen-space god rays (GodRays.hlsl, INI 6 fields) | ✅ | postfx.cpp DrawGodRays | TODO.md §F |
| Sun shafts LUT adoption (radial scattering + cloud sky-mask) | 📋 | GodRays.hlsl | iv-v-port §A |
| SSS blur (skin composite in chain) | ✅ | chars.cpp, postfx.cpp | source-inventory |
| Grain (IGN 3-octave, TAA-safe — keep, not Perlin) | ✅ | shaders/ps/Grain.hlsl, postfx_newfx.cpp:737-763 | noise-system-audit §1 (KEEP verdict) |
| Infrared / night vision (+grain) | ✅ | postfx.cpp | CODEBASE_DOCUMENTATION.md |
| Bloom (simple blur today → Jimenez 6-mip soft-knee Karis behind bloomQuality tier) | 🟡→📋 replace planned | postfx_newfx.cpp, BloomCommon.hlsl | iv-v-port §A |
| DOF (CoC→bokeh→tent→combine; postfx_newfx.cpp family support present) | 🟡 (support landed; full IV port planned) | postfx_newfx.cpp | source-inventory §1; iv-v-port §A |
| FXAA 3.11 | 📋 MISSING | new | iv-v-port §A (fxaaEnable key exists) |
| Chromatic aberration, parametric vignette, stipple/screen-door, pre-alpha depth, MLAA | 📋 (CA param landed caIntensity; IV vignette exists) | postfx.cpp | iv-v-port §A |
| Forward+ tiled light cull/upload | 🟡 landed but health flag: light collection "added 0" class at dig time; FP debug rows alive | src/render/forwardplus.cpp | 2026-10-17-systematic-dig findings |

### 3.9 Mirror
| Feature | Status | Owner files | Source MD |
|---|---|---|---|
| Mirror render intercept + composite (CMirrors::RenderMirrorBuffer) | ✅ | src/render/mirrorPass.cpp (mirrorPass_init main.cpp:5786) | source-inventory §1 |
| envmap/mirrorPass local externs → headers | 📋 rinse M9 | main.cpp:5778/5785 | source-inventory §3 |

---

## 4. Physics

Architecture: unified softbody module with TexDefs-style lifecycle
(src/entities/softbody.h). Native backend = RW-primitive ragdoll port
(velocity → parent-space axis/angle → RtQuatRotate → RtQuatSlerp into AnimBlend interp
frame — BoneNode_c idiom). Bullet = optional backend (g_bulletWorld). physicscfg.cpp owns
physics.ini (softbodyBackend off|native|bullet, budget). NEVER Euphoria (design contract).

| Feature | Status | Owner files | Source MD |
|---|---|---|---|
| Native softbody ragdoll Phase 1 (SoftRagdoll_c, all-bone native integrator, capture bits) | ✅ (absorbed/merged) | src/entities/softbody.cpp/.h, src/entities/sftbdyphsx.cpp/.h | 2026-10-29-rw-physics-ragdoll-re §5; source-inventory §1 |
| Bullet death ragdoll backend (11 capsules, cone/hinge, dismember bake) | ✅ | src/extras/ragdoll_death.cpp/.h, softbody | rw-physics-ragdoll-re §4.1 |
| Dismemberment (RpSkin bake pipeline, zero-scale bone matrices) | ✅ | sftbdyphsx.cpp:563-1184 | rw-physics-ragdoll-re §4.1 |
| Water buoyancy (RagdollWaterForces: buoyancy/drag/coupling/spring/drift, shared swell law) | ✅ (Bullet lane; clock unification still pending) | ragdoll_death.cpp:951, softbody.cpp (SoftbodyWaterQuery/Swell) | water-overhaul-spec §4 |
| Shared water surface-height law (one clock, 3 consumers) | 🟡 clock divergence fix pending (physics contract #1) | softbody.cpp, ragdoll_death.cpp, waterPipe.cpp | water-overhaul-spec §4 |
| Cloth simulation (verlet, ground/water/LOS collision, AnimBlend co-existence, flags/curtains reuse) | 📋 Phase 4 of peds pipe | src/peds/cloth.cpp (design) | 2026-10-31-peds-pipe-design §5 |
| Bullet interop A/B for cloth (clothBackend native|bullet) | 📋 (key defined in design) | softbody, physicscfg | peds-pipe-design §6 |
| Tree bending spring / crack / vehicle softbody | 📋 Phases 2a/2b/3 | softbody, treeWind, ragdoll bake | rw-physics-ragdoll-re §5.3-§5.5 |
| Crash features (HitReaction, vehicle impacts, weapon knockback, writhe, RAGE 3-DOF joint motors, pose-matcher, breakable constraints) | 📋 (spec §2 P3; HitReaction hook point zero-callers at spec time) | ragdoll_death.cpp:853-868 | 2026-10-08-iv-v-graphics-physics-port §2 |
| Vehicle lightweight physics (raycast suspension/tyre forces) | 📋 | new Bullet files | iv-v-port §2 |
| Physics↔graphics synergy (velocities → motion vectors; depth/normals → physics) | 📋 P4 | postfx_motionblur + softbody | iv-v-port §0/§P4 |
| Boolean damage (Red Faction-style) on Bullet | 📋 P5 | new | iv-v-port §3 (reference inventory: 2026-10-29-destruction-mod-inventory.md = reference only, no license) |
| Layout-drift guards (AnimBlendFrameData 0x18, address-derived checks) | ✅ (spec); verify in softbody module | sftbdyphsx.h | rw-physics-ragdoll-re G1 |

---

## 5. Peds Pipe

Design: docs/plans/2026-10-31-peds-pipe-design.md — DESIGN ONLY doc, phased 1-4.
Layout: `src/peds/` — peds_pipe.{cpp,h}, skin.{cpp,h}, hair.{cpp,h}, cloth.{cpp,h}.

| Feature | Status | Owner files | Source MD |
|---|---|---|---|
| peds_pipe module (extract myDefaultCallback ped branch into PedsPipeRenderPedAtomic; wrap-first with pedsPipeEnablerollback) | 🟡 design landed; Phase 1 extraction is next work | design target src/peds/peds_pipe.cpp; today main.cpp:1881-2440 | 2026-10-31-peds-pipe-design §2/§7 |
| skin sub-module (Stage0resync + dual pass + opaque + ambient absorbed) | 🟡 same Phase 1 | peds_pipe + skin | peds-pipe-design §3 |
| Skin shader (FFP→SkinSSS.cso swap, register block reuse, pipeUploadPBR) | 📋 Phase 2 | skin.cpp, pipelinecommon.cpp | peds-pipe-design §3.2 |
| Hair thread (card strips, per-bone HairAttachTable, Kajiya-Kay anisotropic, ≤96 strands/clump) | 📋 Phase 3 (revive dead chars_classifyClump path) | peds_pipe + hair.cpp, HairEnhance.hlsl | peds-pipe-design §4 |
| Animal fur reuse (same hair thread, SPINE/TAIL attach tables) | 📋 Phase 3 extension | hair.cpp | peds-pipe-design §4.2 |
| Cloth sim (verlet + physics link) | 📋 Phase 4 | cloth.cpp | peds-pipe-design §5 |
| clump plugin slot "Pdp1" 0x50647031 (non-ped state attach) | 🟡 designed | peds_pipe | peds-pipe-design §2.2 |
| [Ped] lane INI keys (pedsPipeEnable, skinShaderEnable, hairEnable, clothEnable, clothBackend…) | 📋 | main.cpp readIni [Ped] section | peds-pipe-design §6 |
| Bandana/head-part orientation | ✂ out of scope (requires game-side CClothes hook; flagged so no phase silently claims it) | — | peds-pipe-design §8 |
| chars.cpp split: SSS blur → postfx side, receipt → peds_pipe (rinse M7) | 📋 | chars.cpp | source-inventory §3-M7 |
| Skin PBR decision reserved (IDR_SKINPBR deliberately not loaded) | 🟡 | pipelinecommon.cpp:810-811 | peds-pipe-design §3.2 |

---

## 6. Water Overhaul — 9 missing features (spec `docs/plans/2026-10-30-water-overhaul-spec.md`)

| # | Feature | Phase | Status | Owner |
|---|---|---|---|---|
| M1 | Far-water LOD + over-land flood guard (kills 59% F2 family) | Phase 0 | ✅/🟡 (teal-slab root cause found; Patch A nIdx≤verts*6 + Patch B NULL+cache-seed fallback) | Water_Parallax.hlsl, waterPipe.cpp W7-B |
| M5 | Caustics upgrade (sharpen curve, mips, world-space UV) | Phase 0 | ✅/🟡 (fix-13 Water_Underwater.hlsl:127 saturate rebuilt) | Water_Parallax.hlsl |
| — | FFP foam band tuning (F4 wake streaks) | Phase 0 | 📋 | Water_Parallax.hlsl fallback path |
| M2 | Shoreline foam texture blending (RAGE FoamMask 1D gradient, +s4/s5) | Phase 1 | 📋 | Water_Parallax.hlsl |
| M3 | Foam masks near geometry (soft intersection, softIsec) | Phase 1 | 📋 | Water_Parallax.hlsl |
| M4 | Depth-validated wave refraction (4-tap depthMax, cannotRefract gate) | Phase 1 | 📋 | Water_Parallax.hlsl |
| M7 | SSS + sun pierce polish (Cry EdotL² recipe) | Phase 2 | 📋 | main_v/main_iv composite |
| M6 | Dynamic ripple sim (RT ping-pong, new waterSim.cpp, tier-3 gated) | Phase 3 | 📋 | waterSim.cpp (new) |
| M8 | Volume vs surface split (INTERIOR pools path) | Phase 3 | 📋 | Water_Parallax.hlsl main_volume |
| M9 | FFT-lite swell (VTF LUT ≤64², target-GPU verify first) | Phase 3 | 📋 | Water_VS.hlsl |
| — | Physics clock unification (single accumulator) | Phase 3 | 📋 | waterPipe + softbody |

Do-not-regress list: near-camera caustics, HUD, interior pipeline, tier<2 SRCALPHA fallback,
vanilla FFP water (waterParallaxEnable=0). Register headroom: PS c16-c31 + s4+ free, VS c12+.

---

## 7. Noise System

| Feature | Status | Owner files | Source MD |
|---|---|---|---|
| Dedicated CPU noise service (SkyNoise: tileable Perlin/Worley/white/Bayer + quality tiers + stats) — replaced utilitynoise.cpp (deleted 2026-10-29) | ✅ landed | src/render/noise.cpp/.h | 2026-10-25-noise-system-audit (§ recommended shape = what landed); merge-rinse §0 |
| SkyNoise owns cloud noise tile (DynamicSky clouds) | ✅ | noise.cpp, DynamicSky.hlsl | merge-rinse §6.1 ("SkyNoise owns the tile", main.cpp:721-722) |
| GPU noise include (Noise.hlsl, SM3.0-safe) to replace dead PerlinNoise.hlsl / CarPaintNoise.hlsl / StochasticSamplerPS.hlsl | 🟡 (C++ service shipped; dead shader sources deleted in L13/L14; GPU include consolidation pending) | shaders/include/Noise.hlsl | noise-system-audit §1 |
| Stochastic → proper Perlin shader path (new CSO IDs 281-283, s9/s10, c49-c52 plan) | 📋 (old stochastic 227/231/278 remain as rollback; old stochastic CSOs cull started Wave 8) | buildingPipe.cpp selectPS, fast_build.py | noise-system-audit §3.2-§3.4 |
| SetQuality repoint or delete ([General] NoiseQuality is now pure round-trip, 0 consumers; tooltip stale) | ⛔ decision needed (Option A repoint ~6 ln preferred) | main.cpp:5935/7455/7506-7511, noise.cpp SkyNoise::SetQuality | merge-rinse §6.1 I1 |
| CloudNoise.hlsl merge (DynamicSky/IBL_SkyCloud hash+noise3d+fbm duplicates + alias PBR_chash family) | 📋 | shaders/ps/DynamicSky.hlsl, IBL_SkyCloud.hlsl, PBR_Common.hlsl | merge-rinse §5.1 S2 |
| IBL_SkyCloud.hlsl not embedded (superseded by DynamicSky) — delete or wire | 📋 | shaders/ps/IBL_SkyCloud.hlsl | source-inventory §2-D2 |
| helper_tex.txd (6-texture pre-generated noise TXD) — packed+verified but NOT embedded, NOT loaded | 📋 loader must be written | assets/helper_tex/helper_tex.txd, tools/pack_helper_tex.py | noise-system-audit §2.2 (docs/helper_tex.md:126-131) |
| Water detail-normal bake routed through noise service | 📋 | waterPipe.cpp:727-790 | noise-system-audit §1 |

---

## 8. Refactors

### 8.1 Landed merges (wave "fix-94", 2026-10-27 — verified in activeContext + rinse §0)
| Merge | Status |
|---|---|
| texdb + normalmap + plugin registry → src/texdefs.{cpp,h} | ✅ |
| dbglog/diag ring buffer → src/core/debug_handl.{cpp,h} | ✅ |
| ragdoll + dismember → src/entities/sftbdyphsx.{cpp,h} | ✅ |
| neopipe, grasssys, `*_mode` stubs retirement | ✅ |
| blendToD3D dedupe, `#if 0` deletions (C7), utilitynoise.cpp deletion, defaultFuncs.cpp deletion, legacy DebugMenu UI deletion | ✅ |
| envParkSetState3L / MirrorRestoreState3L → canonical pipeRwParkSet/pipeL2ParkSet/pipeL3Set primitives | ✅ |

### 8.2 The 12 merge candidates (source-inventory §3) — open
| # | Candidate | Status |
|---|---|---|
| M1 | Delete dead grass draw path (~1,350 LOC) — fold into Z1 grasssys decision | ⛔ decision needed |
| M2 | postfx_luminance.cpp → postfx/colourfilter | 📋 |
| M3 | presets.cpp → debugmenu_ui.cpp (alt: modeManager.cpp) | 📋 |
| M4 | vehicles.cpp + veh_shaders.cpp → extras/vehdata.cpp | 📋 |
| M5 | pipeplg.cpp + normmap_stubs.cpp → rw/gta.cpp (keep extern "C" + selectany) | 📋 |
| M6 | Delete forwarding headers neo.h + PC_GrassInstancer.h | 📋 (rinse Q1/M-H) |
| M7 | chars.cpp split (SSS blur → postfx; receipt → peds_pipe) | 📋 |
| M8 | Weather_Init wiring fix (vehiclePipe.cpp:212 → main.cpp init burst) | 📋 |
| M9 | main.cpp local externs → headers | 📋 |
| M10 | debugmenu_theme merge | ✂ recommended NOT to merge (clean seam) |
| M11 | texalpha.cpp → texdefs.cpp | ⛔ defer until main.cpp split |
| M12 | postfx_underwater.cpp → postfx.cpp | ✂ low priority |
| — | postfx_support.cpp merge (luminance+motionblur+utils, 930 ln) | 📋 NOT landed — rinse §1 verdict MERGE |
| — | main.cpp split (modular feature-domain layout) | 📋 2026-10-08-restructure doc ACTIVE (reference); ped slice = peds_pipe |

### 8.3 Config lane-scoping ([Ped]/[Building]/[Vehicle]/[Grass]/[Water]/[PostFX]/[Effects]/[Core])
| Item | Status | Source |
|---|---|---|
| Section-scoped key convention + ReadKey(section, key, fallbackTopLevel) migration | 📋 (mapping table exists; the cross-lane ambient keys ambientFloor/ambientMult/pbrAmbientFloor/pbrIblAmbientWeight are the hard case) | source-inventory §4 |
| [Ped] per-lane section authoritative | 📋 (spec in peds-pipe-design §6) | peds-pipe-design §6 |
| Keys read outside readIni (debugOverlay, forwardPlusDebug, forwardPlusFallback, enableNormalMaps, softbodyNative) | 📋 consolidate | source-inventory §2-D13/D5 |
| Dual normal-map keys (enableNormalMaps texdefs.cpp:1038 vs normalMapEnable readIni) | 📋 consolidate | source-inventory §2-D12 |
| nuclear shared bind helper pipeBindDiffuse3L/pipeBindRaster3L (5 copies → 1: vehBindDiffuse3L ×10, bldBindDiffuse3L ×2, waterPipe_BindVanillaRaster3L ×9, pedStage0Resync, grasssys stage-0) | 📋 (pipelinecommon site proposal; pipeEnforceDepthContract half already shared) | source-inventory §5 |

---

## 9. Performance

Ranked audit outcomes (verified via WORKFLOW_PLAN + systematic-dig + activeContext):

| Item | Status | Owner / evidence |
|---|---|---|
| Safety hardening P0 (normalize guards, div-by-zero epsilon, strcat→strncat, sprintf→snprintf, fxParams memset) | ✅ DONE (52bf910) | TODO.md roadmap table |
| Shared vehicle callback helpers (dedup ~1,500 ln) | ✅ (b680e31) | WORKFLOW_PLAN ARCH-2 |
| Render-state tracking audit (~90 sites) + statics table (3 HIGH: postfx.cpp:883-884/1042/1175, depthhook.cpp:39-41) | ✅ | WORKFLOW_PLAN ARCH-3/4 |
| SSAO upsample 2-liner grey-overlay fix | ✅ | systematic-dig / iv-v-port P0 |
| Colour-filter no-op fix (R2/R1 MOD2X) + PS2 env lane int8 clamp | ✅ | iv-v-port P0; activeContext 2026-10-26 |
| SMAA temporal Step1 (neighbourhood clamp, motion curve) | ✅ | iv-v-port P0 |
| Sky foreign-raster repair + FBWP rework + clouds hygiene | ✅ | iv-v-port P0 |
| Instance optimization (buildingPipe.cpp:1005-1011) | ✅ | WORKFLOW_PLAN Phase 4 |
| Crash guards: 0x7F98DF/0x7F9ECB/0x7FAD4D NULL fixes | ✅ (aac8478, fa1d507, neoCarpipe.cpp:86/124, envmap.cpp:487) | WORKFLOW_PLAN success criteria |
| postfxDumpDebug hygiene (default OFF — was 14 BMP/frame × 590 ms spikes) | ✅ rule recorded | systematic-dig finding #4 |
| Profiler scope table overflow (32 slots, 657k×/session) | 🟡 enlarge or aggregate | systematic-dig workflow #4 |
| Teal-quads water throughput bail (Patch A/B) | 🟡 see §6 M1 | teal-quads diagnosis |
| texAlpha classifier cache 4096 4-way (tac=0 starvation) | ✅ | activeContext Wave 5 |
| Pipe chain gating (18-pass pf_gate) + per-mesh building elision (bpipe_elide) | ✅ | activeContext Wave 8 |
| MEM-5 raw new/delete shutdown cleanup (normalmap_plugin/texdb 4 allocs) | 🟡 remaining low pr. | WORKFLOW_PLAN remaining #4 |
| TODO-1/2 (wheels double-buffer/InterceptCall) | ✂ STALE (wheels module deleted) | WORKFLOW_PLAN Phase 4 |

---

## 10. Tooling / Debug

| Feature | Status | Owner files | Source MD |
|---|---|---|---|
| Frame-correlation overlay + [#id:code] log prefix | ✅ | src/core/debug_handl.cpp/.h | source-inventory §1 (merged; diag UX landed) |
| dbglog / dbglog_throttle (2 s global) / dbglog_once / diag scopes / heartbeat | ✅ | debug_handl; timestamps → skygfx_dbg.log | AGENTS.md rules |
| diagLevel + watchdog (0x40010006 whitelist, VEH+minidump, crash capture) | ✅ | main.cpp (VEH handler), debug_handl | PLAN.md P0-P2; activeContext |
| [BUILD] size fingerprint + binary string verify + `.cache/msbuild_last.log` persistence | ✅ | tools/fast_build.py | activeContext 2026-10-26 |
| ImGui debug menu (All Settings tab, 235 rows, search, reset, copy-as-INI, live diagnostics; theme TU) | ✅ | src/core/debugmenu_ui.cpp, debugmenu_theme.cpp | activeContext Wave 5; README (2026-10-24-imgui-settings-coverage) |
| Mode chips + MODE pill rows + named configs (configs/) | ✅ | debugmenu_ui.cpp | activeContext Wave 8 |
| Atmospheric collapsing header (13 sliders) | ✅ | debugmenu_ui.cpp:651+ | TODO.md §F |
| Profiler (PfFrame, PF_SCOPE) | ✅ (scope-table overflow open §9) | src/render/profiler.cpp | source-inventory §1 |
| Render-state monitor (3-layer rw/drv/dev) | ✅ | src/render/renderstate_monitor.cpp | source-inventory §1 |
| Unified pipeline block (JuniorDjjr fork) | ✂ deliberately disabled, documented dead | debugmenu_ui.cpp:141-208 | WORKFLOW_PLAN resolved; rinse Z3 |
| III/VC in debug menu | ⛔ blocked by P1.3 preset framework | debugmenu_ui.cpp | WORKFLOW_PLAN Phase 5 |
| Dead-code manifest checks (--check-shaders, --verify-embed) | ✅ | tools/fast_build.py | merge-rinse §5.2 |
| INI reference generator (gen_ini_ref.py) | 🟡 --check STALE (drift only); regenerate docs-only | tools/gen_ini_ref.py | merge-rinse §6 I2 |
| True ImGui menu in-plan: parser/save/clamp defect fixes (2026-10-24-imgui-settings-coverage) | 🟡 partially landed (tonemapMaxExposure clamp conflict main.cpp vs debugmenu_ui.cpp still open) | debugmenu_ui.cpp, main.cpp | README index row |

---

## 11. Known Out-of-Scope / Deferred

| Item | Reason | Source |
|---|---|---|
| Bandana/head-part orientation | Requires game-side CClothes hook; leak surface barely exercised (only RenderTest called) | 2026-10-31-peds-pipe-design §8 |
| Xbox pipe → DX12 (4 pipeline), anything SM4/5 | SM3.0-only project rule | AGENTS.md critical rules |
| CryEngine 3.8.6 parity (full) — partial recipes allowed per-feature only | Long-term aspiration; only Cry recipes water/caustics/foam/SSS adopted via water-overhaul-spec | user directive m0068; water-overhaul-spec |
| GTA III / VC / LCS / VCS / VI presets + menu frameworks | Blocked on 64-bit build (PLAN.md P1.3 deferral) | PLAN.md P1.3 |
| Normal map + txd `_n` wiring | Golden rule = LAST feature (texdefs/rwnormal groundwork landed, final wiring deferred) | TODO.md golden rules §C; FUTURE_FEATURES.md |
| Multi-pass vehicle glass with parallax (POM for lens details) | Partially implemented — glass shader exists, POM pending | FUTURE_FEATURES.md |
| Collision-based edge detection (SMAA edges from collision geometry) | Not started | FUTURE_FEATURES.md |
| Boolean damage / MLAA / LensArtefacts (optional) | Later-phase generic list | 2026-10-08-iv-v-graphics-physics-port §1F/§P5 |
| Stale/divergent repo `C:\Dev\shaisse_hub\skygfx_plus` | Ignore — active repo is expIV | PLAN.md header |

---

## 12. Source-of-Truth MD Index

| Feature area | Authoritative MD |
|---|---|
| Plan index + resume procedure | docs/plans/README.md |
| Master feature matrix (IV/V port, Mode Manager, Bullet) | docs/plans/2026-10-08-iv-v-graphics-physics-port.md (ACTIVE; supersedes 2026-08-01-remaining-roadmap.md) |
| Architecture restructure / god-file split | docs/plans/2026-10-08-restructure-2026-10-08.md |
| Source inventory (dead code, merges, config lanes, ped hook map, bind idiom) | docs/plans/2026-10-31-source-inventory.md |
| Peds pipe + skin/hair/cloth design | docs/plans/2026-10-31-peds-pipe-design.md (design only, Phases 1-4) |
| Water overhaul spec + M1-M9 + physics contract | docs/plans/2026-10-30-water-overhaul-spec.md; refs: docs/plans/water-refs.md |
| Sep-3 look regression archaeology (6 files on disk, NOT 7) | docs/plans/2026-10-30-sep3-correlation/{git-render-pipes, git-chars-lighting, git-alpha-corona, git-texdefs-ground, ini-diff-exposure, shader-diff}.md |
| Merge/dedup/deletion verdicts | docs/plans/2026-10-29-merge-rinse.md |
| Native ragdoll / softbody RE | docs/plans/2026-10-29-rw-physics-ragdoll-re.md (supersedes 2026-07-30-ragdoll-restoration.md — deferred archive) |
| Motion blur quality + tiers | docs/plans/2026-10-29-motionblur-physics-research.md |
| Clouds FFP state contract | docs/plans/2026-10-25-clouds-ffp-audit.md |
| Noise system | docs/plans/2026-10-25-noise-system-audit.md; assets: docs/helper_tex.md |
| Alpha reversal + water/washout diagnosis | docs/plans/2026-10-21-alpha-reversal-findings.md (supersedes 2026-10-20-video-evidence-wave3-plan.md) |
| Debug strategy + Rockstar/aap parity checklist | docs/plans/2026-10-25-debug-strategy-parity.md |
| ImGui settings coverage | docs/plans/2026-10-24-imgui-settings-coverage.md |
| Workflow/process | docs/plans/2026-10-17-systematic-dig.md |
| Baseline target look (176 reference shots) | docs/baseline/2026-09-03-reference-caps/findings.md; newer baselines: docs/baseline/2026-10-24-screenshots/, docs/baseline/2026-10-29-finalgate-0042/findings.md |
| Build/shader/embed rules | AGENTS.md |
| Legacy status (STALE where conflicting) | PLAN.md, TODO.md, WORKFLOW_PLAN.md, FUTURE_FEATURES.md, CODEBASE_DOCUMENTATION.md (migrated to Obsidian vault [[00 - Index]]) |
| Wave/session records | .opencode/memory-bank/activeContext.md + progress.md; .serena/memories/crash-recovery-checkpoint-2026-10-20.md (authoritative restore point), wave-12lane.md, round5-*/round6-directives-2026-10-08.md, session-rules.md |
| Inventory baseline (older snapshot, stale LOC/keys) | .serena/memories/project-status.md (STALE 2026-10-01) |

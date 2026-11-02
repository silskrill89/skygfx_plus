# STABLE REFERENCE CONFIG — 2026-11-02 (user-declared "most stable, least bugs")

**Directive (m0283)**: "current config is most stable and least bugs check it out and work your fixes around it" — future fixes must NOT regress this config's behaviour. Treat as the canonical test baseline going forward.

## Config snapshot (E:\games\gtasa_skygfx_plus\skygfx.ini, 269 lines)

### Pipeline
- `[Modes] active = None` (no profile suppression — top-level values live)
- `pipeline = PBR`, `buildingPipe = PBR`, `vehiclePipe = Modern`, `colorFilter = Modern`
- `pipelineOverride = -1`, `explicitBuildingPipe = -1`, `gamePreset = -1`, `qualityPreset = 3`

### ON (the stable core)
- Shields: `coronaBlendShield = 1`, `spriteFlushShield = 1` (spriteStateShield = 0)
- Water: `waterParallaxEnable = 1`, `waterStyle = V`, `waterQuality = 3`, `waterReflectionMix = 0.7`, `waterShoreFoam = 1`, `waterBodyClassify = 1`, `waterFresnelSplit = 1`
- PostFX: `bloomEnable = 1` (q2, 0.8), `motionBlurEnable = 1` (cameraAware), `velocityBufferEnable = 1`, `smaaEnable = 1`, `dofEnable = 1`, `sunShaftsEnable = 1`, `godRaysEnable = 1` (all-zero params = inert), `heightFogEnable = 1` (zero density = inert), `tonemapAutoExposure = 1` (min 0.5 / max 2.0)
- `forwardPlusEnable = 1`, `forwardPlusFallback = 1`
- Peds: `pedAlphaTestEnable = 1`, `pedTexResync = 1`, `pedShadows = 1`, `stencilShadows = 1`
- `camRasterRepair = 1`, `depthHookEnable = 1`, `cloudRenderFix = 1`, `detailMaps = 1`, `noiseDetail = 1`, `stochasticTexturing = 1`, `skidmarkAlphaFix = 1`, `neoWaterDrops = 1`, `neoBloodDrops = 1`, `hairEnhanceEnable = 1`, `sssEnable = 1` (all intensities 0 = inert), `bldEnvReflect = 1`, `pipeChainEnable = 1` (refl off), `coronaZtest = 1`, `moonMaskAlpha = 1`, `lightningIlluminatesWorld = 1`, `infraredVision = 1`, `nightVision = 1`
- `ambientFloor = 0.03`, `pbrAmbientFloor = 0.05`, `pbrIblAmbientWeight = 0.8`, `vegetationAmbientBoost = 1.3`

### OFF (notable — the repairs the user runs WITHOUT)
- `stateBaselineEnable = 0` — world-baseline frame repair OFF
- `texAlphaClassify = 0` — the whole TAC classifier OFF
- `pedOpaqueContract = 0`, all `dualPass* = 0`, `defaultCbShaderHygiene = 0`, `shadowContractEnable = 0`
- `colorFilterEnable = 0` — Modern grading pass OFF (user preference)
- `ssaoEnable = 0`, `fxaaEnable = 0`, `grainEnable = 0`, `radiosityEnable = 0`, `vcsTrails = 0`
- `ragdollEnable = 0` — ragdolls OFF (user turned off; sky_physics.ini separate)
- `normalMapEnable = 0`, `skinEnhanceEnable = 0`, `vegetationEnhanceEnable = 0`, `treeFx/treeWind = 0`, `grassInstancing = 0`, `ps2Modulate* = 0`
- `vehAutoChrome = 0`, `vehEnvMask = 0`, `vehWetEnv = 0`, `fixPcCarLight = 0`, `fixShadows = 0`
- `mirrorDistortionFix = 0`, `sunGlare = 0`, `privateHooks = 0`, `profilerEnable = 0`
- `pedAmbientFloor = 0.2` present but DEAD-BUT-PARSED (W3 revert removed the consumer)

## Interpretation for future fixes
1. The user's stability comes from the CORE fixes (3-layer binds, blend/fog/Z brackets, font-flush pin, flush-site shields) — NOT from the optional repair layers (stateBaseline, texAlphaClassify, pedOpaqueContract, dualPass). Those optional layers add state churn and can interact badly; do not re-enable them by default.
2. PBR pipeline + V-style water quality 3 is the primary test mode (not Xbox as in earlier tests).
3. `colorFilterEnable = 0` is deliberate — do not "fix" it back on.
4. `ragdollEnable = 0` — softbody/ragdoll work must stay gated behind INI and default-off.
5. Any new feature must default to OFF or be inert at these settings.
6. Acceptance = no regression at THIS config; Xbox/PS2 mode testing is secondary.

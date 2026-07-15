# Backwards Compatibility

#backwards-compatibility #ini #testing

## Overview
Every `vehiclePipe=` and `buildingPipe=` INI value from the original skygfx and all forks must continue to work. Users upgrading from older versions get the same rendering by default, with new features opt-in.

---

## Compatibility Matrix: INI Keys by Origin

### aap Original Keys (all still work)

| INI Key | Status | Notes |
|---------|--------|-------|
| `buildingPipe` | ✅ Active | `PS2`, `PC`/`Xbox` supported |
| `vehiclePipe` | ✅ Active | All 9 original values supported |
| `colorFilter` | ✅ Active | `PS2`, `PC` supported (junior values also work) |
| `ps2Modulate` | ✅ Active | Global PS2 modulate |
| `dualPass` | ✅ Active | Global dual-pass |
| `ps2ModulateBuilding` | ✅ Active | Per-type override |
| `dualPassBuilding` | ✅ Active | Per-type override |
| `ps2ModulateVehicle` | ✅ Active | Per-type override |
| `dualPassVehicle` | ✅ Active | Per-type override |
| `ps2ModulateGrass` | ✅ Active | Per-type override |
| `dualPassGrass` | ✅ Active | Per-type override |
| `dualPassDefault` | ✅ Active | Default dual-pass |
| `dualPassPed` | ✅ Active | Ped dual-pass |
| `detailMaps` | ✅ Active | Detail map rendering |
| `sunGlare` | ✅ Active | Sun glare effect |
| `neoWaterDrops` | ✅ Active | Neo water drops |
| `usePCTimecyc` | ✅ Active | Use PC timecycle |
| `grassAddAmbient` | ✅ Active | Grass add ambient |
| `grassBackfaceCull` | ✅ Active | Grass backface culling |
| `pedShadows` | ✅ Active | Ped shadows |
| `stencilShadows` | ✅ Active | Stencil shadows |
| `disableClouds` | ✅ Active | Disable clouds |
| `disableGamma` | ✅ Active | Disable gamma |
| `fixPcCarLight` | ✅ Active | Fix PC car lighting |
| `lightningIlluminatesWorld` | ✅ Active | Lightning illuminates world |
| `envMapSize` | ✅ Active | Reflection map size |
| `envShininessMult` | ✅ Active | Environment shininess |
| `envSpecularityMult` | ✅ Active | Environment specularity |
| `envPower` | ✅ Active | Environment power |
| `envFresnel` | ✅ Active | Fresnel reflection |
| `neoShininessMult` | ✅ Active | Neo shininess |
| `neoSpecularityMult` | ✅ Active | Neo specularity |
| `leedsShininessMult` | ✅ Active | Leeds shininess |
| `infraredVision` | ✅ Active | Infrared vision mode |
| `nightVision` | ✅ Active | Night vision mode |
| `grainFilter` | ✅ Active | Grain filter mode |
| `blurLeft` | ✅ Active | Blur offset left |
| `blurRight` | ✅ Active | Blur offset right |
| `blurTop` | ✅ Active | Blur offset top |
| `blurBottom` | ✅ Active | Blur offset bottom |
| `radiosity` | ✅ Active | Radiosity mode |
| `doRadiosity` | ✅ Active | Enable radiosity |
| `radiosityFilterPasses` | ✅ Active | Filter passes |
| `radiosityRenderPasses` | ✅ Active | Render passes |
| `radiosityIntensity` | ✅ Active | Intensity |
| `zwriteThreshold` | ✅ Active | Z-write threshold |
| `keySwitch` | ✅ Active | Key to switch configs |
| `keyReload` | ✅ Active | Key to reload INI |

### junior Fork Keys (all still work)

| INI Key | Status | Notes |
|---------|--------|-------|
| `envMapUseLODs` | ✅ Active | Use LODs for env map |
| `envMapFarClipMult` | ✅ Active | Far clip multiplier |
| `stochasticTexturing` | ✅ Active | Stochastic sampling |
| `grassFixPlacement` | ✅ Active | Grass placement fix |
| `neoBloodDrops` | ✅ Active | Neo blood drops |
| `coronaZtest` | ✅ Active | Corona Z-test |
| `colorFilter` (III/VC/VCS) | ✅ Active | All junior filter values work |
| `rgb1Mult` | ✅ Active | RGB filter 1 multiplier |
| `rgb2Mult` | ✅ Active | RGB filter 2 multiplier |
| `vcsTrails` | ✅ Active | VCS trails |
| `trailsLimit` | ✅ Active | Trail limit |
| `trailsIntensity` | ✅ Active | Trail intensity |
| `trailsResolution` | ✅ Active | Trail resolution |
| `zwriteThresholdGrass` | ✅ Active | Grass Z-write |
| `zwriteThresholdPed` | ✅ Active | Ped Z-write |
| `smaaEnable` | ✅ Active | SMAA enable |
| `smaaPreset` | ✅ Active | SMAA quality |
| `smaaPredication` | ✅ Active | SMAA predication |
| `smaaTemporal` | ✅ Active | SMAA temporal |
| `ivMode` | ✅ Active | GTA IV mode |
| `ivDesaturation` | ✅ Active | GTA IV desaturation |
| `ivGamma` | ✅ Active | GTA IV gamma |
| `ivVignetteIntensity` | ✅ Active | GTA IV vignette intensity |
| `ivVignetteRadius` | ✅ Active | GTA IV vignette radius |
| `ivVignetteContrast` | ✅ Active | GTA IV vignette contrast |
| `ivBloomIntensity` | ✅ Active | GTA IV bloom intensity |
| `ivExposure` | ✅ Active | GTA IV exposure |
| `forceWindShader` | ✅ Active | Force wind shader |

### skygfx_plus New Keys

| INI Key | Status | Notes |
|---------|--------|-------|
| `pipeline` | ✅ New | Unified pipeline mode (PBR/PS2/Xbox/Mobile/GTAIV) |
| `qualityPreset` | ✅ New | Quality preset (0-3) |
| `fixShadows` | ✅ New | Fix shadow rendering |
| `transparentLockon` | ✅ New | Transparent lock-on |
| `explicitBuildingPipe` | ✅ New | Explicit building pipe override |
| `privateHooks` | ✅ New | Private hooks |
| `YCbCrCorrection` | ✅ New | YCbCr correction |
| `lumaScale` | ✅ New | Luma scale |
| `lumaOffset` | ✅ New | Luma offset |
| `CbScale` | ✅ New | Cb scale |
| `CbOffset` | ✅ New | Cb offset |
| `CrScale` | ✅ New | Cr scale |
| `CrOffset` | ✅ New | Cr offset |
| `ssaoEnable` | ✅ New | SSAO enable |
| `ssaoRadius` | ✅ New | SSAO radius |
| `ssaoPower` | ✅ New | SSAO power |
| `ssaoKernelSize` | ✅ New | SSAO kernel size |
| `ssaoSampleCount` | ✅ New | SSAO sample count |
| `motionBlurEnable` | ✅ New | Motion blur enable |
| `motionBlurStrength` | ✅ New | Motion blur strength |
| `motionBlurRadial` | ✅ New | Motion blur radial |
| `motionBlurSpeedFactor` | ✅ New | Motion blur speed factor |
| `motionBlurCameraAware` | ✅ New | Motion blur camera aware |
| `sssPostProcessEnable` | ✅ New | SSS post-process enable |
| `sssPostProcessStrength` | ✅ New | SSS post-process strength |
| `sssPostProcessRadius` | ✅ New | SSS post-process radius |
| `sssPostProcessThreshold` | ✅ New | SSS post-process threshold |
| `skinEnhanceEnable` | ✅ New | Skin enhancement enable |
| `skinWrapFactor` | ✅ New | Skin wrap factor |
| `skinSpecularPower` | ✅ New | Skin specular power |
| `skinSpecularStrength` | ✅ New | Skin specular strength |
| `skinSSSStrength` | ✅ New | Skin SSS strength |
| `hairEnhanceEnable` | ✅ New | Hair enhancement enable |
| `hairAnisotropicPower` | ✅ New | Hair anisotropic power |
| `hairAnisotropicStrength` | ✅ New | Hair anisotropic strength |
| `hairSSSStrength` | ✅ New | Hair SSS strength |
| `vegetationEnhanceEnable` | ✅ New | Vegetation enhancement enable |
| `vegetationSSSStrength` | ✅ New | Vegetation SSS strength |
| `vegetationAmbientBoost` | ✅ New | Vegetation ambient boost |
| `enableNormalMaps` | ✅ New | Normal mapping enable |
| `normalMapIntensity` | ✅ New | Normal map intensity |
| `normalMapPlayerOnly` | ✅ New | Normal map player-only |
| `normalMapBuilding` | ✅ New | Building normal maps |
| `normalMapVehicle` | ✅ New | Vehicle normal maps |
| `normalMapDebug` | ✅ New | Normal map debug |
| `normalMapDebugMode` | ✅ New | Normal map debug mode |
| `normalBufferEnable` | ✅ New | Faux normal buffer enable |
| `normalBufferOffset` | ✅ New | Normal buffer offset |
| `normalBufferScale` | ✅ New | Normal buffer scale |
| `edgeTessEnable` | ✅ New | Edge tessellation enable |
| `edgeTessStrength` | ✅ New | Edge tessellation strength |
| `edgeTessThreshold` | ✅ New | Edge tessellation threshold |
| `pipeChainEnable` | ✅ New | Pipe chain enable |
| `pipeChainIntensity` | ✅ New | Pipe chain intensity |
| `ivSaturation` | ✅ New | GTA IV saturation |
| `ivCurves` | ✅ New | GTA IV curves |
| `unifiedEnable` | ✅ New | Unified pipeline enable |
| `unifiedVersion` | ✅ New | Unified pipeline version |
| `unifiedSatBoost` | ✅ New | Unified saturation boost |
| `unifiedIblTintStrength` | ✅ New | Unified IBL tint strength |
| `unifiedSsaoNoiseScale` | ✅ New | Unified SSAO noise |
| `unifiedShadowSoftness` | ✅ New | Unified shadow softness |
| `unifiedCloudShadowStr` | ✅ New | Unified cloud shadow |
| `unifiedSunShadowStr` | ✅ New | Unified sun shadow |
| `unifiedVertexAOBoost` | ✅ New | Unified vertex AO boost |
| `unifiedDayReduction` | ✅ New | Unified day reduction |
| `unifiedPointLightOverride` | ✅ New | Unified point light override |
| `unifiedSmaaThreshold` | ✅ New | Unified SMAA threshold |
| `unifiedSmaaCornerRounding` | ✅ New | Unified SMAA corner rounding |
| `unifiedSmaaMaxSearchSteps` | ✅ New | Unified SMAA max search steps |
| `unifiedShowMenu` | ✅ New | Unified debug menu |
| `unifiedShowOverlay` | ✅ New | Unified debug overlay |
| `unifiedDebugOcclusion` | ✅ New | Unified debug occlusion |
| `unifiedEnablePrePass` | ✅ New | Unified pre-pass |
| `unifiedEnableEdgeDetect` | ✅ New | Unified edge detection |
| `unifiedEnableOcclusion` | ✅ New | Unified occlusion |
| `unifiedEnableStoredShadows` | ✅ New | Unified stored shadows |
| `unifiedEnableCloudShadows` | ✅ New | Unified cloud shadows |
| `unifiedEnableSunShadows` | ✅ New | Unified sun shadows |
| `unifiedEnableTimeOfDay` | ✅ New | Unified time-of-day |
| `unifiedEnableVertexAO` | ✅ New | Unified vertex AO |
| `unifiedEnablePointLightOverride` | ✅ New | Unified point light override |
| `unifiedEnablePostPass` | ✅ New | Unified post-pass |
| `unifiedEnableIBL` | ✅ New | Unified IBL |
| `unifiedEnableIBLTint` | ✅ New | Unified IBL tint |
| `unifiedEnableSurfaceWeights` | ✅ New | Unified surface weights |
| `unifiedEnableGrading` | ✅ New | Unified color grading |
| `unifiedEnableGamma` | ✅ New | Unified gamma |

---

## Verified Vehicle Pipe Modes

| INI Value | Status | Render Path | Shaders |
|-----------|--------|-------------|---------|
| `PS2` | ✅ OK | `_CB_PS2` | `simplePS` + `ps2EnvSpecFxPS` |
| `PC` | ✅ OK | `_CB_exe` | Original game function |
| `Xbox` | ✅ OK | `_CB_Xbox` | NULL PS (FFP) |
| `Spec` | ✅ OK | `_CB_Specular` | `simplePS` + `specCarFxPS` |
| `Neo` | ✅ OK | `CarPipe::RenderCallback` | Neo pipe |
| `LCS` / `VCS` | ✅ OK | `_CB_leeds` | `simplePS` + leeds VS |
| `Mobile` | ✅ OK | `_CB_mobile` | `mobileVehiclePipePS` + `simplePS` |
| `Env` | ✅ OK | `_CB_Env` | `envCarPS` / PBR fallback |
| `GTAIV` | ✅ OK | `_CB_PS2` (ivMode) | `gtaivVehiclePS` |
| `Modern` | ✅ OK | `_CB_Env` | `VehiclePBR_Modern` / `Glass_Vehicle` / `Rubber_Vehicle` |

## Verified Building Pipe Modes

| INI Value | Status | Notes |
|-----------|--------|-------|
| `PS2` | ✅ OK | PS2 building VS/PS |
| `PC` / `Xbox` | ✅ OK | Xbox building with env maps |
| `GTAIV` | ✅ OK | GTA IV forward pass |
| `PBR` | ✅ OK | PBR building shaders |

## Quality Presets

| Preset | Vehicle | Building | PostFX |
|--------|---------|----------|--------|
| LOW (0) | PS2 | PS2 | None |
| MEDIUM (1) | PC | Xbox | SMAA LOW |
| HIGH (2) | Modern | Xbox | SMAA HIGH + SSAO + Motion Blur |
| ULTRA (3) | Modern | Xbox | SMAA ULTRA + SSAO + Motion Blur + SSS |

## Shader Loading Integrity
All `makePS()`/`makeVS()` calls in `CreateShaders()` verified:
- Every IDR has a matching `#define` in `resource.h`
- Every IDR has a matching `RCDATA` in `Resource.rc`
- Every CSO file exists on disk (either standalone or compiled from wrapper)
- No IDR collisions (stochastic IDs moved to 251-253 range)

## Deprecated / Renamed Keys

| Key | Status | Notes |
|-----|--------|-------|
| `tagsBuildingPipe` | Deprecated | Now follows `buildingPipe` automatically |
| `forceWindShader` | Legacy | Still parsed but superseded by `stochasticTexturing` |
| `ps2grassFiles` | Removed | Was commented out in aap, never used in skygfx_plus |

## See Also
- [[Vehicle Pipeline]] — Full mode details
- [[Building Pipeline]] — Building mode details
- [[INI Configuration]] — All config fields
- [[Three-Codebase Comparison]] — Full INI comparison across all codebases

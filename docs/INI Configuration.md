# INI Configuration

#ini #config

## Overview
All features are configured via `skygfx.ini`. Multiple INI files supported (`skygfx.ini`, `skygfx.1.ini` through `skygfx.9.ini`). All keys live under the `[SkyGfx]` section.

## Quality Presets
Set `qualityPreset=` to apply defaults. Individual INI values override preset defaults.

| Preset | Value | Description |
|--------|-------|-------------|
| LOW | 0 | PS2 classic, no modern features |
| MEDIUM | 1 | PC classic, SMAA LOW |
| HIGH | 2 | Enhanced, SMAA HIGH + SSAO + Motion Blur |
| ULTRA | 3 | Full PBR, all features maxed |

See [[Implemented Features]] for details on what each preset enables.

---

## Pipeline Selection

| Key | Type | Default | Options | Origin |
|-----|------|---------|---------|--------|
| `pipeline` | string | *(none)* | `PBR`, `PS2`, `Xbox`, `Mobile`, `GTAIV` | skygfx_plus |
| `buildingPipe` | string | *(none)* | `PS2`, `PC`, `Xbox`, `GTAIV`, `PBR` | aap/junior |
| `vehiclePipe` | string | *(none)* | `PS2`, `PC`, `Xbox`, `Spec`, `Neo`, `LCS`/`Leeds`, `VCS`, `Mobile`, `Env`, `GTAIV`, `Modern` | aap/junior |
| `colorFilter` | string | `PC` | `None`, `PS2`, `PC`, `Mobile`, `III`, `VC`, `VCS`, `GTAIV`, `Modern` | aap/junior |
| `qualityPreset` | int | `3` | `0`=LOW, `1`=MED, `2`=HIGH, `3`=ULTRA | skygfx_plus |

## Core Rendering

| Key | Type | Default | Origin | Notes |
|-----|------|---------|--------|-------|
| `ps2Modulate` | int | `0` | aap | Global PS2 modulate |
| `dualPass` | int | `0` | aap | Global dual-pass |
| `ps2ModulateBuilding` | int | *(from ps2Modulate)* | aap | Per-type override |
| `dualPassBuilding` | int | *(from dualPass)* | aap | Per-type override |
| `ps2ModulateVehicle` | int | *(from ps2Modulate)* | aap | Per-type override |
| `dualPassVehicle` | int | *(from dualPass)* | aap | Per-type override |
| `ps2ModulateGrass` | int | *(from ps2Modulate)* | aap | Per-type override |
| `dualPassGrass` | int | *(from dualPass)* | aap | Per-type override |
| `dualPassDefault` | int | *(from dualPass)* | aap | Default dual-pass |
| `dualPassPed` | int | *(from dualPass)* | aap | Ped dual-pass |
| `detailMaps` | int | `0` | aap | Detail map rendering |
| `stochasticTexturing` | int | `0` | junior | Stochastic sampling |
| `sunGlare` | int | `-1` | aap | Sun glare (-1 = auto) |
| `neoWaterDrops` | int | `-1` | aap | Neo water drops (-1 = auto) |
| `neoBloodDrops` | int | `0` | junior | Neo blood drops |
| `usePCTimecyc` | int | `0` | aap | Use PC timecycle |
| `grassAddAmbient` | int | `0` | aap | Grass add ambient |
| `grassBackfaceCull` | int | `1` | aap | Grass backface culling |
| `grassFixPlacement` | int | `0` | aap | Grass placement fix |
| `pedShadows` | bool | *(from game)* | aap | Ped shadows |
| `stencilShadows` | bool | *(from game)* | aap | Stencil shadows |
| `disableClouds` | int | `0` | aap | Disable clouds |
| `disableGamma` | int | `0` | aap | Disable gamma |
| `fixPcCarLight` | int | `0` | aap | Fix PC car lighting |
| `lightningIlluminatesWorld` | int | `0` | aap | Lightning illuminates world |
| `fixShadows` | int | `0` | skygfx_plus | Fix shadow rendering |
| `transparentLockon` | int | `0` | skygfx_plus | Transparent lock-on |
| `coronaZtest` | int | `-1` | junior | Corona Z-test |
| `explicitBuildingPipe` | int | `-1` | skygfx_plus | Explicit building pipe override |

## Environment Mapping

| Key | Type | Default | Origin | Notes |
|-----|------|---------|--------|-------|
| `envMapSize` | int | `256` | aap | Reflection map size (power of 2) |
| `envMapUseLODs` | int | `0` | junior | Use LODs for env map |
| `envMapFarClipMult` | float | `1.0` | junior | Far clip multiplier |
| `envShininessMult` | float | `1.0` | aap | Environment shininess |
| `envSpecularityMult` | float | `1.0` | aap | Environment specularity |
| `envPower` | float | `20.0` | aap | Environment power |
| `envFresnel` | float | `0.7` | aap | Fresnel reflection |
| `neoShininessMult` | float | `1.0` | aap | Neo shininess |
| `neoSpecularityMult` | float | `1.0` | aap | Neo specularity |
| `leedsShininessMult` | float | `1.0` | aap | Leeds shininess |

## Color Filters & Vision

| Key | Type | Default | Origin | Notes |
|-----|------|---------|--------|-------|
| `infraredVision` | string | `PS2` | aap | `PS2` or `PC` |
| `nightVision` | string | `PS2` | aap | `PS2` or `PC` |
| `grainFilter` | string | `PS2` | aap | `PS2` or `PC` |
| `blurLeft` | int | `4000` | aap | Blur offset left (4000 = game default) |
| `blurRight` | int | `4000` | aap | Blur offset right |
| `blurTop` | int | `4000` | aap | Blur offset top |
| `blurBottom` | int | `4000` | aap | Blur offset bottom |
| `rgb1Mult` | float | `1.0` | junior | RGB filter 1 multiplier |
| `rgb2Mult` | float | `1.0` | junior | RGB filter 2 multiplier |

## Radiosity

| Key | Type | Default | Origin | Notes |
|-----|------|---------|--------|-------|
| `radiosity` | string | `Shader` | aap/junior | `PS2` or `Shader` |
| `doRadiosity` | int | *(from game)* | aap | Enable radiosity |
| `radiosityFilterPasses` | int | `2` | aap | Filter passes |
| `radiosityRenderPasses` | int | `1` | aap | Render passes |
| `radiosityIntensity` | int | `0x23` | aap | Intensity |

## VCS Trails

| Key | Type | Default | Origin | Notes |
|-----|------|---------|--------|-------|
| `vcsTrails` | int | `0` | junior | Enable VCS trails |
| `trailsLimit` | int | `80` | junior | Trail limit |
| `trailsIntensity` | int | `38` | junior | Trail intensity |
| `trailsResolution` | int | `1` | junior | Trail resolution |

## Z-Write Threshold

| Key | Type | Default | Origin | Notes |
|-----|------|---------|--------|-------|
| `zwriteThreshold` | int | `128` | aap | Z-write threshold (0-255) |
| `zwriteThresholdGrass` | int | `128` | junior | Grass Z-write |
| `zwriteThresholdPed` | int | `128` | junior | Ped Z-write |

## YCbCr Correction

| Key | Type | Default | Origin | Notes |
|-----|------|---------|--------|-------|
| `YCbCrCorrection` | int | `0` | junior | Enable YCbCr correction |
| `lumaScale` | float | `219/255` | skygfx_plus | Luma scale |
| `lumaOffset` | float | `16/255` | skygfx_plus | Luma offset |
| `CbScale` | float | `1.23` | skygfx_plus | Cb scale |
| `CbOffset` | float | `0.0` | skygfx_plus | Cb offset |
| `CrScale` | float | `1.23` | skygfx_plus | Cr scale |
| `CrOffset` | float | `0.0` | skygfx_plus | Cr offset |

---

## SMAA (Anti-Aliasing)

| Key | Type | Default | Origin | Notes |
|-----|------|---------|--------|-------|
| `smaaEnable` | int | `1` | junior/skygfx_plus | Enable SMAA |
| `smaaPreset` | int | `3` | junior/skygfx_plus | `0`=LOW, `1`=MED, `2`=HIGH, `3`=ULTRA |
| `smaaPredication` | int | `0` | junior/skygfx_plus | Depth-based edge detection |
| `smaaTemporal` | int | `0` | junior/skygfx_plus | Temporal AA |

## SSAO (Ambient Occlusion)

| Key | Type | Default | Origin | Notes |
|-----|------|---------|--------|-------|
| `ssaoEnable` | int | `1` | skygfx_plus | Enable SSAO |
| `ssaoRadius` | float | `0.8` | skygfx_plus | AO radius |
| `ssaoPower` | float | `1.5` | skygfx_plus | AO power |
| `ssaoKernelSize` | float | `16` | skygfx_plus | Kernel size |
| `ssaoSampleCount` | int | `16` | skygfx_plus | Sample count |

## Motion Blur

| Key | Type | Default | Origin | Notes |
|-----|------|---------|--------|-------|
| `motionBlurEnable` | int | `0` | skygfx_plus | Enable motion blur |
| `motionBlurStrength` | float | `0.5` | skygfx_plus | Overall intensity |
| `motionBlurRadial` | float | `0.3` | skygfx_plus | Radial component |
| `motionBlurSpeedFactor` | float | `0.5` | skygfx_plus | Speed factor |
| `motionBlurCameraAware` | int | `1` | skygfx_plus | Reduce on fast camera |

## SSS Post-Process

| Key | Type | Default | Origin | Notes |
|-----|------|---------|--------|-------|
| `sssPostProcessEnable` | int | `0` | skygfx_plus | Enable post-process SSS |
| `sssPostProcessStrength` | float | `0.3` | skygfx_plus | SSS strength |
| `sssPostProcessRadius` | float | `4.0` | skygfx_plus | Blur radius |
| `sssPostProcessThreshold` | float | `0.1` | skygfx_plus | Depth threshold |

## Skin Enhancement

| Key | Type | Default | Origin | Notes |
|-----|------|---------|--------|-------|
| `skinEnhanceEnable` | int | `0` | skygfx_plus | Enable skin enhancement |
| `skinWrapFactor` | float | `0.5` | skygfx_plus | Wrap lighting factor |
| `skinSpecularPower` | float | `16.0` | skygfx_plus | Specular sharpness |
| `skinSpecularStrength` | float | `0.3` | skygfx_plus | Specular intensity |
| `skinSSSStrength` | float | `0.4` | skygfx_plus | SSS strength |

## Hair Enhancement

| Key | Type | Default | Origin | Notes |
|-----|------|---------|--------|-------|
| `hairEnhanceEnable` | int | `0` | skygfx_plus | Enable hair enhancement |
| `hairAnisotropicPower` | float | `32.0` | skygfx_plus | Highlight sharpness |
| `hairAnisotropicStrength` | float | `0.5` | skygfx_plus | Highlight intensity |
| `hairSSSStrength` | float | `0.2` | skygfx_plus | Hair SSS strength |

## Vegetation Enhancement

| Key | Type | Default | Origin | Notes |
|-----|------|---------|--------|-------|
| `vegetationEnhanceEnable` | int | `0` | skygfx_plus | Enable vegetation enhancement |
| `vegetationSSSStrength` | float | `0.3` | skygfx_plus | Translucency strength |
| `vegetationAmbientBoost` | float | `1.2` | skygfx_plus | Ambient multiplier |

## Normal Mapping

| Key | Type | Default | Origin | Notes |
|-----|------|---------|--------|-------|
| `enableNormalMaps` | int | `1` | skygfx_plus | Enable normal mapping |
| `normalMapIntensity` | float | `1.0` | skygfx_plus | Normal map intensity |
| `normalMapPlayerOnly` | int | `1` | skygfx_plus | Player-only normals |
| `normalMapBuilding` | int | `1` | skygfx_plus | Building normal maps |
| `normalMapVehicle` | int | `1` | skygfx_plus | Vehicle normal maps |
| `normalMapDebug` | int | `0` | skygfx_plus | Debug visualization |
| `normalMapDebugMode` | int | `0` | skygfx_plus | Debug mode |

## Normal Buffer (Faux)

| Key | Type | Default | Origin | Notes |
|-----|------|---------|--------|-------|
| `normalBufferEnable` | int | `0` | skygfx_plus | Enable faux normal buffer |
| `normalBufferOffset` | float | `0.5` | skygfx_plus | Stereo disparity offset |
| `normalBufferScale` | float | `1.0` | skygfx_plus | Normal scale |

## Edge Tessellation

| Key | Type | Default | Origin | Notes |
|-----|------|---------|--------|-------|
| `edgeTessEnable` | int | `0` | skygfx_plus | Enable edge tessellation |
| `edgeTessStrength` | float | `0.01` | skygfx_plus | Displacement strength |
| `edgeTessThreshold` | float | `0.1` | skygfx_plus | Edge detection threshold |

## Pipe Chain (4-Pass)

| Key | Type | Default | Origin | Notes |
|-----|------|---------|--------|-------|
| `pipeChainEnable` | int | `0` | skygfx_plus | Enable 4-pass pipe chain |
| `pipeChainIntensity` | float | `0.5` | skygfx_plus | Chain intensity |

## GTA IV Mode

| Key | Type | Default | Origin | Notes |
|-----|------|---------|--------|-------|
| `ivMode` | int | `0` | junior/skygfx_plus | Enable GTA IV mode |
| `ivDesaturation` | float | `1.0` | junior/skygfx_plus | Desaturation strength |
| `ivGamma` | float | `1.0` | junior/skygfx_plus | Gamma correction |
| `ivSaturation` | float | `0.0` | skygfx_plus | Saturation |
| `ivCurves` | float | `0.0` | skygfx_plus | Curves |
| `ivVignetteIntensity` | float | `0.0` | junior/skygfx_plus | Vignette intensity |
| `ivVignetteRadius` | float | `0.75` | junior/skygfx_plus | Vignette radius |
| `ivVignetteContrast` | float | `1.5` | junior/skygfx_plus | Vignette contrast |
| `ivBloomIntensity` | float | `0.0` | junior/skygfx_plus | Bloom intensity |
| `ivExposure` | float | `1.0` | junior/skygfx_plus | Exposure |

---

## Unified Pipeline (skygfx_plus)

| Key | Type | Default | Notes |
|-----|------|---------|-------|
| `unifiedEnable` | bool | `1` | Enable unified pipeline |
| `unifiedVersion` | int | `2` | Pipeline version |
| `unifiedSatBoost` | float | `0.08` | Saturation boost |
| `unifiedIblTintStrength` | float | `0.3` | IBL tint strength |
| `unifiedSsaoNoiseScale` | float | `4.0` | SSAO noise |
| `unifiedShadowSoftness` | float | `0.5` | Shadow softness |
| `unifiedCloudShadowStr` | float | `0.3` | Cloud shadow strength |
| `unifiedSunShadowStr` | float | `0.8` | Sun shadow strength |
| `unifiedVertexAOBoost` | float | `1.4` | Vertex AO boost |
| `unifiedDayReduction` | float | `0.15` | Day reduction |
| `unifiedPointLightOverride` | float | `0.2` | Point light override |
| `unifiedSmaaThreshold` | float | `0.1` | SMAA threshold |
| `unifiedSmaaCornerRounding` | float | `25.0` | SMAA corner rounding |
| `unifiedSmaaMaxSearchSteps` | float | `8.0` | SMAA max search steps |
| `unifiedShowMenu` | bool | `0` | Show debug menu |
| `unifiedShowOverlay` | bool | `0` | Show debug overlay |
| `unifiedDebugOcclusion` | bool | `0` | Debug occlusion |
| `unifiedEnablePrePass` | bool | `1` | Enable pre-pass |
| `unifiedEnableEdgeDetect` | bool | `1` | Enable edge detection |
| `unifiedEnableOcclusion` | bool | `1` | Enable occlusion |
| `unifiedEnableStoredShadows` | bool | `1` | Enable stored shadows |
| `unifiedEnableCloudShadows` | bool | `1` | Enable cloud shadows |
| `unifiedEnableSunShadows` | bool | `1` | Enable sun shadows |
| `unifiedEnableTimeOfDay` | bool | `1` | Enable time-of-day |
| `unifiedEnableVertexAO` | bool | `1` | Enable vertex AO |
| `unifiedEnablePointLightOverride` | bool | `1` | Enable point light override |
| `unifiedEnablePostPass` | bool | `1` | Enable post-pass |
| `unifiedEnableIBL` | bool | `1` | Enable IBL |
| `unifiedEnableIBLTint` | bool | `1` | Enable IBL tint |
| `unifiedEnableSurfaceWeights` | bool | `1` | Enable surface weights |
| `unifiedEnableGrading` | bool | `1` | Enable color grading |
| `unifiedEnableGamma` | bool | `1` | Enable gamma |

## Debug / Misc

| Key | Type | Default | Origin | Notes |
|-----|------|---------|--------|-------|
| `keySwitch` | hex | `0x0` | aap | Key to switch configs |
| `keyReload` | hex | `0x0` | aap | Key to reload INI |
| `privateHooks` | int | `0` | skygfx_plus | Private hooks |
| `forceWindShader` | int | `0` | junior | Force wind shader |

## See Also
- [[Vehicle Pipeline]] — Vehicle pipe options
- [[Building Pipeline]] — Building pipe options
- [[Backwards Compatibility]] — All settings verified
- [[Three-Codebase Comparison]] — Full INI comparison across codebases

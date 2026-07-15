# INI Configuration

#ini #config

## Overview
All features are configured via `skygfx.ini`. Multiple INI files supported (`skygfx.ini`, `skygfx.1.ini` through `skygfx.9.ini`). All keys live under the `[SkyGfx]` section.

INI parsing happens in two locations:
- `config.cpp` — base configuration (legacy aap/junior keys)
- `main.cpp::refreshIni()` — unified pipeline, quality presets, and additional keys

## Pipeline Selection

| Key | Type | Default | Options | Notes |
|-----|------|---------|---------|-------|
| `pipeline` | string | `PBR` | `PBR`, `PS2`, `Xbox`, `Mobile`, `GTAIV` | Locks building/vehicle pipes to preset. Parsed in main.cpp. |
| `qualityPreset` | int | `3` | `0`=LOW, `1`=MEDIUM, `2`=HIGH, `3`=ULTRA | Sets feature defaults. Individual INI values override. Parsed in main.cpp. |
| `buildingPipe` | string | *(from pipeline)* | `PS2`, `Xbox`, `GTAIV`, `PBR` | Explicit override of pipeline mapping. Parsed in main.cpp. |
| `vehiclePipe` | string | *(from pipeline)* | `PS2`, `PC`, `Xbox`, `Specular`, `Neo`, `Leeds`, `VCS`, `Mobile`, `Env`, `GTAIV`, `Modern` | Explicit override of pipeline mapping. Parsed in main.cpp. |
| `colorFilter` | string | `PC` | `None`, `PS2`, `PC`, `Mobile`, `III`, `VC`, `VCS`, `GTAIV`, `Modern` | Color filter mode. Parsed in main.cpp. |

**Pipeline mapping** (when `buildingPipe`/`vehiclePipe` are not explicitly set):

| pipeline | buildingPipe | vehiclePipe | colorFilter |
|----------|-------------|-------------|-------------|
| PBR | PBR | Modern | Modern |
| PS2 | PS2 | PS2 | — |
| Xbox | Xbox | Xbox | — |
| Mobile | Xbox | Mobile | — |
| GTAIV | GTAIV | GTAIV | — |

## Quality Presets

| Preset | Value | Vehicle | Building | PostFX |
|--------|-------|---------|----------|--------|
| LOW | 0 | PS2 | PS2 | None |
| MEDIUM | 1 | PC | Xbox | SMAA LOW |
| HIGH | 2 | Modern | Xbox | SMAA HIGH + SSAO + Motion Blur + SSS |
| ULTRA | 3 | Modern | Xbox | SMAA ULTRA + SSAO + Motion Blur + SSS + all enhancements |

Preset defaults are applied first, then individual INI values override them.

## Core Rendering

| Key | Type | Default | Parsed In | Notes |
|-----|------|---------|-----------|-------|
| `ps2Modulate` | int | `0` | config.cpp | Global PS2 modulate |
| `dualPass` | int | `0` | config.cpp | Global dual-pass |
| `ps2ModulateBuilding` | int | *(from ps2Modulate)* | config.cpp | Per-type override |
| `dualPassBuilding` | int | *(from dualPass)* | config.cpp | Per-type override |
| `dualPassVehicle` | int | *(from dualPass)* | config.cpp | Per-type override |
| `dualPassGrass` | int | *(from dualPass)* | config.cpp | Per-type override |
| `dualPassDefault` | int | *(from dualPass)* | config.cpp | Default dual-pass |
| `dualPassPed` | int | *(from dualPass)* | config.cpp | Ped dual-pass |
| `detailMaps` | int | `0` | main.cpp | Detail map rendering |
| `stochasticTexturing` | int | `0` | main.cpp | Stochastic sampling |
| `usePCTimecyc` | int | `0` | config.cpp | Use PC timecycle |
| `grassAddAmbient` | int | `0` | config.cpp | Grass add ambient |
| `grassBackfaceCull` | int | `1` | config.cpp | Grass backface culling |
| `grassFixPlacement` | int | `0` | config.cpp | Grass placement fix |
| `pedShadows` | bool | *(from game)* | config.cpp | Ped shadows |
| `stencilShadows` | bool | *(from game)* | config.cpp | Stencil shadows |
| `disableClouds` | int | `0` | config.cpp | Disable clouds |
| `disableGamma` | int | `0` | config.cpp | Disable gamma |
| `fixPcCarLight` | int | `0` | config.cpp | Fix PC car lighting |
| `lightningIlluminatesWorld` | int | `0` | config.cpp | Lightning illuminates world |
| `fixShadows` | int | `0` | config.cpp | Fix shadow rendering |
| `transparentLockon` | int | `0` | config.cpp | Transparent lock-on |
| `explicitBuildingPipe` | int | `-1` | config.cpp | Explicit building pipe override (-1 = auto) |

## Environment Mapping

| Key | Type | Default | Parsed In | Notes |
|-----|------|---------|-----------|-------|
| `envMapSize` | int | `256` | config.cpp | Reflection map size (rounded to power of 2) |
| `envMapUseLODs` | int | `0` | main.cpp | Use LODs for env map |
| `envMapFarClipMult` | float | `1.0` | main.cpp | Far clip multiplier |
| `envShininessMult` | float | `1.0` | config.cpp | Environment shininess |
| `envSpecularityMult` | float | `1.0` | config.cpp | Environment specularity |
| `envPower` | float | `20.0` | config.cpp | Environment power |
| `envFresnel` | float | `0.7` | config.cpp | Fresnel reflection |
| `neoShininessMult` | float | `1.0` | config.cpp | Neo shininess |
| `neoSpecularityMult` | float | `1.0` | config.cpp | Neo specularity |
| `leedsShininessMult` | float | `1.0` | config.cpp | Leeds shininess |
| `sunGlare` | int | `-1` | config.cpp | Sun glare (-1 = auto/disable) |
| `neoWaterDrops` | int | `-1` | config.cpp | Neo water drops (-1 = auto/disable) |
| `neoBloodDrops` | int | `0` | config.cpp | Neo blood drops |

## Color Filters & Vision

| Key | Type | Default | Parsed In | Notes |
|-----|------|---------|-----------|-------|
| `infraredVision` | string | `PS2` | main.cpp | `PS2` or `PC` |
| `nightVision` | string | `PS2` | main.cpp | `PS2` or `PC` |
| `grainFilter` | string | `PS2` | main.cpp | `PS2` or `PC` |
| `blurLeft` | int | `4000` | config.cpp | Blur offset left (4000 = game default) |
| `blurRight` | int | `4000` | config.cpp | Blur offset right |
| `blurTop` | int | `4000` | config.cpp | Blur offset top |
| `blurBottom` | int | `4000` | config.cpp | Blur offset bottom |
| `rgb1Mult` | float | `1.0` | main.cpp | RGB filter 1 multiplier |
| `rgb2Mult` | float | `1.0` | main.cpp | RGB filter 2 multiplier |

## Radiosity

| Key | Type | Default | Parsed In | Notes |
|-----|------|---------|-----------|-------|
| `radiosity` | string | `Shader` | config.cpp | `PS2` or `Shader` |
| `doRadiosity` | int | *(from game)* | config.cpp | Enable radiosity |
| `radiosityFilterPasses` | int | `2` | config.cpp | Filter passes |
| `radiosityRenderPasses` | int | `1` | config.cpp | Render passes |
| `radiosityIntensity` | int | `0x23` | config.cpp | Intensity |

## VCS Trails

| Key | Type | Default | Parsed In | Notes |
|-----|------|---------|-----------|-------|
| `vcsTrails` | int | `0` | config.cpp | Enable VCS trails |
| `trailsLimit` | int | `80` | config.cpp | Trail limit |
| `trailsIntensity` | int | `38` | config.cpp | Trail intensity |
| `trailsResolution` | int | `1` | main.cpp | Trail resolution |

## Z-Write Threshold

| Key | Type | Default | Parsed In | Notes |
|-----|------|---------|-----------|-------|
| `zwriteThreshold` | int | `128` | config.cpp | Z-write threshold (0-255) |
| `zwriteThresholdGrass` | int | `128` | main.cpp | Grass Z-write (0-255) |
| `zwriteThresholdPed` | int | `128` | main.cpp | Ped Z-write (0-255) |

## YCbCr Correction

| Key | Type | Default | Parsed In | Notes |
|-----|------|---------|-----------|-------|
| `YCbCrCorrection` | int | `0` | config.cpp | Enable YCbCr correction |
| `lumaScale` | float | `219/255` | config.cpp | Luma scale |
| `lumaOffset` | float | `16/255` | config.cpp | Luma offset |
| `CbScale` | float | `1.23` | config.cpp | Cb scale |
| `CbOffset` | float | `0.0` | config.cpp | Cb offset |
| `CrScale` | float | `1.23` | config.cpp | Cr scale |
| `CrOffset` | float | `0.0` | config.cpp | Cr offset |

## SMAA (Anti-Aliasing)

| Key | Type | Default | Parsed In | Notes |
|-----|------|---------|-----------|-------|
| `smaaEnable` | int | `1` | config.cpp | Enable SMAA |
| `smaaPreset` | int | `3` | config.cpp | `0`=LOW, `1`=MEDIUM, `2`=HIGH, `3`=ULTRA |
| `smaaPredication` | int | `0` | config.cpp | Depth-based edge detection |
| `smaaTemporal` | int | `0` | config.cpp | Temporal AA |

## SSAO (Ambient Occlusion)

| Key | Type | Default | Parsed In | Notes |
|-----|------|---------|-----------|-------|
| `ssaoEnable` | int | `1` | config.cpp | Enable SSAO |
| `ssaoRadius` | float | `0.8` | config.cpp | AO radius |
| `ssaoPower` | float | `1.5` | config.cpp | AO power |
| `ssaoKernelSize` | float | `16` | config.cpp | Kernel size |
| `ssaoSampleCount` | int | `16` | config.cpp | Sample count |

## Motion Blur

| Key | Type | Default | Parsed In | Notes |
|-----|------|---------|-----------|-------|
| `motionBlurEnable` | int | `0` | config.cpp | Enable motion blur |
| `motionBlurStrength` | float | `0.5` | config.cpp | Overall intensity |
| `motionBlurRadial` | float | `0.3` | config.cpp | Radial component |
| `motionBlurSpeedFactor` | float | `0.5` | config.cpp | Speed factor |
| `motionBlurCameraAware` | int | `1` | config.cpp | Reduce on fast camera |

## SSS Post-Process

| Key | Type | Default | Parsed In | Notes |
|-----|------|---------|-----------|-------|
| `sssPostProcessEnable` | int | `0` | config.cpp | Enable post-process SSS |
| `sssPostProcessStrength` | float | `0.3` | config.cpp | SSS strength |
| `sssPostProcessRadius` | float | `4.0` | config.cpp | Blur radius |
| `sssPostProcessThreshold` | float | `0.1` | config.cpp | Depth threshold |

## Skin Enhancement

| Key | Type | Default | Parsed In | Notes |
|-----|------|---------|-----------|-------|
| `skinEnhanceEnable` | int | `0` | config.cpp | Enable skin enhancement |
| `skinWrapFactor` | float | `0.5` | config.cpp | Wrap lighting factor |
| `skinSpecularPower` | float | `16.0` | config.cpp | Specular sharpness |
| `skinSpecularStrength` | float | `0.3` | config.cpp | Specular intensity |
| `skinSSSStrength` | float | `0.4` | config.cpp | SSS strength |

## Hair Enhancement

| Key | Type | Default | Parsed In | Notes |
|-----|------|---------|-----------|-------|
| `hairEnhanceEnable` | int | `0` | config.cpp | Enable hair enhancement |
| `hairAnisotropicPower` | float | `32.0` | config.cpp | Highlight sharpness |
| `hairAnisotropicStrength` | float | `0.5` | config.cpp | Highlight intensity |
| `hairSSSStrength` | float | `0.2` | config.cpp | Hair SSS strength |

## Vegetation Enhancement

| Key | Type | Default | Parsed In | Notes |
|-----|------|---------|-----------|-------|
| `vegetationEnhanceEnable` | int | `0` | config.cpp | Enable vegetation enhancement |
| `vegetationSSSStrength` | float | `0.3` | config.cpp | Translucency strength |
| `vegetationAmbientBoost` | float | `1.2` | config.cpp | Ambient multiplier |

## Normal Mapping

| Key | Type | Default | Parsed In | Notes |
|-----|------|---------|-----------|-------|
| `enableNormalMaps` | int | `1` | config.cpp | Enable normal mapping |
| `normalMapIntensity` | float | `1.0` | config.cpp | Normal map intensity |
| `normalMapPlayerOnly` | int | `1` | config.cpp | Player-only normals |
| `normalMapBuilding` | int | `1` | config.cpp | Building normal maps |
| `normalMapVehicle` | int | `1` | config.cpp | Vehicle normal maps |
| `normalMapDebug` | int | `0` | config.cpp | Debug visualization |
| `normalMapDebugMode` | int | `0` | config.cpp | Debug mode |

## Normal Buffer (Faux)

| Key | Type | Default | Parsed In | Notes |
|-----|------|---------|-----------|-------|
| `normalBufferEnable` | int | `0` | config.cpp | Enable faux normal buffer |
| `normalBufferOffset` | float | `0.5` | config.cpp | Stereo disparity offset |
| `normalBufferScale` | float | `1.0` | config.cpp | Normal scale |

## Edge Tessellation

| Key | Type | Default | Parsed In | Notes |
|-----|------|---------|-----------|-------|
| `edgeTessEnable` | int | `0` | config.cpp | Enable edge tessellation |
| `edgeTessStrength` | float | `0.01` | config.cpp | Displacement strength |
| `edgeTessThreshold` | float | `0.1` | config.cpp | Edge detection threshold |

## Pipe Chain (4-Pass)

| Key | Type | Default | Parsed In | Notes |
|-----|------|---------|-----------|-------|
| `pipeChainEnable` | int | `0` | config.cpp | Enable 4-pass pipe chain |
| `pipeChainIntensity` | float | `0.5` | config.cpp | Chain intensity |

## GTA IV Mode

| Key | Type | Default | Parsed In | Notes |
|-----|------|---------|-----------|-------|
| `ivMode` | int | `0` | config.cpp | Enable GTA IV mode |
| `ivDesaturation` | float | `1.0` | config.cpp | Desaturation strength |
| `ivGamma` | float | `1.0` | config.cpp | Gamma correction |
| `ivSaturation` | float | `0.0` | config.cpp | Saturation |
| `ivCurves` | float | `0.0` | config.cpp | Curves |
| `ivVignetteIntensity` | float | `0.0` | config.cpp | Vignette intensity |
| `ivVignetteRadius` | float | `0.75` | config.cpp | Vignette radius |
| `ivVignetteContrast` | float | `1.5` | config.cpp | Vignette contrast |
| `ivBloomIntensity` | float | `0.0` | config.cpp | Bloom intensity |
| `ivExposure` | float | `1.0` | config.cpp | Exposure |

## Debug / Misc

| Key | Type | Default | Parsed In | Notes |
|-----|------|---------|-----------|-------|
| `keySwitch` | hex | `0x0` | config.cpp | Key to switch configs |
| `keyReload` | hex | `0x0` | config.cpp | Key to reload INI |
| `privateHooks` | int | `0` | config.cpp | Private hooks |
| `forceWindShader` | int | `0` | main.cpp | Force wind shader |
| `coronaZtest` | int | `-1` | config.cpp | Corona Z-test |

## Deprecated / Removed Keys

| Key | Status | Notes |
|-----|--------|-------|
| `ps2ModulateVehicle` | Deprecated | Use `dualPassVehicle` instead |
| `ps2grassFiles` | Removed | Was commented out in aap, never used |
| `tagsBuildingPipe` | Deprecated | Now follows `buildingPipe` automatically |

## See Also
- [[Vehicle Pipeline]] — Vehicle pipe options
- [[Backwards Compatibility]] — All settings verified
- [[Three-Codebase Comparison]] — Full INI comparison across codebases

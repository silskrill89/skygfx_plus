# Backwards Compatibility

#backwards-compatibility #ini #testing

## Overview
Every `vehiclePipe=` and `buildingPipe=` INI value from the original skygfx and all forks must continue to work. Users upgrading from older versions get the same rendering by default, with new features opt-in.

---

## Compatibility Matrix: INI Keys by Origin

### aap Original Keys (all still work)

| INI Key | Status | Parsed In | Notes |
|---------|--------|-----------|-------|
| `buildingPipe` | Active | main.cpp | `PS2`, `Xbox`, `GTAIV`, `PBR` supported |
| `vehiclePipe` | Active | main.cpp | All original values supported |
| `colorFilter` | Active | main.cpp | `PS2`, `PC`, `Mobile`, `III`, `VC`, `VCS`, `GTAIV`, `Modern` |
| `ps2Modulate` | Active | config.cpp | Global PS2 modulate |
| `dualPass` | Active | config.cpp | Global dual-pass |
| `ps2ModulateBuilding` | Active | config.cpp | Per-type override |
| `dualPassBuilding` | Active | config.cpp | Per-type override |
| `dualPassVehicle` | Active | config.cpp | Per-type override |
| `dualPassGrass` | Active | config.cpp | Per-type override |
| `dualPassDefault` | Active | config.cpp | Default dual-pass |
| `dualPassPed` | Active | config.cpp | Ped dual-pass |
| `detailMaps` | Active | main.cpp | Detail map rendering |
| `sunGlare` | Active | config.cpp | Sun glare effect |
| `neoWaterDrops` | Active | config.cpp | Neo water drops |
| `usePCTimecyc` | Active | config.cpp | Use PC timecycle |
| `grassAddAmbient` | Active | config.cpp | Grass add ambient |
| `grassBackfaceCull` | Active | config.cpp | Grass backface culling |
| `pedShadows` | Active | config.cpp | Ped shadows |
| `stencilShadows` | Active | config.cpp | Stencil shadows |
| `disableClouds` | Active | config.cpp | Disable clouds |
| `disableGamma` | Active | config.cpp | Disable gamma |
| `fixPcCarLight` | Active | config.cpp | Fix PC car lighting |
| `lightningIlluminatesWorld` | Active | config.cpp | Lightning illuminates world |
| `envMapSize` | Active | config.cpp | Reflection map size |
| `envShininessMult` | Active | config.cpp | Environment shininess |
| `envSpecularityMult` | Active | config.cpp | Environment specularity |
| `envPower` | Active | config.cpp | Environment power |
| `envFresnel` | Active | config.cpp | Fresnel reflection |
| `neoShininessMult` | Active | config.cpp | Neo shininess |
| `neoSpecularityMult` | Active | config.cpp | Neo specularity |
| `leedsShininessMult` | Active | config.cpp | Leeds shininess |
| `infraredVision` | Active | main.cpp | Infrared vision mode |
| `nightVision` | Active | main.cpp | Night vision mode |
| `grainFilter` | Active | main.cpp | Grain filter mode |
| `blurLeft` | Active | config.cpp | Blur offset left |
| `blurRight` | Active | config.cpp | Blur offset right |
| `blurTop` | Active | config.cpp | Blur offset top |
| `blurBottom` | Active | config.cpp | Blur offset bottom |
| `radiosity` | Active | config.cpp | Radiosity mode |
| `doRadiosity` | Active | config.cpp | Enable radiosity |
| `radiosityFilterPasses` | Active | config.cpp | Filter passes |
| `radiosityRenderPasses` | Active | config.cpp | Render passes |
| `radiosityIntensity` | Active | config.cpp | Intensity |
| `zwriteThreshold` | Active | config.cpp | Z-write threshold |
| `keySwitch` | Active | config.cpp | Key to switch configs |
| `keyReload` | Active | config.cpp | Key to reload INI |

### junior Fork Keys (all still work)

| INI Key | Status | Parsed In | Notes |
|---------|--------|-----------|-------|
| `envMapUseLODs` | Active | main.cpp | Use LODs for env map |
| `envMapFarClipMult` | Active | main.cpp | Far clip multiplier |
| `stochasticTexturing` | Active | main.cpp | Stochastic sampling |
| `grassFixPlacement` | Active | config.cpp | Grass placement fix |
| `neoBloodDrops` | Active | config.cpp | Neo blood drops |
| `coronaZtest` | Active | config.cpp | Corona Z-test |
| `colorFilter` (III/VC/VCS) | Active | main.cpp | All junior filter values work |
| `rgb1Mult` | Active | main.cpp | RGB filter 1 multiplier |
| `rgb2Mult` | Active | main.cpp | RGB filter 2 multiplier |
| `vcsTrails` | Active | config.cpp | VCS trails |
| `trailsLimit` | Active | config.cpp | Trail limit |
| `trailsIntensity` | Active | config.cpp | Trail intensity |
| `trailsResolution` | Active | main.cpp | Trail resolution |
| `zwriteThresholdGrass` | Active | main.cpp | Grass Z-write |
| `zwriteThresholdPed` | Active | main.cpp | Ped Z-write |
| `smaaEnable` | Active | config.cpp | SMAA enable |
| `smaaPreset` | Active | config.cpp | SMAA quality |
| `smaaPredication` | Active | config.cpp | SMAA predication |
| `smaaTemporal` | Active | config.cpp | SMAA temporal |
| `ivMode` | Active | config.cpp | GTA IV mode |
| `ivDesaturation` | Active | config.cpp | GTA IV desaturation |
| `ivGamma` | Active | config.cpp | GTA IV gamma |
| `ivVignetteIntensity` | Active | config.cpp | GTA IV vignette intensity |
| `ivVignetteRadius` | Active | config.cpp | GTA IV vignette radius |
| `ivVignetteContrast` | Active | config.cpp | GTA IV vignette contrast |
| `ivBloomIntensity` | Active | config.cpp | GTA IV bloom intensity |
| `ivExposure` | Active | config.cpp | GTA IV exposure |
| `forceWindShader` | Active | main.cpp | Force wind shader |

### skygfx_plus New Keys

| INI Key | Status | Parsed In | Notes |
|---------|--------|-----------|-------|
| `pipeline` | New | main.cpp | Unified pipeline mode (PBR/PS2/Xbox/Mobile/GTAIV) |
| `qualityPreset` | New | main.cpp | Quality preset (0-3) |
| `fixShadows` | New | config.cpp | Fix shadow rendering |
| `transparentLockon` | New | config.cpp | Transparent lock-on |
| `explicitBuildingPipe` | New | config.cpp | Explicit building pipe override |
| `privateHooks` | New | config.cpp | Private hooks |
| `YCbCrCorrection` | New | config.cpp | YCbCr correction |
| `lumaScale` | New | config.cpp | Luma scale |
| `lumaOffset` | New | config.cpp | Luma offset |
| `CbScale` | New | config.cpp | Cb scale |
| `CbOffset` | New | config.cpp | Cb offset |
| `CrScale` | New | config.cpp | Cr scale |
| `CrOffset` | New | config.cpp | Cr offset |
| `ssaoEnable` | New | config.cpp | SSAO enable |
| `ssaoRadius` | New | config.cpp | SSAO radius |
| `ssaoPower` | New | config.cpp | SSAO power |
| `ssaoKernelSize` | New | config.cpp | SSAO kernel size |
| `ssaoSampleCount` | New | config.cpp | SSAO sample count |
| `motionBlurEnable` | New | config.cpp | Motion blur enable |
| `motionBlurStrength` | New | config.cpp | Motion blur strength |
| `motionBlurRadial` | New | config.cpp | Motion blur radial |
| `motionBlurSpeedFactor` | New | config.cpp | Motion blur speed factor |
| `motionBlurCameraAware` | New | config.cpp | Motion blur camera aware |
| `sssPostProcessEnable` | New | config.cpp | SSS post-process enable |
| `sssPostProcessStrength` | New | config.cpp | SSS post-process strength |
| `sssPostProcessRadius` | New | config.cpp | SSS post-process radius |
| `sssPostProcessThreshold` | New | config.cpp | SSS post-process threshold |
| `skinEnhanceEnable` | New | config.cpp | Skin enhancement enable |
| `skinWrapFactor` | New | config.cpp | Skin wrap factor |
| `skinSpecularPower` | New | config.cpp | Skin specular power |
| `skinSpecularStrength` | New | config.cpp | Skin specular strength |
| `skinSSSStrength` | New | config.cpp | Skin SSS strength |
| `hairEnhanceEnable` | New | config.cpp | Hair enhancement enable |
| `hairAnisotropicPower` | New | config.cpp | Hair anisotropic power |
| `hairAnisotropicStrength` | New | config.cpp | Hair anisotropic strength |
| `hairSSSStrength` | New | config.cpp | Hair SSS strength |
| `vegetationEnhanceEnable` | New | config.cpp | Vegetation enhancement enable |
| `vegetationSSSStrength` | New | config.cpp | Vegetation SSS strength |
| `vegetationAmbientBoost` | New | config.cpp | Vegetation ambient boost |
| `enableNormalMaps` | New | config.cpp | Normal mapping enable |
| `normalMapIntensity` | New | config.cpp | Normal map intensity |
| `normalMapPlayerOnly` | New | config.cpp | Normal map player-only |
| `normalMapBuilding` | New | config.cpp | Building normal maps |
| `normalMapVehicle` | New | config.cpp | Vehicle normal maps |
| `normalMapDebug` | New | config.cpp | Normal map debug |
| `normalMapDebugMode` | New | config.cpp | Normal map debug mode |
| `normalBufferEnable` | New | config.cpp | Faux normal buffer enable |
| `normalBufferOffset` | New | config.cpp | Normal buffer offset |
| `normalBufferScale` | New | config.cpp | Normal buffer scale |
| `edgeTessEnable` | New | config.cpp | Edge tessellation enable |
| `edgeTessStrength` | New | config.cpp | Edge tessellation strength |
| `edgeTessThreshold` | New | config.cpp | Edge tessellation threshold |
| `pipeChainEnable` | New | config.cpp | Pipe chain enable |
| `pipeChainIntensity` | New | config.cpp | Pipe chain intensity |
| `ivSaturation` | New | config.cpp | GTA IV saturation |
| `ivCurves` | New | config.cpp | GTA IV curves |

---

## Verified Vehicle Pipe Modes

| INI Value | Status | Render Path | Shaders |
|-----------|--------|-------------|---------|
| `PS2` | OK | `_CB_PS2` | `simplePS` + `ps2EnvSpecFxPS` |
| `PC` | OK | `_CB_exe` | Original game function |
| `Xbox` | OK | `_CB_Xbox` | NULL PS (FFP) |
| `Specular` | OK | `_CB_Specular` | `simplePS` + `specCarFxPS` |
| `Neo` | OK | `CarPipe::RenderCallback` | Neo pipe |
| `Leeds` | OK | `_CB_leeds` | `simplePS` + leeds VS |
| `VCS` | OK | `_CB_leeds` | `simplePS` + leeds VS |
| `Mobile` | OK | `_CB_mobile` | `mobileVehiclePipePS` + `simplePS` |
| `Env` | OK | `_CB_Env` | `envCarPS` / PBR fallback |
| `GTAIV` | OK | `_CB_PS2` (ivMode) | `gtaivVehiclePS` |
| `Modern` | OK | `_CB_Env` | `VehiclePBR_Modern` / `Glass_Vehicle` / `Rubber_Vehicle` |

## Verified Building Pipe Modes

| INI Value | Status | Notes |
|-----------|--------|-------|
| `PS2` | OK | PS2 building VS/PS |
| `Xbox` | OK | Xbox building with env maps |
| `GTAIV` | OK | GTA IV forward pass |
| `PBR` | OK | PBR building shaders |

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
| `ps2ModulateVehicle` | Deprecated | Use `dualPassVehicle` instead |
| `ps2grassFiles` | Removed | Was commented out in aap, never used in skygfx_plus |

## See Also
- [[Vehicle Pipeline]] — Full mode details
- [[INI Configuration]] — All config fields
- [[Three-Codebase Comparison]] — Full INI comparison across all codebases

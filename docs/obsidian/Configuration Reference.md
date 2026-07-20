# Configuration Reference

SkyGFX Plus reads settings from `skygfx.ini` (and `skygfx.1.ini` through `skygfx.9.ini` for multiple configs). All settings go under the `[SkyGfx]` section.

> [!tip]
> If no INI exists, SkyGFX Plus creates one with recommended defaults. Use the in-game debug menu (F4) to toggle settings at runtime.

## INI File Locations

| File | Purpose |
|------|---------|
| `skygfx.ini` | Default config (used if no numbered configs exist) |
| `skygfx.1.ini` through `skygfx.9.ini` | Numbered configs (switch at runtime with keySwitch key) |
| `stream.ini` | Game file — some radiosity settings read from here too |

## Key Bindings

| Key | Default | Description |
|-----|---------|-------------|
| `keySwitch` | `0x0` (disabled) | Cycle between numbered INI configs |
| `keyReload` | `0x0` (disabled) | Hot-reload all INI files |

---

## Pipelines

### Vehicle Pipeline

| Setting | Values | Default | Description |
|---------|--------|---------|-------------|
| `vehiclePipe` | PS2, PC, Xbox, Spec, Mobile, Neo, LCS, VCS, Env, GTAIV | — | Vehicle rendering pipeline |
| `dualPassVehicle` | 0/1 | inherits `dualPass` | PS2 alpha test emulation |
| `neoShininessMult` | float | 1.0 | Neo pipeline shininess multiplier |
| `neoSpecularityMult` | float | 1.0 | Neo pipeline specularity multiplier |
| `leedsShininessMult` | float | 1.0 | Leeds/LCS pipeline shininess multiplier |
| `envShininessMult` | float | 1.0 | Env pipeline shininess multiplier |
| `envSpecularityMult` | float | 1.0 | Env pipeline specularity multiplier |
| `envPower` | float | 20.0 | Env pipeline specular power |
| `envFresnel` | float | 0.7 | Env pipeline fresnel factor |
| `envMapSize` | 64/128/256/512 | 256 | Environment map render target size |
| `envMapFarClipMult` | float | 1.0 | Env map far clip plane multiplier |
| `envMapUseLODs` | 0/1 | 0 | Use LOD models for env map rendering |
| `fixPcCarLight` | 0/1 | 0 | Fix PC directional light on vehicles |

### Building Pipeline

| Setting | Values | Default | Description |
|---------|--------|---------|-------------|
| `buildingPipe` | PS2, PC/Xbox, GTAIV | — | Building rendering pipeline |
| `tagsBuildingPipe` | PS2, PC/Xbox, GTAIV | — | Pipeline for tagged/tag objects |
| `explicitBuildingPipe` | -1/0/1/2 | -1 | Force specific building pipeline |
| `dualPassBuilding` | 0/1 | inherits `dualPass` | PS2 alpha test emulation |
| `ps2ModulateBuilding` | 0/1 | inherits `ps2Modulate` | PS2 colour modulation |
| `detailMaps` | 0/1 | 0 | Enable detail texture maps on buildings |
| `stochasticTexturing` | 0/1 | 0 | Stochastic texture sampling (reduces tiling) |

### Grass Pipeline

| Setting | Values | Default | Description |
|---------|--------|---------|-------------|
| `grassFixPlacement` | 0/1 | 0 | Fix broken random number generator |
| `grassBackfaceCull` | 0/1 | 1 | Enable/disable backface culling |
| `grassAddAmbient` | 0/1 | 0 | Add ambient colour to grass |
| `ps2ModulateGrass` | 0/1 | inherits `ps2Modulate` | PS2 colour modulation on grass |
| `dualPassGrass` | 0/1 | inherits `dualPass` | PS2 alpha test on grass |

### Global Pipeline Settings

| Setting | Values | Default | Description |
|---------|--------|---------|-------------|
| `ps2Modulate` | 0/1 | 0 | Global PS2 colour modulation |
| `dualPass` | 0/1 | 0 | Global PS2 alpha test emulation (dual pass) |
| `dualPassDefault` | 0/1 | inherits `dualPass` | Default pipeline dual pass |
| `dualPassPed` | 0/1 | inherits `dualPass` | Ped pipeline dual pass |

---

## Post Effects

### Colour Filter

| Setting | Values | Default | Description |
|---------|--------|---------|-------------|
| `colorFilter` | None, PS2, PC, Mobile, III, VC, VCS, GTAIV | PC | Colour filter style |
| `rgb1Mult` | float | 1.0 | Colour filter RGB1 multiplier |
| `rgb2Mult` | float | 1.0 | Colour filter RGB2 multiplier |
| `blurLeft` | int | 4000 (auto) | Blur U offset left |
| `blurRight` | int | 4000 (auto) | Blur U offset right |
| `blurTop` | int | 4000 (auto) | Blur V offset top |
| `blurBottom` | int | 4000 (auto) | Blur V offset bottom |

### Radiosity

| Setting | Values | Default | Description |
|---------|--------|---------|-------------|
| `doRadiosity` | 0/1 | game default | Enable radiosity effect |
| `radiosity` | PS2, Shader | Shader | Radiosity implementation |
| `radiosityFilterPasses` | int | 2 | Downsample passes |
| `radiosityRenderPasses` | int | 1 | Effect render passes |
| `radiosityIntensity` | int | 35 (0x23) | Effect intensity |

### Night/Infrared Vision & Grain

| Setting | Values | Default | Description |
|---------|--------|---------|-------------|
| `infraredVision` | PS2, PC | PC | Infrared vision implementation |
| `nightVision` | PS2, PC | PC | Night vision implementation |
| `grainFilter` | PS2, PC | PC | Film grain implementation |

### VCS Trails (III/VC)

| Setting | Values | Default | Description |
|---------|--------|---------|-------------|
| `vcsTrails` | 0/1 | 0 | Enable VCS-style trails |
| `trailsLimit` | int | 80 | Trail intensity limit |
| `trailsIntensity` | int | 38 | Trail intensity |
| `trailsResolution` | int | 1 | Trail resolution |

### YCbCr Correction

| Setting | Values | Default | Description |
|---------|--------|---------|-------------|
| `YCbCrCorrection` | 0/1 | 0 | Enable YCbCr colour correction |
| `lumaScale` | float | 0.859 | Luma scale |
| `lumaOffset` | float | 0.063 | Luma offset |
| `CbScale` | float | 1.23 | Cb channel scale |
| `CbOffset` | float | 0.0 | Cb channel offset |
| `CrScale` | float | 1.23 | Cr channel scale |
| `CrOffset` | float | 0.0 | Cr channel offset |

---

## SSAO (Screen-Space Ambient Occlusion)

| Setting | Values | Default | Description |
|---------|--------|---------|-------------|
| `ssaoEnable` | 0/1 | 1 | Enable SSAO |
| `ssaoRadius` | float | 0.8 | Sample radius |
| `ssaoPower` | float | 1.5 | Occlusion power curve |
| `ssaoKernelSize` | float | 16.0 | Kernel size |
| `ssaoSampleCount` | int | 16 | Number of samples |

---

## SMAA (Anti-Aliasing)

| Setting | Values | Default | Description |
|---------|--------|---------|-------------|
| `smaaEnable` | 0/1 | 1 | Enable SMAA |
| `smaaPreset` | 0-3 | 2 | Quality preset (0=LOW, 1=MEDIUM, 2=HIGH, 3=ULTRA) |
| `smaaPredication` | 0/1 | 0 | Enable edge predication |
| `smaaTemporal` | 0/1 | 0 | Enable temporal AA |

---

## GTA IV Mode

| Setting | Values | Default | Description |
|---------|--------|---------|-------------|
| `ivMode` | 0/1 | 0 | Enable GTA IV postFX style |
| `ivDesaturation` | float | 0.3 | Colour desaturation |
| `ivGamma` | float | 1.0 | Gamma correction |
| `ivVignetteIntensity` | float | 0.5 | Vignette strength |
| `ivVignetteRadius` | float | 0.5 | Vignette radius |
| `ivVignetteContrast` | float | 2.0 | Vignette contrast |
| `ivBloomIntensity` | float | 0.15 | Bloom intensity |
| `ivExposure` | float | 1.0 | Exposure |

---

## Unified Pipeline (GTA V-style)

The unified pipeline is an advanced rendering system that combines SSAO, shadow merging, time-of-day modulation, IBL, and colour grading into a single coherent system.

### Master Controls

| Setting | Values | Default | Description |
|---------|--------|---------|-------------|
| `unifiedEnable` | 0/1 | 1 | Enable unified pipeline |
| `unifiedVersion` | 0-3 | 2 | Version gamma curve (0=PS2, 1=PC1.0, 2=Steam, 3=Mobile) |

### Lighting & Shadows

| Setting | Values | Default | Description |
|---------|--------|---------|-------------|
| `unifiedSatBoost` | float | 0.08 | Saturation boost |
| `unifiedShadowSoftness` | float | 0.5 | Shadow edge softness |
| `unifiedCloudShadowStr` | float | 0.3 | Cloud shadow strength |
| `unifiedSunShadowStr` | float | 0.8 | Sun shadow strength |
| `unifiedVertexAOBoost` | float | 1.4 | Vertex AO darkening factor |
| `unifiedDayReduction` | float | 0.15 | Day brightness reduction |
| `unifiedPointLightOverride` | float | 0.2 | Point light override |

### Feature Toggles

| Setting | Values | Default | Description |
|---------|--------|---------|-------------|
| `unifiedEnablePrePass` | 0/1 | 1 | Gamma decode + edge detect |
| `unifiedEnableEdgeDetect` | 0/1 | 1 | Edge detection |
| `unifiedEnableOcclusion` | 0/1 | 1 | Combined occlusion |
| `unifiedEnableStoredShadows` | 0/1 | 1 | Vertex stored shadows |
| `unifiedEnableCloudShadows` | 0/1 | 1 | Cloud shadows |
| `unifiedEnableSunShadows` | 0/1 | 1 | Sun shadows |
| `unifiedEnableTimeOfDay` | 0/1 | 1 | Time-of-day modulation |
| `unifiedEnableVertexAO` | 0/1 | 1 | Vertex ambient occlusion |
| `unifiedEnablePointLightOverride` | 0/1 | 1 | Point light override |
| `unifiedEnablePostPass` | 0/1 | 1 | Post-processing pass |
| `unifiedEnableIBL` | 0/1 | 1 | Image-based lighting |
| `unifiedEnableIBLTint` | 0/1 | 1 | IBL car paint tint |
| `unifiedEnableSurfaceWeights` | 0/1 | 1 | Surface material weights |
| `unifiedEnableGrading` | 0/1 | 1 | Colour grading |
| `unifiedEnableGamma` | 0/1 | 1 | Gamma correction |

### IBL Tint

| Setting | Values | Default | Description |
|---------|--------|---------|-------------|
| `unifiedIblTintStrength` | float | 0.3 | Car paint tint on reflections |

### SMAA (Unified)

| Setting | Values | Default | Description |
|---------|--------|---------|-------------|
| `unifiedSmaaThreshold` | float | 0.1 | Edge detection threshold |
| `unifiedSmaaCornerRounding` | float | 25.0 | Corner rounding |
| `unifiedSmaaMaxSearchSteps` | float | 8.0 | Max search steps |

### Debug

| Setting | Values | Default | Description |
|---------|--------|---------|-------------|
| `unifiedShowMenu` | 0/1 | 0 | Show debug menu |
| `unifiedShowOverlay` | 0/1 | 0 | Show debug overlay |
| `unifiedDebugOcclusion` | 0/1 | 0 | Visualize occlusion buffer |

---

## Miscellaneous

| Setting | Values | Default | Description |
|---------|--------|---------|-------------|
| `usePCTimecyc` | 0/1 | 0 | Use PC timecyc.dat (NOT recommended) |
| `sunGlare` | 0/1 | -1 | Sun glare effect |
| `neoWaterDrops` | 0/1 | -1 | Neo water drops on camera |
| `neoBloodDrops` | 0/1 | 0 | Neo blood drops on camera |
| `pedShadows` | 0/1 | — | Ped shadows |
| `stencilShadows` | 0/1 | — | Stencil shadows |
| `lightningIlluminatesWorld` | 0/1 | 0 | Lightning illuminates world |
| `disableClouds` | 0/1 | 0 | Disable cloud rendering |
| `disableGamma` | 0/1 | 0 | Disable gamma correction |
| `transparentLockon` | 0/1 | 0 | Transparent lock-on target |
| `fixShadows` | 0/1 | 0 | Fix shadow rendering |
| `coronaZtest` | -1/0/1 | -1 | Corona Z-test (-1=default) |
| `zwriteThreshold` | 0-255 | 128 | Z-write alpha threshold |
| `zwriteThresholdGrass` | 0-255 | 128 | Z-write threshold for grass |
| `zwriteThresholdPed` | 0-255 | 128 | Z-write threshold for peds |
| `forceWindShader` | 0/1 | 0 | Force wind animation shader |
| `privateHooks` | 0/1 | 0 | Enable private hook points |

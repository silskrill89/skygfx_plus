# SkyGFX Plus — Systematic Test Plan

> Test every effect one by one. For each: toggle ON → verify visual change → toggle OFF → verify reverts → test slider range.
> Use debug menu (F5) to toggle in real-time. Watch `skygfx_dbg.log` for errors.

---

## Test Procedure

1. Build: `python tools/fast_build.py --launch`
2. Game loads → press F5 for debug menu
3. Test each effect in isolation (disable all others first where possible)
4. Check `skygfx_dbg.log` for shader errors, NULL pointers, crashes
5. Mark PASS/FAIL with notes

---

## Group 1: Rendering Pipelines

### 1.1 Building Pipes
| # | Setting | Values | Test | Status |
|---|---------|--------|------|--------|
| 1 | `buildingPipe` | PS2 | Buildings render, PS2-style modulation | |
| 2 | `buildingPipe` | Xbox | Buildings render, Xbox-style | |
| 3 | `buildingPipe` | GTAIV | Buildings render, GTA IV style | |
| 4 | `buildingPipe` | PBR | Buildings render, PBR with GGX | |

### 1.2 Vehicle Pipes
| # | Setting | Values | Test | Status |
|---|---------|--------|------|--------|
| 5 | `vehiclePipe` | PS2 | Vehicles render PS2-style | |
| 6 | `vehiclePipe` | PC | Vehicles render PC-style | |
| 7 | `vehiclePipe` | Xbox | Vehicles render Xbox-style | |
| 8 | `vehiclePipe` | Spec | Vehicles render specular | |
| 9 | `vehiclePipe` | Mobile | Vehicles render mobile-style | |
| 10 | `vehiclePipe` | Neo | Vehicles render Neo-style | |
| 11 | `vehiclePipe` | LCS | Vehicles render LCS-style | |
| 12 | `vehiclePipe` | VCS | Vehicles render VCS-style | |
| 13 | `vehiclePipe` | Env | Vehicles render env-mapped | |
| 14 | `vehiclePipe` | GTAIV | Vehicles render GTA IV style | |
| 15 | `vehiclePipe` | Modern | Vehicles render PBR modern | |

### 1.3 Color Filters
| # | Setting | Values | Test | Status |
|---|---------|--------|------|--------|
| 16 | `colorFilter` | None | No colour filter applied | |
| 17 | `colorFilter` | PS2 | PS2 colour filter | |
| 18 | `colorFilter` | PC | PC colour filter | |
| 19 | `colorFilter` | Mobile | Mobile colour filter | |
| 20 | `colorFilter` | III | GTA III colour filter | |
| 21 | `colorFilter` | VC | Vice City colour filter | |
| 22 | `colorFilter` | VCS | VCS colour filter | |
| 23 | `colorFilter` | GTAIV | GTA IV colour filter | |

---

## Group 2: PostFX Chain

### 2.1 SSAO
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 24 | `ssaoEnable` | 0/1 | Toggle SSAO on/off | |
| 25 | `ssaoRadius` | 0-5 | Larger = wider occlusion | |
| 26 | `ssaoPower` | 0-10 | Higher = darker occlusion | |
| 27 | `ssaoKernelSize` | 1-64 | Larger = smoother | |
| 28 | `ssaoSampleCount` | 1-64 | More = better quality, slower | |
| 29 | `ssaoTemporalEnable` | 0/1 | Temporal accumulation | |
| 30 | `ssaoTemporalBlend` | 0.01-0.5 | Lower = more history | |
| 31 | `ssaoBlurPasses` | 0-3 | More = smoother | |
| 32 | `ssaoBlurRadius` | 1-8 | Larger blur kernel | |
| 33 | `ssaoDepthThreshold` | 0.001-0.1 | Edge preservation | |

### 2.2 SMAA
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 34 | `smaaEnable` | 0/1 | Toggle SMAA, check edges | |
| 35 | — | — | Verify temporal resolve (Pass 3) | |
| 36 | — | — | Verify HUD preserved | |

### 2.3 Motion Blur
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 37 | `motionBlurEnable` | 0/1 | Toggle motion blur | |
| 38 | `motionBlurStrength` | 0-1 | Overall blur intensity | |
| 39 | `motionBlurRadial` | 0-1 | Radial from screen center | |
| 40 | `motionBlurSpeedFactor` | 0-2 | Camera velocity influence | |
| 41 | `motionBlurCameraAware` | 0/1 | Reduce blur on fast camera | |

### 2.4 SSS (Screen-Space Subsurface)
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 42 | `sssEnable` | 0/1 | Toggle SSS | |
| 43 | `sssIntensity` | 0-1 | Global SSS strength | |
| 44 | `sssVegIntensity` | 0-1 | Vegetation SSS | |
| 45 | `sssSkinIntensity` | 0-1 | Skin SSS | |
| 46 | `sssClothIntensity` | 0-1 | Cloth SSS | |

### 2.5 SSS Post-Process Blur
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 47 | `sssPostProcessEnable` | 0/1 | Toggle post-process SSS | |
| 48 | `sssPostProcessStrength` | 0-1 | Blur strength | |
| 49 | `sssPostProcessRadius` | 0-20 | Blur radius | |
| 50 | `sssPostProcessThreshold` | 0-1 | Edge preservation | |

### 2.6 Skin Enhancement
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 51 | `skinEnhanceEnable` | 0/1 | Toggle skin enhancement | |
| 52 | `skinWrapFactor` | 0-1 | Light wrap around surface | |
| 53 | `skinSpecularPower` | 0-100 | Specular sharpness | |
| 54 | `skinSpecularStrength` | 0-2 | Specular intensity | |
| 55 | `skinSSSStrength` | 0-1 | Skin SSS strength | |

### 2.7 Radiosity
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 56 | `doRadiosity` | 0/1 | Toggle radiosity | |
| 57 | `radiosity` | PS2/Shader | Radiosity type | |
| 58 | `radiosityFilterPasses` | 0-8 | More = smoother bloom | |
| 59 | `radiosityRenderPasses` | 0-8 | More = stronger effect | |
| 60 | `radiosityIntensity` | 0-255 | Bloom intensity | |
| 61 | `offLeft/Right/Top/Bottom` | -1000-1000 | Bloom offset | |

### 2.8 GTA IV Mode
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 62 | `ivMode` | 0/1 | Toggle GTA IV mode | |
| 63 | `ivDesaturation` | 0-2 | Color desaturation | |
| 64 | `ivGamma` | 0.5-2 | Gamma correction | |
| 65 | `ivExposure` | 0-3 | Exposure | |
| 66 | `ivBloomIntensity` | 0-2 | Bloom | |
| 67 | `ivVignetteIntensity` | 0-2 | Vignette | |
| 68 | `ivVignetteRadius` | 0-2 | Vignette radius | |
| 69 | `ivVignetteContrast` | 0-5 | Vignette contrast | |
| 70 | `ivSaturation` | -1-2 | Saturation | |
| 71 | `ivCurves` | 0-2 | Tone curves | |

---

## Group 3: Atmospheric

### 3.1 Height Fog
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 72 | `heightFogEnable` | 0/1 | Toggle height fog | |
| 73 | `heightFogDensity` | 0-0.1 | Fog thickness | |
| 74 | `heightFogHeightFalloff` | 0-5 | Height decay | |
| 75 | `heightFogStartHeight` | -100-500 | Fog start height | |
| 76 | `heightFogR/G/B` | 0-1 | Fog color | |
| 77 | `heightFogTimecycleScale` | 0-5 | Timecycle influence | |

### 3.2 God Rays
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 78 | `godRaysEnable` | 0/1 | Toggle god rays | |
| 79 | `godRaysExposure` | 0-0.05 | Brightness per sample | |
| 80 | `godRaysDecay` | 0-2 | Falloff per sample | |
| 81 | `godRaysDensity` | 0-2 | Ray density | |
| 82 | `godRaysWeight` | 0-5 | Ray weight | |
| 83 | `godRaysNumSamples` | 1-64 | Sample count | |

---

## Group 4: Sun & Environment

### 4.1 Sun Flare
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 84 | `sunCoronaIntensity` | 0-10 | Sun corona brightness | |
| 85 | `sunCoreIntensity` | 0-10 | Sun core brightness | |
| 86 | `sunStreakIntensity` | 0-10 | Lens streak brightness | |
| 87 | `sunStreakSize` | 0-10 | Lens streak size | |
| 88 | `doglare` | 0/1 | Sun glare toggle | |

### 4.2 Environment Map
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 89 | `envMapSize` | 4-2048 | Env map resolution | |
| 90 | `envMapFarClipMult` | 0-10 | Far clip multiplier | |
| 91 | `envShininessMult` | 0-10 | Env shininess | |
| 92 | `envSpecularityMult` | 0-10 | Env specularity | |
| 93 | `envPower` | 0-2000 | Env power | |
| 94 | `envFresnel` | 0-10 | Env Fresnel | |

---

## Group 5: Material Features

### 5.1 Dual Pass
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 95 | `dualPassGlobal` | 0/1 | Global dual pass | |
| 96 | `dualPassBuilding` | 0/1 | Building dual pass | |
| 97 | `dualPassVehicle` | 0/1 | Vehicle dual pass | |
| 98 | `dualPassPed` | 0/1 | Ped dual pass | |
| 99 | `dualPassGrass` | 0/1 | Grass dual pass | |
| 100 | `zwriteThreshold` | 0-255 | Alpha threshold | |

### 5.2 PS2 Modulation
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 101 | `ps2ModulateGlobal` | 0/1 | Global PS2 modulate | |
| 102 | `ps2ModulateBuilding` | 0/1 | Building PS2 modulate | |
| 103 | `ps2ModulateGrass` | 0/1 | Grass PS2 modulate | |

### 5.3 Detail & Stochastic
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 104 | `detailMaps` | 0/1 | Detail maps on buildings | |
| 105 | `stochastic` | 0/1 | Stochastic texturing | |

---

## Group 6: Shadows & Grass

### 6.1 Shadows
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 106 | `pedShadows` | -1/0/1 | Ped shadow quality | |
| 107 | `stencilShadows` | -1/0/1 | Stencil shadow quality | |

### 6.2 Grass
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 108 | `grassAddAmbient` | 0/1 | Add ambient to grass | |
| 109 | `backfaceCull` | 0/1 | Grass backface culling | |
| 110 | `fixGrassPlacement` | 0/1 | Fix grass placement | |

---

## Group 7: Screen Effects

### 7.1 YCbCr Filter
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 111 | `bYCbCrFilter` | 0/1 | Toggle YCbCr | |
| 112 | `lumaScale` | 0-10 | Y channel scale | |
| 113 | `lumaOffset` | -1-1 | Y channel offset | |
| 114 | `cbScale` | 0-10 | Cb channel scale | |
| 115 | `cbOffset` | -1-1 | Cb channel offset | |
| 116 | `crScale` | 0-10 | Cr channel scale | |
| 117 | `crOffset` | -1-1 | Cr channel offset | |

### 7.2 Other Screen FX
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 118 | `infraredVision` | PS2/PC | Infrared vision | |
| 119 | `nightVision` | PS2/PC | Night vision | |
| 120 | `grainFilter` | PS2/PC | Film grain | |
| 121 | `neoWaterDrops` | 0/1 | Water drops | |
| 122 | `neoBloodDrops` | 0/1 | Blood drops | |
| 123 | `lightningIlluminatesWorld` | 0/1 | Lightning illumination | |
| 124 | `coronaZtest` | -1/0/1 | Corona Z test | |

---

## Group 8: Advanced Features

### 8.1 Normal Buffer
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 125 | `normalBufferEnable` | 0/1 | Toggle normal buffer | |
| 126 | `normalBufferOffset` | 0-2 | Normal offset | |
| 127 | `normalBufferScale` | 0-2 | Normal scale | |

### 8.2 Forward+ Tiled Lighting
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 128 | `forwardPlusEnable` | 0/1 | Toggle Forward+ | |
| 129 | — | — | Verify point lights appear on vehicles | |
| 130 | — | — | Verify point lights appear on buildings | |

### 8.3 Wet Roads
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 131 | — | — | Wet roads BRDF modulation visible | |

### 8.4 Velocity Buffer
| # | Setting | Range | Test | Status |
|---|---------|-------|------|--------|
| 132 | `velocityBufferEnable` | 0/1 | Toggle velocity buffer | |

---

## Group 9: Presets

| # | Preset | Test | Status |
|---|--------|------|--------|
| 133 | III PS2 | All pipes switch, PS2 modulation on | |
| 134 | III Xbox | All pipes switch | |
| 135 | III PC | All pipes switch | |
| 136 | VC PS2 | All pipes switch | |
| 137 | SA PS2 | All pipes switch, PS2 modulation on | |
| 138 | SA Xbox | All pipes switch | |
| 139 | SA PC | All pipes switch | |
| 140 | LCS PS2 | All pipes switch | |
| 141 | VCS PS2 | All pipes switch, VCS trails on | |
| 142 | IV Xbox 360 | GTA IV mode enabled | |
| 143 | IV PC | GTA IV mode enabled | |
| 144 | Best PC (Default) | All modern features | |

---

## Group 10: Edge Cases & Stability

| # | Test | Status |
|---|------|--------|
| 145 | Alt-tab during rendering | |
| 146 | Resize window (if windowed) | |
| 147 | Toggle SMAA on/off rapidly | |
| 148 | Toggle SSAO on/off rapidly | |
| 149 | Switch building pipe during gameplay | |
| 150 | Switch vehicle pipe during gameplay | |
| 151 | Enter/exit interior | |
| 152 | Enter/exit vehicle | |
| 153 | Cutscene playback | |
| 154 | Weather transition (clear → rain) | |
| 155 | Night → dawn → noon → dusk cycle | |
| 156 | Drive fast through dense area | |
| 157 | Explosion with multiple lights (Forward+) | |

---

## Notes

- Test with `vehiclePipe=Modern` and `buildingPipe=PBR` as baseline (most features active)
- For isolation: set all other effects to OFF, test one at a time
- Watch `skygfx_dbg.log` for `[ERROR]`, `CRASH:`, shader compilation failures
- Screenshot before/after for visual comparison

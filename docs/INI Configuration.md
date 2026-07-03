# INI Configuration

#ini #config

## Overview
All features are configured via `skygfx.ini`. Multiple INI files supported (skygfx.ini, skygfx.1.ini through skygfx.9.ini).

## Quality Presets
Set `qualityPreset=` to apply defaults. Individual INI values override preset defaults.

| Preset | Value | Description |
|--------|-------|-------------|
| LOW | 0 | PS2 classic, no modern features |
| MEDIUM | 1 | PC classic, SMAA LOW |
| HIGH | 2 | Enhanced, SMAA HIGH + SSAO + Motion Blur |
| ULTRA | 3 | Full PBR, all features maxed |

See [[Quality Presets]] for full details.

## Key Fields

### Pipeline Selection
```ini
buildingPipe=PS2|PC|Xbox|GTAIV
vehiclePipe=PS2|PC|Xbox|Spec|Neo|LCS|VCS|Mobile|Env|GTAIV|Modern
colorFilter=PS2|PC|Mobile|III|VC|VCS|GTAIV
```

### SMAA
```ini
smaaEnable=1
smaaPreset=3          # 0=LOW, 1=MED, 2=HIGH, 3=ULTRA
smaaPredication=0
smaaTemporal=0
```

### SSAO
```ini
ssaoEnable=1
ssaoRadius=0.8
ssaoPower=1.5
ssaoKernelSize=16
ssaoSampleCount=16
```

### GTA IV Mode
```ini
ivMode=1
ivDesaturation=1.0    # 1.0 = no desaturation
ivGamma=1.0
ivSaturation=0.3
ivCurves=1.0
ivVignetteIntensity=0.5
ivVignetteRadius=0.5
ivVignetteContrast=2.0
ivBloomIntensity=0.1
ivExposure=2.5
```

### Vehicle PBR
```ini
envMapSize=256
envMapFarClipMult=1.5
envShininessMult=1.0
envSpecularityMult=1.0
envPower=128.0
envFresnel=0.95
```

### Effects
```ini
motionBlurEnable=1
motionBlurStrength=0.3
sssPostProcessEnable=1
sssPostProcessStrength=0.15
skinEnhanceEnable=1
hairEnhanceEnable=1
vegetationEnhanceEnable=1
```

### Normal Buffer
```ini
normalBufferEnable=0
normalBufferOffset=0.5
normalBufferScale=1.0
```

## See Also
- [[Quality Presets]] — Preset defaults
- [[Vehicle Pipeline]] — Vehicle pipe options
- [[Building Pipeline]] — Building pipe options
- [[Backwards Compatibility]] — All settings verified

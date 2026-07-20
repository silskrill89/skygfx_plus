# Weather Timecycle

#weather #timecycle #config

## Overview

Multi-timecyc weather system with GTA V-style expanded config. Sits on top of the game's `CTimeCycle`, overriding `m_CurrentColours` with blended values from alternate timecycle data.

Implemented in `weather.cpp` with hash-based deterministic set selection.

## Weather System

### Multi-Timecyc

- Loads `weathers2.dat` as an alternate timecycle set
- Hash-based deterministic set selection per weather type + time slot
- 70% default set, 30% alternate set (configurable via hash)
- Smooth blending between sets with configurable transition speed

### Hash-Based Selection

```
WeatherHash(weatherType, timeSlot) → set selection
```

Deterministic: same weather type + time slot always produces the same set selection.

### Interpolated Properties

- Sky top/bottom colors
- Ambient colors
- Directional light colors
- Sun core/corona colors
- Fog start
- Low cloud colors

All values are clamped to valid ranges after blending.

## Expanded Weather Config (GTA V Style)

### Sky
```ini
skyZenithR/G/B/Inten
skyZenithTransR/G/B/Inten
skyAzimuthEastR/G/B/Inten
skyAzimuthTransR/G/B/Inten
skyAzimuthWestR/G/B/Inten
skyPlaneR/G/B/Inten
```

### Sun
```ini
sunR/G/B
sunDiscR/G/B
sunDiscSize
sunMiePhase, sunMieScatter, sunMieIntenMult
sunInfluenceRadius, sunScatterInten
```

### Moon/Stars
```ini
moonR/G/B
moonDiscSize
moonInten, starsInten
moonInfluenceRadius, moonScatterInten
```

### Clouds
```ini
cloudGenFreq, cloudGenScale, cloudGenThresh, cloudGenSoftness
cloudDensityMult, cloudDensityBias
cloudMidR/G/B, cloudBaseR/G/B
cloudBaseStrength
cloudShadowR/G/B, cloudShadowStrength
cloudGenDensityOffset, cloudOffset
cloudOverallStrength, cloudOverallColor, cloudEdgeStrength
cloudFadeout, cloudHDR, cloudDitherStrength
smallCloudR/G/B, smallCloudDetailStrength, smallCloudDetailScale
smallCloudDensityMult, smallCloudDensityBias
```

### Light
```ini
lightDirR/G/B/Mult
lightDirAmbR/G/B/Inten/IntenMult/Bounce
lightAmbDownWrap
lightNatAmbDownR/G/B/Inten
lightNatAmbBaseR/G/B/Inten/IntenMult
lightArtifIntAmbDownR/G/B/Inten
lightArtifIntAmbBaseR/G/B/Inten
lightArtifExtAmbDownR/G/B/Inten
lightArtifExtAmbBaseR/G/B/Inten
pedLightR/G/B/Mult, pedLightDirX/Y/Z
```

### Weather Cycle Control
```ini
currentWeatherType
weatherTransition
weatherCycleEnabled
timecycleOverrideHour
timecycleOverrideEnabled
```

## See Also

- [[INI Configuration]] — All config fields
- [[GTA IV Mode]] — PostFX settings

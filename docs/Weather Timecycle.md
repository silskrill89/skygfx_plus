# Weather Timecycle

#weather #timecycle #config

## Overview
GTA V-style expanded weather and timecycle system. 80+ config fields for sky, sun, moon, clouds, lighting, and postFX.

## Sky
```ini
skyZenithR/G/B/Inten
skyZenithTransR/G/B/Inten
skyAzimuthEastR/G/B/Inten
skyAzimuthTransR/G/B/Inten
skyAzimuthWestR/G/B/Inten
skyPlaneR/G/B/Inten
```

## Sun
```ini
sunR/G/B
sunDiscR/G/B
sunDiscSize
sunMiePhase, sunMieScatter, sunMieIntenMult
sunInfluenceRadius, sunScatterInten
```

## Moon/Stars
```ini
moonR/G/B
moonDiscSize
moonInten, starsInten
moonInfluenceRadius, moonScatterInten
```

## Clouds
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

## Light
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

## Weather Cycle Control
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

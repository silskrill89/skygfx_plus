---
tags: [weather, timecycle, hash]
created: 2025-01-02
updated: 2026-07-15
---

# Weather System Architecture

> [!info] Full documentation
> See `docs/Weather Timecycle.md` for the complete weather/timecycle reference.

## Multi-Timecyc + LA 1992 Climate Model

### Overview
Custom weather system that sits on top of GTA SA's CTimeCycle, adding:
- Multiple timecyc files (weathers2.dat) for weather variety
- Hash-based deterministic weather selection (same pattern as paint/tire randomizer)
- Smooth blending between weather sets
- Future: LA 1992 climate model with real-time wind/weather

### Architecture
```
Game's CTimeCycle::Update() → m_CurrentColours (0xB7C4A0)
        ↓
Weather_Update() — reads both timecyc sets, blends values
        ↓
Override m_CurrentColours with blended values
        ↓
RenderIBLBuffer() — reads overridden values for sky shader
        ↓
DynamicSky shader — renders sky with blended colors
```

### Files
| File | Purpose |
|------|---------|
| `data/timecyc.dat` | Main timecyc (PS2 values, promoted from timecycp.dat) |
| `data/weathers2.dat` | Alternate timecyc (LA 1992 film tones) |
| `data/timecyc_main_backup.dat` | Backup of original PC timecyc |
| `data/timecyc_beta_backup.dat` | Backup of PS2 timecyc |
| `src/render/weather.cpp` | Weather system implementation |

### Hash-Based Selection (Same Pattern as Paint/Tire)
```cpp
unsigned int WeatherHash(int weatherType, int timeSlot){
    unsigned int h = (unsigned int)(weatherType * 2654435761u);
    h ^= (unsigned int)(timeSlot * 2246822519u);
    h = (h ^ (h >> 16)) * 0x45d9f3b;
    h = (h ^ (h >> 16)) * 0x45d9f3b;
    h = h ^ (h >> 16);
    return h;
}

int Weather_SelectSet(int weatherType, int timeSlot){
    unsigned int hash = WeatherHash(weatherType, timeSlot);
    float r = (float)(hash & 0xFFFF) / 65535.0f;
    return (r < 0.7f) ? WEATHER_SET_DEFAULT : WEATHER_SET_2;
}
```

### Timecyc Format
- 23 weather types × 8 time slots = 184 entries
- Time slots: Midnight, 5AM, 6AM, 7AM, Midday, 7PM, 8PM, 10PM
- Tab-separated values per line
- Fields: Amb, AmbObj, Dir, SkyTop, SkyBot, SunCore, SunCorona, SunSz, SprSz, SprBght, Shdw, LightShd, PoleShd, FarClp, FogSt, LightOnGround, LowCloudsRGB, BottomCloudRGB, WaterRGBA, Alpha1, RGB1, Alpha2, RGB2, CloudAlpha, Illumination

### Weather Types (from CWeather enum)
| ID | Name | Region |
|----|------|--------|
| 0 | EXTRASUNNY_LA | Los Santos |
| 1 | SUNNY_LA | Los Santos |
| 2 | EXTRASUNNY_SMOG_LA | Los Santos |
| 3 | SUNNY_SMOG_LA | Los Santos |
| 4 | CLOUDY_LA | Los Santos |
| 5 | SUNNY_SF | San Fierro |
| 6 | EXTRASUNNY_SF | San Fierro |
| 7 | CLOUDY_SF | San Fierro |
| 8 | RAINY_SF | San Fierro |
| 9 | FOGGY_SF | San Fierro |
| 10 | SUNNY_VEGAS | Las Venturas |
| 11 | EXTRASUNNY_VEGAS | Las Venturas |
| 12 | CLOUDY_VEGAS | Las Venturas |
| 13 | EXTRASUNNY_COUNTRYSIDE | Countryside |
| 14 | SUNNY_COUNTRYSIDE | Countryside |
| 15 | CLOUDY_COUNTRYSIDE | Countryside |
| 16 | RAINY_COUNTRYSIDE | Countryside |
| 17 | EXTRASUNNY_DESERT | Desert |
| 18 | SUNNY_DESERT | Desert |
| 19 | SANDSTORM_DESERT | Desert |
| 20 | UNDERWATER | Special |
| 21 | EXTRACOLOURS_1 | Special |
| 22 | EXTRACOLOURS_2 | Special |

### Future: LA 1992 Climate Model
Based on real weather data for Los Angeles in 1992:
- **January**: Cool, occasional rain, 15-20°C
- **April**: Warming, dry, 18-25°C
- **July**: Hot, dry, 25-35°C
- **October**: Warm, Santa Ana winds, 20-30°C

#### Wind System
- Real-time wind vectors from game clock
- Wind direction changes based on time of day
- Santa Ana events (strong east→west wind)
- Coastal breeze (west→east afternoon)

#### Location-Based Weather
- **Los Santos**: Smog, warm, occasional rain
- **San Fierro**: Fog, cool, frequent rain
- **Las Venturas**: Clear, hot, sandstorms

## Related
- [[SkyGFX Pipeline Overview]] — rendering pipeline
- [[GTA San Andreas EPK Reference]] — visual target screenshots

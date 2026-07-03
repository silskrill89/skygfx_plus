# Water Rendering

#water #rendering

## Overview
Water rendering with parallax occlusion mapping and reflection system.

## Water Parallax Shader
**File**: `shaders/ps/Water_Parallax.hlsl`
- Parallax occlusion mapping for wave detail
- Reflection from env map
- Config: `waterReflectionFarClip`

## Water System Hooks
- `CWaterLevel__CalculateWavesForCoordinate` — wave height/color
- `CWaterLevel__RenderAndEmptyRenderBuffer` — water render buffer
- Water drops on Neo vehicles (`neoWaterDrops`)
- Blood drops (`neoBloodDrops`)

## INI
```ini
waterReflectionFarClip=...
neoWaterDrops=1
neoBloodDrops=0
```

## See Also
- [[PostFX Pipeline]] — Other effects
- [[INI Configuration]] — Config fields

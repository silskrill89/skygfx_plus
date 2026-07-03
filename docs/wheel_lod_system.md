# Wheel LOD System

## Overview
Wheels use distance-based Level of Detail (LOD) switching to optimize performance.
Three LOD levels based on camera distance from wheel.

## LOD Levels

### LOD0 (High Detail)
- **Distance**: 0-15 meters
- **Geometry**: Full detail (500-2000 triangles)
- **Textures**: 512x512 or higher
- **Features**: 
  - Full spoke detail
  - Bolt/nut geometry
  - Brake disc visible
  - Tire tread pattern
- **Use**: Close-up views, first-person, cutscenes

### LOD1 (Medium Detail)
- **Distance**: 15-40 meters
- **Geometry**: Reduced detail (200-500 triangles)
- **Textures**: 256x256
- **Features**:
  - Simplified spoke geometry
  - No bolt details
  - Brake disc simplified
  - Tire tread simplified
- **Use**: Normal gameplay, third-person view

### LOD2 (Low Detail)
- **Distance**: 40+ meters
- **Geometry**: Minimal (50-100 triangles)
- **Textures**: 128x128
- **Features**:
  - Basic circle for rim
  - No spoke detail
  - No brake disc
  - Simple tire cylinder
- **Use**: Distant vehicles, traffic

## Implementation

### DFF Structure
Each wheel DFF contains multiple atomics with LOD suffixes:
```
wheel_sport_01_uni         (LOD0 - high detail)
wheel_sport_01_uni_lod1    (LOD1 - medium detail)
wheel_sport_01_uni_lod2    (LOD2 - low detail)
```

### Atomic Naming
- Base name: `wheel_{class}_{style}_{variant}`
- LOD1: append `_lod1`
- LOD2: append `_lod2`

### Distance Calculation
```cpp
float distance = Vector3::Distance(cameraPos, wheelPos);
if (distance < 15.0f) {
    RenderLOD0();  // High detail
} else if (distance < 40.0f) {
    RenderLOD1();  // Medium detail
} else {
    RenderLOD2();  // Low detail
}
```

### Performance Impact
- **LOD0**: ~2000 triangles, ~1MB texture
- **LOD1**: ~500 triangles, ~256KB texture
- **LOD2**: ~100 triangles, ~64KB texture
- **Savings**: 75-95% triangle reduction at distance

## Creation Guidelines

### LOD0 (High Detail)
- Model full spoke geometry
- Include bolt/nut details (6-8 bolts)
- Add brake disc with ventilation holes
- Model tire tread pattern
- UV unwrap for 512x512 texture

### LOD1 (Medium Detail)
- Reduce spoke count by 50%
- Remove bolt geometry
- Simplify brake disc to flat disc
- Simplify tire tread to basic pattern
- UV unwrap for 256x256 texture

### LOD2 (Low Detail)
- Single circle for rim face
- No spoke detail
- No brake disc
- Simple cylinder for tire
- UV unwrap for 128x128 texture

## Metadata
```json
{
  "wheel_sport_01_uni": {
    "lod0": {
      "triangles": 1500,
      "texture": "wheel_sport_01_512.png"
    },
    "lod1": {
      "triangles": 400,
      "texture": "wheel_sport_01_256.png"
    },
    "lod2": {
      "triangles": 80,
      "texture": "wheel_sport_01_128.png"
    }
  }
}
```

## Testing
- Verify LOD switching at correct distances
- Check for popping/artifacts during transitions
- Measure FPS impact with multiple wheels at different LODs
- Validate texture memory usage

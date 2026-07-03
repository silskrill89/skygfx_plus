# Glass Shader

#shaders #vehicles #glass

## Overview
Vehicle glass rendering with physically-based Fresnel reflections and per-vehicle tint system.

**File**: `shaders/ps/Glass_Vehicle.hlsl`
**Profile**: ps_3_0

## Features
- **Schlick Fresnel** with glass IOR 1.5 → F0 = 0.04
- **Sphere env mapping** (no pole pinching) via `SphereEnvMapUV()`
- **Sun lighting** — sunspot + broad highlight + Fresnel hotspot
- **Per-vehicle tint** from C++ classification
- **Edge darkening** at grazing angles

## Fresnel Model
```
F = F0 + (1 - F0) * (1 - cosθ)⁵
```

## Per-Vehicle Tint System
Computed in C++ (`vehiclePipe.cpp`), passed as constants:
- **c22** = `{ opacity, tintR, tintG, tintB }`
- **c23** = `{ isLight, lightBoost, 0, 0 }`

### Detection Logic
1. Glass: `hasAlpha && alpha < 255 && texture NOT "vehiclelights"`
2. Light: `hasAlpha && alpha < 255 && texture IS "vehiclelights"`

### Tint Profiles by Vehicle Class
| Class | Model IDs | Tint |
|-------|-----------|------|
| Taxi | 420, 438 | Dark black (0.05, 0.05, 0.08) |
| Cop | 596-598, 427, 490, 528 | Dark black |
| Gang | 402, 467, 474, 478, 567, etc. | Dark black |
| Lowrider | 534-536, 575, 576 | Clear (0, 0, 0) |
| Casual | All others | Blue/turquoise (hash-based) |

## Blend Modes
- **Glass**: `SRCALPHA / INVSRCALPHA` (standard alpha)
- **Light**: `SRCALPHA / ONE` (additive glow)

## See Also
- [[VehiclePBR Modern]] — Main vehicle paint shader
- [[Vehicle Classification]] — How meshes are detected
- [[Vehicle Pipeline]] — How glass is routed

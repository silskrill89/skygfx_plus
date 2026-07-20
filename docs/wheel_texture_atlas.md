# Wheel Texture Atlas System

## Status: Planned / Not Yet Implemented

> **Note**: This document describes the planned texture atlas system. The current implementation loads individual wheel textures per DFF. No atlas packing or UV remapping is implemented yet.

## Overview
Combine multiple wheel textures into a single atlas to reduce draw calls and improve performance. One 512x512 atlas can hold 4 wheel textures (256x256 each). One 1024x1024 atlas can hold 16.

## Atlas Layout

### 512x512 Atlas (4 wheels)
```
+----------------+----------------+
|                |                |
|   Wheel 01     |   Wheel 02     |
|   (256x256)    |   (256x256)    |
|                |                |
+----------------+----------------+
|                |                |
|   Wheel 03     |   Wheel 04     |
|   (256x256)    |   (256x256)    |
|                |                |
+----------------+----------------+
```

### 1024x1024 Atlas (16 wheels)
```
+--------+--------+--------+--------+
|  W01   |  W02   |  W03   |  W04   |
+--------+--------+--------+--------+
|  W05   |  W06   |  W07   |  W08   |
+--------+--------+--------+--------+
|  W09   |  W10   |  W11   |  W12   |
+--------+--------+--------+--------+
|  W13   |  W14   |  W15   |  W16   |
+--------+--------+--------+--------+
```

## Texture Channels

| Atlas | Channels | Purpose |
|-------|----------|---------|
| Diffuse (`wheel_atlas_diffuse.png`) | RGB = base color, A = specular mask | Main appearance |
| Normal (`wheel_atlas_normal.png`) | RG = XY normal, B = height (optional) | Surface detail |
| Specular (`wheel_atlas_spec.png`) | R = intensity, G = glossiness | Reflection control |

## UV Remapping

Each wheel's UVs must be remapped to the correct atlas quadrant:

| Quadrant | UV Range |
|----------|----------|
| Top-left (Wheel 01) | (0.0-0.5, 0.0-0.5) |
| Top-right (Wheel 02) | (0.5-1.0, 0.0-0.5) |
| Bottom-left (Wheel 03) | (0.0-0.5, 0.5-1.0) |
| Bottom-right (Wheel 04) | (0.5-1.0, 0.5-1.0) |

## Performance Benefits

| Metric | Before (16 wheels) | After (1 atlas) | Savings |
|--------|-------------------|-----------------|---------|
| Draw calls | 48 (16 × 3 textures) | 3 (1 × 3 textures) | 94% |
| Memory | 16MB (16 × 512x512) | 4MB (1 × 1024x1024) | 75% |

## Metadata Format

```json
{
  "atlas": {
    "diffuse": "wheel_atlas_diffuse.png",
    "normal": "wheel_atlas_normal.png",
    "specular": "wheel_atlas_spec.png",
    "size": [512, 512],
    "layout": [
      {"wheel": "wheel_sport_01_uni", "quadrant": [0, 0]},
      {"wheel": "wheel_sport_02_uni", "quadrant": [1, 0]},
      {"wheel": "wheel_alloy_01_uni", "quadrant": [0, 1]},
      {"wheel": "wheel_alloy_02_uni", "quadrant": [1, 1]}
    ]
  }
}
```

## See Also
- [[wheel_naming_convention.md]] — Naming conventions
- [[Wheel System Architecture]] — Full system overview
- [[Wheel Extender Technical Plan]] — Implementation plan

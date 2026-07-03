# SMAA: Enhanced Subpixel Morphological Anti-Aliasing
# Source: `E:\docs\SMAA-Enhanced-Subpixel-Morphological-Antialiasing.pdf`

## Overview
- Morphological anti-liasing based on Jimenez's MLAA
- 3-pass algorithm: Edge Detection → Blend Weight Calculation → Neighborhood Blending
- Supports luma, color, and depth-based edge detection
- High quality with minimal performance overhead

## Implementation for SkyGFX
- Already implemented in `src/postfx.cpp` (DrawSMAA)
- Uses 3 render passes with separate shaders:
  - `SMAA_Edge` — edge detection (luminance-based)
  - `SMAA_BlendWeight` — blend weight calculation
  - `SMAA_BlendNeighbor` — final neighborhood blending
- Config: `smaaPreset` (0=LOW, 1=MEDIUM, 2=HIGH, 3=ULTRA)

## Key Parameters
- `smaaThreshold` — edge detection threshold (lower = more edges detected)
- `smaaMaxSearchSteps` — max distance to search for edges
- `smaaCornerRounding` — corner rounding strength

## Related
- [[Conservative_Morphological_Antialiasing]] — simpler CMAA alternative
- [[Dynamic_Temporal_Antialiasing_Call_of_Duty]] — temporal extension
- [[Non-Parametric Sparse BRDF]] — material reference

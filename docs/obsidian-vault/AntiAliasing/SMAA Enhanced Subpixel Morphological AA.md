---
tags: [anti-aliasing, smaa, postfx]
created: 2025-01-02
updated: 2026-07-15
---

# SMAA: Enhanced Subpixel Morphological Anti-Aliasing

> [!info] Source paper
> `E:\docs\SMAA-Enhanced-Subpixel-Morphological-Antialiasing.pdf`

## Overview
- Morphological anti-aliasing based on Jimenez's MLAA
- 3-pass algorithm: Edge Detection → Blend Weight Calculation → Neighborhood Blending
- Supports luma, color, and depth-based edge detection
- High quality with minimal performance overhead

## Implementation for SkyGFX
- Implemented in `src/render/SMAA.cpp` and `src/render/postfx.cpp`
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
- [[Conservative Morphological AA]] — simpler CMAA alternative
- [[Dynamic Temporal AA Call of Duty]] — temporal extension
- [[Non-Parametric Sparse BRDF]] — material reference
- Full docs: `docs/PostFX Pipeline.md`

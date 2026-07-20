---
tags: [anti-aliasing, temporal, postfx]
created: 2025-01-02
updated: 2026-07-15
---

# Dynamic Temporal Anti-Aliasing and Upsampling (Call of Duty)

> [!info] Source paper
> `E:\docs\Dynamic_Temporal_Antialiasing_and_Upsampling_in_Call_of_Duty_v4.pdf`

## Overview
- Temporal AA used in Call of Duty (Infinity Ward)
- Combines jittered samples across frames with motion-vector reprojection
- Handles sub-pixel detail accumulation over time
- Includes upsampling for rendering at lower resolution

## Key Techniques
1. **Jittered projection matrix** — shifts sample position each frame
2. **Motion vectors** — reproject previous frame to current
3. **Blend factor** — lerp between current and history (typically 0.9/0.1)
4. **Neighborhood clamping** — clamp history to current frame's neighborhood
5. **Sharpening** — post-TAA sharpen to recover detail

## Application to SkyGFX
- Could extend SMAA with temporal component (already have SMAA_Temporal shader)
- `smaaTemporal` config flag exists but implementation incomplete
- Motion vectors available from depth buffer (frame-to-frame reprojection)
- Key challenge: GTA SA has no motion vector pass — need to derive from depth

## Implementation Notes
- Render at full resolution, accumulate over N frames
- Each frame: jitter camera by sub-pixel amount
- Reproject using depth-derived motion vectors
- Clamp history to 3x3 neighborhood of current pixel
- Blend: `final = lerp(history, current, 0.1)`

## Related
- [[SMAA Enhanced Subpixel Morphological AA]] — base SMAA
- [[Conservative Morphological AA]] — simpler alternative
- [[Non-Parametric Sparse BRDF]] — material reference

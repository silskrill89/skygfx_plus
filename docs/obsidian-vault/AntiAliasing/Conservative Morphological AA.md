---
tags: [anti-aliasing, cmaa, postfx]
created: 2025-01-02
updated: 2026-07-15
---

# Conservative Morphological Anti-Aliasing (CMAA)

> [!info] Source paper
> `E:\docs\conservative-morphological-anti-aliasing.pdf`

## Overview
- Simplified morphological AA — fewer passes than SMAA
- Conservative approach: only anti-alias edges that are clearly visible
- Single-pass or 2-pass implementation
- Lower quality than SMAA but much faster

## Key Techniques
1. **Edge detection** — luma-based, conservative threshold
2. **Shape identification** — classify edge patterns (L, T, X shapes)
3. **Blend weight** — simple blend based on edge shape
4. **Single-pass variant** — all steps in one shader pass

## Application to SkyGFX
- Good fallback for low-end hardware when SMAA is too expensive
- Could replace SMAA on LOW preset
- Already have SMAA_Edge shader — CMAA uses similar edge detection
- Single-pass variant could be a simple post-process

## Comparison with SMAA
| Feature | CMAA | SMAA |
|---------|------|------|
| Passes | 1-2 | 3 |
| Quality | Good | Excellent |
| Performance | Fast | Moderate |
| Edge detection | Luma | Luma/Color/Depth |
| Search distance | Short | Long |

## Related
- [[SMAA Enhanced Subpixel Morphological AA]] — full SMAA
- [[Dynamic Temporal AA Call of Duty]] — temporal extension
- [[Non-Parametric Sparse BRDF]] — material reference

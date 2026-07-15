---
tags: [brdf, research, paper]
created: 2025-01-02
updated: 2026-07-15
---

# Non-Parametric Sparse BRDF (TOG 2021)

> [!info] Source paper
> `E:\docs\TOG2021_non_parametric_sparse_brdf_final.pdf`

## Overview
- Fits measured BRDF data using **Gaussian basis functions** (2-32 lobes)
- Works with **MERL BRDF database** — 100+ real-world measured materials
- Non-parametric: captures real-world complexity that parametric models (Disney, GGX) miss
- Supports RGB and spectral rendering

## Key Findings for SkyGFX
- Real-world BRDFs are NOT well-captured by parametric models alone
- Measured data > parametric models for accuracy
- Gaussian lobes can represent anisotropic highlights, layered materials, retro-reflection
- **Practical use**: our BRDF table values should be informed by measured data, not just theory

## MERL Database Materials (relevant to GTA SA)
| Material | Category | Notes |
|----------|----------|-------|
| Metallic automotive paints | Paint | Clearcoat + base layer |
| Fabrics (silk, velvet, corduroy) | Cloth | Anisotropic highlights |
| Wood (varnished, bare) | Wood | Varnish = clearcoat layer |
| Metals (aluminum, brass, copper, steel) | Metal | Complex IOR values |
| Stone (marble, granite) | Stone | Subsurface scattering |
| Plastic (opaque, translucent) | Plastic | Specular varies greatly |
| Rubber | Rubber | Very rough, low specular |

## Gaussian Basis BRDF Model
```
f(θi, φi, θr, φr) = Σ wⱼ · G(μᵢ, μᵣ, σⱼ)  (j = 1..N lobes)
```
- Each lobe G is a 2D Gaussian on the sphere
- Weights wⱼ, centers μ, widths σ fitted to measured data
- N=16 lobes gives good quality for most materials
- N=2-4 lobes sufficient for simple dielectrics

## Application to SkyGFX
- Use measured F0 values from MERL (not just IOR-derived)
- Layered materials (paint + clearcoat) need two-lobe model
- Anisotropic materials (brushed metal, silk) need elliptical Gaussians
- For GTA SA: most surfaces are simple dielectrics — 2-lobe model sufficient

## Related Papers in Vault
- [[SMAA Enhanced Subpixel Morphological AA]]
- [[Dynamic Temporal AA Call of Duty]]
- [[Conservative Morphological AA]]

## Full docs
- `docs/BRDF Reference.md`, `docs/BRDF_REFERENCE.md`

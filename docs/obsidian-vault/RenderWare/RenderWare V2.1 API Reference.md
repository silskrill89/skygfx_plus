---
tags: [renderware, rw, api, reference]
created: 2025-01-02
updated: 2026-07-15
---

# RenderWare V2.1 API Reference (1997)

> [!info] Source
> `E:\docs\rendfereware\rwdoc.htm`
> V2.1 — GTA SA uses RW 3.7. Core concepts carry over.

## Overview
- RenderWare is the 3D rendering engine used by GTA III, VC, SA
- V2.1 (1997) is the documented version — V3.x is significantly different
- Core concepts (clumps, atomics, materials, textures) are consistent across versions

## Key Concepts (V2.1 → V3.x)
| V2.1 Concept | V3.x Equivalent | Notes |
|--------------|-----------------|-------|
| Clump | RpClump | Collection of atomics |
| Atomic | RpAtomic | Single mesh + material list |
| Material | RpMaterial | Texture + color + surface props |
| Texture | RwTexture | Texture wrapper |
| Camera | RwCamera | Rendering viewport |
| Light | RpLight | Light source |
| World | RpWorld | BSP world |

## Material System (from RW docs)
- `RpMaterial` holds: texture, color (RGBA), surface properties
- Surface properties: ambient, diffuse, specular (float3)
- Material pipeline: RpMatFX for effects (env map, bump, dual pass)
- Material list on each atomic — per-mesh material assignment

## Pipeline Architecture
- **Atomic pipeline**: processes individual meshes
- **Material pipeline**: processes materials on meshes
- **Skin pipeline**: character skinning (RpSkin)
- **MatFX pipeline**: material effects (env map, bump)
- **Custom pipelines**: our PBR vehicle/building pipes

## Relevant RW 3.7 APIs (not in V2.1 docs)
- `RwD3D9SetTexture(RwTexture*, stage)` — set texture on stage
- `RwD3D9SetVertexShader(void*)` — set vertex shader
- `RwD3D9SetPixelShader(void*)` — set pixel shader
- `RwD3D9SetVertexShaderConstant(reg, data, count)` — upload VS constant
- `RwD3D9SetPixelShaderConstant(reg, data, count)` — upload PS constant
- `RwD3D9SetTextureStageState(stage, type, value)` — texture stage state
- `RwD3D9CreateVertexShader(data, shader)` — create VS from CSO
- `RwD3D9CreatePixelShader(data, shader)` — create PS from CSO

## User Guide PDFs
- Vol1: `E:\docs\rendfereware\UserGuideVol1.pdf`
- Vol2: `E:\docs\rendfereware\UserGuideVol2.pdf`
- Vol3: `E:\docs\rendfereware\UserGuideVol3.pdf`
- BuildRW: `E:\docs\rendfereware\BuildRW.pdf`
- Camera: `E:\docs\rendfereware\cameraview.pdf`

## Related
- [[Non-Parametric Sparse BRDF]] — BRDF reference
- [[SkyGFX Pipeline Overview]] — our rendering pipeline
- [[GTA IV RAGE Engine Format Reference]] — RAGE format comparison
- Full docs: `docs/RW SDK Reference.md`

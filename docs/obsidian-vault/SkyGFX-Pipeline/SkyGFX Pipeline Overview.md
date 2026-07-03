# SkyGFX Plus - Rendering Pipeline Overview

## Architecture
3-step forward+ pipeline:
1. **Prepass** — main pass, gbuffer fill
2. **Dynamic Sky + PBR Lighting** — sky rendering, light accumulation
3. **Clamp + PostFX** — tonemapping, AA, color grading

## Vehicle Pipeline (CAR_MODERN)
Entry: `CCustomCarEnvMapPipeline__CustomPipeRenderCB_Env` in `vehiclePipe.cpp`

### Mesh Classification (per-mesh in callback)
| Path | VS | PS | Condition |
|------|----|----|-----------|
| Glass + Lights | `vehiclePBRVS` | `Glass_Vehicle` | isGlassMesh or isLightMesh |
| Tires | `vehiclePBRVS` | `Rubber_Vehicle` | isTireMesh |
| Opaque PBR | `vehiclePBRVS` | `VehiclePBR_Modern` | default (paint) |

### Texture Binding (PBR path)
| Stage | Texture | PS Register |
|-------|---------|-------------|
| s0 | Diffuse (material) | `diffuseTex` |
| s1 | Normal buffer (raw D3D9) | `normalBufTex` |
| s2 | Reflection mask (RwTexture) | `maskTex` |
| s3 | IBL buffer (raw D3D9) | `iblTex` |
| s4 | Env map / reflection (RwTexture) | `envMapTex` |

### Shader Constants (PBR path)
| Register | Content | Source |
|----------|---------|--------|
| c0 | surfProps (ambient, diffuse, specular, prelight) | material |
| c1 | fxParams (fresnel, power, lightmult, shininess) | material |
| c2 | eyePos | camera |
| c3 | iblParams (roughness, metalness, 0, 0) | BRDF |
| c4 | cloudShadow (sunDir, time, 1, 0) | weather |
| c5-c11 | directCol + lightCol[6] | lights |
| c12-c18 | directDir + lightDir[6] | lights |
| c19 | matCol | material |
| c22 | pbrParams (glossiness, reflectance, clearcoat, subsurface) | BRDF |
| c23 | paintNoise (wheel, noiseScale, edgeBlend, 0) | paint type |
| c24 | ambientColor (r, g, b, normalBufFlag) | timecycle |

## Building Pipeline (BUILDING_PBR)
Entry: building pipe callback in `buildingPipe.cpp`
- Uses vertex color as base color (SA buildings use painted vertex colors)
- Texture on top for detail
- BRDF values from material table based on texture name pattern

## PostFX Chain
1. DynamicSky — sky + clouds (IBL buffer)
2. SSAO — screen-space ambient occlusion
3. Color grading — YCbCr correction + Reinhard tonemapping
4. SSS blur — skin translucency
5. SMAA — anti-aliasing (3-pass)

## Material System
- BRDF table in `brdfLibrary.h` — 37+ surface types
- Glossiness workflow: `glossiness = 1.0 - roughness`
- All dielectric (metalness = 0 for most surfaces)
- F0 from IOR: `((n-1)/(n+1))^2`
- Texture → BRDF mapping via pattern matching on texture names

## Related
- [[Non-Parametric Sparse BRDF]] — BRDF reference
- [[RenderWare V2.1 API Reference]] — RW pipeline
- [[SMAA Enhanced Subpixel Morphological AA]] — AA system

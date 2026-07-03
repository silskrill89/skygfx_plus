---
tags: [shaders, pbr, vehicles, entry-points]
created: 2025-01-02
updated: 2025-01-02
---

# VehiclePBR Modern

## Overview
The unified vehicle pixel shader. **7 entry points** in one HLSL file, covering every vehicle rendering path from PS2 classics to full PBR.

**File**: `shaders/ps/VehiclePBR_Modern.hlsl`
**Profile**: ps_3_0 (main PBR), ps_2_0 (legacy entry points compiled separately)

## Entry Points

### `main` — PBR Vehicle
Full PBR rendering with GGX/Smith/Schlick specular BRDF.

**Features**:
- Burley/Disney diffuse (non-metals)
- Energy conservation: `kD = (1-kS) * (1-metalness)`
- Sphere env mapping (no pole pinching) via `SphereEnvMapUV()`
- Multi-light support (sun + 6 point lights)
- IBL ambient fill from IBL buffer
- Cloud shadow FBM noise
- Surface noise for reflection breakup
- Wheel metallic white noise
- Normal buffer blending (stereo disparity)
- Paint tint from carcols (F0 = paint color for metals)

**Constants**:
| Register | Content |
|----------|---------|
| c0 | surfProps |
| c1 | fxParams (.z = envIntensity) |
| c2 | eyePos |
| c3 | iblParams |
| c4 | cloudShadow |
| c5 | directCol |
| c6-c11 | lightCol[6] |
| c12 | directDir |
| c13-c18 | lightDir[6] |
| c19 | matCol |
| c22 | {glossiness, specular, specTint, envFresnel} |
| c23 | {wheelFlag, noiseScale, edgeBlend, 0} |
| c24 | {ambientColor.xyz, normalBufEnable} |

**Textures**: s0=diffuse, s1=envMap, s2=mask, s3=IBL, s4=normalBuffer

### `main_rubber` — Parametric Rubber/Tire
PBR rubber material with dirt/wear tinting and subsurface wrap.

**Features**:
- Parametric roughness/F0 from c22
- Dirt level tinting (brownish-grey)
- Wear factor tinting (lighter grey)
- Subsurface wrap diffuse
- Fresnel sheen at grazing angles

**Constants**:
| Register | Content |
|----------|---------|
| c22 | {roughness, F0, tintR, tintG} |
| c23 | {tintB, dirtLevel, wearFactor, 0} |

**Implementation**: Merged from standalone `Rubber_Vehicle.hlsl` into unified shader.

### `main_envCar` — PS2/PC Environment Car
Sphere reflection + multi-light specular. Energy-conserved env blend.

### `main_ps2EnvSpecFx` — PS2 Env+Spec Dual-Layer
Two-layer env map + specular texture blend.

### `main_specCarFx` — Specular Car FX
Env map * envcolor + speccolor.

### `main_mobileVehicle` — Mobile Vehicle
Env lerp with shininess control + specular add.

### `main_normMapVehicle` — Normal-Mapped Vehicle
Env blend with normal map detail.

## PBR Material Classification
| roughness | Type |
|-----------|------|
| < 0.2 | Chrome/mirror |
| < 0.5 | Painted metal |
| > 0.5 | Matte/rubber |

`metalness = 0` → dielectric, `1` → metal

## F0 Calculation
```hlsl
float3 F0 = lerp(float3(reflectance), paintTint, metalness);
```
- Dielectric: F0 = reflectance scalar
- Metal: F0 = paint color (energy-conserved via kD = 0)

## IBL Tinting
The IBL (Image-Based Lighting) buffer tints the env map reflection rather than replacing it:
```hlsl
float3 envRefl = tex2D(envMapTex, envReflUV).rgb;
float3 iblTint = tex2D(iblTex, envReflUV).rgb;
float3 layer2 = envRefl * iblTint * paintTint * clearCoatF * envFresnel;
```

This provides sky color variation while preserving the actual scene reflection.

## See Also
- [[03-Shaders/PBR Common]] — Shared GGX/Smith/Schlick functions
- [[02-Pipelines/Vehicle Pipeline]] — How each entry point is selected
- [[03-Shaders/Glass Shader]] — Separate glass rendering
- [[03-Shaders/Shader Architecture]] — Compilation and loading
- [[06-Technical-Decisions/IBL Env Map Decision]] — Why IBL tints instead of replaces

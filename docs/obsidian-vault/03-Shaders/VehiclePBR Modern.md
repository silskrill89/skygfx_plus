---
tags: [shaders, pbr, vehicles, entry-points]
created: 2025-01-02
updated: 2026-07-15
---

# VehiclePBR Modern

> [!info] Full documentation
> See `docs/VehiclePBR Modern.md` for the complete shader reference.

## Overview
The unified vehicle pixel shader. **7 entry points** in one HLSL file, covering every vehicle rendering path from PS2 classics to full PBR.

**File**: `shaders/ps/VehiclePBR_Modern.hlsl`
**Profile**: ps_3_0

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
| c22 | pbrParams {glossiness, reflectance, clearcoat, subsurface} |
| c23 | paintNoise {wheelFlag, noiseScale, edgeBlend, 0} |
| c24 | ambientColor {r, g, b, normalBufFlag} |

**Textures**:
| Stage | Sampler | Purpose |
|-------|---------|---------|
| s0 | `diffuseTex` | Diffuse (material) |
| s1 | `envMapTex` | Env map / reflection |
| s2 | `maskTex` | Reflection mask |
| s3 | `iblTex` | IBL buffer |
| s4 | `normalBufTex` | Normal buffer |

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
| c22 | pbrParams {roughness, F0, tintR, tintG} |
| c23 | paintNoise {tintB, dirtLevel, wearFactor, 0} |

**Implementation**: Merged from standalone `Rubber_Vehicle.hlsl` into unified shader.

### `main_ps2EnvSpecFx` — PS2 Env+Spec Dual-Layer
Two-layer env map + specular texture blend. Uses `envMapTex` (s1) and `maskTex` (s2).

### `main_specCarFx` — Specular Car FX
Env map * envcolor + speccolor. Simple single-texture lookup.

### `main_mobileVehicle` — Mobile Vehicle
Env lerp with shininess control + specular add. Uses `diffuseTex` (s0) and `envMapTex` (s1).

### `main_normMapVehicle` — Normal-Mapped Vehicle
Env blend with normal map detail. Uses `diffuseTex` (s0) and `envMapTex` (s1).

### `main_building` — Building PBR
Unified building shader using vertex color as base color. Full PBR with GGX/Smith/Schlick. Uses `diffuseTex` (s0) and `iblTex` (s3).

> [!warning] Non-existent entry points
> The following entry points do **NOT** exist in VehiclePBR_Modern.hlsl:
> - `main_envCar` — does not exist (use `main` for PBR env car)
> - `main_glass` — glass has its own file: `shaders/ps/Glass_Vehicle.hlsl`
> - `main_leeds` — does not exist (Leeds pipes use separate shaders)

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
- [[../docs/PBR Common]] — Shared GGX/Smith/Schlick functions
- [[../docs/Vehicle Pipeline]] — How each entry point is selected
- [[../docs/Glass Shader]] — Separate glass rendering
- [[../docs/Shader Architecture]] — Compilation and loading
- [[IBL Env Map Decision]] — Why IBL tints instead of replaces

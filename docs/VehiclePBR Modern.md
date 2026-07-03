# VehiclePBR Modern

#shaders #pbr #vehicles

## Overview
The unified vehicle pixel shader. **6 entry points** in one HLSL file, covering every vehicle rendering path from PS2 classics to full PBR.

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
| c22 | {roughness, metalness, reflectance, envFresnel} |
| c23 | {wheel, noiseScale, edgeBlend, paintType} |

**Textures**: s0=diffuse, s1=envMap, s2=mask, s3=IBL, s4=normalBuffer

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

## See Also
- [[PBR Common]] — Shared GGX/Smith/Schlick functions
- [[Vehicle Pipeline]] — How each entry point is selected
- [[Glass Shader]] — Separate glass rendering
- [[Shader Architecture]] — Compilation and loading

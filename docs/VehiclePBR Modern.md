# VehiclePBR Modern

#shaders #pbr #vehicles

## Overview
The unified vehicle pixel shader. **7 entry points** in one HLSL file, covering every vehicle rendering path from PS2 classics to full PBR.

**File**: `shaders/ps/VehiclePBR_Modern.hlsl`
**Profile**: ps_3_0 (main PBR, rubber, building), ps_2_0 (legacy entry points compiled separately)

## Entry Points

### `main` — PBR Vehicle
Full PBR rendering with GGX/Smith/Schlick specular BRDF. Uses glossiness workflow (everything is dielectric, no metalness).

**Features**:
- Burley/Disney diffuse (non-metals)
- Energy conservation: `kD = 1 - kS`
- Sphere env mapping (no pole pinching) via `SphereEnvMapUV()`
- Multi-light support (sun + 6 point lights)
- IBL ambient fill from IBL buffer
- Cloud shadow FBM noise
- Surface noise for reflection breakup
- Wheel metallic white noise
- Normal buffer blending (stereo disparity)
- Paint tint from carcols
- Soft Reinhard tonemap + sRGB gamma encode

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
| c22 | pbrParams = {glossiness, specularF0, specTint, envFresnel} |
| c23 | paintNoise = {wheel, noiseScale, edgeBlend, 0} |
| c24 | ambientColor = {ambientR, ambientG, ambientB, normalBufEnable} |

**Textures**: s0=diffuse, s1=envMap, s2=mask, s3=IBL, s4=normalBuffer

### `main_rubber` — PBR Rubber/Tire
Parametric rubber BRDF with dirt/wear tint and subsurface wrap lighting.

**Constants**: c22 = {roughness, rubberF0, tintR, tintG}, c23 = {tintB, dirtLevel, wearFactor, 0}

### `main_ps2EnvSpecFx` — PS2 Env+Spec Dual-Layer
Two-layer env map + specular texture blend. Compiled as ps_2_0.

### `main_specCarFx` — Specular Car FX
Env map * envcolor + speccolor. Compiled as ps_2_0.

### `main_mobileVehicle` — Mobile Vehicle
Env lerp with shininess control + specular add. Compiled as ps_2_0.

### `main_normMapVehicle` — Normal-Mapped Vehicle
Env blend with normal map detail.

### `main_building` — PBR Building
Material-driven PBR building shader. Uses BRDF material properties from the material library. Supports day/night blending via vertex alpha.

**Constants**: c22 = {glossiness, reflectance, clearcoat, subsurface}, c23 = {specularInt, metalness, 0, 0}

## Glossiness Workflow
The main PBR entry point uses **glossiness** (not roughness):
```hlsl
float roughness = 1.0 - glossiness;
```
- glossiness = 1.0 → smooth/shiny surface
- glossiness = 0.0 → rough/matte surface

## F0 Calculation
```hlsl
float3 F0 = lerp(float3(specularF0), paintTint * specularF0, specTint);
```
- `specTint = 0`: F0 = specular reflectance scalar (default ~0.04 for dielectrics)
- `specTint = 1`: F0 = paint color * specularF0 (tinted by car color)

## See Also
- [[PBR Common]] — Shared GGX/Smith/Schlick functions
- [[Vehicle Pipeline]] — How each entry point is selected
- [[Glass Shader]] — Separate glass rendering
- [[Shader Architecture]] — Compilation and loading

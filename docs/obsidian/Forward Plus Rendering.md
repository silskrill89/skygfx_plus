# GTA IV Forward+ Rendering

## Overview

SkyGFX Plus implements a Forward+ (tiled forward) renderer for GTA IV-style lighting on buildings and vehicles. This enables per-pixel PBR lighting with up to 8 dynamic lights per tile, running entirely within the D3D9 SM 3.0 constraint.

## Forward+ Pipeline

### Pass 1: Depth Pre-Pass

Renders all opaque geometry to a depth-only target. This avoids shading invisible fragments in the lighting pass.

**Cost**: ~5-10% additional vertex processing. Eliminates ~30-40% pixel shader work in dense scenes.

### Pass 2: Light Culling (CPU-side)

GTA SA's limited dynamic light count (0-8 visible lights) allows CPU-side light culling per tile:

1. Divide screen into 64x64 pixel tiles
2. For each light, compute screen-space bounding rectangle
3. Mark light index in tile's light mask (8 bits per tile)
4. Upload per-tile light list as shader constant array

**Why not compute shader?** D3D9 SM 3.0 has no compute shader support. CPU culling is sufficient for ≤8 lights.

### Pass 3: Shaded Render

Geometry is rendered with full PBR:
- GGX normal distribution
- Correlated Smith visibility
- Schlick Fresnel approximation
- Burley diffuse (vehicles) / Oren-Nayar diffuse (buildings)

## PBR Implementation

### BRDF Components

> See also: [[BRDF_References/PBR_BRDF_Reference]] for a comprehensive PBR BRDF reference covering GGX, Smith, Schlick, and diffuse models.

```hlsl
// GGX/Trowbridge-Reitz normal distribution
float GGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

// Smith correlated visibility (height-correlated)
float Smith(float3 N, float3 V, float3 L, float roughness) {
    float a = roughness * roughness;
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float vis = 0.5 / lerp(NdotL * (1.0 - a) + a, NdotV * (1.0 - a) + a, 0.5);
    return vis;
}

// Schlick Fresnel
float3 Schlick(float3 F0, float3 H, float3 V) {
    float cosTheta = max(dot(H, V), 0.0);
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}
```

### Diffuse Models

| Model | Formula | Used For |
|-------|---------|----------|
| Burley | `(1/PI) * (1 + (Fd90-1)*pow(1-NdotL,5)) * (1 + (Fd90-1)*pow(1-NdotV,5))` | Vehicles |
| Oren-Nayar | `(1/PI) * (A + B * max(0,cos(phi))*sin(alpha)*tan(beta))` | Buildings |

### Light Attenuation

```hlsl
float attenuation = 1.0 / (lightRange * lightRange);
float distSq = dot(lightVec, lightVec);
attenuation *= max(0.0, 1.0 - distSq / (lightRange * lightRange));
attenuation *= attenuation; // Smooth falloff
```

## Light Types

| Type | Source | Max Count |
|------|--------|-----------|
| Directional (Sun) | Timecyc.dat | 1 (always active) |
| Point lights | Dynamic objects, explosions, weapon fire | 8 (culled per tile) |
| Ambient | Timecyc.dat | 1 |

### Light Data Upload

Per-frame, the light data is packed into shader constants:

| Constants | Content |
|-----------|---------|
| c25-c32 | Light positions (x, y, z, range) |
| c33-c40 | Light colours (r, g, b, intensity) |
| c41 | Active light count per tile |
| c42 | Per-tile light bitmask |

## Performance

| Scene Complexity | Draw Calls | Pixel Shader | Frame Time |
|-----------------|------------|--------------|------------|
| 0 lights | ~3000 | ~40 instr | +5% |
| 4 lights | ~3000 | ~200 instr | +20% |
| 8 lights | ~3000 | ~360 instr | +35% |

**Optimal tile size**: 64x64 pixels (balance between CPU culling cost and GPU over-shading).

## Limitations

- **No shadow maps**: D3D9 SM 3.0 lacks depth-compare samplers for efficient PCF
- **No compute shaders**: Light culling is CPU-side (adequate for ≤8 lights)
- **Transparency**: Forward transparent geometry uses the same light grid but cannot re-cull
- **No IBL**: Image-based lighting is handled separately in the unified pipeline

## See Also

- [[Pipelines/Overview]]
- [[Shader Architecture]]
- [[Configuration Reference]]

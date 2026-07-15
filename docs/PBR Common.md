# PBR Common

#shaders #pbr #include

## Overview
Shared PBR functions included by [[VehiclePBR Modern]], [[Glass Shader]], and other PBR shaders.

**File**: `shaders/include/PBR_Common.hlsl`

## Functions

### `F_Schlick(cosTheta, F0)`
Schlick Fresnel approximation (vector version):
```hlsl
float3 F_Schlick(float cosTheta, float3 F0)
{
    float fresnel = pow(1.0 - cosTheta, 5.0);
    return F0 + (1.0 - F0) * fresnel;
}
```

### `SchlickFresnelScalar(cosTheta, f0)`
Scalar Fresnel for single-channel use (e.g. clearcoat):
```hlsl
float SchlickFresnelScalar(float cosTheta, float f0)
{
    return f0 + (1.0 - f0) * pow(saturate(1.0 - cosTheta), 5.0);
}
```

### `D_GGX(NdotH, roughness)`
GGX normal distribution function. Returns the microfacet distribution for the given half-angle and roughness.
```hlsl
float D_GGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}
```

### `V_SmithCorrelated(NdotV, NdotL, roughness)`
Smith correlated visibility function (geometry/shadowing term). Uses the height-correlated Smith model with a safety epsilon to avoid division by zero.
```hlsl
float V_SmithCorrelated(float NdotV, float NdotL, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float GGXL = NdotL * NdotL * (1.0 - a2) + a2;
    float GGXV = NdotV * NdotV * (1.0 - a2) + a2;
    return 0.5 / (sqrt(GGXV) * sqrt(GGXL) + 1e-5);
}
```

### `SphereEnvMapUV(normal, viewDir)`
Computes spherical environment map UVs without pole pinching. Adds a small view-direction offset for parallax effect:
```hlsl
float2 SphereEnvMapUV(float3 normal, float3 viewDir)
{
    float m = 2.0 * sqrt(dot(normal.xy, normal.xy) + (normal.z + 1.0) * (normal.z + 1.0));
    float2 envUV = normal.xy / m + 0.5;
    envUV += viewDir.xy * 0.04;
    return envUV;
}
```

### `ComputeSunContribution(N, V, L, F0, NdotL)`
Computes the combined sun contribution from three terms:
1. **Sunspot** — tight specular highlight (`pow(NdotH, 128) * 1.5`)
2. **Broad highlight** — wider reflection (`pow(reflDot, 16) * 0.3`)
3. **Fresnel hotspot** — edge glow (`F_Schlick(NdotV, F0).r * NdotL * 0.2`)

Returns `sunColor * saturate(sunSpot + sunBroad + fresnelHotspot)` where `sunColor ≈ (1.0, 0.95, 0.9)`.

### `PBR_chash(n)` / `PBR_cnoise3d(p)` / `PBR_cloudFBM(p)`
Cloud shadow FBM noise for ground vehicles. Three-octave fractal Brownian motion using hash-based 3D noise. Used for animated cloud shadow patterns on vehicle surfaces. Based on CloudWorks by Brian Tu (CC BY-NC-SA 3.0).

## See Also
- [[VehiclePBR Modern]] — Uses all these functions
- [[Glass Shader]] — Uses SphereEnvMapUV, F_Schlick, and ComputeSunContribution
- [[CarPaint_Reflections]] — Uses F_Schlick and D_GGX

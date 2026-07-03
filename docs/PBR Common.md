# PBR Common

#shaders #pbr #include

## Overview
Shared PBR functions included by [[VehiclePBR Modern]] and other PBR shaders.

**File**: `shaders/include/PBR_Common.hlsl`

## Functions

### `SphereEnvMapUV(normal, viewDir)`
Computes spherical environment map UVs without pole pinching.
```hlsl
float m = 2.0 * sqrt(dot(normal.xy, normal.xy) + (normal.z + 1.0) * (normal.z + 1.0));
return normal.xy / m + 0.5;
```

### `F_Schlick(cosTheta, F0)`
Schlick Fresnel approximation:
```hlsl
return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
```

### `D_GGX(NdotH, roughness)`
GGX normal distribution function.

### `V_SmithCorrelated(NdotV, NdotL, roughness)`
Smith correlated visibility function (geometry term).

### `ComputeSunContribution(N, V, L, F0, NdotL)`
Sunspot + broad highlight + Fresnel hotspot.

### `PBR_cloudFBM(pos)`
Cloud shadow FBM noise for ground vehicles.

## See Also
- [[VehiclePBR Modern]] — Uses all these functions
- [[Glass Shader]] — Uses SphereEnvMapUV and F_Schlick

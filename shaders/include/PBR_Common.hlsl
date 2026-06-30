// PBR_Common.hlsl - Shared PBR functions for all vehicle shaders (ps_3_0)
// Eliminates duplication across VehiclePBR_Modern, Glass_Vehicle, CarPaint_Reflections
//
// All vehicle shaders should #include this file for:
//   - SphereEnvMapUV (no-pinch sphere mapping)
//   - F_Schlick (Fresnel)
//   - D_GGX / V_SmithCorrelated (specular BRDF)
//   - ComputeSunContribution (sunspot + Fresnel hotspot + broad highlight)
//   - Cloud shadow FBM noise (CloudWorks by Brian Tu)

#ifndef PBR_COMMON_INCLUDED
#define PBR_COMMON_INCLUDED

// ---- Fresnel (Schlick) ----
float3 F_Schlick(float cosTheta, float3 F0)
{
    float fresnel = pow(1.0 - cosTheta, 5.0);
    return F0 + (1.0 - F0) * fresnel;
}

float SchlickFresnelScalar(float cosTheta, float f0)
{
    return f0 + (1.0 - f0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// ---- GGX Normal Distribution ----
float D_GGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * d * d);
}

// ---- Smith Correlated Visibility ----
float V_SmithCorrelated(float NdotV, float NdotL, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float GGXL = NdotL * NdotL * (1.0 - a2) + a2;
    float GGXV = NdotV * NdotV * (1.0 - a2) + a2;
    return 0.5 / (sqrt(GGXV) * sqrt(GGXL) + 1e-5);
}

// ---- Sphere Environment Mapping (no pole pinching) ----
float2 SphereEnvMapUV(float3 normal, float3 viewDir)
{
    float m = 2.0 * sqrt(dot(normal.xy, normal.xy) + (normal.z + 1.0) * (normal.z + 1.0));
    float2 envUV = normal.xy / m + 0.5;
    envUV += viewDir.xy * 0.04;
    return envUV;
}

// ---- Sun Contribution (sunspot + broad + Fresnel hotspot) ----
// Returns sun color contribution; NdotL passed in to avoid recomputation
float3 ComputeSunContribution(float3 N, float3 V, float3 L, float3 F0, float NdotL)
{
    float3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float3 reflVec = reflect(-V, N);
    float reflDot = max(dot(reflVec, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    float sunSpot = pow(NdotH, 128.0) * 1.5;
    float sunBroad = pow(reflDot, 16.0) * 0.3;
    float fresnelHotspot = F_Schlick(NdotV, F0).r * NdotL * 0.2;

    float3 sunColor = float3(1.0, 0.95, 0.9);
    return sunColor * (sunSpot + sunBroad + fresnelHotspot);
}

// ---- Cloud shadow noise (CloudWorks by Brian Tu, CC BY-NC-SA 3.0) ----
static float PBR_cnoiseSeed = 1618.03398875;
float PBR_chash(float n) { return frac(sin(n / 1873.1873) * PBR_cnoiseSeed); }
float PBR_cnoise3d(float3 p) {
    float3 fr = floor(p); float3 ft = frac(p);
    float n = 1153.0 * fr.x + 2381.0 * fr.y + fr.z;
    float v  = lerp(PBR_chash(n), PBR_chash(n + 1.0), ft.z);
    float vr = lerp(PBR_chash(n + 1153.0), PBR_chash(n + 1154.0), ft.z);
    float vd = lerp(PBR_chash(n + 2381.0), PBR_chash(n + 2382.0), ft.z);
    float vo = lerp(PBR_chash(n + 3534.0), PBR_chash(n + 3535.0), ft.z);
    return lerp(lerp(v, vr, ft.x), lerp(vd, vo, ft.x), ft.y);
}
float PBR_cloudFBM(float3 p) {
    float f = 0.0;
    f += 0.5000 * PBR_cnoise3d(p); p *= 2.01;
    f += 0.2500 * PBR_cnoise3d(p); p *= 2.02;
    f += 0.1250 * PBR_cnoise3d(p); p *= 2.03;
    f += 0.0625 * PBR_cnoise3d(p);
    return f;
}

#endif

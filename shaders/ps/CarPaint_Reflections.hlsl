// Car Paint Shader - GTA V Style + Enhancements (ps_3_0)
// Based on RAGE engine vehicle_common.fxh two-layer specular system
// Enhanced with: Perlin flake noise, screen-space reflections, paint ramp
//
// Layers (from GTA V):
//   1. Base color with material color
//   2. First specular (tight, exponent ~180)
//   3. Second specular (wide, exponent ~15-40, no fresnel)
//   4. Environment reflection (dual-paraboloid or spherical)
//   5. Dirt overlay (optional)
//
// Constants:
//   c0 = (specularFalloff, specularIntensity, specular2Falloff, specular2Intensity)
//   c1 = (baseColor.r, baseColor.g, baseColor.b, metallic)
//   c2 = (flakeScale, flakeIntensity, envIntensity, paintType)
//   c3 = (envColor.r, envColor.g, envColor.b, dirtAmount)
//   c4 = (screenW, screenH, 1/screenW, 1/screenH)
//
// Textures:
//   s0 = scene color (for SSR)
//   s1 = environment map (dynamic paraboloid or spherical)
//   s2 = specular map (R=intensity, G=falloff)
//   s3 = dirt texture

#include "../include/PBR_Common.hlsl"

sampler2D sceneTex   : register(s0);
sampler2D envMapTex  : register(s1);
sampler2D specMapTex : register(s2);
sampler2D dirtTex    : register(s3);

uniform float4 specParams  : register(c0);
uniform float4 baseColor   : register(c1);
uniform float4 paintParams : register(c2);
uniform float4 envParams   : register(c3);
uniform float4 screenSize  : register(c4);

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float3 viewDir  : TEXCOORD2;
    float3 lightDir : TEXCOORD3;
};

// Perlin noise for metallic flake sparkle
float2 hash22(float2 p)
{
    p = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));
    return -1.0 + 2.0 * frac(sin(p) * 43758.5453123);
}

float perlinNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);

    float a = dot(hash22(i + float2(0.0, 0.0)), f - float2(0.0, 0.0));
    float b = dot(hash22(i + float2(1.0, 0.0)), f - float2(1.0, 0.0));
    float c = dot(hash22(i + float2(0.0, 1.0)), f - float2(0.0, 1.0));
    float d = dot(hash22(i + float2(1.0, 1.0)), f - float2(1.0, 1.0));

    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float metallicFlake(float2 p, float scale)
{
    float n1 = perlinNoise(p * scale);
    float n2 = perlinNoise(p * scale * 2.3 + 17.5);
    float n3 = perlinNoise(p * scale * 4.7 + 33.2);
    float sparkle = n1 * 0.5 + n2 * 0.3 + n3 * 0.2;
    return smoothstep(0.3, 0.7, sparkle);
}

float4 main(PS_INPUT IN) : COLOR
{
    float2 tex = IN.texCoord;

    float3 N = normalize(IN.normal);
    float3 V = normalize(IN.viewDir);
    float3 L = normalize(IN.lightDir);
    float3 H = normalize(V + L);

    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float4 specMap = tex2D(specMapTex, tex);

    float metallic = baseColor.w;
    float flakeScale = paintParams.x;
    float flakeIntensity = paintParams.y;
    float envIntensity = paintParams.z;
    float paintType = paintParams.w;
    float dirtAmount = envParams.w;

    float colorBrightness = dot(baseColor.rgb, float3(0.299, 0.587, 0.114));
    float autoMetallic = smoothstep(0.2, 0.6, colorBrightness);
    if(paintType > 0.5)
        autoMetallic = 1.0;
    else if(paintType < 0.5)
        autoMetallic *= 0.3;
    metallic = autoMetallic;

    // ===== Layer 1: Base Color =====
    float3 colorLayer = baseColor.rgb;

    // ===== Layer 2: Metallic Flake Layer =====
    float flake = metallicFlake(tex, flakeScale) * flakeIntensity * metallic;
    colorLayer *= (0.8 + flake * 0.2);

    // ===== First Specular (Tight, from GTA V) =====
    float rough1 = 1.0 / specParams.x;
    float spec1D = D_GGX(NdotH, rough1);
    float spec1V = V_SmithCorrelated(NdotL, NdotV, rough1);
    float3 spec1F = F_Schlick(VdotH, baseColor.rgb) * metallic;
    float3 specular1 = spec1D * spec1V * spec1F * specParams.y * NdotL;

    // ===== Second Specular (Wide, Clear Coat-like, from GTA V) =====
    float rough2 = 1.0 / specParams.z;
    float spec2D = D_GGX(NdotH, rough2);
    float spec2V = V_SmithCorrelated(NdotL, NdotV, rough2);
    float3 specular2 = spec2D * spec2V * specParams.w * NdotL;

    // ===== Environment Reflection =====
    float3 R = reflect(-V, N);
    float2 envUV = SphereEnvMapUV(R, V);
    float3 envReflection = tex2D(envMapTex, envUV).rgb;

    float2 ssrUV = tex + R.xy * 0.03;
    float3 ssrColor = tex2D(sceneTex, ssrUV).rgb;
    float ssrBlend = metallic * 0.3;
    envReflection = lerp(envReflection, ssrColor, ssrBlend);

    float3 envFresnel = F_Schlick(NdotV, baseColor.rgb);
    envReflection *= envFresnel * envIntensity;

    // ===== Sunspot + Fresnel hotspot (shared) =====
    float3 sunContrib = ComputeSunContribution(N, V, L, baseColor.rgb, NdotL);

    // ===== Dirt Layer =====
    float3 dirtColor = float3(0.2, 0.18, 0.15);
    if(dirtAmount > 0.001)
    {
        float3 dirt = tex2D(dirtTex, tex * 2.0).rgb;
        colorLayer = lerp(colorLayer, dirtColor * dirt, dirtAmount * 0.5);
    }

    // ===== Final Composition =====
    float3 finalColor = float3(0, 0, 0);
    finalColor += colorLayer * 0.15;
    finalColor += colorLayer * NdotL * (1.0 - metallic * 0.5);
    finalColor += specular1;
    finalColor += specular2;
    finalColor += envReflection * envParams.xyz;
    finalColor += sunContrib;
    finalColor = min(finalColor, float3(0.94, 0.94, 0.94));

    return float4(finalColor, 1.0);
}

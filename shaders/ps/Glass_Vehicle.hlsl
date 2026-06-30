/*===================================================================================
SkyGFX Plus - Vehicle Glass Shader (ps_3_0)
Energy-conserving glass with Fresnel reflections and era-based tinting.

Energy Conservation:
  Glass: kS = Fresnel (edges reflect more), kD = 1 - kS (center shows interior)
  Dark parts of glass always stay dark, reflections add on top via Fresnel.

Lens mode (headlights/taillights):
  Nearly fully transparent — interior texture visible through glass.
  Subtle Fresnel glow at edges for lens effect.

Layers:
  1. Interior base (diffuse * vertex color) — always visible
  2. Environment reflection — blended via Fresnel, energy-conserved
  3. Colored tint — subtle overlay
  4. Sun specular — additive highlight
===================================================================================*/

#include "../include/PBR_Common.hlsl"

sampler2D diffuseTex : register(s0);
sampler2D envMapTex  : register(s1);

float4 surfProps   : register(c0);
float4 fxParams    : register(c1);
float4 glassParams : register(c22);  // { tintR, tintG, tintB, opacity }
float4 lightParams : register(c23);  // { isLight, lightBoost, tintStrength, 0 }

struct PS_INPUT{
    float2 texcoord0 : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1;
    float3 WorldPos    : TEXCOORD2;
    float3 ViewDir     : TEXCOORD3;
    float3 SunDir      : TEXCOORD4;
    float4 color       : COLOR0;
    float4 envColor    : COLOR1;
};

float4 main(PS_INPUT IN) : COLOR
{
    float3 N = normalize(IN.WorldNormal);
    float3 V = normalize(IN.ViewDir);
    float3 L = normalize(IN.SunDir);
    float4 diff = tex2D(diffuseTex, IN.texcoord0);

    float NdotV = saturate(dot(N, V));

    // ---- Fresnel (glass: F0 = 0.04 for dielectric) ----
    float3 F0 = float3(0.04, 0.04, 0.04);
    float3 F = F_Schlick(NdotV, F0);
    float fresnel = F.r;

    // ---- Energy conservation for glass ----
    // kS = Fresnel (edges reflect more)
    // kD = 1 - kS (center shows interior)
    float3 kS = F;
    float3 kD = 1.0 - kS;

    // ---- Env map reflection (energy-conserved) ----
    float3 R = reflect(-V, N);
    float2 envUV = SphereEnvMapUV(R, V);
    float4 env = tex2D(envMapTex, envUV);
    float envIntensity = max(IN.envColor.a, 0.15) * 0.8;
    float3 envCol = env.rgb * envIntensity;

    // ---- Sun ----
    float NdotL = saturate(dot(N, L));
    float3 sunContrib = ComputeSunContribution(N, V, L, F0, NdotL);

    // ---- Glass params ----
    float3 tint = glassParams.xyz;
    float opacity = glassParams.w;
    float isLight = lightParams.x;
    float lightBoost = lightParams.y;
    float tintStrength = lightParams.z;

    // ================================================================
    // LIGHT PATH — nearly transparent, interior texture visible
    // Lens effect: very subtle reflection, texture shows through
    // ================================================================
    if(isLight > 0.5){
        float3 lightTint = glassParams.xyz;

        // Interior is fully visible (the light texture IS the interior)
        float3 interior = diff.rgb * lightBoost * lightTint;

        // Very subtle env reflection — just enough to show glass surface
        // Reduces with NdotV so face-on is pure interior, edges show reflection
        float lensReflStrength = fresnel * 0.06;
        float3 lensRefl = envCol * lensReflStrength;

        // Combine: interior base + subtle reflection overlay
        float3 glow = interior + lensRefl;

        // Alpha: mostly transparent, slight opacity at edges for lens effect
        float lensAlpha = saturate(opacity * 0.2 + fresnel * 0.15);
        return float4(glow, lensAlpha);
    }

    // ================================================================
    // GLASS PATH — dark base (44,44,44), subtle reflection
    // ================================================================

    // LAYER 1: Interior base (RGB 68,68,68 ≈ 0.267)
    float3 glassBase = float3(0.267, 0.267, 0.267);
    float3 layer1 = glassBase * IN.color.rgb;

    // LAYER 2: Environment reflection (subtle, Fresnel-blended)
    float3 layer2 = envCol * kS * 0.2;

    // COMPOSITE: dark base + subtle reflection
    float3 color = lerp(layer1, layer2, fresnel * 0.5);

    // Sun highlight (very subtle)
    color += sunContrib * 0.15;

    // LAYER 3: Colored tint
    float3 tintColor = tint * 2.0;
    float tintAlpha = opacity * tintStrength;
    color = lerp(color, color * tintColor, tintAlpha);

    // Final alpha: glass opacity, thickened at grazing angles
    float alpha = opacity + (1.0 - opacity) * (1.0 - NdotV) * 0.25;

    return float4(color, saturate(alpha));
}

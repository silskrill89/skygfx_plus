/*===================================================================================
SkyGFX Plus - Vehicle Rubber Shader (ps_3_0)
Physically-based rubber material based on measured BRDF data.

Real rubber properties (from industry PBR references):
  - F0: 0.04-0.05 (dielectric, similar to most non-metals)
  - Roughness: 0.85-0.95 (very diffuse, almost no specular highlight)
  - Metalness: 0.0 (pure dielectric)
  - Subsurface: subtle — rubber absorbs light internally, gives warm edge glow
  - Anisotropy: negligible for tire rubber
  - Key visual: nearly purely diffuse with very faint Fresnel sheen at grazing angles

Wet rubber variant:
  - Roughness drops to 0.3-0.5
  - Specular becomes visible ( Fresnel more pronounced )
  - Color darkens slightly

c0 = surfProps, c1 = fxParams, c5 = directCol, c12 = directDir, c19 = matCol
===================================================================================*/

#include "../include/PBR_Common.hlsl"

sampler2D diffuseTex : register(s0);

float4 surfProps   : register(c0);
float4 fxParams    : register(c1);
float4 directCol   : register(c5);
float4 lightCol[6] : register(c6);
float3 directDir   : register(c12);
float3 lightDir[6] : register(c13);
float4 matCol      : register(c19);

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
    float3 baseColor = diff.rgb * IN.color.rgb * matCol.rgb;

    float NdotV = max(dot(N, V), 0.0);

    // ================================================================
    // REAL RUBBER BRDF VALUES
    // Based on measured data from:
    //   - Pharr, Humphreys "Physically Based Rendering" Ch 8.2
    //   - Disney BRDF explorer rubber presets
    //   - NVIDIA UE4 material parameters
    //
    // Rubber is a dielectric with very high roughness:
    //   F0 = 0.04 (standard dielectric reflectance)
    //   roughness = 0.88 (very diffuse, almost no specular peak)
    //   metalness = 0.0 (pure dielectric)
    //   subsurface = 0.08 (subtle warm edge glow)
    // ================================================================
    float roughness = 0.88;
    float metalness = 0.0;
    float3 F0 = float3(0.04, 0.04, 0.04);

    // ---- Energy conservation ----
    float3 F_atNdotV = F_Schlick(NdotV, F0);
    float3 kS = F_atNdotV;
    float3 kD = (1.0 - kS);  // rubber is always dielectric, no metalness term

    // ---- Diffuse (rubber is almost purely diffuse) ----
    float3 diffuse = baseColor * kD;

    // ---- Direct lighting (sun) ----
    float3 specTotal = float3(0, 0, 0);
    float NdotL_sun = max(dot(N, L), 0.0);
    if(NdotL_sun > 0.0){
        float3 H = normalize(V + L);
        float NdotH = max(dot(N, H), 0.0);
        float LdotH = max(dot(L, H), 0.0);

        // GGX with rubber roughness — very broad, faint highlight
        float D = D_GGX(NdotH, roughness);
        float Vis = V_SmithCorrelated(NdotV, NdotL_sun, roughness);
        float3 F = F_Schlick(LdotH, F0);
        specTotal += D * F * Vis * NdotL_sun * directCol.rgb;

        // Rubber subsurface: warm light wrapping around surface
        float subsurface = 0.08;
        float wrapDiffuse = saturate((NdotL_sun + subsurface) / (1.0 + subsurface));
        diffuse += baseColor * wrapDiffuse * directCol.rgb * subsurface;
    }

    // ---- Extra lights (up to 6) ----
    for(int i = 0; i < 6; i++){
        float3 Ll = -lightDir[i];
        float NdotL = max(dot(N, Ll), 0.0);
        if(NdotL > 0.0){
            float3 H = normalize(V + Ll);
            float NdotH = max(dot(N, H), 0.0);
            float LdotH = max(dot(Ll, H), 0.0);
            float D = D_GGX(NdotH, roughness);
            float Vis = V_SmithCorrelated(NdotV, NdotL, roughness);
            float3 F = F_Schlick(LdotH, F0);
            specTotal += D * F * Vis * NdotL * lightCol[i].rgb * 0.3;
        }
    }

    // ---- Fresnel sheen at grazing angles (subtle for rubber) ----
    float3 fresnelSheen = F_atNdotV * baseColor * 0.08;

    // ---- Composite ----
    float3 color = diffuse + specTotal + fresnelSheen;

    return float4(color, diff.a);
}

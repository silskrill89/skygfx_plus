/*===================================================================================
SkyGFX Plus - Vehicle Glass Shader (ps_3_0)

Two material modes:
  1. LIGHT MODE (headlights/taillights):
     The lens texture is the interior — show it at full brightness.
     A glossy plastic/glass cover is drawn ON TOP via env reflection + sun specular.
     High alpha so the light clearly sits on the car surface, not sinking in.

  2. GLASS MODE (windows, windshields):
     Dark tinted glass with env reflections as the glossy surface.
     Semi-transparent so interior is visible through the glass.
     Fresnel makes edges more reflective (real glass behavior).

Layers for both:
  1. Base layer: interior texture × tint × boost (the thing behind the glass)
  2. Gloss layer: env reflection blended via Fresnel (the glass surface itself)
  3. Sun specular: additive highlight from directional light
  4. Tint overlay: colored glass tint (subtle for lights, stronger for windows)
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

    // Fresnel (F0 = 0.04 for glass/plastic dielectric)
    float3 F0 = float3(0.04, 0.04, 0.04);
    float fresnel = SchlickFresnelScalar(NdotV, 0.04);

    // Env map reflection — stable sphere map (NO viewDir offset to prevent warping)
    float3 R = reflect(-V, N);
    float m = 2.0 * sqrt(dot(R.xy, R.xy) + (R.z + 1.0) * (R.z + 1.0));
    float2 envUV = R.xy / m + 0.5;
    float4 env = tex2D(envMapTex, envUV);
    float envIntensity = max(IN.envColor.a, 0.15) * 0.8;
    float3 envCol = env.rgb * envIntensity;

    // Sun contribution
    float NdotL = saturate(dot(N, L));
    float3 sunContrib = ComputeSunContribution(N, V, L, F0, NdotL);

    // Glass params
    float3 tint = glassParams.xyz;
    float opacity = glassParams.w;
    float isLight = lightParams.x;
    float lightBoost = lightParams.y;
    float tintStrength = lightParams.z;

    // ================================================================
    // LIGHT PATH — headlight/taillight lens
    //
    // The lens texture IS the interior — show it at full brightness.
    // A glossy plastic/glass cover is layered ON TOP via env reflection.
    // High alpha ensures the light sits visibly on the car surface.
    // ================================================================
    if(isLight > 0.5){
        float3 lightTint = glassParams.xyz;

        // LAYER 1: Interior (the glowing lens/reflector texture)
        // FULLY OPAQUE — the light mesh has nothing behind it,
        // so alpha MUST be 1.0 to prevent see-through/culling bug
        float3 interior = diff.rgb * lightBoost * lightTint;

        // LAYER 2: Glossy glass cover ON TOP of the opaque lens
        // Subtle env reflection: face-on = 0%, grazing = 15%
        float3 glossLayer = envCol * fresnel * 0.15;

        // LAYER 3: Sun specular highlight on the glass cover
        float3 glossSpec = sunContrib * 0.20;

        // Composite: opaque interior + gloss overlay on top
        float3 color = interior + glossLayer + glossSpec;

        // Alpha: ALWAYS 1.0 — lens must be fully opaque, nothing behind it
        return float4(color, 1.0);
    }

    // ================================================================
    // GLASS PATH — windows, windshields
    //
    // Glass is semi-transparent alpha-blended over the car body.
    // The diffuse texture for glass meshes IS the window tint (dark/transparent).
    // We preserve this texture and ADD a subtle env reflection on top.
    // The car body always shows through at (1-alpha).
    // ================================================================

    // LAYER 1: The actual glass texture (preserves window tint from game TXD)
    // IN.color.rgb = vertex color from car body, diff.rgb = glass window texture
    float3 glassBase = diff.rgb * IN.color.rgb;

    // LAYER 2: Subtle env reflection — adds glossy glass surface on top of texture
    // Very gentle Fresnel: face-on = 5% reflection, grazing = 25% reflection
    float envStrength = lerp(0.05, 0.25, fresnel);
    float3 reflLayer = envCol * envStrength;

    // Combine: glass texture + subtle reflection (car body shows through via alpha)
    float3 color = glassBase + reflLayer;

    // LAYER 3: Very subtle sun highlight on glass surface
    color += sunContrib * 0.06;

    // LAYER 4: Colored tint overlay (subtle, adds warmth/color to glass)
    float3 tintColor = tint * 0.3;
    float tintAlpha = opacity * tintStrength * 0.15;
    color = lerp(color, color + tintColor, tintAlpha);

    // Alpha: use the material's original opacity — this controls how much
    // of the car body shows through. Glass is semi-transparent.
    // Thicker at grazing angles (Fresnel) for realistic glass edge behavior.
    float alpha = saturate(opacity * 0.7 + fresnel * 0.15);

    return float4(color, saturate(alpha));
}

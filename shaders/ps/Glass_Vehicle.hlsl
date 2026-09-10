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
float3 viewRight    : register(c25); // view matrix row 0 (world→view rotation)
float3 viewUp       : register(c26); // view matrix row 1
float3 viewFwd      : register(c27); // view matrix row 2

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
    float3 N = length(IN.WorldNormal) > 1e-6 ? IN.WorldNormal / length(IN.WorldNormal) : float3(0, 1, 0);
    float3 V = length(IN.ViewDir) > 1e-6 ? IN.ViewDir / length(IN.ViewDir) : float3(0, 0, 1);
    float3 L = length(IN.SunDir) > 1e-6 ? IN.SunDir / length(IN.SunDir) : float3(0, 0, -1);
    float4 diff = tex2D(diffuseTex, IN.texcoord0);

    float NdotV = saturate(dot(N, V));

    // Fresnel (F0 = 0.04 for glass/plastic dielectric)
    float3 F0 = float3(0.04, 0.04, 0.04);
    float fresnel = SchlickFresnelScalar(NdotV, 0.04);

    // Env map reflection — stable sphere map (view-space R for camera-rendered sphere map)
    float3 R_world = reflect(-V, N);
    float3 R_view = float3(dot(R_world, viewRight), dot(R_world, viewUp), dot(R_world, viewFwd));
    float3 V_view = float3(dot(V, viewRight), dot(V, viewUp), dot(V, viewFwd));
    float2 envUV = SphereEnvMapUV(R_view, V_view);
    float4 env = tex2D(envMapTex, envUV);
    // Env sample multiplied by the VS fresnel-weighted shininess (IN.envColor.a).
    // SINGLE attenuation factor — the old 0.18 pre-scale compounded with the
    // per-path strength below (0.05-0.20) and killed reflections entirely
    // (total 1-4%). Fresnel shaping now happens only in the per-path strengths.
    float envIntensity = max(IN.envColor.a, 0.1);
    float3 envCol = env.rgb * envIntensity;

    // Sun contribution
    float NdotL = saturate(dot(N, L));
    float3 sunContrib = ComputeSunContribution(N, V, L, F0, NdotL);

    // Energy conservation — reduce diffuse by specular reflectance
    // Glass F0=0.04, so ~4% of energy is reflected specularly
    float3 diffConservation = EnergyConservation(F0);

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

        // LAYER 2: Glossy cover glass ON TOP of the opaque lens.
        // Tinted by the lens colour — untinted white env/sun gloss washed the
        // lens (and the whole light) toward white; the cover must stay in hue.
        float3 glossLayer = envCol * lightTint * fresnel * 0.15;

        // LAYER 3: Sun specular highlight on the cover glass (lens-tinted too)
        float3 glossSpec = sunContrib * lightTint * 0.20;

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
    float3 glassBase = diff.rgb * IN.color.rgb * diffConservation;

    // LAYER 2: Env reflection — glossy glass surface on top of texture
    // Fresnel-driven: face-on ≈ 15% reflection, grazing ≈ 45% reflection.
    // This is the ONLY attenuation on the env sample (see envCol above).
    float envStrength = lerp(0.15, 0.45, fresnel);
    float3 reflLayer = envCol * envStrength;

    // Combine: glass texture + subtle reflection (car body shows through via alpha)
    float3 color = glassBase + reflLayer;

    // LAYER 3: Very subtle sun highlight on glass surface
    color += sunContrib * 0.04;

    // LAYER 4: Colored tint overlay (very subtle)
    float3 tintColor = tint * 0.10;
    float tintAlpha = opacity * tintStrength * 0.05;
    color = lerp(color, color + tintColor, tintAlpha);

    // Alpha: View-angle-dependent transparency.
    // At normal incidence (looking straight through): alpha ~0.3 (very transparent).
    // At grazing angles (edges of windows): alpha ~0.8 (more opaque/reflective).
    // This matches real car glass behavior — transparent from front, reflective from side.
    float alpha = lerp(0.3, 0.8, fresnel);

    return float4(color, saturate(alpha));
}

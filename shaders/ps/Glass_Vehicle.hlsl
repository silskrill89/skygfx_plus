/*===================================================================================
SkyGFX Plus - Vehicle Glass Shader (ps_3_0)
Glass material renderer using car paint reflection techniques.
All color/tint data from skygfx core constants.

c0  = surfProps
c1  = fxParams (.z = envIntensity)
c22 = { opacity, tintR, tintG, tintB }  // C++ pre-computed per vehicle class
c23 = { isLight, lightBoost, unused, unused }

Reflection model:
  - PS2 Spherical Env Mapping with view offset
  - Schlick Fresnel (IOR 1.5, F0 = 0.04)
  - VS-computed EnvColor for Fresnel-based env intensity
  - Sunspot: sharp specular highlight where sun reflection hits
  - Fresnel hotspot: Fresnel brightened where sun hits hardest
  - Edge darkening + opacity thickening
  - Light path: additive glow with tint bleed
==================================================================================*/

sampler2D diffuseTex : register(s0);
sampler2D envMapTex  : register(s1);

float4 surfProps   : register(c0);
float4 fxParams    : register(c1);
float4 glassParams : register(c22);  // { opacity, tintR, tintG, tintB }
float4 lightParams : register(c23);  // { isLight, lightBoost, 0, 0 }

struct PS_INPUT
{
    float2 texcoord0 : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1;
    float3 WorldPos    : TEXCOORD2;
    float3 ViewDir     : TEXCOORD3;
    float3 SunDir      : TEXCOORD4;
    float4 color       : COLOR0;
    float4 envColor    : COLOR1;
};

// Schlick Fresnel — glass IOR 1.5 → F0 = 0.04
float SchlickFresnel(float cosTheta)
{
    float f0 = 0.04;
    float pow5 = pow(saturate(1.0 - cosTheta), 5.0);
    return f0 + (1.0 - f0) * pow5;
}

// Proper Spherical Environment Mapping (UV-based, no pole pinching)
// Uses the world normal projected onto a sphere, like the original PS2 approach,
// but with the correct denominator: m = 2*sqrt(nx² + ny² + (nz+1)²)
// The sqrt prevents pole collapse because nx²+ny² keeps denominator
// nonzero even when nz approaches -1 (back-facing normal)
float2 SphereEnvMapUV(float3 normal, float3 viewDir)
{
    float m = 2.0 * sqrt(dot(normal.xy, normal.xy) + (normal.z + 1.0) * (normal.z + 1.0));
    float2 envUV = normal.xy / m + 0.5;
    // Subtle view-dependent offset for parallax feel
    envUV += viewDir.xy * 0.04;
    return envUV;
}

float4 main(PS_INPUT IN) : COLOR
{
    float3 N = normalize(IN.WorldNormal);
    float3 V = normalize(IN.ViewDir);
    float3 L = normalize(IN.SunDir);
    float4 diff = tex2D(diffuseTex, IN.texcoord0);

    // ---- Proper NdotV from actual view direction ----
    float NdotV = saturate(dot(N, V));

    // ---- Fresnel (IOR 1.5) ----
    float fresnel = SchlickFresnel(NdotV);

    // ---- Env map (PS2 spherical, from car paint) ----
    float3 reflVec = reflect(-V, N);
    float2 envUV = SphereEnvMapUV(N, V);
    float4 env = tex2D(envMapTex, envUV);

    // Env intensity from VS (Fresnel-based, uses shininess)
    float envIntensity = IN.envColor.a;
    float3 envCol = env.rgb * envIntensity;

    // ---- Sun lighting ----
    float NdotL = saturate(dot(N, L));

    // Sunspot: sharp specular where reflected view aligns with sun
    float3 H = normalize(V + L);
    float NdotH = saturate(dot(N, H));
    float sunSpot = pow(NdotH, 128.0) * 2.0;
    // Also add a broader highlight based on reflection alignment
    float reflDot = saturate(dot(reflVec, L));
    float sunBroad = pow(reflDot, 16.0) * 0.5;

    // Fresnel hotspot: Fresnel boosted where sun hits the surface
    float fresnelHotspot = fresnel * NdotL * 0.4;

    float3 sunColor = float3(1.0, 0.95, 0.9);
    float3 sunContrib = sunColor * (sunSpot + sunBroad + fresnelHotspot);

    float opacity = glassParams.x;
    float isLight = lightParams.x;
    float lightBoost = lightParams.y;

    if(isLight > 0.5){
        // ---- LIGHT PATH ----
        float3 glow = diff.rgb * lightBoost;
        // Tint bleed from C++ tint
        float3 tint = glassParams.rgb;
        float hasTint = dot(tint, tint) > 0.001 ? 1.0 : 0.0;
        glow *= lerp(float3(1,1,1), tint, hasTint * 0.4);
        // Edge reflection + sun highlight on lens
        glow += envCol * fresnel * 0.3;
        glow += sunContrib * 0.5;
        return float4(glow, saturate(opacity * 1.5));
    }

    // ---- GLASS PATH ----

    // Diffuse texture seen through glass (darkened by Fresnel)
    float3 base = diff.rgb * (1.0 - fresnel * 0.5);

    // Env reflection scaled by VS Fresnel intensity + material Fresnel
    float3 refl = envCol * fresnel;

    // Composite: glass body + reflection
    float3 color = lerp(base, refl, fresnel);

    // Add sun contribution
    color += sunContrib;

    // Apply C++ tint (raw color from vehicle class)
    float3 tint = glassParams.rgb;
    float hasTint = dot(tint, tint) > 0.001 ? 1.0 : 0.0;
    color = lerp(color, tint, hasTint * 0.7);

    // Edge darkening (car paint technique)
    color *= 1.0 - fresnel * 0.3;

    // Opacity thickening at grazing angles
    float edgeAlpha = opacity + (1.0 - opacity) * (1.0 - NdotV) * 0.25;

    return float4(color, saturate(edgeAlpha));
}

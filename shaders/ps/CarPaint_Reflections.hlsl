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
// Enhancements over GTA V:
//   - Perlin noise metallic flakes (Need for Speed style)
//   - Screen-space reflection blend
//   - Paint ramp for view-dependent color shifting
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

sampler2D sceneTex   : register(s0);
sampler2D envMapTex  : register(s1);
sampler2D specMapTex : register(s2);
sampler2D dirtTex    : register(s3);

uniform float4 specParams  : register(c0); // x=specFalloff, y=specIntensity, z=spec2Falloff, w=spec2Intensity
uniform float4 baseColor   : register(c1); // xyz=paint color, w=metallic
uniform float4 paintParams : register(c2); // x=flakeScale, y=flakeIntensity, z=envIntensity, w=paintType
uniform float4 envParams   : register(c3); // xyz=env tint, w=dirtAmount
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

// GGX NDF
float GGX_NDF(float NdotH, float roughness)
{
    float a2 = roughness * roughness * roughness * roughness;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159 * denom * denom);
}

// Smith Visibility
float SmithVisibility(float NdotL, float NdotV, float roughness)
{
    float r2 = roughness * roughness;
    float gv = NdotL * sqrt((-NdotV * r2 + NdotV) * NdotV + r2);
    float gl = NdotV * sqrt((-NdotL * r2 + NdotL) * NdotL + r2);
    return 0.5 / (gv + gl + 0.0001);
}

// Schlick Fresnel
float3 SchlickFresnel(float3 F0, float VdotH)
{
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
}

// PS2 Spherical Environment Mapping
float2 PS2SphericalEnvMap(float3 reflVec, float3 viewDir)
{
    float2 envUV;
    envUV.x = reflVec.x * 0.5 + 0.5;
    envUV.y = reflVec.y * 0.5 + 0.5;
    envUV.x += viewDir.x * 0.1;
    envUV.y += viewDir.y * 0.1;
    return envUV;
}

float4 main(PS_INPUT IN) : COLOR
{
    float2 tex = IN.texCoord;
    float2 pixel = screenSize.zw;
    
    // Lighting vectors
    float3 N = normalize(IN.normal);
    float3 V = normalize(IN.viewDir);
    float3 L = normalize(IN.lightDir);
    float3 H = normalize(V + L);
    
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));
    
    // Sample specular map
    float4 specMap = tex2D(specMapTex, tex);
    
    // Material parameters
    float metallic = baseColor.w;
    float flakeScale = paintParams.x;
    float flakeIntensity = paintParams.y;
    float envIntensity = paintParams.z;
    float paintType = paintParams.w; // 0=solid, 1=metallic
    float dirtAmount = envParams.w;
    
    // Determine paint type from base color brightness
    // Darker colors = solid paint, brighter colors = metallic
    float colorBrightness = dot(baseColor.rgb, float3(0.299, 0.587, 0.114));
    float autoMetallic = smoothstep(0.2, 0.6, colorBrightness);
    
    // Override with explicit paint type if set
    if(paintType > 0.5) // Metallic
        autoMetallic = 1.0;
    else if(paintType < 0.5) // Solid
        autoMetallic *= 0.3;
    
    metallic = autoMetallic;
    
    // ===== Layer 1: Base Color =====
    float3 baseCol = baseColor.rgb;
    
    // ===== Layer 2: Metallic Flake Layer =====
    float flake = metallicFlake(tex, flakeScale);
    flake *= flakeIntensity * metallic; // Only metallic paint has visible flakes
    
    float3 colorLayer = baseCol * (0.8 + flake * 0.2);
    
    // ===== First Specular (Tight, from GTA V) =====
    float specFalloff = specParams.x; // ~180
    float specIntensity = specParams.y; // ~0.15
    float spec1D = GGX_NDF(NdotH, 1.0 / specFalloff);
    float spec1V = SmithVisibility(NdotL, NdotV, 1.0 / specFalloff);
    float3 spec1F = SchlickFresnel(baseCol, VdotH) * metallic;
    float3 specular1 = spec1D * spec1V * spec1F * specIntensity * NdotL;
    
    // ===== Second Specular (Wide, Clear Coat-like, from GTA V) =====
    float spec2Falloff = specParams.z; // ~15-40
    float spec2Intensity = specParams.w; // ~0.75-1.7
    float spec2D = GGX_NDF(NdotH, 1.0 / spec2Falloff);
    float spec2V = SmithVisibility(NdotL, NdotV, 1.0 / spec2Falloff);
    // Second specular has NO fresnel (like GTA V's SECOND_SPECULAR_LAYER_NOFRESNEL)
    float3 specular2 = spec2D * spec2V * float3(1, 1, 1) * spec2Intensity * NdotL;
    
    // ===== Environment Reflection =====
    float3 reflVec = reflect(-V, N);
    float2 envUV = PS2SphericalEnvMap(reflVec, V);
    float3 envReflection = tex2D(envMapTex, envUV).rgb;
    
    // Screen-space reflection blend
    float2 ssrUV = tex + reflVec.xy * 0.03;
    float3 ssrColor = tex2D(sceneTex, ssrUV).rgb;
    
    // Blend SSR with env map (more SSR for smoother surfaces)
    float ssrBlend = metallic * 0.3;
    envReflection = lerp(envReflection, ssrColor, ssrBlend);
    
    // Scale by Fresnel and intensity
    float3 envFresnel = SchlickFresnel(baseCol, NdotV);
    envReflection *= envFresnel * envIntensity;
    
    // ===== Dirt Layer (optional) =====
    float3 dirtColor = float3(0.2, 0.18, 0.15);
    if(dirtAmount > 0.001)
    {
        float3 dirt = tex2D(dirtTex, tex * 2.0).rgb;
        colorLayer = lerp(colorLayer, dirtColor * dirt, dirtAmount * 0.5);
    }
    
    // ===== Final Composition =====
    float3 finalColor = float3(0, 0, 0);
    
    // Ambient
    finalColor += colorLayer * 0.15;
    
    // Diffuse
    finalColor += colorLayer * NdotL * (1.0 - metallic * 0.5);
    
    // Specular layers
    finalColor += specular1;
    finalColor += specular2;
    
    // Environment reflection
    finalColor += envReflection * envParams.xyz;
    
    // Clamp to prevent bloom (GTA V technique)
    finalColor = min(finalColor, float3(0.94, 0.94, 0.94));
    
    return float4(finalColor, 1.0);
}

// Glass Shader - Second Pass Layer (ps_3_0)
// For vehicle windows, light lenses, and transparent surfaces
// Requires first pass from somewhere else (renders on top)
//
// Material Classification (from GTA SA surface properties):
//   - Breakable windows: high alpha, moderate reflectivity
//   - Light lenses: colored glass, emissive
//   - Windshield: slight tint, high reflectivity at grazing angles
//
// Constants:
//   c0 = (glassType, reflectivity, opacity, fresnelPower)
//   c1 = (tintR, tintG, tintB, tintStrength)
//   c2 = (emissiveR, emissiveG, emissiveB, emissiveStrength)
//   c3 = (scratchAmount, dirtAmount, 0, 0)
//
// Textures:
//   s0 = scene color (from first pass)
//   s1 = glass normal map (for distortion)
//   s2 = glass diffuse (tint/color)
//   s3 = scratch/dirt overlay

sampler2D sceneTex    : register(s0);
sampler2D glassNormalTex : register(s1);
sampler2D glassDiffuseTex : register(s2);
sampler2D scratchTex  : register(s3);

uniform float4 glassParams : register(c0); // x=type, y=reflectivity, z=opacity, w=fresnel
uniform float4 tintParams  : register(c1); // xyz=tint color, w=strength
uniform float4 emissiveParams : register(c2); // xyz=emissive color, w=strength
uniform float4 damageParams : register(c3); // x=scratch, y=dirt

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float3 viewDir  : TEXCOORD2;
    float3 lightDir : TEXCOORD3;
};

float3 SchlickFresnel(float3 F0, float VdotH)
{
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
}

float4 main(PS_INPUT IN) : COLOR
{
    float2 tex = IN.texCoord;
    
    // Sample textures
    float3 sceneColor = tex2D(sceneTex, tex).rgb;
    float3 normal = tex2D(glassNormalTex, tex).rgb * 2.0 - 1.0;
    float4 glassDiffuse = tex2D(glassDiffuseTex, tex);
    
    // Lighting vectors
    float3 N = normalize(IN.normal);
    float3 V = normalize(IN.viewDir);
    float3 L = normalize(IN.lightDir);
    float3 H = normalize(V + L);
    
    float NdotV = saturate(dot(N, V));
    float NdotL = saturate(dot(N, L));
    float VdotH = saturate(dot(V, H));
    
    // === Glass Type ===
    // 0 = breakable window, 1 = light lens, 2 = windshield
    float glassType = glassParams.x;
    
    // === Fresnel Reflection ===
    float fresnelPower = glassParams.w;
    float reflectivity = glassParams.y;
    
    float3 F0 = float3(0.04, 0.04, 0.04); // Glass IOR ~1.5
    float3 fresnel = SchlickFresnel(F0, NdotV);
    fresnel = pow(fresnel, float3(fresnelPower, fresnelPower, fresnelPower));
    
    // === Refraction (slight distortion) ===
    float2 refractionOffset = normal.xy * 0.02;
    float2 refractedUV = tex + refractionOffset;
    float3 refractedColor = tex2D(sceneTex, refractedUV).rgb;
    
    // === Tint ===
    float3 tint = tintParams.rgb;
    float tintStrength = tintParams.w;
    float3 tintedColor = refractedColor * lerp(float3(1, 1, 1), tint, tintStrength);
    
    // === Reflection ===
    float3 reflVec = reflect(-V, N);
    float2 envUV = float2(
        atan2(reflVec.x, reflVec.z) / (2.0 * 3.14159) + 0.5,
        asin(reflVec.y) / 3.14159 + 0.5
    );
    // Use scene color as simplified reflection
    float3 reflectionColor = tex2D(sceneTex, envUV).rgb * 0.5;
    
    // === Specular Highlight ===
    float NdotH = saturate(dot(N, H));
    float specular = pow(NdotH, 64.0) * 2.0; // Sharp specular for glass
    
    // === Emissive (for light lenses) ===
    float3 emissive = emissiveParams.rgb * emissiveParams.w;
    
    // === Scratch/Dirt Overlay ===
    float scratchAmount = damageParams.x;
    float dirtAmount = damageParams.y;
    
    float3 scratchColor = float3(1, 1, 1);
    float3 dirtColor = float3(0.2, 0.18, 0.15);
    
    if(scratchAmount > 0.001)
    {
        float3 scratch = tex2D(scratchTex, tex * 3.0).rgb;
        tintedColor = lerp(tintedColor, scratch, scratchAmount * scratch.r);
    }
    
    if(dirtAmount > 0.001)
    {
        float3 dirt = tex2D(scratchTex, tex * 2.0).rgb;
        tintedColor = lerp(tintedColor, dirtColor, dirtAmount * dirt.r);
    }
    
    // === Final Composition ===
    float opacity = glassParams.z;
    
    // Blend refraction and reflection based on Fresnel
    float3 finalColor = lerp(tintedColor, reflectionColor, fresnel * reflectivity);
    
    // Add specular highlight
    finalColor += float3(1, 1, 1) * specular;
    
    // Add emissive for light lenses
    finalColor += emissive;
    
    // Apply glass type adjustments
    // Breakable windows: more opaque, less reflection
    if(glassType < 0.5)
    {
        opacity = max(opacity, 0.7);
        reflectivity *= 0.5;
    }
    // Light lenses: add colored emissive
    else if(glassType < 1.5)
    {
        finalColor += emissive * 0.5;
    }
    // Windshield: more reflective at grazing angles
    else
    {
        reflectivity *= 1.5;
    }
    
    return float4(finalColor, opacity);
}

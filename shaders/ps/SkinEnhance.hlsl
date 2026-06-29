// Skin Enhancement Shader (ps_3_0)
// Works ON TOP of existing Rpskin rendering
// Adds wrap lighting for SSS approximation and improved specular
//
// Constants:
//   c0 = (wrapFactor, specularPower, specularStrength, sssStrength)
//   c1 = (skinColor.r, skinColor.g, skinColor.b, bloodColor.r)
//   c2 = (bloodColor.g, bloodColor.b, ambientWrap, 0)
//
// Textures:
//   s0 = scene color (after Rpskin rendering)
//   s1 = depth buffer
//   s2 = normal buffer (if available)

sampler2D colorTex  : register(s0);
sampler2D depthTex  : register(s1);
sampler2D normalTex : register(s2);

uniform float4 skinParams : register(c0); // x=wrap, y=specPower, z=specStr, w=sssStr
uniform float4 skinColor  : register(c1); // xyz=skin tint, w=bloodR
uniform float4 skinColor2 : register(c2); // xy=bloodGB, z=ambientWrap

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float Luma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float3 CalculateSkinSSS(float3 color, float2 tex)
{
    float3 normal = tex2D(normalTex, tex).rgb * 2.0 - 1.0;
    float depth = tex2D(depthTex, tex).r;
    
    // Calculate view-space position for lighting
    float2 ndc = tex * 2.0 - 1.0;
    ndc.y = -ndc.y;
    
    // Simple wrap lighting for SSS approximation
    // Light wraps around the surface, illuminating shadow areas
    float luma = Luma(color);
    float wrapFactor = skinParams.x;
    
    // Blood color in shadow areas (ears, nostrils, thin areas)
    float3 bloodColor = float3(skinColor.w, skinColor2.x, skinColor2.y);
    float shadowFactor = saturate(1.0 - luma * 2.0); // Dark areas get more SSS
    
    // Subsurface scattering approximation
    float3 sssColor = lerp(color, bloodColor, shadowFactor * skinParams.w);
    
    // Wrap ambient light
    float ambientWrap = skinColor2.z;
    float3 wrappedAmbient = color * ambientWrap;
    
    // Combine: SSS in shadows + wrap ambient + original color
    return lerp(color, sssColor + wrappedAmbient, skinParams.w);
}

float4 main(PS_INPUT IN) : COLOR
{
    float2 tex = IN.texCoord;
    float3 color = tex2D(colorTex, tex).rgb;
    
    // Apply skin SSS
    float3 result = CalculateSkinSSS(color, tex);
    
    // Add subtle specular highlight (Blinn-Phong approximation)
    // This works on top of existing Rpskin specular
    float luma = Luma(result);
    float specMask = smoothstep(0.5, 1.0, luma); // Only on bright areas
    float specular = pow(luma, skinParams.y) * skinParams.z * specMask;
    result += float3(1.0, 0.95, 0.9) * specular; // Warm specular tint
    
    // Apply skin color tint
    result *= float3(skinColor.x, skinColor.y, skinColor.z);
    
    return float4(result, 1.0);
}

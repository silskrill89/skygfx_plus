// Hair Enhancement Shader (ps_3_0)
// Adds anisotropic highlights and SSS to hair rendering
// Uses Kajiya-Kay shading model for realistic hair
//
// Constants:
//   c0 = (anisotropicPower, anisotropicStrength, sssStrength, hairLength)
//   c1 = (hairColor.r, hairColor.g, hairColor.b, highlightShift)
//   c2 = (specularColor.r, specularColor.g, specularColor.b, 0)
//
// Textures:
//   s0 = scene color (after hair rendering)
//   s1 = depth buffer
//   s2 = normal buffer (for tangent estimation)

sampler2D colorTex  : register(s0);
sampler2D depthTex  : register(s1);
sampler2D normalTex : register(s2);

uniform float4 hairParams : register(c0); // x=anisoPower, y=anisoStr, z=sssStr, w=length
uniform float4 hairColor  : register(c1); // xyz=hair color, w=highlight shift
uniform float4 specColor  : register(c2); // xyz=specular color

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float Luma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// Kajiya-Kay anisotropic highlight
// Estimates tangent from depth buffer derivatives
float3 AnisotropicHighlight(float2 tex, float3 color)
{
    float depth = tex2D(depthTex, tex).r;
    float3 normal = tex2D(normalTex, tex).rgb * 2.0 - 1.0;
    
    // Estimate tangent from screen-space derivatives of depth
    float depthDX = ddx(depth);
    float depthDY = ddy(depth);
    
    // Tangent is perpendicular to depth gradient
    float3 tangent = normalize(float3(depthDY, -depthDX, 0.0));
    
    // Kajiya-Kay: diffuse = sqrt(1 - (T.L)^2)
    // For hair, we use the tangent direction for highlights
    float TdotL = dot(tangent, float3(0.3, 0.3, 1.0)); // Approximate light direction
    float sinTL = sqrt(saturate(1.0 - TdotL * TdotL));
    
    // Anisotropic specular with shift
    float shift = hairColor.w;
    float TdotV = dot(tangent, float3(0.0, 0.0, 1.0)); // Approximate view direction
    float sinTV = sqrt(saturate(1.0 - TdotV * TdotV));
    
    // Two specular lobes (primary and secondary highlight)
    float spec1 = pow(sinTL * sinTV, hairParams.x);
    float spec2 = pow(sinTL * sinTV, hairParams.x * 0.5); // Wider lobe
    
    float3 specular = specColor.rgb * (spec1 + spec2 * 0.5) * hairParams.y;
    
    return specular;
}

float3 CalculateHairSSS(float3 color, float2 tex)
{
    float depth = tex2D(depthTex, tex).r;
    
    // Simple SSS for hair: light wraps through thin hair strands
    float luma = Luma(color);
    float3 sssColor = color * 1.2; // Brighten for SSS effect
    
    // Add warm tint in shadow areas (light passing through hair)
    float shadowFactor = saturate(1.0 - luma * 2.0);
    float3 warmTint = float3(1.1, 0.9, 0.8);
    sssColor = lerp(sssColor, sssColor * warmTint, shadowFactor * hairParams.z);
    
    return sssColor;
}

float4 main(PS_INPUT IN) : COLOR
{
    float2 tex = IN.texCoord;
    float3 color = tex2D(colorTex, tex).rgb;
    
    // Apply anisotropic highlights
    float3 specular = AnisotropicHighlight(tex, color);
    
    // Apply hair SSS
    float3 sssColor = CalculateHairSSS(color, tex);
    
    // Combine: original + SSS + specular
    float3 result = lerp(color, sssColor, hairParams.z);
    result += specular;
    
    // Apply hair color tint
    result *= float3(hairColor.x, hairColor.y, hairColor.z);
    
    return float4(result, 1.0);
}

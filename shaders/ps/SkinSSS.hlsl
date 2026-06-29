// Skin Shader with Subsurface Scattering (ps_3_0)
// Simulates light transport through skin for realistic rendering
//
// Constants:
//   c0 = (sssStrength, sssRadius, specularPower, specularStrength)
//   c1 = (skinColor.r, skinColor.g, skinColor.b, bloodColor.r)
//   c2 = (bloodColor.g, bloodColor.b, ambientWrap, diffuseWrap)
//
// Textures:
//   s0 = diffuse texture (skin albedo)
//   s1 = normal map (skin detail)

sampler2D diffuseTex : register(s0);
sampler2D normalTex  : register(s1);

uniform float4 sssParams    : register(c0); // x=strength, y=radius, z=specPower, w=specStrength
uniform float4 skinColor    : register(c1); // xyz=skin tint, w=bloodColor.r
uniform float4 sssColor     : register(c2); // xy=bloodColor.gb, z=ambientWrap, w=diffuseWrap

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
    float3 lightDir : TEXCOORD1;
    float3 viewDir  : TEXCOORD2;
    float3 normal   : TEXCOORD3;
    float3 tangent  : TEXCOORD4;
};

float3 CalculateSSS(float3 normal, float3 lightDir, float3 viewDir, float3 diffuse)
{
    float NdotL = dot(normal, lightDir);
    float NdotV = dot(normal, viewDir);
    
    // Wrap lighting for SSS approximation
    // This simulates light wrapping around the surface
    float wrapFactor = sssColor.w; // diffuseWrap
    float wrappedNdotL = saturate((NdotL + wrapFactor) / (1.0 + wrapFactor));
    
    // Subsurface scattering approximation
    // Light that enters the skin and exits at a different point
    float sssIntensity = sssParams.x;
    float sssRadius = sssParams.y;
    
    // Translucency effect - light passing through thin areas
    float translucency = saturate(1.0 - NdotV) * sssIntensity;
    
    // Blood color in shadow areas (ears, nostrils, etc.)
    float3 bloodColor = float3(skinColor.w, sssColor.x, sssColor.y);
    float shadowFactor = saturate(1.0 - NdotL);
    float3 sssResult = lerp(diffuse, bloodColor, shadowFactor * translucency);
    
    // Wrap ambient light
    float ambientWrap = sssColor.z;
    float3 ambient = diffuse * ambientWrap;
    
    return sssResult * wrappedNdotL + ambient;
}

float4 main(PS_INPUT IN) : COLOR
{
    // Sample textures
    float4 diffuse = tex2D(diffuseTex, IN.texCoord);
    float3 normal = normalize(IN.normal);
    float3 lightDir = normalize(IN.lightDir);
    float3 viewDir = normalize(IN.viewDir);
    
    // Unpack normal map
    float3 normalMap = tex2D(normalTex, IN.texCoord).rgb * 2.0 - 1.0;
    float3 tangent = normalize(IN.tangent);
    float3 binormal = cross(normal, tangent);
    normal = normalize(normal + normalMap.x * tangent + normalMap.y * binormal);
    
    // Calculate SSS lighting
    float3 sssLighting = CalculateSSS(normal, lightDir, viewDir, diffuse.rgb);
    
    // Specular highlight (Blinn-Phong with skin-appropriate roughness)
    float3 halfDir = normalize(lightDir + viewDir);
    float NdotH = saturate(dot(normal, halfDir));
    float specular = pow(NdotH, sssParams.z) * sssParams.w;
    
    // Skin specular has a soft, wide lobe
    float3 specularColor = float3(1.0, 0.95, 0.9) * specular;
    
    // Combine
    float3 finalColor = sssLighting + specularColor;
    finalColor *= skinColor.rgb; // Apply skin tint
    
    return float4(finalColor, diffuse.a);
}

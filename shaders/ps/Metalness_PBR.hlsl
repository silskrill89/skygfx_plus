// Metalness PBR Shader (ps_3_0)
// Base PBR BRDF for metal objects using Crytek GGX specular
// Used as foundation for vehicle paint, chrome, etc.
//
// Constants:
//   c0 = (metalness, roughness, reflectance, aoStrength)
//   c1 = (baseColor.r, baseColor.g, baseColor.b, 0)
//   c2 = (specularColor.r, specularColor.g, specularColor.b, 0)
//   c3 = (emissive.r, emissive.g, emissive.b, emissiveStrength)
//
// Textures:
//   s0 = albedo/diffuse texture
//   s1 = normal map
//   s2 = metallic/roughness map (R=metallic, G=roughness, B=AO)
//   s3 = environment map (cubemap or spherical)

sampler2D albedoTex      : register(s0);
sampler2D normalTex      : register(s1);
sampler2D metallicRoughTex : register(s2);
sampler2D envMapTex      : register(s3);

uniform float4 pbrParams     : register(c0); // x=metalness, y=roughness, z=reflectance, w=ao
uniform float4 baseColor     : register(c1); // xyz=base color tint
uniform float4 specularColor : register(c2); // xyz=specular color (F0)
uniform float4 emissiveColor : register(c3); // xyz=emissive color, w=strength

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float3 viewDir  : TEXCOORD2;
    float3 lightDir : TEXCOORD3;
    float3 tangent  : TEXCOORD4;
};

// GGX Normal Distribution Function (from CryEngine shadeLib.cfi)
float GGX_NDF(float NdotH, float roughness)
{
    float a2 = roughness * roughness * roughness * roughness;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159 * denom * denom);
}

// Smith Visibility Term (from CryEngine shadeLib.cfi)
float SmithVisibility(float NdotL, float NdotV, float roughness)
{
    float r2 = roughness * roughness;
    float gv = NdotL * sqrt((-NdotV * r2 + NdotV) * NdotV + r2);
    float gl = NdotV * sqrt((-NdotL * r2 + NdotL) * NdotL + r2);
    return 0.5 / (gv + gl + 0.0001);
}

// Schlick Fresnel (from CryEngine shadeLib.cfi)
float3 SchlickFresnel(float3 F0, float VdotH)
{
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
}

// Burley Diffuse BRDF (from CryEngine shadeLib.cfi)
float BurleyDiffuse(float NdotL, float NdotV, float VdotH, float roughness)
{
    float energyBias = 0.5 * roughness;
    float energyFactor = lerp(1.0, 1.0 / 1.51, roughness);
    float fd90 = energyBias + 2.0 * VdotH * VdotH * roughness;
    float scatterL = lerp(1.0, fd90, pow(1.0 - NdotL, 5.0));
    float scatterV = lerp(1.0, fd90, pow(1.0 - NdotV, 5.0));
    return scatterL * scatterV * energyFactor;
}

float4 main(PS_INPUT IN) : COLOR
{
    float2 tex = IN.texCoord;
    
    // Sample textures
    float4 albedo = tex2D(albedoTex, tex);
    float4 metallicRough = tex2D(metallicRoughTex, tex);
    
    // Get material properties
    float metalness = metallicRough.r * pbrParams.x;
    float roughness = metallicRough.g * pbrParams.y;
    float ao = metallicRough.b * pbrParams.w;
    
    // Unpack normal map
    float3 normal = normalize(IN.normal);
    float3 normalMap = tex2D(normalTex, tex).rgb * 2.0 - 1.0;
    float3 tangent = normalize(IN.tangent);
    float3 binormal = cross(normal, tangent);
    normal = normalize(normal + normalMap.x * tangent + normalMap.y * binormal);
    
    // Lighting vectors
    float3 N = normalize(normal);
    float3 V = normalize(IN.viewDir);
    float3 L = normalize(IN.lightDir);
    float3 H = normalize(V + L);
    
    // Dot products
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));
    float LdotH = saturate(dot(L, H));
    
    // Base color with tint
    float3 baseColorFinal = albedo.rgb * baseColor.rgb;
    
    // F0 (reflectance at normal incidence)
    // Metals: F0 = base color (tinted reflection)
    // Non-metals: F0 = reflectance (usually 0.04 for dielectrics)
    float3 F0 = lerp(float3(pbrParams.z, pbrParams.z, pbrParams.z), baseColorFinal, metalness);
    
    // GGX Specular BRDF (CryEngine implementation)
    float D = GGX_NDF(NdotH, roughness);
    float V_term = SmithVisibility(NdotL, NdotV, roughness);
    float3 F = SchlickFresnel(F0, VdotH);
    
    float3 specular = D * V_term * F;
    
    // Diffuse BRDF (Burley/Disney)
    // Metals have no diffuse, only specular
    float3 diffuse = baseColorFinal * BurleyDiffuse(NdotL, NdotV, VdotH, roughness);
    diffuse *= (1.0 - metalness); // No diffuse for metals
    
    // Environment reflection
    float3 reflVec = reflect(-V, N);
    float2 envUV = float2(
        atan2(reflVec.x, reflVec.z) / (2.0 * 3.14159) + 0.5,
        asin(reflVec.y) / 3.14159 + 0.5
    );
    float3 envColor = tex2D(envMapTex, envUV).rgb;
    
    // Fresnel for environment reflection
    float3 envFresnel = SchlickFresnel(F0, NdotV);
    float3 envReflection = envColor * envFresnel;
    
    // Ambient occlusion
    float3 ambient = baseColorFinal * ao * 0.3;
    
    // Final composition
    float3 finalColor = ambient;
    finalColor += (diffuse + specular) * NdotL;
    finalColor += envReflection * (1.0 - roughness); // Rough metals reflect less
    
    // Add emissive
    finalColor += emissiveColor.rgb * emissiveColor.w;
    
    return float4(finalColor, albedo.a);
}

// Generic PBR Gloss Shader (ps_3_0)
// Universal material shader using gloss-based PBR
// Used by vehicle and building pipes for all shiny materials
// Highest gloss = mirror-like, lowest = matte
//
// Material Classification:
//   gloss > 0.8 = chrome/mirror (highly reflective)
//   gloss > 0.5 = metal (reflective)
//   gloss > 0.2 = paint (semi-reflective)
//   gloss < 0.2 = rubber/plastic (matte)
//
// Constants:
//   c0 = (gloss, metallic, roughness, reflectance)
//   c1 = (baseColor.r, baseColor.g, baseColor.b, clearCoat)
//   c2 = (specularColor.r, specularColor.g, specularColor.b, 0)
//   c3 = (envColor.r, envColor.g, envColor.b, envIntensity)
//
// Textures:
//   s0 = diffuse texture
//   s1 = environment map
//   s2 = specular/gloss map

sampler2D diffuseTex : register(s0);
sampler2D envMapTex  : register(s1);
sampler2D specMapTex : register(s2);

uniform float4 pbrParams    : register(c0); // x=gloss, y=metallic, z=roughness, w=reflectance
uniform float4 baseColor    : register(c1); // xyz=base color, w=clearCoat
uniform float4 specularColor : register(c2); // xyz=specular tint
uniform float4 envParams    : register(c3); // xyz=env tint, w=intensity

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float3 viewDir  : TEXCOORD2;
    float3 lightDir : TEXCOORD3;
};

// GGX Normal Distribution Function
float GGX_NDF(float NdotH, float roughness)
{
    float a2 = roughness * roughness * roughness * roughness;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159 * denom * denom);
}

// Smith Visibility Term
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
float2 SphericalEnvMap(float3 reflVec, float3 viewDir)
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
    
    // Sample textures
    float4 diffuse = tex2D(diffuseTex, tex);
    float3 baseCol = diffuse.rgb * baseColor.rgb;
    
    // PBR parameters
    float gloss = pbrParams.x;       // 0=matte, 1=mirror
    float metallic = pbrParams.y;    // 0=dielectric, 1=metal
    float roughness = pbrParams.z;   // 0=smooth, 1=rough
    float reflectance = pbrParams.w; // F0 at normal incidence
    
    // Roughness from gloss (inverse relationship)
    float smoothness = 1.0 - roughness;
    float finalRoughness = roughness * (1.0 - gloss * 0.8); // Gloss reduces roughness
    
    // Lighting vectors
    float3 N = normalize(IN.normal);
    float3 V = normalize(IN.viewDir);
    float3 L = normalize(IN.lightDir);
    float3 H = normalize(V + L);
    
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));
    
    // F0 for specular
    float3 F0 = lerp(float3(reflectance, reflectance, reflectance), baseCol, metallic);
    
    // Specular BRDF (GGX)
    float D = GGX_NDF(NdotH, finalRoughness);
    float V_term = SmithVisibility(NdotL, NdotV, finalRoughness);
    float3 F = SchlickFresnel(F0, VdotH);
    float3 specular = D * V_term * F * specularColor.rgb;
    
    // Diffuse (non-metals only)
    float3 diffuseLight = baseCol * NdotL * (1.0 - metallic);
    
    // Environment reflection
    float3 reflVec = reflect(-V, N);
    float2 envUV = SphericalEnvMap(reflVec, V);
    float3 envReflection = tex2D(envMapTex, envUV).rgb;
    
    // Scale reflection by gloss and Fresnel
    float3 envFresnel = SchlickFresnel(F0, NdotV);
    envReflection *= envFresnel * gloss * envParams.w;
    
    // Clear coat layer (glossy overlay)
    float clearCoat = baseColor.w;
    float3 clearCoatF0 = float3(0.04, 0.04, 0.04);
    float3 clearCoatF = SchlickFresnel(clearCoatF0, NdotV);
    float3 clearCoatSpec = clearCoatF * clearCoat * 0.3;
    
    // Final composition
    float3 finalColor = float3(0, 0, 0);
    
    // Ambient
    finalColor += baseCol * 0.15;
    
    // Diffuse + Specular
    finalColor += diffuseLight + specular * NdotL;
    
    // Environment reflection (tinted by envParams)
    finalColor += envReflection * envParams.xyz;
    
    // Clear coat
    finalColor += clearCoatSpec;
    
    return float4(finalColor, diffuse.a);
}

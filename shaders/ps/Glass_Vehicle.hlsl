// Glass Shader (ps_3_0)
// Applies to ALL glass in the game - windows, light lenses, windshields
// Engine handles breakable windows, this shader handles the rendering
// Used by vehicle pipe as part of unified vehicle shader
//
// Material detection from GTA SA:
//   - Glass atomic flag: VEHICLE_ATOMIC_ALPHA
//   - MatFX: rpMATFXEFFECTENVMAP with alpha blending
//   - Surface props: specular > 0 for reflective glass
//
// Constants:
//   c0 = (reflectivity, opacity, fresnelPower, glassType)
//   c1 = (tintR, tintG, tintB, tintStrength)
//   c2 = (specularPower, specularIntensity, 0, 0)
//
// Textures:
//   s0 = scene color (from first pass)
//   s1 = glass normal map (for distortion)
//   s2 = glass diffuse (tint/color)

sampler2D sceneTex      : register(s0);
sampler2D glassNormalTex : register(s1);
sampler2D glassDiffuseTex : register(s2);

uniform float4 glassParams : register(c0); // x=reflectivity, y=opacity, z=fresnelPower, w=glassType
uniform float4 tintParams  : register(c1); // xyz=tint color, w=strength
uniform float4 specParams  : register(c2); // x=specPower, y=specIntensity

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
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));
    
    // Glass parameters
    float reflectivity = glassParams.x;
    float opacity = glassParams.y;
    float fresnelPower = glassParams.z;
    
    // Fresnel reflection (more reflection at grazing angles)
    float3 F0 = float3(0.04, 0.04, 0.04); // Glass IOR ~1.5
    float3 fresnel = SchlickFresnel(F0, NdotV);
    fresnel = pow(fresnel, float3(fresnelPower, fresnelPower, fresnelPower));
    
    // Refraction (slight distortion)
    float2 refractionOffset = normal.xy * 0.02;
    float2 refractedUV = tex + refractionOffset;
    float3 refractedColor = tex2D(sceneTex, refractedUV).rgb;
    
    // Apply tint
    float3 tint = tintParams.rgb;
    float tintStrength = tintParams.w;
    float3 tintedColor = refractedColor * lerp(float3(1, 1, 1), tint, tintStrength);
    
    // Specular highlight (sharp for glass)
    float specular = pow(NdotH, specParams.x) * specParams.y;
    
    // Final composition
    float3 finalColor = tintedColor;
    
    // Add Fresnel reflection
    finalColor = lerp(finalColor, sceneColor * 0.5, fresnel * reflectivity);
    
    // Add specular
    finalColor += float3(1, 1, 1) * specular;
    
    return float4(finalColor, opacity);
}

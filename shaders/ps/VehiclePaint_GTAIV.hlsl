// GTA IV/V Style Vehicle Paint Shader (ps_3_0)
// Inspired by RAGE engine vehicle_common.fxh and CryEngine shadeLib.cfi
// Features: dual specular, Fresnel reflection, paint ramp, clear coat
//
// Constants:
//   c0 = (specularPower, specularIntensity, fresnel, reflectivity)
//   c1 = (specular2Power, specular2Intensity, clearCoatStrength, dirtLevel)
//   c2 = (paintColor.r, paintColor.g, paintColor.b, paintColor.a)
//   c3 = (envColor.r, envColor.g, envColor.b, 0)
//   c4 = (dirtColor.r, dirtColor.g, dirtColor.b, dirtTiling)
//
// Textures:
//   s0 = diffuse texture (car body)
//   s1 = environment map (cubemap or spherical)
//   s2 = specular ramp texture (optional)
//   s3 = dirt texture (optional)

sampler2D diffuseTex    : register(s0);
sampler2D envMapTex     : register(s1); // Using 2D for spherical env map
sampler2D specRampTex   : register(s2);
sampler2D dirtTex       : register(s3);

uniform float4 specParams   : register(c0); // x=power, y=intensity, z=fresnel, w=reflectivity
uniform float4 spec2Params  : register(c1); // x=power2, y=inten2, z=clearCoat, w=dirtLevel
uniform float4 paintColor   : register(c2); // xyz=body color, w=alpha
uniform float4 envColor     : register(c3); // xyz=environment reflection color
uniform float4 dirtParams   : register(c4); // xyz=dirt color, w=tiling

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float3 viewDir  : TEXCOORD2;
    float3 lightDir : TEXCOORD3;
};

float3 CalculateSpecular(float3 N, float3 V, float3 L, float power, float intensity)
{
    float3 H = normalize(V + L);
    float NdotH = saturate(dot(N, H));
    float spec = pow(NdotH, power) * intensity;
    return float3(spec, spec, spec);
}

float3 CalculateFresnel(float3 F0, float VdotH)
{
    // Schlick Fresnel approximation
    float fresnel = pow(1.0 - saturate(VdotH), 5.0);
    return lerp(F0, float3(1.0, 1.0, 1.0), fresnel);
}

float4 main(PS_INPUT IN) : COLOR
{
    float2 tex = IN.texCoord;
    float3 N = normalize(IN.normal);
    float3 V = normalize(IN.viewDir);
    float3 L = normalize(IN.lightDir);
    
    // Sample diffuse texture
    float4 diffuse = tex2D(diffuseTex, tex);
    
    // Apply paint color (body color from vehicle data)
    float3 baseColor = diffuse.rgb * paintColor.rgb;
    
    // Calculate lighting
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float3 H = normalize(V + L);
    float VdotH = saturate(dot(V, H));
    
    // === Specular Layer 1: Tight specular (metallic paint) ===
    float specPower = specParams.x;
    float specIntensity = specParams.y;
    float3 specular1 = CalculateSpecular(N, V, L, specPower, specIntensity);
    
    // Fresnel effect (more reflection at grazing angles)
    float fresnel = specParams.z;
    float3 F0 = float3(fresnel, fresnel, fresnel);
    float3 fresnelColor = CalculateFresnel(F0, VdotH);
    specular1 *= fresnelColor;
    
    // === Specular Layer 2: Wide specular (clear coat) ===
    float spec2Power = spec2Params.x;
    float spec2Intensity = spec2Params.y;
    float3 specular2 = CalculateSpecular(N, V, L, spec2Power, spec2Intensity);
    
    // Clear coat doesn't have Fresnel (it's on top)
    float clearCoatStrength = spec2Params.z;
    specular2 *= clearCoatStrength;
    
    // === Environment Reflection ===
    float3 reflVec = reflect(-V, N);
    // Convert to spherical UV
    float2 envUV = float2(
        atan2(reflVec.x, reflVec.z) / (2.0 * 3.14159) + 0.5,
        asin(reflVec.y) / 3.14159 + 0.5
    );
    float3 envReflection = tex2D(envMapTex, envUV).rgb;
    
    // Scale reflection by Fresnel and reflectivity
    float reflectivity = specParams.w;
    envReflection *= fresnelColor * reflectivity;
    
    // === Dirt Layer ===
    float dirtLevel = spec2Params.w;
    float3 dirtColor = dirtParams.rgb;
    float dirtTiling = dirtParams.w;
    
    if(dirtLevel > 0.001)
    {
        float2 dirtUV = tex * dirtTiling;
        float3 dirtSample = tex2D(dirtTex, dirtUV).rgb;
        float dirtBlend = dirtSample.r * dirtLevel;
        baseColor = lerp(baseColor, dirtColor * dirtSample, dirtBlend);
        // Dirt reduces specular
        specular1 *= (1.0 - dirtBlend * 0.5);
        specular2 *= (1.0 - dirtBlend * 0.5);
    }
    
    // === Combine all layers ===
    // Ambient term (approximate)
    float3 ambient = baseColor * 0.3;
    
    // Diffuse lighting
    float3 diffuseLight = baseColor * NdotL;
    
    // Final composition
    float3 finalColor = ambient + diffuseLight;
    finalColor += specular1 * specIntensity;
    finalColor += specular2 * clearCoatStrength;
    finalColor += envReflection * envColor.rgb;
    
    // Add specular ramp if available (artistic specular color control)
    // This would be sampled from specRampTex based on NdotL
    
    return float4(finalColor, paintColor.a);
}

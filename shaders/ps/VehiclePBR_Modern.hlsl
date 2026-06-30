// Vehicle PBR Shader with Color Separation (ps_3_0)
// Unified shader: paint, chrome, rubber, glass, dirt
// Uses GTA SA's material plugin data for classification
// Supports 4 vehicle color channels (MAT1-MAT4)
// PS2 spherical env mapping
//
// Material Classification (from GTA SA):
//   shininess > 0 + envmap = chrome/envmap
//   specularity > 0 = specular (paint, metal)
//   Both = metallic paint
//   Neither = rubber/plastic
//   Glass: detected by atomic alpha flag, rendered with refraction
//
// Vehicle Colors (from GTA SA):
//   MAT1 = primary body color (most of the car)
//   MAT2 = secondary color (trim, bumpers)
//   MAT3 = tertiary color (rarely used)
//   MAT4 = quaternary color (rarely used)
//
// Constants:
//   c0 = (shininess, specularity, fresnel, metalness)
//   c1 = (mat1Color.r, mat1Color.g, mat1Color.b, clearCoat)
//   c2 = (mat2Color.r, mat2Color.g, mat2Color.b, 0)
//   c3 = (mat3Color.r, mat3Color.g, mat3Color.b, 0)
//   c4 = (mat4Color.r, mat4Color.g, mat4Color.b, 0)
//   c5 = (roughness, reflectance, envMapIntensity, glassReflectivity)
//   c6 = (envColor.r, envColor.g, envColor.b, glassOpacity)
//
// Textures:
//   s0 = vehicle diffuse texture
//   s1 = environment map (spherical)
//   s2 = specular map (if available)
//   s3 = glass normal map (for distortion)

sampler2D vehicleDiffuseTex : register(s0);
sampler2D envMapTex         : register(s1);
sampler2D specMapTex        : register(s2);
sampler2D glassNormalTex    : register(s3);

uniform float4 materialParams : register(c0); // x=shininess, y=specularity, z=fresnel, w=metalness
uniform float4 mat1Color      : register(c1); // xyz=primary color, w=clear coat
uniform float4 mat2Color      : register(c2); // xyz=secondary color
uniform float4 mat3Color      : register(c3); // xyz=tertiary color
uniform float4 mat4Color      : register(c4); // xyz=quaternary color
uniform float4 renderParams   : register(c5); // x=roughness, y=reflectance, z=envMapIntensity, w=glassReflectivity
uniform float4 envColor       : register(c6); // xyz=environment tint, w=glassOpacity

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float3 viewDir  : TEXCOORD2;
    float3 lightDir : TEXCOORD3;
};

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

// PS2 Spherical Env Map
float2 PS2SphericalEnvMap(float3 reflVec, float3 viewDir)
{
    float2 envUV;
    envUV.x = reflVec.x * 0.5 + 0.5;
    envUV.y = reflVec.y * 0.5 + 0.5;
    envUV.x += viewDir.x * 0.1;
    envUV.y += viewDir.y * 0.1;
    return envUV;
}

// Detect vehicle color channel from diffuse texture color
// GTA SA uses sentinel colors in the diffuse to mark which channel
float4 DetectVehicleColorChannel(float3 diffuse)
{
    // MAT1: 0x00ff3c (green-ish)
    // MAT2: 0xaf00ff (purple)
    // MAT3: 0xffff00 (yellow)
    // MAT4: 0xff00ff (magenta)
    
    float r = diffuse.r;
    float g = diffuse.g;
    float b = diffuse.b;
    
    // Check each sentinel color (with tolerance)
    float isMat1 = step(0.9, g) * step(0.9, b) * step(r, 0.1); // green
    float isMat2 = step(0.6, r) * step(0.9, b) * step(g, 0.1); // purple
    float isMat3 = step(0.9, r) * step(0.9, g) * step(b, 0.1); // yellow
    float isMat4 = step(0.9, r) * step(0.9, b) * step(g, 0.1); // magenta
    
    return float4(isMat1, isMat2, isMat3, isMat4);
}

float4 main(PS_INPUT IN) : COLOR
{
    float2 tex = IN.texCoord;
    
    // Sample textures
    float4 diffuse = tex2D(vehicleDiffuseTex, tex);
    
    // Detect which vehicle color channel this pixel uses
    float4 colorMask = DetectVehicleColorChannel(diffuse.rgb);
    float isVehicleColor = colorMask.x + colorMask.y + colorMask.z + colorMask.w;
    
    // Apply vehicle colors based on detected channel
    float3 baseColor;
    if(isVehicleColor > 0.5)
    {
        // This pixel uses a vehicle color - blend based on mask
        baseColor = diffuse.rgb;
        baseColor = lerp(baseColor, mat1Color.rgb, colorMask.x);
        baseColor = lerp(baseColor, mat2Color.rgb, colorMask.y);
        baseColor = lerp(baseColor, mat3Color.rgb, colorMask.z);
        baseColor = lerp(baseColor, mat4Color.rgb, colorMask.w);
    }
    else
    {
        // Normal diffuse (not a vehicle color remap)
        baseColor = diffuse.rgb;
    }
    
    // Unpack material properties
    float shininess = materialParams.x;
    float specularity = materialParams.y;
    float fresnel = materialParams.z;
    float metalness = materialParams.w;
    
    // Lighting vectors
    float3 N = normalize(IN.normal);
    float3 V = normalize(IN.viewDir);
    float3 L = normalize(IN.lightDir);
    float3 H = normalize(V + L);
    
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));
    
    // Material classification
    float isChrome = step(0.5, shininess) * step(0.5, 1.0 - specularity);
    float isPaint = step(0.1, shininess) * step(0.1, specularity);
    float isMetal = step(0.5, specularity) * step(0.5, 1.0 - shininess);
    float isRubber = step(0.5, 1.0 - shininess) * step(0.5, 1.0 - specularity);
    
    // Roughness per material
    float roughness = renderParams.x;
    roughness = lerp(roughness, 0.05, isChrome);
    roughness = lerp(roughness, 0.3, isPaint);
    roughness = lerp(roughness, 0.8, isRubber);
    
    // Metalness per material
    float finalMetalness = metalness;
    finalMetalness = lerp(finalMetalness, 1.0, isChrome);
    finalMetalness = lerp(finalMetalness, 0.8, isMetal);
    finalMetalness = lerp(finalMetalness, 0.0, isRubber);
    
    // F0
    float reflectance = renderParams.y;
    float3 F0 = lerp(float3(reflectance, reflectance, reflectance), baseColor, finalMetalness);
    
    // Specular BRDF
    float D = GGX_NDF(NdotH, roughness);
    float V_term = SmithVisibility(NdotL, NdotV, roughness);
    float3 F = SchlickFresnel(F0, VdotH);
    float3 specular = D * V_term * F;
    
    // Add specular from game data
    if(specularity > 0.001)
    {
        float3 specMapColor = tex2D(specMapTex, tex).rgb;
        specular *= specMapColor * specularity;
    }
    
    // Diffuse (non-metals only)
    float3 diffuseLight = baseColor * NdotL * (1.0 - finalMetalness);
    
    // Environment Reflection (PS2 Spherical)
    float3 reflVec = reflect(-V, N);
    float2 envUV = PS2SphericalEnvMap(reflVec, V);
    float3 envReflection = tex2D(envMapTex, envUV).rgb;
    
    float3 envFresnel = SchlickFresnel(F0, NdotV);
    float envMapIntensity = renderParams.z;
    envReflection *= envFresnel * shininess * envMapIntensity;
    
    // Chrome: stronger, colored reflections
    envReflection = lerp(envReflection, envReflection * baseColor * 2.0, isChrome);
    
    // Clear coat (paint only)
    float clearCoat = mat1Color.w;
    float3 clearCoatF0 = float3(0.04, 0.04, 0.04);
    float3 clearCoatF = SchlickFresnel(clearCoatF0, NdotV);
    float3 clearCoatSpec = clearCoatF * clearCoat * 0.5;
    
    // Final composition
    float3 finalColor = float3(0, 0, 0);
    
    // Ambient
    finalColor += baseColor * 0.15;
    
    // Diffuse + Specular
    finalColor += diffuseLight + specular * NdotL;
    
    // Environment reflection
    finalColor += envReflection * envColor.rgb;
    
    // Clear coat (paint only)
    finalColor += clearCoatSpec * isPaint;
    
    // Rubber: darker
    finalColor *= lerp(1.0, 0.3, isRubber);
    
    return float4(finalColor, diffuse.a);
}

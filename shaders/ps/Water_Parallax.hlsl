// Water Parallax Occlusion Mapping (ps_3_0)
// Mimics GTA IV/V/RDR2 water with depth-based parallax and normal mapping
//
// Constants:
//   c0 = (parallaxScale, parallaxBias, waveSpeed, waveFreq)
//   c1 = (waterDepth, waterAlpha, fresnelPower, fresnelBias)
//   c2 = (normalStrength, specularPower, specularIntensity, 0)
//   c3 = (waterColor.r, waterColor.g, waterColor.b, 0)
//   c4 = (screenW, screenH, 1/screenW, 1/screenH)
//
// Textures:
//   s0 = water diffuse texture
//   s1 = water normal map
//   s2 = depth buffer (for water depth)
//   s3 = scene color (for refraction)

sampler2D waterDiffuseTex : register(s0);
sampler2D waterNormalTex  : register(s1);
sampler2D depthTex        : register(s2);
sampler2D sceneTex        : register(s3);

uniform float4 parallaxParams : register(c0); // x=scale, y=bias, z=speed, w=freq
uniform float4 waterParams    : register(c1); // x=depth, y=alpha, z=fresnelPower, w=fresnelBias
uniform float4 normalParams   : register(c2); // x=strength, y=specPower, z=specIntensity
uniform float4 waterColor     : register(c3); // xyz=deep water color
uniform float4 screenSize     : register(c4); // xy=resolution, zw=1/resolution

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
    float3 viewDir  : TEXCOORD1;
    float3 lightDir : TEXCOORD2;
};

float2 ParallaxOcclusionMapping(float2 texCoord, float3 viewDir, sampler2D heightMap, float scale, float bias)
{
    // Number of layers for parallax
    const int numLayers = 16;
    
    // Calculate layer depth
    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;
    
    // Calculate texture offset per layer
    float2 P = viewDir.xy / viewDir.z * scale;
    float2 deltaTexCoords = P / numLayers;
    
    // Initial values
    float2 currentTexCoords = texCoord;
    float currentDepthMapValue = tex2D(heightMap, currentTexCoords).r;
    
    // Steep parallax loop (unrolled for D3D9 compatibility)
    float2 tc0 = texCoord;
    float2 tc1 = texCoord - deltaTexCoords * 1;
    float2 tc2 = texCoord - deltaTexCoords * 2;
    float2 tc3 = texCoord - deltaTexCoords * 3;
    float2 tc4 = texCoord - deltaTexCoords * 4;
    float2 tc5 = texCoord - deltaTexCoords * 5;
    float2 tc6 = texCoord - deltaTexCoords * 6;
    float2 tc7 = texCoord - deltaTexCoords * 7;
    float2 tc8 = texCoord - deltaTexCoords * 8;
    float2 tc9 = texCoord - deltaTexCoords * 9;
    float2 tc10 = texCoord - deltaTexCoords * 10;
    float2 tc11 = texCoord - deltaTexCoords * 11;
    float2 tc12 = texCoord - deltaTexCoords * 12;
    float2 tc13 = texCoord - deltaTexCoords * 13;
    float2 tc14 = texCoord - deltaTexCoords * 14;
    float2 tc15 = texCoord - deltaTexCoords * 15;
    
    // Sample all depths
    float d0 = tex2D(heightMap, tc0).r;
    float d1 = tex2D(heightMap, tc1).r;
    float d2 = tex2D(heightMap, tc2).r;
    float d3 = tex2D(heightMap, tc3).r;
    float d4 = tex2D(heightMap, tc4).r;
    float d5 = tex2D(heightMap, tc5).r;
    float d6 = tex2D(heightMap, tc6).r;
    float d7 = tex2D(heightMap, tc7).r;
    float d8 = tex2D(heightMap, tc8).r;
    float d9 = tex2D(heightMap, tc9).r;
    float d10 = tex2D(heightMap, tc10).r;
    float d11 = tex2D(heightMap, tc11).r;
    float d12 = tex2D(heightMap, tc12).r;
    float d13 = tex2D(heightMap, tc13).r;
    float d14 = tex2D(heightMap, tc14).r;
    float d15 = tex2D(heightMap, tc15).r;
    
    // Find first layer where depth > layer depth
    float depths[16] = {d0, d1, d2, d3, d4, d5, d6, d7, d8, d9, d10, d11, d12, d13, d14, d15};
    float2 texCoords[16] = {tc0, tc1, tc2, tc3, tc4, tc5, tc6, tc7, tc8, tc9, tc10, tc11, tc12, tc13, tc14, tc15};
    
    // Find the first intersection
    float2 finalTexCoords = texCoord;
    [unroll]
    for(int i = 0; i < 16; i++)
    {
        float layerD = i * layerDepth;
        if(layerD >= depths[i])
        {
            // Interpolate with previous layer
            float2 prevTC = (i > 0) ? texCoords[i-1] : texCoord;
            float prevDepth = (i > 0) ? depths[i-1] : d0;
            float prevLayerD = (i > 0) ? (i-1) * layerDepth : 0.0;
            
            float afterDepth = depths[i] - layerD;
            float beforeDepth = prevDepth - prevLayerD;
            float weight = afterDepth / (afterDepth - beforeDepth + 0.0001);
            
            finalTexCoords = prevTC * weight + texCoords[i] * (1.0 - weight);
            break;
        }
    }
    
    return finalTexCoords;
}

float4 main(PS_INPUT IN) : COLOR
{
    float2 tex = IN.texCoord;
    float2 pixel = screenSize.zw;
    
    // Animate water UVs
    float time = parallaxParams.z * 0.01; // waveSpeed
    float freq = parallaxParams.w;
    float2 animatedTex = tex + float2(
        sin(tex.y * freq + time) * 0.002,
        cos(tex.x * freq + time * 0.7) * 0.002
    );
    
    // Sample normal map with animation
    float3 normal = tex2D(waterNormalTex, animatedTex * 2.0).rgb * 2.0 - 1.0;
    normal.xy *= normalParams.x; // normalStrength
    normal = normalize(normal);
    
    // Parallax occlusion mapping for water surface detail
    float2 parallaxTex = ParallaxOcclusionMapping(
        animatedTex, 
        normalize(IN.viewDir), 
        waterNormalTex, 
        parallaxParams.x,  // parallaxScale
        parallaxParams.y   // parallaxBias
    );
    
    // Sample water diffuse with parallax
    float3 waterDiffuse = tex2D(waterDiffuseTex, parallaxTex).rgb;
    
    // Get screen UV for refraction
    float2 screenUV = tex + normal.xy * 0.02; // Refraction offset
    
    // Sample scene color for refraction
    float3 sceneColor = tex2D(sceneTex, screenUV).rgb;
    
    // Get depth for water transparency
    float depth = tex2D(depthTex, tex).r;
    float waterDepth = waterParams.x;
    float depthFactor = saturate((depth - waterDepth) * 10.0);
    
    // Fresnel effect (more reflection at grazing angles)
    float3 viewDir = normalize(IN.viewDir);
    float NdotV = saturate(dot(normal, viewDir));
    float fresnel = pow(1.0 - NdotV, waterParams.z);
    fresnel = fresnel * (1.0 - waterParams.w) + waterParams.w; // Apply bias
    
    // Specular highlight (Blinn-Phong)
    float3 lightDir = normalize(IN.lightDir);
    float3 halfDir = normalize(viewDir + lightDir);
    float NdotH = saturate(dot(normal, halfDir));
    float specular = pow(NdotH, normalParams.y) * normalParams.z;
    
    // Deep water color
    float3 deepColor = waterColor.rgb;
    
    // Blend between refraction and deep water based on depth
    float3 waterBase = lerp(sceneColor, deepColor, depthFactor * waterParams.y);
    
    // Add water diffuse detail
    waterBase *= lerp(float3(1, 1, 1), waterDiffuse, 0.3);
    
    // Add reflection (Fresnel-based)
    float3 reflection = sceneColor * 0.5; // Simplified reflection
    waterBase = lerp(waterBase, reflection, fresnel * 0.5);
    
    // Add specular highlight
    waterBase += float3(1, 1, 1) * specular;
    
    return float4(waterBase, waterParams.y);
}

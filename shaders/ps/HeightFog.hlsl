// HeightFog.hlsl - Crytek exponential height fog (ps_3_0)
// Fullscreen post-process: reads depth, reconstructs world height, blends fog

sampler2D sceneTex : register(s0);
sampler2D depthTex : register(s1);

uniform float4 fogParams   : register(c0);
uniform float4 fogColor    : register(c1);
uniform float4 projInfo    : register(c2);
uniform float4 screenSize  : register(c3);
uniform float4 camPos      : register(c4);
uniform float4 camAxisZ    : register(c5);

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float4 main(PS_INPUT IN) : COLOR
{
    float2 tex = IN.texCoord;
    
    // Sample depth and scene
    float depth = tex2D(depthTex, tex).r;
    float3 scene = tex2D(sceneTex, tex).rgb;
    
    // Skip sky (depth at far plane)
    if(depth >= 0.999)
        return float4(scene, 1.0);
    
    // Linearize depth
    float linearDepth = projInfo.z / (depth - projInfo.w);
    
    // Reconstruct view-space position
    float2 ndc = tex * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float3 viewPos = float3(ndc * projInfo.xy * linearDepth, linearDepth);
    
    // Approximate world height from camera orientation
    float worldZ = camPos.z + viewPos.x * camAxisZ.x 
                 + viewPos.y * camAxisZ.y 
                 - viewPos.z * camAxisZ.z;
    
    // Unpack fog parameters
    float fogDensity = fogParams.x;
    float heightFalloff = fogParams.y;
    float startHeight = fogParams.z;
    float maxFog = fogParams.w;
    
    // Crytek exponential height fog
    float heightFactor = exp(-heightFalloff * max(worldZ - startHeight, 0.0));
    float distFactor = 1.0 - exp(-fogDensity * linearDepth);
    float totalFog = saturate(heightFactor * distFactor);
    totalFog = min(totalFog, maxFog);
    
    // Blend fog with scene
    float3 result = lerp(scene, fogColor.rgb, totalFog);
    return float4(result, 1.0);
}

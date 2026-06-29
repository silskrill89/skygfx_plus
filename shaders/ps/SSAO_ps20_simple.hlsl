// Ultra-simple SSAO for ps_2_0 (64 instruction limit)
uniform sampler2D depthTexture : register(s0);
uniform sampler2D randomTexture : register(s1);

uniform float4 ssaoParams : register(c0); // x=radius, y=power, z=noiseScale
uniform float4 screenSize : register(c1); // x=width, y=height, z=1/width, w=1/height
uniform float4 projInfo : register(c2); // x=1/proj11, y=1/proj22, z=proj43, w=proj33

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float4 main(PS_INPUT IN) : COLOR
{
    float centerDepth = tex2D(depthTexture, IN.texCoord).r;
    if (centerDepth >= 1.0) return float4(1,1,1,1);
    
    // Reconstruct view-space Z
    float viewZ = projInfo.z / (centerDepth - projInfo.w);
    
    // Random noise for sampling
    float2 noiseScale = ssaoParams.z * screenSize.xy;
    float3 rand = tex2D(randomTexture, IN.texCoord * noiseScale).rgb * 2.0 - 1.0;
    
    float occlusion = 0.0;
    float radius = ssaoParams.x;
    
    // Only 4 samples to fit in ps_2_0
    float2 offs[4] = {
        float2( 1,  0), float2(-1,  0),
        float2( 0,  1), float2( 0, -1)
    };
    
    for (int i = 0; i < 4; ++i)
    {
        float2 offset = offs[i] * radius * screenSize.zw;
        float2 sampleCoord = IN.texCoord + offset;
        
        if (sampleCoord.x >= 0.0 && sampleCoord.x <= 1.0 &&
            sampleCoord.y >= 0.0 && sampleCoord.y <= 1.0)
        {
            float sampleDepth = tex2D(depthTexture, sampleCoord).r;
            float sampleViewZ = projInfo.z / (sampleDepth - projInfo.w);
            
            float diff = viewZ - sampleViewZ;
            occlusion += step(0.0, diff) * (1.0 - saturate(diff / radius));
        }
    }
    
    occlusion = 1.0 - (occlusion / 4.0);
    occlusion = occlusion * occlusion; // simple power
    
    return float4(occlusion, occlusion, occlusion, 1.0);
}
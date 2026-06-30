// Full SSAO implementation
// Uses depth buffer to approximate ambient occlusion
// Compiled with ps_3_0 via build system

uniform sampler2D depthTexture : register(s0);
uniform sampler2D randomTexture : register(s1);

uniform float4 ssaoParams : register(c0); // x=radius, y=power, z=noiseScale, w=kernelSize
uniform float4 screenSize : register(c1); // x=width, y=height, z=1/width, w=1/height
uniform float4 projInfo : register(c2); // projection matrix info

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float3 GetViewPos(float2 texCoord, float depth)
{
    float2 ndc = texCoord * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float viewZ = projInfo.z / (depth - projInfo.w);
    float2 viewXY = ndc * viewZ * projInfo.xy;
    return float3(viewXY, viewZ);
}

float4 main(PS_INPUT IN) : COLOR
{
    float centerDepth = tex2D(depthTexture, IN.texCoord).r;
    
    if (centerDepth >= 1.0)
        return float4(1, 1, 1, 1);
    
    float3 centerPos = GetViewPos(IN.texCoord, centerDepth);
    
    float2 noiseScale = ssaoParams.z * screenSize.xy;
    float3 rand = tex2D(randomTexture, IN.texCoord * noiseScale).rgb * 2.0 - 1.0;
    
    float occlusion = 0.0;
    float radius = ssaoParams.x;
    float power = ssaoParams.y;
    
    // Generate 16 random kernel samples
    float3 kernel[16];
    for (int i = 0; i < 16; ++i)
    {
        float3 sample = float3(
            rand.x * 2.0 - 1.0,
            rand.y * 2.0 - 1.0,
            rand.z * 2.0 - 1.0
        );
        sample = normalize(sample);
        sample *= rand.x * radius;
        kernel[i] = sample;
        rand = tex2D(randomTexture, float2(i * 0.1, 0.0)).rgb * 2.0 - 1.0;
    }
    
    // Calculate occlusion using all kernel samples
    for (int i = 0; i < 16; ++i)
    {
        float3 samplePos = centerPos + kernel[i];
        
        float2 sampleCoord = samplePos.xy / samplePos.z;
        sampleCoord = (sampleCoord + 1.0) * 0.5;
        
        if (sampleCoord.x >= 0.0 && sampleCoord.x <= 1.0 &&
            sampleCoord.y >= 0.0 && sampleCoord.y <= 1.0)
        {
            float sampleDepth = tex2D(depthTexture, sampleCoord).r;
            float3 sampleViewPos = GetViewPos(sampleCoord, sampleDepth);
            
            float diff = length(sampleViewPos - centerPos);
            float rangeCheck = smoothstep(0.0, radius, diff);
            occlusion += rangeCheck * step(sampleViewPos.z, centerPos.z);
        }
    }
    
    occlusion = 1.0 - (occlusion / 16.0);
    occlusion = pow(occlusion, power);
    
    return float4(occlusion, occlusion, occlusion, 1.0);
}

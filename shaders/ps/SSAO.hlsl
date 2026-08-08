// SSAO.hlsl - Screen-space ambient occlusion (ps_3_0)
// Compiled via /E main -> SSAO.cso
// Uses depth buffer + normal buffer for hemisphere-oriented ambient occlusion
// Based on Alex Tardif's SSAO (https://alextardif.com/SSAO.html)

uniform sampler2D depthTexture : register(s0);
uniform sampler2D randomTexture : register(s1);
uniform sampler2D normalTexture : register(s2);

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

float3 GetNormal(float2 texCoord)
{
    float3 normal = tex2D(normalTexture, texCoord).rgb;
    // Decode from [0,1] back to [-1,1]
    return normal * 2.0 - 1.0;
}

float3x3 BuildTBN(float3 normal, float2 texCoord, float2 noiseScale)
{
    float3 randVec = tex2D(randomTexture, texCoord * noiseScale).rgb * 2.0 - 1.0;
    float3 tangent = normalize(randVec - normal * dot(randVec, normal));
    float3 bitangent = cross(normal, tangent);
    return float3x3(tangent, bitangent, normal);
}

float4 main(PS_INPUT IN) : COLOR
{
    float centerDepth = tex2D(depthTexture, IN.texCoord).r;

    if (centerDepth >= 1.0)
        return float4(1, 1, 1, 1);

    float3 centerPos = GetViewPos(IN.texCoord, centerDepth);

    // Get normal from buffer (oriented hemisphere)
    float3 normal = GetNormal(IN.texCoord);
    float normalLen = length(normal);
    if(normalLen < 0.5){
        // Normal buffer not available — decode D3D9 NULL texture (0,0,0,0) gives (-1,-1,-1)
        // or genuinely small normal. Reconstruct from depth as fallback.
        float2 texel = screenSize.zw;
        float dc = centerDepth;
        float dl = tex2D(depthTexture, IN.texCoord - float2(texel.x, 0)).r;
        float dr = tex2D(depthTexture, IN.texCoord + float2(texel.x, 0)).r;
        float du = tex2D(depthTexture, IN.texCoord - float2(0, texel.y)).r;
        float dd = tex2D(depthTexture, IN.texCoord + float2(0, texel.y)).r;
        float3 pc = GetViewPos(IN.texCoord, dc);
        float3 pl = GetViewPos(IN.texCoord - float2(texel.x, 0), dl);
        float3 pr = GetViewPos(IN.texCoord + float2(texel.x, 0), dr);
        float3 pu = GetViewPos(IN.texCoord - float2(0, texel.y), du);
        float3 pd = GetViewPos(IN.texCoord + float2(0, texel.y), dd);
        float3 dx1 = pr - pc;
        float3 dx2 = pc - pl;
        float3 dy1 = pd - pc;
        float3 dy2 = pc - pu;
        float3 dx = (abs(dx1.z) < abs(dx2.z)) ? dx1 : dx2;
        float3 dy = (abs(dy1.z) < abs(dy2.z)) ? dy1 : dy2;
        normal = normalize(cross(dx, dy) + 1e-7);
    }

    float2 noiseScale = ssaoParams.z * screenSize.xy;
    float3x3 TBN = BuildTBN(normal, IN.texCoord, noiseScale);

    float occlusion = 0.0;
    float radius = ssaoParams.x;
    float power = ssaoParams.y;

    // Generate 16 hemisphere kernel samples oriented by surface normal
    float3 kernel[16];
    float3 rand = tex2D(randomTexture, IN.texCoord * noiseScale).rgb * 2.0 - 1.0;
    for (int i = 0; i < 16; ++i)
    {
        float3 sample = float3(
            rand.x * 2.0 - 1.0,
            rand.y * 2.0 - 1.0,
            abs(rand.z) * 2.0 - 0.5  // bias towards hemisphere
        );
        sample = normalize(sample);
        sample *= abs(rand.x) * radius;
        kernel[i] = mul(sample, TBN);  // orient by surface normal
        rand = tex2D(randomTexture, float2(i * 0.1, 0.0)).rgb * 2.0 - 1.0;
    }

    // Calculate occlusion using hemisphere-oriented kernel samples
    for (int i = 0; i < 16; ++i)
    {
        float3 samplePos = centerPos + kernel[i];

        // Project sample position to screen space
        float2 sampleCoord = samplePos.xy / samplePos.z;
        sampleCoord = (sampleCoord + 1.0) * 0.5;

        if (sampleCoord.x >= 0.0 && sampleCoord.x <= 1.0 &&
            sampleCoord.y >= 0.0 && sampleCoord.y <= 1.0)
        {
            float sampleDepth = tex2D(depthTexture, sampleCoord).r;
            float3 sampleViewPos = GetViewPos(sampleCoord, sampleDepth);

            // Range check and angle-aware occlusion
            float diff = length(sampleViewPos - centerPos);
            float rangeCheck = smoothstep(radius, 0.0, diff);
            float nDotS = max(dot(normal, normalize(samplePos - centerPos)), 0.0);
            occlusion += rangeCheck * step(sampleViewPos.z, centerPos.z) * nDotS;
        }
    }

    occlusion = 1.0 - (occlusion / 16.0);
    occlusion = pow(occlusion, power);

    return float4(occlusion, occlusion, occlusion, 1.0);
}

// SSAO.hlsli - D3D9 ps_3_0 compatible SSAO include
// Based on Alex Tardif's SSAO (https://alextardif.com/SSAO.html)
// Converted from DX10+ to D3D9 ps_3_0 syntax for skygfx
//
// Usage: #include this from your pixel shader, call SSAO_Compute()
// Requires: depthTex (s0), noiseTex (s1), normalTex (s2), ssaoParams (c0), screenSize (c1), projInfo (c2)

#ifndef SSAO_HLSLI_INCLUDED
#define SSAO_HLSLI_INCLUDED

float3 SSAO_ViewPosFromDepth(float2 uv, float depth, float4 projInfo)
{
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float viewZ = projInfo.z / (depth - projInfo.w);
    float2 viewXY = ndc * viewZ * projInfo.xy;
    return float3(viewXY, viewZ);
}

float3x3 SSAO_BuildTBN(float3 normal, float2 uv, sampler2D noiseTex, float noiseScale)
{
    float3 randVec = tex2D(noiseTex, uv * noiseScale).xyz * 2.0 - 1.0;
    float3 tangent = normalize(randVec - normal * dot(randVec, normal));
    float3 bitangent = cross(normal, tangent);
    return float3x3(tangent, bitangent, normal);
}

// Hemisphere-oriented SSAO for ps_3_0
// depthTex:   depth buffer (s0)
// noiseTex:   random noise texture (s1)
// normalTex:  normal buffer (s2), rgb in [0,1] decoded to [-1,1]
// ssaoParams: x=radius, y=power, z=noiseScale, w=unused
// screenSize: x=width, y=height, z=1/width, w=1/height
// projInfo:   x=1/proj11, y=1/proj22, z=proj43, w=proj33
float4 SSAO_Compute(float2 uv,
                    sampler2D depthTex,
                    sampler2D noiseTex,
                    sampler2D normalTex,
                    float4 ssaoParams,
                    float4 screenSize,
                    float4 projInfo)
{
    float centerDepth = tex2D(depthTex, uv).r;
    if (centerDepth >= 0.999)
        return float4(1, 1, 1, 1);

    float3 centerPos = SSAO_ViewPosFromDepth(uv, centerDepth, projInfo);

    float3 normal = tex2D(normalTex, uv).rgb * 2.0 - 1.0;
    if (length(normal) < 0.5)
        normal = float3(0, 0, 1);

    float radius = ssaoParams.x;
    float power = ssaoParams.y;
    float noiseScale = ssaoParams.z;

    float3x3 TBN = SSAO_BuildTBN(normal, uv, noiseTex, noiseScale);

    float occlusion = 0.0;

    float2 offsets[4] = {
        float2( 1,  0), float2(-1,  0),
        float2( 0,  1), float2( 0, -1)
    };

    for (int i = 0; i < 4; i++)
    {
        float2 sampleUV = uv + offsets[i] * radius * screenSize.zw;
        float sd = tex2D(depthTex, sampleUV).r;
        float3 sv = SSAO_ViewPosFromDepth(sampleUV, sd, projInfo);

        float diff = centerPos.z - sv.z;
        float3 sampleDir = normalize(sv - centerPos);
        float nDotS = max(dot(normal, sampleDir), 0.0);
        occlusion += step(0.0, diff) * (1.0 - saturate(length(sv - centerPos) / radius)) * nDotS;
    }

    occlusion = 1.0 - occlusion * 0.25;
    return float4(pow(occlusion, power).xxx, 1.0);
}

#endif

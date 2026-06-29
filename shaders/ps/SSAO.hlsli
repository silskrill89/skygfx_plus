// SSAO.hlsli - D3D9 ps_3_0 compatible SSAO include
// Based on Alex Tardif's SSAO (https://alextardif.com/SSAO.html)
// Converted from DX10+ to D3D9 ps_3_0 syntax for skygfx
//
// Usage: #include this from your pixel shader, call SSAO_Compute()
// Requires: depthTex (s0), noiseTex (s1), ssaoParams (c0), screenSize (c1), projInfo (c2)

#ifndef SSAO_HLSLI_INCLUDED
#define SSAO_HLSLI_INCLUDED

float3 SSAO_DecodeSphereMap(float2 e)
{
    float2 tmp = e - e * e;
    float f = tmp.x + tmp.y;
    float m = sqrt(4.0 * f - 1.0);
    float3 n;
    n.xy = m * (e * 4.0 - 2.0);
    n.z = 3.0 - 8.0 * f;
    return n;
}

float3 SSAO_ViewPosFromDepth(float2 uv, float depth, float4 projInfo)
{
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float viewZ = projInfo.z / (depth - projInfo.w);
    float2 viewXY = ndc * viewZ * projInfo.xy;
    return float3(viewXY, viewZ);
}

// 4-sample SSAO for ps_3_0
// depthTex:   depth buffer
// noiseTex:   random noise texture
// ssaoParams: x=radius, y=power, z=noiseScale, w=unused
// screenSize: x=width, y=height, z=1/width, w=1/height
// projInfo:   x=1/proj11, y=1/proj22, z=proj43, w=proj33
float4 SSAO_Compute(float2 uv,
                    sampler2D depthTex,
                    sampler2D noiseTex,
                    float4 ssaoParams,
                    float4 screenSize,
                    float4 projInfo)
{
    float centerDepth = tex2D(depthTex, uv).r;
    if (centerDepth >= 1.0)
        return float4(1, 1, 1, 1);

    float3 centerPos = SSAO_ViewPosFromDepth(uv, centerDepth, projInfo);
    float radius = ssaoParams.x;
    float power = ssaoParams.y;
    float noiseScale = ssaoParams.z;

    float3 randVec = tex2D(noiseTex, uv * noiseScale * screenSize.xy).xyz * 2.0 - 1.0;
    float3 normal = SSAO_DecodeSphereMap(tex2D(noiseTex, uv).xy);
    float3 tangent = normalize(randVec - normal * dot(randVec, normal));
    float3 bitangent = cross(normal, tangent);

    float2 offsets[4] = {
        float2( 1,  0), float2(-1,  0),
        float2( 0,  1), float2( 0, -1)
    };

    float occlusion = 0.0;
    for (int i = 0; i < 4; i++)
    {
        float2 sampleUV = uv + offsets[i] * radius * screenSize.zw;
        float sd = tex2D(depthTex, sampleUV).r;
        float3 sv = SSAO_ViewPosFromDepth(sampleUV, sd, projInfo);

        float diff = centerPos.z - sv.z;
        occlusion += step(0.0, diff) * (1.0 - saturate(length(sv - centerPos) / radius));
    }

    occlusion = 1.0 - occlusion * 0.25;
    return float4(pow(occlusion, power).xxx, 1.0);
}

#endif

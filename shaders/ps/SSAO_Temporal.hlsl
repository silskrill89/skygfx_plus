// SSAO_Temporal.hlsl - Quarter-res temporal SSAO (ps_3_0)
// Compiled via /E main -> SSAO_Temporal.cso
// Quarter-res SSAO with temporal reprojection via velocity buffer.
// 16-sample hemisphere kernel, depth-based normal reconstruction fallback,
// temporal blending with confidence-weighted history.
//
// Registers:
//   s0: depth texture (INTZ/D24S8)
//   s1: noise texture (4x4 RGBA8, tiled, WRAP)
//   s2: normal texture (g_normalBufferTex, A8R8G8B8 half-res)
//   s3: velocity texture (g_velocityTex, A8R8G8B8, RG=velocity XY packed [0,1])
//   s4: history SSAO (quarter-res RGBA8, previous frame temporal result)
//
// Constants:
//   c0: ssaoParams = {radius, power, noiseScale, temporalBlend}
//   c1: screenSize = {quarterW, quarterH, 1/quarterW, 1/quarterH}
//   c2: projInfo = {recipViewWindow.x, recipViewWindow.y, -n*f/(f-n), f/(f-n)}

uniform sampler2D depthTexture   : register(s0);
uniform sampler2D noiseTexture   : register(s1);
uniform sampler2D normalTexture  : register(s2);
uniform sampler2D velocityTexture : register(s3);
uniform sampler2D historyTexture : register(s4);

uniform float4 ssaoParams : register(c0);  // x=radius, y=power, z=noiseScale, w=temporalBlend
uniform float4 screenSize : register(c1);  // x=quarterW, y=quarterH, z=1/quarterW, w=1/quarterH
uniform float4 projInfo   : register(c2);  // recipViewWindow, -n*f/(f-n), f/(f-n)

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

// ---------------------------------------------------------------------------
// Depth -> view-space position reconstruction
// ---------------------------------------------------------------------------
float3 GetViewPos(float2 texCoord, float depth)
{
    float2 ndc = texCoord * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float viewZ = projInfo.z / (depth - projInfo.w);
    float2 viewXY = ndc * viewZ * projInfo.xy;
    return float3(viewXY, viewZ);
}

// ---------------------------------------------------------------------------
// Decode normal from [0,1] -> [-1,1]
// ---------------------------------------------------------------------------
float3 DecodeNormal(float2 texCoord)
{
    float3 normal = tex2D(normalTexture, texCoord).rgb;
    return normal * 2.0 - 1.0;
}

// ---------------------------------------------------------------------------
// Reconstruct normal from depth derivatives (5-tap cross pattern)
// Used as fallback when normal buffer is unavailable.
// ---------------------------------------------------------------------------
float3 ReconstructNormalFromDepth(float2 texCoord)
{
    float2 texel = screenSize.zw;
    float dc = tex2D(depthTexture, texCoord).r;
    float dl = tex2D(depthTexture, texCoord - float2(texel.x, 0)).r;
    float dr = tex2D(depthTexture, texCoord + float2(texel.x, 0)).r;
    float du = tex2D(depthTexture, texCoord - float2(0, texel.y)).r;
    float dd = tex2D(depthTexture, texCoord + float2(0, texel.y)).r;

    float3 pc = GetViewPos(texCoord, dc);
    float3 pl = GetViewPos(texCoord - float2(texel.x, 0), dl);
    float3 pr = GetViewPos(texCoord + float2(texel.x, 0), dr);
    float3 pu = GetViewPos(texCoord - float2(0, texel.y), du);
    float3 pd = GetViewPos(texCoord + float2(0, texel.y), dd);

    float3 dx1 = pr - pc;
    float3 dx2 = pc - pl;
    float3 dy1 = pd - pc;
    float3 dy2 = pc - pu;

    float3 dx = (abs(dx1.z) < abs(dx2.z)) ? dx1 : dx2;
    float3 dy = (abs(dy1.z) < abs(dy2.z)) ? dy1 : dy2;

    return normalize(cross(dx, dy) + 1e-7);
}

// ---------------------------------------------------------------------------
// Build TBN matrix from normal + tiled noise
// ---------------------------------------------------------------------------
float3x3 BuildTBN(float3 normal, float2 texCoord)
{
    float noiseScale = ssaoParams.z;
    float3 randVec = tex2D(noiseTexture, texCoord * noiseScale).rgb * 2.0 - 1.0;
    float3 tangent = normalize(randVec - normal * dot(randVec, normal));
    float3 bitangent = cross(normal, tangent);
    return float3x3(tangent, bitangent, normal);
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------
float4 main(PS_INPUT IN) : COLOR
{
    float centerDepth = tex2D(depthTexture, IN.texCoord).r;

    // Sky pixel — output fully unoccluded
    if (centerDepth >= 0.999)
        return float4(1.0, 1.0, 0.0, 1.0);

    float3 centerPos = GetViewPos(IN.texCoord, centerDepth);

    // --- Normal acquisition: buffer or depth fallback ---
    float3 normal = DecodeNormal(IN.texCoord);
    if (length(normal) < 0.5)
    {
        normal = ReconstructNormalFromDepth(IN.texCoord);
    }

    // --- TBN frame ---
    float3x3 TBN = BuildTBN(normal, IN.texCoord);

    float occlusion = 0.0;
    float radius = ssaoParams.x;
    float power = ssaoParams.y;
    float noiseScale = ssaoParams.z;

    // --- 16-sample hemisphere kernel ---
    float3 rand = tex2D(noiseTexture, IN.texCoord * noiseScale).rgb * 2.0 - 1.0;

    for (int i = 0; i < 16; ++i)
    {
        float3 sample = float3(
            rand.x * 2.0 - 1.0,
            rand.y * 2.0 - 1.0,
            abs(rand.z) * 2.0 - 0.5   // bias towards hemisphere
        );
        sample = normalize(sample);
        sample *= abs(rand.x) * radius;  // FIXED: signed -> abs
        float3 kernelSample = mul(sample, TBN);

        float3 samplePos = centerPos + kernelSample;

        // Project to screen space
        float2 sampleCoord = samplePos.xy / samplePos.z;
        sampleCoord = (sampleCoord + 1.0) * 0.5;

        if (sampleCoord.x >= 0.0 && sampleCoord.x <= 1.0 &&
            sampleCoord.y >= 0.0 && sampleCoord.y <= 1.0)
        {
            float sampleDepth = tex2D(depthTexture, sampleCoord).r;
            float3 sampleViewPos = GetViewPos(sampleCoord, sampleDepth);

            // Range check and angle-aware occlusion
            float diff = length(sampleViewPos - centerPos);
            float rangeCheck = smoothstep(radius, 0.0, diff);  // FIXED: inverted
            float nDotS = max(dot(normal, normalize(samplePos - centerPos)), 0.0);
            occlusion += rangeCheck * step(sampleViewPos.z, centerPos.z) * nDotS;
        }

        // Advance noise for next sample
        rand = tex2D(noiseTexture, float2(i * 0.1, 0.0)).rgb * 2.0 - 1.0;
    }

    occlusion = 1.0 - (occlusion / 16.0);
    occlusion = pow(occlusion, power);

    // --- Temporal reprojection ---
    float2 velocity = tex2D(velocityTexture, IN.texCoord).rg;
    // Decode velocity from [0,1] to [-1,1]
    velocity = velocity * 2.0 - 1.0;

    float2 prevUV = IN.texCoord - velocity;

    // Confidence: how likely the history sample is valid
    float confidence = 1.0;
    // Disocclusion: if previous UV is off-screen, zero out history
    if (prevUV.x < 0.0 || prevUV.x > 1.0 || prevUV.y < 0.0 || prevUV.y > 1.0)
    {
        confidence = 0.0;
    }
    else
    {
        // Also check depth consistency for disocclusion
        float prevDepth = tex2D(depthTexture, prevUV).r;
        float4 history = tex2D(historyTexture, prevUV);
        float historyOcclusion = history.r;

        // Compute expected depth from reprojection
        float2 prevVel = tex2D(velocityTexture, prevUV).rg * 2.0 - 1.0;
        float depthDiff = abs(centerDepth - prevDepth);
        confidence = 1.0 - smoothstep(0.001, 0.05, depthDiff);
        confidence *= history.a;  // multiply by history confidence

        float temporalBlend = ssaoParams.w;
        occlusion = lerp(occlusion, historyOcclusion, temporalBlend * confidence);
    }

    // Clamp output
    occlusion = saturate(occlusion);

    return float4(occlusion, confidence, 0.0, 1.0);
}
